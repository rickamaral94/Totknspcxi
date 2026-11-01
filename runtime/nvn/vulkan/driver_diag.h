#pragma once
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <time.h>

// Diagnostic build: one begin/end pair around each driver compilation, not per draw.
static atomic_ullong driver_diag_sequence;
static atomic_uint driver_diag_active;
static unsigned long long driver_diag_begin(const char *kind, uint64_t a, uint64_t b) {
    unsigned long long id = atomic_fetch_add(&driver_diag_sequence, 1) + 1;
    unsigned active = atomic_fetch_add(&driver_diag_active, 1) + 1;
    unsigned long rss = 0, vm = 0, available = 0;
    char line[256];
    FILE *f = fopen("/proc/self/status", "r");
    if (f) {
        while (fgets(line, sizeof line, f)) {
            if (sscanf(line, "VmRSS: %lu kB", &rss) == 1) continue;
            if (sscanf(line, "VmSize: %lu kB", &vm) == 1) continue;
        }
        fclose(f);
    }
    f = fopen("/proc/meminfo", "r");
    if (f) {
        while (fgets(line, sizeof line, f)) if (sscanf(line, "MemAvailable: %lu kB", &available) == 1) break;
        fclose(f);
    }
    fprintf(stderr, "[driver-diag] begin id=%llu tid=%ld kind=%s shader=%llx aux=%llx active=%u rss_kb=%lu vm_kb=%lu available_kb=%lu\n",
            id, (long)syscall(SYS_gettid), kind, (unsigned long long)a, (unsigned long long)b, active, rss, vm, available);
    fflush(stderr);
    return id;
}
static void driver_diag_end(unsigned long long id, int result) {
    unsigned active = atomic_fetch_sub(&driver_diag_active, 1) - 1;
    fprintf(stderr, "[driver-diag] end id=%llu tid=%ld result=%d active=%u\n", id, (long)syscall(SYS_gettid), result, active);
    fflush(stderr);
}
