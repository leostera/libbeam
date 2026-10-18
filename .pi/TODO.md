# Implementation TODO: OTP BIFs and BEAM instructions

This is an implementation checklist, not a statement of current support. Each BIF has an explicit target behavior:

- **must work**: implement the requested Erlang behavior within the isolate and cover it with an execution test.
- **must panic**: reject/terminate the isolate operation deterministically, without performing the prohibited host-side effect. Do not let this escape as a host-process panic.

The BIF set comes from libbeam/core/otp/bif-batch-request.txt (188 requested MFAs). The 32 must panic classifications mirror libbeam/core/capability_denials.def; the remaining requested MFAs are must work. The instruction list is the 67 generated OTP specific forms absent from libbeam/core/dispatch_support.inc (492 forms total; 425 currently listed as supported). These are specific generated forms, including OTP-internal forms—not necessarily 67 distinct source-level instructions.

## BIFs

- [ ] **must work** — `erlang:universaltime/0`
- [ ] **must work** — `erlang:universaltime_to_localtime/1`
- [ ] **must work** — `erlang:universaltime_to_posixtime/1`
- [ ] **must work** — `erlang:unlink/1`
- [ ] **must work** — `erlang:unregister/1`
- [ ] **must work** — `erlang:whereis/1`
- [ ] **must work** — `erlang:xor/2`
- [ ] **must work** — `erlang:yield/0`

## Unadmitted generated instruction forms

- [ ] `opcode: call_bif_W`
- [ ] `opcode: call_error_handler`
- [ ] `opcode: call_light_bif_be`
- [ ] `opcode: call_light_bif_only_be`
- [ ] `opcode: call_nif_WWW`
- [ ] `opcode: call_nif_early`
- [ ] `opcode: continue_exit`
- [ ] `opcode: error_action_code`
- [ ] `opcode: finish_create_record`
- [ ] `opcode: i_bs_get_binary_all2_xfttd`
- [ ] `opcode: i_bs_get_binary_all2_yfttd`
- [ ] `opcode: i_bs_get_binary_imm2_xftWd`
- [ ] `opcode: i_bs_get_binary_imm2_yftWd`
- [ ] `opcode: i_bs_get_integer_16_xfd`
- [ ] `opcode: i_bs_get_integer_16_yfd`
- [ ] `opcode: i_bs_get_integer_32_xfd`
- [ ] `opcode: i_bs_get_integer_32_yfd`
- [ ] `opcode: i_bs_get_integer_8_xfd`
- [ ] `opcode: i_bs_get_integer_8_yfd`
- [ ] `opcode: i_bs_get_integer_imm_xWtftx`
- [ ] `opcode: i_bs_get_integer_imm_yWtftx`
- [ ] `opcode: i_bs_get_integer_small_imm_xWftx`
- [ ] `opcode: i_bs_get_integer_small_imm_yWftx`
- [ ] `opcode: i_bs_test_unit_fxt`
- [ ] `opcode: i_bs_test_unit_fyt`
- [ ] `opcode: i_bxor_jssd`
- [ ] `opcode: i_call_trace_return`
- [ ] `opcode: i_create_local_native_record_qdtI`
- [ ] `opcode: i_create_native_record_jctI`
- [ ] `opcode: i_debug_breakpoint`
- [ ] `opcode: i_debug_line_IIt`
- [ ] `opcode: i_disabled_line_breakpoint_t`
- [ ] `opcode: i_enabled_line_breakpoint_t`
- [ ] `opcode: i_func_info_IaaI`
- [ ] `opcode: i_generic_breakpoint`
- [ ] `opcode: i_get_local_record_field_jxaad`
- [ ] `opcode: i_get_local_record_field_jyaad`
- [ ] `opcode: i_get_record_elements_jxI`
- [ ] `opcode: i_get_record_elements_jyI`
- [ ] `opcode: i_get_record_field_jxcad`
- [ ] `opcode: i_get_record_field_jycad`
- [ ] `opcode: i_int_bnot_jSd`
- [ ] `opcode: i_is_native_record_fxaa`
- [ ] `opcode: i_is_native_record_fyaa`
- [ ] `opcode: i_is_record_accessible_fx`
- [ ] `opcode: i_is_record_accessible_fy`
- [ ] `opcode: i_is_wildcard_record_accessible_fxa`
- [ ] `opcode: i_is_wildcard_record_accessible_fya`
- [ ] `opcode: i_lambda_error_t`
- [ ] `opcode: i_line_breakpoint_cleanup`
- [ ] `opcode: i_load_nif`
- [ ] `opcode: i_m_div_jssd`
- [ ] `opcode: i_nif_padding`
- [ ] `opcode: i_return_to_trace`
- [ ] `opcode: i_update_native_record_jsdtI`
- [ ] `opcode: i_wait_error`
- [ ] `opcode: i_wait_error_locked`
- [ ] `opcode: int_code_end`
- [ ] `opcode: label_L`
- [ ] `opcode: line_I`
- [ ] `opcode: nif_start`
- [ ] `opcode: normal_exit`
- [ ] `opcode: on_load`
- [ ] `opcode: padding`
- [ ] `opcode: return_trace`
- [ ] `opcode: system_limit_body`
- [ ] `opcode: trace_jump_W`
