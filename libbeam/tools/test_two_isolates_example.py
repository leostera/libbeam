# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Leandro Ostera <leandro@ostera.io>

"""Scaffold validation only: linking the C++ API is not emulator acceptance."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class IsolateExampleScaffoldTests(unittest.TestCase):
    def test_example_links_and_stops_at_missing_engine(self):
        compiler = shutil.which('clang++') or shutil.which('c++')
        if compiler is None:
            self.skipTest('C++ compiler unavailable; no runtime acceptance implied')
        root = Path(__file__).resolve().parents[2]
        with tempfile.TemporaryDirectory() as temp:
            out = Path(temp)
            executable = out / 'two_isolates'
            result = subprocess.run(
                [compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror',
                 '-I' + str(root / 'libbeam/include'),
                 str(root / 'libbeam/examples/two_isolates.cpp'),
                 str(root / 'libbeam/src/engine.cpp'), '-o', str(executable)],
                capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            # Opaque nonempty input only: Engine::create must fail before loading.
            # This is NOT a BEAM fixture or a module-loading acceptance test.
            fixture = out / 'opaque-input'
            fixture.write_bytes(b'not BEAM bytecode')
            run = subprocess.run([str(executable), str(fixture), str(fixture)],
                                 capture_output=True, text=True, timeout=10)
            self.assertEqual(run.returncode, 1)
            self.assertIn('not implemented: Engine::create', run.stderr)
            self.assertNotIn(': OK', run.stdout)


if __name__ == '__main__':
    unittest.main()
