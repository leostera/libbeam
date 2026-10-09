# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Leandro Ostera <leandro@ostera.io>

"""API-shape validation only. This does not link or exercise an emulator."""
from pathlib import Path
import shutil
import subprocess
import unittest


class ProposedIsolateExampleTests(unittest.TestCase):
    def test_example_typechecks_without_an_implementation(self):
        compiler = shutil.which('clang++') or shutil.which('c++')
        if compiler is None:
            self.skipTest('C++ compiler unavailable; no runtime acceptance implied')
        root = Path(__file__).resolve().parents[2]
        result = subprocess.run(
            [compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror', '-fsyntax-only',
             str(root / 'libbeam/examples/two_isolates.cpp')],
            capture_output=True, text=True, timeout=60)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
