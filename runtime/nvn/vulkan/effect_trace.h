#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

// Render-thread only. A bounded burst at a GPU profiling point, followed by two
// adjacent frames. No queries, readbacks or waits are added by this capture.
typedef struct {
    uint64_t frame, end, keys[64];
    unsigned bursts, rows;
    bool valid, active, full;
} EffectTrace;
static inline bool effect_trace_frame(EffectTrace *s, uint64_t frame, bool trigger) {
    if (s->valid && s->frame == frame) return s->active;
    s->valid = true;
    s->frame = frame;
    if (trigger && s->bursts < 3 && frame >= s->end) {
        s->end = frame + 3;
        s->bursts++;
    }
    s->active = frame < s->end;
    s->rows = 0;
    s->full = false;
    return s->active;
}
static inline bool effect_trace_keep(EffectTrace *s, uint64_t key) {
    for (unsigned i=0; i<s->rows; i++) if (s->keys[i] == key) return false;
    if (s->rows == 64) { s->full = true; return false; }
    s->keys[s->rows++] = key;
    return true;
}
