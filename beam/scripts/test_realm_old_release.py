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
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

WRAPPER = Path(__file__).with_name('realm-old-release-wrapper.sh').resolve()


class OldReleaseTests(unittest.TestCase):
    def test_adapter_removes_only_implicit_runtime_inputs(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            release = root / 'installed release'
            release.mkdir()
            env = os.environ.copy()
            env.update(REALM_OLD_RELEASE_BIN=str(release), ERL_FLAGS='poison', ERL_AFLAGS='poison',
                       ERL_ZFLAGS='poison', ERL_LIBS='poison', KEEP='retained')
            for tool in ('erl', 'erlc'):
                target = release / tool
                target.write_text('#!/bin/sh\nprintf "%s\\n" "${ERL_FLAGS-unset}" "${ERL_AFLAGS-unset}" "${ERL_ZFLAGS-unset}" "${ERL_LIBS-unset}" "$KEEP" "$@"\n')
                target.chmod(0o755)
                link = root / tool
                link.symlink_to(WRAPPER)
                result = subprocess.run([str(link), 'argument with spaces'], env=env,
                                        capture_output=True, text=True, timeout=5)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stdout.splitlines(), ['unset'] * 4 + ['retained', 'argument with spaces'])

    def test_invalid_directory_and_recursive_target_fail(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            link = root / 'erl'
            link.symlink_to(WRAPPER)
            for location in ('', 'relative', str(root)):
                result = subprocess.run([str(link)], env={**os.environ, 'REALM_OLD_RELEASE_BIN': location},
                                        capture_output=True, timeout=5)
                self.assertEqual(result.returncode, 2)


if __name__ == '__main__':
    unittest.main()
