package main

import (
	"context"
	"fmt"
	"io/ioutil"
	"log"
	"net/http"
	"os"
	"strconv"
	"time"
)

var ogm map[int]map[int]float64
var bot map[string]int // "ip-addr" -> int ID
var botCount int = 0

type measurement struct {
	receivedTime time.Time
	receivedFrom string // ip-addr
	localGridRow int
	localGridCol int
	occupancyVal float64
}

var sensorData chan *measurement

var isLocalized = false

func doLocalization(seconds int) bool {
	time.Sleep(time.Duration(seconds) * time.Second)
	isLocalized = true
	return true
}

// main server
func main() {
	// OGM setup
	log.Println("Occupancy Grid Mapping setup.")
	ogm = map[int]map[int]float64{}
	bot = map[string]int{}

	// http setup
	log.Println("Starting server.")
	// option to run port on a given input argument
	port := 42
	if len(os.Args) > 1 {
		port, _ = strconv.Atoi(os.Args[1])
	}
	log.Printf("  server running on port %v\n", port)
	// init http server
	server := &http.Server{Addr: ":" + strconv.Itoa(port)}

	// other shit
	// set up endpoint stop
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	// set up http path handlers
	http.HandleFunc("/end", func(w http.ResponseWriter, r *http.Request) {
		// nothing to do yet
		log.Println("Termination signal received...")
		cancel()
	})
	http.HandleFunc("/register", func(w http.ResponseWriter, r *http.Request) {
		log.Println("Registration signal received...")
		switch r.Method {
		case "POST":
			reqBody, err := ioutil.ReadAll(r.Body)
			if err != nil {
				log.Fatal(err)
			}
			fmt.Printf("%s\n", reqBody) // this was the request body
			// get request ip address
			if _, ok := bot[r.RemoteAddr]; !ok {
				// found a new address!
				bot[r.RemoteAddr] = botCount
				botCount++
				// TODO -- do we need to handle non-static ip distribution? probably :(
				w.Write([]byte("Noted.\n"))
			} else {
				w.Write([]byte("Already registered.\n"))
			}
		default:
			w.WriteHeader(http.StatusNotImplemented)
			w.Write([]byte(http.StatusText(http.StatusNotImplemented)))
		}
	})
	http.HandleFunc("/localize", func(w http.ResponseWriter, r *http.Request) {
		// send channel signal to start localization
		log.Println("Localization signal received...")
		if isLocalized {
			log.Println("  already localized.")
			w.Write([]byte("Already localized.\n"))
		} else {
			log.Println("   starting localization in 4 seconds.")
			go doLocalization(4)
			w.Write([]byte("Localization starting.\n"))
		}
	})

	// spawn http server thread
	go func() {
		// always returns error. ErrServerClosed on graceful close
		log.Println("listening...")
		if err := server.ListenAndServe(); err != http.ErrServerClosed {
			// unexpected error. port in use?
			log.Fatalf("ListenAndServe(): %v", err)
		}
	}()

	// wait for localization a-go
	for {

	}

	// spawn data processing thread
	go func() {
		// process jobs from datastore channel
		for m := range sensorData { // since this is a channel, it will iterate forever
			fmt.Println(m)
		}
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
