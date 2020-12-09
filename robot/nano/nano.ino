#define leftA 12
#define leftB 11
#define rightA 3
#define rightB 4

#define MICL A1
#define MICR A2

void setup() {
  Serial.begin(9600);
  analogReference(INTERNAL);
  //microphones
  pinMode(MICL,INPUT);
  pinMode(MICR,INPUT);
  setupMotors();
  boolean sync = false;
  char buf[1];
  while(!sync){
    Serial.write('s');
    delay(50);
    if(Serial.available()){
      Serial.readBytes(buf,1);
      if(buf[0] == 'a')
        sync = true;
    }
  }
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
      Serial.flush();
      ptr += to_send;
    }
    delay(10);
  }
}

void movFor(int dist){
  digitalWrite(leftA, LOW);
  digitalWrite(leftB, HIGH);
  digitalWrite(rightA, LOW);
  digitalWrite(rightB, HIGH);
  delay(dist*10);
  digitalWrite(leftA, LOW);
  digitalWrite(leftB, LOW);
  digitalWrite(rightA, LOW);
  digitalWrite(rightB, LOW);
}

void movBac(int dist){
  digitalWrite(leftA, HIGH);
  digitalWrite(leftB, LOW);
  digitalWrite(rightA, HIGH);
  digitalWrite(rightB, LOW);
  delay(dist*10);
  digitalWrite(leftA, LOW);
  digitalWrite(leftB, LOW);
  digitalWrite(rightA, LOW);
  digitalWrite(rightB, LOW);
}

void movRot(int deg){
  if(deg > 0){
    digitalWrite(leftA, LOW);
    digitalWrite(leftB, HIGH);
    digitalWrite(rightA, HIGH);
    digitalWrite(rightB, LOW);
    delay(1000);
    digitalWrite(leftA, LOW);
    digitalWrite(leftB, LOW);
    digitalWrite(rightA, LOW);
    digitalWrite(rightB, LOW);
  }else{
    digitalWrite(leftA, HIGH);
    digitalWrite(leftB, LOW);
    digitalWrite(rightA, LOW);
    digitalWrite(rightB, HIGH);
    delay(1000);
    digitalWrite(leftA, LOW);
    digitalWrite(leftB, LOW);
    digitalWrite(rightA, LOW);
    digitalWrite(rightB, LOW);    
  }
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
    else if(buf[0] == 'F'){
      Serial.print("HERE\n");
      movFor(param1);
    }else if(buf[0] == 'B')
      movBac(param1);
    else if(buf[0] == 'R')
      movRot(param1);
  }
}
