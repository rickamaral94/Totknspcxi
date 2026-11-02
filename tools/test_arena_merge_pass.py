"""Regression: GPU-resident bytes must survive a range growing in an active pass."""
from pathlib import Path
import os, subprocess, tempfile
r=Path(__file__).resolve().parents[1]
s=(r/'runtime/nvn/vulkan/exec.c').read_text()
a=s.index('typedef struct {',s.index('#define GA_NRES'))
types=s[a:s.index('} GaRes;',a)+len('} GaRes;')]
a=s.index('static GaRes *gres_get(')
fn=s[a:s.index('// This draw\'s / dispatch\'s ranges',a)]
draw=s[s.index('static void draw_(CmdRec *r, bool indexed) {'):]
assert draw.index('begin_pass(); // allow') < draw.index('gcopy_prepare(') < draw.index('VkCommandBuffer cb = vk_cmd();')
assert draw.index('if (!in_pass) begin_pass();') < draw.index('VkCommandBuffer cb = vk_cmd();')
assert 'if (!pass_defer_submit && ch > 0' in s
prefix=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#define GA_NRES 512
#define GA_RES (1<<20)
#define LOG(...) ((void)0)
typedef unsigned VkCommandBuffer;
typedef struct { uint64_t srcOffset,dstOffset,size; } VkBufferCopy;
static uint8_t guest[4096], gpu[GA_RES];
#define HEAP_BASE ((uintptr_t)guest)
'''
support=r'''
static GaRes ga_res[GA_NRES];
static int ga_nres,ends,copies;
static bool in_pass;
static uint64_t ga_res_top, stat_gres_pass_merge;
static uint8_t *ga_ptr=gpu;
static unsigned ga_buf=1;
static void vk_end_pass(void) { assert(in_pass);in_pass=false;ends++; }
static void gres_reclaim(void) {}
static uint64_t ga_res_alloc(uint64_t n) { uint64_t at=ga_res_top;ga_res_top+=n;assert(ga_res_top<=GA_RES);return at; }
static uint64_t renderer_present_count(void) { return 5; }
static uint64_t vk_pending_seq(void) { return 17; }
static void ga_wrote(uint64_t off,uint64_t n) { assert(off+n<=GA_RES); }
static VkCommandBuffer vk_cmd(void) { return 1; }
static void vk_full_barrier(VkCommandBuffer cb) { assert(cb && !in_pass); }
static void gres_sync(GaRes *e) { assert(e->live && !in_pass); }
static void vkCmdCopyBuffer(VkCommandBuffer cb,unsigned src,unsigned dst,unsigned n,const VkBufferCopy *cp) {
    assert(cb && src==dst && n==1 && !in_pass);
    memcpy(gpu+cp->dstOffset,gpu+cp->srcOffset,cp->size);copies++;
}
'''
test=r'''
int main(void) {
    for(int run=0;run<100;run++) {
        memset(guest,17,sizeof guest);ga_res_top=0;ga_nres=0;in_pass=false;ends=copies=0;
        GaRes *a=gres_get(256,512,true);a->stored=true;
        memset(gpu+a->off,0xAB,256); // GPU result absent from guest RAM
        in_pass=true;
        GaRes *b=gres_get(0,768,true);
        assert(b && !in_pass && ends==1 && copies==1 && !a->live && b->stored);
        assert(gpu[b->off+256]==0xAB && guest[256]==17 && b->shadow[256]==17);
        in_pass=true;
        assert(gres_get(256,512,true)==b && in_pass && ends==1); // covered range: no split
        GaRes *c=gres_get(1024,1280,true);assert(c && in_pass && ends==1);
        memset(gpu+c->off,0xCD,256);
        GaRes *d=gres_get(512,1152,true);
        assert(d && !in_pass && ends==2 && gpu[d->off+256]==0xAB && gpu[d->off+1024]==0xCD);
        for(int i=0;i<ga_nres;i++) { free(ga_res[i].shadow);free(ga_res[i].bseq); }
        memset(ga_res,0,sizeof ga_res);
    }
    puts("arena merge: 100 GPU-result retention cases, pass transfer legality, covered ranges and draw ordering OK");
}
'''
with tempfile.TemporaryDirectory() as d:
    c=Path(d)/'test.c';e=Path(d)/'test';c.write_text(prefix+types+support+fn+test)
    subprocess.run(['cc','-O1','-g','-Wall','-Wextra','-fsanitize=address,undefined',str(c),'-o',str(e)],check=True)
    subprocess.run([str(e)],check=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0'})
