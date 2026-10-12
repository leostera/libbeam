/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#ifndef LIBBEAM_CORE_BINARY_VHEAP_H
#define LIBBEAM_CORE_BINARY_VHEAP_H
#include <stdint.h>
/* Native default after heap-size rounding. A soft GC trigger in words, not a
 * quota. The owned single-generation collector accounts retained capacity,
 * including writable spare capacity, rather than claiming native generations. */
#define LB_BINARY_VHEAP_MIN_WORDS UINT64_C(46422)
/* Conservative native work bound for next()'s full-width rounding and growth
 * loops over uint64_t thresholds (charged before moving-GC forwarding). */
#define LB_BINARY_VHEAP_NEXT_MAX_WORK UINT64_C(2048)
/* Native do_next_vheap_size/next_vheap_size under binary64 nearest/ties-even.
 * Pure integer implementation: no allocation, failure or host fenv changes,
 * including after the collector's first forwarding write. Sub-minimum current
 * thresholds normalize to the configured minimum. */
uint64_t lb_binary_vheap_next(uint64_t used,uint64_t current);
/* Native >= pressure boundary, without overflowing used+incoming. */
int lb_binary_vheap_pressure(uint64_t used,uint64_t incoming,uint64_t threshold);
#endif
