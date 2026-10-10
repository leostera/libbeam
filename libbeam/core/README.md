# Additive C core

See [RFD 0003](../../docs/rfds/0003-additive-runtime-construction.md).

This directory is the new runtime construction boundary. Its explicit CMake target
contains bootstrap allocation ownership, the adapted BEAM-file parser, selected
64-bit BEAM term definitions, owner-local atoms, and the owned generic-operation/
literal preparation path. It still has no executable code publication or executor.
It does not include or link whole ERTS and cannot yet execute BEAM code. The public
Engine factory remains blocked; the existing acceptance examples remain unchanged.

`beam_image.c` prepares an owned, structurally checked image from ordinary BEAM
bytes: code header, UTF-8 atom names, imports and exports. Unknown chunks remain
in the owned copy. Image names remain file-local views; `atoms.c` can separately and transactionally
bind them to real namespace atom words. The image itself leaves code and optional
payloads uninterpreted. `beam_program.c` separately prepares generic operations,
supported real literal terms and type/lambda declarations; it is not an executable
loader. See the [A03–A07 checkpoint](../../docs/rfds/0003-loader-program.md). This is not an
executable module loader. See the [transplant record](../../docs/rfds/0003-first-loader-slice.md)
for source hashes, validation differences, lifetime contract and remaining cluster.

`term.h` preserves the selected flat 64-bit/non-reservation BEAM tag profile,
checked small construction, and basic tuple/list storage access. It is an internal
C interface, not arbitrary-word validation or a heap/GC. `atoms.c` owns copied
UTF-8 names, stable indices and binding leases; failed batches roll back all their
new entries/backing. A bare atom Eterm does not carry its namespace: callers must
preserve context, and bindings check the expected owner. See the
[term/atom admission record](../../docs/rfds/0003-terms-and-atoms.md).

Predefined identities are built by the admitted OTP `make_tables` generator from
[`otp/`](otp/) data. Python and Perl are build dependencies; only immutable atom
outputs are compiled, not generated BIF implementations. Full opcode inputs are
also pinned under `otp/opcodes/`; their unchanged `beam_makeops` generates the
matching decoder/transform/emitter/dispatch artifacts. Only immutable metadata is
compiled so far. No files are read by the runtime for these generation steps.

The literal-table reader uses zlib with explicit domain-backed allocation callbacks.
Zlib development headers/library are a build dependency. Program preparation has
an explicit supported ETF subset; notably binaries larger than 64 bytes and maps,
funs, refs, pids and ports are refused rather than backed by dummy resources.

`alloc.c` is a serialized construction-time primitive, not a replacement for BEAM's
heap/GC or a performant general runtime allocator. Callback state is owner-local,
so tests can fail exact allocation steps without process-global fault injection.
A domain must be empty before destruction; clients unwind their own constructed
objects before releasing storage. Bulk-freeing reachable objects is not rollback.

Future transplants need a provenance/dependency/lifetime record as specified in
the RFD. Preserve C implementations and licenses; adapt actual ownership paths.
Next work is the first executable dependency cluster, not a parallel collection
of unused ownership wrappers.

Build and test with fresh external outputs under the repository's validation lock:

```sh
build=$(mktemp -d "${TMPDIR:-/tmp}/libbeam-additive.XXXXXX")
cmake -S libbeam -B "$build" -DBUILD_TESTING=ON
cmake --build "$build"
ctest --test-dir "$build" --output-on-failure
```

For a fresh compiled fixture, comparison with reference OTP, failure-prefix tests,
mutation/truncation checks, term-macro comparison, atom binding/growth rollback,
debug/release builds and UBSan, run:

```sh
python3 -B libbeam/tools/run_additive_slice.py --output /tmp/unused-image-validation
```

The runner acquires the validation lock and requires a new output directory. It
records source hashes and the installed reference OTP version; it does not claim
the reference runtime is our implementation. It requires `erl`, `erlc`, CMake, a C
compiler, Perl, zlib and `nm` (current tooling targets POSIX hosts).

These are component tests. They do not make `engine_lifecycle` or `two_isolates`
pass, and they establish neither a security boundary nor complete BEAM validation.
