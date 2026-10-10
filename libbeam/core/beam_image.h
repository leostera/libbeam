/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#ifndef LIBBEAM_CORE_BEAM_IMAGE_H
#define LIBBEAM_CORE_BEAM_IMAGE_H
#include "alloc.h"
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define LB_BEAM_ID(a,b,c,d) (((uint32_t)(a)<<24)|((uint32_t)(b)<<16)|((uint32_t)(c)<<8)|(uint32_t)(d))
#define LB_BEAM_MAX_BYTES ((size_t)64 * 1024 * 1024)
typedef struct LbBeamImage LbBeamImage;
typedef struct { const unsigned char *data; size_t size; } LbBeamBytes;
typedef struct { uint32_t atom, arity, label; } LbBeamExport;
typedef struct { uint32_t module, function, arity; } LbBeamImport;
typedef struct {
    uint32_t atom_count, import_count, export_count;
    uint32_t max_opcode, label_count, function_count;
    LbBeamBytes code;
} LbBeamImageInfo;
typedef enum {
    LB_BEAM_OK, LB_BEAM_INVALID_ARGUMENT, LB_BEAM_BAD_FORMAT,
    LB_BEAM_UNSUPPORTED, LB_BEAM_LIMIT, LB_BEAM_NO_MEMORY, LB_BEAM_NOT_FOUND
} LbBeamStatus;
/* Structural metadata preparation, NOT executable code loading/validation.
 * Copies input. Valid output is NULL on failure; domain remains usable.
 * No I/O, atom interning, bytecode execution, worker creation or global state.
 * Caller serializes operations and keeps domain alive until image destruction.
 */
LbBeamStatus lb_beam_image_create(LbAllocDomain *, const void *, size_t, LbBeamImage **);
void lb_beam_image_destroy(LbBeamImage *);
/* All returned pointers/views borrow the image. Drop them before destroying it.
 * Atom numbers are file-local and one-based; atom 1 is the module name.
 * Import/export entry indices are zero-based. Unknown chunks remain accessible;
 * lookup returns the last occurrence, as upstream does. Required duplicates fail.
 */
const LbBeamImageInfo *lb_beam_image_info(const LbBeamImage *);
LbBeamStatus lb_beam_image_atom(const LbBeamImage *, uint32_t, LbBeamBytes *);
LbBeamStatus lb_beam_image_export(const LbBeamImage *, uint32_t, LbBeamExport *);
LbBeamStatus lb_beam_image_import(const LbBeamImage *, uint32_t, LbBeamImport *);
LbBeamStatus lb_beam_image_chunk(const LbBeamImage *, uint32_t, LbBeamBytes *);
#ifdef __cplusplus
}
#endif
#endif
