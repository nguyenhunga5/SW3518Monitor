#ifndef CONFIG_H
#define CONFIG_H

#pragma once
#include <Arduino.h>
#include "defines.h"

class Config
{
public:
    Config();
    ~Config();
    
    float totalEnergy1 = 0;
    float totalEnergy2 = 0;
    float totalEnergy3 = 0;
    float totalEnergy4 = 0;

    void saveConfig();
    bool loadConfig();
    // Port 0 -> 1
    void updateTotalEnergy(float energy, int port);
    float totalEnergyOf(int port);
    void resetTotalEnergy(int port);

    void setState(bool state);
    bool getState();

    void setServerName(String name);
    String getServerName();

private:
    bool state = true;
    String serverName = kServerName;
};

#endif