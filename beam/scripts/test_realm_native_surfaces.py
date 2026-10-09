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
import subprocess
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('native_surfaces', Path(__file__).with_name('realm-native-surfaces.py'))
native = importlib.util.module_from_spec(spec)
spec.loader.exec_module(native)
SAMPLE = '''static ErlNifFunc api[] = {
#if FOO
 {"read", 1, read_nif, ERL_NIF_DIRTY_JOB_IO_BOUND},
#else
 {"read", 1, unavailable_nif},
#endif
};
ERL_NIF_INIT(example,api,load,NULL,upgrade,unload)
'''
DRIVER = 'typedef struct erl_drv_entry { int (*init)(void); } ErlDrvEntry;'


class NativeSurfacesTests(unittest.TestCase):
    def test_conditional_exports_and_callbacks(self):
        rows = native.nif_rows(SAMPLE, 'example.c')
        self.assertEqual(len(rows), 6)
        self.assertEqual(rows[-2][:6], ['nif_export', 'example', 'read', 'read_nif', '1', 'ERL_NIF_DIRTY_JOB_IO_BOUND'])
        self.assertEqual(rows[-1][3:6], ['unavailable_nif', '1', '0'])
        self.assertEqual(rows[1][2:4], ['reload', 'NULL'])

    def test_comments_preserve_locations_and_strings(self):
        text = '// ERL_NIF_INIT(fake,x,x,x,x,x)\n/* comment */\n' + SAMPLE
        rows = native.nif_rows(text, 'example.c')
        self.assertEqual(len(rows), 6)
        self.assertEqual(int(rows[-2][-1]), 5)
        self.assertEqual(native.uncomment('"/* not comment */"'), '"/* not comment */"')

    def test_registration_spelling_in_literals_is_not_a_declaration(self):
        text = 'const char *s = "ERL_NIF_INIT(fake,api,a,b,c,d)";\n' + SAMPLE
        self.assertEqual(native.nif_rows(text, 'example.c')[-1][1], 'example')
        self.assertEqual(len(native.nif_rows(text, 'example.c')), 6)

    def test_unknown_grammar_fails(self):
        for text in (SAMPLE.replace('{"read", 1, read_nif, ERL_NIF_DIRTY_JOB_IO_BOUND}', 'SOME_MACRO(read)'),
                     SAMPLE.replace('api[]', 'api[10]'), SAMPLE.replace('load,NULL', 'F(load),NULL')):
            with self.subTest(text=text), self.assertRaises(ValueError):
                native.nif_rows(text, 'example.c')

    def test_unbound_and_unresolved_tables_are_visible(self):
        rows = native.nif_rows('ERL_NIF_INIT(example,elsewhere,NULL,NULL,NULL,NULL)', 'example.c')
        self.assertEqual(rows[-1][0], 'unresolved_nif_table')
        rows = native.nif_rows('ErlNifFunc a[]={{"f",0,f}};', 'example.c')
        self.assertEqual(rows[0][1], 'UNBOUND:a')

    def test_driver_callbacks_keep_platform_variants(self):
        text = DRIVER.replace('int (*init)(void);', 'int (*start)(int x);\n#if OTHER\nint (*start)(int x, int y);\n#endif')
        rows = native.driver_rows(text, 'driver.h')
        self.assertEqual(len(rows), 2)
        self.assertNotEqual(rows[0][5], rows[1][5])
        with self.assertRaises(ValueError):
            native.driver_rows(text.replace('int (*start)', 'int* (*start)'), 'driver.h')

    def test_driver_api_multiline_callback_argument_and_global(self):
        text = DRIVER + '\nEXTERN int driver_async(int port,\n void (*done)(void*));\nEXTERN const int driver_flag;'
        rows = native.driver_rows(text, 'driver.h')
        self.assertEqual(rows[-2][0:4], ['driver_api', 'erl_driver.h', 'driver_async', 'driver_async'])
        self.assertEqual(rows[-1][0:4], ['driver_global', 'erl_driver.h', 'driver_flag', 'driver_flag'])
        with self.assertRaises(ValueError):
            native.driver_rows(text.removesuffix(';'), 'driver.h')

    def test_tracked_lib_discovery_and_body_drift(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            for name, text in [('lib/example/c_src/nested/a.c', SAMPLE + '\nint f(void) { return 1; }'),
                               ('erts/emulator/beam/erl_driver.h', DRIVER)]:
                p = root / name
                p.parent.mkdir(parents=True, exist_ok=True)
                p.write_text(text)
            subprocess.run(['git', 'init', '-q', str(root)], check=True)
            subprocess.run(['git', '-C', str(root), 'add', '.'], check=True)
            before = native.inventory(root)
            self.assertIn('nif_export\texample', before)
            path = root / 'lib/example/c_src/nested/a.c'
            path.write_text(path.read_text().replace('return 1;', 'return 2;'))
            self.assertNotEqual(before, native.inventory(root))


if __name__ == '__main__':
    unittest.main()
