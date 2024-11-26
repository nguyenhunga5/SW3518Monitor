#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH1106.h>
#include <h1_SW35xx.h>
#include "LittleFS.h"
#include <ArduinoJson.h>
#include <ESPAsyncWiFiManager.h>
#include <OneButton.h>
#include <ESP8266mDNS.h>
#include <Ticker.h>
#include <ESPAsyncWebServer.h>
#include <ElegantOTA.h>
#include <ESP8266WiFi.h>
#include <DNSServer.h>

#include "defines.h"

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels


#define OLED_RESET LED_BUILTIN  //4
#define SCREEN_ADDRESS 0x3C ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32

#define TCAADDR 0x70

#define TEMPERATURE_SENSOR_PIN A0 // The ESP8266 pin ADC0

Adafruit_SH1106 display(OLED_RESET);

AsyncWebServer *server;
    
AsyncWiFiManager *wm = nullptr; // global wm instance
DNSServer *dns;

// Helper function for changing TCA output channel
void tcaselect(uint8_t channel) {
  if (channel > 7) return;
  
  Wire.beginTransmission(TCAADDR);
  Wire.write(1 << channel);
  Wire.endTransmission();
  Serial.print(TCAADDR);  
}

void buildServer();
void setupI2C();

void setup() {
  Serial.begin(115200);
 
  byte error, address;
  int nDevices;
 
  Serial.println("Scanning...");
  Wire.begin();
  nDevices = 0;
  for(address = 1; address < 127; address++ )
  {
    // The i2c_scanner uses the return value of
    // the Write.endTransmisstion to see if
    // a device did acknowledge to the address.
    Wire.beginTransmission(address);
    error = Wire.endTransmission();
 
    if (error == 0)
    {
      Serial.print("I2C device found at address 0x");
      if (address<16)
        Serial.print("0");
      Serial.print(address,HEX);
      Serial.println("  !");
 
      nDevices++;
    }
    else if (error==4)
    {
      // Serial.print("Unknown error at address 0x");
      // if (address<16)
      //   Serial.print("0");
      // Serial.println(address,HEX);
    }    
  }
  if (nDevices == 0)
    Serial.println("No I2C devices found\n");
  else
    Serial.println("done\n");

  pinMode(FAN_PIN, OUTPUT);

  setupI2C();

  server = new AsyncWebServer(80);
    dns = new DNSServer();
    Serial.println("Building wifi manager");
    wm = new AsyncWiFiManager(server, dns);

    wm->setSaveConfigCallback([&]() {
        buildServer();
    });

    bool res;
    res = wm->autoConnect();

    if (!res)
    {
        Serial.println("Failed to auto connect");
        // ESP.restart();
    }
    else
    {
        // if you get here you have connected to the WiFi
        Serial.println("connected...yeey :)");
        buildServer();
    }

}

void checkTemperature();

void loop() {
  MDNS.update();
  checkTemperature();
  ElegantOTA.loop();
}

// Ref: https://esp8266tutorials.blogspot.com/2016/09/esp8266-ntc-temperature-thermistor.html
unsigned int Rs = 157000;
double Vcc = 3.3;
double Thermister(int val) {
  double V_NTC = (double)val / 1024;
  double R_NTC = (Rs * V_NTC) / (Vcc - V_NTC);
  R_NTC = log(R_NTC);
  double Temp = 1 / (0.001129148 + (0.000234125 + (0.0000000876741 * R_NTC * R_NTC ))* R_NTC );
  Temp = Temp - 273.15;         
  return Temp;

}

void displayInfo();

uint lastTemperature = 0;
void checkTemperature() {
  static unsigned long lastChecktime = 0;
  unsigned long currentTime = millis();
  if (currentTime - lastChecktime < kTimeToCheckTemperature)
  {
    return;
  }

  lastChecktime = currentTime;
  lastTemperature = (int)Thermister(analogRead(TEMPERATURE_SENSOR_PIN));
  displayInfo();

  int fanSpeed = 0;
  if (lastTemperature > kMinTemperature) {
    
    float percent = (lastTemperature - kMinTemperature) / (kMaxTemperature - kMinTemperature);
    fanSpeed = min((int)(percent * 250), 250);
  }

  analogWrite(FAN_PIN, fanSpeed);
}

void onOTAStart() {
  // Log when OTA has started
  Serial.println("OTA update started!");
  // <Add your own code here>
}

unsigned long ota_progress_millis = 0;
void onOTAProgress(size_t current, size_t final) {
  // Log every 1 second
  if (millis() - ota_progress_millis > 1000) {
    ota_progress_millis = millis();
    Serial.printf("OTA Progress Current: %u bytes, Final: %u bytes\n", current, final);
  }
}

void onOTAEnd(bool success) {
  // Log when OTA has finished
  if (success) {
    Serial.println("OTA update finished successfully!");
  } else {
    Serial.println("There was an error during OTA update!");
  }
  // <Add your own code here>
}

void buildServer() {
  if (!MDNS.begin(kServerName))
    {
        Serial.println("Error setting up MDNS responder!");
    } else {
        Serial.println("mDNS responder started");
        // Add service to MDNS-SD
        MDNS.addService("http", "tcp", 80);
    }

    ElegantOTA.begin(server);

    // ElegantOTA callbacks
    ElegantOTA.onStart(onOTAStart);
    ElegantOTA.onProgress(onOTAProgress);
    ElegantOTA.onEnd(onOTAEnd);

    server->on("/", HTTP_GET, [](AsyncWebServerRequest *request)
               { 
              Serial.println("Request index.html on /");
              request->send(LittleFS, "/index.html", "text/html"); });

  
    server->on("/monitor", HTTP_GET, [&](AsyncWebServerRequest *request) {
        request->send(200, "text/plain", String(lastTemperature));
    });
    
    server->begin();
    Serial.println("HTTP server started");
}

void setupI2C() {
  // Start I2C communication with the Multiplexer
  Wire.begin();
  
  // Init OLED display on bus number 1
  display.begin(SH1106_SWITCHCAPVCC, SCREEN_ADDRESS); 
  
  // Write to OLED on bus number 0
  // tcaselect(0);
  

}

void displayInfo() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);

  display.print("WS3518X monitor ");
  display.println(FWVersion);
  display.setCursor(0, 40);
  display.println(lastTemperature);
  display.display();

}