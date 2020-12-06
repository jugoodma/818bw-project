#define leftA 12
#define leftB 11
#define rightA 2
#define rightB 3

#define MICL A1
#define MICR A2

void setup() {
  Serial.begin(9600);
  analogReference(INTERNAL);
  //microphones
  pinMode(MICL,INPUT);
  pinMode(MICR,INPUT);
  /*
 //motors
	pinMode( leftA, OUTPUT);
	pinMode( leftB, OUTPUT);
	pinMode( rightA, OUTPUT);
	pinMode( rightB, OUTPUT);

  //motors off
	digitalWrite(leftA, LOW);
	digitalWrite(leftB, LOW);
	digitalWrite(rightA, LOW);
	digitalWrite(rightB, LOW);
  */
  delay(3000);
}
void locListen(int pt, int dt){
  unsigned long s_time;
  int half = 256;
  int full = half *2; //512 integers or 1024 bytes
  int to_send = 16; 
  byte outBuf[to_send*sizeof(int)];
  int samples[full];
  int ptr = 0;
  delay(dt);
  s_time = millis();
  while(ptr < half){// && s_time - millis() < pt){
    samples[ptr] = analogRead(MICL);
    samples[ptr+half] = analogRead(MICR);
    ptr++;
    delay(10);
  }
  ptr = 0;
  while(ptr < full){
    if(Serial.availableForWrite()>=to_send*sizeof(int)){
      memcpy(outBuf,samples+ptr, to_send*sizeof(int));
      Serial.write(outBuf, to_send*(sizeof(int)));
//      for(int i = 0; i < to_send; i++){
//        Serial.print(((int *)outBuf)[i]);
//        Serial.print("|");
//      }
      Serial.flush();
      ptr += to_send;
//      Serial.println();
    }
    delay(10);
  }
}

void motor(){
	digitalWrite(leftA, LOW);
	digitalWrite(leftB, HIGH);
	digitalWrite(rightA, LOW);
	digitalWrite(rightB, HIGH);
  delay(1000);
	digitalWrite(leftA, LOW);
	digitalWrite(leftB, LOW);
	digitalWrite(rightA, LOW);
	digitalWrite(rightB, LOW);
	delay(500);
	digitalWrite(leftA, HIGH);
	digitalWrite(leftB, LOW);
	digitalWrite(rightA, HIGH);
	digitalWrite(rightB, LOW);
	delay(1000);
	digitalWrite(leftA, LOW);
	digitalWrite(leftB, LOW);
	digitalWrite(rightA, LOW);
	digitalWrite(rightB, LOW);
	delay(500);
}

void loop() {
  if(Serial.available()){
    byte buf[5];
    int param1;
    int param2;
    Serial.readBytes(buf, 5);
    memcpy(&param1, buf+1, 2);
    memcpy(&param2, buf+3, 2);
    if(buf[0] == 'L')
      locListen(param1, param2);
  }
}
