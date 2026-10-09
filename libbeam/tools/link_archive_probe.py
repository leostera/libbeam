#!/usr/bin/env python3
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

"""Link a C++ host to OTP's archive. Does NOT initialize an engine or isolates."""
import argparse
import fcntl
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shlex
import shutil
import signal
import sys
import tempfile

sys.dont_write_bytecode = True
from build_baseline import clean_environment, git


def parse_settings(text):
    result = {}
    for line in text.splitlines():
        key, sep, value = line.partition('=')
        if sep and key in ('ARCHIVE', 'CXX', 'FLAGS', 'LIBS'):
            if key in result:
                raise ValueError('duplicate make setting')
            result[key] = value
    if set(result) != {'ARCHIVE', 'CXX', 'FLAGS', 'LIBS'} or not result['CXX'].strip():
        raise ValueError('missing make link settings')
    return result


def has_entry_symbol(text):
    # OTP builds with hidden visibility: a linked definition can be local text
    # ('t') on Mach-O, not an exported ('T') symbol. Undefined ('U') is not enough.
    return any(line.split()[-2:] in ([kind, name] for kind in ('T', 't')
                                   for name in ('erl_start', '_erl_start'))
               for line in text.splitlines())


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--baseline', type=Path, required=True, help='passed build_baseline summary.json')
    p.add_argument('--output', type=Path, required=True, help='new directory outside source worktree')
    args = p.parse_args()
    baseline = json.loads(args.baseline.read_text())
    if baseline.get('status') != 'passed' or baseline.get('variant') != 'debug-emu':
        p.error('requires a passed debug-emu baseline')
    root = Path(baseline['source_root']).resolve()
    source, output = root / 'beam', args.output.resolve()
    if output.exists() or output.is_relative_to(root):
        p.error('output must be new and outside the source worktree')
    if git(root, 'rev-parse', 'HEAD') != baseline['revision']:
        p.error('source revision changed since baseline')
    if git(root, 'status', '--porcelain') != baseline['final_worktree']:
        p.error('source worktree status changed since baseline')
    makefiles = [m for m in (source / 'erts/emulator').glob('*/Makefile')
                 if (source / 'bin' / m.parent.name / 'libbeam.a').is_file()]
    if len(makefiles) != 1:
        p.error('expected exactly one configured Unix emulator target')
    helper = source / 'scripts/realm-validation.py'
    spec = importlib.util.spec_from_file_location('realm_validation', helper)
    runner = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(runner)
    tools = Path(__file__).resolve().parent
    cpp = tools.parent / 'examples/archive_link_probe.cpp'
    env, _ = clean_environment(source, output, os.environ)
    cwd = source / 'erts/emulator'
    make = ['make', '--no-print-directory', '-f', str(makefiles[0]), '-f',
            str(tools / 'archive_probe.mk'), 'TYPE=debug', 'FLAVOR=emu']
    report = {'kind': 'archive_link_only', 'status': 'running', 'engine_started': False,
              'isolates_created': 0, 'revision': baseline['revision'], 'steps': [],
              'baseline_sha256': hashlib.sha256(args.baseline.read_bytes()).hexdigest(),
              'driver_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest()}
    def run(name, command):
        step = {'name': name, 'command': command}
        report['steps'].append(step)
        step.update(runner.execute(command, cwd, env, output / f'{name}.log', 1800))
        runner.save(output, report)
        if step['status'] != 'passed':
            raise RuntimeError(name + ' failed')
    lock = Path(tempfile.gettempdir()) / f'otp-realm-validation-{os.getuid()}.lock'
    def interrupted(signum, frame):
        raise KeyboardInterrupt
    signal.signal(signal.SIGTERM, interrupted)
    with lock.open('a') as held:
        fcntl.flock(held, fcntl.LOCK_EX | fcntl.LOCK_NB)
        output.mkdir(parents=True)
        try:
            run('settings', make + ['libbeam-print-link-settings'])
            settings = parse_settings((output / 'settings.log').read_text())
            archive = Path(settings['ARCHIVE']).resolve()
            if archive.name != 'libbeam.a' or not archive.is_relative_to(source.resolve() / 'bin'):
                raise ValueError('refusing to replace an unexpected archive path')
            # OTP reuses libbeam.a across variants, and ar can retain old members.
            # Recreate only this generated archive using the exact debug-emu target.
            archive.unlink(missing_ok=True)
            run('archive', make + [str(archive)])
            copied = output / 'libbeam.debug.emu.a'
            shutil.copyfile(archive, copied)
            host = output / 'archive_link_probe'
            run('link', shlex.split(settings['CXX']) + ['-std=c++17', str(cpp), '-o', str(host)]
                + shlex.split(settings['FLAGS']) + [str(copied)] + shlex.split(settings['LIBS']))
            run('symbols', ['nm', str(host)])
            if not has_entry_symbol((output / 'symbols.log').read_text()):
                raise ValueError('host does not define linked erl_start')
            run('host', [str(host)])
            if (output / 'host.log').read_text().strip() != 'LIBBEAM_ARCHIVE_LINK_OK engine_started=false isolates_created=0':
                raise ValueError('unexpected host output')
            for name, path in [('archive', copied), ('host', host), ('fixture', cpp),
                               ('make_fragment', tools / 'archive_probe.mk')]:
                report[name + '_sha256'] = hashlib.sha256(path.read_bytes()).hexdigest()
            report['status'] = 'linked_not_initialized'
        except (Exception, KeyboardInterrupt) as error:
            report.update(status='failed', error=repr(error))
        finally:
            runner.save(output, report)
    print(report['status'], report.get('error', ''))
    return 0 if report['status'] == 'linked_not_initialized' else 1


if __name__ == '__main__':
    raise SystemExit(main())
