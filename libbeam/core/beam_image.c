/*
 * %CopyrightBegin%
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Copyright Ericsson AB 2020-2026. All Rights Reserved.
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
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
/* Adapted from OTP beam_file.c's reader/IFF/code/atom/import/export parsers.
 * Ownership, bounded unsigned arithmetic, UTF-8 validation and rollback are
 * libbeam adaptations. See docs/rfds/0003-first-loader-slice.md for provenance
 * and deliberately stricter structural validation. This is NOT an executor.
 */
#include "beam_image.h"
#include <string.h>
#include <stdlib.h>

typedef struct { const unsigned char *head; size_t left; } BeamReader;
enum { ATOMS, CODE, STRINGS, IMPORTS, EXPORTS, REQUIRED_CHUNKS };
static const uint32_t chunk_ids[REQUIRED_CHUNKS] = {
    LB_BEAM_ID('A','t','U','8'), LB_BEAM_ID('C','o','d','e'),
    LB_BEAM_ID('S','t','r','T'), LB_BEAM_ID('I','m','p','T'),
    LB_BEAM_ID('E','x','p','T')
};
struct LbBeamImage {
    LbAllocDomain *owner;
    unsigned char *bytes;
    size_t size;
    LbBeamImageInfo info;
    LbBeamBytes *atoms;
    LbBeamImport *imports;
    LbBeamExport *exports;
};
static int read_bytes(BeamReader *r, size_t size, LbBeamBytes *out)
{
    if (size > r->left) return 0;
    out->data = r->head;
    out->size = size;
    r->head += size;
    r->left -= size;
    return 1;
}
static int read_u32(BeamReader *r, uint32_t *out)
{
    LbBeamBytes b;
    if (!read_bytes(r, 4, &b)) return 0;
    *out = (uint32_t)b.data[0] << 24 | (uint32_t)b.data[1] << 16 |
           (uint32_t)b.data[2] << 8 | (uint32_t)b.data[3];
    return 1;
}
static int read_u8(BeamReader *r, unsigned char *out)
{
    LbBeamBytes b;
    if (!read_bytes(r, 1, &b)) return 0;
    *out = b.data[0];
    return 1;
}
/* The unsigned compact-number branch of beamreader_read_tagged, for lengths
 * only. More than eight bytes cannot be a supported atom length. Never recurse
 * through adversarial size prefixes. Full instruction operands are NOT decoded.
 */
static int read_length(BeamReader *r, uint32_t *out)
{
    unsigned char first, extra;
    uint64_t value = 0;
    unsigned count, i;
    if (!read_u8(r, &first) || (first & 7) != 0) return 0; /* TAG_u */
    if (!(first & 8)) { *out = first >> 4; return 1; }
    if (!(first & 16)) {
        if (!read_u8(r, &extra)) return 0;
        *out = ((uint32_t)(first >> 5) << 8) | extra;
        return 1;
    }
    count = (first >> 5) + 2;
    if (count > 8 || count > r->left) return 0;
    for (i = 0; i < count; ++i) {
        if (!read_u8(r, &extra)) return 0;
        value = value << 8 | extra;
    }
    if (value > UINT32_MAX) return 0;
    *out = (uint32_t)value;
    return 1;
}
/* New bounded UTF-8 validation, replacing the validation side effect of
 * erts_atom_put. No normalization, interning or runtime atom identity is implied.
 */
static int valid_atom(LbBeamBytes name)
{
    size_t pos = 0, characters = 0;
    while (pos < name.size) {
        uint32_t code, minimum;
        unsigned n, i, first = name.data[pos++];
        if (++characters > 255) return 0;
        if (first < 0x80) continue;
        if (first >= 0xc2 && first <= 0xdf) { n = 1; code = first & 31; minimum = 0x80; }
        else if (first >= 0xe0 && first <= 0xef) { n = 2; code = first & 15; minimum = 0x800; }
        else if (first >= 0xf0 && first <= 0xf4) { n = 3; code = first & 7; minimum = 0x10000; }
        else return 0;
        if (n > name.size - pos) return 0;
        for (i = 0; i < n; ++i) {
            unsigned b = name.data[pos++];
            if ((b & 0xc0) != 0x80) return 0;
            code = code << 6 | (b & 63);
        }
        if (code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) return 0;
    }
    return 1;
}
static LbBeamStatus iff_init(const void *data, size_t size, BeamReader *body)
{
    BeamReader r = {data, size};
    uint32_t id, declared, form;
    if (!read_u32(&r, &id) || id != LB_BEAM_ID('F','O','R','1') ||
        !read_u32(&r, &declared) || declared != r.left ||
        !read_u32(&r, &form) || form != LB_BEAM_ID('B','E','A','M'))
        return LB_BEAM_BAD_FORMAT;
    *body = r;
    return LB_BEAM_OK;
}
static int read_chunk(BeamReader *r, uint32_t *id, LbBeamBytes *chunk)
{
    uint32_t size;
    size_t padding;
    LbBeamBytes ignored;
    if (!read_u32(r, id) || !read_u32(r, &size)) return 0;
    padding = (4 - (size & 3)) & 3;
    return read_bytes(r, size, chunk) && read_bytes(r, padding, &ignored);
}
static LbBeamStatus read_beam_chunks(const void *data, size_t size, LbBeamBytes *chunks)
{
    BeamReader r;
    unsigned seen = 0, i;
    int legacy = 0;
    LbBeamStatus status = iff_init(data, size, &r);
    if (status != LB_BEAM_OK) return status;
    memset(chunks, 0, sizeof(*chunks) * REQUIRED_CHUNKS);
    while (r.left) {
        uint32_t id;
        LbBeamBytes chunk;
        if (!read_chunk(&r, &id, &chunk)) return LB_BEAM_BAD_FORMAT;
        if (id == LB_BEAM_ID('A','t','o','m')) legacy = 1;
        for (i = 0; i < REQUIRED_CHUNKS; ++i) {
            if (id == chunk_ids[i]) {
                if (seen & (1u << i)) return LB_BEAM_BAD_FORMAT;
                seen |= 1u << i;
                chunks[i] = chunk;
                break;
            }
        }
    }
    if (!(seen & (1u << ATOMS)) && legacy) return LB_BEAM_UNSUPPORTED;
    return seen == (1u << REQUIRED_CHUNKS) - 1 ? LB_BEAM_OK : LB_BEAM_BAD_FORMAT;
}
static LbBeamStatus parse_code_chunk(LbBeamImage *image, LbBeamBytes chunk)
{
    BeamReader r = {chunk.data, chunk.size}, header;
    LbBeamBytes head;
    uint32_t size, version;
    if (!read_u32(&r, &size) || !read_bytes(&r, size, &head)) return LB_BEAM_BAD_FORMAT;
    header = (BeamReader){head.data, head.size};
    if (!read_u32(&header, &version)) return LB_BEAM_BAD_FORMAT;
    if (version != 0) return LB_BEAM_UNSUPPORTED; /* BEAM_FORMAT_NUMBER */
    if (!read_u32(&header, &image->info.max_opcode) ||
        !read_u32(&header, &image->info.label_count) ||
        !read_u32(&header, &image->info.function_count) ||
        !r.left || !image->info.function_count ||
        image->info.function_count >= image->info.label_count)
        return LB_BEAM_BAD_FORMAT;
    image->info.code = (LbBeamBytes){r.head, r.left};
    return LB_BEAM_OK;
}
static LbBeamStatus allocate_array(LbBeamImage *image, size_t count, size_t width, void **out)
{
    *out = NULL;
    if (!count) return LB_BEAM_OK;
    if (count > SIZE_MAX / width) return LB_BEAM_LIMIT;
    return lb_alloc_domain_allocate(image->owner, count * width, out) == LB_ALLOC_OK
        ? LB_BEAM_OK : LB_BEAM_NO_MEMORY;
}
static LbBeamStatus parse_atom_chunk(LbBeamImage *image, LbBeamBytes chunk)
{
    BeamReader r = {chunk.data, chunk.size};
    uint32_t raw, count, i;
    int long_counts;
    void *allocation;
    LbBeamStatus status;
    if (!read_u32(&r, &raw)) return LB_BEAM_BAD_FORMAT;
    long_counts = (raw & UINT32_C(0x80000000)) != 0;
    count = long_counts ? 0u - raw : raw;
    /* Each entry needs at least one length byte. Index zero is NIL, not an atom. */
    if (!count || count > r.left) return LB_BEAM_BAD_FORMAT;
    status = allocate_array(image, count, sizeof(*image->atoms), &allocation);
    if (status != LB_BEAM_OK) return status;
    image->atoms = allocation;
    image->info.atom_count = count;
    for (i = 0; i < count; ++i) {
        uint32_t length;
        unsigned char byte_length;
        if (long_counts) {
            if (!read_length(&r, &length)) return LB_BEAM_BAD_FORMAT;
        } else {
            if (!read_u8(&r, &byte_length)) return LB_BEAM_BAD_FORMAT;
            length = byte_length;
        }
        if (length > 4 * 255 || !read_bytes(&r, length, &image->atoms[i]) ||
            !valid_atom(image->atoms[i])) return LB_BEAM_BAD_FORMAT;
    }
    return r.left ? LB_BEAM_BAD_FORMAT : LB_BEAM_OK;
}
static int atom_index(const LbBeamImage *image, uint32_t index)
{
    return index && index <= image->info.atom_count;
}
static LbBeamStatus parse_table(LbBeamImage *image, LbBeamBytes chunk, int exports)
{
    BeamReader r = {chunk.data, chunk.size};
    uint32_t count, i;
    void *allocation;
    LbBeamStatus status;
    if (!read_u32(&r, &count) || count > r.left / 12 || (size_t)count * 12 != r.left)
        return LB_BEAM_BAD_FORMAT;
    status = allocate_array(image, count,
                            exports ? sizeof(*image->exports) : sizeof(*image->imports), &allocation);
    if (status != LB_BEAM_OK) return status;
    if (exports) { image->exports = allocation; image->info.export_count = count; }
    else { image->imports = allocation; image->info.import_count = count; }
    for (i = 0; i < count; ++i) {
        uint32_t a, b, c;
        if (!read_u32(&r, &a) || !read_u32(&r, &b) || !read_u32(&r, &c)) return LB_BEAM_BAD_FORMAT;
        if (exports) {
            if (!atom_index(image, a) || b > 255 || !c || c >= image->info.label_count)
                return LB_BEAM_BAD_FORMAT;
            image->exports[i] = (LbBeamExport){a, b, c};
        } else {
            if (!atom_index(image, a) || !atom_index(image, b) || c > 255)
                return LB_BEAM_BAD_FORMAT;
            image->imports[i] = (LbBeamImport){a, b, c};
        }
    }
    return LB_BEAM_OK;
}
static void release_owned(LbAllocDomain *owner, void *p)
{
    /* Invariant failure is a core bug, not an input error. Never compile the
     * actual release out with NDEBUG. No fallible operations occur in rollback. */
    if (p && lb_alloc_domain_release(owner, p) != LB_ALLOC_OK) abort();
}
void lb_beam_image_destroy(LbBeamImage *image)
{
    if (!image) return;
    release_owned(image->owner, image->exports);
    release_owned(image->owner, image->imports);
    release_owned(image->owner, image->atoms);
    release_owned(image->owner, image->bytes);
    release_owned(image->owner, image);
}
LbBeamStatus lb_beam_image_create(LbAllocDomain *owner, const void *data, size_t size, LbBeamImage **out)
{
    LbBeamImage *image;
    LbBeamBytes chunks[REQUIRED_CHUNKS];
    LbBeamStatus status;
    void *allocation;
    if (!out) return LB_BEAM_INVALID_ARGUMENT;
    *out = NULL;
    if (!owner || !data) return LB_BEAM_INVALID_ARGUMENT;
    if (size > LB_BEAM_MAX_BYTES) return LB_BEAM_LIMIT;
    status = read_beam_chunks(data, size, chunks);
    if (status != LB_BEAM_OK) return status;
    if (lb_alloc_domain_allocate(owner, sizeof(*image), &allocation) != LB_ALLOC_OK)
        return LB_BEAM_NO_MEMORY;
    image = allocation;
    memset(image, 0, sizeof(*image));
    image->owner = owner;
    image->size = size;
    if (lb_alloc_domain_allocate(owner, size, &allocation) != LB_ALLOC_OK) {
        status = LB_BEAM_NO_MEMORY;
        goto fail;
    }
    image->bytes = allocation;
    memcpy(image->bytes, data, size);
    status = read_beam_chunks(image->bytes, size, chunks);
    if (status != LB_BEAM_OK) goto fail;
    status = parse_code_chunk(image, chunks[CODE]);
    if (status != LB_BEAM_OK) goto fail;
    status = parse_atom_chunk(image, chunks[ATOMS]);
    if (status != LB_BEAM_OK) goto fail;
    status = parse_table(image, chunks[IMPORTS], 0);
    if (status != LB_BEAM_OK) goto fail;
    status = parse_table(image, chunks[EXPORTS], 1);
    if (status != LB_BEAM_OK) goto fail;
    *out = image;
    return LB_BEAM_OK;
fail:
    lb_beam_image_destroy(image);
    return status;
}
const LbBeamImageInfo *lb_beam_image_info(const LbBeamImage *image)
{
    return image ? &image->info : NULL;
}
LbBeamStatus lb_beam_image_atom(const LbBeamImage *image, uint32_t index, LbBeamBytes *out)
{
    if (!out) return LB_BEAM_INVALID_ARGUMENT;
    *out = (LbBeamBytes){0};
    if (!image) return LB_BEAM_INVALID_ARGUMENT;
    if (!atom_index(image, index)) return LB_BEAM_NOT_FOUND;
    *out = image->atoms[index - 1];
    return LB_BEAM_OK;
}
LbBeamStatus lb_beam_image_export(const LbBeamImage *image, uint32_t index, LbBeamExport *out)
{
    if (!out) return LB_BEAM_INVALID_ARGUMENT;
    *out = (LbBeamExport){0};
    if (!image) return LB_BEAM_INVALID_ARGUMENT;
    if (index >= image->info.export_count) return LB_BEAM_NOT_FOUND;
    *out = image->exports[index];
    return LB_BEAM_OK;
}
LbBeamStatus lb_beam_image_import(const LbBeamImage *image, uint32_t index, LbBeamImport *out)
{
    if (!out) return LB_BEAM_INVALID_ARGUMENT;
    *out = (LbBeamImport){0};
    if (!image) return LB_BEAM_INVALID_ARGUMENT;
    if (index >= image->info.import_count) return LB_BEAM_NOT_FOUND;
    *out = image->imports[index];
    return LB_BEAM_OK;
}
LbBeamStatus lb_beam_image_chunk(const LbBeamImage *image, uint32_t wanted, LbBeamBytes *out)
{
    BeamReader r;
    LbBeamBytes found = {0};
    if (!out) return LB_BEAM_INVALID_ARGUMENT;
    *out = (LbBeamBytes){0};
    if (!image) return LB_BEAM_INVALID_ARGUMENT;
    if (iff_init(image->bytes, image->size, &r) != LB_BEAM_OK) return LB_BEAM_BAD_FORMAT;
    while (r.left) {
        LbBeamBytes chunk;
        uint32_t id;
        if (!read_chunk(&r, &id, &chunk)) return LB_BEAM_BAD_FORMAT;
        if (id == wanted) found = chunk; /* upstream last-match semantics */
    }
    *out = found;
    return found.data ? LB_BEAM_OK : LB_BEAM_NOT_FOUND;
}
