/* SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 2017-2026. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Flat64 sizing phase of emu/bs_instrs.tab:i_bs_create_bin. The native kind
 * cases and error order are preserved; typed failures replace exception jumps.
 * Checked cumulative policy arithmetic replaces unchecked aggregate addition.
 * This helper does not admit a partial interpreter dispatch case.
 */
#include "binary_size.h"
#include "big.h"
#include "binary.h"
#include "code_internal.h"
static int operand(LbBinaryRegisters registers,Eterm encoded,Eterm *out)
{
    size_t index;
    switch(loader_tag(encoded)) {
    case LOADER_X_REG:
        index=loader_x_reg_index(encoded);
        if(index>=MAX_REG || index>=registers.x_count) return 0;
        *out=registers.x[index]; return 1;
    case LOADER_Y_REG:
        index=loader_y_reg_index(encoded);
        if(index<CP_SIZE || index>MAX_REG || index>=registers.y_count) return 0;
        *out=registers.y[index]; return 1;
    default:*out=encoded; return 1;
    }
}
static Eterm load(LbBinaryRegisters registers,Eterm encoded)
{
    Eterm value;
    if(!operand(registers,encoded,&value)) abort(); /* established by preflight */
    return value;
}
static int descriptors_valid(LbBinaryRegisters registers,const Uint *words,size_t count)
{
    Eterm ignored;
    if(!words || !count || count%5 || count>5*(65536/6) ||
       (!registers.x && registers.x_count) || (!registers.y && registers.y_count)) return 0;
    for(size_t i=0;i<count;i+=5) {
        const Uint *p=words+i;
        if(p[0]>LB_BSC_UTF32 || p[1]>256 || (p[2]>>3)>(Uint)MAX_SMALL ||
           ((p[2]&7)!=0 && (p[2]&7)!=2)) return 0;
        if(p[0]!=LB_BSC_STRING && !operand(registers,p[3],&ignored)) return 0;
        switch(p[0]) {
        case LB_BSC_APPEND:case LB_BSC_PRIVATE_APPEND:
            if(i || !p[1]) return 0;
            break;
        case LB_BSC_BINARY_ALL:if(!p[1]) return 0;break;
        case LB_BSC_BINARY:case LB_BSC_FLOAT:case LB_BSC_INTEGER:
            if(!p[1] || !operand(registers,p[4],&ignored)) return 0;
            break;
        case LB_BSC_BINARY_FIXED_SIZE:case LB_BSC_FLOAT_FIXED_SIZE:case LB_BSC_INTEGER_FIXED_SIZE:
            if(p[1] || p[4]>>56) return 0;
            break;
        case LB_BSC_STRING:if(p[1] || p[4]>SIZE_MAX/8) return 0;break;
        case LB_BSC_UTF32:if(p[4]!=32) return 0;break;
        case LB_BSC_UTF8:case LB_BSC_UTF16:break;
        default:abort();
        }
    }
    return 1;
}
static LbBinarySizeResult failure(const Uint *p,Eterm reason,Eterm info,Eterm value)
{
    return (LbBinarySizeResult){.status=LB_BINARY_SIZE_GUEST_ERROR,
        .as.failure={(LbBinarySegmentKind)p[0],p[2]>>3,reason,info,value}};
}
/* Native BS_SAFE_MUL, including its unsigned wrap/quotient fallback. Preflight
 * establishes the nonzero unit needed by the fallback division. */
static int multiply(Uint a,Uint b,Uint *result)
{
#ifdef HAVE_OVERFLOW_CHECK_BUILTINS
    return !__builtin_mul_overflow(a,b,result);
#else
    *result=a*b; return *result/b==a;
#endif
}
static void accumulate(Uint bits,Uint maximum,Uint *total,int *limited)
{
    if(*limited) return;
    if(bits>maximum-*total) *limited=1;
    else *total+=bits;
}
LbBinarySizeResult lb_binary_size(LbBinaryRegisters registers,const Uint *words,
                                  size_t count,size_t maximum)
{
    Uint num_bits=0; int limited=0;
    if(!descriptors_valid(registers,words,count))
        return (LbBinarySizeResult){.status=LB_BINARY_SIZE_INVALID};
    for(size_t i=0;i<count;i+=5) {
        const Uint *p=words+i; Eterm Src,Size; Uint bits=0;
        switch(p[0]) {
        case LB_BSC_APPEND:case LB_BSC_PRIVATE_APPEND:
            break;
        case LB_BSC_BINARY_ALL: {
            LbBitstringView view; Src=load(registers,p[3]);
            if(!lb_bitstring_view(Src,&view)) return failure(p,am_badarg,am_type,Src);
            bits=view.bit_size;
            break;
        }
        case LB_BSC_BINARY_FIXED_SIZE:case LB_BSC_FLOAT_FIXED_SIZE:case LB_BSC_INTEGER_FIXED_SIZE:
            bits=p[4]; break;
        case LB_BSC_STRING:
            bits=p[4]*8; break;
        case LB_BSC_BINARY:case LB_BSC_FLOAT:case LB_BSC_INTEGER:
            Size=load(registers,p[4]);
            if(is_small(Size)) {
                Sint signed_size=signed_val(Size);
                if(signed_size<0) return failure(p,am_badarg,am_size,Size);
                if(!multiply((Uint)signed_size,p[1],&bits) || bits>>63)
                    return failure(p,am_system_limit,am_size,Size);
            } else {
                /* Native flat64 requires a small even if a noncanonical big
                 * representation would happen to have a small magnitude. */
                if(lb_is_big(Size) && (*boxed_val(Size)&_TAG_HEADER_MASK)==POS_BIG_SUBTAG)
                    return failure(p,am_system_limit,am_size,Size);
                return failure(p,am_badarg,am_size,Size);
            }
            break;
        case LB_BSC_UTF8:
            /* Deliberately compare tagged words before validating the scalar.
             * An invalid source gives a throwaway estimate; the writer must
             * reject it after this complete sizing pass and allocation. */
            Src=load(registers,p[3]);
            if(Src<make_small(0x80UL)) bits=8;
            else if(Src<make_small(0x800UL)) bits=16;
            else if(Src<make_small(0x10000UL)) bits=24;
            else bits=32;
            break;
        case LB_BSC_UTF16:
            Src=load(registers,p[3]); bits=Src>=make_small(0x10000UL)?32:16;
            break;
        case LB_BSC_UTF32:
            Src=load(registers,p[3]);
            if(!is_small(Src) || Src>make_small(0x10FFFFUL) ||
               (make_small(0xD800UL)<=Src && Src<=make_small(0xDFFFUL)))
                return failure(p,am_badarg,am_type,Src);
            bits=32; break;
        default:abort();
        }
        /* Keep checking later descriptors after an infrastructure limit. A
         * native sizing error there precedes allocation/virtual-heap policy. */
        accumulate(bits,maximum,&num_bits,&limited);
    }
    if(limited) return (LbBinarySizeResult){.status=LB_BINARY_SIZE_LIMIT};
    return (LbBinarySizeResult){.status=LB_BINARY_SIZE_OK,.as.bits=num_bits};
}
