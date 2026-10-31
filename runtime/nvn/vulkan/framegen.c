// Frame generation 2x (docs/FRAMEGEN.md § B2): Video > Frame generation, with the shaders from the user's own
// Lossless.dll (android/.../cpp/lsfg_extract.cpp) and lsfg-vk's framegen on our device and queue (lsfg_framegen_c.h).
//
// The game runs at 30 (hle/framepace.c framepace_target); each real frame B is drawn, as the window would get it, into
// an input image, and the framegen makes I(A, B) between it and the one before (A). The window then shows
//   ... A -> I(A, B) -> B ...
// Pacing: where the platform gives presentation times (VK_GOOGLE_display_timing, Android's swapchain), I and B are
// queued together in FIFO with times one 60 Hz slot apart, and the render thread never waits; elsewhere I goes at
// once and the render thread waits one output period P (half the real frames' interval) before B. Either way B shows
// one slot later than without the frame generation.
// The menus and the overlay are drawn over each shown image, never interpolated; the game's own HUD is in its image,
// and is (known limit).
//   - The first real frame after a reset (out of the menus, a load, a new swapchain) shows alone.
//   - An I whose B came too soon after the last shown image (the window would get two images inside half a period) is
//     dropped, B shown at once: the queue of shown images never grows.
//   - Only in play (diag_in_gameplay): the menus, the pause, events and movies show the real frames alone.
// Order and memory: everything goes to our one queue from this thread. Our image of B is submitted before the
// framegen's work (presentContext submits its own), and full memory barriers at the start of each of our command
// buffers order our reads after its writes and its reads after ours (the images stay in GENERAL, as it wants them).
// Turning it on takes the app's restart (the device's features: device.c); off takes effect at once.
// LARECOMP_FG_LOG=1: a [fg] line every 2 s (always while measuring).
#ifdef __ANDROID__
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vk.h"
#include "../../diag.h"
#include "../../platform.h"
#include "../../settings.h"
#include "../../ui/menu.h"
#include "lsfg_extract.h"
#include "lsfg_framegen_c.h"

#define FG_FORMAT VK_FORMAT_R8G8B8A8_UNORM  // (the images: storage-capable; the window's UNORM bytes as they are)

static bool fg_dev;          // the device has the features (device.c)
static int fg_failed;        // stopped for this session (fg_why says why)
static char fg_why[256];
static char fg_dir[1024], fg_caps[512];

// ------------------------------------------------------------------ the device
static const char *const known_caps[] = {"Shader", "ImageQuery", "StorageImageWriteWithoutFormat", "StorageImageReadWithoutFormat",
                                         "StorageImageExtendedFormats", "VulkanMemoryModel", "VulkanMemoryModelDeviceScope"};

bool fg_cap(const char *name) {
    size_t n = strlen(name);
    for (const char *p = fg_caps; (p = strstr(p, name)); p += n)
        if ((p == fg_caps || p[-1] == ' ') && (p[n] == ' ' || !p[n])) return true;
    return false;
}

// (device.c, before making the device) Video > Frame generation on and a complete set of shaders whose capabilities
// this file knows how to give: the features are asked for. Off: the device as without it.
bool fg_device_wanted(void) {
    if (!g_settings.framegen) return false;
    if (!lsfg_installed_set(fg_dir, sizeof fg_dir, fg_caps, sizeof fg_caps)) {
        snprintf(fg_why, sizeof fg_why, "no Lossless.dll shaders");
        return false;
    }
    char c[sizeof fg_caps];
    snprintf(c, sizeof c, "%s", fg_caps);
    char *save = NULL;
    for (char *t = strtok_r(c, " ", &save); t; t = strtok_r(NULL, " ", &save)) {
        bool ok = false;
        for (size_t i = 0; i < sizeof known_caps / sizeof *known_caps; i++) ok |= !strcmp(t, known_caps[i]);
        if (!ok) {
            snprintf(fg_why, sizeof fg_why, "the shaders need %s, which this version doesn't ask the driver for", t);
            return false;
        }
    }
    return true;
}

void fg_device_made(bool ok, const char *why) {
    fg_dev = ok;
    if (!ok) snprintf(fg_why, sizeof fg_why, "%s", why ? why : "the driver lacks a feature");
    LOG("[fg] frame generation: %s%s%s\n", ok ? "the device has its features (shaders: " : "unavailable: ", ok ? fg_caps : fg_why,
        ok ? ")" : "");
}

// ------------------------------------------------------------------ resources
typedef struct { VkImage img; VmaAllocation mem; VkImageView view; } Img;
static Img fin[2], fout[3];  // (the real frames' inputs; one output per generated frame)
static int ctx_nout;
static bool lib_on, ctx_on;
static int32_t ctx_id;
static VkExtent2D ctx_ext;
static uint64_t fg_count;     // real frames given to the framegen (the one of number k is in fin[k % 2])
static bool have_prev;        // fin[(fg_count - 1) % 2] holds the real frame before (a generated one can be shown)
static uint64_t last_real_ns, last_shown_ns, base_ewma_ns;
static uint64_t next_slot_ns;  // (with presentation times) the next free slot of the 60 Hz output grid
static VkQueryPool ts_pool;   // (the framegen's GPU time: our timestamps either side of its work)
static uint32_t ts_i;
static bool ts_used[8];
static uint64_t st_real, st_gen, st_drop, st_gpu_ns, st_gpu_n, st_since, st_hud;
static uint32_t hud_w, hud_h;  // (the presented image's size: the HUD's target is the same - exec.c hud_capture)
static double fg_gpu_ewma_ms;
static uint64_t win_real, win_shown, win_t0;  // (the overlay's counts, every second)
static float ov_real, ov_shown;

static void img_free(Img *m) {
    if (m->view) vkDestroyImageView(g_vk.dev, m->view, NULL);
    if (m->img) vmaDestroyImage(g_vk.vma, m->img, m->mem);
    memset(m, 0, sizeof *m);
}

static bool img_make(Img *m, VkExtent2D e) {
    VkImageCreateInfo ci = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D, .format = FG_FORMAT,
                            .extent = {e.width, e.height, 1}, .mipLevels = 1, .arrayLayers = 1, .samples = VK_SAMPLE_COUNT_1_BIT,
                            .tiling = VK_IMAGE_TILING_OPTIMAL,
                            .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT};
    VmaAllocationCreateInfo ai = {.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE};
    if (vmaCreateImage(g_vk.vma, &ci, &ai, &m->img, &m->mem, NULL) != VK_SUCCESS) return false;
    VkImageViewCreateInfo vi = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = m->img, .viewType = VK_IMAGE_VIEW_TYPE_2D,
                                .format = FG_FORMAT, .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    if (vkCreateImageView(g_vk.dev, &vi, NULL, &m->view) != VK_SUCCESS) return false;
    vk_image_init_layout(vk_cmd(), m->img, VK_IMAGE_ASPECT_COLOR_BIT);
    return true;
}

// The context and its images go (the GPU done with them first: renderer_flush waits for our work, deleteContext for
// the framegen's)
static void release(void) {
    if (!ctx_on && !fin[0].img) return;
    void renderer_flush(void);
    renderer_flush();
    char err[256];
    if (ctx_on && lsfgfg_delete(ctx_id, err, sizeof err)) LOG("[fg] %s\n", err);
    ctx_on = false;
    img_free(&fin[0]), img_free(&fin[1]);
    for (int i = 0; i < 3; i++) img_free(&fout[i]);
    have_prev = false;
}

static void fail(const char *why, int code) {
    snprintf(fg_why, sizeof fg_why, "%s", why);
    LOG("[fg] frame generation stopped: %s\n", why);
    fg_failed = 1;
    if (code == LSFGFG_DEVICE_LOST) {  // (not again next session: the setting off, with the reason)
        g_settings.framegen = false;
        void settings_save(void);
        settings_save();
        snprintf(fg_why, sizeof fg_why, "the driver lost the device during frame generation: it was turned off");
        return;  // (no clean-up on a lost device)
    }
    release();
}

static const float flow_of[3] = {1.0f, 0.75f, 0.5f};
static int lib_flow = -1, lib_perf = -1, lib_mult = -1;  // (what the framegen was made with: a change makes it again, at once)

// The multipliers: 2x 30 -> 60, 3x 40 -> 120, 4x 30 -> 120 (Video > Frame generation multiplier). Above 60 shown it
// takes presentation times and a screen that runs at 120 Hz: Android is asked for the rate (vk_set_frame_rate), and the
// swapchain's refresh cycle (VK_GOOGLE_display_timing) says what it got; short of that, 2x, and the menu says why.
static const struct { int mult, base, out; } modes[3] = {{2, 30, 60}, {3, 40, 120}, {4, 30, 120}};
static uint64_t refresh_ns;   // the screen's refresh period as the swapchain reports it (0: unknown)
static int rate_asked;        // the frame rate asked of the window (0: none)
static char mode_note[160];   // why a multiplier above 2x isn't in effect

static int mode_now(void) {
    int m = g_settings.fg_mult >= 0 && g_settings.fg_mult <= 2 ? g_settings.fg_mult : 0;
    mode_note[0] = 0;
    if (modes[m].out <= 60) return m;
    if (!g_vk.display_timing) {
        snprintf(mode_note, sizeof mode_note, "%dx needs presentation times (VK_GOOGLE_display_timing): 2x", modes[m].mult);
        return 0;
    }
    if (refresh_ns && refresh_ns > 1000000000ull / (uint64_t)modes[m].out * 11 / 10) {
        snprintf(mode_note, sizeof mode_note, "%dx needs the screen at %d Hz; it runs at %.0f Hz: 2x", modes[m].mult, modes[m].out,
                 1e9 / (double)refresh_ns);
        return 0;
    }
    return m;
}

static bool ready(VkExtent2D e) {
    char err[512];
    int flow = g_settings.fg_flow >= 0 && g_settings.fg_flow <= 2 ? g_settings.fg_flow : 2, perf = g_settings.fg_performance;
    int mult = modes[mode_now()].mult;
    if (lib_on && (flow != lib_flow || perf != lib_perf || mult != lib_mult)) {
        release();
        lsfgfg_finalize();
        lib_on = false;
        LOG("[fg] settings changed: the framegen made again\n");
    }
    if (!lib_on) {
        VkFormatProperties fp;
        vkGetPhysicalDeviceFormatProperties(g_vk.phys, FG_FORMAT, &fp);
        VkFormatFeatureFlags need = VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
        if ((fp.optimalTilingFeatures & need) != need) { fail("RGBA8 images can't be storage images on this driver", 0); return false; }
        int r = lsfgfg_init(vkGetInstanceProcAddr, g_vk.inst, g_vk.phys, g_vk.dev, g_vk.qfam, g_vk.queue, flow_of[flow], mult - 1,
                            perf, fg_dir, err, sizeof err);
        if (r) { fail(err, r); return false; }
        lib_on = true;
        lib_flow = flow, lib_perf = perf, lib_mult = mult;
        VkQueryPoolCreateInfo qi = {.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO, .queryType = VK_QUERY_TYPE_TIMESTAMP, .queryCount = 16};
        if (!ts_pool && g_vk.props.limits.timestampComputeAndGraphics && vkCreateQueryPool(g_vk.dev, &qi, NULL, &ts_pool) != VK_SUCCESS)
            ts_pool = VK_NULL_HANDLE;
        LOG("[fg] framegen ready (LSFG 3.1%s, %dx, flow scale %.2f)\n", perf ? "P, performance" : "", mult, flow_of[flow]);
    }
    if (ctx_on && (ctx_ext.width != e.width || ctx_ext.height != e.height)) release();
    if (!ctx_on) {
        bool ok = img_make(&fin[0], e) && img_make(&fin[1], e);
        for (int i = 0; ok && i < mult - 1; i++) ok = img_make(&fout[i], e);
        if (!ok) { fail("out of GPU memory for its images", 0); return false; }
        VkImage outs[3] = {fout[0].img, fout[1].img, fout[2].img};
        int r = lsfgfg_create(fin[0].img, fin[1].img, outs, mult - 1, e.width, e.height, FG_FORMAT, &ctx_id, err, sizeof err);
        ctx_nout = mult - 1;
        if (r) { fail(err, r); return false; }
        ctx_on = true;
        ctx_ext = e;
        fg_count = 0;
        have_prev = false;
        LOG("[fg] context %ux%u\n", e.width, e.height);
    }
    return true;
}

// ------------------------------------------------------------------ the frame
// (device.c vk_present_now) whether this presented frame goes through the frame generation
// (device.c) FIFO presentation while the frame generation paces its images by their presentation times
bool fg_wants_fifo(void) { return g_vk.display_timing && fg_dev && !fg_failed && g_settings.framegen; }

bool fg_use_now(void) {
    {   // (turned on or off: the swapchain made again in the other present mode before the next image)
        static int last = -1, last_mult = -1;
        int f = fg_wants_fifo();
        if ((last >= 0 && f != last) || (f && last_mult >= 0 && g_settings.fg_mult != last_mult)) g_vk.sc_remake = true;
        last = f, last_mult = g_settings.fg_mult;  // (and another multiplier: another number of images)
    }
    bool on = fg_dev && !fg_failed && g_settings.framegen;
    if (!on) {
        if (ctx_on) {
            release();  // (turned off in play: at once)
            extern void vk_set_frame_rate(float hz);
            vk_set_frame_rate(0);
            rate_asked = 0;
        }
        return false;
    }
    bool unorm = g_vk.sc_format == VK_FORMAT_B8G8R8A8_UNORM || g_vk.sc_format == VK_FORMAT_R8G8B8A8_UNORM;
    if (!unorm) {
        fail("the window's format isn't UNORM", 0);
        return false;
    }
    if (!diag_in_gameplay()) {
        have_prev = false;  // (the menus, events, movies: real frames alone; the next one starts over)
        return false;
    }
    return true;
}

// (hle/framepace.c) the game's frame rate while the frame generation is on (the multiplier's base), else 0
int fg_base_fps(void) { return fg_dev && !fg_failed && g_settings.framegen ? modes[mode_now()].base : 0; }

// (exec.c hud_capture) whether to copy a w x h HUD target before its HUD: the frame generation runs, with its HUD mask,
// in play, and it is the presented image's size
bool fg_hud_capture_wanted(uint32_t w, uint32_t h) {
    return ctx_on && !fg_failed && g_settings.fg_hud && w == hud_w && h == hud_h && diag_in_gameplay();
}

// (device.c) the most images a real frame queues (the swapchain's extra images)
int fg_queued_images(void) { return modes[g_settings.fg_mult >= 0 && g_settings.fg_mult <= 2 ? g_settings.fg_mult : 0].mult; }

// (device.c) one of our images into the window, with the menus and the overlay over it; false: no window image now
static bool show(VkImageView view) {
    extern bool vk_frame_begin(const float clear[4]);
    extern void vk_frame_end(const char *shot_path);
    extern void vk_diag_presented(void);
    static const float black[4] = {0, 0, 0, 1};
    vk_full_barrier(vk_cmd());  // (the framegen's writes / our image of the frame, before the window's draw reads them)
    if (!vk_frame_begin(black)) return false;
    VkCommandBuffer cb = vk_cmd();
    static const float whole[8] = {0, 0, 0, 0, 0, 0, 1, 1};
    vk_present_draw_view(cb, view, g_vk.sc_format, g_vk.sc_extent, 0, 0, (float)g_vk.sc_extent.width, (float)g_vk.sc_extent.height, whole);
    menu_render((void *)cb, g_vk.sc_extent.width, g_vk.sc_extent.height);
    vk_frame_end(NULL);
    vk_diag_presented();
    last_shown_ns = plat_now_ns();
    win_shown++;
    return true;
}

static void ts_write(VkCommandBuffer cb, uint32_t q) {
    if (ts_pool) vkCmdWriteTimestamp2(cb, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, ts_pool, q);
}

// The real frame b (laid out as the window would get it: vx..vh, pv) through the frame generation. false: not done,
// the caller presents it as usual.
bool fg_frame(TexBackend *b, float vx, float vy, float vw, float vh, const float pv[8]) {
    VkExtent2D e = g_vk.sc_extent;
    {   // the rate asked of Android (the multiplier's shown rate), and the refresh the swapchain reports, each second
        static uint64_t next;
        uint64_t t = plat_now_ns();
        int want = modes[g_settings.fg_mult >= 0 && g_settings.fg_mult <= 2 ? g_settings.fg_mult : 0].out;
        if (want != rate_asked) {
            extern void vk_set_frame_rate(float hz);
            vk_set_frame_rate((float)want);
            rate_asked = want;
            next = 0;
        }
        if (g_vk.display_timing && t >= next) {
            next = t + 1000000000ull;
            VkRefreshCycleDurationGOOGLE rc;
            if (vkGetRefreshCycleDurationGOOGLE(g_vk.dev, g_vk.swapchain, &rc) == VK_SUCCESS && rc.refreshDuration) {
                if (!refresh_ns || (refresh_ns > rc.refreshDuration ? refresh_ns - rc.refreshDuration : rc.refreshDuration - refresh_ns) > 500000)
                    LOG("[fg] the screen refreshes at %.1f Hz\n", 1e9 / (double)rc.refreshDuration);
                refresh_ns = rc.refreshDuration;
            }
        }
    }
    if (!ready(e)) return false;
    uint64_t now = plat_now_ns();
    if (last_real_ns && now - last_real_ns > 100000000ull) have_prev = false;  // (a pause, a load: start over)
    if (last_real_ns && have_prev) {
        uint64_t d = now - last_real_ns;
        base_ewma_ns = base_ewma_ns ? (base_ewma_ns * 7 + d) / 8 : d;
    }
    last_real_ns = now;
    // B into its input, as the window would get it
    uint32_t k = (uint32_t)(fg_count & 1);
    VkCommandBuffer cb = vk_cmd();
    VkImageView src = vk_present_source(cb, b, vw, vh, pv);
    if (!src) return false;
    cb = vk_cmd();  // (the post passes record into the current buffer; taken again in case it changed)
    uint32_t tq = ts_i * 2;
    if (ts_pool) {
        if (ts_used[ts_i]) {  // (its last use, 8 real frames ago)
            uint64_t t[2];
            if (vkGetQueryPoolResults(g_vk.dev, ts_pool, tq, 2, sizeof t, t, 8, VK_QUERY_RESULT_64_BIT) == VK_SUCCESS && t[1] > t[0])
                st_gpu_ns += (uint64_t)((double)(t[1] - t[0]) * g_vk.props.limits.timestampPeriod), st_gpu_n++;
            ts_used[ts_i] = false;
        }
        vkCmdResetQueryPool(cb, ts_pool, tq, 2);
    }
    vk_full_barrier(cb);  // (the input's last readers - the framegen, our draw into the window - before it's written)
    VkRenderingAttachmentInfo ca = {.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, .imageView = fin[k].view,
                                    .imageLayout = VK_IMAGE_LAYOUT_GENERAL, .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                                    .storeOp = VK_ATTACHMENT_STORE_OP_STORE, .clearValue.color.float32 = {0, 0, 0, 1}};
    VkRenderingInfo ri = {.sType = VK_STRUCTURE_TYPE_RENDERING_INFO, .renderArea = {{0, 0}, e}, .layerCount = 1,
                          .colorAttachmentCount = 1, .pColorAttachments = &ca};
    const VkFormat f = FG_FORMAT;
    vk_begin_rendering(cb, &ri, &f, VK_FORMAT_UNDEFINED, VK_SAMPLE_COUNT_1_BIT);
    VkRect2D sc = {{0, 0}, e};
    vkCmdSetScissor(cb, 0, 1, &sc);
    vk_present_draw_view(cb, src, f, e, vx, vy, vw, vh, pv);
    vk_end_rendering(cb);
    vk_full_barrier(cb);  // (B's pixels, before the framegen reads them)
    // the HUD mask of B (Video > Frame generation HUD mask): where its HUD is, the generated frames take B's pixels
    extern VkImageView vk_fg_hud_mask(VkCommandBuffer, TexBackend *, VkExtent2D, float, float, float, float, const float *);
    extern VkImageView vk_fg_hud_composite(VkCommandBuffer, VkImageView, VkImageView, VkImageView, VkExtent2D);
    hud_w = b->width, hud_h = b->height;
    VkImageView hud = g_settings.fg_hud ? vk_fg_hud_mask(cb, b, e, vx, vy, vw, vh, pv) : VK_NULL_HANDLE;
    if (hud) st_hud++;
    ts_write(cb, tq);
    void renderer_submit(void);
    renderer_submit();  // (ours first on the queue; the framegen's next)
    char err[512];
    int r = lsfgfg_present(ctx_id, err, sizeof err);
    if (r) {
        fail(err, r);
        return false;
    }
    fg_count++;
    // I(A, B), then B a period later - or B alone
    uint64_t period;
    if (g_vk.display_timing) {
        // With presentation times (FIFO): I at the next slot of a 60 Hz grid, B at the one after, both queued at once -
        // the compositor holds each until its time, and the render thread goes straight on to the next frame. The grid
        // follows the real frames: a slot already past, or more than two periods ahead (the queue would grow), starts
        // it again from now.
        const uint64_t OUT_NS = 1000000000ull / (uint64_t)modes[mode_now()].out;
        period = OUT_NS;
        uint64_t t_i = now + 2000000ull;
        if (next_slot_ns > t_i && next_slot_ns - t_i < 2ull * OUT_NS) t_i = next_slot_ns;
        else if (next_slot_ns && next_slot_ns > t_i) st_drop++;  // (behind: the grid starts over, no image skipped)
        if (have_prev) {
            if (ts_pool) ts_write(vk_cmd(), tq + 1), ts_used[ts_i] = true;
            for (int i = 0; i < ctx_nout; i++) {  // (the generated frames in order, one slot each, then B)
                g_vk.next_present_ns = t_i + (uint64_t)i * OUT_NS;
                VkImageView v = hud ? vk_fg_hud_composite(vk_cmd(), fout[i].view, fin[k].view, hud, e) : fout[i].view;
                if (show(v)) st_gen++;
            }
            g_vk.next_present_ns = t_i + (uint64_t)ctx_nout * OUT_NS;
            next_slot_ns = t_i + (uint64_t)(ctx_nout + 1) * OUT_NS;
        } else {
            g_vk.next_present_ns = t_i;
            next_slot_ns = t_i + OUT_NS;
        }
    } else {
        period = base_ewma_ns ? base_ewma_ns / 2 : 16666667ull;
        if (period < 6000000ull) period = 6000000ull;
        if (period > 25000000ull) period = 25000000ull;
        bool gen = have_prev && now - last_shown_ns >= period / 2;
        if (have_prev && !gen) st_drop++;
        if (gen) {
            if (ts_pool) ts_write(vk_cmd(), tq + 1), ts_used[ts_i] = true;
            if (show(hud ? vk_fg_hud_composite(vk_cmd(), fout[0].view, fin[k].view, hud, e) : fout[0].view)) st_gen++;
            plat_sleep_until_ns(last_shown_ns + period);
        }
    }
    ts_i = (ts_i + 1) % 8;
    show(fin[k].view);
    st_real++;
    win_real++;
    have_prev = true;
    {   // the overlay's counts (each second) and the log's (every 2 s)
        uint64_t t = plat_now_ns();
        if (!win_t0) win_t0 = t;
        if (t - win_t0 >= 1000000000ull) {
            ov_real = (float)(win_real * 1e9 / (double)(t - win_t0)), ov_shown = (float)(win_shown * 1e9 / (double)(t - win_t0));
            win_real = win_shown = 0, win_t0 = t;
        }
        static int log = -1;
        if (log < 0) log = getenv("LARECOMP_FG_LOG") != NULL;
        if (!st_since) st_since = t;
        if (t - st_since >= 2000000000ull) {
            if (st_gpu_n) {
                double ms = st_gpu_ns / 1e6 / (double)st_gpu_n;
                fg_gpu_ewma_ms = fg_gpu_ewma_ms > 0 ? fg_gpu_ewma_ms * 0.5 + ms * 0.5 : ms;
            }
            if (log || diag_want_gpu_ts())
                LOG("[fg] 2 s: %llu real, %llu generated, %llu dropped; framegen GPU %.2f ms a frame; output period %.1f ms; "
                    "HUD mask on %llu real frames\n",
                    (unsigned long long)st_real, (unsigned long long)st_gen, (unsigned long long)st_drop,
                    st_gpu_n ? st_gpu_ns / 1e6 / (double)st_gpu_n : 0.0, period / 1e6, (unsigned long long)st_hud);
            {   // (the HUD mask's targets, every 10 s while it finds none: which images the layouts drew into)
                static int quiet;
                if (g_settings.fg_hud && !st_hud && quiet++ % 5 == 0) {
                    extern void vk_fg_hud_targets(char *out, size_t n);
                    char tg[400];
                    vk_fg_hud_targets(tg, sizeof tg);
                    LOG("[fg] HUD mask: no copy of the presented image (%p %ux%u fmt %d); the layouts drew into: %s\n", (void *)b,
                        b->width, b->height, (int)b->format, tg[0] ? tg : "none");
                }
            }
            st_real = st_gen = st_drop = st_gpu_ns = st_gpu_n = st_hud = 0, st_since = t;
        }
    }
    return true;
}

// (nvn/output_scale.c, Dynamic resolution) the framegen's GPU time per real frame while it runs (its timestamps,
// averaged over the last 2 s windows), else 0
double fg_gpu_ms_per_real(void) { return ctx_on && !fg_failed ? fg_gpu_ewma_ms : 0; }

// (ui/perf_overlay.mm) real and shown frames a second, while it runs; false: not running
bool fg_overlay(float *real, float *shown, int *mult) {
    if (!ctx_on || fg_failed) return false;
    *mult = ctx_nout + 1;
    *real = ov_real, *shown = ov_shown;
    return true;
}

// (ui/menu.mm) the state, for Video > Frame generation: 0 off, 1 on, 2 on next start, 3 unavailable (why)
int fg_status(char *why, size_t n) {
    snprintf(why, n, "%s", fg_why);
    if (fg_failed) return 3;
    if (!g_settings.framegen) return 0;
    if (fg_dev) {
        mode_now();
        snprintf(why, n, "%s", mode_note);  // (a multiplier above 2x the screen or the platform can't give)
        return 1;
    }
    return fg_why[0] && strcmp(fg_why, "no Lossless.dll shaders") ? 3 : 2;
}
#endif
