# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Leandro Ostera <leandro@ostera.io>
"""Project admitted *whole* OTP transformation cases, with fallible allocation.

No rules are reimplemented or dropped inside an admitted case. A case whose
helper dependencies have not been admitted returns an explicit unsupported error;
it must never be treated as TE_FAIL (which would silently skip an optimization).
"""
import re

PURE = {'distinct', 'equal', 'independent_moves', 'is_offset', 'negation_is_small',
        'never', 'succ', 'succ3', 'succ4'}
OWNED = {'needs_nif_padding', 'is_heavy_bif', 'smp_already_locked', 'smp_mark_target_label'}
GENERATORS = {'allocate', 'allocate_heap', 'init_yregs'}


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
        helpers.append(match[0])
    for name in sorted(GENERATORS & used):
        match = re.search(r'^static BeamOp\* '+name+r'\(.*?^\}', text, re.M|re.S)
        if not match: raise RuntimeError('Missing generator '+name)
        body = re.sub(r'(\w+) = beamopallocator_new_op\(&\(S\)->op_allocator\);',
            r'\1 = lb_load_new_op(S);\n  if (!\1) return NULL;', match[0])
        if 'beamopallocator_' in body or 'erts_alloc' in body: raise RuntimeError('Unadapted generator allocation '+name)
        helpers.append(body)
    return '\n'.join(helpers)+'\n'+result, record
