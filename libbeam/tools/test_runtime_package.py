# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Leandro Ostera <leandro@ostera.io>
import json
from pathlib import Path
import tempfile
import unittest

import export_runtime_package as package


class RuntimePackageTests(unittest.TestCase):
    def fixture(self, root):
        # Parser/copy fixtures, deliberately NOT executable native libraries.
        header = root / 'source/erts/emulator/beam/erl_embed.h'
        header.parent.mkdir(parents=True)
        header.write_text('test header')
        dep = header.parent.parent / 'deps/libethread.debug.a'
        dep.parent.mkdir()
        dep.write_bytes(b'test archive')
        probe = root / 'probe/summary.json'
        probe.parent.mkdir()
        archive = probe.parent / 'libbeam.debug.emu.a'
        archive.write_bytes(b'test emulator archive')
        (probe.parent / 'settings.log').write_text(
            'ARCHIVE=unused\nCXX=c++\nFLAGS=-m64\n'
            'LIBS=-Ldeps -lethread.debug -lpthread -framework Cocoa\n')
        probe.write_text(json.dumps({
            'status': 'started_and_returned_not_shutdown',
            'inputs': {str(header): package.digest(header)},
            'archive_sha256': package.digest(archive),
            'native_link_settings_sha256': package.digest(probe.parent / 'settings.log'),
            'native_link_inputs': {str(dep): package.digest(dep)},
        }))
        return probe, dep

    def test_freezes_dependencies_and_generates_hash_checks(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp).resolve()
            probe, dep = self.fixture(root)
            output = root / 'package'
            package.export(probe, output)
            manifest = json.loads((output / 'manifest.json').read_text())
            self.assertEqual(len(manifest['files']), 3)
            self.assertNotIn(str(dep), manifest['libraries'])
            self.assertIn('SHELL:-framework Cocoa', manifest['options'])
            self.assertIn('file(SHA256', (output / 'runtime-package.cmake').read_text())
            for name, sha in manifest['files'].items():
                self.assertEqual(package.digest(output / name), sha)
            with self.assertRaises(FileExistsError):
                package.export(probe, output)

    def test_rejects_changed_dependency(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp).resolve()
            probe, dep = self.fixture(root)
            dep.write_bytes(b'changed after validation')
            with self.assertRaisesRegex(ValueError, 'changed or lacks provenance'):
                package.export(probe, root / 'package')
            self.assertFalse((root / 'package').exists())

    def test_rejects_unresolved_non_system_library(self):
        with self.assertRaisesRegex(ValueError, 'unresolved non-system'):
            package.link_inputs({'LIBS': '-lunknown_private_dependency', 'FLAGS': ''}, Path('/tmp'))

    def test_rejects_changed_link_settings(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp).resolve()
            probe, _ = self.fixture(root)
            (probe.parent / 'settings.log').write_text('changed')
            with self.assertRaisesRegex(ValueError, 'settings changed'):
                package.export(probe, root / 'package')

    def test_rejects_cmake_list_injection(self):
        with self.assertRaises(ValueError):
            package.quote('a;b')
        self.assertEqual(package.quote('${not_expanded}'), '[=[${not_expanded}]=]')


if __name__ == '__main__':
    unittest.main()
