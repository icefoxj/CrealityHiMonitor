#pragma once

// What the printer tells us: Creality's own LAN websocket,
// ws://<printer>:9999/ (what Creality Print uses). It sends a full JSON
// snapshot on connect, then only the fields that changed, several times a
// second. See docs/creality-websocket.md.
//
// Plain C++ and ArduinoJson: no Arduino headers, tested on the host.

#include <stddef.h>
#include <stdint.h>

namespace hm {

// Creality "state" codes, from Creality Print's device dashboard
// (0 idle ... 9 starting a job). See docs/creality-websocket.md.
enum class Phase : uint8_t {
    Unknown,     // nothing received yet, or a code we do not know
    Idle,        // 0
    Preparing,   // 9, or printing with layer 0 (heating, levelling)
    Printing,    // 1, 8 (restoring after a pause)
    Paused,      // 5, 6 (pausing)
    Stopping,    // 7
    Complete,    // 2 (lasts ~50 s, then the printer goes back to 0)
    Failed,      // 3
    Aborted,     // 4
};

const char* phaseName(Phase p);

struct PrinterStatus {
    bool    seen        = false;   // at least one snapshot/delta parsed
    int     state       = -1;      // Creality "state"
    int     deviceState = -1;      // 0 idle, non-zero busy (7 homing)
    int     progress    = -1;      // "printProgress", % of the file (not of the time)
    int32_t leftS       = -1;      // "printLeftTime": slicer estimate minus print time
    int32_t jobS        = -1;      // "printJobTime": print time so far
    int     layer       = -1;
    int     layers      = -1;      // "TotalLayer"
    float   nozzle      = -1000.0f, nozzleTarget = -1000.0f;
    float   bed         = -1000.0f, bedTarget    = -1000.0f;
    int     errorCode   = 0;       // "err.errcode"
    char    file[64]    = "";      // file name without the directory
    char    hostname[32] = "";
    char    model[12]   = "";
};

// Applies one websocket text message. Returns true when it was a JSON
// object (known fields are copied, the rest ignored). *heartbeat is set when
// the printer sent {"ModeCode":"heart_beat"}, which the client answers with
// the bare text "ok".
bool applyCrealityMessage(PrinterStatus& st, const char* data, size_t len, bool* heartbeat = nullptr);

Phase phaseOf(const PrinterStatus& st);


}  // namespace hm
