/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 Leandro Ostera <leandro@ostera.io>
 */
#ifndef LIBBEAM_CORE_CAPABILITY_H
#define LIBBEAM_CORE_CAPABILITY_H
/* Only explicitly reviewed unavailable capabilities use this outcome. Unknown
 * imports/instructions and ordinary missing implementations remain refusals. */
typedef enum {
    LB_CAPABILITY_CODE_LOADING,
    LB_CAPABILITY_NATIVE_LIBRARY,
    LB_CAPABILITY_HOST_OUTPUT,
    LB_CAPABILITY_HOST_CONTROL,
    LB_CAPABILITY_HOST_PORT,
    LB_CAPABILITY_DISTRIBUTION
} LbCapability;
/* Descriptors and all strings have static storage duration. Never point into
 * guest atoms, heaps, code arenas or arguments. This survives World teardown
 * without allocation, interning, GC roots or code leases. */
typedef struct {
    LbCapability capability;
    const char *module,*function;
    unsigned arity;
    const char *message; /* bounded compile-time MFA/capability diagnostic */
} LbCapabilityFailure;
#endif
