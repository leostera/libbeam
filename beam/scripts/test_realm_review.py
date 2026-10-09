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
import copy
import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('realm_review', Path(__file__).with_name('realm-review.py'))
review = importlib.util.module_from_spec(spec)
spec.loader.exec_module(review)


class ReviewTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / 'source.c').write_text('int helper(void) { return 1; }')
        self.row = {field: 'specified' for field in review.FIELDS}
        self.row.update(id='EXAMPLE', entries='bif:example:f/0', disposition='realm_local',
                        source_files='source.c', review_state='DESIGN_REVIEWED_ENFORCEMENT_OPEN')
        self.entries = {'bif:example:f/0'}
        review.check_records(self.root, [self.row], self.entries, refresh=True)

    def test_fingerprint_detects_body_and_helper_change(self):
        review.check_records(self.root, [self.row], self.entries)
        (self.root / 'source.c').write_text('int helper(void) { return 2; }')
        with self.assertRaisesRegex(ValueError, 're-review required'):
            review.check_records(self.root, [self.row], self.entries)
        before = copy.deepcopy(self.row)
        review.check_records(self.root, [self.row], self.entries, refresh=True)
        self.assertEqual({k: v for k, v in before.items() if k != 'source_snapshot_sha256'},
                         {k: v for k, v in self.row.items() if k != 'source_snapshot_sha256'})

    def test_duplicate_unknown_missing_or_approved_fails(self):
        for field, value in [('entries', 'bif:missing:f/0'), ('context', ''),
                             ('disposition', 'allow_everything'), ('review_state', 'APPROVED')]:
            row = {**self.row, field: value}
            with self.subTest(field=field), self.assertRaises(ValueError):
                review.check_records(self.root, [row], self.entries)
        with self.assertRaises(ValueError):
            review.check_records(self.root, [self.row, self.row], self.entries)
        with self.assertRaises(ValueError):
            review.check_records(self.root, [self.row, {**self.row, 'id': 'OTHER'}], self.entries)

    def test_dependency_paths_and_membership(self):
        for sources in ('../outside', '/absolute', 'source.c;source.c', ''):
            with self.subTest(sources=sources), self.assertRaises(ValueError):
                review.source_digest(self.root, sources)
        digest = review.source_digest(self.root, 'source.c')
        (self.root / 'helper.h').write_text('int helper(void);')
        self.assertNotEqual(digest, review.source_digest(self.root, 'source.c;helper.h'))

    def test_empty_or_wrong_schema_fails(self):
        for text in ('', 'id\tentries\nA\tx', '\t'.join(review.FIELDS) + '\n'):
            with self.subTest(text=text), self.assertRaises(ValueError):
                review.read_records(text)


if __name__ == '__main__':
    unittest.main()
