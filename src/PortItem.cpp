#include "PortItem.h"

PortItem::PortItem(float v, float c, float t, String p) : 
    voltage(v), current(c), temperature(t), isActive(true), protocol(p) {}

float PortItem::getPower() const {
    return voltage * current;
}

void PortItem::reset() {
    voltage = 0.0;
    current = 0.0;
    temperature = 0.0;
    isActive = false;
    protocol = "";
} 