/*
 * %CopyrightBegin%
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Copyright Ericsson AB 1996-2026. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
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
/* Export namespace extraction from export.c and erl_code_staged.h. */
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#include "sys.h"
#include "erl_vm.h"
#include "global.h"
#include "export.h"
#include "hash.h"
#include "jit/beam_asm.h"
#include "erl_export_namespace.h"

#define EXPORT_INITIAL_SIZE 4000
#define EXPORT_LIMIT (512*1024)
#define NO_EXPORT_STAGE (~(unsigned)0)

typedef struct {
    IndexSlot slot;
    Export *object;
    ErtsExportNamespace *owner;
} ExportEntry;

typedef struct {
    Export object;
    ExportEntry entries[ERTS_NUM_CODE_IX];
} ExportBlob;

struct ErtsExportNamespace {
    IndexTable tables[ERTS_NUM_CODE_IX];
    erts_rwmtx_t lock;
    erts_atomic_t entry_bytes;
    ErtsExportLiterals *literals;
    int limit;
    unsigned staging;
    int bound;
};

/* Fixed diagnostic adapter, not a current-world selector. All storage and
 * component operations below have an explicit owner. */
static ErtsExportNamespace *diagnostic_exports;

static HashValue export_hash(ExportEntry *entry)
{
    const Export *e = entry->object;
    return (atom_val(e->info.mfa.module) * atom_val(e->info.mfa.function)) ^
        e->info.mfa.arity;
}

static int export_cmp(ExportEntry *a, ExportEntry *b)
{
    return !(a->object->info.mfa.module == b->object->info.mfa.module &&
             a->object->info.mfa.function == b->object->info.mfa.function &&
             a->object->info.mfa.arity == b->object->info.mfa.arity);
}

static void export_init(ErtsExportNamespace *owner, Export *dst, const Export *src)
{
    sys_memset(&dst->info.u, 0, sizeof(dst->info.u));
    dst->info.gen_bp = NULL;
    dst->info.mfa = src->info.mfa;
    dst->bif_number = -1;
    dst->is_bif_traced = 0;
    dst->lambda = erts_export_literal_create(owner->literals, dst);
    sys_memset(&dst->trampoline, 0, sizeof(dst->trampoline));
    if (BeamOpsAreInitialized())
        dst->trampoline.common.op = BeamOpCodeAddr(op_call_error_handler);
    for (int ix = 0; ix < ERTS_NUM_CODE_IX; ++ix)
        erts_activate_export_trampoline(dst, ix);
#ifdef BEAMASM
    dst->dispatch.addresses[ERTS_SAVE_CALLS_CODE_IX] = beam_save_calls_export;
#endif
}

static ExportEntry *export_alloc(ExportEntry *template)
{
    ExportBlob *blob;
    if (template->slot.index == -1) {
        blob = erts_alloc(ERTS_ALC_T_EXPORT, sizeof(*blob));
        export_init(template->owner, &blob->object, template->object);
        erts_atomic_add_nob(&template->owner->entry_bytes, sizeof(*blob));
        for (int i = 0; i < ERTS_NUM_CODE_IX; ++i) {
            blob->entries[i].slot.index = -1;
            blob->entries[i].object = &blob->object;
            blob->entries[i].owner = template->owner;
        }
        return &blob->entries[0];
    }
    blob = ErtsContainerStruct(template->object, ExportBlob, object);
    for (int i = 0; i < ERTS_NUM_CODE_IX; ++i)
        if (blob->entries[i].slot.index < 0)
            return &blob->entries[i];
    erts_exit(ERTS_ABORT_EXIT, "Export has no unused code-index entry\n");
    return NULL;
}

static void export_free(ExportEntry *entry)
{
    ExportBlob *blob = ErtsContainerStruct(entry->object, ExportBlob, object);
    ErtsExportNamespace *owner = entry->owner;
    entry->slot.index = -1;
    for (int i = 0; i < ERTS_NUM_CODE_IX; ++i)
        if (blob->entries[i].slot.index >= 0)
            return;
    erts_atomic_add_nob(&owner->entry_bytes, -((erts_aint_t)sizeof(*blob)));
    /* Literal areas belong to the namespace's pool and are retired only after
     * all its export tables and other borrowers have been retired. */
    erts_free(ERTS_ALC_T_EXPORT, blob);
}

int erts_export_namespace_default_limit(void) { return EXPORT_LIMIT; }

ErtsExportNamespace *erts_export_namespace_create(ErtsExportLiterals *literals, int limit)
{
    ErtsExportNamespace *owner;
    HashFunctions f;
    erts_rwmtx_opt_t opt = ERTS_RWMTX_OPT_DEFAULT_INITER;
    opt.type = ERTS_RWMTX_TYPE_FREQUENT_READ;
    opt.lived = ERTS_RWMTX_LONG_LIVED;
    if (!literals || limit <= 0)
        return NULL;
    owner = erts_alloc(ERTS_ALC_T_EXPORT_TABLE, sizeof(*owner));
    owner->literals = literals;
    owner->limit = limit;
    owner->staging = NO_EXPORT_STAGE;
    owner->bound = 0;
    erts_atomic_init_nob(&owner->entry_bytes, 0);
    erts_rwmtx_init_opt(&owner->lock, &opt, "export_staging_lock", NIL,
                        ERTS_LOCK_FLAGS_CATEGORY_GENERIC);
    f.hash = (H_FUN) export_hash;
    f.cmp = (HCMP_FUN) export_cmp;
    f.alloc = (HALLOC_FUN) export_alloc;
    f.free = (HFREE_FUN) export_free;
    f.meta_alloc = (HMALLOC_FUN) erts_alloc;
    f.meta_free = (HMFREE_FUN) erts_free;
    f.meta_print = (HMPRINT_FUN) erts_print;
    for (int i = 0; i < ERTS_NUM_CODE_IX; ++i)
        erts_index_init(ERTS_ALC_T_EXPORT_TABLE, &owner->tables[i],
                        "export_staged_index", EXPORT_INITIAL_SIZE, limit, f);
    return owner;
}

static void init_key(ErtsExportNamespace *owner, ExportEntry *key, Export *object,
                     Eterm m, Eterm f, unsigned arity)
{
    key->slot.index = -1;
    key->owner = owner;
    key->object = object;
    object->info.mfa.module = m;
    object->info.mfa.function = f;
    object->info.mfa.arity = arity;
}

static Export *find_entry(ErtsExportNamespace *owner, ExportEntry *key, unsigned slot)
{
    ExportEntry *entry = hash_get(&owner->tables[slot].htable, key);
    return entry ? entry->object : NULL;
}

static Export *put_entry(ErtsExportNamespace *owner, ExportEntry *key, unsigned slot)
{
    Export *existing = find_entry(owner, key, slot);
    if (existing)
        return existing;
    if (owner->tables[slot].entries >= owner->limit)
        return NULL;
    return ((ExportEntry *)index_put_entry(&owner->tables[slot], key))->object;
}

const Export *erts_export_namespace_find(ErtsExportNamespace *owner, int module,
                                         int function, unsigned arity, unsigned slot)
{
    ExportEntry key;
    Export object;
    Export *result;
    if (module < 0 || function < 0 || arity > MAX_ARG || slot >= ERTS_NUM_CODE_IX)
        return NULL;
    init_key(owner, &key, &object, make_atom(module), make_atom(function), arity);
    erts_rwmtx_rlock(&owner->lock);
    result = find_entry(owner, &key, slot);
    erts_rwmtx_runlock(&owner->lock);
    return result;
}

Export *erts_export_namespace_put(ErtsExportNamespace *owner, int module,
                                  int function, unsigned arity, unsigned slot)
{
    ExportEntry key;
    Export object;
    Export *result;
    if (module < 0 || function < 0 || arity > MAX_ARG || slot >= ERTS_NUM_CODE_IX)
        return NULL;
    init_key(owner, &key, &object, make_atom(module), make_atom(function), arity);
    erts_rwmtx_rwlock(&owner->lock);
    result = put_entry(owner, &key, slot);
    erts_rwmtx_rwunlock(&owner->lock);
    return result;
}

size_t erts_export_namespace_entry_bytes(ErtsExportNamespace *owner)
{
    return (size_t) erts_atomic_read_nob(&owner->entry_bytes);
}

int erts_export_namespace_count(ErtsExportNamespace *owner, unsigned slot)
{
    int count;
    if (slot >= ERTS_NUM_CODE_IX)
        return -1;
    erts_rwmtx_rlock(&owner->lock);
    count = owner->tables[slot].entries;
    erts_rwmtx_runlock(&owner->lock);
    return count;
}

/* Preserve the staged-table fast path: an occupied blob slot can identify its
 * membership in another code index without hashing the MFA again. */
static int export_in_table(ErtsExportNamespace *owner, ExportEntry *entry, unsigned ix)
{
    ExportBlob *blob = ErtsContainerStruct(entry->object, ExportBlob, object);
    ASSERT(entry->owner == owner);
    for (int i = 0; i < ERTS_NUM_CODE_IX; ++i) {
        int index = blob->entries[i].slot.index;
        if (index >= 0 && index < owner->tables[ix].entries) {
            ExportEntry *found = (ExportEntry *)erts_index_lookup(&owner->tables[ix], index);
            if (found->object == entry->object)
                return 1;
        }
    }
    return 0;
}

static int export_start(ErtsExportNamespace *owner, unsigned src, unsigned dst, int apply)
{
    int missing = 0;
    if (src >= ERTS_NUM_CODE_IX || dst >= ERTS_NUM_CODE_IX || src == dst)
        return 1;
    erts_rwmtx_rwlock(&owner->lock);
    if (owner->staging != NO_EXPORT_STAGE)
        goto refuse;
    /* Validate before modifying dispatch slots or destination storage. */
    for (int i = 0; i < owner->tables[src].entries; ++i) {
        ExportEntry *entry = (ExportEntry *)erts_index_lookup(&owner->tables[src], i);
        if (!export_in_table(owner, entry, dst)) {
            if (find_entry(owner, entry, dst))
                goto refuse;
            ++missing;
        }
    }
    if (missing > owner->limit - owner->tables[dst].entries)
        goto refuse;
    if (!apply) {
        erts_rwmtx_rwunlock(&owner->lock);
        return 0;
    }
    for (int i = 0; i < owner->tables[src].entries; ++i) {
        ExportEntry *entry = (ExportEntry *)erts_index_lookup(&owner->tables[src], i);
        entry->object->dispatch.addresses[dst] = entry->object->dispatch.addresses[src];
        if (!export_in_table(owner, entry, dst))
            index_put_entry(&owner->tables[dst], entry);
    }
    owner->staging = dst;
    erts_rwmtx_rwunlock(&owner->lock);
    return 0;
refuse:
    erts_rwmtx_rwunlock(&owner->lock);
    return 1;
}

int erts_export_namespace_check_staging(ErtsExportNamespace *owner, unsigned src, unsigned dst)
{
    return export_start(owner, src, dst, 0);
}
int erts_export_namespace_start_staging(ErtsExportNamespace *owner, unsigned src, unsigned dst)
{
    return export_start(owner, src, dst, 1);
}
void erts_export_namespace_write_lock(ErtsExportNamespace *owner) { erts_rwmtx_rwlock(&owner->lock); }
void erts_export_namespace_write_unlock(ErtsExportNamespace *owner) { erts_rwmtx_rwunlock(&owner->lock); }
int erts_export_namespace_end_staging(ErtsExportNamespace *owner, unsigned dst)
{
    int result = 1;
    erts_rwmtx_rwlock(&owner->lock);
    if (owner->staging == dst && dst < ERTS_NUM_CODE_IX) {
        owner->staging = NO_EXPORT_STAGE;
        result = 0;
    }
    erts_rwmtx_rwunlock(&owner->lock);
    return result;
}

int erts_export_namespace_can_discard(ErtsExportNamespace *owner)
{
    if (!owner || owner->bound || owner->staging != NO_EXPORT_STAGE)
        return 0;
    for (int ix = 0; ix < ERTS_NUM_CODE_IX; ++ix) {
        for (int i = 0; i < owner->tables[ix].entries; ++i) {
            ExportEntry *entry = (ExportEntry *)erts_index_lookup(&owner->tables[ix], i);
            Export *e = entry->object;
            const unsigned char *header = (const unsigned char *)&e->info.u;
            for (size_t k = 0; k < sizeof(e->info.u); ++k)
                if (header[k])
                    return 0;
#ifdef BEAMASM
            if (e->dispatch.addresses[ERTS_SAVE_CALLS_CODE_IX] != beam_save_calls_export)
                return 0;
#endif
            if (e->bif_number != -1 || e->is_bif_traced || e->info.gen_bp ||
                e->trampoline.not_loaded.deferred)
                return 0;
            if (BeamOpsAreInitialized() &&
                !BeamIsOpCode(e->trampoline.common.op, op_call_error_handler))
                return 0;
            for (int j = 0; j < ERTS_NUM_CODE_IX; ++j)
                if (!erts_is_export_trampoline_active(e, j))
                    return 0;
        }
    }
    return 1;
}

int erts_export_namespace_discard(ErtsExportNamespace *owner)
{
    if (!erts_export_namespace_can_discard(owner))
        return 1;
    for (int ix = 0; ix < ERTS_NUM_CODE_IX; ++ix)
        erts_index_destroy(&owner->tables[ix]);
    ASSERT(erts_atomic_read_nob(&owner->entry_bytes) == 0);
    erts_rwmtx_destroy(&owner->lock);
    erts_free(ERTS_ALC_T_EXPORT_TABLE, owner);
    return 0;
}

/* Existing VM API: fixed diagnostic adapters until process/loader propagation. */
void init_export_table(ErtsExportNamespace *owner)
{
    ASSERT(owner && !diagnostic_exports);
    diagnostic_exports = owner;
    owner->bound = 1;
    owner->staging = 0; /* Single-threaded preloading ends without start_staging. */
    erts_export_literals_bind(owner->literals);
}

int erts_export_table_limit(void) { return diagnostic_exports->limit; }

void export_info(fmtfn_t to, void *arg)
{
    int lock = !ERTS_IS_CRASH_DUMPING;
    if (lock) erts_export_namespace_write_lock(diagnostic_exports);
    index_info(to, arg, &diagnostic_exports->tables[erts_active_code_ix()]);
    hash_info(to, arg, &diagnostic_exports->tables[erts_staging_code_ix()].htable);
    if (lock) erts_export_namespace_write_unlock(diagnostic_exports);
}

const Export *erts_find_export_entry(Eterm m, Eterm f, unsigned a, ErtsCodeIndex ix)
{
    ExportEntry key;
    Export object;
    init_key(diagnostic_exports, &key, &object, m, f, a);
    /* Active lookup remains lock-free under the existing code-index protocol. */
    return find_entry(diagnostic_exports, &key, ix);
}

const Export *erts_find_function(Eterm m, Eterm f, unsigned a, ErtsCodeIndex ix)
{
    const Export *e = erts_find_export_entry(m, f, a, ix);
    if (!e || (erts_is_export_trampoline_active(e, ix) &&
               !BeamIsOpCode(e->trampoline.common.op, op_i_generic_breakpoint)))
        return NULL;
    return e;
}

Export *erts_export_put(Eterm m, Eterm f, unsigned arity)
{
    Export *result = erts_export_namespace_put(diagnostic_exports, atom_val(m),
                                               atom_val(f), arity, erts_staging_code_ix());
    if (!result)
        erts_exit(ERTS_DUMP_EXIT, "no more export entries\n");
    return result;
}

Export *erts_export_get_or_make_stub(Eterm m, Eterm f, unsigned arity)
{
    ExportEntry key;
    Export object, *result;
    ErtsExportNamespace *owner = diagnostic_exports;
    init_key(owner, &key, &object, m, f, arity);
    for (;;) {
        ErtsCodeIndex active = erts_active_code_ix();
        result = find_entry(owner, &key, active);
        if (result)
            return result;
        erts_rwmtx_rlock(&owner->lock);
        if (active != erts_active_code_ix()) {
            erts_rwmtx_runlock(&owner->lock);
            continue;
        }
        result = find_entry(owner, &key, erts_staging_code_ix());
        erts_rwmtx_runlock(&owner->lock);
        if (result)
            return result;
        /* Commit takes this same write lock. Recheck the active index before
         * using staging; never insert into a slot selected before a commit. */
        erts_rwmtx_rwlock(&owner->lock);
        if (active == erts_active_code_ix()) {
            result = put_entry(owner, &key, erts_staging_code_ix());
            erts_rwmtx_rwunlock(&owner->lock);
            if (!result)
                erts_exit(ERTS_DUMP_EXIT, "no more export entries\n");
            return result;
        }
        erts_rwmtx_rwunlock(&owner->lock);
    }
}

Export *export_list(int i, ErtsCodeIndex ix)
{
    ExportEntry *entry = (ExportEntry *)erts_index_lookup(&diagnostic_exports->tables[ix], i);
    return entry->object;
}
int export_list_size(ErtsCodeIndex ix)
{
    return erts_index_num_entries(&diagnostic_exports->tables[ix]);
}
int export_table_sz(void)
{
    UWord bytes = 0;
    int lock = !ERTS_IS_CRASH_DUMPING;
    if (lock) erts_rwmtx_rlock(&diagnostic_exports->lock);
    for (int i = 0; i < ERTS_NUM_CODE_IX; ++i)
        bytes += index_table_sz(&diagnostic_exports->tables[i]);
    if (lock) erts_rwmtx_runlock(&diagnostic_exports->lock);
    return bytes;
}
int export_entries_sz(void) { return erts_atomic_read_nob(&diagnostic_exports->entry_bytes); }
const Export *export_get(const Export *e)
{
    return erts_find_export_entry(e->info.mfa.module, e->info.mfa.function,
                                  e->info.mfa.arity, erts_active_code_ix());
}
