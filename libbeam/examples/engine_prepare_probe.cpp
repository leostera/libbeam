// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Leandro Ostera <leandro@ostera.io>

// Native preparation diagnostic, NOT an Engine/destruction/isolate witness.
#include <erl_embed.h>
#include <libbeam/engine.hpp>
#include <algorithm>
#include <cstdint>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <vector>
#ifdef __APPLE__
#include <mach/mach.h>
#elif defined(__linux__)
#include <dirent.h>
#else
#error "OS thread observer is implemented only for macOS and Linux"
#endif

static std::vector<std::uint64_t> os_threads() {
    std::vector<std::uint64_t> ids;
#ifdef __APPLE__
    thread_act_array_t threads = nullptr;
    mach_msg_type_number_t count = 0;
    if (task_threads(mach_task_self(), &threads, &count) != KERN_SUCCESS)
        std::abort(); // Observer failure, not an emulator cleanup path.
    bool ok = true;
    for (mach_msg_type_number_t i = 0; i < count; ++i) {
        thread_identifier_info_data_t info{};
        mach_msg_type_number_t size = THREAD_IDENTIFIER_INFO_COUNT;
        if (thread_info(threads[i], THREAD_IDENTIFIER_INFO,
                        reinterpret_cast<thread_info_t>(&info), &size) != KERN_SUCCESS)
            ok = false;
        else
            ids.push_back(info.thread_id);
        if (mach_port_deallocate(mach_task_self(), threads[i]) != KERN_SUCCESS)
            ok = false;
    }
    if (vm_deallocate(mach_task_self(), reinterpret_cast<vm_address_t>(threads),
                      count * sizeof(thread_t)) != KERN_SUCCESS || !ok)
        std::abort();
#else
    DIR* directory = opendir("/proc/self/task");
    if (!directory) std::abort();
    errno = 0;
    while (auto* entry = readdir(directory)) {
        if (entry->d_name[0] >= '0' && entry->d_name[0] <= '9')
            ids.push_back(std::strtoull(entry->d_name, nullptr, 10));
    }
    const int error = errno;
    if (closedir(directory) != 0 || error != 0) std::abort();
#endif
    std::sort(ids.begin(), ids.end());
    return ids;
}

static bool factory_rejects(libbeam::ErrorCode expected) {
    auto result = libbeam::Engine::create();
    auto* error = std::get_if<libbeam::Error>(&result);
    return error && error->code == expected;
}

int main(int argc, char** argv) {
    const auto before = os_threads();
    ErtsEngine* engine = erl_engine_alloc();
    ErtsEngine* candidate = erl_engine_alloc();
    if (!engine || !candidate || engine == candidate || erl_runtime_is_claimed() ||
        erl_runtime_startup_phase(engine) != ERL_RUNTIME_UNCLAIMED ||
        erl_runtime_startup_phase(candidate) != ERL_RUNTIME_UNCLAIMED ||
        !factory_rejects(libbeam::ErrorCode::not_implemented)) return 10;
    ErlPreparedRuntimeInventory inventory{91, 92, 93, 94, 95};
    if (erl_prepared_runtime_inventory(engine, &inventory) != 1 ||
        inventory.processes != 91 || inventory.ports != 92 ||
        inventory.loaded_code_bytes != 93 || inventory.init_process_created != 94 ||
        inventory.system_process_roots != 95) return 11;

    if (erl_prepare_runtime(engine, argc, argv) != 0 ||
        erl_runtime_startup_phase(engine) != ERL_RUNTIME_PREPARED) return 12;
    if (erl_prepared_runtime_inventory(engine, &inventory) != 0 ||
        inventory.processes != 0 || inventory.ports != 0 ||
        inventory.loaded_code_bytes != 0 || inventory.init_process_created ||
        inventory.system_process_roots != 0) return 13;
    ErlSchedulerThreadInventory threads{};
    erl_scheduler_thread_inventory(engine, &threads);
    const auto after = os_threads();
    std::fprintf(stderr, "HOST_PREPARE_OBSERVED processes=%zu ports=%zu code_bytes=%zu "
                 "scheduler_threads=%zu os_threads_before=%zu os_threads_after=%zu\n",
                 inventory.processes, inventory.ports, inventory.loaded_code_bytes,
                 threads.total, before.size(), after.size());
    if (threads.total != 0 || after != before) return 14;

    // Preparation has claimed native state even though it launched no threads.
    if (!factory_rejects(libbeam::ErrorCode::invalid_state) ||
        erl_prepare_runtime(engine, argc, argv) != 1 ||
        erl_start_embedded(engine, argc, argv) != 1 ||
        erl_runtime_startup_phase(engine) != ERL_RUNTIME_PREPARED ||
        os_threads() != before) return 15;

    // Other control objects have their own fields, but may not initialize the
    // unmigrated global substrate. Rejection must not mutate that candidate.
    inventory = {91, 92, 93, 94, 95};
    if (erl_runtime_startup_phase(candidate) != ERL_RUNTIME_UNCLAIMED ||
        erl_prepare_runtime(candidate, argc, argv) != 1 ||
        erl_runtime_startup_phase(candidate) != ERL_RUNTIME_UNCLAIMED ||
        erl_prepared_runtime_inventory(candidate, &inventory) != 1 ||
        inventory.processes != 91 || inventory.ports != 92 ||
        inventory.loaded_code_bytes != 93 || inventory.init_process_created != 94 ||
        inventory.system_process_roots != 95 ||
        erl_engine_discard_uninitialized(candidate) != 0 ||
        erl_engine_discard_uninitialized(engine) != 1 ||
        erl_runtime_startup_phase(engine) != ERL_RUNTIME_PREPARED) return 16;
    std::puts("HOST_PREPARE_OWNER_OK candidate_unchanged=true initialized_owner_retained=true");

    std::printf("HOST_PREPARE_OK processes=0 ports=0 code_bytes=0 system_roots=0 "
                "scheduler_threads=0 os_threads_unchanged=true host_control=true\n");
    std::puts("HOST_PREPARE_LIMITS cleanup=false isolates=0 process_exit=true");
    std::fflush(stdout);
    // Native allocations are still process-lifetime. Do not claim destruction.
    std::_Exit(0);
}
