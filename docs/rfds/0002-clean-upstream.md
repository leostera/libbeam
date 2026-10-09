<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 Leandro Ostera <leandro@ostera.io> -->

# RFD 0002: clean upstream OTP snapshot

## Decision and source verification

Start the host-managed libbeam implementation from **clean upstream OTP**, not the
Realm emulator. `beam/` is an ordinary tracked source snapshot, **not a submodule**.
Edit emulator files directly and commit them alongside `libbeam/` changes. There is
no separately maintained patch series or patch-application build step.

Upstream provenance: `https://github.com/erlang/otp.git`, commit
`cca4e72510a97cfca6427602d3da8a22d5ff7a33` (`30.0-rc0`). At migration, all **12,027**
upstream paths and file modes were compared with the staged snapshot. **12,026 files
match upstream exactly.** The sole source difference is
`erts/emulator/beam/erl_init.c`: the startup-phase extraction carried forward from
`eb019d92`. Its SHA-256 is
`5cf769dc9feba2c23571fd2d5232785a8e6ea16a5e0a0e385bd7fd999f1b8ddc`.
This refactor provides neither engine lifecycle nor isolates.

The initially considered upstream-submodule arrangement was abandoned before commit
in favor of direct source editing. Neither a gitlink, `.gitmodules`, nested `.git`,
nor a patch series is part of the final repository. Upstream bootstrap BEAMs and
intentional binary fixtures are preserved exactly, including files matching ignores.
Two imported upstream `.ort` YAML files retain their existing whitespace rather than
being silently reformatted; they are excluded from the migration's whitespace check.

Local comparison details: `/tmp/libbeam-clean-startup/snapshot-verification.json`.
Future direct emulator changes will naturally increase the difference from upstream;
the counts above describe this migration checkpoint, not a permanent restriction.

## What was removed and preserved

- Removed from the active emulator: Realm membership, tenant management BIFs/wrappers,
  policies, quotas, endpoints, resource guards and boundary tests. No `ErtsRealm`
  implementation is a prerequisite for future work.
- Preserved in this repository's history: the complete previous tree at tag
  `archive/realm-snapshot`, commit `eb019d92`. Historical `0001-*` RFDs, source links,
  inventories, commands and test counts refer to that tree, not today's `beam/`.
- Preserved locally without deleting files: the previous `beam/` directory was moved
  to `/tmp/libbeam-realm-snapshot-local`, including the uncommitted generated
  `erts_internal.beam`. The previous detached test worktree also remains available.
  Local backups are not durable publication or evidence for the new source.
- Retained active tools: build/validation, archive-link probe and standalone startup
  fixture. `libbeam/tools/otp_validation.py` has no Realm profile or runtime dependency.
- Removed duplicated top-level OTP GitHub infrastructure and Realm workflows. Upstream
  infrastructure remains inside the snapshot. The root workflow checks Python tooling
  and snapshot layout only; it is not a hosted runtime/security gate.

The historical lock filename is retained solely to serialize with older local jobs.
It does not retain Realm semantics in the runtime.

## Validation

During migration, a fresh external upstream checkout was built with the same startup
refactor now stored directly in `beam/`. Its tested `erl_init.c` is byte-identical to
the snapshot. No old Realm build artifacts were copied into it. On macOS ARM64,
debug interpreter validation passed:

- 84 focused upstream cases, zero failures/skips.
- 35 atomics/counters/persistent-term cases, zero failures/skips.
- Three fresh-VM startup probes, each checking OTP init, six housekeeping processes
  and 20 ordinary spawn/monitor cycles.
- C++ archive linking and host execution: `linked_not_initialized`.

Those transition logs are under `/tmp/libbeam-clean-startup/{patched,link}`. They
used the temporary upstream-worktree tool layout, not the final snapshot interface.
Historical 148/44/5 results included Realm cases and are not carried forward.

The final snapshot driver again accepts a clean detached **libbeam repository root**
containing `beam/`. It builds directly without applying patches. Its validation helper
and startup fixture live outside the emulator snapshot. Twelve tooling tests pass.
The fresh end-to-end snapshot run at `87d63923` now also passes: configure, bootstrap,
preload regeneration/rebuild, 84 focused cases, 35 resource cases, three startup
probes, and archive linking. This validates the final snapshot interface, not only
the source-equivalent transition. Evidence:

| Summary | SHA-256 |
| --- | --- |
| `/tmp/libbeam-snapshot-validation/results/summary.json` | `0fb29524a38a7b1b40c88711f600111f76c76e6b05b274616b6969ad93ff314d` |
| `/tmp/libbeam-snapshot-validation/link/summary.json` | `f168f7de7f91f58cd4efcc804a32fd2463295f9cd13f900c5e63656005d24964` |

See [`libbeam/README.md`](../../libbeam/README.md#build-and-link-probes) for commands.
Subsequent experimental engine-start changes have
[separate evidence and limitations](0002-engine-start-evidence.md).

## Commit checkpoint

**Can do:** edit/commit ordinary emulator sources on a verified upstream base; run
Realm-free validation; link a C++ probe against the reset emulator's archive.

**Cannot do:** create/destroy an embedded engine, create independent namespaces,
safely reclaim isolates, enforce the reduced profile, or claim latency, density or
security acceptance. Upstream has ambient OS/native capabilities; removing historical
checks is not a security improvement or a substitute for new enforcement.

**Next:** map thread shutdown ownership and implement a complete host-returning
engine lifecycle. Any historical
mechanism reused later must be justified and tested, not imported wholesale.
