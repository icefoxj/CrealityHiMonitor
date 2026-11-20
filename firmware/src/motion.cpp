#include "motion.h"

#if defined(HM_BOARD_CORES3)

#include <M5Unified.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "orient.h"

namespace motion {

namespace {

constexpr uint32_t kPeriodMs = 5;   // 200 Hz

portMUX_TYPE       s_mux = portMUX_INITIALIZER_UNLOCKED;
hm::MotionDetector s_det;           // the task's own
hm::MotionConfig   s_cfg;
float              s_g[3] = {0.0f, 0.0f, 0.0f};
bool               s_primed = false;
uint32_t           s_taps = 0;
uint32_t           s_moves = 0;

void task(void*){
    TickType_t wake = xTaskGetTickCount();
    for (;;){
        // update() reads the sensor (the internal I2C bus is locked per
        // transaction, so the touch and power reads on core 1 are safe)
        if (M5.Imu.update()){
            float ax, ay, az, gx, gy, gz;
            M5.Imu.getAccel(&ax, &ay, &az);
            M5.Imu.getGyro(&gx, &gy, &gz);
            s_det.update(ax, ay, az, gx, gy, gz, millis(), s_cfg);
            float g[3];
            s_det.gravity(&g[0], &g[1], &g[2]);
            portENTER_CRITICAL(&s_mux);
            s_g[0] = g[0];
            s_g[1] = g[1];
            s_g[2] = g[2];
            s_primed = s_det.primed();
            s_taps = s_det.taps();
            s_moves = s_det.moves();
            portEXIT_CRITICAL(&s_mux);
        }
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(kPeriodMs));
    }
}

}  // namespace

bool begin(){
    if (!M5.Imu.isEnabled()){
        return false;
    }
    return xTaskCreatePinnedToCore(task, "imu", 4096, nullptr, 2, nullptr, 0) == pdPASS;
}

bool gravity(float* ax, float* ay, float* az){
    portENTER_CRITICAL(&s_mux);
    const bool ok = s_primed;
    *ax = s_g[0];
    *ay = s_g[1];
    *az = s_g[2];
    portEXIT_CRITICAL(&s_mux);
    return ok;
}

uint32_t taps(){
    portENTER_CRITICAL(&s_mux);
    const uint32_t n = s_taps;
    portEXIT_CRITICAL(&s_mux);
    return n;
}

uint32_t moves(){
    portENTER_CRITICAL(&s_mux);
    const uint32_t n = s_moves;
    portEXIT_CRITICAL(&s_mux);
    return n;
}

}  // namespace motion

#else

namespace motion {

bool begin(){ return false; }
bool gravity(float*, float*, float*){ return false; }
uint32_t taps(){ return 0; }
uint32_t moves(){ return 0; }

}  // namespace motion

#endif
