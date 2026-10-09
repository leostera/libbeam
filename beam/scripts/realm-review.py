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

"""Check initial operation-review coverage and declared dependency fingerprints.

A current fingerprint is not semantic approval. No transitive C call-graph coverage
or independent review is claimed. Refresh explicitly only after reviewing changes.
"""
import argparse
import csv
import hashlib
import importlib.util
import io
from pathlib import Path
import sys

sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location('realm_inventory', Path(__file__).with_name('realm-inventory.py'))
inv = importlib.util.module_from_spec(spec)
spec.loader.exec_module(inv)
FIELDS = ['id', 'entries', 'context', 'effects', 'targets', 'disposition', 'rejection',
          'lifetime_accounting', 'implementation_gap', 'evidence', 'remaining_tests',
          'owner', 'source_files', 'source_snapshot_sha256', 'review_state']
DISPOSITIONS = {'realm_local', 'explicit_management', 'brokered_rights', 'denied'}


def source_digest(root, sources):
    paths = sources.split(';')
    if not paths or len(paths) != len(set(paths)):
        raise ValueError('empty or duplicate dependency set')
    digest = hashlib.sha256()
    for name in sorted(paths):
        path = Path(name)
        if path.is_absolute() or '..' in path.parts or not name:
            raise ValueError('dependency must be a relative repository path')
        data = (root / path).read_bytes()
        digest.update(name.encode() + b'\0' + hashlib.sha256(data).digest())
    return digest.hexdigest()


def read_records(text):
    reader = csv.DictReader((line for line in text.splitlines() if line and not line.startswith('#')), delimiter='\t')
    if reader.fieldnames != FIELDS:
        raise ValueError('unexpected review schema')
    rows = list(reader)
    if not rows:
        raise ValueError('empty review registry')
    return rows


def check_records(root, rows, entries, refresh=False):
    ids, covered = set(), set()
    for row in rows:
        if set(row) != set(FIELDS) or any(not isinstance(v, str) or not v.strip() for v in row.values()):
            raise ValueError('missing or extra review fields')
        if row['id'] in ids or row['disposition'] not in DISPOSITIONS:
            raise ValueError('duplicate ID or invalid disposition')
        ids.add(row['id'])
        if row['review_state'] != 'DESIGN_REVIEWED_ENFORCEMENT_OPEN':
            raise ValueError('this registry cannot certify runtime or independent review')
        for entry in row['entries'].split(';'):
            if entry not in entries or entry in covered:
                raise ValueError(f'unknown or duplicate reviewed entry: {entry}')
            covered.add(entry)
        digest = source_digest(root, row['source_files'])
        if refresh:
            row['source_snapshot_sha256'] = digest
        elif row['source_snapshot_sha256'] != digest:
            raise ValueError(f"{row['id']}: source dependency changed; re-review required")
    return covered


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parent.parent)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--check', action='store_true')
    mode.add_argument('--refresh-fingerprints', action='store_true')
    args = parser.parse_args()
    root = args.root.resolve()
    try:
        path = root.parent / 'docs/rfds/0001-operation-reviews.tsv'
        text = path.read_text()
        rows = read_records(text)
        bifs, _ = inv.dispatch_rows((root / inv.BIFS).read_text(), (root / inv.DIRTY).read_text())
        nif = csv.DictReader((line for line in inv.nif_inventory(root).splitlines()
                             if line and not line.startswith('#')), delimiter='\t')
        entries = {'bif:' + row[0] for row in bifs} | {'nif-api:' + row['c_symbol'] for row in nif}
        covered = check_records(root, rows, entries, args.refresh_fingerprints)
        if args.refresh_fingerprints:
            output = io.StringIO()
            output.write('\n'.join(line for line in text.splitlines() if line.startswith('#')) + '\n')
            writer = csv.DictWriter(output, FIELDS, delimiter='\t', lineterminator='\n')
            writer.writeheader()
            writer.writerows(rows)
            path.write_text(output.getvalue())
        print(f'{len(rows)} design records, {len(covered)} declared entries; '
              f'{len(entries - covered)} lack design records. Enforcement/independent review open.')
        return 0
    except (OSError, ValueError) as error:
        print(error, file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
