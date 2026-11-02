"""Compile the production range assembly with a small host harness.

Checks that learned ranges do not expand GPU writeback and real bindings retain
their slots under table pressure. No Vulkan device is needed.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root/'runtime/nvn/vulkan/exec.c').read_text()
start = source.index('    GcRange r[6 * 16];', source.index('static void gcopy_prepare('))
end = source.index('    uint64_t total = 0;', start)
body = source[start:end]
prefix = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#define HEAP_BASE 0x100000000ull
#define HEAP_MAX 0x320000000ull
#define GMAP_MAX 32
typedef struct { uint64_t lo,hi; bool store,learned; } GcRange;
static struct { struct { uint64_t addr,size; } sbuf[6][16]; } st;
static GcRange gl_learned[16];
static int gl_n;
static int assemble(unsigned stage_mask,bool store,GcRange *out) {
'''
suffix = r'''
    memcpy(out,r,(size_t)m*sizeof *out);
    return m;
}
int main(void) {
    GcRange out[96];
    for(unsigned seed=0;seed<1000;seed++) {
        memset(&st,0,sizeof st);
        uint64_t base=0x20000000ull+seed*0x10000ull;
        st.sbuf[0][0].addr=HEAP_BASE+base;
        st.sbuf[0][0].size=256;
        gl_n=2;
        gl_learned[0]=(GcRange){base+0x200,base+0x50000,false,true};
        gl_learned[1]=(GcRange){0x60000000,0x60050000,false,true};
        int n=assemble(1,true,out), stores=0;
        for(int i=0;i<n;i++) if(out[i].store) {
            stores++;
            assert(!out[i].learned);
            assert(out[i].lo==base-4096 && out[i].hi==base+256);
        }
        assert(stores==1); // a learned window never enlarges this writeback
        assert(n==3);
    }
    memset(&st,0,sizeof st);
    for(unsigned s=0;s<2;s++)for(unsigned i=0;i<16;i++) {
        st.sbuf[s][i].addr=HEAP_BASE+0x10000ull+(s*16+i)*0x10000ull;
        st.sbuf[s][i].size=256;
    }
    int n=assemble(3,true,out);
    assert(n==32);
    for(int i=0;i<n;i++) assert(out[i].store && !out[i].learned);
    puts("1000 range fixtures + full table: read-only learned windows never expand GPU writeback");
}
'''
with tempfile.TemporaryDirectory(prefix='totk-range-test-') as tmp:
    src=Path(tmp)/'test.c'
    binary=Path(tmp)/'test'
    src.write_text(prefix+body+suffix)
    subprocess.run(['cc','-O1','-g','-fsanitize=address,undefined',str(src),'-o',str(binary)],check=True)
    import os
    env=os.environ.copy()
    env['ASAN_OPTIONS']='detect_leaks=0'
    subprocess.run([str(binary)],env=env,check=True)
