# Additive C core

See [RFD 0003](../../docs/rfds/0003-additive-runtime-construction.md) and the
[first-execution record](../../docs/rfds/0003-first-execution.md).

This is the C runtime construction boundary, not a whole-ERTS link. It now loads
and executes ordinary `first_slice.erl` through generated BEAM transformations,
specific-instruction words and generated interpreter cases. Tuple allocation,
copying GC, literal/atom returns, `module_info/0,1`, bounded yields, exceptions and
physical code retirement have execution consumers. This is a **limited profile**,
not full A03–A07 coverage or a functioning public Engine/Isolate API. Both unchanged
acceptance examples remain blocked at the public factory.

## Boundaries

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
  profile-specific register/stack/heap dataflow admission. Missing dependencies
  reject admission; they are never false/success-shaped helper implementations.
- `code.c`: private export/module lookup, eager resolution of **all** imports,
  atomic publication, retained entries/frames/import dependencies, and guarded
  physical retirement. No lazy loading, hot reload, NIF loading or root swapping.
- `process.c`, `heap.c`: explicit X/Y/continuation/reduction/exception state,
  bounded generated interpreter dispatch and selected copying-GC algorithms.
  No workers, scheduler-data TLS, global process table or implicit OTP services.
- `bif_info.c`, `md5.c`: the two positive-listed `get_module_info` BIFs and actual
  metadata/checksum behavior, not placeholders for compiler-generated imports.

All control APIs are serialized. Entry/process handles retain actual code and its
literal storage. Returned term views borrow the process until its next mutating
call or destruction. Runtime collection preserves outstanding heap reservations;
new stack slots are valid roots even at host instruction-budget safepoints.

## Deliberate limits

The current literal/GC profile includes smalls, bignums, atoms, tuples, lists,
finite binary64 floats and heap bitstrings up to 64 bytes. Maps, offheap/large
binaries, fun objects, PIDs, ports and refs remain unsupported. Lambda declarations
are not closure objects. Attributes/compile ETF is decoded for executable
admission; ordinary line/debug/feature metadata is passive, while executable
`DbgB` and record `Recs` metadata is refused. Source-line instrumentation is absent.

Only the explicit generated dispatch profile is admitted, including unused
functions. Full transform helpers, catch/fun/string/binary instruction consumers,
broader exception handling, scheduling and the stateful example remain outstanding.
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
Component execution is **not** Engine lifecycle or stateful-Isolate acceptance,
complete Erlang compatibility, a security boundary, or a performance/platform claim.
