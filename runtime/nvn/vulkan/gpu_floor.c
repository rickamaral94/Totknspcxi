// GPU clock floor through the kernel driver (KGSL) for Turnip on Adreno, from peanuts-native (dm/gfx/vk_android/vka_dev.c,
// MIT). Turnip has no VK_QCOM_queue_perf_hint, and the ADPF session (platform/android.c) does not raise the GPU clock: at
// 30 fps the GPU idles, its clock drops and a frame costs more GPU time (the "clock trap", 21.8 against 12.3 ms in the
// cave). The kernel takes a power constraint per context: IOCTL_KGSL_SETPROPERTY KGSL_PROP_PWR_CONSTRAINT
// {KGSL_CONSTRAINT_PWRLEVEL, context, level}, level 1 = the highest clock or 2-100 = a floor at that % of it (GMU DCVS,
// the Adreno 8xx; host-DCVS GPUs take only 1). A descriptor may set it only on contexts it owns, so the device's contexts
// are those this process's /dev/kgsl-3d0 descriptors own after vkCreateDevice and did not own before (Turnip: one per
// VkQueue). The test of ownership is the same ioctl with KGSL_CONSTRAINT_NONE, which changes nothing on a context without
// a constraint. The rule reaches the GMU only while the context is registered (after its first submission, again after
// the GPU slept), so it is sent again every 30 frames.
//
// On the POCO F7 Ultra (Adreno 830, 2026-11-23) the kernel takes it but reports host DCVS, where only "highest" counts and
// only for submissions flagged KGSL_CONTEXT_PWR_CONSTRAINT, which Turnip does not flag: an A/B in one run on the Great Sky
// Island at a forced 60 gave 51.4 / 51.3 and 47.8 / 47.9 fps, no change. Off by default; it may matter where the GMU
// decides the clock (the Adreno 840).
//
// LARECOMP_GPU_FLOOR=<0-100>: the floor in % of the highest clock (100: the highest; 0 or unset: off).
// LARECOMP_AB=<n>:gpufloor: on (at LARECOMP_GPU_FLOOR, 100 if unset) on side 1, off on side 0, every n frames.
// LARECOMP_LOG_GPU_FLOOR=1: each change, and the GPU clock every 2 s when the app can read it.
#ifdef __ANDROID__
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "vk.h"
#include "../../platform.h"
#include "../../rt.h"

struct kgsl_dc { unsigned int type, context_id; void *data; size_t size; };
struct kgsl_gp { unsigned int type; void *value; size_t sizebytes; };
#define KGSL_IOCTL_SETPROPERTY _IOW(0x09, 0x32, struct kgsl_gp)
#define KGSL_PROP_PWR_CONSTRAINT 0x12
enum { KG_FDS = 4, KG_IDS = 1024, KG_CTX = 8 };

static int kg_fd[KG_FDS], kg_nfd;
static uint8_t kg_own[KG_FDS][KG_IDS / 8];
static int was_fd[KG_FDS], was_n;
static uint8_t was_own[KG_FDS][KG_IDS / 8];
static struct { int fd; unsigned id; } kg[KG_CTX];
static int kg_n, kg_host, tried;

static int kg_set(int fd, unsigned id, int type, unsigned level) {
    unsigned lv = level;
    struct kgsl_dc c = {(unsigned)type, id, &lv, sizeof lv};
    struct kgsl_gp g = {KGSL_PROP_PWR_CONSTRAINT, &c, sizeof c};
    int r;
    do r = ioctl(fd, KGSL_IOCTL_SETPROPERTY, &g);
    while (r < 0 && (errno == EINTR || errno == EAGAIN));
    return r < 0 ? -errno : 0;
}

static void kg_scan(void) {  // this process's descriptors on /dev/kgsl-3d0 and the context ids each owns
    kg_nfd = 0;
    DIR *d = opendir("/proc/self/fd");
    if (!d) return;
    struct dirent *de;
    char path[64], to[64];
    while ((de = readdir(d)) && kg_nfd < KG_FDS) {
        int fd = atoi(de->d_name);
        if (fd <= 2) continue;
        snprintf(path, sizeof path, "/proc/self/fd/%d", fd);
        ssize_t n = readlink(path, to, sizeof to - 1);
        if (n <= 0) continue;
        to[n] = 0;
        if (!strncmp(to, "/dev/kgsl-3d", 12)) kg_fd[kg_nfd++] = fd;
    }
    closedir(d);
    for (int i = 0; i < kg_nfd; i++) {
        memset(kg_own[i], 0, sizeof kg_own[i]);
        for (unsigned id = 1; id < KG_IDS; id++)
            if (!kg_set(kg_fd[i], id, 0, 0)) kg_own[i][id / 8] |= (uint8_t)(1u << (id % 8));
    }
}

static int floor_env(void) {
    static int v = -2;
    if (v == -2) {
        const char *e = getenv("LARECOMP_GPU_FLOOR");
        v = e ? atoi(e) : -1;
        if (v > 100) v = 100;
    }
    return v;
}

static bool floor_wanted(void) {
    extern int vk_ab(const char *what);
    return floor_env() > 0 || vk_ab("gpufloor") >= 0;
}

void vk_gpu_floor_before(bool turnip) {  // (device.c, right before vkCreateDevice)
    tried = turnip && floor_wanted();
    if (!tried) return;
    kg_scan();
    was_n = kg_nfd;
    memcpy(was_fd, kg_fd, sizeof kg_fd);
    memcpy(was_own, kg_own, sizeof kg_own);
}

void vk_gpu_floor_after(void) {  // (device.c, right after vkCreateDevice)
    if (!tried) return;
    kg_scan();
    kg_n = 0;
    for (int i = 0; i < kg_nfd; i++) {
        int w = -1;
        for (int j = 0; j < was_n; j++)
            if (was_fd[j] == kg_fd[i]) w = j;
        for (unsigned id = 1; id < KG_IDS && kg_n < KG_CTX; id++)
            if ((kg_own[i][id / 8] >> (id % 8) & 1) && !(w >= 0 && (was_own[w][id / 8] >> (id % 8) & 1))) {
                kg[kg_n].fd = kg_fd[i];
                kg[kg_n].id = id;
                kg_n++;
            }
    }
    if (kg_n) {  // the level form the kernel takes: 2-100 % with GMU DCVS, else -EINVAL and only 1 (the highest)
        kg_host = kg_set(kg[0].fd, kg[0].id, 1, 50) == -EINVAL;
        kg_set(kg[0].fd, kg[0].id, 0, 0);
    }
    char b[96];
    size_t o = 0;
    b[0] = 0;
    for (int k = 0; k < kg_n && o < sizeof b; k++) o += (size_t)snprintf(b + o, sizeof b - o, "%s%u", k ? "," : "", kg[k].id);
    if (kg_n)
        LOG("[vk] GPU clock floor through KGSL: kernel context%s %s (%d descriptor%s; %s)\n", kg_n > 1 ? "s" : "", b, kg_nfd,
            kg_nfd == 1 ? "" : "s", kg_host ? "host DCVS: highest clock only" : "GMU DCVS: floor in % of the highest clock");
    else
        LOG("[vk] GPU clock floor through KGSL: %s\n",
            kg_nfd ? "no kernel context of the device's own" : "no /dev/kgsl-3d0 descriptor");
}

static int gpu_clock_mhz(void) {  // (-1 when the app may not read it)
    static int fd = -2;
    if (fd == -2) fd = open("/sys/class/kgsl/kgsl-3d0/devfreq/cur_freq", O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    char b[32];
    ssize_t n = pread(fd, b, sizeof b - 1, 0);
    if (n <= 0) return -1;
    b[n] = 0;
    return (int)(strtoull(b, NULL, 10) / 1000000);
}

void vk_gpu_floor_frame(uint64_t frame) {  // (device.c, each game frame)
    if (!kg_n) return;
    extern int vk_ab(const char *what);
    int ab = vk_ab("gpufloor"), s = floor_env();
    if (ab >= 0) s = ab ? (s > 0 ? s : 100) : 0;
    if (s < 0) s = 0;
    static int cur = -1;
    static uint64_t sent;
    bool again = s > 0 && (frame >= sent + 30 || frame < sent);
    if (s == cur && !again) return;
    sent = frame;
    int bad = 0;
    for (int k = 0; k < kg_n; k++) {
        int r = s <= 0 ? kg_set(kg[k].fd, kg[k].id, 0, 0)
                       : kg_set(kg[k].fd, kg[k].id, 1, s >= 100 || kg_host ? 1u : (unsigned)(s < 2 ? 2 : s));
        if (r) bad = r;
    }
    static int log = -1;
    if (log < 0) log = getenv("LARECOMP_LOG_GPU_FLOOR") && atoi(getenv("LARECOMP_LOG_GPU_FLOOR"));
    if (cur < 0 || (log && s != cur) || (bad && cur != s)) LOG("[vk] GPU clock floor (KGSL): %d %%: %d\n", s, bad);
    cur = s;
}

void vk_gpu_floor_clock_log(void) {  // (LARECOMP_LOG_GPU_FLOOR=1: the GPU clock every 2 s)
    static int log = -1;
    if (log < 0) log = getenv("LARECOMP_LOG_GPU_FLOOR") && atoi(getenv("LARECOMP_LOG_GPU_FLOOR"));
    if (!log) return;
    static uint64_t last;
    uint64_t now = plat_now_ns();
    if (now - last < 2000000000ull) return;
    last = now;
    extern int vk_ab(const char *what);
    LOG("[vk] GPU clock %d MHz (floor side %d)\n", gpu_clock_mhz(), vk_ab("gpufloor"));
}
#endif
