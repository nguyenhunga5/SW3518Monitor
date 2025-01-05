#ifndef DEFINES_H
#define DEFINES_H

#define FWVersion "1.1"
#define kTimeToCheckTemperature 1000       // 1000ms
#define kTimeToChangeFan 10000       // 10000ms
#define kTimeToReadInformation 150        // 150ms
#define FAN_PIN 12                           // For PWM control fan
#define kServerName "sw351xmonitor"

#define kMaxTemperature 80
#define kMinTemperature 30
#define CONFIG_FILE "/config.json"
#define SWITCH_PIN 13
#define SWITCH_BUTTON 16
#define LED_STATUS 14
#define kMaxPower 100.0
#define kMinPower 30.0

#endif