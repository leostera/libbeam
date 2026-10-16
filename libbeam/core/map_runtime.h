/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#ifndef LIBBEAM_CORE_MAP_RUNTIME_H
#define LIBBEAM_CORE_MAP_RUNTIME_H
#include "process.h"
/* Cap map-update operands at a bounded profile size. Maps beyond the native
 * flatmap limit may be admitted only for compile-time structure and refuse at
 * execution until HAMT construction is supported. */
#define LB_MAP_MAX_FIELDS 256u
typedef enum {
    LB_MAP_OK,
    LB_MAP_MISSING,
    LB_MAP_NO_MEMORY,
    LB_MAP_LIMIT,
    LB_MAP_WORK_LIMIT,
    LB_MAP_UNSUPPORTED,
    LB_MAP_INVALID
} LbMapStatus;
/* Reserve before reading movable operands into native scratch. The subsequent
 * constructors are allocation-free and consume only the words they publish. */
LbMapStatus lb_map_reserve(LbProcess *, size_t maximum_words, size_t live);
LbMapStatus lb_map_is_map(Eterm term, int *is_map);
LbMapStatus lb_map_reservation_words(Eterm map, size_t updates, int exact,
                                    size_t *words);
LbMapStatus lb_map_find(LbProcess *, Eterm map, Eterm key, Eterm *value);
/* Bounded order for loader-time flatmap validation and literal key sorting. */
LbMapStatus lb_map_compare_literals(Eterm left, Eterm right, int *order);
LbMapStatus lb_map_compare_keys(LbProcess *,Eterm,Eterm,int *);
LbMapStatus lb_map_create(LbProcess *, const Eterm *keys, const Eterm *values,
                          size_t count, Eterm *result);
LbMapStatus lb_map_create_literal(LbProcess *, Eterm keys, const Eterm *values,
                                  size_t count, Eterm *result);
LbMapStatus lb_map_update_assoc(LbProcess *, Eterm map, const Eterm *keys,
                                const Eterm *values, size_t count, Eterm *result);
LbMapStatus lb_map_update_exact(LbProcess *, Eterm map, const Eterm *keys,
                                const Eterm *values, size_t count, Eterm *result);
LbMapStatus lb_map_update_exact_missing(LbProcess *, Eterm map, const Eterm *keys,
                                        const Eterm *values, size_t count,
                                        Eterm *result, Eterm *missing_key);
#endif
