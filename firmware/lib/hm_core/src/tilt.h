#pragma once

// Continuous auto-rotation from the accelerometer (the gravity vector, not
// the gyro), ported from claude-monitor's AtomS3R firmware
// (components/monitor-core tilt.h). The HUD is drawn at this angle so it
// stays upright however the cube is turned; the panel's own rotation never
// changes. A low-pass filter makes the turn smooth, and a screen redraw is
// asked for only once the angle has moved by redrawStep.

namespace hm {

struct TiltConfig {
    // Board calibration: atan2(ay, ax) is the direction the accelerometer
    // reports as "up" in the sensor frame; the sign and offset absorb how
    // the IMU is mounted relative to the panel. The "status" console
    // command reports the angle in use.
    float angleSign   = -1.0f;
    float angleOffset = 0.0f;                         // radians

    float flatThreshold = 0.80f;                      // |az| above this = lying flat: hold the angle
    float tiltThreshold = 0.40f;                      // in-plane |g| below this = too noisy: hold
    float smoothing     = 0.25f;                      // per-frame low-pass factor (~130 ms at 30 fps)
    float redrawStep    = 1.0f * 3.14159265f / 180.0f;   // redraw once the angle moved this much
};

struct Tilt {
    float angle = 0.0f;    // smoothed HUD angle, radians clockwise
    float drawn = 0.0f;    // angle of the HUD currently on screen
    bool  valid = false;   // a usable reading has been seen
};

// Wraps to [-pi, pi]
float wrapAngle(float a);

// Feed one accelerometer sample (in g). Returns true when the HUD on screen
// is stale enough to be redrawn.
bool updateTilt(Tilt& t, const TiltConfig& cfg, float ax, float ay, float az);

}  // namespace hm
