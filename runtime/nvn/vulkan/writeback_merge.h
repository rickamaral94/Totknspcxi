#pragma once
#include <stdint.h>
#include <string.h>

// Called with the writeback lock held, after the submission's fence completed.
// An equal 4-KiB GPU/shadow region needs no per-block CPU checks. Changed regions
// retain the original 256-byte conflict policy, including newer-upload protection.
static inline uint64_t gc_merge_writeback(uint8_t *guest, uint8_t *shadow, const uint8_t *gpu,
                                      uint64_t len, const uint64_t *block_seq, uint64_t seq) {
    uint64_t unchanged = 0;
    for (uint64_t base = 0; base < len;) {
        uint64_t end = len - base > 4096 ? base + 4096 : len;
        int eligible = 1;
        for (uint64_t b = base; b < end; b += 256)
            if (block_seq[b / 256] > seq) { eligible = 0; break; }
        if (eligible && !memcmp(gpu + base, shadow + base, (size_t)(end - base))) {
            unchanged += end - base;
        } else {
            for (uint64_t b = base; b < end; b += 256) {
                uint64_t n = end - b < 256 ? end - b : 256;
                if (block_seq[b / 256] > seq) continue;
                if (!memcmp(gpu + b, shadow + b, (size_t)n) || memcmp(guest + b, shadow + b, (size_t)n)) continue;
                memcpy(guest + b, gpu + b, (size_t)n);
                memcpy(shadow + b, gpu + b, (size_t)n);
            }
        }
        base = end;
    }
    return unchanged;
}
