/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 2000-2026. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *     http://www.apache.org/licenses/LICENSE-2.0
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef LIBBEAM_CORE_TERM_H
#define LIBBEAM_CORE_TERM_H
/* Selected representation/access definitions from erl_term.h and erl_vm.h.
 * No OS initialization, mmap, global literals, GC or alternate term model.
 * Internal C interface: pointers must be valid owned storage, not hostile words.
 */
#include <assert.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#if UINTPTR_MAX != UINT64_MAX
#error "The additive core currently requires 64-bit words and pointers"
#endif
typedef uintptr_t Eterm;
typedef uintptr_t Uint;
typedef intptr_t Sint;
_Static_assert(CHAR_BIT == 8, "BEAM byte width");
_Static_assert(sizeof(Eterm) == sizeof(void *) && sizeof(Eterm) == 8, "BEAM word width");
_Static_assert(_Alignof(Eterm) >= 8 && _Alignof(max_align_t) >= 8, "BEAM pointer tags");
_Static_assert(INTPTR_MIN == -INTPTR_MAX - 1 && ((Sint)-1 >> 1) == -1, "BEAM signed words");
#define MAX_ARG 255
#define MAX_REG 1024
#define REG_MASK (MAX_REG - 1)
#define ERTS_X_REGS_ALLOCATED (MAX_REG + 3)
#define TAG_PTR_MASK__ ((Uint)0x7)
#define TAG_LITERAL_PTR ((Uint)0x4)
#define _TAG_PRIMARY_SIZE 2
#define _TAG_PRIMARY_MASK 0x3
#define TAG_PRIMARY_HEADER 0x0
#define TAG_PRIMARY_LIST 0x1
#define TAG_PRIMARY_BOXED 0x2
#define TAG_PRIMARY_IMMED1 0x3
#define primary_tag(x) ((x) & _TAG_PRIMARY_MASK)
#define _TAG_IMMED1_SIZE 4
#define _TAG_IMMED1_MASK 0xF
#define _TAG_IMMED1_PID ((0x0 << _TAG_PRIMARY_SIZE) | TAG_PRIMARY_IMMED1)
#define _TAG_IMMED1_PORT ((0x1 << _TAG_PRIMARY_SIZE) | TAG_PRIMARY_IMMED1)
#define _TAG_IMMED1_IMMED2 ((0x2 << _TAG_PRIMARY_SIZE) | TAG_PRIMARY_IMMED1)
#define _TAG_IMMED1_SMALL ((0x3 << _TAG_PRIMARY_SIZE) | TAG_PRIMARY_IMMED1)
#define _TAG_IMMED2_SIZE 6
#define _TAG_IMMED2_MASK 0x3F
#define _TAG_IMMED2_ATOM ((0x0 << _TAG_IMMED1_SIZE) | _TAG_IMMED1_IMMED2)
#define _TAG_IMMED2_CATCH ((0x1 << _TAG_IMMED1_SIZE) | _TAG_IMMED1_IMMED2)
#define _TAG_IMMED2_NIL ((0x3 << _TAG_IMMED1_SIZE) | _TAG_IMMED1_IMMED2)
#define _TAG_HEADER_MASK 0x3F
#define _HEADER_ARITY_OFFS 6
#define _HEADER_SUBTAG_MASK 0x3C
#define __MAKE_SUBTAG(p) (((p) << _TAG_PRIMARY_SIZE) & _TAG_HEADER_MASK)
#define ARITYVAL_SUBTAG __MAKE_SUBTAG(0x0)
#define POS_BIG_SUBTAG __MAKE_SUBTAG(0x2)
#define NEG_BIG_SUBTAG __MAKE_SUBTAG(0x3)
#define REF_SUBTAG __MAKE_SUBTAG(0x4)
#define FUN_SUBTAG __MAKE_SUBTAG(0x5)
#define FLOAT_SUBTAG __MAKE_SUBTAG(0x6)
#define RECORD_SUBTAG __MAKE_SUBTAG(0x7)
#define HEAP_BITS_SUBTAG __MAKE_SUBTAG(0x8)
#define SUB_BITS_SUBTAG __MAKE_SUBTAG(0x9)
#define BIN_REF_SUBTAG __MAKE_SUBTAG(0xA)
#define MAP_SUBTAG __MAKE_SUBTAG(0xB)
#define EXTERNAL_PID_SUBTAG __MAKE_SUBTAG(0xC)
#define EXTERNAL_PORT_SUBTAG __MAKE_SUBTAG(0xD)
#define EXTERNAL_REF_SUBTAG __MAKE_SUBTAG(0xE)
#define _TAG_HEADER_ARITYVAL (TAG_PRIMARY_HEADER | ARITYVAL_SUBTAG)
#define _TAG_HEADER_FLOAT (TAG_PRIMARY_HEADER | FLOAT_SUBTAG)
#define _make_header(sz,tag) (((Uint)(sz) << _HEADER_ARITY_OFFS) + (tag))
#ifdef NDEBUG
#define THE_NON_VALUE ((Eterm)TAG_PRIMARY_HEADER)
#else
#define THE_NON_VALUE _make_header(0,_TAG_HEADER_FLOAT)
#endif
#define is_non_value(x) ((x) == THE_NON_VALUE)
#define NIL ((Eterm)_TAG_IMMED2_NIL)
#define is_nil(x) ((x) == NIL)
#define is_immed(x) (((x) & _TAG_PRIMARY_MASK) == TAG_PRIMARY_IMMED1)
#define SMALL_BITS (64-4)
#define MAX_SMALL ((((Sint)1) << (SMALL_BITS-1)) - 1)
#define MIN_SMALL (-(((Sint)1) << (SMALL_BITS-1)))
#define make_small(x) (((Uint)(x) << _TAG_IMMED1_SIZE) + _TAG_IMMED1_SMALL)
#define is_small(x) (((x) & _TAG_IMMED1_MASK) == _TAG_IMMED1_SMALL)
static inline Sint signed_val(Eterm x) { assert(is_small(x)); return (Sint)x >> _TAG_IMMED1_SIZE; }
/* Cast before shifting also makes runtime indices safe from signed-int overflow. */
#define make_atom(x) (((Eterm)(x) << _TAG_IMMED2_SIZE) + _TAG_IMMED2_ATOM)
#define is_atom(x) (((x) & _TAG_IMMED2_MASK) == _TAG_IMMED2_ATOM)
static inline Uint atom_val(Eterm x) { assert(is_atom(x)); return x >> _TAG_IMMED2_SIZE; }
#define is_header(x) (((x) & _TAG_PRIMARY_MASK) == TAG_PRIMARY_HEADER)
#define is_boxed(x) (((x) & _TAG_PRIMARY_MASK) == TAG_PRIMARY_BOXED)
#define is_list(x) (((x) & _TAG_PRIMARY_MASK) == TAG_PRIMARY_LIST)
#define _is_taggable_pointer(x) (((Uint)(x) & TAG_PTR_MASK__) == 0)
static inline Eterm make_boxed(const Eterm *p) { assert(p && _is_taggable_pointer(p)); return (Uint)p + TAG_PRIMARY_BOXED; }
static inline Eterm make_list(const Eterm *p) { assert(p && _is_taggable_pointer(p)); return (Uint)p + TAG_PRIMARY_LIST; }
static inline Eterm *ptr_val(Eterm x) { assert(is_boxed(x) || is_list(x)); return (Eterm *)(x & ~TAG_PTR_MASK__); }
static inline Eterm *boxed_val(Eterm x) { assert(is_boxed(x)); return ptr_val(x); }
static inline Eterm *list_val(Eterm x) { assert(is_list(x)); return ptr_val(x); }
#define is_literal_ptr(x) (((x) & TAG_LITERAL_PTR) != 0) /* boxed/list precondition */
#define CAR(p) ((p)[0])
#define CDR(p) ((p)[1])
#define CONS(hp,car,cdr) (CAR(hp)=(car), CDR(hp)=(cdr), make_list(hp))
#define MAX_ARITYVAL ((((Uint)1) << 24) - 1)
#define make_arityval_zero() _make_header(0,_TAG_HEADER_ARITYVAL)
#define make_arityval_unchecked(sz) (assert((Uint)(sz)<=MAX_ARITYVAL), _make_header((sz),_TAG_HEADER_ARITYVAL))
#define make_arityval(sz) (assert((sz) > 0 && (sz) <= MAX_ARITYVAL), _make_header((sz),_TAG_HEADER_ARITYVAL))
#define is_arity_value(x) (((x) & _TAG_HEADER_MASK) == _TAG_HEADER_ARITYVAL)
static inline Uint arityval(Eterm x) { assert(is_arity_value(x)); return x >> _HEADER_ARITY_OFFS; }
#define make_tuple(p) make_boxed(p)
#define tuple_val(x) boxed_val(x)
#define is_tuple(x) (is_boxed(x) && is_arity_value(*boxed_val(x)))
#define TUPLE1(t,e1) ((t)[0]=make_arityval(1), (t)[1]=(e1), make_tuple(t))
#define TUPLE2(t,e1,e2) ((t)[0]=make_arityval(2), (t)[1]=(e1), (t)[2]=(e2), make_tuple(t))
#define TUPLE3(t,e1,e2,e3) ((t)[0]=make_arityval(3), (t)[1]=(e1), (t)[2]=(e2), (t)[3]=(e3), make_tuple(t))
/* Checked construction boundaries; failure leaves output and storage unchanged.
 * Empty tuples reserve TWO words for the upstream read-ahead invariant. Storage
 * and nested terms retain their owners; this helper is not a heap/GC/validator.
 */
static inline int lb_term_small(Sint value, Eterm *out)
{
    if (!out || value < MIN_SMALL || value > MAX_SMALL) return 0;
    *out = make_small(value);
    return 1;
}
static inline int lb_term_tuple(Eterm *storage, size_t words, const Eterm *elements, size_t arity, Eterm *out)
{
    size_t needed;
    if (!out || !storage || !_is_taggable_pointer(storage) || arity > MAX_ARITYVAL ||
        (arity && !elements)) return 0;
    needed = arity ? arity + 1 : 2;
    if (words < needed) return 0;
    if (arity) memmove(storage + 1, elements, arity * sizeof(Eterm));
    else storage[1] = THE_NON_VALUE;
    storage[0] = _make_header(arity, _TAG_HEADER_ARITYVAL);
    *out = make_tuple(storage);
    return 1;
}
static inline int lb_size_mul(size_t a, size_t b, size_t *out)
{
    if (!out || (b && a > SIZE_MAX / b)) return 0;
    *out = a * b;
    return 1;
}
static inline int lb_size_add(size_t a, size_t b, size_t *out)
{
    if (!out || a > SIZE_MAX - b) return 0;
    *out = a + b;
    return 1;
}
#endif
