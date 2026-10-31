// Performance diagnosis (docs/DESEMPENHO-DIAG.md): where the frame's time goes, measured for a window of gameplay.
// Off unless asked for (Settings > Files > Measure performance, or LARECOMP_DIAG=<seconds>): then every thread's wall
// time is split into exclusive categories, the main and presenting threads and the render thread are sampled by
// address, and the presents, the GPU's time and the clocks are recorded; the report goes to the log.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Exclusive categories of a thread's wall time (one at a time: entering one pauses the one it was in)
enum {
    DG_RUN,             // the thread's own work: the game's code, the runtime's HLE / NVN (the samples tell which)
    DG_SVC,             // inside an SVC that doesn't block
    DG_SVC_BLOCK,       // inside an SVC that blocks (per SVC number), the handler's own CPU included
    DG_SPIN,            // SleepThread(0 / -1 / -2): a yield
    DG_CORE0,           // waiting for its turn on core 0 (kernel.c core0_acquire)
    DG_HOST_WAIT,       // the runtime's long waits on a guest thread (kernel_blocking_begin / _end: the renderer)
    DG_GPU_WAIT,        // waiting for a GPU submission (vk_wait_seq)
    DG_RENDER_IDLE,     // the render thread with nothing to do
    DG_RENDER_PRESENT,  // the render thread presenting (vk_present_now, the acquire and present excluded)
    DG_ACQUIRE,         // vkAcquireNextImageKHR
    DG_PRESENT_CALL,    // vkQueuePresentKHR
    DG_N
};

extern volatile int g_diag_armed;  // (read by every hook: 0 almost always)
void diag_push_(int cat, int sub);
void diag_pop_(void);
#define DIAG_PUSH(c, s) do { if (__builtin_expect(g_diag_armed, 0)) diag_push_((c), (s)); } while (0)
#define DIAG_POP() do { if (__builtin_expect(g_diag_armed, 0)) diag_pop_(); } while (0)

// A host thread's name and base category (a guest thread: its KThread, through diag_thread_identity)
void diag_name_thread(const char *name, int base_cat);
// The game's presenting thread (nvn/objects.c), sampled with the main thread
void diag_note_presenting(uint64_t kthread_id);
// After each vkQueuePresentKHR (the render thread): the acquire's and the present call's time. Drives the window.
void diag_present(uint64_t acquire_ns, uint64_t present_ns);
// The final composition's GPU time of a frame (device.c, from timestamps), when diag_want_gpu_ts()
bool diag_want_gpu_ts(void);
void diag_gpu_composition(uint64_t ns);
// When an image was really shown (VK_GOOGLE_display_timing, with the frame generation: its presentID and
// actualPresentTime, CLOCK_MONOTONIC ns)
void diag_displayed(uint32_t id, uint64_t actual_ns);
void diag_real_frame(void);  // (vulkan/device.c) a real frame presented - the frame generation's own images aside
// graphics: a few gameplay frames profiled per GPU pass (vulkan/device.c asks, vulkan/exec.c vk_gpuprof_report feeds)
bool diag_want_gpuprof(void);
bool diag_gpuprof_begin(int marks, double total_ms);
void diag_gpuprof_row(const char *label, double ms, uint32_t draws);
void diag_gpuprof_end(bool kept);  // (kept false: the timestamps couldn't be read)

// Start a measurement of `seconds` of gameplay (it begins once the game is out of its menus for 5 s)
void diag_request(int seconds);
// The same, comparing a feature on and off in alternating 10 s blocks (today: the constant buffers read in place,
// vulkan/exec.c ubo_direct); g_diag_ab: -1 no comparison running, else the feature's state for this block
void diag_request_ab(int seconds);
void diag_request_ab_feature(int seconds, int feature);  // 1 constant buffers in place, 2 the ADPF session's game threads
extern int g_diag_ab_feature;
extern volatile int g_diag_ab;
// The measurement's state for the menu: 0 none, 1 waiting for gameplay, 2 recording, 3 done; msg: a line to show
int diag_status(char *msg, size_t n);

// ---- supplied by the runtime (kernel.c, exec.c, platform), or by the tests
bool diag_thread_identity(uint64_t *id, uint32_t *handle, int *prio);  // the calling thread's KThread, if any
uint64_t diag_gpu_busy_total_ns(void);                                 // GPU busy time so far (exec.c)
bool diag_in_gameplay(void);
bool diag_sampler_allowed(void);                                       // not with native game code (its own signal state)                                           // not in a menu, a pause or an event
const char *diag_function_name(uint64_t pc, uint64_t *offset);         // f_<guest address> for game code, else NULL
const char *diag_report_path(void);                                    // where the report file goes (NULL: log only)

#ifdef DIAG_TESTING
// (tests) the pure parts: percentiles, the per-thread accounting and the ring
double diag_test_percentile(uint64_t *v, int n, double p);
void diag_test_set_clock(uint64_t (*now)(void));
void diag_test_begin_window(uint64_t t0, uint64_t t1);
void diag_test_thread_totals(uint64_t cat_ns[DG_N], uint64_t *unattributed);
void diag_test_samples(uint32_t *n, uint32_t *lost, uint32_t *wrong, uint64_t *pc0);
void diag_test_name(uint64_t pc, char *buf, size_t n);
void diag_test_end(void);
void diag_test_framegen(uint64_t *real, uint64_t *real_ns, int *shown, uint64_t *shown0);
void diag_test_gpuprof(int *n, int *nrows, double *top_ms, char *top, size_t top_n, int *skipped);
#endif

#ifdef __cplusplus
}
#endif
