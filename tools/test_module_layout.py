#!/usr/bin/env python3
"""Reproduce the 1.0.0 main/SDK overlap using the loader's real copy/validation path."""
from pathlib import Path
import os
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
rt = (root / 'runtime/rt.h').read_text()
offset = int(re.search(r'#define SDK_BASE\s+\(GUEST_LO \+ (0x[0-9a-fA-F]+)ull\)', rt)[1], 16)
canonical = 0x7000000000 + offset
for path in ('runtime/gamebuild.c', 'android/build_gamelib.sh', 'tools/sym.py'):
    assert hex(canonical) in (root / path).read_text(), path
assert f"0x{offset:x}" in (root / 'tools/hot_functions.py').read_text()
source = (root / 'runtime/loader.c').read_text()
copy_path = source[source.index('static void load_nso('):source.index('    m->base = base;')]+ '}\n'
harness = r'''
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <setjmp.h>
typedef struct { int unused; } Module;
static uint64_t guest_lo;
#define MAIN_BASE guest_lo
#define SDK_BASE (guest_lo + SDK_OFFSET)
#define HLE_BASE (guest_lo + 0x80000000ull)
static int g_game_version;
static jmp_buf failure;
#define LOG(...) ((void)0)
#define FATAL(...) longjmp(failure, 1)
static uint8_t *fixture;
static size_t fixture_size;
static int commits;
static uint8_t *read_file(const char *path, size_t *len) {
    (void)path; *len = fixture_size;
    uint8_t *p = malloc(*len); memcpy(p, fixture, *len); return p;
}
static int game_version_of_build(const uint8_t *id) { (void)id; return 100; }
static void plat_commit(uint64_t base, uint64_t size) { commits++; memset((void *)base, 0, size); }
static size_t lz4_decompress(const uint8_t *s, size_t n, uint8_t *d, size_t z) {
    (void)s; (void)n; (void)d; (void)z; abort();
}
static void word(size_t off, uint32_t value) { memcpy(fixture+off, &value, 4); }
static void nso(uint32_t data_off, uint32_t data_size, uint32_t text_size) {
    free(fixture); fixture_size=0x100+text_size+data_size;
    fixture=calloc(1,fixture_size); memcpy(fixture,"NSO0",4);
    word(0x10,0x100); word(0x18,text_size);
    word(0x20,0x100); // empty rodata
    word(0x30,0x100+text_size); word(0x34,data_off); word(0x38,data_size);
}
'''
harness = f'#define SDK_OFFSET {offset}ull\n'+harness+copy_path+r'''
int main(void) {
    guest_lo=(uint64_t)calloc(1,SDK_OFFSET+0x1000000); assert(guest_lo);
    Module m;
    // Actual 1.0.0 main's end, and the PLT slot reported in the user's crash.
    nso(0x4000000,0x668000,4);
    uint64_t expected=0x1041302a0ull;
    memcpy(fixture+0x104+0x53f3b0,&expected,8);
    load_nso(&m,"main",MAIN_BASE);
    nso(0x600000,0,0x600000);
    memset(fixture+0x100,0xeb,0x600000);
    load_nso(&m,"sdk",SDK_BASE);
    assert(*(uint64_t *)(guest_lo+0x453f3b0)==expected);
    // Reject a future oversized main before commit/copy can touch SDK.
    nso(SDK_OFFSET,4,4);
    int before=commits;
    if (!setjmp(failure)) { load_nso(&m,"oversized",MAIN_BASE); abort(); }
    assert(commits==before);
    assert(*(uint64_t *)SDK_BASE==0xebebebebebebebebull);
    // Size arithmetic must not wrap at 4 GiB and bypass the overlap guard.
    nso(0xffff0000,0,4); word(0x3c,0x20000);
    if (!setjmp(failure)) { load_nso(&m,"overflow",MAIN_BASE); abort(); }
    assert(commits==before);
    free(fixture); free((void *)guest_lo);
    puts("module layout: 1.0.0 GOT preserved; overlap rejected before writes");
}
'''
with tempfile.TemporaryDirectory() as d:
    c=Path(d)/'test.c'; exe=Path(d)/'test';c.write_text(harness)
    subprocess.run([os.environ.get('CC','cc'),str(c),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
