/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#ifndef LIBBEAM_CORE_BIT_WRITER_INTERNAL_H
#define LIBBEAM_CORE_BIT_WRITER_INTERNAL_H
#include "bit_writer.h"
int lb_bit_writer_valid(const LbBitWriter *);
int lb_bit_order_valid(LbBitEndian);
/* Writer validity is a precondition. Valid declared spans, not pointer validation. */
int lb_bit_writer_overlaps_source(const LbBitWriter *,const void *,size_t);
#endif
