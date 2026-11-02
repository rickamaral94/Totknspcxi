#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "../runtime/nvn/totk_step.h"
static void near(float a,double b) { assert(fabs(a-b)<1e-7); }
int main(void) {
    for(unsigned window=1;window<=5;window+=4) {
        const unsigned rates[]={15,20,30,40,60,120,240};
        for(unsigned j=0;j<sizeof rates/sizeof rates[0];j++) {
            TotkStep s={0};uint64_t now=1000000000;float out=0;
            assert(!totk_step_update(&s,now,window,&out));
            for(unsigned i=0;i<600*rates[j];i++) {
                now+=1000000000ull/rates[j];
                assert(totk_step_update(&s,now,window,&out));
                near(out,1.0/rates[j]);
            }
            float old=out;now+=300000000;
            assert(!totk_step_update(&s,now,window,&out) && out==old);
            now+=100;assert(!totk_step_update(&s,now,window,&out));
            now+=100000000;assert(totk_step_update(&s,now,window,&out));near(out,1.0/15);
            now+=2000000;assert(totk_step_update(&s,now,1,&out));near(out,1.0/240);
            assert(!totk_step_update(&s,now-1,window,&out));
        }
    }
    TotkStep s={0};float out;
    totk_step_update(&s,1000000000,5,&out);
    totk_step_update(&s,1033333333,5,&out);
    totk_step_update(&s,1050000000,1,&out);near(out,1.0/60);
    // Clamp after averaging: do not inflate every individually short sample.
    s=(TotkStep){0};totk_step_update(&s,1000000000,5,&out);
    totk_step_update(&s,1002000000,5,&out);near(out,1.0/240);
    totk_step_update(&s,1012000000,5,&out);near(out,0.006);
    puts("TOTK clock: 10-minute runs 15..240 FPS, both windows, pauses, rollback, clamps and mode changes OK");
}
