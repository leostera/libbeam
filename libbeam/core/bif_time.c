/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Owned system-clock boundary; POSIX clock access is isolated at this layer.
 */
#define _POSIX_C_SOURCE 200809L
#include "process_internal.h"
#include "code_internal.h"
#include "big.h"
#include "time_unit.h"
#include <time.h>
#include <limits.h>

static void time_failure(LbProcess *p,LbAllocStatus failure);
static int term_to_int64(Eterm term,int64_t *out)
{
    if(is_small(term)) { *out=signed_val(term); return 1; }
    if(!lb_is_big(term) || lb_big_size(term)!=1) return 0;
    Uint magnitude=boxed_val(term)[1]; int negative=(*boxed_val(term)&_HEADER_SUBTAG_MASK)==NEG_BIG_SUBTAG;
    if(!negative) {
        if(magnitude>INT64_MAX) return 0;
        *out=(int64_t)magnitude; return 1;
    }
    if(magnitude>(Uint)INT64_MAX+1u) return 0;
    *out=magnitude==(Uint)INT64_MAX+1u?INT64_MIN:-(int64_t)magnitude;
    return 1;
}
static int datetime_fields(Eterm term,Sint *year,Sint *month,Sint *day,Sint *hour,Sint *minute,Sint *second)
{
    if(!is_tuple(term) || arityval(*tuple_val(term))!=2) return 0;
    Eterm date=tuple_val(term)[1],clock=tuple_val(term)[2];
    if(!is_tuple(date) || arityval(*tuple_val(date))!=3 ||
       !is_tuple(clock) || arityval(*tuple_val(clock))!=3) return 0;
    Eterm values[6]={tuple_val(date)[1],tuple_val(date)[2],tuple_val(date)[3],
                     tuple_val(clock)[1],tuple_val(clock)[2],tuple_val(clock)[3]};
    for(size_t i=0;i<6;i++) if(!is_small(values[i])) return 0;
    *year=signed_val(values[0]); *month=signed_val(values[1]); *day=signed_val(values[2]);
    *hour=signed_val(values[3]); *minute=signed_val(values[4]); *second=signed_val(values[5]);
    return *year>=0 && *month>=1 && *month<=12 && *day>=1 && *day<=31 &&
           *hour>=0 && *hour<=23 && *minute>=0 && *minute<=59 && *second>=0 && *second<=59;
}
static int64_t days_from_civil(int64_t year,unsigned month,unsigned day)
{
    year-=month<=2;
    int64_t era=(year>=0?year:year-399)/400;
    unsigned year_of_era=(unsigned)(year-era*400);
    unsigned shifted_month=month>2?month-3:month+9;
    unsigned day_of_year=(153*shifted_month+2)/5+day-1;
    unsigned day_of_era=year_of_era*365+year_of_era/4-year_of_era/100+day_of_year;
    return era*146097+(int64_t)day_of_era-719468;
}
static Eterm datetime_term(LbProcess *p,const struct tm *value)
{
    Eterm date=TUPLE3(p->htop,make_small((Sint)value->tm_year+1900),make_small(value->tm_mon+1),make_small(value->tm_mday)); p->htop+=4;
    Eterm clock=TUPLE3(p->htop,make_small(value->tm_hour),make_small(value->tm_min),make_small(value->tm_sec)); p->htop+=4;
    Eterm result=TUPLE2(p->htop,date,clock); p->htop+=3; return result;
}
static Eterm localtime_to_universaltime(LbProcess *p,Eterm datetime,Eterm is_dst)
{
    LbAllocDomain *domain=p->entry_module->space->domain; Sint year,month,day,hour,minute,second;
    if(!lb_work_domain_charge(domain,1)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    p->last_native_cost=1;
    if(!datetime_fields(datetime,&year,&month,&day,&hour,&minute,&second) ||
       year<(Sint)INT_MIN+1900 || year>(Sint)INT_MAX+1900 ||
       (is_dst!=am_undefined && is_dst!=am_true && is_dst!=am_false)) {
        p->freason=LB_EXC_BADARG; return THE_NON_VALUE;
    }
    struct tm local={0},verified,universal;
    local.tm_year=(int)(year-1900); local.tm_mon=(int)month-1; local.tm_mday=(int)day;
    local.tm_hour=(int)hour; local.tm_min=(int)minute; local.tm_sec=(int)second;
    local.tm_isdst=is_dst==am_undefined?-1:is_dst==am_true?1:0;
    time_t epoch=mktime(&local);
    if(!localtime_r(&epoch,&verified) || verified.tm_year!=local.tm_year || verified.tm_mon!=local.tm_mon ||
       verified.tm_mday!=local.tm_mday || verified.tm_hour!=local.tm_hour || verified.tm_min!=local.tm_min ||
       verified.tm_sec!=local.tm_sec || (is_dst!=am_undefined && verified.tm_isdst!=local.tm_isdst)) {
        p->freason=LB_EXC_BADARG; return THE_NON_VALUE;
    }
    if(!gmtime_r(&epoch,&universal)) { p->status=LB_PROCESS_ENVIRONMENT; return THE_NON_VALUE; }
    if(!lb_work_domain_charge(domain,11)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    p->last_native_cost+=11; size_t need,collections=p->collections;
    if(!lb_size_add(11,p->heap_reserved,&need)) { p->status=LB_PROCESS_LIMIT; return THE_NON_VALUE; }
    if(!lb_heap_reserve(p,need,MAX_REG)) { time_failure(p,lb_alloc_domain_failure(domain)); return THE_NON_VALUE; }
    if(p->collections!=collections) p->last_native_cost+=p->last_gc_cost;
    return datetime_term(p,&universal);
}
Eterm lb_bif_universaltime_to_posixtime_1(LbProcess *p,Eterm *args,const BeamInstr *instruction)
{
    (void)instruction; LbAllocDomain *domain=p->entry_module->space->domain; p->last_native_cost=1;
    if(!lb_work_domain_charge(domain,1)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    Sint year,month,day,hour,minute,second;
    if(!datetime_fields(args[0],&year,&month,&day,&hour,&minute,&second) ||
       (uint64_t)year>(uint64_t)INT64_MAX/86400/366) { p->freason=LB_EXC_BADARG; return THE_NON_VALUE; }
    static const unsigned char month_days[]={31,28,31,30,31,30,31,31,30,31,30,31};
    int leap=(year%4==0 && (year%100!=0 || year%400==0));
    unsigned maximum=month_days[month-1]+(month==2 && leap);
    if((unsigned)day>maximum) { p->freason=LB_EXC_BADARG; return THE_NON_VALUE; }
    int64_t days=days_from_civil(year,(unsigned)month,(unsigned)day);
    if(days>INT64_MAX/86400 || days<INT64_MIN/86400) { p->freason=LB_EXC_BADARG; return THE_NON_VALUE; }
    int64_t base=days*86400,within=(int64_t)hour*3600+(int64_t)minute*60+second;
    if(base>INT64_MAX-within) { p->freason=LB_EXC_BADARG; return THE_NON_VALUE; }
    int64_t value=base+within;
    if(value<MIN_SMALL || value>MAX_SMALL) {
        uint64_t magnitude=value<0?(uint64_t)(-(value+1))+1u:(uint64_t)value;
        if(!lb_work_domain_charge(domain,2)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
        p->last_native_cost+=2; size_t need,collections=p->collections;
        if(!lb_size_add(2,p->heap_reserved,&need)) { p->status=LB_PROCESS_LIMIT; return THE_NON_VALUE; }
        if(!lb_heap_reserve(p,need,MAX_REG)) { time_failure(p,lb_alloc_domain_failure(domain)); return THE_NON_VALUE; }
        if(p->collections!=collections) p->last_native_cost+=p->last_gc_cost;
        p->htop[0]=_make_header(1,value<0?NEG_BIG_SUBTAG:POS_BIG_SUBTAG);
        p->htop[1]=(Uint)magnitude; Eterm result=make_boxed(p->htop); p->htop+=2; return result;
    }
    return make_small((Sint)value);
}
Eterm lb_bif_localtime_to_universaltime_1(LbProcess *p,Eterm *args,const BeamInstr *instruction)
{ (void)instruction; return localtime_to_universaltime(p,args[0],am_undefined); }
Eterm lb_bif_localtime_to_universaltime_2(LbProcess *p,Eterm *args,const BeamInstr *instruction)
{ (void)instruction; return localtime_to_universaltime(p,args[0],args[1]); }
Eterm lb_bif_universaltime_to_localtime_1(LbProcess *p,Eterm *args,const BeamInstr *instruction)
{
    (void)instruction; LbAllocDomain *domain=p->entry_module->space->domain;
    if(!lb_work_domain_charge(domain,1)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    p->last_native_cost=1; Sint year,month,day,hour,minute,second;
    if(!datetime_fields(args[0],&year,&month,&day,&hour,&minute,&second) ||
       year<(Sint)INT_MIN+1900 || year>(Sint)INT_MAX+1900) {
        p->freason=LB_EXC_BADARG; return THE_NON_VALUE;
    }
    struct tm verified,local;
    int64_t days=days_from_civil(year,(unsigned)month,(unsigned)day);
    int64_t seconds=days*86400+(int64_t)hour*3600+(int64_t)minute*60+second;
    time_t epoch=(time_t)seconds;
    if((int64_t)epoch!=seconds || !gmtime_r(&epoch,&verified) ||
       verified.tm_year!=(int)(year-1900) || verified.tm_mon!=(int)month-1 ||
       verified.tm_mday!=(int)day || verified.tm_hour!=(int)hour ||
       verified.tm_min!=(int)minute || verified.tm_sec!=(int)second) {
        p->freason=LB_EXC_BADARG; return THE_NON_VALUE;
    }
    if(!localtime_r(&epoch,&local)) { p->status=LB_PROCESS_ENVIRONMENT; return THE_NON_VALUE; }
    if(!lb_work_domain_charge(domain,11)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    p->last_native_cost+=11; size_t need,collections=p->collections;
    if(!lb_size_add(11,p->heap_reserved,&need)) { p->status=LB_PROCESS_LIMIT; return THE_NON_VALUE; }
    if(!lb_heap_reserve(p,need,MAX_REG)) { time_failure(p,lb_alloc_domain_failure(domain)); return THE_NON_VALUE; }
    if(p->collections!=collections) p->last_native_cost+=p->last_gc_cost;
    return datetime_term(p,&local);
}

Eterm lb_bif_timestamp_0(LbProcess *p,Eterm *args,const BeamInstr *instruction)
{
    (void)args; (void)instruction; LbAllocDomain *domain=p->entry_module->space->domain; p->last_native_cost=1;
    if(!lb_work_domain_charge(domain,1)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    struct timespec now;
    if(clock_gettime(CLOCK_REALTIME,&now) || now.tv_sec<0 || now.tv_nsec<0 ||
       (uint64_t)now.tv_nsec>=LB_NATIVE_TIME_UNITS_PER_SECOND) { p->status=LB_PROCESS_ENVIRONMENT; return THE_NON_VALUE; }
    uint64_t seconds=(uint64_t)now.tv_sec,mega=seconds/1000000,second=seconds%1000000,micro=(uint64_t)now.tv_nsec/1000;
    if(mega>(uint64_t)MAX_SMALL) { p->status=LB_PROCESS_LIMIT; return THE_NON_VALUE; }
    if(!lb_work_domain_charge(domain,4)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    p->last_native_cost+=4; size_t need,collections=p->collections;
    if(!lb_size_add(4,p->heap_reserved,&need)) { p->status=LB_PROCESS_LIMIT; return THE_NON_VALUE; }
    if(!lb_heap_reserve(p,need,MAX_REG)) { time_failure(p,lb_alloc_domain_failure(domain)); return THE_NON_VALUE; }
    if(p->collections!=collections) p->last_native_cost+=p->last_gc_cost;
    Eterm result=TUPLE3(p->htop,make_small((Sint)mega),make_small((Sint)second),make_small((Sint)micro));
    p->htop+=4; return result;
}
Eterm lb_bif_posixtime_to_universaltime_1(LbProcess *p,Eterm *args,const BeamInstr *instruction)
{
    (void)instruction; LbAllocDomain *domain=p->entry_module->space->domain; p->last_native_cost=1;
    if(!lb_work_domain_charge(domain,1)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    int64_t seconds;
    if(!term_to_int64(args[0],&seconds)) { p->freason=LB_EXC_BADARG; return THE_NON_VALUE; }
    time_t epoch=(time_t)seconds; struct tm universal;
    if((int64_t)epoch!=seconds || !gmtime_r(&epoch,&universal) ||
       (int64_t)universal.tm_year+1900<0 || (int64_t)universal.tm_year+1900>MAX_SMALL) {
        p->freason=LB_EXC_BADARG; return THE_NON_VALUE;
    }
    if(!lb_work_domain_charge(domain,11)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    p->last_native_cost+=11; size_t need,collections=p->collections;
    if(!lb_size_add(11,p->heap_reserved,&need)) { p->status=LB_PROCESS_LIMIT; return THE_NON_VALUE; }
    if(!lb_heap_reserve(p,need,MAX_REG)) { time_failure(p,lb_alloc_domain_failure(domain)); return THE_NON_VALUE; }
    if(p->collections!=collections) p->last_native_cost+=p->last_gc_cost;
    return datetime_term(p,&universal);
}
Eterm lb_bif_localtime_0(LbProcess *p,Eterm *args,const BeamInstr *instruction)
{
    (void)args; (void)instruction; p->last_native_cost=0;
    LbAllocDomain *domain=p->entry_module->space->domain;
    if(!lb_work_domain_charge(domain,1)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    ++p->last_native_cost;
    time_t now=time(NULL); struct tm local;
    if(now==(time_t)-1 || !localtime_r(&now,&local)) { p->status=LB_PROCESS_ENVIRONMENT; return THE_NON_VALUE; }
    size_t need=11;
    if(!lb_size_add(need,p->heap_reserved,&need)) { p->status=LB_PROCESS_LIMIT; return THE_NON_VALUE; }
    if(!lb_work_domain_charge(domain,11)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    p->last_native_cost+=11; size_t collections=p->collections;
    if(!lb_heap_reserve(p,need,MAX_REG)) { time_failure(p,lb_alloc_domain_failure(domain)); return THE_NON_VALUE; }
    if(p->collections!=collections) p->last_native_cost+=p->last_gc_cost;
    Eterm date=TUPLE3(p->htop,make_small((Sint)local.tm_year+1900),make_small(local.tm_mon+1),make_small(local.tm_mday)); p->htop+=4;
    Eterm clock=TUPLE3(p->htop,make_small(local.tm_hour),make_small(local.tm_min),make_small(local.tm_sec)); p->htop+=4;
    Eterm result=TUPLE2(p->htop,date,clock); p->htop+=3; return result;
}
Eterm lb_bif_universaltime_0(LbProcess *p,Eterm *args,const BeamInstr *instruction)
{
    (void)args; (void)instruction; LbAllocDomain *domain=p->entry_module->space->domain;
    p->last_native_cost=1;
    if(!lb_work_domain_charge(domain,1)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    time_t now=time(NULL); struct tm utc;
    if(now==(time_t)-1 || !gmtime_r(&now,&utc)) { p->status=LB_PROCESS_ENVIRONMENT; return THE_NON_VALUE; }
    size_t need,collections=p->collections;
    if(!lb_size_add(11,p->heap_reserved,&need)) { p->status=LB_PROCESS_LIMIT; return THE_NON_VALUE; }
    if(!lb_work_domain_charge(domain,11)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    p->last_native_cost+=11;
    if(!lb_heap_reserve(p,need,MAX_REG)) { time_failure(p,lb_alloc_domain_failure(domain)); return THE_NON_VALUE; }
    if(p->collections!=collections) p->last_native_cost+=p->last_gc_cost;
    return datetime_term(p,&utc);
}
static void time_failure(LbProcess *p,LbAllocStatus failure)
{
    if(failure==LB_ALLOC_LIMIT) p->status=LB_PROCESS_LIMIT;
    else if(failure==LB_ALLOC_WORK_LIMIT) p->status=LB_PROCESS_WORK_LIMIT;
    else p->status=LB_PROCESS_NO_MEMORY;
}

static Eterm time_nanoseconds(LbProcess *p,clockid_t clock)
{
    struct timespec now;
    LbAllocDomain *domain=p->entry_module->space->domain;
    p->last_native_cost=1;
    if(!lb_work_domain_charge(domain,1)) {
        p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE;
    }
    if(clock_gettime(clock,&now) || now.tv_sec<0 || now.tv_nsec<0 ||
       (uint64_t)now.tv_nsec>=LB_NATIVE_TIME_UNITS_PER_SECOND ||
       (uint64_t)now.tv_sec>(UINT64_MAX-(uint64_t)now.tv_nsec)/LB_NATIVE_TIME_UNITS_PER_SECOND) {
        p->status=LB_PROCESS_ENVIRONMENT; return THE_NON_VALUE;
    }
    uint64_t nanoseconds=(uint64_t)now.tv_sec*LB_NATIVE_TIME_UNITS_PER_SECOND+(uint64_t)now.tv_nsec;
    if(nanoseconds<=MAX_SMALL) return make_small((Sint)nanoseconds);

    Eterm words[2]; const size_t digits=1; size_t needed;
    if(!lb_work_domain_charge(domain,digits+1)) {
        p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE;
    }
    p->last_native_cost+=digits+1;
    if(!lb_size_add(digits+1,p->heap_reserved,&needed)) {
        p->status=LB_PROCESS_LIMIT; return THE_NON_VALUE;
    }
    size_t collections=p->collections;
    if(!lb_heap_reserve(p,needed,MAX_REG)) {
        time_failure(p,lb_alloc_domain_failure(domain)); return THE_NON_VALUE;
    }
    if(p->collections!=collections) p->last_native_cost+=p->last_gc_cost;
    words[0]=_make_header(digits,POS_BIG_SUBTAG);
    words[1]=(Uint)nanoseconds;
    memcpy(p->htop,words,(digits+1)*sizeof(Eterm));
    Eterm result=make_boxed(p->htop); p->htop+=digits+1;
    return result;
}

static int clock_nanoseconds(clockid_t clock,uint64_t *out)
{
    struct timespec now;
    if(clock_gettime(clock,&now) || now.tv_sec<0 || now.tv_nsec<0 ||
       (uint64_t)now.tv_nsec>=LB_NATIVE_TIME_UNITS_PER_SECOND ||
       (uint64_t)now.tv_sec>(UINT64_MAX-(uint64_t)now.tv_nsec)/LB_NATIVE_TIME_UNITS_PER_SECOND) return 0;
    *out=(uint64_t)now.tv_sec*LB_NATIVE_TIME_UNITS_PER_SECOND+(uint64_t)now.tv_nsec;
    return 1;
}
static Eterm time_offset_nanoseconds(LbProcess *p)
{
    LbAllocDomain *domain=p->entry_module->space->domain; uint64_t realtime,monotonic,magnitude;
    p->last_native_cost=0;
    if(!lb_work_domain_charge(domain,2)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    p->last_native_cost=2;
    if(!clock_nanoseconds(CLOCK_REALTIME,&realtime) ||
       !clock_nanoseconds(CLOCK_MONOTONIC,&monotonic)) { p->status=LB_PROCESS_ENVIRONMENT; return THE_NON_VALUE; }
    int negative=realtime<monotonic; magnitude=negative?monotonic-realtime:realtime-monotonic;
    if((!negative && magnitude<=MAX_SMALL) ||
       (negative && magnitude<=(uint64_t)MAX_SMALL)) return make_small(negative?-(Sint)magnitude:(Sint)magnitude);
    if(negative && magnitude==(uint64_t)MAX_SMALL+1) return make_small(MIN_SMALL);
    size_t words=2,need,collections=p->collections;
    if(!lb_size_add(words,p->heap_reserved,&need)) { p->status=LB_PROCESS_LIMIT; return THE_NON_VALUE; }
    if(!lb_work_domain_charge(domain,words)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    p->last_native_cost+=words;
    if(!lb_heap_reserve(p,need,MAX_REG)) { time_failure(p,lb_alloc_domain_failure(domain)); return THE_NON_VALUE; }
    if(p->collections!=collections) p->last_native_cost+=p->last_gc_cost;
    p->htop[0]=_make_header(1,negative?NEG_BIG_SUBTAG:POS_BIG_SUBTAG);
    p->htop[1]=(Uint)magnitude; Eterm result=make_boxed(p->htop); p->htop+=words; return result;
}
static Eterm time_in_unit(LbProcess *p,Eterm unit,clockid_t clock)
{
    Eterm value=time_nanoseconds(p,clock);
    if(is_non_value(value)) return value;
    Uint clock_cost=p->last_native_cost;
    /* During a moving collection, preserve the measured native clock term as
     * an explicit actor root. The unit argument itself remains in the X bank. */
    if(p->native_time_root!=NIL) abort();
    p->native_time_root=value;
    Eterm from=make_small(LB_NATIVE_TIME_UNITS_PER_SECOND);
    Eterm conversion[3]={p->native_time_root,from,unit};
    Eterm converted=lb_bif_convert_time_unit_3(p,conversion,NULL);
    p->last_native_cost+=clock_cost;
    p->native_time_root=NIL;
    return converted;
}

Eterm lb_bif_monotonic_time_0(LbProcess *p,Eterm *args,const BeamInstr *pc)
{ (void)args; (void)pc; return time_nanoseconds(p,CLOCK_MONOTONIC); }
Eterm lb_bif_monotonic_time_1(LbProcess *p,Eterm *args,const BeamInstr *pc)
{ (void)pc; return time_in_unit(p,args[0],CLOCK_MONOTONIC); }
Eterm lb_bif_system_time_0(LbProcess *p,Eterm *args,const BeamInstr *pc)
{ (void)args; (void)pc; return time_nanoseconds(p,CLOCK_REALTIME); }
Eterm lb_bif_system_time_1(LbProcess *p,Eterm *args,const BeamInstr *pc)
{ (void)pc; return time_in_unit(p,args[0],CLOCK_REALTIME); }
Eterm lb_bif_time_offset_0(LbProcess *p,Eterm *args,const BeamInstr *pc)
{ (void)args; (void)pc; return time_offset_nanoseconds(p); }
Eterm lb_bif_time_offset_1(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    (void)pc; Eterm value=time_offset_nanoseconds(p);
    if(is_non_value(value)) return value;
    Uint clock_cost=p->last_native_cost;
    if(p->native_time_root!=NIL) abort();
    p->native_time_root=value;
    Eterm conversion[3]={p->native_time_root,make_small(LB_NATIVE_TIME_UNITS_PER_SECOND),args[0]};
    Eterm converted=lb_bif_convert_time_unit_3(p,conversion,NULL);
    p->last_native_cost+=clock_cost; p->native_time_root=NIL; return converted;
}
