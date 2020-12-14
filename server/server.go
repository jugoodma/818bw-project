package main

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"io/ioutil"
	"log"
	"net/http"
	"os"
	"strconv"
	"time"
)

type point struct {
	x int
	y int
}

/*
bot local IP may be different than net/http request remoteAddr
	but we need the bot IP to send communications to the bot
	and we need the bot remoteAddr to know who sends _us_ what
		actualy, remoteAddr may change! so, we require the bot send us
		it's ID at every POST
*/
var ogm map[int]map[int]float64
var bot []string          // [int ID] -> "ip-addr"
var pos []point           // [(x,y)] ; index == botID
var remote map[string]int // "bot remote addr" -> int ID      TODO delete
var clocks []int64        // [int ID] -> millisecond start time offset
var n int = 2             // total number of bots

type key int

const (
	requestIDKey key = 0
)

// registration received json
//  data returned from bot POSTing to us
type regPostData struct {
	Clock int64  `json:"clock,omitempty"`
	IP    string `json:"ip,omitempty"`
}

// localization received json
//  data returned from bot POSTing to us
type locPostData struct {
	ID    int     `json:"id"`
	Start int64   `json:"start,omitempty"`
	End   int64   `json:"end,omitempty"`
	Left  []int64 `json:"left,omitempty"`
	Right []int64 `json:"right,omitempty"`
}

// movement received json
//  data returned from bot POSTing to us
type movPostData struct {
	ID    int     `json:"id"`
	Start float64 `json:"start,omitempty"`
	End   float64 `json:"end,omitempty"`
}

var loc chan *locPostData
var mov chan *movPostData

func makeTimestamp() int64 {
	return time.Now().UnixNano() / int64(time.Millisecond)
}

// server middleware for logging
func logging(logger *log.Logger) func(http.Handler) http.Handler {
	return func(next http.Handler) http.Handler {
		return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
			// inject request id?
			requestID := r.Header.Get("X-Request-Id")
			if requestID == "" {
				requestID = fmt.Sprintf("%d", time.Now().UnixNano())
			}
			w.Header().Set("X-Request-Id", requestID)
			// log request
			defer func() {
				logger.Printf("[%v %v] <%v> %v (%v)\n", r.Method, r.URL.Path, r.RemoteAddr, r.UserAgent(), requestID)
			}()
			next.ServeHTTP(w, r)
		})
	}
}

func doLocPost(data string, botID int) []byte {
	fmt.Println(data)
	reqBody := []byte(data)
	resp, err := http.Post("http://"+bot[botID]+"/loc", "application/text", bytes.NewBuffer(reqBody))
	if err != nil {
		fmt.Printf(" doLocPost response error -- %v\n", err)
	}
	defer resp.Body.Close()
	body, err := ioutil.ReadAll(resp.Body)
	if err != nil {
		fmt.Printf(" doLocPost body-read error -- %v\n", err)
	}
	return body
}

type movCMD string

const (
	movForward  movCMD = "f"
	movBackward movCMD = "b"
	movRotate   movCMD = "r"
)

func doMovPost(c movCMD, l int, botID int) []byte {
	reqBody := []byte(fmt.Sprintf("%v,%d", c, l))
	resp, err := http.Post("http://"+bot[botID]+"/mov", "application/text", bytes.NewBuffer(reqBody))
	if err != nil {
		fmt.Printf(" doMovPost response error -- %v\n", err)
	}
	defer resp.Body.Close()
	body, err := ioutil.ReadAll(resp.Body)
	if err != nil {
		fmt.Printf(" doMovPost body-read error -- %v\n", err)
	}
	return body
}

// localization
func localize() {
	// assume num_bots >= 3
	// assume leader == 0 -- this is the bot we localize relative to
	// (1) localize 1 to 0
	// TODO -> parameterize
	var delayTime int64 = 2000
	dDelta := 35
	// tone := 300
	t := makeTimestamp()
	// post the listener first b/c they have more setup work to do
	doLocPost(fmt.Sprintf("l,750,%v", delayTime), 0)                         // s0 (l == listen)
	doLocPost(fmt.Sprintf("s,500,%v", delayTime-(makeTimestamp()-t)+100), 1) // s1 (s == speak)
	// wait for (1) localization data (block)
	// TODO do shit with this data
	lpd1 := <-loc
	lpd2 := <-loc
	doMovPost(movForward, dDelta, 0) // move 0 forward
	mpd := <-mov
	dDeltaTrue1 := mpd.End - mpd.Start // actual distance traveled
	time.Sleep(time.Second * 3)
	// repeat
	t = makeTimestamp()
	doLocPost(fmt.Sprintf("l,750,%v", delayTime), 0)                         // s0 (l == listen)
	doLocPost(fmt.Sprintf("s,500,%v", delayTime-(makeTimestamp()-t)+100), 1) // s1 (s == speak)
	lpd3 := <-loc
	lpd4 := <-loc
	doMovPost(movBackward, dDelta, 0) // return 0 "home"
	mpd = <-mov
	dDeltaTrue2 := mpd.End - mpd.Start // actual distance traveled
	// done localization
	fmt.Printf("L1=%v;\nR1=%v;\nL1=%v;\nR1=%v;\n\nL2=%v;\nR2=%v;\nL2=%v;\nR2=%v;\n\n%v\t%v\n", lpd1.Left, lpd1.Right, lpd2.Left, lpd2.Right, lpd3.Left, lpd3.Right, lpd4.Left, lpd4.Right, dDeltaTrue1, dDeltaTrue2)
}

// main server
func main() {
	// OGM setup
	log.Println("Occupancy Grid Mapping setup.")
	ogm = make(map[int]map[int]float64)
	bot = make([]string, 0) // num robots
	pos = make([]point, 0)  // num robots
	remote = make(map[string]int)
	clocks = make([]int64, 0)
	loc = make(chan *locPostData, n)
	mov = make(chan *movPostData, n)

	// http setup
	log.Println("Starting server.")
	// option to run port on a given input argument
	port := 42
	if len(os.Args) > 1 {
		port, _ = strconv.Atoi(os.Args[1])
	}
	log.Printf("  server will run on port %v\n", port)

	// create logger
	logger := log.New(os.Stdout, "[ROUTER] ", log.LstdFlags)
	// create router
	router := http.NewServeMux()
	//   set up endpoint stop
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	router.HandleFunc("/end", func(w http.ResponseWriter, r *http.Request) {
		// w.Header().Set("Content-Type", "application/json")
		// print whatever statistics
		w.Write([]byte("server closed."))
		cancel()
	})
	router.HandleFunc("/reg", func(w http.ResponseWriter, r *http.Request) {
		log.Println("Someone wants to register...")
		switch r.Method {
		case "POST":
			reqBodyBytes, err := ioutil.ReadAll(r.Body)
			if err != nil {
				log.Fatal(err)
			}
			reqBody := &regPostData{}
			err = json.Unmarshal(reqBodyBytes, reqBody)
			if err != nil {
				log.Fatal(err)
			}
			log.Printf("  %v\n", reqBody)
			// see if ip has registered already
			newID := -1
			for id, ip := range bot {
				if ip == reqBody.IP {
					// robot has registered already
					newID = id
				}
			}
			if newID == -1 {
				// new robot
				newID = len(bot)
				newIP := reqBody.IP
				log.Printf("  %v -> %v\n", newID, newIP)
				bot = append(bot, newIP)
				remote[r.RemoteAddr] = newID
				clocks = append(clocks, makeTimestamp()-reqBody.Clock) // move calculation up?
			}

			w.Write([]byte(strconv.Itoa(newID)))
		default:
			w.WriteHeader(http.StatusNotImplemented)
			w.Write([]byte(http.StatusText(http.StatusNotImplemented)))
		}
	})
	/*
		server will receive localization data from robots
		from speaker:
		- clock offset time when robot _started_ playing speaker

		from listener:
		- clock offset time when robot _started_ recording
		- integer array of amplitude samples from LEFT microphone
		- integer array of amplitude samples from RIGHT microphone

		then just ship these data into the localization channel (var loc chan *locPostData)
		which will be received by the localization thread
	*/
	router.HandleFunc("/loc", func(w http.ResponseWriter, r *http.Request) {
		// botID, ok := remote[r.RemoteAddr]
		// if !ok {
		// 	fmt.Printf("%v not found in remote map\n", r.RemoteAddr)
		// }
		switch r.Method {
		case "POST":
			reqBodyBytes, err := ioutil.ReadAll(r.Body)
			if err != nil {
				fmt.Println(err)
			}
			log.Println(string(reqBodyBytes))
			reqBody := &locPostData{}
			err = json.Unmarshal(reqBodyBytes, reqBody)
			if err != nil {
				fmt.Println(err)
			}
			loc <- reqBody
			w.Write([]byte(`thanks!`))
		default:
			w.WriteHeader(http.StatusNotImplemented)
			w.Write([]byte(http.StatusText(http.StatusNotImplemented)))
		}
	})
	router.HandleFunc("/mov", func(w http.ResponseWriter, r *http.Request) {
		switch r.Method {
		case "POST":
			reqBodyBytes, err := ioutil.ReadAll(r.Body)
			if err != nil {
				fmt.Println(err)
			}
			log.Println(string(reqBodyBytes))
			reqBody := &movPostData{}
			err = json.Unmarshal(reqBodyBytes, reqBody)
			if err != nil {
				fmt.Println(err)
			}
			mov <- reqBody
			w.Write([]byte(`thanks!`))
		default:
			w.WriteHeader(http.StatusNotImplemented)
			w.Write([]byte(http.StatusText(http.StatusNotImplemented)))
		}
	})
	router.HandleFunc("/debug", func(w http.ResponseWriter, r *http.Request) {
		reqBodyBytes, err := ioutil.ReadAll(r.Body)
		if err != nil {
			log.Println(err)
		} else {
			log.Println(string(reqBodyBytes))
		}
		w.Write([]byte(`thanks!`))
	})
	server := &http.Server{
		Addr:    ":" + strconv.Itoa(port),
		Handler: logging(logger)(router),
	}

	// spawn http server thread
	go func() {
		// always returns error. ErrServerClosed on graceful close
		log.Println("server listening...")
		if err := server.ListenAndServe(); err != http.ErrServerClosed {
			// unexpected error. port in use?
			log.Fatalf("ListenAndServe(): %v\n", err)
		}
	}()

	// spawn processor thread
	go func() {
		log.Println("calculation initializing...")
		// localization setup
		for {
			if len(bot) >= n {
				break
			}
		}
		time.Sleep(time.Second * 3)
		log.Printf("[DATA] %v bot(s) have registered -- starting localization procedure\n", len(bot))
		localize()
		log.Println("[DATA] localization completed.")
		// start planning and exploration

		// just end for now
		cancel()
	}()

	// block wait for shutdown
	select {
	case <-ctx.Done():
		// graceful shutdown
		if err := server.Shutdown(ctx); err != nil {
			// failure/timeout shutting down the server gracefully
			panic(err)
		}
	} // no default case needed

	log.Printf("server closed.")
}
