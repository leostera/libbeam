#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Leandro Ostera <leandro@ostera.io>
"""Fresh, serialized C-core preparation/execution validation. OTP is a separate reference."""
import argparse
import fcntl
import hashlib
import json
import os
import re
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
    summary = {'scope': 'additive_engine_lifecycle_and_selected_BEAM_execution_not_stateful_isolate_acceptance', 'commands': [], 'passed': False}
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
                            root / 'libbeam/tools/check_term_representation.py', root / 'libbeam/tools/check_binary_representation.py',
                            root / 'libbeam/tests/core_beam_program_test.c', root / 'libbeam/tools/generate_opcodes.py',
                            root / 'libbeam/tests/core_code_test.c', root / 'libbeam/tests/fixtures/code_peer.erl',
                            root / 'libbeam/tools/project_transform.py', root / 'libbeam/tools/project_dispatch.py',
                            root / 'libbeam/core/otp/execution-sources.json', root / 'libbeam/core/otp/binary-sources.json',
                            root / 'libbeam/core/otp/term-compare-sources.json', root / 'libbeam/core/otp/numeric-sources.json',
                            root / 'libbeam/tests/core_compare_test.c', root / 'libbeam/tests/fixtures/compare_slice.erl',
                            root / 'libbeam/tests/core_shape_test.c', root / 'libbeam/tests/core_shape_verify_test.c',
                            root / 'libbeam/tests/fixtures/shape_slice.erl',
                            root / 'libbeam/tests/core_add_test.c', root / 'libbeam/tests/core_big_test.c',
                            root / 'libbeam/tests/fixtures/add_slice.erl',
                            root / 'libbeam/tests/core_decimal_test.c', root / 'libbeam/tests/fixtures/decimal_slice.erl',
                            root / 'libbeam/core/otp/decimal-sources.json', root / 'libbeam/core/otp/writable-sources.json',
                            root / 'libbeam/tests/core_bit_copy_test.c', root / 'libbeam/core/otp/bit-copy-sources.json',
                            root / 'libbeam/tests/core_bit_writer_test.c', root / 'libbeam/core/otp/bit-writer-sources.json',
                            root / 'libbeam/tests/core_bit_segment_test.c', root / 'libbeam/tests/core_flatmap_test.c',
                            root / 'libbeam/core/otp/flatmap-error-sources.json', root / 'libbeam/tools/check_flatmap_representation.py',
                            root / 'libbeam/tests/fixtures/binary_error_reference.erl',
                            root / 'libbeam/tests/core_binary_transform_test.c', root / 'libbeam/tests/core_binary_emit_test.c',
                            root / 'libbeam/tests/fixtures/binary_create_slice.erl',
                            root / 'libbeam/core/otp/binary-loader-sources.json', root / 'libbeam/core/otp/binary-size-sources.json',
                            root / 'libbeam/tests/core_binary_size_test.c', root / 'libbeam/tests/fixtures/binary_size_reference.erl',
                            root / 'libbeam/tests/core_exception_test.c', root / 'libbeam/tests/fixtures/exception_slice.erl',
                            root / 'libbeam/tests/fixtures/exception_reference.erl', root / 'libbeam/tools/check_exception_representation.py',
                            root / 'libbeam/core/otp/exception-sources.json',
                            root / 'libbeam/tests/core_binary_flow_test.c', root / 'libbeam/tests/fixtures/binary_flow_slice.erl',
                            root / 'libbeam/tests/core_binary_vheap_test.c', root / 'libbeam/core/otp/binary-vheap-sources.json',
                            root / 'libbeam/tests/core_writable_init_test.c', root / 'libbeam/tests/api_writable_init_test.cpp',
                            root / 'libbeam/tests/fixtures/writable_init_slice.S', root / 'libbeam/tests/fixtures/writable_init_reference.erl',
                            root / 'libbeam/core/otp/writable-init-sources.json',
                            root / 'libbeam/core/otp/binary-build-sources.json',
                            root / 'libbeam/tests/core_binary_build_test.c', root / 'libbeam/tests/api_binary_build_test.cpp',
                            root / 'libbeam/tests/core_pid_test.c', root / 'libbeam/tests/fixtures/pid_slice.erl', root / 'libbeam/tests/fixtures/fun_target.erl', root / 'libbeam/tests/fixtures/fun_slice.erl',
                            root / 'libbeam/core/otp/process-identity-sources.json',
                            root / 'libbeam/tests/fixtures/binary_construct_slice.erl', root / 'libbeam/tests/fixtures/binary_control_slice.S',
                            root / 'libbeam/tests/fixtures/binary_construct_reference.erl',
                            root / 'libbeam/tests/fixtures/bit_writer_reference.erl',
                            root / 'libbeam/tests/core_binary_test.c', root / 'libbeam/tests/fixtures/binary_slice.erl',
                            root / 'libbeam/tools/otp/beam_makeops', root / 'libbeam/core/otp/loader-sources.json',
                            root / 'libbeam/tools/otp/make_tables', root / 'libbeam/core/otp/atom.names', root / 'libbeam/core/otp/bif.tab',
                            root / 'libbeam/tests/fixtures/first_slice.erl',
                            root / 'libbeam/tests/api_engine_test.cpp', root / 'libbeam/tests/api_world_test.cpp',
                            root / 'libbeam/tests/core_engine_test.c',
                            root / 'libbeam/tests/core_executor_test.c', root / 'libbeam/tests/core_world_test.c',
                            root / 'libbeam/tests/core_local_test.c', root / 'libbeam/tests/fixtures/local_slice.erl',
                            root / 'libbeam/tests/fixtures/async_slice.erl',
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
            execution_sources = json.loads((root / 'libbeam/core/otp/execution-sources.json').read_text())
            for filename, expected in execution_sources['sources'].items():
                if hashlib.sha256((root / filename).read_bytes()).hexdigest() != expected:
                    raise RuntimeError(f'Execution provenance changed: {filename}')
            summary['execution_sources'] = execution_sources
            binary_sources = json.loads((root / 'libbeam/core/otp/binary-sources.json').read_text())
            for filename, expected in binary_sources['sources'].items():
                if hashlib.sha256((root / filename).read_bytes()).hexdigest() != expected:
                    raise RuntimeError(f'Binary provenance changed: {filename}')
            summary['binary_sources'] = binary_sources
            comparison_sources = json.loads((root / 'libbeam/core/otp/term-compare-sources.json').read_text())
            for filename, expected in comparison_sources['sources'].items():
                if hashlib.sha256((root / filename).read_bytes()).hexdigest() != expected:
                    raise RuntimeError(f'Comparison reference changed: {filename}')
            summary['comparison_sources'] = comparison_sources
            numeric_sources = json.loads((root / 'libbeam/core/otp/numeric-sources.json').read_text())
            for filename, expected in numeric_sources['sources'].items():
                if hashlib.sha256((root / filename).read_bytes()).hexdigest() != expected:
                    raise RuntimeError(f'Numeric reference changed: {filename}')
            summary['numeric_sources'] = numeric_sources
            decimal_sources = json.loads((root / 'libbeam/core/otp/decimal-sources.json').read_text())
            for filename, expected in decimal_sources['sources'].items():
                if hashlib.sha256((root / filename).read_bytes()).hexdigest() != expected:
                    raise RuntimeError(f'Decimal reference changed: {filename}')
            summary['decimal_sources'] = decimal_sources
            bit_copy_sources = json.loads((root / 'libbeam/core/otp/bit-copy-sources.json').read_text())
            for filename, expected in bit_copy_sources['sources'].items():
                if hashlib.sha256((root / filename).read_bytes()).hexdigest() != expected:
                    raise RuntimeError(f'Bit-copy reference changed: {filename}')
            summary['bit_copy_sources'] = bit_copy_sources
            bit_writer_sources = json.loads((root / 'libbeam/core/otp/bit-writer-sources.json').read_text())
            for filename, expected in bit_writer_sources['sources'].items():
                if hashlib.sha256((root / filename).read_bytes()).hexdigest() != expected:
                    raise RuntimeError(f'Bit-writer reference changed: {filename}')
            summary['bit_writer_sources'] = bit_writer_sources
            writable_sources = json.loads((root / 'libbeam/core/otp/writable-sources.json').read_text())
            for filename, expected in writable_sources['sources'].items():
                if hashlib.sha256((root / filename).read_bytes()).hexdigest() != expected:
                    raise RuntimeError(f'Writable-binary reference changed: {filename}')
            summary['writable_sources'] = writable_sources
            flatmap_sources = json.loads((root / 'libbeam/core/otp/flatmap-error-sources.json').read_text())
            for filename, expected in flatmap_sources['sources'].items():
                if hashlib.sha256((root / filename).read_bytes()).hexdigest() != expected:
                    raise RuntimeError(f'Flatmap/error reference changed: {filename}')
            summary['flatmap_error_sources'] = flatmap_sources
            binary_loader_sources = json.loads((root / 'libbeam/core/otp/binary-loader-sources.json').read_text())
            for filename, expected in binary_loader_sources['sources'].items():
                if hashlib.sha256((root / filename).read_bytes()).hexdigest() != expected:
                    raise RuntimeError(f'Binary loader reference changed: {filename}')
            summary['binary_loader_sources'] = binary_loader_sources
            binary_size_sources = json.loads((root / 'libbeam/core/otp/binary-size-sources.json').read_text())
            for filename, expected in binary_size_sources['sources'].items():
                if hashlib.sha256((root / filename).read_bytes()).hexdigest() != expected:
                    raise RuntimeError(f'Binary sizing reference changed: {filename}')
            summary['binary_size_sources'] = binary_size_sources
            binary_vheap_sources = json.loads((root / 'libbeam/core/otp/binary-vheap-sources.json').read_text())
            for filename, expected in binary_vheap_sources['sources'].items():
                if hashlib.sha256((root / filename).read_bytes()).hexdigest() != expected:
                    raise RuntimeError('Unreviewed binary vheap reference '+filename)
            summary['binary_vheap_sources'] = binary_vheap_sources
            writable_init_sources = json.loads((root / 'libbeam/core/otp/writable-init-sources.json').read_text())
            for filename, expected in writable_init_sources['sources'].items():
                if hashlib.sha256((root / filename).read_bytes()).hexdigest() != expected:
                    raise RuntimeError('Unreviewed writable initializer reference '+filename)
            summary['writable_init_sources'] = writable_init_sources
            binary_build_sources = json.loads((root / 'libbeam/core/otp/binary-build-sources.json').read_text())
            for filename, expected in binary_build_sources['sources'].items():
                if hashlib.sha256((root / filename).read_bytes()).hexdigest() != expected:
                    raise RuntimeError('Unreviewed binary construction reference '+filename)
            summary['binary_build_sources'] = binary_build_sources
            pid_sources = json.loads((root / 'libbeam/core/otp/process-identity-sources.json').read_text())
            for filename, expected in pid_sources['sources'].items():
                if hashlib.sha256((root / filename).read_bytes()).hexdigest() != expected:
                    raise RuntimeError('Unreviewed process identity reference '+filename)
            exception_sources = json.loads((root / 'libbeam/core/otp/exception-sources.json').read_text())
            for filename, expected in exception_sources['sources'].items():
                if hashlib.sha256((root / filename).read_bytes()).hexdigest() != expected:
                    raise RuntimeError(f'Exception reference changed: {filename}')
            summary['exception_sources'] = exception_sources
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
            run('binary-representation', ['python3', '-B', root / 'libbeam/tools/check_binary_representation.py',
                                          '--output', out / 'binary-oracle'])
            run('configure', ['cmake', '-S', root / 'libbeam', '-B', out / 'build',
                             '-DBUILD_TESTING=ON', '-DCMAKE_BUILD_TYPE=Debug',
                             '-DCMAKE_C_FLAGS=-Wall -Wextra -Werror', '-DCMAKE_CXX_FLAGS=-Wall -Wextra -Werror'])
            run('build', ['cmake', '--build', out / 'build'])
            run('ctest', ['ctest', '--test-dir', out / 'build', '--output-on-failure'])
            symbols = run('core-symbols', ['nm', '-u', out / 'build/liblibbeam_core.a'])
            if any(name in symbols for name in ('erts_', 'ethr_')):
                raise RuntimeError('Unexpected legacy runtime dependency in C core')
            admitted_threads = {'pthread_mutex_init', 'pthread_mutex_destroy', 'pthread_mutex_lock',
                                'pthread_mutex_unlock', 'pthread_cond_init', 'pthread_cond_destroy',
                                'pthread_cond_signal', 'pthread_cond_wait', 'pthread_cond_timedwait', 'pthread_create', 'pthread_join'}
            thread_symbols = set(re.findall(r'\b_?(pthread_[A-Za-z0-9_]+)\b', symbols))
            if thread_symbols != admitted_threads:
                raise RuntimeError(f'Unexpected POSIX executor surface: {thread_symbols ^ admitted_threads}')
            summary['admitted_thread_symbols'] = sorted(thread_symbols)
            fixture_dir = out / 'fixture'
            fixture_dir.mkdir()
            run('bit-writer-reference-erlc', [args.erlc, '-o', fixture_dir, root / 'libbeam/tests/fixtures/bit_writer_reference.erl'])
            writer_reference = run('bit-writer-reference', [args.erl, '-noshell', '-pa', fixture_dir,
                '-eval', 'bit_writer_reference:run(),halt().'])
            writer_actual = run('bit-writer-vectors', [out / 'build/core_bit_writer_test', '--reference'])
            if writer_reference != writer_actual or len(writer_reference.splitlines()) != 1070:
                raise RuntimeError('Native bit writers differ from stock-OTP vectors')
            segment_reference = run('bit-segment-reference', [args.erl, '-noshell', '-pa', fixture_dir,
                '-eval', 'bit_writer_reference:segments(),halt().'])
            if run('bit-segment-vectors', [out / 'build/core_bit_segment_test', '--reference']) != segment_reference or len(segment_reference.splitlines()) != 840:
                raise RuntimeError('Native float segments differ from stock-OTP vectors')
            summary['bit_segment_reference_vectors'] = 840
            summary['bit_writer_reference_vectors'] = 1070
            summary['bit_writer_reference_sha256'] = hashlib.sha256((fixture_dir / 'bit_writer_reference.beam').read_bytes()).hexdigest()
            run('async-erlc', [args.erlc, '-o', fixture_dir, root / 'libbeam/tests/fixtures/async_slice.erl'])
            async_fixture = fixture_dir / 'async_slice.beam'
            run('executor-execution', [out / 'build/core_executor_test', async_fixture])
            large_source = fixture_dir / 'code_large.erl'
            large_source.write_text('-module(code_large).\n-export([value/1]).\nvalue(_) -> <<"'+'q'*65537+'">>.\n')
            summary['large_fixture_source_sha256'] = hashlib.sha256(large_source.read_bytes()).hexdigest()
            run('large-erlc', [args.erlc, '-o', fixture_dir, large_source])
            large_fixture = fixture_dir / 'code_large.beam'
            run('local-erlc', [args.erlc, '-o', fixture_dir, root / 'libbeam/tests/fixtures/local_slice.erl'])
            local_fixture = fixture_dir / 'local_slice.beam'
            run('local-execution', [out / 'build/core_local_test', local_fixture])
            run('compare-erlc', [args.erlc, '-o', fixture_dir, root / 'libbeam/tests/fixtures/compare_slice.erl'])
            compare_fixture = fixture_dir / 'compare_slice.beam'
            run('compare-execution', [out / 'build/core_compare_test', compare_fixture])
            run('shape-erlc', [args.erlc, '-o', fixture_dir, root / 'libbeam/tests/fixtures/shape_slice.erl'])
            shape_fixture = fixture_dir / 'shape_slice.beam'
            run('shape-execution', [out / 'build/core_shape_test', shape_fixture])
            run('add-erlc', [args.erlc, '-o', fixture_dir, root / 'libbeam/tests/fixtures/add_slice.erl'])
            add_fixture = fixture_dir / 'add_slice.beam'
            run('add-execution', [out / 'build/core_add_test', add_fixture])
            run('decimal-erlc', [args.erlc, '-o', fixture_dir, root / 'libbeam/tests/fixtures/decimal_slice.erl'])
            decimal_fixture = fixture_dir / 'decimal_slice.beam'
            run('decimal-execution', [out / 'build/core_decimal_test', decimal_fixture])
            run('pid-erlc', [args.erlc, '-o', fixture_dir, root / 'libbeam/tests/fixtures/pid_slice.erl', root / 'libbeam/tests/fixtures/fun_target.erl', root / 'libbeam/tests/fixtures/fun_slice.erl'])
            pid_fixture = fixture_dir / 'pid_slice.beam'
            fun_target_fixture = fixture_dir / 'fun_target.beam'
            fun_fixture = fixture_dir / 'fun_slice.beam'
            run('world-execution', [out / 'build/core_world_test', async_fixture, fun_fixture])
            run('pid-execution', [out / 'build/core_pid_test', pid_fixture, fun_target_fixture, fun_fixture])
            run('api-world-execution', [out / 'build/api_world_test', async_fixture, large_fixture, local_fixture, compare_fixture, shape_fixture, add_fixture, decimal_fixture, pid_fixture, fun_fixture])
            run('erlc', [args.erlc, '-o', fixture_dir, root / 'libbeam/tests/fixtures/first_slice.erl'])
            fixture = fixture_dir / 'first_slice.beam'
            run('peer-erlc', [args.erlc, '-o', fixture_dir, root / 'libbeam/tests/fixtures/code_peer.erl'])
            peer_fixture = fixture_dir / 'code_peer.beam'
            run('binary-erlc', [args.erlc, '-o', fixture_dir, root / 'libbeam/tests/fixtures/binary_slice.erl'])
            binary_fixture = fixture_dir / 'binary_slice.beam'
            run('flatmap-representation', ['python3', '-B', root / 'libbeam/tools/check_flatmap_representation.py',
                '--output', out / 'flatmap-representation'])
            run('binary-error-reference-erlc', [args.erlc, '-o', fixture_dir, root / 'libbeam/tests/fixtures/binary_error_reference.erl'])
            error_reference = run('binary-error-reference', [args.erl, '-noshell', '-pa', fixture_dir,
                '-eval', 'binary_error_reference:run(),halt().'])
            if run('binary-error-vectors', [out / 'build/core_flatmap_test', '--reference', binary_fixture]) != error_reference or len(error_reference.splitlines()) != 5:
                raise RuntimeError('Binary error payloads differ from stock-OTP vectors')
            summary['binary_error_reference_vectors'] = 5
            summary['binary_error_reference_sha256'] = hashlib.sha256((fixture_dir / 'binary_error_reference.beam').read_bytes()).hexdigest()
            flatmap_execution = run('flatmap-execution', [out / 'build/core_flatmap_test', binary_fixture])
            if 'CORE_BINARY_ERROR_PAYLOAD_OK' not in flatmap_execution:
                raise RuntimeError('Missing flatmap/error payload execution evidence')
            summary['flatmap_error_payload_dependencies_passed'] = True
            run('binary-create-erlc', [args.erlc, '-o', fixture_dir, root / 'libbeam/tests/fixtures/binary_create_slice.erl'])
            binary_create_fixture = fixture_dir / 'binary_create_slice.beam'
            binary_transform = run('binary-transform', [out / 'build/core_binary_transform_test', binary_create_fixture])
            if 'CORE_BINARY_TRANSFORM_FIXTURE_OK' not in binary_transform:
                raise RuntimeError('Missing native binary generator/fixture evidence')
            summary['binary_transform_dependencies_passed'] = True
            run('binary-flow-erlc', [args.erlc, '-o', fixture_dir, root / 'libbeam/tests/fixtures/binary_flow_slice.erl'])
            binary_flow_fixture = fixture_dir / 'binary_flow_slice.beam'
            binary_flow = run('binary-flow', [out / 'build/core_binary_flow_test', binary_flow_fixture])
            if 'CORE_BINARY_FLOW_FIXTURE_OK' not in binary_flow:
                raise RuntimeError('Missing binary flow proof and executable-refusal evidence')
            summary['binary_flow_dependencies_passed'] = True
            summary['binary_flow_fixture_sha256'] = hashlib.sha256(binary_flow_fixture.read_bytes()).hexdigest()
            run('writable-init-erlc', [args.erlc, '-o', fixture_dir, root / 'libbeam/tests/fixtures/writable_init_slice.S', root / 'libbeam/tests/fixtures/writable_init_reference.erl'])
            writable_init_fixture = fixture_dir / 'writable_init_slice.beam'
            writable_init = run('writable-init-execution', [out / 'build/core_writable_init_test', writable_init_fixture])
            if 'CORE_WRITABLE_INIT_EXECUTION_OK' not in writable_init or 'CORE_WRITABLE_INIT_WORKER_OK' not in writable_init:
                raise RuntimeError('Missing whole initializer/worker execution evidence')
            writable_reference = run('writable-init-reference', [args.erl, '-noshell', '-pa', fixture_dir, '-eval', 'writable_init_reference:run(),halt().'])
            if len(writable_reference.splitlines()) != 9 or run('writable-init-vectors', [out / 'build/core_writable_init_test', '--reference', writable_init_fixture]) != writable_reference:
                raise RuntimeError('Writable initializer values differ from stock OTP')
            run('api-writable-init', [out / 'build/api_writable_init_test', writable_init_fixture])
            summary['writable_init_execution_passed'] = True
            summary['writable_init_reference_vectors'] = 9
            summary['writable_init_fixture_sha256'] = hashlib.sha256(writable_init_fixture.read_bytes()).hexdigest()
            run('binary-build-erlc', [args.erlc, '-o', fixture_dir, root / 'libbeam/tests/fixtures/binary_construct_slice.erl', root / 'libbeam/tests/fixtures/binary_control_slice.S', root / 'libbeam/tests/fixtures/binary_construct_reference.erl'])
            build_fixture = fixture_dir / 'binary_construct_slice.beam'
            control_fixture = fixture_dir / 'binary_control_slice.beam'
            build_execution = run('binary-build-execution', [out / 'build/core_binary_build_test', build_fixture, control_fixture])
            if 'CORE_BINARY_BUILD_EXECUTION_OK' not in build_execution or 'native=2:integer:size' not in build_execution:
                raise RuntimeError('Missing actual binary construction/error-order execution')
            build_reference = run('binary-build-reference', [args.erl, '-noshell', '-pa', fixture_dir, '-eval', 'binary_construct_reference:run(),halt().'])
            if len(build_reference.splitlines()) != 683 or run('binary-build-vectors', [out / 'build/core_binary_build_test', '--reference', build_fixture, control_fixture]) != build_reference:
                raise RuntimeError('Binary construction values differ from stock OTP')
            build_order = run('binary-build-order-reference', [args.erl, '-noshell', '-pa', fixture_dir, '-eval', 'binary_construct_reference:ordering(),halt().']).strip()
            if build_order not in ('1:utf8:type', '2:integer:size'):
                raise RuntimeError('Unreviewed binary construction error ordering')
            run('api-binary-build', [out / 'build/api_binary_build_test', build_fixture, control_fixture])
            summary['binary_build_execution_passed'] = True
            summary['binary_build_reference_vectors'] = 683
            summary['binary_build_error_order'] = dict(stock=build_order, native_interpreter='2:integer:size', matches=build_order=='2:integer:size')
            summary['binary_build_fixture_sha256'] = hashlib.sha256(build_fixture.read_bytes()).hexdigest()
            summary['binary_control_fixture_sha256'] = hashlib.sha256(control_fixture.read_bytes()).hexdigest()
            binary_emission = run('binary-emission', [out / 'build/core_binary_emit_test', binary_create_fixture, binary_fixture])
            if 'CORE_BINARY_EMIT_FIXTURE_OK' not in binary_emission:
                raise RuntimeError('Missing native binary word/fixup ownership evidence')
            summary['binary_emission_dependencies_passed'] = True
            run('binary-size-reference-erlc', [args.erlc, '-o', fixture_dir, root / 'libbeam/tests/fixtures/binary_size_reference.erl'])
            size_reference = run('binary-size-reference', [args.erl, '-noshell', '-pa', fixture_dir,
                '-eval', 'binary_size_reference:run(),halt().'])
            size_native = run('binary-size-vectors', [out / 'build/core_binary_size_test', '--reference'])
            stock_lines, native_lines = size_reference.splitlines(), size_native.splitlines()
            if len(stock_lines) != 11 or len(native_lines) != 11 or stock_lines[1:] != native_lines[1:] or native_lines[0] != 'utf8:badarg:2:integer:size':
                raise RuntimeError('Unexpected native sizing/reference results')
            # The pinned interpreter defers UTF8 validation; the ARM JIT source
            # checks it during sizing. Keep the observed discrepancy, not a
            # normalized value or a claim that all stock vectors matched.
            if stock_lines[0] not in {native_lines[0], 'utf8:badarg:1:utf8:type'}:
                raise RuntimeError('Unreviewed UTF8 reference error ordering')
            summary['binary_size_reference_runtime'] = run('binary-size-reference-runtime', [args.erl, '-noshell', '-eval',
                'io:format("~p ~p~n",[erlang:system_info(otp_release),erlang:system_info(emu_flavor)]),halt().']).strip()
            summary['binary_size_reference_vectors'] = 11
            summary['binary_size_reference_matches'] = sum(a == b for a, b in zip(stock_lines, native_lines))
            summary['binary_size_reference_all_match'] = size_reference == size_native
            summary['binary_size_reference_differences'] = [dict(stock=a, native_interpreter=b) for a, b in zip(stock_lines, native_lines) if a != b]
            summary['binary_size_reference_sha256'] = hashlib.sha256((fixture_dir / 'binary_size_reference.beam').read_bytes()).hexdigest()
            size_execution = run('binary-size-execution', [out / 'build/core_binary_size_test', binary_fixture])
            if 'CORE_BINARY_SIZE_EXECUTION_OK' not in size_execution:
                raise RuntimeError('Missing binary sizing ownership/execution evidence')
            summary['binary_size_dependencies_passed'] = True
            binary_vheap = run('binary-vheap-execution', [out / 'build/core_binary_vheap_test', binary_fixture])
            if 'CORE_BINARY_VHEAP_EXECUTION_OK' not in binary_vheap:
                raise RuntimeError('Missing binary pressure/owned collection evidence')
            summary['binary_vheap_dependencies_passed'] = True
            run('exception-representation', ['python3', '-B', root / 'libbeam/tools/check_exception_representation.py', '--output', out / 'exception-representation'])
            run('exception-erlc', [args.erlc, '-o', fixture_dir, root / 'libbeam/tests/fixtures/exception_slice.erl', root / 'libbeam/tests/fixtures/exception_reference.erl'])
            exception_fixture = fixture_dir / 'exception_slice.beam'
            exception_execution = run('exception-execution', [out / 'build/core_exception_test', exception_fixture])
            if 'CORE_EXCEPTION_EXECUTION_OK' not in exception_execution:
                raise RuntimeError('Missing owned native exception-state evidence')
            exception_reference = run('exception-reference', [args.erl, '-noshell', '-pa', fixture_dir, '-eval', 'exception_reference:run(),halt().'])
            if run('exception-vectors', [out / 'build/core_exception_test', '--reference', exception_fixture]) != exception_reference or len(exception_reference.splitlines()) != 4:
                raise RuntimeError('Expanded exception values differ from stock OTP')
            summary['exception_state_dependencies_passed'] = True
            summary['exception_reference_vectors'] = 4
            summary['exception_fixture_sha256'] = hashlib.sha256(exception_fixture.read_bytes()).hexdigest()
            summary['binary_create_fixture_sha256'] = hashlib.sha256(binary_create_fixture.read_bytes()).hexdigest()
            binary_execution = run('binary-execution', [out / 'build/core_binary_test', binary_fixture, fixture, peer_fixture])
            if 'CORE_BINARY_EXECUTION_OK' not in binary_execution or 'CORE_BINARY_RETIREMENT_OK' not in binary_execution:
                raise RuntimeError('Missing binary execution/retirement evidence')
            if 'CORE_WRITABLE_OK' not in binary_execution:
                raise RuntimeError('Missing writable binary ownership/execution evidence')
            summary['writable_binary_dependencies_passed'] = True
            if 'CORE_BINARY_BEGIN_OK' not in binary_execution:
                raise RuntimeError('Missing fresh binary construction ownership/execution evidence')
            summary['fresh_binary_construction_dependencies_passed'] = True
            if 'CORE_BINARY_SHARED_EDGE_OK' not in binary_execution:
                raise RuntimeError('Missing shared-edge binary segment ownership/execution evidence')
            alias_reference = run('bit-alias-reference', [args.erl, '-noshell', '-pa', fixture_dir,
                '-eval', 'bit_writer_reference:aliases(),halt().'])
            if run('bit-alias-vectors', [out / 'build/core_binary_test', '--alias-reference', binary_fixture]) != alias_reference or len(alias_reference.splitlines()) != 514:
                raise RuntimeError('Shared-edge binary segments differ from stock-OTP vectors')
            summary['shared_edge_binary_dependencies_passed'] = True
            summary['bit_alias_reference_vectors'] = 514
            if 'CORE_PRIVATE_APPEND_OK' not in binary_execution:
                raise RuntimeError('Missing checked private-append ownership/execution evidence')
            summary['private_append_dependencies_passed'] = True
            many_source = fixture_dir / 'code_many.erl'
            many_source.write_text('-module(code_many).\n-export(['+','.join(f'f{i}/0' for i in range(128))+']).\n'+
                                   '\n'.join(f'f{i}() -> {i}.' for i in range(128))+'\n')
            summary['generated_erlang_source_sha256'] = hashlib.sha256(many_source.read_bytes()).hexdigest()
            run('many-erlc', [args.erlc, '-o', fixture_dir, many_source])
            many_fixture = fixture_dir / 'code_many.beam'
            run('core-code-growth', [out / 'build/core_code_test', '--many', many_fixture])
            execution = run('core-execution', [out / 'build/core_code_test', fixture, peer_fixture])
            if 'CORE_CODE_EXECUTION_OK' not in execution or 'CORE_LINKED_CODE_OK' not in execution:
                raise RuntimeError('Missing real execution/linked-retirement evidence')
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
            binary_program = run('binary-program', [out / 'build/core_beam_program_test', binary_fixture])
            compare_functions('binary', binary_fixture, binary_program)
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
                run(variant+'-executable-refusal', [out / 'build/core_code_test', '--reject', probe])
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
                65537 = byte_size(code_large:value(<<>>)),
                <<1,2,3>> = local_slice:identity(<<1,2,3>>),
                <<1,2,3>> = local_slice:gc(<<1,2,3>>),
                <<1,2,3>> = local_slice:tail(<<1,2,3>>),
                {<<1,2,3>>,<<1,2,3>>} = local_slice:pair(<<1,2,3>>),
                <<"equal">> = compare_slice:same({[1|tail],1 bsl 80}, {[1|tail],1 bsl 80}),
                <<"different">> = compare_slice:same(0.0,-0.0),
                <<"different">> = compare_slice:same(0,0.0),
                <<"equal">> = compare_slice:different(<<5:3>>,<<5:3>>),
                <<"42">> = decimal_slice:small(ignored),
                <<"-18446744073709551617">> = decimal_slice:negative(ignored),
                Decimal = integer_to_binary(1 bsl 8192), Decimal = decimal_slice:large(ignored),
                2467 = byte_size(Decimal),
                78914 = byte_size(decimal_slice:too_large(ignored)),
                42 = add_slice:add(40,2),
                2305843009213693950 = add_slice:big(ignored),
                1152921504606847232.0 = add_slice:float(ignored),
                <<"yes">> = add_slice:guarded(40,2),
                <<"no">> = add_slice:guarded(invalid,2),
                <<"test">> = add_slice:copied(<<"test">>),
                <<"test">> = add_slice:rounded(<<"test">>),
                <<"test">> = shape_slice:tuple_binary(<<"test">>),
                <<"test">> = shape_slice:list_binary(<<"test">>),
                42 = shape_slice:tagged_saved({tag,42}),
                9 = shape_slice:head_saved([9]),
                {2,1} = shape_slice:tuple({1,2}),
                {7,8} = shape_slice:list([7|8]),
                [7,7] = shape_slice:build(7),
                <<"0">> = shape_slice:arities({}),
                <<"1">> = shape_slice:arities({one}),
                <<"2">> = shape_slice:arities({one,two}),
                <<"other">> = shape_slice:arities({one,two,three}),
                <<"2">> = shape_slice:two_arities({one,two}),
                <<"empty">> = compare_slice:empty(<<>>),
                try compare_slice:empty(<<1>>) of _ -> error(missing_function_clause)
                catch error:function_clause -> ok end,
                <<"next-ok">> = compare_slice:command(<<"next">>),
                <<"other">> = compare_slice:not_command(<<"read">>),
                Digits = binary:copy(<<"0123456789">>,8),
                {Digits} = binary_slice:literal(), {Bits} = binary_slice:bits(),
                Bits = <<Digits/binary,5:3>>,
                Input = list_to_binary(lists:seq(0,255)), Input = binary_slice:identity(Input),
                {Input,Input} = binary_slice:wrap(Input), discarded = binary_slice:drop(Input),
                [<<0:2048,16#7F>>] = proplists:get_value(payload,binary_slice:module_info(attributes)),
                io:format("REFERENCE_BINARY_OK not_libbeam_execution=true~n"),
                io:format("REFERENCE_MODULE_MD5 ~s~n",[binary:encode_hex(first_slice:module_info(md5))]),
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
            core_md5 = next(r.split()[1] for r in execution.splitlines() if r.startswith('CORE_MODULE_MD5 '))
            reference_md5 = next(r.split()[1].lower() for r in reference.splitlines() if r.startswith('REFERENCE_MODULE_MD5 '))
            if core_md5 != reference_md5: raise RuntimeError('Executed module_info(md5) differs from reference')
            summary['executed_module_md5'] = core_md5
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
                               'beam_program', 'beam_decode', 'beam_metadata', 'beam_literals', 'beam_select', 'binary']
            run('program-ubsan-compile', ['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                '-fsanitize=undefined', '-fno-sanitize-recover=all', '-Ilibbeam/core',
                '-I'+str(out / 'build/generated/atoms'), '-I'+str(out / 'build/generated/opcodes'),
                *[f'libbeam/core/{s}.c' for s in program_sources], 'libbeam/tests/core_beam_program_test.c',
                '-lz', '-o', out / 'program-ubsan'])
            for i, image in enumerate([fixture, binary_fixture, *probes]):
                run(f'program-ubsan-{i}', [out / 'program-ubsan', image])
            execution_sources_c = program_sources + ['beam_transform','beam_emit','beam_verify','code','engine','executor','world','md5','process','heap','bif_info','term_compare','big','bif_numeric','decimal','bif_decimal','bit_copy','bit_writer','bit_float','writable','binary_error','binary_size','binary_vheap','binary_build','exception','pid_table','bif_process','fun','message','receive','registry']
            run('execution-ubsan-compile', ['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                '-fsanitize=undefined', '-fno-sanitize-recover=all', '-Ilibbeam/core',
                '-I'+str(out / 'build/generated/atoms'), '-I'+str(out / 'build/generated/opcodes'),
                *[f'libbeam/core/{s}.c' for s in execution_sources_c], 'libbeam/tests/core_code_test.c',
                '-pthread', '-lz', '-lm', '-o', out / 'execution-ubsan'])
            run('execution-ubsan', [out / 'execution-ubsan', fixture, peer_fixture])
            run('execution-growth-ubsan', [out / 'execution-ubsan', '--many', many_fixture])
            run('binary-ubsan-compile', ['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                '-fsanitize=undefined', '-fno-sanitize-recover=all', '-Ilibbeam/core',
                '-I'+str(out / 'build/generated/atoms'), '-I'+str(out / 'build/generated/opcodes'),
                *[f'libbeam/core/{s}.c' for s in execution_sources_c], 'libbeam/tests/core_binary_test.c',
                '-pthread', '-lz', '-lm', '-o', out / 'binary-ubsan'])
            run('binary-ubsan', [out / 'binary-ubsan', binary_fixture, fixture, peer_fixture])
            if run('bit-alias-ubsan-vectors', [out / 'binary-ubsan', '--alias-reference', binary_fixture]) != alias_reference:
                raise RuntimeError('UBSan shared-edge segments differ from stock-OTP vectors')
            sanitizer_flags = '-Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=all'
            run('engine-ubsan-configure', ['cmake', '-S', root / 'libbeam', '-B', out / 'engine-ubsan',
                '-DBUILD_TESTING=ON', '-DCMAKE_BUILD_TYPE=Debug',
                '-DCMAKE_C_FLAGS='+sanitizer_flags+' -DHAVE_OVERFLOW_CHECK_BUILTINS', '-DCMAKE_CXX_FLAGS='+sanitizer_flags])
            run('engine-ubsan-build', ['cmake', '--build', out / 'engine-ubsan', '--target',
                'core_engine_test', 'core_executor_test', 'core_world_test', 'core_local_test', 'core_compare_test', 'core_shape_test', 'core_shape_verify_test', 'core_big_test', 'core_add_test', 'core_decimal_test', 'core_bit_copy_test', 'core_bit_writer_test', 'core_bit_segment_test', 'core_flatmap_test', 'core_binary_transform_test', 'core_binary_emit_test', 'core_binary_size_test', 'core_exception_test', 'core_binary_flow_test', 'core_binary_vheap_test', 'core_writable_init_test', 'api_writable_init_test', 'core_binary_build_test', 'api_binary_build_test', 'core_pid_test', 'api_engine_test', 'api_world_test', 'engine_lifecycle'])
            run('engine-ubsan-test', ['ctest', '--test-dir', out / 'engine-ubsan', '--output-on-failure',
                '-R', '^(core_engine_lifetime|core_executor_native_lifetime|core_world_lifetime|core_exact_comparison|core_shape_proof|core_big_components|core_decimal_components|core_bit_copy_components|core_bit_writer_components|core_bit_segment_components|core_flatmap_components|core_binary_transform_components|core_binary_emit_components|core_binary_size_components|core_exception_components|core_binary_flow_proof|core_binary_vheap_policy|core_writable_init_gate|core_binary_build_gate|core_pid_gate|api_engine_lifetime|engine_lifecycle_acceptance)$'])
            if run('bit-segment-ubsan-vectors', [out / 'engine-ubsan/core_bit_segment_test', '--reference']) != segment_reference:
                raise RuntimeError('UBSan float segments differ from stock-OTP vectors')
            run('flatmap-ubsan', [out / 'engine-ubsan/core_flatmap_test', binary_fixture])
            run('binary-transform-ubsan', [out / 'engine-ubsan/core_binary_transform_test', binary_create_fixture])
            run('binary-emission-ubsan', [out / 'engine-ubsan/core_binary_emit_test', binary_create_fixture, binary_fixture])
            run('binary-size-ubsan', [out / 'engine-ubsan/core_binary_size_test', binary_fixture])
            run('exception-ubsan', [out / 'engine-ubsan/core_exception_test', exception_fixture])
            run('binary-flow-ubsan', [out / 'engine-ubsan/core_binary_flow_test', binary_flow_fixture])
            run('binary-vheap-ubsan', [out / 'engine-ubsan/core_binary_vheap_test', binary_fixture])
            run('writable-init-ubsan', [out / 'engine-ubsan/core_writable_init_test', writable_init_fixture])
            run('api-writable-init-ubsan', [out / 'engine-ubsan/api_writable_init_test', writable_init_fixture])
            run('binary-build-ubsan', [out / 'engine-ubsan/core_binary_build_test', build_fixture, control_fixture])
            run('api-binary-build-ubsan', [out / 'engine-ubsan/api_binary_build_test', build_fixture, control_fixture])
            if run('binary-build-ubsan-vectors', [out / 'engine-ubsan/core_binary_build_test', '--reference', build_fixture, control_fixture]) != build_reference:
                raise RuntimeError('UBSan binary construction differs from stock OTP')
            if run('writable-init-ubsan-vectors', [out / 'engine-ubsan/core_writable_init_test', '--reference', writable_init_fixture]) != writable_reference:
                raise RuntimeError('UBSan writable initializer values differ from stock OTP')
            if run('exception-ubsan-vectors', [out / 'engine-ubsan/core_exception_test', '--reference', exception_fixture]) != exception_reference:
                raise RuntimeError('UBSan exception values differ from stock OTP')
            if run('binary-size-ubsan-vectors', [out / 'engine-ubsan/core_binary_size_test', '--reference']) != size_native:
                raise RuntimeError('UBSan sizing differs from the native interpreter profile')
            if run('binary-error-ubsan-vectors', [out / 'engine-ubsan/core_flatmap_test', '--reference', binary_fixture]) != error_reference:
                raise RuntimeError('UBSan binary error payloads differ from stock-OTP vectors')
            run('bit-writer-fallback-compile', ['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-DDEBUG',
                '-fsanitize=undefined', '-fno-sanitize-recover=all', '-Ilibbeam/core',
                'libbeam/core/bit_writer.c', 'libbeam/core/bit_copy.c', 'libbeam/core/binary.c', 'libbeam/core/alloc.c', 'libbeam/tests/core_bit_writer_test.c',
                '-o', out / 'bit-writer-fallback'])
            run('bit-writer-fallback', [out / 'bit-writer-fallback'])
            if run('bit-writer-fallback-vectors', [out / 'bit-writer-fallback', '--reference']) != writer_reference:
                raise RuntimeError('Native fallback bit writers differ from stock-OTP vectors')
            run('executor-ubsan', [out / 'engine-ubsan/core_executor_test', async_fixture])
            run('world-ubsan', [out / 'engine-ubsan/core_world_test', async_fixture, fun_fixture])
            run('local-ubsan', [out / 'engine-ubsan/core_local_test', local_fixture])
            run('compare-ubsan', [out / 'engine-ubsan/core_compare_test', compare_fixture])
            run('shape-ubsan', [out / 'engine-ubsan/core_shape_test', shape_fixture])
            run('add-ubsan', [out / 'engine-ubsan/core_add_test', add_fixture])
            run('decimal-ubsan', [out / 'engine-ubsan/core_decimal_test', decimal_fixture])
            run('pid-ubsan', [out / 'engine-ubsan/core_pid_test', pid_fixture, fun_target_fixture, fun_fixture])
            run('api-world-ubsan', [out / 'engine-ubsan/api_world_test', async_fixture, large_fixture, local_fixture, compare_fixture, shape_fixture, add_fixture, decimal_fixture, pid_fixture, fun_fixture])
            run('release-configure', ['cmake', '-S', root / 'libbeam', '-B', out / 'release',
                                     '-DBUILD_TESTING=ON', '-DCMAKE_BUILD_TYPE=Release'])
            run('release-build', ['cmake', '--build', out / 'release'])
            run('release-ctest', ['ctest', '--test-dir', out / 'release', '--output-on-failure'])
            if run('bit-segment-release-vectors', [out / 'release/core_bit_segment_test', '--reference']) != segment_reference:
                raise RuntimeError('Release float segments differ from stock-OTP vectors')
            run('release-fixture', [out / 'release/core_beam_image_test', fixture])
            run('release-atom-binding', [out / 'release/core_terms_atoms_test', fixture])
            run('release-execution', [out / 'release/core_code_test', fixture, peer_fixture])
            run('release-code-growth', [out / 'release/core_code_test', '--many', many_fixture])
            run('release-binary', [out / 'release/core_binary_test', binary_fixture, fixture, peer_fixture])
            if run('bit-alias-release-vectors', [out / 'release/core_binary_test', '--alias-reference', binary_fixture]) != alias_reference:
                raise RuntimeError('Release shared-edge segments differ from stock-OTP vectors')
            run('flatmap-release', [out / 'release/core_flatmap_test', binary_fixture])
            run('binary-transform-release', [out / 'release/core_binary_transform_test', binary_create_fixture])
            run('binary-emission-release', [out / 'release/core_binary_emit_test', binary_create_fixture, binary_fixture])
            run('binary-size-release', [out / 'release/core_binary_size_test', binary_fixture])
            run('exception-release', [out / 'release/core_exception_test', exception_fixture])
            run('binary-flow-release', [out / 'release/core_binary_flow_test', binary_flow_fixture])
            run('binary-vheap-release', [out / 'release/core_binary_vheap_test', binary_fixture])
            run('writable-init-release', [out / 'release/core_writable_init_test', writable_init_fixture])
            run('api-writable-init-release', [out / 'release/api_writable_init_test', writable_init_fixture])
            run('binary-build-release', [out / 'release/core_binary_build_test', build_fixture, control_fixture])
            run('api-binary-build-release', [out / 'release/api_binary_build_test', build_fixture, control_fixture])
            if run('binary-build-release-vectors', [out / 'release/core_binary_build_test', '--reference', build_fixture, control_fixture]) != build_reference:
                raise RuntimeError('Release binary construction differs from stock OTP')
            if run('writable-init-release-vectors', [out / 'release/core_writable_init_test', '--reference', writable_init_fixture]) != writable_reference:
                raise RuntimeError('Release writable initializer values differ from stock OTP')
            if run('exception-release-vectors', [out / 'release/core_exception_test', '--reference', exception_fixture]) != exception_reference:
                raise RuntimeError('Release exception values differ from stock OTP')
            if run('binary-size-release-vectors', [out / 'release/core_binary_size_test', '--reference']) != size_native:
                raise RuntimeError('Release sizing differs from the native interpreter profile')
            if run('binary-error-release-vectors', [out / 'release/core_flatmap_test', '--reference', binary_fixture]) != error_reference:
                raise RuntimeError('Release binary error payloads differ from stock-OTP vectors')
            run('release-executor', [out / 'release/core_executor_test', async_fixture])
            run('release-world', [out / 'release/core_world_test', async_fixture])
            run('local-release', [out / 'release/core_local_test', local_fixture])
            run('compare-release', [out / 'release/core_compare_test', compare_fixture])
            run('shape-release', [out / 'release/core_shape_test', shape_fixture])
            run('add-release', [out / 'release/core_add_test', add_fixture])
            run('decimal-release', [out / 'release/core_decimal_test', decimal_fixture])
            run('pid-release', [out / 'release/core_pid_test', pid_fixture, fun_target_fixture, fun_fixture])
            run('api-world-release', [out / 'release/api_world_test', async_fixture, large_fixture, local_fixture, compare_fixture, shape_fixture, add_fixture, decimal_fixture, pid_fixture, fun_fixture])
            for filename in ('lb_atoms_generated.h', 'lb_atoms_generated.inc', 'lb_bif_ids_generated.h'):
                first = (out / 'build/generated/atoms' / filename).read_bytes()
                second = (out / 'release/generated/atoms' / filename).read_bytes()
                if first != second:
                    raise RuntimeError(f'Atom generation is not reproducible: {filename}')
            summary['atom_generation_reproducible'] = True
            for i, image in enumerate([fixture, binary_fixture, *probes]):
                run(f'release-program-{i}', [out / 'release/core_beam_program_test', image])
            first_outputs = json.loads((out / 'build/generated/opcodes/outputs.json').read_text())
            second_outputs = json.loads((out / 'release/generated/opcodes/outputs.json').read_text())
            if first_outputs != second_outputs:
                raise RuntimeError('Full OTP opcode generation is not reproducible across builds')
            for filename in ('lb_transform_generated.inc','lb_dispatch_generated.inc','lb_dispatch_support.inc','transform-admission.json'):
                if (out / 'build/generated/opcodes' / filename).read_bytes() != (out / 'release/generated/opcodes' / filename).read_bytes():
                    raise RuntimeError(f'Execution projection differs across builds: {filename}')
            summary['execution_projections_reproducible'] = True
            summary['selected_core_execution_passed'] = True
            summary['binary_execution_retirement_passed'] = True
            summary['worker_execution_retirement_passed'] = True
            summary['world_execution_retirement_passed'] = True
            summary['public_selected_profile_execution_passed'] = True
            summary['local_calls_y_roots_execution_passed'] = True
            summary['exact_comparison_execution_passed'] = True
            summary['tuple_list_shape_execution_passed'] = True
            summary['numeric_addition_execution_passed'] = True
            summary['decimal_continuation_execution_passed'] = True
            summary['native_bit_copy_passed'] = True
            summary['native_bit_writer_dependencies_passed'] = True
            summary['native_binary_float_segment_dependencies_passed'] = True
            summary['opcode_generation_outputs'] = first_outputs
            summary['opcode_generation_reproducible'] = True
            summary['program_fixture_hashes'] = {str(p.relative_to(out)): hashlib.sha256(p.read_bytes()).hexdigest()
                                                for p in [fixture, peer_fixture, binary_fixture, async_fixture, large_fixture, local_fixture, compare_fixture, shape_fixture, add_fixture, decimal_fixture, pid_fixture, fun_target_fixture, fun_fixture, many_fixture, *probes]}
            # G1 now requires the unchanged acceptance target and real C/adapter
            # failure recovery. Stateful G3 remains separately observed, not green.
            lifecycle = run('engine-lifecycle', [out / 'build/engine_lifecycle'])
            summary['engine_lifecycle_returncode'] = summary['commands'][-1]['returncode']
            if 'ENGINE_LIFECYCLE_OK create_shutdown_create=true' not in lifecycle:
                raise RuntimeError('Missing unchanged Engine lifetime acceptance marker')
            summary['engine_lifecycle_passed'] = True
            run('engine-lifecycle-release', [out / 'release/engine_lifecycle'])
            run('engine-components', [out / 'build/core_engine_test'])
            run('engine-api', [out / 'build/api_engine_test'])
            run('two-isolates-observed', [out / 'build/two_isolates', *probes], accepted=None)
            summary['two_isolates_returncode'] = summary['commands'][-1]['returncode']
            summary['two_isolates_passed'] = summary['two_isolates_returncode'] == 0
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
