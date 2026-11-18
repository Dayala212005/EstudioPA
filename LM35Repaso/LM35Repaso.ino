const int pin_LM35 = 34;


void setup() {
  Serial.begin(115200);
  analogReadResolution(12);

}

void loop() {
  int miliVolts = analogReadMilliVolts(pin_LM35);

  float temperaturaC = miliVolts / 10.0;

  Serial.print(temperaturaC);
  Serial.println(" C");

  delay(2000);

}
