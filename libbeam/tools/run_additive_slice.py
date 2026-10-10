#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Leandro Ostera <leandro@ostera.io>
"""Fresh, serialized C-core image validation. Reference OTP is never our executor."""
import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--erl', default='erl')
    parser.add_argument('--erlc', default='erlc')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    summary = {'scope': 'owned_beam_image_not_execution_or_engine_lifecycle', 'commands': [], 'passed': False}
    env = dict(os.environ, ERL_FLAGS='+S 1:1 +SDcpu 1:1 +SDio 1 +A 0')

    def run(name, command, accepted=(0,)):
        try:
            result = subprocess.run(list(map(str, command)), cwd=root, env=env,
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=180)
        except subprocess.TimeoutExpired as error:
            (out / f'{name}.log').write_bytes(error.stdout or b'')
            summary['commands'].append({'name': name, 'argv': list(map(str, command)), 'timeout': True})
            raise
        (out / f'{name}.log').write_bytes(result.stdout)
        summary['commands'].append({'name': name, 'argv': list(map(str, command)), 'returncode': result.returncode})
        if accepted is not None and result.returncode not in accepted:
            raise RuntimeError(f'{name} failed; see {out}/{name}.log')
        return result.stdout.decode()

    try:
        with (Path(tempfile.gettempdir()) / f'otp-realm-validation-{os.getuid()}.lock').open('a') as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            files = sorted(set((root / 'libbeam/core').glob('*.[ch]')) |
                           {root / 'libbeam/CMakeLists.txt', root / 'libbeam/src/engine.cpp',
                            root / 'libbeam/tests/core_beam_image_test.c', root / 'libbeam/tests/core_alloc_test.c',
                            root / 'libbeam/tests/fixtures/first_slice.erl',
                            root / 'libbeam/tests/api_scaffold_test.cpp',
                            root / 'libbeam/include/libbeam/engine.hpp',
                            root / 'libbeam/examples/engine_lifecycle.cpp',
                            root / 'libbeam/examples/two_isolates.cpp', Path(__file__).resolve()})
            summary['source_hashes'] = {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest() for p in files}
            summary['reference_inputs'] = {}
            for filename, expected in {
                'beam/erts/emulator/beam/beam_file.c': '52708b4a8fa136d005d962599914b095e01b7fc311c1748309d3682d4ff56d0d',
                'beam/erts/emulator/beam/beam_file.h': '82a4760ac4c3b69d385a26c4c004103ca996960621d5f9bb4f62529e3b4f3718',
                'beam/erts/emulator/beam/erl_vm.h': '5fc1308a52c46629307cbe0fa085ca561ba1244b0252de1a59b1a35c53d8d02b',
                'beam/lib/compiler/src/genop.tab': '0f999b3797672f28bfbd4007245a95637bd38b68ac67dcfcb95eacce29e0780e',
            }.items():
                path = root / filename
                digest = hashlib.sha256(path.read_bytes()).hexdigest()
                if digest != expected:
                    raise RuntimeError(f'Provenance changed: {path}; review the transplant record')
                summary['reference_inputs'][str(path.relative_to(root))] = digest
            for tool in (args.erl, args.erlc):
                if not shutil.which(tool):
                    raise RuntimeError(f'Required reference tool missing: {tool}')
            run('compiler-version', ['cc', '--version'])
            run('configure', ['cmake', '-S', root / 'libbeam', '-B', out / 'build',
                             '-DBUILD_TESTING=ON', '-DCMAKE_BUILD_TYPE=Debug',
                             '-DCMAKE_C_FLAGS=-Wall -Wextra -Werror'])
            run('build', ['cmake', '--build', out / 'build'])
            run('ctest', ['ctest', '--test-dir', out / 'build', '--output-on-failure'])
            symbols = run('core-symbols', ['nm', '-u', out / 'build/liblibbeam_core.a'])
            if any(name in symbols for name in ('erts_', 'ethr_', 'pthread_')):
                raise RuntimeError('Unexpected legacy runtime/thread dependency in C core')
            fixture_dir = out / 'fixture'
            fixture_dir.mkdir()
            run('erlc', [args.erlc, '-o', fixture_dir, root / 'libbeam/tests/fixtures/first_slice.erl'])
            fixture = fixture_dir / 'first_slice.beam'
            native = run('native-fixture', [out / 'build/core_beam_image_test', fixture])
            # Pass paths as plain arguments, never interpolate paths into Erlang code.
            reference_code = '''
                [Path] = init:get_plain_arguments(),
                {ok,{first_slice,Chunks}} = beam_lib:chunks(Path,[atoms,imports,exports]),
                Hex = fun(A) -> binary:encode_hex(atom_to_binary(A,utf8)) end,
                lists:foreach(fun({I,A}) -> io:format("ATOM ~B ~s~n",[I,Hex(A)]) end,proplists:get_value(atoms,Chunks)),
                lists:foreach(fun({M,F,A}) -> io:format("IMPORT_NAME ~s ~s ~B~n",[Hex(M),Hex(F),A]) end,proplists:get_value(imports,Chunks)),
                lists:foreach(fun({F,A}) -> io:format("EXPORT_NAME ~s ~B~n",[Hex(F),A]) end,proplists:get_value(exports,Chunks)),
                42 = first_slice:value(), ok = first_slice:identity(ok), {ok,42} = first_slice:pair(ok),
                io:format("REFERENCE_OTP ~s ~s~n",[erlang:system_info(otp_release),erlang:system_info(version)]),
                io:format("REFERENCE_SEMANTICS_OK not_libbeam_execution=true~n"), halt(0).
            '''
            reference = run('otp-reference', [args.erl, '-noshell', '-noinput', '-pa', fixture_dir,
                                               '-eval', reference_code, '-extra', fixture])
            atoms = {int(row.split()[1]): row.split()[2] for row in native.splitlines() if row.startswith('ATOM ')}
            native_rows = []
            for row in native.splitlines():
                words = row.split()
                if row.startswith('ATOM '): native_rows.append(row)
                elif row.startswith('IMPORT '):
                    native_rows.append(f'IMPORT_NAME {atoms[int(words[1])]} {atoms[int(words[2])]} {words[3]}')
                elif row.startswith('EXPORT '):
                    native_rows.append(f'EXPORT_NAME {atoms[int(words[1])]} {words[2]}')
            reference_rows = [r for r in reference.splitlines() if r.startswith(('ATOM ', 'IMPORT_NAME ', 'EXPORT_NAME '))]
            summary['reference_runtime'] = next(r for r in reference.splitlines() if r.startswith('REFERENCE_OTP '))
            if sorted(native_rows) != sorted(reference_rows):
                raise RuntimeError('Core metadata differs from reference OTP; see fixture/reference logs')
            data = fixture.read_bytes()
            summary['fixture_sha256'] = hashlib.sha256(data).hexdigest()
            pos = 12
            while pos < len(data):
                tag, size = struct.unpack_from('>4sI', data, pos)
                if tag == b'Code':
                    head, version, opcode, labels, functions = struct.unpack_from('>5I', data, pos + 8)
                    expected = f'CODE {opcode} {labels} {functions} {size - 4 - head}'
                    if version != 0 or expected not in native.splitlines():
                        raise RuntimeError('Code header mismatch')
                pos += 8 + ((size + 3) & ~3)
            run('ubsan-compile', ['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-fsanitize=undefined',
                                  '-fno-sanitize-recover=all', '-Ilibbeam/core', 'libbeam/core/alloc.c',
                                  'libbeam/core/beam_image.c', 'libbeam/tests/core_beam_image_test.c',
                                  '-o', out / 'image-ubsan'])
            run('ubsan-fixture', [out / 'image-ubsan', fixture])
            run('release-configure', ['cmake', '-S', root / 'libbeam', '-B', out / 'release',
                                     '-DBUILD_TESTING=ON', '-DCMAKE_BUILD_TYPE=Release'])
            run('release-build', ['cmake', '--build', out / 'release'])
            run('release-ctest', ['ctest', '--test-dir', out / 'release', '--output-on-failure'])
            run('release-fixture', [out / 'release/core_beam_image_test', fixture])
            # Observe the real acceptance target; its failure is not converted
            # into a passing test or a requirement that future Engines stay red.
            run('engine-lifecycle-observed', [out / 'build/engine_lifecycle'], accepted=None)
            summary['engine_lifecycle_returncode'] = summary['commands'][-1]['returncode']
            summary['engine_lifecycle_passed'] = summary['engine_lifecycle_returncode'] == 0
            for p in files:
                if hashlib.sha256(p.read_bytes()).hexdigest() != summary['source_hashes'][str(p.relative_to(root))]:
                    raise RuntimeError(f'Source changed during validation: {p}')
            summary['metadata_matches_reference'] = True
            summary['passed'] = True
    except Exception as error:
        summary['error'] = repr(error)
        raise
    finally:
        (out / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    print(out / 'summary.json')


if __name__ == '__main__':
    main()
