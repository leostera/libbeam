/* SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 1997-2026. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * erl_term_hashing.c:make_hash2 and block_hash, for owned flat64 terms.
 * Explicit typed frames replace ERTS estack/trap state. No heap collection or
 * guest effect occurs while scratch borrows terms; every prefix is releasable.
 */
#include "process_internal.h"
#include "native_record.h"
#include "big.h"
#include "binary.h"
#include "flatmap.h"
#include "reference.h"
#define H UINT32_C(0x9e3779b9)
#define PHASH_F1 UINT32_C(268440163)
#define PHASH_F2 UINT32_C(268439161)
#define PHASH_F3 UINT32_C(268435459)
#define PHASH_F4 UINT32_C(268436141)
#define PHASH_F5 UINT32_C(268438633)
#define PHASH_F6 UINT32_C(268437017)
#define PHASH_F8 UINT32_C(268437511)
#define PHASH_F9 UINT32_C(268439627)
#define PHASH_F10 UINT32_C(268440479)
#define PHASH_F11 UINT32_C(268440577)
#define PHASH_F12 UINT32_C(268440581)
#define PHASH_F13 UINT32_C(268440593)
#define PHASH_F14 UINT32_C(268440611)
static void mix_state(uint32_t *ap,uint32_t *bp,uint32_t *cp)
{
    uint32_t a=*ap,b=*bp,c=*cp;
    a-=b; a-=c; a^=c>>13;
    b-=c; b-=a; b^=a<<8;
    c-=a; c-=b; c^=b>>13;
    a-=b; a-=c; a^=c>>12;
    b-=c; b-=a; b^=a<<16;
    c-=a; c-=b; c^=b>>5;
    a-=b; a-=c; a^=c>>3;
    b-=c; b-=a; b^=a<<10;
    c-=a; c-=b; c^=b>>15;
    *ap=a; *bp=b; *cp=c;
}
static uint32_t mix(uint32_t a,uint32_t b,uint32_t c)
{ mix_state(&a,&b,&c); return c; }
static uint32_t hash_words(uint32_t hash,uint32_t a,uint32_t b,uint32_t multiplier)
{ uint32_t constant=H*multiplier; return mix(constant+a,constant+b,hash); }
typedef enum { HASH_TERM, HASH_MAP_NODE, HASH_PAIR_END, HASH_MAP_END,
               HASH_PHASH_TUPLE_END, HASH_PHASH_LIST_END } HashKind;
typedef struct { HashKind kind; Eterm term; uint32_t hash,pairs; } HashFrame;
typedef struct {
    LbProcess *process;
    HashFrame local[64],*frames;
    size_t count,capacity;
} HashStack;
static int charge(HashStack *s,uint64_t amount)
{
    if(!lb_work_domain_charge(s->process->entry_module->space->domain,amount)) {
        s->process->status=LB_PROCESS_WORK_LIMIT; return 0;
    }
    s->process->last_native_cost+=amount; return 1;
}
static int push(HashStack *s,HashFrame frame)
{
    if(!charge(s,1)) return 0;
    if(s->count==s->capacity) {
        size_t capacity,bytes; void *memory;
        if(!lb_size_add(s->capacity,s->capacity,&capacity) || !lb_size_mul(capacity,sizeof(HashFrame),&bytes)) {
            s->process->status=LB_PROCESS_LIMIT; return 0;
        }
        if(!charge(s,s->count)) return 0;
        LbAllocDomain *domain=s->process->entry_module->space->domain;
        LbAllocStatus status=lb_alloc_domain_allocate(domain,bytes,&memory);
        if(status!=LB_ALLOC_OK) {
            s->process->status=status==LB_ALLOC_WORK_LIMIT?LB_PROCESS_WORK_LIMIT:
                status==LB_ALLOC_NO_MEMORY?LB_PROCESS_NO_MEMORY:LB_PROCESS_LIMIT;
            return 0;
        }
        memcpy(memory,s->frames,s->count*sizeof(HashFrame));
        if(s->frames!=s->local) lb_release(domain,s->frames);
        s->frames=memory; s->capacity=capacity;
    }
    s->frames[s->count++]=frame; return 1;
}
static int push_term(HashStack *s,Eterm term)
{ return push(s,(HashFrame){.kind=HASH_TERM,.term=term}); }
static int push_pair(HashStack *s,Eterm key,Eterm value)
{
    return push(s,(HashFrame){.kind=HASH_PAIR_END}) && push_term(s,value) && push_term(s,key);
}
static uint32_t atom_hash(LbProcess *p,Eterm atom)
{
    uint32_t hash;
    if(lb_atoms_hash(p->entry_module->space->atoms,atom,&hash)!=LB_ATOM_OK) abort();
    return hash;
}
/* Read exactly the requested bits, including a partial final byte. Never read
 * past a backing allocation when an unaligned view ends on a byte boundary. */
static uint32_t bits(const LbBitstringView *v,size_t position,unsigned count)
{
    uint32_t result=0; size_t start=v->bit_offset+position;
    for(unsigned i=0;i<count;++i) result=(result<<1)|((v->data[(start+i)/8]>>(7-(start+i)%8))&1u);
    return result;
}
static uint32_t binary_hash(const LbBitstringView *v,uint32_t hash)
{
    uint32_t length=(uint32_t)(v->bit_size/8),a=H,b=H,c=H*13u+hash;
    unsigned tail=(unsigned)(v->bit_size%8); size_t position=0;
    if(!length && !tail) return c;
    while(length-position>=12) {
        for(unsigned i=0;i<12;++i) {
            uint32_t byte=bits(v,8*(position+i),8);
            if(i<4) a+=byte<<(8*i);
            else if(i<8) b+=byte<<(8*(i-4));
            else c+=byte<<(8*(i-8));
        }
        mix_state(&a,&b,&c);
        position+=12;
    }
    for(unsigned i=0;i<length-position;++i) {
        uint32_t byte=bits(v,8*(position+i),8);
        if(i<4) a+=byte<<(8*i);
        else if(i<8) b+=byte<<(8*(i-4));
        else c+=byte<<(8*(i-7));
    }
    c+=length; hash=mix(a,b,c);
    if(tail) hash=hash_words(hash,tail,bits(v,(size_t)length*8,tail),15);
    return hash;
}
static int hash_term(HashStack *s,Eterm input,uint32_t *out)
{
    uint32_t hash=0,pairs=0; LbProcess *p=s->process;
    if(!push_term(s,input)) return 0;
    while(s->count) {
        HashFrame frame=s->frames[--s->count]; Eterm term=frame.term;
        if(!charge(s,1)) return 0;
        if(frame.kind==HASH_PAIR_END) { pairs^=hash; hash=0; continue; }
        if(frame.kind==HASH_MAP_END) { hash=hash_words(frame.hash,pairs,0,19); pairs=frame.pairs; continue; }
        if(frame.kind==HASH_MAP_NODE) {
            if(is_list(term)) {
                if(!push_pair(s,CAR(list_val(term)),CDR(list_val(term)))) return 0;
            } else if(is_tuple(term)) {
                Eterm *tuple=tuple_val(term);
                for(size_t i=1;i<=arityval(tuple[0]);++i)
                    if(!push(s,(HashFrame){.kind=HASH_MAP_NODE,.term=tuple[i]})) return 0;
            } else {
                Eterm *node=boxed_val(term); size_t offset=lb_map_object_child_offset(node),count=lb_map_object_child_count(node);
                for(size_t i=0;i<count;++i)
                    if(!push(s,(HashFrame){.kind=HASH_MAP_NODE,.term=node[offset+i]})) return 0;
            }
            continue;
        }
        if(is_atom(term)) {
            uint32_t value=atom_hash(p,term); hash=hash?hash_words(hash,value,0,3):value;
        } else if(is_nil(term)) hash=hash?hash_words(hash,2,0,2):UINT32_C(3468870702);
        else if(is_small(term)) {
            Sint value=signed_val(term);
            if(value>=-((Sint)1<<27) && value<((Sint)1<<27)) {
                if(value<0) hash=hash_words(hash,(uint32_t)-value,0,1);
                hash=hash_words(hash,(uint32_t)value,0,1);
            } else {
                uint64_t magnitude=value<0?0-(uint64_t)value:(uint64_t)value;
                hash=hash_words(hash,(uint32_t)magnitude,(uint32_t)(magnitude>>32),value<0?10:11);
            }
        } else if(is_internal_pid(term)) hash=hash_words(hash,(uint32_t)((term>>_TAG_IMMED1_SIZE)&UINT32_C(0xfffffff)),0,5);
        else if(is_list(term)) {
            unsigned count=0; uint32_t word=0;
            while(is_list(term) && is_small(CAR(list_val(term))) && (Uint)signed_val(CAR(list_val(term)))<=255) {
                if(!charge(s,1)) return 0;
                word=(word<<8)+(uint32_t)signed_val(CAR(list_val(term))); ++count;
                term=CDR(list_val(term));
                if(count==4) { hash=hash_words(hash,word,0,4); count=0; word=0; }
            }
            if(count) hash=hash_words(hash,word,0,4);
            if(is_list(term)) {
                if(!push_term(s,CDR(list_val(term))) || !push_term(s,CAR(list_val(term)))) return 0;
            } else if(!push_term(s,term)) return 0;
        } else if(is_tuple(term)) {
            Eterm *tuple=tuple_val(term); size_t count=arityval(tuple[0]);
            hash=hash_words(hash,(uint32_t)count,0,9);
            for(size_t i=count;i;i--) if(!push_term(s,tuple[i])) return 0;
        } else if(is_record(term)) {
            const LbRecordTermDefinition *def=lb_record_term_definition(term);
            const LbRecordTermInstance *record=lb_record_instance(term);
            size_t count=lb_record_term_field_count(term);
            if(!def || !is_small(def->hash)) { p->status=LB_PROCESS_REFUSED; return 0; }
            hash=hash_words(hash,(uint32_t)count,(uint32_t)signed_val(def->hash),23);
            for(size_t i=count;i;i--) if(!push_term(s,record->values[i-1])) return 0;
        } else if(lb_is_big(term)) {
            Eterm *big=boxed_val(term); uint32_t constant=(big[0]&_TAG_HEADER_MASK)==NEG_BIG_SUBTAG?10:11;
            for(size_t i=1;i<=lb_big_size(term);++i) {
                if(!charge(s,1)) return 0;
                hash=hash_words(hash,(uint32_t)big[i],(uint32_t)(big[i]>>32),constant);
            }
        } else if(is_boxed(term) && (*boxed_val(term)&_TAG_HEADER_MASK)==FLOAT_SUBTAG) {
            uint64_t value=boxed_val(term)[1]; if(!(value<<1)) value=0;
            hash=hash_words(hash,(uint32_t)(value>>32),(uint32_t)value,12);
        } else if(is_boxed(term) && (*boxed_val(term)&_HEADER_SUBTAG_MASK)==MAP_SUBTAG) {
            Eterm *map=boxed_val(term); size_t count=map[1]; hash=hash_words(hash,(uint32_t)count,0,16);
            if(count) {
                if(!push(s,(HashFrame){.kind=HASH_MAP_END,.hash=hash,.pairs=pairs})) return 0;
                hash=0; pairs=0;
                if(lb_is_flatmap(term)) {
                    Eterm *keys=tuple_val(map[2]);
                    for(size_t i=0;i<count;++i) if(!push_pair(s,keys[i+1],map[i+3])) return 0;
                } else if(!push(s,(HashFrame){.kind=HASH_MAP_NODE,.term=term})) return 0;
            }
        } else {
            LbBitstringView view; const LbFun *fun; const LbOrdinaryRef *ref;
            if(lb_bitstring_view(term,&view)) {
                if(!charge(s,(uint32_t)(view.bit_size/8)+UINT64_C(1))) return 0;
                hash=binary_hash(&view,hash);
            } else if((fun=lb_fun(term))) {
                if(lb_fun_is_external(fun)) {
                    const LbMFA *mfa=&fun->entry.exp->info.mfa;
                    hash=hash_words(hash,mfa->arity,atom_hash(p,mfa->module),1);
                    hash=hash_words(hash,atom_hash(p,mfa->function),0,14);
                } else {
                    const LbFunEntry *entry=fun->entry.fun; size_t count=lb_fun_free(fun);
                    hash=hash_words(hash,(uint32_t)count,atom_hash(p,entry->module),1);
                    hash=hash_words(hash,(uint32_t)entry->index,(uint32_t)entry->old_uniq,1);
                    for(size_t i=count;i;i--) if(!push_term(s,fun->env[i-1])) return 0;
                }
            } else if((ref=lb_reference(term))) hash=hash_words(hash,ref->num[0],0,7);
            else { p->status=LB_PROCESS_REFUSED; return 0; }
        }
    }
    *out=hash; return 1;
}
static void phash_integer_step(uint32_t *hash,uint32_t value,uint32_t prime)
{
    for(unsigned byte=0;byte<4;++byte) { *hash=*hash*prime+(value&UINT32_C(0xff)); value>>=8; }
}
static uint32_t phash_binary(const LbBitstringView *view,uint32_t hash)
{
    size_t bytes=view->bit_size/8; unsigned tail=(unsigned)(view->bit_size%8);
    for(size_t i=0;i<bytes;++i) hash=hash*PHASH_F1+bits(view,i*8,8);
    if(tail) hash=(hash*PHASH_F1+bits(view,bytes*8,tail))*PHASH_F12+tail;
    return hash*PHASH_F4+(uint32_t)bytes;
}
static int hash_legacy(HashStack *s,Eterm input,uint32_t *out)
{
    uint32_t hash=0; LbProcess *p=s->process;
    if(!push_term(s,input)) return 0;
    while(s->count) {
        HashFrame frame=s->frames[--s->count]; Eterm term=frame.term;
        if(!charge(s,1)) return 0;
        if(frame.kind==HASH_PHASH_TUPLE_END) { hash=hash*PHASH_F9+frame.pairs; continue; }
        if(frame.kind==HASH_PHASH_LIST_END) { hash*=PHASH_F8; continue; }
        if(is_nil(term)) hash=hash*PHASH_F3+1;
        else if(is_atom(term)) hash=hash*PHASH_F1+atom_hash(p,term);
        else if(is_small(term)) {
            Sint signed_value=signed_val(term); Uint value=signed_value<0?-(Uint)signed_value:(Uint)signed_value;
            phash_integer_step(&hash,(uint32_t)value,PHASH_F2);
            if(value>>32) phash_integer_step(&hash,(uint32_t)(value>>32),PHASH_F2);
            hash*=signed_value<0?PHASH_F4:PHASH_F3;
        } else if(lb_is_big(term)) {
            Eterm *big=boxed_val(term); size_t count=lb_big_size(term); int negative=(*big&_TAG_HEADER_MASK)==NEG_BIG_SUBTAG;
            for(size_t i=0;i<count;++i) {
                Uint digit=big[i+1]; unsigned bytes=sizeof(Uint);
                if(i+1==count && sizeof(Uint)>4 && !(digit>>32)) bytes=4;
                if(!charge(s,bytes)) return 0;
                for(unsigned j=0;j<bytes;++j) { hash=hash*PHASH_F2+(uint8_t)digit; digit>>=8; }
            }
            hash*=negative?PHASH_F4:PHASH_F3;
        } else if(is_boxed(term) && (*boxed_val(term)&_TAG_HEADER_MASK)==FLOAT_SUBTAG) {
            uint64_t value=boxed_val(term)[1]; if((value<<1)==0) value=0;
            hash=hash*PHASH_F6+((uint32_t)value^(uint32_t)(value>>32));
        } else if(is_list(term)) {
            while(is_list(term) && is_small(CAR(list_val(term))) &&
                  signed_val(CAR(list_val(term)))>=0 && signed_val(CAR(list_val(term)))<=255) {
                if(!charge(s,1)) return 0;
                hash=hash*PHASH_F2+(uint32_t)signed_val(CAR(list_val(term)));
                term=CDR(list_val(term));
            }
            if(is_list(term)) {
                Eterm *cell=list_val(term);
                if(!push(s,(HashFrame){.kind=HASH_PHASH_LIST_END}) ||
                   !push_term(s,CDR(cell)) || !push_term(s,CAR(cell))) return 0;
            } else if(!push(s,(HashFrame){.kind=HASH_PHASH_LIST_END}) || !push_term(s,term)) return 0;
        } else if(is_tuple(term)) {
            Eterm *tuple=tuple_val(term); size_t arity=arityval(tuple[0]);
            if(arity>UINT32_MAX || !push(s,(HashFrame){.kind=HASH_PHASH_TUPLE_END,.pairs=(uint32_t)arity})) return 0;
            for(size_t i=arity;i;i--) if(!push_term(s,tuple[i])) return 0;
        } else if(is_internal_pid(term)) {
            uint32_t number=(uint32_t)((term>>_TAG_IMMED1_SIZE)&UINT32_C(0x7fff));
            phash_integer_step(&hash,number,PHASH_F5); hash*=PHASH_F6;
        } else {
            LbBitstringView view; const LbOrdinaryRef *ref; const LbFun *fun;
            if(lb_bitstring_view(term,&view)) {
                uint64_t cost=(uint64_t)(view.bit_size/8)+(view.bit_size%8!=0)+1;
                if(!charge(s,cost)) return 0;
                hash=phash_binary(&view,hash);
            } else if((ref=lb_reference(term))) {
                phash_integer_step(&hash,ref->num[0],PHASH_F9); hash*=PHASH_F10;
            } else if((fun=lb_fun(term))) {
                if(lb_fun_is_external(fun)) {
                    const LbMFA *mfa=&fun->entry.exp->info.mfa;
                    hash=hash*PHASH_F11+mfa->arity;
                    hash=hash*PHASH_F1+atom_hash(p,mfa->module);
                    hash=hash*PHASH_F1+atom_hash(p,mfa->function);
                } else {
                    const LbFunEntry *entry=fun->entry.fun; size_t count=lb_fun_free(fun);
                    hash=hash*PHASH_F10+(uint32_t)count;
                    hash=hash*PHASH_F1+atom_hash(p,entry->module);
                    hash=hash*PHASH_F2+(uint32_t)entry->index;
                    hash=hash*PHASH_F2+entry->old_uniq;
                    for(size_t i=count;i;i--) if(!push_term(s,fun->env[i-1])) return 0;
                }
            } else if(is_boxed(term) && (*boxed_val(term)&_HEADER_SUBTAG_MASK)==MAP_SUBTAG) {
                HashStack nested={.process=p,.capacity=64}; nested.frames=nested.local;
                uint32_t map_hash; int ok=hash_term(&nested,term,&map_hash);
                if(nested.frames!=nested.local) lb_release(p->entry_module->space->domain,nested.frames);
                if(!ok) return 0;
                hash=hash*PHASH_F13+PHASH_F14+map_hash;
            } else { p->status=LB_PROCESS_REFUSED; return 0; }
        }
    }
    *out=hash; return 1;
}
static Eterm phash2(LbProcess *p,Eterm term,uint64_t range)
{
    HashStack stack={.process=p,.capacity=64}; stack.frames=stack.local;
    uint32_t hash; int ok=hash_term(&stack,term,&hash);
    if(stack.frames!=stack.local) lb_release(p->entry_module->space->domain,stack.frames);
    return ok?make_small((Sint)(hash%range)):THE_NON_VALUE;
}
Eterm lb_bif_phash_2(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    (void)pc; p->last_native_cost=1;
    if(!lb_work_domain_charge(p->entry_module->space->domain,1)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    if(!is_small(args[1]) || signed_val(args[1])<1 || (uint64_t)signed_val(args[1])>(UINT64_C(1)<<32)) {
        p->freason=LB_EXC_BADARG; return THE_NON_VALUE;
    }
    HashStack stack={.process=p,.capacity=64}; stack.frames=stack.local; uint32_t hash;
    int ok=hash_legacy(&stack,args[0],&hash);
    if(stack.frames!=stack.local) lb_release(p->entry_module->space->domain,stack.frames);
    return ok?make_small((Sint)(1+(uint64_t)hash%(uint64_t)signed_val(args[1]))):THE_NON_VALUE;
}
Eterm lb_bif_phash2_1(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    (void)pc; p->last_native_cost=1;
    if(!lb_work_domain_charge(p->entry_module->space->domain,1)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    return phash2(p,args[0],UINT64_C(1)<<27);
}
Eterm lb_bif_phash2_2(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    (void)pc; p->last_native_cost=1;
    if(!lb_work_domain_charge(p->entry_module->space->domain,1)) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    if(!is_small(args[1]) || signed_val(args[1])<1 || (uint64_t)signed_val(args[1])>(UINT64_C(1)<<32)) {
        p->freason=LB_EXC_BADARG; return THE_NON_VALUE;
    }
    return phash2(p,args[0],(uint64_t)signed_val(args[1]));
}
