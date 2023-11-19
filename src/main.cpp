#include <Arduino.h>
#include <esp_timer.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// #include <cstring>

#include <MQUnifiedsensor.h>
#include <DHT.h>
#include <DHT_U.h>
#include <Adafruit_Sensor.h>
#include <DallasTemperature.h>
#include <OneWire.h>

#define DHT_PIN 18
#define MQ135_PIN 35
#define MQ4_PIN 34
#define CH4_ISR_PIN 19
#define ONE_WIRE_PIN 15

#define COLLECT_PERIOD_SEC 5
#define ISSUE_TIMER_SEC 600
#define RECORDS_PACKAGE_SIZE 3
#define RECORDS_QUEUE_SIZE 5
#define EXTRA_QUEUE_SIZE 5

#define RECORDS_ITEM_SIZE sizeof(char *)
#define EXTRA_ITEM_SIZE sizeof(int *)

esp_timer_handle_t restart_timer = NULL;
esp_timer_create_args_t restartTimerConfig = {
    .callback = [](void *)
    { ESP.restart(); },
    .arg = NULL,
    .dispatch_method = ESP_TIMER_TASK,
    .name = "restart_timer",
    .skip_unhandled_events = false};

StaticQueue_t _recordsQueueBuffer;
uint8_t _recordsQueueStorage[RECORDS_QUEUE_SIZE * RECORDS_ITEM_SIZE];
StaticQueue_t _extraQueueBuffer;
uint8_t _extraQueueStorage[EXTRA_QUEUE_SIZE * EXTRA_ITEM_SIZE];

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
TaskHandle_t apiTask = NULL;
TaskHandle_t collectorTask = NULL;
QueueHandle_t recordsQueue;
QueueHandle_t extraQueue;

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

static IRAM_ATTR void highCH4Callback()
{
  int *code = new int(EspState::HIGH_CH4);
  int ret;
  xQueueSendFromISR(extraQueue, &code, &ret);
  portYIELD_FROM_ISR(ret);
}

static IRAM_ATTR void highCO2Callback()
{
  int *code = new int(EspState::HIGH_CO2);
  int ret;
  xQueueSendFromISR(extraQueue, &code, &ret);
  portYIELD_FROM_ISR(ret);
}

void wifiDisconnetcedCallback(arduino_event_t *event)
{
  if (!esp_timer_is_active(restart_timer))
  {
    ESP_LOGI("restart_timer", "Started");
    esp_timer_start_once(restart_timer, ISSUE_TIMER_SEC * 1000000);
    vTaskSuspend(apiTask);
  }
  WiFi.begin(ssid, password);
  digitalWrite(LED_BUILTIN, LED_VALUE = !LED_VALUE);
  vTaskDelay(pdMS_TO_TICKS(1000));
}

void wifiConnetcedCallback(arduino_event_t *event)
{
  configTime(3 * 3600, 0, "europe.pool.ntp.org");
  tm inTime;
  do
  {
    getLocalTime(&inTime);
  } while (inTime.tm_year < 71);
  vTaskResume(apiTask);
  digitalWrite(LED_BUILTIN, LED_VALUE = LOW);
  esp_timer_stop(restart_timer);
  ESP_LOGI("restart_timer", "Dropped");
  ESP_LOGI("wifi", "Connected");
}

class Api
{
  HTTPClient *httpClient;
  String apiUrl = "https://airqualityapi-georga399.amvera.io/api/";

public:
  Api()
  {
  }

  int POST(String endpoint, char *payload)
  {
    httpClient = new HTTPClient();
    httpClient->begin(apiUrl + endpoint);
    httpClient->addHeader("Content-Type", "application/json");
    int code = httpClient->POST((uint8_t *)payload, strlen(payload));
    delete httpClient;

    ESP_LOGI("api", "POST query (%s). Response code: %d", endpoint.c_str(), code);
    return code;
  }
};

void api(void *)
{
  Api server;
  while (1)
  {
    int *code;
    char *payload;
    if (uxQueueMessagesWaiting(extraQueue) > 0 && xQueuePeek(extraQueue, &code, portMAX_DELAY))
    {
      StaticJsonDocument<20> extraCode;
      extraCode["statusCode"] = *code;
      payload = new char[20];
      serializeJson(extraCode, payload, 20);

      ESP_LOGI("code", "%s", payload);

      int responseCode = server.POST("rawdata/pushExtra", payload);
      delete payload;
      if (responseCode == 200 || responseCode == -11)
      {
        if (xQueueReceive(extraQueue, &code, pdMS_TO_TICKS(1)) == pdTRUE)
          ;
        delete code;
      }
      continue;
    }

    if (xQueuePeek(recordsQueue, &payload, 0))
    {
      int responseCode = server.POST("rawdata/push", payload);
      if (responseCode == 200 || responseCode == -11)
      {
        if (xQueueReceive(recordsQueue, &payload, pdMS_TO_TICKS(1)) == pdTRUE)
          ;
        delete payload;
      }
    }
  }
  vTaskDelete(NULL);
}

class Collector
{
  DynamicJsonDocument *records;

public:
  Collector()
  {
    records = new DynamicJsonDocument(10000);
  }

  void push(time_t timestamp, float *raw_records)
  {
    JsonObject record = records->createNestedObject();

    record["date"] = timestamp;
    record["temperatureC"] = raw_records[0];
    record["humidity"] = raw_records[1];
    record["cH4"] = raw_records[2];
    record["cO2"] = raw_records[3];
    delete raw_records;

    ESP_LOGI("collector", "Pushed new record. Size: %d", records->size());
  }

  size_t size()
  {
    return records->size();
  }

  void serialize(char *item)
  {
    serializeJson(*records, item, 8500);
    delete records;
    records = new DynamicJsonDocument(8500);
  }
};

void collector(void *)
{
  Sensors sensors(MQ4_PIN, MQ135_PIN, DHT_PIN, ONE_WIRE_PIN);
  sensors.begin();
  Collector cltr;
  while (1)
  {
    tm loctime;
    getLocalTime(&loctime);
    float *records = sensors.get_records();
    if (!records)
    {
      if (espState == EspState::NONE)
      {
        int *code = new int(EspState::SENSORS_ISSUE);
        xQueueSend(extraQueue, &code, portMAX_DELAY);
      }
    }
    else
    {
      if (espState == EspState::SENSORS_ISSUE)
        espState = EspState::NONE;
      cltr.push(mktime(&loctime), records);
      if (cltr.size() >= RECORDS_PACKAGE_SIZE)
      {
        char *item = new char[8500];
        cltr.serialize(item);
        xQueueSend(recordsQueue, &item, portMAX_DELAY);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(COLLECT_PERIOD_SEC * 1000));
  }
  vTaskDelete(NULL);
}

void setup()
{
  Serial.begin(9600);
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(CH4_ISR_PIN, INPUT);
  digitalWrite(LED_BUILTIN, LED_VALUE = HIGH);

  ESP_LOGI("timer", "Software timers creating");
  esp_timer_create(&restartTimerConfig, &restart_timer);
  if (restart_timer == NULL)
  {
    ESP_LOGI("timer", "Failed to create");
    ESP.restart();
  }
  ESP_LOGI("timer", "Succsessfully created");

  recordsQueue = xQueueCreateStatic(RECORDS_QUEUE_SIZE, RECORDS_ITEM_SIZE, &_recordsQueueStorage[0], &_recordsQueueBuffer);
  extraQueue = xQueueCreateStatic(EXTRA_QUEUE_SIZE, EXTRA_ITEM_SIZE, &_extraQueueStorage[0], &_extraQueueBuffer);

  xTaskCreate(api, "API", 10480, nullptr, NULL, &apiTask);
  xTaskCreate(collector, "Collector", 10480, nullptr, 5, &collectorTask);

  ESP_LOGI("wifi", "WiFi connecting...");
  WiFi.onEvent(wifiConnetcedCallback, ARDUINO_EVENT_WIFI_STA_CONNECTED);
  WiFi.onEvent(wifiDisconnetcedCallback, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  WiFi.onEvent(wifiDisconnetcedCallback, ARDUINO_EVENT_WIFI_STA_LOST_IP);
  wifiDisconnetcedCallback(nullptr);

  attachInterrupt(digitalPinToInterrupt(CH4_ISR_PIN), highCH4Callback, RISING);
  // attachInterrupt(19, highCH4Callback, RISING);  
}

void loop() {}