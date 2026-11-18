#include <ESP32Servo.h>

const int PIN_SERVO = 13;

Servo miServo;

void setup() {
  Serial.begin(115200);

  // Frecuencia estándar de los servos: 50 Hz (un pulso cada 20 ms)
  miServo.setPeriodHertz(50);

  // attach(pin, ancho mínimo en µs, ancho máximo en µs)
  miServo.attach(PIN_SERVO, 500, 2400);
}

void loop() {
  // Barrido de 0 a 180 grados, un grado a la vez
  for (int angulo = 0; angulo <= 180; angulo = angulo + 1) {
    miServo.write(angulo);
    delay(15);   // tiempo para que el servo alcance la posición
  }

  // Barrido de regreso, de 180 a 0
  for (int angulo = 180; angulo >= 0; angulo = angulo - 1) {
    miServo.write(angulo);
    delay(15);
  }
}
