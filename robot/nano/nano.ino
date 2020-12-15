#include <Wire.h>
#include <MPU6050.h>

#define leftA  12
#define leftB  11
#define rightA  3
#define rightB  4

#define MICL A1
#define MICR A2

#define MAX_LR_MIC_SAMPLES 2048

// global variables

MPU6050 mpu;
// float x_accel_offset = 0;
// float y_accel_offset = 0;

// main stuff

void setup() {
  delay(1000);
  Serial.begin(115200); // 38400
  analogReference(INTERNAL); // can take out?
  // After changing the analog reference, the first few readings from analogRead() may not be accurate.
  motor_setup();
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
      move_rotate(param1);
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

// void calibrate_accel(int samples) {
//   Vector normAccel;
//   x_accel_offset = 0;
//   y_accel_offset = 0;
//   for (int i = 0; i < samples; i++) {
// 	  normAccel = mpu.readNormalizeAccel();
//     x_accel_offset += normAccel.XAxis;
//     y_accel_offset += normAccel.YAxis;
//   	delay(5);
//   }
//   x_accel_offset /= samples;
//   y_accel_offset /= samples;
// }

// void accel_setup() {
//   mpu.begin(MPU6050_SCALE_2000DPS, MPU6050_RANGE_2G);
//   // mpu.setThreshold(3);
//   mpu.calibrateGyro(100);
//   calibrate_accel(100);
// }

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
  unsigned short buf[2] = {0xffff};
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

// TODO!
void move_rotate(unsigned short deg){
  float time_step = 0.01;
  Vector norm = mpu.readNormalizeGyro();
  float yaws = norm.ZAxis * time_step;
  float yawe = yaws;
  unsigned long start_t = millis();
  if (deg > 0) {
    motors_rig();
  } else {
    deg = -deg;
    motors_lef();
  }
//  while(yawe - yaws < deg*1.58){
  for (int i = 0; i < (int) (deg*3.5); i++) {
    norm = mpu.readNormalizeGyro();
    yawe = yawe + abs(norm.ZAxis * time_step);
    delay(1);
  }
  motors_off();
  // Serial.println(yawe);
}
