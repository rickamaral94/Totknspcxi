#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../runtime/nvn/vulkan/writeback_merge.h"

static uint32_t seed = 17;
static uint32_t rnd(void) { seed = seed * 1664525u + 1013904223u; return seed; }
static void legacy(uint8_t *g, uint8_t *s, const uint8_t *p, uint64_t len, const uint64_t *q, uint64_t seq) {
    for (uint64_t b=0; b<len; b+=256) {
        uint64_t n=len-b<256?len-b:256;
        if(q[b/256]>seq)continue;
        if(!memcmp(p+b,s+b,n)||memcmp(g+b,s+b,n))continue;
        memcpy(g+b,p+b,n);memcpy(s+b,p+b,n);
    }
}
int main(void) {
    uint8_t g[9000],s[9000],p[9000],g2[9000],s2[9000];
    uint64_t q[36];
    for(int test=0;test<5000;test++) {
        size_t len=test<900?(size_t)test:(size_t)(rnd()%9001);
        for(size_t i=0;i<len;i++)g[i]=s[i]=p[i]=(uint8_t)rnd();
        for(size_t b=0;b<len;b+=256) {
            q[b/256]=(rnd()%4)?3:5;
            size_t at=b+rnd()%((len-b<256)?len-b:256);
            if(rnd()%3==0)p[at]^=1;
            if(rnd()%4==0)g[at]^=2;
        }
        memcpy(g2,g,len);memcpy(s2,s,len);
        legacy(g,s,p,len,q,3);gc_merge_writeback(g2,s2,p,len,q,3);
        assert(!memcmp(g,g2,len));assert(!memcmp(s,s2,len));
    }
    puts("5000 cases: identical to legacy writeback, including tails and newer CPU uploads");
}
