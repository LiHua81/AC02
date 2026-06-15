#ifndef ALGO_PI_H
#define ALGO_PI_H

typedef struct {
    float kp, ki;
    float dt;
    float integral;
    float min_out, max_out;
} pi_t;

void pi_init(pi_t *p);
void pi_set_gains(pi_t *p, float kp, float ki, float dt);
void pi_set_limits(pi_t *p, float min, float max);
float pi_update(pi_t *p, float ref, float fb);

#endif