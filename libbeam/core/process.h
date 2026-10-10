/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#ifndef LIBBEAM_CORE_PROCESS_H
#define LIBBEAM_CORE_PROCESS_H
#include "code.h"
typedef struct LbProcess LbProcess;
typedef enum { LB_PROCESS_READY, LB_PROCESS_YIELDED, LB_PROCESS_DONE,
               LB_PROCESS_EXCEPTION, LB_PROCESS_NO_MEMORY, LB_PROCESS_INVALID } LbProcessStatus;
/* Internal synchronous executor, not host API success or a scheduler. Entry,
 * registers, stack, result and literal roots retain actual code until free.
 * Initial host terms are namespace-relative immediate values only. Heap/ETF
 * host marshalling is a separate API slice. No tenant TLS or implicit services. */
LbProcessStatus lb_process_create(const LbCodeEntry *,const Eterm *,size_t,size_t initial_heap_words,LbProcess **);
LbProcessStatus lb_process_run(LbProcess *,size_t instruction_budget,unsigned reductions);
/* Full collection, including finished result roots. On allocation failure the
 * old heap/stack/registers are unchanged. Caller may retry. */
LbProcessStatus lb_process_collect(LbProcess *,size_t needed_words);
void lb_process_destroy(LbProcess *);
/* Borrowed until next mutating/collecting call or destruction. */
Eterm lb_process_result(const LbProcess *);
Eterm lb_process_exception(const LbProcess *);
size_t lb_process_collections(const LbProcess *);
#endif
