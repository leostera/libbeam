/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Verifier regression for a bs_match equality spanning one machine word.
 */
#include "code_internal.h"
#include <stdio.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

typedef struct { size_t live; } Memory;

static void *allocate(void *context, size_t size)
{
    Memory *memory = context;
    void *result = malloc(size);
    if (result) ++memory->live;
    return result;
}

static void release(void *context, void *pointer)
{
    Memory *memory = context;
    CHECK(memory->live > 0);
    --memory->live;
    free(pointer);
}

static unsigned char *read_file(const char *path, size_t *size)
{
    FILE *file = fopen(path, "rb");
    CHECK(file != NULL);
    CHECK(fseek(file, 0, SEEK_END) == 0);
    long length = ftell(file);
    CHECK(length > 0 && fseek(file, 0, SEEK_SET) == 0);
    unsigned char *bytes = malloc((size_t)length);
    CHECK(bytes != NULL);
    CHECK(fread(bytes, 1, (size_t)length, file) == (size_t)length);
    CHECK(fclose(file) == 0);
    *size = (size_t)length;
    return bytes;
}

int main(int argc, char **argv)
{
    CHECK(argc == 2);
    Memory memory = {0};
    LbSystemAllocator allocator = {&memory, allocate, release};
    LbEngine *engine = NULL;
    LbCodeSpace *space = NULL;
    CHECK(lb_engine_create(&allocator, &engine) == LB_ENGINE_OK);
    CHECK(lb_code_space_create(engine, &space) == LB_CODE_OK);

    size_t byte_count;
    unsigned char *bytes = read_file(argv[1], &byte_count);
    LbBeamProgram *program = NULL;
    LbBeamError error;
    CHECK(lb_beam_program_prepare(space->domain, space->atoms, bytes, byte_count,
                                  &program, &error) == LB_BEAM_OK);
    const LbBeamImageInfo *info = lb_beam_image_info(program->image);
    BeamFile_ImportEntry *imports = calloc(info->import_count ? info->import_count : 1,
                                           sizeof(*imports));
    CHECK(imports != NULL);
    for (size_t i = 0; i < info->import_count; ++i) {
        LbBeamImport import;
        CHECK(lb_beam_image_import(program->image, i, &import) == LB_BEAM_OK);
        imports[i] = (BeamFile_ImportEntry){program->file_atoms[import.module],
                                            program->file_atoms[import.function],
                                            import.arity};
    }

    LoaderState state = {.space = space, .program = program,
                         .label_count = info->label_count};
    state.beam.imports.count = info->import_count;
    state.beam.imports.entries = imports;
    CHECK(lb_verify_program(&state) == LB_CODE_OK);

    int found_full_word_equality = 0;
    for (BeamOp *op = program->ops; op; op = op->next) {
        if (op->op != genop_bs_match_3) continue;
        Uint packed = 0;
        for (unsigned pos = 3; pos < op->arity;) {
            Eterm command = (Eterm)op->a[pos].val;
            if (command == am_Eq) {
                CHECK(pos + 4 <= op->arity);
                CHECK(op->a[pos + 1].type == TAG_n);
                CHECK(op->a[pos + 2].type == TAG_u);
                packed += (Uint)op->a[pos + 2].val;
                if (packed == sizeof(Uint) * 8) found_full_word_equality = 1;
                pos += 4;
            } else if (command == am_ensure_at_least) {
                packed = 0;
                pos += 3;
            } else if (command == am_ensure_exactly || command == am_skip) {
                packed = 0;
                pos += 2;
            } else if (command == am_integer) {
                CHECK(pos + 6 <= op->arity);
                pos += 6;
            } else {
                CHECK(0 && "unexpected command in minimal equality fixture");
            }
        }
    }
    if (sizeof(Uint) == 8) CHECK(found_full_word_equality);

    free(imports);
    lb_beam_program_destroy(program);
    free(bytes);
    CHECK(lb_code_space_destroy(space) == LB_CODE_OK);
    CHECK(lb_engine_shutdown(engine) == LB_ENGINE_OK);
    lb_engine_release(engine);
    CHECK(memory.live == 0);
    puts("CORE_BS_MATCH_EQUALITY_OK full_word_admitted=true");
    return 0;
}
