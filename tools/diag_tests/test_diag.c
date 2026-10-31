// Host tests of runtime/diag.c: the percentiles, the exclusive accounting (every ns of the window in one category,
// nested moves, a pop with no push), the sampler on this thread's own CPU clock, and the naming of addresses.
// cc -DDIAG_TESTING -I runtime runtime/diag.c tools/diag_tests/test_diag.c -o test_diag -lpthread -ldl && ./test_diag
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "diag.h"

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

// ---- what the runtime supplies
bool diag_thread_identity(uint64_t *id, uint32_t *handle, int *prio) { (void)id, (void)handle, (void)prio; return false; }
uint64_t diag_gpu_busy_total_ns(void) { return 0; }
bool diag_in_gameplay(void) { return true; }
bool diag_sampler_allowed(void) { return true; }
const char *diag_function_name(uint64_t pc, uint64_t *offset) { (void)pc, (void)offset; return NULL; }
const char *diag_report_path(void) { return NULL; }

static uint64_t fake_ns;
static uint64_t fake_now(void) { return fake_ns; }

static void test_percentiles(void) {
    uint64_t v[100];
    for (int i = 0; i < 100; i++) v[i] = (uint64_t)(100 - i);  // (unsorted: 100 .. 1)
    CHECK(diag_test_percentile(v, 100, 50) == 50);
    CHECK(diag_test_percentile(v, 100, 95) == 95);
    CHECK(diag_test_percentile(v, 100, 99) == 99);
    CHECK(diag_test_percentile(v, 100, 100) == 100);
    CHECK(v[0] == 100);  // (the input is left as it was)
    uint64_t one = 7;
    CHECK(diag_test_percentile(&one, 1, 99) == 7);
    CHECK(diag_test_percentile(v, 0, 50) == 0);
}

static void test_accounting(void) {
    diag_test_set_clock(fake_now);
    fake_ns = 1000;
    diag_test_begin_window(2000, 12000);
    diag_pop_();                          // (a pop from before arming: ignored)
    diag_push_(DG_HOST_WAIT, 0);          // t=1000: first move, nothing before it is attributed
    fake_ns = 3000;
    diag_push_(DG_CORE0, 0);              // host-wait 2000-3000 (clipped to the window from 2000)
    fake_ns = 4500;
    diag_pop_();                          // core0 3000-4500
    fake_ns = 5000;
    diag_pop_();                          // host-wait 4500-5000
    fake_ns = 6000;
    diag_push_(DG_SVC_BLOCK, 0x18);       // run 5000-6000
    fake_ns = 9000;
    diag_pop_();                          // svc-block 6000-9000
    fake_ns = 15000;
    diag_push_(DG_SPIN, 0x0B);            // run 9000-12000 (clipped at the window's end)
    diag_pop_();
    uint64_t c[DG_N], un;
    diag_test_thread_totals(c, &un);
    CHECK(c[DG_HOST_WAIT] == 1500);
    CHECK(c[DG_CORE0] == 1500);
    CHECK(c[DG_SVC_BLOCK] == 3000);
    CHECK(c[DG_RUN] == 4000);
    CHECK(c[DG_SPIN] == 0);
    uint64_t sum = 0;
    for (int i = 0; i < DG_N; i++) sum += c[i];
    CHECK(sum + un == 10000);  // (exclusive: the categories and the unattributed add up to the window)
    CHECK(un == 0);
    diag_test_set_clock(NULL);
}

static void test_sampler(void) {
    diag_name_thread("render", DG_RUN);  // (a thread the sampler takes)
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t now = (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
    diag_test_begin_window(now, now + 10000000000ull);
    diag_push_(DG_SVC, 1);  // (the first move of this session: no time yet)
    diag_pop_();            // (the next one, in the window: the thread starts its own timer)
    volatile uint64_t x = 0;
    struct timespec c0;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &c0);
    for (;;) {  // 300 ms of this thread's CPU from here
        for (int i = 0; i < 1000000; i++) x += (uint64_t)i * 3;
        struct timespec c;
        clock_gettime(CLOCK_THREAD_CPUTIME_ID, &c);
        if ((c.tv_sec - c0.tv_sec) * 1000000000ll + (c.tv_nsec - c0.tv_nsec) > 300000000ll) break;
        diag_push_(DG_SVC, 2);
        diag_pop_();
    }
    uint32_t n, lost, wrong;
    uint64_t pc0;
    diag_test_samples(&n, &lost, &wrong, &pc0);
    diag_test_end();
    fprintf(stderr, "sampler: %u samples, %u lost, %u wrong thread\n", n, lost, wrong);
    // (asked for every 1 ms of this thread's CPU; the kernel's CPU timers fire at its tick: HZ 100-1000)
    CHECK(n >= 25 && n <= 330);
    CHECK(lost == 0);
    CHECK(wrong == 0);
    char nm[96];
    diag_test_name(pc0, nm, sizeof nm);
    fprintf(stderr, "first sample: %s\n", nm);
    CHECK(strstr(nm, "(main)") != NULL || strstr(nm, "libc") != NULL || strstr(nm, "vdso") != NULL);  // (the executable: "(main)")
    diag_test_name((uint64_t)(uintptr_t)&test_sampler, nm, sizeof nm);
    CHECK(strstr(nm, "(main)") != NULL);  // (module + offset, or its symbol)
}

// The whole path: a request, 5 s of gameplay before the window, 2 s of frames, the report (to stderr) and "done"
static void test_report(void) {
    diag_test_set_clock(fake_now);
    fake_ns = 1000000000ull;
    diag_request(2);
    CHECK(diag_status(NULL, 0) == 1);
    for (int f = 0; f < 600; f++) {  // 10 s at 60 fps (the window is 2 s of it)
        fake_ns += 16666667;
        diag_push_(DG_SVC_BLOCK, 0x1C);
        fake_ns += 1000000;
        diag_pop_();
        diag_present(200000, 300000);
    }
    struct timespec ts = {1, 0};
    nanosleep(&ts, NULL);  // (the report's thread)
    char msg[256];
    CHECK(diag_status(msg, sizeof msg) == 3);
    diag_test_set_clock(NULL);
}

// The comparison: the feature alternates every 10 s inside the window, and goes back to "not compared" at its end
static void test_ab(void) {
    diag_test_set_clock(fake_now);
    fake_ns = 100000000000ull;
    diag_request_ab(30);
    CHECK(g_diag_ab == -1);  // (not before the window)
    int seen[2] = {0, 0};
    for (int f = 0; f < 60 * 40; f++) {  // 40 s at 60 fps: 5 s of waiting, then the 30 s window
        fake_ns += 16666667;
        if (g_diag_ab >= 0) seen[g_diag_ab]++;
        diag_present(100000, 200000);
    }
    CHECK(seen[0] > 500 && seen[1] > 500);
    CHECK(g_diag_ab == -1);
    struct timespec ts = {1, 0};
    nanosleep(&ts, NULL);
    CHECK(diag_status(NULL, 0) == 3);
    diag_test_set_clock(NULL);
}

// Graphics: a gameplay frame profiled every 15 s from 10 s into the window (as vulkan/device.c and exec.c do), its rows
// kept heaviest first, and the profiled frame and the next two left out of the frame statistics
static void test_graphics(void) {
    diag_test_set_clock(fake_now);
    fake_ns = 200000000000ull;
    diag_request(40);
    int profiled = 0;
    bool pending = false;
    for (int f = 0; f < 60 * 50; f++) {  // 50 s at 60 fps: 5 s of waiting, then the 40 s window
        fake_ns += 16666667;
        if (pending) {  // (the frame after: vk_gpuprof_report)
            pending = false;
            CHECK(diag_gpuprof_begin(40, 9.5));
            for (int r = 0; r < 39; r++) {
                char l[64];
                snprintf(l, sizeof l, r % 3 ? "pass: %d color 1600x896" : "dispatch %d", r);
                diag_gpuprof_row(l, r == 17 ? 2.5 : 0.1 + r * 0.001, r % 3 ? 100 : 0);
            }
            diag_gpuprof_end(true);
            profiled++;
        }
        if (diag_want_gpuprof()) pending = true;
        diag_present(100000, 200000);
    }
    CHECK(!diag_gpuprof_begin(1, 1));  // (not asked for: an LARECOMP_GPUPROF_AT frame isn't the diagnosis's)
    struct timespec ts = {1, 0};
    nanosleep(&ts, NULL);
    CHECK(diag_status(NULL, 0) == 3);
    int n, nrows, skipped;
    double top_ms;
    char top[112];
    diag_test_gpuprof(&n, &nrows, &top_ms, top, sizeof top, &skipped);
    fprintf(stderr, "graphics: %d frames profiled, %d rows kept, top %.2f ms %s, %d frames left out\n", n, nrows, top_ms, top, skipped);
    CHECK(profiled == 2 && n == 2);  // (10 and 25 s; 40 s is the window's end - a 60 s window gets 10, 25, 40, 55)
    CHECK(nrows == 24);
    CHECK(top_ms == 2.5 && strstr(top, "pass: 17") != NULL);
    CHECK(skipped == 2 * 3);
    diag_test_set_clock(NULL);
}

// Frame generation 2x: 30 real frames a second, two images shown for each (each a present), the compositor showing
// them 16.7 ms apart - with a lost presentID in between, whose interval isn't counted
static void test_framegen(void) {
    diag_test_set_clock(fake_now);
    fake_ns = 300000000000ull;
    diag_request(20);
    uint32_t id = 0;
    for (int f = 0; f < 30 * 30; f++) {  // 30 s at 30 real frames a second: 5 s of waiting, then the 20 s window
        fake_ns += 33333333;
        diag_real_frame();
        diag_present(100000, 200000);  // (the generated image)
        diag_present(100000, 200000);  // (the real one)
        diag_displayed(++id, fake_ns - 16666667);
        if (f % 50 == 7) id++;  // (a presentID the platform's history lost)
        diag_displayed(++id, fake_ns);
    }
    uint64_t real, real_ns, shown0;
    int shown;
    diag_test_framegen(&real, &real_ns, &shown, &shown0);
    double fps = real_ns ? real * 1e9 / real_ns : 0;
    fprintf(stderr, "framegen: %.2f real frames a second, %d screen intervals, first %.2f ms\n", fps, shown, shown0 / 1e6);
    CHECK(fps > 29.9 && fps < 30.1);
    CHECK(shown > 1000 && shown < 1200);  // (20 s at 60 shown, less the ones across a lost id)
    CHECK(shown0 > 16600000 && shown0 < 16700000);
    struct timespec ts = {1, 0};
    nanosleep(&ts, NULL);
    CHECK(diag_status(NULL, 0) == 3);
    diag_test_set_clock(NULL);
}

int main(void) {
    test_percentiles();
    test_accounting();
    test_sampler();
    test_report();
    test_ab();
    test_graphics();
    test_framegen();
    if (fails) { fprintf(stderr, "%d failed\n", fails); return 1; }
    fprintf(stderr, "all passed\n");
    return 0;
}
