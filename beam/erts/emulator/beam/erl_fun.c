/*
 * %CopyrightBegin%
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Copyright Ericsson AB 2000-2026. All Rights Reserved.
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
#include "erl_fun.h"
#include "hash.h"
#include "beam_common.h"
#include "erl_code_table.h"

#define FUN_INITIAL_SIZE   512
#define FUN_LIMIT          (512*1024)

const ErtsCodeMFA *erts_get_fun_mfa(const ErlFunEntry *fe, ErtsCodeIndex ix)
{
    ErtsCodePtr address = fe->dispatch.addresses[ix];
 
    if (address != beam_unloaded_fun) {
        return erts_find_function_from_pc(address);
    }
 
    return NULL;
}
 
void erts_set_fun_code(ErlFunEntry *fe, ErtsCodeIndex ix, ErtsCodePtr address)
{
    /* Fun entries MUST NOT be updated during a purge! */
    ASSERT(fe->pend_purge_address == NULL);
    fe->dispatch.addresses[ix] = address;
}
 
int erts_is_fun_loaded(const ErlFunEntry* fe, ErtsCodeIndex ix)
{
    return fe->dispatch.addresses[ix] != beam_unloaded_fun;
}

/* ************************************************************************* */

static HashValue fun_hash(const void *object)
{
    const ErlFunEntry *fe = object;
    HashValue components[2];

#ifdef ARCH_64
    ERTS_CT_ASSERT(sizeof(HashValue) == sizeof(Uint64));
    sys_memcpy(&components[0], &fe->uniq[0], sizeof(Uint64));
    sys_memcpy(&components[1], &fe->uniq[8], sizeof(Uint64));
#else
    ERTS_CT_ASSERT(sizeof(HashValue) == sizeof(Uint32));
    sys_memcpy(&components[0], &fe->uniq[0], sizeof(Uint32));
    sys_memcpy(&components[1], &fe->uniq[4], sizeof(Uint32));
#endif

    return components[0] ^ components[1] ^ (HashValue)fe->index;
}

static int fun_cmp(const void *a, const void *b)
{
    const ErlFunEntry *lhs = a, *rhs = b;
    return !(lhs->module == rhs->module &&
             lhs->index == rhs->index &&
             lhs->arity == rhs->arity &&
             !sys_memcmp(lhs->uniq, rhs->uniq, sizeof(lhs->uniq)));
}

static void fun_init(void *target, const void *template)
{
    ErlFunEntry *dst = target;
    ErtsDispatchable *dispatch = &dst->dispatch;

    sys_memcpy(dst, template, sizeof(ErlFunEntry));

    for (int ix = 0; ix < ERTS_NUM_CODE_IX; ix++) {
        dispatch->addresses[ix] = beam_unloaded_fun;
    }

#ifdef BEAMASM
    dispatch->addresses[ERTS_SAVE_CALLS_CODE_IX] = beam_save_calls_fun;
#endif

    dst->pend_purge_address = NULL;
}

static void fun_stage(void *object, ErtsCodeIndex src_ix, ErtsCodeIndex dst_ix)
{
    ErlFunEntry *entry = object;
    ErtsDispatchable *dispatch = &entry->dispatch;

    /* Fun entries MUST NOT be updated during a purge! */
    ASSERT(entry->pend_purge_address == NULL);

    dispatch->addresses[dst_ix] = dispatch->addresses[src_ix];
}

static int fun_unpublished(const void *object)
{
    const ErlFunEntry *entry = object;
    if (entry->pend_purge_address) return 0;
    for (int ix = 0; ix < ERTS_NUM_CODE_IX; ++ix)
        if (entry->dispatch.addresses[ix] != beam_unloaded_fun) return 0;
#ifdef BEAMASM
    if (entry->dispatch.addresses[ERTS_SAVE_CALLS_CODE_IX] != beam_save_calls_fun) return 0;
#endif
    return 1;
}

static int fun_stage_ready(const void *object)
{
    return ((const ErlFunEntry *)object)->pend_purge_address == NULL;
}
ErtsCodeTable *erts_fun_namespace_create(int limit)
{
    ErtsCodeTableOps ops = {fun_hash, fun_cmp, fun_init, fun_stage, fun_unpublished, fun_stage_ready};
    return erts_code_table_create(sizeof(ErlFunEntry), FUN_INITIAL_SIZE,
        limit > 0 ? limit : FUN_LIMIT, ERTS_ALC_T_EXPORT, ERTS_ALC_T_EXPORT_TABLE,
        "fun_staging_lock", ops);
}

/* Fixed diagnostic boundary; component storage never selects a current world. */
static ErtsCodeTable *diagnostic_funs;
void erts_init_fun_table(ErtsCodeTable *owner)
{
    ASSERT(owner && !diagnostic_funs);
    diagnostic_funs = owner;
    erts_code_table_bind(owner);
}

void erts_fun_info(fmtfn_t to, void *to_arg)
{
    erts_code_table_info(diagnostic_funs, erts_active_code_ix(), erts_staging_code_ix(), to, to_arg);
}

int erts_fun_table_sz(void)
{
    return erts_code_table_size(diagnostic_funs);
}

int erts_fun_entries_sz(void)
{
    return erts_code_table_entry_bytes(diagnostic_funs);
}

struct fun_prepare_purge_args {
    struct erl_module_instance* modp;
    ErtsCodeIndex code_ix;
};

static void fun_purge_foreach(void *object, void *args_)
{
    ErlFunEntry *fe = object;
    struct fun_prepare_purge_args *args = args_;
    struct erl_module_instance* modp = args->modp;
    const char *mod_start;
    ErtsCodePtr fun_addr;

    fun_addr = fe->dispatch.addresses[args->code_ix];
    mod_start = (const char*)modp->code_hdr;

    if (ErtsInArea((const char*)fun_addr, mod_start, modp->code_length)) {
        ASSERT(fe->pend_purge_address == NULL);

        fe->pend_purge_address = fun_addr;
        ERTS_THR_WRITE_MEMORY_BARRIER;

        fe->dispatch.addresses[args->code_ix] = beam_unloaded_fun;

        erts_purge_state_add_fun(fe);
    }
}

void erts_fun_purge_prepare(struct erl_module_instance* modp)
{
    struct fun_prepare_purge_args args = {modp, erts_active_code_ix()};

    ERTS_LC_ASSERT(erts_has_code_stage_permission());

    erts_code_table_foreach(diagnostic_funs, erts_active_code_ix(), fun_purge_foreach, &args);
}

void erts_fun_purge_abort_prepare(ErlFunEntry **funs, Uint no)
{
    ErtsCodeIndex code_ix = erts_active_code_ix();
    Uint fun_ix;

    ERTS_LC_ASSERT(erts_has_code_stage_permission());

    for (fun_ix = 0; fun_ix < no; fun_ix++) {
        ErlFunEntry *fe = funs[fun_ix];

        ASSERT(fe->dispatch.addresses[code_ix] == beam_unloaded_fun);
        fe->dispatch.addresses[code_ix] = fe->pend_purge_address;
    }
}

void erts_fun_purge_abort_finalize(ErlFunEntry **funs, Uint no)
{
#ifdef DEBUG
    ErtsCodeIndex code_ix = erts_active_code_ix();
#endif
    Uint fun_ix;

    ERTS_LC_ASSERT(erts_has_code_stage_permission());

    for (fun_ix = 0; fun_ix < no; fun_ix++) {
        ErlFunEntry *fe = funs[fun_ix];

        /* The abort_prepare step should have set the active address to the
         * actual one. */
        ASSERT(fe->dispatch.addresses[code_ix] != beam_unloaded_fun);
        fe->pend_purge_address = NULL;
    }
}

void erts_fun_purge_complete(ErlFunEntry **funs, Uint no)
{
#ifdef DEBUG
    ErtsCodeIndex code_ix = erts_active_code_ix();
#endif
    Uint ix;

    ERTS_LC_ASSERT(erts_has_code_stage_permission());

    for (ix = 0; ix < no; ix++) {
        ErlFunEntry *fe = funs[ix];

        ASSERT(fe->dispatch.addresses[code_ix] == beam_unloaded_fun);
        fe->pend_purge_address = NULL;
    }

    ERTS_THR_WRITE_MEMORY_BARRIER;
}

struct dump_fun_foreach_args {
    fmtfn_t to;
    void *to_arg;
    ErtsCodeIndex code_ix;
};

static void
dump_fun_foreach(void *object, void *_args)
{
    ErlFunEntry *fe = object;
    struct dump_fun_foreach_args *args = _args;

    erts_print(args->to, args->to_arg, "=fun\n");
    erts_print(args->to, args->to_arg, "Module: %T\n", fe->module);
    erts_print(args->to, args->to_arg, "Uniq: %d\n", fe->old_uniq);
    erts_print(args->to, args->to_arg, "Index: %d\n",fe->old_index);
    erts_print(args->to, args->to_arg, "Address: %p\n",
               fe->dispatch.addresses[args->code_ix]);
    erts_print(args->to, args->to_arg, "Refc: 1\n");
}

void
erts_dump_fun_entries(fmtfn_t to, void *to_arg)
{
    struct dump_fun_foreach_args args = {to, to_arg, erts_active_code_ix()};
    erts_code_table_foreach(diagnostic_funs, erts_active_code_ix(), dump_fun_foreach, &args);
}

static void init_fun_template(ErlFunEntry *template,
                              Eterm mod, int old_uniq, int old_index,
                              const byte* uniq, int index, int arity)
{
    ErlFunEntry *object;

    ASSERT(is_atom(mod));

    sys_memset(template, 0, sizeof(*template));
    object = template;
    object->old_index = old_index;
    object->old_uniq = old_uniq;
    object->index = index;
    object->module = mod;
    object->arity = arity;
    sys_memcpy(object->uniq, uniq, sizeof(object->uniq));
}

ErlFunEntry *erts_fun_entry_put(Eterm mod,
                                int old_uniq,
                                int old_index,
                                const byte* uniq,
                                int index,
                                int arity)
{
    ErlFunEntry template;
    init_fun_template(&template, mod, old_uniq, old_index, uniq, index, arity);
    return erts_code_table_diagnostic_upsert(diagnostic_funs, &template);
}

const ErlFunEntry *erts_fun_entry_get_or_make_stub(Eterm mod,
                                                   int old_uniq,
                                                   int old_index,
                                                   const byte* uniq,
                                                   int index,
                                                   int arity)
{
    ErlFunEntry template;
    init_fun_template(&template, mod, old_uniq, old_index, uniq, index, arity);
    return erts_code_table_diagnostic_upsert(diagnostic_funs, &template);
}
