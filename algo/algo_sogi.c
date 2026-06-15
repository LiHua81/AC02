#include "algo_sogi.h"

#define SOGI_A1  1.93814003f
#define SOGI_A2 -0.93909647f
#define SOGI_BD0 0.03045174f
#define SOGI_BD2 -0.03045174f
#define SOGI_BQ0 0.00047833f
#define SOGI_BQ1 0.00095667f
#define SOGI_BQ2 0.00047833f

void sogi_init(sogi_t *s) {
    s->x_old1 = 0.0f;
    s->x_old2 = 0.0f;
    s->va_old1 = 0.0f;
    s->va_old2 = 0.0f;
    s->vb_old1 = 0.0f;
    s->vb_old2 = 0.0f;
}

void sogi_process_batch(sogi_t *s, const float *samples, int count,
                        float *vAlpha, float *vBeta) {
    float v_alpha = 0.0f, v_beta = 0.0f;
    int i;

    for (i = 0; i < count; i++) {
        v_alpha = SOGI_BD0 * samples[i] + SOGI_BD2 * s->x_old2 +
                  SOGI_A1 * s->va_old1 + SOGI_A2 * s->va_old2;

        v_beta = SOGI_BQ0 * samples[i] + SOGI_BQ1 * s->x_old1 + SOGI_BQ2 * s->x_old2 +
                 SOGI_A1 * s->vb_old1 + SOGI_A2 * s->vb_old2;

        s->x_old2 = s->x_old1;
        s->x_old1 = samples[i];
        s->va_old2 = s->va_old1;
        s->va_old1 = v_alpha;
        s->vb_old2 = s->vb_old1;
        s->vb_old1 = v_beta;
    }

    *vAlpha = v_alpha;
    *vBeta = v_beta;
}