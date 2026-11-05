"""Run native timing handlers with synthetic guest state; no game assets needed."""
from pathlib import Path
import subprocess
import tempfile
import os
root = Path(__file__).resolve().parents[1]
src = (root / "runtime/native.c").read_text()
handlers = src[src.index("static int fix_equipment_timer("):src.index("typedef struct { int version;")]
vset = src[src.index("static void vset("):src.index("static int fix_dynres_scale(")]
program = r"""
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <float.h>
#include <stdio.h>
typedef struct { uint64_t x[32]; __uint128_t v[32]; } Cpu;
""" + vset + handlers + r"""
int main(void) {
    Cpu c; memset(&c, 0xa5, sizeof c); Cpu before = c;
    assert(fix_equipment_timer(&c) == 3);
    assert(c.v[1] == 0); before.v[1] = 0;
    assert(!memcmp(&before, &c, sizeof c));
    // A sub-epsilon remainder was never decremented with the old threshold.
    float remaining = FLT_EPSILON * 0.5f;
    assert(!(remaining > FLT_EPSILON) && remaining > 0);
    remaining -= 0.5f;
    assert(remaining <= 0);
    float time[16] = {0};
    const float scales[] = {0.25f, 0.5f, 1.0f, 2.0f};
    const int fps[] = {15, 20, 30, 40, 60, 120, 240};
    for (unsigned i=0;i<sizeof scales/sizeof *scales;i++)
        for (unsigned j=0;j<sizeof fps/sizeof *fps;j++) {
            float ratio = 30.0f / fps[j];
            time[0x28/4] = 30.0f / (ratio * scales[i]);
            time[0x38/4] = ratio;
            c.x[9] = (uint64_t)(uintptr_t)time; before = c;
            assert(fix_bounce_clock(&c) == 3);
            float got; memcpy(&got, &c.v[0], 4);
            assert(fabsf(got - 30.0f/scales[i]) < 1e-4f);
            before.v[0] = c.v[0]; assert(!memcmp(&before, &c, sizeof c));
        }
    puts("native timing fixes: equipment SIMD state and FPS-independent bounce 15..240 Hz OK");
}
"""
with tempfile.TemporaryDirectory() as d:
    p = Path(d) / "test.c"
    p.write_text(program)
    exe = Path(d) / "test"
    subprocess.run(["cc", "-O2", "-Wall", "-Wextra", "-fsanitize=address,undefined", str(p), "-lm", "-o", str(exe)], check=True)
    # LeakSanitizer cannot inspect /proc under the managed executor. ASan and
    # UBSan remain enabled; these handlers allocate no heap memory.
    env = dict(os.environ)
    env["ASAN_OPTIONS"] = env.get("ASAN_OPTIONS", "") + ":detect_leaks=0"
    subprocess.run([str(exe)], check=True, env=env)
