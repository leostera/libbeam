/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#include "atoms.h"
#include "lb_atoms_generated.h"
#include "lb_atoms_generated.inc" /* independent expected sequence, not runtime storage */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%d: %s\n", __LINE__, #x); abort(); } } while (0)
typedef struct { void *p; size_t bytes; } Allocation;
typedef struct { size_t calls, fail_at, live, bytes; Allocation allocations[16384]; } Host;
static void *allocate(void *ctx, size_t n)
{
    Host *h = ctx;
    void *p;
    if (++h->calls == h->fail_at) return NULL;
    CHECK(h->live < 16384);
    p = malloc(n); CHECK(p);
    h->allocations[h->live++] = (Allocation){p, n}; h->bytes += n;
    return p;
}
static void release(void *ctx, void *p)
{
    Host *h = ctx;
    size_t i;
    /* Reverse search makes expected reverse-order rollback cheap while still
     * checking every exact system-allocation base and detecting repeated frees. */
    for (i = h->live; i; --i) if (h->allocations[i-1].p == p) {
        h->bytes -= h->allocations[i-1].bytes;
        h->allocations[i-1] = h->allocations[--h->live]; free(p); return;
    }
    CHECK(0);
}
static LbAllocDomain *new_domain(Host *h)
{
    LbSystemAllocator allocator = {h, allocate, release};
    LbAllocDomain *d;
    CHECK(lb_alloc_domain_create(&allocator, &d) == LB_ALLOC_OK);
    return d;
}
static void terms(void)
{
    Eterm storage[8] = {0}, t, list, out = NIL;
    Eterm values[] = {make_small(-7), make_atom(0), NIL};
    const Sint numbers[] = {MIN_SMALL, MIN_SMALL+1, -1000, -1, 0, 1, 255, MAX_SMALL-1, MAX_SMALL};
    size_t i, result = 17;
    CHECK(sizeof(Eterm) == 8 && NIL == 0x3b && make_atom(0) == 0xb && make_small(0) == 0xf);
    CHECK(make_small(-1) == UINTPTR_MAX && !is_atom(NIL) && !is_small(THE_NON_VALUE));
    for (i = 0; i < sizeof(numbers)/sizeof(numbers[0]); ++i) {
        CHECK(lb_term_small(numbers[i], &out)); CHECK(is_small(out) && signed_val(out) == numbers[i]);
    }
    out = NIL;
    CHECK(!lb_term_small(MIN_SMALL-1, &out) && out == NIL);
    CHECK(!lb_term_small(MAX_SMALL+1, &out) && out == NIL);
    CHECK(!lb_term_small(INTPTR_MIN, &out) && !lb_term_small(INTPTR_MAX, &out));
    CHECK(lb_term_tuple(storage, 8, values, 3, &t));
    CHECK(is_tuple(t) && tuple_val(t) == storage && storage[0] == 0xc0 && arityval(storage[0]) == 3);
    CHECK(storage[1] == values[0] && storage[2] == values[1] && storage[3] == NIL);
    CHECK(ptr_val(t | TAG_LITERAL_PTR) == storage && is_literal_ptr(t | TAG_LITERAL_PTR));
    list = CONS(storage + 4, t, NIL);
    CHECK(is_list(list) && CAR(list_val(list)) == t && is_nil(CDR(list_val(list))));
    CHECK(TUPLE2(storage, make_small(1), make_small(2)) == t && arityval(storage[0]) == 2);
    storage[0] = make_small(44); out = NIL;
    CHECK(!lb_term_tuple(storage, 1, NULL, 0, &out) && out == NIL && storage[0] == make_small(44));
    CHECK(!lb_term_tuple(storage, 8, values, SIZE_MAX, &out) && out == NIL);
    CHECK(lb_term_tuple(storage, 2, NULL, 0, &out) && is_tuple(out) && !arityval(storage[0]));
    CHECK(storage[1] == THE_NON_VALUE);
    CHECK(!lb_size_mul(SIZE_MAX, 2, &result) && result == 17);
    CHECK(!lb_size_add(SIZE_MAX, 1, &result) && result == 17);
    CHECK(lb_size_mul(3, sizeof(Eterm), &result) && result == 24);
}
static void name_is(LbAtomTable *table, Eterm atom, const void *expected, size_t size)
{
    LbBeamBytes view;
    CHECK(lb_atoms_name(table, atom, &view) == LB_ATOM_OK);
    CHECK(view.size == size && (!size || memcmp(view.data, expected, size) == 0));
}
static void constructor_failures(void)
{
    Host h = {0};
    LbAllocDomain *d = new_domain(&h);
    LbAtomTable *table;
    size_t start = h.calls, steps, i, bytes = h.bytes;
    CHECK(lb_atoms_create(d, lb_atoms_predefined_count(), &table) == LB_ATOM_OK);
    steps = h.calls - start;
    name_is(table, am_false, "false", 5); name_is(table, am_true, "true", 4);
    name_is(table, am_Empty, NULL, 0); name_is(table, am_erlang, "erlang", 6);
    CHECK(am_false == make_atom(0) && am_true == make_atom(1));
    CHECK(lb_atoms_predefined_count() == sizeof(lb_predefined_names)/sizeof(lb_predefined_names[0])-1);
    for (i=0; i<lb_atoms_predefined_count(); ++i) {
        Eterm found;
        size_t n=strlen(lb_predefined_names[i]);
        name_is(table,make_atom(i),lb_predefined_names[i],n);
        CHECK(lb_atoms_find(table,lb_predefined_names[i],n,&found)==LB_ATOM_OK && found==make_atom(i));
    }
    CHECK(lb_atoms_destroy(table) == LB_ATOM_OK && h.live == 1 && h.bytes == bytes);
    for (i = 1; i <= steps; ++i) {
        h.fail_at = h.calls + i;
        CHECK(lb_atoms_create(d, lb_atoms_predefined_count(), &table) == LB_ATOM_NO_MEMORY && !table);
        CHECK(h.live == 1 && h.bytes == bytes);
    }
    h.fail_at = 0;
    CHECK(lb_atoms_create(d, lb_atoms_predefined_count(), &table) == LB_ATOM_OK);
    {
        Eterm value;
        size_t live = h.live, old_bytes = h.bytes;
        CHECK(lb_atoms_intern(table, "new beyond limit", 16, &value) == LB_ATOM_LIMIT);
        CHECK(is_non_value(value) && lb_atoms_count(table) == lb_atoms_predefined_count());
        CHECK(lb_atoms_intern(table, "false", 5, &value) == LB_ATOM_OK && value == am_false);
        CHECK(h.live == live && h.bytes == old_bytes);
    }
    CHECK(lb_atoms_destroy(table) == LB_ATOM_OK);
    CHECK(lb_alloc_domain_destroy(d) == LB_ALLOC_OK && !h.live && !h.bytes);
}
static void independent_tables(void)
{
    Host h = {0};
    LbAllocDomain *d = new_domain(&h);
    LbAtomTable *a, *b;
    Eterm av, bv, found;
    LbBeamBytes saved, after;
    unsigned char unicode[] = {0xf0,0x9f,0xa4,0x96,0,'x'};
    unsigned char bad[] = {0xed,0xa0,0x80};
    size_t i;
    CHECK(lb_atoms_create(d, 8192, &a) == LB_ATOM_OK && lb_atoms_create(d, 8192, &b) == LB_ATOM_OK);
    CHECK(lb_atoms_intern(a, "private_A", 9, &av) == LB_ATOM_OK);
    CHECK(lb_atoms_intern(b, "private_B", 9, &bv) == LB_ATOM_OK && av == bv);
    name_is(a, av, "private_A", 9); name_is(b, bv, "private_B", 9);
    CHECK(lb_atoms_name(a, av, &saved) == LB_ATOM_OK);
    for (i = 0; i < 3000; ++i) {
        char name[64]; int n = snprintf(name, sizeof(name), "growth_atom_%zu", i);
        CHECK(lb_atoms_intern(a, name, (size_t)n, &found) == LB_ATOM_OK);
        CHECK(atom_val(found) == lb_atoms_predefined_count() + i + 1);
    }
    CHECK(lb_atoms_name(a, av, &after) == LB_ATOM_OK && after.data == saved.data);
    CHECK(lb_atoms_find(b, "growth_atom_0", 13, &found) == LB_ATOM_NOT_FOUND);
    CHECK(lb_atoms_intern(a, unicode, sizeof(unicode), &found) == LB_ATOM_OK);
    memset(unicode, 0, sizeof(unicode));
    { const unsigned char expected[] = {0xf0,0x9f,0xa4,0x96,0,'x'}; name_is(a, found, expected, sizeof(expected)); }
    CHECK(lb_atoms_intern(a, bad, sizeof(bad), &found) == LB_ATOM_BAD_UTF8 && is_non_value(found));
    CHECK(lb_atoms_retain(a) == LB_ATOM_OK && lb_atoms_destroy(a) == LB_ATOM_BUSY);
    CHECK(lb_atoms_release(a) == LB_ATOM_OK && lb_atoms_release(a) == LB_ATOM_INVALID);
    CHECK(lb_atoms_destroy(a) == LB_ATOM_OK);
    name_is(b, bv, "private_B", 9);
    CHECK(lb_atoms_destroy(b) == LB_ATOM_OK);
    CHECK(lb_alloc_domain_destroy(d) == LB_ALLOC_OK && !h.live && !h.bytes);
}
static void u32(unsigned char *p, uint32_t value)
{
    p[0]=(unsigned char)(value>>24); p[1]=(unsigned char)(value>>16);
    p[2]=(unsigned char)(value>>8); p[3]=(unsigned char)value;
}
static size_t chunk(unsigned char *p, uint32_t id, const unsigned char *data, size_t size)
{
    size_t padded = (size + 3) & ~(size_t)3;
    u32(p,id); u32(p+4,(uint32_t)size);
    if (size) memcpy(p+8,data,size);
    memset(p+8+size,0,padded-size);
    return padded+8;
}
/* Structural image drives atomic name admission, not bytecode execution. */
static LbBeamImage *batch_image(LbAllocDomain *d, size_t count, int duplicates)
{
    unsigned char *raw = calloc(1, count*32+256), *atoms = calloc(1,count*32+4);
    unsigned char code[21] = {0}, empty[4] = {0};
    size_t size=12, atom_size=4, i;
    LbBeamImage *image;
    CHECK(raw && atoms); u32(atoms,(uint32_t)count);
    for (i=0; i<count; ++i) {
        char name[32]; int len=snprintf(name,sizeof(name),"batch_term_%zu",duplicates ? i%3 : i);
        atoms[atom_size++]=(unsigned char)len;
        memcpy(atoms+atom_size,name,(size_t)len); atom_size+=(size_t)len;
    }
    u32(code,16); u32(code+8,19); u32(code+12,3); u32(code+16,1); code[20]=3;
    size+=chunk(raw+size,LB_BEAM_ID('A','t','U','8'),atoms,atom_size);
    size+=chunk(raw+size,LB_BEAM_ID('C','o','d','e'),code,sizeof(code));
    size+=chunk(raw+size,LB_BEAM_ID('S','t','r','T'),NULL,0);
    size+=chunk(raw+size,LB_BEAM_ID('I','m','p','T'),empty,4);
    size+=chunk(raw+size,LB_BEAM_ID('E','x','p','T'),empty,4);
    u32(raw,LB_BEAM_ID('F','O','R','1')); u32(raw+4,(uint32_t)size-8); u32(raw+8,LB_BEAM_ID('B','E','A','M'));
    CHECK(lb_beam_image_create(d,raw,size,&image)==LB_BEAM_OK);
    free(raw); free(atoms); return image;
}
static LbAtomTable *base_table(LbAllocDomain *d)
{
    LbAtomTable *table;
    size_t target=8;
    Eterm value;
    CHECK(lb_atoms_create(d,8192,&table)==LB_ATOM_OK);
    while (target<lb_atoms_count(table)) target*=2;
    while (lb_atoms_count(table)<target) {
        char name[48]; int n=snprintf(name,sizeof(name),"fill_atom_%zu",lb_atoms_count(table));
        CHECK(lb_atoms_intern(table,name,(size_t)n,&value)==LB_ATOM_OK);
    }
    return table;
}
static void bindings(void)
{
    Host h={0}, ih={0};
    LbAllocDomain *d=new_domain(&h), *id=new_domain(&ih);
    LbBeamImage *image=batch_image(id,1600,0);
    LbAtomTable *table=base_table(d), *peer;
    LbAtomBinding *binding;
    Eterm value;
    size_t i, start=h.calls, steps, before, bytes, count;
    CHECK(lb_atom_binding_create(table,image,&binding)==LB_ATOM_OK);
    steps=h.calls-start; CHECK(steps>=1604); /* map, stage, nodes, index and/or buckets */
    CHECK(lb_atoms_destroy(table)==LB_ATOM_BUSY);
    lb_atom_binding_destroy(binding); CHECK(lb_atoms_destroy(table)==LB_ATOM_OK);
    table=base_table(d); before=h.live; bytes=h.bytes; count=lb_atoms_count(table);
    for (i=1;i<=steps;++i) {
        h.fail_at=h.calls+i;
        CHECK(lb_atom_binding_create(table,image,&binding)==LB_ATOM_NO_MEMORY && !binding);
        CHECK(h.live==before && h.bytes==bytes && lb_atoms_count(table)==count);
        CHECK(lb_atoms_find(table,"batch_term_0",12,&value)==LB_ATOM_NOT_FOUND);
        name_is(table,am_true,"true",4);
    }
    h.fail_at=0;
    {
        LbAtomTable *limited;
        size_t live, allocated;
        CHECK(lb_atoms_create(d,lb_atoms_predefined_count()+3,&limited)==LB_ATOM_OK);
        live=h.live; allocated=h.bytes;
        CHECK(lb_atom_binding_create(limited,image,&binding)==LB_ATOM_LIMIT && !binding);
        CHECK(h.live==live && h.bytes==allocated && lb_atoms_count(limited)==lb_atoms_predefined_count());
        CHECK(lb_atoms_find(limited,"batch_term_0",12,&value)==LB_ATOM_NOT_FOUND);
        {
            LbBeamImage *duplicates=batch_image(id,1600,1);
            Eterm first, repeated;
            CHECK(lb_atom_binding_create(limited,duplicates,&binding)==LB_ATOM_OK);
            CHECK(lb_atoms_count(limited)==lb_atoms_predefined_count()+3);
            CHECK(lb_atom_binding_get(binding,limited,1,&first)==LB_ATOM_OK);
            CHECK(lb_atom_binding_get(binding,limited,4,&repeated)==LB_ATOM_OK && first==repeated);
            lb_beam_image_destroy(duplicates);
            lb_atom_binding_destroy(binding);
        }
        CHECK(lb_atoms_destroy(limited)==LB_ATOM_OK);
    }
    CHECK(lb_atom_binding_create(table,image,&binding)==LB_ATOM_OK);
    lb_beam_image_destroy(image); CHECK(lb_alloc_domain_destroy(id)==LB_ALLOC_OK && !ih.live);
    CHECK(lb_atoms_create(d,8192,&peer)==LB_ATOM_OK);
    CHECK(lb_atom_binding_get(binding,peer,1,&value)==LB_ATOM_INVALID && is_non_value(value));
    CHECK(lb_atom_binding_get(binding,table,0,&value)==LB_ATOM_OK && is_nil(value));
    CHECK(lb_atom_binding_get(binding,table,1,&value)==LB_ATOM_OK); name_is(table,value,"batch_term_0",12);
    CHECK(lb_atom_binding_get(binding,table,1601,&value)==LB_ATOM_NOT_FOUND);
    lb_atom_binding_destroy(binding);
    CHECK(lb_atoms_destroy(table)==LB_ATOM_OK && lb_atoms_destroy(peer)==LB_ATOM_OK);
    CHECK(lb_alloc_domain_destroy(d)==LB_ALLOC_OK && !h.live && !h.bytes);
}
static void real_image(const char *path)
{
    FILE *file=fopen(path,"rb"); long size; unsigned char *data;
    Host h={0}; LbAllocDomain *d=new_domain(&h);
    LbBeamImage *image; LbAtomTable *table; LbAtomBinding *binding;
    size_t i, count; Eterm term;
    CHECK(file && !fseek(file,0,SEEK_END)); size=ftell(file); CHECK(size>0 && !fseek(file,0,SEEK_SET));
    data=malloc((size_t)size); CHECK(data && fread(data,1,(size_t)size,file)==(size_t)size && !fclose(file));
    CHECK(lb_beam_image_create(d,data,(size_t)size,&image)==LB_BEAM_OK); free(data);
    CHECK(lb_atoms_create(d,8192,&table)==LB_ATOM_OK);
    CHECK(lb_atom_binding_create(table,image,&binding)==LB_ATOM_OK);
    count=lb_beam_image_info(image)->atom_count;
    for (i=1;i<=count;++i) {
        LbBeamBytes file_name, actual;
        CHECK(lb_beam_image_atom(image,(uint32_t)i,&file_name)==LB_BEAM_OK);
        CHECK(lb_atom_binding_get(binding,table,i,&term)==LB_ATOM_OK && is_atom(term));
        CHECK(lb_atoms_name(table,term,&actual)==LB_ATOM_OK);
        CHECK(actual.size==file_name.size && !memcmp(actual.data,file_name.data,actual.size));
    }
    lb_beam_image_destroy(image);
    CHECK(lb_atom_binding_get(binding,table,1,&term)==LB_ATOM_OK); name_is(table,term,"first_slice",11);
    lb_atom_binding_destroy(binding); CHECK(lb_atoms_destroy(table)==LB_ATOM_OK);
    CHECK(lb_alloc_domain_destroy(d)==LB_ALLOC_OK && !h.live && !h.bytes);
    puts("CORE_IMAGE_ATOMS_OK actual_terms=true image_released=true execution=false");
}
int main(int argc, char **argv)
{
    terms(); constructor_failures(); independent_tables(); bindings();
    if (argc==2) real_image(argv[1]); else CHECK(argc==1);
    puts("CORE_TERMS_ATOMS_OK beam_tags=true predefined_ids=true transactional_binding=true engine_lifecycle=false");
    return 0;
}
