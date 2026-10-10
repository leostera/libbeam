/*
 * %CopyrightBegin%
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Copyright Ericsson AB 2012-2026. All Rights Reserved.
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

#include "code_ix.h"
#include "global.h"
#include "beam_catches.h"
#include "erl_record.h"
#include "erl_isolate_state.h"
#include "erl_code_table.h"
#include "erl_module_namespace.h"
#include "beam_ranges.h"


#if 0
# define CIX_TRACE(text) erts_fprintf(stderr, "CIX_TRACE: " text " act=%u load=%u\r\n", erts_active_code_ix(), erts_staging_code_ix())
#else
# define CIX_TRACE(text)
#endif

#if defined(BEAMASM) && defined(ERTS_THR_INSTRUCTION_BARRIER)
#    define CODE_IX_ISSUE_INSTRUCTION_BARRIERS
#endif

/* If we need to issue a code barrier when thread progress is blocked, we use
 * this counter to signal all managed threads to execute an instruction barrier
 * when thread progress is unblocked. */
erts_atomic32_t outstanding_blocking_code_barriers;

struct code_permission {
    erts_mtx_t lock;

    Process *owner;
    void (*aux_func)(void*);
    void *aux_arg;

    bool seized;
    struct code_permission_queue_item *first;
    struct code_permission_queue_item **tail_p;

    erts_aint32_t xstate_handover_flg; /* Immutable after construction. */

#ifdef ERTS_ENABLE_LOCK_CHECK
    int lc_soft_check;
#endif
};

struct code_permission_queue_item {
    Process *p;
    void (*aux_func)(void *);
    void *aux_arg;

    struct code_permission_queue_item *next;
};

enum CodeSpacePhase { CODE_SPACE_IDLE, CODE_SPACE_STAGING, CODE_SPACE_READY };
struct ErtsCodeSpace {
    ErtsIsolateNamespaceState *namespace;
    erts_atomic32_t active, staging;
    struct code_permission modification, staging_permission;
    enum CodeSpacePhase phase;
    int bound;
};
static ErtsCodeSpace *diagnostic_code_space;

static void code_permission_init(struct code_permission* perm,
                                 const char* name)
{
    erts_mtx_init(&(perm->lock), name, NIL,
                  ERTS_LOCK_FLAGS_CATEGORY_GENERIC);
    perm->seized = false;
    perm->first = NULL;
    perm->tail_p = &(perm->first);
}


#ifdef DEBUG
static erts_tsd_key_t needs_code_barrier;
#endif

ErtsCodeSpace *erts_code_space_create(ErtsIsolateNamespaceState *namespace)
{
    ErtsCodeSpace *owner = erts_alloc(ERTS_ALC_T_MODULE_TABLE, sizeof(*owner));
    sys_memset(owner, 0, sizeof(*owner));
    owner->namespace = namespace;
    owner->phase = CODE_SPACE_IDLE;
    erts_atomic32_init_nob(&owner->active, 0);
    erts_atomic32_init_nob(&owner->staging, 1);
    owner->modification.xstate_handover_flg = ERTS_PXSFLG_HANDOVER_CODE_MOD_PERM;
    owner->staging_permission.xstate_handover_flg = ERTS_PXSFLG_HANDOVER_CODE_STAGE_PERM;
    code_permission_init(&owner->modification, "code_mod_permission");
    code_permission_init(&owner->staging_permission, "code_stage_permission");
    return owner;
}
ErtsCodeIndex erts_code_space_active(ErtsCodeSpace *owner) { return erts_atomic32_read_nob(&owner->active); }
ErtsCodeIndex erts_code_space_staging(ErtsCodeSpace *owner) { return erts_atomic32_read_nob(&owner->staging); }
erts_atomic32_t *erts_diagnostic_active_code_index(void) { return &diagnostic_code_space->active; }
erts_atomic32_t *erts_diagnostic_staging_code_index(void) { return &diagnostic_code_space->staging; }
int erts_code_space_can_discard(ErtsCodeSpace *owner)
{
    return owner && !owner->bound && owner->phase == CODE_SPACE_IDLE &&
        !owner->modification.seized && !owner->modification.first &&
        !owner->staging_permission.seized && !owner->staging_permission.first;
}
int erts_code_space_discard(ErtsCodeSpace *owner)
{
    if (!erts_code_space_can_discard(owner)) return 1;
    erts_mtx_destroy(&owner->modification.lock);
    erts_mtx_destroy(&owner->staging_permission.lock);
    erts_free(ERTS_ALC_T_MODULE_TABLE, owner);
    return 0;
}
void erts_code_ix_init(ErtsCodeSpace *owner)
{
    ASSERT(owner && !diagnostic_code_space);
    diagnostic_code_space = owner;
    owner->bound = 1;
    owner->phase = CODE_SPACE_STAGING;
    /* We start emulator by initializing preloaded modules
     * single threaded with active and staging set both to zero.
     * Preloading is finished by a commit that will set things straight.
     */
    erts_atomic32_init_nob(&outstanding_blocking_code_barriers, 0);
    erts_atomic32_set_nob(&owner->active, 0);
    erts_atomic32_set_nob(&owner->staging, 0);

#ifdef DEBUG
    erts_tsd_key_create(&needs_code_barrier,
                        "erts_needs_code_barrier");
#endif
    CIX_TRACE("init");
}

int erts_code_space_start(ErtsCodeSpace *owner, int num_new)
{
    ErtsIsolateNamespaceState *ns = owner->namespace;
    ErtsCodeIndex src = erts_code_space_active(owner), dst = erts_code_space_staging(owner);
    if (owner->phase != CODE_SPACE_IDLE || num_new < 0) return 1;
    /* Exclusive staging authority keeps this complete preflight stable. No
     * child is mutated when a later child refuses capacity/identity/state. */
    if (erts_catch_namespace_check_staging(erts_isolate_namespace_catches(ns), src, dst) ||
        erts_code_table_check_staging(erts_isolate_namespace_funs(ns), src, dst) ||
        erts_export_namespace_check_staging(erts_isolate_namespace_exports(ns), src, dst) ||
        erts_code_table_check_staging(erts_isolate_namespace_records(ns), src, dst) ||
        erts_module_namespace_check_staging(erts_isolate_namespace_module_state(ns), src, dst) ||
        erts_range_namespace_check_staging(erts_isolate_namespace_ranges(ns), src, dst, num_new))
        return 1;
    if (erts_catch_namespace_start_staging(erts_isolate_namespace_catches(ns), src, dst) ||
        erts_code_table_start_staging(erts_isolate_namespace_funs(ns), src, dst) ||
        erts_export_namespace_start_staging(erts_isolate_namespace_exports(ns), src, dst) ||
        erts_code_table_start_staging(erts_isolate_namespace_records(ns), src, dst) ||
        erts_module_namespace_start_staging(erts_isolate_namespace_module_state(ns), src, dst) ||
        erts_range_namespace_start_staging(erts_isolate_namespace_ranges(ns), src, dst, num_new))
        erts_exit(ERTS_ABORT_EXIT, "Code-space preflight lost exclusive ownership\n");
    owner->phase = CODE_SPACE_STAGING;
    return 0;
}
int erts_code_space_end(ErtsCodeSpace *owner, int commit)
{
    ErtsIsolateNamespaceState *ns = owner->namespace;
    ErtsCodeIndex dst = erts_code_space_staging(owner);
    if (owner->phase != CODE_SPACE_STAGING ||
        erts_module_namespace_check_end(erts_isolate_namespace_module_state(ns), dst, commit))
        return 1;
    /* Borrowed children must not be independently staged while this transaction
     * owns them. Code/literal retirement and instruction barriers are separate
     * execution obligations, not inferred from a table-level abort or commit. */
    if (erts_catch_namespace_end_staging(erts_isolate_namespace_catches(ns), dst) ||
        erts_code_table_end_staging(erts_isolate_namespace_funs(ns), dst) ||
        erts_export_namespace_end_staging(erts_isolate_namespace_exports(ns), dst) ||
        erts_code_table_end_staging(erts_isolate_namespace_records(ns), dst) ||
        erts_module_namespace_end_staging(erts_isolate_namespace_module_state(ns), dst, commit) ||
        erts_range_namespace_end_staging(erts_isolate_namespace_ranges(ns), commit))
        erts_exit(ERTS_ABORT_EXIT, "Code-space transaction lost child ownership\n");
    owner->phase = commit ? CODE_SPACE_READY : CODE_SPACE_IDLE;
    return 0;
}
int erts_code_space_commit(ErtsCodeSpace *owner)
{
    ErtsIsolateNamespaceState *ns = owner->namespace;
    ErtsExportNamespace *exports = erts_isolate_namespace_exports(ns);
    ErtsCodeTable *funs = erts_isolate_namespace_funs(ns), *records = erts_isolate_namespace_records(ns);
    ErtsCodeIndex ix;
    if (owner->phase != CODE_SPACE_READY) return 1;
    erts_export_namespace_write_lock(exports);
    erts_code_table_write_lock(funs);
    erts_code_table_write_lock(records);
    ix = erts_code_space_staging(owner);
    erts_atomic32_set_nob(&owner->active, ix);
    erts_atomic32_set_nob(&owner->staging, (ix + 1) % ERTS_NUM_CODE_IX);
    owner->phase = CODE_SPACE_IDLE;
    erts_code_table_write_unlock(records);
    erts_code_table_write_unlock(funs);
    erts_export_namespace_write_unlock(exports);
    return 0;
}
void erts_start_staging_code_ix(int num_new)
{
    ERTS_LC_ASSERT(!erts_initialized || erts_has_code_stage_permission());
    if (erts_code_space_start(diagnostic_code_space, num_new))
        erts_exit(ERTS_ABORT_EXIT, "Cannot stage diagnostic code space\n");
    CIX_TRACE("start");
}
void erts_end_staging_code_ix(void)
{
    ERTS_LC_ASSERT(erts_active_code_ix() == erts_staging_code_ix() || erts_has_code_stage_permission());
    if (erts_code_space_end(diagnostic_code_space, 1))
        erts_exit(ERTS_ABORT_EXIT, "Cannot end diagnostic code space\n");
    CIX_TRACE("end");
}
void erts_commit_staging_code_ix(void)
{
    if (erts_code_space_commit(diagnostic_code_space))
        erts_exit(ERTS_ABORT_EXIT, "Cannot commit diagnostic code space\n");
    erts_tracer_nif_clear();
    CIX_TRACE("activate");
}
void erts_abort_staging_code_ix(void)
{
    ERTS_LC_ASSERT(erts_active_code_ix() == erts_staging_code_ix() || erts_has_code_stage_permission());
    if (erts_code_space_end(diagnostic_code_space, 0))
        erts_exit(ERTS_ABORT_EXIT, "Cannot abort diagnostic code space\n");
    CIX_TRACE("abort");
}

#if defined(DEBUG) || defined(ADDRESS_SANITIZER)
/*
 * To make sure we can handle failed attempts correctly at all call sites,
 * always force a fail at the first attempt.
 */
static bool CWP_DBG_FORCE_TRAP(Process *c_p)
{
    if (erts_atomic32_read_nob(&c_p->xstate)
        & (ERTS_PXSFLG_HANDOVER_CODE_MOD_PERM |
           ERTS_PXSFLG_HANDOVER_CODE_STAGE_PERM)) {
        // Don't fail when we already got the permission
        return false;
    }

    if (!(c_p->flags & F_DBG_FORCED_TRAP)) {
        c_p->flags |= F_DBG_FORCED_TRAP;
        return true;
    } else {
        // back from forced trap
        c_p->flags &= ~F_DBG_FORCED_TRAP;
        return false;
    }
}
#else
# define CWP_DBG_FORCE_TRAP(P) false
#endif

#ifdef ERTS_ENABLE_LOCK_CHECK
static int has_code_permission(struct code_permission *lock, Process *owner);
#endif

static bool try_seize_code_permission(struct code_permission *perm,
                                      Process* c_p,
                                      void (*aux_func)(void *),
                                      void *aux_arg)
{
    bool success;

    ASSERT(!!c_p != !!aux_func);
    ASSERT(!!aux_arg == !!aux_func);
    ERTS_LC_ASSERT(!erts_thr_progress_is_blocking()); /* To avoid deadlock */

    if (c_p) {
        if (erts_atomic32_read_nob(&c_p->xstate) & perm->xstate_handover_flg) {
            /*
             * This process already got the permission.
             * Must have been given to us as waiter.
             */
            ASSERT(perm->seized);
            ASSERT(perm->owner == c_p);
            ASSERT(!perm->aux_arg && !perm->aux_func);
            (void) erts_atomic32_read_band_nob(&c_p->xstate,
                                               ~(perm->xstate_handover_flg));
            return true;
        }
    }


    erts_mtx_lock(&perm->lock);

    if (!perm->seized) {
        perm->owner = c_p;
        perm->aux_func = aux_func;
        perm->aux_arg = aux_arg;
        perm->seized = true;
        success = true;
    }
    else if (aux_func && aux_func == perm->aux_func
             && aux_arg == perm->aux_arg) {
        /*
         * This aux job already got the permission.
         * Must have been given to us as waiter.
         */
        ASSERT(!perm->owner);
        success = true;
    }
    else { /* Locked by someone else */
        struct code_permission_queue_item* qitem;

        qitem = erts_alloc(ERTS_ALC_T_CODE_IX_LOCK_Q, sizeof(*qitem));
        if (c_p) {
            ERTS_LC_ASSERT(perm->owner != c_p);

            qitem->p = c_p;
            qitem->aux_func = NULL;
            qitem->aux_arg = NULL;
            erts_proc_inc_refc(c_p);
            erts_suspend(c_p, ERTS_PROC_LOCK_MAIN, NULL);
        } else {
            qitem->p = NULL;
            qitem->aux_func = aux_func;
            qitem->aux_arg = aux_arg;
        }

        // Link last in wait queue
        qitem->next = NULL;
        ASSERT(*(perm->tail_p) == NULL);
        *(perm->tail_p) = qitem;
        perm->tail_p = &(qitem->next);

        success = false;
    }

    erts_mtx_unlock(&perm->lock);

    return success;
}

static void release_code_permission(struct code_permission *perm,
                                    Process* owner)
{
    ERTS_LC_ASSERT(has_code_permission(perm, owner));

    erts_mtx_lock(&perm->lock);

    perm->seized = false;
    perm->owner = NULL;
    perm->aux_func = NULL;
    perm->aux_arg = NULL;
#ifdef ERTS_ENABLE_LOCK_CHECK
    perm->lc_soft_check = 0;
#endif

    /* Find first eligible waiter */
    while (perm->first != NULL && !perm->seized) {
        struct code_permission_queue_item* qitem = perm->first;

        perm->first = qitem->next;
        if (perm->first == NULL) {
            perm->tail_p = &(perm->first);
        }

        if (qitem->p) {
            erts_proc_lock(qitem->p, ERTS_PROC_LOCK_STATUS);

            if (!ERTS_PROC_IS_EXITING(qitem->p)) {
                erts_aint32_t xstate;

                /*
                 * Avoid processes doing potentially long GC.
                 * Resume them and let them retry after GC is done.
                 * This can only happen if someone has sent the waiting
                 * suspended process a GC signal.
                 */
                xstate = erts_atomic32_read_nob(&(qitem->p->xstate));
                while (!(xstate & ERTS_PXSFLG_GC)) {
                    erts_aint32_t act =
                        erts_atomic32_cmpxchg_nob(&(qitem->p->xstate),
                                                  xstate | perm->xstate_handover_flg,
                                                  xstate);
                    if (act == xstate) {
                        perm->seized = true;
                        perm->owner = qitem->p;
                        break;
                    }
                    xstate = act;
                }
                erts_resume(qitem->p, ERTS_PROC_LOCK_STATUS);
            }

            erts_proc_unlock(qitem->p, ERTS_PROC_LOCK_STATUS);
            erts_proc_dec_refc(qitem->p);
        } else { /* aux work */
            ErtsSchedulerData *esdp = erts_get_scheduler_data();
            ASSERT(esdp && esdp->type == ERTS_SCHED_NORMAL);
            perm->seized = true;
            perm->aux_func = qitem->aux_func;
            perm->aux_arg = qitem->aux_arg;
            erts_schedule_misc_aux_work((int) esdp->no,
                                        qitem->aux_func,
                                        qitem->aux_arg);
        }

        erts_free(ERTS_ALC_T_CODE_IX_LOCK_Q, qitem);
    }

    erts_mtx_unlock(&perm->lock);
}

bool erts_try_seize_code_mod_permission_aux(void (*aux_func)(void *),
                                           void *aux_arg)
{
    ASSERT(aux_func != NULL);
    return try_seize_code_permission(&diagnostic_code_space->modification, NULL,
                                     aux_func, aux_arg);
}

#ifdef ERTS_ENABLE_LOCK_CHECK
void erts_lc_soften_code_mod_permission_check(void)
{
    diagnostic_code_space->modification.lc_soft_check = 1;
}
#endif

bool erts_try_seize_code_mod_permission(Process* c_p)
{
    ASSERT(c_p != NULL);

    if (CWP_DBG_FORCE_TRAP(c_p))
        return false;

    return try_seize_code_permission(&diagnostic_code_space->modification, c_p, NULL, NULL);
}

void erts_release_code_mod_permission(void)
{
    release_code_permission(&diagnostic_code_space->modification, NULL);
}

bool erts_try_seize_code_stage_permission(Process* c_p)
{
    ASSERT(c_p != NULL);

    if (CWP_DBG_FORCE_TRAP(c_p))
        return false;

    return try_seize_code_permission(&diagnostic_code_space->staging_permission, c_p, NULL, NULL);
}

void erts_release_code_stage_permission(void) {
    release_code_permission(&diagnostic_code_space->staging_permission, NULL);
}

bool erts_try_seize_code_load_permission(Process* c_p) {
    ASSERT(c_p != NULL);

    if (CWP_DBG_FORCE_TRAP(c_p)) {
        return false;
    }

    if (try_seize_code_permission(&diagnostic_code_space->staging_permission, c_p, NULL, NULL)) {
        if (try_seize_code_permission(&diagnostic_code_space->modification, c_p, NULL, NULL)) {
            return true;
        }

        erts_release_code_stage_permission();
    }

    return false;
}

void erts_release_code_load_permission(void) {
    erts_release_code_mod_permission();
    erts_release_code_stage_permission();
}

void erts_reject_code_permissions(Process *p) {
    const erts_aint32_t xstate = erts_atomic32_read_nob(&p->xstate);

    ASSERT(erts_atomic32_read_acqb(&p->state)
           & (ERTS_PSFLG_EXITING | ERTS_PSFLG_GC));

    if (xstate & ERTS_PXSFLG_HANDOVER_CODE_MOD_PERM) {
        release_code_permission(&diagnostic_code_space->modification, p);
        erts_atomic32_read_band_nob(&p->xstate,
                                    ~ERTS_PXSFLG_HANDOVER_CODE_MOD_PERM);
    }
    if (xstate & ERTS_PXSFLG_HANDOVER_CODE_STAGE_PERM) {
        release_code_permission(&diagnostic_code_space->staging_permission, p);
        erts_atomic32_read_band_nob(&p->xstate,
                                    ~ERTS_PXSFLG_HANDOVER_CODE_STAGE_PERM);
    }
}


#ifdef ERTS_ENABLE_LOCK_CHECK
static int has_code_permission(struct code_permission *perm,
                               Process *owner)
{
    const ErtsSchedulerData *esdp;

    if (owner) {
        return perm->seized && perm->owner == owner;
    }

    esdp = erts_get_scheduler_data();

    if (esdp && esdp->type == ERTS_SCHED_NORMAL) {
        int res;

        /*
         * We don't take perm->lock for lock order reasons.
         * This is technically not thread safe if we don't have the permission
         * seized. But in practice this should not be a problem as we only use
         * this for asserts.
         */

        if (!perm->seized) {
            res = 0;
        }
        else if (esdp->current_process != NULL) {
            /* If we're running a process, it has to match the owner of the
             * permission. We don't care about which scheduler we are running 
             * on in order to support holding permissions when yielding (such
             * as in code purging). */
            res = (perm->owner == esdp->current_process);
        } else {
            /* If we're running an aux job, we crudely assume that this current
             * job was started by the owner if there is one, and therefore has
             * permission.
             *
             * If we don't have an owner, we assume that we have permission if
             * we're running with the same 'aux_arg' as started the job.
             *
             * This is very blunt and only catches _some_ cases where we lack
             * lack permission, but at least it's better than the old method of
             * using thread-specific-data. */
            res = (perm->owner
        #ifdef ERTS_ENABLE_LOCK_CHECK
                   || perm->lc_soft_check
        #endif
                   || esdp->aux_work_data.lc_aux_arg == perm->aux_arg);
        }

        return res;
    }

    return 0;
}

int erts_has_code_load_permission(void) {
    return erts_has_code_stage_permission() && erts_has_code_mod_permission();
}

int erts_has_code_stage_permission(void) {
    return has_code_permission(&diagnostic_code_space->staging_permission, NULL);
}

int erts_has_code_mod_permission(void) {
    return has_code_permission(&diagnostic_code_space->modification, NULL);
}
#endif

#ifdef DEBUG
void erts_debug_require_code_barrier(void) {
    erts_tsd_set(needs_code_barrier, (void*)(1));
}

void erts_debug_check_code_barrier(void) {
    ASSERT(erts_tsd_get(needs_code_barrier) == (void*)0);
}

static void erts_debug_unrequire_code_barrier(void) {
    erts_tsd_set(needs_code_barrier, (void*)(0));
}
#endif

static void schedule_code_barrier_later_op(void *barrier_) {
    ErtsCodeBarrier *barrier = (ErtsCodeBarrier*)barrier_;

    if (barrier->size == 0) {
        erts_schedule_thr_prgr_later_op(barrier->later_function,
                                        barrier->later_data,
                                        &barrier->later_op);
    } else {
        erts_schedule_thr_prgr_later_cleanup_op(barrier->later_function,
                                                barrier->later_data,
                                                &barrier->later_op,
                                                barrier->size);
    }
}

#ifdef CODE_IX_ISSUE_INSTRUCTION_BARRIERS
static void issue_instruction_barrier(void *barrier_) {
    ErtsCodeBarrier *barrier = (ErtsCodeBarrier*)barrier_;

    ERTS_THR_INSTRUCTION_BARRIER;

    if (erts_refc_dectest(&barrier->pending_schedulers, 0) == 0) {
        schedule_code_barrier_later_op(barrier);
    }
}
#endif

void erts_schedule_code_barrier(ErtsCodeBarrier *barrier,
                                void (*later_function)(void *),
                                void *later_data) {
    erts_schedule_code_barrier_cleanup(barrier, later_function, later_data, 0);
}

void erts_schedule_code_barrier_cleanup(ErtsCodeBarrier *barrier,
                                        void (*later_function)(void *),
                                        void *later_data,
                                        UWord size)
{
#ifdef DEBUG
    erts_debug_unrequire_code_barrier();
#endif

    barrier->later_function = later_function;
    barrier->later_data = later_data;
    barrier->size = size;

#ifdef CODE_IX_ISSUE_INSTRUCTION_BARRIERS
    /* Issue instruction barriers on all normal schedulers, ensuring that they
     * won't execute old code.
     *
     * The last scheduler to run the barrier gets the honor of scheduling a
     * thread progress op to run the `later_function`. */
    erts_refc_init(&barrier->pending_schedulers,
                   (erts_aint_t)erts_no_schedulers);
    erts_schedule_multi_misc_aux_work(1, 1, erts_no_schedulers,
                                      issue_instruction_barrier,
                                      barrier);
    issue_instruction_barrier(barrier);
#else
    schedule_code_barrier_later_op(barrier);
#endif
}

#ifdef CODE_IX_ISSUE_INSTRUCTION_BARRIERS
static ErtsThrPrgrLaterOp global_code_barrier_lop;

static void decrement_blocking_code_barriers(void *ignored) {
    (void)ignored;

    if (erts_atomic32_dec_read_nob(&outstanding_blocking_code_barriers) > 0) {
        /* We had more than one barrier in the same tick, and can't tell
         * whether the later ones were issued before any of the managed threads
         * were woken. Keep telling all managed threads to execute an
         * instruction barrier on wake-up for one more tick. */
        erts_atomic32_set_nob(&outstanding_blocking_code_barriers, 1);
        erts_schedule_thr_prgr_later_op(decrement_blocking_code_barriers,
                                        NULL,
                                        &global_code_barrier_lop);
    }
}

static void schedule_blocking_code_barriers(void *ignored) {
    ERTS_THR_INSTRUCTION_BARRIER;

    /* Tell all managed threads to execute an instruction barrier as soon as we
     * unblock thread progress, and schedule a thread progress job to clear the
     * counter.
     *
     * Note that we increment and decrement instead of setting and clearing
     * since we might schedule several blocking barriers in the same tick. */
    if (erts_atomic32_inc_read_nob(&outstanding_blocking_code_barriers) == 1) {
        erts_schedule_thr_prgr_later_op(decrement_blocking_code_barriers,
                                        NULL,
                                        &global_code_barrier_lop);
    }
}
#endif

void erts_blocking_code_barrier(void)
{
#ifdef DEBUG
    erts_debug_unrequire_code_barrier();
#endif

    ERTS_LC_ASSERT(erts_thr_progress_is_blocking());

#ifdef CODE_IX_ISSUE_INSTRUCTION_BARRIERS
    schedule_blocking_code_barriers(NULL);
#endif
}

void erts_code_ix_finalize_wait(void) {
#ifdef CODE_IX_ISSUE_INSTRUCTION_BARRIERS
    if (erts_atomic32_read_nob(&outstanding_blocking_code_barriers) != 0) {
        ERTS_THR_INSTRUCTION_BARRIER;
    }
#endif
}
