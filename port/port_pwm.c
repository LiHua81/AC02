#include "port_pwm.h"
#include "stm32f1xx_hal.h"

extern TIM_HandleTypeDef htim8;

#define PWM_MIN_DUTY 0.05f
#define PWM_MAX_DUTY 0.95f

static float clamp(float val, float min, float max) {
    if (val < min) return min;
    if (val > max) return max;
    return val;
}

void pwm_start(void) {
    HAL_TIM_PWM_Start(&htim8, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&htim8, TIM_CHANNEL_2);
    HAL_TIMEx_PWMN_Start(&htim8, TIM_CHANNEL_1);
    HAL_TIMEx_PWMN_Start(&htim8, TIM_CHANNEL_2);
}

void pwm_stop(void) {
    HAL_TIM_PWM_Stop(&htim8, TIM_CHANNEL_1);
    HAL_TIM_PWM_Stop(&htim8, TIM_CHANNEL_2);
    HAL_TIMEx_PWMN_Stop(&htim8, TIM_CHANNEL_1);
    HAL_TIMEx_PWMN_Stop(&htim8, TIM_CHANNEL_2);
}

static void set_compare(uint32_t channel, float duty) {
    uint32_t arr = __HAL_TIM_GET_AUTORELOAD(&htim8);
    duty = clamp(duty, PWM_MIN_DUTY, PWM_MAX_DUTY);
    __HAL_TIM_SET_COMPARE(&htim8, channel, (uint32_t)(duty * arr));
}

void pwm_ch1_set(float duty) {
    set_compare(TIM_CHANNEL_1, duty);
}

void pwm_ch2_set(float duty) {
    set_compare(TIM_CHANNEL_2, duty);
}