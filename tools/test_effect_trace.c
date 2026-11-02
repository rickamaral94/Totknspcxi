#include <assert.h>
#include <stdio.h>
#include "../runtime/nvn/vulkan/effect_trace.h"
int main(void) {
    assert(!effect_trace_candidate(0xfe,1,1280,720,false,false));
    assert(!effect_trace_candidate(0xff,1,64,64,false,false));
    assert(!effect_trace_candidate(0xff,0,1280,720,true,false));
    assert(effect_trace_candidate(1,1,640,360,true,false));
    assert(effect_trace_candidate(0,1,1280,720,true,false));
    assert(!effect_trace_candidate(0,1,1280,720,true,true));
    EffectTrace s={0};
    assert(!effect_trace_frame(&s,0,false));
    for(unsigned burst=0;burst<3;burst++) {
        unsigned at=100+burst*100;
        for(unsigned j=0;j<3;j++) {
            assert(effect_trace_frame(&s,at+j,j==0));
            for(unsigned k=0;k<64;k++) assert(effect_trace_keep(&s,k));
            assert(!effect_trace_keep(&s,12));
            assert(!effect_trace_keep(&s,65));
            assert(s.full);
            assert(effect_trace_frame(&s,at+j,false));
            assert(s.rows==64); // repeated draws in one frame cannot reset the budget
        }
        assert(!effect_trace_frame(&s,at+3,false));
    }
    assert(!effect_trace_frame(&s,500,true)); // finite session budget
    puts("effect capture: adjacent frames, deduplication and row/session limits OK");
}
