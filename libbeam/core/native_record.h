/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Heap-compatible native-record layout, matching the field positions in
 * OTP's ErtsRecordDefinition and ErtsRecordInstance.
 */
#ifndef LIBBEAM_CORE_NATIVE_RECORD_H
#define LIBBEAM_CORE_NATIVE_RECORD_H
#include "term.h"
#include "beam_program_internal.h"
#include "process_internal.h"
typedef struct {
    Eterm thing_word, field_order, hash, module, name, is_exported;
    Eterm keys[];
} LbRecordTermDefinition;
typedef struct {
    Eterm thing_word, record_definition;
    Eterm values[];
} LbRecordTermInstance;
static inline LbRecordTermInstance *lb_record_instance(Eterm term)
{
    return is_record(term)?(LbRecordTermInstance *)record_val(term):NULL;
}
static inline size_t lb_record_term_field_count(Eterm term)
{
    return ((size_t)(lb_record_instance(term)->thing_word>>_HEADER_ARITY_OFFS))-1;
}
static inline LbRecordTermDefinition *lb_record_term_definition(Eterm term)
{
    LbRecordTermInstance *instance=lb_record_instance(term);
    if(!instance || !is_tuple(instance->record_definition)) return NULL;
    return (LbRecordTermDefinition *)tuple_val(instance->record_definition);
}
/* Updates are emitted pairs of atom keys and source terms; the latter are
 * resolved only after a possible GC has refreshed the process registers. */
Eterm lb_record_create(LbProcess *,const LbRecordDefinition *,Eterm module,
                       size_t live,size_t update_words,const BeamInstr *updates);
Eterm lb_record_update(LbProcess *,Eterm source,size_t live,
                       size_t update_words,const BeamInstr *updates);
Eterm lb_record_get_field(LbProcess *,Eterm source,Eterm id,Eterm key,int local);
#endif
