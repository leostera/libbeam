# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Leandro Ostera <leandro@ostera.io>
"""Project admitted *whole* OTP transformation cases, with fallible allocation.

No rules are reimplemented or dropped inside an admitted case. A case whose
helper dependencies have not been admitted returns an explicit unsupported error;
it must never be treated as TE_FAIL (which would silently skip an optimization).
"""
import re

PURE = {'distinct', 'equal', 'independent_moves', 'is_offset', 'negation_is_small',
        'never', 'succ', 'succ3', 'succ4', 'fixed_size_values', 'floats_or_bignums', 'mixed_types', 'use_jump_tab'}
OWNED = {'needs_nif_padding', 'is_heavy_bif', 'smp_already_locked', 'smp_mark_target_label'}
GENERATORS = {'allocate', 'allocate_heap', 'init_yregs', 'select_tuple_arity', 'increment', 'create_bin', 'call_fun2', 'make_fun3', 'is_function2', 'get', 'element', 'literal_timeout', 'literal_timeout_locked', 'select_val', 'bs_match', 'const_select_val', 'jump_tab', 'select_literals', 'split_values'}


def project(text):
    start = text.index('int erts_transform_engine(')
    end = text.index('const GenOpEntry gen_opc[]', start)
    engine = text[start:end].strip()
    split = list(re.finditer(r'^    case (\d+): /\* ([^\n]+) \*/', engine, re.M))
    finish = engine.rindex('    default: ASSERT(0); return TE_FAIL;')
    prefix = engine[:split[0].start()].replace('int erts_transform_engine(', 'static int lb_transform_engine(')
    cases = []
    used = set()
    record = {}
    for i, match in enumerate(split):
        body = engine[match.start():split[i+1].start() if i+1<len(split) else finish]
        dependencies = set(re.findall(r'Call (?:predicate|generator) (\w+)', body))
        missing = sorted(dependencies - PURE - OWNED - GENERATORS)
        record[match[2]] = {'opcode': int(match[1]), 'missing_helpers': missing}
        if missing:
            cases.append(body.splitlines()[0]+'\n      return TE_UNSUPPORTED;\n')
        else:
            used |= dependencies
            cases.append(body)
    result = prefix+'\n'.join(cases)+'    default: return TE_UNSUPPORTED;\n  }\n}\n'
    # The input generator predates fallible operation allocation. This is the
    # one mandatory behavioral adaptation, not a success-shaped helper shim.
    result = result.replace('BeamOp* new_instr = beamopallocator_new_op(&st->op_allocator);',
        'BeamOp* new_instr = lb_load_new_op(st);\n      if (!new_instr) return TE_NO_MEMORY;')
    result = result.replace('beamopallocator_free_op(&st->op_allocator, first);', 'lb_load_free_op(st, first);')
    result = re.sub(r'(BeamOp\* new_instr = (?:'+ '|'.join(sorted(GENERATORS))+r')\(.*?\);)',
        r'\1\n      if (st->program->error.status != LB_BEAM_OK) return TE_NO_MEMORY;', result)
    result, allocations = re.subn(r'erts_alloc\(ERTS_ALC_T_LOADER_TMP,\s*instr->arity \* sizeof\(BeamOpArg\)\)',
        'lb_palloc(st->program, instr->arity, sizeof(BeamOpArg))', result)
    result = result.replace('instr->a = lb_palloc(st->program, instr->arity, sizeof(BeamOpArg));',
        'instr->a = lb_palloc(st->program, instr->arity, sizeof(BeamOpArg));\n    if (!instr->a) return TE_NO_MEMORY;')
    result = result.replace('sys_memcpy', 'memcpy').replace('ASSERT(', 'TR_REQUIRE(')
    # The retained Engine catalog is immutable. Upstream locals only compare
    # descriptor identities; preserve those comparisons without a writable alias.
    result = result.replace('BifEntry *entry = st->bif_imports[i];',
                            'const BifEntry *entry = st->bif_imports[i];')
    # File operands are 64-bit here. Never truncate a malformed import index to
    # upstream's int local before checking it against the owned import directory.
    result = re.sub(r'int i = (instr->a\[\d+\])\.val;',
        r'Uint i; TR_REQUIRE(\1.type == TAG_u && \1.val >= 0); i = (Uint)\1.val;', result)
    if 'erts_alloc' in result or 'beamopallocator_' in result:
        raise RuntimeError('Unadapted transform allocation')
    helpers = []
    for name in sorted(PURE & used):
        match = re.search(r'^static int '+name+r'\(.*?^\}', text, re.M|re.S)
        if not match: raise RuntimeError('Missing pure helper '+name)
        helper=match[0]
        if name=='fixed_size_values':
            helper=helper.replace('beamfile_get_literal(&S->beam, Rest[i].val)', 'load_literal(S, Rest[i].val)')
            helper=helper.replace('is_float(', 'lb_transform_is_float(')
        if name=='use_jump_tab':
            helper=helper.replace('return max - min <= Size.val;', 'return (Uint)max - (Uint)min <= (Uint)Size.val;')
        helpers.append(helper)
    for name in sorted(GENERATORS & used):
        match = re.search(r'^static BeamOp\* '+name+r'\(.*?^\}', text, re.M|re.S)
        if not match: raise RuntimeError('Missing generator '+name)
        body = re.sub(r'(\w+) = beamopallocator_new_op\(&\(S\)->op_allocator\);',
            r'\1 = lb_load_new_op(S);\n  if (!\1) return NULL;', match[0])
        if name == 'create_bin':
            # The complete native generator is retained. Structural validation
            # runs before entry; the upstream assertion's assignment is a typo,
            # not permission to rewrite a malformed source operand in Debug.
            body, assertions = re.subn(r'ASSERT\(src\[0\]\.type = TAG_a\);',
                                       'ASSERT(src[0].type == TAG_a);', body)
            body, arrays = re.subn(r'op->a = erts_alloc\(ERTS_ALC_T_LOADER_TMP, op->arity \* sizeof\(BeamOpArg\)\);',
                'op->a = lb_palloc(S->program, op->arity, sizeof(BeamOpArg));\n  if (!op->a) return NULL;', body)
            body, literals = re.subn(r'beamfile_get_literal\(&S->beam, Flags.val\)',
                                    'load_literal(S, Flags.val)', body)
            body = re.sub(r'\bBSC_(\w+)', r'LB_BSC_\1', body)
            if (assertions, arrays, literals) != (1, 1, 1):
                raise RuntimeError('Unreviewed create_bin generator boundary')
        if name == 'get':
            old='op->a[1].val = (BeamInstr) erts_pd_make_hx(key_term);'
            if body.count(old)!=1: raise RuntimeError('Changed dictionary hash boundary')
            body=body.replace(old,'if (!dictionary_hash(S,key_term,&op->a[1].val)) return NULL;')
        if name == 'is_function2':
            old='if (Arity.val > MAX_ARG)'
            if body.count(old)!=1: raise RuntimeError('Changed native unsigned arity boundary')
            body=body.replace(old,'if ((Uint)Arity.val > MAX_ARG)')
        if name == 'make_fun3':
            for old,new,expected in (
                ('S->beam.lambdas.count','S->program->lambda_count',1),
                ('S->beam.lambdas.entries','S->program->lambdas',1),
                ('beamfile_add_literal(&S->beam, make_fun((Eterm*)funp), 0)',
                 'load_fun_literal(S, (size_t)Idx.val, make_fun((Eterm*)funp))',1),
                ('erts_alloc(ERTS_ALC_T_LOADER_TMP, op->arity * sizeof(BeamOpArg))',
                 'lb_palloc(S->program, op->arity, sizeof(BeamOpArg))',1)):
                if body.count(old)!=expected: raise RuntimeError('Unreviewed fun generator boundary '+old)
                body=body.replace(old,new)
            body=body.replace('op->a = lb_palloc(S->program, op->arity, sizeof(BeamOpArg));',
                'op->a = lb_palloc(S->program, op->arity, sizeof(BeamOpArg));\n  if (!op->a) return NULL;')
        if name in {'const_select_val','jump_tab','select_literals','split_values'}:
            body=body.replace('beamfile_get_literal(&S->beam, Src.val)', 'load_literal(S, Src.val)')
            body=body.replace('beamfile_get_literal(&S->beam, Rest[i].val)', 'load_literal(S, Rest[i].val)')
            body=body.replace('eq(term, expr)', 'lb_load_literal_equal(S,term,expr)')
            body=body.replace('beam_load_new_label(S)', 'lb_load_new_label(S)')
            body=re.sub(r'(op(?:1|2)?->a) = erts_alloc\(ERTS_ALC_T_LOADER_TMP, ([^;]+)\);',
                r'\1 = lb_palloc(S->program, 1, \2);\n  if (!\1) return NULL;',body)
        if name == 'bs_match':
            body=body.replace('beamfile_get_literal(&S->beam, Flags.val)', 'load_literal(S, Flags.val)')
            body=body.replace('erts_extracted_bitstring_size(size)', 'lb_match_extracted_words(size)')
            body,count=re.subn(r'erts_exit\(ERTS_ERROR_EXIT, "loading bs_match in %T:%T/%d: no instructions loaded",\s*S->module, S->function, S->arity\);',
                'S->program->error.status=LB_BEAM_BAD_FORMAT; return NULL;',body)
            if count!=1: raise RuntimeError('Unreviewed bs_match fatal boundary')
        if name == 'select_val':
            body=body.replace('int arity = Size.val + 3;',
                'int arity;\n  if (Size.type!=TAG_u || Size.val<2 || Size.val%2 || Size.val>MAX_REG*2) { S->program->error.status=LB_BEAM_LIMIT; return NULL; }\n  arity = Size.val + 3;')
            body=body.replace('erts_alloc(ERTS_ALC_T_LOADER_TMP, op->arity * sizeof(BeamOpArg))',
                'lb_palloc(S->program, op->arity, sizeof(BeamOpArg))')
            body=body.replace('op->a[0] = Src;', 'if (!op->a) return NULL;\n    op->a[0] = Src;')
            body=body.replace('(BeamOpArg *) erts_alloc(ERTS_ALC_T_LOADER_TMP, sizeof(BeamOpArg)*(arity-2*align))',
                'lb_palloc(S->program, arity-2*align, sizeof(BeamOpArg))')
            body=body.replace('  for (i = 3; i < arity - 2*align; i++)',
                '  if (!tmp) return NULL;\n  for (i = 3; i < arity - 2*align; i++)')
            body=body.replace('erts_free(ERTS_ALC_T_LOADER_TMP, (void *) tmp);',
                '/* Owned temporary preparation arena. */')
        if name == 'select_tuple_arity':
            body = body.replace('erts_alloc(ERTS_ALC_T_LOADER_TMP, op->arity * sizeof(BeamOpArg))',
                                'lb_palloc(S->program, op->arity, sizeof(BeamOpArg))')
            body = body.replace('op->next = NULL;\n    op->a[0]', 'if (!op->a) return NULL;\n    op->next = NULL;\n    op->a[0]')
            body = body.replace('op->next = NULL;\n  op->a[0]', 'if (!op->a) return NULL;\n  op->next = NULL;\n  op->a[0]')
            body = body.replace('erts_alloc(ERTS_ALC_T_LOADER_TMP, sizeof(BeamOpArg)*(arity-2*align))',
                                'lb_palloc(S->program, arity-2*align, sizeof(BeamOpArg))')
            body = body.replace('  for (i = 3; i < arity - 2*align; i+=2)',
                                '  if (!tmp) return NULL;\n  for (i = 3; i < arity - 2*align; i+=2)')
            body = body.replace('erts_free(ERTS_ALC_T_LOADER_TMP, (void *) tmp);',
                                '/* Temporary argument arrays belong to the preparation arena. */')
        if 'beamopallocator_' in body or 'erts_alloc' in body or 'erts_free' in body: raise RuntimeError('Unadapted generator allocation '+name)
        helpers.append(body)
    return '\n'.join(helpers)+'\n'+result, record
