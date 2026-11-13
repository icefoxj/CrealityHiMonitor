#include "job.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

namespace hm {

namespace {

bool isActive(Phase p){
    return p == Phase::Preparing || p == Phase::Printing || p == Phase::Paused || p == Phase::Stopping;
}

int roundTemp(float t){
    if (t <= -999.0f){
        return -1000;
    }
    return static_cast<int>(t >= 0.0f ? t + 0.5f : t - 0.5f);
}

// "Creality Hi-5EBC" -> "HI-5EBC"; upper case, unknown characters as spaces
void shortName(const char* host, char* out, int n){
    const char* s = host;
    const char* prefix = "Creality ";
    if (strncmp(s, prefix, strlen(prefix)) == 0){
        s += strlen(prefix);
    }
    int i = 0;
    for (; *s && i < n - 1; s++){
        out[i++] = static_cast<char>(toupper(static_cast<unsigned char>(*s)));
    }
    out[i] = 0;
}

void clockAt(int64_t epoch, LocalTimeFn fn, char* out, int n){
    out[0] = 0;
    int h, m;
    if (epoch > 0 && fn && fn(epoch, &h, &m)){
        formatClock(h, m, out, n);
    }
}

}  // namespace

void JobView::update(const PrinterStatus& st, const Estimate& est, uint32_t nowS, int64_t epoch){
    const Phase p = phaseOf(st);

    if (isActive(p)){
        if (!active_ || ended_ != End::None){
            ended_ = End::None;   // a new job clears the last one
        }
        active_ = true;
        if (p != Phase::Stopping){
            if (est.percent >= 0) lastPercent_ = est.percent;
            if (st.jobS >= 0) lastElapsed_ = st.jobS;
            lastLayers_ = st.layers;
        }
        if (p == Phase::Stopping && ended_ == End::None){
            ended_ = End::Stopped;
            endedAt_ = nowS;
            endedEpoch_ = epoch;
        }
        return;
    }

    End end = End::None;
    switch (p){
        case Phase::Complete: end = End::Done; break;
        case Phase::Aborted:  end = End::Stopped; break;
        case Phase::Failed:   end = End::Failed; break;
        case Phase::Idle:
            // Back to idle without us seeing the end state (it is short)
            if (active_){
                end = lastPercent_ >= 99 ? End::Done : End::Stopped;
            }
            break;
        default: break;
    }
    if (end != End::None && (ended_ == End::None || (ended_ == End::Stopped && end != End::Stopped))){
        if (ended_ == End::None){
            endedAt_ = nowS;
            endedEpoch_ = epoch;
        }
        ended_ = end;
        // The final print time (the idle state may already have reset it)
        if (st.jobS > lastElapsed_) lastElapsed_ = st.jobS;
        if (!active_){
            // Booted into the end state: take what the printer still shows
            if (st.jobS >= 0) lastElapsed_ = st.jobS;
            lastPercent_ = end == End::Done ? 100 : st.progress;
            lastLayers_ = st.layers;
        }
    }
    if (p != Phase::Unknown){
        active_ = false;
    }
    if (ended_ != End::None && nowS - endedAt_ >= kEndHoldS){
        ended_ = End::None;
    }
}

void JobView::fill(View& v, const PrinterStatus& st, const Estimate& est, int64_t epoch, LocalTimeFn localTime) const {
    const Phase p = phaseOf(st);
    v.nozzle    = roundTemp(st.nozzle);
    v.bed       = roundTemp(st.bed);
    v.errorCode = st.errorCode;
    v.layer     = st.layer;
    v.layers    = st.layers;
    v.heating   = false;
    v.eta[0]    = 0;
    clockAt(epoch, localTime, v.clock, sizeof(v.clock));

    if (ended_ != End::None){
        v.screen     = ended_ == End::Done ? Screen::Done : (ended_ == End::Failed ? Screen::Error : Screen::Stopped);
        v.percent    = ended_ == End::Done ? 100 : lastPercent_;
        v.elapsedS   = lastElapsed_;
        v.remainingS = -1;
        v.layer      = -1;   // no layer line on the end screens
        v.layers     = lastLayers_;
        clockAt(endedEpoch_, localTime, v.eta, sizeof(v.eta));
        return;
    }

    switch (p){
        case Phase::Preparing:
        case Phase::Printing:
        case Phase::Paused:
            v.screen     = p == Phase::Paused ? Screen::Paused : Screen::Printing;
            v.heating    = p == Phase::Preparing;
            v.percent    = est.percent;
            v.remainingS = est.remainingS;
            v.elapsedS   = st.jobS;
            if (est.remainingS >= 0 && p != Phase::Paused){
                clockAt(epoch + est.remainingS, localTime, v.eta, sizeof(v.eta));
            }
            break;
        default:
            v.screen     = Screen::Idle;
            v.percent    = -1;
            v.remainingS = -1;
            v.elapsedS   = -1;
            v.layer      = -1;
            shortName(st.hostname, v.line1, sizeof(v.line1));
            break;
    }
}

}  // namespace hm
