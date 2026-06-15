#ifndef ALGO_PHASE_GEN_H
#define ALGO_PHASE_GEN_H

typedef struct {
    float phase_accum;
    float freq_step;
} phase_gen_t;

void phase_init(phase_gen_t *p);
void phase_advance(phase_gen_t *p);
void phase_set_freq(phase_gen_t *p, float hz);
float phase_sin(const phase_gen_t *p);
float phase_cos(const phase_gen_t *p);

#endif