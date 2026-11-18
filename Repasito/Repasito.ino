#include <SPI.h>
#include <MFRC522.h>
#include <ESP32Servo.h>

const int PIN_IR    = 27;
const int PIN_PIR   = 26;
const int PIN_SERVO = 13;
const int PIN_LM35  = 34;

const int PIN_SS  = 5;
const int PIN_RST = 22;
const int PIN_SCK  = 18;
const int PIN_MISO = 19;
const int PIN_MOSI = 23;

MFRC522 lector(PIN_SS, PIN_RST);
Servo puerta;

// UID autorizado (cámbialo por el de tu tarjeta)
byte uidAutorizado[4] = {0xDE, 0xAD, 0xBE, 0xEF};

bool esAutorizada() {
  if (lector.uid.size != 4) {
    return false;
  }
  for (byte i = 0; i < 4; i = i + 1) {
    if (lector.uid.uidByte[i] != uidAutorizado[i]) {
      return false;
    }
  }
  return true;
}

float leerTemperatura() {
  float sumaMilivoltios = 0;
  int cantidadMuestras = 10;

  for (int i = 0; i < cantidadMuestras; i = i + 1) {
    sumaMilivoltios = sumaMilivoltios + analogReadMilliVolts(PIN_LM35);
    delay(5);
  }

  float promedioMilivoltios = sumaMilivoltios / cantidadMuestras;
  float temperaturaC = promedioMilivoltios / 10.0;
  return temperaturaC;
}

void setup() {
  Serial.begin(115200);

  pinMode(PIN_PIR, INPUT);
  pinMode(PIN_IR, INPUT);

  SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_SS);
  lector.PCD_Init();

  puerta.setPeriodHertz(50);
  puerta.attach(PIN_SERVO, 500, 2400);
  puerta.write(0);

  Serial.println("Calentando PIR (30 s)...");
  delay(30000);
  Serial.println("Sistema listo");
}

void loop() {
  int presencia = digitalRead(PIN_PIR);

  if (presencia == HIGH) {
    Serial.println("Alguien cerca. Acerca tu tarjeta.");

    if (lector.PICC_IsNewCardPresent() == true) {
      if (lector.PICC_ReadCardSerial() == true) {

        if (esAutorizada() == true) {
          Serial.println("Acceso concedido");
          puerta.write(90);
          delay(3000);

          while (digitalRead(PIN_IR) == LOW) {
            Serial.println("Obstaculo en la puerta, esperando...");
            delay(500);
          }
          puerta.write(0);
        } else {
          Serial.println("Acceso denegado");
        }

        lector.PICC_HaltA();
        lector.PCD_StopCrypto1();
      }
    }
  }

  float temperatura = leerTemperatura();
  Serial.print("Temp: ");
  Serial.print(temperatura);
  Serial.println(" C");

  delay(300);
}