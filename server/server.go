package main

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"io/ioutil"
	"log"
	"math"
	"math/cmplx"
	"math/rand"
	"net/http"
	"os"
	"strconv"
	"strings"
	"time"

	"github.com/mjibson/go-dsp/dsputils"

	"github.com/mjibson/go-dsp/fft"
)

type pose struct {
	x float64
	y float64
	r float64
}

type cell struct {
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
var (
	ogm       map[cell]float64
	bot       []string           // [int ID] -> "ip-addr"
	pos       []pose             // [(x,y,r)] ; index == botID
	remote    map[string]int     // "bot remote addr" -> int ID      TODO delete
	clocks    []int64            // [int ID] -> millisecond start time offset
	n         int            = 2 // total number of bots
	localized bool           = false
)

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
	Mov   string  `json:"mov"`
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

func doUltPost(botID int) float64 {
	reqBody := []byte("5")
	resp, err := http.Post("http://"+bot[botID]+"/ult", "application/text", bytes.NewBuffer(reqBody))
	if err != nil {
		fmt.Printf(" doUltPost response error -- %v\n", err)
	}
	defer resp.Body.Close()
	body, err := ioutil.ReadAll(resp.Body)
	if err != nil {
		fmt.Printf(" doUltPost body-read error -- %v\n", err)
	}
	f, err := strconv.ParseFloat(string(body), 64)
	// TODO handle error
	return f
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

func findTransformLength(m int) int {
	m = 2 * m
	for {
		r := m
		for _, p := range []int{2, 3, 5, 7} {
			for r > 1 && r%p == 0 {
				r = r / p
			}
		}
		if r == 1 {
			break
		}
		m = m + 1
	}
	return m
}

func xcorr(freq int, samples []float64, offset int) float64 {
	/*
		matlab:
		% Transform both vectors
		X = fft(x,2^nextpow2(2*M-1));
		Y = fft(y,2^nextpow2(2*M-1));

		% Compute cross-correlation
		c = ifft(X.*conj(Y));
	*/
	//
	// TODO -- this does not work properly
	//
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
	//
	nR := 0.0
	for i := 0; i < len(samples); i++ {
		nR = nR + math.Pow(math.Abs(samples[i]), 2)
	}
	nR = math.Sqrt(nR)
	nT := 0.0
	for i := 0; i < len(tx); i++ {
		nT = nT + math.Pow(math.Abs(tx[i]), 2)
	}
	nT = math.Sqrt(nT)
	//
	// corr(a, b) = ifft(fft(a_and_zeros) * conj(fft(b_and_zeros))) / norm(a)*norm(b)
	size := dsputils.NextPowerOf2(len(samples) + len(tx) - 1)
	a := fft.FFT(dsputils.ZeroPad(dsputils.ToComplex(samples), size))
	b := fft.FFT(dsputils.ZeroPad(dsputils.ToComplex(tx), size))
	m := make([]complex128, 0)
	for i := 0; i < size; i++ {
		m = append(m, a[i]*cmplx.Conj(b[i]))
	}
	corr := fft.IFFT(m)
	// matlab:
	// % Keep only the lags we want and move negative lags before positive
	// % lags.
	// c = [c1(m2 - mxl + (1:mxl)); c1(1:mxl+1)];
	scaled := make([]float64, 0)
	// maxLag == numSamples
	k := findTransformLength(len(samples))
	// fmt.Println(k)
	for i := 0; i < len(samples); i++ {
		scaled = append(scaled, real(corr[k-len(samples)-1+i])/(nR*nT))
	}
	for i := 0; i < len(samples); i++ {
		scaled = append(scaled, real(corr[i])/(nR*nT))
	}
	maxIdx := 0
	for i := range scaled {
		if math.Abs(scaled[i]) > math.Abs(scaled[maxIdx]) {
			maxIdx = i
		}
	}
	maxIdx = maxIdx - len(samples) // shift back
	// fmt.Println(corr)
	// fmt.Println(scaled)
	// estimate distance
	c := 343.0                       // meters per second
	i = float64(maxIdx - offset + 1) // sample (need abs?)
	// fmt.Println(lags)
	// fmt.Println(maxLag)
	// print distance
	fmt.Println(offset, maxIdx, i, i*listenTime*c*100/numSamples)
	return i * listenTime * c * 100 / numSamples // 100 cm/m
}

func euclDist(x0, y0, x1, y1 float64) float64 {
	return math.Sqrt(math.Pow(x1-x0, 2) + math.Pow(y1-y0, 2))
}

func midpoint(x0, y0, x1, y1 float64) pose {
	return pose{x: (x0 + x1) / 2, y: (y0 + y1) / 2, r: 0}
}

func quadlaterate(L0, R0, L1, R1, x0, y0, x1, y1 float64) pose {
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
	return pose{}
}

func localize(numBots int) {
	// assume num_bots n >= 2
	// assume leader == 0 -- this is the bot we localize everyone relative to
	// LOCALIZE BOT i TO BOT 0
	var delayTime int64 = 500
	pos = nil
	// dDelta := 100
	for i := 0; i < n; i++ {
		pos = append(pos, pose{0, 0, 0}) // todo, calculate rotation
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
		time.Sleep(time.Second * 1) // small pause
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
		pos[1] = quadlaterate(dL0, dR0, dL1, dR1, pos[0].x, pos[0].y, pos[0].x+0, pos[0].y+(mpd0.Start-mpd0.End))
		// pos[0].x = // TODO
		pos[0].y += (mpd0.Start - mpd0.End) + (mpd1.Start - mpd1.End)
		// fmt.Println(clocks)
		// fmt.Printf("speaker index starts:\n %v\t%v\n", lpd0.sOffset, lpd1.sOffset)
		fmt.Printf("attempted to localize %v to leader\n positions: %v\n", i, pos)
	}
}

// *** MAIN EXPLORATION PROCEDURE ***

var (
	paths     [][]cell          // [botID] -> the path (list) to take
	botjobs   map[int]bool      // ids to bool
	botids    map[cell]int      // cords to id
	xscale    float64      = 10 // centimeters per cell
	yscale    float64      = 10 // centimeters per cell
	occThresh float64      = 2
	odds      float64      = 0.7 // probability that occ(i,j)=1
)

func binPose(p pose) cell {
	return cell{x: int(p.x / xscale), y: int(p.y / yscale)}
}

func generatePath(start cell, hashList map[cell]cell, origin cell) []cell { // hashList : child -> parent
	list := make([]cell, 0)
	curr := start
	next := hashList[curr]
	for curr != origin {
		// list = append([]cell{key}, list...) // it's a forward trajectory
		list = append(list, curr)
		curr = next
		next = hashList[next]
	}
	return list // head == bot
}

type bfsQNode struct {
	node     cell
	cameFrom cell
}

func expand(path []cell) [][]cell {
	neighbors := [][]cell{}
	for a := -1; a < 2; a++ {
		for b := -1; b < 2; b++ {
			if a != 0 || b != 0 {
				neighbors = append(neighbors, append([]cell{cell{x: path[0].x + a, y: path[0].y + b}}, path...))
			}
		}
	}
	return neighbors
}

func bfs(origin cell, botID int) {
	fmt.Printf("BFS(%v) -> %v (%v)\n", origin, binPose(pos[botID]), pos[botID])
	Q := make([][]cell, 0)
	visited := make(map[cell]bool) // cell positions we have expanded already
	botCell := binPose(pos[botID])
	Q = append(Q, []cell{origin}) // spark
	for len(Q) > 0 {
		path := Q[0]         // obtain top of queue
		node := path[0]      // get node on path
		Q = Q[1:]            // behead
		if botCell == node { // is this the goal?
			// found bot, update trajectory
			fmt.Println("found bot!")
			paths[botID] = path
			fmt.Printf("%v -> %v -> %v\n", botCell, paths[botID], origin)
			break
		} else if !visited[node] && math.Abs(ogm[node]) < occThresh {
			visited[node] = true
			Q = append(Q, expand(path)...)
		}
	}
}

func calculateRotation(botID int) int {
	fmt.Println("calculating rotation.")
	rot := 0
	botCell := binPose(pos[botID])
	xdelta := botCell.x - paths[botID][0].x
	ydelta := botCell.y - paths[botID][0].y
	// make a box of of where each neighbor is
	// 1 2 3
	// 8   4
	// 7 6 5
	// left == +deg, right == -deg
	if xdelta == 1 && ydelta == -1 {
		rot = 45
	}
	if xdelta == 0 && ydelta == -1 {
		rot = 0
	}
	if xdelta == -1 && ydelta == -1 {
		rot = 315
	}
	if xdelta == -1 && ydelta == 0 {
		rot = 270
	}
	if xdelta == -1 && ydelta == 1 {
		rot = 225
	}
	if xdelta == 0 && ydelta == 1 {
		rot = 180
	}
	if xdelta == 1 && ydelta == 1 {
		rot = 135
	}
	if xdelta == 1 && ydelta == 0 {
		rot = 90
	}
	fmt.Printf("%v\t%v\t%v\n", rot, botCell, paths[botID][0])
	return (rot - int(pos[botID].r)) // calculate offset of rotation
}

// greatest common divisor (GCD) via Euclidean algorithm
func gcd(a, b int) int {
	for b != 0 {
		t := b
		b = a % b
		a = t
	}
	return a
}

// find Least Common Multiple (LCM) via GCD
func lcm(a, b int, integers ...int) int {
	if a == 0 || b == 0 {
		return 1
	}
	result := a * b / gcd(a, b)
	for i := 0; i < len(integers); i++ {
		result = lcm(result, integers[i])
	}
	return result
}

func policy(mpd *movPostData) {
	// update current pose
	pos[mpd.ID].r += mpd.Rot
	d := math.Abs(mpd.Start - mpd.End)
	pos[mpd.ID].x += d * math.Sin(pos[mpd.ID].r)
	pos[mpd.ID].y += d * math.Cos(pos[mpd.ID].r)
	if mpd.Mov == "r" {
		// update current rotation
		pos[mpd.ID].r = pos[mpd.ID].r + mpd.Rot
		// tell bot to move forward
		dist := math.Sqrt(math.Pow(pos[mpd.ID].x-xscale*float64(paths[mpd.ID][0].x), 2) + math.Pow(pos[mpd.ID].y-yscale*float64(paths[mpd.ID][0].y), 2))
		// move forward
		doMovPost(movForward, int(dist), mpd.ID)
		// remove from trajectory
		paths[mpd.ID] = paths[mpd.ID][1:]
	} else {
		// take measurement
		d = doUltPost(mpd.ID)
		// upate OGM based on current pose
		fmt.Println("updating OGM.")
		c := binPose(pose{x: d*math.Sin(pos[mpd.ID].r) + pos[mpd.ID].x, y: d*math.Cos(pos[mpd.ID].r) + pos[mpd.ID].y})
		// update all OGM values along path (use (1-odds)/odds for occupancy evidence of zero!)
		// 1 / lcm( ending grid position - starting grid position )
		b := binPose(pos[mpd.ID])
		deltaOGM := 1 / math.Abs(float64(lcm(c.x-b.x, c.y-b.y)))
		fmt.Printf(" %v\n", deltaOGM)
		seen := map[cell]bool{b: true, c: true}
		for t := 0.0; t < 1; t += deltaOGM {
			a := binPose(pose{x: float64(b.x) + float64(c.x-b.x)*t, y: float64(b.y) + float64(c.y-b.y)*t})
			if !seen[a] {
				seen[a] = true
				// not occupied ogm update rule:
				ogm[a] += math.Log((1 - odds) / odds)
			}
		}
		// occupied ogm update rule:
		ogm[c] += math.Log(odds / (1 - odds))
		// new point?
		if len(paths[mpd.ID]) < 2 {
			fmt.Println("choosing new trajectory.")
			paths[mpd.ID] = nil
			// select new point (POLICY -- we're doing some random/greedy policy here)
			// -- pick K random cell points
			// -- do BFS from point K_i to bot -> yield trajectory
			// -- select shortest trajectory that minimizes sum(abs(occupancy)) value
			//   -- sort by total occ
			//   -- of best L < K, choose shortest path
			// -> output goal point
			// TODO
			randCells := make([]cell, 0)
			K := 10
			// b := binPose(pos[mpd.ID])
			for i := 0; i < K; i++ {
				xDelta := rand.Intn(2) + 1
				yDelta := rand.Intn(2) + 1
				if rand.Intn(2)%2 == 0 {
					xDelta *= -1
				}
				if rand.Intn(2)%2 == 0 {
					yDelta *= -1
				}
				randCells = append(randCells, cell{b.x + xDelta, b.y + yDelta})
			}
			minIdx := 0
			for i := 0; i < len(randCells); i++ {
				if math.Abs(ogm[randCells[i]]) < math.Abs(ogm[randCells[minIdx]]) {
					minIdx = i
				}
			}
			// calculate new trajectory
			bfs(randCells[minIdx], mpd.ID) // void, will update botID's path
		} // else, continue on same trajectory
		// then tell bot to rotate
		fmt.Println("sending rotation->move command.")
		doMovPost(movRotate, calculateRotation(mpd.ID), mpd.ID)
	}
}

func explore(expTime float64) {
	if !localized {
		// assume the bots are localized:
		// and are pointing forward
		pos = append(pos, pose{0, 0, 0}, pose{127, 0, 0}, pose{0, 127, 0})
		localized = true
	}
	ticker := time.NewTicker(1 * time.Second)
	done := make(chan bool)
	go func() {
		// spark 'em
		mov <- &movPostData{ID: 0, Mov: "m"}
		mov <- &movPostData{ID: 1, Mov: "m"}
		// mov <- &movPostData{ID: 2, Mov: "m"}
		for {
			select {
			case <-done:
				return
			case mpd := <-mov:
				go policy(mpd)
			case <-ticker.C:
				fmt.Printf(".")
			}
		}
	}()
	time.Sleep(time.Duration(expTime) * time.Second)
	done <- true
	printOGM()
}

func printOGM() {
	x := []int{0}
	y := []int{0}
	z := []float64{0}
	for k, e := range ogm {
		x = append(x, k.x)
		y = append(y, k.y)
		z = append(z, e)
	}
	fmt.Printf("\nx=%v;\ny=%v;\nz=%v;\n\n", x, y, z)
	// print as probability matrix
	minX := 0
	maxX := 0
	for _, v := range x {
		if v < minX {
			minX = v
		}
		if v > maxX {
			maxX = v
		}
	}
	minY := 0
	maxY := 0
	for _, v := range y {
		if v < minY {
			minY = v
		}
		if v > maxY {
			maxY = v
		}
	}
	p := "p=[ "
	for i := minX; i <= maxX; i++ {
		for j := minY; j <= maxY; j++ {
			p += fmt.Sprintf("%v ", ogm[cell{x: i, y: j}])
		}
		p += "; "
	}
	p = p[:len(p)-2]
	p += "];"
	fmt.Println(p)
}

// *** MAIN SERVER ***

func main() {
	// OGM setup
	log.Println("Localization and Mapping setup.")
	ogm = make(map[cell]float64)
	bot = make([]string, 0) // num robots
	pos = make([]pose, 0)   // num robots
	remote = make(map[string]int)
	clocks = make([]int64, 0)
	loc = make(chan *locPostData, n)
	mov = make(chan *movPostData, n)

	paths = make([][]cell, 0)
	botjobs = make(map[int]bool)
	botids = make(map[cell]int)

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
				paths = append(paths, []cell{})
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
	// TODO -- send robot trajectory, then robot gives us log of what happened
	// router.HandleFunc("/path", func(w http.ResponseWriter, r *http.Request) {
	// 	switch r.Method {
	// 	case "POST":
	// 		reqBodyBytes, err := ioutil.ReadAll(r.Body)
	// 		if err != nil {
	// 			fmt.Println(err)
	// 		}
	// 		reqBody := &pathPostData{}
	// 		err = json.Unmarshal(reqBodyBytes, reqBody)
	// 		if err != nil {
	// 			fmt.Println(err)
	// 		}
	// 		ogm <- reqBody
	// 		idx = ogm.id
	// 		status = ogm.status
	// 		handlePathPost(idx, status)
	// 	default:
	// 		w.WriteHeader(http.StatusNotImplemented)
	// 		w.Write([]byte(http.StatusText(http.StatusNotImplemented)))
	// 	}
	// })
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
