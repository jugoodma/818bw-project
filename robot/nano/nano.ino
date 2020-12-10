#include <Wire.h>
#include <MPU6050.h>

#define leftA 12
#define leftB 11
#define rightA 3
#define rightB 4

#define MICL A1
#define MICR A2

MPU6050 mpu;
float timeStep = 0.01;
unsigned long s_time;
unsigned long e_time;

float x_accel_offset = 0;
float y_accel_offset = 0;

void calibrateAccel(int samples) {
  Vector normAccel;
  x_accel_offset = 0;
  y_accel_offset = 0;
  for (int i = 0; i < samples; i++) {
	  normAccel = mpu.readNormalizeAccel();
    x_accel_offset += normAccel.XAxis;
    y_accel_offset += normAccel.YAxis;
  	delay(5);
  }
  x_accel_offset /= samples;
  y_accel_offset /= samples;
}

void setup() {
  Serial.begin(9600);
  Serial.println("NANO STARTING.");
  analogReference(INTERNAL);
  //microphones
  pinMode(MICL,INPUT);
  pinMode(MICR,INPUT);
//  mpu.begin(MPU6050_SCALE_2000DPS, MPU6050_RANGE_2G);
  // mpu.setThreshold(3);
//  mpu.calibrateGyro(100);
//  calibrateAccel(100);
  setupMotors();
  boolean sync = false;
  char buf[1];
  while(!sync){
    if(Serial.availableForWrite()){
      Serial.write('s');
      Serial.flush();
      delay(50);
      if(Serial.available()){
        Serial.readBytes(buf,1);
        if(buf[0] == 'a'){
          sync = true;
        }
      }
    }
  }
//  Serial.println("ATTEMPTING TO SYNC WITH ESP.");
//  while (!sync) {
//    Serial.write('s');
//    delay(50);
//    if (Serial.available()) {
//      Serial.readBytes(buf, 1);
//      // Serial.print(" |");
//      // Serial.println(buf[0]);
//      if (buf[0] == 's') {
//        sync = true;
//      }
//    }
//  }
//  Serial.println("\nSYNCED.");
}

void setupMotors(){
  pinMode( leftA, OUTPUT);
  pinMode( leftB, OUTPUT);
  pinMode( rightA, OUTPUT);
  pinMode( rightB, OUTPUT);

  digitalWrite(leftA, LOW);
  digitalWrite(leftB, LOW);
  digitalWrite(rightA, LOW);
  digitalWrite(rightB, LOW);
}

void locListen(int pt, int dt){
  int buf[2];
  int times[4];
  int count = 0;
  boolean end = true;
  times[0] = -1;
  times[1] = -1;
  times[2] = 0;
  delay(dt);
  s_time = millis();
  while(millis()-s_time < pt && Serial.availableForWrite()>=4 && count < 2048){
    buf[0] = analogRead(MICL);
    buf[1] = analogRead(MICR);
    Serial.write((byte *)buf, 4);
    Serial.flush();
    count++;
  }
  e_time = (int)(millis()-s_time);

  times[3] = e_time;
  while(end){
    if(Serial.availableForWrite()>=8){
      Serial.write((byte *)times, 8);
      Serial.flush();
      end = false;
    }
    delay(10);
  }
}

void motors_fwd() {
  digitalWrite(leftA, LOW);
  digitalWrite(leftB, HIGH);
  digitalWrite(rightA, LOW);
  digitalWrite(rightB, HIGH);
}

void motors_bwd() {
  digitalWrite(leftA, HIGH);
  digitalWrite(leftB, LOW);
  digitalWrite(rightA, HIGH);
  digitalWrite(rightB, LOW);
}

void motors_off() {
  digitalWrite(leftA, LOW);
  digitalWrite(leftB, LOW);
  digitalWrite(rightA, LOW);
  digitalWrite(rightB, LOW);
}

void move_straight(int max_time) {
  byte buf[5];
  int param1, param2;
  unsigned long start_time = millis(), command_start_time;
  while (millis() - start_time < max_time && buf[0] != 'x') {
    if (Serial.available() >= 5) {
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
}

//void movFor(int dist) {
//  Serial.println(dist);
//  // float distx = 0;
//  float disty = 0;
//  // float distz = 0;
//  Vector normAccel;
//  float x, y, z, delta;
//  digitalWrite(leftA, LOW);
//  digitalWrite(leftB, HIGH);
//  digitalWrite(rightA, LOW);
//  digitalWrite(rightB, HIGH);
//  unsigned long curr_time = millis();
//  unsigned long prev_time = millis();
//  float prev_a = 0, curr_a, prev_v = 0, curr_v;
//
//  while(dist - disty*100 > 1) {
//    curr_time = millis();
//    normAccel = mpu.readNormalizeAccel();
//    delta = (curr_time - prev_time)/1000.0;
//    prev_time = curr_time;
//    curr_a = normAccel.YAxis - y_accel_offset;
//    if (curr_a < 0) curr_a *= -1;
//    curr_v = (prev_a + curr_a)/2 * delta;
//    disty += (prev_v + curr_v)/2 * delta;
//    Serial.print(delta);
//    Serial.print("\t");
//    Serial.print(curr_a);
//    Serial.print("\t");
//    Serial.print(curr_v);
//    Serial.print("\t");
//    Serial.println(disty);
//    prev_a = curr_a;
//    prev_v = curr_v;
//  }
//
//  digitalWrite(leftA, LOW);
//  digitalWrite(leftB, LOW);
//  digitalWrite(rightA, LOW);
//  digitalWrite(rightB, LOW);
//}

void movRot(int deg){
  Vector norm = mpu.readNormalizeGyro();
  float yaws = norm.ZAxis * timeStep;
  float yawe = yaws;
  unsigned long start_t = millis();
  if(deg > 0){
    digitalWrite(leftA, LOW);
    digitalWrite(leftB, HIGH);
    digitalWrite(rightA, HIGH);
    digitalWrite(rightB, LOW);
  }else{
    deg = -deg;
    digitalWrite(leftA, HIGH);
    digitalWrite(leftB, LOW);
    digitalWrite(rightA, LOW);
    digitalWrite(rightB, HIGH);
  }
  
//  while(yawe - yaws < deg*1.58){
  for(int i =0; i < (int)(deg*3.5); i++){
    norm = mpu.readNormalizeGyro();
    yawe = yawe + abs(norm.ZAxis * timeStep);
    delay(1);
  }
  digitalWrite(leftA, LOW);
  digitalWrite(leftB, LOW);
  digitalWrite(rightA, LOW);
  digitalWrite(rightB, LOW);
  Serial.println(yawe);
}

void loop() {
  if(Serial.available()){
    byte buf[5];
    int param1;
    int param2;
    char param3;
    Serial.readBytes(buf, 5);
    memcpy(&param1, buf+1, 2);
    memcpy(&param2, buf+3, 2);
    if(buf[0] == 'L')
      locListen(param1, param2);
    else if(buf[0] == 'f')
      move_straight(param1);
    else if(buf[0] == 'r')
      movRot(param1);
  }
}
