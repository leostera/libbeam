/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#ifndef LIBBEAM_CORE_LOAD_REPORT_H
#define LIBBEAM_CORE_LOAD_REPORT_H
#include <stddef.h>
/* Value-only loader diagnostic. Stage is static runtime metadata; names are
 * copied UTF-8 bytes, not atom handles, C strings or borrowed preparation data.
 * Batch errors optionally identify the input module index. Native atoms have at
 * most 255 code points (1020 UTF-8 bytes). */
typedef struct {
    const char *stage;
    size_t offset;
    size_t module_index;
    int has_module_index;
    int has_import;
    size_t module_size,function_size;
    unsigned arity;
    unsigned char module[1020],function[1020];
} LbLoadReport;
#endif
