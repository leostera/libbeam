/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 * Exact opcode-profile regression: cataloging an OTP form is not admission.
 */
#include "process_internal.h"
#include <stdio.h>
#include <stdlib.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        abort(); \
    } \
} while (0)

int main(void)
{
    CHECK(lb_instruction_supported(op_set_tuple_element_sSP));
    CHECK(lb_instruction_supported(op_i_bs_match_string_xfWW));
    CHECK(lb_instruction_supported(op_i_bxor_jssd));
    CHECK(lb_instruction_supported(op_i_int_bnot_jSd));
    CHECK(lb_instruction_supported(op_i_m_div_jssd));
    puts("OPCODE_PROFILE_OK set_tuple_element=supported match_string=supported");
    return 0;
}
