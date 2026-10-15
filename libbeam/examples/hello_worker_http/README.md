# Local HelloWorker Isolate-to-HTTP bridge

This is a **localhost-only demonstration**, not a production HTTP server or a
Midgard deployment host. It reads the complete, unchanged `LBAPP001` HelloWorker
fixture, atomically loads all its exact-name BEAM modules into one real libbeam
Isolate, and passes HTTP method/path/query/headers/body to the existing
`Elixir.HelloWorker:__midgard_call__/1` binary protocol. It does not start
ordinary OTP, stub modules, skip dependencies, or grant denied host operations.
If strict whole-group admission fails, it prints the refusal and **never binds
a listening port**.

From the OTP checkout root:

```sh
bazel test //libbeam/examples/hello_worker_http:codec_test \
  //libbeam/examples/hello_worker_http:append_inference_test
bazel build //libbeam/examples/hello_worker_http:bridge //libbeam:hello_worker_fixture
libbeam/examples/hello_worker_http/smoke.sh \
  "$PWD/bazel-bin/libbeam/examples/hello_worker_http/bridge" \
  "$PWD/bazel-bin/libbeam/fixtures/hello_worker.bundle" \
  "$PWD/../midgard/examples/hello_worker"
```

`bridge --self-check` tests the HTTP/worker wire codec without claiming a
running Isolate. The smoke script runs it before the real end-to-end check.
`append_inference_test` separately executes the existing OTP-compiled
binary-comprehension fixture in a real Isolate, checking the private-append
behavior needed for `Code.Fragment`; it is **not** HelloWorker admission.

`smoke.sh` starts the bridge on an ephemeral port bound only to `127.0.0.1`,
uses **curl** to check GET `/hello` and POST `/json` headers and exact bodies,
then waits for its two-request limit to shut down and verifies the Isolate's
closed, zero-current-memory receipt. It runs `otp_reference.exs` in the
unchanged Midgard example under ordinary OTP and compares the complete binary
worker response bytes logged by the Isolate with the OTP responses. For manual use, run `bridge BUNDLE PORT
REQUEST_COUNT`; `PORT=0` prints an allocated port as `READY
http://127.0.0.1:PORT`. The bridge accepts one bounded, non-chunked HTTP/1.0
or HTTP/1.1 request per connection, returns `Connection: close`, and forwards
only after parsing it. It is intentionally not a general HTTP implementation.

A fourth optional `MEMORY_MiB` argument to `smoke.sh` (fifth to `bridge`)
sets an explicit finite per-Isolate memory budget for diagnosing large-closure
proof allocations. The default remains 256 MiB. If an override is needed,
record it alongside results; it does not make default-budget tests pass.

## Current local status

The unchanged 381-module fixture SHA-256 is
`25039ee656bd1148221085309a4cab379847953b61c82074cf2b80ede8b27d91`.
The latest literal `bazel test //...` run completed 477 of 478 tests successfully;
`//libbeam:hello_worker_test` fails its first real greeting during atomic module
admission at module index 197 (`crypto`), stage `on_load`, offset 16066. Pinned
`crypto:on_load/0` calls `erlang:load_nif/2`, which remains an explicit capability
deny. The loader does not skip the callback or publish the module as initialized.
The remaining HelloWorker request/response and two-Isolate tests are skipped after
that shared setup failure. With the public default 256 MiB limit, the bridge smoke
attempt instead hits the accounted memory limit at module index 117
(`link/admission`); both paths refuse before opening a listener. No worker request
runs.

This is a **v0.1 blocker**, not successful application support. The artifact and
closure are unchanged; the runtime policy refuses because truthful crypto
initialization is outside the current denied-NIF boundary. Debug, Release, and
UBSan each report 477 passing test targets and the same HelloWorker failure; the
freshness check reports zero out-of-date tests but preserves that failure. Focused
tests for the local codec, append inference, float operations, list reads, bit
skips, and match-string do not prove full closure admission or HelloWorker behavior.

A successful curl demonstration would prove only the exact routes exercised on
that build. It would not prove full OTP compatibility, untested route behavior,
or successful initialization of a module whose NIF capability is denied.
