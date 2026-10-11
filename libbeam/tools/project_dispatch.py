# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Leandro Ostera <leandro@ostera.io>
"""Exact generated instruction bodies plus reviewed ownership-boundary edits.

This is an explicit single interpreter capability profile, not an alternate
bytecode, a fixture opcode parser, or dummy handlers for the remaining cases.
"""
import re

NAMES = ('move_cr move_cx move_xr move_xx move_rx move_nx move_x1_c move_x2_c move_shift_cxx '
         'move_return_c move_return_n move_return_x return put_tuple2_xI test_heap_It '
         'i_call_ext_only_e i_move_call_ext_only_ec jump_f badmatch_x case_end_x '
         'i_allocate_zero_tt i_allocate_heap_zero_tIt allocate_tt allocate_heap_tIt '
         'i_call_ext_e i_move_call_ext_ce deallocate_Q i_is_eq_exact_immed_frc '
         'i_is_eq_exact_immed_fxc move_jump_fcr move_jump_fcx '
         'move_xy move_yr move_yx move_yy move_ry move_cy swap_xx swap_yx swap_yy '
         'i_call_f i_call_last_fQ i_call_only_f '
         'move_call_cf move_call_xf move_call_yf move_call_last_cfQ move_call_last_xfQ move_call_last_yfQ '
         'move_call_only_cf move_call_only_xf '
         'move_deallocate_return_cQ move_deallocate_return_nQ move_deallocate_return_xQ move_deallocate_return_yQ '
         'i_call_ext_last_eQ i_move_call_ext_last_eQc '
         'i_init_y i_init2_yy i_init3_yyy i_init_seq3_y i_init_seq4_y i_init_seq5_y '
         'i_is_eq_exact_immed_fyc i_is_eq_exact_literal_fxc i_is_eq_exact_literal_fyc '
         'i_is_ne_exact_immed_fxc i_is_ne_exact_immed_fyc i_is_ne_exact_literal_fxc i_is_ne_exact_literal_fyc '
         'is_eq_exact_fxx is_eq_exact_fxy is_eq_exact_fyy is_ne_exact_fSS is_nil_fx is_nil_fy').split()


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


def project(source):
    bodies=[]
    shared=group(source, 'deallocate_return0')
    names=NAMES+re.findall(r'OpCase\((\w+)\)',shared)
    selected=[(name,case(source,name)) for name in NAMES]+[('deallocation group',shared)]
    for name, body in selected:
        body=fallible_equality(body)
        # No tracing/saved-call/lock instrumentation capability exists in this
        # serialized interpreter. Remove those paths, not their semantic peers.
        body=re.sub(r'^\s*DTRACE_\w+\([^;]+;', '', body, flags=re.M)
        body=re.sub(r'if \(ERTS_PROC_GET_SAVED_CALLS_BUF\(c_p\) && FCALLS > neg_o_reds\) \{\s*save_calls\(c_p, ep\);\s*\} else \{\s*goto context_switch;\s*\}', 'goto context_switch;', body)
        body=body.replace('erts_active_code_ix()', '0') # one immutable code generation; no hot load
        # BEAM stores signed relative offsets in unsigned instruction words.
        # Recover the signed value before C pointer arithmetic (also in asserts).
        body=body.replace('(I + (I[1]) + 0)', '(I + (Sint)I[1])')
        body=body.replace('I += I[1] + 0', 'I += (Sint)I[1]')
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
        if name in {'allocate_tt', 'allocate_heap_tIt'}:
            # Host instruction-budget yields introduce safepoints between
            # allocate and init_yregs. Keep all physical stack slots valid roots;
            # the verifier still rejects logical reads before initialization.
            body=body.replace('*E = NIL;', '*E = NIL;\n    for (unsigned lb_y=1; lb_y<needed; ++lb_y) E[lb_y]=NIL;')
        if name=='put_tuple2_xI':
            body=body.replace('ASSERT(arity != 0);',
                'if (!arity || (Uint)(E-HTOP) < arity+1+S_RESERVED || c_p->heap_reserved < arity+1) goto malformed_code;\n  c_p->heap_reserved -= arity+1;')
        if 'erts_' in body or 'ERTS_PROC_GET_' in body or 'save_calls(' in body:
            raise RuntimeError('Unadapted execution dependency in '+name)
        bodies.append(body)
    support='\n'.join('case op_'+name+': return 1;' for name in names)
    return '\n\n'.join(bodies)+'\n',support+'\n'
