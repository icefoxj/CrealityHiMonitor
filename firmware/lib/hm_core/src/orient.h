#pragma once

// Motion on a rectangular panel that stands on a desk (the CoreS3):
//
// - Orient: which of the four quarter turns is "up", from the gravity
//   vector, with hysteresis and a settle time, held while the device lies
//   flat. The firmware turns the display by quarters: landscape either
//   way up, or portrait on either side.
// - MotionDetector: fed at ~200 Hz from the accelerometer and gyro. It
//   keeps a low-passed gravity vector, counts "moves" (the device is
//   picked up or turned: wakes a dimmed screen) and "taps" (a knock on the
//   case: silences an alert).
//
// Plain C++, tested on the host.

#include <stdint.h>

namespace hm {

struct OrientConfig {
    // atan2(ay, ax) of gravity in the sensor frame, times sign plus offset,
    // is the angle the panel is turned from upright (radians, clockwise),
    // the same mapping as the AtomS3R tilt (TiltConfig)
    float    sign      = -1.0f;
    float    offset    = 0.0f;
    float    flatG     = 0.80f;   // |az| above this: lying flat, hold
    float    minG      = 0.45f;   // in-plane |g| below this: too unsure, hold
    float    marginDeg = 15.0f;   // a new quarter must be this far past the 45-degree line
    uint32_t settleMs  = 600;     // and stay there this long
};

class Orient {
public:
    // One gravity sample (g). Returns true when the quarter changed.
    bool update(float ax, float ay, float az, uint32_t nowMs, const OrientConfig& cfg);

    // At boot: take the nearest quarter at once, without the settle time.
    // False (quarter unchanged) when the device lies flat.
    bool snap(float ax, float ay, float az, const OrientConfig& cfg);

    int  quarter() const { return q_; }   // 0..3 quarter turns clockwise from upright
    float angle() const { return angle_; }   // last usable angle, radians
    void reset(int q = 0){ q_ = q & 3; pending_ = -1; }

private:
    int      q_       = 0;
    int      pending_ = -1;
    uint32_t since_   = 0;
    float    angle_   = 0.0f;
};

struct MotionConfig {
    float    gravityAlpha = 0.04f;   // low-pass per sample (~120 ms at 200 Hz)
    float    tapG         = 0.9f;    // a knock: the dynamic acceleration jumps over this...
    float    quietG       = 0.25f;   // ...and falls back under this...
    uint32_t tapMaxMs     = 80;      // ...within this time (longer = handling, not a knock)
    uint32_t tapGapMs     = 300;     // one knock per this time
    float    moveDps      = 30.0f;   // turning faster than this (deg/s) is a move
    float    moveG        = 0.30f;   // or a dynamic acceleration over this, held...
    uint32_t moveHoldMs   = 60;      // ...this long
    uint32_t moveGapMs    = 500;     // one move per this time
};

class MotionDetector {
public:
    // Accelerometer in g, gyro in deg/s
    void update(float ax, float ay, float az, float gx, float gy, float gz, uint32_t nowMs,
                const MotionConfig& cfg);

    uint32_t taps() const { return taps_; }     // counters: compare with the last value seen
    uint32_t moves() const { return moves_; }
    void gravity(float* x, float* y, float* z) const { *x = g_[0]; *y = g_[1]; *z = g_[2]; }
    bool primed() const { return primed_; }

private:
    float    g_[3]     = {0.0f, 0.0f, 0.0f};
    bool     primed_   = false;
    bool     inTap_    = false;
    uint32_t tapStart_ = 0;
    uint32_t lastTap_  = 0;
    bool     tapSeen_  = false;
    bool     pushing_  = false; // the dynamic acceleration is over moveG...
    uint32_t moveFrom_ = 0;     // ...since then
    uint32_t lastMove_ = 0;
    bool     moveSeen_ = false;
    uint32_t taps_     = 0;
    uint32_t moves_    = 0;
};

}  // namespace hm
