#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Leandro Ostera <leandro@ostera.io>
"""Compare the admitted term subset to macro expressions extracted from OTP.

Not a whole-runtime oracle: uses the declared flat 64-bit/non-reservation profile.
The calling validation runner holds the user-wide lock and supplies a fresh dir.
"""
import argparse
import hashlib
import re
import subprocess
from pathlib import Path

# Exact original macros, not reimplemented numeric formulas. Conditional duplicate
# definitions choose the first (64-bit/optimized) branch except NON_VALUE below.
NAMES = '''TAG_PTR_MASK__ TAG_LITERAL_PTR _TAG_PRIMARY_SIZE _TAG_PRIMARY_MASK TAG_PRIMARY_HEADER TAG_PRIMARY_LIST
TAG_PRIMARY_BOXED TAG_PRIMARY_IMMED1 _TAG_IMMED1_SIZE _TAG_IMMED1_MASK
_TAG_IMMED1_IMMED2 _TAG_IMMED1_SMALL _TAG_IMMED2_SIZE _TAG_IMMED2_MASK
_TAG_IMMED2_ATOM _TAG_IMMED2_NIL _TAG_HEADER_MASK _HEADER_ARITY_OFFS
__MAKE_SUBTAG ARITYVAL_SUBTAG FLOAT_SUBTAG POS_BIG_SUBTAG NEG_BIG_SUBTAG REF_SUBTAG
FUN_SUBTAG RECORD_SUBTAG HEAP_BITS_SUBTAG SUB_BITS_SUBTAG BIN_REF_SUBTAG MAP_SUBTAG
EXTERNAL_PID_SUBTAG EXTERNAL_PORT_SUBTAG EXTERNAL_REF_SUBTAG _TAG_HEADER_ARITYVAL _TAG_HEADER_FLOAT
_make_header NIL SMALL_BITS MAX_SMALL MIN_SMALL MAX_ARITYVAL make_small is_small make_atom is_atom
_unchecked_signed_val signed_val _unchecked_atom_val atom_val make_arityval
make_arityval_zero _unchecked_arityval arityval _unchecked_make_boxed make_boxed
_unchecked_ptr_val _unchecked_boxed_val boxed_val _unchecked_make_list make_list
_unchecked_list_val list_val make_tuple TUPLE2 CAR CDR CONS'''.split()
BODY = r'''
#include <inttypes.h>
#include <stdio.h>
int main(void) {
    Eterm h[8], t, l;
    Sint values[] = {MIN_SMALL, MIN_SMALL+1, -1000, -1, 0, 1, 255, MAX_SMALL-1, MAX_SMALL};
    const Eterm headers[] = {ARITYVAL_SUBTAG, POS_BIG_SUBTAG, NEG_BIG_SUBTAG, REF_SUBTAG,
        FUN_SUBTAG, FLOAT_SUBTAG, RECORD_SUBTAG, HEAP_BITS_SUBTAG, SUB_BITS_SUBTAG,
        BIN_REF_SUBTAG, MAP_SUBTAG, EXTERNAL_PID_SUBTAG, EXTERNAL_PORT_SUBTAG, EXTERNAL_REF_SUBTAG};
    size_t i;
    for (i=0;i<sizeof(headers)/sizeof(headers[0]);++i) printf("header=%" PRIuPTR "\n", (uintptr_t)headers[i]);
    printf("word=%zu nil=%" PRIuPTR " atom0=%" PRIuPTR " atom1024=%" PRIuPTR " nonvalue=%" PRIuPTR "\n",
           sizeof(Eterm), (uintptr_t)NIL, (uintptr_t)make_atom(0), (uintptr_t)make_atom(1024), (uintptr_t)THE_NON_VALUE);
    for (i=0;i<sizeof(values)/sizeof(values[0]);++i)
        printf("small=%" PRIuPTR ":%" PRIdPTR "\n", (uintptr_t)make_small(values[i]), (intptr_t)signed_val(make_small(values[i])));
    t = TUPLE2(h, make_small(-7), make_atom(42));
    l = CONS(h+4,t,NIL);
    printf("tuple=%" PRIuPTR ":%" PRIuPTR ":%" PRIuPTR " list=%" PRIuPTR " literal=%d\n",
           (uintptr_t)h[0], (uintptr_t)arityval(h[0]), (uintptr_t)(t-(Uint)h),
           (uintptr_t)(l-(Uint)(h+4)), boxed_val(t|TAG_LITERAL_PTR)==h);
    printf("fields=%" PRIdPTR ":%" PRIuPTR ":%d:%" PRIuPTR "\n",
           (intptr_t)signed_val(h[1]), (uintptr_t)atom_val(h[2]), CAR(list_val(l))==t, (uintptr_t)CDR(list_val(l)));
    return 0;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    source = root / 'beam/erts/emulator/beam/erl_term.h'
    if hashlib.sha256(source.read_bytes()).hexdigest() != '8e016126fb7a1bc1db8d31111601c0bea21d72207211bf91e1f3c127e943b5c2':
        raise RuntimeError('Term reference changed; review admission record')
    lines = iter(source.read_text().splitlines(keepends=True))
    definitions = {}
    for line in lines:
        match = re.match(r'\s*#\s*define\s+(\w+)', line)
        if not match:
            continue
        text = line
        while text.rstrip().endswith('\\'):
            text += next(lines)
        definitions.setdefault(match[1], []).append(text)
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    for mode in ('debug', 'release'):
        reference = source.read_text().split('#ifndef __ERL_TERM_H', 1)[0]
        reference += '#include <stdint.h>\n#include <stddef.h>\n#include <assert.h>\n'
        reference += 'typedef uintptr_t Eterm; typedef uintptr_t Uint; typedef intptr_t Sint;\n'
        reference += '#define SWORD_CONSTANT(x) ((Sint)(x))\n#define ASSERT(x) assert(x)\n'
        reference += '#define _ET_APPLY(f,x) _unchecked_##f(x)\n'
        for name in NAMES:
            reference += definitions[name][0]
        reference += definitions['THE_NON_VALUE'][0 if mode == 'debug' else -1]
        logs = []
        for kind, text in [('reference', reference + BODY), ('core', '#include "term.h"\n' + BODY)]:
            path = out / f'{mode}-{kind}.c'
            binary = path.with_suffix('')
            path.write_text(text)
            command = ['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-I'+str(root/'libbeam/core')]
            if mode == 'release': command += ['-DNDEBUG']
            command += [str(path), '-o', str(binary)]
            result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30)
            path.with_suffix('.compile.log').write_bytes(result.stdout)
            result.check_returncode()
            result = subprocess.run([str(binary)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30)
            path.with_suffix('.log').write_bytes(result.stdout)
            result.check_returncode()
            logs.append(result.stdout)
        if logs[0] != logs[1]:
            raise RuntimeError(f'{mode}: core representation differs from OTP macro subset')
    print('TERM_REPRESENTATION_MATCH debug=true release=true scope=admitted_64_bit_subset')


if __name__ == '__main__':
    main()
