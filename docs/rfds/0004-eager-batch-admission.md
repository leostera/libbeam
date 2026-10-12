<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 Leandro Ostera <leandro@ostera.io> -->

# RFD 0004 eager transactional module groups

Status: **The C loader, public Isolate API, and example runner now admit an
exact-name module group transactionally, including forward and mutual imports.**
This is a loader checkpoint only. The unchanged Midgard `hello_worker` dependency
closure still needs additional native/Elixir capabilities and has not been
executed successfully.

## Contract

`lb_code_load_batch()` and `Isolate::load_modules()` take a bounded group of BEAM
images. The module names come only from each image; there are no filesystem aliases,
renaming, lazy loads, or post-start admission. A group has a maximum of 4,096
images and 64 MiB of aggregate encoded input. The C++ adapter copies only views;
BEAM image and preparation storage is owned by the Isolate allocation domain.

Admission has these private stages:

1. Copy/decode all programs while atom destinations remain unresolved and retain
   each program's namespace lease.
2. Prepare one atom transaction from the concatenated file/ETF names, then map all
   staged name destinations and generic operands to provisional namespace terms.
3. Reject duplicate/existing module names; prepare every module's exports, import
   records, labels, lambdas, and private storage before resolving any imports.
4. Resolve every imported MFA against the exact candidate module names (including
   forward/self/mutual references) or the already-published private code space.
   Validate aggregate importer-lease capacity, then verify, transform, emit, and
   relocate every module. An unresolved or unsupported import/function refuses
   the whole group, including unused functions.
5. Commit the atom transaction and publish every export/module using only
   allocation-free operations. No partial public namespace or code graph exists.

Any failure before commit aborts the atom transaction and releases all image,
program, code, dependency, and temporary-array allocations. Retry in the same
still-loading world is supported. No rollback of guest execution is implied.

Module imports retain their owners. A mutual-import cycle cannot be physically
unloaded one module at a time because every member has an importer lease. Whole
world retirement therefore unlinks the closed code namespace and destroys the
fully drained set as one group after checking that no code entry/users remain.
This preserves exact physical release without inventing reload or cycle-breaking
semantics. Independent worlds own different code and atom namespaces and can admit
the same exact module names concurrently.

## Public and runnable surface

The C++ `Isolate::load_module()` is the one-image convenience path through the
same transaction. `Isolate::load_modules(vector<ByteView>)` admits an application
group. `run_beam` groups each `--isolate MAIN.beam` with all following
`--dependency DEP.beam` images and admits them together; dependencies may appear
in any order. Multiple groups are separate simultaneous Isolates on the Engine's
shared worker.

The regression fixtures `batch_alpha` and `batch_beta` have forward and mutual
imports; `batch_alpha:base/0` returns a static literal tuple, exercising retained
literal/code lifetime through call and retirement. The tests exercise both input
orders and actual binary calls on the public worker. `batch_unsupported` includes
a valid but currently unadmitted multi-entry map literal. Failure tests cover
missing imports, corrupt images, unsupported BEAM, duplicate module names, exact
atom/allocation rollback, retry, all allocator failure prefixes of a successful
batch, mutual-cycle retirement, independent code spaces, simultaneous Isolates,
repeated calls and replacement. These fixtures establish admission semantics
only; they do not expand the application's admitted language profile.

## Validation

Fresh `/tmp/libbeam-batch-admission-integrated-final-third/summary.json` reports
`passed=true` across 209 commands. Debug and Release each pass all 38 CTests;
the focused UBSan run passes 31 selected tests, including the core/public batch
tests and grouped runner. The unchanged Engine lifecycle and two-Isolate acceptance
also pass. The strict Debug build uses `-Wall -Wextra -Werror`; the first strict
attempt exposed a signed-byte test-fixture initializer, corrected before this clean
run. No Midgard application-success or performance claim follows from the fixtures.
