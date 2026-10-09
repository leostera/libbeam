# %CopyrightBegin%
#
# SPDX-License-Identifier: Apache-2.0
#
# Copyright 2026 Leandro Ostera <leandro@ostera.io>
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# %CopyrightEnd%

import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("realm_inventory", Path(__file__).with_name("realm-inventory.py"))
inv = importlib.util.module_from_spec(spec)
spec.loader.exec_module(inv)


class InventoryTests(unittest.TestCase):
    def test_aliases_and_quoted_atoms(self):
        rows, unused = inv.dispatch_rows("# comment\nubif erlang:'and'/2\nbif binary:part/2 binary_binary_part_2\nhbif erts_internal:foo/1\n", "")
        symbols = {row[0]: row[2] for row in rows}
        self.assertEqual(symbols, {"erlang:'and'/2": "and_2", "binary:part/2": "binary_binary_part_2", "erts_internal:foo/1": "erts_internal_foo_1"})
        self.assertEqual(unused, [])

    def test_dirty_modes_and_ubif_override(self):
        rows, _ = inv.dispatch_rows("bif erlang:a/1\nbif erlang:b/1\nubif erlang:c/1\n",
                                    "dirty-cpu erlang:a/1\ndirty-io-test erlang:b/1\ndirty-cpu-test erlang:c/1\n")
        self.assertEqual([r[3:5] for r in rows], [["dirty_cpu", "dirty_cpu"],
                                                ["normal", "dirty_io"], ["normal", "normal"]])

    def test_unmatched_annotations_remain_visible(self):
        rows, unused = inv.dispatch_rows("bif erlang:a/1\n", "dirty-cpu-test erlang:removed/1\n")
        self.assertEqual(len(rows), 1)
        self.assertEqual(unused, [("erlang:removed/1", "dirty-cpu-test", inv.DIRTY + ":1")])

    def test_reject_unsupported_and_duplicate_input(self):
        for text in ("new-bif erlang:a/1", "bif malformed", "bif erlang:a/1 alias extra",
                     "bif erlang:a/1\nbif erlang:a/1", "bif erlang:'+'/2"):
            with self.subTest(text=text), self.assertRaises(ValueError):
                inv.dispatch_rows(text, "")
        with self.assertRaises(ValueError):
            inv.dispatch_rows("bif erlang:a/1", "dirty-cpu erlang:a/1\ndirty-io erlang:a/1")

    def test_candidate_definitions_and_source_lines(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            path = root / "erts/emulator/beam/example.c"
            path.parent.mkdir(parents=True)
            path.write_text("\nEterm a_1(BIF_ALIST_1) { return 0; }\nBIF_RETTYPE b_2(BIF_ALIST_2) /* args */\n{ return 0; }\n")
            self.assertEqual(inv.definitions(root), {"a_1": ["erts/emulator/beam/example.c:2"],
                                                     "b_2": ["erts/emulator/beam/example.c:3"]})

    def test_nif_signature_order_and_context_hints(self):
        text = ("ERL_NIF_API_FUNC_DECL(ErlNifEnv*,enif_alloc_env,(void));\n"
                "ERL_NIF_API_FUNC_DECL(int,enif_send,(ErlNifEnv *env, void (*f)(int,int)));\n")
        result = inv.nif_declarations(text, "test.h")
        self.assertIn("1\tenif_alloc_env\tErlNifEnv*\t(void)\tabsent\ttest.h:1", result)
        self.assertIn("2\tenif_send\tint\t(ErlNifEnv *env, void (*f)(int,int))\tpresent_not_authority", result)
        self.assertNotEqual(result, inv.nif_declarations(text.replace("int,int", "int,long"), "test.h"))

    def test_nif_malformed_empty_and_duplicate_fail(self):
        declaration = "ERL_NIF_API_FUNC_DECL(int,enif_test,(void));\n"
        for text in ("", declaration * 2, "ERL_NIF_API_FUNC_DECL(int,enif_test,((void));"):
            with self.subTest(text=text), self.assertRaises(ValueError):
                inv.nif_declarations(text, "test.h")

    def test_inventory_drift_and_unresolved_candidates(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / inv.BIFS).parent.mkdir(parents=True)
            (root / inv.BIFS).write_text("bif erlang:a/1\n")
            (root / inv.DIRTY).write_text("")
            before = inv.inventory(root)
            self.assertIn("unresolved\tUNREVIEWED", before)
            for changed in ("bif erlang:a/1 alternate_1\n", "ubif erlang:a/1\n", "bif erlang:a/1\nbif erlang:b/1\n"):
                (root / inv.BIFS).write_text(changed)
                self.assertNotEqual(before, inv.inventory(root))
            (root / inv.BIFS).write_text("bif erlang:a/1\n")
            (root / inv.DIRTY).write_text("dirty-cpu erlang:a/1\n")
            self.assertNotEqual(before, inv.inventory(root))


if __name__ == "__main__":
    unittest.main()
