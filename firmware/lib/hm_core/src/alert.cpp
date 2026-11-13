#include "alert.h"

namespace hm {

namespace {

// C6 E6 G6 C7, twice: a bright "job done" chime
const Note kDone[] = {
    {1047, 110}, {1319, 110}, {1568, 110}, {2093, 260}, {0, 180},
    {1047, 110}, {1319, 110}, {1568, 110}, {2093, 360}, {0, 0},
};

// Two-tone alarm, three times
const Note kAttention[] = {
    {1760, 160}, {1175, 160}, {1760, 160}, {1175, 160}, {1760, 160}, {1175, 300}, {0, 0},
};

}  // namespace

const Note* melodyFor(AlertKind k){
    switch (k){
        case AlertKind::Done:      return kDone;
        case AlertKind::Attention: return kAttention;
        default:                   return nullptr;
    }
}

AlertKind Alerts::trigger(AlertKind k, uint32_t nowMs){
    kind_    = k;
    startMs_ = nowMs;
    rounds_  = k == AlertKind::None ? 0 : 1;
    test_    = false;
    return k;
}

AlertKind Alerts::test(AlertKind k, uint32_t nowMs){
    trigger(k, nowMs);
    test_ = k != AlertKind::None;
    return k;
}

AlertKind Alerts::update(const View& v, uint32_t nowMs){
    const Screen s = v.screen;
    const bool err = v.errorCode != 0;
    AlertKind start = AlertKind::None;

    if (s != last_ || err != lastErr_){
        if (s == Screen::Done && live(last_)){
            start = AlertKind::Done;
        } else if (s == Screen::Error && last_ != Screen::Error){
            start = AlertKind::Attention;
        } else if (s == Screen::Paused && err && !(last_ == Screen::Paused && lastErr_)){
            start = AlertKind::Attention;
        }
    }
    if (test_){
        // A console test ends with its flash (the repeats fall inside it)
        if (!active(nowMs)){
            kind_ = AlertKind::None;
            test_ = false;
        }
    } else {
        // The state that raised the alert is gone: stop it
        if (kind_ == AlertKind::Done && s != Screen::Done){
            kind_ = AlertKind::None;
        }
        if (kind_ == AlertKind::Attention && !(s == Screen::Error || (s == Screen::Paused && err))){
            kind_ = AlertKind::None;
        }
    }
    last_ = s;
    lastErr_ = err;

    if (start != AlertKind::None){
        return trigger(start, nowMs);
    }

    // Repeats
    if (kind_ == AlertKind::Done && rounds_ < kDoneRounds && nowMs - startMs_ >= rounds_ * kRepeatMs){
        rounds_++;
        return AlertKind::Done;
    }
    if (kind_ == AlertKind::Attention){
        if (nowMs - startMs_ >= kAttentionMs){
            kind_ = AlertKind::None;
        } else if (nowMs - startMs_ >= rounds_ * kRepeatMs){
            rounds_++;
            return AlertKind::Attention;
        }
    }
    return AlertKind::None;
}

bool Alerts::active(uint32_t nowMs) const {
    switch (kind_){
        case AlertKind::Done:      return nowMs - startMs_ < kDoneFlashMs;
        case AlertKind::Attention: return nowMs - startMs_ < kAttentionMs;
        default:                   return false;
    }
}

void MelodyPlayer::start(const Note* m, uint32_t nowMs){
    m_ = m;
    i_ = 0;
    dueMs_ = nowMs;
}

bool MelodyPlayer::next(uint32_t nowMs, Note* note){
    if (!m_ || static_cast<int32_t>(nowMs - dueMs_) < 0){
        return false;
    }
    const Note n = m_[i_];
    if (n.hz == 0 && n.ms == 0){
        m_ = nullptr;
        return false;
    }
    i_++;
    dueMs_ = nowMs + n.ms;
    if (n.hz == 0){
        return false;   // a rest: nothing to start
    }
    *note = n;
    return true;
}

}  // namespace hm
