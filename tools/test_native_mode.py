#!/usr/bin/env python3
"""Exercise the native-mode policy from native.c, including setup of another version.
Usage: CC=clang python tools/test_native_mode.py
"""
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "runtime/native.c").read_text()
start = source.index("bool native_wanted_for_version(int version) {")
end = source.index("void native_begin_boot(void)", start)
harness = r'''
#include <assert.h>
#include <stdlib.h>
#include "settings.h"
#include "paths.h"
Settings g_settings;
static int current_version = GAME_V_143;
int rt_game_version(void) { return current_version; }
'''
harness += source[start:end]
harness += r'''
int main(void) {
    unsetenv("LARECOMP_NATIVE");
    g_settings.native_code = true;
    assert(native_wanted());
    // Importing an older version while the launcher is in 1.4.3 must still build its code.
    assert(!native_wanted_for_version(GAME_V_142));
    assert(!native_wanted_for_version(GAME_V_100));
    assert(!native_wanted_for_version(0));
    current_version = GAME_V_142;
    assert(!native_wanted());
    assert(native_wanted_for_version(GAME_V_143));
    g_settings.native_code = false;
    assert(!native_wanted_for_version(GAME_V_143));
    setenv("LARECOMP_NATIVE", "1", 1);
    assert(native_wanted());
    setenv("LARECOMP_NATIVE", "0", 1);
    g_settings.native_code = true;
    assert(!native_wanted_for_version(GAME_V_143));
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="totk-native-mode-") as d:
    src = Path(d) / "test.c"
    exe = Path(d) / "test"
    src.write_text(harness)
    subprocess.run([os.environ.get("CC", "cc"), "-D__ANDROID__", "-I", str(root / "runtime"), str(src), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print("native mode: all passed")
