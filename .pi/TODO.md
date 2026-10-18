# Implementation TODO: OTP BIFs and BEAM instructions

This is an implementation checklist, not a statement of current support. Each BIF has an explicit target behavior:

- **must work**: implement the requested Erlang behavior within the isolate and cover it with an execution test.
- **must panic**: reject/terminate the isolate operation deterministically, without performing the prohibited host-side effect. Do not let this escape as a host-process panic.

The BIF set comes from libbeam/core/otp/bif-batch-request.txt (188 requested MFAs). The 32 must panic classifications mirror libbeam/core/capability_denials.def; the remaining requested MFAs are must work. The instruction list is the 67 generated OTP specific forms absent from libbeam/core/dispatch_support.inc (492 forms total; 425 currently listed as supported). These are specific generated forms, including OTP-internal forms—not necessarily 67 distinct source-level instructions.

## BIFs


## Unadmitted generated instruction forms

Record predicate/field forms previously removed after non-record-only tests are
listed again: their successful paths require `Recs` definitions and native-record
terms, which the loader still rejects. Negative-path coverage is not full support.

- [ ] `opcode: call_nif_WWW`
- [ ] `opcode: call_nif_early`
- [ ] `opcode: finish_create_record`
- [ ] `opcode: i_create_local_native_record_qdtI`
- [ ] `opcode: i_create_native_record_jctI`
- [ ] `opcode: i_get_local_record_field_jxaad`
- [ ] `opcode: i_get_local_record_field_jyaad`
- [ ] `opcode: i_get_record_elements_jxI`
- [ ] `opcode: i_get_record_elements_jyI`
- [ ] `opcode: i_get_record_field_jxcad`
- [ ] `opcode: i_get_record_field_jycad`
- [ ] `opcode: i_is_native_record_fxaa`
- [ ] `opcode: i_is_native_record_fyaa`
- [ ] `opcode: i_is_record_accessible_fx`
- [ ] `opcode: i_is_record_accessible_fy`
- [ ] `opcode: i_is_wildcard_record_accessible_fxa`
- [ ] `opcode: i_is_wildcard_record_accessible_fya`
- [ ] `opcode: i_update_native_record_jsdtI`
