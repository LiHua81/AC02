#ifndef APP_PARAMS_H
#define APP_PARAMS_H

typedef struct {
    float kp;
    float ki;
    float vref;
    float freq;
    float modulation;
    float vrms;
} params_t;

extern params_t params_shadow;
extern params_t params_active;
extern int params_pending;

void params_init(void);
void apply_params(void);

#endif