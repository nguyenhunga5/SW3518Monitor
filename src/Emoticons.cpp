#include "Emoticons.hpp"

Emoticons *mySelf = nullptr;

Emoticons::Emoticons()
{
    mySelf = this;
    loadEmoticons();
}

Emoticons::~Emoticons()
{
}

bool Emoticons::draw(SCREEN_CLASS *display, int screen_width, int screen_height, int px_color_white, int px_color_black)
{
    if (emoticons.size() == 0)
    {
        loadEmoticons();
        return false;
    }

    File file;
    if (emoticons.size() == 1)
    {
        file = LittleFS.open(emoticons[0], "r");
    }
    else
    {
        file = LittleFS.open(emoticons[random(0, emoticons.size())], "r");
    }

    if (file)
    {
        display->clearDisplay();
        Serial.print("FILE: ");
        Serial.println(file.name());
        // char binaryString[9]; // 8 bits + null terminator
        int16_t x = 0;
        int16_t y = 0;
        while (file.available())
        {
            uint8_t byte = file.read(); // Read one byte at a time
            for (int i = 7; i >= 0; --i)
            {
                display->drawPixel(x++, y, (byte & (1 << i)) ? px_color_white : px_color_black);
                if (x == screen_width)
                {
                    x = 0;
                    y++;
                }
            }
        }
        display->display();
        return true;
    }
    else
    {
        return false;
    }
}

void Emoticons::addListener(AsyncWebServer *server)
{
    server->on("/emoticons", HTTP_GET, [](AsyncWebServerRequest *request)
               { request->send(LittleFS, "/emoticons.html", "text/html"); });

    server->on("/emoticons_list", HTTP_GET, [](AsyncWebServerRequest *request)
               {
                Serial.println("Request emoticons_list");
                   String response = "[";
                   for (int i = 0; i < mySelf->emoticons.size(); i++)
                   {
                       response += "\"" + mySelf->emoticons[i] + "\"";
                       if (i < mySelf->emoticons.size() - 1)
                       {
                           response += ",";
                       }
                   }
                   response += "]";
                   request->send(200, "application/json", response); });

    server->on("/emoticons_get", HTTP_GET, [](AsyncWebServerRequest *request)
               {
                   String fileName = request->arg("file");
                   Serial.print("Request file: ");
                    Serial.println(fileName);
                   File file = LittleFS.open(fileName, "r");
                   if (file)
                   {
                       request->send(file, fileName,  "application/octet-stream", false);
                   }
                   else
                   {
                       request->send(404, "text/plain", "File not found");
                   }
               });

    server->on("/emoticons_delete", HTTP_DELETE, [](AsyncWebServerRequest *request)
               {
                   String fileName = request->arg("file");
                   Serial.print("Delete file: ");
                   Serial.println(fileName);
                   if (LittleFS.exists(fileName))
                   {
                       LittleFS.remove(fileName);
                       mySelf->loadEmoticons();
                       request->send(200, "text/plain", "File deleted");
                   }
                   else
                   {
                       request->send(404, "text/plain", "File not found");
                   }
               });

    server->on("/emoticons", HTTP_POST, [](AsyncWebServerRequest *request)
               { request->send(200, "text/plain", "File written successfully"); }, [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final)
               {
                
        if (index == 0) {
            // First chunk of data, open the file
            String fn = "/" + filename;
            Serial.print("Starting emoticon upload: ");
            Serial.println(fn);
            if (LittleFS.exists(fn))
            {
                LittleFS.remove(fn); // Remove the existing file if it exists
            }
            mySelf->uploadFile = LittleFS.open(fn, "w");
            if (!mySelf->uploadFile)
            {
                request->send(500, "text/plain", "Failed to open file for writing");
                return;
            }
        }

        // Write the current chunk to the file
        if (mySelf->uploadFile)
        {
            mySelf->uploadFile.write(data, len);
            Serial.println("Written " + String(len) + " bytes");
        }
        else
        {
            Serial.println("File not open during write");
        }

        if (final)
        {
            // All chunks received
            Serial.println("Emoticon upload complete");
            if (mySelf->uploadFile)
            {
                mySelf->uploadFile.close();
                mySelf->loadEmoticons();
            }
        }
         });
}

void Emoticons::loadEmoticons()
{
    emoticons.clear();
    File root = LittleFS.open("/", "r");
    File file = root.openNextFile();
    while (file)
    {
        String fileName = "/" + String(file.name());
        Serial.print("FILE: ");
        Serial.println(fileName);
        if (fileName.endsWith(".emo"))
        {
            emoticons.push_back(fileName);
        }
        file.close();
        file = root.openNextFile();
    }

    root.close();
}
