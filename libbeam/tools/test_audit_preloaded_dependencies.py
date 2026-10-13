#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Leandro Ostera <leandro@ostera.io>
"""Run under the shared validation lock: this invokes trusted OTP for AST inspection."""
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / 'libbeam/tools/audit_preloaded_dependencies.escript'


class PreloadedAuditTests(unittest.TestCase):
    def inspect(self, source):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'audit_fixture.erl'
            path.write_text(source)
            return subprocess.run(['escript', str(SCRIPT), str(path)],
                                  capture_output=True, text=True, timeout=30)

    def test_ast_dependencies_without_execution(self):
        run = self.inspect('''-module(audit_fixture).
-export([f/2, stub/1, stub/2, dormant/0]).
f(F, X) when erlang:is_integer(X) ->
    local(X), peer:target(1, X), F(X), {fun local/1, fun peer:target/2}, self() ! X.
local(X) -> X.
stub(_) -> erlang:nif_error(undef).
stub(_, _) -> erlang:nif_error(undef).
dormant() -> erlang:halt(73).
''')
        self.assertEqual(run.returncode, 0, run.stderr)
        result = json.loads(run.stdout)
        functions = {(f['name'], f['arity']): f for f in result['functions']}
        f = functions[('f', 2)]
        self.assertEqual(f['dynamic_call_count'], 1)
        self.assertFalse(f['nif_stub'])
        self.assertEqual([(c['module'], c['name'], c['arity']) for c in f['guard_calls']],
                         [('erlang', 'is_integer', 1)])
        self.assertEqual({(r.get('module'), r['name'], r['arity']) for r in f['static_fun_references']},
                         {(None, 'local', 1), ('peer', 'target', 2)})
        self.assertEqual(f['operators'], ['!'])
        calls = {(c.get('module'), c['name'], c['arity']) for c in f['static_calls']}
        self.assertIn(('peer', 'target', 2), calls)
        self.assertIn((None, 'local', 1), calls)
        self.assertIn((None, 'self', 0), calls)
        self.assertTrue(functions[('stub', 1)]['nif_stub'])
        self.assertTrue(functions[('stub', 2)]['nif_stub'])
        self.assertFalse(functions[('dormant', 0)]['nif_stub'])

    def test_parse_errors_are_not_partial_success(self):
        run = self.inspect('-module(audit_fixture).\nf( -> invalid.\n')
        self.assertNotEqual(run.returncode, 0)
        self.assertIn('source_parse_errors', run.stderr)
        self.assertEqual(run.stdout, '')

    def test_lowering_records_body_dynamic_guard_predicate_and_send(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            inventory = root / 'inventory.json'
            inventory.write_text(json.dumps({'entries': [
                {'function': '+', 'arity': 1}, {'function': 'is_pid', 'arity': 1},
                {'function': 'halt', 'arity': 0}, {'function': 'send', 'arity': 2}]}))
            run = subprocess.run(['escript', str(ROOT / 'libbeam/tools/audit_bif_lowering.escript'),
                                  str(inventory), str(root / 'output')],
                                 capture_output=True, text=True, timeout=30)
            self.assertEqual(run.returncode, 0, run.stderr)
            rows = json.loads(run.stdout)['entries']
            self.assertEqual(sum(r['form'] == 'body' for r in rows), 4)
            self.assertEqual(sum(r['form'] == 'dynamic' for r in rows), 4)
            self.assertEqual(sum(r['form'] == 'guard' for r in rows), 2)
            self.assertEqual(sum(r['form'] == 'predicate_test' for r in rows), 1)
            send = next(r for r in rows if r['form'] == 'operator')
            self.assertIn('send', send['instructions'])
            self.assertTrue(all(r['instructions'] for r in rows))

    def test_all_requested_overloads_exist_in_pinned_ast(self):
        run = subprocess.run(['escript', str(SCRIPT), str(ROOT / 'beam/erts/preloaded/src/erlang.erl')],
                             capture_output=True, text=True, timeout=30)
        self.assertEqual(run.returncode, 0, run.stderr)
        result = json.loads(run.stdout)
        functions = {(f['name'], f['arity']): f for f in result['functions']}
        self.assertEqual(len(functions), len(result['functions']))
        from audit_bif_batch import requested_mfas
        for mfa in requested_mfas(ROOT / 'libbeam/core/otp/bif-batch-request.txt'):
            self.assertIn(mfa, functions)
        self.assertEqual(functions[('apply', 2)]['static_calls'][0]['name'], 'apply')
        self.assertFalse(functions[('apply', 2)]['nif_stub'])
        self.assertTrue(functions[('node', 0)]['nif_stub'])


if __name__ == '__main__':
    unittest.main()
