/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Test-only dlopen witness. It is never linked into the production runtime.
 */
#include <stdio.h>
#include <stdlib.h>

__attribute__((constructor)) static void boundary_library_loaded(void)
{
    const char *path = getenv("LIBBEAM_BOUNDARY_LIBRARY_MARKER");
    if (path) {
        FILE *file = fopen(path, "wb");
        if (file) {
            fputs("native constructor executed", file);
            fclose(file);
        }
    }
}
