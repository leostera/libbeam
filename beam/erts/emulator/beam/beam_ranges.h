/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#ifndef BEAM_RANGES_H__
#define BEAM_RANGES_H__
#include "code_ix.h"
#include "beam_code.h"
typedef struct ErtsRangeNamespace ErtsRangeNamespace;
ErtsRangeNamespace *erts_range_namespace_create(void);
int erts_range_namespace_check_staging(ErtsRangeNamespace *, ErtsCodeIndex, ErtsCodeIndex, int);
int erts_range_namespace_start_staging(ErtsRangeNamespace *, ErtsCodeIndex, ErtsCodeIndex, int);
int erts_range_namespace_end_staging(ErtsRangeNamespace *, int commit);
int erts_range_namespace_update(ErtsRangeNamespace *, const BeamCodeHeader *, Uint);
int erts_range_namespace_remove(ErtsRangeNamespace *, ErtsCodeIndex, const BeamCodeHeader *);
const BeamCodeHeader *erts_range_namespace_find(ErtsRangeNamespace *, ErtsCodeIndex, ErtsCodePtr);
UWord erts_range_namespace_size(ErtsRangeNamespace *);
int erts_range_namespace_can_discard(ErtsRangeNamespace *);
int erts_range_namespace_discard(ErtsRangeNamespace *);
#endif
