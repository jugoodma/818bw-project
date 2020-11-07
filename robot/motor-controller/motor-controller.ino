#define leftA 12
#define leftB 11
#define rightA 2
#define rightB 3

void setup() {
  Serial.begin(9600);
	pinMode( leftA, OUTPUT);
	pinMode( leftB, OUTPUT);
	pinMode( rightA, OUTPUT);
	pinMode( rightB, OUTPUT);

	digitalWrite(leftA, LOW);
	digitalWrite(leftB, LOW);
	digitalWrite(rightA, LOW);
	digitalWrite(rightB, LOW);
}
void motor(){
	digitalWrite(leftA, LOW);
	digitalWrite(leftB, HIGH);
	digitalWrite(rightA, LOW);
	digitalWrite(rightB, HIGH);
	delay(500);
	digitalWrite(leftA, LOW);
	digitalWrite(leftB, LOW);
	digitalWrite(rightA, LOW);
	digitalWrite(rightB, LOW);
	delay(100);
	digitalWrite(leftA, HIGH);
	digitalWrite(leftB, LOW);
	digitalWrite(rightA, HIGH);
	digitalWrite(rightB, LOW);
	delay(500);
	digitalWrite(leftA, LOW);
	digitalWrite(leftB, LOW);
	digitalWrite(rightA, LOW);
	digitalWrite(rightB, LOW);
	delay(200);
}
void usonic(){
	byte buf[4];
	long duration, inches, cm;
	if (Serial.available()) {
		Serial.readBytes(buf, 4);
		memcpy(&duration, buf, 4);
		inches = microsecondsToInches(duration);
		cm = microsecondsToCentimeters(duration);
		Serial.print(inches);
		Serial.print("in, ");
		Serial.print(cm);
		Serial.print("cm");
		Serial.println();
	}
}

void loop() {
	usonic();
}

long microsecondsToInches(long microseconds) {
   return microseconds / 74 / 2;
}

long microsecondsToCentimeters(long microseconds) {
   return microseconds / 29 / 2;
}

