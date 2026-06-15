#include "app_control.h"
#include "app_params.h"
#include "port_pwm.h"
#include "port_adc.h"
#include "algo_phase_gen.h"
#include "algo_spwm.h"
#include "algo_sogi.h"
#include "algo_pi.h"
#include "algo_park.h"
#include "stm32f1xx_hal.h"
#include <math.h>

static phase_gen_t phase;
static sogi_t sogi;
static pi_t pi;
static float samples[50];

void app_control_init(void) {
    phase_init(&phase);
    sogi_init(&sogi);
    pi_init(&pi);
    pi_set_gains(&pi, params_active.kp, params_active.ki, 0.005f);
    pi_set_limits(&pi, -0.95f, 0.95f);
    phase_set_freq(&phase, params_active.freq);
}

void app_on_10khz(void) {
    float mod_active = params_shadow.modulation;
    float sin_theta, duty_a, duty_b;
    phase_advance(&phase);
    sin_theta = phase_sin(&phase);
    spwm_compute(mod_active, sin_theta, &duty_a, &duty_b);
    pwm_ch1_set(duty_a);
    pwm_ch2_set(duty_b);
}

void app_on_200hz(void) {
    float vAlpha, vBeta, cosT, sinT, vD, mod;

    __disable_irq();
    adc_get_batch(samples, 50);
    sogi_process_batch(&sogi, samples, 50, &vAlpha, &vBeta);
    __enable_irq();

    cosT = phase_cos(&phase);
    sinT = phase_sin(&phase);

    vD = park_vd(vAlpha, vBeta, cosT, sinT);
    mod = pi_update(&pi, params_active.vref, vD);
    params_shadow.modulation = mod;
    params_shadow.vrms = fabsf(vD) * 0.7071f;  /* |vD| / √2 = RMS */

    if (params_pending) {
        apply_params();
        pi_set_gains(&pi, params_active.kp, params_active.ki, 0.005f);
        phase_set_freq(&phase, params_active.freq);
        params_pending = 0;
    }
}