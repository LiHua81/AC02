#ifndef ALGO_SOGI_H
#define ALGO_SOGI_H

typedef struct {
    float x_old1, x_old2;
    float va_old1, va_old2;
    float vb_old1, vb_old2;
} sogi_t;

void sogi_init(sogi_t *s);
void sogi_process_batch(sogi_t *s, const float *samples, int count,
                        float *vAlpha, float *vBeta);

#endif