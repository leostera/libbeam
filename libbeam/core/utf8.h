/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#ifndef LIBBEAM_CORE_UTF8_H
#define LIBBEAM_CORE_UTF8_H
#include <stddef.h>
/* Unicode scalar UTF-8, at most 255 characters. Empty names and embedded NUL
 * are allowed; no normalization or C-string interpretation. */
int lb_utf8_atom_validate(const unsigned char *data, size_t size);
#endif
