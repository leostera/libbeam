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

"""Standard-library tests: python3 -m unittest discover -s scripts -p 'test_realm_*.py'."""

import argparse
import importlib.util
import contextlib
import io
import json
import os
from pathlib import Path
import sys
import tempfile
import time
import unittest
from unittest import mock

spec = importlib.util.spec_from_file_location("realm_validation", Path(__file__).with_name("realm-validation.py"))
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


class ValidationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.log = self.root / "test.log"

    def summary(self, passed=2, failed=0, total=2, suite="example_SUITE"):
        return (f"Testing make_test_dir.emulator_test.{suite}.groups.cases: "
                f"TEST COMPLETE, {passed} ok, {failed} failed of {total} test cases\n")

    def test_valid_summary(self):
        self.log.write_text("unrelated output\n" + self.summary())
        self.assertEqual(runner.ct_results(self.log, {"example_SUITE": 2}),
                         {"example_SUITE": {"passed": 2, "failed": 0, "skipped": 0, "total": 2}})

    def test_reject_missing_failed_skipped_reduced_extra_duplicate(self):
        for text in ("", self.summary(1, 1), self.summary(1, 0),
                     self.summary(1, 0, 1), self.summary() + self.summary(suite="extra_SUITE"),
                     self.summary() * 2):
            with self.subTest(text=text):
                self.log.write_text(text)
                with self.assertRaises(ValueError):
                    runner.ct_results(self.log, {"example_SUITE": 2})

    def test_skip_summary_is_preserved_but_not_accepted(self):
        self.log.write_text("Testing x.process_SUITE: TEST COMPLETE, 100 ok, 1 failed, 3 skipped of 104 test cases\n")
        observed = runner.read_ct_results(self.log)["process_SUITE"]
        self.assertEqual(observed, {"passed": 100, "failed": 1, "skipped": 3, "total": 104})
        with self.assertRaises(ValueError):
            runner.ct_results(self.log, {"process_SUITE": 104})

    def test_broad_plan_does_not_claim_known_case_counts(self):
        plan = runner.plan(self.args(profile="broad", variants=["debug-jit"],
                                     broad_suites=["process_SUITE", "ets_SUITE"], skip_build=True))
        self.assertEqual([s.get("review_suite") for s in plan], [None, "process_SUITE", "ets_SUITE"])
        self.assertIn("stdlib_test", plan[-1]["command"])
        self.assertNotIn("expected", plan[-1])

    def test_nif_plan_compiles_cross_suite_helper(self):
        plan = runner.plan(self.args(profile="broad", variants=["debug-jit"],
                                     broad_suites=["nif_SUITE"], cases=["basic"], skip_build=True))
        self.assertEqual(len(plan), 4)
        self.assertEqual(plan[1]["command"][:2], ["mkdir", "-p"])
        self.assertTrue(plan[2]["command"][-1].endswith("/erts/emulator/test/driver_SUITE.erl"))
        self.assertIn("ARGS=-suite nif_SUITE -case basic", plan[3]["command"])
        self.assertEqual(plan[3]["selection"], ["basic"])

    def test_multiple_suites(self):
        self.log.write_text(self.summary() + self.summary(suite="second_SUITE"))
        self.assertEqual(len(runner.ct_results(self.log, {"example_SUITE": 2, "second_SUITE": 2})), 2)

    def test_command_exit_and_log(self):
        for code in (0, 7):
            result = runner.execute([sys.executable, "-c", f"print('evidence'); exit({code})"],
                                    self.root, os.environ.copy(), self.log, 10)
            self.assertEqual(result["returncode"], code)
            self.assertEqual(result["status"], "passed" if code == 0 else "failed")
            self.assertIn("evidence", self.log.read_text())

    def test_timeout_kills_descendant_after_leader_exits(self):
        # The child ignores TERM; the parent does not. Waiting only for the parent
        # would leave the child running and producing the forbidden sentinel.
        child = ("import signal,time; from pathlib import Path; "
                 "signal.signal(signal.SIGTERM, signal.SIG_IGN); "
                 "Path('ready').touch(); time.sleep(4); Path('leaked').touch()")
        parent = f"import subprocess,sys,time; subprocess.Popen([sys.executable,'-c',{child!r}]); time.sleep(60)"
        result = runner.execute([sys.executable, "-c", parent], self.root,
                                os.environ.copy(), self.log, 2)
        self.assertEqual(result["status"], "timed_out")
        self.assertEqual(result["returncode"], 124)
        self.assertTrue((self.root / "ready").exists())
        time.sleep(3)
        self.assertFalse((self.root / "leaked").exists())

    def args(self, **overrides):
        values = dict(root=self.root, variants=list(runner.VARIANTS), make="make", jobs=4,
                      skip_build=False, profile="all", build_timeout=100, test_timeout=50)
        values.update(overrides)
        return argparse.Namespace(**values)

    def test_matrix(self):
        plan = runner.plan(self.args())
        self.assertEqual(len({s["name"] for s in plan}), len(plan))
        for variant in runner.VARIANTS:
            steps = [s for s in plan if s["name"].startswith(variant)]
            self.assertEqual(len(steps), 8)  # build, runtime, Realms, five regression batches
            self.assertEqual(sum(sum(s.get("expected", {}).values()) for s in steps), 148)
            self.assertTrue(all(s["status"] == "not_run" for s in steps))

    def test_resource_profile_counts(self):
        plan = runner.plan(self.args(profile="resources", variants=["debug-jit"], skip_build=True))
        self.assertEqual(plan[-1]["expected"], {"realm_resource_SUITE": 9, "atomics_SUITE": 7, "counters_SUITE": 6, "persistent_term_SUITE": 22})
        self.assertEqual(len(plan), 2)

    def test_public_api_plan_and_opt_interpreter(self):
        plan = runner.plan(self.args(profile="public-api", variants=["opt-emu"], skip_build=True))
        self.assertEqual(len(plan), 2)
        self.assertEqual(plan[-1]["expected"], {"realm_api_SUITE": 5})
        self.assertIn("FLAVOR=emu", plan[-1]["command"])
        self.assertNotIn("opt-emu", runner.DEFAULT_VARIANTS)

    def test_upstream_c_node_plan_does_not_require_realms(self):
        plan = runner.plan(self.args(profile="c-node", variants=["debug-jit"], skip_build=True))
        self.assertEqual([s["name"] for s in plan], ["debug-jit-runtime", "debug-jit-c-node"])
        self.assertNotIn("realm_identity", str(plan))
        self.assertEqual(plan[-1]["expected"], {"process_SUITE": 1})

    def test_main_rejects_zero_exit_without_tests(self):
        (self.root / "Makefile").touch()
        (self.root / "bin").mkdir()
        (self.root / ".git").mkdir()
        erl = self.root / "bin/erl"
        erl.write_text("#!/bin/sh\nexit 0\n")
        erl.chmod(0o700)
        output = self.root / "results"
        argv = ["realm-validation.py", "--root", str(self.root), "--output", str(output),
                "--variants", "debug-jit", "--profile", "realms", "--skip-build", "--make", "true"]
        def fake_git(root, *args):
            return b".git/realm-validation.lock" if "--git-path" in args else b"test-source"
        with mock.patch.object(sys, "argv", argv), mock.patch.object(runner, "git", fake_git), \
                mock.patch.object(runner.tempfile, "gettempdir", return_value=str(self.root)), \
                contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(runner.main(), 1)
        report = json.loads((output / "summary.json").read_text())
        self.assertEqual(report["status"], "failed")
        self.assertEqual(report["steps"][-1]["returncode"], 0)
        self.assertIn("expected suites", report["steps"][-1]["error"])

    def test_broad_main_keeps_failures_and_continues(self):
        (self.root / "Makefile").touch()
        (self.root / "bin").mkdir()
        erl = self.root / "bin/erl"
        erl.write_text("#!/bin/sh\nexit 0\n")
        erl.chmod(0o700)
        make = self.root / "fake-make"
        make.write_text("#!/bin/sh\ncase \"$*\" in\n*process_SUITE*)\n"
                        "echo 'Testing x.process_SUITE: TEST COMPLETE, 0 ok, 1 failed of 1 test cases'\nexit 2;;\n"
                        "*)\necho 'Testing x.code_SUITE: TEST COMPLETE, 1 ok, 0 failed of 1 test cases'\nexit 0;;\nesac\n")
        make.chmod(0o700)
        output = self.root / "results"
        argv = ["realm-validation.py", "--root", str(self.root), "--output", str(output),
                "--variants", "debug-jit", "--profile", "broad", "--broad-suites",
                "process_SUITE", "code_SUITE", "--skip-build", "--make", str(make)]
        with mock.patch.object(sys, "argv", argv), mock.patch.object(runner, "git", return_value=b"test-source"), \
                mock.patch.object(runner.tempfile, "gettempdir", return_value=str(self.root)), \
                contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(runner.main(), 1)
        report = json.loads((output / "summary.json").read_text())
        self.assertEqual(report["status"], "failed")
        self.assertEqual([s["status"] for s in report["steps"]], ["passed", "failed", "needs_review"])
        self.assertEqual(report["steps"][1]["results"]["process_SUITE"]["failed"], 1)

    def test_user_lock_prevents_cross_worktree_collision(self):
        other = self.root / "other-checkout"
        (other / "bin").mkdir(parents=True)
        (other / "Makefile").touch()
        (other / "bin/erl").touch()
        lock_path = self.root / f"otp-realm-validation-{os.getuid()}.lock"
        argv = ["realm-validation.py", "--root", str(other), "--output", str(other / "results")]
        with lock_path.open("a") as holder:
            runner.fcntl.flock(holder, runner.fcntl.LOCK_EX | runner.fcntl.LOCK_NB)
            with mock.patch.object(sys, "argv", argv), \
                    mock.patch.object(runner.tempfile, "gettempdir", return_value=str(self.root)), \
                    contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as error:
                runner.main()
            self.assertEqual(error.exception.code, 2)
        self.assertFalse((other / "results").exists())

    def test_atomic_summary_replacement(self):
        runner.save(self.root, {"status": "running"})
        runner.save(self.root, {"status": "failed"})
        self.assertIn('"failed"', (self.root / "summary.json").read_text())
        self.assertFalse((self.root / "summary.json.tmp").exists())


if __name__ == "__main__":
    unittest.main()
