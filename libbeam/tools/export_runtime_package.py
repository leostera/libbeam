#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Leandro Ostera <leandro@ostera.io>
"""Freeze a verified debug-interpreter probe's static link inputs for CMake.

This is not a runtime build or lifecycle acceptance. System libraries/frameworks
and the host toolchain remain external; this is not a hermetic SDK distribution.
"""
import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import tempfile

from link_archive_probe import parse_settings


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def link_inputs(settings, cwd):
    """Resolve OTP-owned archives instead of leaving mutable -L/-l references."""
    tokens = shlex.split(settings['LIBS'])
    searches, items, options = [], [], []
    i = 0
    while i < len(tokens):
        token = tokens[i]
        if token == '-L':
            i += 1
            searches.append((cwd / tokens[i]).resolve())
        elif token.startswith('-L'):
            searches.append((cwd / token[2:]).resolve())
        elif token == '-framework':
            i += 1
            options.append('SHELL:' + shlex.join(['-framework', tokens[i]]))
        else:
            items.append(token)
        i += 1
    libraries, files = [], []
    for item in items:
        if item.startswith('-l'):
            candidates = [p / ('lib' + item[2:] + '.a') for p in searches]
            found = next((p for p in candidates if p.is_file()), None)
            if found is not None:
                item = str(found.resolve())
            elif any((p / ('lib' + item[2:] + ext)).exists()
                     for p in searches for ext in ('.dylib', '.so')):
                raise ValueError('external dynamic dependency is not snapshotted: ' + item)
            elif item not in ('-lutil', '-ldl', '-lm', '-lncurses', '-lz',
                              '-lpthread', '-lc', '-lrt'):
                raise ValueError('unresolved non-system library: ' + item)
        if not item.startswith('-'):
            path = (cwd / item).resolve()
            if path.suffix != '.a' or not path.is_file():
                raise ValueError('expected static archive: ' + item)
            item = str(path)
            files.append(path)
        elif not item.startswith('-l'):
            raise ValueError('unsupported library setting: ' + item)
        libraries.append(item)
    flags = shlex.split(settings['FLAGS'])
    if flags:
        options.append('SHELL:' + shlex.join(flags))
    return libraries, options, files


def quote(value):
    # Bracket arguments prevent CMake variable expansion or command injection.
    # Semicolons are rejected because target properties are CMake lists.
    if ';' in value:
        raise ValueError('CMake list separator in value')
    equals = '='
    while ']' + equals + ']' in value:
        equals += '='
    return '[' + equals + '[' + value + ']' + equals + ']'


def export(probe, output):
    report = json.loads(probe.read_text())
    if report['status'] != 'started_and_returned_not_shutdown':
        raise ValueError('requires a passing native startup probe (not shutdown acceptance)')
    if not {'native_link_inputs', 'native_link_settings_sha256'} <= report.keys():
        raise ValueError('probe lacks native link provenance; rerun the startup probe')
    header_paths = [Path(p) for p in report['inputs'] if p.endswith('/erts/emulator/beam/erl_embed.h')]
    if len(header_paths) != 1:
        raise ValueError('missing/ambiguous native header provenance')
    header = header_paths[0]
    archive = probe.parent / 'libbeam.debug.emu.a'
    settings_path = probe.parent / 'settings.log'
    if digest(settings_path) != report['native_link_settings_sha256']:
        raise ValueError('native link settings changed after validation')
    if (output.is_relative_to(header.parents[3]) or
            output.is_relative_to(Path(__file__).resolve().parents[2])):
        raise ValueError('runtime package must be outside source trees')
    settings = parse_settings(settings_path.read_text())
    libraries, options, dependencies = link_inputs(settings, header.parent.parent)
    expected = {str(header): report['inputs'][str(header)],
                str(archive): report['archive_sha256'], **report['native_link_inputs']}
    for path in [header, archive, *dependencies]:
        if expected.get(str(path)) != digest(path):
            raise ValueError('native input changed or lacks provenance: ' + str(path))
    output.mkdir(parents=True, exist_ok=False)
    (output / 'include').mkdir()
    (output / 'lib').mkdir()
    copies = {header: output / 'include/erl_embed.h',
              archive: output / 'lib/libbeam.erts.a'}
    copies.update({p: output / 'lib' / (str(i) + '-' + p.name)
                   for i, p in enumerate(dict.fromkeys(dependencies))})
    for source, target in copies.items():
        shutil.copyfile(source, target)
        if digest(target) != expected[str(source)]:
            raise ValueError('native input changed during copy: ' + str(source))
    libraries = [str(copies[Path(x)]) if not x.startswith('-') else x for x in libraries]
    hashes = {str(p.relative_to(output)): digest(p) for p in copies.values()}
    lines = ['# Generated immutable-input import. No VM startup or shutdown is implied.']
    for relative, sha in hashes.items():
        path = output / relative
        lines += ['file(SHA256 ' + quote(str(path)) + ' actual)',
                  'if(NOT actual STREQUAL ' + quote(sha) + ')',
                  '  message(FATAL_ERROR "Native runtime package was modified")', 'endif()']
    lines += ['add_library(libbeam_erts STATIC IMPORTED GLOBAL)',
              'set_target_properties(libbeam_erts PROPERTIES',
              '  IMPORTED_LOCATION ' + quote(str(copies[archive])),
              '  INTERFACE_INCLUDE_DIRECTORIES ' + quote(str(output / 'include')), ' )']
    # Each argument is quoted separately; CMake assembles the property list.
    lines += ['target_link_libraries(libbeam_erts INTERFACE ' +
              ' '.join(map(quote, libraries)) + ')',
              'target_link_options(libbeam_erts INTERFACE ' +
              ' '.join(map(quote, options)) + ')']
    (output / 'runtime-package.cmake').write_text('\n'.join(lines) + '\n')
    (output / 'manifest.json').write_text(json.dumps({
        'status': 'native_link_package_not_lifecycle_acceptance',
        'probe': str(probe), 'probe_sha256': digest(probe),
        'generator_sha256': digest(__file__),
        'settings_sha256': digest(settings_path),
        'files': hashes, 'libraries': libraries, 'options': options,
    }, indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--probe', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    lock = Path(tempfile.gettempdir()) / f'otp-realm-validation-{os.getuid()}.lock'
    with lock.open('a') as held:
        fcntl.flock(held, fcntl.LOCK_EX | fcntl.LOCK_NB)
        export(args.probe.resolve(), args.output.resolve())
    print(args.output.resolve() / 'runtime-package.cmake')


if __name__ == '__main__':
    main()
