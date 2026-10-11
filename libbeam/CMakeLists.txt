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
    core/beam_metadata.c core/beam_literals.c core/beam_select.c core/binary.c core/term_compare.c
    core/beam_transform.c core/beam_emit.c core/beam_verify.c core/code.c core/engine.c core/executor.c core/world.c core/md5.c core/process.c core/heap.c core/bif_info.c)
add_dependencies(libbeam_core libbeam_atom_definitions libbeam_opcode_definitions)
target_link_libraries(libbeam_core PUBLIC ZLIB::ZLIB Threads::Threads)
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

include(CTest)
if(BUILD_TESTING)
    add_test(NAME engine_lifecycle_acceptance COMMAND engine_lifecycle)
    set_tests_properties(engine_lifecycle_acceptance PROPERTIES TIMEOUT 20)
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
    add_executable(api_world_test tests/api_world_test.cpp)
    target_link_libraries(api_world_test PRIVATE libbeam::libbeam)
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
