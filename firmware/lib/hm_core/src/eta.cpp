#include "eta.h"

namespace hm {

void Estimator::reset(){
    total_ = -1;
    last_  = Estimate{};
}

Estimate Estimator::update(const PrinterStatus& st){
    const Phase phase = phaseOf(st);
    const bool active = phase == Phase::Printing || phase == Phase::Paused || phase == Phase::Preparing;
    if (!active){
        if (phase != Phase::Stopping){
            reset();
        }
        Estimate e;
        e.percent    = st.progress;
        e.remainingS = st.leftS;
        last_ = e;
        return e;
    }

    // A paused job is not progressing: hold what was shown
    if (phase == Phase::Paused && last_.percent >= 0){
        return last_;
    }

    // The slicer's total, while the countdown still runs. A new job shows
    // up as a smaller print time.
    if (st.jobS >= 0 && st.leftS > 0){
        total_ = st.jobS + st.leftS;
    }

    Estimate e;
    if (total_ <= 0 || st.jobS < 0){
        // Nothing to go on yet (heating): the printer's own numbers
        e.percent    = st.progress;
        e.remainingS = st.leftS;
        last_ = e;
        return e;
    }

    if (st.leftS > 0){
        e.percent = static_cast<int>((100LL * st.jobS + total_ / 2) / total_);
        e.remainingS = st.leftS;
    } else {
        // Past the estimate: the rest from the file position (the end of
        // the file is where it is accurate)
        e.overrun = true;
        const int p = st.progress;
        if (p > 0 && p < 100){
            e.remainingS = static_cast<int32_t>(static_cast<int64_t>(st.jobS) * (100 - p) / p);
        } else {
            e.remainingS = 0;
        }
        e.percent = 99;
    }
    // Still printing: never 100 % until the printer says it is done
    if (e.percent > 99) e.percent = 99;
    if (e.percent < 0) e.percent = 0;
    last_ = e;
    return e;
}

}  // namespace hm
