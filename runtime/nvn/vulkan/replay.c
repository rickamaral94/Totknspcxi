// Android: the draws' Vulkan recording on a thread of its own - see replay.h.
#ifdef __ANDROID__
#define _GNU_SOURCE  // (cpu_set_t)
#include <dlfcn.h>
#include <linux/futex.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "replay.h"
#include "vk_fpenv.h"
#include "../../platform.h"
#include "../../rt.h"

extern uint64_t renderer_present_count(void);

RpReal rp_real;
bool rp_on;
uint64_t rp_ring[RP_WORDS];
uint64_t rp_w, rp_free_end = RP_WORDS;
__thread char rp_tls_mark;
void *rp_owner;
_Atomic uint64_t rp_wpub, rp_rpos;
_Atomic int rp_sleeping;
VkCommandBuffer rp_last_cb;
uint64_t rp_stat_pkts;

static bool installed, started;
static __thread bool on_replay_thread;  // (a driver function the replay thread calls may itself call volk's: device.c sync1_*)
static uint64_t spin_ns = 2000000;  // (50 us: ~26 sleeps a frame, and the render thread waited for each wake-up)
static int log_on;
// (the log) waits for the ring and who asked for them
static uint64_t st_op_ns[32], st_op_n[32], st_op_max[32];  // (the replay thread's time per packet type; racy reads)
static uint64_t st_backlog;  // (words queued when a wait began)
static uint64_t st_drains, st_drain_ns, st_space, st_space_ns, st_sleeps, st_busy_ns;
static _Atomic uint64_t st_busy_atomic, st_sleeps_atomic;
typedef struct { void *ra; uint64_t n, ns; } DrainSite;
static DrainSite sites[32];

static inline void cpu_relax(void) {
#if defined(__aarch64__)
    __asm__ volatile("yield" ::: "memory");
#endif
}
static void futex_wait(_Atomic int *a, int v) { syscall(SYS_futex, (int *)a, FUTEX_WAIT_PRIVATE, v, NULL, NULL, 0); }
static void futex_wake(_Atomic int *a) { syscall(SYS_futex, (int *)a, FUTEX_WAKE_PRIVATE, 1, NULL, NULL, 0); }

void rp_wake(void) {
    if (atomic_exchange_explicit(&rp_sleeping, 0, memory_order_seq_cst)) futex_wake(&rp_sleeping);
}

// The producer waits for the replay thread to free the ring up to `end` (full: several frames' draws behind)
void rp_wait_space(uint64_t end) {
    uint64_t t0 = plat_now_ns();
    atomic_store_explicit(&rp_wpub, rp_w, memory_order_seq_cst);  // (all of it published, and the thread awake)
    rp_wake();
    WdMark wm = vk_wd_enter(WD_RP_WAIT_SPACE, end);  // (freeze watchdog: a producer spinning on a stuck replay thread)
    for (unsigned i = 0; end - atomic_load_explicit(&rp_rpos, memory_order_acquire) > RP_WORDS; i++) {
        cpu_relax();
        if (i > 4096) sched_yield();
    }
    vk_wd_leave(wm);
    st_space++, st_space_ns += plat_now_ns() - t0;
}
// The rest of the ring after idx is too short for the next packet: a WRAP word there, and the packet at the start
void rp_wrap(uint32_t idx) {
    uint32_t pad = RP_WORDS - idx;
    if (rp_w + pad + 1 - atomic_load_explicit(&rp_rpos, memory_order_acquire) > RP_WORDS) rp_wait_space(rp_w + pad + 1);
    rp_ring[idx] = RP_HDR(RP_WRAP, 0, 0, 0);
    rp_w += pad;
}

void vk_rp_drain_at(void *ra) {
    uint64_t w = rp_w;
    // the packets not seen yet: published now by the thread that wrote them; another thread waits for what is published
    if (rp_owner == &rp_tls_mark) { if (atomic_load_explicit(&rp_wpub, memory_order_relaxed) != w) rp_commit(); }
    else w = atomic_load_explicit(&rp_wpub, memory_order_acquire);
    if (atomic_load_explicit(&rp_rpos, memory_order_acquire) == w) return;
    uint64_t t0 = plat_now_ns();
    st_backlog += w - atomic_load_explicit(&rp_rpos, memory_order_relaxed);
    // (up to w, not exactly w: a caller other than the producer may hold an older w while the ring moves on)
    WdMark wm = vk_wd_enter(WD_RP_DRAIN, w);  // (freeze watchdog)
    for (unsigned i = 0; (int64_t)(atomic_load_explicit(&rp_rpos, memory_order_acquire) - w) < 0; i++) {
        cpu_relax();
        // (no sched_yield: the scheduler then ran a game thread on the render thread's core for a whole slice - the
        // render thread was off its core ~40% of the time, 2026-11-15)
        if (i > 2000000) sched_yield();
    }
    vk_wd_leave(wm);
    uint64_t ns = plat_now_ns() - t0;
    st_drains++, st_drain_ns += ns;
    if (log_on) {
        int k = (int)(((uintptr_t)ra >> 2) % 31);
        for (int i = 0; i < 32; i++, k = (k + 1) % 32) {
            if (sites[k].ra && sites[k].ra != ra) continue;
            sites[k].ra = ra, sites[k].n++, sites[k].ns += ns;
            break;
        }
    }
}

// ---- the replay thread
static inline void run(uint64_t *p, uint64_t h) {
    VkCommandBuffer cb = (VkCommandBuffer)(uintptr_t)p[1];
    uint32_t v = (uint32_t)(h >> 32), aux = (uint32_t)(h >> 16) & 0xFFFF;
    switch (h & 0xFF) {
    case RP_U32: ((void (*)(VkCommandBuffer, uint32_t))(uintptr_t)p[2])(cb, v); break;
    case RP_FACE_U32: ((void (*)(VkCommandBuffer, VkStencilFaceFlags, uint32_t))(uintptr_t)p[2])(cb, aux, v); break;
    case RP_STENCIL_OP:
        rp_real.vkCmdSetStencilOp(cb, aux, (VkStencilOp)(uint32_t)p[2], (VkStencilOp)(uint32_t)(p[2] >> 32),
                                  (VkStencilOp)(uint32_t)p[3], (VkCompareOp)(uint32_t)(p[3] >> 32));
        break;
    case RP_DEPTH_BIAS: {
        float f[4];
        memcpy(f, &p[2], 16);
        rp_real.vkCmdSetDepthBias(cb, f[0], f[1], f[2]);
        break;
    }
    case RP_BLEND: {
        float f[4];
        memcpy(f, &p[2], 16);
        rp_real.vkCmdSetBlendConstants(cb, f);
        break;
    }
    case RP_VIEWPORT: {
        VkViewport vp;
        memcpy(&vp, &p[2], sizeof vp);
        rp_real.vkCmdSetViewport(cb, v, 1, &vp);
        break;
    }
    case RP_SCISSOR: {
        VkRect2D r;
        memcpy(&r, &p[2], sizeof r);
        rp_real.vkCmdSetScissor(cb, v, 1, &r);
        break;
    }
    case RP_PIPE: rp_real.vkCmdBindPipeline(cb, (VkPipelineBindPoint)v, (VkPipeline)(uintptr_t)p[2]); break;
    case RP_SETS: {
        uint32_t first = aux & 0xFF, n = aux >> 8 & 0xF;
        VkDescriptorSet sets[8];
        uint32_t dyn[128];
        for (uint32_t i = 0; i < n; i++) sets[i] = (VkDescriptorSet)(uintptr_t)p[3 + i];
        memcpy(dyn, &p[3 + n], v * 4);
        rp_real.vkCmdBindDescriptorSets(cb, (VkPipelineBindPoint)(aux >> 12), (VkPipelineLayout)(uintptr_t)p[2], first, n, sets,
                                        v, dyn);
        break;
    }
    case RP_IB: rp_real.vkCmdBindIndexBuffer(cb, (VkBuffer)(uintptr_t)p[2], p[3], (VkIndexType)v); break;
    case RP_VB: {
        VkBuffer b[16];
        VkDeviceSize o[16];
        for (uint32_t i = 0; i < aux; i++) b[i] = (VkBuffer)(uintptr_t)p[2 + i], o[i] = p[2 + aux + i];
        rp_real.vkCmdBindVertexBuffers(cb, v, aux, b, o);
        break;
    }
    case RP_DRAW: rp_real.vkCmdDraw(cb, (uint32_t)p[2], (uint32_t)(p[2] >> 32), (uint32_t)p[3], (uint32_t)(p[3] >> 32)); break;
    case RP_DRAW_IDX:
        rp_real.vkCmdDrawIndexed(cb, (uint32_t)p[2], (uint32_t)(p[2] >> 32), (uint32_t)p[3], (int32_t)(uint32_t)(p[3] >> 32), v);
        break;
    case RP_PUSH: {
        uint8_t d[256];
        memcpy(d, &p[4], v);
        rp_real.vkCmdPushConstants(cb, (VkPipelineLayout)(uintptr_t)p[2], (VkShaderStageFlags)p[3], aux, v, d);
        break;
    }
    case RP_BEGIN_RENDERING: {
        VkRenderingInfo ri;
        VkRenderingAttachmentInfo at[10];
        memcpy(&ri, &p[2], sizeof ri);
        uint32_t nc = aux & 0xFF, na = nc + (aux >> 8 & 1) + (aux >> 9 & 1);
        for (uint32_t i = 0; i < na; i++)
            memcpy(&at[i], &p[2 + RP_SZW(VkRenderingInfo) + i * RP_SZW(VkRenderingAttachmentInfo)], sizeof at[i]);
        ri.pColorAttachments = nc ? at : NULL;
        ri.pDepthAttachment = aux >> 8 & 1 ? &at[nc] : NULL;
        ri.pStencilAttachment = aux >> 9 & 1 ? &at[nc + (aux >> 8 & 1)] : NULL;
        rp_real.vkCmdBeginRendering(cb, &ri);
        break;
    }
    case RP_END_RENDERING: rp_real.vkCmdEndRendering(cb); break;
    case RP_FULL_BARRIER: {
        VkMemoryBarrier2 mb = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2, .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                               .srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT, .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                               .dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT};
        VkDependencyInfo di = {.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .memoryBarrierCount = 1, .pMemoryBarriers = &mb};
        rp_real.vkCmdPipelineBarrier2(cb, &di);
        break;
    }
    case RP_DISPATCH: rp_real.vkCmdDispatch(cb, (uint32_t)p[2], (uint32_t)(p[2] >> 32), v); break;
    case RP_COPY_I2B: {
        VkBufferImageCopy c;
        memcpy(&c, &p[4], sizeof c);
        rp_real.vkCmdCopyImageToBuffer(cb, (VkImage)(uintptr_t)p[2], (VkImageLayout)v, (VkBuffer)(uintptr_t)p[3], 1, &c);
        break;
    }
    case RP_VI: {  // (read in place: the producer's copy stays in the ring until this packet is done)
        uint32_t nb = aux & 0xFF, na = aux >> 8;
        const VkVertexInputBindingDescription2EXT *vb = (const VkVertexInputBindingDescription2EXT *)&p[2];
        const VkVertexInputAttributeDescription2EXT *va =
            (const VkVertexInputAttributeDescription2EXT *)&p[2 + nb * RP_VI_W(VkVertexInputBindingDescription2EXT)];
        rp_real.vkCmdSetVertexInputEXT(cb, nb, nb ? vb : NULL, na, na ? va : NULL);
        break;
    }
    case RP_MEM_BARRIER: {
        VkMemoryBarrier2 mb = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2, .srcStageMask = p[2], .srcAccessMask = p[3],
                               .dstStageMask = p[4], .dstAccessMask = p[5]};
        VkDependencyInfo di = {.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .memoryBarrierCount = 1, .pMemoryBarriers = &mb};
        rp_real.vkCmdPipelineBarrier2(cb, &di);
        break;
    }
    default: LOG("[replay] bad packet %#llx\n", (unsigned long long)h), abort();
    }
}

static _Atomic int rp_tid;
int vk_rp_tid(void) { return atomic_load_explicit(&rp_tid, memory_order_relaxed); }
static void *rp_main(void *arg) {
    (void)arg;
    vk_fp_thread("vk-replay");  // (made from a game thread: its FPCR - vk_fpenv.h)
    on_replay_thread = true;
    // as the render thread: its priority, and (exec.c) in the ADPF session - else the governor kept its core's clock low
    // and the render thread waited for it (~200 words queued, ~180 us a wait)
    plat_thread_unpin();
    plat_thread_realtime(0);
    atomic_store(&rp_tid, (int)gettid());
    {   // LARECOMP_VK_REPLAY_CPUS=<n>[+<n>...]: the replay thread on those cores (test: beside the render thread's, sharing
        // its cache - the driver's calls were 2-3x slower here than on the render thread, on another cluster)
        const char *e = getenv("LARECOMP_VK_REPLAY_CPUS");
        if (e && *e) {
            cpu_set_t set;
            CPU_ZERO(&set);
            for (const char *q = e; *q;) {
                int c = (int)strtol(q, (char **)&q, 10);
                if (c >= 0 && c < CPU_SETSIZE) CPU_SET(c, &set);
                while (*q == '+') q++;
                if (*q && (*q < '0' || *q > '9')) break;
            }
            int r = sched_setaffinity(0, sizeof set, &set);
            LOG("[replay] on cores %s: %s\n", e, r ? "failed" : "ok");
        }
    }
    uint64_t r = 0;
    for (;;) {
        uint64_t w = atomic_load_explicit(&rp_wpub, memory_order_acquire);
        if (w == r) {  // (spin a little - the next draw's packets are usually microseconds away - then sleep)
            uint64_t t0 = 0;
            for (unsigned i = 0;; i++) {
                cpu_relax();
                w = atomic_load_explicit(&rp_wpub, memory_order_acquire);
                if (w != r) break;
                if (i & 1023) continue;
                uint64_t now = plat_now_ns();
                if (!t0) { t0 = now; continue; }
                if (now - t0 < spin_ns) continue;
                atomic_store_explicit(&rp_sleeping, 1, memory_order_seq_cst);
                w = atomic_load_explicit(&rp_wpub, memory_order_seq_cst);
                if (w != r) { atomic_store_explicit(&rp_sleeping, 0, memory_order_relaxed); break; }
                vk_wd_set(WD_RP_SLEEP, r);  // (freeze watchdog: idle, nothing published; the replay thread's own slot)
                futex_wait(&rp_sleeping, 1);
                atomic_fetch_add_explicit(&st_sleeps_atomic, 1, memory_order_relaxed);
                t0 = 0;
            }
        }
        uint64_t tb = log_on ? plat_now_ns() : 0;
        vk_wd_set(WD_RP_RUN, r);  // (once per batch, not per packet)
        for (unsigned k = 0; r != w;) {
            uint32_t idx = (uint32_t)r & (RP_WORDS - 1);
            uint64_t *p = &rp_ring[idx];
            uint64_t h = p[0];
            if ((h & 0xFF) == RP_WRAP) { r += RP_WORDS - idx; continue; }
            if (log_on) {
                uint64_t t = plat_now_ns();
                run(p, h);
                t = plat_now_ns() - t;
                unsigned op = h & 31;
                st_op_ns[op] += t, st_op_n[op]++;
                if (t > st_op_max[op]) st_op_max[op] = t;
            } else
                run(p, h);
            r += (h >> 8) & 0xFF;
            if (++k == 32) {
                k = 0;
                atomic_store_explicit(&rp_rpos, r, memory_order_release);
                w = atomic_load_explicit(&rp_wpub, memory_order_acquire);  // (more since: in the same batch)
            }
        }
        atomic_store_explicit(&rp_rpos, r, memory_order_release);
        vk_wd_set(WD_NONE, 0);  // (the batch is done: spinning for the next is not a marked call)
        if (tb) atomic_fetch_add_explicit(&st_busy_atomic, plat_now_ns() - tb, memory_order_relaxed);
    }
    return NULL;
}

void vk_rp_latch(bool want) {
    if (!installed) want = false;
    if (installed && !started) {  // (started even while off - an A/B's side 0 - so it is in the ADPF session early)
        pthread_t th;
        if (pthread_create(&th, NULL, rp_main, NULL)) { installed = false; return; }
        pthread_setname_np(th, "vk replay");
        started = true;
        LOG("[replay] the draws' Vulkan recording on its own thread (spin %llu us)\n", (unsigned long long)(spin_ns / 1000));
    }
    rp_on = want;
}

bool vk_rp_installed(void) { return installed; }

void vk_rp_frame(void) {
    if (!log_on) return;
    static uint64_t at, pk0;
    uint64_t f = renderer_present_count();
    if (f < at + 120) return;
    uint64_t frames = at ? f - at : 0;
    at = f;
    uint64_t busy = atomic_exchange(&st_busy_atomic, 0), sleeps = atomic_exchange(&st_sleeps_atomic, 0);
    st_busy_ns = busy, st_sleeps = sleeps;
    if (frames) {
        LOG("[replay] %s: %.0f packets, replay thread busy %.3f ms, %.1f sleeps; ring waits %.1f (%.3f ms, %.0f words queued each), full %.1f (%.3f ms) a frame\n",
            rp_on ? "on" : "off", (double)(rp_stat_pkts - pk0) / frames, busy / 1e6 / frames, (double)sleeps / frames,
            (double)st_drains / frames, st_drain_ns / 1e6 / frames, st_drains ? (double)st_backlog / st_drains : 0.0,
            (double)st_space / frames, st_space_ns / 1e6 / frames);
        {   // the replay thread's time by packet type
            static const char *nm[] = {"wrap", "u32", "face", "stencil op", "depth bias", "blend", "viewport", "scissor", "pipe",
                                       "sets", "ib", "vb", "draw", "draw idx", "push", "begin", "end", "barrier",
                                       "dispatch", "copy i2b", "mem barrier", "vi"};
            char b[1024];
            int n = 0;
            for (int k = 1; k < 22 && n < 900; k++)
                if (st_op_n[k]) n += snprintf(b + n, sizeof b - n, " %s %.0f/%.3fms/max%.0fus", nm[k], (double)st_op_n[k] / frames,
                                              st_op_ns[k] / 1e6 / frames, st_op_max[k] / 1e3);
            LOG("[replay]   per frame:%s\n", b);
            memset(st_op_ns, 0, sizeof st_op_ns), memset(st_op_n, 0, sizeof st_op_n), memset(st_op_max, 0, sizeof st_op_max);
        }
        for (int j = 0; j < 6; j++) {  // the callers that waited longest
            int b = -1;
            for (int i = 0; i < 32; i++)
                if (sites[i].ra && sites[i].ns && (b < 0 || sites[i].ns > sites[b].ns)) b = i;
            if (b < 0) break;
            Dl_info di;
            uintptr_t off = (uintptr_t)sites[b].ra;
            const char *lib = "?";
            if (dladdr(sites[b].ra, &di) && di.dli_fbase) off -= (uintptr_t)di.dli_fbase, lib = di.dli_fname;
            const char *sl = strrchr(lib, '/');
            LOG("[replay]   wait at %s+%#lx: %.1f a frame, %.3f ms\n", sl ? sl + 1 : lib, (unsigned long)off,
                (double)sites[b].n / frames, sites[b].ns / 1e6 / frames);
            sites[b].ns = 0;
        }
    }
    memset(sites, 0, sizeof sites);
    pk0 = rp_stat_pkts;
    st_drains = st_drain_ns = st_space = st_space_ns = st_backlog = 0;
}

// ---- the checks in front of volk's pointers: a command into the buffer the packets went to waits for them first
#define SYNC(cb) do { if ((cb) == rp_last_cb && !on_replay_thread && atomic_load_explicit(&rp_rpos, memory_order_acquire) != rp_w) \
                          vk_rp_drain_at(__builtin_return_address(0)); } while (0)
#define SYNC_ANY() do { if (!on_replay_thread && atomic_load_explicit(&rp_rpos, memory_order_acquire) != rp_w) \
                            vk_rp_drain_at(__builtin_return_address(0)); } while (0)
#define SHIM(f, params, args) static VKAPI_ATTR void VKAPI_CALL shim_##f params { SYNC(cb); rp_real.f args; }
SHIM(vkCmdBindPipeline, (VkCommandBuffer cb, VkPipelineBindPoint a, VkPipeline b), (cb, a, b))
SHIM(vkCmdBindDescriptorSets, (VkCommandBuffer cb, VkPipelineBindPoint a, VkPipelineLayout b, uint32_t c, uint32_t d,
                               const VkDescriptorSet *e, uint32_t f_, const uint32_t *g), (cb, a, b, c, d, e, f_, g))
SHIM(vkCmdBindIndexBuffer, (VkCommandBuffer cb, VkBuffer a, VkDeviceSize b, VkIndexType c), (cb, a, b, c))
SHIM(vkCmdBindVertexBuffers, (VkCommandBuffer cb, uint32_t a, uint32_t b, const VkBuffer *c, const VkDeviceSize *d), (cb, a, b, c, d))
SHIM(vkCmdDraw, (VkCommandBuffer cb, uint32_t a, uint32_t b, uint32_t c, uint32_t d), (cb, a, b, c, d))
SHIM(vkCmdDrawIndexed, (VkCommandBuffer cb, uint32_t a, uint32_t b, uint32_t c, int32_t d, uint32_t e), (cb, a, b, c, d, e))
SHIM(vkCmdDrawIndirect, (VkCommandBuffer cb, VkBuffer a, VkDeviceSize b, uint32_t c, uint32_t d), (cb, a, b, c, d))
SHIM(vkCmdDrawIndexedIndirect, (VkCommandBuffer cb, VkBuffer a, VkDeviceSize b, uint32_t c, uint32_t d), (cb, a, b, c, d))
SHIM(vkCmdPushConstants, (VkCommandBuffer cb, VkPipelineLayout a, VkShaderStageFlags b, uint32_t c, uint32_t d, const void *e),
     (cb, a, b, c, d, e))
SHIM(vkCmdSetViewport, (VkCommandBuffer cb, uint32_t a, uint32_t b, const VkViewport *c), (cb, a, b, c))
SHIM(vkCmdSetScissor, (VkCommandBuffer cb, uint32_t a, uint32_t b, const VkRect2D *c), (cb, a, b, c))
SHIM(vkCmdSetDepthBias, (VkCommandBuffer cb, float a, float b, float c), (cb, a, b, c))
SHIM(vkCmdSetBlendConstants, (VkCommandBuffer cb, const float a[4]), (cb, a))
SHIM(vkCmdSetStencilCompareMask, (VkCommandBuffer cb, VkStencilFaceFlags a, uint32_t b), (cb, a, b))
SHIM(vkCmdSetStencilWriteMask, (VkCommandBuffer cb, VkStencilFaceFlags a, uint32_t b), (cb, a, b))
SHIM(vkCmdSetStencilReference, (VkCommandBuffer cb, VkStencilFaceFlags a, uint32_t b), (cb, a, b))
SHIM(vkCmdSetCullMode, (VkCommandBuffer cb, VkCullModeFlags a), (cb, a))
SHIM(vkCmdSetFrontFace, (VkCommandBuffer cb, VkFrontFace a), (cb, a))
SHIM(vkCmdSetPrimitiveTopology, (VkCommandBuffer cb, VkPrimitiveTopology a), (cb, a))
SHIM(vkCmdSetDepthTestEnable, (VkCommandBuffer cb, VkBool32 a), (cb, a))
SHIM(vkCmdSetDepthWriteEnable, (VkCommandBuffer cb, VkBool32 a), (cb, a))
SHIM(vkCmdSetDepthCompareOp, (VkCommandBuffer cb, VkCompareOp a), (cb, a))
SHIM(vkCmdSetStencilTestEnable, (VkCommandBuffer cb, VkBool32 a), (cb, a))
SHIM(vkCmdSetStencilOp, (VkCommandBuffer cb, VkStencilFaceFlags a, VkStencilOp b, VkStencilOp c, VkStencilOp d, VkCompareOp e),
     (cb, a, b, c, d, e))
SHIM(vkCmdSetPrimitiveRestartEnable, (VkCommandBuffer cb, VkBool32 a), (cb, a))
SHIM(vkCmdSetDepthBiasEnable, (VkCommandBuffer cb, VkBool32 a), (cb, a))
SHIM(vkCmdSetVertexInputEXT, (VkCommandBuffer cb, uint32_t a, const VkVertexInputBindingDescription2EXT *b, uint32_t c,
                              const VkVertexInputAttributeDescription2EXT *d), (cb, a, b, c, d))
SHIM(vkCmdClearAttachments, (VkCommandBuffer cb, uint32_t a, const VkClearAttachment *b, uint32_t c, const VkClearRect *d),
     (cb, a, b, c, d))
SHIM(vkCmdBeginRendering, (VkCommandBuffer cb, const VkRenderingInfo *a), (cb, a))
SHIM(vkCmdEndRendering, (VkCommandBuffer cb), (cb))
SHIM(vkCmdBeginRenderPass, (VkCommandBuffer cb, const VkRenderPassBeginInfo *a, VkSubpassContents b), (cb, a, b))
SHIM(vkCmdEndRenderPass, (VkCommandBuffer cb), (cb))
SHIM(vkCmdNextSubpass, (VkCommandBuffer cb, VkSubpassContents a), (cb, a))
SHIM(vkCmdExecuteCommands, (VkCommandBuffer cb, uint32_t a, const VkCommandBuffer *b), (cb, a, b))
SHIM(vkCmdPipelineBarrier, (VkCommandBuffer cb, VkPipelineStageFlags a, VkPipelineStageFlags b, VkDependencyFlags c, uint32_t d,
                            const VkMemoryBarrier *e, uint32_t f_, const VkBufferMemoryBarrier *g, uint32_t h,
                            const VkImageMemoryBarrier *i), (cb, a, b, c, d, e, f_, g, h, i))
SHIM(vkCmdPipelineBarrier2, (VkCommandBuffer cb, const VkDependencyInfo *a), (cb, a))
SHIM(vkCmdWriteTimestamp, (VkCommandBuffer cb, VkPipelineStageFlagBits a, VkQueryPool b, uint32_t c), (cb, a, b, c))
SHIM(vkCmdWriteTimestamp2, (VkCommandBuffer cb, VkPipelineStageFlags2 a, VkQueryPool b, uint32_t c), (cb, a, b, c))
SHIM(vkCmdResetQueryPool, (VkCommandBuffer cb, VkQueryPool a, uint32_t b, uint32_t c), (cb, a, b, c))
SHIM(vkCmdBeginQuery, (VkCommandBuffer cb, VkQueryPool a, uint32_t b, VkQueryControlFlags c), (cb, a, b, c))
SHIM(vkCmdEndQuery, (VkCommandBuffer cb, VkQueryPool a, uint32_t b), (cb, a, b))
SHIM(vkCmdCopyQueryPoolResults, (VkCommandBuffer cb, VkQueryPool a, uint32_t b, uint32_t c, VkBuffer d, VkDeviceSize e,
                                 VkDeviceSize f_, VkQueryResultFlags g), (cb, a, b, c, d, e, f_, g))
SHIM(vkCmdCopyBuffer, (VkCommandBuffer cb, VkBuffer a, VkBuffer b, uint32_t c, const VkBufferCopy *d), (cb, a, b, c, d))
SHIM(vkCmdCopyImage, (VkCommandBuffer cb, VkImage a, VkImageLayout b, VkImage c, VkImageLayout d, uint32_t e, const VkImageCopy *f_),
     (cb, a, b, c, d, e, f_))
SHIM(vkCmdBlitImage, (VkCommandBuffer cb, VkImage a, VkImageLayout b, VkImage c, VkImageLayout d, uint32_t e, const VkImageBlit *f_,
                      VkFilter g), (cb, a, b, c, d, e, f_, g))
SHIM(vkCmdCopyBufferToImage, (VkCommandBuffer cb, VkBuffer a, VkImage b, VkImageLayout c, uint32_t d, const VkBufferImageCopy *e),
     (cb, a, b, c, d, e))
SHIM(vkCmdCopyImageToBuffer, (VkCommandBuffer cb, VkImage a, VkImageLayout b, VkBuffer c, uint32_t d, const VkBufferImageCopy *e),
     (cb, a, b, c, d, e))
SHIM(vkCmdResolveImage, (VkCommandBuffer cb, VkImage a, VkImageLayout b, VkImage c, VkImageLayout d, uint32_t e,
                         const VkImageResolve *f_), (cb, a, b, c, d, e, f_))
SHIM(vkCmdFillBuffer, (VkCommandBuffer cb, VkBuffer a, VkDeviceSize b, VkDeviceSize c, uint32_t d), (cb, a, b, c, d))
SHIM(vkCmdUpdateBuffer, (VkCommandBuffer cb, VkBuffer a, VkDeviceSize b, VkDeviceSize c, const void *d), (cb, a, b, c, d))
SHIM(vkCmdClearColorImage, (VkCommandBuffer cb, VkImage a, VkImageLayout b, const VkClearColorValue *c, uint32_t d,
                            const VkImageSubresourceRange *e), (cb, a, b, c, d, e))
SHIM(vkCmdClearDepthStencilImage, (VkCommandBuffer cb, VkImage a, VkImageLayout b, const VkClearDepthStencilValue *c, uint32_t d,
                                   const VkImageSubresourceRange *e), (cb, a, b, c, d, e))
SHIM(vkCmdDispatch, (VkCommandBuffer cb, uint32_t a, uint32_t b, uint32_t c), (cb, a, b, c))
SHIM(vkCmdDispatchIndirect, (VkCommandBuffer cb, VkBuffer a, VkDeviceSize b), (cb, a, b))
#undef SHIM
static VKAPI_ATTR VkResult VKAPI_CALL shim_vkBeginCommandBuffer(VkCommandBuffer cb, const VkCommandBufferBeginInfo *a) {
    SYNC(cb);
    return rp_real.vkBeginCommandBuffer(cb, a);
}
static VKAPI_ATTR VkResult VKAPI_CALL shim_vkEndCommandBuffer(VkCommandBuffer cb) {
    SYNC(cb);
    return rp_real.vkEndCommandBuffer(cb);
}
static VKAPI_ATTR VkResult VKAPI_CALL shim_vkResetCommandBuffer(VkCommandBuffer cb, VkCommandBufferResetFlags a) {
    SYNC(cb);
    return rp_real.vkResetCommandBuffer(cb, a);
}
static VKAPI_ATTR VkResult VKAPI_CALL shim_vkResetCommandPool(VkDevice d, VkCommandPool p, VkCommandPoolResetFlags a) {
    SYNC_ANY();
    return rp_real.vkResetCommandPool(d, p, a);
}
static VKAPI_ATTR void VKAPI_CALL shim_vkFreeCommandBuffers(VkDevice d, VkCommandPool p, uint32_t n, const VkCommandBuffer *c) {
    SYNC_ANY();
    rp_real.vkFreeCommandBuffers(d, p, n, c);
}

void vk_rp_install(void) {
    const char *e = getenv("LARECOMP_VK_REPLAY"), *ab = getenv("LARECOMP_AB");
    bool want = (e && atoi(e) != 0) || (ab && strstr(ab, ":replay"));
    if (!want || vkCmdDraw == shim_vkCmdDraw) return;  // (off, or already in: device.c may run this again)
    if (getenv("LARECOMP_VK_REPLAY_SPIN_US")) spin_ns = (uint64_t)atoll(getenv("LARECOMP_VK_REPLAY_SPIN_US")) * 1000;
    log_on = getenv("LARECOMP_VK_REPLAY_LOG") != NULL;
#define RP_SAVE(f) rp_real.f = f; if (f) f = shim_##f;
    RP_FUNCS(RP_SAVE)
#undef RP_SAVE
    installed = true;
}
#else
typedef int replay_unused;  // (Android only)
#endif
