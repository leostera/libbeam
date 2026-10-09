# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Leandro Ostera <leandro@ostera.io>

import argparse
from pathlib import Path
import tempfile
import unittest
import otp_validation as validation


class ValidationTests(unittest.TestCase):
    def plan(self, profile):
        return validation.plan(argparse.Namespace(
            variants=['debug-emu'], make='make', jobs=4, skip_build=False,
            root=Path('/otp'), output=Path('/results'), profile=profile,
            build_timeout=60, test_timeout=60))

    def test_focused_is_84_upstream_cases_without_realms(self):
        plan = self.plan('focused')
        self.assertEqual(sum(sum(s.get('expected', {}).values()) for s in plan), 84)
        self.assertNotIn('realm', str(plan).lower())

    def test_resources_is_35_upstream_cases(self):
        plan = self.plan('resources')
        self.assertEqual(sum(sum(s.get('expected', {}).values()) for s in plan), 35)
        self.assertNotIn('realm', str(plan).lower())

    def test_startup_runs_three_fresh_vms(self):
        plan = self.plan('startup')
        runs = [s for s in plan if 'startup_probe:run()' in str(s['command'])]
        self.assertEqual(len(runs), 3)
        self.assertTrue(all(s['timeout'] == 60 for s in runs))
        self.assertTrue(all('build_type := debug, flavor := emu' in str(s['command'])
                            for s in runs))

    def test_exact_counts_required(self):
        with tempfile.TemporaryDirectory() as temp:
            log = Path(temp) / 'ct.log'
            good = 'Testing test.monitor_SUITE: TEST COMPLETE, 25 ok, 0 failed of 25 test cases\n'
            log.write_text(good)
            self.assertEqual(validation.ct_results(log, {'monitor_SUITE': 25})
                             ['monitor_SUITE']['passed'], 25)
            for bad in ('', good + good, good.replace('25 ok', '24 ok'),
                        good.replace('0 failed', '0 failed, 1 skipped')):
                log.write_text(bad)
                with self.assertRaises(ValueError):
                    validation.ct_results(log, {'monitor_SUITE': 25})

    def test_execute_success_and_timeout(self):
        import os
        import sys
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            passed = validation.execute([sys.executable, '-c', 'pass'], root,
                                        os.environ, root / 'passed.log', 10)
            self.assertEqual(passed['status'], 'passed')
            timed_out = validation.execute([sys.executable, '-c', 'import time; time.sleep(60)'],
                                           root, os.environ, root / 'timeout.log', 0.05)
            self.assertEqual(timed_out['status'], 'timed_out')


if __name__ == '__main__':
    unittest.main()
