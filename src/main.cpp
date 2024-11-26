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
#include "PortItem.h"

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

// Create an array of ports (global variable)
PortItem ports[4];

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
 
 if (!LittleFS.begin()) {
      Serial.println("LittleFS mount failed");
      return;
  } else {
      Serial.println("Little FS Mounted Successfully");
      File root = LittleFS.open("/", "r");
      File file = root.openNextFile();

      while (file) {
          Serial.print("FILE: ");
          Serial.println(file.name());

          file = root.openNextFile();
      }
  }
  
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
void updatePortValues();

float readVoltage(int port);

float readCurrent(int port);

float readTemp(int port);

String readProtocol(int port);

void loop() {
    static unsigned long lastUpdate = 0;
    if (millis() - lastUpdate > 1000) {  // Update every second
        lastUpdate = millis();
        updatePortValues();
    }
    
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

// Add this global variable at the top with other globals
unsigned int scrollPosition = 0;
unsigned long lastScrollTime = 0;

void displayInfo() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  
  // Header with scrolling text
  display.setCursor(0, 4);
  // I hope you do not remove my name
  String headerText = "WS3518X " + String(FWVersion) + " by NguyenHungA5    ";  // Extra spaces for smooth loop
  
  // Update scroll position every 200ms
  if (millis() - lastScrollTime > 200) {
    lastScrollTime = millis();
    scrollPosition++;
    if (scrollPosition >= headerText.length()) {
      scrollPosition = 0;
    }
  }
  
  // Create scrolling effect by showing a window of the text
  String displayText = headerText.substring(scrollPosition) + headerText.substring(0, scrollPosition);
  display.println(displayText.substring(0, 21));  // Show only what fits on screen
  
  // Draw separator line
  display.drawLine(0, 13, 128, 13, WHITE);
  
  // Port information
  for (int i = 0; i < 4; i++) {
    int yPos = 18 + (i * 13);
    display.setCursor(0, yPos);
    
    // Port number and status
    display.print("P");
    display.print(i + 1);
    display.print(": ");
    
    if (ports[i].isActive) {
      // Voltage and Current
      display.print(ports[i].voltage, 1);
      display.print("V ");
      display.print(ports[i].current, 1);
      display.print("A");
      
      // Temperature on the right side
      display.print(" ");
      display.print(ports[i].temperature, 1);
      display.print("C");
    } else {
      display.print("OFF");
    }
  }
  
  display.display();
}

float lastTemperature = 0;
void checkTemperature() {
  static unsigned long lastChecktime = 0;
  unsigned long currentTime = millis();
  if (currentTime - lastChecktime < kTimeToCheckTemperature)
  {
    return;
  }

  lastChecktime = currentTime;
  lastTemperature = Thermister(analogRead(TEMPERATURE_SENSOR_PIN));
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
        StaticJsonDocument<512> doc;
        JsonArray portsArray = doc.createNestedArray("ports");
        
        for (int i = 0; i < 4; i++) {
            JsonObject port = portsArray.createNestedObject();
            port["voltage"] = ports[i].voltage;
            port["current"] = ports[i].current;
            port["temperature"] = ports[i].temperature;
            port["protocol"] = ports[i].protocol;
            port["isActive"] = ports[i].isActive;
            port["power"] = ports[i].getPower();
        }
        
        // Module temperature
        doc["moduleTemp"] = lastTemperature;
        
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });
    
    server->on("/info", HTTP_GET, [](AsyncWebServerRequest *request) {
        StaticJsonDocument<128> doc;
        doc["firmware"] = FWVersion;
        
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
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

// Example of updating port values
void updatePortValues() {
    for (int i = 0; i < 4; i++) {
        ports[i].voltage = readVoltage(i);    
        ports[i].current = readCurrent(i);    
        ports[i].temperature = readTemp(i);
        ports[i].protocol = readProtocol(i);
        ports[i].isActive = random(0, 10) > 2;  // 70% chance to be active
    }
}

float readVoltage(int port) {
    // Predefined voltage values
    const float voltages[] = {5.0, 9.0, 15.0, 19.0};
    const int numVoltages = 4;
    
    // Pick a random index from 0 to 3
    int index = random(0, numVoltages);
    return voltages[index];
}

float readCurrent(int port) {
    // Random current between 0-3A
    return random(0, 30) / 10.0;
}

float readTemp(int port) {
    // Random temperature between 0-100°C
    return random(0, 1000) / 10.0;  // Divide by 10 to get decimal places
}

String readProtocol(int port) {
    // Predefined protocols
    const String protocols[] = {"PD", "QC3", "QC4", "QC5"};
    const int numProtocols = 4;
    return protocols[port];
}