# Ownership integration sweep: progress, not completion

The approved scope remains the full Engine/isolate ownership inventory. This
change does **not** complete that scope or unlock the public factory. It adds an
explicit red lifecycle target and connects two more real runtime subsystems to
the ownership tree; it must not be reported as "everything moved".

## Executable target

`libbeam/examples/engine_lifecycle.cpp` calls create → shutdown, destroys the
owning handle, and repeats, without creating an isolate or requesting OTP boot.
CMake builds it in native and scaffold configurations. It is deliberately not an
expected-failure CTest. Both currently exit 1 at the first `Engine::create`.

This example expresses the successful lifecycle path. Recoverable startup and
failed-initialization recovery still need implementation and fault-injection
coverage. `two_isolates.cpp` is unchanged and still blocked at the factory.

## Engine constant-literal arena

`ErtsEngine.global_literals` now owns the actual arena control, chunks, lock,
construction state and accounting used by existing constant builders. The fixed
runtime binding and exported constant terms are borrowed views; this does not
introduce a per-isolate selector.

Each chunk retains its exact original allocation base, including debug-mode
page-alignment over-allocation. Unpublished disposal unprotects debug pages,
cleans off-heap references, frees original bases and destroys the lock/control.
It refuses bound arenas and unfinished allocation/register pairs. The caller
must exclusively own unpublished arenas and ensure no term pointer escapes.
Returning storage to the allocator is not proof of carrier unmapping.

The diagnostic Engine arena remains bound: its consumers must be retired before
an aggregate Engine teardown can release it. Platform constants also require
namespace-independent atom identities before they can be shared by executing
private worlds. Crash iteration now handles an absent arena without dereferencing
NULL. No generally recoverable ERTS allocation-failure protocol is added here.

## Isolate persistent-term storage and work

Each namespace owns an `ErtsPersistentTermState` containing the current table,
update permission, waiter queue, fast-update state, later operation, deletion
queue and destructible locks. Actual BIF reads/updates select the process's
namespace; the native getter now requires an explicit namespace. The distribution
caller names the diagnostic boundary explicitly.

Copied tables and delete operations carry their state owner. Deferred deletion
uses that owner rather than a global table/queue. Ownership is retained through:

- update permission, until waiter handling and lock release complete;
- every queued delete operation, until its physical cleanup completes;
- put/erase/get/info magic-binary contexts, until physical binary free;
- detached literal areas, via their existing namespace leases.

Trap resume checks state provenance. Unpublished disposal requires an empty
store, no updater/waiters/deletion queue, and no namespace borrowers. Diagnostic
binding refuses disposal. Parent preflight includes this child before any
mutation. This is not running isolate shutdown: stored terms and outstanding
jobs must still be retired through a complete stop protocol.

The migration also fixes `info/0`'s trapped-context update-permission flag and
uses its arity-one continuation. Fast-update state is cleared in release builds
as well as debug builds, avoiding a stale retained term word.

Trap exports, crash-dump output views and debug scan scratch remain diagnostic
boundaries. The shared literal collector/purger and their service processes are
not migrated by this change. Private process creation remains disabled, so no
private-world execution or cross-isolate persistent-term acceptance is claimed.

## Validation

Configured macOS ARM64 debug interpreter, serialized under the user-wide lock:

- Nine native component probes passed. New arena checks cover 32 independent
  arena-pair cycles, chunk growth, unfinished/bound disposal refusal, original
  allocation-base reclamation, real off-heap binary reference release and peer
  survival. Namespace checks include distinct persistent stores, explicit-owner
  reads of empty stores and borrowed-state refusal.
- Diagnostic bytecode exercises 192 persistent entries, growth, trapped get/info,
  four concurrent writers, reader termination, erase and retained values after GC.
  Reader termination is stress coverage, not deterministic injection at every trap.
- Six native CTests, one scaffold CTest and 29 tooling tests passed; three raw,
  four PTY and three CMake returning-start hosts passed.
- Both new lifecycle binaries fail at the honest factory, as does the unchanged
  two-isolate example. Component process exit is not Engine reclamation.

Authoritative evidence:

- `/tmp/libbeam-sweep-arenas-persistent-reviewed/summary.json`
- `/tmp/libbeam-sweep-arenas-persistent-reviewed-package/manifest.json`
- `/tmp/libbeam-sweep-arenas-persistent-reviewed-verified/summary.json`
- `/tmp/libbeam-sweep-arenas-persistent-reviewed-verified/engine-lifecycle-summary.json`
- `/tmp/libbeam-sweep-arenas-persistent-reviewed-sync.json`

Earlier first/final/stress-fixed runs are retained. The stress run's
`compile-constant-arena.log` preserves a test-header omission, corrected by
including `erl_binary.h`. Initial whitespace-check failures were fixed before
reviewed validation. No clean-bootstrap, JIT, other-platform, sanitizer, security,
latency or density acceptance is claimed.

## Still in the approved sweep

See the [current ownership inventory](0002-current-ownership-inventory.md) for
consolidated status across this and earlier checkpoints.

Scheduler/run-queue/sleep/auxiliary domains; async stop/join; remaining thread
progress, TLS values and raw thread-library resources; clocks/topology; backend
flags/locks/options and allocator draining; shared/world initialization split;
process/port tables; ETS; timers; tracing/logging/services; node/reference/native
resource provenance; remaining loader/interpreter/JIT context propagation;
purger/literal-collector retirement; and the full initialization/unwind/shutdown
transaction. The runtime claim cannot yet be safely reset.
