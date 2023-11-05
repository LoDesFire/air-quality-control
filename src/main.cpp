#include <Arduino.h>
#include <esp_timer.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

#include <cstring>

#include <MQ4.h>
#include <MQ135.h>
#include <DHT.h>
#include <DHT_U.h>
#include <Adafruit_Sensor.h>
#include <DallasTemperature.h>
#include <OneWire.h>

#define DHT_PIN 18
#define MQ135_PIN 35
#define MQ4_PIN 34
#define ONE_WIRE_PIN 15
#define PERIOD_SEC 5
#define ISSUE_TIMER_SEC 600

void wifiDisconnetcedHandler(arduino_event_t *event);

static void recordings_timer_callback(void *args);

esp_timer_handle_t _timer = NULL;

esp_timer_create_args_t timerConfig = {
    .callback = recordings_timer_callback,
    .arg = NULL,
    .dispatch_method = ESP_TIMER_TASK,
    .name = "get_recordings_timer",
    .skip_unhandled_events = false};


esp_timer_handle_t restart_timer = NULL;

esp_timer_create_args_t restartTimerConfig = {
    .callback = [](void *) {ESP.restart();},
    .arg = NULL,
    .dispatch_method = ESP_TIMER_TASK,
    .name = "restart_timer",
    .skip_unhandled_events = false};


// const char *ssid = "define (;;) ever \\n forever";
// const char *password = "bsuir_the_best";

// const char *ssid = "DETI_KSISA";
// const char *password = "r4i93fjsa";

// const char *ssid = "32342";
// const char *password = "02291103";

const char *ssid = "Xiaomi 11 Lite 5G NE";
const char *password = "";

bool LED_VALUE;

class ServerApi
{
  String apiUrl = "https://airqualityapi-georga399.amvera.io/api/";
  HTTPClient *httpClient;
  DynamicJsonDocument *jsonDocument;

  int POST(String endpoint, DynamicJsonDocument *document)
  {
    String payload;
    httpClient = new HTTPClient();
    httpClient->begin(apiUrl + endpoint);
    httpClient->addHeader("Content-Type", "application/json");
    serializeJson(*document, payload);
    vTaskDelay(pdMS_TO_TICKS(1));
    int code = httpClient->POST(payload);
    delete httpClient;

    if (code == 200)
    {
      delete jsonDocument;
      jsonDocument = new DynamicJsonDocument(40000);
    }
    ESP_LOGI("api", "POST query. Response code: %d", code);
    return code;
  }

public:
  ServerApi()
  {
    jsonDocument = new DynamicJsonDocument(40000);
  }

  bool push(uint32_t timestamp, float *record)
  {
    JsonObject jObj = jsonDocument->createNestedObject();

    jObj["date"] = timestamp;
    jObj["temperatureC"] = record[0];
    jObj["humidity"] = record[1];
    jObj["cH4"] = record[2];
    jObj["cO2"] = record[3];

    ESP_LOGI("api", "Pushed new record. Size: %d", jsonDocument->size());
    if (jsonDocument->size() > 19 && POST("rawdata/push", jsonDocument) == 200)
      jsonDocument->clear();

    if (jsonDocument->size() > 50)
      return false;

    return true;
  }

  void pushExtra(uint32_t *timestamp, float *record)
  {
  }
};

class Sensors
{
  MQ4 mq4;
  MQ135 mq135;
  DHT_Unified dht;
  OneWire oneWire;
  DallasTemperature tSensor;

public:
  Sensors(u8_t mq4_pin, u8_t mq135_pin, u8_t dht_pin, u8_t dallasTemperature_pin)
      : mq4(mq4_pin), mq135(mq135_pin), dht(dht_pin, DHT11), oneWire(dallasTemperature_pin), tSensor(&oneWire)
  {
  }

  void begin()
  {
    dht.begin();
    mq4.begin();
    mq135.begin();
    tSensor.begin();
  }

  float *get_records()
  {
    float *records = new float[4];
    sensors_event_t event;
    dht.humidity().getEvent(&event);
    tSensor.requestTemperatures();
    if (isnan(event.relative_humidity))
      return nullptr;
    records[0] = tSensor.getTempCByIndex(0);
    records[1] = event.relative_humidity;
    records[2] = mq4.getValue(records[0], records[1]);
    records[3] = mq135.getValue(records[0], records[1]);
    return records;
  }
};

ServerApi api;
Sensors sensors(MQ4_PIN, MQ135_PIN, DHT_PIN, ONE_WIRE_PIN);

static void recordings_timer_callback(void *args)
{
  ESP_LOGI("timer", "Callback");
  tm loctime;
  getLocalTime(&loctime);
  float *records = sensors.get_records();

  if (loctime.tm_year == 70 || records == nullptr)
  {
    ESP_LOGI("recordings", "Timestamp: %u, rh: %f", mktime(&loctime));
    return;
  }
  else
  {
    ESP_LOGI("recordings", "Timestamp: %u, values: [%f, %f, %f, %f]", mktime(&loctime), records[0], records[1], records[2], records[3]);
  }

  if (!api.push(mktime(&loctime), records))
  {
    esp_timer_stop(_timer);
    ESP_LOGW("timer", "Timer stopped due to Internet problems");
  }
  delete[] records;
}

void wifiDisconnetcedHandler(arduino_event_t *event)
{
  if (!esp_timer_is_active(restart_timer)) {
    ESP_LOGI("restart_timer", "Started");
    esp_timer_start_once(restart_timer, ISSUE_TIMER_SEC * 1000000);
  }
  WiFi.begin(ssid, password);
  digitalWrite(LED_BUILTIN, LED_VALUE = !LED_VALUE);
  delay(500);
}

void initializeTime() {
  configTime(3 * 3600, 0, "europe.pool.ntp.org");
  tm inTime;
  while (inTime.tm_year < 71) 
    getLocalTime(&inTime);
}

void wifiConnetcedHandler(arduino_event_t *event)
{
  if (!esp_timer_is_active(_timer))
  {
    initializeTime();
    esp_timer_start_periodic(_timer, PERIOD_SEC * 1000000);
    ESP_LOGI("timer", "Started");
  }
  ESP_LOGI("wifi", "Connected");
  digitalWrite(LED_BUILTIN, LED_VALUE = LOW);
  esp_timer_stop(restart_timer);
  ESP_LOGI("restart_timer", "Dropped");
}

void setup()
{
  WiFi.begin(ssid, password);
  Serial.begin(9600);
  sensors.begin();
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LED_VALUE = HIGH);

  ESP_LOGI("timer", "Software timers creating");
  esp_timer_create(&timerConfig, &_timer);
  esp_timer_create(&restartTimerConfig, &restart_timer);
  if (_timer == NULL || restart_timer == NULL)
  {
    ESP_LOGI("timer", "Failed to create");
    ESP.restart();
  }
  ESP_LOGI("timer", "Succsessfully created");

  ESP_LOGI("wifi", "WiFi connecting...");
  WiFi.onEvent(wifiConnetcedHandler, ARDUINO_EVENT_WIFI_STA_CONNECTED);
  WiFi.onEvent(wifiDisconnetcedHandler, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  WiFi.onEvent(wifiDisconnetcedHandler, ARDUINO_EVENT_WIFI_STA_LOST_IP);

  wifiDisconnetcedHandler(nullptr);
}

void loop() {}