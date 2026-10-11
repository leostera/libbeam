/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#ifndef LIBBEAM_CORE_BINARY_BUILD_H
#define LIBBEAM_CORE_BINARY_BUILD_H
#include "binary_size.h"
#include "writable.h"
typedef enum {
    LB_BUILD_OK, LB_BUILD_GUEST_ERROR, LB_BUILD_INVALID, LB_BUILD_NO_MEMORY,
    LB_BUILD_LIMIT, LB_BUILD_REFUSED, LB_BUILD_ENVIRONMENT
} LbBinaryBuildStatus;
typedef struct {
    LbBitWriter writer;
    Eterm result,float_info;
    size_t expected;
    Uint cost;
    LbBinaryBuildStatus status;
    LbBinaryFailure failure;
} LbBinaryBuild;
/* One serialized, verified native instruction. No writer/temporary result may
 * survive collection: error preparation expires both and the caller must exit
 * the instruction. Success must finish exactly expected before publishing.
 * No callback or arbitrary source storage may retain an unrooted private alias.
 * These helpers confer no independent opcode admission. */
LbBinaryBuildStatus lb_binary_build_begin(LbProcess *,const Uint *,size_t count,size_t alloc,size_t live,LbBinaryBuild *);
LbBinaryBuildStatus lb_binary_build_raise(LbProcess *,LbBinaryBuild *);
LbBinaryBuildStatus lb_binary_build_error(LbProcess *,LbBinaryBuild *,const Uint *descriptor,Uint reason,Eterm info,Eterm value);
int lb_build_binary_all(LbBinaryBuild *,Eterm,unsigned);
int lb_build_binary(LbBinaryBuild *,Eterm,size_t);
int lb_build_integer_be(LbBinaryBuild *,Eterm,size_t);
int lb_build_integer_le(LbBinaryBuild *,Eterm,size_t);
int lb_build_utf8(LbBinaryBuild *,Eterm);
int lb_build_utf16(LbBinaryBuild *,Eterm,Uint);
void lb_build_string(LbBinaryBuild *,const unsigned char *,size_t);
Eterm lb_build_float(LbBinaryBuild *,Eterm,size_t,Uint);
int lb_build_is_bitstring(Eterm);
#endif
