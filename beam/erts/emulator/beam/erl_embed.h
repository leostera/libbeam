/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */

#ifndef ERL_EMBED_H__
#define ERL_EMBED_H__

#ifdef __cplusplus
extern "C" {
#endif

/* EXPERIMENTAL POSIX bring-up entry, not a stable libbeam API.
 *
 * Must run once on the host main thread, with no concurrent startup calls.
 * Uses the normal emulator command-line arguments; retain their mutable storage
 * for the lifetime of the process. Embedded startup omits the native forker;
 * executable ports (spawn/spawn_executable) return notsup before effects.
 * BINDIR is not required. Other native effects remain: this is NOT a complete
 * reduced bytecode profile or an approved isolation boundary.
 * Initializes the REAL global OTP runtime and
 * starts its threads, then returns instead of occupying the host main thread.
 * Return does not acknowledge completion of asynchronous OTP boot/application
 * execution. There are no tenant isolates and no local code/atom namespaces.
 *
 * Normal startup does not install standalone signal handlers, a signal dispatcher,
 * a Darwin main-thread pump, or a host alternate signal stack. os:set_signal/2
 * is unsupported. Runtime-owned threads still prepare their own signal stacks.
 * Only default untrapped floating-point environments have been tested. Fatal
 * crash dumping retains legacy signal behavior. CLI errors, boot failure,
 * halt and fatal runtime errors can still terminate the host. No engine shutdown,
 * restart, unload or physical reclamation is implemented.
 * Runtime threads remain live until PROCESS EXIT. Darwin wx/Cocoa main-thread
 * callbacks are unsupported. Trusted standalone bring-up fixtures only.
 *
 * Returns 0 after thread launch/signal publication, 1 for a second startup attempt
 * (including an attempt after ordinary erl_start). Failure is otherwise governed
 * by existing fatal OTP startup semantics, NOT recoverable C++ exceptions.
 */
#if !defined(_WIN32) && !defined(__WIN32__)
int erl_start_embedded(int argc, char **argv);
#endif

#ifdef __cplusplus
}
#endif

#endif
