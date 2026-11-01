// Host tests of the dynamic resolution (runtime/nvn/output_scale.c, Video > Internal resolution > Dynamic) as Android
// builds it: the GPU's time over the whole decision window (its busy total), the frame rate it keeps, and that a scene
// the CPU holds doesn't lower the scale.
// clang -D__ANDROID__ -I runtime runtime/nvn/output_scale.c tools/dynres_tests/test_dynres.c -lm -o test_dynres && ./test_dynres
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "settings.h"
#include "nvn/output_scale.h"

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

// ---- what the runtime supplies
Settings g_settings;
static uint64_t gpu_total;
static bool cutscene, fps60 = true;
extern int g_present_vsyncs;
uint64_t diag_gpu_busy_total_ns(void) { return gpu_total; }
bool rt_fps60(void) { return fps60; }
bool rt_cutscene_now(void) { return cutscene; }
static int fg_base;
static double fg_ms;
int fg_base_fps(void) { return fg_base; }
double fg_gpu_ms_per_real(void) { return fg_ms; }
bool framepace_on(void) { return false; }
int framepace_target(void) { return 30; }

static double t;
// n frames of frame_ms each, the GPU busy gpu_ms of each - read back in bursts (every 4th frame gets 4 frames' worth)
static void frames(int n, double frame_ms, double gpu_ms) {
    OutScale o;
    for (int i = 0; i < n; i++) {
        t += frame_ms / 1000.0;
        if (i % 4 == 3) gpu_total += (uint64_t)(4 * gpu_ms * 1e6);
        output_scale(2400, 1080, false, t, -1, &o);
    }
}

int main(void) {
    g_settings.res_mode = RES_DYNAMIC;
    g_present_vsyncs = 2;
    g_settings.fps_mode = 1;  // 30 fps: 33.3 ms a frame
    frames(120, 33.3, 20);
    CHECK(output_scale_dynamic_factor() == 1.0);  // (room enough)

    // The CPU holds the frame (40 ms, the GPU 15 of them): a smaller image wouldn't help - the scale stays
    frames(600, 40, 15);
    CHECK(output_scale_dynamic_factor() == 1.0);

    // The GPU holds it (40 ms, 32 of GPU): the scale goes down
    frames(600, 40, 32);
    double low = output_scale_dynamic_factor();
    fprintf(stderr, "GPU-bound at 30 fps: scale %.2f\n", low);
    CHECK(low < 1.0 && low >= 0.5);

    // Plenty of room again (33 ms, 12 of GPU): it climbs back, in steps
    frames(3000, 33.3, 12);
    fprintf(stderr, "room again: scale %.2f\n", output_scale_dynamic_factor());
    CHECK(output_scale_dynamic_factor() > low);

    // Frame rate 60 (TOTK: the frame controller's interval 1): 16.7 ms a frame - 15 ms of GPU is too much
    g_settings.fps_mode = 2;
    g_present_vsyncs = 1;
    double before = output_scale_dynamic_factor();
    frames(600, 16.7, 15);
    fprintf(stderr, "60 fps, GPU 15 ms: scale %.2f -> %.2f\n", before, output_scale_dynamic_factor());
    CHECK(output_scale_dynamic_factor() < before);

    // A cutscene (held at 30 fps): 33 ms frames with 15 ms of GPU aren't slow then - the scale doesn't drop for it
    cutscene = true;
    g_present_vsyncs = 2;
    double at = output_scale_dynamic_factor();
    frames(600, 33.3, 15);
    CHECK(output_scale_dynamic_factor() >= at);
    cutscene = false;
    g_present_vsyncs = 1;

    // No GPU timestamps read back at all for a while (the total doesn't move) on a device that has them: no decision
    // (first a settled stretch at 30 fps, so no window still holds the cutscene's frames)
    g_settings.fps_mode = 1;
    g_present_vsyncs = 2;
    frames(300, 33.3, 25);
    double still = output_scale_dynamic_factor();
    {
        OutScale o;
        for (int i = 0; i < 300; i++) t += 0.040, output_scale(2400, 1080, false, t, -1, &o);
    }
    CHECK(output_scale_dynamic_factor() == still);

    // Frame generation 2x (the game held at 30) costing 8 ms of GPU a real frame: 22 ms of the game's own is too much
    // (33.3 - 8 = 25.3 ms left, 87% of it 22), though alone it would fit a 30 fps frame
    g_settings.fps_mode = 2;
    fg_base = 30, fg_ms = 8;
    frames(300, 33.3, 12);  // (settled first: plenty of room)
    double fgb = output_scale_dynamic_factor();
    frames(600, 33.3, 23);
    fprintf(stderr, "framegen 2x, 8 ms of its own, game 23 ms: scale %.2f -> %.2f\n", fgb, output_scale_dynamic_factor());
    CHECK(output_scale_dynamic_factor() < fgb);
    fg_base = 0, fg_ms = 0;

    // Auto can now choose 60 itself: follow the actual NVN interval, not only the menu's fixed-60 setting.
    g_settings.fps_mode = 0;
    g_present_vsyncs = 2;
    frames(3000, 33.3, 8);
    double auto_before = output_scale_dynamic_factor();
    g_present_vsyncs = 1;
    frames(600, 16.7, 15);
    CHECK(output_scale_dynamic_factor() < auto_before);
    double auto_low = output_scale_dynamic_factor();
    g_present_vsyncs = 2;  // Auto returned to 30 / a cutscene: the same GPU cost fits again.
    frames(600, 33.3, 15);
    CHECK(output_scale_dynamic_factor() >= auto_low);

    if (fails) { fprintf(stderr, "%d failed\n", fails); return 1; }
    fprintf(stderr, "all passed\n");
    return 0;
}
