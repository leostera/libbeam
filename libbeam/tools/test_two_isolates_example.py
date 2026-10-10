# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Leandro Ostera <leandro@ostera.io>

"""Real Engine linkage; the unchanged stateful Isolate target must still refuse."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class IsolateExampleBoundaryTests(unittest.TestCase):
    def test_example_links_and_stops_at_missing_isolate(self):
        if not shutil.which('cmake'):
            self.skipTest('CMake unavailable; no runtime acceptance implied')
        root = Path(__file__).resolve().parents[2]
        with tempfile.TemporaryDirectory() as temp:
            out = Path(temp)
            # Build-graph sentinel, never a runtime implementation: a production
            # dependency on this nonexistent archive must fail the link. The
            # optional package must affect only unbuilt diagnostic targets.
            package = out / 'diagnostic-package.cmake'
            package.write_text('add_library(libbeam_erts STATIC IMPORTED)\n'
                               'set_target_properties(libbeam_erts PROPERTIES IMPORTED_LOCATION "'+
                               str(out / 'must-not-link-erts.a')+'")\n')
            for command in (
                ['cmake', '-S', str(root / 'libbeam'), '-B', str(out / 'build'),
                 '-DBUILD_TESTING=OFF', '-DCMAKE_BUILD_TYPE=Debug',
                 '-DLIBBEAM_ERTS_PACKAGE='+str(package),
                 '-DCMAKE_CXX_FLAGS=-Wall -Wextra -Werror'],
                ['cmake', '--build', str(out / 'build'), '--target', 'two_isolates'],
            ):
                result = subprocess.run(command, capture_output=True, text=True, timeout=180)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            # Opaque nonempty input only: Isolate creation must fail before load.
            # Not a BEAM fixture or a stateful Isolate acceptance test.
            fixture = out / 'opaque-input'
            fixture.write_bytes(b'not BEAM bytecode')
            run = subprocess.run([str(out / 'build/two_isolates'), str(fixture), str(fixture)],
                                 capture_output=True, text=True, timeout=10)
            self.assertEqual(run.returncode, 1)
            self.assertIn('not implemented: Engine::create_isolate', run.stderr)
            self.assertNotIn(': OK', run.stdout)


if __name__ == '__main__':
    unittest.main()
