"""Exercise the production ga_load_copy helper with host allocation/fence doubles."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root/'runtime/nvn/vulkan/exec.c').read_text()
start = source.index('static uint64_t ga_load_copy(')
end = source.index('// The table entries', start)
prefix = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
static uint8_t guest[1<<17], arena[1<<20];
#define HEAP_BASE ((uintptr_t)guest)
static uint8_t *ga_ptr=arena;
static uint64_t seq=1, gcopy_gen, top, flushes;
static uint64_t stat_ga_hits, stat_ga_misses, stat_ga_reused, stat_ga_copied, stat_ga_evicted;
static uint64_t vk_pending_seq(void) { return seq; }
static uint64_t ga_ring(uint64_t n) {
    if(n>sizeof arena-top)return ~0ull;
    uint64_t off=top;top+=n;return off;
}
static void ga_wrote(uint64_t off,uint64_t n) { assert(off+n<=top);flushes++; }
'''
tests = r'''
int main(void) {
    uint64_t saved[4];
    for(unsigned i=0;i<4;i++) {
        guest[i*16384]=(uint8_t)(i+1);
        saved[i]=ga_load_copy(i*16384,64);
    }
    // All four keys collide in the old direct-mapped cache. No extra payloads.
    for(unsigned j=0;j<1000;j++)for(unsigned i=0;i<4;i++)
        assert(ga_load_copy(i*16384,64)==saved[i]);
    assert(top==256 && flushes==4 && stat_ga_hits==4000);
    guest[63]=9;
    uint64_t fresh=ga_load_copy(0,64);
    assert(fresh!=saved[0] && arena[saved[0]+63]==0 && arena[fresh+63]==9);
    uint64_t before=top;
    seq++;
    assert(ga_load_copy(0,64)>=before);
    before=top;gcopy_gen++;
    fresh=ga_load_copy(0,64);assert(fresh>=before);
    // Failed allocation must not evict the valid immutable snapshot.
    before=top;top=sizeof arena;guest[20]=1;
    assert(ga_load_copy(0,64)==~0ull);
    guest[20]=0;assert(ga_load_copy(0,64)==fresh);
    top=before;
    uint32_t random=17;
    for(unsigned j=0;j<10000;j++) {
        random=random*1664525u+1013904223u;
        unsigned at=((random>>16)%7)*16384;
        unsigned n=16+((random>>12)&3)*16;
        if(j%3==0)guest[at+(random%n)]++;
        if(j%41==0)seq++;
        if(j%67==0)gcopy_gen++;
        uint64_t off=ga_load_copy(at,n);
        assert(off!=~0ull && !memcmp(arena+off,guest+at,n));
    }
    assert(stat_ga_evicted>0);
    puts("arena upload cache: collisions, immutable snapshots, fences, invalidation, allocation failure and 10000 mutations OK");
}
'''
with tempfile.TemporaryDirectory() as directory:
    c = Path(directory)/'test.c'
    executable = Path(directory)/'test'
    c.write_text(prefix+source[start:end]+tests)
    subprocess.run(['cc','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(c),'-o',str(executable)],check=True)
    subprocess.run([str(executable)],check=True,env={**__import__('os').environ,'ASAN_OPTIONS':'detect_leaks=0'})
