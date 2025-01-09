#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include "LittleFS.h"
#include <ArduinoJson.h>
#include <ESPAsyncWiFiManager.h>
#include <ESP8266mDNS.h>
#include <Ticker.h>
#include <ESPAsyncWebServer.h>
#include <ElegantOTA.h>
#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <WebSocketsServer.h>
#include <memory>
#include <vector>

#include "defines.h"
#include "PortItem.h"
#include "Config.h"
#include "Emoticons.hpp"

constexpr int SCREEN_WIDTH = 128; // OLED display width, in pixels
constexpr int SCREEN_HEIGHT = 64; // OLED display height, in pixels

constexpr int TCAADDR = 0x70;

constexpr int TEMPERATURE_SENSOR_PIN = A0; // The ESP8266 pin ADC0
constexpr int SCREEN_ADDRESS = 0x3C;

#ifdef OLED_SSD1306
#include <Adafruit_SSD1306.h>
#define OLED_RESET -1 // Reset pin # (or -1 if sharing Arduino reset pin)
#define PX_COLOR_WHITE SSD1306_WHITE
#define PX_COLOR_BLACK SSD1306_BLACK
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
#else
#include <Adafruit_SH110x.h>
constexpr int OLED_RESET = LED_BUILTIN; // 4
#define PX_COLOR_WHITE SH110X_WHITE
#define PX_COLOR_BLACK SH110X_BLACK
Adafruit_SH1106G display = Adafruit_SH1106G(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
#endif

std::unique_ptr<AsyncWebServer> server = nullptr;
// Create a WebSocket object
WebSocketsServer webSocket(81);

std::unique_ptr<AsyncWiFiManager> wm = nullptr; // global wm instance
std::unique_ptr<DNSServer> dns = nullptr;

// Create an array of ports (global variable)
std::vector<std::unique_ptr<PortItem>> ports;

// Create an array of emoticons
std::unique_ptr<Emoticons> emoticons = nullptr;

// Config
std::unique_ptr<Config> config = nullptr;
bool needUpdateState = false;

// Is updating
bool isUpdating = false;
bool isUpdateSuccess = false;
int progressValue = 0;
int progressWidth = 100;
int progressHeight = 10;
char updatingTitle[12] = "Updating...";
void drawUpdateProgress();

int fanSpeed = 0;
float lastTemperature = 0;
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
void setupFileManagement();
void setupI2C();
void updateSwitch();

bool drawFunnyEmotion();
void dimScreen();

void buildWelcome();

/**
 * @brief Setup function for initializing the system.
 * 
 * This function performs the following tasks:
 * - Initializes serial communication at 115200 baud rate.
 * - Mounts the LittleFS filesystem and lists all files in the root directory.
 * - Configures pin modes for switch and button.
 * - Initializes the configuration object and updates the switch state.
 * - Sets up ElegantOTA with auto-reboot disabled.
 * - Scans for I2C devices and prints their addresses.
 * - Configures the fan pin mode.
 * - Calls setupI2C() to initialize I2C communication.
 * - Adds default headers for CORS to the HTTP server.
 * - Initializes the asynchronous web server and DNS server.
 * - Sets up the WiFi manager with callbacks for AP mode and saving configuration.
 * - Attempts to auto-connect to WiFi using the configured server name.
 * - Initializes the emoticons object.
 * - Sets up button click and long press callbacks to update switch state and reset WiFi settings, respectively.
 */
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

    root.close();
  }

  pinMode(SWITCH_PIN, OUTPUT);
  pinMode(SWITCH_BUTTON, INPUT);

  config = std::make_unique<Config>();
  
  ElegantOTA.setAutoReboot(false);
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

  // Force update switch state
  updateSwitch();

  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "POST, GET, OPTIONS");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Content-Type");
  server = std::make_unique<AsyncWebServer>(80);
  dns = std::make_unique<DNSServer>();
  Serial.println("Building wifi manager");
  wm = std::make_unique<AsyncWiFiManager>(server.get(), dns.get());

  wm->setAPCallback([](AsyncWiFiManager *wifiConfig) {
    tcaselect(0);
    display.clearDisplay();
    display.setCursor(4, 10);
    display.setTextSize(1);
    display.setTextColor(PX_COLOR_WHITE);
    display.println("WiFi Setup Mode");
    display.println("Connect to:");
    display.println(wifiConfig->getConfigPortalSSID());
    display.println("to configure WiFi");
    display.display();
    delay(200); });

  bool res;
  res = wm->autoConnect(config->getServerName().c_str());

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

  emoticons = std::make_unique<Emoticons>();
  config->buttonClickedCallback = []
  {
    needUpdateState = true;
  };

  config->buttonLongPressedCallback = [] {
    logMessage("Button Long Pressed!");
    wm->resetSettings();
    delay(200);
    ESP.reset();
  };

  buildWelcome();
}

void checkTemperature();
void updatePortValues();

void debugMemory();

/**
 * @brief The main loop of the program.
 *
 * This function is called repeatedly by the Arduino framework.
 *
 * It does the following:
 * - Updates the port values every 1000ms (1 second).
 * - Updates the MDNS service.
 * - Checks the temperature.
 * - Checks for OTA updates.
 * - Checks for WebSocket messages.
 */
void loop()
{
  config->loop();
  static unsigned long lastUpdate = 0;
  if (millis() - lastUpdate > 1000)
  { // Update every second
    lastUpdate = millis();
    drawUpdateProgress();
    updatePortValues();
    debugMemory();
  }

  if (needUpdateState)
  {
    needUpdateState = false;
    updateSwitch();
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
  if (isUpdating || !digitalRead(SWITCH_BUTTON))
  {
    return;
  }

  tcaselect(0);
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(PX_COLOR_WHITE);

  // Header with scrolling text
  display.setCursor(0, 4);
  // I hope you do not remove my name
  String headerText = "SW3518X " + String(FWVersion);
  headerText += " by NguyenHungA5 IP: " + WiFi.localIP().toString();
  headerText += " | http://" + config->getServerName() + ".local";
  headerText += "    "; // Extra spaces for smooth loop

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
  display.print(displayText.substring(0, 15)); // Show only what fits on screen
  display.setCursor(86, 4);
  String tempStr = " |" + String(lastTemperature, 1) + "C";
  display.println(tempStr);

  // Draw separator line
  display.drawLine(0, 13, 128, 13, PX_COLOR_WHITE);

  bool allPortsIdle = true;
  for (const auto &port : ports)
  {
    if (port->current > 0.0)
    {
      allPortsIdle = false;
      break;
    }
  }

  if (allPortsIdle && drawFunnyEmotion())
  {
    return;
  }

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
        {                         // Assuming 7 characters fit in the display
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

void checkTemperature()
{
  static unsigned long lastChecktime = 0;
  static unsigned long lastChangeFanSpeed = 0;
  unsigned long currentTime = millis();
  if (currentTime - lastChecktime < kTimeToCheckTemperature)
  {
    return;
  }

  lastChecktime = currentTime;
  lastTemperature = Thermister(analogRead(TEMPERATURE_SENSOR_PIN));
  displayInfo();

  if (currentTime - lastChangeFanSpeed < kTimeToChangeFan)
  {
    return;
  }

  lastChangeFanSpeed = currentTime;

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
    if (percent < 0.05)
    {
      percent = 0.0;
    }
    fanSpeed = min((int)(percent * 250), 250);
  }
  else
  {
    fanSpeed = 0;
  }

  analogWrite(FAN_PIN, fanSpeed);
}

void onOTAStart()
{
  // Log when OTA has started
  Serial.println("OTA update started!");
  isUpdating = true;
  isUpdateSuccess = false;
}

unsigned long ota_progress_millis = 0;
void onOTAProgress(size_t current, size_t final)
{
  // Log every 1 second
  if (millis() - ota_progress_millis > 1000)
  {
    ota_progress_millis = millis();
    progressValue = map(current, 0, final, 0, progressWidth);

    debugMemory();
  }
}

void onOTAEnd(bool success)
{
  // Log when OTA has finished
  if (success)
  {
    Serial.println("OTA update finished successfully!");
    isUpdateSuccess = true;
  }
  else
  {
    Serial.println("There was an error during OTA update!");
    isUpdating = false;
  }
  // <Add your own code here>
}

File uploadFile;
// Handle large file upload
void handleTextUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final);
void buildServer()
{
  if (!MDNS.begin(config->getServerName()))
  {
    Serial.println("Error setting up MDNS responder!");
  }
  else
  {
    logMessage("mDNS responder started", true);
    String message = "You can access the web interface at http://" + config->getServerName() + ".local or http://" + WiFi.localIP().toString();
    logMessage(message, true);
    // Add service to MDNS-SD
    MDNS.addService("http", "tcp", 80);
  }

  ElegantOTA.begin(server.get());

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
        doc["serverName"] = config->getServerName();

        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response); });

  server->on("/state", HTTP_GET, [](AsyncWebServerRequest *request)
             {
        StaticJsonDocument<128> doc;
        doc["state"] = config->getState();

        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response); });

  server->on("/state", HTTP_POST, [](AsyncWebServerRequest *request)
             {
        String state = request->arg("state");
        if (state == "on") {
          config->setState(true);
        } else if (state == "off") {
          config->setState(false);
        }

        needUpdateState = true;
        request->send(200, "application/json", "State updated"); });

  server->on("/reset_energy", HTTP_POST, [](AsyncWebServerRequest *request)
             {
        int portIndex = request->arg("port").toInt();
        config->resetTotalEnergy(portIndex - 1);
        logMessage("Reset total energy of port " + String(portIndex));
        request->send(200, "application/json", "State updated"); });

  server->on("/edit", HTTP_GET, [](AsyncWebServerRequest *request)
             { request->send(LittleFS, "/edit.html", "text/html"); });

  server->on("/edit_content", HTTP_GET, [](AsyncWebServerRequest *request)
             { request->send(LittleFS, "/index.html", "text/html"); });

  server->on("/edit", HTTP_POST, [](AsyncWebServerRequest *request)
             { request->send(200, "text/plain", "File written successfully"); }, handleTextUpload);
  emoticons->addListener(server.get());

  /*
    // API to change server name
    server->on("/serverName", HTTP_POST, [](AsyncWebServerRequest *request)
               {
                 if (request->hasArg("serverName"))
                 {
                   String newServerName = request->arg("serverName");
                   config->setServerName(newServerName);
                   MDNS.begin(newServerName.c_str());
                   request->send(200, "application/json", "{\"status\":\"success\"}");
                 }
                 else
                 {
                   request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing serverName parameter\"}");
                 }
               });

    // Serve the web form
    server->on("/serverName", HTTP_GET, [](AsyncWebServerRequest *request)
               {
                StaticJsonDocument<128> doc;
                doc["serverName"] = config->getServerName();

                String response;
                serializeJson(doc, response);
                request->send(200, "application/json", response);
               });
  */

  setupFileManagement();

      // Start server
  server -> begin();

  Serial.println("HTTP server started");

  webSocket.begin();
}

void setupDisplay() {
  // Init OLED display on bus number 0
  tcaselect(0);
#ifdef OLED_SSD1306
    display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS);
#else
    display.begin(SCREEN_ADDRESS, true);
#endif
}

void setupI2C()
{
  // Start I2C communication with the Multiplexer
  Wire.begin();

  setupDisplay();
  
  for (uint i = 0; i < 4; i++)
  {
    tcaselect(i + 1);
    auto item = std::make_unique<PortItem>();
    item->update();
    ports.push_back(std::move(item));
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

void logMessage(const String &message, bool sendToSerial)
{
  notifyClients(message); // Send message to all connected WebSocket clients
  if (sendToSerial)
  {
    Serial.println(message); // Send message to the serial monitor
  }
}

void updateSwitch()
{

  bool needDim = !config->getState();

  if (needDim) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(PX_COLOR_WHITE);
    display.setCursor(4, 14);
    display.println("Bye bye...");
    display.display();
    delay(1000);
  }

#ifdef OLED_SSD1306
  display.dim(needDim);
#else
  display.setContrast(needDim ? 0 : 0x7F);
#endif

  digitalWrite(SWITCH_PIN, config->getState());
  String stateStr = config->getState() ? "On" : "Off";
  logMessage("Update state to => " + stateStr);
  if (config->getState())
  {
    delay(150);
    setupDisplay();
    buildWelcome();
  }
  
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

File root = LittleFS.open("/*.emo", "r");
bool drawFunnyEmotion()
{

  if (isUpdating || !config->getState())
  {
    return false;
  }

  static unsigned long lastDrawTime = 0;
  if (millis() - lastDrawTime < 2000)
  {
    return true;
  }
  lastDrawTime = millis();
  return emoticons->draw(&display, SCREEN_WIDTH, SCREEN_HEIGHT, PX_COLOR_WHITE, PX_COLOR_BLACK);
}

unsigned long lastActivityTime = 0;
int screenBrightness = 255;
void dimScreen()
{
  unsigned long currentTime = millis();
  if (currentTime - lastActivityTime > 10000)
  {
    screenBrightness = max(screenBrightness - 25, 0);
    // display.setContrast(screenBrightness);
    // display.dim(screenBrightness);
    lastActivityTime = currentTime;
  }
}

const int lowMemoryThreshold = 2000; // Alert if free heap is below 2000 bytes
void debugMemory()
{
  size_t freeHeap = ESP.getFreeHeap();
  logMessage("Free Heap: " + String(freeHeap), true);

  if (freeHeap < lowMemoryThreshold)
  {
    logMessage("WARNING: Low memory!", true);
  }
}

void drawUpdateProgress()
{
  if (!isUpdating)
  {
    return;
  }

  tcaselect(0);
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(PX_COLOR_WHITE);
  int startY = 10;
  display.setCursor(0, startY);

  if (isUpdateSuccess)
  {
    display.println("Update successful!");
    display.println("Rebooting...");
    display.display();
    delay(2000);
    ESP.restart();
    return;
  }

  display.println(updatingTitle);

  int progressX = (SCREEN_WIDTH - progressWidth) / 2;
  int progressY = 40;

  String processText = String(progressValue) + "%";
  int16_t textX, textY = 0;
  uint16_t textWidth, textHeight = 0;
  display.getTextBounds(processText.c_str(), 0, 0, &textX, &textY, &textWidth, &textHeight);
  display.setCursor((SCREEN_WIDTH - textWidth) / 2, startY + 20);
  display.println(processText);

  display.fillRect(progressX, progressY, progressWidth, progressHeight, PX_COLOR_BLACK);
  display.fillRect(progressX, progressY, progressValue, progressHeight, PX_COLOR_WHITE);
  display.display();
}

void buildWelcome() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(PX_COLOR_WHITE);
  display.setCursor(4, 14);
  display.println("Hello from:\n NguyenHungA5!!!"); // I hope you keep this message ^^!!
  display.display();
  delay(2000);
}

void handleFileList(AsyncWebServerRequest *request) {
  String path = "/";
  if (request->hasParam("dir")) {
    path = request->getParam("dir")->value();
  }

  Dir dir = LittleFS.openDir(path);
  String output = "[";
  String fileName = "";
  while (dir.next()) {
    File entry = dir.openFile("r");
    fileName = String(entry.name());
    if (fileName == "config.json") {
      continue;
    }

    if (output != "[")
    {
      output += ',';
    }
    bool isDir = false;
    output += "{\"type\":\"";
    output += (isDir) ? "dir" : "file";
    output += "\",\"name\":\"";
    output += fileName;
    output += "\"}";
    entry.close();
  }
  output += "]";
  request->send(200, "application/json", output);
}

void handleFileUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
  if (!index) {
    if (!filename.startsWith("/")) {
      filename = "/" + filename;
    }

    if (LittleFS.exists(filename))
    {
      LittleFS.remove(filename); // Remove the existing file if it exists
    }

    request->_tempFile = LittleFS.open(filename, "w");
  }
  if (len) {
    request->_tempFile.write(data, len);
  }
  if (final) {
    request->_tempFile.close();
    request->send(200, "text/plain", "File Uploaded!");
  }
}

void handleFileDelete(AsyncWebServerRequest *request) {
  if (request->hasParam("file")) {
    String path = request->getParam("file")->value();
    if (path == "/") {
      request->send(400, "text/plain", "Cannot delete root directory");
      return;
    }
    if (!LittleFS.exists(path)) {
      request->send(404, "text/plain", "File not found");
      return;
    }
    LittleFS.remove(path);
    request->send(200, "text/plain", "File deleted");
  } else {
    request->send(400, "text/plain", "File parameter missing");
  }
}

void handleFileCreate(AsyncWebServerRequest *request) {
  if (request->hasParam("file")) {
    String path = request->getParam("file")->value();
    if (path == "/") {
      request->send(400, "text/plain", "Cannot create root directory");
      return;
    }
    if (LittleFS.exists(path)) {
      request->send(400, "text/plain", "File already exists");
      return;
    }
    File file = LittleFS.open(path, "w");
    if (file) {
      file.close();
      request->send(200, "text/plain", "File created");
    } else {
      request->send(500, "text/plain", "File creation failed");
    }
  } else {
    request->send(400, "text/plain", "File parameter missing");
  }
}

void handleFileRename(AsyncWebServerRequest *request) {
  if (request->hasParam("from") && request->hasParam("to")) {
    String from = request->getParam("from")->value();
    String to = request->getParam("to")->value();
    if (!LittleFS.exists(from)) {
      request->send(404, "text/plain", "Source file not found");
      return;
    }
    if (LittleFS.exists(to)) {
      request->send(400, "text/plain", "Destination file already exists");
      return;
    }
    if (LittleFS.rename(from, to)) {
      request->send(200, "text/plain", "File renamed");
    } else {
      request->send(500, "text/plain", "File rename failed");
    }
  } else {
    request->send(400, "text/plain", "Parameters missing");
  }
}

void handleGetFileRequest(AsyncWebServerRequest *request) {
  if (!request->hasParam("name")) {
    request->send(400, "text/plain", "Parameters missing");
    return;
  }

  request->send(LittleFS, request->getParam("name")->value(), "text/plain");
}

void setupFileManagement() {
  server->on("/list", HTTP_GET, handleFileList);
  server->on("/upload", HTTP_POST, [](AsyncWebServerRequest *request) {
    request->send(200);
  }, handleFileUpload);
  server->on("/delete", HTTP_POST, handleFileDelete);
  server->on("/create", HTTP_POST, handleFileCreate);
  server->on("/rename", HTTP_POST, handleFileRename);
  server->on("/view", handleGetFileRequest);
}