/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#include "big.h"
#include <stdio.h>
#include <float.h>
#include <fenv.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"CHECK %s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while(0)
static uint64_t random_word(uint64_t *state)
{
    *state=*state*UINT64_C(6364136223846793005)+UINT64_C(1442695040888963407); return *state;
}
static Eterm encode(__int128 value,Eterm *p)
{
    __uint128_t magnitude=value<0?-(__uint128_t)value:(__uint128_t)value;
    size_t words=(magnitude>>64)?2:1;
    p[0]=_make_header(words,value<0?NEG_BIG_SUBTAG:POS_BIG_SUBTAG);
    p[1]=(Uint)magnitude; if(words==2) p[2]=(Uint)(magnitude>>64);
    return make_boxed(p);
}
static __int128 decode(Eterm value)
{
    Eterm *p; __uint128_t magnitude;
    if(is_small(value)) return signed_val(value);
    CHECK(lb_is_big(value) && lb_big_size(value)<=2); p=boxed_val(value); magnitude=p[1];
    if(lb_big_size(value)==2) magnitude|=(__uint128_t)p[2]<<64;
    CHECK(magnitude<((__uint128_t)1<<127));
    return (p[0]&_TAG_HEADER_MASK)==NEG_BIG_SUBTAG?-(__int128)magnitude:(__int128)magnitude;
}
static void check_sum(__int128 a,__int128 b)
{
    Eterm x[3]={0},y[3]={0},saved_x[3],saved_y[3],out[6],v,result;
    Eterm left=encode(a,x),right=encode(b,y);
    memcpy(saved_x,x,sizeof(x)); memcpy(saved_y,y,sizeof(y));
    memset(out,0xa5,sizeof(out)); result=lb_big_plus(left,right,out+1);
    CHECK(result!=NIL && decode(result)==a+b);
    CHECK(out[0]==UINT64_C(0xa5a5a5a5a5a5a5a5) && out[5]==UINT64_C(0xa5a5a5a5a5a5a5a5));
    CHECK(!memcmp(x,saved_x,sizeof(x)) && !memcmp(y,saved_y,sizeof(y)));
    v=lb_big_plus(right,left,out+1); CHECK(decode(v)==a+b);
    if(a+b>=MIN_SMALL && a+b<=MAX_SMALL) CHECK(is_small(v)); else CHECK(lb_is_big(v));
}
static void boundaries(void)
{
    const Sint values[]={0,1,-1,MIN_SMALL,MAX_SMALL,MIN_SMALL-1,MAX_SMALL+1,INT64_MIN,INT64_MAX};
    size_t i,j; Eterm out[2];
    for(i=0;i<sizeof(values)/sizeof(values[0]);++i) {
        CHECK(decode(lb_small_to_big(values[i],out))==values[i]);
        for(j=0;j<sizeof(values)/sizeof(values[0]);++j) check_sum(values[i],values[j]);
    }
}
static void carry_chains(size_t digits)
{
    Eterm *a=malloc((digits+1)*sizeof(Eterm)),*b=malloc((digits+1)*sizeof(Eterm));
    Eterm *r=malloc((digits+2)*sizeof(Eterm)); Eterm one[2],minus_one[2],result; size_t i;
    CHECK(a && b && r && digits>1);
    a[0]=_make_header(digits,POS_BIG_SUBTAG);
    for(i=1;i<=digits;++i) a[i]=UINTPTR_MAX;
    result=lb_big_plus(make_boxed(a),lb_small_to_big(1,one),r);
    if(digits==LB_BIG_ARITY_MAX) CHECK(result==NIL);
    else {
        CHECK(lb_is_big(result) && lb_big_size(result)==digits+1 && r[digits+1]==1);
        for(i=1;i<=digits;++i) CHECK(!r[i]);
    }
    memcpy(b,a,(digits+1)*sizeof(Eterm)); b[0]=_make_header(digits,NEG_BIG_SUBTAG);
    CHECK(lb_big_plus(make_boxed(a),make_boxed(b),r)==make_small(0));
    for(i=1;i<digits;++i) a[i]=0;
    a[digits]=1;
    result=lb_big_plus(make_boxed(a),lb_small_to_big(-1,minus_one),r);
    CHECK(lb_is_big(result) && lb_big_size(result)==digits-1);
    for(i=1;i<digits;++i) CHECK(r[i]==UINTPTR_MAX);
    free(r); free(b); free(a);
}
static void floating_rounding(void)
{
    Eterm digits[18]={0}; uint64_t state=UINT64_C(0x726f756e64696e67),bits,expected;
    fenv_t saved; double actual,wanted; size_t i,n,sign;
    CHECK(!fegetenv(&saved) && !fesetenv(FE_DFL_ENV));
    for(i=0;i<10000;++i) {
        __int128 value=(__int128)(((__uint128_t)((random_word(&state)>>4)|1)<<64)|random_word(&state));
        if(i&1) value=-value;
        CHECK(!lb_big_to_double(encode(value,digits),&actual));
        wanted=(double)value; CHECK(!memcmp(&actual,&wanted,sizeof(actual)));
    }
    for(n=2;n<=16;++n) for(sign=0;sign<2;++sign) {
        memset(digits,0,sizeof(digits)); digits[0]=_make_header(n,sign?NEG_BIG_SUBTAG:POS_BIG_SUBTAG);
        digits[n]=1; digits[n-1]=(Uint)1<<11;
        expected=((Uint)(1023+64*(n-1))<<52)|((Uint)sign<<63);
        CHECK(!lb_big_to_double(make_boxed(digits),&actual)); memcpy(&bits,&actual,sizeof(bits)); CHECK(bits==expected);
        digits[1]|=1;
        CHECK(!lb_big_to_double(make_boxed(digits),&actual)); memcpy(&bits,&actual,sizeof(bits)); CHECK(bits==expected+1);
        digits[1]&=~(Uint)1; digits[n-1]|=(Uint)1<<12;
        CHECK(!lb_big_to_double(make_boxed(digits),&actual)); memcpy(&bits,&actual,sizeof(bits)); CHECK(bits==expected+2);
    }
    memset(digits,0,sizeof(digits)); digits[0]=_make_header(16,POS_BIG_SUBTAG); digits[16]=UINT64_C(0xfffffffffffff800);
    CHECK(!lb_big_to_double(make_boxed(digits),&actual) && actual==DBL_MAX);
    for(i=1;i<16;++i) digits[i]=UINTPTR_MAX;
    digits[16]+=1023;
    CHECK(!lb_big_to_double(make_boxed(digits),&actual) && actual==DBL_MAX);
    ++digits[16]; actual=17;
    CHECK(lb_big_to_double(make_boxed(digits),&actual)==-1 && actual==17);
    CHECK(!fesetenv(&saved));
}
int main(void)
{
    uint64_t state=UINT64_C(0x72666430303033); size_t i;
    boundaries(); floating_rounding();
    for(i=0;i<10000;++i) {
        __int128 a=(__int128)(((__uint128_t)(random_word(&state)>>4)<<64)|random_word(&state));
        __int128 b=(__int128)(((__uint128_t)(random_word(&state)>>4)<<64)|random_word(&state));
        if(i&1) a=-a; if(i&2) b=-b;
        check_sum(a,b);
    }
    carry_chains(2); carry_chains(257); carry_chains(LB_BIG_ARITY_MAX);
    puts("CORE_BIG_COMPONENT_OK native_add_subtract=true randomized_128bit_oracle=10000 carry_borrow=true native_size_limit=true float_rounding_oracle=10000 limb_crossing_ties=true immutable_inputs=true execution=false");
    return 0;
}
