#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH1106.h>
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
#include <WebSocketsServer.h>

#include "defines.h"
#include "PortItem.h"
#include "Config.h"

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels

#define OLED_RESET LED_BUILTIN // 4
#define SCREEN_ADDRESS 0x3C    ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32

#define TCAADDR 0x70

#define TEMPERATURE_SENSOR_PIN A0 // The ESP8266 pin ADC0

Adafruit_SH1106 display(OLED_RESET);

AsyncWebServer *server;
// Create a WebSocket object
WebSocketsServer webSocket = WebSocketsServer(81);

AsyncWiFiManager *wm = nullptr; // global wm instance
DNSServer *dns;

// Create an array of ports (global variable)
std::vector<PortItem *> ports;

// Config
Config *config = nullptr;

// Is updating
bool isUpdating = false;

int fanSpeed = 0;

// Helper function for changing TCA output channel
void tcaselect(uint8_t channel)
{
  if (channel > 7)
    return;

  Wire.beginTransmission(TCAADDR);
  Wire.write(1 << channel);
  Wire.endTransmission();
}

void buildServer();
void setupI2C();
void updateSwitch();

void setup()
{
  Serial.begin(115200);

  if (!LittleFS.begin())
  {
    Serial.println("LittleFS mount failed");
    return;
  }
  else
  {
    Serial.println("Little FS Mounted Successfully");
    File root = LittleFS.open("/", "r");
    File file = root.openNextFile();

    while (file)
    {
      Serial.print("FILE: ");
      Serial.println(file.name());

      file = root.openNextFile();
    }
  }

  pinMode(SWITCH_PIN, OUTPUT);
  pinMode(SWITCH_BUTTON, INPUT);

  config = new Config();
  // Force update switch state
  updateSwitch();

  byte error, address;
  int nDevices;

  Serial.println("Scanning...");
  Wire.begin();
  nDevices = 0;
  for (address = 1; address < 127; address++)
  {
    // The i2c_scanner uses the return value of
    // the Write.endTransmisstion to see if
    // a device did acknowledge to the address.
    Wire.beginTransmission(address);
    error = Wire.endTransmission();

    if (error == 0)
    {
      Serial.print("I2C device found at address 0x");
      if (address < 16)
        Serial.print("0");
      Serial.print(address, HEX);
      Serial.println("  !");

      nDevices++;
    }
    else if (error == 4)
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

  wm->setSaveConfigCallback([&]()
                            { buildServer(); });

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

void loop()
{
  static unsigned long lastUpdate = 0;
  if (millis() - lastUpdate > 1000)
  { // Update every second
    lastUpdate = millis();
    updatePortValues();
  }

  MDNS.update();
  checkTemperature();
  ElegantOTA.loop();
  webSocket.loop();
}

// Ref: https://esp8266tutorials.blogspot.com/2016/09/esp8266-ntc-temperature-thermistor.html
unsigned int Rs = 157000;
double Vcc = 3.3;
double Thermister(int val)
{
  double V_NTC = (double)val / 1024;
  double R_NTC = (Rs * V_NTC) / (Vcc - V_NTC);
  R_NTC = log(R_NTC);
  double Temp = 1 / (0.001129148 + (0.000234125 + (0.0000000876741 * R_NTC * R_NTC)) * R_NTC);
  Temp = Temp - 273.15;
  return Temp;
}

// Add this global variable at the top with other globals
unsigned int scrollPosition = 0;
unsigned long lastScrollTime = 0;
uint8_t pageTime = 0;
void displayInfo()
{
  if (isUpdating) {
    return;
  }

  tcaselect(0);
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);

  // Header with scrolling text
  display.setCursor(0, 4);
  // I hope you do not remove my name
  String headerText = "WS3518X " + String(FWVersion) + " by NguyenHungA5 IP: " + WiFi.localIP().toString() + "    "; // Extra spaces for smooth loop

  // Update scroll position every 200ms
  if (millis() - lastScrollTime > 200)
  {
    lastScrollTime = millis();
    scrollPosition++;
    if (scrollPosition >= headerText.length())
    {
      scrollPosition = 0;
    }
  }

  // Create scrolling effect by showing a window of the text
  String displayText = headerText.substring(scrollPosition) + headerText.substring(0, scrollPosition);
  display.println(displayText.substring(0, 21)); // Show only what fits on screen

  // Draw separator line
  display.drawLine(0, 13, 128, 13, WHITE);

  // Port information
  for (int i = 0; i < 4; i++)
  {
    int yPos = 18 + (i * 13);

    // Port number (column 0)
    display.setCursor(0, yPos);
    display.print("P");
    display.print(i + 1);

    if (ports[i]->isActive)
    {
      // Page 1: Voltage, Current, Power
      if (pageTime < 30)
      { // Show first page for 30 cycles
        // Voltage (column 20)
        display.setCursor(18, yPos);
        display.print(ports[i]->voltage, 1);
        display.print("V");

        // Current (column 55)
        display.setCursor(55, yPos);
        display.print(ports[i]->current, 1);
        display.print("A");

        // Power (column 90)
        display.setCursor(90, yPos);
        display.print(ports[i]->getPower(), 1);
        display.print("W");
      }
      // Page 2: Protocol and Temperature
      else
      { // Show second page for exactly 5 cycles
        // Create a formatted string for the protocol
        String protocolText = ports[i]->protocol;
        String displayText;

        // Check if the protocol text is too long
        if (protocolText.length() > 7)
        { // Assuming 7 characters fit in the display
          protocolText += "    "; // Extra spaces for smooth loop
          // Scroll the text
          static unsigned long lastScrollTime = 0;
          static unsigned int scrollPosition = 0;
          if (millis() - lastScrollTime > 200)
          { // Update every 200ms
            lastScrollTime = millis();
            scrollPosition++;
            if (scrollPosition >= protocolText.length())
            {
              scrollPosition = 0;
            }
          }
          // Create the scrolling effect
          protocolText = protocolText.substring(scrollPosition) + protocolText.substring(0, scrollPosition);
          displayText = protocolText.substring(0, 7);
        }
        else
        {
          displayText = protocolText; // Use the full text if it's short enough
        }

        // Protocol (column 20)
        display.setCursor(18, yPos);
        display.print(displayText);

        // Temperature (column 90)
        display.setCursor(70, yPos);
        display.print("Vin ");
        display.print(ports[i]->inputVoltage, 1);
        display.print("V");
      }
      pageTime++;
      // Show second page for 10 cycles
      if (pageTime >= 40)
      {
        pageTime = 0;
      }
    }
    else
    {
      // OFF status (column 20)
      display.setCursor(18, yPos);
      display.print("OFF");
    }
  }

  display.display();
}

float lastTemperature = 0;
void checkTemperature()
{
  static unsigned long lastChecktime = 0;
  unsigned long currentTime = millis();
  if (currentTime - lastChecktime < kTimeToCheckTemperature)
  {
    return;
  }

  lastChecktime = currentTime;
  lastTemperature = Thermister(analogRead(TEMPERATURE_SENSOR_PIN));
  displayInfo();

  float percent = (lastTemperature - kMinTemperature) / (kMaxTemperature - kMinTemperature);
    float totalPower = 0;
    if (ports.size() == 4)
    {
      for (size_t i = 0; i < 4; i++)
      {
        if (ports[i]->isActive)
        {
          totalPower += ports[i]->getPower();
        }
      }
    } 
    
    float pPercent = (totalPower - kMinPower) / (kMaxPower - kMinPower); // kMaxPower is power max per port, so we just estimate total power

  if (lastTemperature > kMinTemperature || pPercent > percent)
  {
    percent = max(pPercent, percent);

    fanSpeed = min((int)(percent * 250), 250);
  }

  analogWrite(FAN_PIN, fanSpeed);
}

void onOTAStart()
{
  // Log when OTA has started
  Serial.println("OTA update started!");
  isUpdating = true;
}

unsigned long ota_progress_millis = 0;
void onOTAProgress(size_t current, size_t final)
{
  // Log every 1 second
  if (millis() - ota_progress_millis > 1000)
  {
    ota_progress_millis = millis();
    Serial.printf("OTA Progress Current: %u bytes, Final: %u bytes\n", current, final);

    tcaselect(0);
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(WHITE);
    int startY = 10;
    display.setCursor(0, startY);
    display.println("Updating...");

    int progressWidth = 100;
    int progressHeight = 10;
    int progressX = (SCREEN_WIDTH - progressWidth) / 2;
    int progressY = startY + 30;
    int progressValue = map(current, 0, final, 0, progressWidth);

    String processText = String(progressValue) + "%";
    display.setCursor((SCREEN_WIDTH - processText.length()) / 2, startY + 20);
    display.println(processText);

    display.fillRect(progressX, progressY, progressWidth, progressHeight, BLACK);
    display.fillRect(progressX, progressY, progressValue, progressHeight, WHITE);
    display.display();
  }
}

void onOTAEnd(bool success)
{
  // Log when OTA has finished
  if (success)
  {
    Serial.println("OTA update finished successfully!");
  }
  else
  {
    Serial.println("There was an error during OTA update!");
  }
  // <Add your own code here>
}

File uploadFile;
// Handle large file upload
void handleTextUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final);
void buildServer()
{
  if (!MDNS.begin(kServerName))
  {
    Serial.println("Error setting up MDNS responder!");
  }
  else
  {
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

  server->on("/log", HTTP_GET, [](AsyncWebServerRequest *request)
             { 
              Serial.println("Request log.html on /");
              request->send(LittleFS, "/log.html", "text/html"); });

  server->on("/monitor", HTTP_GET, [&](AsyncWebServerRequest *request)
             {
        StaticJsonDocument<640> doc;
        JsonArray portsArray = doc.createNestedArray("ports");
        float inputVoltage = 0.0;
        for (int i = 0; i < 4; i++)
        {
          JsonObject port = portsArray.createNestedObject();
          port["voltage"] = ports[i]->voltage;
          port["current"] = ports[i]->current;
          port["temperature"] = ports[i]->temperature;
          port["protocol"] = ports[i]->protocol;
          port["isActive"] = ports[i]->isActive;
          port["power"] = ports[i]->getPower();
          port["totalPower"] = config->totalEnergyOf(i);
          if (ports[i]->isActive) {
            inputVoltage = ports[i]->inputVoltage;
          }
        }

        // Module temperature
        doc["moduleTemp"] = lastTemperature;
        
        // Module Input Voltage
        // Because all port using same input source, so we just need first active port
        doc["inputVoltage"] = inputVoltage;
        
        // State of module
        doc["state"] = config->getState();

        doc["fanSpeed"] = fanSpeed;

        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response); });

  server->on("/info", HTTP_GET, [](AsyncWebServerRequest *request)
             {
        StaticJsonDocument<128> doc;
        doc["firmware"] = FWVersion;
        
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response); });

  server->on("/state", HTTP_GET, [](AsyncWebServerRequest *request)
             {
        StaticJsonDocument<128> doc;
        doc["state"] = config->getState();

        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
  });

  server->on("/state", HTTP_POST, [](AsyncWebServerRequest *request)
             {
        String state = request->arg("state");
        if (state == "on") {
          config->setState(true);
        } else if (state == "off") {
          config->setState(false);
        }

        updateSwitch();
        request->send(200, "application/json", "State updated");
  });

  server->on("/reset_energy", HTTP_POST, [](AsyncWebServerRequest *request)
             {
        int portIndex = request->arg("port").toInt();
        config->resetTotalEnergy(portIndex - 1);
        logMessage("Reset total energy of port " + String(portIndex));
        request->send(200, "application/json", "State updated"); });

  server->on("/edit", HTTP_GET, [](AsyncWebServerRequest *request)
             {
        request->send(LittleFS, "/edit.html", "text/html");
  });

  server->on("/edit_content", HTTP_GET, [](AsyncWebServerRequest *request)
             { request->send(LittleFS, "/index.html", "text/html"); });

  server->on("/edit", HTTP_POST, [](AsyncWebServerRequest *request)
             { request->send(200, "text/plain", "File written successfully"); }, handleTextUpload);

  server->begin();
  Serial.println("HTTP server started");

  webSocket.begin();
}

void setupI2C()
{
  // Start I2C communication with the Multiplexer
  Wire.begin();

  // Init OLED display on bus number 0
  tcaselect(0);
  display.begin(SH1106_SWITCHCAPVCC, SCREEN_ADDRESS);

  for (uint i = 0; i < 4; i++)
  {
    tcaselect(i + 1);
    PortItem *item = new PortItem();
    item->update();
    ports.push_back(item);
  }
}

// Updating port values
float powerInterval = 1000.0 / 3600000.0;
void updatePortValues()
{
  for (int i = 0; i < 4; i++)
  {
    tcaselect(i + 1);
    Wire.beginTransmission(SCREEN_ADDRESS); // WS3518 have same address with OLED
    if (Wire.endTransmission() == 0)
    {
      logMessage("\nPort " + String(i + 1) + " is active");
      ports[i]->update();
      ports[i]->isActive = true;
      float power = ports[i]->getPower();
      if (power > 0.0)
      {
        float enegy = power * powerInterval;
        config->updateTotalEnergy(enegy, i);
      }
    }
    else
    {
      ports[i]->isActive = false;
      logMessage("Port " + String(i + 1) + " is deactive");
    }
  }
}

void notifyClients(String data)
{
  webSocket.broadcastTXT(data);
}

void logMessage(const String &message)
{
  notifyClients(message); // Send message to all connected WebSocket clients
}

void updateSwitch() {
  digitalWrite(SWITCH_PIN, config->getState());
  String stateStr = config->getState() ? "On" : "Off";
  logMessage("Update state to => " + stateStr);
}

// Handle large file upload
void handleTextUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final)
{
    if (index == 0)
    {
      // First chunk of data, open the file
      logMessage("Starting text upload");
      if (LittleFS.exists("/index.html"))
      {
        LittleFS.remove("/index.html"); // Remove the existing file if it exists
      }
      uploadFile = LittleFS.open("/index.html", "w");
      if (!uploadFile)
      {
        logMessage("Failed to open file for writing");
        request->send(500, "text/plain", "Failed to open file for writing");
        return;
      }
    }

    // Write the current chunk to the file
    if (uploadFile)
    {
      uploadFile.write(data, len);
      logMessage("Written " + String(len) + " bytes");
    }
    else
    {
      logMessage("File not open during write");
    }

    if (final)
    {
      // All chunks received
      logMessage("Html upload complete");
      if (uploadFile)
      {
        uploadFile.close();
      }
    }
  }