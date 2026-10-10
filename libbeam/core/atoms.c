/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 1996-2025. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *     http://www.apache.org/licenses/LICENSE-2.0
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
/* atom.c hashpjw + hash.h Fibonacci mapping + hash.c separate chaining/rehash.
 * Explicit owner, fallible transactional growth and binding leases are libbeam
 * adaptations. Not the global atom table, and not a parallel mock identity map.
 */
#include "atoms.h"
#include "utf8.h"
#include <stdlib.h>
#include "lb_atoms_generated.inc"

typedef struct AtomEntry {
    struct AtomEntry *next, *pending;
    Uint hash;
    size_t index, length;
    unsigned char name[];
} AtomEntry;
struct LbAtomTable {
    LbAllocDomain *owner;
    size_t count, limit, capacity, slots, borrowers;
    AtomEntry **index, **bucket;
    LbAtomTransaction *transaction;
};
struct LbAtomTransaction {
    LbAtomTable *table;
    AtomEntry *pending, **stage, **index, **buckets;
    size_t added, capacity, slots;
};
struct LbAtomBinding {
    LbAtomTable *table;
    size_t count;
    Eterm terms[];
};
typedef LbBeamBytes (*NameSource)(const void *, size_t);
static void release_owned(LbAllocDomain *owner, void *p)
{
    if (p && lb_alloc_domain_release(owner, p) != LB_ALLOC_OK) abort();
}
static void *allocate(LbAtomTable *table, size_t count, size_t width)
{
    size_t size;
    void *result;
    if (!lb_size_mul(count, width, &size) || !size ||
        lb_alloc_domain_allocate(table->owner, size, &result) != LB_ALLOC_OK) return NULL;
    memset(result, 0, size);
    return result;
}
/* The original hash algorithm, including R16 Latin-1 compatibility. */
static Uint atom_hash(const unsigned char *p, size_t len)
{
    Uint h = 0, g;
    unsigned char v;
    while (len--) {
        v = *p++;
        if (len && (v & 0xFE) == 0xC2 && (*p & 0xC0) == 0x80) {
            v = (unsigned char)((v << 6) | (*p & 0x3F));
            p++; len--;
        }
        h = (h << 4) + v;
        if ((g = h & UINT32_C(0xf0000000))) { h ^= g >> 24; h ^= g; }
    }
    return h;
}
static size_t slot(size_t slots, Uint hash)
{
    unsigned shift = 64;
    size_t n = slots;
    /* Power-of-two buckets, Fibonacci mapping from upstream hash_get_slot. */
    assert(slots >= 2 && !(slots & (slots - 1)));
    while (n > 1) { --shift; n >>= 1; }
    hash ^= hash >> shift;
    return (UINT64_C(11400714819323198485) * hash) >> shift;
}
static AtomEntry *find(AtomEntry **buckets, size_t slots, LbBeamBytes name, Uint hash)
{
    AtomEntry *p;
    if (!slots) return NULL;
    for (p = buckets[slot(slots, hash)]; p; p = p->next)
        if (p->hash == hash && p->length == name.size &&
            (!name.size || !memcmp(p->name, name.data, name.size))) return p;
    return NULL;
}
size_t lb_atoms_predefined_count(void)
{
    return sizeof(lb_predefined_names) / sizeof(lb_predefined_names[0]) - 1;
}
static LbBeamBytes predefined(const void *unused, size_t i)
{
    (void)unused;
    return (LbBeamBytes){(const unsigned char *)lb_predefined_names[i], strlen(lb_predefined_names[i])};
}
static LbBeamBytes from_image(const void *image, size_t i)
{
    LbBeamBytes result;
    if (lb_beam_image_atom(image, (uint32_t)i + 1, &result) != LB_BEAM_OK) abort();
    return result;
}
static LbBeamBytes one_name(const void *name, size_t i)
{
    (void)i;
    return *(const LbBeamBytes *)name;
}
/* All fallible work is staged. Existing hash chains/count/index/backing remain
 * untouched until every new name and any replacement metadata are allocated.
 * Thus a failed binding cannot leak atom identities or capacity into its owner.
 */
static LbAtomStatus prepare_batch(LbAtomTable *t, size_t count, NameSource source,
                                  const void *context, Eterm *terms, LbAtomTransaction **out)
{
    LbAtomTransaction *tx;
    AtomEntry *pending = NULL, **stage = NULL, **new_index = NULL, **new_buckets = NULL;
    size_t stage_slots = 8, added = 0, capacity = t->capacity, slots = t->slots;
    size_t i, bound = count < t->limit - t->count ? count : t->limit - t->count;
    LbAtomStatus status = LB_ATOM_NO_MEMORY;
    *out = NULL;
    if (t->transaction) return LB_ATOM_BUSY;
    tx = allocate(t, 1, sizeof(*tx));
    if (!tx) return LB_ATOM_NO_MEMORY;
    tx->table = t;
    while (stage_slots < bound) {
        if (stage_slots > SIZE_MAX / 2 / sizeof(*stage)) { status = LB_ATOM_LIMIT; goto fail; }
        stage_slots *= 2;
    }
    for (i = 0; i < count; ++i) {
        LbBeamBytes name = source(context, i);
        AtomEntry *entry;
        Uint hash;
        size_t bytes;
        if (!lb_utf8_atom_validate(name.data, name.size)) { status = LB_ATOM_BAD_UTF8; goto fail; }
        hash = atom_hash(name.data, name.size);
        entry = find(t->bucket, t->slots, name, hash);
        if (!entry && stage) entry = find(stage, stage_slots, name, hash);
        if (!entry) {
            if (added == t->limit - t->count) { status = LB_ATOM_LIMIT; goto fail; }
            if (!stage && !(stage = allocate(t, stage_slots, sizeof(*stage)))) goto fail;
            if (!lb_size_add(sizeof(*entry), name.size, &bytes)) { status = LB_ATOM_LIMIT; goto fail; }
            entry = allocate(t, 1, bytes);
            if (!entry) goto fail;
            entry->index = t->count + added++;
            entry->hash = hash; entry->length = name.size;
            if (name.size) memcpy(entry->name, name.data, name.size);
            entry->pending = pending; pending = entry;
            entry->next = stage[slot(stage_slots, hash)];
            stage[slot(stage_slots, hash)] = entry;
        }
        if (terms) terms[i] = make_atom(entry->index);
    }
    if (!added) goto prepared;
    if (!capacity) capacity = 8;
    while (capacity < t->count + added) {
        if (capacity > SIZE_MAX / 2 / sizeof(*new_index)) { status = LB_ATOM_LIMIT; goto fail; }
        capacity *= 2;
    }
    if (capacity != t->capacity) {
        new_index = allocate(t, capacity, sizeof(*new_index));
        if (!new_index) goto fail;
        if (t->count) memcpy(new_index, t->index, t->count * sizeof(*new_index));
    }
    if (!slots) slots = 8;
    /* Upstream's 160% growth threshold; no shrinking while identities are live. */
    while (t->count + added > slots * 8 / 5) {
        if (slots > SIZE_MAX / 2 / sizeof(*new_buckets)) { status = LB_ATOM_LIMIT; goto fail; }
        slots *= 2;
    }
    if (slots != t->slots) {
        new_buckets = allocate(t, slots, sizeof(*new_buckets));
        if (!new_buckets) goto fail;
    }
prepared:
    tx->pending=pending; tx->stage=stage; tx->index=new_index; tx->buckets=new_buckets;
    tx->capacity=capacity; tx->slots=slots; tx->added=added;
    t->transaction=tx; *out=tx;
    return LB_ATOM_OK;
fail:
    release_owned(t->owner, new_buckets);
    release_owned(t->owner, new_index);
    while (pending) {
        AtomEntry *next = pending->pending;
        release_owned(t->owner, pending); pending = next;
    }
    release_owned(t->owner, stage);
    release_owned(t->owner, tx);
    return status;
}
void lb_atoms_abort(LbAtomTransaction *tx)
{
    LbAtomTable *t;
    if (!tx) return;
    t=tx->table;
    if (t->transaction!=tx) abort();
    release_owned(t->owner,tx->buckets); release_owned(t->owner,tx->index);
    while (tx->pending) {
        AtomEntry *e=tx->pending; tx->pending=e->pending; release_owned(t->owner,e);
    }
    release_owned(t->owner,tx->stage); t->transaction=NULL; release_owned(t->owner,tx);
}
void lb_atoms_commit(LbAtomTransaction *tx)
{
    LbAtomTable *t=tx->table;
    size_t i;
    if (t->transaction!=tx) abort();
    if (tx->index) {
        AtomEntry **old=t->index; t->index=tx->index; t->capacity=tx->capacity;
        release_owned(t->owner,old);
    }
    while (tx->pending) {
        AtomEntry *e=tx->pending; tx->pending=e->pending; e->pending=NULL;
        t->index[e->index]=e;
        if (!tx->buckets) {
            size_t ix=slot(t->slots,e->hash); e->next=t->bucket[ix]; t->bucket[ix]=e;
        }
    }
    t->count+=tx->added;
    if (tx->buckets) {
        for (i=0;i<t->count;++i) {
            AtomEntry *e=t->index[i]; size_t ix=slot(tx->slots,e->hash);
            e->next=tx->buckets[ix]; tx->buckets[ix]=e;
        }
        release_owned(t->owner,t->bucket); t->bucket=tx->buckets; t->slots=tx->slots;
    }
    release_owned(t->owner,tx->stage); t->transaction=NULL; release_owned(t->owner,tx);
}
static LbAtomStatus intern_batch(LbAtomTable *t,size_t count,NameSource source,const void *context,Eterm *terms)
{
    LbAtomTransaction *tx;
    LbAtomStatus status=prepare_batch(t,count,source,context,terms,&tx);
    if (status==LB_ATOM_OK) lb_atoms_commit(tx);
    return status;
}
LbAtomStatus lb_atoms_transaction_name(const LbAtomTransaction *tx,Eterm term,LbBeamBytes *out)
{
    AtomEntry *e;
    if (!out) return LB_ATOM_INVALID;
    *out=(LbBeamBytes){0};
    if (!tx || !is_atom(term)) return LB_ATOM_INVALID;
    if (atom_val(term)<tx->table->count) return lb_atoms_name(tx->table,term,out);
    for (e=tx->pending;e;e=e->pending) if (e->index==atom_val(term)) {
        *out=(LbBeamBytes){e->name,e->length}; return LB_ATOM_OK;
    }
    return LB_ATOM_NOT_FOUND;
}
LbAtomStatus lb_atoms_create(LbAllocDomain *domain, size_t limit, LbAtomTable **out)
{
    LbAtomTable *t;
    void *memory;
    LbAtomStatus status;
    if (!out) return LB_ATOM_INVALID;
    *out = NULL;
    if (!domain || limit < lb_atoms_predefined_count() || limit > (UINTPTR_MAX >> _TAG_IMMED2_SIZE) ||
        limit > SIZE_MAX / sizeof(AtomEntry *)) return LB_ATOM_INVALID;
    if (lb_alloc_domain_allocate(domain, sizeof(*t), &memory) != LB_ALLOC_OK) return LB_ATOM_NO_MEMORY;
    t = memory; memset(t, 0, sizeof(*t)); t->owner = domain; t->limit = limit;
    status = intern_batch(t, lb_atoms_predefined_count(), predefined, NULL, NULL);
    if (status != LB_ATOM_OK) { lb_atoms_destroy(t); return status; }
    if (t->count != lb_atoms_predefined_count()) abort();
    *out = t;
    return LB_ATOM_OK;
}
LbAtomStatus lb_atoms_destroy(LbAtomTable *t)
{
    size_t i;
    if (!t) return LB_ATOM_INVALID;
    if (t->borrowers || t->transaction) return LB_ATOM_BUSY;
    for (i = t->count; i; --i) release_owned(t->owner, t->index[i - 1]);
    release_owned(t->owner, t->index);
    release_owned(t->owner, t->bucket);
    release_owned(t->owner, t);
    return LB_ATOM_OK;
}
size_t lb_atoms_count(const LbAtomTable *t) { return t ? t->count : 0; }
LbAtomStatus lb_atoms_intern(LbAtomTable *t, const void *data, size_t size, Eterm *out)
{
    LbBeamBytes name = {data, size};
    Eterm term;
    LbAtomStatus status;
    if (!out) return LB_ATOM_INVALID;
    *out = THE_NON_VALUE;
    if (!t || (!data && size)) return LB_ATOM_INVALID;
    status = intern_batch(t, 1, one_name, &name, &term);
    if (status == LB_ATOM_OK) *out = term;
    return status;
}
static LbBeamBytes name_array(const void *names, size_t i)
{
    return ((const LbBeamBytes *)names)[i];
}
LbAtomStatus lb_atoms_prepare_names(LbAtomTable *t,const LbBeamBytes *names,size_t count,Eterm *terms,LbAtomTransaction **out)
{
    if (!out) return LB_ATOM_INVALID;
    *out=NULL;
    if (!t || (count && (!names || !terms))) return LB_ATOM_INVALID;
    return prepare_batch(t,count,name_array,names,terms,out);
}
LbAtomStatus lb_atoms_intern_names(LbAtomTable *t, const LbBeamBytes *names, size_t count, Eterm *out)
{
    if (!t || (count && (!names || !out))) return LB_ATOM_INVALID;
    return intern_batch(t, count, name_array, names, out);
}
LbAtomStatus lb_atoms_find(const LbAtomTable *t, const void *data, size_t size, Eterm *out)
{
    LbBeamBytes name = {data, size};
    AtomEntry *entry;
    if (!out) return LB_ATOM_INVALID;
    *out = THE_NON_VALUE;
    if (!t || (!data && size)) return LB_ATOM_INVALID;
    if (!lb_utf8_atom_validate(data, size)) return LB_ATOM_BAD_UTF8;
    entry = find(t->bucket, t->slots, name, atom_hash(data, size));
    if (!entry) return LB_ATOM_NOT_FOUND;
    *out = make_atom(entry->index);
    return LB_ATOM_OK;
}
LbAtomStatus lb_atoms_name(const LbAtomTable *t, Eterm term, LbBeamBytes *out)
{
    AtomEntry *entry;
    if (!out) return LB_ATOM_INVALID;
    *out = (LbBeamBytes){0};
    if (!t || !is_atom(term)) return LB_ATOM_INVALID;
    if (atom_val(term) >= t->count) return LB_ATOM_NOT_FOUND;
    entry = t->index[atom_val(term)];
    *out = (LbBeamBytes){entry->name, entry->length};
    return LB_ATOM_OK;
}
LbAtomStatus lb_atoms_retain(LbAtomTable *t)
{
    if (!t) return LB_ATOM_INVALID;
    if (t->borrowers == SIZE_MAX) return LB_ATOM_LIMIT;
    ++t->borrowers; return LB_ATOM_OK;
}
LbAtomStatus lb_atoms_release(LbAtomTable *t)
{
    if (!t || !t->borrowers) return LB_ATOM_INVALID;
    --t->borrowers; return LB_ATOM_OK;
}
LbAtomStatus lb_atom_binding_create(LbAtomTable *t, const LbBeamImage *image, LbAtomBinding **out)
{
    const LbBeamImageInfo *info = lb_beam_image_info(image);
    LbAtomBinding *binding;
    LbAtomStatus status;
    size_t bytes;
    if (!out) return LB_ATOM_INVALID;
    *out = NULL;
    if (!t || !info) return LB_ATOM_INVALID;
    if (t->borrowers == SIZE_MAX || !lb_size_mul((size_t)info->atom_count + 1, sizeof(Eterm), &bytes) ||
        !lb_size_add(sizeof(*binding), bytes, &bytes)) return LB_ATOM_LIMIT;
    binding = allocate(t, 1, bytes);
    if (!binding) return LB_ATOM_NO_MEMORY;
    binding->table = t; binding->count = (size_t)info->atom_count + 1; binding->terms[0] = NIL;
    status = intern_batch(t, info->atom_count, from_image, image, binding->terms + 1);
    if (status != LB_ATOM_OK) { release_owned(t->owner, binding); return status; }
    ++t->borrowers; /* limit checked before transaction; serialized caller */
    *out = binding;
    return LB_ATOM_OK;
}
LbAtomStatus lb_atom_binding_get(const LbAtomBinding *binding, const LbAtomTable *owner,
                                 size_t index, Eterm *out)
{
    if (!out) return LB_ATOM_INVALID;
    *out = THE_NON_VALUE;
    if (!binding || !owner || binding->table != owner) return LB_ATOM_INVALID;
    if (index >= binding->count) return LB_ATOM_NOT_FOUND;
    *out = binding->terms[index];
    return LB_ATOM_OK;
}
void lb_atom_binding_destroy(LbAtomBinding *binding)
{
    LbAtomTable *t;
    if (!binding) return;
    t = binding->table;
    release_owned(t->owner, binding);
    if (lb_atoms_release(t) != LB_ATOM_OK) abort();
}
