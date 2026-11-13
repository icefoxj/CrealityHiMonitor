#include "settings.h"

#include <Preferences.h>
#include <stdio.h>
#include <string.h>

#include "config.h"

namespace {

Settings s_settings;
uint8_t  s_defaultRotation = 0;

void copyStr(char* dst, size_t n, const char* src){
    snprintf(dst, n, "%s", src ? src : "");
}

void applyCalibrationDefaults(){
    s_settings.calStored = false;
    s_settings.rotation  = s_defaultRotation;
    s_settings.sign      = kTiltSign;
    s_settings.offsetDeg = kTiltOffsetDeg;
}

}  // namespace

namespace settings {

void begin(uint8_t defaultRotation){
    s_defaultRotation = defaultRotation & 3;
    Preferences p;
    p.begin("hm01", true);
    copyStr(s_settings.printer, sizeof(s_settings.printer), p.getString("printer", "").c_str());
    s_settings.printerAuto = p.getBool("pauto", false);
    copyStr(s_settings.tz, sizeof(s_settings.tz), p.getString("tz", kDefaultTz).c_str());
    s_settings.brightness = p.getUChar("bright", kBrightness);
    if (p.isKey("rot")){
        s_settings.calStored = true;
        s_settings.rotation  = p.getUChar("rot", s_defaultRotation) & 3;
        s_settings.sign      = p.getFloat("sign", kTiltSign) < 0.0f ? -1.0f : 1.0f;
        s_settings.offsetDeg = p.getFloat("offset", kTiltOffsetDeg);
    } else {
        applyCalibrationDefaults();
    }
    p.end();
}

const Settings& get(){
    return s_settings;
}

void setPrinter(const char* host, bool foundBySearch){
    copyStr(s_settings.printer, sizeof(s_settings.printer), host);
    s_settings.printerAuto = foundBySearch && s_settings.printer[0];
    Preferences p;
    p.begin("hm01", false);
    p.putString("printer", s_settings.printer);
    p.putBool("pauto", s_settings.printerAuto);
    p.end();
}

void setTz(const char* tz){
    copyStr(s_settings.tz, sizeof(s_settings.tz), tz && *tz ? tz : kDefaultTz);
    Preferences p;
    p.begin("hm01", false);
    p.putString("tz", s_settings.tz);
    p.end();
}

void setCalibration(uint8_t rotation, float sign, float offsetDeg){
    s_settings.calStored = true;
    s_settings.rotation  = rotation & 3;
    s_settings.sign      = sign < 0.0f ? -1.0f : 1.0f;
    s_settings.offsetDeg = offsetDeg;
    Preferences p;
    p.begin("hm01", false);
    p.putUChar("rot", s_settings.rotation);
    p.putFloat("sign", s_settings.sign);
    p.putFloat("offset", s_settings.offsetDeg);
    p.end();
}

void resetCalibration(){
    applyCalibrationDefaults();
    Preferences p;
    p.begin("hm01", false);
    p.remove("rot");
    p.remove("sign");
    p.remove("offset");
    p.end();
}

void setBrightness(uint8_t b){
    s_settings.brightness = b;
    Preferences p;
    p.begin("hm01", false);
    p.putUChar("bright", b);
    p.end();
}

}  // namespace settings
