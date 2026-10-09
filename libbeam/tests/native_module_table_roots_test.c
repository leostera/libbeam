/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Internal guard test: compile with the configured emulator's CFLAGS/INCLUDES.
 * Synthetic resource markers below are NEVER executed or dereferenced and are
 * not evidence of loading/reclaiming BEAM code.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "sys.h"
#include "erl_vm.h"
#include "global.h"
#include "module.h"
#include "erl_embed.h"

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "module roots check failed at line %d: %s\n", __LINE__, #expr); \
    return 1; } } while (0)
#define REFUSES(field, value) do { \
    module->curr.field = (value); \
    CHECK(erts_module_table_discard_unpublished(table) == 1); \
    CHECK(erts_module_table_find(table, atom_val(am_true)) == module); \
    erts_module_instance_init(&module->curr); \
} while (0)

int main(int argc, char **argv)
{
    ErtsEngine *engine = erl_engine_alloc();
    ErtsModuleTable *table;
    Module *module;
    void *marker = malloc(4096);
    CHECK(marker);
    CHECK(engine && erl_prepare_runtime(engine, argc, argv) == 0);
    table = erts_module_table_create(1024);
    CHECK(table);
    module = erts_module_table_put(table, atom_val(am_true));
    CHECK(module && module->table_owner == table);
    CHECK(!module->curr.executable_region && !module->curr.writable_region &&
          !module->curr.metadata && !module->old.metadata);
    REFUSES(code_hdr, (const BeamCodeHeader *)marker);
    REFUSES(code_length, 1);
    REFUSES(nif, (struct erl_module_nif *)marker);
    REFUSES(num_breakpoints, 1);
    REFUSES(num_traced_exports, 1);
    REFUSES(executable_region, marker);
    REFUSES(writable_region, marker);
    REFUSES(metadata, marker);
    REFUSES(unsealed, 1);
    /* Change the catch sentinel without depending on its numeric representation. */
    REFUSES(catches, module->curr.catches ^ 1U);
    module->old.metadata = marker;
    CHECK(erts_module_table_discard_unpublished(table) == 1);
    erts_module_instance_init(&module->old);
    module->on_load = &module->curr;
    CHECK(erts_module_table_discard_unpublished(table) == 1);
    module->on_load = NULL;
    CHECK(erts_module_table_discard_unpublished(table) == 0);
    free(marker);
    puts("NATIVE_MODULE_ROOT_GUARDS_OK synthetic_markers=12 loaded_beam=0 isolates=0");
    fflush(stdout);
    _Exit(0); /* Native preparation is not reclaimed by this component test. */
}
