/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "sys.h"
#include "erl_vm.h"
#include "global.h"
#include "export.h"
#include "erl_export_literals.h"

struct ExportLiteralNode {
    struct ExportLiteralNode *next;
    ErtsLiteralArea *area;
};

struct ErtsExportLiterals {
    erts_mtx_t lock;
    struct ExportLiteralNode *head;
    size_t count;
    int bound;
};

ErtsExportLiterals *erts_export_literals_create(void)
{
    ErtsExportLiterals *owner = erts_alloc(ERTS_ALC_T_EXPORT_TABLE, sizeof(*owner));
    owner->head = NULL;
    owner->count = 0;
    owner->bound = 0;
    erts_mtx_init(&owner->lock, "export_literals", NIL, ERTS_LOCK_FLAGS_CATEGORY_GENERIC);
    return owner;
}

Eterm erts_export_literal_create(ErtsExportLiterals *owner, Export *export)
{
    struct ExportLiteralNode *node = erts_alloc(ERTS_ALC_T_EXPORT, sizeof(*node));
    ErtsLiteralArea *area = erts_alloc(ERTS_ALC_T_LITERAL,
                                      ERTS_LITERAL_AREA_ALLOC_SIZE(ERL_FUN_SIZE));
    ErlFunThing *lambda = (ErlFunThing *) area->start;
    Eterm term;
    area->end = area->start + ERL_FUN_SIZE;
    area->retained_namespace = NULL; /* Parent/pool lifetime, not a detached lease. */
    area->off_heap = NULL;
    lambda->thing_word = MAKE_FUN_HEADER(export->info.mfa.arity, 0, 1);
    lambda->entry.exp = export;
    term = make_fun(lambda);
    erts_set_literal_tag(&term, area->start, ERL_FUN_SIZE);
    node->area = area;
    erts_mtx_lock(&owner->lock);
    node->next = owner->head;
    owner->head = node;
    owner->count++;
    erts_mtx_unlock(&owner->lock);
    return term;
}

size_t erts_export_literals_count(ErtsExportLiterals *owner)
{
    size_t count;
    erts_mtx_lock(&owner->lock);
    count = owner->count;
    erts_mtx_unlock(&owner->lock);
    return count;
}

void erts_export_literals_bind(ErtsExportLiterals *owner)
{
    /* Control-thread operation before execution publishes references. */
    owner->bound = 1;
}

int erts_export_literals_can_discard(ErtsExportLiterals *owner)
{
    return owner && !owner->bound;
}

int erts_export_literals_discard(ErtsExportLiterals *owner)
{
    struct ExportLiteralNode *node, *next;
    if (!erts_export_literals_can_discard(owner))
        return 1;
    for (node = owner->head; node; node = next) {
        next = node->next;
        erts_release_literal_area(node->area);
        erts_free(ERTS_ALC_T_EXPORT, node);
    }
    erts_mtx_destroy(&owner->lock);
    erts_free(ERTS_ALC_T_EXPORT_TABLE, owner);
    return 0;
}
