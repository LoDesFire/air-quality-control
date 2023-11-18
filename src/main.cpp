#include <Arduino.h>
#include <esp_timer.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

#include <cstring>

#include <MQUnifiedsensor.h>
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

static void recordingsTimerCallback(void *args);

esp_timer_handle_t _timer = NULL;

esp_timer_create_args_t timerConfig = {
    .callback = recordingsTimerCallback,
    .arg = NULL,
    .dispatch_method = ESP_TIMER_TASK,
    .name = "get_recordings_timer",
    .skip_unhandled_events = true};

esp_timer_handle_t restart_timer = NULL;

esp_timer_create_args_t restartTimerConfig = {
    .callback = [](void *)
    { ESP.restart(); },
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

enum EspState
{
  NONE = 0,
  HIGH_CO2 = 1,
  HIGH_CH4 = 2,
  SENSORS_ISSUE = 4,
};

int espState = EspState::NONE;
TaskHandle_t pushQueryTaskHandle;
TaskHandle_t pushExtraQueryTaskHandle;

class ServerApi
{
  String apiUrl;
  DynamicJsonDocument *records;
  StaticJsonDocument<20> **statusCodes;
  HTTPClient *httpClient;
  const int STATUS_CODES_LENGTH = 10;
  int statusCodesPointer;

  int POST(String endpoint, JsonDocument *document)
  {
    String payload;
    httpClient = new HTTPClient();
    httpClient->begin(apiUrl + endpoint);
    httpClient->addHeader("Content-Type", "application/json");
    serializeJson(*document, payload);
    vTaskDelay(pdMS_TO_TICKS(1));
    int code = httpClient->POST(payload);
    delete httpClient;

    ESP_LOGI("api", "POST query. Response code: %d", code);
    return code;
  }

public:
  ServerApi()
  {
    apiUrl = "https://airqualityapi-georga399.amvera.io/api/";
    records = new DynamicJsonDocument(40000);
    statusCodes = new StaticJsonDocument<20> *[STATUS_CODES_LENGTH]
    { nullptr };
    statusCodesPointer = -1;
  }

  void push(uint32_t timestamp, float *raw)
  {
    JsonObject record = records->createNestedObject();

    record["date"] = timestamp;
    record["temperatureC"] = raw[0];
    record["humidity"] = raw[1];
    record["cH4"] = raw[2];
    record["cO2"] = raw[3];

    ESP_LOGI("api", "Pushed new record. Size: %d", records->size());
  }

  void pushExtra(int *extraCode)
  {
    auto extraData = new StaticJsonDocument<20>;
    // int *code = static_cast<int *>(extraCode);
    (*extraData)["statusCode"] = *extraCode;
    delete extraCode;
    if (statusCodesPointer < STATUS_CODES_LENGTH - 1)
      statusCodes[++statusCodesPointer] = extraData;
    // makeQueryTask();
  }

  void pushQuery(void *)
  {
    // DynamicJsonDocument *records = static_cast<DynamicJsonDocument *>(jsonDocument);
    int responseCode = 0;
    ESP_LOGI("tasks", "%d", esp_get_free_heap_size());
    int responseCode = POST("rawdata/push", records);
    if (responseCode == 200 || responseCode == -11)
    {
      delete records;
      records = new DynamicJsonDocument(40000);
    }
  }

  void pushExtraQuery(void *)
  {
    // StaticJsonDocument<20> *statusCode = static_cast<StaticJsonDocument<20> *>(jsonDocument);
    int responseCode = POST("rawdata/pushExtra", statusCodes[statusCodesPointer]);

    if (responseCode == 200 || responseCode == -11)
    {
      delete statusCodes[statusCodesPointer];
      statusCodesPointer--;
    }
  }

  bool isExtraEmpty()
  {
    return statusCodesPointer != -1;
  }

  int recordsSize()
  {
    return records->size();
  }
};

class Sensors
{
  MQUnifiedsensor mq4;
  MQUnifiedsensor mq135;
  DHT_Unified dht;
  OneWire oneWire;
  DallasTemperature tSensor;

public:
  Sensors(u8_t mq4_pin, u8_t mq135_pin, u8_t dht_pin, u8_t dallasTemperature_pin)
      : mq4("ESP-32", 3.3f, 12, mq4_pin, "MQ-4"),
        mq135("ESP-32", 3.3f, 12, mq135_pin, "MQ-135"),
        dht(dht_pin, DHT11),
        oneWire(dallasTemperature_pin),
        tSensor(&oneWire)
  {
    mq4.setRegressionMethod(1);
    mq4.setA(1012.7);
    mq4.setB(-2.786);
    mq4.setRL(43.0f);
    mq4.setR0(380.0f);
    mq135.setRegressionMethod(1);
    mq135.setA(102.2);
    mq135.setB(-2.473);
    mq135.setRL(75.0f);
    mq135.setR0(2100);
  }

  void begin()
  {
    mq4.init();
    mq135.init();
    dht.begin();
    tSensor.begin();
  }

  float *get_records()
  {
    sensors_event_t event;
    mq4.update();
    mq135.update();
    dht.humidity().getEvent(&event);
    tSensor.requestTemperatures();
    float temp = tSensor.getTempCByIndex(0);
    float humidity = event.relative_humidity;
    float cFactor_mq4 = -((0.83f / 100.0f * temp * temp - 0.92 * temp + 25 - (0.3 * humidity)) / 100.0f);
    float cFactor_mq135 = 0.00035f * temp * temp - 0.02718f * temp + 1.39538f - (humidity - 33.f) * 0.0018f;
    float mq4_value = mq4.readSensor(false, cFactor_mq4);
    float mq135_value = mq135.readSensor(false, cFactor_mq135);

    if (isnanf(humidity) || isinff(mq4_value) || isnan(mq4_value) || isinff(mq135_value) || isnan(mq135_value))
      return nullptr;
    float *records = new float[4]{
        temp,
        humidity,
        mq4_value,
        mq135_value};
    return records;
  }
};

ServerApi api;
Sensors sensors(MQ4_PIN, MQ135_PIN, DHT_PIN, ONE_WIRE_PIN);

void pushExtraQuery(void *)
{
  api.pushExtraQuery(nullptr);
  vTaskDelete(NULL);
}

void pushQuery(void *)
{
  api.pushQuery(nullptr);
  vTaskDelete(NULL);
}

void pushExtra(void *code)
{
  int *extraCode = static_cast<int *>(code);
  api.pushExtra(extraCode);
  vTaskDelete(NULL);
}

void makeQueryTask()
{
  if (api.isExtraEmpty())
  {
    xTaskCreate(
        pushExtraQuery,
        "pushExtraQuery",
        20480,
        // static_cast<void *>(statusCodes[statusCodesPointer]),
        nullptr,
        4 | portPRIVILEGE_BIT,
        &pushExtraQueryTaskHandle);
  }
  if (api.recordsSize() > 0)
    xTaskCreate(
        pushQuery,
        "pushQuery",
        20480,
        nullptr,
        1 | portPRIVILEGE_BIT,
        &pushQueryTaskHandle);
}

static void recordingsTimerCallback(void *args)
{
  ESP_LOGI("timer", "Callback");
  tm loctime;
  getLocalTime(&loctime);
  float *records = sensors.get_records();

  if (records == nullptr)
  {
    ESP_LOGI("recordings", "Timestamp: %u", mktime(&loctime));
    if (espState != EspState::SENSORS_ISSUE)
    {
      int *code = new int(EspState::SENSORS_ISSUE);
      api.pushExtra(code);
    }
  }
  else
  {
    if (espState == EspState::SENSORS_ISSUE)
      espState = EspState::NONE;
    ESP_LOGI("recordings", "Timestamp: %u, values: [%f, %f, %f, %f]", mktime(&loctime), records[0], records[1], records[2], records[3]);
  }

  api.push(mktime(&loctime), records);

  makeQueryTask();
}

static IRAM_ATTR void highCH4Callback(void *args)
{
  TaskHandle_t null;
  int *code = new int(EspState::HIGH_CH4);
  xTaskCreate(
      pushExtra,
      "highCH4",
      3000,
      static_cast<void *>(code),
      4 | portPRIVILEGE_BIT,
      &null);
}

static IRAM_ATTR void highCO2Callback(void *args)
{
  TaskHandle_t null;
  int *code = new int(EspState::HIGH_CO2);
  xTaskCreate(
      pushExtra,
      "highCO2",
      3000,
      static_cast<void *>(code),
      4 | portPRIVILEGE_BIT,
      &null);
}

void wifiDisconnetcedCallback(arduino_event_t *event)
{
  if (!esp_timer_is_active(restart_timer))
  {
    ESP_LOGI("restart_timer", "Started");
    esp_timer_start_once(restart_timer, ISSUE_TIMER_SEC * 1000000);
  }
  WiFi.begin(ssid, password);
  digitalWrite(LED_BUILTIN, LED_VALUE = !LED_VALUE);
  delay(1000);
}

void initializeTime()
{
  configTime(3 * 3600, 0, "europe.pool.ntp.org");
  tm inTime;
  do
  {
    getLocalTime(&inTime);
  } while (inTime.tm_year < 71);
}

void wifiConnetcedCallback(arduino_event_t *event)
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
  WiFi.onEvent(wifiConnetcedCallback, ARDUINO_EVENT_WIFI_STA_CONNECTED);
  WiFi.onEvent(wifiDisconnetcedCallback, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  WiFi.onEvent(wifiDisconnetcedCallback, ARDUINO_EVENT_WIFI_STA_LOST_IP);

  wifiDisconnetcedCallback(nullptr);
}

void loop()
{
}