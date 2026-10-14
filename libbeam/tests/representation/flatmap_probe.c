/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Native probe retained from the representation checker, now built by Bazel.
 */
#ifdef LB_REFERENCE
#include "flatmap_reference.h"
#else
#include "flatmap.h"
#include "binary_error.h"
#include "flatmap_core_aliases.h"
typedef LbFlatMap M;
#define WORDS LB_MAP3_WORDS
#define BUILD lb_map3

#endif

#include <stdio.h>
#include <inttypes.h>
int main(void) {
    Eterm storage[10], result;
    const unsigned kinds[]={K_APPEND,K_PRIVATE,K_BINARY,K_BINARY_FIXED,K_ALL,
        K_FLOAT,K_FLOAT_FIXED,K_INTEGER,K_INTEGER_FIXED,K_STRING,K_UTF8,K_UTF16,K_UTF32};
    size_t i;
    result=BUILD(storage,make_small(1),make_small(11),make_small(2),make_small(12),make_small(3),make_small(13));
    printf("layout=%zu:%zu:%zu:%zu words=%u\n",sizeof(M),offsetof(M,thing_word),offsetof(M,size),offsetof(M,keys),(unsigned)WORDS);
    printf("map=%" PRIuPTR ":%" PRIuPTR ":%td keys=%" PRIuPTR " values=%" PRIuPTR ":%" PRIuPTR ":%" PRIuPTR "\n",
        storage[0],storage[1],boxed_val(storage[2])-storage,*boxed_val(storage[2]),storage[3],storage[4],storage[5]);
    printf("result=%td keys=%" PRIuPTR ":%" PRIuPTR ":%" PRIuPTR "\n",boxed_val(result)-storage,storage[7],storage[8],storage[9]);
    for(i=0;i<sizeof(kinds)/sizeof(kinds[0]);++i) printf("kind=%u\n",kinds[i]);
    return 0;
}
