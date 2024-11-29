#pragma once
#include <Arduino.h>
#include <h1_SW35xx.h>
#include <Wire.h>
#include "log.h"

using namespace h1_SW35xx;
class PortItem {
public:
    float inputVoltage = 0.0;    // Input Voltage in V
    float voltage = 0.0;    // Voltage in V
    float current = 0.0;    // Current in A
    float temperature = 0.0; // Temperature in °C
    String protocol = ""; 
    bool isActive = true;  // Port status
    SW35xx *sw;

    // Constructor
    PortItem();
    
    // Calculate power in watts
    float getPower() const;
    
    // Reset all values
    void reset();
    
    // Update all values
    void update();
}; 