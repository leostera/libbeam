# Additive C core

See [RFD 0003](../../docs/rfds/0003-additive-runtime-construction.md) and the
[first-execution record](../../docs/rfds/0003-first-execution.md) and
[ordinary-binary extension](../../docs/rfds/0003-owned-binaries.md) and
[C Engine lifecycle](../../docs/rfds/0003-engine-lifecycle.md) and
[owned asynchronous executor](../../docs/rfds/0003-owned-executor.md) and
[public Isolates/calls](../../docs/rfds/0003-public-isolates.md) and
[local calls/Y roots](../../docs/rfds/0003-local-calls.md) and
[exact comparisons](../../docs/rfds/0003-exact-comparison.md) and
[tuple/list consumers and shape proofs](../../docs/rfds/0003-tuple-list-shapes.md) and
[native numeric addition](../../docs/rfds/0003-numeric-addition.md) and
[decimal continuations](../../docs/rfds/0003-decimal-continuations.md).

This is the C runtime construction boundary, not a whole-ERTS link. It now loads
and executes ordinary `first_slice.erl` through generated BEAM transformations,
specific-instruction words and generated interpreter cases. Tuple allocation,
copying GC, literal/atom returns, `module_info/0,1`, bounded yields, exceptions and
physical code retirement have execution consumers. This is a **limited profile**,
not full A03–A07 coverage or full Erlang process machinery. Public Isolates now run
this profile with copied calls and physical reclamation. The unchanged Engine
lifecycle and unchanged stateful two-Isolate examples pass in Debug, Release and
available UBSan. Whole Midgard/Elixir application compatibility is still being
constructed; [the earlier application attempt](../../docs/rfds/0003-midgard-application-attempt.md)
records a real refusal, not execution. [RFD 0004](../../docs/rfds/0004-isolate-resource-policy.md)
selects memory/invocation policy. [World memory accounting/enforcement](../../docs/rfds/0004-accounted-memory.md)
is implemented; invocation work charging and full causal-family cancellation are
still under construction.

## Boundaries

- `engine.c`: fallible C parent for the actual allocation/native catalog substrate.
  The existing executor also owns internal background local-fun tasks: queue
  storage precedes child preparation, publication is nonfallible, terminal
  children retire physically, and serialized world stop drains persistent
  children before code-space retirement. Worker-produced closure tests cover
  allocation-prefix rollback, source destruction, peer progress and stop.
  Native `spawn/3` now executes unmonitored `erlang:apply(F,[])` startup for
  owned local fun/0, with copied environments and worker-only execution.
  Malformed module/function/argument-list inputs retain native badarg; other
  valid MFAs/apply shapes refuse the runtime profile, not a guest exception.
  `preloaded.c` loads ordinary compiler-produced BEAM for complete pinned
  `spawn/1`, `spawn_monitor/1` and their badarg helper into each world. Protected
  bootstrap cannot be replaced by guest loading; these are not native aliases.
  `spawn_opt/4` admits monitored local fun/0 startup, preparing owned monitor
  halves before publishing the child. Other valid options/startup shapes refuse.
  Spaces retain it through physical release; shutdown refuses live children;
  out-of-order owner drop defers cleanup to the last physical child. No singleton
  or implicit world. The C++ factory owns this C handle.
- `executor.c`: one lazy joinable POSIX worker shared by real retained task/process
  contexts. Bounded generated execution, cancellation at safepoints and physical
  join/cleanup. Internal raw tasks and public calls use the same interpreter.
- `world.c`: C world/call/reclamation ownership, private code-space loading, bounded
  copied transport and once-only completion. Closing cancels pending invocations;
  reclamation physically frees code/atoms/heaps while finite host controls survive.
  Spawned and waiting processes and their owned messages drain on stop. Receive
  deadlines belong to actors rather than external callbacks; broader timer and
  timer services remain pending.
- `message.c`, `receive.c`: selective receive through whole native scan/removal/
  wait/timeout cases, GC-rooted actor copies, waiting-task exclusion and wakeup
  after send publication. Compiler-produced persistent request/reply and timeout
  fixtures execute on the shared worker; copied-closure failure prefixes drain
  queues and identities. Monotonic deadlines use bounded idle polling, not native
  timer wheels or a hard latency guarantee. Registered destinations now resolve
  in the owning namespace. Broader signals remain unadmitted.
- `monitor.c`: native monitor-node/data prefixes, owned ordinary references and
  circular origin/target half lists under Engine serialization. Normal and kill
  DOWN messages copy before queue publication; demonitor/2 accepts flush, repairs
  queue anchors and releases both halves. Private cancellation detaches silently.
  Allocation failures propagate infrastructure failure, not fabricated DOWN.
  Error-class DOWN now captures real MFA/argument/continuation frames;
  aliases, distribution, info options and non-kill exit/2 are not admitted.
  Actual monitored worker actors cover normal exit, waiting-child kill, flush,
  rollback and reclamation. Unchanged two_isolates now passes Debug/Release/UBSan.
- `receive_marker.c`: all four native receive-marker dispatch cases execute.
  Eight-slot reuse/reference binding owns stable ordinary-reference storage.
  Serialized tail anchors replace non-message queue insertion; message unlink
  repairs borrowed anchors before freeing nodes. Unknown/stale references leave
  the scan unchanged. Worker requests use compiler-emitted plain-reference markers;
  allocation-prefix, slot-reuse, GC and predecessor-removal tests cover ownership.
- `registry.c`: native atom-index hash/equality and Fibonacci/chained buckets;
  fallible name association before publication, logical-exit removal before PID
  retirement, empty-table reclamation and registered-name message delivery.
  Actual worker receive and failure-prefix tests verify cleanup. Ports are absent.
- `bif_process.c`: native error/1 and complete error/3 option scanning/error/2
  fallback; retain raw arglist/error-info payload and expanded reason separately.
  Empty/single-entry MAP_EXT literal flatmaps supply wrapper metadata; larger maps,
  line locations, saved ftrace and catch/unwind remain unadmitted.
- `stacktrace.c`: immediate uncaught-error capture/build follows native
  save_stacktrace/gather_stacktrace/build_stacktrace/MFA construction, with pinned
  default depth 8 and the existing explicit no-line-info policy. Code-pointer/MFA
  snapshots have no moving aliases; reserve before borrowing raw error payloads.
  Real tuple4 frames preserve arglists, BIF metadata and non-tail continuations.
  Monitor DOWN copies the expanded reason and trace, never fabricated empty frames.
  Terminal interpreter roots publish before capture GC and reload afterward.
  Worker allocation-prefix tests require actual frame/argument/error-info contents,
  including an empty error-info flatmap with its native zero-arity key tuple.
  This is not raw ftrace/bignum packaging or catch/unwind; unsupported BIF cause
  metadata still refuses rather than producing a success-shaped trace.
- `binary_match.c`: native SubBits match-context flags/origin roots and complete
  start-match, ensure/read/extract/equality/tail dispatch groups. Whole native
  bs_match/selector generators are compiled, but flow admits only bounded local
  unsigned big-endian small-integer/equality packets and sparse two-small selectors.
  Match cursors are not guest binaries: moves and GC retain them, heap-origin base
  pointers reload after GC, and joins preserve possible-context poison to prevent
  escape through calls or containers. Bitdata survives host-budget safepoints.
  No binary extraction, signed/little-endian, dense/general selector or context-call
  authority follows from generator availability. Private-append comprehension
  still lacks interprocedural entry facts. Message copies freeze nested writers.
  The existing stateful fixtures now execute, without source changes; tests add
  32 replacements/4096 correlated calls, actor GC, queued stop, detached host replies,
  boot allocation prefixes, peer progress and exact final allocation balance.
- `alloc.c`: serialized fallible allocation domains, exact-base release and busy
  destruction. Not a guest quota or a performant carrier allocator.
- `term.h`, `atoms.c`: selected flat 64-bit BEAM terms and private atom namespaces.
  Bindings retain namespaces. Bare atom words are namespace-relative, not
  provenance-bearing capabilities. Prepared atom transactions also support
  allocation-free commit after real code linking; abort restores backing and names.
- `beam_image.c`: owned IFF bytes, file atoms, imports/exports and code header.
  Image success alone grants no execution permission.
- `beam_program.c`: owned generic operations, supported ETF literals and type/lambda
  declarations. Its standalone preparation API still does not publish executable
  code. The internal loader leaves atom identities provisional until publication.
- `beam_transform.c`, `beam_emit.c`, `beam_verify.c`: generated whole transform
  cases with fallible operation allocation, native word packing/fixups, and
  profile-specific register/stack/heap/shape dataflow admission. Tuple/list reads
  need proven shapes and bounds across all incoming edges. Proof scratch is
  physically released before transformation; untrusted type hints are stripped
  from executable registers before native adjacency optimizations. Missing dependencies
  reject admission; they are never false/success-shaped helper implementations.
- `code.c`: private export/module lookup, eager resolution of **all** imports,
  atomic publication, retained entries/frames/import dependencies, and guarded
  physical retirement. Construction requires an Engine, whose catalog is used
  by import binding and transformation; mutable exports remain private.
  No lazy loading, hot reload, NIF loading or root swapping.
- `process.c`, `heap.c`: explicit X/Y/continuation/reduction/exception state,
  bounded generated interpreter dispatch and selected copying-GC algorithms.
  Local calls use verified entry labels and native continuations; private-entry
  reduction yields resolve real function headers. Saved Y roots survive native
  allocation and forced copying collection, including allocation failure/retry.
  No per-process workers, scheduler-data TLS, global process table or implicit OTP services.
- `binary.c`: real immutable refcounted payloads, native BinRef/SubBits layouts,
  owner-local offheap chains, GC sweep and physical release. Literal/metadata
  decoding and copied byte-input invocation share this constructor.
- `beam_transform.c`: additionally projects the complete native `create_bin`
  generator after structural descriptor validation and with fallible owned
  arrays. `beam_emit.c` packs native words and fixes retained literal/string
  pointers, checking whole string spans across code growth. These are preparation
  dependencies only: register/root and private-slot consumption proofs now
  precede transforms, but runtime uniqueness checks remain mandatory and the
  whole dispatch case remains gated. Facts never come from compiler hints.
- `binary_vheap.c`: native growth/shrink thresholds with deterministic binary64
  rounding in integer arithmetic. Each process owns its soft pressure threshold;
  fresh offheap construction can collect without exhausting term-heap space.
  Collection accounts both retained-capacity chains, not a hard quota or native
  generational/apparent-size accounting. Instruction work policy is still pending.
- `exception.c`: native scalar reason codes and error-value expansion. The
  executor owns expanded terminal values as real GC roots; reason flags are
  not terms. Binary failure preparation atomically sets extended payload/code.
  These dependencies do not implement stacktraces, catch/unwind or new effects.
- `pid_table.c`, `bif_process.c`: native flat64 internal PIDs and modern PTAB
  free-ID ring/index permutation, owned by each code namespace. Real `self/0`
  and `is_pid/1` execute; IDs remain stable through calls/GC and are removed on
  exit before completion. Stale generations refuse lookup; generation exhaustion
  retires slots. The lazy 1,024-slot directory is fallible and physically released
  with its world. PID terms are namespace-relative, not cross-world capabilities.
  `make_ref/0` also executes: native ordinary references use an Engine-owned
  serialized counter, survive moving GC/copying, and support native equality
  and reference tests. Exhaustion refuses before allocation or counter wrap.
  Spawn, mailboxes and monitors still require their own implementation.
- `fun.c`: native local closure layout and defining-code leases, environment
  tracing, copying and exact equality. The whole `make_fun3` generator and native
  constructor/call cases now execute guest-created captured, nested and closed
  closures. Native lambda entries bind verified function labels and module MD5.
  Literal-fun operand sites retain their defining module before any instruction
  effects, including fused moves/returns. Moving GC preserves live environments,
  current code and saved continuations; dead leases are swept. OOM, badfun and
  badarity are tested through actual execution. External/unloaded funs and spawn
  remain outside admission.
- `binary_size.c`: read-only native flat64 aggregate sizing over emitted
  descriptors, with explicit register spans and typed guest/metadata/policy
  failures. Native UTF8/16 deferred validation remains distinct from UTF32.
  The stock JIT UTF8 error-order discrepancy is recorded, not normalized away.
- `writable.c`: fresh heap/refcounted construction spans, process-owned native
  writable backing and non-private append,
  fallible preparation before revocation/publication, volatile-prefix reads,
  explicit pin before sharing, and copying-GC chain transfer/dead release.
  Pin/GC retain capacity rather than performing fallible shrink operations.
  Checked private append consumes a unique X/Y source slot and preserves native
  in-place/frozen-copy branches; stored aliases refuse before mutation.
  Source-slot consumption is statically checked, separately from runtime
  uniqueness. The complete `bs_init_writable` case now executes with native
  capacity defaults, allocation-failure rollback, work charging and explicit
  infrastructure limits through the worker/public API.
- `binary_build.c`: owned boundaries for the complete generated `bs_create_bin`
  case. Native sizing precedes allocation, moving operands are reloaded, private
  source consumption is enforced, and the destination is published only after
  exact writer completion. Guard and success exits yield at their next real PC.
  OOM, runtime limits, ownership refusal and floating-environment failure stay
  distinct from guest errors. See [construction evidence](../../docs/rfds/0003-binary-construction.md).
- `bit_copy.c`: native forward bit copying behind checked span/overlap bounds.
  Disjoint bit intervals may share a boundary byte; exact self-copy is a no-op.
  Other overlapping bit ranges remain refused before mutation.
  Copied public results use it; arbitrary offsets retain outside bits and need
  no terminal padding. No bit-syntax instruction is admitted by this helper.
- `bit_writer.c`, `bit_float.c`: native integer, UTF, binary/string and float
  segment writer dependencies, with an explicit initialized span/cursor,
  pre-write refusals, and no allocation or GC. Float conversion scopes/restores
  the IEEE environment; infrastructure failure is distinct from badarg.
  C-built heap/offheap arguments survive actual identity execution and collection;
  the complete construction instruction now uses these dependencies.
- `flatmap.h`, `binary_error.c`: native MAP3 binary-construction error payload,
  with explicit temporary value rooting and fallible heap reservation. Flatmap
  keys/values participate in GC, copying, equality and writable alias checking.
  Only the 0..3 representation profile is consumed. Construction publishes native
  extended-error flags/payloads; general maps, trace capture and catch/unwind
  remain unadmitted. Pending native action flags are not completed actions.
- `term_compare.c`: native-derived nonrecursive exact equality for admitted terms
  and aligned/unaligned bitstrings. Explicit scratch OOM is checked before a
  generated comparison branch; failure is never an equality/inequality answer.
- `big.c`, `bif_numeric.c`: native small/bignum/float addition and positive-listed
  `+/2`, fallible independent scratch, collection-safe X/Y publication, native
  size errors and scoped IEEE arithmetic. OOM is never a false arithmetic guard.
- `decimal.c`, `bif_decimal.c`: native schoolbook `integer_to_binary/1`, with
  independent process-owned scratch and worker continuations between division
  passes. Completion produces ordinary heap/offheap binaries; stop/cancellation
  destroys partial state. D&C/dirty schedulers and other bases are not admitted.
- `bif_info.c`, `md5.c`: the two positive-listed `get_module_info` BIFs and actual
  metadata/checksum behavior, not placeholders for compiler-generated imports.

All control APIs are serialized. With a worker, raw code/heap/domain access must
hold the Engine control borrow; task/world APIs acquire it themselves. Worker-side
orphan completion and code retirement also mutate ownership: final-detachment and
shutdown checks therefore occur under the mutex, with disposal/join after unlock.
Entry/process handles retain actual code and its
literal storage. Returned term views borrow the process until its next mutating
call or destruction. Runtime collection preserves outstanding heap reservations;
new stack slots are valid roots even at host instruction-budget safepoints.
Metadata copies retain binary payloads independently of source code arenas.
`lb_process_create_binary` copies host bytes into a real arity-one invocation.
`world.c` applies the public 64-KiB input/output, 64-call and 1-MiB reservation limits;
C++ copies ready outputs into independently host-owned vectors before consumption.

## Deliberate limits

The current literal/GC profile includes smalls, bignums, atoms, tuples, lists,
finite binary64 floats, heap bitstrings up to 64 bytes and ordinary immutable
refcounted binaries above that threshold. C-created process-owned writable
binaries also survive copying GC, read-only consumers, pinning and retirement;
literal admission remains immutable-only. C-created small flatmaps support error
payload dependencies and representation consumers. Empty/single-entry MAP_EXT
literals are admitted for wrapper metadata; larger MAP_EXT and map operations are not.
HAMT, magic/resource binaries, ports and external/magic references remain unsupported.
Local fun objects/captured environments, same-world PIDs and ordinary local references
are implemented with owned leases and moving roots; they do not grant cross-world authority. Attributes/compile ETF is decoded for executable
admission; ordinary line/debug/feature metadata is passive, while executable
`DbgB` and record `Recs` metadata is refused. Source-line instrumentation is absent.

Only the explicit generated dispatch profile is admitted, including unused
functions. Other arithmetic/bases, full transform helpers,
general catch/unwind and broader string/binary consumers remain outstanding.
Local fun calls, bounded binary match cursors and serialized actor scheduling are
implemented only within their documented whole-case admission profiles.
Retained preparation arenas include temporary operations/backing; density is not
claimed. The bootstrap allocator supplies backing, not a replacement term format.

Opcode and naming inputs under `otp/` are pinned. Python/Perl run unchanged OTP
`beam_makeops`/`make_tables` at build time; no generation runs in the runtime.
Generated numeric BIF identities are not permission grants. Zlib uses explicit
allocation-domain callbacks. See the provenance records before admitting new code.

## Validation

Use fresh external outputs and the user-wide validation lock. The integrated runner
acquires it, compiles ordinary fixtures, executes the C core, separately compares
stock OTP metadata/semantics, checks failure prefixes and physical lifetime, and
runs Debug/Release, UBSan, representation and reproducibility checks:

```sh
python3 -B libbeam/tools/run_additive_slice.py --output /tmp/unused-core-validation
```

It requires `erl`, `erlc`, CMake, a C compiler, Python, Perl, zlib and `nm` (POSIX
host tooling). Compiler-produced BEAM files remain outside source control.
The runner requires unchanged Engine lifecycle and stateful two-Isolate acceptance
in Debug/Release/available UBSan. [Runnable examples](../examples/README.md) use the
same public Engine/Isolate API and their existing language sources as fixtures. Component execution alone is not either
acceptance gate, complete Erlang compatibility, a security boundary, or a
performance/platform claim.
