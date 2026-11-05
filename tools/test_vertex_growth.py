"""Exercise production guest-copy cache with CPU-backed transient allocations."""
from pathlib import Path
import os, subprocess, tempfile
r=Path(__file__).resolve().parents[1]
s=(r/'runtime/nvn/vulkan/exec.c').read_text()
decl=s[s.index('#define GCOPY_TAB'):s.index('// Buffer copies made on the GPU')]
body=s[s.index('uint64_t prof_gcopy_bytes,'):s.index('// ================================================================== worker threads')]
pre=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef int VkBuffer;
typedef struct { VkBuffer buf; uint64_t off; void *ptr; } VkSpan;
static struct { struct { struct { uint64_t minUniformBufferOffsetAlignment; } limits; } props; } g_vk;
#define HEAP_BASE 0ull
#define HEAP_MAX 1ull
#define ALIAS_BASE 0ull
#define ALIAS_SIZE 0ull
#define LOG(...) ((void)0)
#define AB(x) 1
static uint64_t seq=1, stat_cb_copied,stat_guest_invalidated,stat_guest_reused,stat_guest_upload[3],stat_guest_misses[3],stat_guest_miss_bytes[3];
static int bcopy_npend;
static VkBuffer gmem_buf;
static bool bcopy_pending(uint64_t a,uint64_t b) { (void)a;(void)b;return false; }
static uint64_t vk_pending_seq(void) { return seq; }
static uint64_t renderer_present_count(void) { return 0; }
static void *allocs[64];static unsigned nalloc;
static VkSpan vk_transient(uint64_t size,uint64_t align) {
 (void)align;assert(nalloc<64);void *p=malloc(size);allocs[nalloc++]=p;
 memset(p,0xcc,size);return (VkSpan){1,0,p};
}
'''
test=r'''
int main(void) {
 uint8_t src[8192];for(unsigned i=0;i<sizeof src;i++) src[i]=(uint8_t)(i*13);
 uint64_t a=(uint64_t)(uintptr_t)src;
 VkSpan first=guest_copy(a,1000,GC_VTX);
 assert(nalloc==1 && !memcmp(first.ptr,src,1000));
 // Cold copies allocate exactly the reached bytes; reservation begins on growth.
 VkSpan second=guest_copy(a,1010,GC_VTX);
 assert(second.ptr!=first.ptr && nalloc==2 && !memcmp(second.ptr,src,1010));
 assert(((uint8_t*)second.ptr)[1010]==0xcc); // reserved tail was not read
 VkSpan appended=guest_copy(a,1020,GC_VTX);
 assert(appended.ptr==second.ptr && nalloc==2 && !memcmp(appended.ptr,src,1020));
 assert(stat_guest_grow_saved==1010 && stat_guest_grow_appends==1);
 VkSpan bigger=guest_copy(a,1600,GC_VTX);
 assert(bigger.ptr!=first.ptr && nalloc==3 && !memcmp(first.ptr,src,1000));
 VkSpan third=guest_copy(a,2000,GC_VTX);
 assert(third.ptr==bigger.ptr && nalloc==3 && !memcmp(third.ptr,src,2000));
 assert(guest_copy(a,800,GC_VTX).ptr==bigger.ptr);
 // A write in the initialized tail invalidates the whole cache entry; prior draws keep old bytes.
 uint8_t old=((uint8_t*)third.ptr)[1800];src[1800]^=0xff;
 guest_copy_invalidate(a+1800,a+1801);
 VkSpan fresh=guest_copy(a,2000,GC_VTX);
 assert(fresh.ptr!=third.ptr && !memcmp(fresh.ptr,src,2000));
 assert(((uint8_t*)third.ptr)[1800]==old && stat_guest_invalidated==1);
 assert(gcopy_nactive==1); // invalidated/refilled slot appears once
 seq++;assert(guest_copy(a,2000,GC_VTX).ptr!=fresh.ptr);
 guest_copy_flush();assert(guest_copy(a,2000,GC_VTX).ptr!=fresh.ptr);
 for(unsigned i=0;i<nalloc;i++) free(allocs[i]);
 puts("vertex growth: tail-only copies, immutable prefixes, grow/reallocate, invalidation, submission and flush OK");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d)/'test.c';e=Path(d)/'test';p.write_text(pre+decl+body+test)
 subprocess.run(['cc','-O2','-fsanitize=address,undefined',str(p),'-o',str(e)],check=True)
 env=dict(os.environ);env['ASAN_OPTIONS']='detect_leaks=0'
 subprocess.run([str(e)],check=True,env=env)
