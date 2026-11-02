#pragma once
#include <stdint.h>
#include <stddef.h>

typedef struct {
    const uint8_t *src;
    uint64_t addr, len, seq;
    uint8_t *shadow;
    const uint64_t *bseq;
} GcBack;

// Caller holds gc_lock. A cache hint is validated against the actual queue, so
// compaction or index reuse can only cause a miss, never a false duplicate.
// Only arena entries coalesce; transient copies and different fences stay intact.
// Returns 1 added, 0 exact duplicate, -1 full. No waits, allocation or reordering.
static inline int wb_queue_add(GcBack *q, int *count, int capacity, int hints[256], GcBack v) {
    uint64_t h = (((uint64_t)(uintptr_t)v.src >> 8) * 0x9e3779b97f4a7c15ull) +
                 ((v.addr >> 8) * 0xbf58476d1ce4e5b9ull) + v.len + v.seq * 0x94d049bb133111ebull;
    unsigned slot = (unsigned)(h ^ (h >> 32)) & 255;
    int i = hints[slot];
    if (v.shadow && i >= 0 && i < *count && q[i].src == v.src && q[i].addr == v.addr &&
        q[i].len == v.len && q[i].seq == v.seq && q[i].shadow == v.shadow && q[i].bseq == v.bseq) {
        int safe = 1;
        for (int j=i+1; j<*count; j++) {
            // Keep A,B,A ordering when B can write any byte of A's guest range.
            if (q[j].addr <= v.addr ? v.addr-q[j].addr < q[j].len : q[j].addr-v.addr < v.len) { safe = 0; break; }
        }
        if (safe) return 0;
    }
    if (*count == capacity) return -1;
    hints[slot] = *count;
    q[(*count)++] = v;
    return 1;
}
