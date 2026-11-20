#pragma once

// Line commands on the USB serial console (115200 baud, any terminal):
//
//   help                         list the commands
//   status                       one STATUS line: link, printer, job, tilt
//   version                      firmware and board
//   printer <ip|host>            set the printer address (saved)
//   printer auto                 forget it and search the network again
//   tz <POSIX TZ>                time zone, e.g. tz GMT0BST,M3.5.0/1,M10.5.0
//   calibrate rot=<0-3> sign=<1|-1> offset=<deg>   orientation (saved)
//   calibrate reset              back to the compiled defaults
//   bright <0-255>               backlight (saved)
//   sound on|off                 alert sounds (saved)
//   volume <0-255>               alert volume (saved)
//   buzzer <gpio>|off            piezo buzzer pin, when there is no Echo Base (saved)
//   beep                         play the "job done" sound (a speaker test)
//   alert done|attention|off     run an alert now, flash and sound (a test), or stop it
//   autorotate on|off            CoreS3: turn the dashboard with the device (saved)
//   setup                       open the Wi-Fi setup portal
//   wifi reset                   forget the Wi-Fi network and reboot into setup
//   reboot

#include <stddef.h>

namespace hm {

enum class Cmd {
    None,        // empty line
    Help,
    Status,
    Version,
    Printer,     // arg = address, or "auto"
    Tz,          // arg = POSIX TZ string
    Calibrate,
    Bright,
    Sound,       // value = 1 on, 0 off
    Volume,      // value = 0..255
    Buzzer,      // value = GPIO, -1 = off
    Beep,
    Alert,       // value = 1 done, 2 attention, 0 off (the AlertKind values)
    AutoRotate,  // value = 1 on, 0 off
    Setup,
    WifiReset,
    Reboot,
    Unknown,
    BadArgs,     // known command, bad arguments; arg = usage
};

struct Command {
    Cmd   kind   = Cmd::None;
    char  arg[64] = "";
    // calibrate
    bool  reset     = false;
    bool  hasRot    = false;
    int   rot       = 0;
    bool  hasSign   = false;
    int   sign      = 1;
    bool  hasOffset = false;
    float offsetDeg = 0.0f;
    // bright, sound, volume, buzzer, alert, autorotate
    int   value  = 0;
};

Command parseCommand(const char* line);

// Collects characters into lines (CR, LF or both end a line)
class LineReader {
public:
    // Returns true when a complete line is ready in line()
    bool feed(char c);
    const char* line() const { return buf_; }

private:
    char   buf_[96] = "";
    size_t len_     = 0;
    bool   ready_   = false;
};

}  // namespace hm
