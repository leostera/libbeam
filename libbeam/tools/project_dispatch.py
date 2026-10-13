# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Leandro Ostera <leandro@ostera.io>
"""Exact generated instruction bodies plus reviewed ownership-boundary edits.

This is an explicit single interpreter capability profile, not an alternate
bytecode, a fixture opcode parser, or dummy handlers for the remaining cases.
"""
import re
import hashlib

NAMES = ('move_cr move_cx move_xr move_xx move_rx move_nx move_x1_c move_x2_c move_shift_cxx '
         'move_shift_nyx move_shift_xxx move_shift_xyx move_shift_yry move_shift_yxx '
         'move_return_c move_return_n move_return_x return put_tuple2_xI put_tuple2_yI test_heap_It '
         'i_call_ext_only_e i_move_call_ext_only_ec jump_f badmatch_x case_end_x '
         'i_allocate_zero_tt i_allocate_heap_zero_tIt allocate_tt allocate_heap_tIt '
         'i_call_ext_e i_move_call_ext_ce deallocate_Q i_is_eq_exact_immed_frc '
         'i_is_eq_exact_immed_fxc move_jump_fcr move_jump_fcx '
         'move_xy move_yr move_yx move_yy move_ry move_cy swap_xx swap_yx swap_yy swap2_xxx '
         'i_call_f i_call_last_fQ i_call_only_f i_times_jssd i_int_div_jssd '
         'move_call_cf move_call_xf move_call_yf move_call_last_cfQ move_call_last_xfQ move_call_last_yfQ '
         'move_call_only_cf move_call_only_xf '
         'move_deallocate_return_cQ move_deallocate_return_nQ move_deallocate_return_xQ move_deallocate_return_yQ '
         'i_call_ext_last_eQ i_move_call_ext_last_eQc '
         'i_init_y i_init2_yy i_init3_yyy i_init_seq3_y i_init_seq4_y i_init_seq5_y '
         'i_is_eq_exact_immed_fyc i_is_eq_exact_literal_fxc i_is_eq_exact_literal_fyc '
         'i_is_ne_exact_immed_fxc i_is_ne_exact_immed_fyc i_is_ne_exact_literal_fxc i_is_ne_exact_literal_fyc '
         'is_eq_exact_fxx is_eq_exact_fxy is_eq_exact_fyy is_ne_exact_fSS is_nil_fx is_nil_fy '
         'i_get_sd i_get_hash_cWd i_apply i_apply_last_Q i_apply_only apply_t apply_last_tQ is_binary_fx is_binary_fy is_bitstring_fx is_bitstring_fy is_boolean_fx is_boolean_fy is_float_fx is_float_fy is_number_fx is_number_fy is_port_fx is_port_fy is_pid_fy is_reference_fy is_list_fx is_list_fy is_integer_fx is_integer_fy is_integer_allocate_fxtt is_tuple_fr is_tuple_fx is_tuple_fy is_tuple_of_arity_frA is_tuple_of_arity_fxA is_tuple_of_arity_fyA '
         'is_tagged_tuple_frAa is_tagged_tuple_fxAa is_tagged_tuple_fyAa is_tagged_tuple_ff_ffrAa is_tagged_tuple_ff_ffxAa '
         'test_arity_fxA test_arity_fyA test_arity_get_tuple_element_fxAPx '
         'i_get_tuple_element_xPx i_get_tuple_element_xPy i_get_tuple_element_yPx i_get_tuple_element_yPy '
         'i_get_tuple_element2_xPx i_get_tuple_element3_xPx i_get_tuple_element2_dst_xPxx i_get_tuple_element2_dst_xPyy '
         'is_nonempty_list_fx is_nonempty_list_fy is_nonempty_list_allocate_frtt is_nonempty_list_allocate_fxtt '
         'is_nonempty_list_get_hd_fxx is_nonempty_list_get_tl_fxx is_nonempty_list_get_list_frxx is_nonempty_list_get_list_fxxx '
         'get_hd_xx get_hd_xy get_hd_yx get_hd_yy get_tl_xx get_tl_xy get_tl_yx get_tl_yy '
         'get_list_rry get_list_rxr get_list_rxx get_list_rxy get_list_ryr get_list_xrx '
         'get_list_xxx get_list_xxy get_list_xyx get_list_xyy get_list_yxx get_list_yxy get_list_yyx get_list_yyy '
         'put_list_cxx put_list_cyx put_list_rnr put_list_rnx put_list_rxr put_list_rxx '
         'put_list_xcx put_list_xnx put_list_xny put_list_xxr put_list_xxx put_list_xyx '
         'put_list_ycx put_list_ynx put_list_yny put_list_yxx put_list_yyx put_list_ssd '
         'update_list_cx update_list_xx update_list_yx i_trim_t move_trim_yxt move2_par_xxxy move2_par_yxxx move_window2_xxy move_shift_xxy bs_init_writable i_bs_create_bin_jIWdW self_x self_y node_x node_y is_atom_fx is_atom_fy is_pid_fx is_reference_fx i_call_fun_t i_call_fun_last_tQ i_make_fun3_Fdtt send recv_marker_reserve_S recv_marker_bind_SS recv_marker_clear_S recv_marker_use_S i_loop_rec_f loop_rec_end_f remove_message timeout timeout_locked bs_test_tail_imm2_fxW bs_test_tail_imm2_fyW bs_test_zero_tail2_fx bs_test_zero_tail2_fy i_bs_eq_ftW i_bs_extract_integer_td is_function_fx is_function_fy hot_is_function2_fSt cold_is_function2_fxx '
         'new_map_dtI i_new_small_map_lit_dtq i_get_map_elements_fsI is_map_fx is_map_fy '
         'i_get_map_element_fxxx i_get_map_element_fxxy i_get_map_element_fxyx i_get_map_element_fxyy '
         'i_get_map_element_fyxx i_get_map_element_fyxy i_get_map_element_fyyx i_get_map_element_fyyy '
         'i_get_map_element_hash_fxcWx i_get_map_element_hash_fxcWy i_get_map_element_hash_fycWx i_get_map_element_hash_fycWy').split()


def case(source, name):
    match = re.search(r'\bOpCase\('+re.escape(name)+r'\):\s*\{', source)
    if not match: raise RuntimeError('Missing generated case '+name)
    depth = 1
    tokens = re.finditer(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]', source[match.end():], re.S)
    for token in tokens:
        if token[0]=='{': depth += 1
        if token[0]=='}': depth -= 1
        if depth==0: return source[match.start():match.end()+token.end()]
    raise RuntimeError('Unbalanced generated case '+name)


def group(source, name):
    marker = source.index('OpCase('+name+'):')
    start = source.rfind('\n{\n', 0, marker)+1
    # Shared generated cases carry a common lexical scope and epilogue. Admit
    # the whole group, never cut its labels or reconstruct the individual ops.
    end = source.index('\n}\n', marker)+2
    if start<=0 or end<=marker: raise RuntimeError('Missing shared case group '+name)
    return source[start:end]


def fallible_equality(body):
    """Preserve the generated branch, but test scratch failure before taking it."""
    calls=list(re.finditer(r'\b(eq|EQ)\(',body))
    if not calls: return body
    if len(calls)!=1: raise RuntimeError('Unreviewed multiple equality calls')
    call=calls[0]
    depth=1
    end=call.end()
    while depth and end<len(body):
        if body[end]=='(': depth+=1
        elif body[end]==')': depth-=1
        end+=1
    if depth: raise RuntimeError('Unbalanced equality call')
    branch=body.rfind('  if (',0,call.start())
    if branch<0: raise RuntimeError('Equality outside reviewed condition')
    args=body[call.end():end-1]
    # Literal cases skip compound equality for immediate sources. EQ cases
    # retain native identity/immediate fast paths inside the owned helper.
    guard='!is_immed(src) && ' if call[1]=='eq' else ''
    prefix=('  int lb_equal=0;\n  if ('+guard+
            'lb_term_equal(c_p->entry_module->space->domain, '+args+
            ', &lb_equal)!=LB_ALLOC_OK) goto memory_failure;\n')
    return body[:branch]+prefix+body[branch:call.start()]+'lb_equal'+body[end:]


def fallible_arithmetic(body):
    """Adapt native fragments to owned collection without stale destinations."""
    if 'erts_unary_minus' in body:
        native, owned, bif = 'erts_unary_minus', 'lb_unary_minus', 'BIF_sminus_1'
    else:
        operation = 'times' if 'erts_mixed_times' in body else 'plus'
        native, owned, bif = f'erts_mixed_{operation}', f'lb_mixed_{operation}', f'BIF_s{operation}_2'
    if native not in body: return body
    body=re.sub(r'#ifdef DEBUG\s*Eterm\* orig_htop = HTOP;\s*Eterm\* orig_stop = E;\s*#endif','',body)
    for text in ('DEBUG_SWAPOUT;','DEBUG_SWAPIN;','c_p->fcalls = FCALLS;',
                 'FCALLS = c_p->fcalls;','ASSERT(orig_htop == HTOP && orig_stop == E);','ERTS_HOLE_CHECK(c_p);'):
        body=body.replace(text,'')
    body,count=re.subn(rf'result = {native}\s*\(c_p, ([^;]+)\);',
        rf'SWAPOUT; result = {owned}(c_p, \1); SWAPIN;\n'
        r'      dst_ptr = REG_TARGET_PTR(dst); FCALLS -= (Sint)c_p->last_native_cost;\n'
        r'      if (lb_alloc_domain_failure(c_p->entry_module->space->domain)==LB_ALLOC_WORK_LIMIT) goto work_failure;\n'
        r'      if (c_p->status == LB_PROCESS_NO_MEMORY) goto memory_failure;\n'
        r'      if (c_p->status == LB_PROCESS_LIMIT) goto resource_limit;\n'
        r'      if (c_p->status == LB_PROCESS_ENVIRONMENT) goto environment_failure;\n'
        r'      if (c_p->status == LB_PROCESS_EXCEPTION) goto find_func_info;',body)
    error=f'I = handle_error(c_p, I, reg, &BIF_TRAP_EXPORT({bif})->info.mfa);\n      goto post_error_handling;'
    expected_errors=0 if 'OpCase(i_increment_rWd)' in body else 1
    if count!=1 or body.count(error)!=expected_errors: raise RuntimeError('Unreviewed arithmetic boundary')
    # Preserve native tagged arithmetic without shifting a negative signed
    # increment when the compiler-overflow fast path is enabled.
    body=body.replace('(Sint)increment_val << _TAG_IMMED1_SIZE', '(Sint)((Uint)increment_val << _TAG_IMMED1_SIZE)')
    return body.replace(error,'goto find_func_info;')


def fallible_integer_division(body):
    if 'OpCase(i_int_div_jssd)' in body:
        operands, bif = 'op1, op2', 'BIF_intdiv_2'
    elif 'rem__execute:' in body:
        operands, bif = 'RemOp1, RemOp2', 'BIF_rem_2'
    else:
        return body
    body=re.sub(r'#ifdef DEBUG\s*Eterm\* orig_htop = HTOP;\s*Eterm\* orig_stop = E;\s*#endif','',body)
    for text in ('DEBUG_SWAPOUT;','DEBUG_SWAPIN;','c_p->fcalls = FCALLS;',
                 'FCALLS = c_p->fcalls;','ASSERT(orig_htop == HTOP && orig_stop == E);','ERTS_HOLE_CHECK(c_p);'):
        body=body.replace(text,'')
    call=f'succeeded = erts_int_div_rem(c_p, {operands}, &q, &r);'
    if body.count(call)!=1: raise RuntimeError('Unreviewed integer division boundary')
    body=body.replace(call,
        f'SWAPOUT; succeeded = lb_int_div_rem(c_p, {operands}, &q, &r); SWAPIN;\n'
        '        dst_ptr = REG_TARGET_PTR(dst); FCALLS -= (Sint)c_p->last_native_cost;\n'
        '        if(lb_alloc_domain_failure(c_p->entry_module->space->domain)==LB_ALLOC_WORK_LIMIT) goto work_failure;\n'
        '        if(c_p->status==LB_PROCESS_NO_MEMORY) goto memory_failure;\n'
        '        if(c_p->status==LB_PROCESS_LIMIT) goto resource_limit;\n'
        '        if(c_p->status==LB_PROCESS_INVALID) goto malformed_code;')
    pattern=rf'I = handle_error\(c_p, I, reg, &BIF_TRAP_EXPORT\({bif}\)->info.mfa\);\s*goto post_error_handling;'
    body,count=re.subn(pattern,'goto find_func_info;',body)
    if count!=2: raise RuntimeError('Unreviewed integer division error paths')
    return body


def fallible_writable_init(body):
    if 'OpCase(bs_init_writable):' not in body: return body
    old='  HEAVY_SWAPOUT;\n  x(0) = erts_bs_init_writable(c_p, x(0));\n  HEAVY_SWAPIN;'
    if body.count(old)!=1 or body.count('  GotoPF(next_pf);')!=1:
        raise RuntimeError('Unreviewed writable initializer boundary')
    body=body.replace(old,
        '  Eterm lb_binary;\n  SWAPOUT;\n'
        '  LbWritableStatus lb_status=lb_writable_init_term(c_p,x(0),&lb_binary);\n'
        '  SWAPIN;\n  FCALLS-=(Sint)c_p->last_native_cost;\n'
        '  if (lb_status==LB_WRITABLE_NO_MEMORY) goto memory_failure;\n'
        '  if (lb_status==LB_WRITABLE_LIMIT) goto resource_limit;\n'
        '  if (lb_status!=LB_WRITABLE_OK) goto malformed_code;\n'
        '  x(0)=lb_binary;')
    # Publication and the next real PC precede yielding. No constructor replay
    # or borrowed writer survives the boundary. One bounded operation may overrun.
    return body.replace('  GotoPF(next_pf);',
        '  if (FCALLS<=0) goto context_switch3;\n  GotoPF(next_pf);')


def owned_send(body):
    if 'OpCase(send):' not in body: return body
    replacements={
        'PRE_BIF_SWAPOUT(c_p);':'SWAPOUT;',
        'c_p->fcalls = FCALLS - 1;':'',
        'ERTS_VERIFY_UNUSED_TEMP_ALLOC(c_p);':'',
        'ERTS_REQ_PROC_MAIN_LOCK(c_p);':'',
        'PROCESS_MAIN_CHK_LOCKS(c_p);':'',
        'HTOP = HEAP_TOP(c_p);':'SWAPIN;',
        'FCALLS = c_p->fcalls;':'FCALLS-=1+(Sint)c_p->last_native_cost;\n  if(c_p->status==LB_PROCESS_NO_MEMORY) goto memory_failure;\n  if(c_p->status==LB_PROCESS_LIMIT) goto resource_limit;\n  if(c_p->status==LB_PROCESS_REFUSED) goto execution_refused;\n  if(c_p->status==LB_PROCESS_CAPABILITY_DENIED) goto capability_denied;\n  if(c_p->status==LB_PROCESS_ENVIRONMENT) goto environment_failure;'
    }
    for old,new in replacements.items():
        if body.count(old)!=1: raise RuntimeError('Changed native send boundary '+old)
        body=body.replace(old,new)
    # Complete the local send once, then yield at its successor. The retained
    # native trap branch has no producer in this positive local-PID slice.
    body=body.replace('  GotoPF(next_pf);','  if(FCALLS<=0) goto context_switch3;\n  GotoPF(next_pf);')
    return body


def owned_bif_call(body):
    if 'i_bif__call:' not in body and 'i_bif_body__call:' not in body: return body
    # Positive nonallocating BIFs and gc_bif1 abs/1 and floor/1 share these complete native
    # groups. Both stage numeric payloads before collection; guest badarg
    # precedes collection, so the native error operands cannot become stale.
    # Infrastructure failures bypass the guest guard-failure branch.
    body=body.replace('Eterm tmp_reg[3];','Eterm tmp_reg[3]={NIL,NIL,NIL};')
    body=re.sub(r'#ifdef DEBUG\s*Eterm\* orig_htop = HTOP;\s*Eterm\* orig_stop = E;\s*#endif',
                'Eterm* orig_htop=HTOP; Eterm* orig_stop=E;',body)
    body=body.replace('ASSERT(orig_htop == HTOP && orig_stop == E);',
                      'if(bf!=lb_bif_abs_1 && bf!=lb_bif_floor_1 && (orig_htop!=HTOP || orig_stop!=E)) goto malformed_code;\n'
                      '      dst_ptr=REG_TARGET_PTR(dst);')
    for text in ('c_p->fcalls = FCALLS;','FCALLS = c_p->fcalls;',
                 'ERTS_DBG_CHK_REDS(c_p, FCALLS);','ASSERT(!ERTS_PROC_IS_EXITING(c_p));',
                 'ASSERT(!ERTS_PROC_IS_EXITING(c_p) || is_non_value(result));',
                 'ERTS_CHK_MBUF_SZ(c_p);','ERTS_HOLE_CHECK(c_p);'):
        body=body.replace(text,'')
    body=body.replace('DEBUG_SWAPOUT;','SWAPOUT;').replace('DEBUG_SWAPIN;','SWAPIN;')
    call='result = (*bf)(c_p, tmp_reg, I);'
    if body.count(call)!=1: raise RuntimeError('Changed owned BIF call boundary')
    body=body.replace(call,
        'c_p->last_native_cost=0; '+call+'\n'
        '      FCALLS-=(Sint)c_p->last_native_cost;\n'
        '      if(lb_alloc_domain_failure(c_p->entry_module->space->domain)==LB_ALLOC_WORK_LIMIT) goto work_failure;\n'
        '      if(c_p->status==LB_PROCESS_REFUSED) goto execution_refused;\n'
        '      if(c_p->status==LB_PROCESS_CAPABILITY_DENIED) goto capability_denied;\n'
        '      if(c_p->status==LB_PROCESS_NO_MEMORY) goto memory_failure;\n'
        '      if(c_p->status==LB_PROCESS_LIMIT) goto resource_limit;\n'
        '      if(c_p->status==LB_PROCESS_ENVIRONMENT) goto environment_failure;\n'
        '      if(c_p->status==LB_PROCESS_INVALID) goto malformed_code;')
    body=body.replace('SET_I((BeamInstr *) I+4);',
        'SET_I((BeamInstr *) I+4); if(FCALLS<=0) goto context_switch3;')
    error='I = handle_error(c_p, I, reg, ubif2mfa((void *) bf));\n    goto post_error_handling;'
    expected=1 if 'i_bif_body__call:' in body else 0
    if body.count(error)!=expected: raise RuntimeError('Changed predicate error boundary')
    body=body.replace(error,'goto find_func_info;')
    return body


def owned_make_fun(body):
    if 'OpCase(i_make_fun3_Fdtt):' not in body: return body
    old='  funp = (ErlFunThing*)HTOP;'
    if body.count(old)!=1: raise RuntimeError('Changed fun construction boundary')
    body=body.replace(old,
        '  if(!lb_work_domain_charge(c_p->entry_module->space->domain,num_free+lb_work_bulk_units(num_free))) goto work_failure;\n'
        '  LbAllocStatus held=lb_fun_retain(c_p,fe);\n'
        '  FCALLS-=1+num_free;\n'
        '  if(held==LB_ALLOC_NO_MEMORY) goto memory_failure;\n'
        '  if(held!=LB_ALLOC_OK || (Uint)(E-HTOP)<ERL_FUN_SIZE+(Uint)num_free+S_RESERVED) goto malformed_code;\n'+old)
    old='const ErtsCodeMFA *mfa = erts_get_fun_mfa(fe, erts_active_code_ix());'
    if body.count(old)!=1: raise RuntimeError('Changed fun debug MFA boundary')
    body=body.replace(old,
        'LbMFA actual_mfa; memcpy(&actual_mfa,fe->dispatch.addresses[0]-sizeof(LbMFA)/sizeof(BeamInstr),sizeof(actual_mfa));\n'
        '    const LbMFA *mfa=&actual_mfa;')
    old='ASSERT(tb((tmp_packed1>>BEAM_TIGHT_SHIFT)&BEAM_TIGHT_MASK) == fe->arity);'
    if body.count(old)!=1: raise RuntimeError('Changed fun arity boundary')
    body=body.replace(old,old.replace('fe->arity','(Uint)fe->arity'))
    old='  Goto(*I);ASSERT(!"Fell through \'i_make_fun3\' (-no_next)");'
    if body.count(old)!=1: raise RuntimeError('Changed fun construction completion')
    return body.replace(old,'  if(FCALLS<=0) goto context_switch3;\n'+old)


def owned_apply(body):
    if not re.search(r'OpCase\((?:i_apply|i_apply_last_Q|i_apply_only|apply_t|apply_last_tQ)\):',body): return body
    pattern=r'HEAVY_SWAPOUT;\s+ep = (fixed_apply|apply)\(c_p, reg, (.*?)\);\s+HEAVY_SWAPIN;'
    def call(match):
        arguments=match[2]
        if match[1]=='fixed_apply':
            # The first operand is the arity expression; remaining native
            # arguments only adjust tracing/error-handler stack metadata.
            arity=arguments.split(', ',1)[0]
            helper=f'lb_fixed_apply(c_p,{arity})'
        else:
            helper='lb_apply(c_p)'
        return ('SWAPOUT; c_p->heap_reserved=0; ep='+helper+'; SWAPIN;\n'
                '    FCALLS-=(Sint)c_p->last_native_cost;\n'
                '    if(c_p->status==LB_PROCESS_WORK_LIMIT) goto work_failure;\n'
                '    if(c_p->status==LB_PROCESS_LIMIT) goto resource_limit;\n'
                '    if(c_p->status==LB_PROCESS_NO_MEMORY) goto memory_failure;\n'
                '    if(c_p->status==LB_PROCESS_REFUSED) goto execution_refused;\n'
                '    if(c_p->status==LB_PROCESS_INVALID) goto malformed_code;')
    body,count=re.subn(pattern,call,body)
    if count!=1: raise RuntimeError('Changed native apply boundary')
    body,count=re.subn(r'static ErtsCodeMFA apply3_mfa = \{am_erlang, am_apply, 3\};\s*I = handle_error\(c_p, I, reg, &apply3_mfa\);\s*goto post_error_handling;',
        'c_p->current=&c_p->entry_module->space->apply_export.info.mfa;\n        goto find_func_info;',body)
    if count!=1: raise RuntimeError('Changed native apply error boundary')
    body,count=re.subn(r' else if \(ERTS_PROC_GET_SAVED_CALLS_BUF\(c_p\)\) \{\s*save_calls\(c_p, ep\);\s*\}', '',body)
    if count!=1: raise RuntimeError('Changed apply saved-call instrumentation')
    return body


def owned_call_fun(body):
    if not re.search(r'OpCase\(i_call_fun_(?:t|last_tQ)\):',body): return body
    pattern=r'HEAVY_SWAPOUT;\s+next = call_fun\(c_p, (.*?), reg, THE_NON_VALUE\);\s+HEAVY_SWAPIN;'
    body,count=re.subn(pattern,lambda m:
        'SWAPOUT; c_p->heap_reserved=0; next=lb_fun_call(c_p,'+m[1]+'); SWAPIN;\n'
        'FCALLS-=(Sint)c_p->last_native_cost;\n'
        'if(c_p->status==LB_PROCESS_NO_MEMORY) goto memory_failure;\n'
        'if(c_p->status==LB_PROCESS_REFUSED) goto execution_refused;\n'
        'if(c_p->status==LB_PROCESS_INVALID) goto malformed_code;',body)
    if count!=1: raise RuntimeError('Unreviewed fun call boundary')
    return body


def fallible_self(body):
    if not re.search(r'OpCase\(self_[xy]\):',body): return body
    if body.count('c_p->common.id')!=1 or body.count('GotoPF(next_pf);')!=1:
        raise RuntimeError('Unreviewed self instruction boundary')
    body=body.replace('  BeamInstr next_pf',
        '  Eterm identity;\n'
        '  LbProcessStatus identity_status=lb_process_identity(c_p,&identity);\n'
        '  FCALLS-=(Sint)c_p->last_native_cost;\n'
        '  if(identity_status==LB_PROCESS_NO_MEMORY) goto memory_failure;\n'
        '  if(identity_status==LB_PROCESS_LIMIT) goto resource_limit;\n'
        '  if(identity_status!=LB_PROCESS_READY) goto malformed_code;\n'
        '  BeamInstr next_pf')
    return body.replace('c_p->common.id','identity').replace('GotoPF(next_pf);','if(FCALLS<=0) goto context_switch3; GotoPF(next_pf);')


def owned_node(body):
    if not re.search(r'OpCase\(node_[xy]\):',body): return body
    if body.count('erts_this_node->sysname')!=1:
        raise RuntimeError('Unreviewed node instruction boundary')
    # Pure owned-state read, covered by the ordinary instruction work charge.
    return body.replace('erts_this_node->sysname','c_p->entry_module->space->node_name')


def binary_failure_checks():
    return ('if (build.status==LB_BUILD_NO_MEMORY) goto memory_failure;\n'
            'if (build.status==LB_BUILD_LIMIT) goto resource_limit;\n'
            'if (build.status==LB_BUILD_REFUSED) goto execution_refused;\n'
            'if (build.status==LB_BUILD_ENVIRONMENT) goto environment_failure;\n'
            'if (build.status!=LB_BUILD_GUEST_ERROR) goto malformed_code;\n')


def fallible_binary_build(body):
    if 'OpCase(i_bs_create_bin_jIWdW):' not in body: return body
    if hashlib.sha256(body.encode()).hexdigest()!='92f19e31911500eb3b33a5f5ebeadbb3cff106d66abcf9984e5839176cd28c42':
        raise RuntimeError('Unreviewed complete binary construction case')
    start=body.index('  /* We count the total number of bits')
    end=body.index('  /* Construct the segments. */')
    phase=body[start:end]
    # Delegate the COMPLETE sizing/allocation phases to their owned native
    # transplants. No kind or allocation branch is an optimization omission.
    for token in ('case BSC_UTF32:', 'if (p[0] == BSC_APPEND)',
                  'else if (p[0] == BSC_PRIVATE_APPEND)', 'erts_wrap_refc_bitstring'):
        if token not in phase: raise RuntimeError('Changed binary construction phase '+token)
    replacement=('  LbBinaryBuild build; LbBinaryBuild *EBS=&build;\n'
        '  SWAPOUT;\n  lb_binary_build_begin(c_p,p_start,n,alloc,I[3],&build);\n'
        '  if (build.status!=LB_BUILD_OK) lb_binary_build_raise(c_p,&build);\n  SWAPIN;\n'
        '  if (build.status!=LB_BUILD_OK) {\n'
        '    FCALLS-=(Sint)build.cost; build.cost=0;\n'+binary_failure_checks()+
        '    if (I[1]) { I+=(Sint)I[1]; if (FCALLS<=0) goto context_switch3; Goto(*I); }\n'
        '    goto find_func_info;\n  }\n'
        '  if (p_start[0]==BSC_APPEND || p_start[0]==BSC_PRIVATE_APPEND) p_start+=BSC_NUM_ARGS;\n\n')
    body=body[:start]+replacement+body[end:]
    for declaration in ('  Eterm new_binary;\n', '  ErlBitsState* EBS = ERL_BITS_EBS_FROM_REG(reg);\n'):
        if body.count(declaration)!=1: raise RuntimeError('Changed binary construction local')
        body=body.replace(declaration,'')
    error=re.compile(r'erts_prepare_bs_construct_fail_info\(c_p, p, ([^,]+), ([^,]+), ([^)]+)\);')
    body,count=error.subn(lambda m: 'SWAPOUT; lb_binary_build_error(c_p,&build,p,'+
        m[1]+','+m[2]+','+m[3]+'); SWAPIN;\n'
        'FCALLS-=(Sint)build.cost; build.cost=0;\n'+binary_failure_checks(),body)
    if count!=9: raise RuntimeError('Changed binary writer failure branches')
    replacements={
        'erts_bs_put_binary_all(EBS, c_p,':'lb_build_binary_all(EBS,',
        'erts_bs_put_binary(EBS, c_p,':'lb_build_binary(EBS,',
        'erts_bs_put_float(EBS, c_p,':'lb_build_float(EBS,',
        'erts_bs_put_integer_le(':'lb_build_integer_le(',
        'erts_bs_put_integer_be(':'lb_build_integer_be(',
        'erts_bs_put_utf8(':'lb_build_utf8(', 'erts_bs_put_utf16(':'lb_build_utf16(',
        'erts_bs_put_string(':'lb_build_string(',
        'is_bitstring(Src)':'lb_build_is_bitstring(Src)',
        'c_p->fvalue':'build.float_info', 'byte* string;':'const unsigned char* string;',
        '(byte *) p[3]':'(const unsigned char *) p[3]',
        'FCALLS = c_p->fcalls;':'FCALLS-=(Sint)build.cost; build.cost=0;',
        '*dst_ptr = new_binary;':'if (build.status!=LB_BUILD_OK || build.writer.offset!=build.expected) goto malformed_code;\n  *dst_ptr = build.result;',
    }
    counts=(1,2,2,2,2,1,1,1,2,2,1,1,1,1)
    if len(replacements)!=len(counts): raise RuntimeError('Incomplete binary substitution counts')
    for (old,new),expected in zip(replacements.items(),counts):
        if body.count(old)!=expected: raise RuntimeError('Changed binary dependency '+old)
        body=body.replace(old,new)
    old='      lb_build_string(EBS, string, Size);'
    if body.count(old)!=1: raise RuntimeError('Changed string writer boundary')
    body=body.replace(old,old+'\n      if (build.status!=LB_BUILD_OK) {\n'
        '        FCALLS-=(Sint)build.cost;\n'+binary_failure_checks()+'      }')
    # Guard and success exits must honor charged work at a real, rooted PC.
    body=body.replace('Goto(*I);;', 'if (FCALLS<=0) goto context_switch3; Goto(*I);;')
    body=body.replace('  Goto(*I);\n}', '  if (FCALLS<=0) goto context_switch3; Goto(*I);\n}')
    body=body.replace('BSC_NUM_ARGS','5')
    body=re.sub(r'\bBSC_(\w+)',r'LB_BSC_\1',body)
    return body


def owned_receive_wait(body):
    if 'OpCase(wait_locked_f)' not in body: return body
    body=re.sub(r'erts_proc_(?:lock|unlock)\(c_p, ERTS_PROC_LOCKS_MSG_RECEIVE\);', '', body)
    body=body.replace('      const BeamInstr** pi = (const BeamInstr **) c_p->def_arg_reg;\n      *pi = I+3;', '      c_p->receive_timeout_pc=I+3;')
    body=body.replace('            const BeamInstr** pi = (const BeamInstr **) c_p->def_arg_reg;\n            *pi = I+3;', '            c_p->receive_timeout_pc=I+3;')
    body=body.replace('erts_set_proc_timer_uword(c_p, I[1]);',
        'LbProcessStatus lb_timer_status=lb_receive_timer_set(c_p,I[1]);\n'
        '      if(lb_timer_status==LB_PROCESS_LIMIT) goto resource_limit;\n'
        '      if(lb_timer_status!=LB_PROCESS_READY) goto environment_failure;')
    body=body.replace('timeout_value == am_infinity', 'lb_receive_is_infinity(c_p,timeout_value)')
    body=body.replace('int tres = erts_set_proc_timer_term(c_p, timeout_value);',
        'int tres=!is_small(timeout_value) || signed_val(timeout_value)<0;\n'
        '          if(!tres) {\n'
        '            LbProcessStatus lb_timer_status=lb_receive_timer_set(c_p,(Uint)signed_val(timeout_value));\n'
        '            if(lb_timer_status==LB_PROCESS_LIMIT) goto resource_limit;\n'
        '            if(lb_timer_status!=LB_PROCESS_READY) goto environment_failure;\n          }')
    body=body.replace('EXC_TIMEOUT_VALUE', 'LB_EXC_TIMEOUT_VALUE')
    body=body.replace('erts_msgq_set_save_first(c_p);', 'c_p->messages.save=&c_p->messages.first;')
    body=body.replace('c_p->i = I + (I[1]);', 'SET_I(I+(Sint)I[1]);')
    body=body.replace('ERTS_PTMR_IS_TIMED_OUT(c_p)', 'c_p->receive_timed_out')
    body=body.replace('erts_atomic32_read_band_relb(&c_p->state,\n      ~ERTS_PSFLG_ACTIVE);', 'c_p->waiting=1;')
    body=body.replace('ERTS_PROC_IS_EXITING(c_p)', '(c_p->status!=LB_PROCESS_READY && c_p->status!=LB_PROCESS_YIELDED)')
    body=body.replace('goto do_schedule;', 'goto context_switch3;')
    return body


def owned_receive_timeout(body):
    if 'OpCase(timeout)' not in body and 'OpCase(timeout_locked)' not in body: return body
    body=re.sub(r'  if \(ERTS_IS_P_TRACED_FL\(c_p, F_TRACE_RECEIVE\)\) \{.*?\n  \}', '', body, flags=re.S)
    body=re.sub(r'  if \(ERTS_PROC_GET_SAVED_CALLS_BUF\(c_p\)\) \{.*?\n  \}', '', body, flags=re.S)
    body=body.replace('erts_proc_unlock(c_p, ERTS_PROC_LOCKS_MSG_RECEIVE);', '')
    body=body.replace('c_p->flags &= ~F_TIMO;', 'lb_receive_timer_cancel(c_p);')
    body=body.replace('erts_msgq_set_save_first(c_p);', 'c_p->messages.save=&c_p->messages.first;')
    return body


def owned_receive_remove(body):
    if 'OpCase(remove_message)' not in body: return body
    body=body.replace('ErtsMessage*', 'LbMessage*')
    body=body.replace('erts_msgq_peek_msg(c_p)', '(*c_p->messages.save)')
    start=body.index('  if (ERTS_PROC_GET_SAVED_CALLS_BUF(c_p))')
    end=body.index('  erts_msgq_unlink_msg_set_save_first',start)
    # Saved-call, sequence-trace and probe effects have no admitted producers.
    # Check the fragment token instead of silently accepting a traced message.
    body=body[:start]+'  if (msgp->m[1]!=NIL) goto execution_refused;\n'+body[end:]
    body=body.replace('erts_msgq_unlink_msg_set_save_first(c_p, msgp);',
        'if(!lb_work_domain_charge(c_p->entry_module->space->domain,LB_RECV_MARKERS)) goto work_failure;\n'
        '  if(msgp!=c_p->receive_message || lb_message_queue_take(&c_p->messages)!=msgp) goto malformed_code;\n'
        '  c_p->messages.save=&c_p->messages.first;\n  c_p->receive_message=NULL; c_p->receive_root=NIL;\n'
        '  lb_work_receive_end(c_p,1);')
    body=body.replace('CANCEL_TIMER(c_p);', 'lb_receive_timer_cancel(c_p);')
    body=body.replace('erts_save_message_in_proc(c_p, msgp);', 'lb_message_destroy(msgp);')
    body=body.replace('ERTS_IS_GC_DESIRED_INTERNAL(c_p, HTOP, E, 0)',
        '(c_p->off_heap.overhead>=c_p->bin_vheap_sz)')
    body=body.replace('ERTS_VBUMP_LEAVE_REDS_INTERNAL(c_p, 5, FCALLS);', 'if(FCALLS>5) FCALLS=5;')
    body=body.replace('ERTS_DBG_CHK_REDS(c_p, FCALLS);', '')
    body=body.replace('ERTS_CHK_MBUF_SZ(c_p);', '')
    return body


def owned_receive_scan(body):
    if 'OpCase(i_loop_rec_f)' not in body and 'OpCase(loop_rec_end_f)' not in body:
        return body
    body=body.replace('goto do_schedule;', 'goto context_switch3;')
    body=body.replace('ERTS_PROC_IS_EXITING(c_p)', '(c_p->status!=LB_PROCESS_READY && c_p->status!=LB_PROCESS_YIELDED)')
    body=body.replace('erts_msgq_peek_msg(c_p)', '(*c_p->messages.save)')
    body=body.replace('ErtsMessage*', 'LbMessage*')
    body=body.replace('ERTS_UNREQ_PROC_MAIN_LOCK(c_p);', '')
    body=body.replace('ERTS_REQ_PROC_MAIN_LOCK(c_p);', '')
    body=body.replace('FCALLS -= erts_proc_sig_receive_helper(c_p, FCALLS, neg_o_reds,\n    &msgp, &get_out);',
                      'get_out=0; msgp=*c_p->messages.save;')
    body=body.replace('ERTS_SIG_IS_MSG(msgp)', '(msgp->queue==&c_p->messages)')
    body=body.replace('ERTS_SIG_IS_EXTERNAL_MSG(msgp)', 'is_non_value(msgp->m[0])')
    body=body.replace('ERTS_SIG_IS_INTERNAL_MSG(msgp)', '!is_non_value(msgp->m[0])')
    start=body.find('    if (!erts_proc_sig_decode_dist(')
    if start>=0:
        end=body.index('    SWAPIN;',start)
        # Distribution has no admitted producer/decoder. Refuse the effect,
        # rather than dropping a message or manufacturing successful decoding.
        body=body[:start]+'    goto execution_refused;\n'+body[end:]
    body=body.replace('  x(0) = ERL_MESSAGE_TERM(msgp);',
        '  SWAPOUT;\n  Eterm lb_view;\n  LbProcessStatus lb_receive_status=lb_message_peek_owned(c_p,&lb_view);\n  SWAPIN;\n'
        '  if(lb_receive_status==LB_PROCESS_NO_MEMORY) goto memory_failure;\n'
        '  if(lb_receive_status==LB_PROCESS_LIMIT) goto resource_limit;\n'
        '  if(lb_receive_status==LB_PROCESS_WORK_LIMIT) goto work_failure;\n'
        '  if(lb_receive_status!=LB_PROCESS_READY || is_non_value(lb_view)) goto malformed_code;\n'
        '  FCALLS-=(Sint)c_p->last_native_cost;\n  x(0)=lb_view;')
    body=body.replace('erts_msgq_set_save_next(c_p);', 'lb_message_skip_owned(c_p);')
    return body


def owned_map_single_lookup(body):
    pattern=r'Eterm res = get_map_element(?:_hash)?\((.*?)\);'
    match=re.search(pattern,body)
    if not match: return body
    args=[part.strip() for part in match[1].split(',')]
    if len(args) not in (2,3): raise RuntimeError('Unreviewed map lookup operands')
    replacement=('Eterm lb_map_term='+args[0]+'; Eterm lb_key='+args[1]+'; Eterm res; int lb_is_map=0;\n'
        '  LbMapStatus lb_status=lb_map_is_map(lb_map_term,&lb_is_map); MAP_STATUS(lb_status);\n'
        '  if(!lb_is_map) res=THE_NON_VALUE; else {\n'
        '    c_p->last_native_cost=0;\n'
        '    lb_status=lb_map_find(c_p,lb_map_term,lb_key,&res);\n'
        '    FCALLS-=(Sint)c_p->last_native_cost;\n'
        '    if(lb_status==LB_MAP_MISSING) res=THE_NON_VALUE; else MAP_STATUS(lb_status);\n'
        '  }')
    body=body[:match.start()]+replacement+body[match.end():]
    body=body.replace('  I += 3;','  I += 3; if(FCALLS<=0) goto context_switch3;',1)
    body=body.replace('  I += 5;','  I += 5; if(FCALLS<=0) goto context_switch3;',1)
    return body


def owned_map_bulk_lookup(body):
    return '''OpCase(i_get_map_elements_fsI):
{
  Eterm packed=I[1], map;
  GetSource(I[2],map);
  Uint words=Ib(packed>>BEAM_WIDE_SHIFT);
  if(words%3 || words/3>LB_FLATMAP_MAX_SIZE) goto malformed_code;
  if(!lb_work_domain_charge(c_p->entry_module->space->domain,1)) goto work_failure;
  FCALLS--;
  int is_map=0;
  LbMapStatus status=lb_map_is_map(map,&is_map);
  MAP_STATUS(status);
  if(!is_map) { I+=fb(packed&BEAM_WIDE_MASK); Goto(*I); }
  const BeamInstr *args=I+3;
  Uint count=words/3;
  for(Uint i=0;i<count;++i) {
    Eterm value,key;
    GetSource(args[0],key);
    c_p->last_native_cost=0;
    status=lb_map_find(c_p,map,key,&value);
    FCALLS-=(Sint)c_p->last_native_cost;
    if(status==LB_MAP_MISSING) { I+=fb(packed&BEAM_WIDE_MASK); Goto(*I); }
    MAP_STATUS(status);
    MAP_PUT_TERM_REG(value,args[1]);
    args+=3;
  }
  I+=3+words;
  if(FCALLS<=0) goto context_switch3;
  Goto(*I);
}'''


def owned_map_construction(body,name):
    if name=='new_map_dtI':
        return '''OpCase(new_map_dtI):
{
  Eterm packed=I[1], dst=db(packed&BEAM_LOOSE_MASK), result;
  Uint words=I[2], live=tb(packed>>BEAM_LOOSE_SHIFT);
  if((words&1) || words>2*LB_FLATMAP_MAX_SIZE || live>MAX_REG) goto malformed_code;
  size_t count=words/2;
  size_t reserve=LB_FLATMAP_WORDS+count+(count?count+1:2);
  c_p->last_native_cost=0; SWAPOUT;
  LbMapStatus status=lb_map_reserve(c_p,reserve,live);
  SWAPIN; FCALLS-=(Sint)c_p->last_native_cost; MAP_STATUS(status);
  Eterm keys[LB_FLATMAP_MAX_SIZE],values[LB_FLATMAP_MAX_SIZE];
  for(size_t i=0;i<count;++i) { GetSource(I[3+2*i],keys[i]); GetSource(I[4+2*i],values[i]); }
  c_p->last_native_cost=0; SWAPOUT;
  status=lb_map_create(c_p,keys,values,count,&result);
  SWAPIN; FCALLS-=(Sint)c_p->last_native_cost; c_p->heap_reserved=0; MAP_STATUS(status);
  *REG_TARGET_PTR(dst)=result; I+=3+words;
  if(FCALLS<=0) goto context_switch3;
  Goto(*I);
}'''
    if name=='i_new_small_map_lit_dtq':
        return '''OpCase(i_new_small_map_lit_dtq):
{
  Eterm packed=I[1], dst=db(packed&BEAM_LOOSE_MASK), keys=I[2], result;
  Uint live=tb(packed>>BEAM_LOOSE_SHIFT);
  if(!is_tuple(keys) || live>MAX_REG) goto malformed_code;
  size_t count=(size_t)arityval(*tuple_val(keys));
  if(count>LB_FLATMAP_MAX_SIZE) goto execution_refused;
  size_t reserve=LB_FLATMAP_WORDS+count+(count?count+1:2);
  c_p->last_native_cost=0; SWAPOUT;
  LbMapStatus status=lb_map_reserve(c_p,reserve,live);
  SWAPIN; FCALLS-=(Sint)c_p->last_native_cost; MAP_STATUS(status);
  Eterm values[LB_FLATMAP_MAX_SIZE];
  for(size_t i=0;i<count;++i) GetSource(I[3+i],values[i]);
  c_p->last_native_cost=0; SWAPOUT;
  status=lb_map_create_literal(c_p,keys,values,count,&result);
  SWAPIN; FCALLS-=(Sint)c_p->last_native_cost; c_p->heap_reserved=0; MAP_STATUS(status);
  *REG_TARGET_PTR(dst)=result; I+=3+count;
  if(FCALLS<=0) goto context_switch3;
  Goto(*I);
}'''
    return body


def owned_map_update_group(body,name):
    if name=='update_map_assoc_cdtI':
        return '''{
  Eterm map;
  OpCase(update_map_assoc_cdtI): { map=I[1]; } goto map_assoc_execute;
  OpCase(update_map_assoc_xdtI): { map=xb(I[1]); } goto map_assoc_execute;
  OpCase(update_map_assoc_ydtI): { map=yb(I[1]); } goto map_assoc_execute;
map_assoc_execute: {
  Eterm packed=I[2],dst=db(packed&BEAM_LOOSE_MASK);
  Uint live=tb(packed>>BEAM_LOOSE_SHIFT),words=I[3];
  if(words&1 || words>2*LB_FLATMAP_MAX_SIZE || live>=MAX_REG) goto malformed_code;
  size_t count=words/2, reserve=0; int is_map=0;
  reg[live]=map;
  LbMapStatus status=lb_map_is_map(reg[live],&is_map); MAP_STATUS(status);
  if(!is_map) { c_p->heap_reserved=0; c_p->freason=LB_EXC_BADMAP; c_p->fvalue=reg[live]; goto find_func_info; }
  if(count) {
    status=lb_map_reservation_words(reg[live],count,0,&reserve); MAP_STATUS(status);
    c_p->last_native_cost=0; SWAPOUT;
    status=lb_map_reserve(c_p,reserve,live+1);
    SWAPIN; FCALLS-=(Sint)c_p->last_native_cost; MAP_STATUS(status);
  }
  map=reg[live];
  Eterm keys[LB_FLATMAP_MAX_SIZE],values[LB_FLATMAP_MAX_SIZE],result;
  for(size_t i=0;i<count;++i) { GetSource(I[4+2*i],keys[i]); GetSource(I[5+2*i],values[i]); }
  c_p->last_native_cost=0; SWAPOUT;
  status=lb_map_update_assoc(c_p,map,keys,values,count,&result);
  SWAPIN; FCALLS-=(Sint)c_p->last_native_cost; c_p->heap_reserved=0; MAP_STATUS(status);
  *REG_TARGET_PTR(dst)=result; I+=4+words;
  if(FCALLS<=0) goto context_switch3;
  Goto(*I);
}
}'''
    if name=='update_map_exact_xjdtI':
        return '''{
  Eterm map;
  OpCase(update_map_exact_xjdtI): { map=xb(I[1]); } goto map_exact_execute;
  OpCase(update_map_exact_yjdtI): { map=yb(I[1]); } goto map_exact_execute;
map_exact_execute: {
  Eterm packed=I[2], ext=I[3], dst=db(packed>>BEAM_WIDE_SHIFT);
  Sint fail=jb(packed&BEAM_WIDE_MASK);
  Uint live=tb(ext&BEAM_WIDE_MASK),words=Ib(ext>>BEAM_WIDE_SHIFT);
  if((words&1) || words>2*LB_FLATMAP_MAX_SIZE || live>=MAX_REG) goto malformed_code;
  size_t count=words/2,reserve=0; int is_map=0;
  reg[live]=map;
  LbMapStatus status=lb_map_is_map(reg[live],&is_map); MAP_STATUS(status);
  if(!is_map) {
    c_p->heap_reserved=0;
    if(fail) { I+=fail; Goto(*I); }
    c_p->freason=LB_EXC_BADMAP; c_p->fvalue=reg[live]; goto find_func_info;
  }
  if(count) {
    status=lb_map_reservation_words(reg[live],count,1,&reserve); MAP_STATUS(status);
    c_p->last_native_cost=0; SWAPOUT;
    status=lb_map_reserve(c_p,reserve,live+1);
    SWAPIN; FCALLS-=(Sint)c_p->last_native_cost; MAP_STATUS(status);
  }
  map=reg[live];
  Eterm keys[LB_FLATMAP_MAX_SIZE],values[LB_FLATMAP_MAX_SIZE],result,missing=NIL;
  for(size_t i=0;i<count;++i) { GetSource(I[4+2*i],keys[i]); GetSource(I[5+2*i],values[i]); }
  c_p->last_native_cost=0; SWAPOUT;
  status=lb_map_update_exact_missing(c_p,map,keys,values,count,&result,&missing);
  SWAPIN; FCALLS-=(Sint)c_p->last_native_cost; c_p->heap_reserved=0;
  if(status==LB_MAP_MISSING) {
    if(fail) { I+=fail; Goto(*I); }
    c_p->freason=LB_EXC_BADKEY; c_p->fvalue=missing; goto find_func_info;
  }
  MAP_STATUS(status);
  *REG_TARGET_PTR(dst)=result; I+=4+words;
  if(FCALLS<=0) goto context_switch3;
  Goto(*I);
}
}'''
    return body


def owned_map_test(name):
    source='xb' if name=='is_map_fx' else 'yb'
    return '''OpCase(%s):
{
  Eterm packed=I[1],value=%s(packed>>BEAM_WIDE_SHIFT);
  Sint fail=fb(packed&BEAM_WIDE_MASK); int is_map=0;
  if(!lb_work_domain_charge(c_p->entry_module->space->domain,1)) goto work_failure;
  FCALLS--;
  LbMapStatus status=lb_map_is_map(value,&is_map); MAP_STATUS(status);
  if(!is_map) { I+=fail; Goto(*I); }
  I+=2; if(FCALLS<=0) goto context_switch3; Goto(*I);
}''' % (name,source)


def project(source):
    bodies=[]
    groups=[group(source,name) for name in ('deallocate_return0','i_select_tuple_arity2_xfAA','i_select_tuple_arity_xfI','i_increment_rWd','i_plus_xxjd','i_rem_ssjd','i_unary_minus_cjd','i_bif1_sfbd','i_bif1_body_sbd','wait_locked_f','i_bs_start_match3_xtjd','i_bs_ensure_bits_read_xtf','i_bs_ensure_bits_unit_xItf','i_bs_ensure_bits_xIf')]
    map_assoc=group(source,'update_map_assoc_cdtI')
    map_exact=group(source,'update_map_exact_xjdtI')
    groups.extend((map_assoc,map_exact))
    names=NAMES+[name for shared in groups for name in re.findall(r'OpCase\((\w+)\)',shared)]
    selected=([(name,case(source,name)) for name in NAMES]+
        [('shared group',shared) for shared in groups[:-2]]+
        [('update_map_assoc_cdtI',map_assoc),('update_map_exact_xjdtI',map_exact)])
    for name, body in selected:
        if name in ('i_get_sd','i_get_hash_cWd'):
            call = ('erts_pd_hash_get(c_p, targ1)' if name=='i_get_sd'
                    else 'erts_pd_hash_get_with_hx(c_p, I[2], I[1])')
            source = 'targ1' if name=='i_get_sd' else 'I[1]'
            old = '*dst_ptr = '+call+';'
            if body.count(old)!=1: raise RuntimeError('Changed native dictionary lookup boundary')
            check = '' if name=='i_get_sd' else (
                'ASSERT(is_small(I[1]) || is_atom(I[1]));\n'
                '    ASSERT(I[2]==(is_small(I[1])?I[1]>>_TAG_IMMED1_SIZE:atom_val(I[1])));\n')
            body=body.replace(old,check+'Eterm args[1]={'+source+'};\n'
                '    Eterm result=lb_bif_get_1(c_p,args,I);\n'
                '    FCALLS-=(Sint)c_p->last_native_cost;\n'
                '    if(c_p->status==LB_PROCESS_WORK_LIMIT) goto work_failure;\n'
                '    if(c_p->status==LB_PROCESS_REFUSED) goto execution_refused;\n'
                '    if(is_non_value(result)) goto malformed_code;\n'
                '    *dst_ptr=result;')
            body=body.replace('GotoPF(next_pf);','if(FCALLS<=0) goto context_switch3; GotoPF(next_pf);')
        if name in ('is_binary_fx','is_binary_fy'):
            operand=('xb' if name=='is_binary_fx' else 'yb')+'((tmp_packed1>>BEAM_WIDE_SHIFT))'
            predicate=f'is_not_bitstring({operand}) || TAIL_BITS(bitstring_size({operand})) != 0'
            if body.count(predicate)!=1: raise RuntimeError('Changed native binary predicate boundary')
            body=body.replace(predicate,f'!lb_is_binary({operand})')
        body=body.replace('is_not_integer(', '!lb_is_integer(')
        body=fallible_equality(body)
        body=fallible_arithmetic(body)
        body=fallible_integer_division(body)
        body=fallible_writable_init(body)
        body=fallible_self(body)
        body=owned_node(body)
        body=owned_apply(body)
        body=owned_call_fun(body)
        body=owned_make_fun(body)
        body=owned_bif_call(body)
        body=owned_send(body)
        body=body.replace('erts_msgq_recv_marker_bind(',
            'if(!lb_work_domain_charge(c_p->entry_module->space->domain,LB_RECV_MARKERS)) goto work_failure; lb_receive_marker_bind(')
        body=body.replace('erts_msgq_recv_marker_clear(',
            'if(!lb_work_domain_charge(c_p->entry_module->space->domain,LB_RECV_MARKERS)) goto work_failure; lb_receive_marker_clear(')
        body=body.replace('erts_msgq_recv_marker_set_save(',
            'if(!lb_work_domain_charge(c_p->entry_module->space->domain,LB_RECV_MARKERS)) goto work_failure; lb_receive_marker_use(')
        body=body.replace('  Sb(I[1]) = erts_msgq_recv_marker_insert(c_p);',
            '  Eterm lb_marker;\n'
            '  if(!lb_work_domain_charge(c_p->entry_module->space->domain,LB_RECV_MARKERS)) goto work_failure;\n'
            '  LbProcessStatus lb_marker_status=lb_receive_marker_reserve(c_p,&lb_marker);\n'
            '  if(lb_marker_status==LB_PROCESS_NO_MEMORY) goto memory_failure;\n'
            '  if(lb_marker_status==LB_PROCESS_LIMIT) goto resource_limit;\n'
            '  if(lb_marker_status!=LB_PROCESS_READY) goto malformed_code;\n  *REG_TARGET_PTR(I[1])=lb_marker;')
        body=owned_receive_scan(body)
        body=owned_receive_remove(body)
        body=owned_receive_wait(body)
        body=owned_receive_timeout(body)
        body=fallible_binary_build(body)
        if 'i_bs_start_match3__execute:' in body:
            body=body.replace('erts_bs_start_match_3(', 'lb_bs_start_match(')
            body=body.replace('(I + (I[3]) + 0)', '(I + (Sint)I[3])').replace('I += I[3] + 0', 'I += (Sint)I[3]')
        body=body.replace('(I + (I[3]) + 0)', '(I + (Sint)I[3])').replace('I += I[3] + 0', 'I += (Sint)I[3]')
        body=body.replace('(I + (I[2]) + 1)', '(I + (Sint)I[2] + 1)').replace('I += I[2] + 1', 'I += (Sint)I[2] + 1')
        # No tracing/saved-call/lock instrumentation capability exists in this
        # serialized interpreter. Remove those paths, not their semantic peers.
        body=re.sub(r'^\s*DTRACE_\w+\([^;]+;', '', body, flags=re.M)
        body=re.sub(r'if \(ERTS_PROC_GET_SAVED_CALLS_BUF\(c_p\) && FCALLS > neg_o_reds\) \{\s*save_calls\(c_p, ep\);\s*\} else \{\s*goto context_switch;\s*\}', 'goto context_switch;', body)
        body=body.replace('erts_active_code_ix()', '0') # one immutable code generation; no hot load
        # BEAM stores signed relative offsets in unsigned instruction words.
        # Recover the signed value before C pointer arithmetic (also in asserts).
        body=body.replace('(I + (I[1]) + 0)', '(I + (Sint)I[1])')
        body=body.replace('I += I[1] + 0', 'I += (Sint)I[1]')
        body=body.replace('(I + (I[2]) + 0)', '(I + (Sint)I[2])')
        body=body.replace('I += I[2] + 0', 'I += (Sint)I[2]')
        body=body.replace('(I + (offset) + 0)', '(I + (Sint)offset)')
        body=body.replace('I += offset + 0', 'I += (Sint)offset')
        # Native packed label tables are two signed 32-bit displacements per
        # word. memcpy reads preserve the layout without aliasing BeamInstr[].
        body=re.sub(r'Sint32\* jump_tab = \(Sint32 \*\) (.*);', r'const BeamInstr* jump_tab = \1;',body)
        body=re.sub(r'jump_tab\[([^]]+)\]',r'lb_packed_label(jump_tab, \1)',body)
        body=body.replace('(I + (lbl) + 0)', '(I + (Sint)lbl)')
        body=body.replace('I += lbl + 0', 'I += (Sint)lbl')
        body=body.replace('(I + (call_dest) + 0)', '(I + (Sint)call_dest)')
        body=body.replace('I += call_dest + 0', 'I += (Sint)call_dest')
        body=body.replace('(E - HTOP) < (need + S_RESERVED)', '(Uint)(E - HTOP) < (need + S_RESERVED)')
        body=body.replace('PROCESS_MAIN_CHK_LOCKS(c_p);', '')
        body=body.replace('ERTS_VERIFY_UNUSED_TEMP_ALLOC(c_p);', '')
        body=re.sub(r'FCALLS -= erts_garbage_collect_nobump\(c_p, need, reg, (.*), FCALLS\);',
            r'if (!lb_process_collect_live(c_p, need, \1)) goto memory_failure;\n      FCALLS -= (Sint)c_p->last_gc_cost;', body)
        body=re.sub(r'if \(ERTS_PSFLG_EXITING & erts_atomic32_read_nob\(&c_p->state\)\) \{\s*goto context_switch3;\s*\};', '', body)
        if name in {'allocate_tt', 'allocate_heap_tIt', 'is_integer_allocate_fxtt'} or name.startswith('is_nonempty_list_allocate_'):
            # Host instruction-budget yields introduce safepoints between
            # allocate and init_yregs. Keep all physical stack slots valid roots;
            # the verifier still rejects logical reads before initialization.
            body=body.replace('*E = NIL;', '*E = NIL;\n    for (unsigned lb_y=1; lb_y<needed; ++lb_y) E[lb_y]=NIL;')
        if name.startswith(('put_list_','update_list_')):
            body=body.replace('{','{\n  if ((Uint)(E-HTOP)<2+S_RESERVED || c_p->heap_reserved<2) goto malformed_code;\n  c_p->heap_reserved-=2;',1)
        if name in {'put_tuple2_xI','put_tuple2_yI'}:
            body=body.replace('ASSERT(arity != 0);',
                'if (!arity || (Uint)(E-HTOP) < arity+1+S_RESERVED || c_p->heap_reserved < arity+1) goto malformed_code;\n'
                '  if(!lb_work_domain_charge(c_p->entry_module->space->domain,arity+lb_work_bulk_units(arity+1))) goto work_failure;\n'
                '  c_p->heap_reserved -= arity+1;')
        # Actual dynamic native loops: precharge initialized frames/closures,
        # and checkpoint the bounded linear-selector walk before each pair.
        body=re.sub(r'(unsigned needed = [^;]+;)',
            r'\1\n    if(!lb_work_domain_charge(c_p->entry_module->space->domain,lb_work_bulk_units(needed))) goto work_failure;',body)
        if 'select_val_lin__execute:' in body:
            body=body.replace('    for (;;) {',
                '    for (;;) {\n      if(!lb_work_domain_charge(c_p->entry_module->space->domain,2)) goto work_failure;')
        if name.startswith(('i_get_map_element_',)):
            body=owned_map_single_lookup(body)
        elif name=='i_get_map_elements_fsI':
            body=owned_map_bulk_lookup(body)
        elif name in ('new_map_dtI','i_new_small_map_lit_dtq'):
            body=owned_map_construction(body,name)
        elif name in ('is_map_fx','is_map_fy'):
            body=owned_map_test(name)
        elif name in ('update_map_assoc_cdtI','update_map_exact_xjdtI'):
            body=owned_map_update_group(body,name)
        dependency_code=re.sub(r'/\*.*?\*/|//[^\n]*', '', body, flags=re.S)
        if 'erts_' in dependency_code or 'ERTS_PROC_GET_' in dependency_code or 'save_calls(' in dependency_code:
            raise RuntimeError('Unadapted execution dependency in '+name)
        bodies.append(body)
    support='\n'.join('case op_'+name+': return 1;' for name in names)
    return '\n\n'.join(bodies)+'\n',support+'\n'
