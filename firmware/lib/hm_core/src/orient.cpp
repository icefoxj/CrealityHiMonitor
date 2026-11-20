#include "orient.h"

#include <math.h>

#include "tilt.h"

namespace hm {

namespace {
constexpr float kPi  = 3.14159265f;
constexpr float kDeg = kPi / 180.0f;
}  // namespace

bool Orient::update(float ax, float ay, float az, uint32_t nowMs, const OrientConfig& cfg){
    // Flat on the desk, or too little gravity in the panel's plane: hold
    if (fabsf(az) > cfg.flatG || sqrtf(ax * ax + ay * ay) < cfg.minG){
        pending_ = -1;
        return false;
    }
    angle_ = wrapAngle(cfg.sign * atan2f(ay, ax) + cfg.offset);
    // The nearest quarter, if the angle is clearly inside it
    const float qf = angle_ / (0.5f * kPi);
    const int   q  = static_cast<int>(floorf(qf + 0.5f)) & 3;
    const float off = fabsf(wrapAngle(angle_ - q * 0.5f * kPi));   // 0 .. 45 degrees
    if (q == q_ || off > (45.0f - cfg.marginDeg) * kDeg){
        pending_ = -1;
        return false;
    }
    if (pending_ != q){
        pending_ = q;
        since_ = nowMs;
        return false;
    }
    if (nowMs - since_ < cfg.settleMs){
        return false;
    }
    q_ = q;
    pending_ = -1;
    return true;
}

bool Orient::snap(float ax, float ay, float az, const OrientConfig& cfg){
    if (fabsf(az) > cfg.flatG || sqrtf(ax * ax + ay * ay) < cfg.minG){
        return false;
    }
    angle_ = wrapAngle(cfg.sign * atan2f(ay, ax) + cfg.offset);
    q_ = static_cast<int>(floorf(angle_ / (0.5f * kPi) + 0.5f)) & 3;
    pending_ = -1;
    return true;
}

void MotionDetector::update(float ax, float ay, float az, float gx, float gy, float gz, uint32_t nowMs,
                            const MotionConfig& cfg){
    if (!primed_){
        g_[0] = ax;
        g_[1] = ay;
        g_[2] = az;
        primed_ = true;
        return;
    }
    // Dynamic acceleration: the sample minus the slow gravity estimate
    const float dx = ax - g_[0], dy = ay - g_[1], dz = az - g_[2];
    const float dyn = sqrtf(dx * dx + dy * dy + dz * dz);
    // Gravity follows slowly, and not during a knock (nor its first sample)
    if (!inTap_ && dyn <= cfg.tapG){
        g_[0] += cfg.gravityAlpha * dx;
        g_[1] += cfg.gravityAlpha * dy;
        g_[2] += cfg.gravityAlpha * dz;
    }

    // A knock: a sharp spike that is over quickly
    if (!inTap_){
        if (dyn > cfg.tapG){
            inTap_ = true;
            tapStart_ = nowMs;
        }
    } else if (dyn < cfg.quietG){
        inTap_ = false;
        if (nowMs - tapStart_ <= cfg.tapMaxMs && (!tapSeen_ || nowMs - lastTap_ >= cfg.tapGapMs)){
            taps_++;
            lastTap_ = nowMs;
            tapSeen_ = true;
        }
    } else if (nowMs - tapStart_ > cfg.tapMaxMs){
        inTap_ = false;   // a long shake: handling, which the move test covers
    }

    // A move: turning, or a sustained push
    const float rate = sqrtf(gx * gx + gy * gy + gz * gz);
    bool moving = rate > cfg.moveDps;
    if (dyn > cfg.moveG){
        if (!pushing_){
            pushing_ = true;
            moveFrom_ = nowMs;
        }
        if (nowMs - moveFrom_ >= cfg.moveHoldMs) moving = true;
    } else {
        pushing_ = false;
    }
    if (moving && (!moveSeen_ || nowMs - lastMove_ >= cfg.moveGapMs)){
        moves_++;
        lastMove_ = nowMs;
        moveSeen_ = true;
    }
}

}  // namespace hm
