#pragma once

// The IMU on the CoreS3, sampled at ~200 Hz by a task on core 0: a knock on
// the case lasts a few milliseconds, and the main loop only samples at
// ~30 Hz and pauses for each dashboard frame. The task is the only reader
// of M5.Imu; the main loop takes the results from here.
//
// On the AtomS3R this is empty: its main loop reads the IMU for the tilt.

#include <stdint.h>

namespace motion {

// Starts the task. False when there is no IMU (or not a CoreS3).
bool begin();

// The low-passed gravity vector, in g; false until the first samples
bool gravity(float* ax, float* ay, float* az);

// Counters: knocks on the case, and moves (picked up, turned)
uint32_t taps();
uint32_t moves();

}  // namespace motion
