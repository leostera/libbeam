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

import argparse
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import build_baseline as baseline
import link_archive_probe as link_probe


class BaselineTests(unittest.TestCase):
    def test_environment_is_sanitized_without_mutating_caller(self):
        original = {"PATH": "/usr/bin", "ERL_FLAGS": "bad", "ERL_TOP": "/old",
                    "ERL_COMPILER_OPTIONS": "bad", "ERL_ROOTDIR": "/installed"}
        env, removed = baseline.clean_environment(Path('/repo/beam'), Path('/logs'), original)
        self.assertEqual(env['ERL_TOP'], '/repo/beam')
        self.assertEqual(env['PATH'], '/repo/beam/bin:/usr/bin')
        self.assertNotIn('ERL_COMPILER_OPTIONS', env)
        self.assertNotIn('ERL_ROOTDIR', env)
        self.assertEqual(set(removed), set(original) - {'PATH'})
        self.assertEqual(original['ERL_TOP'], '/old')

    def test_build_order_includes_preloaded_regeneration_and_rebuild(self):
        plan = baseline.build_plan(Path('/repo/beam'), 4, ['--with-ssl=/ssl'])
        self.assertEqual([p[0] for p in plan], ['configure', 'bootstrap-build',
                                               'update-preloaded', 'rebuilt-preloaded'])
        self.assertEqual(plan[0][1][-1], '--with-ssl=/ssl')
        self.assertEqual(plan[2][1][-1], '--no-commit')
        self.assertEqual(plan[3][1], ['make', '-j4'])

    def test_legacy_compatibility_suites_are_opt_in(self):
        self.assertEqual(baseline.validation_profiles(False), ('startup',))
        self.assertEqual(baseline.validation_profiles(True),
                         ('focused', 'resources', 'startup'))

    def test_positive_counts(self):
        self.assertEqual(baseline.positive('4'), 4)
        for value in ('0', '-1'):
            with self.assertRaises(argparse.ArgumentTypeError):
                baseline.positive(value)

    def test_source_must_be_clean_unconfigured_repository_root(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp).resolve()
            source = root / 'beam'
            source.mkdir()
            (source / 'otp_build').touch()
            with patch.object(baseline, 'git', side_effect=[str(root), '']):
                self.assertEqual(baseline.check_source(root), source)
            with patch.object(baseline, 'git', side_effect=[str(root), ' M file']):
                with self.assertRaisesRegex(ValueError, 'clean detached'):
                    baseline.check_source(root)
            with patch.object(baseline, 'git', return_value=str(root.parent)):
                with self.assertRaisesRegex(ValueError, 'repository root'):
                    baseline.check_source(root)
            (source / '.git').touch()
            with patch.object(baseline, 'git', side_effect=[str(root), '']):
                with self.assertRaisesRegex(ValueError, 'not a submodule'):
                    baseline.check_source(root)
            (source / '.git').unlink()
            (source / 'Makefile').touch()
            with patch.object(baseline, 'git', side_effect=[str(root), '']):
                with self.assertRaisesRegex(ValueError, 'already configured'):
                    baseline.check_source(root)


class LinkSettingsTests(unittest.TestCase):
    def test_complete_settings_allow_make_chatter(self):
        text = ('make: Entering directory\nARCHIVE=/tmp/libbeam.a\n'
                'CXX=clang++\nFLAGS=-g\nLIBS=-lm -framework Cocoa\n')
        result = link_probe.parse_settings(text)
        self.assertEqual(result['ARCHIVE'], '/tmp/libbeam.a')
        self.assertEqual(result['LIBS'], '-lm -framework Cocoa')

    def test_entry_symbol_may_be_hidden_but_must_be_defined(self):
        for symbol in ('000001 T erl_start', '000001 t _erl_start'):
            self.assertTrue(link_probe.has_entry_symbol(symbol))
        for symbol in (' U _erl_start', '000001 t unrelated', ''):
            self.assertFalse(link_probe.has_entry_symbol(symbol))

    def test_missing_duplicate_and_empty_compiler_rejected(self):
        base = 'ARCHIVE=/tmp/libbeam.a\nCXX=clang++\nFLAGS=\nLIBS=\n'
        for text in (base.replace('LIBS=\n', ''), base + 'CXX=g++\n',
                     base.replace('CXX=clang++', 'CXX=')):
            with self.subTest(text=text), self.assertRaises(ValueError):
                link_probe.parse_settings(text)


if __name__ == '__main__':
    unittest.main()
