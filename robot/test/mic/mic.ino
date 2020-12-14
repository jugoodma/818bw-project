#include <Arduino.h>

// this file is intended to be loaded onto the NANO
// no dependence on the ESP

#define MICL A1
#define MICR A2

void mic_setup(int samples) {
  pinMode(MICL, INPUT);
  pinMode(MICR, INPUT);
  // b/c we changed the analog reference, so we need to flush
  // the first few readings
  for (int i = 0; i < samples; i++) {
    analogRead(MICL);
    analogRead(MICR);
  }
}

void setup() {
  delay(1000);
  Serial.begin(9600);
  analogReference(INTERNAL);
  mic_setup(64);
}

void loop() {
  Serial.print(analogRead(MICL));
  Serial.print("\t");
  Serial.println(analogRead(MICR));
  delay(100);
}
