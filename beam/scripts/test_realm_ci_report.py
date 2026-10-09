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
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('realm_ci_report', Path(__file__).with_name('realm-ci-report.py'))
ci = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ci)
SHA = 'a' * 40
RUNNER = hashlib.sha256(Path(ci.runner.__file__).read_bytes()).hexdigest()


def fixture(machine='x86_64', variant='opt-jit', profile='api'):
    counts = {'public-api': {'realm_api_SUITE': 5}} if profile == 'api' else {
        'realms': {'realm_SUITE': 45, 'realm_process_SUITE': 10, 'realm_resource_SUITE': 9},
        **{n: c for n, _, c in ci.runner.FOCUSED}}
    steps = [{'name': variant + '-' + name, 'status': 'passed', 'returncode': 0,
              'expected': expected, 'results': {suite: dict(passed=n, failed=0, skipped=0, total=n)
                                               for suite, n in expected.items()}}
             for name, expected in counts.items()]
    kind, flavor = ci.runner.VARIANTS[variant]
    steps += [{'name': variant + '-build', 'status': 'passed', 'returncode': 0},
              {'name': variant + '-runtime', 'status': 'passed', 'returncode': 0,
               'command': ['erl', '-emu_type', kind, '-emu_flavor', flavor]}]
    return dict(schema=1, revision=SHA, status='passed', machine=machine,
                runner_sha256=RUNNER, platform='Linux-test', finished_utc='finished',
                worktree=' M erts/preloaded/ebin/erts_internal.beam\n', steps=steps)


class CIReportTests(unittest.TestCase):
    def test_valid_cell_and_generated_preload(self):
        self.assertEqual(ci.validate(fixture(), SHA, 'x86_64', 'opt-jit', 'api', RUNNER), 5)

    def test_provenance_and_source_changes_fail(self):
        for key, value in [('revision', 'b' * 40), ('machine', 'aarch64'), ('status', 'running'),
                           ('platform', 'macOS-ARM64'), ('runner_sha256', 'other'),
                           ('worktree', ' M erts/emulator/beam/bif.c')]:
            with self.subTest(key=key), self.assertRaises(ValueError):
                ci.validate({**fixture(), key: value}, SHA, 'x86_64', 'opt-jit', 'api', RUNNER)

    def test_reduced_skipped_boolean_and_failed_counts_rejected(self):
        for key, value in [('passed', 4), ('skipped', 1), ('failed', True), ('failed', False)]:
            f = fixture()
            f['steps'][0]['results']['realm_api_SUITE'][key] = value
            with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                ci.validate(f, SHA, 'x86_64', 'opt-jit', 'api', RUNNER)

    def test_missing_duplicate_step_and_wrong_probe_fail(self):
        for mutate in (lambda f: f['steps'].pop(), lambda f: f['steps'].append(copy.deepcopy(f['steps'][0])),
                       lambda f: f['steps'][-1]['command'].__setitem__(-1, 'emu')):
            f = fixture()
            mutate(f)
            with self.assertRaises(ValueError):
                ci.validate(f, SHA, 'x86_64', 'opt-jit', 'api', RUNNER)

    def test_complete_matrix_then_missing_artifact(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            empty = ci.collect(root, SHA)
            self.assertEqual(empty['status'], 'incomplete_or_failed')
            self.assertEqual(len(empty['cells']), 8)
            for os_name, machine in ci.PLATFORMS.items():
                for variant in ci.runner.VARIANTS:
                    for profile in ('core', 'api'):
                        path = root / f'realms-{os_name}-{variant}' / f'realm-ci-{profile}' / 'summary.json'
                        path.parent.mkdir(parents=True)
                        path.write_text(json.dumps(fixture(machine, variant, profile)))
            complete = ci.collect(root, SHA)
            self.assertEqual(complete['status'], 'runtime_matrix_passed')
            self.assertEqual(sum(c['passed_cases'] for c in complete['cells']), 8 * 153)
            path.unlink()
            self.assertEqual(ci.collect(root, SHA)['status'], 'incomplete_or_failed')


if __name__ == '__main__':
    unittest.main()
