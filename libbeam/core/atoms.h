/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#ifndef LIBBEAM_CORE_ATOMS_H
#define LIBBEAM_CORE_ATOMS_H
#include "term.h"
#include "beam_image.h"
typedef struct LbAtomTable LbAtomTable;
typedef struct LbAtomBinding LbAtomBinding;
typedef enum {
    LB_ATOM_OK, LB_ATOM_INVALID, LB_ATOM_BAD_UTF8, LB_ATOM_LIMIT,
    LB_ATOM_NO_MEMORY, LB_ATOM_BUSY, LB_ATOM_NOT_FOUND
} LbAtomStatus;
/* Serialized internal C interface. Tables own their strings and stable indices;
 * bare Eterms carry no owner bits. Never interpret a foreign term in this table.
 * Domain and caller-supplied allocation callbacks outlive the table/bindings.
 */
size_t lb_atoms_predefined_count(void);
LbAtomStatus lb_atoms_create(LbAllocDomain *, size_t limit, LbAtomTable **);
LbAtomStatus lb_atoms_destroy(LbAtomTable *); /* refuses live leases/bindings */
size_t lb_atoms_count(const LbAtomTable *);
LbAtomStatus lb_atoms_intern(LbAtomTable *, const void *, size_t, Eterm *);
LbAtomStatus lb_atoms_find(const LbAtomTable *, const void *, size_t, Eterm *);
LbAtomStatus lb_atoms_name(const LbAtomTable *, Eterm, LbBeamBytes *);
/* Leases require an already-valid table. Release never automatically destroys.
 * Raw borrowed names/terms require the caller to retain their owner. */
LbAtomStatus lb_atoms_retain(LbAtomTable *);
LbAtomStatus lb_atoms_release(LbAtomTable *);
/* Transactionally map file atoms to actual namespace atom terms. Failure leaves
 * namespace contents AND backing unchanged; out is NULL. Mapping owns a lease,
 * not the image. File slot zero maps to NIL, not runtime atom zero (false).
 */
LbAtomStatus lb_atom_binding_create(LbAtomTable *, const LbBeamImage *, LbAtomBinding **);
LbAtomStatus lb_atom_binding_get(const LbAtomBinding *, const LbAtomTable *expected_owner,
                                 size_t file_index, Eterm *);
void lb_atom_binding_destroy(LbAtomBinding *);
#endif
