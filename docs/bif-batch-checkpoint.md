<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 Leandro Ostera <leandro@ostera.io> -->

# Workspace checkpoint before the BIF batch

This commit preserves the current workspace before starting the user-requested
complete BIF inventory/pass. It is a source checkpoint, not application acceptance
or a claim that every historical probe path is verified.

## Included state

- Additive runtime compatibility work through `node/0`: 83 native BIF entries
  plus the separate `apply/3` instruction export, associated fixtures, provenance,
  validation tooling and application-attempt documentation.
- Existing shared-preparation/constant-arena probe changes, the shared-prefix
  native test, startup-probe tooling and the Fibonacci example source. These were
  previously separate dirty work; the user explicitly requested a workspace
  checkpoint including existing changes. No historical changes were discarded.
- The existing deletion of the superseded refused-map fixture is preserved;
  map support and explicit remaining refusal fixtures are included.

Generated BEAMs, build/cache outputs, temporary experiment artifacts, `.pi/`
state and secrets are excluded. Staging is limited to the reviewed changed/new
source, configuration, documentation and test paths. Nothing is pushed.

## Evidence actually available

`/tmp/libbeam-node-integrated-final/summary.json` reports `passed=true` for 273
commands: 108 Debug CTests, 108 Release CTests, 31 existing plus 65 profile UBSan
tests, and the existing Engine lifecycle/two-Isolate acceptance. All 381 recorded
source-input hashes still matched the workspace when preparing this checkpoint.
This verifies the selected additive profile, not every historical/native-ERTS
probe configuration included in the checkpoint. Whitespace checks and targeted
credential/binary-artifact scans found no findings in the checkpoint candidates;
these are not a security certification.

The user-installed Elixir `1.21.0-dev (59531b7)` fixes the Dict qualification bug.
The original 32-module Midgard artifact remains unchanged. Its refreshed
379-module dependency closure has zero missing non-native import bindings in
an independent structural audit. Actual libbeam admission still refuses
`erlang:nodes/0` from Elixir.GenServer, with clean reclamation; no hello_worker
response bytes have yet been produced. See
[the application attempt](rfds/0003-midgard-application-attempt.md).

## Next work, not yet implemented by this checkpoint

First inventory every MFA from the user's complete list, including existing
support and compiler/dynamic-call forms; then implement and validate them as one
coordinated pass before further application-driven retries. Explicitly catalogued
unavailable capabilities will resolve at admission but fail invocation at the
host boundary, uncatchably by guest code. Genuine absent/unclassified imports
remain load-time failures. The authorized `load_module/2` compatibility exception
is an already-loaded no-op, never new loading or code replacement. These revised
policies require implementation and tests; merely recording them here does not
make them available in this checkpoint.
