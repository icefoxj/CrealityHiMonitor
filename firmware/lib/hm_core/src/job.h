#pragma once

// Turns the printer's state into a screen. The printer only says
// "complete" for about a minute before going back to idle, so the end of a
// job (done, stopped, failed) is latched here and shown for kEndHoldS, with
// the total print time and the time it ended.

#include <stdint.h>

#include "eta.h"
#include "printer.h"
#include "screens.h"

namespace hm {

// Local wall-clock hour and minute for a Unix time; false when the clock is
// not set. The firmware passes localtime_r() with the configured TZ.
typedef bool (*LocalTimeFn)(int64_t epoch, int* hour, int* minute);

class JobView {
public:
    static constexpr uint32_t kEndHoldS = 30 * 60;

    // nowS: a monotonic clock in seconds; epoch: Unix time, 0 when unknown
    void update(const PrinterStatus& st, const Estimate& est, uint32_t nowS, int64_t epoch);

    // Fills the job and idle fields of v (screen, percent, times, temps...)
    void fill(View& v, const PrinterStatus& st, const Estimate& est, int64_t epoch, LocalTimeFn localTime) const;

    // Drops a latched end (the button does this)
    void dismiss() { ended_ = End::None; }

    bool ended() const { return ended_ != End::None; }

private:
    enum class End : uint8_t { None, Done, Stopped, Failed };

    bool     active_     = false;   // a job was running at the last update
    End      ended_      = End::None;
    uint32_t endedAt_    = 0;       // monotonic
    int64_t  endedEpoch_ = 0;
    int      lastPercent_ = -1;
    int32_t  lastElapsed_ = -1;
    int      lastLayers_  = -1;
};

}  // namespace hm
