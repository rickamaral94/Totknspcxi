#!/usr/bin/env python3
"""Compare production driver constants byte-for-byte against the original builder.
Usage: python tools/test_driver_constants.py <NDK-bin> <adb-serial>
"""
from pathlib import Path
import subprocess
import sys
root = Path(__file__).resolve().parents[1]
out = root / 'build/driver-constant-tests'
out.mkdir(parents=True, exist_ok=True)
source = (root / 'runtime/nvn/vulkan/exec.c').read_text()
start = source.index('    if (k == 0) {\n', source.index('static VkDescriptorBufferInfo cbuf_info('))
body = source[start:source.index('    if (k == 1) {', start)]
prefix = r'''#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
typedef struct { uint64_t addr,size; } Range;
static struct { Range ubuf[6][18], sbuf[6][16]; } st;
typedef struct { uint32_t cbuf_size[18]; } ShaderInfo;
typedef struct { uint8_t bytes[2048]; uint64_t size; } VkDescriptorBufferInfo;
static int ab_mode;
#define AB(name) ab_mode
enum { DEDUPE_DRV_C, DEDUPE_DRV_F, DEDUPE_DRV_V };
static const uint8_t zeros[65536];
static VkDescriptorBufferInfo copy_dedupe(int slot, const void *data,uint64_t size,int align) {
 (void)slot; (void)align; VkDescriptorBufferInfo r={0}; r.size=size; memcpy(r.bytes,data,size); return r;
}
#define ubo_span(span,size) span
'''
reference = r'''static VkDescriptorBufferInfo reference(int stage, ShaderInfo *si, int k, uint32_t gx, uint32_t gy, uint32_t gz) {
    if (k == 0) {
        uint32_t drv[0x800 / 4] = {0};
        if (stage == 5) {
            drv[0x200 / 4] = gx; drv[0x204 / 4] = gy; drv[0x208 / 4] = gz;
            for (int i = 0; i < 16; i++) {
                uint32_t *u = &drv[(0x210 + 16 * i) / 4];
                u[0] = (uint32_t)st.ubuf[5][i].addr; u[1] = (uint32_t)(st.ubuf[5][i].addr >> 32); u[2] = (uint32_t)st.ubuf[5][i].size;
                uint32_t *e = &drv[(0x310 + 16 * i) / 4];
                e[0] = (uint32_t)st.sbuf[5][i].addr; e[1] = (uint32_t)(st.sbuf[5][i].addr >> 32); e[2] = (uint32_t)st.sbuf[5][i].size;
            }
        } else {
            uint32_t base = 0x110 + 0x100 * (stage == 1 ? 4 : 0);
            for (int i = 0; i < 16; i++) {
                uint32_t *e = &drv[(base + 16 * i) / 4];
                e[0] = (uint32_t)st.sbuf[stage][i].addr;
                e[1] = (uint32_t)(st.sbuf[stage][i].addr >> 32);
                e[2] = (uint32_t)st.sbuf[stage][i].size;
            }
        }
        uint64_t size = si->cbuf_size[0] > sizeof drv ? sizeof drv : (si->cbuf_size[0] ? si->cbuf_size[0] : 256);
        if (size < 0x500 && stage == 5) size = 0x500;
        return ubo_span(copy_dedupe(stage == 5 ? DEDUPE_DRV_C : stage == 1 ? DEDUPE_DRV_F : DEDUPE_DRV_V, drv, size, 256), size);
    }
return (VkDescriptorBufferInfo){0};}
'''
tests = r'''int main(void) {
 ShaderInfo si={0}; unsigned count=0;
 for(int stage=0;stage<=5;stage++) for(unsigned size=0;size<=2049;size++) {
  si.cbuf_size[0]=size;
  for(int i=0;i<16;i++) { st.sbuf[stage][i]=(Range){0x123456780000ull+i*1024+size,~(uint64_t)size}; st.ubuf[stage][i]=(Range){0xabcd98760000ull+i*2048,32+i}; }
  VkDescriptorBufferInfo ref=reference(stage,&si,0,7,13,19);
  for(ab_mode=0;ab_mode<=1;ab_mode++) {
   VkDescriptorBufferInfo got=candidate(stage,&si,0,7,13,19);
   if(got.size!=ref.size || memcmp(got.bytes,ref.bytes,ref.size)) return 1;
   count++;
  }
 }
 return count==24600 ? 0:2;
}
'''
candidate = 'static VkDescriptorBufferInfo candidate(int stage, ShaderInfo *si, int k, uint32_t gx, uint32_t gy, uint32_t gz) {\n' + body + 'return (VkDescriptorBufferInfo){0};}\n'
(out / 'test.c').write_text(prefix + reference + candidate + tests)
subprocess.run([str(Path(sys.argv[1]) / 'clang.exe'), '--target=aarch64-linux-android30', '-O2', str(out / 'test.c'), '-o', str(out / 'test')], check=True)
adb = ['adb', '-s', sys.argv[2]]
remote = '/data/local/tmp/totkrecompandroid-tests/driver_constants_test'
subprocess.run(adb + ['shell', 'mkdir', '-p', '/data/local/tmp/totkrecompandroid-tests'], check=True)
subprocess.run(adb + ['push', str(out / 'test'), remote], check=True)
subprocess.run(adb + ['shell', 'chmod', '755', remote], check=True)
subprocess.run(adb + ['shell', remote], check=True)
print('PASS: 24600 byte-exact driver constant comparisons')
