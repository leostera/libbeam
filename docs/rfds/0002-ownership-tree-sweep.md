# Engine-rooted ownership: namespace membership, registrations and runtime keys

This is a **partial sweep**, not the requested completion of all runtime ownership.
`Engine::create()` remains blocked. Neither create → shutdown → create nor the
unchanged two-isolate example passes. No process-lifetime retention is being
presented as destruction.

## Implemented

### Engine → namespace membership

Every successfully constructed namespace joins an intrusive list on its Engine.
The Engine owns its diagnostic namespace reference; the former file-static
reference is gone. Construction failures do not publish children. Disposal
preflights children and leases, destroys children, then unlinks the namespace.
An Engine retaining children cannot be discarded as an empty control object.

Namespace admission can be closed without detaching existing children. The cold
I/O release stage closes it. This is **not** process admission control, stopping
an isolate, or retiring its execution references. Membership and admission
operations require engine-wide control-thread serialization; process/code/literal
leases retain their existing atomic lifetime protocol.

### Namespace → registered names

`ErtsRegistry` owns the real name hash and its destructible reader/writer lock.
Each namespace constructs its own registry. Registration, lookup, enumeration,
and process-exit unregistration select the caller's namespace registry and keep
that choice across lock release/retry. Registration checks the target process's
namespace before mutation. Entries carry their registry owner; port-close
unregistration follows that owner.

NULL-process service calls, crash diagnostics and aggregate size reporting remain
explicit diagnostic boundaries. Private port registration is refused: ports do
not yet have complete isolate ownership. Private process creation is still
blocked, so this is **not evidence of executing private worlds**.

Unpublished disposal requires an unbound, empty registry and parent disposal
requires no leases. Bound diagnostic registries cannot be discarded. The stale
port registration error path now avoids unlocking a registry lock that it never
acquired; the bytecode fixture exercises that path.

### Engine → I/O group → driver-event state

The real driver-event vector/hash control and lock storage are allocated beneath
the owning I/O group, replacing their global storage object. Runtime/debugger
access retains one fixed engine binding, not an isolate selector. The existing
cold continuous-FD POSIX release destroys the locks/vector, releases the control
allocation, and clears the binding before discarding the group. A group retaining
event state refuses unpublished disposal. Size reporting includes the control;
max-files reporting returns zero after release. Running and Windows teardown are
still not implemented. Debugger references were updated, not executed.

### Engine → ERTS thread-key ledger

An Engine-owned system-allocated ledger is bound **before platform initialization**.
The actual `erts_tsd_key_create` wrapper records keys, including early lock-check,
crash, scheduler and subsequent wrapper-created keys. Record-allocation failure
rolls back the newly created key before the existing fatal error path. Actual
key deletion removes ownership only on successful deletion. Lookup/removal and
OS deletion are serialized, avoiding removal of a newer record after OS key-ID
reuse. Bookkeeping uses C11 atomics independently of ERTS locks and allocators.

Allocator and thread-progress raw keys retain their already separate domain
ownership. Raw ethread/library/driver-NIF key creation is **not universally
covered**. This ledger does not own or destroy arbitrary TLS values, infer that
all users have stopped, or bulk-delete keys. Empty-ledger disposal requires
exclusive quiescence; live-key disposal refuses. Its storage is included in
external memory accounting. Static numeric key variables remain fixed borrowed
views, not current-isolate selectors.

## Still required — do not mark the sweep complete

This section records the `27ff6518` checkpoint. See the
[current ownership inventory](0002-current-ownership-inventory.md) for consolidated
status: `921fc4ad` subsequently moved persistent-term storage/update/deletion work
and Engine constant-arena storage. Running retirement is still incomplete.

- Separate shared initialization from diagnostic namespace/service construction.
- Complete scheduler/run-queue/sleep/auxiliary ownership and queued-reference
  retirement, beyond existing allocator backing and scheduler handle ownership.
- Retire TLS values, raw thread-library resources, backend flags/locks/options,
  allocator caches/pools/mappings and initialization state in dependency order.
- Move process/PID and port tables, ETS/heirs/transfers, timers and
  application/service roots beneath isolates, with corresponding access paths
  and callback ownership. Persistent-term tables and update/deletion queues have
  since moved; their running stop/drain protocol remains outstanding.
- Complete explicit owner propagation through loader/ETF/import/fixup,
  interpreter/JIT, purger and literal-collector work.
- Implement recoverable initialization, complete cold teardown and runtime-claim
  reset before enabling the owning C++ factory; then running stop/join/reclaim.

Physical storage reachable through allocator ledgers is not a substitute for
these lifetime/access-path migrations. Registration ownership does not isolate
PID sends, ETS, timers or other still-global operations.

## Evidence

On the configured macOS ARM64 debug interpreter snapshot, under the shared
validation lock:

- Eight native component probes passed. New ownership-tree probe checks 32 empty
  key-ledger/control cycles before platform initialization, 64 real TLS key
  create/set/get/delete cycles, separately owned raw keys, live-ledger refusal,
  failed namespace construction, 32 three-child membership/disposal cycles,
  borrowed-child refusal, distinct empty registries, admission closure and
  diagnostic-parent survival.
- The real bytecode fixture checks registered process lookup/enumeration/send,
  duplicate refusal, unregister/re-register, process exit, port lookup/close and
  stale-port refusal in the diagnostic world.
- Six packaged native CTests, one scaffold CTest and 29 tooling tests passed.
- Three raw, four PTY and three CMake hosts passed their existing returning-start
  checks. The unchanged acceptance example still fails at the honest factory.

Authoritative artifacts:

- `/tmp/libbeam-ownership-tree-final/summary.json`
- `/tmp/libbeam-ownership-tree-final-package/manifest.json`
- `/tmp/libbeam-ownership-tree-final-verified/summary.json`
- `/tmp/libbeam-ownership-tree-final-sync.json`

Earlier passing iterations: `/tmp/libbeam-ownership-tree-first/summary.json` and
`/tmp/libbeam-ownership-tree-keys/summary.json`. No new observed compiler warnings;
the existing I/O pointer-cast and other baseline warnings remain. This is not
clean-bootstrap, JIT, other-platform, security, latency, density or shutdown
acceptance. Test process exit still reclaims intentionally retained diagnostic
runtime resources; it is not Engine reclamation.
