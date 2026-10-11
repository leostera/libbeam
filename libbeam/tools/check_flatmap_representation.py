#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Leandro Ostera <leandro@ostera.io>
"""Compare native flatmap/MAP3 and BSC identities with the owned representation.

Map-specific declarations/macros come from pinned OTP sources. Both probes use
already-oracled core word/tuple primitives; this is not an execution oracle.
Caller holds the user-wide validation lock.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
from check_binary_representation import declaration, macros

BODY = r'''
#include <stdio.h>
#include <inttypes.h>
int main(void) {
    Eterm storage[10], result;
    const unsigned kinds[]={K_APPEND,K_PRIVATE,K_BINARY,K_BINARY_FIXED,K_ALL,
        K_FLOAT,K_FLOAT_FIXED,K_INTEGER,K_INTEGER_FIXED,K_STRING,K_UTF8,K_UTF16,K_UTF32};
    size_t i;
    result=BUILD(storage,make_small(1),make_small(11),make_small(2),make_small(12),make_small(3),make_small(13));
    printf("layout=%zu:%zu:%zu:%zu words=%u\n",sizeof(M),offsetof(M,thing_word),offsetof(M,size),offsetof(M,keys),(unsigned)WORDS);
    printf("map=%" PRIuPTR ":%" PRIuPTR ":%td keys=%" PRIuPTR " values=%" PRIuPTR ":%" PRIuPTR ":%" PRIuPTR "\n",
        storage[0],storage[1],boxed_val(storage[2])-storage,*boxed_val(storage[2]),storage[3],storage[4],storage[5]);
    printf("result=%td keys=%" PRIuPTR ":%" PRIuPTR ":%" PRIuPTR "\n",boxed_val(result)-storage,storage[7],storage[8],storage[9]);
    for(i=0;i<sizeof(kinds)/sizeof(kinds[0]);++i) printf("kind=%u\n",kinds[i]);
    return 0;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    manifest = json.loads((root/'libbeam/core/otp/flatmap-error-sources.json').read_text())
    for name, digest in manifest['sources'].items():
        if hashlib.sha256((root/name).read_bytes()).hexdigest() != digest:
            raise RuntimeError(f'Flatmap reference changed: {name}')
    maps = (root/'beam/erts/emulator/beam/erl_map.h').read_text()
    term = (root/'beam/erts/emulator/beam/erl_term.h').read_text()
    bits = (root/'beam/erts/emulator/beam/erl_bits.h').read_text()
    reference = '#include "term.h"\ntypedef uint16_t Uint16;\n'
    reference += declaration(maps, r'typedef struct flatmap_s \{.*?\n\} flatmap_t;')
    reference += macros(term, 'MAP_HEADER_TAG_SZ MAP_HEADER_ARITY_SZ MAP_HEADER_TAG_FLATMAP_HEAD _TAG_HEADER_MAP make_flatmap MAP_HEADER MAP_SZ MAP3_SZ MAP3')
    reference += macros(maps, 'MAKE_MAP_HEADER MAP_HEADER_FLATMAP MAP_HEADER_FLATMAP_SZ')
    native_names = 'BSC_APPEND BSC_PRIVATE_APPEND BSC_BINARY BSC_BINARY_FIXED_SIZE BSC_BINARY_ALL BSC_FLOAT BSC_FLOAT_FIXED_SIZE BSC_INTEGER BSC_INTEGER_FIXED_SIZE BSC_STRING BSC_UTF8 BSC_UTF16 BSC_UTF32'
    reference += macros(bits, native_names)
    reference += 'typedef flatmap_t M;\n#define WORDS MAP3_SZ\n#define BUILD MAP3\n'
    core = '#include "flatmap.h"\n#include "binary_error.h"\ntypedef LbFlatMap M;\n#define WORDS LB_MAP3_WORDS\n#define BUILD lb_map3\n'
    aliases = 'K_APPEND K_PRIVATE K_BINARY K_BINARY_FIXED K_ALL K_FLOAT K_FLOAT_FIXED K_INTEGER K_INTEGER_FIXED K_STRING K_UTF8 K_UTF16 K_UTF32'
    for alias, native in zip(aliases.split(), native_names.split(), strict=True):
        reference += f'#define {alias} {native}\n'
        core += f'#define {alias} LB_{native}\n'
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
            result = subprocess.run(command+[str(source), '-o', str(executable)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30)
            source.with_suffix('.compile.log').write_bytes(result.stdout)
            result.check_returncode()
            result = subprocess.run([str(executable)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30)
            source.with_suffix('.log').write_bytes(result.stdout)
            result.check_returncode()
            results.append(result.stdout)
        if results[0] != results[1]:
            raise RuntimeError(f'{mode}: native flatmap representation differs')
    print('FLATMAP_REPRESENTATION_MATCH debug=true release=true map3=true descriptor_identities=true execution=false')


if __name__ == '__main__':
    main()
