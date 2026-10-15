/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Explicit host policy boundary, not an implementation of prohibited effects.
 */
#include "process_internal.h"

#define LB_DENIAL(id, name, arity, category) \
    Eterm lb_bif_deny_##name##_##arity(LbProcess *p,Eterm *args,const BeamInstr *pc) { \
        static const LbCapabilityFailure failure={LB_CAPABILITY_##category, "erlang", #name, arity, \
            "erlang:" #name "/" #arity " denied: " #category}; \
        _Static_assert(sizeof("erlang:" #name "/" #arity " denied: " #category)<256,"bounded denial diagnostic"); \
        (void)args; (void)pc; return lb_process_deny_capability(p,&failure); \
    }
#include "capability_denials.def"
#undef LB_DENIAL

Eterm lb_bif_deny_uninitialized_module(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    static const LbCapabilityFailure failure={LB_CAPABILITY_NATIVE_LIBRARY,"erlang","load_nif",2,
        "erlang:load_nif/2 denied: NATIVE_LIBRARY"};
    (void)args; (void)pc;
    return lb_process_deny_capability(p,&failure);
}
Eterm lb_process_deny_capability(LbProcess *p,const LbCapabilityFailure *failure)
{
    assert(p && failure && failure->module && failure->function && failure->message);
    p->last_native_cost=1;
    if(!lb_work_domain_charge(p->entry_module->space->domain,1)) {
        p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE;
    }
    p->capability_failure=failure;
    p->status=LB_PROCESS_CAPABILITY_DENIED;
    return THE_NON_VALUE;
}
