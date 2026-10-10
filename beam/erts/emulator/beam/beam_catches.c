/*
 * %CopyrightBegin%
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Copyright Ericsson AB 2000-2025. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * %CopyrightEnd%
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "sys.h"
#include "beam_catches.h"
#include "global.h"

/* R14B04 has about 380 catches when starting erlang */
#define DEFAULT_TABSIZE (1024)
typedef struct {
    ErtsCodePtr cp;
    unsigned cdr;
} beam_catch_t;

#ifdef DEBUG
#  define IF_DEBUG(x) x
#else
#  define IF_DEBUG(x)
#endif

struct bc_pool {
    int free_list;
    unsigned high_mark;
    unsigned tabsize;
    beam_catch_t *beam_catches;
    /* 
     * Note that the 'beam_catches' area is shared by pools. Used slots
     * are readonly as long as the module is not purgable. The free-list is
     * protected by the code_ix lock.
     */

    int is_staging;
};

struct ErtsCatchNamespace {
    struct bc_pool pools[ERTS_NUM_CODE_IX];
    int bound;
};
static ErtsCatchNamespace *diagnostic_catches;

ErtsCatchNamespace *erts_catch_namespace_create(void)
{
    ErtsCatchNamespace *owner = erts_alloc(ERTS_ALC_T_CATCHES, sizeof(*owner));
    struct bc_pool *bccix = owner->pools;
    int i;
    owner->bound = 0;

    bccix[0].tabsize   = DEFAULT_TABSIZE;
    bccix[0].free_list = -1;
    bccix[0].high_mark = 0;
    bccix[0].beam_catches = erts_alloc(ERTS_ALC_T_CATCHES,
				     sizeof(beam_catch_t)*DEFAULT_TABSIZE);
    bccix[0].is_staging = 0;
    for (i=1; i<ERTS_NUM_CODE_IX; i++) {
	bccix[i] = bccix[i-1];
    }
    return owner;
}

void beam_catches_init(ErtsCatchNamespace *owner)
{
    ASSERT(owner && !diagnostic_catches);
    diagnostic_catches = owner;
    owner->bound = 1;
    owner->pools[0].is_staging = 1;
}

static void gc_old_vec(ErtsCatchNamespace *owner, beam_catch_t* vec)
{
    struct bc_pool *bccix = owner->pools;
    int i;
    for (i=0; i<ERTS_NUM_CODE_IX; i++) {
	if (bccix[i].beam_catches == vec) {
	    return;
	}
    }
    erts_free(ERTS_ALC_T_CATCHES, vec);
}


static int catch_start(ErtsCatchNamespace *owner, ErtsCodeIndex src, ErtsCodeIndex dst, int apply)
{
    struct bc_pool *bccix = owner->pools;
    beam_catch_t *prev_vec;
    if (src >= ERTS_NUM_CODE_IX || dst >= ERTS_NUM_CODE_IX || src == dst)
        return 1;
    for (int i = 0; i < ERTS_NUM_CODE_IX; ++i)
        if (bccix[i].is_staging) return 1;
    if (!apply) return 0;
    prev_vec = bccix[dst].beam_catches;
    bccix[dst] = bccix[src];
    gc_old_vec(owner, prev_vec);
    bccix[dst].is_staging = 1;
    return 0;
}

int erts_catch_namespace_check_staging(ErtsCatchNamespace *owner, ErtsCodeIndex src, ErtsCodeIndex dst)
{
    return catch_start(owner, src, dst, 0);
}
int erts_catch_namespace_start_staging(ErtsCatchNamespace *owner, ErtsCodeIndex src, ErtsCodeIndex dst)
{
    return catch_start(owner, src, dst, 1);
}
int erts_catch_namespace_end_staging(ErtsCatchNamespace *owner, ErtsCodeIndex dst)
{
    if (dst >= ERTS_NUM_CODE_IX || !owner->pools[dst].is_staging) return 1;
    owner->pools[dst].is_staging = 0;
    return 0;
}

unsigned erts_catch_namespace_cons(ErtsCatchNamespace *owner, ErtsCodeIndex ix,
                                   ErtsCodePtr cp, unsigned cdr, ErtsCodePtr **cppp)
{
    int i;
    struct bc_pool *p;
    if (ix >= ERTS_NUM_CODE_IX || !owner->pools[ix].is_staging) return (unsigned)-1;
    p = &owner->pools[ix];

    ASSERT(p->is_staging);
    /*
     * Allocate from free_list while it is non-empty.
     * If free_list is empty, allocate at high_mark.
     */
    if (p->free_list >= 0) {
	i = p->free_list;
	p->free_list = p->beam_catches[i].cdr;
    }
    else {
	if (p->high_mark >= p->tabsize) {
	    /* No free slots and table is full: realloc table */
	    beam_catch_t* prev_vec = p->beam_catches;
	    unsigned newsize = p->tabsize*2;

	    p->beam_catches = erts_alloc(ERTS_ALC_T_CATCHES,
					 newsize*sizeof(beam_catch_t));
	    sys_memcpy(p->beam_catches, prev_vec,
		       p->tabsize*sizeof(beam_catch_t));
	    gc_old_vec(owner, prev_vec);
	    p->tabsize = newsize;
	}
	i = p->high_mark++;
    }

    p->beam_catches[i].cp  = cp;
    p->beam_catches[i].cdr = cdr;
    if (cppp) {
        *cppp = &p->beam_catches[i].cp;
    }

    return i;
}

ErtsCodePtr erts_catch_namespace_car(ErtsCatchNamespace *owner, ErtsCodeIndex ix, unsigned i)
{
    struct bc_pool *p;
    if (ix >= ERTS_NUM_CODE_IX) return NULL;
    p = &owner->pools[ix];

    if (i >= p->high_mark ) {
	erts_exit(ERTS_ERROR_EXIT, "beam_catches_delmod: index %#x is out of range\r\n", i);
    }
    return p->beam_catches[i].cp;
}

void erts_catch_namespace_delmod(ErtsCatchNamespace *owner, unsigned head,
                         const BeamCodeHeader *hdr,
                         unsigned code_bytes,
                         ErtsCodeIndex code_ix)
{
    struct bc_pool* p = &owner->pools[code_ix];
    const char *code_start;
    unsigned i, cdr;

    code_start = (const char*)hdr;

    for(i = head; i != (unsigned)-1;) {
        const char *catch_addr;

        if (i >= p->high_mark) {
            erts_exit(ERTS_ERROR_EXIT,
                      "beam_catches_delmod: index %#x is out of range\r\n", i);
        }

        catch_addr = (char*)p->beam_catches[i].cp;
        if (!ErtsInArea(catch_addr, code_start, code_bytes)) {
            erts_exit(ERTS_ERROR_EXIT,
                      "beam_catches_delmod: item %#x has cp %p which is not "
                      "in module's range [%p,%p[\r\n",
                      i,
                      p->beam_catches[i].cp,
                      code_start,
                      &code_start[code_bytes]);
        }

        p->beam_catches[i].cp = NULL;
        cdr = p->beam_catches[i].cdr;
        p->beam_catches[i].cdr = p->free_list;
        p->free_list = i;
        i = cdr;
    }
}

int erts_catch_namespace_can_discard(ErtsCatchNamespace *owner)
{
    if (!owner || owner->bound) return 0;
    for (int ix = 0; ix < ERTS_NUM_CODE_IX; ++ix) {
        struct bc_pool *p = &owner->pools[ix];
        if (p->is_staging) return 0;
        for (unsigned i = 0; i < p->high_mark; ++i)
            if (p->beam_catches[i].cp) return 0;
    }
    return 1;
}
int erts_catch_namespace_discard(ErtsCatchNamespace *owner)
{
    if (!erts_catch_namespace_can_discard(owner)) return 1;
    for (int ix = 0; ix < ERTS_NUM_CODE_IX; ++ix) {
        beam_catch_t *vec = owner->pools[ix].beam_catches;
        if (!vec) continue;
        for (int j = ix + 1; j < ERTS_NUM_CODE_IX; ++j)
            if (owner->pools[j].beam_catches == vec) owner->pools[j].beam_catches = NULL;
        erts_free(ERTS_ALC_T_CATCHES, vec);
    }
    erts_free(ERTS_ALC_T_CATCHES, owner);
    return 0;
}
unsigned beam_catches_cons(ErtsCodePtr cp, unsigned cdr, ErtsCodePtr **cppp)
{
    return erts_catch_namespace_cons(diagnostic_catches, erts_staging_code_ix(), cp, cdr, cppp);
}
ErtsCodePtr beam_catches_car(unsigned i)
{
    return erts_catch_namespace_car(diagnostic_catches, erts_active_code_ix(), i);
}
ErtsCodePtr beam_catches_car_staging(unsigned i)
{
    return erts_catch_namespace_car(diagnostic_catches, erts_staging_code_ix(), i);
}
void beam_catches_delmod(unsigned head, const BeamCodeHeader *hdr, unsigned size, ErtsCodeIndex ix)
{
    ASSERT((ix == erts_active_code_ix()) != diagnostic_catches->pools[erts_staging_code_ix()].is_staging);
    erts_catch_namespace_delmod(diagnostic_catches, head, hdr, size, ix);
}
