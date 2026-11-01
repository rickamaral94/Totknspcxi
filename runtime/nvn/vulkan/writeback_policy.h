#pragma once
#include <stdbool.h>
#include <stdint.h>

// CPU-visible small targets include TotK's 128x128 RGBA8 grass interaction map.
// Keep the legacy limit for other pools and the larger limit for linear targets.
#define WB_CPU_SMALL_MAX (64u * 1024)
static inline bool wb_size_allowed(uint64_t size, uint32_t pool_flags, bool linear,
                                   uint64_t legacy_max, uint64_t slot_max) {
    return size <= legacy_max || (linear && size <= slot_max) ||
           ((pool_flags & 0x6u) && size <= WB_CPU_SMALL_MAX);
}

// Sync lag must not expose stale CPU-visible targets. Never wait past this fence.
static inline uint64_t wb_small_wait_target(uint64_t target, uint64_t small_seq, uint64_t fence) {
    uint64_t required = small_seq < fence ? small_seq : fence;
    return required > target ? required : target;
}
