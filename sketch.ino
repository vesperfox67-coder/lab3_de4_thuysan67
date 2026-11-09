/*
 * Ho ten - lop: TrieuTrongHieu_231432235_KTVT_K64
 * LAB 3 - DE 4: Giam sat be nuoi thuy/hai san (ESP32 + FreeRTOS + MQTT)
 * Cam bien : HC-SR04 (muc nuoc), DS18B20 (nhiet do be)
 * Chap hanh: May bom vao (LED xanh duong, PWM), Van thoat ra (LED xanh la)
 *
 * Task truyen data (Core 0): WiFi + MQTT, lay du lieu tu dataQueue gui len broker
 * Task doc data   (Core 1): doc cam bien, in Serial, day dataQueue + alertQueue
 * Task canh bao   (Core 1): doc alertQueue, dieu khien bom/van theo thuat toan
 */

#include <WiFi.h>
#include <PubSubClient.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// ===================== CAU HINH =====================
#define TRIG_PIN     5
#define ECHO_PIN     18
#define ONEWIRE_PIN  4
#define PUMP_PIN     25   // LED PWM mo phong may bom
#define VALVE_PIN    26   // LED mo phong van thoat

const char* WIFI_SSID = "Wokwi-GUEST";
const char* WIFI_PASS = "";

const char* MQTT_SERVER = "broker.hivemq.com";
const int   MQTT_PORT   = 1883;

#define TOPIC_PREFIX "lab3_de4_TrieuTrongHieu_KTVT_K64"
const char* TOPIC_DATA  = TOPIC_PREFIX "/data";     // ESP32 -> Web: so lieu cam bien
const char* TOPIC_STATE = TOPIC_PREFIX "/state";    // ESP32 -> Web: trang thai bom/van
const char* TOPIC_CTRL  = TOPIC_PREFIX "/control";  // Web -> ESP32: lenh dieu khien

const float TANK_HEIGHT_CM = 200.0;  // be cao 2 m, cam bien gan tren nap: muc nuoc (m) = (200 - khoang cach cm)/100
#define REPEAT_IN_ZONE false         // true: het thoi gian ma muc van trong vung thi chay lap lai chu ky moi
const uint32_t READ_PERIOD_MS = 2000;

#define PWM_FREQ 5000
#define PWM_RES  8

// ===================== KIEU DU LIEU =====================
struct SensorData { float distance; float level; float temp; };  // vao dataQueue
struct AlertData  { float level; };                              // vao alertQueue

struct SharedState {           // dung chung giua cac task, bao ve bang mutex
  bool manualMode;
  bool pumpManual;
  bool valveManual;
  int  pumpPct;                // cong suat bom thuc te 0-100
  bool valveOn;                // trang thai van thuc te
};

enum Zone { ZONE_NONE, ZONE_PUMP50, ZONE_PUMP100, ZONE_VALVE };

// ===================== BIEN TOAN CUC =====================
QueueHandle_t dataQueue;
QueueHandle_t alertQueue;
SemaphoreHandle_t stateMutex;
SharedState shared = {false, false, false, 0, false};

WiFiClient espClient;
PubSubClient mqtt(espClient);
OneWire oneWire(ONEWIRE_PIN);
DallasTemperature sensors(&oneWire);

// ===================== HAM PHU =====================
void pwmInit() {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(PUMP_PIN, PWM_FREQ, PWM_RES);
#else
  ledcSetup(0, PWM_FREQ, PWM_RES);
  ledcAttachPin(PUMP_PIN, 0);
#endif
}

void pwmWrite(int duty) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(PUMP_PIN, duty);
#else
  ledcWrite(0, duty);
#endif
}

float readDistanceCm() {
  digitalWrite(TRIG_PIN, LOW);  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH); delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  long dur = pulseIn(ECHO_PIN, HIGH, 30000);  // timeout 30 ms
  if (dur == 0) return -1;
  return dur * 0.0343 / 2.0;
}

// Xuat ra bom/van va cap nhat trang thai dung chung
void applyOutputs(int pumpPct, bool valveOn) {
  pwmWrite(pumpPct * 255 / 100);
  digitalWrite(VALVE_PIN, valveOn ? HIGH : LOW);
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  shared.pumpPct = pumpPct;
  shared.valveOn = valveOn;
  xSemaphoreGive(stateMutex);
}


// Gui vao queue; neu day thi bo goi cu nhat de luon giu du lieu moi nhat
void sendLatest(QueueHandle_t q, const void* item) {
  if (xQueueSend(q, item, 0) != pdTRUE) {
    uint8_t trash[32];
    xQueueReceive(q, trash, 0);
    xQueueSend(q, item, 0);
  }
}

Zone getZone(float level) {
  if (level < 0.5)                    return ZONE_VALVE;    // < 0.5 m
  if (level >= 1.0 && level < 1.5)    return ZONE_PUMP50;   // 1 - 1.5 m
  if (level >= 1.5 && level <= 2.0)   return ZONE_PUMP100;  // 1.5 - 2 m
  return ZONE_NONE;                                          // 0.5-1 m va > 2 m
}

// ===================== MQTT =====================
void mqttCallback(char* topic, byte* payload, unsigned int len) {
  String msg;
  for (unsigned int i = 0; i < len; i++) msg += (char)payload[i];
  msg.trim();
  Serial.printf("[MQTT] Lenh nhan: %s\n", msg.c_str());

  xSemaphoreTake(stateMutex, portMAX_DELAY);
  if (msg == "AUTO") {
    shared.manualMode = false;
    shared.pumpManual = false;
    shared.valveManual = false;
  } else if (msg == "PUMP_ON")   { shared.manualMode = true; shared.pumpManual  = true;  }
  else if (msg == "PUMP_OFF")    { shared.manualMode = true; shared.pumpManual  = false; }
  else if (msg == "VALVE_ON")    { shared.manualMode = true; shared.valveManual = true;  }
  else if (msg == "VALVE_OFF")   { shared.manualMode = true; shared.valveManual = false; }
  xSemaphoreGive(stateMutex);
}

void connectWiFi() {
  Serial.print("[WIFI] Dang ket noi");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS, 6);  // kenh 6 cho Wokwi-GUEST
  while (WiFi.status() != WL_CONNECTED) {
    vTaskDelay(pdMS_TO_TICKS(500));
    Serial.print(".");
  }
  Serial.printf("\n[WIFI] Da ket noi, IP: %s\n", WiFi.localIP().toString().c_str());
}

void connectMQTT() {
  while (!mqtt.connected()) {
    String clientId = "esp32_de4_" + String((uint32_t)esp_random(), HEX);
    Serial.print("[MQTT] Dang ket noi broker...");
    if (mqtt.connect(clientId.c_str())) {
      Serial.println(" OK");
      mqtt.subscribe(TOPIC_CTRL);
    } else {
      Serial.printf(" that bai, rc=%d. Thu lai sau 3s\n", mqtt.state());
      vTaskDelay(pdMS_TO_TICKS(3000));
    }
  }
}

void publishState(bool force, int &lastPump, int &lastValve, int &lastMode, uint32_t &lastMs) {
  SharedState s;
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  s = shared;
  xSemaphoreGive(stateMutex);

  int mode = s.manualMode ? 1 : 0;
  int valve = s.valveOn ? 1 : 0;
  if (force || s.pumpPct != lastPump || valve != lastValve || mode != lastMode ||
      millis() - lastMs > 5000) {
    char buf[96];
    snprintf(buf, sizeof(buf), "{\"pump\":%d,\"valve\":%d,\"mode\":\"%s\"}",
             s.pumpPct, valve, s.manualMode ? "MANUAL" : "AUTO");
    mqtt.publish(TOPIC_STATE, buf);
    lastPump = s.pumpPct; lastValve = valve; lastMode = mode; lastMs = millis();
  }
}

// ===================== TASK TRUYEN DATA (CORE 0) =====================
void transmitTask(void* pv) {
  connectWiFi();                              // 1) ket noi WiFi
  mqtt.setServer(MQTT_SERVER, MQTT_PORT);
  mqtt.setCallback(mqttCallback);

  int lastPump = -1, lastValve = -1, lastMode = -1;
  uint32_t lastMs = 0;
  SensorData d;

  for (;;) {
    if (WiFi.status() != WL_CONNECTED) connectWiFi();
    if (!mqtt.connected()) connectMQTT();
    mqtt.loop();

    // 2) WiFi OK -> doc dataQueue va gui len broker
    if (xQueueReceive(dataQueue, &d, pdMS_TO_TICKS(100)) == pdTRUE) {
      char buf[96];
      snprintf(buf, sizeof(buf), "{\"distance\":%.1f,\"level\":%.2f,\"temp\":%.1f}",
               d.distance, d.level, d.temp);
      mqtt.publish(TOPIC_DATA, buf);
      Serial.printf("[TX] %s\n", buf);
    }
    publishState(false, lastPump, lastValve, lastMode, lastMs);
  }
}

// ===================== TASK DOC DATA (CORE 1) =====================
void readTask(void* pv) {
  for (;;) {
    float dist = readDistanceCm();
    sensors.requestTemperatures();
    float temp = sensors.getTempCByIndex(0);

    if (dist < 0 || temp == DEVICE_DISCONNECTED_C) {
      Serial.println("[READ] Loi doc cam bien!");
    } else {
      float level = (TANK_HEIGHT_CM - dist) / 100.0;  // muc nuoc (m)
      if (level < 0) level = 0;

      Serial.printf("[READ] Khoang cach: %.1f cm | Muc nuoc: %.2f m | Nhiet do: %.1f C\n",
                    dist, level, temp);

      SensorData d = {dist, level, temp};
      AlertData  a = {level};
      sendLatest(dataQueue, &d);    // du lieu day du
      sendLatest(alertQueue, &a);   // du lieu muc nuoc
    }
    vTaskDelay(pdMS_TO_TICKS(READ_PERIOD_MS));
  }
}

// ===================== TASK CANH BAO / DIEU KHIEN (CORE 1) =====================
void startAction(Zone z, uint32_t &endMs) {
  switch (z) {
    case ZONE_PUMP50:                       // 1 m - 1.5 m : bom 50% trong 10 s
      endMs = millis() + 10000; applyOutputs(50, false);
      Serial.println("[ALERT] Muc 1-1.5m: BOM 50% trong 10s");  break;
    case ZONE_PUMP100:                      // 1.5 m - 2 m : bom 100% trong 20 s
      endMs = millis() + 20000; applyOutputs(100, false);
      Serial.println("[ALERT] Muc 1.5-2m: BOM 100% trong 20s"); break;
    case ZONE_VALVE:                        // < 0.5 m : mo van thoat trong 10 s
      endMs = millis() + 10000; applyOutputs(0, true);
      Serial.println("[ALERT] Muc <0.5m: MO VAN THOAT trong 10s"); break;
    default: break;
  }
}

void alertTask(void* pv) {
  AlertData a;
  float level = 0;
  bool haveData = false;
  Zone handled = ZONE_NONE;     // vung da kich hoat (tranh bat lap lien tuc)
  Zone runZone = ZONE_NONE;     // vung dang chay
  bool running = false;
  uint32_t endMs = 0;

  for (;;) {
    if (xQueueReceive(alertQueue, &a, pdMS_TO_TICKS(100)) == pdTRUE) {
      level = a.level;
      haveData = true;
    }

    bool manual, pumpM, valveM;
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    manual = shared.manualMode; pumpM = shared.pumpManual; valveM = shared.valveManual;
    xSemaphoreGive(stateMutex);

    if (manual) {                                   // ---- MANUAL ----
      running = false;
      handled = ZONE_NONE;
      applyOutputs(pumpM ? 100 : 0, valveM);
      continue;
    }

    if (!haveData) continue;                        // ---- AUTO ----
    Zone z = getZone(level);

    if (running && millis() >= endMs) {             // het thoi gian -> tat
      running = false;
      runZone = ZONE_NONE;
      applyOutputs(0, false);
      Serial.println("[ALERT] Het thoi gian -> tat bom/van");
      if (REPEAT_IN_ZONE) handled = ZONE_NONE;      // cho phep chay lai chu ky moi
    }

    if (z == ZONE_NONE) handled = ZONE_NONE;        // muc ra khoi moi vung -> reset

    // Kich hoat khi: vao vung moi (ke ca dang chay vung khac -> chuyen ngay)
    bool newZone = (z != ZONE_NONE) && (z != handled) && !(running && z == runZone);
    if (newZone) {
      startAction(z, endMs);
      running = true;
      runZone = z;
      handled = z;
    }

    if (!running) applyOutputs(0, false);           // khong chay -> dam bao tat
  }
}

// ===================== SETUP / LOOP =====================
void setup() {
  Serial.begin(115200);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(VALVE_PIN, OUTPUT);
  pwmInit();
  pwmWrite(0);
  digitalWrite(VALVE_PIN, LOW);
  sensors.begin();

  stateMutex = xSemaphoreCreateMutex();
  dataQueue  = xQueueCreate(5, sizeof(SensorData));
  alertQueue = xQueueCreate(5, sizeof(AlertData));

  //                      ham          ten              stack  param prio handle core
  xTaskCreatePinnedToCore(transmitTask, "TaskTruyenData", 8192, NULL, 1, NULL, 0);
  xTaskCreatePinnedToCore(readTask,     "TaskDocData",    4096, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(alertTask,    "TaskCanhBao",    4096, NULL, 2, NULL, 1);
}

void loop() {
  vTaskDelete(NULL);   // moi viec do cac task dam nhiem
}
