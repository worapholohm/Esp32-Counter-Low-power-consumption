#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <SD.h>
#include "esp_wifi.h"

const int irSensorPin = 34; // IR sensor pin
volatile int counter = 0;
volatile unsigned long currentEpochTime = 0; // Epoch time variable
const int timeZoneOffset = 7 * 3600; 
volatile unsigned long currentTime =0;
const int chipSelect = 5; // SD card module pin
const char* ssid = "";
const char* password = "";
const char* timeServer = "http://worldtimeapi.org/api/timezone/Asia/Bangkok"; // Timezone API
const int frequency_write = 60; // seconds
const int frequency_send = 300; // seconds
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
int lastIrValue = 0;

volatile int objectCount = 0; // Object count variable

const char* googleScriptId = ""; // Google Script ID

bool updateTime() {
  if (WiFi.status() == WL_CONNECTED) {
    delay(5000);
    HTTPClient http;
    http.begin(timeServer);
    int httpResponseCode = http.GET();
    if (httpResponseCode == 200) {
      digitalWrite(32, HIGH);
      String payload = http.getString();
      int timeIndex = payload.indexOf("unixtime") + 10;
      int endIndex = payload.indexOf(",", timeIndex);
      currentEpochTime = payload.substring(timeIndex, endIndex).toInt();
      //Serial.print("Fetch Time: ");
      currentTime = currentEpochTime +  timeZoneOffset;
      time_t rawtime = currentTime;
      struct tm *ti;
      ti = localtime(&rawtime);
      char buffer[80];
      //Serial.println(currentEpochTime);
      strftime(buffer, sizeof(buffer), "%Y-%m-%d>%H:%M:%S", ti);
      Serial.print("Fetch Time: ");
      Serial.println(buffer);
      File dataFile = SD.open("/data.csv", FILE_APPEND);
      dataFile.print(buffer);
      dataFile.print(",");
      dataFile.close();
      http.end();
      delay(200);
      digitalWrite(32, LOW);
      return true; // Successfully updated time
    } else {
      Serial.print("Error on getting time: ");
      Serial.println(httpResponseCode);
    }
    http.end();
  } else {
    Serial.println("WiFi Disconnected");
  }
  return false; // Failed to update time
}

int sendToGoogleSheets(String data) {
  int httpResponseCode = -1;
  
  if (WiFi.status() == WL_CONNECTED) {
    delay(5000);
    HTTPClient http;
    String url = "https://script.google.com/macros/s/" + String(googleScriptId) + "/exec?data=" + data;
    
      http.begin(url);
      Serial.println(url);
      httpResponseCode = http.GET();
      
      if (httpResponseCode != -1) { // Success
        String response = http.getString();
        Serial.println(httpResponseCode);
        Serial.print("response:");
        Serial.println(response);
        SD.remove("/data.csv"); // Delete file after successful send
        Serial.println("Data file removed after successful send");
      } else {
        Serial.print("Error on sending GET");
        Serial.println(httpResponseCode);
        delay(5000); // Wait 5 seconds before retrying
      }
      
      http.end();
    
    
    WiFi.disconnect(true); // Disconnect WiFi
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM); // Turn off WiFi hardware
    
    return httpResponseCode;
  } else {
    Serial.println("WiFi Disconnected");
    return -1; // Indicate WiFi disconnection
  }
}

void countTask(void *pvParameters) {
  while (true) {
    counter++;
    vTaskDelay(1000 / portTICK_PERIOD_MS); // 1 second delay
  }
}

void connectToWiFi() {
  Serial.println("Connecting to WiFi...");
  WiFi.mode(WIFI_STA); // Enable WiFi hardware
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED && counter <=20) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("Connected to WiFi");
  esp_wifi_set_max_tx_power(82);
  
}

void irSensorTask(void *pvParameters) {
  while (true) {
    if (counter >= frequency_send) {
      counter = 0; // Reset counter
      connectToWiFi();
      updateTime();
      File dataFile = SD.open("/data.csv");
      if (dataFile && WiFi.status() == WL_CONNECTED ) {
        String data = dataFile.readStringUntil('\n');
        sendToGoogleSheets(data);
      } else {
        Serial.println("Error opening data file");
      }dataFile.close();
    }
    vTaskDelay(50 / portTICK_PERIOD_MS); // 50 ms delay
  }
}
void writeDataToSD(void *pvParameters) {
  while (true) {
    
    if (counter % frequency_write == 0) {
      Serial.print("Counter: ");
      Serial.println(counter);
      // Write objectCount to SD card as CSV with two columns: timestamp, objectCount
      File dataFile = SD.open("/data.csv", FILE_APPEND);
      if (dataFile) {
        dataFile.print(objectCount);
        dataFile.print(",");
        dataFile.close();
        Serial.println("Data written to SD card");
      } else {
        Serial.println("Error opening data file");
      }
      Serial.print("Object Count: ");
      Serial.println(objectCount);
      objectCount = 0;
      vTaskDelay(45000 / portTICK_PERIOD_MS);
    }
    vTaskDelay(800 / portTICK_PERIOD_MS);
  }
}
void setup() {
  Serial.begin(115200);
  if (!SD.begin(chipSelect)) {
    Serial.println("Card failed, or not present");
    return; 
  }
  Serial.println("SD card initialized.");
  pinMode(32, OUTPUT);
  connectToWiFi();
  updateTime();
  WiFi.disconnect(true); 
  esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
  pinMode(irSensorPin, INPUT);
  
  xTaskCreate(countTask, "Count Task", 4096, NULL, 1, NULL);
  xTaskCreate(writeDataToSD, "writeSD Task", 4096, NULL, 1, NULL);
  xTaskCreate(irSensorTask, "IR Sensor Task", 4096, NULL, 1, NULL);
}

void loop() {
  int irValue = digitalRead(irSensorPin);
  if (irValue == 1 && lastIrValue == 0) { // Detect change from 0 to 1
    objectCount++;
  }
  lastIrValue = irValue; 
  delay(50);
}
