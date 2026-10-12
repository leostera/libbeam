# Runnable bounded-profile examples

`run_beam.cpp` is a small host runner over the production public Engine/Isolate
API—not another evaluator, manifest loader or serving OTP VM. It supersedes the
former test-only `api_world_test --application` mode. Language sources below are
also the compiler-produced fixtures used by the integrated validation; there is no
separate reduced implementation for examples.

## Build and run

```sh
cmake -S libbeam -B /tmp/my-libbeam-build -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/my-libbeam-build --target run_beam
mkdir -p /tmp/my-beams
erlc -o /tmp/my-beams libbeam/tests/fixtures/async_slice.erl
/tmp/my-libbeam-build/run_beam --entry async_slice identity \
  --call hello --expect hello --call again --expect again \
  --isolate /tmp/my-beams/async_slice.beam \
  --isolate /tmp/my-beams/async_slice.beam
```

Each `--isolate MAIN.beam` declares a separate world. Subsequent `--dependency
DEP.beam` options belong to that world, **not** to independent worlds. Dependencies
are eagerly loaded before the main module; exact names come from BEAM, never path
aliases. At this checkpoint the single-module loader needs dependencies ordered
before importers; transactional graph/mutual-dependency admission is unfinished.
All groups are loaded before any guest invocation. Post-start loading stays
forbidden. All worlds coexist; calls run round-robin with optional actor bootstrap.

`--entry MODULE FUNCTION` declares an arity-one binary entrypoint. Repeat `--call
TEXT [--expect TEXT]` to reuse each world; `--call-file FILE [--expect-file FILE]`
supplies arbitrary bytes, including NUL. The runner verifies byte equality when
expected output is declared and prints actual result bytes as hex. An expectation
mismatch or guest/loader error exits nonzero. Host wait deadlines are five seconds;
that is not an execution budget. All selected worlds are stopped, physically
reclaimed and the Engine shut down on the normal result/error-report path; fatal
host exceptions use the public RAII ownership path without claiming successful
reclamation in output.

## The examples are the fixtures

Compile the linked `.erl` source into `/tmp/my-beams`, then select its entry below.
The integrated runner executes the same examples and asserts the same bytes.

| Language feature | Source | Entry and request/response |
| --- | --- | --- |
| Copied binary calls and world reuse | [async_slice.erl](../tests/fixtures/async_slice.erl) | `identity`, `hello` → `hello`; `again` → `again` |
| Captured and nested local closures | [fun_slice.erl](../tests/fixtures/fun_slice.erl) | `nested`, `captured` → `captured` |
| Tuple/list construction and consumers | [shape_slice.erl](../tests/fixtures/shape_slice.erl) | `list_binary`, `shape` → `shape` |
| Native bignum decimal continuation | [decimal_slice.erl](../tests/fixtures/decimal_slice.erl) | `negative`, empty input → `-18446744073709551617` |
| Persistent correlated actor, registry, reference, monitor and binary match | [probe.erl](../tests/fixtures/two_isolates/a/probe.erl), [shared implementation](../tests/fixtures/two_isolates/probe_impl.hrl) | bootstrap `boot`; entry `request`; `next` → `A:1`, `next` → `A:2`, `read` → `A:2` |

For example, compile the unchanged persistent actor fixture:

```sh
mkdir -p /tmp/my-beams/a
erlc -I libbeam/tests/fixtures/two_isolates -o /tmp/my-beams/a \
  libbeam/tests/fixtures/two_isolates/a/probe.erl
/tmp/my-libbeam-build/run_beam --entry probe request \
  --boot boot '' --expect-boot A:ready \
  --call next --expect A:1 --call next --expect A:2 --call read --expect A:2 \
  --isolate /tmp/my-beams/a/probe.beam --isolate /tmp/my-beams/a/probe.beam
```

Both actors must independently return counts 1, 2, 2. For deliberately different
same-name module versions, omit shared expectations and inspect each world's bytes,
or use the unchanged [two-Isolate acceptance example](two_isolates.md), which
asserts each version's own expected bytes and stronger lifecycle behavior.

## What compatibility means

The [core boundary](../core/README.md) is a positive, bounded native BEAM profile,
not general Erlang/OTP or Elixir compatibility. Entire modules—including unused
functions/imports—must pass representation, ownership/dataflow, selected native
case and linker checks. The presence of an opcode or native implementation does
not establish admission for every operand shape.

Loader errors now include a stage/opcode and location in the public diagnostic.
`stage=unresolved import, import_index=N, mfa="MODULE":"FUNCTION"/ARITY`
identifies the zero-based BEAM import entry and its copied, escaped exact name;
supply the real dependency,
or implement its required native slice. Other offsets are loader byte/instruction
locations, not source lines. Static Packer reports list named imports and
conditionally supported operations, explicitly report a profile revision and keep
`execution=not_verified`; only actual admission/execution settles compatibility.

Selected current limits: copied input/result ≤64 KiB, 64 outstanding calls/world,
1 MiB reserved queued transport. **These are not heap/CPU quotas.** The
[resource policy](../../docs/rfds/0004-isolate-resource-policy.md) selects
256 MiB accounted world memory and per-invocation work enforcement. Memory defaults
and overrides are exposed/enforced. [Invocation wiring](../../docs/rfds/0004-invocation-work-wiring.md)
now charges actual shared-worker/native paths, attributes child/message/background
work and distinguishes private cancellation/reuse from escaped-world retirement.
Native coverage and failure-boundary audit remain unfinished. See the
[memory checkpoint](../../docs/rfds/0004-accounted-memory.md) for accounting exclusions. Whole Midgard remains an
[honestly refused application attempt](../../docs/rfds/0003-midgard-application-attempt.md),
not a runnable Elixir compatibility example yet. No security, RSS, latency,
throughput, density, Linux or multicore claim is made.

## Verification

```sh
python3 -B libbeam/tools/run_additive_slice.py --output /tmp/fresh-example-validation
```

This acquires the shared validation lock, compiles the fixtures, runs the example
runner and unchanged lifecycle/two-Isolate acceptance, and preserves separate
stock-OTP reference versus actual worker execution evidence. Generated BEAMs are
not committed.
