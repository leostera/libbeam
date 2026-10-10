/*
 * %CopyrightBegin%
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Copyright Ericsson AB 1996-2025. All Rights Reserved.
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
#include "module.h"
#include "beam_catches.h"

#ifdef BEAMASM
#  include "beam_asm.h"
#endif

#ifdef DEBUG
#  define IF_DEBUG(x) x
#else
#  define IF_DEBUG(x)
#endif

#define MODULE_SIZE   50
#define MODULE_LIMIT  (64*1024)

int erts_module_table_default_limit(void) { return MODULE_LIMIT; }

struct ErtsModuleTable {
    IndexTable index;
    erts_atomic_t bytes;
    int independently_allocated;
};

/* Diagnostic OTP-world adapter until loader/process code-space propagation.
 * Private tables use the same component, never swap these roots. */
static ErtsModuleTable **module_tables; /* Borrowed diagnostic state slots. */

#include "erl_module_namespace.h"
struct ErtsModuleNamespace {
    ErtsModuleTable **tables;
    erts_rwmtx_t old_code_locks[ERTS_NUM_CODE_IX];
    struct erl_module_instance *unsealed;
    ErtsCodeIndex staging;
    int entries_at_start, bound;
};
static ErtsModuleNamespace *diagnostic_modules;
#define NO_MODULE_STAGE (~(ErtsCodeIndex)0)


/* SMP note: Active module table lookup and current module instance can be
 *           read without any locks. Old module instances are protected by
 *           the namespace's old-code locks while purging the active table.
 *           Staging table is protected by the "code_ix lock". 
 */

void module_info(fmtfn_t to, void *to_arg)
{
    index_info(to, to_arg, &module_tables[erts_active_code_ix()]->index);
}


static HashValue module_hash(Module* x)
{
    return (HashValue) x->module;
}


static int module_cmp(Module* tmpl, Module* obj)
{
    return tmpl->module != obj->module;
}

void erts_module_instance_init(struct erl_module_instance* modi)
{
    modi->code_hdr = 0;
    modi->code_length = 0;
    modi->catches = BEAM_CATCHES_NIL;
    modi->nif = NULL;
    modi->num_breakpoints = 0;
    modi->num_traced_exports = 0;
    modi->executable_region = NULL;
    modi->writable_region = NULL;
    modi->metadata = NULL;
    modi->unsealed = 0;
}

static Module* module_alloc(Module* tmpl)
{
    Module* obj = (Module*) erts_alloc(ERTS_ALC_T_MODULE, sizeof(Module));
    obj->table_owner = tmpl->table_owner;
    erts_atomic_add_nob(&obj->table_owner->bytes, sizeof(Module));

    obj->module = tmpl->module;
    obj->slot.index = -1;
    erts_module_instance_init(&obj->curr);
    erts_module_instance_init(&obj->old);
    obj->on_load = 0;
    return obj;
}

static void module_free(Module* mod)
{
    ErtsModuleTable *owner = mod->table_owner;
    erts_free(ERTS_ALC_T_MODULE, mod);
    erts_atomic_add_nob(&owner->bytes, -sizeof(Module));
}

static void init_owned_module_table(ErtsModuleTable *owner, int limit)
{
    HashFunctions f;
    erts_atomic_init_nob(&owner->bytes, 0);
    owner->independently_allocated = 0;

    f.hash = (H_FUN) module_hash;
    f.cmp  = (HCMP_FUN) module_cmp;
    f.alloc = (HALLOC_FUN) module_alloc;
    f.free = (HFREE_FUN) module_free;
    f.meta_alloc = (HMALLOC_FUN) erts_alloc;
    f.meta_free = (HMFREE_FUN) erts_free;
    f.meta_print = (HMPRINT_FUN) erts_print;

    erts_index_init(ERTS_ALC_T_MODULE_TABLE, &owner->index, "module_code",
                    MODULE_SIZE, limit, f);
}

ErtsModuleNamespace *erts_module_namespace_create(ErtsModuleTable **tables)
{
    ErtsModuleNamespace *owner = erts_alloc(ERTS_ALC_T_MODULE_TABLE, sizeof(*owner));
    owner->tables = tables;
    owner->unsealed = NULL;
    owner->staging = NO_MODULE_STAGE;
    owner->entries_at_start = 0;
    owner->bound = 0;
    for (int i = 0; i < ERTS_NUM_CODE_IX; ++i)
        erts_rwmtx_init(&owner->old_code_locks[i], "old_code", make_small(i),
                        ERTS_LOCK_FLAGS_CATEGORY_GENERIC);
    return owner;
}
void init_module_table(ErtsModuleNamespace *owner)
{
    ASSERT(owner && !diagnostic_modules);
    diagnostic_modules = owner;
    module_tables = owner->tables;
    owner->bound = 1;
    owner->staging = 0;
}
erts_rwmtx_t *erts_diagnostic_old_code_lock(ErtsCodeIndex ix)
{
    return &diagnostic_modules->old_code_locks[ix];
}


ErtsModuleTable *erts_module_table_create(int limit)
{
    ErtsModuleTable *owner;
    if (limit <= 0)
        return NULL;
    owner = erts_alloc(ERTS_ALC_T_MODULE_TABLE, sizeof(*owner));
    init_owned_module_table(owner, limit);
    owner->independently_allocated = 1;
    return owner;
}

Module *erts_module_table_find(ErtsModuleTable *owner, int atom_index)
{
    Module key;
    if (atom_index < 0)
        return NULL;
    key.module = atom_index;
    return (Module *) hash_get(&owner->index.htable, &key);
}

int erts_module_table_count(const ErtsModuleTable *owner)
{
    return owner->index.entries;
}

int erts_module_table_capacity(const ErtsModuleTable *owner)
{
    return owner->index.limit;
}

static int module_instance_has_resources(const struct erl_module_instance *modi)
{
    return modi->code_hdr || modi->code_length || modi->catches != BEAM_CATCHES_NIL ||
        modi->nif || modi->num_breakpoints || modi->num_traced_exports ||
        modi->executable_region || modi->writable_region || modi->metadata ||
        modi->unsealed;
}

int erts_module_table_can_discard_unpublished(ErtsModuleTable *owner)
{
    int i;
    if (!owner || !owner->independently_allocated)
        return 0;
    for (i = 0; i < owner->index.entries; i++) {
        Module *mod = (Module *) erts_index_lookup(&owner->index, i);
        if (mod->on_load || module_instance_has_resources(&mod->curr) ||
            module_instance_has_resources(&mod->old))
            return 0;
    }
    return 1;
}

int erts_module_table_discard_unpublished(ErtsModuleTable *owner)
{
    if (!erts_module_table_can_discard_unpublished(owner))
        return 1;
    erts_index_destroy(&owner->index);
    erts_free(ERTS_ALC_T_MODULE_TABLE, owner);
    return 0;
}

Module*
erts_get_module(Eterm mod, ErtsCodeIndex code_ix)
{
    ASSERT(is_atom(mod));
    ERTS_LC_ASSERT(erts_get_scheduler_id() > 0 || erts_thr_progress_lc_is_delaying());
    return erts_module_table_find(module_tables[code_ix], atom_val(mod));
}


Module *erts_module_table_put(ErtsModuleTable *owner, int atom_index)
{
    IndexTable *mod_tab = &owner->index;
    Module e;
    int oldsz, newsz;
    Module* res;

    if (atom_index < 0)
        return NULL;
    e.module = atom_index;
    e.table_owner = owner;
    res = (Module *) hash_get(&mod_tab->htable, &e);
    if (res)
        return res;
    if (mod_tab->entries >= mod_tab->limit)
        return NULL;
    oldsz = index_table_sz(mod_tab);
    res = (Module*) index_put_entry(mod_tab, (void*) &e);
    newsz = index_table_sz(mod_tab);
    erts_atomic_add_nob(&owner->bytes, (newsz - oldsz));
    return res;
}

Module*
erts_put_module(Eterm mod)
{
    Module *result;
    ERTS_LC_ASSERT(erts_initialized == 0 || erts_has_code_load_permission());
    ASSERT(is_atom(mod));
    result = erts_module_table_put(module_tables[erts_staging_code_ix()], atom_val(mod));
    if (!result)
        erts_exit(ERTS_DUMP_EXIT, "no more index entries in module_code (max=%d)\n",
                  module_tables[erts_staging_code_ix()]->index.limit);
    DBG_TRACE_MFA(mod, 0, 0, "module_put");
    return result;
}

void *erts_writable_code_ptr(struct erl_module_instance *modi,
                             const void *ptr)
{
    const char *code_start, *code_end, *ptr_raw;

    ASSERT(modi->unsealed);

    code_start = (char*)modi->code_hdr;
    code_end = code_start + modi->code_length;
    ptr_raw = (const char*)ptr;

    (void)code_end;
    (void)ptr_raw;

    ASSERT(ptr_raw >= code_start && ptr_raw < code_end);

    {
        const char *exec_mod_start;
        char *rw_mod_start;

        exec_mod_start = (const char*)modi->executable_region;
        rw_mod_start = (char*)modi->writable_region;

        ASSERT(code_start >= exec_mod_start);

        return (void*)(rw_mod_start + (ptr_raw - exec_mod_start));
    }
}

int erts_module_namespace_unseal(ErtsModuleNamespace *owner, struct erl_module_instance *modi) {
    if (owner->unsealed || modi->unsealed) return 1;

#ifdef BEAMASM
    beamasm_unseal_module(modi->executable_region,
                          modi->writable_region,
                          modi->code_length);
#endif

    owner->unsealed = modi;
    modi->unsealed = 1;
    return 0;
}

int erts_module_namespace_seal(ErtsModuleNamespace *owner, struct erl_module_instance *modi)
{
    if (owner->unsealed != modi || modi->unsealed != 1) return 1;

#ifdef BEAMASM
    beamasm_flush_icache(modi->executable_region, modi->code_length);
    beamasm_seal_module(modi->executable_region,
                        modi->writable_region,
                        modi->code_length);
#endif

    owner->unsealed = NULL;
    modi->unsealed = 0;
    return 0;
}
void erts_unseal_module(struct erl_module_instance *modi)
{
    ERTS_LC_ASSERT(!erts_initialized || erts_thr_progress_is_blocking() || erts_has_code_mod_permission());
    if (erts_module_namespace_unseal(diagnostic_modules, modi))
        erts_exit(ERTS_ABORT_EXIT, "Cannot unseal diagnostic module\n");
}
void erts_seal_module(struct erl_module_instance *modi)
{
    ERTS_LC_ASSERT(!erts_initialized || erts_thr_progress_is_blocking() || erts_has_code_mod_permission());
    if (erts_module_namespace_seal(diagnostic_modules, modi))
        erts_exit(ERTS_ABORT_EXIT, "Cannot seal diagnostic module\n");
}

Module *module_code(int i, ErtsCodeIndex code_ix)
{
    return (Module*) erts_index_lookup(&module_tables[code_ix]->index, i);
}

int module_code_size(ErtsCodeIndex code_ix)
{
    return module_tables[code_ix]->index.entries;
}

int erts_module_table_limit(void)
{
    return module_tables[erts_active_code_ix()]->index.limit;
}

int module_table_sz(void)
{
    int i;
    erts_aint_t bytes = 0;
    for (i = 0; i < ERTS_NUM_CODE_IX; i++)
        bytes += erts_atomic_read_nob(&module_tables[i]->bytes);
    return bytes;
}

static ERTS_INLINE void copy_module(Module* dst_mod, Module* src_mod)
{
    dst_mod->curr = src_mod->curr;
    dst_mod->old = src_mod->old;
    dst_mod->on_load = src_mod->on_load;
}

static int module_start(ErtsModuleNamespace *owner, ErtsCodeIndex source, ErtsCodeIndex destination, int apply)
{
    IndexTable *src, *dst;
    ErtsModuleTable *dst_owner;
    Module *src_mod, *dst_mod;
    int i, oldsz, newsz;
    if (source >= ERTS_NUM_CODE_IX || destination >= ERTS_NUM_CODE_IX || source == destination ||
        owner->staging != NO_MODULE_STAGE || owner->unsealed) return 1;
    src = &owner->tables[source]->index;
    dst_owner = owner->tables[destination];
    dst = &dst_owner->index;

    if (dst->entries > src->entries || src->entries > dst->limit) return 1;
    for (i = 0; i < dst->entries; ++i) {
        src_mod = (Module *)erts_index_lookup(src, i);
        dst_mod = (Module *)erts_index_lookup(dst, i);
        if (src_mod->module != dst_mod->module) return 1;
    }
    if (!apply) return 0;

    /*
     * Make sure our existing modules are up-to-date
     */
    for (i = 0; i < dst->entries; i++) {
	src_mod = (Module*) erts_index_lookup(src, i);
	dst_mod = (Module*) erts_index_lookup(dst, i);
	ASSERT(src_mod->module == dst_mod->module);
        copy_module(dst_mod, src_mod);
    }

    /*
     * Copy all new modules from active table
     */
    oldsz = index_table_sz(dst);
    for (i = dst->entries; i < src->entries; i++) {
        Module template;
	src_mod = (Module*) erts_index_lookup(src, i);
        template.module = src_mod->module;
        template.table_owner = dst_owner;
	dst_mod = (Module*) index_put_entry(dst, &template);
	ASSERT(dst_mod != src_mod);

        copy_module(dst_mod, src_mod);
    }
    newsz = index_table_sz(dst);
    erts_atomic_add_nob(&dst_owner->bytes, (newsz - oldsz));

    owner->entries_at_start = dst->entries;
    owner->staging = destination;
    return 0;
}

int erts_module_namespace_check_staging(ErtsModuleNamespace *owner, ErtsCodeIndex src, ErtsCodeIndex dst)
{
    return module_start(owner, src, dst, 0);
}
int erts_module_namespace_start_staging(ErtsModuleNamespace *owner, ErtsCodeIndex src, ErtsCodeIndex dst)
{
    return module_start(owner, src, dst, 1);
}
int erts_module_namespace_check_end(ErtsModuleNamespace *state, ErtsCodeIndex destination, int commit)
{
    if (destination >= ERTS_NUM_CODE_IX || state->staging != destination || state->unsealed) return 1;
    if (!commit) {
        IndexTable *table = &state->tables[destination]->index;
        for (int i = state->entries_at_start; i < table->entries; ++i) {
            Module *mod = (Module *)erts_index_lookup(table, i);
            if (mod->on_load || module_instance_has_resources(&mod->curr) || module_instance_has_resources(&mod->old))
                return 1;
        }
    }
    return 0;
}
int erts_module_namespace_end_staging(ErtsModuleNamespace *state, ErtsCodeIndex destination, int commit)
{
    if (erts_module_namespace_check_end(state, destination, commit)) return 1;

    if (!commit) { /* abort */
        ErtsModuleTable *owner = state->tables[destination];
	IndexTable* tab = &owner->index;
	int oldsz, newsz;

	ASSERT(state->entries_at_start <= tab->entries);
	oldsz = index_table_sz(tab);
	index_erase_latest_from(tab, state->entries_at_start);
	newsz = index_table_sz(tab);
	erts_atomic_add_nob(&owner->bytes, (newsz - oldsz));
    }

    state->staging = NO_MODULE_STAGE;
    return 0;
}
int erts_module_namespace_can_discard(ErtsModuleNamespace *owner)
{
    return owner && !owner->bound && !owner->unsealed && owner->staging == NO_MODULE_STAGE;
}
int erts_module_namespace_discard(ErtsModuleNamespace *owner)
{
    if (!erts_module_namespace_can_discard(owner)) return 1;
    for (int i = 0; i < ERTS_NUM_CODE_IX; ++i) erts_rwmtx_destroy(&owner->old_code_locks[i]);
    erts_free(ERTS_ALC_T_MODULE_TABLE, owner);
    return 0;
}
