#include <Wire.h>

#define leftA  12
#define leftB  11
#define rightA  3
#define rightB  4

#define MICL A1
#define MICR A2

#define MPU_ADDR 0x68
#define rot_err 0.25

#define MAX_LR_MIC_SAMPLES 2048

// global variables

long z_cal = 0;

// main stuff

void setup() {
  delay(1000);
  Serial.begin(115200);
  Wire.begin();
  analogReference(INTERNAL); // can take out?
  // After changing the analog reference, the first few readings from analogRead() may not be accurate.
  motor_setup();
  mpu_setup();
  callibrateGyroValues(5000);
  mic_setup(128);
  // accel_setup();
  sync_board(10000);
}

void loop() {
  if (Serial.available()) {
    byte buf[5];
    unsigned short param1;
    unsigned short param2;
    Serial.readBytes(buf, 5);
    memcpy(&param1, buf+1, 2);
    memcpy(&param2, buf+3, 2);
    if (buf[0] == 'L')
      loc_listen(param1, param2);
    else if(buf[0] == 'f')
      move_straight(param1);
    else if(buf[0] == 'r')
      move_rotate((short) param1);
  }
}

// utility functions

void motors_fwd() {
  digitalWrite(leftA,  LOW);
  digitalWrite(leftB,  HIGH);
  digitalWrite(rightA, LOW);
  digitalWrite(rightB, HIGH);
}

void motors_bwd() {
  digitalWrite(leftA,  HIGH);
  digitalWrite(leftB,  LOW);
  digitalWrite(rightA, HIGH);
  digitalWrite(rightB, LOW);
}

void motors_lef() {
  // left go backwards
  digitalWrite(leftA,  HIGH);
  digitalWrite(leftB,  LOW);
  // right go forwards
  digitalWrite(rightA, LOW);
  digitalWrite(rightB, HIGH);
}

void motors_rig() {
  // left go forwards
  digitalWrite(leftA,  LOW);
  digitalWrite(leftB,  HIGH);
  // right go backwards
  digitalWrite(rightA, HIGH);
  digitalWrite(rightB, LOW);
}

void motors_off() {
  digitalWrite(leftA,  LOW);
  digitalWrite(leftB,  LOW);
  digitalWrite(rightA, LOW);
  digitalWrite(rightB, LOW);
}

// setup functions

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

void motor_setup() {
  pinMode(leftA,  OUTPUT);
  pinMode(leftB,  OUTPUT);
  pinMode(rightA, OUTPUT);
  pinMode(rightB, OUTPUT);
  motors_off();
}

void mpu_setup() {
  Wire.beginTransmission(0b1101000); // Start the communication by using address of MPU
  Wire.write(0x6B); // Access the power management register
  Wire.write(0b00000000); // Set sleep = 0
  Wire.endTransmission(); // End the communication

  // configure gyro
  Wire.beginTransmission(0b1101000);
  Wire.write(0x1B); // Access the gyro configuration register
  Wire.write(0x10);
  Wire.endTransmission();
}

void callibrateGyroValues(int samples) {
  for (int i = 0; i < samples; i++) {
    z_cal = z_cal + gyro_z();
  }
  z_cal = z_cal/samples;
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

// controls and sensors

void loc_listen(unsigned short listen_time, unsigned short delay_time) {
  unsigned long start_time, total_time;
  unsigned short buf[2] = {0xf7f7};
  int count = 0;
  Serial.write((byte *) buf, sizeof(short)); // ack
  delay(delay_time); // what if delay is not long enough for the write to async flush?
  start_time = millis();
  ((byte *) buf)[0] = 0xfe;
  Serial.write((byte *) buf, 1);
  while (millis() - start_time < listen_time && count < MAX_LR_MIC_SAMPLES) {
    if (Serial.availableForWrite() >= 2*sizeof(short)) {
      buf[0] = (unsigned short) analogRead(MICL);
      buf[1] = (unsigned short) analogRead(MICR);
//      buf[0] = 0x102;
//      buf[1] = 0x304;
      // todo -- make a bigger local buffer and read into that
      //  iff Serial.availableForWrite() < local buffer size
      // might be too complicated though
      Serial.write((byte *) buf, 2*sizeof(short));
      Serial.flush(); // sampling rate bounded by flush rate
      count++;
    }
  }
  total_time = millis() - start_time;
  // should be flushed already -- maybe not available for 8 bytes tho?
//  while (Serial.availableForWrite() < 4*sizeof(int)) {
//    delay(10);
//  }
  buf[0] = 0xffff;
  buf[1] = 0xffff;
  Serial.write((byte *) buf, 2*sizeof(short)); // indicate we're done
  Serial.flush();
  Serial.write((byte *) &total_time, sizeof(long));
  Serial.flush();
}

void move_straight(unsigned short max_time) {
  byte buf[5];
  int param1, param2;
  unsigned long start_time = millis(), command_start_time;
  while (millis() - start_time < max_time && buf[0] != 'x') {
    while (Serial.available() >= 5) {
      // new command!
      Serial.readBytes(buf, 5);
      memcpy(&param1, buf+1, 2);
      memcpy(&param2, buf+3, 2);
      command_start_time = millis();
    }
    if (millis() - command_start_time < param1) {
      // keep doing current command
      if (buf[0] == 'f') {
        motors_fwd();
      } else if (buf[0] == 'b') {
        motors_bwd();
      } else {
        motors_off();
      }
    } else {
      motors_off();
    }
  }
  motors_off(); // in case of max computation
  // ack to esp that we stopped
  ((unsigned long *) buf)[0] = 0x00000000;
  Serial.write((byte *) buf, 4);
}

long gyro_z() {
  // retrieve and return the raw gyro_z value
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x47);
  Wire.endTransmission();
  Wire.requestFrom(MPU_ADDR, 2);
  while(Wire.available() < 2);
  // only care about Z rotation
  return Wire.read()<<8|Wire.read();
}

float angle_delta(long old_z, long new_z, unsigned long old_time, unsigned long new_time) {
  // calculate the degrees of angles traveled over old_time to now
  // (integrate) TODO trapezoid rule
  return ((new_time - old_time)/1000.0)*((new_z-z_cal) / 32.8);
}

void move_rotate(short deg) {
  // deg == how far to rotate (+ == left, - == right)
  unsigned long start_time = millis(), prev_time = millis(), curr_time = millis();
  long prev_z = gyro_z(), curr_z = gyro_z();
  float curr_angle = 0; // starting angle always zero, goal to equal deg
  byte buf[sizeof(float)] = {0xf7,0xf7};
  while(abs(curr_angle - deg) > rot_err && curr_time - start_time < 5000) {
    // take gyro_z reading
    prev_time = curr_time;
    curr_time = millis();
    prev_z = curr_z;
    curr_z = gyro_z();
    // add the angle change
    curr_angle += angle_delta(prev_z, curr_z, prev_time, curr_time);
    // LEFT == +deg, RIGHT == -deg
    if (curr_angle - deg > 0) {
      motors_rig();
    } else {
      motors_lef();
    }
    delay(20);
    motors_off();
    delay(40);
  }
  Serial.write((byte *) buf, 2); // ack done
  Serial.write((byte *) &curr_angle, sizeof(float));
}
