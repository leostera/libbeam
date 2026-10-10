#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Leandro Ostera <leandro@ostera.io>

"""Start the real linked OTP runtime and regain host control; NOT isolate acceptance."""
import argparse
import fcntl
import hashlib
import json
import os
import pty
import re
import termios
from pathlib import Path
import shlex
import shutil
import signal
import subprocess
import sys
import tempfile
import time

sys.dont_write_bytecode = True
from build_baseline import clean_environment, positive
from link_archive_probe import parse_settings
from export_runtime_package import link_inputs, digest
import otp_validation as runner


REMOVED_NATIVE_SYMBOLS = {
    'erts_is_embedded', 'erts_set_signal', 'erts_set_ignore_break',
    'erts_replace_intr', 'init_break_handler', 'erl_sys_late_init',
    'erts_sys_unix_later_init', 'sys_init_signal_stack', 'os_set_signal_2',
    'erl_drv_steal_main_thread', 'erl_drv_stolen_main_thread_join',
    'spawn_driver', 'forker_driver', 'spawn_driver_entry', 'forker_driver_entry',
    'sys_tty_reset', 'sys_get_key', 'do_break', 'erts_do_break_handling',
    'erts_break_requested', 'erl_sys_initial_tty_mode', 'using_oldshell',
}


def removed_symbols(text):
    return REMOVED_NATIVE_SYMBOLS & {
        line.split()[-1].removeprefix('_') for line in text.splitlines() if line.split()
    }


def host_markers(text, pid):
    host = f'HOST_STARTUP_RETURNED pid={pid} second_start=rejected'
    beam = f'BEAM_STARTUP_OK pid={pid}'
    denied = 'EXECUTABLE_PORTS_DENIED checks=11 forker_port=false'
    signal_denied = 'SIGNAL_ADMIN_REMOVED checks=6 signal_server=false'
    lines = [line for line in text.splitlines()
             if line.startswith(('HOST_STARTUP_RETURNED ', 'BEAM_STARTUP_OK ',
                                 'EXECUTABLE_PORTS_DENIED ', 'SIGNAL_ADMIN_REMOVED '))]
    return sorted(lines) == sorted([host, beam, denied, signal_denied])


def thread_handle_marker(text):
    lines = [line for line in text.splitlines() if line.startswith('HOST_THREAD_HANDLES_OK ')]
    if len(lines) != 1:
        return None
    match = re.fullmatch(r'HOST_THREAD_HANDLES_OK total=(\d+) joinable=true stopped=false joined=false', lines[0])
    if not match or int(match[1]) < 6:
        return None
    return int(match[1])


def run_host(command, cwd, env, log, timeout=60):
    """Observe both same-PID witnesses before releasing private host control FD."""
    read_fd, write_fd = os.pipe()
    child_env = dict(env, LIBBEAM_PROBE_CONTROL_FD=str(read_fd))
    process = None
    start = time.monotonic()
    try:
        with log.open('wb') as output:
            process = subprocess.Popen(command, cwd=cwd, env=child_env,
                                       stdin=subprocess.DEVNULL, stdout=output,
                                       stderr=subprocess.STDOUT, pass_fds=(read_fd,),
                                       start_new_session=True)
            os.close(read_fd)
            read_fd = -1
            while True:
                if log.stat().st_size > 1024 * 1024:
                    raise RuntimeError('host output limit exceeded')
                text = log.read_text(errors='replace')
                if process.poll() is not None:
                    raise RuntimeError('host exited before control acknowledgement')
                if host_markers(text, process.pid):
                    os.write(write_fd, b'X')
                    break
                if time.monotonic() - start >= timeout:
                    raise TimeoutError('host/bytecode startup deadline exceeded')
                time.sleep(0.02)
            process.wait(timeout=max(0.1, timeout - (time.monotonic() - start)))
            text = log.read_text(errors='replace')
            terminal = 'HOST_CONTROL_OK engine_shutdown=false isolates_created=0 process_exit=true'
            if (process.returncode != 0 or text.splitlines().count(terminal) != 1
                    or text.splitlines().count('HOST_NO_CHILDREN sigchld_preserved=true') != 1
                    or text.splitlines().count('HOST_SIGNALS_OK dispositions=9 altstack=true mask=true usr1_delivered=true') != 1
                    or not host_markers(text, process.pid)
                    or text.splitlines().count('ATOM_STORAGE_OK copied_names=true empty_binary=true') != 1
                    or text.splitlines().count('EXPORT_TABLE_OK reload_and_stub_lookup=true private_execution=false') != 1
                    or text.splitlines().count('EXPORT_LITERAL_OK gc_roundtrip_and_dispatch=true') != 1
                    or thread_handle_marker(text) is None
                    or text.splitlines().count('HOST_ENGINE_OWNER_OK live_handles_private=true uninitialized_candidate_released=true') != 1):
                raise RuntimeError('host control witness failed')
            return {'status': 'passed', 'pid': process.pid, 'returncode': process.returncode,
                    'seconds': round(time.monotonic() - start, 3),
                    'same_pid_bytecode': True, 'second_start': 'rejected',
                    'engine_shutdown': False, 'isolates_created': 0,
                    'scheduler_thread_handles': thread_handle_marker(text),
                    'scheduler_threads_created_joinable': True,
                    'explicit_engine_owner_checked': True,
                    'scheduler_threads_stopped_or_joined': False,
                    'executable_port_denials': 11, 'forker_port': False,
                    'no_children_at_ack': True, 'sigchld_preserved': True,
                    'removed_signal_api_checks': 6, 'preserved_signal_dispositions': 9,
                    'host_altstack_and_mask_preserved': True, 'usr1_delivered': True}
    except BaseException:
        if process is not None:
            runner.stop_group(process)
        raise
    finally:
        if read_fd >= 0:
            os.close(read_fd)
        os.close(write_fd)


def run_terminal_host(command, cwd, env, log, *, nonblocking, exit_code=0, timeout=30):
    """Observe the host-owned PTY through its shared open file description.

    Only stdin is a terminal; stdout/stderr stay in the bounded evidence log.
    Process exit is intentional here, not evidence of engine destruction.
    """
    master, slave = pty.openpty()
    process = None
    start = time.monotonic()
    try:
        os.set_blocking(slave, not nonblocking)
        attrs = termios.tcgetattr(slave)
        attrs[3] &= ~(termios.ECHO | termios.ICANON)
        termios.tcsetattr(slave, termios.TCSANOW, attrs)
        before = termios.tcgetattr(slave)
        flags = fcntl.fcntl(slave, fcntl.F_GETFL)
        with log.open('wb') as output:
            process = subprocess.Popen(command, cwd=cwd, env=env, stdin=slave,
                                       stdout=output, stderr=subprocess.STDOUT,
                                       start_new_session=True)
            while process.poll() is None:
                if log.stat().st_size > 1024 * 1024:
                    raise RuntimeError('terminal host output limit exceeded')
                if time.monotonic() - start > timeout:
                    raise TimeoutError('terminal host deadline exceeded')
                time.sleep(0.02)
        if log.stat().st_size > 1024 * 1024:
            raise RuntimeError('terminal host output limit exceeded')
        if (process.returncode != exit_code or
                log.read_text(errors='replace').splitlines().count('TERMINAL_VM_READY') != 1):
            raise RuntimeError('terminal bytecode/exit witness failed')
        if (fcntl.fcntl(slave, fcntl.F_GETFL) != flags or
                termios.tcgetattr(slave) != before):
            raise RuntimeError('runtime mutated host terminal state')
        return {'status': 'passed', 'returncode': process.returncode,
                'stdin_flags_preserved': True, 'stdin_termios_preserved': True,
                'nonblocking': nonblocking, 'engine_shutdown': False}
    except BaseException:
        if process is not None:
            runner.stop_group(process)
        raise
    finally:
        os.close(slave)
        os.close(master)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--root', type=Path, required=True, help='configured OTP source (beam/)')
    p.add_argument('--output', type=Path, required=True, help='new external result directory')
    p.add_argument('--jobs', type=positive, default=4)
    p.add_argument('--iterations', type=positive, default=3)
    args = p.parse_args()
    source, output = args.root.resolve(), args.output.resolve()
    if output.exists() or output.is_relative_to(source):
        p.error('output must be new and outside the OTP source')
    if not (source / 'Makefile').exists() or not (source / 'erts/emulator/beam/erl_embed.h').exists():
        p.error('requires configured sources with experimental returning startup')
    tools = Path(__file__).resolve().parent
    fixtures = tools.parent / 'tests/fixtures'
    cpp = tools.parent / 'examples/engine_start_probe.cpp'
    env, removed = clean_environment(source, output, os.environ)
    report = {'kind': 'experimental_process_lifetime_engine_start', 'status': 'running',
              'engine_shutdown': 'not_implemented', 'isolate_acceptance': 'not_implemented',
              'revision': runner.git(source, 'rev-parse', 'HEAD').decode().strip(),
              'worktree': runner.git(source, 'status', '--porcelain').decode(),
              'source_diff_sha256': hashlib.sha256(runner.git(source, 'diff', 'HEAD', '--binary')).hexdigest(),
              'removed_environment_keys': removed, 'steps': [], 'inputs': {}}
    for path in [Path(__file__), tools / 'otp_validation.py', tools / 'build_baseline.py',
                 tools / 'link_archive_probe.py', tools / 'export_runtime_package.py',
                 tools / 'archive_probe.mk', cpp,
                 fixtures / 'startup_probe.erl', fixtures / 'engine_start_probe.erl',
                 fixtures / 'export_namespace_probe.erl',
                 source / 'erts/emulator/beam/erl_embed.h',
                 source / 'erts/emulator/beam/erl_engine.h',
                 source / 'erts/emulator/beam/erl_engine.c',
                 source / 'erts/emulator/beam/export.c',
                 source / 'erts/emulator/beam/export.h',
                 source / 'erts/emulator/beam/erl_export_namespace.h',
                 tools.parent / 'tests/native_export_namespace_test.c',
                 source / 'erts/emulator/beam/erl_export_literals.c',
                 source / 'erts/emulator/beam/erl_export_literals.h',
                 tools.parent / 'tests/native_export_literals_test.c',
                 tools.parent / 'tests/native_module_table_roots_test.c',
                 tools.parent / 'tests/native_atom_namespace_test.c',
                 source / 'erts/emulator/beam/atom.c',
                 source / 'erts/emulator/beam/atom.h',
                 source / 'erts/emulator/beam/erl_unicode.c',
                 source / 'erts/emulator/beam/erl_bif_binary.c',
                 source / 'erts/emulator/beam/erl_global_literals.c',
                 source / 'erts/emulator/beam/erl_global_literals.h',
                 source / 'erts/emulator/beam/erl_lock_check.c',
                 source / 'erts/emulator/beam/erl_atom_namespace.h',
                 source / 'erts/emulator/beam/erl_isolate_state.c',
                 source / 'erts/emulator/beam/erl_isolate_state.h',
                 source / 'erts/emulator/beam/erl_module_table.h',
                 source / 'erts/emulator/beam/module.c',
                 source / 'erts/emulator/beam/module.h',
                 source / 'erts/emulator/beam/index.c',
                 source / 'erts/emulator/beam/index.h',
                 source / 'erts/emulator/beam/hash.c',
                 source / 'erts/emulator/beam/hash.h',
                 source / 'erts/emulator/beam/erl_init.c', source / 'erts/emulator/beam/sys.h',
                 source / 'erts/emulator/beam/erl_bif_port.c',
                 source / 'erts/emulator/beam/erl_bif_os.c',
                 source / 'erts/emulator/beam/erl_drv_thread.c',
                 source / 'erts/emulator/sys/unix/sys_float.c',
                 source / 'erts/emulator/beam/erl_bif_info.c',
                 source / 'erts/emulator/beam/bif.tab',
                 source / 'erts/emulator/beam/io.c',
                 source / 'erts/emulator/beam/break.c',
                 source / 'erts/emulator/nifs/common/prim_tty_nif.c',
                 source / 'erts/emulator/beam/erl_process.c',
                 source / 'erts/emulator/beam/erl_process.h',
                 source / 'erts/emulator/beam/erl_alloc.types',
                 source / 'erts/emulator/beam/erl_async.c',
                 source / 'erts/emulator/beam/erl_async.h',
                 source / 'erts/emulator/beam/erl_trace.c',
                 source / 'erts/emulator/beam/erl_trace.h',
                 source / 'erts/emulator/beam/global.h',
                 source / 'erts/emulator/beam/erl_driver.h',
                 source / 'erts/emulator/Makefile.in',
                 source / 'erts/emulator/sys/unix/sys_signal_stack.c',
                 source / 'lib/kernel/src/kernel.erl',
                 source / 'lib/kernel/src/os.erl',
                 source / 'erts/emulator/sys/unix/sys_drivers.c',
                 source / 'erts/emulator/sys/unix/sys.c']:
        report['inputs'][str(path)] = hashlib.sha256(path.read_bytes()).hexdigest()

    def run(name, command, cwd=source):
        step = {'name': name, 'command': command}
        report['steps'].append(step)
        step.update(runner.execute(command, cwd, env, output / f'{name}.log', 1800))
        runner.save(output, report)
        if step['status'] != 'passed':
            raise RuntimeError(name + ' failed')

    def interrupted(signum, frame):
        raise KeyboardInterrupt
    signal.signal(signal.SIGTERM, interrupted)
    lock = Path(tempfile.gettempdir()) / f'otp-realm-validation-{os.getuid()}.lock'
    with lock.open('a') as held:
        fcntl.flock(held, fcntl.LOCK_EX | fcntl.LOCK_NB)
        output.mkdir(parents=True)
        runner.save(output, report)
        try:
            # Update these interdependent bootstrap modules in one compiler VM.
            # The existing configured compiler is a build tool, not the runtime
            # used by the native-host witness below.
            compiler_vm = next((source / 'bin').glob('*/beam.smp'))
            report['compiler_vm_sha256'] = hashlib.sha256(compiler_vm.read_bytes()).hexdigest()
            kernel = source / 'lib/kernel'
            run('compile-runtime-bootstrap', [str(source / 'bin/erlc'), '-I',
                str(kernel / 'include'), '-o', str(kernel / 'ebin'),
                str(kernel / 'src/kernel.erl'), str(kernel / 'src/os.erl')])
            run('build', ['make', f'-j{args.jobs}', 'TYPE=debug', 'FLAVOR=emu'])
            report['bootstrap_beam_sha256'] = {
                name: hashlib.sha256((kernel / 'ebin' / name).read_bytes()).hexdigest()
                for name in ('kernel.beam', 'os.beam')
            }
            makefiles = [m for m in (source / 'erts/emulator').glob('*/Makefile')
                         if (source / 'bin' / m.parent.name / 'libbeam.a').exists()]
            if len(makefiles) != 1:
                raise RuntimeError('expected one configured Unix emulator target')
            make = ['make', '--no-print-directory', '-f', str(makefiles[0]), '-f',
                    str(tools / 'archive_probe.mk'), 'TYPE=debug', 'FLAVOR=emu']
            cwd = source / 'erts/emulator'
            run('settings', make + ['libbeam-print-link-settings'], cwd)
            report['native_link_settings_sha256'] = digest(output / 'settings.log')
            settings = parse_settings((output / 'settings.log').read_text())
            _, _, dependencies = link_inputs(settings, cwd)
            report['native_link_inputs'] = {str(p): digest(p) for p in dependencies}
            archive = Path(settings['ARCHIVE']).resolve()
            if archive.name != 'libbeam.a' or not archive.is_relative_to(source / 'bin'):
                raise RuntimeError('unexpected generated archive path')
            env.pop('BINDIR', None)
            report['native_forker'] = 'implementation_deleted'
            report['standalone_signal_dispatcher'] = 'implementation_deleted'
            archive.unlink(missing_ok=True)
            run('archive', make + [str(archive)], cwd)
            copied = output / 'libbeam.debug.emu.a'
            shutil.copyfile(archive, copied)
            run('removed-symbols', ['nm', '-g', str(copied)])
            remaining = removed_symbols((output / 'removed-symbols.log').read_text())
            if remaining:
                raise RuntimeError(f'removed native symbols still present: {sorted(remaining)}')
            report['removed_native_symbols_absent'] = True
            host = output / 'engine_start_probe'
            run('link', shlex.split(settings['CXX']) + ['-std=c++17', str(cpp),
                '-I' + str(source / 'erts/emulator/beam'),
                '-I' + str(source / 'erts/include'),
                '-I' + str(source / 'erts/include' / archive.parent.name), '-o', str(host)] +
                shlex.split(settings['FLAGS']) + [str(copied)] + shlex.split(settings['LIBS']), cwd)
            run('compile-settings', make + ['libbeam-print-compile-settings'], cwd)
            compile_settings = dict(line.split('=', 1) for line in
                (output / 'compile-settings.log').read_text().splitlines()
                if line.startswith(('CC=', 'CFLAGS=', 'INCLUDES=')))
            components = [
                ('export-namespace', 'native_export_namespace_test.c',
                 'NATIVE_EXPORT_NAMESPACE_OK same_mfa_independent=true scoped_staging=true guarded_disposal=true private_execution=false'),
                ('export-literals', 'native_export_literals_test.c',
                 'NATIVE_EXPORT_LITERALS_OK independent_areas=true peer_survives=true bound_retained=true private_execution=false'),
                ('module-roots', 'native_module_table_roots_test.c',
                 'NATIVE_MODULE_ROOT_GUARDS_OK synthetic_markers=12 loaded_beam=0 isolates=0'),
                ('atom-namespace', 'native_atom_namespace_test.c',
                 'NATIVE_ATOM_NAMESPACE_OK local_indices=true owned_names=true peer_survives=true fresh_state=true global_atoms_unchanged=true loaded_beam=0')]
            for name, fixture, marker in components:
                obj = output / (name + '.o')
                executable = output / name
                run('compile-' + name, shlex.split(compile_settings['CC']) +
                    shlex.split(compile_settings['CFLAGS']) + shlex.split(compile_settings['INCLUDES']) +
                    ['-c', str(tools.parent / 'tests' / fixture), '-o', str(obj)], cwd)
                run('link-' + name, shlex.split(settings['CXX']) + [str(obj),
                    '-o', str(executable)] + shlex.split(settings['FLAGS']) + [str(copied)] +
                    shlex.split(settings['LIBS']), cwd)
                run(name, [str(executable), '-S', '2:2', '-SDcpu', '1:1',
                    '-SDio', '1', '-A', '0', '--', '-root', '/libbeam-missing-root',
                    '--', '-boot', '/libbeam-missing-boot'])
                if (output / (name + '.log')).read_text().splitlines().count(marker) != 1:
                    raise RuntimeError(name + ' witness missing')
            report['unpublished_module_root_guard_checks'] = 12
            report['private_atom_namespace_state_checked'] = True
            run('compile-fixtures', [str(source / 'bin/erlc'), '-o', str(output),
                str(fixtures / 'startup_probe.erl'), str(fixtures / 'engine_start_probe.erl'),
                str(fixtures / 'export_namespace_probe.erl')])
            (output / 'export-v2').mkdir()
            run('compile-export-v2', [str(source / 'bin/erlc'), '-DVERSION=2',
                '-o', str(output / 'export-v2'), str(fixtures / 'export_namespace_probe.erl')])
            command = [str(host), '-S', '2:2', '-SDcpu', '1:1', '-SDio', '1', '--',
                       '-root', str(source), '-bindir', str(archive.parent), '-progname', 'libbeam-probe',
                       '--', '-home', str(output), '--', '-noshell', '-noinput', '-pa', str(output),
                       '-s', 'engine_start_probe', 'run']
            canary_dir = output / 'forker-canary'
            canary_dir.mkdir()
            forker_sentinel = output / 'forker-executed'
            canary = canary_dir / 'erl_child_setup'
            canary.write_text('#!/bin/sh\nprintf FORKER_STARTED > ' +
                              shlex.quote(str(forker_sentinel)) + '\nexit 99\n')
            canary.chmod(0o700)
            for i in range(args.iterations):
                host_env = dict(env)
                bindir = (None, str(output / 'missing-bindir'), str(canary_dir))[i % 3]
                if bindir is not None:
                    host_env['BINDIR'] = bindir
                sentinel = output / f'exec-{i}.sentinel'
                host_env['LIBBEAM_PROBE_EXEC_SENTINEL'] = str(sentinel)
                step = {'name': f'host-{i}', 'command': command, 'status': 'running',
                        'bindir': bindir}
                report['steps'].append(step)
                step.update(run_host(command, output, host_env, output / f'host-{i}.log'))
                if sentinel.exists() or forker_sentinel.exists():
                    step['status'] = 'failed'
                    raise RuntimeError('unsupported executable or forker ran')
                runner.save(output, report)
            terminal_vm = archive.parent / 'beam.debug.emu'
            report['terminal_vm_sha256'] = hashlib.sha256(terminal_vm.read_bytes()).hexdigest()
            for nonblocking in (False, True):
                for exit_code in (0, 17):
                    name = f'terminal-{int(nonblocking)}-{exit_code}'
                    terminal_command = [str(terminal_vm)] + command[1:command.index('-s')] + [
                        '-eval', f'io:format("TERMINAL_VM_READY~n"), halt({exit_code}).']
                    step = {'name': name, 'command': terminal_command, 'status': 'running'}
                    report['steps'].append(step)
                    step.update(run_terminal_host(terminal_command, output, env,
                        output / f'{name}.log', nonblocking=nonblocking, exit_code=exit_code))
                    runner.save(output, report)
            for path, sha in report['native_link_inputs'].items():
                if digest(path) != sha:
                    raise RuntimeError('native link input changed during validation: ' + path)
            report['host_sha256'] = hashlib.sha256(host.read_bytes()).hexdigest()
            report['archive_sha256'] = hashlib.sha256(copied.read_bytes()).hexdigest()
            report['status'] = 'started_and_returned_not_shutdown'
        except (Exception, KeyboardInterrupt) as error:
            if report['steps'] and report['steps'][-1].get('status') == 'running':
                report['steps'][-1].update(status='failed', error=repr(error))
            report.update(status='failed', error=repr(error))
        finally:
            runner.save(output, report)
    print(report['status'], report.get('error', ''), flush=True)
    return 0 if report['status'] == 'started_and_returned_not_shutdown' else 1


if __name__ == '__main__':
    raise SystemExit(main())
