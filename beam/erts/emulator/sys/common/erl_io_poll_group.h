/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#ifndef ERL_IO_POLL_GROUP_H__
#define ERL_IO_POLL_GROUP_H__

/* Owned pollsets and result buffers used by the real I/O coordinator.
 * The native allocators and polling backend must already be initialized.
 * Not an Engine factory or a running-worker shutdown interface. */
typedef struct ErtsIoPollGroup ErtsIoPollGroup;
ErtsIoPollGroup *erts_io_poll_group_create(int sets, int threads, int scheduler);
/* Exclusive ownership required; no concurrent registration, borrowing or wait.
 * Refuse bound groups and groups handed to workers. */
int erts_io_poll_group_can_discard(ErtsIoPollGroup *);
int erts_io_poll_group_discard(ErtsIoPollGroup *);
struct erts_poll_thread;
/* A successful borrow permanently leaves the supported cold-cleanup scope. */
struct erts_poll_thread *erts_io_poll_group_borrow_thread(ErtsIoPollGroup *, int id);
#endif
