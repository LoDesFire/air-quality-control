#include <Arduino.h>

#include <MQ4.h>
#include <MQ135.h>
#include <DHT.h>
#include <DHT_U.h>
#include <Adafruit_Sensor.h>

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

#define DHT_PIN 18
#define MQ135_PIN 35
#define MQ4_PIN 34

const char *ssid = "define (;;) ever \\n forever";
const char *password = "bsuir_the_best";

class Time
{
  static time_t getTime()
  {
    HTTPClient http;
    http.begin("http://worldtimeapi.org/api/timezone/Europe/Minsk");
    int httpResponseCode = http.GET();
    String payload = "{}";
    if (httpResponseCode == 200)
    {
      payload = http.getString();
    }
    else
    {
      Serial.print("Error code: ");
      Serial.println(httpResponseCode);
    }
    http.~HTTPClient();
    DynamicJsonDocument doc(400);
    deserializeJson(doc, payload);
    if (doc.containsKey("unixtime"))
    {
      time_t time = doc["unixtime"].as<time_t>() + 3600 * 3;
      return time;
    }
    return 0;
  }

public:
  static String getTimeString()
  {
    time_t *cur_time = new time_t(getTime());
    tm *loctime = localtime(cur_time);
    char strtime[32];
    strftime(strtime, 32, "%FT%T", loctime);
    delete cur_time;
    return String(strtime) + ".000Z";
  }
};

HTTPClient http;
MQ4 mq4(MQ4_PIN);
MQ135 mq135(MQ135_PIN);
DHT_Unified dht(DHT_PIN, DHT11);
int hum, t;
uint32_t delayMS;

void setup()
{
  Serial.begin(9600);
  // WiFi.begin(ssid, password);
  dht.begin();
  mq4.begin();
  mq135.begin();

  // while (WiFi.status() != WL_CONNECTED)
  // {
  //   delay(500);
  //   Serial.println("Connecting to WiFi..");
  // }

  // if (WiFi.status() != WL_CONNECTED)
  // {
  //   Serial.println("Non Connecting to WiFi..");
  // }
  // else
  // {
  //   Serial.println("\nWiFi connected");
  //   Serial.println("IP address: ");
  //   Serial.println(WiFi.localIP());
  // }

  sensor_t sensor;
  dht.temperature().getSensor(&sensor);
  delayMS = sensor.min_delay / 1000;
}

void loop()
{
  delay(delayMS);

  sensors_event_t event;
  dht.temperature().getEvent(&event);
  if (isnan(event.temperature)) {
    Serial.println(F("Error reading temperature!"));
  }
  else {
    Serial.print(F("Temperature: "));
    t = event.temperature;
    Serial.print(event.temperature);
    Serial.println(F("°C"));
  }
  // Get humidity event and print its value.
  dht.humidity().getEvent(&event);
  if (isnan(event.relative_humidity)) {
    Serial.println(F("Error reading humidity!"));
  }
  else {
    Serial.print(F("Humidity: "));
    hum = event.relative_humidity;
    Serial.print(hum);
    Serial.println(F("%"));
  }
  Serial.printf("MQ135: %f, MQ4: %f\n", mq135.getValue(t, hum),  mq4.getValue(t, hum));
}

// void loop()
// {x
//   // StaticJsonDocument<500> doc;
//   // JsonObject jObj = doc.createNestedObject();

//   // jObj["data"] = Time::getTimeString();
//   // jObj["temperatureC"] = 200*rand() % 100;
//   // jObj["humidity"] = 123123 * rand() % 123;
//   // jObj["cH4"] = 178 * rand() % 7;
//   // jObj["cO2"] =  178 * rand() % 97;

//   // String payload;
//   // serializeJson(doc, payload);
//   // int httpCode = http.POST(payload);
//   // Serial.print(payload);

//   Serial.printf("MQ-135: %.6f\n", mq135.getValue());
//   delay(1000);
// }

// [
//   {
//     "date": "2023-10-20T18:36:39",
//     "temperatureC": 0,
//     "humidity": 0,
//     "cO2": 0,
//     "cH4": 0
//   }
// ]