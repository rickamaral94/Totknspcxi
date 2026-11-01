#!/usr/bin/env python3
"""Exercise production linear upload row copies with padded height-map storage.

Usage: python tools/test_linear_rt_upload.py <NDK-bin> <adb-serial>
"""
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
out = root / 'build/linear-rt-tests'
out.mkdir(parents=True, exist_ok=True)
textures = (root / 'runtime/nvn/vulkan/textures.c').read_text()
objects = (root / 'runtime/nvn/objects.c').read_text()
start = objects.index('uint64_t nvn_linear_rt_pitch(')
pitch = objects[start:objects.index('\n}', start) + 2]
macro = next(line for line in textures.splitlines() if line.startswith('#define LIN_STRIDE('))
start = textures.index('            if (linear) {', textures.index('uint32_t out_row ='))
copy = textures[start:textures.index('            } else {', start)] + '\n}\n'
source = r'''#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
static uint64_t alignment;
static uint64_t device_integer(int property) { assert(property == 57); return alignment; }
typedef struct { unsigned flags; uint64_t stride; } Texture;
typedef struct { unsigned hb; } Level;
'''
source += pitch + '\n' + macro + '\n'
source += 'static void upload(Texture *t, Level l, unsigned row, const uint8_t *src, uint8_t *lin) { int linear = 1;\n'
source += copy + '}\n'
source += r'''int main(void) {
    const unsigned widths[] = {1, 63, 64, 127, 128, 256, 260, 512};
    const unsigned alignments[] = {0, 32, 128, 256};
    unsigned cases = 0;
    for (unsigned a = 0; a < 4; a++) for (unsigned w = 0; w < 8; w++)
    for (unsigned explicit_stride = 0; explicit_stride < 2; explicit_stride++) {
        alignment = alignments[a];
        unsigned row = widths[w] * 2, height = 260;
        unsigned stride = explicit_stride ? row + 176 : alignment ?
            ((row + alignment - 1) / alignment) * alignment : row;
        uint8_t *src = malloc((size_t)stride * height), *dst = malloc((size_t)row * height);
        memset(src, 0xcd, (size_t)stride * height);
        memset(dst, 0xee, (size_t)row * height);
        for (unsigned y = 0; y < height; y++) for (unsigned x = 0; x < widths[w]; x++) {
            uint16_t value = (uint16_t)(11000 + y * 31 + x);
            memcpy(src + (size_t)y * stride + x * 2, &value, 2);
        }
        // Linear render targets must ignore the builder stride; ordinary linear textures must honor it.
        Texture t = {explicit_stride ? 0x10 : 0x40, explicit_stride ? stride : 123456};
        upload(&t, (Level){height}, row, src, dst);
        for (unsigned y = 0; y < height; y++) for (unsigned x = 0; x < widths[w]; x++) {
            uint16_t value; memcpy(&value, dst + (size_t)y * row + x * 2, 2);
            assert(value == (uint16_t)(11000 + y * 31 + x));
        }
        free(src); free(dst); cases++;
    }
    printf("PASS: %u padded height-map uploads, packed and explicit-stride paths\n", cases);
    return 0;
}
'''
(out / 'test.c').write_text(source)
subprocess.run([str(Path(sys.argv[1]) / 'clang.exe'), '--target=aarch64-linux-android30',
                '-O2', str(out / 'test.c'), '-o', str(out / 'test')], check=True)
adb = ['adb', '-s', sys.argv[2]]
remote = '/data/local/tmp/totkrecompandroid-tests/linear_rt_upload'
subprocess.run(adb + ['shell', 'mkdir', '-p', '/data/local/tmp/totkrecompandroid-tests'], check=True)
subprocess.run(adb + ['push', str(out / 'test'), remote], check=True)
subprocess.run(adb + ['shell', 'chmod', '755', remote], check=True)
subprocess.run(adb + ['shell', remote], check=True)
