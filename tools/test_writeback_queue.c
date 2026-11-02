#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../runtime/nvn/vulkan/writeback_queue.h"

int main(void) {
    GcBack q[8]; int n=0,hints[256]={0};
    uint8_t gpu[1024]={0},shadow[1024]={0};uint64_t seqs[4]={0};
    GcBack v={gpu,0x100000,256,10,shadow,seqs};
    assert(wb_queue_add(q,&n,8,hints,v)==1);
    assert(wb_queue_add(q,&n,8,hints,v)==0 && n==1);
    // Every component is part of the identity; a difference must survive.
    for(int field=0;field<6;field++) {
        GcBack x=v;
        switch(field) {case 0:x.src++;break;case 1:x.addr++;break;case 2:x.len++;break;
        case 3:x.seq++;break;case 4:x.shadow++;break;case 5:x.bseq++;break;}
        assert(wb_queue_add(q,&n,8,hints,x)==1);
    }
    // Simulate retirement/compaction without clearing hints: validate actual entries.
    q[0]=q[3];n=1;
    assert(wb_queue_add(q,&n,8,hints,v)==1);
    assert(wb_queue_add(q,&n,8,hints,v)==0);
    n=0;
    assert(wb_queue_add(q,&n,8,hints,v)==1);
    GcBack overlap=v;overlap.src++;overlap.addr+=128;
    assert(wb_queue_add(q,&n,8,hints,overlap)==1);
    assert(wb_queue_add(q,&n,8,hints,v)==1 && n==3); // preserve A,B,A ordering
    n=0;v.shadow=NULL;
    for(int i=0;i<8;i++)assert(wb_queue_add(q,&n,8,hints,v)==1);
    assert(wb_queue_add(q,&n,8,hints,v)==-1); // preserve legacy capacity behavior
    n=0;v.shadow=shadow;
    assert(wb_queue_add(q,&n,1,hints,v)==1);
    assert(wb_queue_add(q,&n,1,hints,v)==0); // duplicates need no extra capacity
    // Reuse queue slots for 10000 retire/refill cycles; hints must never alias entries.
    for(unsigned run=0;run<10000;run++) {
        n=0;
        for(unsigned j=0;j<8;j++) {v.seq=run;v.addr=0x100000+j*256;
            assert(wb_queue_add(q,&n,8,hints,v)==1);
            assert(wb_queue_add(q,&n,8,hints,v)==0);
        }
        assert(n==8);
    }
    puts("writeback queue: exact identity, fences, capacity, transient entries and 10000 compaction cycles OK");
}
