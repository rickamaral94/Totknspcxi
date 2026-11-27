#include <assert.h>
#include <stdio.h>
#include "../runtime/nvn/vulkan/fg_pacing.h"
int main(void) {
    const uint64_t lead=2000000, base=1000000000;
    for(unsigned hz=60;hz<=120;hz+=60) {
        uint64_t p=1000000000ull/hz;
        FgSlot a=fg_slot(0,base,p);assert(a.reset && a.target==base+lead);
        uint64_t next=a.target+2*p;
        a=fg_slot(next,next-lead-1000000,p);assert(a.target==next && !a.reset);
        a=fg_slot(next,next-lead+1,p);assert(a.target==next+p && a.skipped==1 && !a.reset);
        a=fg_slot(next,next-lead+p+1,p);assert(a.target==next+2*p && a.skipped==2);
        a=fg_slot(next, next+300000000,p);assert(a.reset);
        a=fg_slot(next+10*p,next-lead,p);assert(a.reset);
        // Jitter over 10 min, preserving phase and staying inside the live bound.
        uint64_t phase=base+lead;next=phase;
        for(unsigned i=0;i<600*hz/2;i++) {
            uint64_t now=base+i*2*p+(i%5)*1000000;
            a=fg_slot(next,now,p);
            assert(!a.reset && a.target>=now+lead && a.target<now+lead+2*p);
            assert((a.target-phase)%p==0);
            next=a.target+2*p;
        }
    }
    puts("FG pacing: on-time, missed slots, bounded backlog, pause and 10-minute jitter OK");
}
