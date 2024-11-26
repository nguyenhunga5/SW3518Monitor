#pragma once

class PortItem {
public:
    float voltage = 0.0;    // Voltage in V
    float current = 0.0;    // Current in A
    float temperature = 0.0; // Temperature in °C
    bool isActive = true;  // Port status
    
    // Constructor
    PortItem() = default;
    
    // Constructor with parameters
    PortItem(float v, float c, float t);
    
    // Calculate power in watts
    float getPower() const;
    
    // Reset all values
    void reset();
}; 