/*
 * %CopyrightBegin%
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Copyright Ericsson AB 1996-2025. All Rights Reserved.
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

#include "sys.h"
#include "erl_sys_driver.h"
#include "erl_vm.h"
#include "global.h"
#include "hash.h"
#include "atom.h"
#include "erl_atom_namespace.h"

struct ErtsAtomNamespace {
    IndexTable index;
    erts_rwmtx_t lock;
    Uint text_bytes;
    int limit;
    erts_atomic_t put_ops;
};


#define ATOM_SIZE  3000

/* Fixed borrowed diagnostic binding, installed once by explicit startup.
 * Storage/lifetime belongs to ErtsIsolateNamespaceState, not this adapter. */
static ErtsAtomNamespace *diagnostic_atoms;

#define atom_read_lock() erts_rwmtx_rlock(&diagnostic_atoms->lock)
#define atom_read_unlock() erts_rwmtx_runlock(&diagnostic_atoms->lock)

#if 0
#define ERTS_ATOM_PUT_OPS_STAT
#endif

/*
 * Print info about atom tables
 */
void atom_info(fmtfn_t to, void *to_arg)
{
    int lock = !ERTS_IS_CRASH_DUMPING;
    if (lock)
	atom_read_lock();
    index_info(to, to_arg, &diagnostic_atoms->index);
#ifdef ERTS_ATOM_PUT_OPS_STAT
    erts_print(to, to_arg, "atom_put_ops: %ld\n",
	       erts_atomic_read_nob(&diagnostic_atoms->put_ops));
#endif

    if (lock)
	atom_read_unlock();
}




/*
 * Calculate atom hash value (using the hash algorithm
 * hashpjw from the Dragon Book).
 */

static HashValue
atom_hash(Atom* obj)
{
    byte* p = obj->u.name;
    int len = obj->len;
    HashValue h = 0, g;
    byte v;

    while(len--) {
	v = *p++;
	/* latin1 clutch for r16 */
	if (len && (v & 0xFE) == 0xC2 && (*p & 0xC0) == 0x80) {
	    v = (v << 6) | (*p & 0x3F);
	    p++; len--;
	}
	/* normal hashpjw follows for v */
	h = (h << 4) + v;
	if ((g = h & 0xf0000000)) {
	    h ^= (g >> 24);
	    h ^= g;
	}
    }
    return h;
}

const byte *erts_atom_get_name(const Atom *atom)
{
    return atom->u.name;
}

static int 
atom_cmp(Atom* tmpl, Atom* obj)
{
    if (tmpl->len == obj->len &&
	sys_memcmp(tmpl->u.name, erts_atom_get_name(obj), tmpl->len) == 0)
	return 0;
    return 1;
}


static int atom_ordinal(const byte *name, int len)
{
    unsigned char c[4] = {0, 0, 0, 0};
    int i;
    /* First three bytes + seven bits: leave the sign bit free for comparison. */
    for (i = 0; i < len && i < 4; ++i)
        c[i] = name[i];
    return (c[0] << 23) + (c[1] << 15) + (c[2] << 7) + (c[3] >> 1);
}

static void latin1_to_utf8(byte* conv_buf, Uint buf_sz,
                           const byte** srcp, Uint* lenp)
{
    byte* dst;
    const byte* src = *srcp;
    Uint i, len = *lenp;

    ASSERT(len <= MAX_ATOM_CHARACTERS);
    ASSERT(buf_sz >= MAX_ATOM_SZ_FROM_LATIN1);

    for (i=0 ; i < len; ++i) {
	if (src[i] & 0x80) {
	    goto need_convertion;
	}
    }
    return;

need_convertion:
    sys_memcpy(conv_buf, src, i);
    dst = conv_buf + i;
    for ( ; i < len; ++i) {
	unsigned char chr = src[i];
	if (!(chr & 0x80)) {
	    *dst++ = chr;
	}
	else {
	    *dst++ = 0xC0 | (chr >> 6);
	    *dst++ = 0x80 | (chr & 0x3F);
	}
    }
    *srcp = conv_buf;	
    *lenp = dst - conv_buf;
}

/*
 * erts_atom_put_index() may fail. Returns negative indexes for errors.
 */
static int
atom_put_index(ErtsAtomNamespace *owner, const byte *name, Sint len,
               ErtsAtomEncoding enc, int trunc)
{
    byte utf8_copy[MAX_ATOM_SZ_FROM_LATIN1];
    const byte *text = name;
    Uint tlen;
    Sint no_latin1_chars;
    Atom a;
    int aix;
    IndexTable *table = &owner->index;
    erts_rwmtx_t *lock = &owner->lock;

    ERTS_UNDEF(no_latin1_chars, -1);

#ifdef ERTS_ATOM_PUT_OPS_STAT
    erts_atomic_inc_nob(&owner->put_ops);
#endif

    if (len < 0) {
        if (trunc) {
            len = 0;
        } else {
            return ATOM_MAX_CHARS_ERROR;
        }
    }

    tlen = len;

    switch (enc) {
    case ERTS_ATOM_ENC_7BIT_ASCII:
	if (tlen > MAX_ATOM_CHARACTERS) {
	    if (trunc)
		tlen = MAX_ATOM_CHARACTERS;
	    else
		return ATOM_MAX_CHARS_ERROR;
	}
#ifdef DEBUG
	for (aix = 0; aix < len; aix++) {
	    ASSERT((name[aix] & 0x80) == 0);
	}
#endif
	no_latin1_chars = tlen;
	break;
    case ERTS_ATOM_ENC_LATIN1:
	if (tlen > MAX_ATOM_CHARACTERS) {
	    if (trunc)
		tlen = MAX_ATOM_CHARACTERS;
	    else
		return ATOM_MAX_CHARS_ERROR;
	}
	no_latin1_chars = tlen;
	latin1_to_utf8(utf8_copy, sizeof(utf8_copy), &text, &tlen);
	break;
    case ERTS_ATOM_ENC_UTF8:
	/* First sanity check; need to verify later */
	if (tlen > MAX_ATOM_SZ_LIMIT && !trunc)
	    return ATOM_MAX_CHARS_ERROR;
        if (tlen > ERTS_SINT16_MAX)
            return ATOM_MAX_CHARS_ERROR;
	break;
    }

    a.len = tlen;
    a.u.name = (byte *) text;
    erts_rwmtx_rlock(lock);
    aix = index_get(table, (void*) &a);
    erts_rwmtx_runlock(lock);
    if (aix >= 0) {
	/* Already in table no need to verify it */
	return aix;
    }

    if (enc == ERTS_ATOM_ENC_UTF8) {
	/* Need to verify encoding and length */
	const byte *err_pos;
	Uint no_chars;
	switch (erts_analyze_utf8_x((byte *) text,
				    (Uint) tlen,
				    &err_pos,
				    &no_chars, NULL,
				    &no_latin1_chars,
				    MAX_ATOM_CHARACTERS)) {
	case ERTS_UTF8_OK:
	    ASSERT(no_chars <= MAX_ATOM_CHARACTERS);
	    break;
	case ERTS_UTF8_OK_MAX_CHARS:
	    /* Truncated... */
	    if (!trunc)
		return ATOM_MAX_CHARS_ERROR;
	    ASSERT(no_chars == MAX_ATOM_CHARACTERS);
	    tlen = err_pos - text;
	    break;
	default:
	    /* Bad utf8... */
	    return ATOM_BAD_ENCODING_ERROR;
	}
    }

    ASSERT(tlen <= MAX_ATOM_SZ_LIMIT);
    ASSERT(-1 <= no_latin1_chars && no_latin1_chars <= MAX_ATOM_CHARACTERS);

    a.len = tlen;
    a.latin1_chars = (Sint16) no_latin1_chars;
    a.u.name = (byte *) text;
    erts_rwmtx_rwlock(lock);
    /* Recheck under the writer lock: another writer may have interned it. */
    aix = index_get(table, &a);
    if (aix < 0) {
        if (table->entries >= owner->limit)
            aix = -3;
        else {
            aix = index_put(table, &a);
            owner->text_bytes += tlen;
        }
    }
    erts_rwmtx_rwunlock(lock);
    return aix;
}

int
erts_atom_put_index(const byte *name, Sint len, ErtsAtomEncoding enc, int trunc)
{
    /* Explicit diagnostic-world adapter, not a current-isolate selector. */
    int index = atom_put_index(diagnostic_atoms, name, len, enc, trunc);
    if (index == -3)
        erts_exit(ERTS_DUMP_EXIT, "no more index entries in atom table\n");
    return index;
}

/*
 * erts_atom_put() may fail. If it fails THE_NON_VALUE is returned!
 */
Eterm
erts_atom_put(const byte *name, Sint len, ErtsAtomEncoding enc, int trunc)
{
    Sint aix = erts_atom_put_index(name, len, enc, trunc);
    if (aix >= 0)
	return make_atom(aix);
    else
	return THE_NON_VALUE;
}

Eterm
am_atom_put(const char* name, Sint len)
{
    /* Assumes 7-bit ascii; use erts_atom_put() for other encodings... */
    return erts_atom_put((byte *) name, len, ERTS_ATOM_ENC_7BIT_ASCII, 1);
}

int atom_table_size(void)
{
    int ret;
    int lock = !ERTS_IS_CRASH_DUMPING;
    if (lock)
	atom_read_lock();
    ret = diagnostic_atoms->index.entries;
    if (lock)
	atom_read_unlock();
    return ret;
}

int atom_table_sz(void)
{
    int ret;
    int lock = !ERTS_IS_CRASH_DUMPING;
    if (lock)
	atom_read_lock();
    ret = index_table_sz(&diagnostic_atoms->index);
    if (lock)
	atom_read_unlock();
    return ret;
}

int
erts_atom_get(const char *name, Uint len, Eterm* ap, ErtsAtomEncoding enc)
{
    byte utf8_copy[MAX_ATOM_SZ_FROM_LATIN1];
    Atom a;
    int i;
    int res;

    switch (enc) {
    case ERTS_ATOM_ENC_LATIN1:
        if (len > MAX_ATOM_CHARACTERS) {
            return 0;
        }

        latin1_to_utf8(utf8_copy, sizeof(utf8_copy), (const byte**)&name, &len);

        a.u.name = (byte*)name;
        a.len = (Sint16)len;
        break;
    case ERTS_ATOM_ENC_7BIT_ASCII:
        if (len > MAX_ATOM_CHARACTERS) {
            return 0;
        }

        for (i = 0; i < len; i++) {
            if (name[i] & 0x80) {
                return 0;
            }
        }

        a.len = (Sint16)len;
        a.u.name = (byte*)name;
        break;
    case ERTS_ATOM_ENC_UTF8:
        if (len > MAX_ATOM_SZ_LIMIT) {
            return 0;
        }

        /* We don't need to check whether the encoding is legal as all atom
         * names are stored as UTF-8 and we know a lookup with a badly encoded
         * name will fail. */

        a.len = (Sint16)len;
        a.u.name = (byte*)name;
        break;
    }

    atom_read_lock();
    i = index_get(&diagnostic_atoms->index, (void*) &a);
    res = i < 0 ? 0 : (*ap = make_atom(i), 1);
    atom_read_unlock();

    return res;
}

void
erts_atom_get_text_space_sizes(Uint *reserved, Uint *used)
{
    int lock = !ERTS_IS_CRASH_DUMPING;
    if (lock)
	atom_read_lock();
    if (reserved)
	*reserved = diagnostic_atoms->text_bytes;
    if (used)
	*used = diagnostic_atoms->text_bytes;
    if (lock)
	atom_read_unlock();
}

void
init_atom_table(ErtsAtomNamespace *owner)
{
    ASSERT(owner && !diagnostic_atoms);
    diagnostic_atoms = owner;
}

Atom *erts_diagnostic_atom_at(Uint i)
{
    return (Atom *) erts_index_lookup(&diagnostic_atoms->index, i);
}

int erts_diagnostic_atom_index_ok(Uint i)
{
    return i < (Uint) diagnostic_atoms->index.entries;
}

/* Private names never enter the process-global literal registry. Records and
 * UTF-8 text share one allocation, reclaimed with the owning index/hash table. */
static Atom *private_atom_alloc(Atom *tmpl)
{
    Atom *obj = erts_alloc(ERTS_ALC_T_ATOM, sizeof(*obj) + tmpl->len + 1);
    byte *text = (byte *)(obj + 1);
    sys_memcpy(text, tmpl->u.name, tmpl->len);
    text[tmpl->len] = 0;
    obj->u.name = text;
    obj->len = tmpl->len;
    obj->latin1_chars = tmpl->latin1_chars;
    obj->slot.index = -1;
    obj->ord0 = atom_ordinal(text, tmpl->len);
    return obj;
}

static void private_atom_free(Atom *atom)
{
    erts_free(ERTS_ALC_T_ATOM, atom);
}

ErtsAtomNamespace *erts_atom_namespace_create(int limit)
{
    ErtsAtomNamespace *owner;
    HashFunctions f;
    int i, predefined = 0;
    while (erl_atom_names[predefined])
        ++predefined;
    if (limit < predefined || limit > MAX_ATOM_TABLE_SIZE)
        return NULL;
    owner = erts_alloc(ERTS_ALC_T_ATOM_TABLE, sizeof(*owner));
    owner->text_bytes = 0;
    owner->limit = limit;
    erts_atomic_init_nob(&owner->put_ops, 0);
    erts_rwmtx_init(&owner->lock, "isolate_atom_tab", NIL,
                   ERTS_LOCK_FLAGS_CATEGORY_GENERIC);
    f.hash = (H_FUN) atom_hash;
    f.cmp = (HCMP_FUN) atom_cmp;
    f.alloc = (HALLOC_FUN) private_atom_alloc;
    f.free = (HFREE_FUN) private_atom_free;
    f.meta_alloc = (HMALLOC_FUN) erts_alloc;
    f.meta_free = (HMFREE_FUN) erts_free;
    f.meta_print = (HMPRINT_FUN) erts_print;
    erts_index_init(ERTS_ALC_T_ATOM_TABLE, &owner->index, "isolate_atom_tab",
                    ATOM_SIZE, limit, f);
    /* Preserve predefined indices without importing any diagnostic-world atoms. */
    for (i = 0; i < predefined; ++i) {
        Atom a;
        int index;
        a.len = sys_strlen(erl_atom_names[i]);
        a.latin1_chars = a.len;
        a.u.name = (byte *) erl_atom_names[i];
        index = index_put(&owner->index, &a);
        ASSERT(index == i);
        (void) index;
        owner->text_bytes += a.len;
    }
    return owner;
}

int erts_atom_namespace_put(ErtsAtomNamespace *owner, const unsigned char *name, size_t len)
{
    if (len > MAX_ATOM_SZ_LIMIT)
        return ATOM_MAX_CHARS_ERROR;
    if (!owner || (!name && len))
        return ATOM_BAD_ENCODING_ERROR;
    return atom_put_index(owner, name ? name : (const byte *) "", len,
                          ERTS_ATOM_ENC_UTF8, 0);
}

int erts_atom_namespace_name(ErtsAtomNamespace *owner, int index,
                           unsigned char *out, size_t capacity)
{
    int result = -1;
    erts_rwmtx_rlock(&owner->lock);
    if (index >= 0 && index < owner->index.entries) {
        Atom *atom = (Atom *) erts_index_lookup(&owner->index, index);
        if (capacity >= atom->len && (out || !atom->len)) {
            if (atom->len)
                sys_memcpy(out, erts_atom_get_name(atom), atom->len);
            result = atom->len;
        }
    }
    erts_rwmtx_runlock(&owner->lock);
    return result;
}

int erts_atom_namespace_count(ErtsAtomNamespace *owner)
{
    int count;
    erts_rwmtx_rlock(&owner->lock);
    count = owner->index.entries;
    erts_rwmtx_runlock(&owner->lock);
    return count;
}

size_t erts_atom_namespace_text_bytes(ErtsAtomNamespace *owner)
{
    size_t bytes;
    erts_rwmtx_rlock(&owner->lock);
    bytes = owner->text_bytes;
    erts_rwmtx_runlock(&owner->lock);
    return bytes;
}

void erts_atom_namespace_discard_unpublished(ErtsAtomNamespace *owner)
{
    erts_index_destroy(&owner->index);
    erts_rwmtx_destroy(&owner->lock);
    erts_free(ERTS_ALC_T_ATOM_TABLE, owner);
}

void
dump_atoms(fmtfn_t to, void *to_arg)
{
    int i = diagnostic_atoms->index.entries;

    /*
     * Print out the atom table starting from the end.
     */
    while (--i >= 0) {
	if (erts_index_lookup(&diagnostic_atoms->index, i)) {
	    erts_print(to, to_arg, "%T\n", make_atom(i));
	}
    }
}

Uint
erts_get_atom_limit(void)
{
    return diagnostic_atoms->limit;
}
