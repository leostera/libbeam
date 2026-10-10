# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Leandro Ostera <leandro@ostera.io>

import os
from pathlib import Path
import sys
import tempfile
import unittest
import run_engine_start_probe as probe


class EngineStartProbeTests(unittest.TestCase):
    def test_removed_symbols_must_have_neither_definitions_nor_callers(self):
        self.assertEqual(probe.removed_symbols('000 T _erts_set_signal\n U _forker_driver'),
                         {'erts_set_signal', 'forker_driver'})
        self.assertEqual(probe.removed_symbols('000 T os_set_signal_2'), {'os_set_signal_2'})
        self.assertFalse(probe.removed_symbols('archive.o:\n000 T _erl_start_embedded'))

    def test_requires_host_bytecode_and_denial_markers(self):
        host = 'HOST_STARTUP_RETURNED pid=123 second_start=rejected\n'
        beam = ('BEAM_STARTUP_OK pid=123\n'
                'EXECUTABLE_PORTS_DENIED checks=11 forker_port=false\n'
                'SIGNAL_ADMIN_REMOVED checks=6 signal_server=false\n')
        self.assertTrue(probe.host_markers(host + beam, 123))
        self.assertTrue(probe.host_markers(beam + host, 123))
        for text in (host, beam, host + beam + beam, host + beam.replace('123', '124'),
                     host + beam + 'BEAM_STARTUP_OK pid=999\n',
                     host + 'BEAM_STARTUP_OK pid=123\n'):
            self.assertFalse(probe.host_markers(text, 123))

    def test_thread_handle_marker_rejects_missing_duplicate_and_detached(self):
        marker = 'HOST_THREAD_HANDLES_OK total=7 joinable=true stopped=false joined=false\n'
        self.assertEqual(probe.thread_handle_marker(marker), 7)
        for text in ('', marker * 2, marker.replace('true', 'false'),
                     marker.replace('total=7', 'total=0')):
            self.assertIsNone(probe.thread_handle_marker(text))

    def test_private_control_handshake(self):
        code = '''import os
pid=os.getpid()
print("HOST_THREAD_HANDLES_OK total=7 joinable=true stopped=false joined=false", flush=True)
print("HOST_ENGINE_OWNER_OK live_handles_private=true uninitialized_candidate_released=true", flush=True)
print("ATOM_STORAGE_OK copied_names=true empty_binary=true", flush=True)
print("EXPORT_LITERAL_OK gc_roundtrip_and_dispatch=true", flush=True)
print("EXPORT_TABLE_OK reload_and_stub_lookup=true private_execution=false", flush=True)
print(f"HOST_STARTUP_RETURNED pid={pid} second_start=rejected", flush=True)
print(f"BEAM_STARTUP_OK pid={pid}", flush=True)
print("EXECUTABLE_PORTS_DENIED checks=11 forker_port=false", flush=True)
print("SIGNAL_ADMIN_REMOVED checks=6 signal_server=false", flush=True)
assert os.read(int(os.environ['LIBBEAM_PROBE_CONTROL_FD']),1)==b'X'
print("HOST_NO_CHILDREN sigchld_preserved=true", flush=True)
print("HOST_SIGNALS_OK dispositions=9 altstack=true mask=true usr1_delivered=true", flush=True)
print("HOST_CONTROL_OK engine_shutdown=false isolates_created=0 process_exit=true", flush=True)
'''
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            result = probe.run_host([sys.executable, '-c', code], root, os.environ,
                                    root / 'host.log', timeout=10)
            self.assertEqual(result['status'], 'passed')
            self.assertFalse(result['engine_shutdown'])
            self.assertTrue(result['explicit_engine_owner_checked'])
            owner_line = next(line for line in code.splitlines(True)
                              if line.startswith('print("HOST_ENGINE_OWNER_OK'))
            for copies in (0, 2):
                with self.subTest(owner_markers=copies):
                    with self.assertRaisesRegex(RuntimeError, 'host control witness failed'):
                        probe.run_host([sys.executable, '-c',
                                        code.replace(owner_line, owner_line * copies)],
                                       root, os.environ, root / f'owner-{copies}.log', timeout=10)

    def test_terminal_observer_accepts_preserved_state(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            for nonblocking in (False, True):
                result = probe.run_terminal_host(
                    [sys.executable, '-c', 'print("TERMINAL_VM_READY")'],
                    root, os.environ, root / 'terminal.log', nonblocking=nonblocking)
                self.assertTrue(result['stdin_flags_preserved'])
                self.assertTrue(result['stdin_termios_preserved'])

    def test_terminal_observer_detects_blocking_reset(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            with self.assertRaisesRegex(RuntimeError, 'mutated host terminal'):
                probe.run_terminal_host([sys.executable, '-c',
                    'import os; os.set_blocking(0, True); print("TERMINAL_VM_READY")'],
                    root, os.environ, root / 'terminal.log', nonblocking=True)

    def test_terminal_observer_detects_termios_mutation(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            with self.assertRaisesRegex(RuntimeError, 'mutated host terminal'):
                probe.run_terminal_host([sys.executable, '-c',
                    'import termios; a=termios.tcgetattr(0); a[3]^=termios.ECHO; '
                    'termios.tcsetattr(0, termios.TCSANOW, a); print("TERMINAL_VM_READY")'],
                    root, os.environ, root / 'terminal.log', nonblocking=False)

    def test_terminal_observer_times_out(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            with self.assertRaises(TimeoutError):
                probe.run_terminal_host([sys.executable, '-c', 'import time;time.sleep(60)'],
                    root, os.environ, root / 'terminal.log', nonblocking=True, timeout=0.1)

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
