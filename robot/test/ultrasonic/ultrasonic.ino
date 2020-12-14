#include <Arduino.h>

#define echoPin 5
#define trigPin 4

// this file is intended to be loaded onto the ESP
// no dependence on the NANO

void usonic_setup(){
  pinMode(echoPin, INPUT);
  pinMode(trigPin, OUTPUT);
}

float microsecondsToCentimeters(long microseconds) {
  return microseconds / 29.0 / 2.0;
}

float read_ult() {
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  // Reads the echoPin, returns the sound wave travel time in microseconds
  return microsecondsToCentimeters(pulseIn(echoPin, HIGH));
}

void setup() {
  Serial.begin(9600);
  usonic_setup();
}

void loop() {
  Serial.print(read_ult());
  Serial.println("cm");
  delay(100);
}
