#!/usr/bin/env python3
"""Host checks for tools/patches.py render_sites (amaral.27): the rendering reductions found by signature in the
recompiled game's main, unique matches only, and their C compiling against runtime/cpu.h. No game file: a synthetic
image with the 1.4.3 words around each site (the native mode's guards, runtime/native.c) and decoys."""
import struct, subprocess, sys, tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import patches

BL = 0x94000000


class FakeNso:
    def __init__(self, size, build_id='5cb42b1cf25469fb' + '0' * 48, text_off=0x100):
        self.image = bytearray(size)
        self.text = (text_off, size - text_off)
        self.build_id = build_id

    def put(self, off, *words):
        for i, w in enumerate(words):
            struct.pack_into('<I', self.image, self.text[0] + off + 4 * i, w)


def cascades(n, at):
    n.put(at - 0xe8, 0xb940fc0f)
    n.put(at - 8, 0x2a0f03f0, 0xaa0903f1, 0x52817606, 0x3800ce30)
    n.put(at + 0x18, 0x6b01009f, 0x1a9f8024)
    n.put(at + 0x12c, 0xb904da0f)


def points(n, at):
    n.put(at - 4, BL | 0x3a7, 0xb9400001, 0xaa1703e0, 0xaa1403e2, 0xaa1303e3, BL | 0xc6d)


def far(n, at):
    n.put(at, 0xbd413a60)
    n.put(at + 0x44, 0xbd414260)


logs = []
n = FakeNso(0x40000)
cascades(n, 0x10000), points(n, 0x20000), far(n, 0x30000)
n.put(0x11000, 0x52817606)          # decoys: the site's word alone
n.put(0x21000, 0xb9400001)
n.put(0x31000, 0xbd413a60)
found = patches.render_sites(n, logs.append)
assert found == {'shadow cascades': patches.MAIN_BASE + 0x100 + 0x10000, 'point shadows': patches.MAIN_BASE + 0x100 + 0x20000,
                 'far plane': patches.MAIN_BASE + 0x100 + 0x30000}, (found, logs)

n2 = FakeNso(0x40000)                # two full matches: ambiguous, left off; a broken one: none
far(n2, 0x1000), far(n2, 0x2000)
points(n2, 0x8000); n2.put(0x8000 + 8, 0xaa1403e3)
assert patches.render_sites(n2, logs.append) == {}, logs

n3 = FakeNso(0x40000)                # 1.4.2 build: patches_for adds them to its table
cascades(n3, 0x10000)
p = patches.patches_for(patches.BUILD_TOTK_1_4_2, n3)
a = patches.MAIN_BASE + 0x100 + 0x10000
assert p[a] is patches._patch_cascades and len(p) == len(patches.PATCHES_142) + 1
assert patches.patches_for(patches.BUILD_TOTK_1_4_2) is patches.PATCHES_142   # (without the NSO: as before)
n4 = FakeNso(0x40000, build_id=patches.BUILD_TOTK_1_4_3 + '0' * 48)   # 1.4.3: only on the native mode's address
cascades(n4, 0x10000)
assert a not in patches.patches_for(patches.BUILD_TOTK_1_4_3, n4)

class Gen:                           # the C of each, compiled against the runtime's cpu.h
    def __init__(self, nso): self.nso, self.base = nso, patches.MAIN_BASE
g = Gen(n)
body = '\n'.join('    ' + patches._RENDER_PATCH[k](g, found[k]) for k in found)
src = ('#include <string.h>\n#include <stdint.h>\n#include "cpu.h"\n'
       'void f(Cpu *c) {\n' + body + '\n}\n')
root = Path(__file__).resolve().parents[1]
with tempfile.NamedTemporaryFile('w', suffix='.c', delete=False) as f:
    f.write(src)
r = subprocess.run(['clang', '-fsyntax-only', '-Wall', '-Werror', '-Wno-unknown-pragmas', '-I', str(root / 'runtime'), f.name], capture_output=True, text=True)
assert r.returncode == 0, r.stderr + src
print('render signatures: unique matches patched, decoys / ambiguous / broken left off, 1.4.3 address check, C compiles')
