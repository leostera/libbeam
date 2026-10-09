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
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('benchmark_compare', Path(__file__).with_name('realm-benchmark-compare.py'))
compare = importlib.util.module_from_spec(spec)
spec.loader.exec_module(compare)


def fixture(factor=1):
    measurements = []
    for name in sorted(compare.bench.NAMES):
        m = dict(name=name, supported=name != 'realm_activation_stop')
        if name == 'process_memory':
            m.update(processes=100, process_info_memory_sum=10000 * factor)
        else:
            m.update(samples=1000, min_ns=factor, p50_ns=2 * factor, p99_ns=3 * factor,
                     max_ns=4 * factor, total_ns=4000 * factor)
        measurements.append(m)
    raw = dict(mode='host', iterations=1000, schedulers=2, build_type='opt', flavor='jit',
               architecture=list(b'aarch64-test'), measurements=measurements)
    report = dict(status='measured_not_accepted', repetitions=3, modes=['host'],
                  results=[copy.deepcopy(raw) for _ in range(3)], variant='opt-jit',
                  platform='test', fixture_sha256='fixture', driver_sha256='driver',
                  iterations=1000, schedulers=2, revision='revision')
    report['aggregate'] = compare.bench.aggregate(report['results'])
    return report


class BenchmarkCompareTests(unittest.TestCase):
    def test_ratios_are_observations_not_acceptance(self):
        report = compare.compare(fixture(), fixture(2))
        self.assertEqual(report['status'], 'compared_not_accepted')
        self.assertEqual(report['thresholds'], 'unagreed')
        self.assertTrue(all(row['median_ratio'] == 2 for row in report['comparisons']))

    def test_mismatched_configuration_rejected(self):
        for key in compare.MATCH:
            with self.subTest(key=key), self.assertRaises((ValueError, KeyError, TypeError)):
                compare.compare(fixture(), {**fixture(), key: 'different'})

    def test_missing_results_stale_aggregate_and_bad_data_fail(self):
        for mutate in (lambda f: f['results'].pop(), lambda f: f['aggregate'].pop(),
                       lambda f: f['results'][0]['measurements'][0].update(p99_ns=float('nan')),
                       lambda f: f['results'][0]['measurements'][0].update(samples=9),
                       lambda f: f['results'][0]['measurements'][0].update(supported=False)):
            data = fixture()
            mutate(data)
            with self.assertRaises(ValueError):
                compare.validate(data)

    def test_zero_clock_baseline_and_architecture_mismatch(self):
        base = fixture()
        for result in base['results']:
            row = next(m for m in result['measurements'] if m['name'] == 'clock_pair')
            row.update(min_ns=0, p50_ns=0)
        base['aggregate'] = compare.bench.aggregate(base['results'])
        report = compare.compare(base, fixture())
        row = next(r for r in report['comparisons'] if (r['workload'], r['metric']) == ('clock_pair', 'p50_ns'))
        self.assertIsNone(row['median_ratio'])
        self.assertIsNone(row['observed_ratio_max'])
        other = fixture()
        for result in other['results']:
            result['architecture'] = 'x86_64-test'
        with self.assertRaises(ValueError):
            compare.compare(base, other)


if __name__ == '__main__':
    unittest.main()
