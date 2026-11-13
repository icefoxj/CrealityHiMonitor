#include "eta.h"

#include <math.h>
#include <string.h>

namespace hm {

void Estimator::reset(){
    head_  = 0;
    count_ = 0;
    lastP_ = -1.0f;
    total_ = -1.0f;
    last_  = Estimate{};
}

void Estimator::addStep(float jobS, float p){
    steps_[head_] = {jobS, p};
    head_ = (head_ + 1) % kSteps;
    if (count_ < kSteps){
        count_++;
    }
}

// Real print seconds per slicer second, between the newest M73 step and the
// oldest one inside the window. Only steps seen live count: the first value
// after a (re)start was reached at an unknown time and is not stored.
float Estimator::ratio(float jobNow) const {
    (void)jobNow;
    if (count_ < 2 || total_ <= 0.0f){
        return 1.0f;
    }
    const Step& n = steps_[(head_ - 1 + kSteps) % kSteps];
    const Step* o = nullptr;
    for (int k = 1; k < count_; k++){
        const Step& s = steps_[(head_ - 1 - k + 2 * kSteps) % kSteps];
        if (n.jobS - s.jobS > cfg_.windowS){
            break;
        }
        o = &s;
    }
    if (!o){
        return 1.0f;
    }
    const float span = n.jobS - o->jobS;
    const float dp   = n.p - o->p;
    if (span < cfg_.minWindowS || dp <= 0.0f){
        return 1.0f;
    }
    float r = span / (dp * total_);
    if (r < cfg_.minRatio) r = cfg_.minRatio;
    if (r > cfg_.maxRatio) r = cfg_.maxRatio;
    return r;
}

Estimate Estimator::update(const PrinterStatus& st, const MoonrakerStatus* m){
    // The slicer's total, while the printer's countdown still runs
    if (st.jobS >= 0 && st.leftS > 0){
        total_ = static_cast<float>(st.jobS + st.leftS);
    }

    const Phase phase = phaseOf(st);
    const bool jobActive = phase == Phase::Printing || phase == Phase::Paused || phase == Phase::Preparing;
    const bool m73ok = m && m->valid && m->m73 >= 0.0f
                    && (strcmp(m->state, "printing") == 0 || strcmp(m->state, "paused") == 0);

    Estimate e;
    if (!jobActive){
        if (phase != Phase::Stopping){
            reset();
        }
        e.percent    = st.progress;
        e.remainingS = st.leftS;
        last_ = e;
        return e;
    }

    if (!m73ok){
        // Fallback: the printer's own numbers
        e.percent    = st.progress;
        e.remainingS = st.leftS;
        last_ = e;
        return e;
    }

    // A paused job: hold the estimate
    if (phase == Phase::Paused && last_.fromM73){
        return last_;
    }

    const float job = static_cast<float>(st.jobS >= 0 ? st.jobS : static_cast<int32_t>(m->printS));
    const float p   = m->m73;
    if (lastP_ < 0.0f){
        lastP_ = p;                         // first value: time of arrival unknown
    } else if (p < lastP_ - 0.05f){
        reset();                            // a new job
        lastP_ = p;
    } else if (p > lastP_ + 1e-4f){
        addStep(job, p);                    // a step seen live
        lastP_ = p;
    }

    e.fromM73 = true;
    e.percent = static_cast<int>(p * 100.0f + 0.5f);
    if (e.percent > 100) e.percent = 100;

    if (total_ > 0.0f){
        const float r = ratio(job);
        float rem;
        if (count_ > 0){
            const Step& n = steps_[(head_ - 1 + kSteps) % kSteps];
            rem = total_ * (1.0f - n.p) * r - (job - n.jobS);
            // Still short of the next 1 % mark: at least the rest after it
            const float floorRem = total_ * (1.0f - n.p - 0.01f) * r;
            if (rem < floorRem) rem = floorRem;
        } else {
            rem = total_ * (1.0f - p) * r;
        }
        if (rem < 0.0f) rem = 0.0f;
        e.remainingS = static_cast<int32_t>(rem + 0.5f);
        e.ratio = r;
    } else {
        e.remainingS = st.leftS;
    }
    last_ = e;
    return e;
}

}  // namespace hm
