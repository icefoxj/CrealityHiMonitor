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

int roundInt(float f){
    return static_cast<int>(f >= 0.0f ? f + 0.5f : f - 0.5f);
}

// Text for the mech font: upper case, '_' as a space, accented Latin
// letters (UTF-8 U+00C0..U+00FF) without the accent, other non-ASCII bytes
// dropped (0xB0 would draw as a degree sign), trailing spaces trimmed
void fontText(const char* s, int len, char* out, int n){
    static const char kFold[] = "AAAAAAACEEEEIIIIDNOOOOOXOUUUUYTS"    // U+00C0..U+00DF
                                "AAAAAAACEEEEIIIIDNOOOOO/OUUUUYTY";   // U+00E0..U+00FF
    int j = 0;
    for (int i = 0; i < len && j < n - 1; i++){
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c == 0xC3 && i + 1 < len){
            const unsigned char c2 = static_cast<unsigned char>(s[i + 1]);
            if (c2 >= 0x80 && c2 <= 0xBF){
                out[j++] = kFold[c2 - 0x80];
                i++;
                continue;
            }
        }
        if (c >= 0x80){
            continue;
        }
        out[j++] = c == '_' ? ' ' : static_cast<char>(toupper(c));
    }
    while (j > 0 && out[j - 1] == ' '){
        j--;
    }
    out[j] = 0;
}

bool endsWithNoCase(const char* s, int len, const char* tail){
    const int tl = static_cast<int>(strlen(tail));
    if (len < tl){
        return false;
    }
    for (int i = 0; i < tl; i++){
        if (tolower(static_cast<unsigned char>(s[len - tl + i])) != tail[i]){
            return false;
        }
    }
    return true;
}

}  // namespace

int32_t parseSliceTime(const char* s, int len){
    int32_t total = 0;
    int32_t num = 0;
    bool digits = false;
    bool any = false;
    for (int i = 0; i < len; i++){
        const char c = s[i];
        if (c >= '0' && c <= '9'){
            num = num * 10 + (c - '0');
            digits = true;
            if (num > 100000){
                return -1;
            }
            continue;
        }
        if (!digits){
            return -1;
        }
        switch (c){
            case 'd': total += num * 86400; break;
            case 'h': total += num * 3600; break;
            case 'm': total += num * 60; break;
            case 's': total += num; break;
            default:  return -1;
        }
        num = 0;
        digits = false;
        any = true;
    }
    return (any && !digits) ? total : -1;
}

void splitJobFile(const char* file, char* name, int nameLen, char* material, int materialLen,
                  int32_t* sliceS){
    name[0] = 0;
    material[0] = 0;
    *sliceS = -1;
    if (!file || !*file){
        return;
    }
    int end = static_cast<int>(strlen(file));
    if (endsWithNoCase(file, end, ".gcode")){
        end -= 6;
    }
    // "..._<time>": the slicer's estimate
    int us = end - 1;
    while (us >= 0 && file[us] != '_') us--;
    if (us > 0){
        const int32_t t = parseSliceTime(file + us + 1, end - us - 1);
        if (t >= 0){
            *sliceS = t;
            end = us;
            // "..._<material>" before it: short, letters, digits and '-'
            int um = end - 1;
            while (um >= 0 && file[um] != '_') um--;
            const int ml = end - um - 1;
            bool ok = um > 0 && ml >= 2 && ml < materialLen;
            for (int i = um + 1; ok && i < end; i++){
                const char c = file[i];
                ok = isalnum(static_cast<unsigned char>(c)) || c == '-';
            }
            if (ok){
                fontText(file + um + 1, ml, material, materialLen);
                end = um;
            }
        }
    }
    // The model's own extension: "NovoCubo2_base.stl"
    static const char* const kModelExt[] = {".stl", ".step", ".stp", ".3mf", ".obj"};
    for (const char* ext : kModelExt){
        if (endsWithNoCase(file, end, ext)){
            end -= static_cast<int>(strlen(ext));
            break;
        }
    }
    fontText(file, end, name, nameLen);
}

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
            if (st.file[0]) snprintf(lastFile_, sizeof(lastFile_), "%s", st.file);
            if (st.usedMm >= 0) lastUsedMm_ = st.usedMm;
            if (st.startEpoch > 0) lastStart_ = st.startEpoch;
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
            snprintf(lastFile_, sizeof(lastFile_), "%s", st.file);
            lastUsedMm_ = st.usedMm;
            lastStart_ = st.startEpoch;
        }
        if (st.usedMm > lastUsedMm_) lastUsedMm_ = st.usedMm;
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

    // Printer details, on every online screen
    Details& d = v.d;
    shortName(st.hostname, d.printer, sizeof(d.printer));
    fontText(st.model, static_cast<int>(strlen(st.model)), d.model, sizeof(d.model));
    d.nozzleTarget = roundTemp(st.nozzleTarget);
    d.bedTarget    = roundTemp(st.bedTarget);
    d.nozzleMax    = st.nozzleMax;
    d.bedMax       = st.bedMax;
    if (st.boxMax > 0){
        d.chamber       = roundTemp(st.box);
        d.chamberTarget = roundTemp(st.boxTarget);
    }

    if (ended_ != End::None){
        v.screen     = ended_ == End::Done ? Screen::Done : (ended_ == End::Failed ? Screen::Error : Screen::Stopped);
        v.percent    = ended_ == End::Done ? 100 : lastPercent_;
        v.elapsedS   = lastElapsed_;
        v.remainingS = -1;
        v.layer      = -1;   // no layer line on the end screens
        v.layers     = lastLayers_;
        clockAt(endedEpoch_, localTime, v.eta, sizeof(v.eta));
        splitJobFile(lastFile_, d.job, sizeof(d.job), d.material, sizeof(d.material), &d.sliceS);
        d.totalS = lastElapsed_;
        d.usedDm = lastUsedMm_ >= 0 ? (lastUsedMm_ + 50) / 100 : -1;
        clockAt(lastStart_, localTime, d.start, sizeof(d.start));
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
            splitJobFile(st.file, d.job, sizeof(d.job), d.material, sizeof(d.material), &d.sliceS);
            d.totalS  = (st.jobS >= 0 && est.remainingS >= 0) ? st.jobS + est.remainingS : -1;
            clockAt(st.startEpoch, localTime, d.start, sizeof(d.start));
            d.speed   = st.speed >= 0.0f ? roundInt(st.speed) : -1;
            d.flow10  = st.flow >= 0.0f ? roundInt(st.flow * 10.0f) : -1;
            d.feedPct = st.feedPct;
            d.flowPct = st.flowPct;
            d.fanPart = st.fanPart;
            d.fanAux  = st.fanAux;
            d.fanCase = st.fanCase;
            d.z100    = st.z > -999.0f ? roundInt(st.z * 100.0f) : -1;
            d.usedDm  = st.usedMm >= 0 ? (st.usedMm + 50) / 100 : -1;
            d.filePct = st.progress;
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
