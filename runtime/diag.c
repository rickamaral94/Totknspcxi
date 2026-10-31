// Performance diagnosis (diag.h; docs/DESEMPENHO-DIAG.md).
//
// Wall time: every thread that passes a hook while the diagnosis is armed gets a record; each hook moves it from one
// exclusive category to another (a stack: an SVC inside a host wait goes back to the wait), and the time between two
// moves goes to the category it was in, clipped to the window. A thread's time before its first move is not
// attributed (shown as such), nor is anything outside the window.
// Addresses: the main thread, the presenting thread, the render thread and LARECOMP_DIAG_TID's threads each create
// their own timer on their own CPU clock (CLOCK_THREAD_CPUTIME_ID is the calling thread's) that signals that thread
// (SIGEV_THREAD_ID) every 1 ms of its CPU time - in practice at the kernel's tick, as CPU timers are checked there (4 ms
// at HZ 250, Android's usual), which the report shows as samples per second of CPU; the handler checks it runs on the thread the timer is for, and keeps
// pc and lr - two addresses, not a stack. The sampler runs 5 s on, 5 s off: the frame times of both halves show what it
// costs. Game code is named by its guest address (f_<addr>), the rest by module + offset with the module's build id.
#define _GNU_SOURCE
#include "diag.h"

#include <errno.h>
#include <link.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <time.h>
#include <ucontext.h>
#include <unistd.h>
#include <dlfcn.h>

#include "platform.h"

#define LOG(...) fprintf(stderr, __VA_ARGS__)

volatile int g_diag_armed;

static uint64_t clock_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}
static uint64_t (*now_fn)(void) = clock_now;
static uint64_t thread_cpu(void) {
    struct timespec ts;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static const char *const cat_name[DG_N] = {"run", "svc", "svc-block", "yield", "core0-wait", "host-wait", "gpu-wait",
                                           "render-idle", "render-present", "acquire", "present-call"};

// ---------------------------------------------------------------- the window
static _Atomic uint32_t g_session;
static _Atomic int g_state;                       // 0 none, 1 waiting for gameplay, 2 recording, 3 reporting, 4 done
static _Atomic uint64_t win_t0 = ~0ull, win_t1 = ~0ull;
static int g_seconds;
static char g_done_msg[160];

// ---------------------------------------------------------------- per-thread records
#define DEPTH 16
typedef struct DiagT {
    uint32_t session;
    int tid, base, prio;
    uint64_t kid;
    uint32_t handle;
    char name[16];
    int depth, overflow;
    uint8_t cat[DEPTH], sub[DEPTH];
    uint64_t last;  // the last move (0: none yet this session)
    uint64_t cat_ns[DG_N], svc_ns[128];
    uint32_t svc_n[128];
    uint64_t moves;
    uint64_t first_wall, first_cpu, last_wall, last_cpu;  // the thread's own CPU clock, read by itself
    int slot;                                             // its sampler slot, -1 none, -2 not to be sampled
    struct DiagT *next;
} DiagT;
static DiagT *g_threads;
static pthread_mutex_t g_threads_lock = PTHREAD_MUTEX_INITIALIZER;
static __thread DiagT *t_self;
static __thread char t_name[16];
static __thread int t_base = DG_RUN;
static _Atomic uint64_t g_presenting = ~0ull;
static _Atomic int g_main_tid;  // (the main thread's tid: its CPU clock read from the render thread)

void diag_name_thread(const char *name, int base_cat) {
    snprintf(t_name, sizeof t_name, "%s", name);
    t_base = base_cat;
}
void diag_note_presenting(uint64_t kid) { g_presenting = kid; }

// ---------------------------------------------------------------- the sampler
#define SLOTS 8
#define RING 65536
typedef struct { uint64_t pc, lr; uint8_t cat; } Samp;
typedef struct {
    _Atomic int tid;
    timer_t timer;
    bool live;
    Samp *ring;
    DiagT *d;
    _Atomic uint32_t n, lost, wrong;
    _Atomic uint64_t handler_ns;
} Slot;
static Slot g_slots[SLOTS];
static _Atomic int g_nslots;
static _Atomic int g_sampler_on;  // (the current 5 s block)
static uint64_t g_extra_tids[8];
static int g_nextra = -1;

static void on_prof(int sig, siginfo_t *si, void *ucv) {
    (void)sig;
    int saved = errno;
    uint64_t a = clock_now();
    int k = si->si_value.sival_int;
    if (k >= 0 && k < SLOTS) {
        Slot *s = &g_slots[k];
        if ((int)syscall(SYS_gettid) != atomic_load_explicit(&s->tid, memory_order_relaxed)) {
            atomic_fetch_add(&s->wrong, 1);
        } else {
            ucontext_t *uc = ucv;
            uint64_t pc = 0, lr = 0;
#if defined(__aarch64__)
            pc = uc->uc_mcontext.pc, lr = uc->uc_mcontext.regs[30];
#elif defined(__x86_64__)
            pc = (uint64_t)uc->uc_mcontext.gregs[REG_RIP];
#endif
            DiagT *d = s->d;
            uint8_t cat = (uint8_t)(d && d->depth > 0 && d->depth <= DEPTH ? d->cat[d->depth - 1] : d ? d->base : DG_RUN);
            uint32_t i = atomic_fetch_add(&s->n, 1);
            if (i < RING) s->ring[i] = (Samp){pc, lr, cat};
            else atomic_fetch_add(&s->lost, 1);
        }
        atomic_fetch_add(&s->handler_ns, clock_now() - a);
    }
    errno = saved;
}

static bool wants_sampling(const DiagT *d) {
    if (g_nextra < 0) {  // LARECOMP_DIAG_TID=<id>[,<id>...]: those guest threads too
        g_nextra = 0;
        const char *e = getenv("LARECOMP_DIAG_TID");
        for (const char *p = e; p && *p && g_nextra < 8;) {
            g_extra_tids[g_nextra++] = strtoull(p, (char **)&p, 10);
            if (*p == ',') p++; else break;
        }
    }
    if (!diag_sampler_allowed()) return false;
    if (!strcmp(d->name, "render") || d->handle == 0x101 || (d->kid && d->kid == g_presenting)) return true;
    for (int i = 0; i < g_nextra; i++) if (d->kid && d->kid == g_extra_tids[i]) return true;
    return false;
}

static void arm_timer(timer_t t, bool on) {
    struct itimerspec it = {{0, on ? 1000000 : 0}, {0, on ? 1000000 : 0}};
    timer_settime(t, 0, &it, NULL);
}

// (on the thread to be sampled: the clock and the target are its own)
static void sampler_start(DiagT *d) {
    int k = atomic_fetch_add(&g_nslots, 1);
    if (k >= SLOTS) { d->slot = -2; return; }
    Slot *s = &g_slots[k];
    s->ring = calloc(RING, sizeof *s->ring);
    if (!s->ring) { d->slot = -2; return; }
    s->d = d;
    atomic_store(&s->n, 0), atomic_store(&s->lost, 0), atomic_store(&s->wrong, 0), atomic_store(&s->handler_ns, 0);
    atomic_store(&s->tid, (int)syscall(SYS_gettid));
    static struct sigaction sa;
    sa.sa_sigaction = on_prof;
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGPROF, &sa, NULL);
    struct sigevent ev;
    memset(&ev, 0, sizeof ev);
    ev.sigev_notify = SIGEV_THREAD_ID;
    ev.sigev_signo = SIGPROF;
    ev.sigev_value.sival_int = k;
#ifdef sigev_notify_thread_id
    ev.sigev_notify_thread_id = (int)syscall(SYS_gettid);
#else
    ev._sigev_un._tid = (int)syscall(SYS_gettid);
#endif
    if (timer_create(CLOCK_THREAD_CPUTIME_ID, &ev, &s->timer)) { d->slot = -2; return; }
    s->live = true;
    d->slot = k;
    arm_timer(s->timer, atomic_load(&g_sampler_on));
}

// ---------------------------------------------------------------- the accounting
static DiagT *self(void) {
    DiagT *d = t_self;
    uint32_t ses = atomic_load_explicit(&g_session, memory_order_relaxed);
    if (d && d->session == ses) return d;
    if (!d) {
        d = calloc(1, sizeof *d);
        if (!d) return NULL;
        pthread_mutex_lock(&g_threads_lock);
        d->next = g_threads;
        g_threads = d;
        pthread_mutex_unlock(&g_threads_lock);
        t_self = d;
    }
    // (a new session: what this thread knew of the last one goes)
    DiagT *keep = d->next;
    memset(d, 0, sizeof *d);
    d->next = keep;
    d->session = ses;
    d->tid = (int)syscall(SYS_gettid);
    d->base = t_base;
    d->slot = -1;
    snprintf(d->name, sizeof d->name, "%s", t_name);
    int prio = 0;
    diag_thread_identity(&d->kid, &d->handle, &prio);
    d->prio = prio;
    if (d->handle == 0x101) atomic_store(&g_main_tid, d->tid);
    return d;
}

static void charge(DiagT *d, uint64_t now) {
    d->moves++;
    if (d->last) {
        uint64_t t0 = atomic_load_explicit(&win_t0, memory_order_relaxed), t1 = atomic_load_explicit(&win_t1, memory_order_relaxed);
        uint64_t a = d->last > t0 ? d->last : t0, b = now < t1 ? now : t1;
        if (b > a) {
            int top = d->depth ? d->cat[d->depth - 1] : d->base;
            d->cat_ns[top] += b - a;
            if (top == DG_SVC_BLOCK) d->svc_ns[d->sub[d->depth - 1] & 127] += b - a;
        }
        if (now >= t0 && now < t1) {  // (its own CPU clock, at most every 100 ms)
            if (!d->first_wall) d->first_wall = now, d->first_cpu = thread_cpu();
            else if (now - d->last_wall > 100000000ull) d->last_wall = now, d->last_cpu = thread_cpu();
            if (d->slot == -1) d->slot = wants_sampling(d) ? -3 : -2;
            if (d->slot == -3 && atomic_load(&g_state) == 2) sampler_start(d);
        }
    }
    d->last = now;
}

void diag_push_(int cat, int sub) {
    DiagT *d = self();
    if (!d) return;
    charge(d, now_fn());
    if (d->depth >= DEPTH) { d->overflow++; return; }
    d->cat[d->depth] = (uint8_t)cat, d->sub[d->depth] = (uint8_t)sub;
    d->depth++;
    if (cat == DG_SVC_BLOCK && d->last >= atomic_load(&win_t0) && d->last < atomic_load(&win_t1)) d->svc_n[sub & 127]++;
}

void diag_pop_(void) {
    DiagT *d = self();
    if (!d || !d->depth) return;  // (pushed before the diagnosis was armed)
    charge(d, now_fn());
    d->depth--;
}

// ---------------------------------------------------------------- frames
#define FMAX 16384
static uint64_t f_interval[FMAX], f_acquire[FMAX], f_present[FMAX], f_gpu[FMAX], f_comp[FMAX];
static uint8_t f_flags[FMAX];  // 1 gameplay, 2 sampler on, 4 the compared feature on (diag_request_ab)
static uint64_t f_rcpu[FMAX], f_mcpu[FMAX];  // the render thread's and the main thread's own CPU time per interval
static int f_n, f_ncomp, f_nmenu;
static uint64_t f_last, f_last_gpu, f_next_stats, f_next_block;
#define CL 8
static uint32_t cl_min[CL], cl_max[CL];
static uint64_t cl_sum[CL];
static int cl_n, cl_samples;
static float temp_min = 1e9f, temp_max = -1e9f, headroom_min = 1e9f;
static int thermal_max = -1;
static uint64_t g_move_cost_ns;
// A/B (diag_request_ab): the feature alternates every 10 s in the window, so both sides share the clocks and the heat
volatile int g_diag_ab = -1;
static int g_ab_want;
int g_diag_ab_feature = 1;  // (what the comparison alternates: 1 constant buffers in place, 2 the ADPF session's game threads)
static uint64_t f_next_ab;
static uint64_t ab_cl_sum[2][CL];
static int ab_cl_samples[2];
static uint64_t f_last_rcpu, f_last_mcpu;
// Another thread's CPU clock by its tid (Linux: the per-thread scheduler clock, as pthread_getcpuclockid makes it)
static uint64_t tid_cpu(int tid) {
    struct timespec ts;
    if (tid <= 0 || clock_gettime((clockid_t)((~(unsigned)tid << 3) | 6), &ts)) return 0;
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void report_start(void);

// ---------------------------------------------------------------- graphics: GPU time per pass of a few gameplay frames
// (vulkan/exec.c gp_mark / vk_gpuprof_report: a timestamp after each pass, dispatch and copy). One frame every 15 s of
// the window, from 10 s in; reading the timestamps back waits for the GPU, so that frame and the next two are left out
// of the frame statistics (counted as not gameplay).
#define GPCAP 4
#define GPROWS 24
enum { GK_PASS, GK_DISPATCH, GK_COPY, GK_PRESENT, GK_OTHER, GK_N };
static const char *const gk_name[GK_N] = {"render passes", "compute dispatches", "copies", "present / upscale / AA", "other"};
typedef struct {
    double total, kind_ms[GK_N];
    int marks, nrows;
    uint32_t kind_n[GK_N];
    struct { char label[112]; double ms; uint32_t draws; } row[GPROWS];
} GpCapture;
static GpCapture g_gp[GPCAP];
static int g_gp_n;
static _Atomic int g_gp_pending, g_gp_skip;
static uint64_t g_gp_next;
static pthread_mutex_t g_gp_lock = PTHREAD_MUTEX_INITIALIZER;

static int gp_kind(const char *l) {
    if (!strncmp(l, "pass", 4)) return GK_PASS;
    if (!strncmp(l, "dispatch", 8)) return GK_DISPATCH;
    if (!strncmp(l, "copy", 4) || !strncmp(l, "feedback", 8)) return GK_COPY;
    if (!strncmp(l, "present", 7) || !strncmp(l, "scene upscale", 13)) return GK_PRESENT;
    return GK_OTHER;
}

// (vulkan/device.c, each present) whether the next frame should be profiled for the report
bool diag_want_gpuprof(void) {
    if (atomic_load_explicit(&g_state, memory_order_relaxed) != 2 || g_gp_n >= GPCAP || atomic_load(&g_gp_pending)) return false;
    uint64_t now = now_fn();
    if (now < g_gp_next || now + 1000000000ull > atomic_load(&win_t1) || !diag_in_gameplay()) return false;  // (not in the last 1 s)
    g_gp_next = now + 15000000000ull;
    atomic_store(&g_gp_pending, 1);
    atomic_store(&g_gp_skip, 3);
    return true;
}
// (vulkan/exec.c vk_gpuprof_report) a profiled frame's rows, if it was the diagnosis's
bool diag_gpuprof_begin(int marks, double total_ms) {
    if (!atomic_load(&g_gp_pending)) return false;
    pthread_mutex_lock(&g_gp_lock);
    bool ok = g_gp_n < GPCAP;
    if (ok) {
        memset(&g_gp[g_gp_n], 0, sizeof g_gp[0]);
        g_gp[g_gp_n].marks = marks, g_gp[g_gp_n].total = total_ms;
    }
    pthread_mutex_unlock(&g_gp_lock);
    if (!ok) atomic_store(&g_gp_pending, 0);
    return ok;
}
void diag_gpuprof_row(const char *label, double ms, uint32_t draws) {
    if (!atomic_load(&g_gp_pending)) return;
    pthread_mutex_lock(&g_gp_lock);
    GpCapture *c = &g_gp[g_gp_n];
    int k = gp_kind(label);
    c->kind_ms[k] += ms, c->kind_n[k]++;
    int at = c->nrows < GPROWS ? c->nrows : GPROWS - 1;  // (kept sorted, heaviest first; the lightest falls off)
    if (c->nrows < GPROWS || ms > c->row[GPROWS - 1].ms) {
        while (at > 0 && c->row[at - 1].ms < ms) {
            if (at < GPROWS) c->row[at] = c->row[at - 1];
            at--;
        }
        snprintf(c->row[at].label, sizeof c->row[at].label, "%s", label);
        c->row[at].ms = ms, c->row[at].draws = draws;
        if (c->nrows < GPROWS) c->nrows++;
    }
    pthread_mutex_unlock(&g_gp_lock);
}
void diag_gpuprof_end(bool kept) {
    if (!atomic_load(&g_gp_pending)) return;
    pthread_mutex_lock(&g_gp_lock);
    if (kept && g_gp_n < GPCAP) g_gp_n++;
    pthread_mutex_unlock(&g_gp_lock);
    atomic_store(&g_gp_pending, 0);
}

static void out(const char *fmt, ...);
static void gp_report(void) {
    pthread_mutex_lock(&g_gp_lock);
    out("-- graphics: GPU time per pass, %d gameplay frame(s) profiled (a timestamp after each pass, dispatch and copy; "
        "the frame's whole GPU time, idle between submissions included)", g_gp_n);
    if (!g_gp_n) out("(none: no gameplay at the profiling points, or the GPU has no timestamps)");
    double kind_sum[GK_N] = {0}, total_sum = 0;
    for (int i = 0; i < g_gp_n; i++) {
        GpCapture *c = &g_gp[i];
        total_sum += c->total;
        char kinds[400];
        int o = 0;
        for (int k = 0; k < GK_N; k++) {
            kind_sum[k] += c->kind_ms[k];
            if (c->kind_n[k] && o < (int)sizeof kinds - 80)
                o += snprintf(kinds + o, sizeof kinds - (size_t)o, "%s%s %.2f ms (%u)", o ? ", " : "", gk_name[k], c->kind_ms[k],
                              c->kind_n[k]);
        }
        out("frame %d: %.2f ms on the GPU, %d marks: %s", i + 1, c->total, c->marks, kinds);
        for (int r = 0; r < c->nrows && r < 12; r++) {
            if (c->row[r].draws) out("  %7.3f ms %5.1f%%  %s, %u draws", c->row[r].ms, 100 * c->row[r].ms / (c->total > 0 ? c->total : 1),
                                     c->row[r].label, c->row[r].draws);
            else out("  %7.3f ms %5.1f%%  %s", c->row[r].ms, 100 * c->row[r].ms / (c->total > 0 ? c->total : 1), c->row[r].label);
        }
    }
    if (g_gp_n > 1) {
        char kinds[400];
        int o = 0;
        for (int k = 0; k < GK_N; k++)
            if (kind_sum[k] > 0 && o < (int)sizeof kinds - 64)
                o += snprintf(kinds + o, sizeof kinds - (size_t)o, "%s%s %.2f", o ? ", " : "", gk_name[k], kind_sum[k] / g_gp_n);
        out("mean of the %d frames: %.2f ms - %s ms", g_gp_n, total_sum / g_gp_n, kinds);
    }
    pthread_mutex_unlock(&g_gp_lock);
}

void diag_request_ab_feature(int seconds, int feature) {
    if (atomic_load(&g_state) == 1 || atomic_load(&g_state) == 2 || atomic_load(&g_state) == 3) return;
    g_diag_ab_feature = feature == 2 ? 2 : 1;
    diag_request_ab(seconds);
}

void diag_request_ab(int seconds) {
    if (atomic_load(&g_state) == 1 || atomic_load(&g_state) == 2 || atomic_load(&g_state) == 3) return;
    g_ab_want = 1;
    diag_request(seconds);
}

void diag_request(int seconds) {
    if (atomic_load(&g_state) == 1 || atomic_load(&g_state) == 2 || atomic_load(&g_state) == 3) return;
    memset(ab_cl_sum, 0, sizeof ab_cl_sum), memset(ab_cl_samples, 0, sizeof ab_cl_samples);
    g_seconds = seconds > 0 ? seconds : 60;
    atomic_store(&win_t0, ~0ull);
    atomic_store(&win_t1, ~0ull);
    g_gp_n = 0, atomic_store(&g_gp_pending, 0), atomic_store(&g_gp_skip, 0);
    f_n = f_ncomp = f_nmenu = 0;
    f_last = f_last_gpu = 0;
    cl_n = cl_samples = 0;
    memset(cl_min, 0, sizeof cl_min), memset(cl_max, 0, sizeof cl_max), memset(cl_sum, 0, sizeof cl_sum);
    temp_min = 1e9f, temp_max = -1e9f, headroom_min = 1e9f, thermal_max = -1;
    atomic_fetch_add(&g_session, 1);
    atomic_store(&g_state, 1);
    g_diag_armed = 1;
    LOG("[diag] measurement of %d s requested: it starts after 5 s of gameplay\n", g_seconds);
}

int diag_status(char *msg, size_t n) {
    int s = atomic_load(&g_state);
    if (msg && n) {
        msg[0] = 0;
        if (s == 2) {
            uint64_t now = now_fn(), t1 = atomic_load(&win_t1);
            snprintf(msg, n, "%d", t1 > now ? (int)((t1 - now) / 1000000000ull) + 1 : 0);
        } else if (s == 4)
            snprintf(msg, n, "%s", g_done_msg);
    }
    return s == 3 ? 2 : s == 4 ? 3 : s;
}

bool diag_want_gpu_ts(void) { return atomic_load_explicit(&g_state, memory_order_relaxed) == 2; }
void diag_gpu_composition(uint64_t ns) {
    if (atomic_load(&g_state) == 2 && f_ncomp < FMAX) f_comp[f_ncomp++] = ns;
}

void diag_present(uint64_t acquire_ns, uint64_t present_ns) {
    if (!g_diag_armed) return;
    if (g_seconds == 0) {  // LARECOMP_DIAG=<seconds>: from the start
        const char *e = getenv("LARECOMP_DIAG");
        if (e && atoi(e) > 0 && atomic_load(&g_state) == 0) diag_request(atoi(e));
    }
    uint64_t now = now_fn();
    int st = atomic_load(&g_state);
    if (st == 1) {
        static uint64_t since;
        if (!diag_in_gameplay()) { since = 0; return; }
        if (!since) since = now;
        if (now - since < 5000000000ull) return;
        since = 0;
        {   // (what a move costs: two clock reads and the bookkeeping, for the overhead line)
            uint64_t a = clock_now();
            for (int i = 0; i < 1000; i++) (void)now_fn();
            g_move_cost_ns = (clock_now() - a) / 1000 + 20;
        }
        atomic_store(&g_sampler_on, 1);
        f_next_block = now + 5000000000ull;
        f_next_stats = now;
        f_last = 0;
        f_last_gpu = diag_gpu_busy_total_ns();
        atomic_store(&win_t1, now + (uint64_t)g_seconds * 1000000000ull);
        atomic_store(&win_t0, now);
        g_gp_next = now + 10000000000ull;
        if (g_ab_want) g_diag_ab = 1, f_next_ab = now + 10000000000ull;
        f_last_rcpu = thread_cpu(), f_last_mcpu = tid_cpu(atomic_load(&g_main_tid));
        atomic_store(&g_state, 2);
        LOG("[diag] recording %d s from here%s\n", g_seconds, g_ab_want ? " (the compared feature alternating every 10 s)" : "");
        return;
    }
    if (st != 2) return;
    uint64_t gpu = diag_gpu_busy_total_ns();
    uint64_t rcpu = thread_cpu(), mcpu = tid_cpu(atomic_load(&g_main_tid));
    if (f_last && f_n < FMAX) {
        f_rcpu[f_n] = rcpu - f_last_rcpu;
        f_mcpu[f_n] = mcpu >= f_last_mcpu ? mcpu - f_last_mcpu : 0;
        bool game = diag_in_gameplay();
        if (atomic_load(&g_gp_skip) > 0) atomic_fetch_sub(&g_gp_skip, 1), game = false;  // (a GPU profile's wait)
        f_interval[f_n] = now - f_last;
        f_acquire[f_n] = acquire_ns;
        f_present[f_n] = present_ns;
        f_gpu[f_n] = gpu - f_last_gpu;
        f_flags[f_n] = (uint8_t)((game ? 1 : 0) | (atomic_load(&g_sampler_on) ? 2 : 0) | (g_diag_ab == 1 ? 4 : 0));
        f_nmenu += !game;
        f_n++;
    }
    f_last = now, f_last_gpu = gpu, f_last_rcpu = rcpu, f_last_mcpu = mcpu;
    if (g_diag_ab >= 0 && now >= f_next_ab) {
        f_next_ab = now + 10000000000ull;
        g_diag_ab = !g_diag_ab;
    }
#ifdef __ANDROID__
    if (now >= f_next_stats) {  // the clocks and the heat, every 2 s
        f_next_stats = now + 2000000000ull;
        PlatDeviceStats ds;
        memset(&ds, 0, sizeof ds);
        plat_device_stats(&ds);
        cl_n = ds.ncluster < CL ? ds.ncluster : CL;
        for (int i = 0; i < cl_n; i++) {
            uint32_t m = ds.cluster_mhz[i];
            if (!cl_samples || m < cl_min[i]) cl_min[i] = m;
            if (m > cl_max[i]) cl_max[i] = m;
            cl_sum[i] += m;
            if (g_diag_ab >= 0) ab_cl_sum[g_diag_ab][i] += m;
        }
        if (g_diag_ab >= 0) ab_cl_samples[g_diag_ab]++;
        cl_samples++;
        if (ds.soc_temp_c > 0) {
            if (ds.soc_temp_c < temp_min) temp_min = ds.soc_temp_c;
            if (ds.soc_temp_c > temp_max) temp_max = ds.soc_temp_c;
        }
        if (ds.thermal > thermal_max) thermal_max = ds.thermal;
        if (ds.headroom >= 0 && ds.headroom < headroom_min) headroom_min = ds.headroom;
    }
#endif
    if (now >= f_next_block) {  // the sampler: 5 s on, 5 s off
        f_next_block = now + 5000000000ull;
        int on = !atomic_load(&g_sampler_on);
        atomic_store(&g_sampler_on, on);
        int n = atomic_load(&g_nslots);
        for (int i = 0; i < n && i < SLOTS; i++) if (g_slots[i].live) arm_timer(g_slots[i].timer, on);
    }
    if (now >= atomic_load(&win_t1)) {
        int n = atomic_load(&g_nslots);
        for (int i = 0; i < n && i < SLOTS; i++) if (g_slots[i].live) arm_timer(g_slots[i].timer, false);
        g_diag_ab = -1;  // (the feature back to its normal state)
        atomic_store(&g_state, 3);
        report_start();
    }
}

// ---------------------------------------------------------------- the report
static int cmp_u64(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return x < y ? -1 : x > y;
}
// (nearest rank, on a sorted copy)
static double percentile(const uint64_t *v, int n, double p) {
    if (n <= 0) return 0;
    uint64_t *c = malloc(sizeof *c * (size_t)n);
    if (!c) return 0;
    memcpy(c, v, sizeof *c * (size_t)n);
    qsort(c, (size_t)n, sizeof *c, cmp_u64);
    int k = (int)(p / 100.0 * n + 0.999999) - 1;
    if (k < 0) k = 0;
    if (k >= n) k = n - 1;
    double r = (double)c[k];
    free(c);
    return r;
}

static FILE *g_out;
static void out(const char *fmt, ...) {
    char line[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    LOG("[diag] %s\n", line);
    if (g_out) fprintf(g_out, "%s\n", line);
}

// a set of values, those whose flags match (mask / want; flags NULL: all), in ms: mean, p50, p95, p99, max
static void stat_line(const char *what, const uint64_t *v, int n, const uint8_t *flags, uint8_t mask, uint8_t want) {
    uint64_t *c = malloc(sizeof *c * (size_t)(n ? n : 1));
    if (!c) return;
    int k = 0;
    double sum = 0;
    for (int i = 0; i < n; i++)
        if (!flags || (flags[i] & mask) == want) c[k++] = v[i], sum += (double)v[i];
    if (!k) { out("%s: no frames", what); free(c); return; }
    out("%s: %d frames, mean %.2f, p50 %.2f, p95 %.2f, p99 %.2f, max %.2f ms", what, k, sum / k / 1e6, percentile(c, k, 50) / 1e6,
        percentile(c, k, 95) / 1e6, percentile(c, k, 99) / 1e6, percentile(c, k, 100) / 1e6);
    free(c);
}

typedef struct { char name[24]; uint64_t base, end; char build_id[48]; } Mod;
static Mod g_mods[1024];  // (an app process maps several hundred libraries: libmain.so came after the first 64)
static int g_nmods;
static int on_phdr(struct dl_phdr_info *info, size_t size, void *arg) {
    (void)size, (void)arg;
    if (g_nmods >= 1024) return 1;
    Mod *m = &g_mods[g_nmods];
    const char *nm = info->dlpi_name && *info->dlpi_name ? strrchr(info->dlpi_name, '/') : NULL;
    snprintf(m->name, sizeof m->name, "%s", nm ? nm + 1 : info->dlpi_name && *info->dlpi_name ? info->dlpi_name : "(main)");
    uint64_t lo = ~0ull, hi = 0;
    m->build_id[0] = 0;
    for (int i = 0; i < info->dlpi_phnum; i++) {
        const ElfW(Phdr) *ph = &info->dlpi_phdr[i];
        if (ph->p_type == PT_LOAD) {
            uint64_t a = info->dlpi_addr + ph->p_vaddr;
            if (a < lo) lo = a;
            if (a + ph->p_memsz > hi) hi = a + ph->p_memsz;
        } else if (ph->p_type == PT_NOTE) {
            const uint8_t *p = (const uint8_t *)(uintptr_t)(info->dlpi_addr + ph->p_vaddr), *e = p + ph->p_memsz;
            while (p + 12 <= e) {
                uint32_t nsz = *(const uint32_t *)p, dsz = *(const uint32_t *)(p + 4), type = *(const uint32_t *)(p + 8);
                const uint8_t *d = p + 12 + ((nsz + 3) & ~3u);
                if (d + dsz > e) break;
                if (type == 3 /* NT_GNU_BUILD_ID */ && nsz == 4 && !memcmp(p + 12, "GNU", 4)) {
                    for (uint32_t k = 0; k < dsz && k < 20; k++) sprintf(m->build_id + 2 * k, "%02x", d[k]);
                    break;
                }
                p = d + ((dsz + 3) & ~3u);
            }
        }
    }
    if (hi <= lo) return 0;
    m->base = info->dlpi_addr ? info->dlpi_addr : lo;
    m->end = hi;
    g_nmods++;
    return 0;
}
static const Mod *mod_of(uint64_t pc) {
    for (int i = 0; i < g_nmods; i++) if (pc >= g_mods[i].base && pc < g_mods[i].end) return &g_mods[i];
    return NULL;
}

// An address's name: f_<guest address> for game code, else module + offset (the symbol, when the module exports one)
static void name_of(uint64_t pc, char *buf, size_t n, bool with_offset) {
    uint64_t off = 0;
    const char *f = pc ? diag_function_name(pc, &off) : NULL;
    if (f) { snprintf(buf, n, "%s", f); return; }
    const Mod *m = mod_of(pc);
    Dl_info di;
    if (m && dladdr((void *)(uintptr_t)pc, &di) && di.dli_sname)
        snprintf(buf, n, "%s:%s", m->name, di.dli_sname);
    else if (m && with_offset)
        snprintf(buf, n, "%s+%#llx", m->name, (unsigned long long)(pc - m->base));
    else if (m)
        snprintf(buf, n, "%s+%#llx", m->name, (unsigned long long)((pc - m->base) & ~0x3Full));  // (64-byte buckets)
    else
        snprintf(buf, n, "?%#llx", (unsigned long long)pc);
}

typedef struct { char name[80]; uint32_t n, by_cat[DG_N]; } Ent;
static int cmp_ent(const void *a, const void *b) {
    const Ent *x = a, *y = b;
    return x->n < y->n ? 1 : x->n > y->n ? -1 : 0;
}
static int cmp_addr(const void *a, const void *b) {
    const uint64_t *x = a, *y = b;  // (address << 8 | category)
    return *x < *y ? -1 : *x > *y;
}
static void top_list(const char *what, const Slot *s, uint32_t n, bool lr) {
    int cap = 8192, ne = 0;
    Ent *e = calloc((size_t)cap, sizeof *e);
    uint64_t *v = malloc(sizeof *v * n);
    if (!e || !v) { free(e); free(v); return; }
    for (uint32_t i = 0; i < n; i++) v[i] = ((lr ? s->ring[i].lr : s->ring[i].pc) << 8) | (s->ring[i].cat < DG_N ? s->ring[i].cat : 0);
    qsort(v, n, sizeof *v, cmp_addr);  // (each address named once)
    static uint32_t mod_n[1024];
    uint32_t other = 0;
    memset(mod_n, 0, sizeof mod_n);
    char nm[80];
    int k = -1;
    for (uint32_t i = 0; i < n; i++) {
        uint64_t a = v[i] >> 8;
        if (!lr) {
            const Mod *m = mod_of(a);
            if (m) mod_n[m - g_mods]++; else other++;
        }
        if (!i || a != v[i - 1] >> 8) {
            name_of(a, nm, sizeof nm, false);
            for (k = 0; k < ne && strcmp(e[k].name, nm); k++) {}
            if (k == ne) {
                if (ne == cap) { k = -1; continue; }
                snprintf(e[ne].name, sizeof e[ne].name, "%s", nm);
                ne++;
            }
        }
        if (k < 0) continue;
        e[k].n++;
        e[k].by_cat[v[i] & 0xFF]++;
    }
    free(v);
    qsort(e, (size_t)ne, sizeof *e, cmp_ent);
    if (!lr) {
        char line[512];
        int o = snprintf(line, sizeof line, "  by module:");
        for (int i = 0; i < g_nmods && o < (int)sizeof line - 40; i++)
            if (mod_n[i] * 200 >= n)
                o += snprintf(line + o, sizeof line - o, " %s %.1f%%", g_mods[i].name, 100.0 * mod_n[i] / n);
        if (other) snprintf(line + o, sizeof line - o, " other %.1f%%", 100.0 * other / n);
        out("%s", line);
    }
    out("  %s (%s):", what, lr ? "the caller's address in lr at the sample - one address, not a stack" : "where the thread was");
    for (int i = 0; i < ne && i < (lr ? 30 : 100); i++) {
        int bc = 0;
        for (int c = 1; c < DG_N; c++) if (e[i].by_cat[c] > e[i].by_cat[bc]) bc = c;
        out("    %5.2f%%  %-60s [%s]", 100.0 * e[i].n / n, e[i].name, cat_name[bc]);
    }
    free(e);
}

static const char *svc_name(int s) {
    switch (s) {
    case 0x0A: return "ExitThread";
    case 0x0B: return "SleepThread";
    case 0x18: return "WaitSynchronization";
    case 0x1A: return "ArbitrateLock";
    case 0x1C: return "WaitProcessWideKeyAtomic";
    case 0x21: return "SendSyncRequest";
    case 0x22: return "SendSyncRequestWithUserBuffer";
    case 0x34: return "WaitForAddress";
    case 0x43: return "ReplyAndReceive";
    default: return "?";
    }
}

static void thread_lines(DiagT *d, uint64_t t0, uint64_t t1, int frames) {
    uint64_t wall = t1 - t0, sum = 0;
    for (int c = 0; c < DG_N; c++) sum += d->cat_ns[c];
    char who[64];
    if (d->kid) snprintf(who, sizeof who, "t%llu (handle %#x, prio %d%s%s)", (unsigned long long)d->kid, d->handle, d->prio,
                         d->handle == 0x101 ? ", the main thread" : "", d->kid == g_presenting ? ", presenting" : "");
    else snprintf(who, sizeof who, "%s (host thread, tid %d)", d->name[0] ? d->name : "?", d->tid);
    char line[900];
    int o = snprintf(line, sizeof line, "thread %s:", who);
    for (int c = 0; c < DG_N && o < (int)sizeof line - 48; c++)
        if (d->cat_ns[c] * 1000 >= wall)
            o += snprintf(line + o, sizeof line - o, " %s %.1f%% (%.2f ms/frame)", cat_name[c], 100.0 * d->cat_ns[c] / wall,
                          frames ? d->cat_ns[c] / 1e6 / frames : 0);
    uint64_t un = wall > sum ? wall - sum : 0;
    snprintf(line + o, sizeof line - o, " | not attributed %.1f%%", 100.0 * un / wall);
    out("%s", line);
    if (d->last_wall > d->first_wall && d->last_cpu >= d->first_cpu)
        out("  its own CPU clock: %.1f%% of %.1f s busy (%.2f ms/frame); wall minus CPU minus the waits above = time it was "
            "runnable but not running (preemption, migration)",
            100.0 * (d->last_cpu - d->first_cpu) / (d->last_wall - d->first_wall), (d->last_wall - d->first_wall) / 1e9,
            frames ? (d->last_cpu - d->first_cpu) / 1e6 / frames * ((double)wall / (d->last_wall - d->first_wall)) : 0);
    if (d->cat_ns[DG_SVC_BLOCK]) {
        o = snprintf(line, sizeof line, "  blocking SVCs:");
        for (int pass = 0; pass < 6; pass++) {  // (the six longest)
            int best = -1;
            for (int s = 0; s < 128; s++)
                if (d->svc_ns[s] && (best < 0 || d->svc_ns[s] > d->svc_ns[best])) best = s;
            if (best < 0) break;
            o += snprintf(line + o, sizeof line - o, " %02x %s %.1f ms/frame (%u calls);", best, svc_name(best),
                          frames ? d->svc_ns[best] / 1e6 / frames : 0, d->svc_n[best]);
            d->svc_ns[best] = 0;
        }
        out("%s", line);
    }
    if (d->overflow) out("  (%d moves past the category stack's depth were not counted)", d->overflow);
}

static int cmp_busy(const void *a, const void *b) {
    const DiagT *x = *(DiagT *const *)a, *y = *(DiagT *const *)b;
    uint64_t bx = x->cat_ns[DG_RUN] + x->cat_ns[DG_SVC], by = y->cat_ns[DG_RUN] + y->cat_ns[DG_SVC];
    return bx < by ? 1 : bx > by ? -1 : 0;
}

static void *report_main(void *arg) {
    (void)arg;
    struct timespec ts = {0, 300000000};
    nanosleep(&ts, NULL);  // (the threads' last moves)
    uint64_t t0 = atomic_load(&win_t0), t1 = atomic_load(&win_t1);
    uint32_t ses = atomic_load(&g_session);
    const char *path = diag_report_path();
    g_out = path ? fopen(path, "w") : NULL;
    g_nmods = 0;
    dl_iterate_phdr(on_phdr, NULL);
    int frames = f_n, game = f_n - f_nmenu;
    out("===== performance diagnosis: %.0f s, %d presents (%d in gameplay, %d in menus or events)", (t1 - t0) / 1e9, frames, game,
        f_nmenu);
    {   // what it ran with, and one line to compare runs by (drivers, settings: docs/DESEMPENHO-DIAG.md)
        char ctx[768] = "n/a";
#if defined(__ANDROID__) && !defined(DIAG_TESTING)
        extern void vk_diag_context(char *buf, size_t n);
        vk_diag_context(ctx, sizeof ctx);
#endif
        out("run: %s", ctx);
        // (TOTK has no frame generation: every present is a real frame - gameplay presents under 250 ms)
        uint64_t rf_n = 0, rf_ns = 0;
        double gpu = 0;
        uint64_t *c = malloc(sizeof *c * (size_t)(f_n ? f_n : 1));
        for (int i = 0; i < f_n; i++)
            if ((f_flags[i] & 1) && f_interval[i] < 250000000ull) {
                if (c) c[rf_n] = f_interval[i];
                rf_n++, rf_ns += f_interval[i], gpu += f_gpu[i] / 1e6;
            }
        double p50 = 0, p95 = 0, p99 = 0;
        if (c && rf_n) {
            qsort(c, (size_t)rf_n, sizeof *c, cmp_u64);
            p50 = c[rf_n / 2] / 1e6, p95 = c[(size_t)(rf_n * 0.95)] / 1e6, p99 = c[(size_t)(rf_n * 0.99)] / 1e6;
        }
        free(c);
        char cl[160] = "n/a";
        if (cl_samples && cl_n) {
            int o = 0;
            for (int i = 0; i < cl_n && o < (int)sizeof cl - 16; i++)
                o += snprintf(cl + o, sizeof cl - (size_t)o, "%s%llu", i ? "/" : "", (unsigned long long)(cl_sum[i] / (uint64_t)cl_samples));
        }
        out("summary: %.2f frames a second | present p50 %.2f p95 %.2f p99 %.2f ms | GPU %.2f ms a frame | "
            "CPU clusters %s MHz | SoC %.0f-%.0f C",
            rf_ns ? rf_n * 1e9 / rf_ns : 0.0, p50, p95, p99, rf_n ? gpu / (double)rf_n : 0.0, cl,
            temp_max > -1e8f ? temp_min : 0.0f, temp_max > -1e8f ? temp_max : 0.0f);
    }
    out("-- frames (the time between two vkQueuePresentKHR on the render thread)");
    stat_line("present interval, gameplay", f_interval, f_n, f_flags, 1, 1);
    stat_line("  of which with the sampler on", f_interval, f_n, f_flags, 3, 3);
    stat_line("  of which with the sampler off", f_interval, f_n, f_flags, 3, 1);
    stat_line("acquire (vkAcquireNextImageKHR), gameplay", f_acquire, f_n, f_flags, 1, 1);
    stat_line("present call (vkQueuePresentKHR), gameplay", f_present, f_n, f_flags, 1, 1);
    if (g_ab_want) {
        out("-- A/B: %s (on / off, alternating every 10 s; gameplay frames)",
            g_diag_ab_feature == 2 ? "the game's busiest threads in the ADPF session" : "constant buffers read in place");
        for (int a = 1; a >= 0; a--) {  // (frames a second on each side: the gameplay presents under 250 ms)
            uint64_t k = 0, ns = 0;
            for (int i = 0; i < f_n; i++)
                if ((f_flags[i] & 5) == (a ? 5 : 1) && f_interval[i] < 250000000ull) k++, ns += f_interval[i];
            if (ns) out("frames a second with it %s: %.2f (%llu in %.1f s)", a ? "ON " : "OFF", k * 1e9 / ns, (unsigned long long)k, ns / 1e9);
        }
        stat_line("present interval, ON ", f_interval, f_n, f_flags, 5, 5);
        stat_line("present interval, OFF", f_interval, f_n, f_flags, 5, 1);
        stat_line("render thread CPU per interval, ON ", f_rcpu, f_n, f_flags, 5, 5);
        stat_line("render thread CPU per interval, OFF", f_rcpu, f_n, f_flags, 5, 1);
        stat_line("main thread CPU per interval, ON ", f_mcpu, f_n, f_flags, 5, 5);
        stat_line("main thread CPU per interval, OFF", f_mcpu, f_n, f_flags, 5, 1);
        stat_line("GPU busy per interval (use the mean), ON ", f_gpu, f_n, f_flags, 5, 5);
        stat_line("GPU busy per interval (use the mean), OFF", f_gpu, f_n, f_flags, 5, 1);
        for (int s = 1; s >= 0; s--) {
            if (!ab_cl_samples[s] || !cl_n) continue;
            char line[256];
            int o = snprintf(line, sizeof line, "CPU clusters mean MHz with it %s:", s ? "ON " : "OFF");
            for (int i = 0; i < cl_n; i++)
                o += snprintf(line + o, sizeof line - o, " [%d] %llu", i, (unsigned long long)(ab_cl_sum[s][i] / (uint64_t)ab_cl_samples[s]));
            out("%s", line);
        }
    }
    out("-- GPU (timestamps at each submission's start and end, idle excluded)");
    // (the busy time arrives in batches, as submissions' timestamps are read back: the mean is the GPU's time per frame,
    // the percentiles are those of the batches)
    stat_line("GPU busy per present interval, gameplay (use the mean)", f_gpu, f_n, f_flags, 1, 1);
    stat_line("final composition (the game image into the window + the menus / overlay)", f_comp, f_ncomp, NULL, 0, 0);
    out("-- device (every 2 s)");
    if (cl_samples && cl_n) {
        char line[512];
        int o = snprintf(line, sizeof line, "CPU clusters MHz (min / mean / max):");
        for (int i = 0; i < cl_n; i++)
            o += snprintf(line + o, sizeof line - o, " [%d] %u / %llu / %u", i, cl_min[i],
                          (unsigned long long)(cl_sum[i] / (uint64_t)cl_samples), cl_max[i]);
        out("%s", line);
    } else out("CPU clusters: n/a");
    if (temp_max > -1e8f) out("SoC temperature %.0f-%.0f C", temp_min, temp_max); else out("SoC temperature: n/a");
    if (thermal_max >= 0) out("Android thermal status (max): %d", thermal_max); else out("Android thermal status: n/a");
    if (headroom_min < 1e8f) out("thermal headroom (min): %.2f", headroom_min); else out("thermal headroom: n/a");
    out("-- threads (wall time in exclusive categories; the busiest first)");
    DiagT *list[256];
    int nl = 0;
    pthread_mutex_lock(&g_threads_lock);
    for (DiagT *d = g_threads; d && nl < 256; d = d->next) {
        if (d->session != ses || !d->last) continue;
        if (d->last < t1) {  // (its open interval up to the window's end)
            uint64_t a = d->last > t0 ? d->last : t0;
            int top = d->depth ? d->cat[d->depth - 1] : d->base;
            if (t1 > a) {
                d->cat_ns[top] += t1 - a;
                if (top == DG_SVC_BLOCK) d->svc_ns[d->sub[d->depth - 1] & 127] += t1 - a;
            }
        }
        list[nl++] = d;
    }
    pthread_mutex_unlock(&g_threads_lock);
    qsort(list, (size_t)nl, sizeof *list, cmp_busy);
    uint64_t pres = g_presenting;
    DiagT *main_t = NULL, *pres_t = NULL;
    for (int i = 0; i < nl; i++) {
        if (list[i]->handle == 0x101) main_t = list[i];
        if (list[i]->kid && list[i]->kid == pres) pres_t = list[i];
    }
    if (main_t && pres_t)
        out("the presenting thread is %s the main thread (t%llu, t%llu)", main_t == pres_t ? "" : "NOT", (unsigned long long)pres_t->kid,
            (unsigned long long)main_t->kid);
    uint64_t moves = 0;
    for (int i = 0; i < nl; i++) {
        moves += list[i]->moves;
        bool key = list[i] == main_t || list[i] == pres_t || !strcmp(list[i]->name, "render");
        if (i < 12 || key) thread_lines(list[i], t0, t1, frames);
    }
    out("(%d threads seen; %llu category moves, ~%.0f ms of the hooks' own time across all threads)", nl, (unsigned long long)moves,
        moves * g_move_cost_ns / 1e6);
    out("-- samples (asked for every 1 ms of the thread's own CPU time, delivered at the kernel's tick; in the 5 s blocks "
        "with the sampler on)");
    int ns = atomic_load(&g_nslots);
    for (int i = 0; i < ns && i < SLOTS; i++) {
        Slot *s = &g_slots[i];
        if (!s->live || !s->d) continue;
        uint32_t n = atomic_load(&s->n);
        if (n > RING) n = RING;
        DiagT *d = s->d;
        char who[48];
        if (d->kid) snprintf(who, sizeof who, "t%llu", (unsigned long long)d->kid);
        else snprintf(who, sizeof who, "%s", d->name);
        uint64_t hns = atomic_load(&s->handler_ns);
        uint32_t taken = atomic_load(&s->n);
        out("%s: %u samples kept, %u lost (ring full), %u on the wrong thread (dropped); the handler took %.1f us each, %.1f ms "
            "in all (the frame times with the sampler on and off above show its whole cost)", who, n, atomic_load(&s->lost),
            atomic_load(&s->wrong), taken ? hns / 1e3 / taken : 0, hns / 1e6);
        if (n) {
            top_list("functions", s, n, false);
            top_list("callers", s, n, true);
        }
    }
    gp_report();
    out("-- modules (for offline symbols: tools/diag_symbolize.py)");
    for (int i = 0; i < g_nmods; i++)
        if (strstr(g_mods[i].name, "libmain") || strstr(g_mods[i].name, "totkgame") || strstr(g_mods[i].name, "vulkan"))
            out("%s base %#llx build-id %s", g_mods[i].name, (unsigned long long)g_mods[i].base,
                g_mods[i].build_id[0] ? g_mods[i].build_id : "-");
    out("===== end of the diagnosis");
    if (g_out) fclose(g_out);
    g_out = NULL;
    if (path) {  // (a copy by date and time, so several runs - one per driver or setting - can be sent together)
        char copy[1200];
        time_t now = time(NULL);
        struct tm tm;
        localtime_r(&now, &tm);
        const char *dot = strrchr(path, '.');
        int stem = dot ? (int)(dot - path) : (int)strlen(path);
        snprintf(copy, sizeof copy, "%.*s-%04d%02d%02d-%02d%02d%02d.txt", stem, path, tm.tm_year + 1900, tm.tm_mon + 1,
                 tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
        FILE *in = fopen(path, "r"), *o = in ? fopen(copy, "w") : NULL;
        if (o) {
            char buf[8192];
            size_t k;
            while ((k = fread(buf, 1, sizeof buf, in)) > 0) fwrite(buf, 1, k, o);
            fclose(o);
            LOG("[diag] report copied to %s\n", copy);
        }
        if (in) fclose(in);
    }
    // (the sampler stays installed, its timers stopped; the hooks stop counting)
    for (int i = 0; i < ns && i < SLOTS; i++)
        if (g_slots[i].live) { timer_delete(g_slots[i].timer); g_slots[i].live = false; free(g_slots[i].ring); g_slots[i].ring = NULL; }
    atomic_store(&g_nslots, 0);
    snprintf(g_done_msg, sizeof g_done_msg, "%s", path ? path : "log");
    g_diag_armed = 0;
    g_ab_want = 0;
    atomic_store(&g_state, 4);
    return NULL;
}

static void report_start(void) {
    pthread_t th;
    if (pthread_create(&th, NULL, report_main, NULL) == 0) pthread_detach(th);
    else report_main(NULL);
}

#ifdef DIAG_TESTING
double diag_test_percentile(uint64_t *v, int n, double p) { return percentile(v, n, p); }
void diag_test_set_clock(uint64_t (*fn)(void)) { now_fn = fn ? fn : clock_now; }
void diag_test_begin_window(uint64_t t0, uint64_t t1) {
    atomic_fetch_add(&g_session, 1);
    g_diag_armed = 1;
    atomic_store(&win_t0, t0);
    atomic_store(&win_t1, t1);
    atomic_store(&g_sampler_on, 1);
    atomic_store(&g_state, 2);
}
void diag_test_samples(uint32_t *n, uint32_t *lost, uint32_t *wrong, uint64_t *pc0) {
    *n = *lost = *wrong = 0, *pc0 = 0;
    for (int i = 0; i < atomic_load(&g_nslots) && i < SLOTS; i++) {
        uint32_t k = atomic_load(&g_slots[i].n);
        *n += k, *lost += atomic_load(&g_slots[i].lost), *wrong += atomic_load(&g_slots[i].wrong);
        if (k && !*pc0) *pc0 = g_slots[i].ring[0].pc;
    }
}
void diag_test_name(uint64_t pc, char *buf, size_t n) {
    g_nmods = 0;
    dl_iterate_phdr(on_phdr, NULL);
    name_of(pc, buf, n, true);
}
void diag_test_gpuprof(int *n, int *nrows, double *top_ms, char *top, size_t top_n, int *skipped) {
    *n = g_gp_n, *nrows = g_gp_n ? g_gp[0].nrows : 0, *top_ms = g_gp_n ? g_gp[0].row[0].ms : 0, *skipped = f_nmenu;
    snprintf(top, top_n, "%s", g_gp_n ? g_gp[0].row[0].label : "");
}
void diag_test_end(void) {
    for (int i = 0; i < atomic_load(&g_nslots) && i < SLOTS; i++)
        if (g_slots[i].live) { timer_delete(g_slots[i].timer); g_slots[i].live = false; free(g_slots[i].ring); g_slots[i].ring = NULL; }
    atomic_store(&g_nslots, 0);
    atomic_store(&g_state, 0);
}
void diag_test_thread_totals(uint64_t cat_ns[DG_N], uint64_t *unattributed) {
    DiagT *d = t_self;
    uint64_t sum = 0;
    for (int c = 0; c < DG_N; c++) cat_ns[c] = d ? d->cat_ns[c] : 0, sum += cat_ns[c];
    uint64_t wall = atomic_load(&win_t1) - atomic_load(&win_t0);
    *unattributed = wall > sum ? wall - sum : 0;
}
#endif
