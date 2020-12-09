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
  boolean sync = false;
  char buf[1];
  while(!sync){
    if(Serial.available()){
      Serial.readBytes(buf,1);
      if(buf[0] == 's'){
        Serial.write('a');
        Serial.flush();
        sync = true;
      } 
    }
    delay(10);
  }
  internet_setup();
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
  String message = "Hello there! General Kenobi.";
  String body = server.arg("plain");
  server.send(200, "text/plain", message);
  int comma = body.indexOf(',');
  int param = body.substring(comma+1).toInt();
  char sig = body.charAt(0);
  if(sig == 'f'||sig=='b'||sig=='r')
    motor_sig(sig-32,param);
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

void motor_sig(char sig, int param){
  byte outBuf[5];
  long duration,cm;
  memcpy(outBuf, &sig, 1);
  memcpy(outBuf+1, &param,2);
  memcpy(outBuf+3, &param, 2);
  
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  // Reads the echoPin, returns the sound wave travel time in microseconds
  duration = pulseIn(echoPin, HIGH);
  cm = microsecondsToCentimeters(duration);
  Serial.write(outBuf,5);
  Serial.flush();
}
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

void test_usonic(){
  long duration, inches, cm;
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  // Reads the echoPin, returns the sound wave travel time in microseconds
  duration = pulseIn(echoPin, HIGH);
  inches = microsecondsToInches(duration);
  cm = microsecondsToCentimeters(duration);
  Serial.print(inches);
  Serial.print("in, ");
  Serial.print(cm);
  Serial.print("cm");
  Serial.println();
}

void handleRoot() {
  server.send(200, "text/plain", "hello from esp8266!");
}

void handleNotFound(){
  server.send(404, "text/plain", ":(");
}

long microsecondsToInches(long microseconds) {
   return microseconds / 74 / 2;
}

long microsecondsToCentimeters(long microseconds) {
   return microseconds / 29 / 2;
}
