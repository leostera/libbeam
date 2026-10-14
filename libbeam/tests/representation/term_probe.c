/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Native probe retained from the representation checker, now built by Bazel.
 */
#ifdef LB_REFERENCE
#include "term_reference.h"
#else
#include "term.h"
#endif

#include <inttypes.h>
#include <stdio.h>
int main(void) {
    Eterm h[8], t, l;
    Sint values[] = {MIN_SMALL, MIN_SMALL+1, -1000, -1, 0, 1, 255, MAX_SMALL-1, MAX_SMALL};
    const Eterm headers[] = {ARITYVAL_SUBTAG, POS_BIG_SUBTAG, NEG_BIG_SUBTAG, REF_SUBTAG,
        FUN_SUBTAG, FLOAT_SUBTAG, RECORD_SUBTAG, HEAP_BITS_SUBTAG, SUB_BITS_SUBTAG,
        BIN_REF_SUBTAG, MAP_SUBTAG, EXTERNAL_PID_SUBTAG, EXTERNAL_PORT_SUBTAG, EXTERNAL_REF_SUBTAG};
    size_t i;
    for (i=0;i<sizeof(headers)/sizeof(headers[0]);++i) printf("header=%" PRIuPTR "\n", (uintptr_t)headers[i]);
    printf("word=%zu nil=%" PRIuPTR " atom0=%" PRIuPTR " atom1024=%" PRIuPTR " nonvalue=%" PRIuPTR "\n",
           sizeof(Eterm), (uintptr_t)NIL, (uintptr_t)make_atom(0), (uintptr_t)make_atom(1024), (uintptr_t)THE_NON_VALUE);
    for (i=0;i<sizeof(values)/sizeof(values[0]);++i)
        printf("small=%" PRIuPTR ":%" PRIdPTR "\n", (uintptr_t)make_small(values[i]), (intptr_t)signed_val(make_small(values[i])));
    t = TUPLE2(h, make_small(-7), make_atom(42));
    l = CONS(h+4,t,NIL);
    printf("tuple=%" PRIuPTR ":%" PRIuPTR ":%" PRIuPTR " list=%" PRIuPTR " literal=%d\n",
           (uintptr_t)h[0], (uintptr_t)arityval(h[0]), (uintptr_t)(t-(Uint)h),
           (uintptr_t)(l-(Uint)(h+4)), boxed_val(t|TAG_LITERAL_PTR)==h);
    printf("fields=%" PRIdPTR ":%" PRIuPTR ":%d:%" PRIuPTR "\n",
           (intptr_t)signed_val(h[1]), (uintptr_t)atom_val(h[2]), CAR(list_val(l))==t, (uintptr_t)CDR(list_val(l)));
    return 0;
}
