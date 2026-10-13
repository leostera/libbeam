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
import sys
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
    summary = {'scope': 'additive_engine_lifecycle_and_stateful_isolate_acceptance_with_bounded_native_BEAM_profile', 'commands': [], 'passed': False}
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
                           set((root / 'libbeam/core').glob('*.def')) |
                           {p for p in (root / 'libbeam/core/otp/opcodes').rglob('*') if p.is_file()} |
                           {p for p in (root / 'libbeam/tests/fixtures/two_isolates').rglob('*') if p.is_file()} |
                           {root / 'libbeam/CMakeLists.txt', root / 'libbeam/src/engine.cpp',
                            root / 'libbeam/examples/run_beam.cpp', root / 'libbeam/include/libbeam/engine.hpp',
                            root / 'libbeam/tests/api_memory_test.cpp', root / 'libbeam/tests/core_memory_test.c',
                            root / 'libbeam/tests/core_capability_test.c', root / 'libbeam/tests/api_capability_test.cpp',
                            root / 'libbeam/tests/fixtures/capability_slice.erl',
                            root / 'libbeam/core/otp/capability-denial-sources.json',
                            root / 'libbeam/core/otp/bif-batch-contracts.json', root / 'libbeam/core/otp/bif-batch-wrapper-plan.json',
                            root / 'libbeam/core/otp/bif-batch-request.txt', root / 'libbeam/tools/audit_bif_batch.py',
                            root / 'libbeam/tools/test_audit_bif_batch.py',
                            root / 'libbeam/tests/core_work_test.c', root / 'libbeam/tests/core_work_runtime_test.c',
                            root / 'libbeam/tests/api_work_test.cpp', root / 'libbeam/tests/fixtures/work_slice.erl',
                            root / 'libbeam/tests/fixtures/memory_slice.erl',
                            root / 'libbeam/tests/fixtures/refused_exception.erl',
                            root / 'libbeam/tests/core_hamt_test.c', root / 'libbeam/tests/core_map_test.c',
                            root / 'libbeam/tests/core_map_execution_test.c', root / 'libbeam/tests/fixtures/map_slice.erl',
                            root / 'libbeam/tests/core_external_fun_test.c', root / 'libbeam/tests/fixtures/external_fun_slice.erl',
                            root / 'libbeam/tests/core_raise_trace_test.c', root / 'libbeam/tests/fixtures/raise_trace_reference.erl',
                            root / 'libbeam/tests/core_raise_execution_test.c', root / 'libbeam/tests/core_raise_work_test.c',
                            root / 'libbeam/tests/fixtures/raise_slice.erl',
                            root / 'libbeam/tests/core_max_test.c', root / 'libbeam/tests/core_setelement_test.c',
                            root / 'libbeam/tests/core_bif_work_test.c', root / 'libbeam/tests/core_times_test.c',
                            root / 'libbeam/tests/fixtures/times_slice.erl', root / 'libbeam/tests/fixtures/times_reference.erl',
                            root / 'libbeam/core/otp/times-sources.json', root / 'libbeam/core/otp/div-sources.json',
                            root / 'libbeam/tests/core_div_test.c', root / 'libbeam/tools/generate_div_vectors.py',
                            root / 'libbeam/tests/fixtures/div_slice.erl', root / 'libbeam/tests/fixtures/div_reference.erl',
                            root / 'libbeam/tests/core_abs_test.c', root / 'libbeam/core/otp/abs-sources.json',
                            root / 'libbeam/tests/fixtures/abs_slice.erl', root / 'libbeam/tests/fixtures/abs_reference.erl',
                            root / 'libbeam/tests/core_negate_test.c', root / 'libbeam/core/otp/negate-sources.json',
                            root / 'libbeam/tests/fixtures/negate_slice.erl', root / 'libbeam/tests/fixtures/negate_reference.erl',
                            root / 'libbeam/core/otp/rem-sources.json',
                            root / 'libbeam/tests/fixtures/rem_slice.erl', root / 'libbeam/tests/fixtures/rem_reference.erl',
                            root / 'libbeam/tests/core_list_integer_test.c', root / 'libbeam/core/otp/list-integer-sources.json',
                            root / 'libbeam/tests/fixtures/list_integer_slice.erl', root / 'libbeam/tests/fixtures/list_integer_reference.erl',
                            root / 'libbeam/tests/core_map_key_test.c', root / 'libbeam/core/otp/map-key-sources.json',
                            root / 'libbeam/core/otp/map-size-sources.json',
                            root / 'libbeam/core/otp/integer-predicate-sources.json', root / 'libbeam/tests/core_integer_predicate_test.c',
                            root / 'libbeam/tests/fixtures/integer_predicate_slice.erl', root / 'libbeam/tests/fixtures/integer_predicate_reference.erl',
                            root / 'libbeam/tests/fixtures/integer_predicate_literals.S',
                            root / 'libbeam/core/otp/list-parts-sources.json', root / 'libbeam/tests/core_list_parts_test.c',
                            root / 'libbeam/tests/fixtures/list_parts_slice.erl', root / 'libbeam/tests/fixtures/list_parts_reference.erl',
                            root / 'libbeam/core/otp/list-predicate-sources.json',
                            root / 'libbeam/tests/fixtures/list_predicate_slice.erl', root / 'libbeam/tests/fixtures/list_predicate_reference.erl',
                            root / 'libbeam/tests/fixtures/list_predicate_literals.S',
                            root / 'libbeam/core/otp/not-sources.json', root / 'libbeam/tests/core_not_test.c',
                            root / 'libbeam/tests/fixtures/not_slice.erl', root / 'libbeam/tests/fixtures/not_guard_slice.S',
                            root / 'libbeam/tests/fixtures/not_reference.erl',
                            root / 'libbeam/core/otp/atom-predicate-sources.json',
                            root / 'libbeam/tests/fixtures/atom_predicate_slice.erl', root / 'libbeam/tests/fixtures/atom_predicate_reference.erl',
                            root / 'libbeam/tests/fixtures/atom_predicate_literals.S',
                            root / 'libbeam/core/otp/node-sources.json', root / 'libbeam/tests/core_node_test.c',
                            root / 'libbeam/tests/fixtures/node_slice.erl', root / 'libbeam/tests/fixtures/node_locations.S',
                            root / 'libbeam/tests/fixtures/node_reference.erl',
                            root / 'libbeam/core/otp/floor-sources.json', root / 'libbeam/tests/core_floor_test.c',
                            root / 'libbeam/tests/fixtures/floor_slice.erl', root / 'libbeam/tests/fixtures/floor_reference.erl',
                            root / 'libbeam/core/otp/list-tuple-sources.json', root / 'libbeam/tests/fixtures/list_tuple_slice.erl',
                            root / 'libbeam/tests/fixtures/list_tuple_reference.erl',
                            root / 'libbeam/core/otp/tuple-list-sources.json', root / 'libbeam/tests/core_tuple_list_test.c',
                            root / 'libbeam/tests/fixtures/tuple_list_slice.erl', root / 'libbeam/tests/fixtures/tuple_list_reference.erl',
                            root / 'libbeam/core/otp/make-tuple-sources.json', root / 'libbeam/tests/core_make_tuple_test.c',
                            root / 'libbeam/tests/fixtures/make_tuple_slice.erl', root / 'libbeam/tests/fixtures/make_tuple_reference.erl',
                            root / 'libbeam/core/otp/error2-sources.json', root / 'libbeam/tests/core_error2_test.c',
                            root / 'libbeam/tests/fixtures/error2_slice.erl', root / 'libbeam/tests/fixtures/error2_reference.erl',
                            root / 'libbeam/core/otp/process-dict-sources.json', root / 'libbeam/core/bif_dict.c',
                            root / 'libbeam/core/process_dict.h', root / 'libbeam/tests/core_dict_test.c',
                            root / 'libbeam/tests/fixtures/dict_slice.erl', root / 'libbeam/tests/fixtures/dict_locations.S',
                            root / 'libbeam/tests/fixtures/dict_reference.erl',
                            root / 'libbeam/core/otp/module-loaded-sources.json', root / 'libbeam/tests/core_module_loaded_test.c',
                            root / 'libbeam/tests/fixtures/module_loaded_slice.erl', root / 'libbeam/tests/fixtures/module_loaded_reference.erl',
                            root / 'libbeam/tests/fixtures/module_on_load_refused.erl',
                            root / 'libbeam/core/otp/external-sources.json', root / 'libbeam/core/bif_external.c',
                            root / 'libbeam/tests/core_external_decode_test.c', root / 'libbeam/tests/fixtures/external_slice.erl',
                            root / 'libbeam/tests/fixtures/external_reference.erl',
                            root / 'libbeam/core/otp/etf-arena-sources.json', root / 'libbeam/tests/core_etf_arena_test.c',
                            root / 'libbeam/tests/fixtures/etf_arena_reference.erl',
                            root / 'libbeam/core/otp/process-info-sources.json', root / 'libbeam/core/bif_process_info.c',
                            root / 'libbeam/tests/core_process_info_test.c', root / 'libbeam/tests/fixtures/process_info_slice.erl',
                            root / 'libbeam/tests/fixtures/process_info_reference.erl',
                            root / 'libbeam/core/otp/function-exported-sources.json', root / 'libbeam/tests/core_function_exported_test.c',
                            root / 'libbeam/tests/fixtures/function_exported_slice.erl', root / 'libbeam/tests/fixtures/function_exported_reference.erl',
                            root / 'libbeam/core/otp/apply-sources.json', root / 'libbeam/core/apply.c', root / 'libbeam/core/apply.h',
                            root / 'libbeam/tests/core_apply_test.c', root / 'libbeam/tests/fixtures/apply_host.erl',
                            root / 'libbeam/tests/fixtures/apply_target.erl', root / 'libbeam/tests/fixtures/apply_reference.erl',
                            root / 'libbeam/core/otp/binary-predicate-sources.json',
                            root / 'libbeam/tests/fixtures/binary_predicate_slice.erl', root / 'libbeam/tests/fixtures/binary_predicate_reference.erl',
                            root / 'libbeam/tests/fixtures/binary_predicate_literals.S',
                            root / 'libbeam/core/otp/atom-list-sources.json', root / 'libbeam/tests/core_atom_list_test.c',
                            root / 'libbeam/tests/fixtures/atom_list_slice.erl', root / 'libbeam/tests/fixtures/atom_list_reference.erl',
                            root / 'libbeam/tests/fixtures/map_key_slice.erl', root / 'libbeam/tests/fixtures/map_key_reference.erl',
                            root / 'libbeam/core/otp/min-sources.json',
                            root / 'libbeam/tests/fixtures/min_slice.erl', root / 'libbeam/tests/fixtures/min_reference.erl',
                            root / 'libbeam/tests/fixtures/setelement_slice.erl', root / 'libbeam/tests/fixtures/setelement_reference.erl',
                            root / 'libbeam/core/otp/setelement-sources.json',
                            root / 'libbeam/tests/fixtures/max_slice.erl', root / 'libbeam/tests/fixtures/max_reference.erl',
                            root / 'libbeam/core/otp/raise-trace-sources.json', root / 'libbeam/core/otp/max-sources.json',
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
                            root / 'libbeam/tests/core_batch_admission_test.c', root / 'libbeam/tests/api_batch_admission_test.cpp',
                            root / 'libbeam/tests/fixtures/batch_alpha.erl', root / 'libbeam/tests/fixtures/batch_beta.erl',
                            root / 'libbeam/tests/fixtures/batch_unsupported.erl',
                            root / 'libbeam/core/README.md', root / 'libbeam/README.md',
                            root / 'docs/rfds/0004-eager-batch-admission.md',
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
            raise_sources = json.loads((root / 'libbeam/core/otp/raise-trace-sources.json').read_text())
            for filename, expected in raise_sources['sources'].items():
                if hashlib.sha256((root / filename).read_bytes()).hexdigest() != expected:
                    raise RuntimeError(f'Raise trace reference changed: {filename}')
            summary['raise_trace_sources'] = raise_sources
            max_sources = json.loads((root / 'libbeam/core/otp/max-sources.json').read_text())
            for filename, expected in max_sources['sources'].items():
                if hashlib.sha256((root / filename).read_bytes()).hexdigest() != expected:
                    raise RuntimeError(f'Max reference changed: {filename}')
            summary['max_sources'] = max_sources
            setelement_sources = json.loads((root / 'libbeam/core/otp/setelement-sources.json').read_text())
            for filename, expected in setelement_sources['sources'].items():
                if hashlib.sha256((root / filename).read_bytes()).hexdigest() != expected:
                    raise RuntimeError(f'Setelement reference changed: {filename}')
            summary['setelement_sources'] = setelement_sources
            times_sources = json.loads((root / 'libbeam/core/otp/times-sources.json').read_text())
            for filename, expected in times_sources['sources'].items():
                if hashlib.sha256((root / filename).read_bytes()).hexdigest() != expected:
                    raise RuntimeError(f'Multiplication reference changed: {filename}')
            summary['times_sources'] = times_sources
            div_sources = json.loads((root / 'libbeam/core/otp/div-sources.json').read_text())
            for filename, expected in div_sources['sources'].items():
                if hashlib.sha256((root / filename).read_bytes()).hexdigest() != expected:
                    raise RuntimeError(f'Integer division reference changed: {filename}')
            summary['div_sources'] = div_sources
            abs_sources = json.loads((root / 'libbeam/core/otp/abs-sources.json').read_text())
            for filename, expected in abs_sources['sources'].items():
                if hashlib.sha256((root / filename).read_bytes()).hexdigest() != expected:
                    raise RuntimeError(f'Absolute-value reference changed: {filename}')
            summary['abs_sources'] = abs_sources
            negate_sources = json.loads((root / 'libbeam/core/otp/negate-sources.json').read_text())
            for filename, expected in negate_sources['sources'].items():
                if hashlib.sha256((root / filename).read_bytes()).hexdigest() != expected:
                    raise RuntimeError(f'Unary-negation reference changed: {filename}')
            summary['negate_sources'] = negate_sources
            rem_sources = json.loads((root / 'libbeam/core/otp/rem-sources.json').read_text())
            for filename, expected in rem_sources['sources'].items():
                if hashlib.sha256((root / filename).read_bytes()).hexdigest() != expected:
                    raise RuntimeError(f'Remainder reference changed: {filename}')
            summary['rem_sources'] = rem_sources
            list_integer_sources = json.loads((root / 'libbeam/core/otp/list-integer-sources.json').read_text())
            for filename, expected in list_integer_sources['sources'].items():
                if hashlib.sha256((root / filename).read_bytes()).hexdigest() != expected:
                    raise RuntimeError(f'List integer reference changed: {filename}')
            summary['list_integer_sources'] = list_integer_sources
            min_sources = json.loads((root / 'libbeam/core/otp/min-sources.json').read_text())
            for filename, expected in min_sources['sources'].items():
                if hashlib.sha256((root / filename).read_bytes()).hexdigest() != expected:
                    raise RuntimeError(f'Minimum reference changed: {filename}')
            summary['min_sources'] = min_sources
            map_key_sources = json.loads((root / 'libbeam/core/otp/map-key-sources.json').read_text())
            for filename, expected in map_key_sources['sources'].items():
                if hashlib.sha256((root / filename).read_bytes()).hexdigest() != expected:
                    raise RuntimeError(f'Map key reference changed: {filename}')
            summary['map_key_sources'] = map_key_sources
            map_size_sources = json.loads((root / 'libbeam/core/otp/map-size-sources.json').read_text())
            for filename, expected in map_size_sources['sources'].items():
                if hashlib.sha256((root / filename).read_bytes()).hexdigest() != expected:
                    raise RuntimeError(f'Map size reference changed: {filename}')
            summary['map_size_sources'] = map_size_sources
            integer_predicate_sources = json.loads((root / 'libbeam/core/otp/integer-predicate-sources.json').read_text())
            for filename, expected in integer_predicate_sources['sources'].items():
                if hashlib.sha256((root / filename).read_bytes()).hexdigest() != expected:
                    raise RuntimeError(f'Integer predicate reference changed: {filename}')
            summary['integer_predicate_sources'] = integer_predicate_sources
            list_parts_sources = json.loads((root / 'libbeam/core/otp/list-parts-sources.json').read_text())
            for filename, expected in list_parts_sources['sources'].items():
                if hashlib.sha256((root / filename).read_bytes()).hexdigest() != expected:
                    raise RuntimeError(f'List parts reference changed: {filename}')
            summary['list_parts_sources'] = list_parts_sources
            list_predicate_sources = json.loads((root / 'libbeam/core/otp/list-predicate-sources.json').read_text())
            for filename, expected in list_predicate_sources['sources'].items():
                if hashlib.sha256((root / filename).read_bytes()).hexdigest() != expected:
                    raise RuntimeError(f'List predicate reference changed: {filename}')
            summary['list_predicate_sources'] = list_predicate_sources
            not_sources = json.loads((root / 'libbeam/core/otp/not-sources.json').read_text())
            for filename, expected in not_sources['sources'].items():
                if hashlib.sha256((root / filename).read_bytes()).hexdigest() != expected:
                    raise RuntimeError(f'Boolean not reference changed: {filename}')
            summary['not_sources'] = not_sources
            atom_predicate_sources = json.loads((root / 'libbeam/core/otp/atom-predicate-sources.json').read_text())
            for filename, expected in atom_predicate_sources['sources'].items():
                if hashlib.sha256((root / filename).read_bytes()).hexdigest() != expected:
                    raise RuntimeError(f'Atom predicate reference changed: {filename}')
            summary['atom_predicate_sources'] = atom_predicate_sources
            atom_list_sources = json.loads((root / 'libbeam/core/otp/atom-list-sources.json').read_text())
            for filename, expected in atom_list_sources['sources'].items():
                if hashlib.sha256((root / filename).read_bytes()).hexdigest() != expected:
                    raise RuntimeError(f'Atom list reference changed: {filename}')
            summary['atom_list_sources'] = atom_list_sources
            binary_predicate_sources = json.loads((root / 'libbeam/core/otp/binary-predicate-sources.json').read_text())
            for filename, expected in binary_predicate_sources['sources'].items():
                if hashlib.sha256((root / filename).read_bytes()).hexdigest() != expected:
                    raise RuntimeError(f'Binary predicate reference changed: {filename}')
            summary['binary_predicate_sources'] = binary_predicate_sources
            apply_sources = json.loads((root / 'libbeam/core/otp/apply-sources.json').read_text())
            for filename, expected in apply_sources['sources'].items():
                if hashlib.sha256((root / filename).read_bytes()).hexdigest() != expected:
                    raise RuntimeError(f'Apply reference changed: {filename}')
            summary['apply_sources'] = apply_sources
            function_exported_sources = json.loads((root / 'libbeam/core/otp/function-exported-sources.json').read_text())
            for filename, expected in function_exported_sources['sources'].items():
                if hashlib.sha256((root / filename).read_bytes()).hexdigest() != expected:
                    raise RuntimeError(f'Function exported reference changed: {filename}')
            summary['function_exported_sources'] = function_exported_sources
            process_info_sources = json.loads((root / 'libbeam/core/otp/process-info-sources.json').read_text())
            for filename, expected in process_info_sources['sources'].items():
                if hashlib.sha256((root / filename).read_bytes()).hexdigest() != expected:
                    raise RuntimeError(f'Process info reference changed: {filename}')
            summary['process_info_sources'] = process_info_sources
            etf_arena_sources = json.loads((root / 'libbeam/core/otp/etf-arena-sources.json').read_text())
            for filename, expected in etf_arena_sources['sources'].items():
                if hashlib.sha256((root / filename).read_bytes()).hexdigest() != expected:
                    raise RuntimeError(f'ETF arena reference changed: {filename}')
            summary['etf_arena_sources'] = etf_arena_sources
            external_sources = json.loads((root / 'libbeam/core/otp/external-sources.json').read_text())
            for filename, expected in external_sources['sources'].items():
                if hashlib.sha256((root / filename).read_bytes()).hexdigest() != expected:
                    raise RuntimeError(f'External decoding reference changed: {filename}')
            summary['external_sources'] = external_sources
            module_loaded_sources = json.loads((root / 'libbeam/core/otp/module-loaded-sources.json').read_text())
            for filename, expected in module_loaded_sources['sources'].items():
                if hashlib.sha256((root / filename).read_bytes()).hexdigest() != expected:
                    raise RuntimeError(f'Module-loaded reference changed: {filename}')
            summary['module_loaded_sources'] = module_loaded_sources
            process_dict_sources = json.loads((root / 'libbeam/core/otp/process-dict-sources.json').read_text())
            for filename, expected in process_dict_sources['sources'].items():
                if hashlib.sha256((root / filename).read_bytes()).hexdigest() != expected:
                    raise RuntimeError(f'Process dictionary reference changed: {filename}')
            summary['process_dict_sources'] = process_dict_sources
            error2_sources = json.loads((root / 'libbeam/core/otp/error2-sources.json').read_text())
            for filename, expected in error2_sources['sources'].items():
                if hashlib.sha256((root / filename).read_bytes()).hexdigest() != expected:
                    raise RuntimeError(f'Error/2 reference changed: {filename}')
            summary['error2_sources'] = error2_sources
            make_tuple_sources = json.loads((root / 'libbeam/core/otp/make-tuple-sources.json').read_text())
            for filename, expected in make_tuple_sources['sources'].items():
                if hashlib.sha256((root / filename).read_bytes()).hexdigest() != expected:
                    raise RuntimeError(f'Make-tuple reference changed: {filename}')
            summary['make_tuple_sources'] = make_tuple_sources
            tuple_list_sources = json.loads((root / 'libbeam/core/otp/tuple-list-sources.json').read_text())
            for filename, expected in tuple_list_sources['sources'].items():
                if hashlib.sha256((root / filename).read_bytes()).hexdigest() != expected:
                    raise RuntimeError(f'Tuple-list reference changed: {filename}')
            summary['tuple_list_sources'] = tuple_list_sources
            list_tuple_sources = json.loads((root / 'libbeam/core/otp/list-tuple-sources.json').read_text())
            for filename, expected in list_tuple_sources['sources'].items():
                if hashlib.sha256((root / filename).read_bytes()).hexdigest() != expected:
                    raise RuntimeError(f'List-tuple reference changed: {filename}')
            summary['list_tuple_sources'] = list_tuple_sources
            floor_sources = json.loads((root / 'libbeam/core/otp/floor-sources.json').read_text())
            for filename, expected in floor_sources['sources'].items():
                if hashlib.sha256((root / filename).read_bytes()).hexdigest() != expected:
                    raise RuntimeError(f'Floor reference changed: {filename}')
            summary['floor_sources'] = floor_sources
            node_sources = json.loads((root / 'libbeam/core/otp/node-sources.json').read_text())
            for filename, expected in node_sources['sources'].items():
                if hashlib.sha256((root / filename).read_bytes()).hexdigest() != expected:
                    raise RuntimeError(f'Node reference changed: {filename}')
            summary['node_sources'] = node_sources
            capability_sources = json.loads((root / 'libbeam/core/otp/capability-denial-sources.json').read_text())
            for filename, expected in capability_sources['sources'].items():
                if hashlib.sha256((root / filename).read_bytes()).hexdigest() != expected:
                    raise RuntimeError(f'Capability provenance changed: {filename}')
            summary['capability_sources'] = capability_sources
            run('capability-policy-inventory', [sys.executable, '-B', '-m', 'unittest', 'discover',
                '-s', root / 'libbeam/tools', '-p', 'test_audit_bif_batch.py', '-v'])
            for filename, expected in {
                'beam/erts/emulator/beam/beam_file.c': '52708b4a8fa136d005d962599914b095e01b7fc311c1748309d3682d4ff56d0d',
                'beam/erts/emulator/beam/beam_file.h': '82a4760ac4c3b69d385a26c4c004103ca996960621d5f9bb4f62529e3b4f3718',
                'beam/erts/emulator/beam/erl_term.h': '8e016126fb7a1bc1db8d31111601c0bea21d72207211bf91e1f3c127e943b5c2',
                'beam/erts/emulator/beam/atom.c': 'a83d7789ac31e475e4c006cb379f9cf74634b251d1c350358e321c60412610a8',
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
            run('memory-erlc', [args.erlc, '-o', fixture_dir, root / 'libbeam/tests/fixtures/memory_slice.erl'])
            memory_fixture = fixture_dir / 'memory_slice.beam'
            run('work-erlc', [args.erlc, '-o', fixture_dir, root / 'libbeam/tests/fixtures/work_slice.erl'])
            work_fixture = fixture_dir / 'work_slice.beam'
            run('core-work-runtime', [out / 'build/core_work_runtime_test', work_fixture])
            run('api-work-runtime', [out / 'build/api_work_test', work_fixture])
            run('api-memory-execution', [out / 'build/api_memory_test', memory_fixture])
            run('core-memory-gc', [out / 'build/core_memory_test', memory_fixture])
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
            application_input = out / 'application-input.bin'
            application_input.write_bytes(b'generic artifact admission witness')
            run('application-mode-supported', [out / 'build/run_beam', '--entry',
                 'async_slice', 'identity', '--call-file', application_input, '--expect-file', application_input,
                 '--isolate', async_fixture, '--isolate', async_fixture])
            run('runner-invalid-cli', [out / 'build/run_beam', '--dependency', async_fixture], accepted=(2,))
            run('runner-closure', [out / 'build/run_beam', '--entry', 'fun_slice', 'nested',
                 '--call', 'captured', '--expect', 'captured', '--isolate', fun_fixture])
            run('runner-shapes', [out / 'build/run_beam', '--entry', 'shape_slice', 'list_binary',
                 '--call', 'shape', '--expect', 'shape', '--isolate', shape_fixture])
            run('runner-decimal', [out / 'build/run_beam', '--entry', 'decimal_slice', 'negative',
                 '--call', '', '--expect', '-18446744073709551617', '--isolate', decimal_fixture])
            run('erlc', [args.erlc, '-o', fixture_dir, root / 'libbeam/tests/fixtures/first_slice.erl'])
            fixture = fixture_dir / 'first_slice.beam'
            run('peer-erlc', [args.erlc, '-o', fixture_dir, root / 'libbeam/tests/fixtures/code_peer.erl'])
            peer_fixture = fixture_dir / 'code_peer.beam'
            run('runner-dependency-groups', [out / 'build/run_beam', '--entry', 'first_slice', 'identity',
                 '--call', 'dependency', '--expect', 'dependency', '--isolate', peer_fixture,
                 '--dependency', fixture, '--isolate', peer_fixture, '--dependency', fixture])
            missing = run('runner-missing-dependency', [out / 'build/run_beam', '--entry', 'first_slice', 'identity',
                 '--call', 'dependency', '--isolate', peer_fixture], accepted=(1,))
            if 'stage=unresolved import, import_index=' not in missing or 'mfa="first_slice":"literal"/0' not in missing:
                raise RuntimeError('missing dependency diagnostic lost its owned MFA/location')
            run('refused-exception-erlc', [args.erlc, '-o', fixture_dir, root / 'libbeam/tests/fixtures/refused_exception.erl'])
            refused = run('runner-unused-unsupported', [out / 'build/run_beam', '--entry', 'refused_exception', 'identity',
                 '--call', 'must-not-run', '--isolate', fixture_dir / 'refused_exception.beam'], accepted=(1,))
            if 'stage=try' not in refused or 'bytes=' in refused:
                raise RuntimeError('unused exception function did not fail closed with an opcode diagnostic')
            run('node-reference-erlc', [args.erlc, '-o', fixture_dir,
                root / 'libbeam/tests/fixtures/node_slice.erl', root / 'libbeam/tests/fixtures/node_locations.S',
                root / 'libbeam/tests/fixtures/node_reference.erl'])
            run('node-reference', [args.erl, '-noshell', '-pa', fixture_dir,
                '-s', 'node_reference', 'run', '-s', 'init', 'stop'])
            run('floor-reference-erlc', [args.erlc, '-o', fixture_dir,
                root / 'libbeam/tests/fixtures/floor_slice.erl', root / 'libbeam/tests/fixtures/floor_reference.erl'])
            run('floor-reference', [args.erl, '-noshell', '-pa', fixture_dir,
                '-s', 'floor_reference', 'run', '-s', 'init', 'stop'])
            run('list-tuple-reference-erlc', [args.erlc, '-o', fixture_dir,
                root / 'libbeam/tests/fixtures/list_tuple_slice.erl', root / 'libbeam/tests/fixtures/list_tuple_reference.erl'])
            run('list-tuple-reference', [args.erl, '-noshell', '-pa', fixture_dir,
                '-s', 'list_tuple_reference', 'run', '-s', 'init', 'stop'])
            run('tuple-list-reference-erlc', [args.erlc, '-o', fixture_dir,
                root / 'libbeam/tests/fixtures/tuple_list_slice.erl', root / 'libbeam/tests/fixtures/tuple_list_reference.erl'])
            run('tuple-list-reference', [args.erl, '-noshell', '-pa', fixture_dir,
                '-s', 'tuple_list_reference', 'run', '-s', 'init', 'stop'])
            run('make-tuple-reference-erlc', [args.erlc, '-o', fixture_dir,
                root / 'libbeam/tests/fixtures/make_tuple_slice.erl', root / 'libbeam/tests/fixtures/make_tuple_reference.erl'])
            run('make-tuple-reference', [args.erl, '-noshell', '-pa', fixture_dir,
                '-s', 'make_tuple_reference', 'run', '-s', 'init', 'stop'])
            run('error2-reference-erlc', [args.erlc, '-o', fixture_dir,
                root / 'libbeam/tests/fixtures/error2_slice.erl', root / 'libbeam/tests/fixtures/error2_reference.erl'])
            run('error2-reference', [args.erl, '-noshell', '-pa', fixture_dir,
                '-s', 'error2_reference', 'run', '-s', 'init', 'stop'])
            run('dict-reference-erlc', [args.erlc, '-o', fixture_dir,
                root / 'libbeam/tests/fixtures/dict_slice.erl', root / 'libbeam/tests/fixtures/dict_locations.S',
                root / 'libbeam/tests/fixtures/dict_reference.erl'])
            run('dict-reference', [args.erl, '-noshell', '-pa', fixture_dir,
                '-s', 'dict_reference', 'run', '-s', 'init', 'stop'])
            run('module-loaded-reference-erlc', [args.erlc, '-o', fixture_dir,
                root / 'libbeam/tests/fixtures/module_loaded_slice.erl', root / 'libbeam/tests/fixtures/module_loaded_reference.erl',
                root / 'libbeam/tests/fixtures/apply_target.erl'])
            run('module-loaded-reference', [args.erl, '-noshell', '-pa', fixture_dir,
                '-s', 'module_loaded_reference', 'run', '-s', 'init', 'stop'])
            run('external-reference-erlc', [args.erlc, '-o', fixture_dir,
                root / 'libbeam/tests/fixtures/external_slice.erl', root / 'libbeam/tests/fixtures/external_reference.erl'])
            run('external-reference', [args.erl, '-noshell', '-pa', fixture_dir,
                '-s', 'external_reference', 'run', '-s', 'init', 'stop'])
            run('etf-arena-reference-erlc', [args.erlc, '-o', fixture_dir,
                root / 'libbeam/tests/fixtures/etf_arena_reference.erl'])
            run('etf-arena-reference', [args.erl, '-noshell', '-pa', fixture_dir,
                '-s', 'etf_arena_reference', 'run', '-s', 'init', 'stop'])
            run('process-info-reference-erlc', [args.erlc, '-o', fixture_dir,
                root / 'libbeam/tests/fixtures/process_info_slice.erl', root / 'libbeam/tests/fixtures/process_info_reference.erl'])
            run('process-info-reference', [args.erl, '-noshell', '-pa', fixture_dir,
                '-s', 'process_info_reference', 'run', '-s', 'init', 'stop'])
            run('function-exported-reference-erlc', [args.erlc, '-o', fixture_dir,
                root / 'libbeam/tests/fixtures/function_exported_slice.erl', root / 'libbeam/tests/fixtures/function_exported_reference.erl',
                root / 'libbeam/tests/fixtures/apply_target.erl'])
            run('function-exported-reference', [args.erl, '-noshell', '-pa', fixture_dir,
                '-s', 'function_exported_reference', 'run', '-s', 'init', 'stop'])
            run('apply-reference-erlc', [args.erlc, '-o', fixture_dir,
                root / 'libbeam/tests/fixtures/apply_host.erl', root / 'libbeam/tests/fixtures/apply_target.erl',
                root / 'libbeam/tests/fixtures/apply_reference.erl'])
            run('apply-reference', [args.erl, '-noshell', '-pa', fixture_dir,
                '-s', 'apply_reference', 'run', '-s', 'init', 'stop'])
            run('binary-predicate-reference-erlc', [args.erlc, '-o', fixture_dir,
                root / 'libbeam/tests/fixtures/binary_predicate_slice.erl', root / 'libbeam/tests/fixtures/binary_predicate_literals.S',
                root / 'libbeam/tests/fixtures/binary_predicate_reference.erl'])
            run('binary-predicate-reference', [args.erl, '-noshell', '-pa', fixture_dir,
                '-s', 'binary_predicate_reference', 'run', '-s', 'init', 'stop'])
            run('atom-list-reference-erlc', [args.erlc, '-o', fixture_dir,
                root / 'libbeam/tests/fixtures/atom_list_slice.erl', root / 'libbeam/tests/fixtures/atom_list_reference.erl'])
            run('atom-list-reference', [args.erl, '-noshell', '-pa', fixture_dir,
                '-s', 'atom_list_reference', 'run', '-s', 'init', 'stop'])
            run('atom-predicate-reference-erlc', [args.erlc, '-o', fixture_dir,
                root / 'libbeam/tests/fixtures/atom_predicate_slice.erl', root / 'libbeam/tests/fixtures/atom_predicate_literals.S',
                root / 'libbeam/tests/fixtures/atom_predicate_reference.erl'])
            run('atom-predicate-reference', [args.erl, '-noshell', '-pa', fixture_dir,
                '-s', 'atom_predicate_reference', 'run', '-s', 'init', 'stop'])
            run('not-reference-erlc', [args.erlc, '-o', fixture_dir,
                root / 'libbeam/tests/fixtures/not_slice.erl', root / 'libbeam/tests/fixtures/not_guard_slice.S',
                root / 'libbeam/tests/fixtures/not_reference.erl'])
            run('not-reference', [args.erl, '-noshell', '-pa', fixture_dir, '-s', 'not_reference', 'run', '-s', 'init', 'stop'])
            run('list-predicate-reference-erlc', [args.erlc, '-o', fixture_dir,
                root / 'libbeam/tests/fixtures/list_predicate_slice.erl', root / 'libbeam/tests/fixtures/list_predicate_literals.S',
                root / 'libbeam/tests/fixtures/list_predicate_reference.erl'])
            run('list-predicate-reference', [args.erl, '-noshell', '-pa', fixture_dir,
                '-s', 'list_predicate_reference', 'run', '-s', 'init', 'stop'])
            run('list-parts-reference-erlc', [args.erlc, '-o', fixture_dir,
                root / 'libbeam/tests/fixtures/list_parts_slice.erl', root / 'libbeam/tests/fixtures/list_parts_reference.erl'])
            run('list-parts-reference', [args.erl, '-noshell', '-pa', fixture_dir,
                '-s', 'list_parts_reference', 'run', '-s', 'init', 'stop'])
            run('integer-predicate-reference-erlc', [args.erlc, '-o', fixture_dir,
                root / 'libbeam/tests/fixtures/integer_predicate_slice.erl', root / 'libbeam/tests/fixtures/integer_predicate_literals.S',
                root / 'libbeam/tests/fixtures/integer_predicate_reference.erl'])
            run('integer-predicate-reference', [args.erl, '-noshell', '-pa', fixture_dir,
                '-s', 'integer_predicate_reference', 'run', '-s', 'init', 'stop'])
            run('map-key-reference-erlc', [args.erlc, '-o', fixture_dir,
                root / 'libbeam/tests/fixtures/map_key_slice.erl', root / 'libbeam/tests/fixtures/map_key_reference.erl'])
            run('map-key-reference', [args.erl, '-noshell', '-pa', fixture_dir,
                '-s', 'map_key_reference', 'run', '-s', 'init', 'stop'])
            run('min-reference-erlc', [args.erlc, '-o', fixture_dir,
                root / 'libbeam/tests/fixtures/min_slice.erl', root / 'libbeam/tests/fixtures/min_reference.erl'])
            run('min-reference', [args.erl, '-noshell', '-pa', fixture_dir,
                '-s', 'min_reference', 'run', '-s', 'init', 'stop'])
            run('list-integer-reference-erlc', [args.erlc, '-o', fixture_dir,
                root / 'libbeam/tests/fixtures/list_integer_slice.erl', root / 'libbeam/tests/fixtures/list_integer_reference.erl'])
            run('list-integer-reference', [args.erl, '-noshell', '-pa', fixture_dir,
                '-s', 'list_integer_reference', 'run', '-s', 'init', 'stop'])
            run('rem-reference-erlc', [args.erlc, '-o', fixture_dir,
                root / 'libbeam/tests/fixtures/rem_slice.erl', root / 'libbeam/tests/fixtures/rem_reference.erl'])
            run('rem-reference', [args.erl, '-noshell', '-pa', fixture_dir, '-eval',
                'rem_reference:run('+json.dumps(str(out / 'build/fixtures/div_vectors.txt'))+'),halt().'])
            run('negate-reference-erlc', [args.erlc, '-o', fixture_dir,
                root / 'libbeam/tests/fixtures/negate_slice.erl', root / 'libbeam/tests/fixtures/negate_reference.erl'])
            run('negate-reference', [args.erl, '-noshell', '-pa', fixture_dir,
                '-s', 'negate_reference', 'run', '-s', 'init', 'stop'])
            run('abs-reference-erlc', [args.erlc, '-o', fixture_dir,
                root / 'libbeam/tests/fixtures/abs_slice.erl', root / 'libbeam/tests/fixtures/abs_reference.erl'])
            run('abs-reference', [args.erl, '-noshell', '-pa', fixture_dir,
                '-s', 'abs_reference', 'run', '-s', 'init', 'stop'])
            run('div-reference-erlc', [args.erlc, '-o', fixture_dir,
                root / 'libbeam/tests/fixtures/div_slice.erl', root / 'libbeam/tests/fixtures/div_reference.erl'])
            run('div-reference', [args.erl, '-noshell', '-pa', fixture_dir, '-eval',
                'div_reference:run('+json.dumps(str(out / 'build/fixtures/div_vectors.txt'))+'),halt().'])
            run('times-reference-erlc', [args.erlc, '-o', fixture_dir,
                root / 'libbeam/tests/fixtures/times_slice.erl', root / 'libbeam/tests/fixtures/times_reference.erl'])
            run('times-reference', [args.erl, '-noshell', '-pa', fixture_dir,
                '-s', 'times_reference', 'run', '-s', 'init', 'stop'])
            run('setelement-reference-erlc', [args.erlc, '-o', fixture_dir,
                root / 'libbeam/tests/fixtures/setelement_slice.erl', root / 'libbeam/tests/fixtures/setelement_reference.erl'])
            run('setelement-reference', [args.erl, '-noshell', '-pa', fixture_dir,
                '-s', 'setelement_reference', 'run', '-s', 'init', 'stop'])
            run('max-reference-erlc', [args.erlc, '-o', fixture_dir,
                root / 'libbeam/tests/fixtures/max_slice.erl', root / 'libbeam/tests/fixtures/max_reference.erl'])
            run('max-reference', [args.erl, '-noshell', '-pa', fixture_dir,
                '-s', 'max_reference', 'run', '-s', 'init', 'stop'])
            run('raise-reference-erlc', [args.erlc, '-o', fixture_dir,
                root / 'libbeam/tests/fixtures/raise_trace_reference.erl', root / 'libbeam/tests/fixtures/raise_slice.erl'])
            run('raise-reference', [args.erl, '-noshell', '-pa', fixture_dir,
                '-s', 'raise_trace_reference', 'run', '-s', 'raise_trace_reference', 'execution', '-s', 'init', 'stop'])
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
            trace_reference = out / 'monitor-trace-reference'
            trace_reference.mkdir()
            run('monitor-trace-reference-erlc', [args.erlc, '+no_line_info', '-o', trace_reference,
                 root / 'libbeam/tests/fixtures/fun_slice.erl'])
            run('monitor-trace-reference', [args.erl, '-noshell', '-pa', trace_reference, '-eval',
                 'lists:foreach(fun(F) -> <<"monitored reply">> = apply(fun_slice,F,[<<"monitored reply">>]) end,'
                 '[monitored_error,monitored_error_info,monitored_bif_error,monitored_error_call,monitored_empty_info]), '
                 'io:format("MONITOR_TRACE_REFERENCE_OK~n"), halt().'])
            run('stateful-world-execution', [out / 'build/core_world_test', async_fixture, fun_fixture, *probes])
            run('runner-stateful', [out / 'build/run_beam', '--entry', 'probe', 'request',
                 '--boot', 'boot', '', '--expect-boot', 'A:ready', '--call', 'next', '--expect', 'A:1',
                 '--call', 'next', '--expect', 'A:2', '--call', 'read', '--expect', 'A:2',
                 '--isolate', probes[0], '--isolate', probes[0]])
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
            execution_sources_c = program_sources + ['beam_transform','beam_emit','beam_verify','code','engine','executor','world','work_budget','md5','process','heap','capability','bif_info','term_compare','big','bif_numeric','bif_binary','bif_map','map_hamt','map_runtime','decimal','bif_decimal','bit_copy','bit_writer','bit_float','writable','binary_error','binary_size','binary_vheap','binary_build','exception','stacktrace','raise_trace','bif_exception','pid_table','bif_process','bif_process_info','bif_external','bif_dict','fun','apply','message','receive','registry','receive_marker','monitor','preloaded','binary_match']
            run('program-ubsan-compile', ['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                '-fsanitize=undefined', '-fno-sanitize-recover=all', '-Ilibbeam/core',
                '-I'+str(out / 'build/generated/atoms'), '-I'+str(out / 'build/generated/opcodes'),
                *[f'libbeam/core/{s}.c' for s in execution_sources_c], 'libbeam/tests/core_beam_program_test.c',
                '-pthread', '-lz', '-lm', '-o', out / 'program-ubsan'])
            for i, image in enumerate([fixture, binary_fixture, *probes]):
                run(f'program-ubsan-{i}', [out / 'program-ubsan', image])
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
                'core_engine_test', 'core_executor_test', 'core_batch_admission_test', 'api_batch_admission_test', 'core_world_test', 'core_local_test', 'core_compare_test', 'core_shape_test', 'core_shape_verify_test', 'core_big_test', 'core_add_test', 'core_decimal_test', 'core_bit_copy_test', 'core_bit_writer_test', 'core_bit_segment_test', 'core_flatmap_test', 'core_binary_transform_test', 'core_binary_emit_test', 'core_binary_size_test', 'core_exception_test', 'core_binary_flow_test', 'core_binary_vheap_test', 'core_writable_init_test', 'api_writable_init_test', 'core_binary_build_test', 'api_binary_build_test', 'core_pid_test', 'api_engine_test', 'api_world_test', 'api_memory_test', 'core_memory_test', 'core_work_test', 'core_work_runtime_test', 'api_work_test', 'core_alloc_test', 'engine_lifecycle', 'two_isolates', 'run_beam'])
            run('profile-ubsan-build', ['cmake', '--build', out / 'engine-ubsan', '--target',
                'core_hamt_test', 'core_map_test', 'core_map_execution_test', 'core_external_fun_test', 'core_raise_trace_test',
                'core_raise_execution_test', 'core_raise_work_test', 'core_max_test', 'core_setelement_test', 'core_bif_work_test', 'core_times_test',  'core_div_test', 'core_abs_test', 'core_negate_test', 'core_list_integer_test', 'core_map_key_test', 'core_integer_predicate_test', 'core_list_parts_test', 'core_not_test', 'core_atom_list_test', 'core_apply_test', 'core_function_exported_test', 'core_process_info_test', 'core_etf_arena_test', 'core_external_decode_test', 'core_module_loaded_test', 'core_dict_test', 'core_error2_test', 'core_make_tuple_test', 'core_tuple_list_test', 'core_list_tuple_test', 'core_floor_test', 'core_node_test', 'core_capability_test', 'api_capability_test'])
            run('profile-ubsan-test', ['ctest', '--test-dir', out / 'engine-ubsan', '--output-on-failure',
                '-R', '^(core_hamt_runtime|core_map_runtime|core_map_execution|core_external_fun_runtime|core_raise_trace_prepare|core_raise_execution|core_raise_work|core_max_execution|core_setelement_execution|core_setelement_work|core_times_execution|core_times_work|core_div_execution|core_div_work|core_abs_execution|core_abs_work|core_negate_execution|core_negate_work|core_rem_execution|core_rem_work|core_list_integer_execution|core_list_integer_work|core_min_execution|core_min_work|core_map_key_execution|core_map_key_work|core_integer_predicate_execution|core_integer_predicate_work|core_list_parts_execution|core_list_parts_work|core_list_predicate_execution|core_list_predicate_work|core_not_execution|core_not_work|core_atom_predicate_execution|core_atom_predicate_work|core_atom_list_execution|core_atom_list_work|core_binary_predicate_execution|core_binary_predicate_work|core_apply_components|core_apply_work|core_function_exported_execution|core_function_exported_work|core_process_info_execution|core_process_info_work|core_etf_arena|core_external_decode_execution|core_external_decode_work|core_module_loaded_execution|core_module_loaded_work|core_dict_execution|core_dict_work|core_error2_execution|core_error2_work|core_make_tuple_execution|core_make_tuple_work|core_tuple_list_execution|core_tuple_list_work|core_list_tuple_execution|core_list_tuple_work|core_floor_execution|core_floor_work|core_node_execution|core_node_work|core_capability_execution|api_capability_execution)$'])
            run('engine-ubsan-test', ['ctest', '--test-dir', out / 'engine-ubsan', '--output-on-failure',
                '-R', '^(core_batch_admission|api_batch_admission|beam_runner_eager_dependency_groups|core_engine_lifetime|core_executor_native_lifetime|core_world_lifetime|core_exact_comparison|core_shape_proof|core_big_components|core_decimal_components|core_bit_copy_components|core_bit_writer_components|core_bit_segment_components|core_flatmap_components|core_binary_transform_components|core_binary_emit_components|core_binary_size_components|core_exception_components|core_binary_flow_proof|core_binary_vheap_policy|core_writable_init_gate|core_binary_build_gate|core_pid_gate|api_engine_lifetime|api_memory_gate|core_allocation_lifetime|core_memory_scope|core_work_ledger|core_work_runtime_gate|api_work_gate|engine_lifecycle_acceptance)$'])
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
            run('batch-admission-ubsan', [out / 'engine-ubsan/core_batch_admission_test',
                 out / 'engine-ubsan/fixtures/batch_admission/batch_alpha.beam',
                 out / 'engine-ubsan/fixtures/batch_admission/batch_beta.beam',
                 out / 'engine-ubsan/fixtures/batch_admission/batch_unsupported.beam'])
            run('api-batch-admission-ubsan', [out / 'engine-ubsan/api_batch_admission_test',
                 out / 'engine-ubsan/fixtures/batch_admission/batch_alpha.beam',
                 out / 'engine-ubsan/fixtures/batch_admission/batch_beta.beam',
                 out / 'engine-ubsan/fixtures/batch_admission/batch_unsupported.beam'])
            run('executor-ubsan', [out / 'engine-ubsan/core_executor_test', async_fixture])
            run('world-ubsan', [out / 'engine-ubsan/core_world_test', async_fixture, fun_fixture, *probes])
            run('runner-ubsan', [out / 'engine-ubsan/run_beam', '--entry', 'probe', 'request',
                 '--boot', 'boot', '', '--expect-boot', 'A:ready', '--call', 'next', '--expect', 'A:1',
                 '--call', 'read', '--expect', 'A:1', '--isolate', probes[0], '--isolate', probes[0]])
            run('local-ubsan', [out / 'engine-ubsan/core_local_test', local_fixture])
            run('compare-ubsan', [out / 'engine-ubsan/core_compare_test', compare_fixture])
            run('shape-ubsan', [out / 'engine-ubsan/core_shape_test', shape_fixture])
            run('add-ubsan', [out / 'engine-ubsan/core_add_test', add_fixture])
            run('decimal-ubsan', [out / 'engine-ubsan/core_decimal_test', decimal_fixture])
            run('pid-ubsan', [out / 'engine-ubsan/core_pid_test', pid_fixture, fun_target_fixture, fun_fixture])
            run('api-memory-ubsan', [out / 'engine-ubsan/api_memory_test', memory_fixture])
            run('core-memory-gc-ubsan', [out / 'engine-ubsan/core_memory_test', memory_fixture])
            run('core-work-runtime-ubsan', [out / 'engine-ubsan/core_work_runtime_test', work_fixture])
            run('api-work-runtime-ubsan', [out / 'engine-ubsan/api_work_test', work_fixture])
            run('api-world-ubsan', [out / 'engine-ubsan/api_world_test', async_fixture, large_fixture, local_fixture, compare_fixture, shape_fixture, add_fixture, decimal_fixture, pid_fixture, fun_fixture])
            run('release-configure', ['cmake', '-S', root / 'libbeam', '-B', out / 'release',
                                     '-DBUILD_TESTING=ON', '-DCMAKE_BUILD_TYPE=Release'])
            run('release-build', ['cmake', '--build', out / 'release'])
            run('release-ctest', ['ctest', '--test-dir', out / 'release', '--output-on-failure'])
            run('api-memory-release', [out / 'release/api_memory_test', memory_fixture])
            run('core-memory-gc-release', [out / 'release/core_memory_test', memory_fixture])
            run('core-work-runtime-release', [out / 'release/core_work_runtime_test', work_fixture])
            run('api-work-runtime-release', [out / 'release/api_work_test', work_fixture])
            run('runner-release', [out / 'release/run_beam', '--entry', 'probe', 'request',
                 '--boot', 'boot', '', '--expect-boot', 'A:ready', '--call', 'next', '--expect', 'A:1',
                 '--call', 'read', '--expect', 'A:1', '--isolate', probes[0], '--isolate', probes[0]])
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
            run('two-isolates-observed', [out / 'build/two_isolates', *probes])
            run('two-isolates-release', [out / 'release/two_isolates', *probes])
            run('two-isolates-ubsan', [out / 'engine-ubsan/two_isolates', *probes])
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
