#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Leandro Ostera <leandro@ostera.io>
"""Fresh, serialized C-core loader-component validation. Reference OTP is not our executor."""
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
    summary = {'scope': 'owned_generic_program_literals_not_emitted_code_or_engine_lifecycle', 'commands': [], 'passed': False}
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
                           {p for p in (root / 'libbeam/core/otp/opcodes').rglob('*') if p.is_file()} |
                           {p for p in (root / 'libbeam/tests/fixtures/two_isolates').rglob('*') if p.is_file()} |
                           {root / 'libbeam/CMakeLists.txt', root / 'libbeam/src/engine.cpp',
                            root / 'libbeam/tests/core_beam_image_test.c', root / 'libbeam/tests/core_alloc_test.c',
                            root / 'libbeam/tests/core_terms_atoms_test.c', root / 'libbeam/tools/generate_atoms.py',
                            root / 'libbeam/tools/check_term_representation.py',
                            root / 'libbeam/tests/core_beam_program_test.c', root / 'libbeam/tools/generate_opcodes.py',
                            root / 'libbeam/tools/otp/beam_makeops', root / 'libbeam/core/otp/loader-sources.json',
                            root / 'libbeam/tools/otp/make_tables', root / 'libbeam/core/otp/atom.names', root / 'libbeam/core/otp/bif.tab',
                            root / 'libbeam/tests/fixtures/first_slice.erl',
                            root / 'libbeam/tests/api_scaffold_test.cpp',
                            root / 'libbeam/include/libbeam/engine.hpp',
                            root / 'libbeam/examples/engine_lifecycle.cpp',
                            root / 'libbeam/examples/two_isolates.cpp', Path(__file__).resolve()})
            summary['source_hashes'] = {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest() for p in files}
            summary['reference_inputs'] = {}
            opcode_manifest = json.loads((root / 'libbeam/core/otp/opcodes/manifest.json').read_text())
            for entry in opcode_manifest['inputs']:
                if hashlib.sha256((root / entry['source']).read_bytes()).hexdigest() != entry['sha256']:
                    raise RuntimeError(f"Opcode reference changed: {entry['source']}")
            summary['opcode_inputs'] = opcode_manifest
            loader_sources = json.loads((root / 'libbeam/core/otp/loader-sources.json').read_text())
            for filename, expected in loader_sources['sources'].items():
                if hashlib.sha256((root / filename).read_bytes()).hexdigest() != expected:
                    raise RuntimeError(f'Loader provenance changed: {filename}')
            summary['loader_sources'] = loader_sources
            for filename, expected in {
                'beam/erts/emulator/beam/beam_file.c': '52708b4a8fa136d005d962599914b095e01b7fc311c1748309d3682d4ff56d0d',
                'beam/erts/emulator/beam/beam_file.h': '82a4760ac4c3b69d385a26c4c004103ca996960621d5f9bb4f62529e3b4f3718',
                'beam/erts/emulator/beam/erl_term.h': '8e016126fb7a1bc1db8d31111601c0bea21d72207211bf91e1f3c127e943b5c2',
                'beam/erts/emulator/beam/atom.c': '7d73c9f537444aa725c4dad64bce65460a7282fb0d3de4d2917f9f9cabd5a6d5',
                'beam/erts/emulator/beam/hash.c': '8a90d388e20ad89f981b9a38731459be3ea82efaf92f3347e9af702f94d5e40c',
                'beam/erts/emulator/beam/hash.h': '9d4523310ac9362320b463cc48cefe28e6f5a46acdacd80229352a8252afbe22',
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
            run('term-representation', ['python3', '-B', root / 'libbeam/tools/check_term_representation.py',
                                        '--output', out / 'term-oracle'])
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
            run('native-atom-binding', [out / 'build/core_terms_atoms_test', fixture])
            program = run('native-program', [out / 'build/core_beam_program_test', fixture])
            disassemble = '''
                [Path] = init:get_plain_arguments(),
                {beam_file,_,_,_,_,Fs} = beam_disasm:file(Path),
                lists:foreach(fun({function,F,A,_,_}) ->
                    io:format("FUNCTION ~ts/~B~n",[atom_to_binary(F,utf8),A]) end,Fs), halt(0).
            '''
            def compare_functions(label, path, native):
                reference = run(label+'-disassembly', [args.erl, '-noshell', '-noinput', '-eval', disassemble, '-extra', path])
                rows = lambda text: sorted(r for r in text.splitlines() if r.startswith('FUNCTION '))
                if rows(native) != rows(reference):
                    raise RuntimeError(f'{label}: decoded function boundaries differ from OTP disassembly')
            compare_functions('first', fixture, program)
            probes = []
            for variant in ('a', 'b'):
                probe_dir = out / variant
                probe_dir.mkdir()
                run(variant+'-erlc', [args.erlc, '-o', probe_dir,
                     root / f'libbeam/tests/fixtures/two_isolates/{variant}/probe.erl'])
                probe = probe_dir / 'probe.beam'
                probes.append(probe)
                native = run(variant+'-program', [out / 'build/core_beam_program_test', probe])
                compare_functions(variant, probe, native)
            # Restore the image dump used by the independent metadata check below.
            native = (out / 'native-fixture.log').read_text()
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
                                  'libbeam/core/utf8.c', 'libbeam/core/beam_image.c', 'libbeam/tests/core_beam_image_test.c',
                                  '-o', out / 'image-ubsan'])
            run('ubsan-fixture', [out / 'image-ubsan', fixture])
            run('terms-ubsan-compile', ['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                                      '-fsanitize=undefined', '-fno-sanitize-recover=all', '-Ilibbeam/core',
                                      '-I' + str(out / 'build/generated/atoms'), 'libbeam/core/alloc.c',
                                      'libbeam/core/utf8.c', 'libbeam/core/beam_image.c', 'libbeam/core/atoms.c',
                                      'libbeam/tests/core_terms_atoms_test.c', '-o', out / 'terms-ubsan'])
            run('terms-ubsan-fixture', [out / 'terms-ubsan', fixture])
            program_sources = ['alloc', 'utf8', 'atoms', 'beam_image', 'opcodes', 'beam_reader',
                               'beam_program', 'beam_decode', 'beam_metadata', 'beam_literals', 'beam_select']
            run('program-ubsan-compile', ['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                '-fsanitize=undefined', '-fno-sanitize-recover=all', '-Ilibbeam/core',
                '-I'+str(out / 'build/generated/atoms'), '-I'+str(out / 'build/generated/opcodes'),
                *[f'libbeam/core/{s}.c' for s in program_sources], 'libbeam/tests/core_beam_program_test.c',
                '-lz', '-o', out / 'program-ubsan'])
            for i, image in enumerate([fixture, *probes]):
                run(f'program-ubsan-{i}', [out / 'program-ubsan', image])
            run('release-configure', ['cmake', '-S', root / 'libbeam', '-B', out / 'release',
                                     '-DBUILD_TESTING=ON', '-DCMAKE_BUILD_TYPE=Release'])
            run('release-build', ['cmake', '--build', out / 'release'])
            run('release-ctest', ['ctest', '--test-dir', out / 'release', '--output-on-failure'])
            run('release-fixture', [out / 'release/core_beam_image_test', fixture])
            run('release-atom-binding', [out / 'release/core_terms_atoms_test', fixture])
            for filename in ('lb_atoms_generated.h', 'lb_atoms_generated.inc'):
                first = (out / 'build/generated/atoms' / filename).read_bytes()
                second = (out / 'release/generated/atoms' / filename).read_bytes()
                if first != second:
                    raise RuntimeError(f'Atom generation is not reproducible: {filename}')
            summary['atom_generation_reproducible'] = True
            for i, image in enumerate([fixture, *probes]):
                run(f'release-program-{i}', [out / 'release/core_beam_program_test', image])
            first_outputs = json.loads((out / 'build/generated/opcodes/outputs.json').read_text())
            second_outputs = json.loads((out / 'release/generated/opcodes/outputs.json').read_text())
            if first_outputs != second_outputs:
                raise RuntimeError('Full OTP opcode generation is not reproducible across builds')
            summary['opcode_generation_outputs'] = first_outputs
            summary['opcode_generation_reproducible'] = True
            summary['program_fixture_hashes'] = {str(p.relative_to(out)): hashlib.sha256(p.read_bytes()).hexdigest()
                                                for p in [fixture, *probes]}
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
