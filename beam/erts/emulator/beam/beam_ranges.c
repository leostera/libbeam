/*
 * %CopyrightBegin%
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Copyright Ericsson AB 2012-2025. All Rights Reserved.
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
#  include "config.h"
#endif

#include "sys.h"
#include "erl_vm.h"
#include "global.h"
#include "beam_code.h"
#include "erl_unicode.h"
#include "beam_ranges.h"

typedef struct {
    ErtsCodePtr start; /* Pointer to start of module. */
    erts_atomic_t end; /* Points one word beyond last function in module. */
} Range;


/* Range 'end' needs to be atomic as we purge module
    by setting end=start in active code_ix */
#define RANGE_END(R) ((ErtsCodePtr)erts_atomic_read_nob(&(R)->end))

static Range* find_range(ErtsRangeNamespace *, ErtsCodeIndex, ErtsCodePtr);
static void lookup_loc(FunctionInfo* fi, ErtsCodePtr pc,
                       const BeamCodeHeader*, int idx);

/*
 * The following variables keep a sorted list of address ranges for
 * each module.  It allows us to quickly find a function given an
 * instruction pointer.
 */
struct ranges {
    Range* modules;	       /* Sorted lists of module addresses. */
    Sint n;		       /* Number of range entries. */
    Sint allocated;	       /* Number of allocated entries. */
    erts_atomic_t mid;     /* Cached search start point */
};
struct ErtsRangeNamespace {
    struct ranges slots[ERTS_NUM_CODE_IX];
    erts_atomic_t mem_used;
    Range *write_ptr;
    ErtsCodeIndex source, destination;
    Sint insert_limit, inserted;
    int bound;
    ErtsLiteralArea **dump_areas;
    Uint dump_capacity;
};
static ErtsRangeNamespace *diagnostic_ranges;
#define NO_RANGE_STAGE (~(ErtsCodeIndex)0)

#ifdef HARD_DEBUG
static void check_consistency(struct ranges* p)
{
    int i;

    ASSERT(p->n <= p->allocated);
    ASSERT((Uint)(p->mid - p->modules) < p->n ||
	   (p->mid == p->modules && p->n == 0));
    for (i = 0; i < p->n; i++) {
	ASSERT(p->modules[i].start <= RANGE_END(&p->modules[i]));
	ASSERT(!i || RANGE_END(&p->modules[i-1]) < p->modules[i].start);
    }
}
#  define CHECK(r) check_consistency(r)
#else
#  define CHECK(r)
#endif /* HARD_DEBUG */

static int
rangecompare(Range* a, Range* b)
{
    if (a->start < b->start) {
	return -1;
    } else if (a->start == b->start) {
	return 0;
    } else {
	return 1;
    }
}

ErtsRangeNamespace *
erts_range_namespace_create(void)
{
    ErtsRangeNamespace *owner = erts_alloc(ERTS_ALC_T_MODULE_REFS, sizeof(*owner));
    struct ranges *r = owner->slots;
    Sint i;
    owner->bound = 0;
    owner->source = owner->destination = NO_RANGE_STAGE;
    owner->write_ptr = NULL;
    owner->insert_limit = owner->inserted = 0;
    erts_atomic_init_nob(&owner->mem_used, 0);
    for (i = 0; i < ERTS_NUM_CODE_IX; i++) {
	r[i].modules = 0;
	r[i].n = 0;
	r[i].allocated = 0;
	erts_atomic_init_nob(&r[i].mid, 0);
    }

    owner->dump_capacity = 8;
    owner->dump_areas = (ErtsLiteralArea **)
        erts_alloc(ERTS_ALC_T_CRASH_DUMP, owner->dump_capacity * sizeof(ErtsLiteralArea*));
    return owner;
}

static int
range_start(ErtsRangeNamespace *owner, ErtsCodeIndex src,
             ErtsCodeIndex dst, int num_new, int apply)
{
    struct ranges *r = owner->slots;
    Sint need;
    if (owner->destination != NO_RANGE_STAGE || src >= ERTS_NUM_CODE_IX ||
        dst >= ERTS_NUM_CODE_IX || src == dst || num_new < 0)
        return 1;
    if ((UWord)num_new > ((UWord)ERTS_SINT_MAX / sizeof(Range)) - (UWord)r[src].n)
        return 1;
    if (!apply) return 0;
    if (r[dst].modules) {
	erts_atomic_add_nob(&owner->mem_used, -r[dst].allocated);
	erts_free(ERTS_ALC_T_MODULE_REFS, r[dst].modules);
    }

    need = r[dst].allocated = r[src].n + num_new;
    erts_atomic_add_nob(&owner->mem_used, need);
    owner->write_ptr = erts_alloc(ERTS_ALC_T_MODULE_REFS, need * sizeof(Range));
    r[dst].modules = owner->write_ptr;
    r[dst].n = 0;
    owner->source = src;
    owner->destination = dst;
    owner->insert_limit = num_new;
    owner->inserted = 0;
    return 0;
}

int erts_range_namespace_check_staging(ErtsRangeNamespace *owner, ErtsCodeIndex src, ErtsCodeIndex dst, int count)
{
    return range_start(owner, src, dst, count, 0);
}
int erts_range_namespace_start_staging(ErtsRangeNamespace *owner, ErtsCodeIndex src, ErtsCodeIndex dst, int count)
{
    return range_start(owner, src, dst, count, 1);
}
int
erts_range_namespace_end_staging(ErtsRangeNamespace *owner, int commit)
{
    struct ranges *r = owner->slots;
    Range *write_ptr = owner->write_ptr;
    ErtsCodeIndex src = owner->source, dst = owner->destination;
    if (dst == NO_RANGE_STAGE) return 1;
    if (commit) {
	Sint i;
	Range* mp;
	Sint num_inserted;

	mp = r[dst].modules;
	num_inserted = write_ptr - mp;
	for (i = 0; i < r[src].n; i++) {
	    Range* rp = r[src].modules+i;
	    if (rp->start < RANGE_END(rp)) {
		/* Only insert a module that has not been purged. */
		write_ptr->start = rp->start;
		erts_atomic_init_nob(&write_ptr->end,
					 (erts_aint_t)(RANGE_END(rp)));
		write_ptr++;
	    }
	}

	/*
	 * There are num_inserted new range entries (unsorted) at the
	 * beginning of the modules array, followed by the old entries
	 * (sorted). We must now sort the entire array.
	 */

	r[dst].n = write_ptr - mp;
	if (num_inserted > 1) {
	    qsort(mp, r[dst].n, sizeof(Range),
		  (int (*)(const void *, const void *)) rangecompare);
	} else if (num_inserted == 1) {
	    /* Sift the new range into place. This is faster than qsort(). */
	    Range t = mp[0];
	    for (i = 0; i < r[dst].n-1 && t.start > mp[i+1].start; i++) {
		mp[i] = mp[i+1];
	    }
	    mp[i] = t;
	}
	r[dst].modules = mp;
	CHECK(&r[dst]);
	erts_atomic_set_nob(&r[dst].mid,
				(erts_aint_t) (r[dst].modules +
					       r[dst].n / 2));

        if (r[dst].allocated > owner->dump_capacity) {
            owner->dump_capacity = r[dst].allocated * 2;
            owner->dump_areas = (ErtsLiteralArea **)
                erts_realloc(ERTS_ALC_T_CRASH_DUMP, owner->dump_areas,
                             owner->dump_capacity * sizeof(ErtsLiteralArea*));
        }
    } else {
        r[dst].n = 0;
        erts_atomic_set_nob(&r[dst].mid, 0);
    }
    owner->source = owner->destination = NO_RANGE_STAGE;
    owner->write_ptr = NULL;
    return 0;
}

int
erts_range_namespace_update(ErtsRangeNamespace *owner, const BeamCodeHeader* code, Uint size)
{
    struct ranges *r = owner->slots;
    ErtsCodeIndex dst = owner->destination, src = owner->source;
    Range *write_ptr = owner->write_ptr;
    if (dst == NO_RANGE_STAGE || owner->inserted >= owner->insert_limit) return 1;

    if (src == dst) {
	ASSERT(!erts_initialized);

	/*
	 * During start-up of system, the indices are the same
	 * and erts_start_staging_ranges() has not been called.
	 */
	if (r[dst].modules == NULL) {
	    Sint need = 128;
	    erts_atomic_add_nob(&owner->mem_used, need);
	    r[dst].modules = erts_alloc(ERTS_ALC_T_MODULE_REFS,
					need * sizeof(Range));
	    r[dst].allocated = need;
	    write_ptr = r[dst].modules;
	}
    }

    ASSERT(r[dst].modules);
    write_ptr->start = code;
    erts_atomic_init_nob(&(write_ptr->end),
			     (erts_aint_t)(((byte *)code) + size));
    owner->write_ptr = write_ptr + 1;
    ++owner->inserted;
    return 0;
}

int
erts_range_namespace_remove(ErtsRangeNamespace *owner, ErtsCodeIndex ix, const BeamCodeHeader* code)
{
    Range* rp = find_range(owner, ix, code);
    if (!rp) return 1;
    erts_atomic_set_nob(&rp->end, (erts_aint_t)rp->start);
    return 0;
}

UWord
erts_range_namespace_size(ErtsRangeNamespace *owner)
{
    return erts_atomic_read_nob(&owner->mem_used) * sizeof(Range);
}

/*
 * Find a function from the given pc and fill information in
 * the FunctionInfo struct. If the full_info is non-zero, fill
 * in all available information (including location in the
 * source code). If no function is found, the 'current' field
 * will be set to NULL.
 */

void
erts_lookup_function_info(FunctionInfo* fi, ErtsCodePtr pc, int full_info)
{
    const ErtsCodeInfo * const *low;
    const ErtsCodeInfo * const *high;
    const ErtsCodeInfo * const *mid;
    const BeamCodeHeader *hdr;
    Range* rp;

    fi->mfa = NULL;
    fi->needed = 5;
    fi->loc = LINE_INVALID_LOCATION;
    rp = find_range(diagnostic_ranges, erts_active_code_ix(), pc);
    if (rp == 0) {
	return;
    }
    hdr = (BeamCodeHeader*) rp->start;

    low = hdr->functions;
    high = low + hdr->num_functions;
    while (low < high) {
	mid = low + (high-low) / 2;
	if (pc < (ErtsCodePtr)(mid[0])) {
	    high = mid;
	} else if (pc < (ErtsCodePtr)(mid[1])) {
	    fi->mfa = &mid[0]->mfa;
	    if (full_info) {
		const ErtsCodeInfo * const *fp = hdr->functions;
		int idx = mid - fp;
		lookup_loc(fi, pc, hdr, idx);
	    }
	    return;
	} else {
	    low = mid + 1;
	}
    }
}

/*
 * Force setting of the current function in a FunctionInfo
 * structure. No source code location will be associated with
 * the function.
 */
void
erts_set_current_function(FunctionInfo* fi, const ErtsCodeMFA* mfa)
{
    fi->mfa = mfa;
    fi->needed = 5;
    fi->loc = LINE_INVALID_LOCATION;
}

/*
 * Returns a pointer to {module, function, arity}, or NULL if not found.
 */
const ErtsCodeMFA*
erts_find_function_from_pc(ErtsCodePtr pc)
{
    FunctionInfo fi;

    erts_lookup_function_info(&fi, pc, 0);
    return fi.mfa;
}

static Range*
find_range(ErtsRangeNamespace *owner, ErtsCodeIndex active, ErtsCodePtr pc)
{
    struct ranges *r = owner->slots;
    Range *low, *high, *mid;
    if (active >= ERTS_NUM_CODE_IX || !r[active].n) return NULL;
    low = r[active].modules;
    high = low + r[active].n;
    mid = (Range *) erts_atomic_read_nob(&r[active].mid);

    CHECK(&r[active]);
    while (low < high) {
	if (pc < mid->start) {
	    high = mid;
	} else if (pc >= RANGE_END(mid)) {
	    low = mid + 1;
	} else {
	    erts_atomic_set_nob(&r[active].mid, (erts_aint_t) mid);
	    return mid;
	}
	mid = low + (high-low) / 2;
    }
    return 0;
}

static void
lookup_loc(FunctionInfo* fi, const void* pc,
           const BeamCodeHeader* code_hdr, int idx)
{
    const BeamCodeLineTab *lt = code_hdr->line_table;
    const void** low;
    const void** high;
    const void** mid;

    if (lt == NULL) {
	return;
    }

    fi->fname_ptr = lt->fname_ptr;
    low = lt->func_tab[idx];
    high = lt->func_tab[idx+1];
    while (high > low) {
	mid = low + (high-low) / 2;
	if (pc < mid[0]) {
	    high = mid;
	} else if (pc < mid[1]) {
	    int index = mid - lt->func_tab[0];

	    if (lt->loc_size == 2) {
		fi->loc = lt->loc_tab.p2[index];
	    } else {
		ASSERT(lt->loc_size == 4);
		fi->loc = lt->loc_tab.p4[index];
	    }
	    if (fi->loc == LINE_INVALID_LOCATION) {
		return;
	    }
	    fi->needed += 3+2+3+2;
	    return;
	} else {
	    low = mid + 1;
	}
    }
}

ErtsCodePtr
erts_find_next_code_for_line(const BeamCodeHeader* code_hdr,
                             unsigned int line,
                             unsigned int *start_from)
{
    const BeamCodeLineTab *lt = code_hdr->line_table;
    const UWord num_functions = code_hdr->num_functions;
    unsigned int line_index = -1;
    unsigned int num_lines;

    if (lt == NULL) {
	return NULL;
    }

    num_lines = lt->func_tab[num_functions] - lt->func_tab[0];

    /* NB. While at the moment lt->loc_tab is sorted (except at
     * the edges, since module_info/0,1, etc have no line info),
     * there's no strong guarantee this will be sorted in general,
     * as the compiler could reorder functions, code with no
     * dependencies, etc. So we do a linear-search here.
     */
    if (lt->loc_size == 2) {
        for(unsigned int i=*start_from; i<num_lines; i++) {
            int curr_line = LOC_LINE(lt->loc_tab.p2[i]);
            if (curr_line == line) {
                line_index = i;
                *start_from = i+1;
                break;
            }
        }
    } else {
        for(unsigned int i=*start_from; i<num_lines; i++) {
            int curr_line = LOC_LINE(lt->loc_tab.p4[i]);
            if (curr_line == line) {
                line_index = i;
                *start_from = i+1;
                break;
            }
        }
    }

    if (line_index == -1) {
        *start_from = 0;
        return NULL;
    }

    return lt->func_tab[0][line_index];
}

const BeamCodeHeader *erts_range_namespace_find(ErtsRangeNamespace *owner, ErtsCodeIndex ix, ErtsCodePtr pc)
{
    Range *range = find_range(owner, ix, pc);
    return range ? range->start : NULL;
}
int erts_range_namespace_can_discard(ErtsRangeNamespace *owner)
{
    if (!owner || owner->bound || owner->destination != NO_RANGE_STAGE) return 0;
    for (int ix = 0; ix < ERTS_NUM_CODE_IX; ++ix)
        for (Sint i = 0; i < owner->slots[ix].n; ++i) {
            Range *range = &owner->slots[ix].modules[i];
            if (range->start < RANGE_END(range)) return 0;
        }
    return 1;
}
int erts_range_namespace_discard(ErtsRangeNamespace *owner)
{
    if (!erts_range_namespace_can_discard(owner)) return 1;
    for (int ix = 0; ix < ERTS_NUM_CODE_IX; ++ix)
        if (owner->slots[ix].modules)
            erts_free(ERTS_ALC_T_MODULE_REFS, owner->slots[ix].modules);
    erts_free(ERTS_ALC_T_CRASH_DUMP, owner->dump_areas);
    erts_free(ERTS_ALC_T_MODULE_REFS, owner);
    return 0;
}
void erts_init_ranges(ErtsRangeNamespace *owner)
{
    ASSERT(owner && !diagnostic_ranges);
    diagnostic_ranges = owner;
    owner->bound = 1;
    owner->source = owner->destination = 0;
    owner->insert_limit = 128;
}
void erts_update_ranges(const BeamCodeHeader *code, Uint size)
{
    if (erts_range_namespace_update(diagnostic_ranges, code, size))
        erts_exit(ERTS_ABORT_EXIT, "Cannot insert diagnostic range\n");
}
void erts_remove_from_ranges(const BeamCodeHeader *code)
{
    if (erts_range_namespace_remove(diagnostic_ranges, erts_active_code_ix(), code))
        erts_exit(ERTS_ABORT_EXIT, "Cannot remove diagnostic range\n");
}
UWord erts_ranges_sz(void) { return erts_range_namespace_size(diagnostic_ranges); }
ErtsLiteralArea **erts_diagnostic_dump_literal_areas(void) { return diagnostic_ranges->dump_areas; }
Uint erts_diagnostic_dump_literal_capacity(void) { return diagnostic_ranges->dump_capacity; }
