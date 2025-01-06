#pragma once
#include <LittleFS.h>
#include <vector>
#ifdef OLED_SSD1306
#include <Adafruit_SSD1306.h>
#define SCREEN_CLASS Adafruit_SSD1306
#else
#include <Adafruit_SH110x.h>
#define SCREEN_CLASS Adafruit_SH1106G
#endif

#include <ESPAsyncWebServer.h>

class Emoticons {

public:
    Emoticons();
    ~Emoticons();

    bool draw(SCREEN_CLASS *display, int screen_width, int screen_height, int px_color_white, int px_color_black);
    void addListener(AsyncWebServer *server);

    File uploadFile;
private : std::vector<String> emoticons;
    void loadEmoticons();
};