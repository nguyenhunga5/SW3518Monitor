#include "Config.h"
#include "LittleFS.h"
#include <ArduinoJson.h>

String defaultName() {
    return String(kServerName) + "-" + String(ESP.getChipId());
}

Config::Config()
{
    if (!loadConfig()) {
        this->state = true;
        this->totalEnergy1 = 0.0;
        this->totalEnergy2 = 0.0;
        this->totalEnergy3 = 0.0;
        this->totalEnergy4 = 0.0;
        this->serverName = defaultName();
    }
}

Config::~Config()
{

}

void Config::saveConfig()
{
    // Serial.println("Begin save configuration file");
    // Create a JSON document to hold the configuration
    DynamicJsonDocument doc(256);

    // Convert the configuration struct to JSON
    doc["state"] = this->state;
    doc["totalEnergy1"] = this->totalEnergy1;
    doc["totalEnergy2"] = this->totalEnergy2;
    doc["totalEnergy3"] = this->totalEnergy3;
    doc["totalEnergy4"] = this->totalEnergy4;
    doc["serverName"] = this->serverName;

    // Open the configuration file in write mode
    File configFile = LittleFS.open(CONFIG_FILE, "w");
    if (!configFile)
    {
        Serial.println("Failed to open config file for writing");
        return;
    }

    // Serialize JSON to the file
    serializeJson(doc, configFile);
    configFile.close();

    // Serial.println("Save configuration file successfully");
}

bool Config::loadConfig()
{
    if (!LittleFS.begin())
    {
        Serial.println("LittleFS Mount Failed");
        return false;
    }

    // Open the configuration file in read mode
    File configFile = LittleFS.open(CONFIG_FILE, "r");
    if (!configFile)
    {
        Serial.println("Failed to open config file for reading");
        return false;
    }

    // Parse the JSON file into a JSON document
    DynamicJsonDocument doc(256);
    DeserializationError error = deserializeJson(doc, configFile);

    // Check for parsing errors
    if (error)
    {
        Serial.print(F("Failed to read configuration: "));
        Serial.println(error.c_str());
        return false;
    }

    this->state = doc["state"].as<bool>();
    this->totalEnergy1 = doc["totalEnergy1"].as<float>();
    this->totalEnergy2 = doc["totalEnergy2"].as<float>();
    this->totalEnergy3 = doc["totalEnergy3"].as<float>();
    this->totalEnergy4 = doc["totalEnergy4"].as<float>();
    this->serverName = doc["serverName"].isNull() ? defaultName() : doc["serverName"].as<String>();

    configFile.close();
    return true;
}

void Config::updateTotalEnergy(float energy, int port) {
    switch (port)
    {
    case 0:
        this->totalEnergy1 += energy;
        break;

    case 1:
        this->totalEnergy2 += energy;
        break;

    case 2:
        this->totalEnergy3 += energy;
        break;

    case 3:
        this->totalEnergy4 += energy;
        break;

    default:
        return;
    }

    saveConfig();
}

float Config::totalEnergyOf(int port) {
    switch (port)
    {
    case 0:
        return this->totalEnergy1;

    case 1:
        return this->totalEnergy2;

    case 2:
        return this->totalEnergy3;

    case 3:
        return this->totalEnergy4;

    default:
        return 0.0;
    }
}

void Config::resetTotalEnergy(int port) {
    switch (port)
    {
    case 0:
        this->totalEnergy1 = 0;

    case 1:
        this->totalEnergy2 = 0;

    case 2:
        this->totalEnergy3 = 0;

    case 3:
        this->totalEnergy4 = 0;

    default:
        return;
    }

    this->saveConfig();
}

    void Config::setState(bool state)
{
    this->state = state;
    this->saveConfig();
}

bool Config::getState() {
    return this->state;
}

void Config::setServerName(String name)
{
    this->serverName = name;
    this->saveConfig();
}

String Config::getServerName()
{
    return this->serverName;
}
