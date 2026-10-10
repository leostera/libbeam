/* SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 1996-2026. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Owned fun/record storage extracted from the former erl_code_staged.h.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "sys.h"
#include "erl_vm.h"
#include "global.h"
#include "erl_code_table.h"
#define NO_STAGE (~(ErtsCodeIndex)0)
typedef struct CodeBlob CodeBlob;
typedef struct {
    IndexSlot slot;
    ErtsCodeTable *owner;
    void *object;
    CodeBlob *blob;
} CodeEntry;
struct CodeBlob {
    CodeEntry entries[ERTS_NUM_CODE_IX];
    UWord object[];
};
struct ErtsCodeTable {
    IndexTable tables[ERTS_NUM_CODE_IX];
    erts_rwmtx_t lock;
    erts_atomic_t bytes;
    ErtsCodeTableOps ops;
    size_t object_size;
    ErtsAlcType_t object_type, table_type;
    int limit, bound;
    ErtsCodeIndex staging;
};
static HashValue table_hash(CodeEntry *entry) { return entry->owner->ops.hash(entry->object); }
static int table_compare(CodeEntry *a, CodeEntry *b)
{
    ASSERT(a->owner == b->owner);
    return a->owner->ops.compare(a->object, b->object);
}
static CodeEntry *table_alloc(CodeEntry *key)
{
    CodeBlob *blob = key->blob;
    ErtsCodeTable *owner = key->owner;
    if (key->slot.index == -1) {
        size_t bytes = sizeof(*blob) + owner->object_size;
        blob = erts_alloc(owner->object_type, bytes);
        owner->ops.initialize(blob->object, key->object);
        erts_atomic_add_nob(&owner->bytes, bytes);
        for (int i = 0; i < ERTS_NUM_CODE_IX; ++i) {
            blob->entries[i].slot.index = -1;
            blob->entries[i].owner = owner;
            blob->entries[i].object = blob->object;
            blob->entries[i].blob = blob;
        }
    }
    for (int i = 0; i < ERTS_NUM_CODE_IX; ++i)
        if (blob->entries[i].slot.index < 0)
            return &blob->entries[i];
    erts_exit(ERTS_ABORT_EXIT, "No free code-table slot\n");
    return NULL;
}
static void table_free(CodeEntry *entry)
{
    CodeBlob *blob = entry->blob;
    ErtsCodeTable *owner = entry->owner;
    entry->slot.index = -1;
    for (int i = 0; i < ERTS_NUM_CODE_IX; ++i)
        if (blob->entries[i].slot.index >= 0)
            return;
    erts_atomic_add_nob(&owner->bytes, -(erts_aint_t)(sizeof(*blob) + owner->object_size));
    erts_free(owner->object_type, blob);
}
ErtsCodeTable *erts_code_table_create(size_t object_size, int initial, int limit,
                                     ErtsAlcType_t object_type, ErtsAlcType_t table_type,
                                     const char *lock_name, ErtsCodeTableOps ops)
{
    ErtsCodeTable *owner;
    HashFunctions f;
    erts_rwmtx_opt_t opt = ERTS_RWMTX_OPT_DEFAULT_INITER;
    if (!object_size || object_size > SIZE_MAX - sizeof(CodeBlob) || initial <= 0 || limit <= 0 ||
        !ops.hash || !ops.compare || !ops.initialize || !ops.stage || !ops.unpublished)
        return NULL;
    owner = erts_alloc(table_type, sizeof(*owner));
    owner->object_size = object_size;
    owner->object_type = object_type;
    owner->table_type = table_type;
    owner->ops = ops;
    owner->limit = limit;
    owner->bound = 0;
    owner->staging = NO_STAGE;
    erts_atomic_init_nob(&owner->bytes, 0);
    opt.type = ERTS_RWMTX_TYPE_FREQUENT_READ;
    opt.lived = ERTS_RWMTX_LONG_LIVED;
    /* Legacy ERTS name parameters are char *, but retain rather than mutate
     * these static-lifetime names. */
    erts_rwmtx_init_opt(&owner->lock, &opt, (char *)lock_name, NIL, ERTS_LOCK_FLAGS_CATEGORY_GENERIC);
    f.hash = (H_FUN)table_hash;
    f.cmp = (HCMP_FUN)table_compare;
    f.alloc = (HALLOC_FUN)table_alloc;
    f.free = (HFREE_FUN)table_free;
    f.meta_alloc = (HMALLOC_FUN)erts_alloc;
    f.meta_free = (HMFREE_FUN)erts_free;
    f.meta_print = (HMPRINT_FUN)erts_print;
    for (int i = 0; i < ERTS_NUM_CODE_IX; ++i)
        erts_index_init(table_type, &owner->tables[i], (char *)lock_name, initial, limit, f);
    return owner;
}
void erts_code_table_bind(ErtsCodeTable *owner)
{
    ASSERT(!owner->bound);
    owner->bound = 1;
    owner->staging = 0; /* Diagnostic single-threaded preloading. */
}
void erts_code_table_write_lock(ErtsCodeTable *owner) { erts_rwmtx_rwlock(&owner->lock); }
void erts_code_table_write_unlock(ErtsCodeTable *owner) { erts_rwmtx_rwunlock(&owner->lock); }
static CodeEntry make_key(ErtsCodeTable *owner, const void *object)
{
    CodeEntry key;
    key.slot.index = -1;
    key.owner = owner;
    key.object = (void *)object;
    key.blob = NULL;
    return key;
}
void *erts_code_table_get(ErtsCodeTable *owner, const void *object, ErtsCodeIndex ix)
{
    CodeEntry key = make_key(owner, object), *entry;
    if (ix >= ERTS_NUM_CODE_IX) return NULL;
    /* Active tables are read without a lock under the code-index protocol.
     * Private callers must serialize mutation and keep the owner alive. */
    entry = hash_get(&owner->tables[ix].htable, &key);
    return entry ? entry->object : NULL;
}
static void *put_locked(ErtsCodeTable *owner, const void *object, ErtsCodeIndex ix)
{
    CodeEntry key = make_key(owner, object);
    void *found = erts_code_table_get(owner, object, ix);
    if (found) return found;
    if (owner->tables[ix].entries >= owner->limit) return NULL;
    return ((CodeEntry *)index_put_entry(&owner->tables[ix], &key))->object;
}
void *erts_code_table_put(ErtsCodeTable *owner, const void *object, ErtsCodeIndex ix)
{
    void *result;
    if (ix >= ERTS_NUM_CODE_IX) return NULL;
    erts_code_table_write_lock(owner);
    result = put_locked(owner, object, ix);
    erts_code_table_write_unlock(owner);
    return result;
}
void *erts_code_table_diagnostic_upsert(ErtsCodeTable *owner, const void *object)
{
    ASSERT(owner->bound);
    for (;;) {
        ErtsCodeIndex active = erts_active_code_ix();
        void *result = erts_code_table_get(owner, object, active);
        if (result) return result;
        erts_rwmtx_rlock(&owner->lock);
        if (active != erts_active_code_ix()) {
            erts_rwmtx_runlock(&owner->lock);
            continue;
        }
        result = erts_code_table_get(owner, object, erts_staging_code_ix());
        erts_rwmtx_runlock(&owner->lock);
        if (result) return result;
        erts_code_table_write_lock(owner);
        if (active == erts_active_code_ix()) {
            result = put_locked(owner, object, erts_staging_code_ix());
            erts_code_table_write_unlock(owner);
            if (!result) erts_exit(ERTS_DUMP_EXIT, "Code table full\n");
            return result;
        }
        erts_code_table_write_unlock(owner);
    }
}
static int in_table(ErtsCodeTable *owner, CodeEntry *entry, ErtsCodeIndex ix)
{
    for (int j = 0; j < ERTS_NUM_CODE_IX; ++j) {
        int index = entry->blob->entries[j].slot.index;
        if (index >= 0 && index < owner->tables[ix].entries &&
            ((CodeEntry *)erts_index_lookup(&owner->tables[ix], index))->object == entry->object)
            return 1;
    }
    return 0;
}
static int start_staging(ErtsCodeTable *owner, ErtsCodeIndex src, ErtsCodeIndex dst, int apply)
{
    int missing = 0;
    if (src >= ERTS_NUM_CODE_IX || dst >= ERTS_NUM_CODE_IX || src == dst) return 1;
    erts_code_table_write_lock(owner);
    if (owner->staging != NO_STAGE) goto refuse;
    for (int i = 0; i < owner->tables[src].entries; ++i) {
        CodeEntry *entry = (CodeEntry *)erts_index_lookup(&owner->tables[src], i);
        if (owner->ops.stage_ready && !owner->ops.stage_ready(entry->object)) goto refuse;
        if (!in_table(owner, entry, dst)) {
            if (erts_code_table_get(owner, entry->object, dst)) goto refuse;
            ++missing;
        }
    }
    if (missing > owner->limit - owner->tables[dst].entries) goto refuse;
    if (!apply) {
        erts_code_table_write_unlock(owner);
        return 0;
    }
    for (int i = 0; i < owner->tables[src].entries; ++i) {
        CodeEntry *entry = (CodeEntry *)erts_index_lookup(&owner->tables[src], i);
        owner->ops.stage(entry->object, src, dst);
        if (!in_table(owner, entry, dst)) index_put_entry(&owner->tables[dst], entry);
    }
    owner->staging = dst;
    erts_code_table_write_unlock(owner);
    return 0;
refuse:
    erts_code_table_write_unlock(owner);
    return 1;
}
int erts_code_table_check_staging(ErtsCodeTable *owner, ErtsCodeIndex src, ErtsCodeIndex dst)
{
    return start_staging(owner, src, dst, 0);
}
int erts_code_table_start_staging(ErtsCodeTable *owner, ErtsCodeIndex src, ErtsCodeIndex dst)
{
    return start_staging(owner, src, dst, 1);
}
int erts_code_table_end_staging(ErtsCodeTable *owner, ErtsCodeIndex dst)
{
    int result = 1;
    erts_code_table_write_lock(owner);
    if (dst < ERTS_NUM_CODE_IX && owner->staging == dst) {
        owner->staging = NO_STAGE;
        result = 0;
    }
    erts_code_table_write_unlock(owner);
    return result;
}
void erts_code_table_foreach(ErtsCodeTable *owner, ErtsCodeIndex ix,
                            void (*callback)(void *, void *), void *arg)
{
    ASSERT(ix < ERTS_NUM_CODE_IX);
    for (int i = 0; i < owner->tables[ix].entries; ++i)
        callback(((CodeEntry *)erts_index_lookup(&owner->tables[ix], i))->object, arg);
}
size_t erts_code_table_entry_bytes(ErtsCodeTable *owner) { return erts_atomic_read_nob(&owner->bytes); }
size_t erts_code_table_size(ErtsCodeTable *owner)
{
    size_t size = 0;
    int lock = !ERTS_IS_CRASH_DUMPING;
    if (lock) erts_rwmtx_rlock(&owner->lock);
    for (int i = 0; i < ERTS_NUM_CODE_IX; ++i) size += index_table_sz(&owner->tables[i]);
    if (lock) erts_rwmtx_runlock(&owner->lock);
    return size;
}
void erts_code_table_info(ErtsCodeTable *owner, ErtsCodeIndex active, ErtsCodeIndex staging,
                         fmtfn_t to, void *arg)
{
    int lock = !ERTS_IS_CRASH_DUMPING;
    if (lock) erts_code_table_write_lock(owner);
    index_info(to, arg, &owner->tables[active]);
    hash_info(to, arg, &owner->tables[staging].htable);
    if (lock) erts_code_table_write_unlock(owner);
}
int erts_code_table_can_discard(ErtsCodeTable *owner)
{
    if (!owner || owner->bound || owner->staging != NO_STAGE) return 0;
    for (int ix = 0; ix < ERTS_NUM_CODE_IX; ++ix)
        for (int i = 0; i < owner->tables[ix].entries; ++i)
            if (!owner->ops.unpublished(((CodeEntry *)erts_index_lookup(&owner->tables[ix], i))->object))
                return 0;
    return 1;
}
int erts_code_table_discard(ErtsCodeTable *owner)
{
    if (!erts_code_table_can_discard(owner)) return 1;
    for (int i = 0; i < ERTS_NUM_CODE_IX; ++i) erts_index_destroy(&owner->tables[i]);
    ASSERT(erts_code_table_entry_bytes(owner) == 0);
    erts_rwmtx_destroy(&owner->lock);
    erts_free(owner->table_type, owner);
    return 0;
}
