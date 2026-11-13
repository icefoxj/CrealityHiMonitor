#pragma once

// Progress and time-left estimate for the running job, from the Creality
// websocket alone.
//
// Measured on a real 2 h 31 min print (docs/creality-websocket.md):
// - "printLeftTime" is the slicer's total estimate T minus "printJobTime":
//   a countdown. The job ended 163 s after T (1.8 % late), so it is a good
//   time left.
// - printJobTime / T tracked the real share of the time within ~1.5 points
//   all along: that is the percent shown.
// - "printProgress" is the position in the G-code file. It lags badly while
//   the wide bottom layers print (16 % shown at 43 % of the time), and is
//   only used for the tail when the countdown has run out.
// - Moonraker's M73 progress (from Creality Print's G-code) was useless:
//   78 % when the job ended. It is not used.
//
// While paused, the estimate is held.

#include <stdint.h>

#include "printer.h"

namespace hm {

struct Estimate {
    int     percent    = -1;   // 0..100, -1 unknown
    int32_t remainingS = -1;   // -1 unknown
    bool    overrun    = false;   // past the slicer's estimate: tail from the file position
};

class Estimator {
public:
    void reset();

    // One update with the printer's latest numbers
    Estimate update(const PrinterStatus& st);

    const Estimate& last() const { return last_; }
    int32_t total() const { return total_; }   // slicer estimate T, seconds; -1 unknown

private:
    int32_t  total_ = -1;
    Estimate last_;
};

}  // namespace hm
