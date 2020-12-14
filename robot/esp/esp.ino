#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>

#define echoPin 5
#define trigPin 4
#define speakerPin 2

#define MAX_LR_MIC_SAMPLES 2048

// global variables

const char* ssid = "bot";
const char* password = "dankmemes";
String server_addr = "http://192.168.1.186:42";
ESP8266WebServer server(80);
int ID = -1;
unsigned long my_time;
unsigned short samples[2 * MAX_LR_MIC_SAMPLES];
//unsigned byte samples[2*2*MAX_LR_MIC_SAMPLES];

// main stuff

void setup() {
  delay(1000);
  Serial.begin(115200); // 38400
  sound_setup();
  usonic_setup();
  if (sync_board(10000)) {
    beep(100, 440);
    delay(100);
  } else {
    beep(1000, 55);
    delay(1000);
  }
  internet_setup();
  ready_melody();
}

void loop() {
  server.handleClient();
}

// utility functions

void beep(int ms, int hz) {
  // CAREFUL, tone will interfere with PWM output on pins 3 and 11
  // in our schematic, should be ok?
  tone(speakerPin, hz, ms);
}

float mcs_to_cm(long microseconds) {
  return microseconds / 29.0 / 2.0;
}

// internet responders

void do_post(String endpoint, String message) {
  HTTPClient http;
  if (http.begin(server_addr+endpoint)){
    int httpCode = http.POST(message);
    http.end();
  }
}

void ping_server() {
  HTTPClient http;
  if (http.begin(server_addr+"/reg")) {
    my_time = millis();
    String message = "{\"clock\":";
    message += my_time;
    message += ",\"ip\":\"";
    message += WiFi.localIP().toString();
    message += "\"}";
    int httpCode = http.POST(message);
    // server never returns bad http code though so...
    if (httpCode > 0) {
      if (httpCode == HTTP_CODE_OK) { // httpCode == HTTP_CODE_MOVED_PERMANENTLY
        String payload = http.getString();
        ID = payload.toInt();
      } else {
        beep(100, 59);
      }
    } else {
      beep(100, 69);
    }
    http.end();
  }
}

void send_loc(String message) {
  do_post("/loc", message);
}

void send_mov(String message) {
  do_post("/mov", message);
}

void send_debug(String message) {
  do_post("/debug", message);
}

// setup functions

void ready_melody() {
  beep(125, 293);
  delay(125);
  beep(125, 329);
  delay(125);
  beep(125, 370);
  delay(125);
  beep(125, 440);
}

void sound_setup(){
  pinMode(speakerPin, OUTPUT);
  //
  beep(100, 110);
  delay(150);
  beep(50, 110);
  delay(100);
  beep(50, 110);
  delay(500);
}

void usonic_setup(){
  pinMode(echoPin, INPUT);
  pinMode(trigPin, OUTPUT);
  //
  beep(100, 220);
  delay(500);
}

boolean sync_board(int max_ms) {
  unsigned long start_time = millis();
  char buf[1];
  short count = 0;
  while (millis() - start_time < max_ms && count < 8) {
    Serial.write("ssssssss");
    while (Serial.available()) {
      Serial.readBytes(buf, 1);
      if (buf[0] == 's') {
        count += 1;
      }
    }
    delay(50);
  }
  return count >= 4;
}

void internet_setup(){
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    beep(100, 49);
    delay(1000);
  }
  beep(250, 270);
  while(ID < 0){
    ping_server();  
    delay(1000);
  }
  server.on("/", handle_root); // need?
  server.on("/loc", get_loc_data);
  server.on("/mov", get_mov_data);
  server.on("/ult", get_ult_data);
  server.on("/bep", get_bep_data);
  server.onNotFound(handle_not_found); // need?
  server.begin();
  beep(100, 300);
  delay(200);
  beep(100, 300);
  delay(500);
//  send_debug(String(sizeof(byte))); // 1
//  send_debug(String(sizeof(short))); // 2
//  send_debug(String(sizeof(int))); // 4 (on esp)
//  send_debug(String(sizeof(long))); // 4
//  send_debug(String(sizeof(unsigned short)));
//  send_debug(String(sizeof(unsigned int)));
//  send_debug(String(sizeof(unsigned long)));
}

// sensor interaction functions

float read_ult(int samples) {
  float tot = 0;
  for (int i = 0; i < samples; i++) {
    digitalWrite(trigPin, HIGH);
    delayMicroseconds(10);
    digitalWrite(trigPin, LOW);
    // Reads the echoPin, returns the sound wave travel time in microseconds
    tot += pulseIn(echoPin, HIGH);
  }
  return mcs_to_cm(tot) / samples;
}

// void test_usonic() {
//   Serial.print(read_ult(3));
//   Serial.print("cm");
//   Serial.println();
// }

// internet handlers

void handle_root() {
  server.send(200, "text/plain", "hello from esp8266!\n");
}

void handle_not_found(){
  server.send(404, "text/plain", ":(\n");
}

void get_loc_data(){
//  String message = ""; message += millis();
//  server.send(200, "text/plain", message);
  String body = server.arg("plain");
  int comma = body.indexOf(',');
  unsigned short post_time = (unsigned short) body.substring(comma+1,body.indexOf(',', comma+1)).toInt();
  unsigned short post_delay = (unsigned short) body.substring(body.indexOf(',', comma+1)+1).toInt();
  if (body.charAt(0) == 'l') {
    listen_sig(post_time, post_delay);
  } else if (body.charAt(0) == 's') {
    speaker_sig(post_time, post_delay);
  } else {
    server.send(200, "text/plain", "invalid command\n");
  }
}

void get_mov_data(){
  String body = server.arg("plain");
  unsigned short param = (unsigned short) body.substring(body.indexOf(',')+1).toInt();
  char sig = body.charAt(0);
  if (sig == 'f' || sig == 'b' || sig == 'r') {
    server.send(200, "text/plain", "Hello there! General Kenobi.\n");
    motor_sig(sig, param);
  } else {
    server.send(200, "text/plain", "invalid command\n");
  }
}

void get_ult_data() {
  int samples = server.arg("plain").toInt();
  server.send(200, "text/plain", String(read_ult(samples)));
}

void get_bep_data() {
  // just do a beep routine
  int hz = server.arg("plain").toInt();
  beep(100, hz);
  server.send(200, "text/plain", "bepis\n");
}

// controls / sensor interfaces

// ESP STORES DATA IN REVERSE BYTE ORDER
// SO DOES ARDUINO? (assume yes)
// this is "little endian" ness

void listen_sig(unsigned short post_time, unsigned short delay_time) {
  byte buf[5] = {0};
  int ptr = 0;
  int count = 0;
  boolean flag = false;
  unsigned long total_time;
  buf[0] = 'L';
  memcpy(buf+1, (byte *) &post_time,  sizeof(short));
  memcpy(buf+3, (byte *) &delay_time, sizeof(short));
  Serial.write(buf, 5);
//  Serial.flush();
  ((unsigned long *) buf)[0] = 0xffffffff;
  while (((unsigned short *) buf)[0] | ((unsigned short *) buf)[1]) { // TODO -- timeout error?
    delay(1);
    if (Serial.available() >= 4) {
      Serial.readBytes(buf, 4);
    }
  }
  server.send(200, "text/plain", String(millis())); // critical for timing
//  delay(delay_time);
  unsigned long st = millis();
//  while (ptr < 2*2*MAX_LR_MIC_SAMPLES && !flag && millis() - st < 2000) {
//    while (Serial.available() > 0 && millis() - st < 2000) {
//      samples[ptr] = Serial.read();
//      flag = samples[ptr] & samples[ptr+MAX_LR_MIC_SAMPLES] == 0xffff;
//      if (flag) break;
//      if (ptr >= MAX_LR_MIC_SAMPLES) break;
//      delay(1);
//    }
//    delay(1);
//  }
  while (ptr < MAX_LR_MIC_SAMPLES && !flag && millis() - st < 2000) {
    while (Serial.available() > 0 && millis() - st < 2000) {
      // process incoming byte
      switch (count) {
        case 0:
          ((byte *)(samples+ptr))[0] = Serial.read();
          count++;
          break;
        case 1:
          ((byte *)(samples+ptr))[1] = Serial.read();
          count++;
          break;
        case 2:
          ((byte *)(samples+ptr+MAX_LR_MIC_SAMPLES))[0] = Serial.read();
          count++;
          break;
        case 3:
          ((byte *)(samples+ptr+MAX_LR_MIC_SAMPLES))[1] = Serial.read();
          count = 0;
          ptr++;
          break;
        default: // should not happen
          count = 0;
          ptr++;
          break;
      }
      flag = samples[ptr] & samples[ptr+MAX_LR_MIC_SAMPLES] == 0xffff;
      if (flag) break;
      if (ptr >= MAX_LR_MIC_SAMPLES) break;
//      delay(1);
    }
//    if (Serial.available() >= 4) {
//      Serial.readBytes(buf, 4);
//      samples[ptr] = ((unsigned short *) buf)[0];
//      samples[ptr+MAX_LR_MIC_SAMPLES] = ((unsigned short *) buf)[1];
//      flag = samples[ptr] & samples[ptr+MAX_LR_MIC_SAMPLES] == 0xffff; // we done?
//      ptr++;
//    }
//    delay(1);
  }
  send_debug(String(ptr));
//  Serial.readBytes((byte *) &total_time, sizeof(long));
  String message = "{\"start\":0,\"end\":";
  message += total_time;
  message += ",\"id\":";
  message += ID;
  message += ",\"left\":[";
  for (int i = 0; i < ptr-1; i++) {
    message += samples[i];
    message += ",";
    delay(1);
  }
  message += samples[ptr-1];
  message += "],\"right\":[";
  for (int i = 0; i < ptr-1; i++) {
    message += samples[i+MAX_LR_MIC_SAMPLES];
    message += ",";
    delay(1);
  }
  message += samples[ptr-1+MAX_LR_MIC_SAMPLES];
  message += "]}";
  send_loc(message);
}

void speaker_sig(unsigned short post_time, unsigned short delay_time) {
  server.send(200, "text/plain", String(millis())); // critical for timing
  delay(delay_time);
  beep(post_time, 300);
  delay(post_time);
  String message = "{\"id\":";
  message += ID;
  message += "}";
  send_loc(message);
}

void motor_sig(char sig, short param) {
  // if rotate,  param == degrees
  // if fwd/bak, param == travel distance
  byte out_buf[5];
  if (sig == 'r') {
    memcpy(out_buf, &sig, 1);
    memcpy(out_buf+1, &param, sizeof(short));
    memcpy(out_buf+3, &param, sizeof(short));
    while (Serial.availableForWrite() < 5) {;}
    Serial.write(out_buf, 5);
    Serial.flush();
  } else {
    if (sig == 'b') {
      param *= -1;
    }
    boolean flag = true;
    unsigned short max_computation = 10000;
    out_buf[0] = 'f';
//    memcpy(out_buf, &sig, 1);
    memcpy(out_buf+1, &max_computation, sizeof(short));
    memcpy(out_buf+3, &max_computation, sizeof(short));
    while (Serial.availableForWrite() < 5) {;}
    Serial.write(out_buf, 5);
    Serial.flush();
    // todo, control code for rotation on nano?
    // int param == centimeters of wanted fwd/bak travel distance
    unsigned long start_time = millis();
    unsigned short max_motor_time = 1;
    int pause_count = 0;
    // todo -- ensure start_ult_dist is within 2 and 500 or whatever bounds
    float start_ult_dist = read_ult(10), curr_ult_dist, delta_distance = 0, c;
    // while within computation time
    // and have not traveled enough distance (fwd / bak)
    while (millis() - start_time < max_computation && flag && sig != 'x') {
      // compute distance to-go
      curr_ult_dist = read_ult(3);
      delta_distance = start_ult_dist - curr_ult_dist;
      // decide which motor action to take
      c = param - delta_distance;
      if (sig == 'p') {
        pause_count++;
      } else {
        pause_count = 0;
      }
      if (curr_ult_dist < 10) {
        sig = 'b'; // we're too close to a forward obstacle
      } else if (c > 0.2) {
        sig = 'f';
      } else if (c < -0.2) {
        sig = 'b';
      } else if (pause_count > 8) {
        sig = 'x'; // stop!
      } else {
        sig = 'p';
      }
      // decide how long to keep motors on (max)
      max_motor_time = 50; // todo -- function of acceleration and distance-to-go
      // tell nano what to do
      memcpy(out_buf,   &sig, 1);
      memcpy(out_buf+1, &max_motor_time, sizeof(short));
      memcpy(out_buf+3, &max_motor_time, sizeof(short));
      //while (Serial.availableForWrite() < 5) {;}
      Serial.write(out_buf, 5);
      Serial.flush(); // wait until buffer is fully transmitted
      // repeat
      if (param < 0) { // account for fwd/bwd
        flag = delta_distance > param;
      } else {
        flag = delta_distance < param;
      }
      delay(2*max_motor_time);
    }
    if (sig != 'x') {
      // stop motors
      sig = 'x';
      max_motor_time = 0;
      memcpy(out_buf,   &sig, 1);
      memcpy(out_buf+1, &max_motor_time, sizeof(short));
      memcpy(out_buf+3, &max_motor_time, sizeof(short));
      Serial.write(out_buf, 5);
      Serial.flush();
    }
    ((unsigned long *) out_buf)[0] = 0xffffffff;
    while (((unsigned short *) out_buf)[0] | ((unsigned short *) out_buf)[1]) { // TODO -- timeout error?
      delay(1);
      if (Serial.available() >= 4) {
        Serial.readBytes(out_buf, 4);
      }
    }
    delay(250);
    // tell server what happened
    String message = "{\"id\":";
    message += ID;
    message += ",\"start\":";
    message += start_ult_dist;
    message += ",\"end\":";
    message += read_ult(10);
    message += "}";
    send_mov(message);
//    beep(100, 110);
//    delay(200);
//    beep(200, 264);
  }
}
