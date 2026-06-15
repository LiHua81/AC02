#include "algo_pi.h"

void pi_init(pi_t *p) {
    p->kp = 0.2f;
    p->ki = 0.1f;
    p->dt = 0.005f;
    p->integral = 0.0f;
    p->min_out = -0.95f;
    p->max_out = 0.95f;
}

void pi_set_gains(pi_t *p, float kp, float ki, float dt) {
    p->kp = kp;
    p->ki = ki;
    p->dt = dt;
}

void pi_set_limits(pi_t *p, float min, float max) {
    p->min_out = min;
    p->max_out = max;
}

float pi_update(pi_t *p, float ref, float fb) {
    float error = ref - fb;
    float P = p->kp * error;
    float I_temp = p->integral + p->ki * error * p->dt;
    float Out_temp = P + I_temp;
    float Out, I;

    if (Out_temp > p->max_out) {
        Out = p->max_out;
        I = p->max_out - P;
    } else if (Out_temp < p->min_out) {
        Out = p->min_out;
        I = p->min_out - P;
    } else {
        Out = Out_temp;
        I = I_temp;
    }

    p->integral = I;
    return Out;
}