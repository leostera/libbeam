/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#include "beam_image.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%d: %s\n", __LINE__, #c); abort(); } } while (0)
typedef struct { size_t calls, fail_at, live; void *p[64]; } Host;
static void *allocate(void *ctx, size_t size)
{
    Host *h = ctx;
    void *p;
    if (++h->calls == h->fail_at) return NULL;
    CHECK(h->live < 64);
    p = malloc(size);
    CHECK(p);
    h->p[h->live++] = p;
    return p;
}
static void release(void *ctx, void *p)
{
    Host *h = ctx;
    size_t i;
    for (i = 0; i < h->live; ++i) if (h->p[i] == p) {
        h->p[i] = h->p[--h->live];
        free(p);
        return;
    }
    CHECK(0);
}
static LbAllocDomain *domain(Host *h)
{
    LbSystemAllocator a = {h, allocate, release};
    LbAllocDomain *d;
    CHECK(lb_alloc_domain_create(&a, &d) == LB_ALLOC_OK);
    return d;
}
typedef struct { unsigned char bytes[8192]; size_t size, atoms, code, imports, exports; } File;
static void u32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)(v >> 24); p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8); p[3] = (unsigned char)v;
}
static size_t chunk(File *f, uint32_t id, const void *data, size_t size)
{
    size_t start = f->size, aligned = (size + 3) & ~(size_t)3;
    CHECK(start + 8 + aligned <= sizeof(f->bytes));
    u32(f->bytes + start, id); u32(f->bytes + start + 4, (uint32_t)size);
    if (size) memcpy(f->bytes + start + 8, data, size);
    memset(f->bytes + start + 8 + size, 0, aligned - size);
    f->size += 8 + aligned;
    u32(f->bytes + 4, (uint32_t)f->size - 8);
    return start + 8;
}
/* Structural test image only, deliberately NOT executable BEAM evidence. The
 * separate erlc fixture is the real-file witness. No instruction interpretation. */
static File make_file(const unsigned char *name, size_t length, int compact)
{
    File f = {{0}, 12, 0, 0, 0, 0};
    unsigned char atoms[2048], code[21] = {0}, table[16] = {0};
    const unsigned char opaque[] = {1, 0, 0xff, 4, 9};
    size_t p = 4;
    u32(f.bytes, LB_BEAM_ID('F','O','R','1')); u32(f.bytes + 8, LB_BEAM_ID('B','E','A','M'));
    CHECK(length <= 1024 && (compact || length <= 255));
    u32(atoms, compact ? UINT32_MAX - 1 : 2);
    if (!compact) atoms[p++] = (unsigned char)length;
    else if (length < 16) atoms[p++] = (unsigned char)(length << 4);
    else { atoms[p++] = (unsigned char)(((length >> 8) << 5) | 8); atoms[p++] = (unsigned char)length; }
    memcpy(atoms + p, name, length); p += length;
    atoms[p++] = compact ? 16 : 1; atoms[p++] = 'f';
    f.atoms = chunk(&f, LB_BEAM_ID('A','t','U','8'), atoms, p);
    u32(code, 16); u32(code + 8, 19); u32(code + 12, 3); u32(code + 16, 1); code[20] = 3;
    f.code = chunk(&f, LB_BEAM_ID('C','o','d','e'), code, sizeof(code));
    chunk(&f, LB_BEAM_ID('S','t','r','T'), NULL, 0);
    u32(table, 1); u32(table + 4, 1); u32(table + 8, 2); u32(table + 12, 0);
    f.imports = chunk(&f, LB_BEAM_ID('I','m','p','T'), table, sizeof(table));
    u32(table + 4, 2); u32(table + 8, 0); u32(table + 12, 2);
    f.exports = chunk(&f, LB_BEAM_ID('E','x','p','T'), table, sizeof(table));
    chunk(&f, LB_BEAM_ID('X','t','r','a'), opaque, sizeof(opaque));
    return f;
}
static void expect(LbAllocDomain *d, Host *h, File *f, LbBeamStatus expected)
{
    size_t before = h->live;
    LbBeamImage *image = NULL;
    LbBeamStatus status = lb_beam_image_create(d, f->bytes, f->size, &image);
    CHECK(status == expected);
    CHECK((status == LB_BEAM_OK) == (image != NULL));
    lb_beam_image_destroy(image);
    CHECK(h->live == before);
}
static void malformed(void)
{
    Host h = {0};
    LbAllocDomain *d = domain(&h);
    File base = make_file((const unsigned char *)"m", 1, 0), f;
    size_t i;
    LbBeamImage *image;
    LbBeamBytes view;
    expect(d, &h, &base, LB_BEAM_OK);
    {
        const unsigned char payload = 42;
        f = base; chunk(&f, LB_BEAM_ID('X','t','r','a'), &payload, 1);
        CHECK(lb_beam_image_create(d, f.bytes, f.size, &image) == LB_BEAM_OK);
        CHECK(lb_beam_image_chunk(image, LB_BEAM_ID('X','t','r','a'), &view) == LB_BEAM_OK);
        CHECK(view.size == 1 && view.data[0] == 42);
        lb_beam_image_destroy(image);
        CHECK(h.live == 1);
    }
    CHECK(lb_beam_image_create(d, NULL, 0, &image) == LB_BEAM_INVALID_ARGUMENT && !image);
    CHECK(lb_beam_image_create(d, base.bytes, LB_BEAM_MAX_BYTES + 1, &image) == LB_BEAM_LIMIT && !image);
    CHECK(lb_beam_image_atom(NULL, 0, &view) == LB_BEAM_INVALID_ARGUMENT && !view.data);
    CHECK(!lb_beam_image_info(NULL));
    for (i = 0; i < base.size; ++i) {
        CHECK(lb_beam_image_create(d, base.bytes, i, &image) == LB_BEAM_BAD_FORMAT && !image);
        CHECK(h.live == 1);
    }
#define BAD(offset, value) do { f = base; u32(f.bytes + (offset), (value)); expect(d, &h, &f, LB_BEAM_BAD_FORMAT); } while (0)
    BAD(0, 0); BAD(4, UINT32_MAX); BAD(8, 0); BAD(base.atoms - 4, UINT32_MAX);
    BAD(base.atoms, UINT32_C(0x80000000)); BAD(base.atoms, 1000); BAD(base.atoms, 0);
    BAD(base.code, 15); BAD(base.code, UINT32_MAX); BAD(base.code + 12, 1);
    BAD(base.code + 16, 0); BAD(base.imports, UINT32_MAX); BAD(base.imports, 0);
    BAD(base.imports + 4, 0); BAD(base.imports + 8, 3); BAD(base.imports + 12, 256);
    BAD(base.exports, 0); BAD(base.exports + 4, 0); BAD(base.exports + 8, 256);
    BAD(base.exports + 12, 0); BAD(base.exports + 12, 3);
#undef BAD
    f = base; u32(f.bytes + f.code + 4, 1); expect(d, &h, &f, LB_BEAM_UNSUPPORTED);
    f = base; u32(f.bytes + f.atoms - 8, LB_BEAM_ID('A','t','o','m')); expect(d, &h, &f, LB_BEAM_UNSUPPORTED);
    f = base; chunk(&f, LB_BEAM_ID('C','o','d','e'), base.bytes + base.code, 21); expect(d, &h, &f, LB_BEAM_BAD_FORMAT);
    f = base; f.bytes[f.size++] = 0; expect(d, &h, &f, LB_BEAM_BAD_FORMAT);
    /* Empty import table: no zero-byte allocation, still valid metadata. */
    f = base;
    memmove(f.bytes + f.imports + 4, f.bytes + f.imports + 16, f.size - f.imports - 16);
    f.size -= 12; u32(f.bytes + 4, (uint32_t)f.size - 8);
    u32(f.bytes + f.imports - 4, 4); u32(f.bytes + f.imports, 0);
    expect(d, &h, &f, LB_BEAM_OK);
    /* Truncate inside the form too, not just mismatch its outer length. A cut
     * immediately before the optional final chunk is the one valid prefix. */
    for (i = 12; i < base.size; ++i) {
        f = base; f.size = i; u32(f.bytes + 4, (uint32_t)i - 8);
        expect(d, &h, &f, i == base.size - 16 ? LB_BEAM_OK : LB_BEAM_BAD_FORMAT);
    }
    /* Missing opaque padding with an otherwise consistent form length. */
    f = base; --f.size; u32(f.bytes + 4, (uint32_t)f.size - 8); expect(d, &h, &f, LB_BEAM_BAD_FORMAT);
    /* Mutation stress: success means structural metadata only, never execution. */
    for (i = 0; i < base.size * 8; ++i) {
        LbBeamStatus status;
        f = base; f.bytes[i / 8] ^= (unsigned char)(1u << (i % 8));
        status = lb_beam_image_create(d, f.bytes, f.size, &image);
        CHECK((status == LB_BEAM_OK) == (image != NULL));
        lb_beam_image_destroy(image);
        CHECK(h.live == 1);
    }
    CHECK(lb_alloc_domain_destroy(d) == LB_ALLOC_OK && h.live == 0);
}
static void atom_encodings(void)
{
    Host h = {0};
    LbAllocDomain *d = domain(&h);
    unsigned char name[1024];
    File f;
    size_t i;
    const unsigned char bad[][4] = {{0xc0,0x80,0,0}, {0xed,0xa0,0x80,0}, {0xf4,0x90,0x80,0x80}, {0x80,0,0,0}};
    const size_t lengths[] = {2,3,4,1};
    for (i = 0; i < 255; ++i) { name[i * 4] = 0xf4; name[i * 4 + 1] = 0x8f; name[i * 4 + 2] = 0xbf; name[i * 4 + 3] = 0xbf; }
    f = make_file(name, 1020, 1); expect(d, &h, &f, LB_BEAM_OK);
    f = make_file(name, 1019, 1); expect(d, &h, &f, LB_BEAM_BAD_FORMAT);
    memset(name, 'a', 256);
    f = make_file(name, 255, 0); expect(d, &h, &f, LB_BEAM_OK);
    f = make_file(name, 256, 1); expect(d, &h, &f, LB_BEAM_BAD_FORMAT);
    f = make_file(name, 0, 1); expect(d, &h, &f, LB_BEAM_OK);
    for (i = 0; i < 4; ++i) { f = make_file(bad[i], lengths[i], 1); expect(d, &h, &f, LB_BEAM_BAD_FORMAT); }
    f = make_file((const unsigned char *)"m", 1, 1);
    f.bytes[f.atoms + 4] |= 1; expect(d, &h, &f, LB_BEAM_BAD_FORMAT); /* wrong compact tag */
    f = make_file((const unsigned char *)"m", 1, 1);
    f.bytes[f.atoms + 4] = 0xf8; expect(d, &h, &f, LB_BEAM_BAD_FORMAT); /* huge nested prefix */
    /* Exercise the multi-byte compact branch with a non-minimal, valid length
     * representation: 0x18 followed by two bytes encodes unsigned 1. */
    f = make_file((const unsigned char *)"m", 1, 1);
    {
        size_t offset = f.atoms + 4;
        memmove(f.bytes + offset + 3, f.bytes + offset + 1, f.size - offset - 1);
        f.bytes[offset] = 0x18; f.bytes[offset + 1] = 0; f.bytes[offset + 2] = 1;
        /* Atom payload was eight bytes and is now ten, requiring two new pad bytes. */
        memmove(f.bytes + f.code - 8 + 4, f.bytes + f.code - 8 + 2, f.size - (f.code - 8));
        f.bytes[f.code - 8 + 2] = f.bytes[f.code - 8 + 3] = 0;
        f.size += 4; u32(f.bytes + 4, (uint32_t)f.size - 8);
        u32(f.bytes + f.atoms - 4, 10);
        expect(d, &h, &f, LB_BEAM_OK);
    }
    CHECK(lb_alloc_domain_destroy(d) == LB_ALLOC_OK && h.live == 0);
}
static void lifetime(const unsigned char *data, size_t size)
{
    Host a = {0}, b = {0};
    LbAllocDomain *da = domain(&a), *db = domain(&b);
    LbBeamImage *image, *peer;
    LbBeamBytes code, module;
    unsigned char *copy = malloc(size), *expected;
    size_t calls = a.calls, steps, i;
    CHECK(copy);
    memcpy(copy, data, size);
    CHECK(lb_beam_image_create(da, copy, size, &image) == LB_BEAM_OK);
    steps = a.calls - calls;
    CHECK(steps >= 4);
    CHECK(lb_beam_image_create(db, data, size, &peer) == LB_BEAM_OK);
    CHECK(lb_alloc_domain_destroy(da) == LB_ALLOC_BUSY);
    CHECK(lb_beam_image_chunk(image, LB_BEAM_ID('C','o','d','e'), &code) == LB_BEAM_OK);
    /* Every chunk, including opaque literals/debug/type/vendor chunks, borrows
     * the owned copy, not caller memory. */
    {
        size_t pos = 12;
        while (pos < size) {
            uint32_t id = (uint32_t)data[pos] << 24 | (uint32_t)data[pos+1] << 16 |
                          (uint32_t)data[pos+2] << 8 | data[pos+3];
            uint32_t n = (uint32_t)data[pos+4] << 24 | (uint32_t)data[pos+5] << 16 |
                         (uint32_t)data[pos+6] << 8 | data[pos+7];
            LbBeamBytes view;
            CHECK(lb_beam_image_chunk(image, id, &view) == LB_BEAM_OK);
            CHECK(view.size == n && view.data != copy + pos + 8);
            CHECK(memcmp(view.data, data + pos + 8, n) == 0);
            pos += 8 + (((size_t)n + 3) & ~(size_t)3);
        }
        CHECK(pos == size);
    }
    expected = malloc(code.size); CHECK(expected); memcpy(expected, code.data, code.size);
    memset(copy, 0xa5, size); free(copy);
    CHECK(memcmp(expected, code.data, code.size) == 0);
    CHECK(lb_alloc_domain_release(db, (void *)code.data) == LB_ALLOC_INVALID);
    free(expected);
    lb_beam_image_destroy(image);
    CHECK(a.live == 1);
    for (i = 1; i <= steps; ++i) {
        size_t peer_live = b.live;
        a.fail_at = a.calls + i;
        CHECK(lb_beam_image_create(da, data, size, &image) == LB_BEAM_NO_MEMORY && !image);
        CHECK(a.live == 1 && b.live == peer_live);
        CHECK(lb_beam_image_atom(peer, 1, &module) == LB_BEAM_OK);
        a.fail_at = 0;
        CHECK(lb_beam_image_create(da, data, size, &image) == LB_BEAM_OK);
        lb_beam_image_destroy(image);
        CHECK(a.live == 1);
    }
    for (i = 0; i < 64; ++i) {
        CHECK(lb_beam_image_create(da, data, size, &image) == LB_BEAM_OK);
        lb_beam_image_destroy(image);
    }
    CHECK(lb_alloc_domain_destroy(da) == LB_ALLOC_OK && !a.live);
    CHECK(lb_beam_image_atom(peer, 1, &module) == LB_BEAM_OK);
    CHECK(lb_beam_image_chunk(peer, 0, &module) == LB_BEAM_NOT_FOUND && !module.data);
    CHECK(lb_beam_image_atom(peer, 0, &module) == LB_BEAM_NOT_FOUND);
    lb_beam_image_destroy(peer);
    CHECK(lb_alloc_domain_destroy(db) == LB_ALLOC_OK && !b.live);
}
static void inspect_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    long length;
    unsigned char *bytes;
    Host h = {0};
    LbAllocDomain *d = domain(&h);
    LbBeamImage *image;
    const LbBeamImageInfo *info;
    uint32_t i;
    CHECK(f && fseek(f, 0, SEEK_END) == 0);
    length = ftell(f); CHECK(length > 0 && (size_t)length <= LB_BEAM_MAX_BYTES);
    CHECK(fseek(f, 0, SEEK_SET) == 0);
    bytes = malloc((size_t)length); CHECK(bytes);
    CHECK(fread(bytes, 1, (size_t)length, f) == (size_t)length && fclose(f) == 0);
    lifetime(bytes, (size_t)length);
    {
        size_t n;
        for (n = 0; n < (size_t)length; ++n) {
            CHECK(lb_beam_image_create(d, bytes, n, &image) == LB_BEAM_BAD_FORMAT && !image);
            CHECK(h.live == 1);
        }
    }
    CHECK(lb_beam_image_create(d, bytes, (size_t)length, &image) == LB_BEAM_OK);
    memset(bytes, 0, (size_t)length); free(bytes);
    info = lb_beam_image_info(image);
    printf("CODE %u %u %u %zu\n", info->max_opcode, info->label_count, info->function_count, info->code.size);
    for (i = 1; i <= info->atom_count; ++i) {
        LbBeamBytes atom; size_t j;
        CHECK(lb_beam_image_atom(image, i, &atom) == LB_BEAM_OK);
        printf("ATOM %u ", i);
        for (j = 0; j < atom.size; ++j) printf("%02X", atom.data[j]);
        puts("");
    }
    for (i = 0; i < info->import_count; ++i) {
        LbBeamImport entry;
        CHECK(lb_beam_image_import(image, i, &entry) == LB_BEAM_OK);
        printf("IMPORT %u %u %u\n", entry.module, entry.function, entry.arity);
    }
    for (i = 0; i < info->export_count; ++i) {
        LbBeamExport entry;
        CHECK(lb_beam_image_export(image, i, &entry) == LB_BEAM_OK);
        printf("EXPORT %u %u %u\n", entry.atom, entry.arity, entry.label);
    }
    lb_beam_image_destroy(image);
    CHECK(lb_alloc_domain_destroy(d) == LB_ALLOC_OK && !h.live);
}
int main(int argc, char **argv)
{
    File f = make_file((const unsigned char *)"m", 1, 0);
    malformed(); atom_encodings(); lifetime(f.bytes, f.size);
    if (argc == 2) inspect_file(argv[1]); else CHECK(argc == 1);
    puts("CORE_BEAM_IMAGE_OK owned_bytes=true rollback=true peer_survival=true execution=false");
    return 0;
}
