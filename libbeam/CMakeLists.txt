# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Leandro Ostera <leandro@ostera.io>
cmake_minimum_required(VERSION 3.20)
project(libbeam LANGUAGES C CXX)

# RFD 0003 additive C core: the one production execution/lifetime substrate.
# Historical ERTS packages are available only to explicit diagnostic targets.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_package(Perl REQUIRED)
set(LIBBEAM_ATOM_GEN ${CMAKE_CURRENT_BINARY_DIR}/generated/atoms)
add_custom_command(
    OUTPUT ${LIBBEAM_ATOM_GEN}/lb_atoms_generated.h ${LIBBEAM_ATOM_GEN}/lb_atoms_generated.inc ${LIBBEAM_ATOM_GEN}/lb_bif_ids_generated.h
    COMMAND ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_atoms.py
        --perl ${PERL_EXECUTABLE} --output ${LIBBEAM_ATOM_GEN}
    DEPENDS tools/generate_atoms.py tools/otp/make_tables core/otp/atom.names core/otp/bif.tab
    VERBATIM)
add_custom_target(libbeam_atom_definitions DEPENDS
    ${LIBBEAM_ATOM_GEN}/lb_atoms_generated.h ${LIBBEAM_ATOM_GEN}/lb_atoms_generated.inc ${LIBBEAM_ATOM_GEN}/lb_bif_ids_generated.h)
find_program(LIBBEAM_ERLC erlc REQUIRED)
add_custom_command(
    OUTPUT ${LIBBEAM_ATOM_GEN}/lb_preloaded_generated.inc
    COMMAND ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_preloaded.py
        --erlc ${LIBBEAM_ERLC} --output ${LIBBEAM_ATOM_GEN}
    DEPENDS tools/generate_preloaded.py ../beam/erts/preloaded/src/erlang.erl
    VERBATIM)
add_custom_target(libbeam_preloaded_definitions DEPENDS ${LIBBEAM_ATOM_GEN}/lb_preloaded_generated.inc)
add_dependencies(libbeam_atom_definitions libbeam_preloaded_definitions)
set(LIBBEAM_OPCODE_GEN ${CMAKE_CURRENT_BINARY_DIR}/generated/opcodes)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS core/otp/opcodes/manifest.json)
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/core/otp/opcodes/manifest.json LIBBEAM_OPCODE_MANIFEST)
string(JSON LIBBEAM_OPCODE_COUNT LENGTH "${LIBBEAM_OPCODE_MANIFEST}" inputs)
math(EXPR LIBBEAM_OPCODE_LAST "${LIBBEAM_OPCODE_COUNT}-1")
set(LIBBEAM_OPCODE_INPUTS core/otp/opcodes/manifest.json tools/otp/beam_makeops tools/generate_opcodes.py tools/project_transform.py tools/project_dispatch.py)
foreach(i RANGE ${LIBBEAM_OPCODE_LAST})
    string(JSON input GET "${LIBBEAM_OPCODE_MANIFEST}" inputs ${i} input)
    list(APPEND LIBBEAM_OPCODE_INPUTS core/otp/opcodes/${input})
endforeach()
add_custom_command(
    OUTPUT ${LIBBEAM_OPCODE_GEN}/lb_opcodes_generated.h ${LIBBEAM_OPCODE_GEN}/lb_opcodes_generated.inc ${LIBBEAM_OPCODE_GEN}/lb_transform_generated.inc
        ${LIBBEAM_OPCODE_GEN}/lb_dispatch_generated.inc ${LIBBEAM_OPCODE_GEN}/lb_dispatch_support.inc
    COMMAND ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_opcodes.py
        --perl ${PERL_EXECUTABLE} --output ${LIBBEAM_OPCODE_GEN}
    DEPENDS ${LIBBEAM_OPCODE_INPUTS}
    VERBATIM)
add_custom_target(libbeam_opcode_definitions DEPENDS
    ${LIBBEAM_OPCODE_GEN}/lb_opcodes_generated.h ${LIBBEAM_OPCODE_GEN}/lb_opcodes_generated.inc ${LIBBEAM_OPCODE_GEN}/lb_transform_generated.inc
    ${LIBBEAM_OPCODE_GEN}/lb_dispatch_generated.inc ${LIBBEAM_OPCODE_GEN}/lb_dispatch_support.inc)
find_package(ZLIB REQUIRED)
find_package(Threads REQUIRED)
if(NOT UNIX)
    message(FATAL_ERROR "The admitted asynchronous executor currently requires POSIX threads")
endif()
add_library(libbeam_core STATIC core/alloc.c core/utf8.c core/beam_image.c core/atoms.c
    core/opcodes.c core/beam_reader.c core/beam_program.c core/beam_decode.c
    core/beam_metadata.c core/beam_literals.c core/beam_select.c core/binary.c core/binary_match.c core/map_hamt.c core/map_runtime.c core/bif_map.c core/writable.c core/binary_error.c core/binary_size.c core/binary_vheap.c core/binary_build.c core/exception.c core/stacktrace.c core/raise_trace.c core/bif_exception.c core/pid_table.c core/bif_process.c core/bif_process_info.c core/bif_external.c core/bif_dict.c core/fun.c core/apply.c core/message.c core/receive.c core/registry.c core/receive_marker.c core/monitor.c core/preloaded.c core/bit_copy.c core/bit_writer.c core/bit_float.c core/term_compare.c
    core/beam_transform.c core/beam_emit.c core/beam_verify.c core/code.c core/engine.c core/executor.c core/world.c core/work_budget.c core/md5.c core/process.c core/heap.c core/capability.c core/bif_info.c core/bif_binary.c core/big.c core/bif_numeric.c core/decimal.c core/bif_decimal.c)
add_dependencies(libbeam_core libbeam_atom_definitions libbeam_opcode_definitions)
target_link_libraries(libbeam_core PUBLIC ZLIB::ZLIB Threads::Threads m)
target_include_directories(libbeam_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/core ${LIBBEAM_ATOM_GEN} ${LIBBEAM_OPCODE_GEN})
target_compile_features(libbeam_core PUBLIC c_std_11)
set_target_properties(libbeam_core PROPERTIES C_EXTENSIONS OFF)

# Thin C++ host adapter, always backed by the additive C runtime.
add_library(libbeam STATIC src/engine.cpp)
add_library(libbeam::libbeam ALIAS libbeam)
target_include_directories(libbeam PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(libbeam PUBLIC cxx_std_17)
target_link_libraries(libbeam PRIVATE libbeam_core)
set_target_properties(libbeam PROPERTIES CXX_EXTENSIONS OFF OUTPUT_NAME beam)

set(LIBBEAM_ERTS_PACKAGE "" CACHE FILEPATH "runtime-package.cmake exported by export_runtime_package.py")
if(LIBBEAM_ERTS_PACKAGE)
    include("${LIBBEAM_ERTS_PACKAGE}")
    if(NOT TARGET libbeam_erts)
        message(FATAL_ERROR "Runtime package must define libbeam_erts")
    endif()
    # Explicit process-lifetime diagnostic, not an Engine/Isolate acceptance test.
    add_executable(engine_start_probe examples/engine_start_probe.cpp)
    target_link_libraries(engine_start_probe PRIVATE libbeam_erts)
    add_executable(engine_prepare_probe examples/engine_prepare_probe.cpp)
    target_link_libraries(engine_prepare_probe PRIVATE libbeam::libbeam libbeam_erts)
endif()

# Unchanged acceptance target; success requires real create/shutdown/recreate.
add_executable(engine_lifecycle examples/engine_lifecycle.cpp)
target_link_libraries(engine_lifecycle PRIVATE libbeam::libbeam)

add_executable(two_isolates examples/two_isolates.cpp)
target_link_libraries(two_isolates PRIVATE libbeam::libbeam)

# Generic host example; fixture cases below use exactly this execution path.
add_executable(run_beam examples/run_beam.cpp)
target_link_libraries(run_beam PRIVATE libbeam::libbeam)

include(CTest)
if(BUILD_TESTING)
    set(LIBBEAM_BATCH_FIXTURE_DIR ${CMAKE_CURRENT_BINARY_DIR}/fixtures/batch_admission)
    set(LIBBEAM_BATCH_ALPHA ${LIBBEAM_BATCH_FIXTURE_DIR}/batch_alpha.beam)
    set(LIBBEAM_BATCH_BETA ${LIBBEAM_BATCH_FIXTURE_DIR}/batch_beta.beam)
    set(LIBBEAM_BATCH_UNSUPPORTED ${LIBBEAM_BATCH_FIXTURE_DIR}/batch_unsupported.beam)
    add_custom_command(OUTPUT ${LIBBEAM_BATCH_ALPHA} ${LIBBEAM_BATCH_BETA} ${LIBBEAM_BATCH_UNSUPPORTED}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${LIBBEAM_BATCH_FIXTURE_DIR}
        COMMAND ${LIBBEAM_ERLC} -o ${LIBBEAM_BATCH_FIXTURE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/batch_alpha.erl
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/batch_beta.erl
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/batch_unsupported.erl
        DEPENDS tests/fixtures/batch_alpha.erl tests/fixtures/batch_beta.erl
            tests/fixtures/batch_unsupported.erl
        VERBATIM)
    add_custom_target(libbeam_batch_fixtures DEPENDS
        ${LIBBEAM_BATCH_ALPHA} ${LIBBEAM_BATCH_BETA} ${LIBBEAM_BATCH_UNSUPPORTED})
    add_executable(core_batch_admission_test tests/core_batch_admission_test.c)
    target_link_libraries(core_batch_admission_test PRIVATE libbeam_core)
    add_dependencies(core_batch_admission_test libbeam_batch_fixtures)
    add_test(NAME core_batch_admission
        COMMAND core_batch_admission_test ${LIBBEAM_BATCH_ALPHA} ${LIBBEAM_BATCH_BETA} ${LIBBEAM_BATCH_UNSUPPORTED})
    set_tests_properties(core_batch_admission PROPERTIES TIMEOUT 120)
    add_executable(api_batch_admission_test tests/api_batch_admission_test.cpp)
    target_link_libraries(api_batch_admission_test PRIVATE libbeam::libbeam)
    add_dependencies(api_batch_admission_test libbeam_batch_fixtures)
    add_test(NAME api_batch_admission
        COMMAND api_batch_admission_test
            ${LIBBEAM_BATCH_ALPHA} ${LIBBEAM_BATCH_BETA} ${LIBBEAM_BATCH_UNSUPPORTED})
    set_tests_properties(api_batch_admission PROPERTIES TIMEOUT 30)
    add_test(NAME beam_runner_help COMMAND run_beam --help)
    add_test(NAME beam_runner_invalid_cli COMMAND run_beam --dependency missing.beam)
    set_tests_properties(beam_runner_invalid_cli PROPERTIES WILL_FAIL TRUE)
    add_test(NAME beam_runner_eager_dependency_groups COMMAND run_beam
        --entry batch_alpha entry
        --isolate ${LIBBEAM_BATCH_ALPHA} --dependency ${LIBBEAM_BATCH_BETA}
        --isolate ${LIBBEAM_BATCH_ALPHA} --dependency ${LIBBEAM_BATCH_BETA}
        --call payload --expect payload --call repeated --expect repeated)
    set_tests_properties(beam_runner_eager_dependency_groups PROPERTIES TIMEOUT 30)
    add_dependencies(run_beam libbeam_batch_fixtures)
    add_test(NAME engine_lifecycle_acceptance COMMAND engine_lifecycle)
    set_tests_properties(engine_lifecycle_acceptance PROPERTIES TIMEOUT 20)
    add_executable(core_work_runtime_test tests/core_work_runtime_test.c)
    target_link_libraries(core_work_runtime_test PRIVATE libbeam_core)
    add_test(NAME core_work_runtime_gate COMMAND core_work_runtime_test)
    add_executable(core_work_test tests/core_work_test.c)
    target_link_libraries(core_work_test PRIVATE libbeam_core)
    add_test(NAME core_work_ledger COMMAND core_work_test)
    add_executable(core_memory_test tests/core_memory_test.c)
    target_link_libraries(core_memory_test PRIVATE libbeam_core)
    add_test(NAME core_memory_scope COMMAND core_memory_test)
    add_executable(core_pid_test tests/core_pid_test.c)
    target_link_libraries(core_pid_test PRIVATE libbeam_core)
    add_test(NAME core_pid_gate COMMAND core_pid_test)
    add_executable(core_binary_build_test tests/core_binary_build_test.c)
    target_link_libraries(core_binary_build_test PRIVATE libbeam_core)
    add_test(NAME core_binary_build_gate COMMAND core_binary_build_test)
    set_tests_properties(core_binary_build_gate PROPERTIES TIMEOUT 90)
    add_executable(core_writable_init_test tests/core_writable_init_test.c)
    target_link_libraries(core_writable_init_test PRIVATE libbeam_core)
    add_test(NAME core_writable_init_gate COMMAND core_writable_init_test)
    set_tests_properties(core_writable_init_gate PROPERTIES TIMEOUT 90)
    add_executable(core_binary_vheap_test tests/core_binary_vheap_test.c)
    target_link_libraries(core_binary_vheap_test PRIVATE libbeam_core)
    add_test(NAME core_binary_vheap_policy COMMAND core_binary_vheap_test)
    set_tests_properties(core_binary_vheap_policy PROPERTIES TIMEOUT 90)
    add_executable(core_binary_flow_test tests/core_binary_flow_test.c)
    target_link_libraries(core_binary_flow_test PRIVATE libbeam_core)
    add_test(NAME core_binary_flow_proof COMMAND core_binary_flow_test)
    set_tests_properties(core_binary_flow_proof PROPERTIES TIMEOUT 90)
    add_executable(core_exception_test tests/core_exception_test.c)
    target_link_libraries(core_exception_test PRIVATE libbeam_core)
    add_test(NAME core_exception_components COMMAND core_exception_test)
    set_tests_properties(core_exception_components PROPERTIES TIMEOUT 90)
    add_executable(core_binary_size_test tests/core_binary_size_test.c)
    target_link_libraries(core_binary_size_test PRIVATE libbeam_core)
    add_test(NAME core_binary_size_components COMMAND core_binary_size_test)
    set_tests_properties(core_binary_size_components PROPERTIES TIMEOUT 90)
    add_executable(core_binary_emit_test tests/core_binary_emit_test.c)
    target_link_libraries(core_binary_emit_test PRIVATE libbeam_core)
    add_test(NAME core_binary_emit_components COMMAND core_binary_emit_test)
    set_tests_properties(core_binary_emit_components PROPERTIES TIMEOUT 90)
    add_executable(core_binary_transform_test tests/core_binary_transform_test.c)
    target_link_libraries(core_binary_transform_test PRIVATE libbeam_core)
    add_test(NAME core_binary_transform_components COMMAND core_binary_transform_test)
    set_tests_properties(core_binary_transform_components PROPERTIES TIMEOUT 90)
    add_executable(core_flatmap_test tests/core_flatmap_test.c)
    target_link_libraries(core_flatmap_test PRIVATE libbeam_core)
    add_test(NAME core_flatmap_components COMMAND core_flatmap_test)
    set(LIBBEAM_LIST_INTEGER_FIXTURE ${CMAKE_CURRENT_BINARY_DIR}/fixtures/list_integer_slice.beam)
    add_custom_command(OUTPUT ${LIBBEAM_LIST_INTEGER_FIXTURE}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/fixtures
        COMMAND ${CMAKE_COMMAND} -E env "ERL_FLAGS=+S 1:1 +SDcpu 1:1 +SDio 1 +A 0"
            ${LIBBEAM_ERLC} -o ${CMAKE_CURRENT_BINARY_DIR}/fixtures
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/list_integer_slice.erl
        DEPENDS tests/fixtures/list_integer_slice.erl VERBATIM)
    add_custom_target(libbeam_list_integer_fixture DEPENDS ${LIBBEAM_LIST_INTEGER_FIXTURE})
    add_executable(core_list_integer_test tests/core_list_integer_test.c)
    target_link_libraries(core_list_integer_test PRIVATE libbeam_core)
    add_dependencies(core_list_integer_test libbeam_list_integer_fixture)
    add_test(NAME core_list_integer_execution COMMAND core_list_integer_test ${LIBBEAM_LIST_INTEGER_FIXTURE})
    set_tests_properties(core_list_integer_execution PROPERTIES TIMEOUT 90)
    set(LIBBEAM_NEGATE_FIXTURE ${CMAKE_CURRENT_BINARY_DIR}/fixtures/negate_slice.beam)
    add_custom_command(OUTPUT ${LIBBEAM_NEGATE_FIXTURE}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/fixtures
        COMMAND ${CMAKE_COMMAND} -E env "ERL_FLAGS=+S 1:1 +SDcpu 1:1 +SDio 1 +A 0"
            ${LIBBEAM_ERLC} -o ${CMAKE_CURRENT_BINARY_DIR}/fixtures
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/negate_slice.erl
        DEPENDS tests/fixtures/negate_slice.erl VERBATIM)
    add_custom_target(libbeam_negate_fixture DEPENDS ${LIBBEAM_NEGATE_FIXTURE})
    add_executable(core_negate_test tests/core_negate_test.c)
    target_link_libraries(core_negate_test PRIVATE libbeam_core)
    add_dependencies(core_negate_test libbeam_negate_fixture)
    add_test(NAME core_negate_execution COMMAND core_negate_test ${LIBBEAM_NEGATE_FIXTURE})
    set_tests_properties(core_negate_execution PROPERTIES TIMEOUT 90)
    set(LIBBEAM_ABS_FIXTURE ${CMAKE_CURRENT_BINARY_DIR}/fixtures/abs_slice.beam)
    add_custom_command(OUTPUT ${LIBBEAM_ABS_FIXTURE}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/fixtures
        COMMAND ${CMAKE_COMMAND} -E env "ERL_FLAGS=+S 1:1 +SDcpu 1:1 +SDio 1 +A 0"
            ${LIBBEAM_ERLC} -o ${CMAKE_CURRENT_BINARY_DIR}/fixtures
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/abs_slice.erl
        DEPENDS tests/fixtures/abs_slice.erl VERBATIM)
    add_custom_target(libbeam_abs_fixture DEPENDS ${LIBBEAM_ABS_FIXTURE})
    add_executable(core_abs_test tests/core_abs_test.c)
    target_link_libraries(core_abs_test PRIVATE libbeam_core)
    add_dependencies(core_abs_test libbeam_abs_fixture)
    add_test(NAME core_abs_execution COMMAND core_abs_test ${LIBBEAM_ABS_FIXTURE})
    set_tests_properties(core_abs_execution PROPERTIES TIMEOUT 90)
    set(LIBBEAM_REM_FIXTURE ${CMAKE_CURRENT_BINARY_DIR}/fixtures/rem_slice.beam)
    add_custom_command(OUTPUT ${LIBBEAM_REM_FIXTURE}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/fixtures
        COMMAND ${CMAKE_COMMAND} -E env "ERL_FLAGS=+S 1:1 +SDcpu 1:1 +SDio 1 +A 0"
            ${LIBBEAM_ERLC} -o ${CMAKE_CURRENT_BINARY_DIR}/fixtures
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/rem_slice.erl
        DEPENDS tests/fixtures/rem_slice.erl VERBATIM)
    add_custom_target(libbeam_rem_fixture DEPENDS ${LIBBEAM_REM_FIXTURE})
    set(LIBBEAM_DIV_FIXTURE ${CMAKE_CURRENT_BINARY_DIR}/fixtures/div_slice.beam)
    set(LIBBEAM_DIV_VECTORS ${CMAKE_CURRENT_BINARY_DIR}/fixtures/div_vectors.txt)
    add_custom_command(OUTPUT ${LIBBEAM_DIV_FIXTURE} ${LIBBEAM_DIV_VECTORS}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/fixtures
        COMMAND ${CMAKE_COMMAND} -E env "ERL_FLAGS=+S 1:1 +SDcpu 1:1 +SDio 1 +A 0"
            ${LIBBEAM_ERLC} -o ${CMAKE_CURRENT_BINARY_DIR}/fixtures
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/div_slice.erl
        COMMAND ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_div_vectors.py ${LIBBEAM_DIV_VECTORS}
        DEPENDS tests/fixtures/div_slice.erl tools/generate_div_vectors.py VERBATIM)
    add_custom_target(libbeam_div_fixture DEPENDS ${LIBBEAM_DIV_FIXTURE} ${LIBBEAM_DIV_VECTORS})
    add_executable(core_div_test tests/core_div_test.c)
    target_link_libraries(core_div_test PRIVATE libbeam_core)
    add_dependencies(core_div_test libbeam_div_fixture libbeam_rem_fixture)
    add_test(NAME core_rem_execution COMMAND core_div_test ${LIBBEAM_REM_FIXTURE} ${LIBBEAM_DIV_VECTORS} rem)
    set_tests_properties(core_rem_execution PROPERTIES TIMEOUT 90)
    add_test(NAME core_div_execution COMMAND core_div_test ${LIBBEAM_DIV_FIXTURE} ${LIBBEAM_DIV_VECTORS})
    set_tests_properties(core_div_execution PROPERTIES TIMEOUT 90)
    set(LIBBEAM_TIMES_FIXTURE ${CMAKE_CURRENT_BINARY_DIR}/fixtures/times_slice.beam)
    add_custom_command(OUTPUT ${LIBBEAM_TIMES_FIXTURE}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/fixtures
        COMMAND ${CMAKE_COMMAND} -E env "ERL_FLAGS=+S 1:1 +SDcpu 1:1 +SDio 1 +A 0"
            ${LIBBEAM_ERLC} -o ${CMAKE_CURRENT_BINARY_DIR}/fixtures
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/times_slice.erl
        DEPENDS tests/fixtures/times_slice.erl VERBATIM)
    add_custom_target(libbeam_times_fixture DEPENDS ${LIBBEAM_TIMES_FIXTURE})
    add_executable(core_times_test tests/core_times_test.c)
    target_link_libraries(core_times_test PRIVATE libbeam_core)
    add_dependencies(core_times_test libbeam_times_fixture)
    add_test(NAME core_times_execution COMMAND core_times_test ${LIBBEAM_TIMES_FIXTURE})
    set_tests_properties(core_times_execution PROPERTIES TIMEOUT 90)
    set(LIBBEAM_SETELEMENT_FIXTURE ${CMAKE_CURRENT_BINARY_DIR}/fixtures/setelement_slice.beam)
    add_custom_command(OUTPUT ${LIBBEAM_SETELEMENT_FIXTURE}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/fixtures
        COMMAND ${CMAKE_COMMAND} -E env "ERL_FLAGS=+S 1:1 +SDcpu 1:1 +SDio 1 +A 0"
            ${LIBBEAM_ERLC} -o ${CMAKE_CURRENT_BINARY_DIR}/fixtures
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/setelement_slice.erl
        DEPENDS tests/fixtures/setelement_slice.erl VERBATIM)
    add_custom_target(libbeam_setelement_fixture DEPENDS ${LIBBEAM_SETELEMENT_FIXTURE})
    add_executable(core_setelement_test tests/core_setelement_test.c)
    target_link_libraries(core_setelement_test PRIVATE libbeam_core)
    add_dependencies(core_setelement_test libbeam_setelement_fixture)
    add_test(NAME core_setelement_execution COMMAND core_setelement_test ${LIBBEAM_SETELEMENT_FIXTURE})
    add_executable(core_bif_work_test tests/core_bif_work_test.c)
    target_link_libraries(core_bif_work_test PRIVATE libbeam_core)
    add_dependencies(core_bif_work_test libbeam_setelement_fixture libbeam_times_fixture libbeam_div_fixture libbeam_abs_fixture libbeam_negate_fixture libbeam_rem_fixture libbeam_list_integer_fixture)
    add_test(NAME core_list_integer_work COMMAND core_bif_work_test ${LIBBEAM_LIST_INTEGER_FIXTURE} list_integer_slice)
    set_tests_properties(core_list_integer_work PROPERTIES TIMEOUT 90)
    add_test(NAME core_rem_work COMMAND core_bif_work_test ${LIBBEAM_REM_FIXTURE} rem_slice)
    set_tests_properties(core_rem_work PROPERTIES TIMEOUT 90)
    add_test(NAME core_negate_work COMMAND core_bif_work_test ${LIBBEAM_NEGATE_FIXTURE} negate_slice)
    set_tests_properties(core_negate_work PROPERTIES TIMEOUT 90)
    add_test(NAME core_abs_work COMMAND core_bif_work_test ${LIBBEAM_ABS_FIXTURE} abs_slice)
    set_tests_properties(core_abs_work PROPERTIES TIMEOUT 90)
    add_test(NAME core_div_work COMMAND core_bif_work_test ${LIBBEAM_DIV_FIXTURE} div_slice)
    set_tests_properties(core_div_work PROPERTIES TIMEOUT 90)
    add_test(NAME core_times_work COMMAND core_bif_work_test ${LIBBEAM_TIMES_FIXTURE} times_slice)
    set_tests_properties(core_times_work PROPERTIES TIMEOUT 90)
    add_test(NAME core_setelement_work COMMAND core_bif_work_test ${LIBBEAM_SETELEMENT_FIXTURE} setelement_slice)
    set_tests_properties(core_setelement_execution core_setelement_work PROPERTIES TIMEOUT 90)
    set(LIBBEAM_APPLY_HOST ${CMAKE_CURRENT_BINARY_DIR}/fixtures/apply_host.beam)
    set(LIBBEAM_APPLY_TARGET ${CMAKE_CURRENT_BINARY_DIR}/fixtures/apply_target.beam)
    add_custom_command(OUTPUT ${LIBBEAM_APPLY_HOST} ${LIBBEAM_APPLY_TARGET}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/fixtures
        COMMAND ${CMAKE_COMMAND} -E env "ERL_FLAGS=+S 1:1 +SDcpu 1:1 +SDio 1 +A 0"
            ${LIBBEAM_ERLC} -o ${CMAKE_CURRENT_BINARY_DIR}/fixtures
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/apply_host.erl
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/apply_target.erl
        DEPENDS tests/fixtures/apply_host.erl tests/fixtures/apply_target.erl VERBATIM)
    add_custom_target(libbeam_apply_fixtures DEPENDS ${LIBBEAM_APPLY_HOST} ${LIBBEAM_APPLY_TARGET})
    add_executable(core_apply_test tests/core_apply_test.c)
    target_link_libraries(core_apply_test PRIVATE libbeam_core)
    add_dependencies(core_apply_test libbeam_apply_fixtures)
    add_test(NAME core_apply_components COMMAND core_apply_test ${LIBBEAM_APPLY_HOST} ${LIBBEAM_APPLY_TARGET})
    add_dependencies(core_bif_work_test libbeam_apply_fixtures)
    add_test(NAME core_apply_work COMMAND core_bif_work_test ${LIBBEAM_APPLY_HOST} apply_host)
    set_tests_properties(core_apply_components core_apply_work PROPERTIES TIMEOUT 90)
    set(LIBBEAM_FUNCTION_EXPORTED_FIXTURE ${CMAKE_CURRENT_BINARY_DIR}/fixtures/function_exported_slice.beam)
    add_custom_command(OUTPUT ${LIBBEAM_FUNCTION_EXPORTED_FIXTURE}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/fixtures
        COMMAND ${CMAKE_COMMAND} -E env "ERL_FLAGS=+S 1:1 +SDcpu 1:1 +SDio 1 +A 0"
            ${LIBBEAM_ERLC} -o ${CMAKE_CURRENT_BINARY_DIR}/fixtures
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/function_exported_slice.erl
        DEPENDS tests/fixtures/function_exported_slice.erl VERBATIM)
    add_custom_target(libbeam_function_exported_fixture DEPENDS ${LIBBEAM_FUNCTION_EXPORTED_FIXTURE})
    add_executable(core_function_exported_test tests/core_function_exported_test.c)
    target_link_libraries(core_function_exported_test PRIVATE libbeam_core)
    add_dependencies(core_function_exported_test libbeam_function_exported_fixture libbeam_apply_fixtures)
    add_test(NAME core_function_exported_execution COMMAND core_function_exported_test
        ${LIBBEAM_FUNCTION_EXPORTED_FIXTURE} ${LIBBEAM_APPLY_TARGET})
    add_dependencies(core_bif_work_test libbeam_function_exported_fixture)
    add_test(NAME core_function_exported_work COMMAND core_bif_work_test ${LIBBEAM_FUNCTION_EXPORTED_FIXTURE} function_exported_slice)
    set_tests_properties(core_function_exported_execution core_function_exported_work PROPERTIES TIMEOUT 90)
    set(LIBBEAM_PROCESS_INFO_FIXTURE ${CMAKE_CURRENT_BINARY_DIR}/fixtures/process_info_slice.beam)
    add_custom_command(OUTPUT ${LIBBEAM_PROCESS_INFO_FIXTURE}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/fixtures
        COMMAND ${CMAKE_COMMAND} -E env "ERL_FLAGS=+S 1:1 +SDcpu 1:1 +SDio 1 +A 0"
            ${LIBBEAM_ERLC} -o ${CMAKE_CURRENT_BINARY_DIR}/fixtures
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/process_info_slice.erl
        DEPENDS tests/fixtures/process_info_slice.erl VERBATIM)
    add_custom_target(libbeam_process_info_fixture DEPENDS ${LIBBEAM_PROCESS_INFO_FIXTURE})
    add_executable(core_process_info_test tests/core_process_info_test.c)
    target_link_libraries(core_process_info_test PRIVATE libbeam_core)
    add_dependencies(core_process_info_test libbeam_process_info_fixture)
    add_test(NAME core_process_info_execution COMMAND core_process_info_test ${LIBBEAM_PROCESS_INFO_FIXTURE})
    add_dependencies(core_bif_work_test libbeam_process_info_fixture)
    add_test(NAME core_process_info_work COMMAND core_bif_work_test ${LIBBEAM_PROCESS_INFO_FIXTURE} process_info_slice)
    set_tests_properties(core_process_info_execution core_process_info_work PROPERTIES TIMEOUT 90)
    set(LIBBEAM_CAPABILITY_FIXTURE ${CMAKE_CURRENT_BINARY_DIR}/fixtures/capability_slice.beam)
    add_custom_command(OUTPUT ${LIBBEAM_CAPABILITY_FIXTURE}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/fixtures
        COMMAND ${CMAKE_COMMAND} -E env "ERL_FLAGS=+S 1:1 +SDcpu 1:1 +SDio 1 +A 0"
            ${LIBBEAM_ERLC} -o ${CMAKE_CURRENT_BINARY_DIR}/fixtures
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/capability_slice.erl
        DEPENDS tests/fixtures/capability_slice.erl VERBATIM)
    add_custom_target(libbeam_capability_fixture DEPENDS ${LIBBEAM_CAPABILITY_FIXTURE})
    add_executable(core_capability_test tests/core_capability_test.c)
    target_link_libraries(core_capability_test PRIVATE libbeam_core)
    add_dependencies(core_capability_test libbeam_capability_fixture)
    add_test(NAME core_capability_execution COMMAND core_capability_test ${LIBBEAM_CAPABILITY_FIXTURE})
    add_executable(api_capability_test tests/api_capability_test.cpp)
    target_link_libraries(api_capability_test PRIVATE libbeam)
    add_dependencies(api_capability_test libbeam_capability_fixture)
    add_test(NAME api_capability_execution COMMAND api_capability_test ${LIBBEAM_CAPABILITY_FIXTURE})
    set_tests_properties(core_capability_execution api_capability_execution PROPERTIES TIMEOUT 90)

    set(LIBBEAM_NODE_FIXTURE ${CMAKE_CURRENT_BINARY_DIR}/fixtures/node_slice.beam)
    set(LIBBEAM_NODE_LOCATIONS ${CMAKE_CURRENT_BINARY_DIR}/fixtures/node_locations.beam)
    add_custom_command(OUTPUT ${LIBBEAM_NODE_FIXTURE} ${LIBBEAM_NODE_LOCATIONS}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/fixtures
        COMMAND ${CMAKE_COMMAND} -E env "ERL_FLAGS=+S 1:1 +SDcpu 1:1 +SDio 1 +A 0"
            ${LIBBEAM_ERLC} -o ${CMAKE_CURRENT_BINARY_DIR}/fixtures
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/node_slice.erl
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/node_locations.S
        DEPENDS tests/fixtures/node_slice.erl tests/fixtures/node_locations.S VERBATIM)
    add_custom_target(libbeam_node_fixture DEPENDS ${LIBBEAM_NODE_FIXTURE} ${LIBBEAM_NODE_LOCATIONS})
    add_executable(core_node_test tests/core_node_test.c)
    target_link_libraries(core_node_test PRIVATE libbeam_core)
    add_dependencies(core_node_test libbeam_node_fixture)
    add_test(NAME core_node_execution COMMAND core_node_test ${LIBBEAM_NODE_FIXTURE} ${LIBBEAM_NODE_LOCATIONS})
    add_dependencies(core_bif_work_test libbeam_node_fixture)
    add_test(NAME core_node_work COMMAND core_bif_work_test ${LIBBEAM_NODE_FIXTURE} node_slice)
    set_tests_properties(core_node_execution core_node_work PROPERTIES TIMEOUT 90)
    set(LIBBEAM_FLOOR_FIXTURE ${CMAKE_CURRENT_BINARY_DIR}/fixtures/floor_slice.beam)
    add_custom_command(OUTPUT ${LIBBEAM_FLOOR_FIXTURE}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/fixtures
        COMMAND ${CMAKE_COMMAND} -E env "ERL_FLAGS=+S 1:1 +SDcpu 1:1 +SDio 1 +A 0"
            ${LIBBEAM_ERLC} -o ${CMAKE_CURRENT_BINARY_DIR}/fixtures
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/floor_slice.erl
        DEPENDS tests/fixtures/floor_slice.erl VERBATIM)
    add_custom_target(libbeam_floor_fixture DEPENDS ${LIBBEAM_FLOOR_FIXTURE})
    add_executable(core_floor_test tests/core_floor_test.c)
    target_link_libraries(core_floor_test PRIVATE libbeam_core)
    add_dependencies(core_floor_test libbeam_floor_fixture)
    add_test(NAME core_floor_execution COMMAND core_floor_test ${LIBBEAM_FLOOR_FIXTURE})
    add_dependencies(core_bif_work_test libbeam_floor_fixture)
    add_test(NAME core_floor_work COMMAND core_bif_work_test ${LIBBEAM_FLOOR_FIXTURE} floor_slice)
    set_tests_properties(core_floor_execution core_floor_work PROPERTIES TIMEOUT 90)
    set(LIBBEAM_LIST_TUPLE_FIXTURE ${CMAKE_CURRENT_BINARY_DIR}/fixtures/list_tuple_slice.beam)
    add_custom_command(OUTPUT ${LIBBEAM_LIST_TUPLE_FIXTURE}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/fixtures
        COMMAND ${CMAKE_COMMAND} -E env "ERL_FLAGS=+S 1:1 +SDcpu 1:1 +SDio 1 +A 0"
            ${LIBBEAM_ERLC} -o ${CMAKE_CURRENT_BINARY_DIR}/fixtures
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/list_tuple_slice.erl
        DEPENDS tests/fixtures/list_tuple_slice.erl VERBATIM)
    add_custom_target(libbeam_list_tuple_fixture DEPENDS ${LIBBEAM_LIST_TUPLE_FIXTURE})
    add_executable(core_list_tuple_test tests/core_tuple_list_test.c)
    target_compile_definitions(core_list_tuple_test PRIVATE LB_TEST_LIST_INPUT=1)
    target_link_libraries(core_list_tuple_test PRIVATE libbeam_core)
    add_dependencies(core_list_tuple_test libbeam_list_tuple_fixture)
    add_test(NAME core_list_tuple_execution COMMAND core_list_tuple_test ${LIBBEAM_LIST_TUPLE_FIXTURE})
    add_dependencies(core_bif_work_test libbeam_list_tuple_fixture)
    add_test(NAME core_list_tuple_work COMMAND core_bif_work_test ${LIBBEAM_LIST_TUPLE_FIXTURE} list_tuple_slice)
    set_tests_properties(core_list_tuple_execution core_list_tuple_work PROPERTIES TIMEOUT 90)
    set(LIBBEAM_TUPLE_LIST_FIXTURE ${CMAKE_CURRENT_BINARY_DIR}/fixtures/tuple_list_slice.beam)
    add_custom_command(OUTPUT ${LIBBEAM_TUPLE_LIST_FIXTURE}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/fixtures
        COMMAND ${CMAKE_COMMAND} -E env "ERL_FLAGS=+S 1:1 +SDcpu 1:1 +SDio 1 +A 0"
            ${LIBBEAM_ERLC} -o ${CMAKE_CURRENT_BINARY_DIR}/fixtures
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/tuple_list_slice.erl
        DEPENDS tests/fixtures/tuple_list_slice.erl VERBATIM)
    add_custom_target(libbeam_tuple_list_fixture DEPENDS ${LIBBEAM_TUPLE_LIST_FIXTURE})
    add_executable(core_tuple_list_test tests/core_tuple_list_test.c)
    target_link_libraries(core_tuple_list_test PRIVATE libbeam_core)
    add_dependencies(core_tuple_list_test libbeam_tuple_list_fixture)
    add_test(NAME core_tuple_list_execution COMMAND core_tuple_list_test ${LIBBEAM_TUPLE_LIST_FIXTURE})
    add_dependencies(core_bif_work_test libbeam_tuple_list_fixture)
    add_test(NAME core_tuple_list_work COMMAND core_bif_work_test ${LIBBEAM_TUPLE_LIST_FIXTURE} tuple_list_slice)
    set_tests_properties(core_tuple_list_execution core_tuple_list_work PROPERTIES TIMEOUT 90)
    set(LIBBEAM_MAKE_TUPLE_FIXTURE ${CMAKE_CURRENT_BINARY_DIR}/fixtures/make_tuple_slice.beam)
    add_custom_command(OUTPUT ${LIBBEAM_MAKE_TUPLE_FIXTURE}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/fixtures
        COMMAND ${CMAKE_COMMAND} -E env "ERL_FLAGS=+S 1:1 +SDcpu 1:1 +SDio 1 +A 0"
            ${LIBBEAM_ERLC} -o ${CMAKE_CURRENT_BINARY_DIR}/fixtures
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/make_tuple_slice.erl
        DEPENDS tests/fixtures/make_tuple_slice.erl VERBATIM)
    add_custom_target(libbeam_make_tuple_fixture DEPENDS ${LIBBEAM_MAKE_TUPLE_FIXTURE})
    add_executable(core_make_tuple_test tests/core_make_tuple_test.c)
    target_link_libraries(core_make_tuple_test PRIVATE libbeam_core)
    add_dependencies(core_make_tuple_test libbeam_make_tuple_fixture)
    add_test(NAME core_make_tuple_execution COMMAND core_make_tuple_test ${LIBBEAM_MAKE_TUPLE_FIXTURE})
    add_dependencies(core_bif_work_test libbeam_make_tuple_fixture)
    add_test(NAME core_make_tuple_work COMMAND core_bif_work_test ${LIBBEAM_MAKE_TUPLE_FIXTURE} make_tuple_slice)
    set_tests_properties(core_make_tuple_execution core_make_tuple_work PROPERTIES TIMEOUT 90)
    set(LIBBEAM_ERROR2_FIXTURE ${CMAKE_CURRENT_BINARY_DIR}/fixtures/error2_slice.beam)
    add_custom_command(OUTPUT ${LIBBEAM_ERROR2_FIXTURE}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/fixtures
        COMMAND ${CMAKE_COMMAND} -E env "ERL_FLAGS=+S 1:1 +SDcpu 1:1 +SDio 1 +A 0"
            ${LIBBEAM_ERLC} -o ${CMAKE_CURRENT_BINARY_DIR}/fixtures
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/error2_slice.erl
        DEPENDS tests/fixtures/error2_slice.erl VERBATIM)
    add_custom_target(libbeam_error2_fixture DEPENDS ${LIBBEAM_ERROR2_FIXTURE})
    add_executable(core_error2_test tests/core_error2_test.c)
    target_link_libraries(core_error2_test PRIVATE libbeam_core)
    add_dependencies(core_error2_test libbeam_error2_fixture)
    add_test(NAME core_error2_execution COMMAND core_error2_test ${LIBBEAM_ERROR2_FIXTURE})
    add_dependencies(core_bif_work_test libbeam_error2_fixture)
    add_test(NAME core_error2_work COMMAND core_bif_work_test ${LIBBEAM_ERROR2_FIXTURE} error2_slice)
    set_tests_properties(core_error2_execution core_error2_work PROPERTIES TIMEOUT 90)
    set(LIBBEAM_DICT_FIXTURE ${CMAKE_CURRENT_BINARY_DIR}/fixtures/dict_slice.beam)
    set(LIBBEAM_DICT_LOCATIONS ${CMAKE_CURRENT_BINARY_DIR}/fixtures/dict_locations.beam)
    add_custom_command(OUTPUT ${LIBBEAM_DICT_FIXTURE} ${LIBBEAM_DICT_LOCATIONS}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/fixtures
        COMMAND ${CMAKE_COMMAND} -E env "ERL_FLAGS=+S 1:1 +SDcpu 1:1 +SDio 1 +A 0"
            ${LIBBEAM_ERLC} -o ${CMAKE_CURRENT_BINARY_DIR}/fixtures
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/dict_slice.erl
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/dict_locations.S
        DEPENDS tests/fixtures/dict_slice.erl tests/fixtures/dict_locations.S VERBATIM)
    add_custom_target(libbeam_dict_fixture DEPENDS ${LIBBEAM_DICT_FIXTURE} ${LIBBEAM_DICT_LOCATIONS})
    add_executable(core_dict_test tests/core_dict_test.c)
    target_link_libraries(core_dict_test PRIVATE libbeam_core)
    add_dependencies(core_dict_test libbeam_dict_fixture libbeam_apply_fixtures)
    add_test(NAME core_dict_execution COMMAND core_dict_test ${LIBBEAM_DICT_FIXTURE} ${LIBBEAM_DICT_LOCATIONS} ${LIBBEAM_APPLY_TARGET})
    add_dependencies(core_bif_work_test libbeam_dict_fixture)
    add_test(NAME core_dict_work COMMAND core_bif_work_test ${LIBBEAM_DICT_FIXTURE} dict_slice)
    set_tests_properties(core_dict_execution core_dict_work PROPERTIES TIMEOUT 90)
    set(LIBBEAM_MODULE_LOADED_FIXTURE ${CMAKE_CURRENT_BINARY_DIR}/fixtures/module_loaded_slice.beam)
    set(LIBBEAM_MODULE_ON_LOAD_REFUSED ${CMAKE_CURRENT_BINARY_DIR}/fixtures/module_on_load_refused.beam)
    add_custom_command(OUTPUT ${LIBBEAM_MODULE_LOADED_FIXTURE} ${LIBBEAM_MODULE_ON_LOAD_REFUSED}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/fixtures
        COMMAND ${CMAKE_COMMAND} -E env "ERL_FLAGS=+S 1:1 +SDcpu 1:1 +SDio 1 +A 0"
            ${LIBBEAM_ERLC} -o ${CMAKE_CURRENT_BINARY_DIR}/fixtures
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/module_loaded_slice.erl
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/module_on_load_refused.erl
        DEPENDS tests/fixtures/module_loaded_slice.erl tests/fixtures/module_on_load_refused.erl VERBATIM)
    add_custom_target(libbeam_module_loaded_fixture DEPENDS ${LIBBEAM_MODULE_LOADED_FIXTURE} ${LIBBEAM_MODULE_ON_LOAD_REFUSED})
    add_executable(core_module_loaded_test tests/core_module_loaded_test.c)
    target_link_libraries(core_module_loaded_test PRIVATE libbeam_core)
    add_dependencies(core_module_loaded_test libbeam_module_loaded_fixture libbeam_apply_fixtures)
    add_test(NAME core_module_loaded_execution COMMAND core_module_loaded_test ${LIBBEAM_MODULE_LOADED_FIXTURE} ${LIBBEAM_APPLY_TARGET} ${LIBBEAM_MODULE_ON_LOAD_REFUSED})
    add_dependencies(core_bif_work_test libbeam_module_loaded_fixture)
    add_test(NAME core_module_loaded_work COMMAND core_bif_work_test ${LIBBEAM_MODULE_LOADED_FIXTURE} module_loaded_slice)
    set_tests_properties(core_module_loaded_execution core_module_loaded_work PROPERTIES TIMEOUT 90)
    set(LIBBEAM_EXTERNAL_FIXTURE ${CMAKE_CURRENT_BINARY_DIR}/fixtures/external_slice.beam)
    add_custom_command(OUTPUT ${LIBBEAM_EXTERNAL_FIXTURE}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/fixtures
        COMMAND ${CMAKE_COMMAND} -E env "ERL_FLAGS=+S 1:1 +SDcpu 1:1 +SDio 1 +A 0"
            ${LIBBEAM_ERLC} -o ${CMAKE_CURRENT_BINARY_DIR}/fixtures
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/external_slice.erl
        DEPENDS tests/fixtures/external_slice.erl VERBATIM)
    add_custom_target(libbeam_external_fixture DEPENDS ${LIBBEAM_EXTERNAL_FIXTURE})
    add_executable(core_external_decode_test tests/core_external_decode_test.c)
    target_link_libraries(core_external_decode_test PRIVATE libbeam_core)
    add_dependencies(core_external_decode_test libbeam_external_fixture)
    add_test(NAME core_external_decode_execution COMMAND core_external_decode_test ${LIBBEAM_EXTERNAL_FIXTURE})
    add_dependencies(core_bif_work_test libbeam_external_fixture)
    add_test(NAME core_external_decode_work COMMAND core_bif_work_test ${LIBBEAM_EXTERNAL_FIXTURE} external_slice)
    set_tests_properties(core_external_decode_execution core_external_decode_work PROPERTIES TIMEOUT 180)
    add_executable(core_etf_arena_test tests/core_etf_arena_test.c)
    target_link_libraries(core_etf_arena_test PRIVATE libbeam_core)
    add_dependencies(core_etf_arena_test libbeam_apply_fixtures)
    add_test(NAME core_etf_arena COMMAND core_etf_arena_test ${LIBBEAM_APPLY_HOST})
    set_tests_properties(core_etf_arena PROPERTIES TIMEOUT 90)
    set(LIBBEAM_ATOM_LIST_FIXTURE ${CMAKE_CURRENT_BINARY_DIR}/fixtures/atom_list_slice.beam)
    add_custom_command(OUTPUT ${LIBBEAM_ATOM_LIST_FIXTURE}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/fixtures
        COMMAND ${CMAKE_COMMAND} -E env "ERL_FLAGS=+S 1:1 +SDcpu 1:1 +SDio 1 +A 0"
            ${LIBBEAM_ERLC} -o ${CMAKE_CURRENT_BINARY_DIR}/fixtures
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/atom_list_slice.erl
        DEPENDS tests/fixtures/atom_list_slice.erl VERBATIM)
    add_custom_target(libbeam_atom_list_fixture DEPENDS ${LIBBEAM_ATOM_LIST_FIXTURE})
    add_executable(core_atom_list_test tests/core_atom_list_test.c)
    target_link_libraries(core_atom_list_test PRIVATE libbeam_core)
    add_dependencies(core_atom_list_test libbeam_atom_list_fixture)
    add_test(NAME core_atom_list_execution COMMAND core_atom_list_test ${LIBBEAM_ATOM_LIST_FIXTURE})
    add_dependencies(core_bif_work_test libbeam_atom_list_fixture)
    add_test(NAME core_atom_list_work COMMAND core_bif_work_test ${LIBBEAM_ATOM_LIST_FIXTURE} atom_list_slice)
    set_tests_properties(core_atom_list_execution core_atom_list_work PROPERTIES TIMEOUT 90)
    set(LIBBEAM_NOT_FIXTURE ${CMAKE_CURRENT_BINARY_DIR}/fixtures/not_slice.beam)
    set(LIBBEAM_NOT_GUARD_FIXTURE ${CMAKE_CURRENT_BINARY_DIR}/fixtures/not_guard_slice.beam)
    add_custom_command(OUTPUT ${LIBBEAM_NOT_FIXTURE} ${LIBBEAM_NOT_GUARD_FIXTURE}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/fixtures
        COMMAND ${CMAKE_COMMAND} -E env "ERL_FLAGS=+S 1:1 +SDcpu 1:1 +SDio 1 +A 0"
            ${LIBBEAM_ERLC} -o ${CMAKE_CURRENT_BINARY_DIR}/fixtures
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/not_slice.erl
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/not_guard_slice.S
        DEPENDS tests/fixtures/not_slice.erl tests/fixtures/not_guard_slice.S VERBATIM)
    add_custom_target(libbeam_not_fixture DEPENDS ${LIBBEAM_NOT_FIXTURE} ${LIBBEAM_NOT_GUARD_FIXTURE})
    add_executable(core_not_test tests/core_not_test.c)
    target_link_libraries(core_not_test PRIVATE libbeam_core)
    add_dependencies(core_not_test libbeam_not_fixture)
    add_test(NAME core_not_execution COMMAND core_not_test ${LIBBEAM_NOT_FIXTURE} ${LIBBEAM_NOT_GUARD_FIXTURE})
    add_dependencies(core_bif_work_test libbeam_not_fixture)
    add_test(NAME core_not_work COMMAND core_bif_work_test ${LIBBEAM_NOT_FIXTURE} not_slice)
    set_tests_properties(core_not_execution core_not_work PROPERTIES TIMEOUT 90)
    set(LIBBEAM_LIST_PARTS_FIXTURE ${CMAKE_CURRENT_BINARY_DIR}/fixtures/list_parts_slice.beam)
    add_custom_command(OUTPUT ${LIBBEAM_LIST_PARTS_FIXTURE}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/fixtures
        COMMAND ${CMAKE_COMMAND} -E env "ERL_FLAGS=+S 1:1 +SDcpu 1:1 +SDio 1 +A 0"
            ${LIBBEAM_ERLC} -o ${CMAKE_CURRENT_BINARY_DIR}/fixtures
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/list_parts_slice.erl
        DEPENDS tests/fixtures/list_parts_slice.erl VERBATIM)
    add_custom_target(libbeam_list_parts_fixture DEPENDS ${LIBBEAM_LIST_PARTS_FIXTURE})
    add_executable(core_list_parts_test tests/core_list_parts_test.c)
    target_link_libraries(core_list_parts_test PRIVATE libbeam_core)
    add_dependencies(core_list_parts_test libbeam_list_parts_fixture)
    add_test(NAME core_list_parts_execution COMMAND core_list_parts_test ${LIBBEAM_LIST_PARTS_FIXTURE})
    add_dependencies(core_bif_work_test libbeam_list_parts_fixture)
    add_test(NAME core_list_parts_work COMMAND core_bif_work_test ${LIBBEAM_LIST_PARTS_FIXTURE} list_parts_slice)
    set_tests_properties(core_list_parts_execution core_list_parts_work PROPERTIES TIMEOUT 90)
    set(LIBBEAM_INTEGER_PREDICATE_FIXTURE ${CMAKE_CURRENT_BINARY_DIR}/fixtures/integer_predicate_slice.beam)
    add_custom_command(OUTPUT ${LIBBEAM_INTEGER_PREDICATE_FIXTURE}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/fixtures
        COMMAND ${CMAKE_COMMAND} -E env "ERL_FLAGS=+S 1:1 +SDcpu 1:1 +SDio 1 +A 0"
            ${LIBBEAM_ERLC} -o ${CMAKE_CURRENT_BINARY_DIR}/fixtures
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/integer_predicate_slice.erl
        DEPENDS tests/fixtures/integer_predicate_slice.erl VERBATIM)
    set(LIBBEAM_INTEGER_LITERAL_FIXTURE ${CMAKE_CURRENT_BINARY_DIR}/fixtures/integer_predicate_literals.beam)
    add_custom_command(OUTPUT ${LIBBEAM_INTEGER_LITERAL_FIXTURE}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/fixtures
        COMMAND ${CMAKE_COMMAND} -E env "ERL_FLAGS=+S 1:1 +SDcpu 1:1 +SDio 1 +A 0"
            ${LIBBEAM_ERLC} -o ${CMAKE_CURRENT_BINARY_DIR}/fixtures
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/integer_predicate_literals.S
        DEPENDS tests/fixtures/integer_predicate_literals.S VERBATIM)
    add_custom_target(libbeam_integer_predicate_fixture DEPENDS ${LIBBEAM_INTEGER_PREDICATE_FIXTURE}
        ${LIBBEAM_INTEGER_LITERAL_FIXTURE})
    add_executable(core_integer_predicate_test tests/core_integer_predicate_test.c)
    target_link_libraries(core_integer_predicate_test PRIVATE libbeam_core)
    add_dependencies(core_integer_predicate_test libbeam_integer_predicate_fixture)
    add_test(NAME core_integer_predicate_execution COMMAND core_integer_predicate_test ${LIBBEAM_INTEGER_PREDICATE_FIXTURE}
        ${LIBBEAM_INTEGER_LITERAL_FIXTURE})
    add_dependencies(core_bif_work_test libbeam_integer_predicate_fixture)
    add_test(NAME core_integer_predicate_work COMMAND core_bif_work_test ${LIBBEAM_INTEGER_PREDICATE_FIXTURE} integer_predicate_slice)
    set_tests_properties(core_integer_predicate_execution core_integer_predicate_work PROPERTIES TIMEOUT 90)
    set(LIBBEAM_LIST_PREDICATE_FIXTURE ${CMAKE_CURRENT_BINARY_DIR}/fixtures/list_predicate_slice.beam)
    set(LIBBEAM_LIST_LITERAL_FIXTURE ${CMAKE_CURRENT_BINARY_DIR}/fixtures/list_predicate_literals.beam)
    add_custom_command(OUTPUT ${LIBBEAM_LIST_PREDICATE_FIXTURE} ${LIBBEAM_LIST_LITERAL_FIXTURE}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/fixtures
        COMMAND ${CMAKE_COMMAND} -E env "ERL_FLAGS=+S 1:1 +SDcpu 1:1 +SDio 1 +A 0"
            ${LIBBEAM_ERLC} -o ${CMAKE_CURRENT_BINARY_DIR}/fixtures
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/list_predicate_slice.erl
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/list_predicate_literals.S
        DEPENDS tests/fixtures/list_predicate_slice.erl tests/fixtures/list_predicate_literals.S VERBATIM)
    add_custom_target(libbeam_list_predicate_fixture DEPENDS ${LIBBEAM_LIST_PREDICATE_FIXTURE} ${LIBBEAM_LIST_LITERAL_FIXTURE})
    add_dependencies(core_integer_predicate_test libbeam_list_predicate_fixture)
    add_dependencies(core_bif_work_test libbeam_list_predicate_fixture)
    add_test(NAME core_list_predicate_execution COMMAND core_integer_predicate_test
        ${LIBBEAM_LIST_PREDICATE_FIXTURE} ${LIBBEAM_LIST_LITERAL_FIXTURE} list)
    add_test(NAME core_list_predicate_work COMMAND core_bif_work_test ${LIBBEAM_LIST_PREDICATE_FIXTURE} list_predicate_slice)
    set_tests_properties(core_list_predicate_execution core_list_predicate_work PROPERTIES TIMEOUT 90)
    set(LIBBEAM_ATOM_PREDICATE_FIXTURE ${CMAKE_CURRENT_BINARY_DIR}/fixtures/atom_predicate_slice.beam)
    set(LIBBEAM_ATOM_LITERAL_FIXTURE ${CMAKE_CURRENT_BINARY_DIR}/fixtures/atom_predicate_literals.beam)
    add_custom_command(OUTPUT ${LIBBEAM_ATOM_PREDICATE_FIXTURE} ${LIBBEAM_ATOM_LITERAL_FIXTURE}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/fixtures
        COMMAND ${CMAKE_COMMAND} -E env "ERL_FLAGS=+S 1:1 +SDcpu 1:1 +SDio 1 +A 0"
            ${LIBBEAM_ERLC} -o ${CMAKE_CURRENT_BINARY_DIR}/fixtures
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/atom_predicate_slice.erl
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/atom_predicate_literals.S
        DEPENDS tests/fixtures/atom_predicate_slice.erl tests/fixtures/atom_predicate_literals.S VERBATIM)
    add_custom_target(libbeam_atom_predicate_fixture DEPENDS ${LIBBEAM_ATOM_PREDICATE_FIXTURE} ${LIBBEAM_ATOM_LITERAL_FIXTURE})
    add_dependencies(core_integer_predicate_test libbeam_atom_predicate_fixture)
    add_dependencies(core_bif_work_test libbeam_atom_predicate_fixture)
    add_test(NAME core_atom_predicate_execution COMMAND core_integer_predicate_test
        ${LIBBEAM_ATOM_PREDICATE_FIXTURE} ${LIBBEAM_ATOM_LITERAL_FIXTURE} atom)
    add_test(NAME core_atom_predicate_work COMMAND core_bif_work_test ${LIBBEAM_ATOM_PREDICATE_FIXTURE} atom_predicate_slice)
    set_tests_properties(core_atom_predicate_execution core_atom_predicate_work PROPERTIES TIMEOUT 90)
    set(LIBBEAM_BINARY_PREDICATE_FIXTURE ${CMAKE_CURRENT_BINARY_DIR}/fixtures/binary_predicate_slice.beam)
    set(LIBBEAM_BINARY_LITERAL_FIXTURE ${CMAKE_CURRENT_BINARY_DIR}/fixtures/binary_predicate_literals.beam)
    add_custom_command(OUTPUT ${LIBBEAM_BINARY_PREDICATE_FIXTURE} ${LIBBEAM_BINARY_LITERAL_FIXTURE}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/fixtures
        COMMAND ${CMAKE_COMMAND} -E env "ERL_FLAGS=+S 1:1 +SDcpu 1:1 +SDio 1 +A 0"
            ${LIBBEAM_ERLC} -o ${CMAKE_CURRENT_BINARY_DIR}/fixtures
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/binary_predicate_slice.erl
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/binary_predicate_literals.S
        DEPENDS tests/fixtures/binary_predicate_slice.erl tests/fixtures/binary_predicate_literals.S VERBATIM)
    add_custom_target(libbeam_binary_predicate_fixture DEPENDS ${LIBBEAM_BINARY_PREDICATE_FIXTURE} ${LIBBEAM_BINARY_LITERAL_FIXTURE})
    add_dependencies(core_integer_predicate_test libbeam_binary_predicate_fixture)
    add_dependencies(core_bif_work_test libbeam_binary_predicate_fixture)
    add_test(NAME core_binary_predicate_execution COMMAND core_integer_predicate_test
        ${LIBBEAM_BINARY_PREDICATE_FIXTURE} ${LIBBEAM_BINARY_LITERAL_FIXTURE} binary)
    add_test(NAME core_binary_predicate_work COMMAND core_bif_work_test ${LIBBEAM_BINARY_PREDICATE_FIXTURE} binary_predicate_slice)
    set_tests_properties(core_binary_predicate_execution core_binary_predicate_work PROPERTIES TIMEOUT 90)
    set(LIBBEAM_MAP_KEY_FIXTURE ${CMAKE_CURRENT_BINARY_DIR}/fixtures/map_key_slice.beam)
    add_custom_command(OUTPUT ${LIBBEAM_MAP_KEY_FIXTURE}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/fixtures
        COMMAND ${CMAKE_COMMAND} -E env "ERL_FLAGS=+S 1:1 +SDcpu 1:1 +SDio 1 +A 0"
            ${LIBBEAM_ERLC} -o ${CMAKE_CURRENT_BINARY_DIR}/fixtures
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/map_key_slice.erl
        DEPENDS tests/fixtures/map_key_slice.erl VERBATIM)
    add_custom_target(libbeam_map_key_fixture DEPENDS ${LIBBEAM_MAP_KEY_FIXTURE})
    add_executable(core_map_key_test tests/core_map_key_test.c)
    target_link_libraries(core_map_key_test PRIVATE libbeam_core)
    add_dependencies(core_map_key_test libbeam_map_key_fixture)
    add_test(NAME core_map_key_execution COMMAND core_map_key_test ${LIBBEAM_MAP_KEY_FIXTURE})
    add_dependencies(core_bif_work_test libbeam_map_key_fixture)
    add_test(NAME core_map_key_work COMMAND core_bif_work_test ${LIBBEAM_MAP_KEY_FIXTURE} map_key_slice)
    set_tests_properties(core_map_key_execution core_map_key_work PROPERTIES TIMEOUT 90)
    set(LIBBEAM_MIN_FIXTURE ${CMAKE_CURRENT_BINARY_DIR}/fixtures/min_slice.beam)
    add_custom_command(OUTPUT ${LIBBEAM_MIN_FIXTURE}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/fixtures
        COMMAND ${CMAKE_COMMAND} -E env "ERL_FLAGS=+S 1:1 +SDcpu 1:1 +SDio 1 +A 0"
            ${LIBBEAM_ERLC} -o ${CMAKE_CURRENT_BINARY_DIR}/fixtures
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/min_slice.erl
        DEPENDS tests/fixtures/min_slice.erl VERBATIM)
    add_custom_target(libbeam_min_fixture DEPENDS ${LIBBEAM_MIN_FIXTURE})
    add_dependencies(core_bif_work_test libbeam_min_fixture)
    add_test(NAME core_min_work COMMAND core_bif_work_test ${LIBBEAM_MIN_FIXTURE} min_slice)
    set_tests_properties(core_min_work PROPERTIES TIMEOUT 90)
    set(LIBBEAM_MAX_FIXTURE ${CMAKE_CURRENT_BINARY_DIR}/fixtures/max_slice.beam)
    add_custom_command(OUTPUT ${LIBBEAM_MAX_FIXTURE}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/fixtures
        COMMAND ${CMAKE_COMMAND} -E env "ERL_FLAGS=+S 1:1 +SDcpu 1:1 +SDio 1 +A 0"
            ${LIBBEAM_ERLC} -o ${CMAKE_CURRENT_BINARY_DIR}/fixtures
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/max_slice.erl
        DEPENDS tests/fixtures/max_slice.erl VERBATIM)
    add_custom_target(libbeam_max_fixture DEPENDS ${LIBBEAM_MAX_FIXTURE})
    add_executable(core_max_test tests/core_max_test.c)
    target_link_libraries(core_max_test PRIVATE libbeam_core)
    add_dependencies(core_max_test libbeam_max_fixture libbeam_min_fixture)
    add_test(NAME core_min_execution COMMAND core_max_test ${LIBBEAM_MIN_FIXTURE} min)
    set_tests_properties(core_min_execution PROPERTIES TIMEOUT 90)
    add_test(NAME core_max_execution COMMAND core_max_test ${LIBBEAM_MAX_FIXTURE})
    set_tests_properties(core_max_execution PROPERTIES TIMEOUT 90)
    set(LIBBEAM_RAISE_FIXTURE ${CMAKE_CURRENT_BINARY_DIR}/fixtures/raise_slice.beam)
    add_custom_command(OUTPUT ${LIBBEAM_RAISE_FIXTURE}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/fixtures
        COMMAND ${LIBBEAM_ERLC} -o ${CMAKE_CURRENT_BINARY_DIR}/fixtures
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/raise_slice.erl
        DEPENDS tests/fixtures/raise_slice.erl VERBATIM)
    add_custom_target(libbeam_raise_fixture DEPENDS ${LIBBEAM_RAISE_FIXTURE})
    add_executable(core_raise_execution_test tests/core_raise_execution_test.c)
    target_link_libraries(core_raise_execution_test PRIVATE libbeam_core)
    add_dependencies(core_raise_execution_test libbeam_raise_fixture)
    add_test(NAME core_raise_execution COMMAND core_raise_execution_test ${LIBBEAM_RAISE_FIXTURE})
    add_executable(core_raise_work_test tests/core_raise_work_test.c)
    target_link_libraries(core_raise_work_test PRIVATE libbeam_core)
    add_dependencies(core_raise_work_test libbeam_raise_fixture)
    add_test(NAME core_raise_work COMMAND core_raise_work_test ${LIBBEAM_RAISE_FIXTURE})
    set_tests_properties(core_raise_execution core_raise_work PROPERTIES TIMEOUT 90)
    add_executable(core_raise_trace_test tests/core_raise_trace_test.c)
    target_link_libraries(core_raise_trace_test PRIVATE libbeam_core)
    add_test(NAME core_raise_trace_prepare COMMAND core_raise_trace_test)
    add_executable(core_hamt_test tests/core_hamt_test.c)
    target_link_libraries(core_hamt_test PRIVATE libbeam_core)
    add_test(NAME core_hamt_runtime COMMAND core_hamt_test)
    add_executable(core_map_test tests/core_map_test.c)
    target_link_libraries(core_map_test PRIVATE libbeam_core)
    add_test(NAME core_map_runtime COMMAND core_map_test)
    set(LIBBEAM_MAP_FIXTURE_DIR ${CMAKE_CURRENT_BINARY_DIR}/fixtures/maps)
    set(LIBBEAM_MAP_FIXTURE ${LIBBEAM_MAP_FIXTURE_DIR}/map_slice.beam)
    add_custom_command(OUTPUT ${LIBBEAM_MAP_FIXTURE}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${LIBBEAM_MAP_FIXTURE_DIR}
        COMMAND ${LIBBEAM_ERLC} -o ${LIBBEAM_MAP_FIXTURE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/map_slice.erl
        DEPENDS tests/fixtures/map_slice.erl
        VERBATIM)
    add_custom_target(libbeam_map_fixture DEPENDS ${LIBBEAM_MAP_FIXTURE})
    add_dependencies(run_beam libbeam_map_fixture)
    add_executable(core_map_execution_test tests/core_map_execution_test.c)
    target_link_libraries(core_map_execution_test PRIVATE libbeam_core)
    add_dependencies(core_map_execution_test libbeam_map_fixture)
    add_test(NAME core_map_execution COMMAND core_map_execution_test ${LIBBEAM_MAP_FIXTURE})
    set_tests_properties(core_map_execution PROPERTIES TIMEOUT 90)
    foreach(case IN ITEMS new_binary add_created_binary update_created_binary single_created_binary dynamic_binary)
        if("${case}" STREQUAL "new_binary")
            set(expected map-ok)
        elseif("${case}" STREQUAL "add_created_binary")
            set(expected assoc-ok)
        elseif("${case}" STREQUAL "update_created_binary")
            set(expected exact-ok)
        elseif("${case}" STREQUAL "single_created_binary")
            set(expected single-ok)
        else()
            set(expected dynamic-ok)
        endif()
        add_test(NAME beam_runner_map_${case} COMMAND run_beam
            --entry map_slice ${case} --call payload --expect ${expected}
            --isolate ${LIBBEAM_MAP_FIXTURE})
        set_tests_properties(beam_runner_map_${case} PROPERTIES TIMEOUT 30)
    endforeach()
    set(LIBBEAM_EXTERNAL_FUN_FIXTURE ${CMAKE_CURRENT_BINARY_DIR}/fixtures/external_fun_slice.beam)
    add_custom_command(OUTPUT ${LIBBEAM_EXTERNAL_FUN_FIXTURE}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/fixtures
        COMMAND ${LIBBEAM_ERLC} -o ${CMAKE_CURRENT_BINARY_DIR}/fixtures
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/external_fun_slice.erl
        DEPENDS tests/fixtures/external_fun_slice.erl
        VERBATIM)
    add_custom_target(libbeam_external_fun_fixture DEPENDS ${LIBBEAM_EXTERNAL_FUN_FIXTURE})
    add_executable(core_external_fun_test tests/core_external_fun_test.c)
    target_link_libraries(core_external_fun_test PRIVATE libbeam_core)
    add_dependencies(core_external_fun_test libbeam_external_fun_fixture)
    add_test(NAME core_external_fun_runtime COMMAND core_external_fun_test ${LIBBEAM_EXTERNAL_FUN_FIXTURE})
    set_tests_properties(core_flatmap_components core_external_fun_runtime PROPERTIES TIMEOUT 90)
    add_executable(core_bit_segment_test tests/core_bit_segment_test.c)
    target_link_libraries(core_bit_segment_test PRIVATE libbeam_core ${CMAKE_DL_LIBS})
    add_test(NAME core_bit_segment_components COMMAND core_bit_segment_test)
    set_tests_properties(core_bit_segment_components PROPERTIES TIMEOUT 90)
    add_executable(core_bit_writer_test tests/core_bit_writer_test.c)
    target_link_libraries(core_bit_writer_test PRIVATE libbeam_core)
    add_test(NAME core_bit_writer_components COMMAND core_bit_writer_test)
    set_tests_properties(core_bit_writer_components PROPERTIES TIMEOUT 90)
    add_executable(core_bit_copy_test tests/core_bit_copy_test.c)
    target_link_libraries(core_bit_copy_test PRIVATE libbeam_core)
    add_test(NAME core_bit_copy_components COMMAND core_bit_copy_test)
    add_executable(core_decimal_test tests/core_decimal_test.c)
    target_link_libraries(core_decimal_test PRIVATE libbeam_core)
    add_test(NAME core_decimal_components COMMAND core_decimal_test)
    add_executable(core_add_test tests/core_add_test.c)
    target_link_libraries(core_add_test PRIVATE libbeam_core ${CMAKE_DL_LIBS})
    add_executable(core_big_test tests/core_big_test.c)
    target_link_libraries(core_big_test PRIVATE libbeam_core)
    add_test(NAME core_big_components COMMAND core_big_test)
    add_executable(core_shape_test tests/core_shape_test.c)
    target_link_libraries(core_shape_test PRIVATE libbeam_core)
    add_executable(core_shape_verify_test tests/core_shape_verify_test.c)
    target_link_libraries(core_shape_verify_test PRIVATE libbeam_core)
    add_test(NAME core_shape_proof COMMAND core_shape_verify_test)
    add_executable(core_compare_test tests/core_compare_test.c)
    target_link_libraries(core_compare_test PRIVATE libbeam_core)
    add_test(NAME core_exact_comparison COMMAND core_compare_test)
    set_tests_properties(core_exact_comparison PROPERTIES TIMEOUT 90)
    add_executable(core_local_test tests/core_local_test.c)
    target_link_libraries(core_local_test PRIVATE libbeam_core)
    add_executable(core_world_test tests/core_world_test.c)
    target_link_libraries(core_world_test PRIVATE libbeam_core)
    add_test(NAME core_world_lifetime COMMAND core_world_test)
    set_tests_properties(core_world_lifetime PROPERTIES TIMEOUT 90)
    add_executable(core_executor_test tests/core_executor_test.c)
    target_link_libraries(core_executor_test PRIVATE libbeam_core ${CMAKE_DL_LIBS})
    add_test(NAME core_executor_native_lifetime COMMAND core_executor_test)
    set_tests_properties(core_executor_native_lifetime PROPERTIES TIMEOUT 90)
    add_executable(core_engine_test tests/core_engine_test.c)
    target_link_libraries(core_engine_test PRIVATE libbeam_core)
    add_test(NAME core_engine_lifetime COMMAND core_engine_test)
    set_tests_properties(core_engine_lifetime PROPERTIES TIMEOUT 90)
    add_executable(core_binary_test tests/core_binary_test.c)
    target_link_libraries(core_binary_test PRIVATE libbeam_core)
    add_test(NAME core_binary_components COMMAND core_binary_test)
    set_tests_properties(core_binary_components PROPERTIES TIMEOUT 90)
    add_executable(core_code_test tests/core_code_test.c)
    target_link_libraries(core_code_test PRIVATE libbeam_core)
    add_test(NAME core_code_components COMMAND core_code_test)
    set_tests_properties(core_code_components PROPERTIES TIMEOUT 90)
    add_executable(core_beam_program_test tests/core_beam_program_test.c)
    target_link_libraries(core_beam_program_test PRIVATE libbeam_core)
    add_test(NAME core_beam_program_lifetime COMMAND core_beam_program_test)
    set_tests_properties(core_beam_program_lifetime PROPERTIES TIMEOUT 90)
    add_executable(core_terms_atoms_test tests/core_terms_atoms_test.c)
    target_link_libraries(core_terms_atoms_test PRIVATE libbeam_core)
    add_test(NAME core_terms_atoms_lifetime COMMAND core_terms_atoms_test)
    set_tests_properties(core_terms_atoms_lifetime PROPERTIES TIMEOUT 90)
    add_executable(core_alloc_test tests/core_alloc_test.c)
    target_link_libraries(core_alloc_test PRIVATE libbeam_core)
    add_test(NAME core_allocation_lifetime COMMAND core_alloc_test)
    add_executable(core_beam_image_test tests/core_beam_image_test.c)
    target_link_libraries(core_beam_image_test PRIVATE libbeam_core)
    add_test(NAME core_beam_image_lifetime COMMAND core_beam_image_test)
    set_tests_properties(core_beam_image_lifetime PROPERTIES TIMEOUT 30)
    add_executable(api_binary_build_test tests/api_binary_build_test.cpp)
    target_link_libraries(api_binary_build_test PRIVATE libbeam::libbeam)
    add_executable(api_writable_init_test tests/api_writable_init_test.cpp)
    target_link_libraries(api_writable_init_test PRIVATE libbeam::libbeam)
    add_executable(api_world_test tests/api_world_test.cpp)
    target_link_libraries(api_world_test PRIVATE libbeam::libbeam)
    add_executable(api_work_test tests/api_work_test.cpp)
    target_link_libraries(api_work_test PRIVATE libbeam)
    add_test(NAME api_work_gate COMMAND api_work_test)
    add_executable(api_memory_test tests/api_memory_test.cpp)
    target_link_libraries(api_memory_test PRIVATE libbeam::libbeam)
    add_test(NAME api_memory_gate COMMAND api_memory_test)
    add_executable(api_engine_test tests/api_engine_test.cpp)
    target_link_libraries(api_engine_test PRIVATE libbeam::libbeam)
    if(LIBBEAM_ERTS_PACKAGE)
        add_executable(native_engine_control_test tests/native_engine_control_test.cpp)
        target_link_libraries(native_engine_control_test PRIVATE libbeam_erts)
        add_test(NAME native_engine_control_lifetime COMMAND native_engine_control_test)
        # Internal component test against this checkout's header, not a new SDK.
        add_executable(native_module_table_test tests/native_module_table_test.cpp)
        target_include_directories(native_module_table_test PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/../beam/erts/emulator/beam)
        target_link_libraries(native_module_table_test PRIVATE libbeam_erts)
        target_compile_features(native_module_table_test PRIVATE cxx_std_17)
        add_test(NAME native_owned_module_tables COMMAND native_module_table_test
            -S 2:2 -SDcpu 1:1 -SDio 1 -A 0 -- -root /libbeam-missing-root --
            -boot /libbeam-missing-boot)
        set_tests_properties(native_owned_module_tables PROPERTIES TIMEOUT 20)
        foreach(workers IN ITEMS 0 1 4)
            add_test(NAME native_prepare_without_otp_async_${workers}
                COMMAND engine_prepare_probe -S 2:2 -SDcpu 1:1 -SDio 1 -A ${workers}
                -i libbeam_boot_must_not_run -- -root /libbeam-missing-root --
                -boot /libbeam-missing-boot -s libbeam_boot_must_not_run run)
            set_tests_properties(native_prepare_without_otp_async_${workers} PROPERTIES
                TIMEOUT 20)
        endforeach()
    endif()
    add_test(NAME api_engine_lifetime COMMAND api_engine_test)
endif()
