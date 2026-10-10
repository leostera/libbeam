/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#ifndef LIBBEAM_CORE_ATOMS_H
#define LIBBEAM_CORE_ATOMS_H
#include "term.h"
#include "beam_image.h"
typedef struct LbAtomTable LbAtomTable;
typedef struct LbAtomBinding LbAtomBinding;
typedef struct LbAtomTransaction LbAtomTransaction;
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
/* Atomic namespace admission for prepared-module names (file + ETF atoms).
 * Caller provides disposable output storage; its contents on failure are not
 * published terms. Namespace contents/backing are unchanged on failure. */
LbAtomStatus lb_atoms_intern_names(LbAtomTable *, const LbBeamBytes *, size_t, Eterm *);
/* Code loading needs provisional identities through transformation/linking.
 * One serialized writer transaction may be open. Reads see committed names;
 * other writes and destruction refuse busy. Commit is allocation-free. */
LbAtomStatus lb_atoms_prepare_names(LbAtomTable *, const LbBeamBytes *, size_t, Eterm *, LbAtomTransaction **);
LbAtomStatus lb_atoms_transaction_name(const LbAtomTransaction *, Eterm, LbBeamBytes *);
void lb_atoms_commit(LbAtomTransaction *);
void lb_atoms_abort(LbAtomTransaction *);
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
