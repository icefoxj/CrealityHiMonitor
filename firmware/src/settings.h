#pragma once

// Settings kept in NVS (Preferences namespace "hm01"). Wi-Fi credentials
// are not here: the ESP32 Wi-Fi driver keeps them (WiFiManager saves them).
// Only the main loop calls these.

#include <stdint.h>

struct Settings {
    char    printer[64] = "";   // IP or host name; "" = search the network
    bool    printerAuto = false;   // printer was found by the search (search again if it moves)
    char    tz[48]      = "";
    bool    calStored   = false;
    uint8_t rotation    = 0;    // absolute display rotation 0..3
    float   sign        = -1.0f;
    float   offsetDeg   = 0.0f;
    uint8_t brightness  = 128;
};

namespace settings {

// Loads from NVS; defaultRotation is used when no calibration is stored
void begin(uint8_t defaultRotation);
const Settings& get();

void setPrinter(const char* host, bool foundBySearch = false);
void setTz(const char* tz);
void setCalibration(uint8_t rotation, float sign, float offsetDeg);
void resetCalibration();
void setBrightness(uint8_t b);

}  // namespace settings
