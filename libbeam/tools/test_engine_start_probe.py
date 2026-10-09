# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Leandro Ostera <leandro@ostera.io>

import os
from pathlib import Path
import sys
import tempfile
import unittest
import run_engine_start_probe as probe


class EngineStartProbeTests(unittest.TestCase):
    def test_requires_host_bytecode_and_denial_markers(self):
        host = 'HOST_STARTUP_RETURNED pid=123 second_start=rejected\n'
        beam = ('BEAM_STARTUP_OK pid=123\n'
                'EXECUTABLE_PORTS_DENIED checks=7 forker_port=false\n')
        self.assertTrue(probe.host_markers(host + beam, 123))
        self.assertTrue(probe.host_markers(beam + host, 123))
        for text in (host, beam, host + beam + beam, host + beam.replace('123', '124'),
                     host + beam + 'BEAM_STARTUP_OK pid=999\n',
                     host + 'BEAM_STARTUP_OK pid=123\n'):
            self.assertFalse(probe.host_markers(text, 123))

    def test_private_control_handshake(self):
        code = '''import os
pid=os.getpid()
print(f"HOST_STARTUP_RETURNED pid={pid} second_start=rejected", flush=True)
print(f"BEAM_STARTUP_OK pid={pid}", flush=True)
print("EXECUTABLE_PORTS_DENIED checks=7 forker_port=false", flush=True)
assert os.read(int(os.environ['LIBBEAM_PROBE_CONTROL_FD']),1)==b'X'
print("HOST_NO_CHILDREN sigchld_preserved=true", flush=True)
print("HOST_CONTROL_OK engine_shutdown=false isolates_created=0 process_exit=true", flush=True)
'''
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            result = probe.run_host([sys.executable, '-c', code], root, os.environ,
                                    root / 'host.log', timeout=10)
            self.assertEqual(result['status'], 'passed')
            self.assertFalse(result['engine_shutdown'])

    def test_missing_markers_times_out(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            with self.assertRaises(TimeoutError):
                probe.run_host([sys.executable, '-c', 'import time;time.sleep(60)'],
                               root, os.environ, root / 'host.log', timeout=0.1)

    def test_early_exit_fails(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            with self.assertRaisesRegex(RuntimeError, 'before control'):
                probe.run_host([sys.executable, '-c', 'pass'], root, os.environ,
                               root / 'host.log', timeout=10)


if __name__ == '__main__':
    unittest.main()
