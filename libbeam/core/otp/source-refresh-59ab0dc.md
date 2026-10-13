# Authoritative source refresh at 59ab0dc

The additive integration gate found stale whole-file hashes after the `beam/`
reference tree was replaced. Reviewed against repository commit
`59ab0dc16719a8bf97a9e242f3405b0785bb059f`, not the abandoned ERTS modifications.
All fourteen mismatching manifest entries matched their respective files at
`8c86d346` exactly before updating. Historical `baseline` fields remain intact;
`reviewed_source_refresh` records this later review.

## Reviewed differences

- `atom.c` (also pinned directly by the validation driver): experimental
  namespace tables/text storage are replaced by the standard global table and
  binary-backed global atom literals. Hashing, UTF-8 conversion and ordinal
  comparison arithmetic are unchanged. libbeam's own atom tables remain
  Isolate-owned, fallible and reclaimable; no global literal allocator is linked.
- `erl_fun.h`, `erl_record.h`: removal of experimental namespace constructors;
  restoration of native table initialization and staging declarations. Fun and
  record term layouts are unchanged.
- `beam_load.c`, `emu/emu_load.c`, `emu/load.h`: removal of experimental namespace
  ownership/leases and diagnostic-only gates from ERTS loader state, code and
  literal areas. Signature emission, opcode packing and literal relocation
  algorithms are unchanged. libbeam retains its own explicit code-space and
  literal leases; it does not restore ERTS globals.
- `bif.c`: spawn calls no longer take the abandoned namespace-owner argument.
  The reviewed argument checks, result/error behavior and `raise_3` implementation
  are unchanged.
- `register.c`: experimental per-namespace registry plumbing is replaced by
  upstream global registry plumbing. Registration name checks, hash algorithms
  and process/port behavior remain reference semantics only. The additive
  runtime's explicit owner-local registry and lack of port admission remain.
- `code_ix.h`, `export.h`: experimental namespace APIs removed and native staging
  APIs/global indices restored. libbeam continues to use its independent
  immutable code generation and explicit external-fun leases; it does not adopt
  the global export literal lifetime.
- `global.h`: experimental literal-area owner and namespace declarations removed;
  standard loader/range/persistent-term signatures and signal/driver declarations
  restored. Numeric and comparison algorithms/macros are unchanged.
- `erl_bif_info.c`: restored `system_info(break_ignored)` branch is outside the
  admitted native catalog. No new system-info authority follows from the hash.
- `erl_unix_sys.h`: restored Unix late-init and signal-stack declarations do not
  change the floating-point conversion code used by the bit writer.

The full diff reviewed is reproducible with `git diff 8c86d346 59ab0dc --` and
these paths under `beam/erts/emulator/`. Refreshing provenance is not validation
of new runtime capabilities.

## Integration evidence

`python3 -B libbeam/tools/run_additive_slice.py --output
/tmp/libbeam-profile-integration-current-4` completed with `passed=true` and
211 commands. Debug and Release each passed all 48 CTests. The existing UBSan
selection passed 31 tests and the added map/external-fun/trace-preparation
selection passed five. Existing lifecycle/two-Isolate checks also ran in this
gate. Logs and input digests are in that external output directory.

The full run exposed a lifecycle-test race: an intentionally infinite loop
exhausted the default invocation quota during unrelated peer checks in Release.
That cancellation-only Isolate now explicitly requests `UINT64_MAX` work;
default/quota tests remain unchanged. Five repeated focused Release runs passed
before the fresh full gate. The test reports the exact assertion location and
actual error on future mismatches.

The Midgard application still requires actual successful public-Isolate
execution; these results do not establish it. Exception preparation is not
catch/unwind or `raise/3` admission.
