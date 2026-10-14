/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#ifndef LIBBEAM_CORE_IOLIST_SIZE_H
#define LIBBEAM_CORE_IOLIST_SIZE_H
#include "process.h"
/* Each visited leaf costs at least one of a uint64_t budget's units and adds
 * at most 64 MiB. Two words therefore cover every size observable by an owned
 * invocation, including sizes exceeding a machine word. No binary is built. */
typedef struct { Uint low,high; } LbIolistSize;
int lb_iolist_size_add(LbIolistSize *,size_t bytes);
/* Scratch traversal must have ended before this potentially collecting step. */
Eterm lb_iolist_size_result(LbProcess *,LbIolistSize);
#endif
