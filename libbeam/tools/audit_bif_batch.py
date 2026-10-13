#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Leandro Ostera <leandro@ostera.io>
"""Inventory source facts only. This is not an admission or completeness oracle."""
import argparse
import hashlib
import json
from pathlib import Path
import re


def parse_mfa(text, location):
    match = re.fullmatch(r"([^\s]+)/([0-9]+)", text)
    if not match:
        raise ValueError(f'{location}: malformed MFA')
    name, arity_text = match.groups()
    if "'" in name:
        if not (name.startswith("'") and name.endswith("'") and name.count("'") == 2):
            raise ValueError(f'{location}: malformed quoted name')
        name = name[1:-1]
    arity = int(arity_text)
    if not name or arity > 255:
        raise ValueError(f'{location}: invalid MFA')
    return name, arity


def requested_mfas(path):
    result = []
    seen = set()
    for line_number, line in enumerate(path.read_text().splitlines(), 1):
        line = line.strip()
        if not line or line.startswith('#'):
            continue
        name, arity = parse_mfa(line, f'{path}:{line_number}')
        if (name, arity) in seen:
            raise ValueError(f'{path}:{line_number}: duplicate MFA')
        seen.add((name, arity))
        result.append((name, arity))
    if not result:
        raise ValueError('Empty request inventory')
    return result


def contracts_for_mfas(path, expected):
    document = json.loads(path.read_text())
    contracts = {}
    groups = set()
    for group in document['groups']:
        if group['id'] in groups or group['classification'] not in {'local', 'query', 'denial', 'exception'}:
            raise ValueError('Duplicate group or invalid policy classification')
        groups.add(group['id'])
        if not group['contract'].strip():
            raise ValueError('Empty implementation contract')
        for mfa in group['mfas']:
            key = parse_mfa(mfa, f'{path}:{group["id"]}')
            if key not in expected or key in contracts:
                raise ValueError(f'Unexpected or duplicate contract MFA: {mfa}')
            contracts[key] = {field: group[field] for field in ('id', 'classification', 'contract')}
    if contracts.keys() != expected:
        raise ValueError(f'Missing implementation contracts: {expected - contracts.keys()}')
    return document['common'], contracts


def wrapper_plans(path, expected):
    plans = {}
    allowed = {'beam', 'intrinsic', 'owned-query-adapter', 'local-native-adapter', 'denial', 'compatibility-exception'}
    for group in json.loads(path.read_text())['plans']:
        if group['mode'] not in allowed or not group['reason'].strip():
            raise ValueError('Invalid wrapper implementation plan')
        for mfa in group['mfas']:
            key = parse_mfa(mfa, str(path))
            if key not in expected or key in plans:
                raise ValueError(f'Unexpected or duplicate wrapper plan: {mfa}')
            plans[key] = {field: group[field] for field in ('mode', 'reason')}
    if plans.keys() != expected:
        raise ValueError(f'Missing wrapper plans: {expected - plans.keys()}')
    return plans


def audit(root, request, contracts_path=None, wrappers_path=None):
    table_path = root / 'beam/erts/emulator/beam/bif.tab'
    table = {}
    for line_number, line in enumerate(table_path.read_text().splitlines(), 1):
        match = re.fullmatch(r"(bif|ubif|hbif)\s+erlang:(\S+)/(\d+)(?:\s+(\w+))?\s*", line)
        if not match and re.match(r'(?:bif|ubif|hbif)\s+erlang:', line):
            raise ValueError(f'{table_path}:{line_number}: unparsed erlang declaration')
        if match:
            kind, name, arity, symbol = match.groups()
            name = name.strip("'")
            key = name, int(arity)
            if key in table:
                raise ValueError(f'Duplicate pinned BIF declaration: {key}')
            table[key] = {'kind': kind, 'symbol': symbol or f'{name}_{arity}', 'line': line_number}
    definitions = {}
    for path in sorted((root / 'beam/erts/emulator').rglob('*.c')):
        data = path.read_bytes()
        text = data.decode(errors='replace')
        digest = hashlib.sha256(data).hexdigest()
        for match in re.finditer(r'\b(?:BIF_RETTYPE|Eterm)\s+(\w+)\s*\(', text):
            definitions.setdefault(match[1], []).append({
                'path': str(path.relative_to(root)),
                'line': text.count('\n', 0, match.start()) + 1,
                'sha256': digest,
            })
    engine = (root / 'libbeam/core/engine.c').read_text()
    denials_path = root / 'libbeam/core/capability_denials.def'
    has_denials = denials_path.is_file() and '#include "capability_denials.def"' in engine
    registered = engine + ('\n' + denials_path.read_text() if has_denials else '')
    preloaded_path = root / 'beam/erts/preloaded/src/erlang.erl'
    preloaded = preloaded_path.read_text()
    requested = requested_mfas(request)
    common, contracts = contracts_for_mfas(contracts_path, set(requested)) if contracts_path else ({}, {})
    plans = wrapper_plans(wrappers_path, set(requested) - table.keys()) if wrappers_path else {}
    rows = []
    for name, arity in requested:
        native = table.get((name, arity))
        row = {'module': 'erlang', 'function': name, 'arity': arity,
               'policy_status': 'unreviewed', 'implementation_evidence': []}
        if contracts_path:
            row['policy_status'] = 'contract-assigned; implementation-review-pending'
            row['proposed_contract'] = contracts[(name, arity)]
        if wrappers_path:
            row['proposed_implementation'] = (
                {'mode': 'native-c', 'reason': 'Pinned native C body or explicit C capability boundary; see contract.'}
                if native else plans[(name, arity)])
        if native:
            row['native_declaration'] = native
            row['native_source_candidates'] = definitions.get(native['symbol'], [])
            row['native_id_in_engine_catalog'] = bool(re.search(
                r'(?:\{BIF_|LB_DENIAL\(BIF_)' + re.escape(native['symbol']) + r'\s*,', registered))
        else:
            row['native_declaration'] = None
            row['requires_wrapper_or_intrinsic_review'] = True
        # Candidate lines deliberately do NOT assert an arity or executable body.
        # A head can belong to another overload or to an intrinsic/NIF stub.
        pattern = r"(?m)^(?:" + re.escape(name) + "|'" + re.escape(name) + "')\\("
        row['preloaded_head_candidate_lines'] = [
            preloaded.count('\n', 0, match.start()) + 1
            for match in re.finditer(pattern, preloaded)]
        rows.append(row)
    sources = [table_path, preloaded_path, root / 'libbeam/core/engine.c', request]
    if has_denials:
        sources.append(denials_path)
    if contracts_path:
        sources.append(contracts_path)
    if wrappers_path:
        sources.append(wrappers_path)
    return {'scope': 'source facts and proposed contracts; not capability admission or test coverage',
            'common_proposed_contract': common,
            'requested_count': len(rows),
            'direct_native_declarations': sum(row['native_declaration'] is not None for row in rows),
            'source_hashes': {str(path.relative_to(root)): hashlib.sha256(path.read_bytes()).hexdigest()
                              for path in sources},
            'entries': rows}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[2])
    args = parser.parse_args()
    root = args.root.resolve()
    print(json.dumps(audit(root, root / 'libbeam/core/otp/bif-batch-request.txt',
                           root / 'libbeam/core/otp/bif-batch-contracts.json',
                           root / 'libbeam/core/otp/bif-batch-wrapper-plan.json'), indent=2))


if __name__ == '__main__':
    main()
