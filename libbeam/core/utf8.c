/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#include "utf8.h"
#include <stdint.h>
/* Shared, unchanged validation algorithm from the first image slice. */
int lb_utf8_atom_validate(const unsigned char *data, size_t size)
{
    size_t pos = 0, characters = 0;
    if ((!data && size) || size > 4 * 255) return 0;
    while (pos < size) {
        uint32_t code, minimum;
        unsigned n, i, first = data[pos++];
        if (++characters > 255) return 0;
        if (first < 0x80) continue;
        if (first >= 0xc2 && first <= 0xdf) { n = 1; code = first & 31; minimum = 0x80; }
        else if (first >= 0xe0 && first <= 0xef) { n = 2; code = first & 15; minimum = 0x800; }
        else if (first >= 0xf0 && first <= 0xf4) { n = 3; code = first & 7; minimum = 0x10000; }
        else return 0;
        if (n > size - pos) return 0;
        for (i = 0; i < n; ++i) {
            unsigned b = data[pos++];
            if ((b & 0xc0) != 0x80) return 0;
            code = code << 6 | (b & 63);
        }
        if (code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) return 0;
    }
    return 1;
}
