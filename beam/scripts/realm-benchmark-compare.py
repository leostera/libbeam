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

"""Compare matched benchmark observations without inventing acceptance thresholds."""
import argparse
from collections import Counter
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import sys

sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location('realm_benchmark', Path(__file__).with_name('realm-benchmark.py'))
bench = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bench)
MATCH = ('platform', 'fixture_sha256', 'driver_sha256', 'variant', 'iterations', 'repetitions', 'schedulers')


def validate(report):
    if (any(type(report[k]) is not int for k in ('iterations', 'repetitions', 'schedulers')) or
        not 100 <= report['iterations'] <= 10000 or report['schedulers'] < 1):
        raise ValueError('invalid sampling configuration')
    if report['status'] != 'measured_not_accepted' or report['repetitions'] < 3:
        raise ValueError('unfinished or insufficient benchmark repetitions')
    modes = report['modes']
    if not modes or len(set(modes)) != len(modes) or not set(modes) <= {'host', 'restricted'}:
        raise ValueError('invalid modes')
    if Counter(r['mode'] for r in report['results']) != {mode: report['repetitions'] for mode in modes}:
        raise ValueError('missing or duplicate mode repetitions')
    architectures = set()
    kind, flavor = bench.runner.VARIANTS[report['variant']]
    for result in report['results']:
        if (result['iterations'], result['schedulers'], result['build_type'], result['flavor']) != (
                report['iterations'], report['schedulers'], kind, flavor):
            raise ValueError('raw runtime configuration mismatch')
        architecture = result['architecture']
        if isinstance(architecture, list) and all(type(c) is int and 0 <= c < 128 for c in architecture):
            architecture = bytes(architecture).decode('ascii')
        if not isinstance(architecture, str) or not architecture:
            raise ValueError('invalid runtime architecture')
        architectures.add(architecture)
        measurements = result['measurements']
        if len(measurements) != len(bench.NAMES) or {m['name'] for m in measurements} != bench.NAMES:
            raise ValueError('missing/duplicate workloads')
        for m in measurements:
            if type(m['supported']) is not bool:
                raise ValueError('invalid support marker')
            if not m['supported']:
                if m['name'] != 'realm_activation_stop' and not (result['mode'] == 'restricted' and m['name'] == 'registration'):
                    raise ValueError('unexpected unsupported workload')
                continue
            if m['name'] == 'process_memory':
                if m['processes'] != min(report['iterations'], 100) or type(m['process_info_memory_sum']) is not int or m['process_info_memory_sum'] <= 0:
                    raise ValueError('invalid memory sample')
            else:
                values = [m[k] for k in ('min_ns', 'p50_ns', 'p99_ns', 'max_ns', 'total_ns')]
                if any(type(v) is not int or v < 0 for v in values) or values != sorted(values):
                    raise ValueError('invalid timing sample')
                expected = min(report['iterations'], 100) if m['name'] == 'realm_activation_stop' else report['iterations']
                if m['samples'] != expected:
                    raise ValueError('reduced sample count')
    if len(architectures) != 1 or report['aggregate'] != bench.aggregate(report['results']):
        raise ValueError('architecture changed or aggregate does not match raw data')
    if any(not math.isfinite(row[key]) for row in report['aggregate'] for key in ('median', 'min', 'max', 'population_stddev')):
        raise ValueError('non-finite aggregate')
    return architectures.pop()


def compare(baseline, candidate):
    if validate(baseline) != validate(candidate) or any(baseline[k] != candidate[k] for k in MATCH):
        raise ValueError('incomparable architecture, fixture, driver, platform, variant or sampling configuration')
    if baseline['modes'] != ['host']:
        raise ValueError('baseline must be host-only')
    reference = {(r['workload'], r['metric']): r for r in baseline['aggregate']}
    rows, unavailable = [], []
    for row in candidate['aggregate']:
        key = row['workload'], row['metric']
        if key not in reference:
            unavailable.append(dict(mode=row['mode'], workload=key[0], metric=key[1], reason='no_baseline'))
            continue
        ref = reference[key]
        rows.append(dict(mode=row['mode'], workload=key[0], metric=key[1],
                         baseline=ref, candidate=row,
                         median_ratio=row['median'] / ref['median'] if ref['median'] else None,
                         observed_ratio_min=row['min'] / ref['max'] if ref['max'] else None,
                         observed_ratio_max=row['max'] / ref['min'] if ref['min'] else None))
    for mode in candidate['modes']:
        present = {(r['workload'], r['metric']) for r in candidate['aggregate'] if r['mode'] == mode}
        for key in sorted(reference.keys() - present):
            unavailable.append(dict(mode=mode, workload=key[0], metric=key[1], reason='candidate_unsupported'))
    return dict(schema=1, status='compared_not_accepted', thresholds='unagreed',
                range_interpretation='cross-repetition extrema ratios, not confidence intervals or paired estimates',
                configuration={k: baseline[k] for k in MATCH},
                baseline_revision=baseline['revision'], candidate_revision=candidate['revision'],
                comparisons=rows, unavailable=unavailable)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', type=Path, required=True)
    parser.add_argument('--candidate', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    try:
        report = compare(json.loads(args.baseline.read_text()), json.loads(args.candidate.read_text()))
        report['inputs'] = {name: {'path': str(path), 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}
                            for name, path in [('baseline', args.baseline), ('candidate', args.candidate)]}
        args.output.write_text(json.dumps(report, indent=2) + '\n')
        print(report['status'])
        return 0
    except (OSError, ValueError, KeyError, TypeError) as error:
        print(error, file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
