# Additive C core

See [RFD 0003](../../docs/rfds/0003-additive-runtime-construction.md).

This directory is the new runtime construction boundary. Its explicit CMake target
contains bootstrap allocation ownership and the first adapted BEAM-file parser.
It does not include or link whole ERTS and cannot yet execute BEAM code. The public
Engine factory remains blocked; the existing acceptance examples remain unchanged.

`beam_image.c` prepares an owned, structurally checked image from ordinary BEAM
bytes: code header, UTF-8 atom names, imports and exports. Unknown chunks remain
in the owned copy. Names are file-local views, not interned runtime atoms; code,
ETF literals, closures, type/debug/record data remain uninterpreted. This is not an
executable module loader. See the [transplant record](../../docs/rfds/0003-first-loader-slice.md)
for source hashes, validation differences, lifetime contract and remaining cluster.

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
mutation/truncation checks, debug/release builds and UBSan, run:

```sh
python3 -B libbeam/tools/run_additive_slice.py --output /tmp/unused-image-validation
```

The runner acquires the validation lock and requires a new output directory. It
records source hashes and the installed reference OTP version; it does not claim
the reference runtime is our implementation. It requires `erl`, `erlc`, CMake, a C
compiler and `nm` (current tooling targets POSIX hosts).

These are component tests. They do not make `engine_lifecycle` or `two_isolates`
pass, and they establish neither a security boundary nor complete BEAM validation.
