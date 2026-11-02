"""Compile the production controller policy and settings codec expressions."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[1]
s=(r/'runtime/nvn/objects.c').read_text()
a=s.index('bool rt_fps60(void) {');b=s.index('// TOTK cutscenes:',a)
functions=s[a:b]
settings=(r/'runtime/settings.c').read_text()
line=next(l.strip() for l in settings.splitlines() if 'else if (!strcmp(key, "framegen_multiplier"))' in l)
decode=line.replace('else if', 'if')
encode='g_settings.fg_mult == 3 ? "2x60" : g_settings.fg_mult == 2 ? "4" : g_settings.fg_mult == 1 ? "3" : "2"'
assert encode in settings
assert 'if (fg_base > 30 && rt_cutscene()) fg_base = 30;' in s
assert 'frame_step_update(plat_now_ns());' in s
prefix=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#define __ANDROID__ 1
#define GAME_V_143 143
static int g_game_version=143,base;
static struct { int fps_mode,fg_mult; } g_settings;
int fg_base_fps(void) { return base; }
'''
tests=r'''
int main(void) {
    unsetenv("LARECOMP_FPS60");
    const char *key="framegen_multiplier";
    const char *names[]={"2","3","4","2x60"};
    for(int i=0;i<4;i++) {
        const char *val=names[i]; DECODE
        assert(g_settings.fg_mult==i);
        const char *encoded=ENCODE;
        assert(!strcmp(encoded,val));
    }
    base=0;g_settings.fps_mode=1;assert(!rt_fps60());
    g_settings.fps_mode=2;assert(rt_fps60());
    base=30;assert(!rt_fps60() && !rt_fg_native60());
    base=40;assert(!rt_fps60() && !rt_fg_native60());
    base=60;assert(rt_fps60() && rt_fg_native60());
    g_settings.fps_mode=1;assert(rt_fps60());
    g_game_version=182;assert(!rt_fps60() && !rt_fg_native60());
    puts("native FG policy: legacy settings round-trip, 30/40 behavior and TOTK-only native60 controller OK");
}
'''.replace('DECODE',decode).replace('ENCODE',encode)
with tempfile.TemporaryDirectory() as d:
    p=Path(d)/'test.c';e=Path(d)/'test';p.write_text(prefix+functions+tests)
    subprocess.run(['cc','-O2','-Wall','-Wextra',str(p),'-o',str(e)],check=True)
    subprocess.run([str(e)],check=True)
