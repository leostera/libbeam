/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#ifndef ERL_ENGINE_THREAD_KEYS_H__
#define ERL_ENGINE_THREAD_KEYS_H__
#include "erl_embed.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Before platform initialization: system memory only, no VM allocators. */
int erts_engine_init_thread_keys(ErtsEngine *);
size_t erts_engine_thread_key_count(const ErtsEngine *);
size_t erts_engine_thread_keys_shared_bytes(void);
/* Does not delete live keys or their values. Shutdown must first retire every
 * user/value and explicitly delete its keys through erts_tsd_key_delete. */
int erts_engine_discard_empty_thread_keys(ErtsEngine *);
#ifdef __cplusplus
}
#endif
#endif
