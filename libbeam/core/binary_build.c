/* SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 1999-2025. All Rights Reserved.
 * Copyright Ericsson AB 2017-2026. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Owned boundaries for the complete bs_instrs.tab i_bs_create_bin instruction.
 * Native sizing/writers/storage live in their reviewed C transplants; this
 * layer coordinates roots, failure kinds, work and transactional publication.
 */
#include "binary_build.h"
#include "process_internal.h"
#include "big.h"
static Uint byte_cost(size_t bytes) { return (Uint)(bytes/1024+(bytes%1024!=0)); }
static Eterm source(LbProcess *p,Eterm encoded)
{
    switch(loader_tag(encoded)) {
    case LOADER_X_REG:return p->reg[loader_x_reg_index(encoded)];
    case LOADER_Y_REG:return p->stop[loader_y_reg_index(encoded)];
    default:return encoded;
    }
}
static void collection_cost(LbProcess *p,size_t before,LbBinaryBuild *b)
{
    if(p->collections!=before) b->cost+=p->last_gc_cost;
}
static LbBinaryBuildStatus storage_status(LbWritableStatus status)
{
    switch(status) {
    case LB_WRITABLE_OK:return LB_BUILD_OK;
    case LB_WRITABLE_NO_MEMORY:return LB_BUILD_NO_MEMORY;
    case LB_WRITABLE_LIMIT:return LB_BUILD_LIMIT;
    case LB_WRITABLE_NOT_UNIQUE:case LB_WRITABLE_INVALID:case LB_WRITABLE_TYPE:return LB_BUILD_REFUSED;
    default:return LB_BUILD_INVALID;
    }
}
LbBinaryBuildStatus lb_binary_build_begin(LbProcess *p,const Uint *words,size_t count,
                                         size_t alloc,size_t live,LbBinaryBuild *b)
{
    if(!p || !b) return LB_BUILD_INVALID;
    *b=(LbBinaryBuild){.result=THE_NON_VALUE,.float_info=NIL,.status=LB_BUILD_INVALID};
    if(live>MAX_REG || alloc>UINT32_MAX) return b->status;
    if(!lb_work_domain_charge(p->entry_module->space->domain,1+3*(count/5)))
        return b->status=LB_BUILD_NO_MEMORY;
    LbBinaryRegisters registers={p->reg,live,p->stop,(size_t)(p->hend-p->stop)};
    LbBinarySizeResult sized=lb_binary_size(registers,words,count,8u*(size_t)LB_MAX_BINARY_BYTES);
    /* Sizing preflight and sizing/writing scans are all bounded by the native
     * descriptor count. Work is conservative, not a hard-latency guarantee. */
    if(sized.status==LB_BINARY_SIZE_INVALID) return b->status;
    b->cost=1+3*(count/5);
    if(sized.status==LB_BINARY_SIZE_GUEST_ERROR) {
        b->failure=sized.as.failure; return b->status=LB_BUILD_GUEST_ERROR;
    }
    if(sized.status==LB_BINARY_SIZE_LIMIT) return b->status=LB_BUILD_LIMIT;
    size_t bits=sized.as.bits,before; LbWritableStatus status;
    /* Bounded complete segment writes and dead-register initialization. */
    if(!lb_work_domain_charge(p->entry_module->space->domain,
        lb_work_bulk_units((bits+7)/8)+lb_work_bulk_units(MAX_REG-live))) return b->status=LB_BUILD_NO_MEMORY;
    b->cost+=byte_cost((bits+7)/8);
    /* The verifier proves no operand needs these slots. Retire physical dead
     * aliases before private uniqueness checking, not just on a lucky GC. */
    b->cost+=byte_cost((MAX_REG-live)*sizeof(Eterm));
    for(size_t i=live;i<MAX_REG;++i) p->reg[i]=NIL;
    if(words[0]==LB_BSC_PRIVATE_APPEND) {
        LbBinarySource location;
        if(loader_tag(words[3])==LOADER_X_REG) location=(LbBinarySource){LB_BINARY_SOURCE_X,loader_x_reg_index(words[3])};
        else if(loader_tag(words[3])==LOADER_Y_REG) location=(LbBinarySource){LB_BINARY_SOURCE_Y,loader_y_reg_index(words[3])};
        else return b->status;
        before=p->collections;
        int reserved=lb_heap_reserve(p,alloc,live); collection_cost(p,before,b);
        if(!reserved) return b->status=LB_BUILD_NO_MEMORY;
        p->heap_reserved=alloc;
        /* Conservative scan charge includes real allocated term positions and
         * all physical roots; dead compound aliases may still refuse. */
        b->cost+=1+(MAX_REG+(size_t)(p->htop-p->heap)+(size_t)(p->hend-p->stop))/16;
        before=p->collections;
        status=lb_writable_private_append(p,location,bits,&b->writer,&b->result);
    } else if(words[0]==LB_BSC_APPEND) {
        before=p->collections;
        status=lb_writable_append(p,source(p,words[3]),bits,alloc,(unsigned)words[1],&b->writer,&b->result);
        if(status==LB_WRITABLE_TYPE || status==LB_WRITABLE_UNIT) {
            b->failure=(LbBinaryFailure){LB_BSC_APPEND,words[2]>>3,am_badarg,
                status==LB_WRITABLE_TYPE?am_type:am_unit,source(p,words[3])};
            return b->status=LB_BUILD_GUEST_ERROR;
        }
    } else {
        before=p->collections;
        status=lb_binary_begin(p,bits,alloc,&b->writer,&b->result);
    }
    collection_cost(p,before,b); b->cost+=byte_cost(p->last_binary_bytes);
    b->status=storage_status(status);
    if(b->status==LB_BUILD_OK) {
        p->heap_reserved=alloc;
        b->expected=b->writer.offset+bits; /* bounded prefix + build from storage preflight */
    }
    return b->status;
}
LbBinaryBuildStatus lb_binary_build_raise(LbProcess *p,LbBinaryBuild *b)
{
    if(b->status!=LB_BUILD_GUEST_ERROR) return b->status;
    Uint reason;
    if(b->failure.reason==am_badarg) reason=LB_EXC_BADARG;
    else if(b->failure.reason==am_system_limit) reason=LB_EXC_SYSTEM_LIMIT;
    else return b->status=LB_BUILD_INVALID;
    /* Error allocation may move roots and release abandoned backing. Expire
     * the construction borrow before it happens; never read result again. */
    b->writer=(LbBitWriter){0}; b->result=THE_NON_VALUE;
    size_t before=p->collections;
    LbAllocStatus status=lb_binary_error_prepare(p,b->failure.kind,b->failure.segment,
        reason,b->failure.info,b->failure.value);
    b->failure.value=THE_NON_VALUE; collection_cost(p,before,b); ++b->cost;
    if(status==LB_ALLOC_NO_MEMORY) b->status=LB_BUILD_NO_MEMORY;
    else if(status!=LB_ALLOC_OK) b->status=LB_BUILD_INVALID;
    return b->status;
}
LbBinaryBuildStatus lb_binary_build_error(LbProcess *p,LbBinaryBuild *b,const Uint *word,
                                         Uint reason,Eterm info,Eterm value)
{
    if(b->status!=LB_BUILD_OK) return b->status; /* infrastructure failure is not badarg */
    Eterm atom;
    if(reason==LB_EXC_BADARG) atom=am_badarg;
    else if(reason==LB_EXC_SYSTEM_LIMIT) atom=am_system_limit;
    else return b->status=LB_BUILD_INVALID;
    b->failure=(LbBinaryFailure){(LbBinarySegmentKind)word[0],word[2]>>3,atom,info,value};
    b->status=LB_BUILD_GUEST_ERROR; return lb_binary_build_raise(p,b);
}
static int written(LbBinaryBuild *b,LbBitWriteStatus status)
{
    if(status==LB_BITS_OK) return 1;
    if(status==LB_BITS_ENVIRONMENT) b->status=LB_BUILD_ENVIRONMENT;
    else if(status!=LB_BITS_BADARG) b->status=LB_BUILD_REFUSED;
    return 0;
}
int lb_build_binary_all(LbBinaryBuild *b,Eterm value,unsigned unit)
{
    return written(b,lb_bits_put_binary_all(&b->writer,value,unit));
}
int lb_build_binary(LbBinaryBuild *b,Eterm value,size_t bits)
{
    return written(b,lb_bits_put_binary(&b->writer,value,bits));
}
int lb_build_integer_be(LbBinaryBuild *b,Eterm value,size_t bits)
{
    if(lb_is_big(value)) b->cost+=1+lb_big_size(value)/16;
    return written(b,lb_bits_put_integer(&b->writer,value,bits,LB_BITS_BIG_ENDIAN));
}
int lb_build_integer_le(LbBinaryBuild *b,Eterm value,size_t bits)
{
    if(lb_is_big(value)) b->cost+=1+lb_big_size(value)/16;
    return written(b,lb_bits_put_integer(&b->writer,value,bits,LB_BITS_LITTLE_ENDIAN));
}
int lb_build_utf8(LbBinaryBuild *b,Eterm value) { return written(b,lb_bits_put_utf8(&b->writer,value)); }
int lb_build_utf16(LbBinaryBuild *b,Eterm value,Uint flags)
{
    return written(b,lb_bits_put_utf16(&b->writer,value,flags&2?LB_BITS_LITTLE_ENDIAN:LB_BITS_BIG_ENDIAN));
}
void lb_build_string(LbBinaryBuild *b,const unsigned char *bytes,size_t count)
{
    if(!written(b,lb_bits_put_string(&b->writer,bytes,count)) && b->status==LB_BUILD_OK)
        b->status=LB_BUILD_REFUSED; /* strings are loader-owned spans, not guest terms */
}
Eterm lb_build_float(LbBinaryBuild *b,Eterm value,size_t bits,Uint flags)
{
    if(lb_is_big(value)) b->cost+=1+lb_big_size(value)/16;
    LbBitFloatResult result=lb_bits_put_float(&b->writer,value,bits,flags&2?LB_BITS_LITTLE_ENDIAN:LB_BITS_BIG_ENDIAN);
    if(written(b,result.status)) return THE_NON_VALUE;
    switch(result.error) {
    case LB_FLOAT_TYPE:b->float_info=am_type;break;
    case LB_FLOAT_NO_FLOAT:b->float_info=am_no_float;break;
    case LB_FLOAT_SIZE:b->float_info=am_invalid;return make_small(bits);
    default:break; /* infrastructure failure, never guest error preparation */
    }
    return value;
}
int lb_build_is_bitstring(Eterm value)
{
    LbBitstringView view; return lb_bitstring_view(value,&view);
}
