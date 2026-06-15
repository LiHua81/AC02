#ifndef PORT_PWM_H
#define PORT_PWM_H

void pwm_start(void);
void pwm_stop(void);
void pwm_ch1_set(float duty);
void pwm_ch2_set(float duty);

#endif