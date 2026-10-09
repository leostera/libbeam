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
 * for the lifetime of the process. BINDIR must name the absolute configured
 * emulator directory (normally supplied by erlexec); ordinary native forker/port
 * support is retained. This is NOT the proposed reduced bytecode profile.
 * Initializes the REAL global OTP runtime and
 * starts its threads, then returns instead of occupying the host main thread.
 * Return does not acknowledge completion of asynchronous OTP boot/application
 * execution. There are no tenant isolates and no local code/atom namespaces.
 *
 * Takes process-wide signal/alternate-stack ownership. CLI errors, boot failure,
 * halt and fatal runtime errors can still terminate the host. No engine shutdown,
 * signal restoration, restart, unload or physical reclamation is implemented.
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
