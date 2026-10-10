/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#ifndef ERL_MODULE_NAMESPACE_H__
#define ERL_MODULE_NAMESPACE_H__
#include "erl_module_table.h"
#include "code_ix.h"
typedef struct ErtsModuleNamespace ErtsModuleNamespace;
struct erl_module_instance;
ErtsModuleNamespace *erts_module_namespace_create(ErtsModuleTable **);
int erts_module_namespace_check_staging(ErtsModuleNamespace *, ErtsCodeIndex, ErtsCodeIndex);
int erts_module_namespace_check_end(ErtsModuleNamespace *, ErtsCodeIndex, int);
int erts_module_namespace_start_staging(ErtsModuleNamespace *, ErtsCodeIndex, ErtsCodeIndex);
int erts_module_namespace_end_staging(ErtsModuleNamespace *, ErtsCodeIndex, int);
int erts_module_namespace_unseal(ErtsModuleNamespace *, struct erl_module_instance *);
int erts_module_namespace_seal(ErtsModuleNamespace *, struct erl_module_instance *);
int erts_module_namespace_can_discard(ErtsModuleNamespace *);
int erts_module_namespace_discard(ErtsModuleNamespace *);
#endif
