#include <Arduino.h>
#include "WiFi.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>

const char* ssid = "define (;;) ever \\n forever";
const char* password =  "bsuir_the_best";
byte tries = 10;
HTTPClient http;

void setup() {
  Serial.begin(9600);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.println("Connecting to WiFi..");
  }

  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("Non Connecting to WiFi..");
  }
  else
  {
    // Иначе удалось подключиться отправляем сообщение
    // о подключении и выводим адрес IP
    Serial.println("");
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());

    http.begin("https://airqualityapi-georga399.amvera.io/api/rawdata/push"); //HTTP
    http.addHeader("Content-Type", "application/json");
    int httpCode = http.POST("");
    Serial.print(httpCode);
  }

  StaticJsonDocument<20> document;
  JsonArray array = document.createNestedArray();
  JsonObject obj = array.createNestedObject() 
  // JsonObject obj = JsonObject();
  // obj.
  // array.add()
}

void loop() {

}