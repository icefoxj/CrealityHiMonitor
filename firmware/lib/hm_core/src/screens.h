#pragma once

// The HM-01 screens: a fixed square bezel with mech corner markings, and a
// round HUD inside it that turns with the tilt angle. Everything that
// matters lives inside the HUD circle, so it can turn to any angle without
// being cut by the panel's corners.

#include <stdint.h>

#include "gfx.h"

namespace hm {

namespace pal {
constexpr Color bezel  = rgb(8, 9, 11);
constexpr Color face   = rgb(22, 24, 28);
constexpr Color dim    = rgb(46, 50, 58);
constexpr Color mid    = rgb(120, 126, 138);
constexpr Color text   = rgb(242, 242, 236);
constexpr Color yellow = rgb(255, 204, 0);    // house accent #FFCC00 (no orange)
constexpr Color green  = rgb(64, 220, 120);
constexpr Color red    = rgb(255, 64, 52);
constexpr Color cyan   = rgb(40, 196, 255);
}  // namespace pal

enum class Screen : uint8_t {
    Splash,     // boot: the HM-01 title card
    Setup,      // Wi-Fi setup portal open
    Linking,    // joining Wi-Fi, looking for the printer, connecting
    Offline,    // printer not answering
    Idle,       // printer ready, no job
    Printing,
    Paused,
    Done,       // job complete
    Stopped,    // job cancelled
    Error,      // printer reports an error / failed job
};

const char* screenName(Screen s);

// Job and printer details for the larger panels (the CoreS3 dashboard).
// The AtomS3R HUD does not show them. Values are rounded to what a panel
// shows, so a redraw happens only when a shown digit changes. Strings are
// upper case; empty / negative / -1000 means "not known".
struct Details {
    char    job[32]       = "";     // job name, from the file name
    char    material[10]  = "";     // "PLA", from the file name
    int32_t sliceS        = -1;     // the slicer's estimate, from the file name
    int32_t totalS        = -1;     // print time so far + time left
    char    start[8]      = "";     // wall-clock time the job started
    int     nozzleTarget  = -1000;  // degrees C
    int     bedTarget     = -1000;
    int     nozzleMax     = -1;     // the printer's limits, for the gauges
    int     bedMax        = -1;
    int     chamber       = -1000;  // -1000: no chamber sensor
    int     chamberTarget = -1000;
    int     speed         = -1;     // mm/s, real time
    int     flow10        = -1;     // mm3/s x 10, real time
    int     feedPct       = -1;     // speed override %
    int     flowPct       = -1;     // flow override %
    int     fanPart       = -1;     // %
    int     fanAux        = -1;
    int     fanCase       = -1;
    int     z100          = -1;     // nozzle height, 1/100 mm
    int     usedDm        = -1;     // filament used, 1/10 m
    int     filePct       = -1;     // position in the G-code file, %
    char    printer[20]   = "";     // "HI-5EBC"
    char    model[10]     = "";     // "F018"
    char    ip[16]        = "";     // our own address
    int8_t  wifiBars      = -1;     // 0..4, -1 unknown
    int8_t  battery       = -1;     // our own battery, 0..100 %; -1 none (or no battery fitted)
    bool    charging      = false;

    bool operator==(const Details& o) const;
    bool operator!=(const Details& o) const { return !(*this == o); }
};

// Everything a screen shows. Strings are upper case (the font has no lower
// case); empty means "not known".
struct View {
    Screen  screen     = Screen::Splash;
    int     percent    = -1;     // 0..100
    int32_t remainingS = -1;     // time left, seconds
    int32_t elapsedS   = -1;     // print time so far / total print time
    char    eta[8]     = "";     // wall-clock finish time, "17:33"
    char    clock[8]   = "";     // wall-clock time now
    int     layer      = -1;
    int     layers     = -1;
    int     nozzle     = -1000;  // degrees C, -1000 = unknown
    int     bed        = -1000;
    bool    heating    = false;  // job started, still heating / levelling
    int     errorCode  = 0;
    char    line1[24]  = "";     // free text: setup, linking and offline screens
    char    line2[24]  = "";
    char    line3[24]  = "";
    char    version[12] = "";
    bool    alert      = false;  // an alert is on: the ring and band flash
    uint8_t blink      = 0;      // animation phase, advanced by the caller
    uint8_t spin       = 0;      // spinner position 0..39
    uint8_t page       = 0;      // CardputerZero: 0 main, 1 details
    Details d;                   // the larger panels only

    // Same content (ignores nothing: the caller bumps blink/spin only when
    // the screen animates)
    bool operator==(const View& o) const;
    bool operator!=(const View& o) const { return !(*this == o); }
};

constexpr int kScreenSize = 128;
constexpr int kRingSegments = 40;

// Draws the whole frame: bezel, then the HUD turned by angle (radians,
// clockwise). The canvas must be kScreenSize square.
void drawScreen(Canvas& cv, const View& v, float angle);

// Format helpers (upper case, mech-font friendly)
void formatDuration(int32_t seconds, char* out, int n);   // "1:25" (h:mm), "0:07"
void formatClock(int hour, int minute, char* out, int n); // "17:33"

// Whether a view animates (needs redraws without data changes)
bool animates(const View& v);

}  // namespace hm
