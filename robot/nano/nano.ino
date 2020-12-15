#include <Wire.h>

#define leftA  12
#define leftB  11
#define rightA  3
#define rightB  4

#define MICL A1
#define MICR A2

#define MPU_ADDR 0x68
#define rot_err 5

#define MAX_LR_MIC_SAMPLES 2048

// global variables

long z_cal = 0;
long angle = 0;
long gyroZ = 0;
long old_Z = 0;
long timePast = 0;
long timePresent = 0;

// main stuff

void setup() {
  delay(1000);
  Serial.begin(115200); // 38400
  Wire.begin();
  analogReference(INTERNAL); // can take out?
  // After changing the analog reference, the first few readings from analogRead() may not be accurate.
  motor_setup();
  mpu_setup();
  callibrateGyroValues();
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

void mpu_setup(){
  Wire.beginTransmission(0b1101000);          // Start the communication by using address of MPU
  Wire.write(0x6B);                           // Access the power management register
  Wire.write(0b00000000);                     // Set sleep = 0
  Wire.endTransmission();                     // End the communication

  // configure gyro
  Wire.beginTransmission(0b1101000);
  Wire.write(0x1B);                           // Access the gyro configuration register
  Wire.write(0b00000000);
  Wire.endTransmission();
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
void callibrateGyroValues() {
  for (int i=0; i<5000; i++) {
    getGyroValues();
    z_cal = z_cal + gyroZ;
  }
  z_cal = z_cal/5000;
}

void getGyroValues() {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x47);
  Wire.endTransmission();
  Wire.requestFrom(MPU_ADDR,2);
  while(Wire.available() < 2);    
  gyroZ = Wire.read()<<8|Wire.read();
}

long updateGyro(){
  old_Z = gyroZ;
  timePast = timePresent;
  timePresent = millis();
  getGyroValues();
  angle = angle + ((timePresent - timePast)*(gyroZ + old_Z - 2*z_cal)) * 0.00000382;
  delay(10);
  return angle;
}

void correct_turn(int degree){
  int count = 5;
    //idea: while the overshot degree is over the threashold
  // go the opposite direction and get a new overshot.
  while(abs(degree) > rot_err && count >=0){
    long lal = updateGyro();
    if(degree > 0){
      turn_Right();
      while(abs(lal-updateGyro())<degree);
      motors_off();
      degree = -(abs(lal-updateGyro())-degree);
      count--;
    }else{
      turn_Left();
      while(abs(lal-updateGyro())<-degree);
      motors_off();
      degree = abs(lal-updateGyro())+degree;
      count--;
    }
  }
}

void rotate_right(int degree){
  long lal = updateGyro();
  motors_rig();
  while(abs(lal-updateGyro())<degree);
  motors_off();
  //this will always be an overshoot so negative tells correct if
  //the overshoot is left or right
  correct_turn(-(abs(lal-updateGyro())-degree));
}

void rotate_left(int degree){
  long lal = updateGyro();
  motors_lef();
  while(abs(lal-updateGyro())<degree);
  motors_off();
  correct_turn(abs(lal-updateGyro())-degree);
}

void move_rotate(unsigned short deg){
  timePresent = millis();
  if(deg > 0){
    rotate_right(deg);
  }else{
    rotate_left(-deg);
  }
}
