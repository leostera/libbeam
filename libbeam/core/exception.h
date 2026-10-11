/* SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 1996-2026. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Native error.h encoding. Identities confer no execution capability.
 */
#ifndef LIBBEAM_CORE_EXCEPTION_H
#define LIBBEAM_CORE_EXCEPTION_H
#include "alloc.h"
#include "term.h"
typedef struct LbProcess LbProcess;
#define LB_EXTAG_OFFSET 0
#define LB_EXTAG_BITS 2
#define LB_EXTAG_ERROR 0
#define LB_EXTAG_EXIT 1
#define LB_EXTAG_THROWN 2
#define LB_NUMBER_EXC_TAGS 3
#define LB_EXC_CLASSBITS ((1<<LB_EXTAG_BITS)-1)
#define LB_GET_EXC_CLASS(x) ((x)&LB_EXC_CLASSBITS)
#define LB_EXF_OFFSET LB_EXTAG_BITS
#define LB_EXF_BITS 8
#define LB_EXF_PANIC (1<<(0+LB_EXF_OFFSET))
#define LB_EXF_THROWN (1<<(1+LB_EXF_OFFSET))
#define LB_EXF_LOG (1<<(2+LB_EXF_OFFSET))
#define LB_EXF_NATIVE (1<<(3+LB_EXF_OFFSET))
#define LB_EXF_SAVETRACE (1<<(4+LB_EXF_OFFSET))
#define LB_EXF_ARGLIST (1<<(5+LB_EXF_OFFSET))
#define LB_EXF_RESTORE_NFUNC (1<<(6+LB_EXF_OFFSET))
#define LB_EXF_HAS_EXT_INFO (1<<(7+LB_EXF_OFFSET))
#define LB_EXC_FLAGBITS (((1<<(LB_EXF_BITS+LB_EXF_OFFSET))-1)&~((1<<LB_EXF_OFFSET)-1))
#define LB_EXF_PRIMARY (LB_EXF_PANIC|LB_EXF_THROWN|LB_EXF_LOG|LB_EXF_NATIVE)
#define LB_PRIMARY_EXCEPTION(x) ((x)&(LB_EXF_PRIMARY|LB_EXC_CLASSBITS))
#define LB_NATIVE_EXCEPTION(x) ((x)|LB_EXF_NATIVE)
#define LB_EXC_OFFSET (LB_EXF_OFFSET+LB_EXF_BITS)
#define LB_EXC_BITS 5
#define LB_EXC_INDEXBITS (((1<<(LB_EXC_BITS+LB_EXC_OFFSET))-1)&~((1<<LB_EXC_OFFSET)-1))
#define LB_GET_EXC_INDEX(x) (((x)&LB_EXC_INDEXBITS)>>LB_EXC_OFFSET)
#define LB_EXC_NULL 0
#define LB_EXC_PRIMARY LB_EXF_SAVETRACE
#define LB_EXC_ERROR (LB_EXC_PRIMARY|LB_EXTAG_ERROR|LB_EXF_LOG)
#define LB_EXC_EXIT (LB_EXC_PRIMARY|LB_EXTAG_EXIT)
#define LB_EXC_THROWN (LB_EXC_PRIMARY|LB_EXTAG_THROWN|LB_EXF_THROWN)
#define LB_EXC_ERROR_2 (LB_EXC_ERROR|LB_EXF_ARGLIST)
#define LB_EXC_ERROR_3 (LB_EXC_ERROR|LB_EXF_ARGLIST|LB_EXF_HAS_EXT_INFO)
#define LB_EXC_NORMAL ((1<<LB_EXC_OFFSET)|LB_EXC_EXIT)
#define LB_EXC_INTERNAL_ERROR ((2<<LB_EXC_OFFSET)|LB_EXC_ERROR|LB_EXF_PANIC)
#define LB_EXC_BADARG ((3<<LB_EXC_OFFSET)|LB_EXC_ERROR)
#define LB_EXC_BADARITH ((4<<LB_EXC_OFFSET)|LB_EXC_ERROR)
#define LB_EXC_BADMATCH ((5<<LB_EXC_OFFSET)|LB_EXC_ERROR)
#define LB_EXC_FUNCTION_CLAUSE ((6<<LB_EXC_OFFSET)|LB_EXC_ERROR)
#define LB_EXC_CASE_CLAUSE ((7<<LB_EXC_OFFSET)|LB_EXC_ERROR)
#define LB_EXC_IF_CLAUSE ((8<<LB_EXC_OFFSET)|LB_EXC_ERROR)
#define LB_EXC_UNDEF ((9<<LB_EXC_OFFSET)|LB_EXC_ERROR)
#define LB_EXC_BADFUN ((10<<LB_EXC_OFFSET)|LB_EXC_ERROR)
#define LB_EXC_BADARITY ((11<<LB_EXC_OFFSET)|LB_EXC_ERROR)
#define LB_EXC_TIMEOUT_VALUE ((12<<LB_EXC_OFFSET)|LB_EXC_ERROR)
#define LB_EXC_NOPROC ((13<<LB_EXC_OFFSET)|LB_EXC_ERROR)
#define LB_EXC_NOTALIVE ((14<<LB_EXC_OFFSET)|LB_EXC_ERROR)
#define LB_EXC_SYSTEM_LIMIT ((15<<LB_EXC_OFFSET)|LB_EXC_ERROR)
#define LB_EXC_TRY_CLAUSE ((16<<LB_EXC_OFFSET)|LB_EXC_ERROR)
#define LB_EXC_NOTSUP ((17<<LB_EXC_OFFSET)|LB_EXC_ERROR)
#define LB_EXC_BADMAP ((18<<LB_EXC_OFFSET)|LB_EXC_ERROR)
#define LB_EXC_BADKEY ((19<<LB_EXC_OFFSET)|LB_EXC_ERROR)
#define LB_EXC_BADRECORD ((20<<LB_EXC_OFFSET)|LB_EXC_ERROR)
#define LB_EXC_BADFIELD ((21<<LB_EXC_OFFSET)|LB_EXC_ERROR)
#define LB_EXC_NOVALUE ((22<<LB_EXC_OFFSET)|LB_EXC_ERROR)
#define LB_NUMBER_EXIT_CODES 23
#define LB_TRAP (1<<LB_EXC_OFFSET)
/* Field/range validation only; not permission to raise a class, use a flag,
 * call a native function or admit an instruction. NULL/TRAP are not errors. */
int lb_exception_code_valid(Uint);
/* Native shorthand descriptor/class tables; primary index has no shorthand. */
Eterm lb_exception_atom(Uint);
Eterm lb_exception_class(Uint);
/* Native expand_error_value, with fallible final reservation and explicit
 * rooting of the incoming owned value (already unwrapped by the caller if
 * an arglist wrapper exists). Output is stable/non-overlapping and unchanged
 * on refusal. Preserves reason, pending value and heap credit.
 * No stacktrace, catch/unwind, promotion of uncaught throws or terminal state
 * transition is performed here. Internal-error/panic codes are refused. */
LbAllocStatus lb_exception_expand(LbProcess *,Uint,Eterm,Eterm *);
#endif
