#include <WiFi.h>
#include <SPI.h>
#include <MFRC522.h>
#include "Adafruit_MQTT.h"
#include "Adafruit_MQTT_Client.h"

// ==================================================
// CONFIGURACION WIFI Y ADAFRUIT IO (cambia estos datos)
// ==================================================
#define WIFI_SSID      "TU_WIFI"
#define WIFI_PASSWORD  "TU_CONTRASENA"

#define AIO_SERVER      "io.adafruit.com"
#define AIO_SERVERPORT  1883
#define AIO_USERNAME    "TU_USUARIO_AIO"
#define AIO_KEY         "TU_AIO_KEY"

// ==================================================
// PINES
// ==================================================
// Sensor IR
const int PIN_IR = 4;

// Sensor PIR
const int PIN_PIR = 27;

// Lector RFID (MFRC522)
// SCK = 18, MOSI = 23, MISO = 19 (pines SPI por defecto del ESP32)
constexpr int RST_PIN = 22;
constexpr int SS_PIN  = 5;

// ==================================================
// OBJETOS Y VARIABLES
// ==================================================
MFRC522 mfrc522(SS_PIN, RST_PIN);

WiFiClient cliente;
Adafruit_MQTT_Client mqtt(&cliente, AIO_SERVER, AIO_SERVERPORT, AIO_USERNAME, AIO_KEY);

// Un feed por sensor
Adafruit_MQTT_Publish feedIR   = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/sensor-ir");
Adafruit_MQTT_Publish feedPIR  = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/sensor-pir");
Adafruit_MQTT_Publish feedRFID = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/tarjeta-rfid");

// ---- IR ----
int estadoIR = 0;      // 1 = detectado, 0 = nada
int ultimoIR = -1;     // -1 fuerza el primer envio
unsigned long tEnvioIR = 0;

// ---- PIR ----
const unsigned long TIEMPO_CALENTAMIENTO_PIR = 30000;  // ~30 s al encender
bool pirListo = false;
int estadoPIR = 0;     // 1 = movimiento, 0 = reposo
int ultimoPIR = -1;
unsigned long tEnvioPIR = 0;

// ---- RFID ----
unsigned long tEnvioRFID = 0;

// ---- Tiempos generales ----
const unsigned long INTERVALO_LECTURA   = 50;      // lectura de IR y PIR (ms)
const unsigned long INTERVALO_MIN_ENVIO = 2500;    // minimo entre envios del mismo feed (ms)
const unsigned long INTERVALO_PERIODICO = 20000;   // reenvio periodico de IR y PIR (ms)
const unsigned long INTERVALO_REINTENTO = 5000;    // reintento de conexion MQTT (ms)

unsigned long tLectura    = 0;
unsigned long tPeriodico  = 0;
unsigned long tReintento  = 0;

// ==================================================
// WIFI
// ==================================================
void conectarWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

  Serial.print("[WiFi] Conectando");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.println("[WiFi] Conectado");
}

// ==================================================
// ADAFRUIT IO (MQTT)
// ==================================================
void conectarMQTT() {
  if (mqtt.connected()) return;

  // Un solo intento cada 5 s, sin bloquear la lectura de sensores
  if (millis() - tReintento < INTERVALO_REINTENTO && tReintento != 0) return;
  tReintento = millis();

  Serial.println("[AIO] Conectando a Adafruit IO...");
  int8_t resultado = mqtt.connect();

  if (resultado == 0) {
    Serial.println("[AIO] Conectado");
  } else {
    Serial.print("[AIO] Error: ");
    Serial.println(mqtt.connectErrorString(resultado));
    mqtt.disconnect();
  }
}

void enviarNumero(Adafruit_MQTT_Publish &feed, int valor, const char *nombre) {
  if (feed.publish((int32_t)valor)) {
    Serial.print("[AIO] Enviado ");
    Serial.print(nombre);
    Serial.print(": ");
    Serial.println(valor);
  } else {
    Serial.print("[AIO] Error al enviar ");
    Serial.println(nombre);
  }
}

// ==================================================
// SENSOR IR
// ==================================================
void iniciarIR() {
  pinMode(PIN_IR, INPUT);
  Serial.println("[IR] Listo");
}

void leerIR() {
  // La mayoria de modulos IR dan LOW cuando detectan algo
  estadoIR = (digitalRead(PIN_IR) == LOW) ? 1 : 0;

  if (estadoIR != ultimoIR) {
    Serial.print("[IR] ");
    Serial.println(estadoIR ? "Objeto detectado" : "Sin objeto");

    // Enviar a Adafruit IO si ya paso el tiempo minimo
    if (mqtt.connected() && millis() - tEnvioIR >= INTERVALO_MIN_ENVIO) {
      enviarNumero(feedIR, estadoIR, "IR");
      tEnvioIR = millis();
      ultimoIR = estadoIR;
    } else if (!mqtt.connected()) {
      ultimoIR = estadoIR;
    }
  }
}

// ==================================================
// SENSOR PIR
// ==================================================
void iniciarPIR() {
  pinMode(PIN_PIR, INPUT);
  Serial.println("[PIR] Calentando (30 s)...");
}

void leerPIR() {
  // Mientras se calienta, se ignoran las lecturas
  if (!pirListo) {
    if (millis() >= TIEMPO_CALENTAMIENTO_PIR) {
      pirListo = true;
      Serial.println("[PIR] Listo");
    }
    return;
  }

  // El PIR da HIGH cuando detecta movimiento
  estadoPIR = (digitalRead(PIN_PIR) == HIGH) ? 1 : 0;

  if (estadoPIR != ultimoPIR) {
    Serial.print("[PIR] ");
    Serial.println(estadoPIR ? "Movimiento detectado" : "Sin movimiento");

    // Enviar a Adafruit IO si ya paso el tiempo minimo
    if (mqtt.connected() && millis() - tEnvioPIR >= INTERVALO_MIN_ENVIO) {
      enviarNumero(feedPIR, estadoPIR, "PIR");
      tEnvioPIR = millis();
      ultimoPIR = estadoPIR;
    } else if (!mqtt.connected()) {
      ultimoPIR = estadoPIR;
    }
  }
}

// ==================================================
// LECTOR RFID
// ==================================================
void iniciarRFID() {
  SPI.begin();
  mfrc522.PCD_Init();
  Serial.println("[RFID] Listo");
}

void leerRFID() {
  // Si no hay tarjeta nueva, salir
  if (!mfrc522.PICC_IsNewCardPresent()) return;
  if (!mfrc522.PICC_ReadCardSerial()) return;

  // Convertir el UID a texto, por ejemplo "04 A3 7F 2B"
  char uid[32] = "";
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    char byteTexto[4];
    sprintf(byteTexto, i == 0 ? "%02X" : " %02X", mfrc522.uid.uidByte[i]);
    strcat(uid, byteTexto);
  }

  Serial.print("[RFID] Tarjeta detectada. UID: ");
  Serial.println(uid);

  // Enviar el UID a Adafruit IO
  if (mqtt.connected() && millis() - tEnvioRFID >= INTERVALO_MIN_ENVIO) {
    if (feedRFID.publish(uid)) {
      Serial.println("[AIO] Enviado RFID");
    } else {
      Serial.println("[AIO] Error al enviar RFID");
    }
    tEnvioRFID = millis();
  }

  // Detener la lectura de esta tarjeta
  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();
}

// ==================================================
// SETUP
// ==================================================
void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println("=== Iniciando sensores ===");

  iniciarIR();
  iniciarPIR();
  iniciarRFID();

  Serial.println("=== Iniciando conexion ===");
  conectarWiFi();
  conectarMQTT();

  Serial.println("=== Sistema en marcha ===");
}


// ==================================================
// LOOP
// ==================================================
void loop() {
  // Mantener las conexiones
  conectarWiFi();
  conectarMQTT();
  if (mqtt.connected()) mqtt.processPackets(10);

  // El RFID se revisa en cada vuelta para no perder tarjetas
  leerRFID();

  // IR y PIR se leen cada 50 ms
  unsigned long ahora = millis();
  if (ahora - tLectura >= INTERVALO_LECTURA) {
    tLectura = ahora;

    leerIR();
    leerPIR();
  }

  // Reenvio periodico del estado de IR y PIR
  if (mqtt.connected() && ahora - tPeriodico >= INTERVALO_PERIODICO) {
    tPeriodico = ahora;
    enviarNumero(feedIR, estadoIR, "IR (periodico)");
    if (pirListo) enviarNumero(feedPIR, estadoPIR, "PIR (periodico)");
  }
}