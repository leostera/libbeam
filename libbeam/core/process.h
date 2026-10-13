/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#ifndef LIBBEAM_CORE_PROCESS_H
#define LIBBEAM_CORE_PROCESS_H
#include "code.h"
typedef struct LbProcess LbProcess;
/* NO_MEMORY, LIMIT, REFUSED and ENVIRONMENT are terminal infrastructure
 * outcomes, not guest exceptions or false guard results. */
typedef enum { LB_PROCESS_READY, LB_PROCESS_YIELDED, LB_PROCESS_DONE,
               LB_PROCESS_EXCEPTION, LB_PROCESS_NO_MEMORY, LB_PROCESS_INVALID,
               LB_PROCESS_LIMIT, LB_PROCESS_REFUSED, LB_PROCESS_ENVIRONMENT, LB_PROCESS_WORK_LIMIT, LB_PROCESS_CAPABILITY_DENIED } LbProcessStatus;
/* Internal synchronous executor, not host API success or a scheduler. Entry,
 * registers, stack, result and literal roots retain actual code until free.
 * Initial Eterm arguments are namespace-relative immediate values only.
 * No tenant TLS or implicit services. */
LbProcessStatus lb_process_create(const LbCodeEntry *,const Eterm *,size_t,size_t initial_heap_words,LbProcess **);
/* Real copied byte input for arity-one BEAM invocation; ordinary heap/refc
 * binary construction. Does not yet implement the public asynchronous Call. */
LbProcessStatus lb_process_create_binary(const LbCodeEntry *,const void *,size_t bytes,
                                        size_t initial_heap_words,LbProcess **);
LbProcessStatus lb_process_run(LbProcess *,size_t instruction_budget,unsigned reductions);
/* Full collection, including finished result roots. On allocation failure the
 * old heap/stack/registers are unchanged. Caller may retry. */
LbProcessStatus lb_process_collect(LbProcess *,size_t needed_words);
void lb_process_destroy(LbProcess *);
/* Borrowed until next mutating/collecting call or destruction. */
Eterm lb_process_result(const LbProcess *);
/* Compatibility shorthand category atom, separate from the native expanded
 * value. Extended info is a retained payload, NOT a captured stacktrace.
 * All return THE_NON_VALUE unless the process has a terminal exception;
 * info also requires EXF_HAS_EXT_INFO. No catch/unwind capability implied. */
Eterm lb_process_exception(const LbProcess *);
Eterm lb_process_exception_value(const LbProcess *);
Eterm lb_process_exception_info(const LbProcess *);
size_t lb_process_collections(const LbProcess *);
#endif
