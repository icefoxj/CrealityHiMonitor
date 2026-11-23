#pragma once

// The CardputerZero's hardware, through Linux: the framebuffer (ST7789V,
// 320 x 170), the keyboard (evdev), the battery, the Wi-Fi link, the
// speaker (aplay) and a small config file. The launcher (APPLaunch) stops
// drawing while an app runs: the app owns the framebuffer until it exits.

#include <cstdint>
#include <string>
#include <vector>

#include "alert.h"

namespace cz {

// ---------------------------------------------------------------- framebuffer

class Framebuffer {
public:
    ~Framebuffer(){ close(); }

    // path: a device, or "" to take the first /dev/fbN that is 320 x 170
    // (or 170 x 320), else /dev/fb0. rotate: 0, 90 or 270 when the panel
    // is mounted portrait; -1 = choose from its shape.
    bool open(const std::string& path, int rotate);
    void close();

    // Copies a w x h native RGB565 frame (rows top .. top + h - 1 of the
    // logical frame) to the screen, converting the pixel format
    void push(const uint16_t* px, int w, int h, int top);

    std::string describe() const;

private:
    int      fd_ = -1;
    uint8_t* mem_ = nullptr;
    size_t   len_ = 0;
    int      xres_ = 0, yres_ = 0, bpp_ = 0, stride_ = 0;
    int      rshift_ = 11, gshift_ = 5, bshift_ = 0;   // 32 bpp offsets
    bool     bgr_ = false;     // 16 bpp with red in the low bits
    int      rotate_ = 0;
    std::string path_;
};

// ---------------------------------------------------------------- keyboard

enum class Key : uint8_t { None, Quit, PageNext, PagePrev, Click, Test };

class Keyboard {
public:
    ~Keyboard(){ close(); }
    bool open();          // every input device with ESC and letter keys
    Key  poll();          // the next key press, or None
    void close();
    int  devices() const { return static_cast<int>(fds_.size()); }

private:
    std::vector<int> fds_;
};

// ---------------------------------------------------------------- system

struct Battery {
    int  percent = -1;    // -1: none
    bool charging = false;
};
Battery readBattery();

int         wifiBars();     // 0..4 from /proc/net/wireless, -1 unknown
std::string ownIp();        // "" when offline

// ---------------------------------------------------------------- sound

// Plays a melody on the speaker in the background (aplay); silent when
// aplay is missing
void playMelody(hm::AlertKind k);

// ---------------------------------------------------------------- config

struct Config {
    std::string printer;      // "" = search
    bool        printerAuto = false;
    bool        muted = false;
};
Config      loadConfig();
void        saveConfig(const Config& c);
std::string configPath();

}  // namespace cz
