package main

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"io/ioutil"
	"log"
	"math"
	"net/http"
	"os"
	"strconv"
	"strings"
	"time"
)

type point struct {
	x float64
	y float64
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

const (
	tone      int     = 300  // same as on ESP board
	micLRDist float64 = 10.1 // cm distance between the L and R mics
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
	ID      int    `json:"id"`
	Start   int64  `json:"start,omitempty"`
	Total   int64  `json:"total,omitempty"`
	Data    string `json:"data,omitempty"`
	left    []float64
	right   []float64
	sOffset int // index offset at which the speaker starts
}

func normalize(samples *[]float64) {
	// normalize values in-place
	// t = L1 - (max(L1)+min(L1))/2
	// t = t / max(abs(t))
	max := -1.0
	maxIdx := -1
	min := math.MaxFloat64
	minIdx := -1
	for i, v := range *samples {
		if v > max {
			max = v
			maxIdx = i
		}
		if v < min {
			min = v
			minIdx = i
		}
	}
	for i := 0; i < len(*samples); i++ {
		(*samples)[i] = (*samples)[i] - ((max + min) / 2)
	}
	maxAbs := math.Max(math.Abs((*samples)[maxIdx]), math.Abs((*samples)[minIdx]))
	for i := 0; i < len(*samples); i++ {
		(*samples)[i] = (*samples)[i] / maxAbs
	}
}

func (lpd *locPostData) formatSamples() {
	// Data == []byte == [b1, b2, b3, b4, ....]
	//  -> add b2b1 to Left
	//  -> add b4b3 to Right
	// TODO -- handle len(lpd.Data) % 4 != 0
	// for i := 0; i < len(lpd.Data); i += 4 {
	// 	if i+3 >= len(lpd.Data) {
	// 		fmt.Println(" DATA UNPACKING ERROR.")
	// 		break
	// 	}
	// 	lpd.left = append(lpd.left, int64(lpd.Data[i+1])<<8|int64(lpd.Data[i]))
	// 	lpd.right = append(lpd.right, int64(lpd.Data[i+3])<<8|int64(lpd.Data[i+2]))
	// }
	if lpd.Data == "" || len(lpd.left) > 0 || len(lpd.right) > 0 {
		lpd.Data = ""
		return
	}
	for i, v := range strings.Split(lpd.Data, ",") {
		value, err := strconv.ParseInt(v, 16, 64)
		if err != nil {
			fmt.Printf("Conversion failed: %s\n", err)
		}
		if i%2 == 0 {
			lpd.left = append(lpd.left, float64(value))
		} else {
			lpd.right = append(lpd.right, float64(value))
		}
	}
	// normalize left and right
	normalize(&lpd.left)
	normalize(&lpd.right)
}

// movement received json
//  data returned from bot POSTing to us
type movPostData struct {
	ID    int     `json:"id"`
	Start float64 `json:"start,omitempty"`
	End   float64 `json:"end,omitempty"`
	Rot   float64 `json:"rot,omitempty"`
}

// NOTING HERE -- rotation: + is left, - is right

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
	// fmt.Println(data)
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

// *** MAIN LOCALIZATION PROCEDURE ***

func listenAndSpeak(delayTime int64, botID int) (*locPostData, *locPostData, int64) {
	preTime := makeTimestamp()
	// post the listener first b/c they have more setup work to do
	res := doLocPost(fmt.Sprintf("l,500,%v", delayTime), 0) // s0 (l == listen)
	posTime := makeTimestamp()
	// TODO -- tune
	doLocPost(fmt.Sprintf("s,125,%v", delayTime+10), botID) // s1 (s == speak) // -((makeTimestamp()-t)+u-t1)
	s := strings.Split(string(res), ",")
	l1, _ := strconv.ParseInt(s[1], 10, 64)
	l0, _ := strconv.ParseInt(s[0], 10, 64)
	listenerSetupTime := l1 - l0
	// wait
	spd0 := <-loc // speaker more likely to post back first
	lpd0 := <-loc
	if lpd0.Data == "" { // potentially swap
		tmp := lpd0
		lpd0 = spd0
		spd0 = tmp
	}
	return spd0, lpd0, (posTime - preTime - listenerSetupTime) / 2 // avg wifi flight time
}

func xcorr(freq int, samples []float64, offset int) float64 {
	// generate "sent" tone
	// ts = (listenTime/1000)/len(samples)
	//  0:ts:speakTime -> speakTime-0/ts iterations
	//  speakTime*len(samples)*1000/listenTime
	//
	// from listenAndSpeak
	listenTime := 0.500
	speakTime := 0.125
	numSamples := float64(len(samples))
	up := speakTime * numSamples / listenTime // use same sampling rate -> same vector length
	tx := make([]float64, 0)
	var i float64 = 0
	for i <= up { // include upper bound
		tx = append(tx, math.Sin(2.0*math.Pi*float64(freq)*(i/up*listenTime)))
		i = i + 1
	}
	// fmt.Println(len(samples))
	// fmt.Println(tx)
	// calculate lags
	// starting at the provided offset index
	lags := make([]float64, 0)
	maxIdx := 0
	maxLag := 0.0
	for i := 0; i <= len(samples)-len(tx); i++ {
		lag := 0.0
		// correlation
		for j := 0; j < len(tx); j++ {
			lag = lag + (samples[i+j] * tx[j])
		}
		lags = append(lags, lag)
		if lag > maxLag {
			maxIdx = i
			maxLag = lag
		}
	}
	// fmt.Println(lags)
	// estimate distance
	c := 343.0                       // meters per second
	i = float64(maxIdx - offset + 1) // sample (need abs?)
	// fmt.Println(lags)
	// fmt.Println(maxLag)
	// print distance
	// fmt.Println(offset, maxIdx, i, i*listenTime*c*100/numSamples)
	return i * listenTime * c * 100 / numSamples // 100 cm/m
}

func euclDist(x0, y0, x1, y1 float64) float64 {
	return math.Sqrt(math.Pow(x1-x0, 2) + math.Pow(y1-y0, 2))
}

func midpoint(x0, y0, x1, y1 float64) point {
	return point{x: (x0 + x1) / 2, y: (y0 + y1) / 2}
}

func triangulate(L0, R0, L1, R1, x0, y0, x1, y1 float64) point {
	fmt.Println(L0, R0, L1, R1, x0, y0, x1, y1)
	/*
		bot 0:

		pos0    mL (x1,y1) mR

		              ^
		              |             bot i:
		                               (x?, y?)
		pos1    mL (x0,y0) mR

		we have:
			L0 == dist(mL0, bot i)
			R0 == dist(mR0, bot i)
			L1 == dist(mL1, bot i)
			R1 == dist(mR1, bot i)
		we calculate both points from overlapping circles (using law of cosines)
		 created from each bot0 position
		then pick the pair (pos0=(x,y), pos1=(x,y))
		 with the smallest euclidean distance
		 and return the midpoint of those as bot i's position
	*/
	//
	ix0 := 0.0
	iy0p := 0.0
	iy0n := 0.0
	if L0 > R0 {
		ix0 = ((math.Pow(R0, 2) - math.Pow(L0, 2) - math.Pow(micLRDist, 2)) / (-2 * L0 * micLRDist)) * L0
		iy0p = math.Sqrt(math.Pow(L0, 2) - math.Pow(ix0, 2))
		iy0n = -1 * iy0p
		ix0 = ix0 - micLRDist/2 + x0 // shift for bot center
	} else {
		ix0 = ((math.Pow(L0, 2) - math.Pow(R0, 2) - math.Pow(micLRDist, 2)) / (-2 * R0 * micLRDist)) * R0
		iy0p = math.Sqrt(math.Pow(R0, 2) - math.Pow(ix0, 2))
		iy0n = -1 * iy0p
		ix0 = ix0 + micLRDist/2 + x0
	}
	// shift for bot center
	iy0p += y0
	iy0n += y0
	//
	ix1 := 0.0
	iy1p := 0.0
	iy1n := 0.0
	if L1 > R1 {
		ix1 = ((math.Pow(R1, 2) - math.Pow(L1, 2) - math.Pow(micLRDist, 2)) / (-2 * L1 * micLRDist)) * L1
		iy1p = math.Sqrt(math.Pow(L1, 2) - math.Pow(ix1, 2))
		iy1n = -1 * iy1p
		ix1 = ix1 - micLRDist/2 + x1 // shift for bot center
	} else {
		ix1 = ((math.Pow(L1, 2) - math.Pow(R1, 2) - math.Pow(micLRDist, 2)) / (-2 * R1 * micLRDist)) * R1
		iy1p = math.Sqrt(math.Pow(R1, 2) - math.Pow(ix1, 2))
		iy1n = -1 * iy1p
		ix1 = ix1 + micLRDist/2 + x1
	}
	// shift for bot center
	iy1p += y1
	iy1n += y1
	//
	fmt.Println(ix0, iy0p)
	fmt.Println(ix0, iy0n)
	fmt.Println(ix1, iy1p)
	fmt.Println(ix1, iy1n)
	//
	// pairings: {(ix0, iy0p), (ix0, iy0n)} x {(ix1, iy1p), (ix1, iy1n)}
	ds := []float64{
		euclDist(ix0, iy0p, ix1, iy1p),
		euclDist(ix0, iy0p, ix1, iy1n),
		euclDist(ix0, iy0n, ix1, iy1p),
		euclDist(ix0, iy0n, ix1, iy1n),
	}
	fmt.Println(ds)
	minD := math.MaxFloat64
	minI := 4
	for i := 0; i < 4; i++ {
		if ds[i] < minD {
			minD = ds[i]
			minI = i
		}
	}
	switch minI {
	case 0:
		return midpoint(ix0, iy0p, ix1, iy1p)
	case 1:
		return midpoint(ix0, iy0p, ix1, iy1n)
	case 2:
		return midpoint(ix0, iy0n, ix1, iy1p)
	case 3:
		return midpoint(ix0, iy0n, ix1, iy1n)
	}
	fmt.Println("ERROR: could not localize :(")
	return point{}
}

func localize(numBots int) {
	// assume num_bots n >= 2
	// assume leader == 0 -- this is the bot we localize everyone relative to
	// LOCALIZE BOT i TO BOT 0
	var delayTime int64 = 500
	pos = nil
	// dDelta := 100
	for i := 0; i < n; i++ {
		pos = append(pos, point{0, 0})
	}
	// main loop
	for i := 1; i < numBots; i++ {
		//
		// (1) COLLECT AUDIO SAMPLES
		//
		dDelta := 100 // cm
		// bot i speaks to listener 0
		spd0, lpd0, _ := listenAndSpeak(delayTime, i) // wait
		// listener 0 moves forward
		doMovPost(movForward, dDelta, 0)
		// wait
		mpd0 := <-mov
		// update 0's position (assume no drift) TODO
		time.Sleep(time.Second * 5)
		// time.Sleep(time.Second * 1) // small pause
		// bot i speaks to listener 0
		spd1, lpd1, _ := listenAndSpeak(delayTime, i) // wait
		// listener 0 moves back
		doMovPost(movBackward, dDelta, 0) // TODO -- depend on mpd0
		mpd1 := <-mov
		//
		// (2) CROSS-CORRELATION WITH TONE
		//
		lpd0.formatSamples()
		lpd1.formatSamples()
		fmt.Printf("L1=%v;\nR1=%v;\n\nL2=%v;\nR2=%v;\n\n", lpd0.left, lpd0.right, lpd1.left, lpd1.right)
		// calculate offsets
		// server true start time:
		//  STs = lpdSPEAKER.Start + clocks[1]
		//  STm = lpdLISTENR.Start + clocks[0]
		// idx = (STs - STm)*(len(lpdi.left)/500) (average with right?)
		// lpdLISTENR.sOffset = idx
		// ((t1+t2)/2)
		lpd0.sOffset = int(((spd0.Start + clocks[i]) - (lpd0.Start + clocks[0])) * (int64(len(lpd0.left)) / lpd0.Total))
		lpd1.sOffset = int(((spd1.Start + clocks[i]) - (lpd1.Start + clocks[0])) * (int64(len(lpd1.left)) / lpd1.Total))
		dL0 := xcorr(tone, lpd0.left, lpd0.sOffset)
		dR0 := xcorr(tone, lpd0.right, lpd0.sOffset)
		dL1 := xcorr(tone, lpd1.left, lpd1.sOffset)
		dR1 := xcorr(tone, lpd1.right, lpd1.sOffset)
		// assume bot 0 does not drift left/right (x-pos)
		pos[1] = triangulate(dL0, dR0, dL1, dR1, pos[0].x, pos[0].y, pos[0].x+0, pos[0].y+(mpd0.Start-mpd0.End))
		// pos[0].x = // TODO
		pos[0].y += (mpd0.Start - mpd0.End) + (mpd1.Start - mpd1.End)
		// fmt.Println(clocks)
		// fmt.Printf("speaker index starts:\n %v\t%v\n", lpd0.sOffset, lpd1.sOffset)
		fmt.Printf("attempted to localize %v to leader\n positions: %v\n", i, pos)
	}
}

// *** MAIN EXPLORATION PROCEDURE ***

func explore(expTime float64) {
	start := time.Now()
	duration := time.Since(start)
	for duration.Seconds() < expTime {
		// repeat
		duration = time.Since(start)
	}
}

// *** MAIN SERVER ***

func main() {
	// OGM setup
	log.Println("Localization and Mapping setup.")
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
	// MAIN SERVER ENDPOINT HANDLERS
	router.HandleFunc("/end", func(w http.ResponseWriter, r *http.Request) {
		// w.Header().Set("Content-Type", "application/json")
		// print whatever statistics
		w.Write([]byte("server closed."))
		cancel()
	})
	router.HandleFunc("/reg", func(w http.ResponseWriter, r *http.Request) {
		t := makeTimestamp()
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
				clocks = append(clocks, t-reqBody.Clock) // move calculation up?
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
			// log.Println(string(reqBodyBytes))
			reqBody := &locPostData{}
			err = json.Unmarshal(reqBodyBytes, reqBody)
			if err != nil {
				fmt.Println(err)
			}
			// fmt.Println(reqBody.left)
			// fmt.Println(reqBody.right)
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
	router.HandleFunc("/localize", func(w http.ResponseWriter, r *http.Request) {
		// eg: POST "3" will localize the first three bots relative to 0
		reqBodyBytes, err := ioutil.ReadAll(r.Body)
		numBots, err := strconv.Atoi(string(reqBodyBytes))
		if err != nil || numBots < 2 || numBots > n || numBots > len(bot) {
			w.Write([]byte("invalid bot!\n"))
		} else {
			go localize(numBots)
			w.Write([]byte("performing localization.\n"))
		}
	})
	router.HandleFunc("/explore", func(w http.ResponseWriter, r *http.Request) {
		// eg: POST "3" will explore for 3 seconds
		reqBodyBytes, err := ioutil.ReadAll(r.Body)
		expTime, err := strconv.ParseFloat(string(reqBodyBytes), 64)
		if err != nil || expTime < 0 { // or not localized...
			w.Write([]byte("invalid exploration time!\n"))
		} else {
			go explore(expTime)
			w.Write([]byte("explorin'\n"))
		}
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

	// block wait for shutdown
	select {
	case <-ctx.Done():
		// graceful shutdown
		if err := server.Shutdown(ctx); err != nil {
			// failure/timeout shutting down the server gracefully
			log.Println(err)
		}
	} // no default case needed

	log.Printf("server closed.")
}
