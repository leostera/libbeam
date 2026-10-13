/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#include "process_internal.h"
#include "map_runtime.h"
typedef enum { LOOKUP_VALUE, LOOKUP_MEMBERSHIP } LookupKind;
static int require_map(LbProcess *p,Eterm map)
{
    int is_map=0; p->last_native_cost=0;
    if(!lb_work_domain_charge(p->entry_module->space->domain,1)) {
        p->status=LB_PROCESS_WORK_LIMIT; return 0;
    }
    ++p->last_native_cost;
    LbMapStatus status=lb_map_is_map(map,&is_map);
    if(status!=LB_MAP_OK) { p->status=LB_PROCESS_REFUSED; return 0; }
    if(!is_map) { p->fvalue=map; p->freason=LB_EXC_BADMAP; return 0; }
    return 1;
}
static Eterm lookup(LbProcess *p,Eterm *args,LookupKind kind)
{
    Eterm value;
    if(!require_map(p,args[1])) return THE_NON_VALUE;
    LbMapStatus status=lb_map_find(p,args[1],args[0],&value);
    if(status==LB_MAP_OK) return kind==LOOKUP_MEMBERSHIP?am_true:value;
    if(status==LB_MAP_MISSING) {
        if(kind==LOOKUP_MEMBERSHIP) return am_false;
        p->fvalue=args[0]; p->freason=LB_EXC_BADKEY; return THE_NON_VALUE;
    }
    if(status==LB_MAP_WORK_LIMIT) { p->status=LB_PROCESS_WORK_LIMIT; return THE_NON_VALUE; }
    if(status==LB_MAP_NO_MEMORY) { p->status=LB_PROCESS_NO_MEMORY; return THE_NON_VALUE; }
    if(status==LB_MAP_LIMIT) { p->status=LB_PROCESS_LIMIT; return THE_NON_VALUE; }
    p->status=LB_PROCESS_REFUSED; return THE_NON_VALUE;
}
Eterm lb_bif_map_get_2(LbProcess *p,Eterm *args,const BeamInstr *pc)
{ (void)pc; return lookup(p,args,LOOKUP_VALUE); }
Eterm lb_bif_is_map_key_2(LbProcess *p,Eterm *args,const BeamInstr *pc)
{ (void)pc; return lookup(p,args,LOOKUP_MEMBERSHIP); }
Eterm lb_bif_map_size_1(LbProcess *p,Eterm *args,const BeamInstr *pc)
{
    (void)pc;
    if(!require_map(p,args[0])) return THE_NON_VALUE;
    /* Both validated native map roots store cardinality immediately after
     * their header. Internal HAMT nodes are not valid map operands. */
    Uint size=boxed_val(args[0])[1];
    if(size>(Uint)MAX_SMALL) { p->status=LB_PROCESS_REFUSED; return THE_NON_VALUE; }
    return make_small((Sint)size);
}
