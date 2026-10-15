/* SPDX-License-Identifier: Apache-2.0
 * Copyright Ericsson AB 1999-2025. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Selected erl_binary.h / erl_bits.h / erl_message.h layouts. Native payloads
 * are retained by real heap/literal BinRefs; writable terms are process-owned.
 */
#ifndef LIBBEAM_CORE_BINARY_H
#define LIBBEAM_CORE_BINARY_H
#include "alloc.h"
#include "term.h"
#include <stdatomic.h>

#define LB_ONHEAP_BINARY_LIMIT 64u
#define LB_MAX_BINARY_BYTES (64u * 1024u * 1024u)
#define LB_BINARY_WRITABLE ((Uint)1<<2)
#define LB_BINARY_ACTIVE_WRITER ((Uint)1<<3)
#define LB_SUB_MUTABLE ((Uint)1)
#define LB_SUB_VOLATILE ((Uint)2)
#define LB_SUB_WRITABLE (LB_SUB_MUTABLE|LB_SUB_VOLATILE)

typedef struct {
    struct {
        Uint flags, apparent_size;
        _Atomic Uint refc;
    } intern;
    Sint orig_size;
    unsigned char orig_bytes[];
} LbBinary;
typedef struct LbBinRef {
    Eterm thing_word;
    LbBinary *val;
    struct LbBinRef *next;
} LbBinRef;
typedef struct {
    Eterm thing_word;
    Uint base_flags, start, end;
    Eterm orig;
} LbSubBits;
typedef struct {
    LbBinRef *first;
    uint64_t overhead; /* allocated payload words, as in ErlOffHeap */
} LbOffHeap;

#define LB_BIN_REF_WORDS (sizeof(LbBinRef)/sizeof(Eterm))
#define LB_SUB_BITS_WORDS (sizeof(LbSubBits)/sizeof(Eterm))
#define LB_REFC_BITS_WORDS (LB_BIN_REF_WORDS+LB_SUB_BITS_WORDS)
#define LB_BITSTRING_TAIL_HEAP_NEED 10
#define LB_HEADER_BIN_REF _make_header(LB_BIN_REF_WORDS-1,BIN_REF_SUBTAG)
#define LB_HEADER_SUB_BITS _make_header(LB_SUB_BITS_WORDS-1,SUB_BITS_SUBTAG)
_Static_assert(sizeof(_Atomic Uint)==sizeof(Uint), "native reference-count word");
_Static_assert(offsetof(LbBinary,orig_size)==3*sizeof(Eterm), "native binary header");
_Static_assert(offsetof(LbBinary,orig_bytes)==4*sizeof(Eterm), "native binary payload alignment");
_Static_assert(LB_BIN_REF_WORDS==3 && offsetof(LbBinRef,next)==2*sizeof(Eterm), "native offheap link");
_Static_assert(LB_SUB_BITS_WORDS==5 && offsetof(LbSubBits,orig)==4*sizeof(Eterm), "native sub-bits root");

/* Internal serialized interfaces. Sizes/terms refer to valid owned storage.
 * Only the reference counter is atomic; this does NOT make domains thread safe.
 * A domain cannot be destroyed while any binary allocated in it remains live.
 */
size_t lb_binary_allocation_size(size_t bytes); /* zero when over the profile limit */
int lb_binary_retain(LbBinary *); /* immutable only; checked overflow/refusal */
void lb_binary_release(LbBinary *);
size_t lb_bitstring_heap_words(size_t bits);
/* Caller reserves the returned heap size. No heap/list/output mutation on failure.
 * Copies source bytes, masks trailing unused bits; flags is 0 or TAG_LITERAL_PTR. */
LbAllocStatus lb_bitstring_build(LbAllocDomain *,LbOffHeap *,Eterm **heap,
                                const void *bytes,size_t bits,Uint flags,Eterm *out);
/* Drop references before freeing their heap/arena. The stop node is an existing
 * chain prefix checkpoint, used to undo construction/copy without allocation. */
void lb_offheap_rollback(LbOffHeap *,LbBinRef *stop);
void lb_offheap_clear(LbOffHeap *);
void lb_offheap_sweep(LbOffHeap *); /* after forwarding; before old heap release */
/* Copy a SubBits with immutable backing and its BinRef, like copy_struct.
 * Frozen volatile views resolve/canonicalize the destination only. Pin writable
 * backing explicitly before copying; no mutation of the source on failure. */
int lb_bitstring_copy_ref(const LbSubBits *,LbOffHeap *,Eterm **heap,Eterm *out);
LbAllocStatus lb_bitstring_tail(LbAllocDomain *,LbOffHeap *,Eterm **heap,
                                const LbSubBits *,Eterm *out);
LbAllocStatus lb_bitstring_slice(LbAllocDomain *,LbOffHeap *,Eterm **heap,
                                 const LbSubBits *,size_t bits,Eterm *out);

typedef struct {
    const unsigned char *data;
    size_t bit_offset, bit_size;
} LbBitstringView;
/* Borrowed until collection/mutation/destruction. Valid owned terms only, not
 * an arbitrary-word validator. False for non-bitstrings; clears the view. */
int lb_bitstring_view(Eterm,LbBitstringView *);
/* Read-only byte-length predicate; starting bit alignment is irrelevant. */
int lb_is_binary(Eterm);
/* Explicit native pin before sharing/immutable flat-copy. Infallible for valid
 * bitstrings: revokes backing mutability and canonicalizes this view. Capacity
 * is retained until final release; other volatile views still resolve via BinRef.
 * A frozen reference may remain on its process writable chain until GC. */
int lb_bitstring_pin(Eterm);
#endif
