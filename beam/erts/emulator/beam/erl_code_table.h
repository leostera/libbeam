/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#ifndef ERL_CODE_TABLE_H__
#define ERL_CODE_TABLE_H__
#include "index.h"
#include "code_ix.h"
#include "erl_alloc.h"
typedef struct ErtsCodeTable ErtsCodeTable;
typedef struct {
    HashValue (*hash)(const void *);
    int (*compare)(const void *, const void *);
    void (*initialize)(void *, const void *);
    void (*stage)(void *, ErtsCodeIndex, ErtsCodeIndex);
    int (*unpublished)(const void *);
    int (*stage_ready)(const void *);
} ErtsCodeTableOps;
/* The lock/index name is borrowed and must outlive the table. */
ErtsCodeTable *erts_code_table_create(size_t, int, int, ErtsAlcType_t,
                                     ErtsAlcType_t, const char *, ErtsCodeTableOps);
void erts_code_table_bind(ErtsCodeTable *);
void erts_code_table_write_lock(ErtsCodeTable *);
void erts_code_table_write_unlock(ErtsCodeTable *);
void *erts_code_table_get(ErtsCodeTable *, const void *, ErtsCodeIndex);
void *erts_code_table_put(ErtsCodeTable *, const void *, ErtsCodeIndex);
/* Fixed diagnostic code-index protocol only, not private index selection. */
void *erts_code_table_diagnostic_upsert(ErtsCodeTable *, const void *);
int erts_code_table_check_staging(ErtsCodeTable *, ErtsCodeIndex, ErtsCodeIndex);
int erts_code_table_start_staging(ErtsCodeTable *, ErtsCodeIndex, ErtsCodeIndex);
int erts_code_table_end_staging(ErtsCodeTable *, ErtsCodeIndex);
void erts_code_table_foreach(ErtsCodeTable *, ErtsCodeIndex, void (*)(void *, void *), void *);
size_t erts_code_table_entry_bytes(ErtsCodeTable *);
size_t erts_code_table_size(ErtsCodeTable *);
void erts_code_table_info(ErtsCodeTable *, ErtsCodeIndex, ErtsCodeIndex, fmtfn_t, void *);
/* Exclusive owner, no borrowed objects or executing code. */
int erts_code_table_can_discard(ErtsCodeTable *);
int erts_code_table_discard(ErtsCodeTable *);
#endif
