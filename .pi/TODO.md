# Implementation TODO: OTP BIFs and BEAM instructions

This is an implementation checklist, not a statement of current support. Each BIF has an explicit target behavior:

- **must work**: implement the requested Erlang behavior within the isolate and cover it with an execution test.
- **must panic**: reject/terminate the isolate operation deterministically, without performing the prohibited host-side effect. Do not let this escape as a host-process panic.

The BIF set comes from libbeam/core/otp/bif-batch-request.txt (188 requested MFAs). The 32 must panic classifications mirror libbeam/core/capability_denials.def; the remaining requested MFAs are must work. The original instruction list came from generated OTP specific forms absent from libbeam/core/dispatch_support.inc. These include OTP-internal forms—not necessarily distinct source-level instructions. Current support is determined by the generated catalog, loader, and executable tests, not this historical count.

## BIFs


## Unadmitted generated instruction forms

The remaining native-record forms still need complete error/continuation semantics;
negative-only paths do not count as support.

- [ ] `opcode: call_nif_WWW`
- [ ] `opcode: call_nif_early`
- [ ] `opcode: finish_create_record`
- [ ] `opcode: i_create_native_record_jctI`
- [ ] `opcode: i_get_record_elements_jxI`
- [ ] `opcode: i_get_record_elements_jyI`
