#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>

#define echoPin 5
#define trigPin 4
#define speakerPin 2

const char* ssid = "bot";
const char* password = "dankmemes";
ESP8266WebServer server(80);
int ID = -1;
unsigned long myTime;

void setup() {
  Serial.begin(9600);
  sound_setup();
  usonic_setup();
  // boolean sync = false;
  // char buf[1];
  // while(!sync){
  //   if(Serial.available()){
  //     Serial.readBytes(buf,1);
  //     if(buf[0] == 's'){
  //       Serial.write('a');
  //       Serial.flush();
  //       sync = true;
  //     }
  //   }
  //   delay(10);
  // }
  boolean sync = false;
  char buf[1];
  Serial.println("ATTEMPTING TO SYNC WITH NANO.");
  while (!sync) {
    Serial.write('s');
    delay(50);
    if (Serial.available()) {
      Serial.readBytes(buf, 1);
      if (buf[0] == 's') {
        sync = true;
      }
    }
  }
  Serial.println("\nSYNCED.");
  internet_setup();
  beep(100, 110);
  beep(100, 130);
}

void loop() {
  server.handleClient();
}

void sound_setup(){
  pinMode(speakerPin, OUTPUT);
}

void usonic_setup(){
  pinMode(echoPin, INPUT);
  pinMode(trigPin, OUTPUT);
}

float read_ult(int samples) {
  float tot = 0;
  for (int i = 0; i < samples; i++) {
    digitalWrite(trigPin, HIGH);
    delayMicroseconds(10);
    digitalWrite(trigPin, LOW);
    // Reads the echoPin, returns the sound wave travel time in microseconds
    tot += pulseIn(echoPin, HIGH);
  }
  return microsecondsToCentimeters(tot) / samples;
}

void beep(int ms, int hz) {
  analogWrite(speakerPin, hz);
  delay(ms);
  analogWrite(speakerPin, 0);
}

void internet_setup(){
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED)
    delay(500);
  while(ID < 0){
    pingServer();  
    delay(1000);
  }
  server.on("/", handleRoot);
  server.on("/loc", getLocData);
  server.on("/mov", getMovData);
  server.on("/ult", getUltData);
  server.onNotFound(handleNotFound);
  server.begin();
}

void getLocData(){
  String message = ""; message += millis();
  String body = server.arg("plain");
  server.send(200, "text/plain", message);
  int comma = body.indexOf(',');
  int postTime = body.substring(comma+1,body.indexOf(',', comma+1)).toInt();
  int postDelay = body.substring(body.indexOf(',', comma + 1)+1).toInt();  
  if(body.charAt(0) == 'l')
    listen_sig(postTime, postDelay);
  else if(body.charAt(0) == 's')
    speaker_sig(postTime, postDelay);
}

void getMovData(){
  String body = server.arg("plain");
  int comma = body.indexOf(',');
  int param = body.substring(comma+1).toInt();
  char sig = body.charAt(0);
  if (sig == 'f' || sig == 'b' || sig == 'r') {
    server.send(200, "text/plain", "Hello there! General Kenobi.\n");
    motor_sig(sig, param);
  } else {
    server.send(200, "text/plain", "invalid command\n");
  }
}

void getUltData() {
  int samples = server.arg("plain").toInt();
  server.send(200, "text/plain", String(read_ult(samples)));
}

void listen_sig(int postTime, int delayTime){
  byte outBuf[5];
  int half = 256;
  int full = half *2;
  int to_read = 16;
  byte inBuf[to_read*sizeof(short)];
  short samples[full];
  int ptr = 0;
  int count = 0;
  outBuf[0] = 'L';
  memcpy(outBuf+1, &postTime, 2);
  memcpy(outBuf+3, &delayTime, 2);
  Serial.write(outBuf,5);
  Serial.flush();
  delay((postTime+ delayTime)*2);
  while(ptr < full){
    if (Serial.available()>=to_read*sizeof(short)) {
      Serial.readBytes(inBuf, to_read*sizeof(short));
      for(int j = 0; j < to_read;j++)
        samples[j+ptr] = ((short *)inBuf)[j];
      ptr += to_read;
    } 
    delay(10);
  }
  String message = "{\"start\":";
  message += myTime;
  message += ",\"id\":";
  message += ID;
  message += ",\"left\":[";
  for(int i = 0; i < 255; i++){
    message+=samples[i];
    message+=",";
  }
  message+=samples[255];
  message += "],\"right\":[";
  for(int i = 0; i < 255; i++){
    message+=samples[i+256];
    message+=",";
  }
  message+=samples[511];
  message += "]}";
  sendLoc(message);
}

void speaker_sig(int postTime, int delayTime){
  delay(delayTime);
  analogWrite(speakerPin, 400);
  delay(postTime);
  analogWrite(speakerPin, 0);
  String message = "{\"id\":";
  message += ID;
  message +="}";
  sendLoc(message);
}

void motor_sig(char sig, int param) {
  beep(250, 110);
  // if rotate,  param == degrees
  // if fwd/bak, param == travel distance
  byte out_buf[5];
  if (sig == 'r') {
    memcpy(out_buf, &sig, 1);
    memcpy(out_buf+1, &param, 2);
    memcpy(out_buf+3, &param, 2);
    while (Serial.availableForWrite() < 5) {;}
    Serial.write(out_buf, 5);
    Serial.flush();
  } else {
    if (sig == 'b') {
      sig = 'f';
      param *= -1;
    }
    int max_computation = 10000;
    memcpy(out_buf, &sig, 1);
    memcpy(out_buf+1, &max_computation, 2);
    memcpy(out_buf+3, &max_computation, 2);
    while (Serial.availableForWrite() < 5) {;}
    Serial.write(out_buf, 5);
    Serial.flush();
    // todo, control code for rotation on nano?
    // int param == centimeters of wanted fwd/bak travel distance
    unsigned long start_time = millis();
    int max_motor_time = 1;
    float start_ult_dist = read_ult(10), curr_ult_dist, delta_distance = 0, c;
    sendDebug(String(start_ult_dist));
    // while within computation time
    // and have not traveled enough distance (fwd / bak)
    while (millis() - start_time < max_computation && delta_distance < param && sig != 'x') {
      // compute distance to-go
      curr_ult_dist = read_ult(3);
      sendDebug(String(curr_ult_dist));
      delta_distance = start_ult_dist - curr_ult_dist;
      // decide which motor action to take
      c = param - delta_distance;
      if (curr_ult_dist < 10) {
        sig = 'b'; // we're too close to a forward obstacle
      } else if (c > 0.5) {
        sig = 'f';
      } else if (c < -0.5) {
        sig = 'b';
      } else if (sig == 'p') {
        sig = 'x'; // stop!
      } else {
        sig = 'p';
      }
      // decide how long to keep motors on (max)
      max_motor_time = 50; // todo -- function of acceleration and distance-to-go
      // tell nano what to do
      memcpy(out_buf, &sig, 1);
      memcpy(out_buf+1, &max_motor_time, 2);
      memcpy(out_buf+3, &max_motor_time, 2);
      while (Serial.availableForWrite() < 5) {;}
      Serial.write(out_buf, 5);
      Serial.flush(); // wait until buffer is fully written
      // repeat
      // beep(max_motor_time, 110);
      delay(2*max_motor_time);
    }
    beep(100, 110);
    beep(200, 130);
  }
}

//void motor_sig(char sig, int param) {
//  // int param == centimeters of forward travel distance
//  byte outBuf[5];
//  float start_cm, prev_cm, curr_cm;
//  // unsigned long start_time;
//  memcpy(outBuf, &sig, 1);
//  memcpy(outBuf+1, &param, 2);
//  memcpy(outBuf+3, &param, 2);
//  start_cm = read_ult();
//  Serial.write(outBuf,5);
//  Serial.flush();
//
//  // start_time = millis();
//  prev_cm = start_cm;
//  curr_cm = -1;
//  // wait until we finish moving
//  delay(1000);
//  while (curr_cm > start_cm - 1 || prev_cm - curr_cm > 0.25) {
//    prev_cm = curr_cm;
//    curr_cm = read_ult();
//    // duration = millis();
//    analogWrite(speakerPin, 220);
//    delay(100);
//    analogWrite(speakerPin, 0);
//    delay(500);
//  }
//  // mitigate error
//  // how much distance do we still need to travel?
//  // how far did we go? X = start_cm - curr_cm
//  // how far to-go? Y = param - X
//  // what next distance to send the nano? f(wanted)->actual, so  wanted / actual * (new actual) -> new wanted
//  // param / X * Y
//  param = param*param / (start_cm - curr_cm) - param;
//  delay(10000);
//  memcpy(outBuf, &sig, 1);
//  memcpy(outBuf+1, &param, 2);
//  memcpy(outBuf+3, &param, 2);
//  Serial.write(outBuf,5);
//  Serial.flush();
//}

void pingServer(){
  HTTPClient http;
  if (http.begin("http://192.168.1.186:42/reg")) {
    myTime = millis();
    String message = "{\"clock\":";
    message += myTime;
    message += ",\"ip\":\"";
    message += WiFi.localIP().toString();
    message += "\"}";
    int httpCode = http.POST(message);
    if (httpCode > 0) {
      if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
        String payload = http.getString();
        ID = payload.toInt();
      }
    }
    http.end();
  }
}

void sendLoc(String message){
  HTTPClient http;
  if (http.begin("http://192.168.1.186:42/loc")){
    int httpCode = http.POST(message);
    http.end();
  }
}

void sendMov(String message){
  HTTPClient http;
  if (http.begin("http://192.168.1.186:42/mov")){
    int httpCode = http.POST(message);
    http.end();
  }
}

void sendDebug(String message) {
  HTTPClient http;
  if (http.begin("http://192.168.1.186:42/debug")){
    int httpCode = http.POST(message);
    http.end();
  }
}

void test_usonic(){
  Serial.print(read_ult(3));
  Serial.print("cm");
  Serial.println();
}

void handleRoot() {
  server.send(200, "text/plain", "hello from esp8266!");
}

void handleNotFound(){
  server.send(404, "text/plain", ":(");
}

// long microsecondsToInches(long microseconds) {
//   return microseconds / 74 / 2;
// }

float microsecondsToCentimeters(long microseconds) {
  return microseconds / 29.0 / 2.0;
}
