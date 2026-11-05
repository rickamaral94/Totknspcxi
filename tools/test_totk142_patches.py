"""Validate 1.4.2 sites against a supplied main and execute lifted patch C."""
import argparse, subprocess, tempfile
from pathlib import Path
import patches,lifter
from nso import Nso
p=argparse.ArgumentParser();p.add_argument('main',type=Path);p.add_argument('--cc',default='clang');a=p.parse_args()
n=Nso(a.main);assert n.build_id.startswith(patches.BUILD_TOTK_1_4_2),n.build_id
class G:
 base=0x7000000000
 nso=n
 lifter=lifter.Lifter(base,'c')
g=G(); funcs=[]
for addr,patch in patches.PATCHES_142.items():
 code=patch(g,addr); funcs.append(f'static void p_{addr:x}(Cpu *c) {{ {code} }}')
 # Guard must fail on an incorrect instruction, even at an otherwise valid address.
 i=addr-g.base;old=n.image[i:i+4];n.image[i]^=1
 try:
  patch(g,addr)
 except AssertionError: pass
 else: raise AssertionError(f'missing original-instruction guard at {addr:x}')
 n.image[i:i+4]=old
pre=r'''
#include "cpu.h"
#include <assert.h>
#include <stdbool.h>
#include <math.h>
#include <stdio.h>
static bool sixty;
bool rt_fps60(void) { return sixty; }
float rt_frame_ratio30(float nominal) { (void)nominal;return 0.5f; }
float rt_frame_interval60(float nominal) { (void)nominal;return 1.0f; }
uint32_t rt_interval_pick(uint32_t n) { return n; }
static float vf(Cpu *c,int r) { float f;memcpy(&f,&c->v[r],4);return f; }
'''
test=r'''
int main(void) {
 Cpu c={0};int32_t obj[256]={0};obj[0x250/4]=30;c.x[19]=(uint64_t)(uintptr_t)obj;
 p_70002d4730(&c);assert(vf(&c,0)==0.5f);
 p_70002d4748(&c);p_70002d474c(&c);p_70002d4754(&c);assert(vf(&c,0)==0.5f);
 float f=0.5f;memcpy(&c.v[1],&f,4);p_700037ea88(&c);assert(vf(&c,8)==0.5f);
 uint32_t ctl[64]={0};ctl[0]=2;ctl[0x44/4]=2;c.x[20]=(uint64_t)(uintptr_t)ctl;
 c.x[9]=1;c.x[10]=1;c.x[8]=2;p_70002d4ee0(&c);p_70002d4ee4(&c);assert(c.x[10]==2);
 sixty=true;c.x[9]=1;c.x[10]=1;c.x[8]=2;p_70002d4ee0(&c);p_70002d4ee4(&c);assert(c.x[10]==1);
 p_70002d501c(&c);assert(c.x[8]==1);
 sixty=false;p_70002d501c(&c);assert(c.x[8]==2);
 p_700047e5bc(&c);assert(c.v[1]==0);
 float time[16]={0};time[0x28/4]=60;time[0x38/4]=0.5f;
 c.x[8]=(uint64_t)(uintptr_t)time;p_70002e9ad4(&c);assert(vf(&c,0)==30);
 puts("TOTK 1.4.2: 15 guarded patches, wrong-word refusal, timing, 30/60 controller, cooldown and bounce C OK");
}
'''
with tempfile.TemporaryDirectory() as d:
 src=Path(d)/'test.c';exe=Path(d)/'test';src.write_text(pre+'\n'.join(funcs)+test)
 subprocess.run([a.cc,'-O2','-I',str(Path(__file__).resolve().parents[1]/'runtime'),str(src),'-lm','-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
