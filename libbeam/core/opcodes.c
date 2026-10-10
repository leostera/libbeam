/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#include "opcodes.h"
#include "lb_opcodes_generated.inc"
_Static_assert(MAX_GENERIC_OPCODE < 256, "File opcodes occupy one byte");
_Static_assert(TAG_u == 0 && TAG_i == 1 && TAG_a == 2 && TAG_z == 7, "Compact file tags");
