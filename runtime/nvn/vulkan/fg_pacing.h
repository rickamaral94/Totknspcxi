#pragma once
#include <stdint.h>

typedef struct { uint64_t target, skipped; int reset; } FgSlot;
// Keep the phase of a live output grid when a deadline is missed. No waits or
// extra images: skipped counts time slots, not generated frames discarded.
static inline FgSlot fg_slot(uint64_t next, uint64_t now, uint64_t period) {
    uint64_t earliest = now + 2000000ull;
    if (!period || !next) return (FgSlot){earliest, 0, 1};
    if (next >= earliest) {
        if (next - earliest < 2 * period) return (FgSlot){next, 0, 0};
        return (FgSlot){earliest, 0, 1}; // bound a queue too far ahead
    }
    uint64_t behind = earliest - next;
    if (behind > 250000000ull) return (FgSlot){earliest, 0, 1}; // pause / mode change
    uint64_t steps = behind / period + (behind % period != 0);
    return (FgSlot){next + steps * period, steps, 0};
}
