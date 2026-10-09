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

"""Validate all eight hosted runtime cells, not overall job status or missing artifacts."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import sys

sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location('realm_validation', Path(__file__).with_name('realm-validation.py'))
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)
PLATFORMS = {'ubuntu-24.04': 'x86_64', 'ubuntu-24.04-arm': 'aarch64'}


def validate(report, revision, machine, variant, profile, runner_sha):
    if (report['schema'], report['revision'], report['status'], report['machine'],
        report['runner_sha256']) != (1, revision, 'passed', machine, runner_sha):
        raise ValueError('schema/revision/status/architecture/runner mismatch')
    if not report['platform'].startswith('Linux-') or not report.get('finished_utc'):
        raise ValueError('not a finished Linux run')
    if any(not re.fullmatch(r' M (?:beam/)?erts/preloaded/ebin/[^/]+\.beam', line)
           for line in report['worktree'].splitlines()):
        raise ValueError('source worktree changed beyond generated preloads')
    expected = ({'public-api': {'realm_api_SUITE': 5}} if profile == 'api' else
                {'realms': {'realm_SUITE': 45, 'realm_process_SUITE': 10, 'realm_resource_SUITE': 9},
                 **{name: counts for name, _, counts in runner.FOCUSED}})
    steps = report['steps']
    names = [s['name'] for s in steps]
    wanted = {variant + '-' + name for name in expected} | {variant + '-build', variant + '-runtime'}
    if len(names) != len(set(names)) or set(names) != wanted:
        raise ValueError('missing, duplicate or unexpected steps')
    if any(s['status'] != 'passed' or type(s['returncode']) is not int or s['returncode'] != 0 for s in steps):
        raise ValueError('unfinished/failed step')
    total = 0
    for name, counts in expected.items():
        step = next(s for s in steps if s['name'] == variant + '-' + name)
        exact = {suite: dict(passed=n, failed=0, skipped=0, total=n) for suite, n in counts.items()}
        if (step['expected'] != counts or step['results'] != exact or
            any(type(value) is not int for row in step['results'].values() for value in row.values())):
            raise ValueError('missing, reduced, failed or skipped tests')
        total += sum(counts.values())
    probe = next(s for s in steps if s['name'] == variant + '-runtime')['command']
    kind, flavor = runner.VARIANTS[variant]
    if probe[probe.index('-emu_type') + 1] != kind or probe[probe.index('-emu_flavor') + 1] != flavor:
        raise ValueError('runtime probe variant mismatch')
    return total


def collect(root, revision):
    sha = hashlib.sha256(Path(runner.__file__).read_bytes()).hexdigest()
    cells = []
    for os_name, machine in PLATFORMS.items():
        for variant in runner.VARIANTS:
            cell = dict(os=os_name, variant=variant, status='passed', passed_cases=0, errors=[])
            for profile in ('core', 'api'):
                path = root / f'realms-{os_name}-{variant}' / f'realm-ci-{profile}' / 'summary.json'
                try:
                    cell['passed_cases'] += validate(json.loads(path.read_text()), revision,
                                                     machine, variant, profile, sha)
                except (OSError, ValueError, KeyError, TypeError, StopIteration, IndexError) as error:
                    cell['errors'].append(f'{profile}: {error}')
                    cell['status'] = 'incomplete_or_failed'
            cells.append(cell)
    return dict(schema=1, revision=revision, cells=cells,
                status='runtime_matrix_passed' if all(c['status'] == 'passed' for c in cells)
                else 'incomplete_or_failed', security_acceptance='not_established')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--artifacts', type=Path, required=True)
    parser.add_argument('--revision', required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if not re.fullmatch('[a-f0-9]{40}', args.revision):
        parser.error('expected full source revision')
    report = collect(args.artifacts, args.revision)
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(report['status'])
    return 0 if report['status'] == 'runtime_matrix_passed' else 1


if __name__ == '__main__':
    sys.exit(main())
