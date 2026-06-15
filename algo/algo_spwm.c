#include "algo_spwm.h"

static float clamp(float val, float min, float max) {
    if (val < min) return min;
    if (val > max) return max;
    return val;
}

void spwm_compute(float mod, float sin_theta, float *duty_a, float *duty_b) {
    if (sin_theta >= 0.0f) {
        *duty_a = clamp(mod * sin_theta, 0.05f, 0.95f);
        *duty_b = 0.05f;
    } else {
        *duty_a = 0.05f;
        *duty_b = clamp(mod * (-sin_theta), 0.05f, 0.95f);
    }
}