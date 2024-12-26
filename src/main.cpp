#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
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
#include <memory>
#include <vector>

#include "defines.h"
#include "PortItem.h"
#include "Config.h"

constexpr int SCREEN_WIDTH = 128; // OLED display width, in pixels
constexpr int SCREEN_HEIGHT = 64; // OLED display height, in pixels

constexpr int TCAADDR = 0x70;

constexpr int TEMPERATURE_SENSOR_PIN = A0; // The ESP8266 pin ADC0
constexpr int SCREEN_ADDRESS = 0x3D;    ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32

#ifdef OLED_SSD1306
#include <Adafruit_SSD1306.h>
#define OLED_RESET     -1 // Reset pin # (or -1 if sharing Arduino reset pin)
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

// Config
std::unique_ptr<Config> config = nullptr;

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

void drawFunnyEmotion();
void dimScreen();

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

  config = std::make_unique<Config>();
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

  server = std::make_unique<AsyncWebServer>(80);
  dns = std::make_unique<DNSServer>();
  Serial.println("Building wifi manager");
  wm = std::make_unique<AsyncWiFiManager>(server.get(), dns.get());

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
  display.setTextColor(PX_COLOR_WHITE);

  // Header with scrolling text
  display.setCursor(0, 4);
  // I hope you do not remove my name
  String headerText = "WS3518X " + String(FWVersion);
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
  display.println(displayText.substring(0, 21)); // Show only what fits on screen

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

  if (allPortsIdle)
  {
    drawFunnyEmotion();
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
    if (percent < 0.05) {
      percent = 0.0;
    }
    fanSpeed = min((int)(percent * 250), 250);
  } else {
    fanSpeed = 0;
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
    display.setTextColor(PX_COLOR_WHITE);
    int startY = 10;
    display.setCursor(0, startY);
    display.println("Updating...");

    int progressWidth = 100;
    int progressHeight = 10;
    int progressX = (SCREEN_WIDTH - progressWidth) / 2;
    int progressY = startY + 30;
    int progressValue = map(current, 0, final, 0, progressWidth);

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
  if (!MDNS.begin(config->getServerName()))
  {
    Serial.println("Error setting up MDNS responder!");
  }
  else
  {
    Serial.println("mDNS responder started");
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
  // Start server
  server->begin();

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
  display.begin(SCREEN_ADDRESS, true);

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

#include <cstdlib> // For rand() and srand()
#include <ctime>   // For time()

void drawFunnyEmotion()
{
  display.clearDisplay();
  display.setTextSize(2);

  // Seed the random number generator
  srand(time(0));
  int randomEmotion = rand() % 10; // Generate a random number between 0 and 9

  switch (randomEmotion)
  {
  case 0:
    // Draw a smiley face
    display.drawCircle(64, 32, 20, PX_COLOR_WHITE); // Face outline
    display.fillCircle(54, 24, 2, PX_COLOR_WHITE); // Left eye
    display.fillCircle(74, 24, 2, PX_COLOR_WHITE); // Right eye
    display.drawLine(54, 42, 74, 42, PX_COLOR_WHITE); // Smile
    display.drawLine(54, 42, 64, 52, PX_COLOR_WHITE); // Smile
    display.drawLine(64, 52, 74, 42, PX_COLOR_WHITE); // Smile
    break;
  case 1:
    // Draw a sad face
    display.drawCircle(64, 32, 20, PX_COLOR_WHITE); // Face outline
    display.fillCircle(54, 24, 2, PX_COLOR_WHITE); // Left eye
    display.fillCircle(74, 24, 2, PX_COLOR_WHITE); // Right eye
    display.drawLine(54, 52, 74, 52, PX_COLOR_WHITE); // Frown
    display.drawLine(54, 52, 64, 42, PX_COLOR_WHITE); // Frown
    display.drawLine(64, 42, 74, 52, PX_COLOR_WHITE); // Frown
    break;
  case 2:
    // Draw a laughing face
    display.drawCircle(64, 32, 20, PX_COLOR_WHITE); // Face outline
    display.fillCircle(54, 24, 2, PX_COLOR_WHITE); // Left eye
    display.fillCircle(74, 24, 2, PX_COLOR_WHITE); // Right eye
    display.drawLine(54, 42, 74, 42, PX_COLOR_WHITE); // Smile
    display.drawLine(54, 42, 64, 52, PX_COLOR_WHITE); // Smile
    display.drawLine(64, 52, 74, 42, PX_COLOR_WHITE); // Smile
    display.drawLine(64, 52, 64, 62, PX_COLOR_WHITE); // Tongue
    break;
  case 3:
    // Draw a surprised face
    display.drawCircle(64, 32, 20, PX_COLOR_WHITE); // Face outline
    display.fillCircle(54, 24, 2, PX_COLOR_WHITE); // Left eye
    display.fillCircle(74, 24, 2, PX_COLOR_WHITE); // Right eye
    display.drawCircle(64, 42, 5, PX_COLOR_WHITE); // Open mouth
    break;
  case 4:
    // Draw a winking face
    display.drawCircle(64, 32, 20, PX_COLOR_WHITE); // Face outline
    display.fillCircle(54, 24, 2, PX_COLOR_WHITE); // Left eye
    display.drawLine(72, 24, 76, 28, PX_COLOR_WHITE); // Right eye wink
    display.drawLine(54, 42, 74, 42, PX_COLOR_WHITE); // Smile
    display.drawLine(54, 42, 64, 52, PX_COLOR_WHITE); // Smile
    display.drawLine(64, 52, 74, 42, PX_COLOR_WHITE); // Smile
    break;
  case 5:
    // Draw a crying face
    display.drawCircle(64, 32, 20, PX_COLOR_WHITE); // Face outline
    display.fillCircle(54, 24, 2, PX_COLOR_WHITE); // Left eye
    display.fillCircle(74, 24, 2, PX_COLOR_WHITE); // Right eye
    display.drawLine(54, 52, 74, 52, PX_COLOR_WHITE); // Frown
    display.drawLine(54, 52, 64, 42, PX_COLOR_WHITE); // Frown
    display.drawLine(64, 42, 74, 52, PX_COLOR_WHITE); // Frown
    display.drawLine(54, 26, 54, 36, PX_COLOR_WHITE); // Left tear
    display.drawLine(74, 26, 74, 36, PX_COLOR_WHITE); // Right tear
    break;
  case 6:
    // Draw a cool face
    display.drawCircle(64, 32, 20, PX_COLOR_WHITE); // Face outline
    display.drawLine(54, 24, 74, 24, PX_COLOR_WHITE); // Sunglasses top
    display.drawLine(54, 24, 54, 28, PX_COLOR_WHITE); // Left sunglasses side
    display.drawLine(74, 24, 74, 28, PX_COLOR_WHITE); // Right sunglasses side
    display.drawLine(54, 28, 74, 28, PX_COLOR_WHITE); // Sunglasses bottom
    display.drawLine(54, 42, 74, 42, PX_COLOR_WHITE); // Smile
    display.drawLine(54, 42, 64, 52, PX_COLOR_WHITE); // Smile
    display.drawLine(64, 52, 74, 42, PX_COLOR_WHITE); // Smile
    break;
  case 7:
    // Draw a confused face
    display.drawCircle(64, 32, 20, PX_COLOR_WHITE); // Face outline
    display.fillCircle(54, 24, 2, PX_COLOR_WHITE); // Left eye
    display.drawCircle(74, 24, 2, PX_COLOR_WHITE); // Right eye
    display.drawLine(54, 52, 74, 52, PX_COLOR_WHITE); // Frown
    display.drawLine(54, 52, 64, 42, PX_COLOR_WHITE); // Frown
    display.drawLine(64, 42, 74, 52, PX_COLOR_WHITE); // Frown
    break;
  case 8:
    // Draw a tongue out face
    display.drawCircle(64, 32, 20, PX_COLOR_WHITE); // Face outline
    display.fillCircle(54, 24, 2, PX_COLOR_WHITE); // Left eye
    display.fillCircle(74, 24, 2, PX_COLOR_WHITE); // Right eye
    display.drawLine(54, 42, 74, 42, PX_COLOR_WHITE); // Smile
    display.drawLine(54, 42, 64, 52, PX_COLOR_WHITE); // Smile
    display.drawLine(64, 52, 74, 42, PX_COLOR_WHITE); // Smile
    display.drawLine(64, 52, 64, 62, PX_COLOR_WHITE); // Tongue
    break;
  case 9:
    // Draw a heart eyes face
    display.drawCircle(64, 32, 20, PX_COLOR_WHITE); // Face outline
    display.drawLine(52, 22, 56, 26, PX_COLOR_WHITE); // Left heart top
    display.drawLine(56, 22, 52, 26, PX_COLOR_WHITE); // Left heart bottom
    display.drawLine(72, 22, 76, 26, PX_COLOR_WHITE); // Right heart top
    display.drawLine(76, 22, 72, 26, PX_COLOR_WHITE); // Right heart bottom
    display.drawLine(54, 42, 74, 42, PX_COLOR_WHITE); // Smile
    display.drawLine(54, 42, 64, 52, PX_COLOR_WHITE); // Smile
    display.drawLine(64, 52, 74, 42, PX_COLOR_WHITE); // Smile
    break;
  }

  display.display();
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