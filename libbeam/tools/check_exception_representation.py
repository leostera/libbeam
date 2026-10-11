#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Leandro Ostera <leandro@ostera.io>
"""Compare native exception encodings/tables, not exception execution.

Caller holds the user-wide validation lock. Primitive term representation is
already oracled independently; no runtime startup or whole-ERTS linkage occurs.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
from check_binary_representation import macros


def table(text, name):
    match = re.search(r'\b'+name+r'\[[^]]+\]\s*=\s*\{(.*?)\};', text, re.S)
    if not match:
        raise RuntimeError('Missing exception table '+name)
    body = re.sub(r'/\*.*?\*/', '', match[1], flags=re.S)
    return [value.strip() for value in body.split(',') if value.strip()]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    manifest = json.loads((root/'libbeam/core/otp/exception-sources.json').read_text())
    for name, digest in manifest['sources'].items():
        if hashlib.sha256((root/name).read_bytes()).hexdigest() != digest:
            raise RuntimeError('Exception reference changed: '+name)
    native = (root/'beam/erts/emulator/beam/error.h').read_text()
    core = (root/'libbeam/core/exception.h').read_text()
    common = (root/'beam/erts/emulator/beam/beam_common.c').read_text()
    implementation = (root/'libbeam/core/exception.c').read_text()
    for name in ('error_atom', 'exception_tag'):
        if table(common, name) != table(implementation, name):
            raise RuntimeError('Exception atom table differs: '+name)
    names = re.findall(r'^#define LB_(\w+)(\([^\n]*?\))?', core, re.M)
    reference = '#include "term.h"\n'+macros(native, ' '.join(name for name, _ in names))
    prefixes = [('reference', reference, ''), ('core', '#include "exception.h"\n', 'LB_')]
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    for mode in ('debug', 'release'):
        results = []
        for kind, header, prefix in prefixes:
            body = '#include <stdio.h>\n#include <inttypes.h>\nint main(void) {\n'
            for name, parameters in names:
                if not parameters:
                    body += f'printf("{name}=%" PRIuPTR "\\n",(Uint){prefix}{name});\n'
            body += 'Uint hash=0; for(Uint code=0;code<32768;++code) {\n'
            for name in ('GET_EXC_CLASS', 'GET_EXC_INDEX', 'PRIMARY_EXCEPTION', 'NATIVE_EXCEPTION'):
                body += f'hash=hash*UINT64_C(1099511628211)+{prefix}{name}(code);\n'
            body += '} printf("fields=%" PRIuPTR "\\n",hash); return 0; }\n'
            source = out/f'{mode}-{kind}.c'
            executable = source.with_suffix('')
            source.write_text(header+body)
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
            raise RuntimeError(mode+': native exception encodings differ')
    print('EXCEPTION_REPRESENTATION_MATCH debug=true release=true codes=true flags=true tables=true execution=false')


if __name__ == '__main__':
    main()
