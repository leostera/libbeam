/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#include "process_internal.h"
#include "map_runtime.h"
#include "flatmap.h"
#include "term_compare.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"CHECK %s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while(0)

static void intern(LbAtomTable *atoms,const char *name,Eterm *out)
{ CHECK(lb_atoms_intern(atoms,name,strlen(name),out)==LB_ATOM_OK); }

int main(void)
{
    LbAllocDomain *domain; LbAtomTable *atoms; LbCodeSpace space={0};
    LbCodeModule module={0}; LbProcess process={0}; Eterm *heap,map,updated,exact;
    Eterm a,b,c,d,keys[4],values[4],found,unchanged; size_t words; int equal;
    CHECK(lb_alloc_domain_create(NULL,&domain)==LB_ALLOC_OK);
    CHECK(lb_atoms_create(domain,1048576,&atoms)==LB_ATOM_OK);
    intern(atoms,"gamma",&c); intern(atoms,"alpha",&a); intern(atoms,"beta",&b); intern(atoms,"delta",&d);
    space.domain=domain; space.atoms=atoms; module.space=&space; process.entry_module=&module;
    process.status=LB_PROCESS_READY;
    CHECK(lb_alloc_domain_allocate(domain,256*sizeof(Eterm),(void **)&heap)==LB_ALLOC_OK);
    process.heap=process.htop=heap; process.hend=heap+256; process.stop=process.hend-S_RESERVED;
    keys[0]=c; keys[1]=a; keys[2]=b; values[0]=make_small(30); values[1]=make_small(10); values[2]=make_small(20);
    CHECK(lb_map_reserve(&process,LB_FLATMAP_WORDS+3+4,MAX_REG)==LB_MAP_OK);
    CHECK(lb_map_create(&process,keys,values,3,&map)==LB_MAP_OK);
    CHECK(boxed_val(map)[0]==LB_FLATMAP_HEADER && boxed_val(map)[1]==3);
    Eterm *map_keys=tuple_val(boxed_val(map)[2]);
    CHECK(map_keys[1]==c && map_keys[2]==a && map_keys[3]==b);
    CHECK(lb_map_find(&process,map,b,&found)==LB_MAP_OK && found==make_small(20));
    CHECK(lb_map_find(&process,map,d,&found)==LB_MAP_MISSING && is_non_value(found));

    keys[0]=d; values[0]=make_small(40);
    CHECK(lb_map_reserve(&process,LB_FLATMAP_WORDS+4+5,MAX_REG)==LB_MAP_OK);
    CHECK(lb_map_update_assoc(&process,map,keys,values,1,&updated)==LB_MAP_OK);
    CHECK(boxed_val(updated)[1]==4);
    CHECK(lb_map_find(&process,updated,d,&found)==LB_MAP_OK && found==make_small(40));
    CHECK(lb_map_find(&process,map,d,&found)==LB_MAP_MISSING);

    keys[0]=b; values[0]=make_small(22);
    CHECK(lb_map_reserve(&process,LB_FLATMAP_WORDS+4,MAX_REG)==LB_MAP_OK);
    CHECK(lb_map_update_exact(&process,updated,keys,values,1,&exact)==LB_MAP_OK);
    CHECK(lb_map_find(&process,exact,b,&found)==LB_MAP_OK && found==make_small(22));
    CHECK(lb_map_find(&process,updated,b,&found)==LB_MAP_OK && found==make_small(20));
    keys[0]=a; keys[1]=make_small(99); values[0]=make_small(1); values[1]=make_small(2);
    unchanged=make_small(77);
    CHECK(lb_map_reserve(&process,LB_FLATMAP_WORDS+4,MAX_REG)==LB_MAP_OK);
    CHECK(lb_map_update_exact(&process,updated,keys,values,2,&unchanged)==LB_MAP_MISSING);
    CHECK(unchanged==make_small(77));

    /* A code-literal keys tuple stays code-owned while the new map is process-owned. */
    Eterm literal_keys[4]={0}; literal_keys[0]=make_arityval(3);
    literal_keys[1]=a; literal_keys[2]=b; literal_keys[3]=c;
    Eterm literal=make_tuple(literal_keys)|TAG_LITERAL_PTR;
    values[0]=make_small(1); values[1]=make_small(2); values[2]=make_small(3);
    CHECK(lb_map_reserve(&process,LB_FLATMAP_WORDS+3+4,MAX_REG)==LB_MAP_OK);
    CHECK(lb_map_create_literal(&process,literal,values,3,&map)==LB_MAP_OK);
    CHECK(boxed_val(map)[2]!=literal);
    map_keys=tuple_val(boxed_val(map)[2]);
    CHECK(map_keys[1]==c && map_keys[2]==a && map_keys[3]==b);
    CHECK(lb_map_find(&process,map,c,&found)==LB_MAP_OK && found==make_small(3));

    /* Published constructors consume exactly their reserved process-heap prefix. */
    CHECK((size_t)(process.htop-process.heap)==10+12+7+10);
    CHECK(process.heap_reserved==0); /* later constructor grows and consumes that credit */
    words=0; CHECK(lb_flat_size(domain,map,&words) && words==LB_FLATMAP_WORDS+3+4);
    CHECK(lb_term_equal(domain,map,map,&equal)==LB_ALLOC_OK && equal);
    lb_release(domain,heap);
    CHECK(lb_atoms_destroy(atoms)==LB_ATOM_OK);
    CHECK(lb_alloc_domain_destroy(domain)==LB_ALLOC_OK);
    puts("CORE_MAP_RUNTIME_OK flatmap_0_32=true ordered_keys=true lookup=true assoc=true exact=true literal_key_lease=true prefix_accounting=true hamt=false");
    return 0;
}
