#include "PortItem.h"

PortItem::PortItem() {
    sw = new SW35xx(Wire);
    sw->begin();
}

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

const char *fastChargeType2String(SW35xx::fastChargeType_t type,
                                  uint8_t PDVersion)
{
    switch (type)
    {
    case SW35xx::NOT_FAST_CHARGE:
        return "None";
        break;
    case SW35xx::QC2:
        return "QC2.0";
        break;
    case SW35xx::QC3:
        return "QC3.0";
        break;
    case SW35xx::FCP:
        return "FCP";
        break;
    case SW35xx::SCP:
        return "SCP";
        break;
    case SW35xx::PD_FIX:
        switch (PDVersion)
        {
        case 2:
            return "PD2.0";
            break;
        case 3:
            return "PD3.0";
            break;
        default:
            return "Unknown PD";
            break;
        }
        break;
    case SW35xx::PD_PPS:
        switch (PDVersion)
        {
        case 3:
            return "PD3.0 PPS";
            break;
        default:
            return "Unknown PD PPS";
            break;
        }
        break;
    case SW35xx::MTKPE1:
        return "MTK PE1.1";
        break;
    case SW35xx::MTKPE2:
        return "MTK PE2.0";
        break;
    case SW35xx::LVDC:
        return "LVDC";
        break;
    case SW35xx::SFCP:
        return "SFCP";
        break;
    case SW35xx::AFC:
        return "AFC";
        break;
    default:
        return "Unknown";
        break;
    }
}

void PortItem::update() {
    sw->readStatus();
    protocol = fastChargeType2String(sw->fastChargeType, sw->PDVersion);
    temperature = sw->readTemperature();
    // Convert temperature from mV to Celsius
    // float tempCelsius = (temperature - 500) / 10.0;
    // Current disable because in the board, NTC pin is connected to GND, so we cannot use it.
    logMessage("=======================================");
    logMessage("Current input voltage: " + String(sw->vin_mV) + "mV");
    logMessage("Current output voltage: " + String(sw->vout_mV) + "mV");
    logMessage("Current USB-C current: " + String(sw->iout_usbc_mA) + "mA");
    logMessage("Current USB-A current: " + String(sw->iout_usba_mA) + "mA");
    logMessage("Current fast charge type: " + String(sw->fastChargeType));
    logMessage(protocol);
    if (sw->fastChargeType == SW35xx::PD_FIX || sw->fastChargeType == SW35xx::PD_PPS)
        logMessage("Current PD version: " + String(sw->PDVersion));

    logMessage("=======================================\n");

    inputVoltage = sw->vin_mV / 1000.0;
    voltage = sw->vout_mV / 1000.0;
    current = (sw->iout_usbc_mA + sw->iout_usba_mA) / 1000.0;
}