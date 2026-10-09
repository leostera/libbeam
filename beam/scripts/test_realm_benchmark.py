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
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("realm_benchmark", Path(__file__).with_name("realm-benchmark.py"))
bench = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bench)


class BenchmarkTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.log = Path(self.temp.name) / "bench.log"
        self.result = {"mode": "host", "iterations": 100, "build_type": "opt", "flavor": "jit",
                       "measurements": [{"name": n, "supported": True, "samples": 100,
                                         "total_ns": 1000, "min_ns": 1, "p50_ns": 2,
                                         "p99_ns": 3, "max_ns": 4}
                                        for n in sorted(bench.NAMES)]}
        for m in self.result["measurements"]:
            if m["name"] == "process_memory":
                m.update(processes=100, process_info_memory_sum=10000)

    def parse(self, result):
        self.log.write_text("REALM_BENCH " + json.dumps(result) + "\n")
        return bench.parse_result(self.log, "host", 100, "opt", "jit")

    def test_complete_result(self):
        self.assertEqual(self.parse(self.result), self.result)

    def test_empty_duplicate_or_reduced_results_fail(self):
        for text in ("", "REALM_BENCH " + json.dumps(self.result) + "\n"):
            self.log.write_text(text * 2)
            with self.assertRaises(ValueError):
                bench.parse_result(self.log, "host", 100, "opt", "jit")
        result = copy.deepcopy(self.result)
        result["measurements"].pop()
        with self.assertRaises(ValueError):
            self.parse(result)

    def test_configuration_and_sample_mismatch_fail(self):
        result = copy.deepcopy(self.result)
        result["flavor"] = "emu"
        with self.assertRaises(ValueError):
            self.parse(result)
        result = copy.deepcopy(self.result)
        result["measurements"][0]["samples"] = 99
        with self.assertRaises(ValueError):
            self.parse(result)

    def test_unexpected_unsupported_workload_fails(self):
        result = copy.deepcopy(self.result)
        result["measurements"][0]["supported"] = False
        with self.assertRaises(ValueError):
            self.parse(result)

    def test_repetition_dispersion(self):
        rows = [{"mode": "host", "measurements": [{"name": "spawn_exit", "supported": True, "p50_ns": v}]}
                for v in [10, 20, 30]]
        summary = bench.aggregate(rows)[0]
        self.assertEqual((summary["median"], summary["min"], summary["max"], summary["repetitions"]), (20, 10, 30, 3))
        self.assertGreater(summary["population_stddev"], 0)


if __name__ == "__main__":
    unittest.main()
