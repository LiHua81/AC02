#include "app_params.h"

params_t params_shadow = {
    .kp = 0.2f,
    .ki = 0.1f,
    .vref = 10.0f,
    .freq = 50.0f,
    .modulation = 0.0f,
    .vrms = 0.0f
};

params_t params_active = {
    .kp = 0.2f,
    .ki = 0.1f,
    .vref = 10.0f,
    .freq = 50.0f,
    .modulation = 0.0f,
    .vrms = 0.0f
};

int params_pending = 0;

void params_init(void) {
}

void apply_params(void) {
    params_active.kp = params_shadow.kp;
    params_active.ki = params_shadow.ki;
    params_active.vref = params_shadow.vref;
    params_active.freq = params_shadow.freq;
    params_pending = 0;
}