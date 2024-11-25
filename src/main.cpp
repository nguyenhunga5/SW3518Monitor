#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels


#define OLED_RESET     -1 // Reset pin # (or -1 if sharing Arduino reset pin)
#define SCREEN_ADDRESS 0x3D ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32

#define TCAADDR 0x70

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Helper function for changing TCA output channel
void tcaselect(uint8_t channel) {
  if (channel > 7) return;
  
  Wire.beginTransmission(TCAADDR);
  Wire.write(1 << channel);
  Wire.endTransmission();
  Serial.print(TCAADDR);  
}

void setup() {
  Serial.begin(9600);
 
  // Start I2C communication with the Multiplexer
  Wire.begin();

  // Init OLED display on bus number 0
  tcaselect(0);
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;);
  } 

   // Clear the buffer
  display.clearDisplay();
  
  // Init OLED display on bus number 1
   tcaselect(1);
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;);
  } 
  
  // Clear the buffer
  display.clearDisplay();

  // Write to OLED on bus number 0
  tcaselect(0);
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 8);
  // Display static text
  display.println("Hello World Disp 0");
  display.display(); 

  
  // Write to OLED on bus number 1
  tcaselect(1);
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 8);
  // Display static text
  display.println("Hello World Disp 1");
  display.display(); 

}

void loop() {
  // put your main code here, to run repeatedly:
}