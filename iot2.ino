/*
  Smart Environment Monitor - ESP32
  Sensors  : DHT11, LDR, PIR
  Outputs  : LED, Servo, Buzzer
  Status   : 3x LED (WiFi, Normal, Alert)
*/

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <DHT.h>
#include <ESP32Servo.h>

// ---- WiFi & Server ----
const char* ssid      = "Mahalnw_2.4G";
const char* password  = "yosupbro";
const char* serverUrl = "http://147.50.253.55:3000";

// ---- Pin ----
#define DHT_PIN     4
#define DHT_TYPE    DHT11
#define LDR_PIN     34
#define PIR_PIN     27

#define LED_ROOM    26
#define SERVO_PIN   25  
#define BUZZER_PIN  33

#define LED_WIFI    18
#define LED_NORMAL  19
#define LED_ALERT   14

// ---- Objects ----
DHT dht(DHT_PIN, DHT_TYPE);
Servo myServo;

// ---- Timing ----
unsigned long lastSendTime   = 0;
unsigned long lastStatusTime = 0;
const long SEND_INTERVAL   = 10000;
const long STATUS_INTERVAL = 5000;

// ---- Sensor Values ----
float temperature = 0;
float humidity    = 0;
int   lightVal    = 0;
bool  motionVal   = false;

// ---- Device State ----
bool ledState    = false;
bool servoState  = false;
bool buzzerState = false;

// ---- Sensor Layer ----
#define LIGHT_THRESHOLD   1000
#define MOTION_TIMEOUT    1000
#define BUZZER_BEEP_MS    200

bool          ledSensor       = false;
bool          buzzerBeeping   = false;
bool          prevMotion      = false;
unsigned long lastMotionTime  = 0;
unsigned long buzzerStartTime = 0;

// ---- Manual Layer ----
bool ledManual    = false;
bool servoManual  = false;
bool buzzerManual = false;

// ---- Temp Auto Servo ----
#define TEMP_THRESHOLD    35.0
bool servoAuto = false;

// -------- Setup --------
void setup() {
  Serial.begin(115200);

  pinMode(PIR_PIN,    INPUT_PULLDOWN);
  pinMode(LED_ROOM,   OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_WIFI,   OUTPUT);
  pinMode(LED_NORMAL, OUTPUT);
  pinMode(LED_ALERT,  OUTPUT);

  myServo.attach(SERVO_PIN);
  myServo.write(0);

  dht.begin();

  connectWiFi();
}

// -------- Loop --------
void loop() {
  unsigned long now = millis();

  readSensors();
  runSensorLogic();

  if (WiFi.status() == WL_CONNECTED) {
    if (now - lastSendTime >= SEND_INTERVAL) {
      lastSendTime = now;
      sendSensorData();
    }
    if (now - lastStatusTime >= STATUS_INTERVAL) {
      lastStatusTime = now;
      fetchDeviceStatus();
    }
  }

  runServoAuto();

  ledState    = ledSensor    || ledManual;
  servoState  = servoManual  || servoAuto;
  buzzerState = buzzerBeeping || buzzerManual;

  applyDeviceState();
  updateStatusLEDs();

  delay(500);
}

// -------- Read Sensors --------
void readSensors() {
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  if (!isnan(t)) temperature = t;
  if (!isnan(h)) humidity    = h;

  lightVal  = analogRead(LDR_PIN);
  motionVal = digitalRead(PIR_PIN);
}

// -------- Sensor Logic --------
void runSensorLogic() {
  unsigned long now = millis();

  bool isDark     = (lightVal > LIGHT_THRESHOLD);
  bool hasPerson  = motionVal;

  if (hasPerson) lastMotionTime = now;

  if (hasPerson || isDark) {
    ledSensor = true;
  } else if (now - lastMotionTime > MOTION_TIMEOUT) {
    ledSensor = false;
  }

  bool risingEdge = (motionVal && !prevMotion);

  if (risingEdge && !buzzerBeeping) {
    buzzerBeeping   = true;
    buzzerStartTime = now;
  }

  if (buzzerBeeping && (now - buzzerStartTime >= BUZZER_BEEP_MS)) {
    buzzerBeeping = false;
  }

  prevMotion = motionVal;
}

// -------- Temp → Servo --------
void runServoAuto() {
  servoAuto = (temperature >= TEMP_THRESHOLD);
}

// -------- Apply Output --------
void applyDeviceState() {
  digitalWrite(LED_ROOM,  ledState    ? HIGH : LOW);
  myServo.write(servoState ? 180 : 90);
  digitalWrite(BUZZER_PIN, buzzerState ? HIGH : LOW);
}

// -------- Status LEDs --------
void updateStatusLEDs() {
  bool wifiOk = (WiFi.status() == WL_CONNECTED);

  if (wifiOk) {
    digitalWrite(LED_WIFI, HIGH);
  } else {
    digitalWrite(LED_WIFI, (millis() / 500) % 2);
  }

  bool sensorOk = !isnan(temperature) && !isnan(humidity);
  digitalWrite(LED_NORMAL, sensorOk ? HIGH : LOW);

  bool tempAlert   = temperature > 35;
  bool sensorError = isnan(dht.readTemperature());

  if (sensorError) {
    digitalWrite(LED_ALERT, (millis() / 200) % 2);
  } else if (tempAlert || motionVal) {
    digitalWrite(LED_ALERT, (millis() / 800) % 2);
  } else {
    digitalWrite(LED_ALERT, LOW);
  }
}

// -------- WiFi --------
void connectWiFi() {
  WiFi.begin(ssid, password);
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 20) {
    delay(500);
    digitalWrite(LED_WIFI, (tries % 2));
    tries++;
  }
  digitalWrite(LED_WIFI, WiFi.status() == WL_CONNECTED);
}

// -------- Send Data --------
void sendSensorData() {
  HTTPClient http;
  http.begin(String(serverUrl) + "/api/data");
  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<128> doc;
  doc["temperature"] = temperature;
  doc["humidity"]    = humidity;
  doc["light"]       = lightVal;
  doc["motion"]      = motionVal;

  String body;
  serializeJson(doc, body);

  http.POST(body);
  http.end();
}

// -------- Fetch Status --------
void fetchDeviceStatus() {
  HTTPClient http;
  http.begin(String(serverUrl) + "/api/status");

  int code = http.GET();
  if (code == 200) {
    String payload = http.getString();
    StaticJsonDocument<128> doc;
    if (!deserializeJson(doc, payload)) {
      ledManual    = doc["led"]    | false;
      servoManual  = doc["servo"]  | false;
      buzzerManual = doc["buzzer"] | false;
    }
  }
  http.end();
}