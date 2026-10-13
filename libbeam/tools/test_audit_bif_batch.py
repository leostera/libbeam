#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Leandro Ostera <leandro@ostera.io>
import hashlib
import json
from pathlib import Path
import tempfile
import unittest

from audit_bif_batch import audit, requested_mfas, contracts_for_mfas, wrapper_plans


class BifInventoryTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.request = self.root / 'request.txt'

    def put(self, name, text):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)
        return path

    def test_exact_names_and_arities(self):
        self.request.write_text("# requirement list\n\n+/1\n'/'/2\nfoo/0\nfoo/255\n")
        self.assertEqual(requested_mfas(self.request), [('+', 1), ('/', 2), ('foo', 0), ('foo', 255)])

    def test_invalid_and_duplicate_requests(self):
        for text in ('', '# empty\n', 'foo/-1', 'foo/256', 'foo', "'foo/1",
                     "foo'/1", "''/1", "foo/1\n'foo'/1", 'foo/1\nfoo/1', 'foo /1'):
            with self.subTest(text=text):
                self.request.write_text(text)
                with self.assertRaises(ValueError):
                    requested_mfas(self.request)

    def fixture(self):
        self.request.write_text("+/1\ntrace_info/2\napply/2\nunknown/0\n")
        self.put('beam/erts/emulator/beam/bif.tab', "ubif erlang:'+'/1 splus_1\nbif erlang:trace_info/2\n")
        self.put('beam/erts/emulator/beam/example.c',
                 'BIF_RETTYPE splus_1(BIF_ALIST_1) { return 0; }\n'
                 'Eterm\ntrace_info_2(BIF_ALIST_2) { return 0; }\n')
        self.put('beam/erts/preloaded/src/erlang.erl',
                 'apply(Fun, Args) -> erlang:apply(Fun, Args).\n'
                 'apply(Module, Name, Args) -> erlang:apply(Module, Name, Args).\n')
        self.put('libbeam/core/engine.c', '{BIF_splus_1, {am_erlang, am_Plus, 1, implementation, BIF_KIND_GUARD}}\n')

    def test_candidates_do_not_claim_semantics_or_coverage(self):
        self.fixture()
        result = audit(self.root, self.request)
        self.assertEqual(result['requested_count'], 4)
        self.assertEqual(result['direct_native_declarations'], 2)
        plus, trace, apply, unknown = result['entries']
        self.assertEqual(plus['native_declaration']['symbol'], 'splus_1')
        self.assertTrue(plus['native_id_in_engine_catalog'])
        self.assertFalse(trace['native_id_in_engine_catalog'])
        self.assertEqual(trace['native_source_candidates'][0]['line'], 2)
        self.assertIsNone(apply['native_declaration'])
        self.assertEqual(apply['preloaded_head_candidate_lines'], [1, 2])
        self.assertEqual(unknown['preloaded_head_candidate_lines'], [])
        for row in result['entries']:
            self.assertEqual(row['policy_status'], 'unreviewed')
            self.assertEqual(row['implementation_evidence'], [])
        for name, digest in result['source_hashes'].items():
            self.assertEqual(digest, hashlib.sha256((self.root / name).read_bytes()).hexdigest())
        self.assertEqual(result, audit(self.root, self.request))

    def test_duplicate_native_declarations_fail(self):
        self.fixture()
        self.put('beam/erts/emulator/beam/bif.tab', 'bif erlang:trace_info/2\nbif erlang:trace_info/2\n')
        with self.assertRaises(ValueError):
            audit(self.root, self.request)

    def test_unparsed_native_declarations_fail(self):
        self.fixture()
        self.put('beam/erts/emulator/beam/bif.tab', 'bif erlang:trace_info/2 unexpected extra\n')
        with self.assertRaises(ValueError):
            audit(self.root, self.request)

    def test_contract_coverage_is_exact(self):
        path = self.root / 'contracts.json'
        document = {'common': {}, 'groups': [
            {'id': 'numeric', 'classification': 'local', 'contract': 'real arithmetic', 'mfas': ['+/1']}]}
        path.write_text(json.dumps(document))
        _, contracts = contracts_for_mfas(path, {('+', 1)})
        self.assertEqual(contracts[('+', 1)]['classification'], 'local')
        with self.assertRaises(ValueError):
            contracts_for_mfas(path, {('+', 1), ('foo', 0)})
        for mfas in (['+/1', "'+'/1"], ['unknown/0'], ["'+/1"]):
            with self.subTest(mfas=mfas):
                document['groups'][0]['mfas'] = mfas
                path.write_text(json.dumps(document))
                with self.assertRaises(ValueError):
                    contracts_for_mfas(path, {('+', 1)})
        document['groups'][0]['mfas'] = ['+/1']
        document['groups'][0]['classification'] = 'successful-placeholder'
        path.write_text(json.dumps(document))
        with self.assertRaises(ValueError):
            contracts_for_mfas(path, {('+', 1)})

    def test_wrapper_plan_coverage_is_exact(self):
        path = self.root / 'wrappers.json'
        document = {'plans': [{'mode': 'beam', 'reason': 'preserve complete function', 'mfas': ['alias/0']}]}
        path.write_text(json.dumps(document))
        self.assertEqual(wrapper_plans(path, {('alias', 0)})[('alias', 0)]['mode'], 'beam')
        with self.assertRaises(ValueError):
            wrapper_plans(path, {('alias', 0), ('other', 1)})
        document['plans'][0]['mfas'].append('alias/0')
        path.write_text(json.dumps(document))
        with self.assertRaises(ValueError):
            wrapper_plans(path, {('alias', 0)})

    def test_denial_declarations_match_policy(self):
        import re
        root = Path(__file__).resolve().parents[2]
        expected = set(requested_mfas(root / 'libbeam/core/otp/bif-batch-request.txt'))
        _, contracts = contracts_for_mfas(root / 'libbeam/core/otp/bif-batch-contracts.json', expected)
        expected = {mfa for mfa, contract in contracts.items() if contract['classification'] == 'denial'}
        declarations = re.findall(r'^LB_DENIAL\(([^,]+), (\w+), (\d+), (\w+)\)$',
                                 (root / 'libbeam/core/capability_denials.def').read_text(), re.M)
        self.assertEqual({(name, int(arity)) for _, name, arity, _ in declarations}, expected)
        self.assertEqual(len(declarations), len(expected))
        self.assertEqual(len({identity for identity, _, _, _ in declarations}), len(expected))
        self.assertEqual(next(category for _, name, _, category in declarations if name == 'load_nif'), 'NATIVE_LIBRARY')

    def test_pinned_inventory(self):
        root = Path(__file__).resolve().parents[2]
        result = audit(root, root / 'libbeam/core/otp/bif-batch-request.txt',
                       root / 'libbeam/core/otp/bif-batch-contracts.json',
                       root / 'libbeam/core/otp/bif-batch-wrapper-plan.json')
        self.assertEqual(result['requested_count'], 185)
        self.assertEqual(result['direct_native_declarations'], 135)
        for row in result['entries']:
            self.assertIn(row['proposed_contract']['classification'], {'local', 'query', 'denial', 'exception'})
            self.assertEqual(row['policy_status'], 'contract-assigned; implementation-review-pending')
            self.assertIn('mode', row['proposed_implementation'])
            if row['native_declaration']:
                self.assertTrue(row['native_source_candidates'], row)
            else:
                self.assertTrue(row['preloaded_head_candidate_lines'], row)


if __name__ == '__main__':
    unittest.main()
