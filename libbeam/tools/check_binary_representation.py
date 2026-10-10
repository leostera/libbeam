#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Leandro Ostera <leandro@ostera.io>
"""Compare binary layouts/macros extracted from the pinned flat-64 OTP sources.

The reference counter's implementation is explicitly a word-sized C11 atomic in
both probes, not a claim about every ethread build configuration. No execution or
resource-lifetime claim follows from this layout check. Caller holds the lock.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess


def macros(text, names):
    lines = iter(text.splitlines(keepends=True))
    found = {}
    for line in lines:
        match = re.match(r'\s*#\s*define\s+(\w+)', line)
        if match:
            value = line
            while value.rstrip().endswith('\\'):
                value += next(lines)
            found.setdefault(match[1], value)
    return ''.join(found[name] for name in names.split())


def declaration(text, pattern):
    matches = re.findall(pattern, text, re.S)
    if len(matches) != 1:
        raise RuntimeError(f'Expected one upstream declaration: {pattern}')
    return matches[0] + '\n'


BODY = r'''
#include <stdio.h>
#include <inttypes.h>
int main(void) {
    const size_t sizes[]={0,1,7,8,511,512,513,520,65536*8};
    size_t i;
    printf("binary=%zu:%zu:%zu:%zu\n", offsetof(B,intern.flags),
           offsetof(B,intern.apparent_size),offsetof(B,intern.refc),offsetof(B,orig_bytes));
    printf("binref=%zu:%zu:%zu:%zu\n", sizeof(R),offsetof(R,thing_word),offsetof(R,val),offsetof(R,next));
    printf("subbits=%zu:%zu:%zu:%zu:%zu:%zu\n", sizeof(S),offsetof(S,thing_word),
           offsetof(S,base_flags),offsetof(S,start),offsetof(S,end),offsetof(S,orig));
    printf("headers=%" PRIuPTR ":%" PRIuPTR " threshold=%zu\n",(uintptr_t)REF_HEADER,(uintptr_t)SUB_HEADER,(size_t)LIMIT);
    for(i=0;i<sizeof(sizes)/sizeof(sizes[0]);++i) printf("words=%zu:%zu\n",sizes[i],(size_t)WORDS(sizes[i]));
    return 0;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    manifest = json.loads((root/'libbeam/core/otp/binary-sources.json').read_text())
    for name, digest in manifest['sources'].items():
        if hashlib.sha256((root/name).read_bytes()).hexdigest() != digest:
            raise RuntimeError(f'Binary reference changed: {name}')
    binary = (root/'beam/erts/emulator/beam/erl_binary.h').read_text()
    bits = (root/'beam/erts/emulator/beam/erl_bits.h').read_text()
    term = (root/'beam/erts/emulator/beam/erl_term.h').read_text()
    reference = binary.split('#ifndef ERL_BINARY_H__TYPES__', 1)[0]
    reference += '''#include <stdint.h>
#include <stddef.h>
#include <stdatomic.h>
typedef uintptr_t Uint, UWord, Eterm; typedef intptr_t SWord;
typedef uint64_t Uint64; typedef _Atomic Uint erts_refc_t;
#define ERTS_BINARY_STRUCT_ALIGNMENT
'''
    reference += macros(term, '''_TAG_PRIMARY_SIZE TAG_PRIMARY_HEADER _TAG_HEADER_MASK _HEADER_ARITY_OFFS
        __MAKE_SUBTAG SUB_BITS_SUBTAG BIN_REF_SUBTAG _TAG_HEADER_SUB_BITS _TAG_HEADER_BIN_REF _make_header''')
    reference += declaration(binary, r'struct binary_internals \{.*?\n\};')
    reference += declaration(binary, r'typedef struct binary \{.*?\n\} Binary;')
    reference += declaration(bits, r'typedef struct erl_sub_bits \{.*?\n\} ErlSubBits;')
    reference += declaration(bits, r'typedef struct bin_ref \{.*?\n\} BinRef;')
    reference += declaration(bits, r'typedef struct erl_heap_bits \{.*?\n\} ErlHeapBits;')
    reference += macros(bits, '''ERL_BIN_REF_SIZE ERL_SUB_BITS_SIZE ERL_REFC_BITS_SIZE
        HEADER_SUB_BITS HEADER_BIN_REF ERL_ONHEAP_BINARY_LIMIT ERL_ONHEAP_BITS_LIMIT
        NBYTES heap_bin_size__ heap_bits_size''')
    reference += '''typedef Binary B; typedef BinRef R; typedef ErlSubBits S;
#define REF_HEADER HEADER_BIN_REF
#define SUB_HEADER HEADER_SUB_BITS
#define LIMIT ERL_ONHEAP_BINARY_LIMIT
#define WORDS(bits) ((bits)<=ERL_ONHEAP_BITS_LIMIT ? heap_bits_size(bits) : ERL_REFC_BITS_SIZE)
'''
    core = '''#include "binary.h"
typedef LbBinary B; typedef LbBinRef R; typedef LbSubBits S;
#define REF_HEADER LB_HEADER_BIN_REF
#define SUB_HEADER LB_HEADER_SUB_BITS
#define LIMIT LB_ONHEAP_BINARY_LIMIT
#define WORDS(bits) lb_bitstring_heap_words(bits)
'''
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    for mode in ('debug', 'release'):
        results = []
        for kind, text in [('reference', reference), ('core', core)]:
            source = out/f'{mode}-{kind}.c'
            executable = source.with_suffix('')
            source.write_text(text+BODY)
            command = ['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-I'+str(root/'libbeam/core')]
            if mode == 'release':
                command += ['-DNDEBUG']
            if kind == 'core':
                command += [str(root/'libbeam/core/alloc.c'), str(root/'libbeam/core/binary.c')]
            result = subprocess.run(command+[str(source), '-o', str(executable)],
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30)
            source.with_suffix('.compile.log').write_bytes(result.stdout)
            result.check_returncode()
            result = subprocess.run([str(executable)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30)
            source.with_suffix('.log').write_bytes(result.stdout)
            result.check_returncode()
            results.append(result.stdout)
        if results[0] != results[1]:
            raise RuntimeError(f'{mode}: binary layouts differ from the declared upstream profile')
    print('BINARY_REPRESENTATION_MATCH debug=true release=true scope=flat_64_ordinary_binary_layouts')


if __name__ == '__main__':
    main()
