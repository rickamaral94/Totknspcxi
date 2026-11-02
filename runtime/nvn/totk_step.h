// Timing policy adapted from valentderah/totk-pc-recomp runtime/frame_rate.c
// (MIT; see docs/PC-TIMING-AMARAL15.md for sources and license).
#pragma once
#include <stdint.h>
typedef struct { uint64_t last; double sample[5]; unsigned n, next, window; } TotkStep;
static inline int totk_step_update(TotkStep *s, uint64_t now, unsigned window, float *out) {
    if (window != 1 && window != 5) window = 5;
    uint64_t previous = s->last;
    s->last = now;
    if (s->window != window) { s->n = s->next = 0; s->window = window; }
    if (!previous || now <= previous) { s->n = s->next = 0; return 0; }
    double dt = (now - previous) * 1e-9;
    if (dt > 0.25 || dt < 0.001) { s->n = s->next = 0; return 0; }
    if (dt > 1.0 / 15.0) dt = 1.0 / 15.0;
    s->sample[s->next++ % window] = dt;
    if (s->n < window) s->n++;
    double sum = 0;
    for (unsigned i = 0; i < s->n; i++) sum += s->sample[i];
    double step = sum / s->n;
    if (step < 1.0 / 240.0) step = 1.0 / 240.0;
    *out = (float)step;
    return 1;
}
