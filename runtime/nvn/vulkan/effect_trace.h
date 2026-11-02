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

// Ignore blend bits belonging to unused targets and tiny auxiliary cube maps.
static inline bool effect_trace_candidate(unsigned blend_mask, unsigned colors, unsigned width, unsigned height,
                                           bool depth_test, bool depth_write) {
    if (!colors || width < 160 || height < 90) return false;
    unsigned active = colors >= 8 ? 255u : (1u << colors) - 1u;
    return (blend_mask & active) || (depth_test && !depth_write);
}
