#define FWVersion "1.0"
unsigned long kTimeToCheckTemperature = 500; // 500ms
unsigned long kTimeToReadInformation = 150;  // 150ms
#define FAN_PIN 12                           // For PWM control fan
#define kServerName "sw3518monitor"

float kMaxTemperature = 80;
float kMinTemperature = 30;