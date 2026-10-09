#!/usr/bin/env python3
# %CopyrightBegin%
#
# SPDX-License-Identifier: Apache-2.0
#
# Copyright 2026 Leandro Ostera <leandro@ostera.io>
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# %CopyrightEnd%

"""Unpreprocessed production NIF registrations and driver interface declarations.

Source facts only. Conditions are not evaluated; aliases, native bodies, dynamically
scheduled jobs and arbitrary native effects still require separate semantic review.
"""
import argparse
import csv
import difflib
import hashlib
import io
from pathlib import Path
import re
import subprocess
import sys

LEXEME = re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|/\*[\s\S]*?\*/|//[^\n]*')
TABLE = re.compile(r'\bErlNifFunc\s+(\w+)\s*\[\s*\]\s*=\s*\{([\s\S]*?)\}\s*;')
ENTRY = re.compile(r'\{\s*("[A-Za-z_0-9]+"|NULL)\s*,\s*(\d+)\s*,\s*(\w+)\s*(?:,\s*([\w |()]+?))?\s*\}\s*,?')
INIT = re.compile(r'\bERL_NIF_INIT\s*\(([^()]*)\)')


def uncomment(text):
    return LEXEME.sub(lambda m: re.sub(r'[^\n]', ' ', m[0]) if m[0].startswith('/') else m[0], text)


def nif_rows(text, source):
    clean = uncomment(text)
    # Mask literals for declaration discovery, retaining offsets into the original
    # text so table entries can still parse their quoted Erlang function names.
    masked = LEXEME.sub(lambda m: re.sub(r'[^\n]', ' ', m[0]), clean)
    rows = []
    bindings = {}
    for match in INIT.finditer(masked):
        fields = [s.strip() for s in match[1].split(',')]
        if len(fields) != 6 or any(not re.fullmatch(r'\w+', f) for f in fields):
            raise ValueError(f'{source}: unsupported NIF initialization')
        module, table, *callbacks = fields
        bindings.setdefault(table, set()).add(module)
        for name, callback in zip(('load', 'reload', 'upgrade', 'unload'), callbacks):
            rows.append(['nif_lifecycle', module, name, callback, 'not_applicable',
                         'not_applicable', str(clean.count('\n', 0, match.start()) + 1)])
    if len(list(re.finditer(r'\bERL_NIF_INIT\s*\(', masked))) != len(list(INIT.finditer(masked))):
        raise ValueError(f'{source}: unsupported nested NIF initialization')
    tables = set()
    for match in TABLE.finditer(masked):
        table, body = match[1], clean[match.start(2):match.end(2)]
        tables.add(table)
        # Retain the union of all preprocessor branches, not the current build.
        body = re.sub(r'^\s*#.*$', lambda m: re.sub(r'[^\n]', ' ', m[0]), body, flags=re.M)
        cursor = 0
        for entry in ENTRY.finditer(body):
            if body[cursor:entry.start()].strip():
                raise ValueError(f'{source}: unsupported entry in {table}')
            name, arity, symbol, flags = entry.groups()
            owner = '|'.join(sorted(bindings.get(table, {f'UNBOUND:{table}'})))
            rows.append(['nif_export' if name != 'NULL' else 'nif_sentinel', owner,
                         name.strip('"'), symbol, arity, flags or '0',
                         str(clean.count('\n', 0, match.start(2) + entry.start()) + 1)])
            cursor = entry.end()
        if body[cursor:].strip():
            raise ValueError(f'{source}: unparsed NIF table tail in {table}')
    # Detect changed declaration grammar rather than silently losing a table.
    declarations = len(re.findall(r'\bErlNifFunc\s+\w+\s*\[[^]]*\]\s*=', masked))
    if declarations != len(list(TABLE.finditer(masked))):
        raise ValueError(f'{source}: unsupported NIF table grammar')
    for table in bindings.keys() - tables:
        rows.append(['unresolved_nif_table', '|'.join(sorted(bindings[table])), table,
                     'UNRESOLVED', 'not_applicable', 'not_applicable', '0'])
    return rows


def driver_rows(text, source):
    clean = uncomment(text)
    match = re.search(r'typedef struct erl_drv_entry\s*\{([\s\S]*?)\}\s*ErlDrvEntry;', clean)
    if not match:
        raise ValueError(f'{source}: missing driver entry structure')
    body = match[1]
    pattern = re.compile(r'(\w+)\s*\(\*(\w+)\)\(([^;]*)\);')
    rows = []
    for entry in pattern.finditer(body):
        result, name, args = entry.groups()
        rows.append(['driver_callback', 'ErlDrvEntry', name, name, 'not_applicable',
                     ' '.join(f'{result} ({args})'.split()),
                     str(clean.count('\n', 0, match.start(1) + entry.start()) + 1)])
    if not rows or len(rows) != body.count('(*'):
        raise ValueError(f'{source}: unparsed driver callback grammar')
    declarations = list(re.finditer(r'^EXTERN\s+([^;]+);', clean, re.M))
    if len(declarations) != len(re.findall(r'^EXTERN\s+', clean, re.M)):
        raise ValueError(f'{source}: unterminated driver API declaration')
    for declaration in declarations:
        signature = ' '.join(declaration[1].split())
        function = re.search(r'\b(\w+)\s*\(', signature)
        if function:
            kind, name = 'driver_api', function[1]
        else:
            variable = re.fullmatch(r'[\w\s*]+\s+(\w+)', signature)
            if not variable:
                raise ValueError(f'{source}: unsupported driver global declaration')
            kind, name = 'driver_global', variable[1]
        rows.append([kind, 'erl_driver.h', name, name, 'not_applicable', signature,
                     str(clean.count('\n', 0, declaration.start()) + 1)])
    return rows


def inventory(root):
    paths = subprocess.check_output(['git', 'ls-files', '-z', '--',
        'erts/emulator/nifs', 'lib/*/c_src/*'], cwd=root).decode().split('\0')
    paths = sorted(p for p in paths if p.endswith(('.c', '.cpp', '.h')))
    driver = 'erts/emulator/beam/erl_driver.h'
    all_rows = []
    for source in paths + [driver]:
        data = (root / source).read_bytes()
        rows = driver_rows(data.decode(), source) if source == driver else nif_rows(data.decode(), source)
        digest = hashlib.sha256(data).hexdigest()
        for row in rows:
            all_rows.append(row + [source, digest, 'UNREVIEWED'])
    if not all_rows:
        raise ValueError('empty native surface inventory')
    output = io.StringIO()
    header = Path(__file__).read_text().split('"""', 1)[0].split('\n', 1)[1]
    output.write('\n'.join(line or '#' for line in header.splitlines()) + '\n')
    output.write('# Generated native source facts; all conditional branches retained. Not an allowlist.\n')
    writer = csv.writer(output, delimiter='\t', lineterminator='\n')
    writer.writerow(['kind', 'owner', 'entry', 'c_symbol', 'arity', 'flags_or_signature',
                     'line', 'source', 'source_sha256', 'review_status'])
    writer.writerows(all_rows)
    return output.getvalue()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parent.parent)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--check', action='store_true')
    mode.add_argument('--write', action='store_true')
    args = parser.parse_args()
    try:
        path = args.root.resolve().parent / 'docs/rfds/0001-native-surfaces.tsv'
        generated = inventory(args.root)
        if args.write:
            path.write_text(generated)
        elif not path.exists() or path.read_text() != generated:
            print('\n'.join(difflib.unified_diff(path.read_text().splitlines() if path.exists() else [],
                  generated.splitlines(), fromfile=str(path), tofile='current source')))
            return 1
        print('Native source facts match; no policy approval or completeness claim.')
        return 0
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        print(error, file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
