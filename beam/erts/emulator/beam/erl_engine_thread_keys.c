/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "sys.h"
#include "erl_threads.h"
#include "erl_engine.h"
#include "erl_engine_thread_keys.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <errno.h>

typedef struct ErtsEngineThreadKey {
    erts_tsd_key_t key;
    struct ErtsEngineThreadKey *next;
} ErtsEngineThreadKey;
struct ErtsEngineThreadKeys {
    atomic_int lock;
    ErtsEngineThreadKey *head;
    size_t count;
};
/* One engine binding, installed before platform/lock-check initialization.
 * Never selects an isolate and never changes while keys/users remain alive. */
static struct ErtsEngineThreadKeys *bound_keys;

static void lock_keys(struct ErtsEngineThreadKeys *keys)
{
    while (atomic_exchange_explicit(&keys->lock, 1, memory_order_acquire)) {}
}
static void unlock_keys(struct ErtsEngineThreadKeys *keys)
{
    atomic_store_explicit(&keys->lock, 0, memory_order_release);
}

int erts_engine_init_thread_keys(ErtsEngine *engine)
{
    struct ErtsEngineThreadKeys *keys;
    if (!engine || engine->startup_phase != ERL_RUNTIME_UNCLAIMED ||
        engine->thread_keys || bound_keys) return EBUSY;
    keys = calloc(1, sizeof(*keys));
    if (!keys) return ENOMEM;
    atomic_init(&keys->lock, 0);
    engine->thread_keys = bound_keys = keys;
    return 0;
}

int erts_engine_track_tsd_key(erts_tsd_key_t key)
{
    struct ErtsEngineThreadKeys *keys = bound_keys;
    ErtsEngineThreadKey *entry;
    if (!keys) return EINVAL;
    entry = malloc(sizeof(*entry));
    if (!entry) return ENOMEM;
    entry->key = key;
    lock_keys(keys);
    entry->next = keys->head;
    keys->head = entry;
    keys->count++;
    unlock_keys(keys);
    return 0;
}

int erts_engine_delete_tsd_key(erts_tsd_key_t key)
{
    struct ErtsEngineThreadKeys *keys = bound_keys;
    ErtsEngineThreadKey **link, *entry;
    int result;
    /* Allocator/thread-progress domains own their raw ethread keys separately.
     * Locate ownership BEFORE deletion: the OS may immediately reuse key IDs.
     * Never remove a newer owner's record after deleting an untracked key. */
    if (!keys) return ethr_tsd_key_delete(key);
    lock_keys(keys);
    for (link = &keys->head; *link && (*link)->key != key; link = &(*link)->next) {}
    entry = *link;
    result = ethr_tsd_key_delete(key);
    if (!result && entry) { *link = entry->next; keys->count--; }
    unlock_keys(keys);
    if (!result) free(entry);
    return result;
}

size_t erts_engine_thread_key_count(const ErtsEngine *engine)
{
    size_t count;
    struct ErtsEngineThreadKeys *keys = engine ? engine->thread_keys : NULL;
    if (!keys) return 0;
    lock_keys(keys);
    count = keys->count;
    unlock_keys(keys);
    return count;
}

size_t erts_engine_thread_keys_shared_bytes(void)
{
    struct ErtsEngineThreadKeys *keys = bound_keys;
    size_t bytes;
    if (!keys) return 0;
    lock_keys(keys);
    bytes = sizeof(*keys) + keys->count * sizeof(ErtsEngineThreadKey);
    unlock_keys(keys);
    return bytes;
}

int erts_engine_discard_empty_thread_keys(ErtsEngine *engine)
{
    struct ErtsEngineThreadKeys *keys = engine ? engine->thread_keys : NULL;
    /* Exclusive control-thread teardown. An empty list alone is not proof that
     * no thread can enter key creation; admission/quiescence belongs to caller. */
    if (!keys || keys != bound_keys || keys->head || keys->count) return 1;
    bound_keys = NULL;
    engine->thread_keys = NULL;
    free(keys);
    return 0;
}
