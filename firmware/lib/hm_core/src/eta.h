#pragma once

// Progress and time-left estimate for the running job.
//
// What the printer offers, and why none of it is used as is:
// - "printProgress" (websocket) is the position in the G-code file. Big
//   bottom layers hold few bytes per minute, so it lags far behind the time
//   spent (16 % shown at 43 % of the time on a measured print).
// - "printLeftTime" is the slicer's total estimate minus the time printed so
//   far: a plain countdown. When the printer runs slower than the slicer
//   assumed, it reaches 0:00 long before the end.
// - Moonraker's display_status.progress is the slicer's own M73 percentage:
//   the share of the slicer's estimated time that the current file position
//   represents. This is the right "percent done".
//
// The estimate: percent = M73. Time left = the slicer's time left at this
// position, T * (1 - p), times the speed ratio actually seen on this
// printer: real print seconds per slicer second, measured between M73
// steps over a sliding window. T (the slicer's total) is printJobTime +
// printLeftTime while the countdown is positive.
//
// Without Moonraker the estimate falls back to the printer's own numbers.

#include <stdint.h>

#include "printer.h"

namespace hm {

struct Estimate {
    int     percent    = -1;   // 0..100, -1 unknown
    int32_t remainingS = -1;   // -1 unknown
    float   ratio      = 1.0f; // speed ratio in use (1 = as the slicer said)
    bool    fromM73    = false;
};

struct EstimatorConfig {
    float windowS   = 1200.0f;   // speed ratio measured over the last ~20 min of print time
    float minWindowS = 300.0f;   // below this much history, trust the slicer (ratio 1)
    float minRatio  = 0.5f;      // clamp the measured ratio
    float maxRatio  = 3.0f;
};

class Estimator {
public:
    explicit Estimator(const EstimatorConfig& cfg = {}) : cfg_(cfg) {}

    void reset();

    // One update. m73 may be null (Moonraker not reachable) or stale.
    Estimate update(const PrinterStatus& st, const MoonrakerStatus* m73);

    const Estimate& last() const { return last_; }

private:
    struct Step {
        float jobS;   // print time when M73 reached p
        float p;      // 0..1
    };
    static constexpr int kSteps = 128;

    void addStep(float jobS, float p);
    float ratio(float jobNow) const;

    EstimatorConfig cfg_;
    Step   steps_[kSteps];
    int    head_   = 0;      // next write position
    int    count_  = 0;
    float  lastP_  = -1.0f;
    float  total_  = -1.0f;  // slicer total T, seconds
    Estimate last_;
};

}  // namespace hm
