"""Execute production guest_copy/invalidation code with host buffer/fence doubles."""
from pathlib import Path
import os
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
s=(root/'runtime/nvn/vulkan/exec.c').read_text()
decl=s[s.index('#define GCOPY_TAB'):s.index('// Buffer copies made on the GPU')]
fn=s[s.index('static VkSpan guest_copy('):s.index('// ================================================================== worker threads')]
prefix=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
typedef struct { uint64_t buf,off; void *ptr; } VkSpan;
static uint8_t guest[1<<20], copies[2<<20];
#define HEAP_BASE ((uintptr_t)guest)
#define HEAP_MAX sizeof guest
#define ALIAS_BASE 0x100000000ull
#define ALIAS_SIZE 0x1000ull
#define LOG(...) ((void)0)
enum { GC_CONST, GC_UBO, GC_VTX };
static uint64_t seq=1, top, allocations;
static uint64_t stat_guest_invalidated, stat_guest_reused, stat_guest_upload[3];
static uint64_t stat_guest_misses[3],stat_guest_miss_bytes[3];
static uint64_t prof_gcopy_bytes,prof_gcopy_reused,prof_gcopy_kind[3],prof_gcopy_where[3],prof_gcopy_alias[16],stat_cb_copied;
static int bcopy_npend, reuse=1;
static uint64_t gmem_buf;
static struct { struct { struct { uint64_t minUniformBufferOffsetAlignment; } limits; } props; } g_vk;
static bool bcopy_pending(uint64_t a,uint64_t b) { (void)a;(void)b;return false; }
static int AB(const char *name) { (void)name;return reuse; }
static uint64_t vk_pending_seq(void) { return seq; }
static uint64_t renderer_present_count(void) { return 0; }
static VkSpan vk_transient(uint64_t n,uint64_t align) {
    top=(top+align-1)&~(align-1);assert(top+n<=sizeof copies);
    VkSpan out={1,top,copies+top};top+=n;allocations++;return out;
}
'''
tests=r'''
static unsigned bucket(uint64_t addr) {
    uint64_t h=addr*0x9E3779B97F4A7C15ull;
    return (unsigned)(h^(h>>32)) & (GCOPY_TAB-1);
}
int main(void) {
    uint64_t keys[5];unsigned n=0,want=bucket(HEAP_BASE);
    for(unsigned off=0;off+128<sizeof guest && n<5;off+=16)
        if(bucket(HEAP_BASE+off)==want)keys[n++]=HEAP_BASE+off;
    assert(n==5);
    VkSpan old[4];
    for(unsigned i=0;i<4;i++) { *(uint8_t *)(uintptr_t)keys[i]=i+1;old[i]=guest_copy(keys[i],32,GC_VTX); }
    for(unsigned j=0;j<1000;j++)for(unsigned i=0;i<4;i++)
        assert(guest_copy(keys[i],32,GC_VTX).ptr==old[i].ptr);
    assert(allocations==4);
    VkSpan grown=guest_copy(keys[0],64,GC_VTX);
    assert(grown.ptr!=old[0].ptr && guest_copy(keys[0],16,GC_VTX).ptr==grown.ptr);
    reuse=0;assert(guest_copy(keys[0],16,GC_VTX).ptr!=grown.ptr);reuse=1;
    // A guest write requires a fresh immutable snapshot, including overlap tails.
    *(uint8_t *)(uintptr_t)keys[1]=99;
    guest_copy_invalidate(keys[1]+1,keys[1]+32);
    VkSpan fresh=guest_copy(keys[1],32,GC_VTX);
    assert(fresh.ptr!=old[1].ptr && *(uint8_t *)fresh.ptr==99 && *(uint8_t *)old[1].ptr==2);
    // Boundary-touching writes do not overlap.
    guest_copy_invalidate(keys[1]+32,keys[1]+64);
    assert(guest_copy(keys[1],32,GC_VTX).ptr==fresh.ptr);
    guest_copy(keys[4],32,GC_VTX); // full bucket: eviction
    assert(stat_guest_misses[2]>0);
    unsigned count=(unsigned)gcopy_nactive;
    for(unsigned i=0;i<5000;i++) {
        guest_copy_invalidate(keys[1],keys[1]+32);
        guest_copy(keys[1],32,GC_VTX);
    }
    assert((unsigned)gcopy_nactive==count && gcopy_nactive<=GCOPY_TAB);
    // A later key must remain discoverable by invalidation after that churn.
    uint64_t late=HEAP_BASE+sizeof guest-128;
    VkSpan a=guest_copy(late,64,GC_UBO);
    guest_copy_invalidate(late,late+64);
    assert(guest_copy(late,64,GC_UBO).ptr!=a.ptr);
    a=guest_copy(keys[1],32,GC_CONST);seq++;
    assert(guest_copy(keys[1],32,GC_CONST).ptr!=a.ptr);
    a=guest_copy(keys[1],32,GC_VTX);guest_copy_flush();
    assert(guest_copy(keys[1],32,GC_VTX).ptr!=a.ptr);
    assert(gcopy_nactive==1);
    puts("guest cache: collisions, growth, AB policy, overlapping writes, immutable snapshots, fences, flush and 5000 invalidations OK");
}
'''
with tempfile.TemporaryDirectory() as d:
    c=Path(d)/'test.c';exe=Path(d)/'test'
    c.write_text(prefix+decl+fn+tests)
    subprocess.run(['cc','-O1','-g','-Wall','-Wextra','-fsanitize=address,undefined',str(c),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0'})
