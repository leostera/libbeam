/* SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 2020-2026. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Adapted from beam_file.c: bounded cursors and compact tagged-number reader.
 */
#ifndef LIBBEAM_CORE_BEAM_READER_H
#define LIBBEAM_CORE_BEAM_READER_H
#include "opcodes.h"
typedef struct { const unsigned char *data; size_t size, pos; } LbBeamReader;
typedef struct { int tag; Sint word; const unsigned char *bytes; size_t size; } LbTaggedNumber;
int lb_reader_bytes(LbBeamReader *, size_t, const unsigned char **);
int lb_reader_u8(LbBeamReader *, unsigned *);
int lb_reader_u32(LbBeamReader *, uint32_t *);
int lb_reader_tagged(LbBeamReader *, LbTaggedNumber *);
#endif
