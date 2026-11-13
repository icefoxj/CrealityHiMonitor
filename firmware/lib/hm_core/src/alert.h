#pragma once

// End-of-job and attention alerts.
//
// The AtomS3R has no speaker, so an alert is always visual: the screen
// flashes at full brightness. When a speaker is fitted (Atomic Echo Base,
// or a piezo buzzer on a GPIO) the alert also plays a melody.
//
// - Done: the job finished (the screen went from PRINT/PAUSED to DONE).
//   A chime now and twice more, 30 s apart; the screen flashes for 90 s.
// - Attention: the printer stopped on an error, or paused itself with an
//   error code (filament run-out, ...). An alarm every 30 s for 5 min.
// A cancelled job (STOP) is the user's own doing: no alert. A press of the
// button ends any alert.

#include <stdint.h>

#include "screens.h"

namespace hm {

enum class AlertKind : uint8_t { None, Done, Attention };

// One note: frequency in Hz (0 = rest) and length in ms
struct Note {
    uint16_t hz;
    uint16_t ms;
};

// The melodies, terminated by {0, 0}
const Note* melodyFor(AlertKind k);

class Alerts {
public:
    static constexpr uint32_t kRepeatMs        = 30000;
    static constexpr int      kDoneRounds      = 3;
    static constexpr uint32_t kDoneFlashMs     = 90000;
    static constexpr uint32_t kAttentionMs     = 5UL * 60UL * 1000UL;

    // Feed the screen being shown, every frame. Returns the melody to start
    // now (AlertKind::None most of the time).
    AlertKind update(const View& v, uint32_t nowMs);

    // The screen should flash (and be at full brightness)
    bool active(uint32_t nowMs) const;
    AlertKind kind() const { return kind_; }

    // The button: stop the alert
    void acknowledge() { kind_ = AlertKind::None; }

    // Starts an alert now (also used by the tests)
    AlertKind trigger(AlertKind k, uint32_t nowMs);

private:
    static bool live(Screen s){ return s == Screen::Printing || s == Screen::Paused; }

    Screen    last_      = Screen::Splash;
    bool      lastErr_   = false;
    AlertKind kind_      = AlertKind::None;
    uint32_t  startMs_   = 0;
    int       rounds_    = 0;   // melodies played in this alert
};

// Steps through a melody without blocking: call next() each frame; it
// returns the note to start now, if one is due.
class MelodyPlayer {
public:
    void start(const Note* m, uint32_t nowMs);
    void stop() { m_ = nullptr; }
    bool playing() const { return m_ != nullptr; }
    // true and *note filled when a new note starts at nowMs
    bool next(uint32_t nowMs, Note* note);

private:
    const Note* m_ = nullptr;
    int         i_ = 0;
    uint32_t    dueMs_ = 0;
};

}  // namespace hm
