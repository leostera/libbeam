/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Native probe retained from the representation checker, now built by Bazel.
 */
#ifdef LB_REFERENCE
#include "binary_reference.h"
#else
#include "binary.h"
typedef LbBinary B; typedef LbBinRef R; typedef LbSubBits S;
#define REF_HEADER LB_HEADER_BIN_REF
#define SUB_HEADER LB_HEADER_SUB_BITS
#define WRITABLE LB_BINARY_WRITABLE
#define ACTIVE LB_BINARY_ACTIVE_WRITER
#define MUTABLE LB_SUB_MUTABLE
#define VOLATILE LB_SUB_VOLATILE
#define LIMIT LB_ONHEAP_BINARY_LIMIT
#define WORDS(bits) lb_bitstring_heap_words(bits)

#endif

#include <stdio.h>
#include <inttypes.h>
int main(void) {
    const size_t sizes[]={0,1,7,8,511,512,513,520,65536*8};
    size_t i;
    printf("binary=%zu:%zu:%zu:%zu\n", offsetof(B,intern.flags),
           offsetof(B,intern.apparent_size),offsetof(B,intern.refc),offsetof(B,orig_bytes));
    printf("binref=%zu:%zu:%zu:%zu\n", sizeof(R),offsetof(R,thing_word),offsetof(R,val),offsetof(R,next));
    printf("subbits=%zu:%zu:%zu:%zu:%zu:%zu\n", sizeof(S),offsetof(S,thing_word),
           offsetof(S,base_flags),offsetof(S,start),offsetof(S,end),offsetof(S,orig));
    printf("headers=%" PRIuPTR ":%" PRIuPTR " threshold=%zu\n",(uintptr_t)REF_HEADER,(uintptr_t)SUB_HEADER,(size_t)LIMIT);
    printf("flags=%" PRIuPTR ":%" PRIuPTR ":%" PRIuPTR ":%" PRIuPTR "\n",
           (uintptr_t)WRITABLE,(uintptr_t)ACTIVE,(uintptr_t)MUTABLE,(uintptr_t)VOLATILE);
    for(i=0;i<sizeof(sizes)/sizeof(sizes[0]);++i) printf("words=%zu:%zu\n",sizes[i],(size_t)WORDS(sizes[i]));
    return 0;
}
