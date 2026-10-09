#!/usr/bin/env python3
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

"""Validate standalone upstream OTP; no Realm or isolate acceptance is implied.

Derived from the historical validation runner, retaining bounded execution,
strict CT counts and the shared user-wide lock for compatibility with old jobs.
"""

import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import signal
import subprocess
import sys
import tempfile
import time

VARIANTS = {"opt-jit": ("opt", "jit"), "debug-jit": ("debug", "jit"),
            "debug-emu": ("debug", "emu"), "opt-emu": ("opt", "emu")}
DEFAULT_VARIANTS = ["opt-jit", "debug-jit", "debug-emu"]
# Deliberately explicit: an unexpectedly empty, skipped, or reduced run must fail.
FOCUSED = [
    ("core", "-suite monitor_SUITE register_SUITE timer_bif_SUITE dirty_bif_SUITE",
     {"monitor_SUITE": 25, "register_SUITE": 1, "timer_bif_SUITE": 23,
      "dirty_bif_SUITE": 13}),
    ("process", "-suite process_SUITE -case spawn_with_binaries spawn_request_bif alias_bif",
     {"process_SUITE": 3}),
    ("signal", "-suite signal_SUITE -case kill2killed unlink_exit monitor_order "
     "monitor_named_order_local priority_messages_link_enable_disable "
     "priority_messages_monitor_enable_disable priority_messages_alias_enable_disable "
     "priority_messages_order priority_messages_qmarkers", {"signal_SUITE": 9}),
    *[("trace-" + group, "-suite trace_SUITE -group " + group +
       " -case send_trace receive_trace_priority_messages suspend suspend_opts trace_delivered",
       {"trace_SUITE": 5}) for group in ("legacy_pre_post", "dynamic_pre_post")],
]
BROAD = {suite: "emulator_test" for suite in (
    "process_SUITE", "signal_SUITE", "monitor_SUITE", "timer_bif_SUITE",
    "register_SUITE", "trace_SUITE", "distribution_SUITE", "code_SUITE",
    "multi_load_SUITE", "code_parallel_load_SUITE", "nif_SUITE", "dirty_nif_SUITE",
    "port_bif_SUITE")}
BROAD["ets_SUITE"] = "stdlib_test"
SUMMARY = re.compile(r"^Testing .*?\.(\w+_SUITE)(?:\.[^:]*)?: TEST COMPLETE, "
                     r"(\d+) ok, (\d+) failed(?:, (\d+) skipped)? of (\d+) test cases")


def read_ct_results(log):
    """Collect summaries without interpreting skips/failures as acceptance."""
    found = {}
    with log.open(errors="replace") as stream:
        for line in stream:
            match = SUMMARY.match(line)
            if not match:
                continue
            suite, passed, failed, skipped, total = match.groups()
            if suite in found:
                raise ValueError("duplicate Common Test summary: " + suite)
            found[suite] = {"passed": int(passed), "failed": int(failed),
                            "skipped": int(skipped or 0), "total": int(total)}
    return found


def ct_results(log, expected):
    """Require exactly the selected suites/case counts, with no failures or skips."""
    found = read_ct_results(log)
    if set(found) != set(expected):
        raise ValueError(f"expected suites {sorted(expected)}, found {sorted(found)}")
    for suite, count in expected.items():
        if found[suite] != {"passed": count, "failed": 0, "skipped": 0, "total": count}:
            raise ValueError(f"unexpected result for {suite}: {found[suite]} (expected {count})")
    return found


def stop_group(process):
    """Terminate only the session/process group created for this step."""
    try:
        os.killpg(process.pid, signal.SIGTERM)
    except ProcessLookupError:
        pass
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        pass
    # The leader can exit before a descendant that ignores SIGTERM.
    try:
        os.killpg(process.pid, signal.SIGKILL)
    except ProcessLookupError:
        pass
    process.wait()


def execute(command, cwd, env, log, timeout):
    start = time.monotonic()
    with log.open("wb") as output:
        process = subprocess.Popen(command, cwd=cwd, env=env, stdout=output,
                                   stderr=subprocess.STDOUT, start_new_session=True)
        try:
            code = process.wait(timeout=timeout)
            status = "passed" if code == 0 else "failed"
        except subprocess.TimeoutExpired:
            stop_group(process)
            code, status = 124, "timed_out"
        except BaseException:
            stop_group(process)
            raise
    return {"returncode": code, "status": status,
            "seconds": round(time.monotonic() - start, 3)}


def plan(args):
    steps = []
    for variant in args.variants:
        build_type, flavor = VARIANTS[variant]
        make = [args.make, f"-j{args.jobs}", f"TYPE={build_type}", f"FLAVOR={flavor}"]
        if not args.skip_build:
            steps.append({"name": variant + "-build", "command": make,
                          "timeout": args.build_timeout})
        probe = ("Info=[{K,erlang:system_info(K)} || K <- "
                 "[otp_release,system_version,system_architecture,build_type,emu_flavor]],"
                 "io:format(\"~p~n\",[Info]),"
                 f"case {{erlang:system_info(build_type),erlang:system_info(emu_flavor)}} of "
                 f"{{{build_type},{flavor}}} -> halt(0); _ -> halt(1) end.")
        steps.append({"name": variant + "-runtime", "command":
                      [str(args.root / "bin/erl"), "-emu_type", build_type,
                       "-emu_flavor", flavor, "-noshell", "-eval", probe],
                      "timeout": min(60, args.test_timeout)})
        if args.profile == "startup":
            fixture = Path(__file__).resolve().parents[1] / "tests/fixtures/startup_probe.erl"
            steps.append({"name": variant + "-startup-compile", "command":
                          [str(args.root / "bin/erlc"), "-o", str(args.output), str(fixture)],
                          "timeout": 60})
            for iteration in range(3):
                probe = (f"#{{build_type := {build_type}, flavor := {flavor}}} = "
                         "startup_probe:run(),io:format(\"STARTUP_OK~n\"),halt().")
                steps.append({"name": f"{variant}-startup-{iteration}", "command":
                              [str(args.root / "bin/erl"), "-emu_type", build_type,
                               "-emu_flavor", flavor, "-noshell", "-pa", str(args.output),
                               "-eval", probe], "timeout": 60})
        if args.profile == "resources":
            steps.append({"name": variant + "-resources", "command": make +
                          ["emulator_test", "ARGS=-suite atomics_SUITE counters_SUITE persistent_term_SUITE"],
                          "expected": {"atomics_SUITE": 7, "counters_SUITE": 6,
                                       "persistent_term_SUITE": 22},
                          "timeout": args.test_timeout})
        cases = FOCUSED if args.profile == "focused" else []
        if args.profile == "c-node":
            cases = [("c-node", "-suite process_SUITE -case spawn_against_ei_node",
                      {"process_SUITE": 1})]
        if args.profile == "broad":
            for suite in args.broad_suites:
                if suite == "nif_SUITE":
                    staging = args.root / "erts/emulator/make_test_dir/emulator_test"
                    steps.append({"name": variant + "-nif-helper-directory",
                                  "command": ["mkdir", "-p", str(staging)], "timeout": 30})
                    steps.append({"name": variant + "-nif-helper-compile",
                                  "command": [str(args.root / "bin/erlc"), "-o", str(staging),
                                              str(args.root / "erts/emulator/test/driver_SUITE.erl")],
                                  "timeout": args.test_timeout})
                selected = getattr(args, "cases", None)
                selection = "-suite " + suite + (" -case " + " ".join(selected) if selected else "")
                steps.append({"name": variant + "-" + suite, "command": make +
                              [BROAD[suite], "ARGS=" + selection],
                              "review_suite": suite, "selection": selected or "all",
                              "timeout": args.test_timeout})
        for name, selection, expected in cases:
            steps.append({"name": variant + "-" + name,
                          "command": make + ["emulator_test", "ARGS=" + selection],
                          "expected": expected, "timeout": args.test_timeout})
    for step in steps:
        step["status"] = "not_run"
    return steps


def save(output, report):
    temp = output / "summary.json.tmp"
    temp.write_text(json.dumps(report, indent=2) + "\n")
    temp.replace(output / "summary.json")


def git(root, *args):
    return subprocess.check_output(["git", "-C", str(root), *args])


def positive(value):
    result = int(value)
    if result <= 0:
        raise argparse.ArgumentTypeError("must be positive")
    return result


def interrupt(signum, frame):
    raise KeyboardInterrupt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True, help="new output directory")
    parser.add_argument("--variants", nargs="+", choices=VARIANTS,
                        default=DEFAULT_VARIANTS)
    parser.add_argument("--profile", choices=("focused", "c-node", "broad", "resources", "startup"), default="focused")
    parser.add_argument("--broad-suites", nargs="+", choices=BROAD, default=list(BROAD),
                        help="full suites for broad discovery; every result requires review")
    parser.add_argument("--cases", nargs="+", help="isolate cases in exactly one broad suite; still requires review")
    parser.add_argument("--jobs", type=positive, default=4)
    parser.add_argument("--make", default="make")
    parser.add_argument("--build-timeout", type=positive, default=1800)
    parser.add_argument("--test-timeout", type=positive, default=1800)
    parser.add_argument("--skip-build", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    args.root = args.root.resolve()
    args.output = args.output.resolve()
    if len(set(args.broad_suites)) != len(args.broad_suites):
        parser.error("broad suites must be unique")
    if args.cases and (args.profile != "broad" or len(args.broad_suites) != 1
                       or any(not re.fullmatch(r"[a-z][A-Za-z0-9_]*", case) for case in args.cases)):
        parser.error("--cases requires one --broad-suites selection and ordinary testcase names")
    if len(set(args.variants)) != len(args.variants):
        parser.error("duplicate variants are not allowed")
    steps = plan(args)
    if args.dry_run:
        print(json.dumps(steps, indent=2))
        return 0
    if not (args.root / "Makefile").is_file() or not (args.root / "bin/erl").is_file():
        parser.error("root must be a configured, bootstrapped OTP checkout")
    # OTP's compile_datadirs phase uses the fixed distributed node name 'test'.
    # Different worktrees can therefore collide too: serialize this user's runs.
    lock_path = Path(tempfile.gettempdir()) / f"otp-realm-validation-{os.getuid()}.lock"
    lock = lock_path.open("a")
    try:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except BlockingIOError:
        lock.close()
        parser.error("another validation runner holds the user-wide test lock")
    args.output.mkdir(parents=True, exist_ok=False)
    env = os.environ.copy()
    # Do not let an installed OTP, extra code paths, or implicit flags alter tests.
    removed = [key for key in ("ERL_FLAGS", "ERL_AFLAGS", "ERL_ZFLAGS", "ERL_LIBS", "ERL_COMPILER_OPTIONS", "ERL_ROOTDIR") if key in env]
    for key in removed:
        del env[key]
    env["ERL_TOP"] = str(args.root)
    env["PATH"] = str(args.root / "bin") + os.pathsep + env.get("PATH", "")
    env["CT_NODENAME"] = f"libbeam_validation_{os.getpid()}"
    env["ERL_CRASH_DUMP"] = str(args.output / "erl_crash.dump")
    report = {"schema": 1, "revision": git(args.root, "rev-parse", "HEAD").decode().strip(),
              "worktree": git(args.root, "status", "--porcelain").decode(),
              "tracked_diff_sha256": hashlib.sha256(git(args.root, "diff", "HEAD", "--binary")).hexdigest(),
              "runner_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
              "startup_fixture_sha256": hashlib.sha256((Path(__file__).resolve().parents[1] /
                  "tests/fixtures/startup_probe.erl").read_bytes()).hexdigest() if args.profile == "startup" else None,
              "root": str(args.root), "platform": platform.platform(),
              "machine": platform.machine(), "python": platform.python_version(),
              "started_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
              "removed_environment_keys": removed, "status": "running", "steps": steps}
    save(args.output, report)
    previous_term_handler = signal.signal(signal.SIGTERM, interrupt)
    try:
        for step in steps:
            step["status"] = "running"
            step["log"] = str(args.output / (step["name"] + ".log"))
            save(args.output, report)
            print(step["name"], flush=True)
            step.update(execute(step["command"], args.root, env, Path(step["log"]), step["timeout"]))
            if "review_suite" in step and step["status"] in ("passed", "failed"):
                try:
                    step["results"] = read_ct_results(Path(step["log"]))
                    if set(step["results"]) != {step["review_suite"]}:
                        raise ValueError("missing or unexpected full-suite summaries")
                    result = step["results"][step["review_suite"]]
                    if result["total"] <= 0 or result["passed"] + result["failed"] + result["skipped"] != result["total"]:
                        raise ValueError("empty or inconsistent full-suite counts")
                    if step["status"] == "passed":
                        step["status"] = "failed" if result["failed"] else "needs_review"
                except ValueError as error:
                    step.update(status="failed", error=str(error))
            if step["status"] == "passed" and "expected" in step:
                try:
                    step["results"] = ct_results(Path(step["log"]), step["expected"])
                except ValueError as error:
                    step.update(status="failed", error=str(error))
            save(args.output, report)
            if step["status"] != "passed":
                if "review_suite" in step and step["status"] in ("failed", "needs_review"):
                    continue
                report["status"] = step["status"]
                return 1
        statuses = {step["status"] for step in steps}
        report["status"] = "failed" if "failed" in statuses else (
            "needs_review" if "needs_review" in statuses else "passed")
        return 0 if report["status"] == "passed" else 1
    except KeyboardInterrupt:
        step["status"] = report["status"] = "interrupted"
        return 130
    except Exception as error:
        if "step" in locals() and step["status"] == "running":
            step.update(status="error", error=str(error))
        report.update(status="error", error=str(error))
        return 1
    finally:
        report["finished_utc"] = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())
        save(args.output, report)
        print(f'{report["status"]}: {args.output / "summary.json"}', flush=True)
        signal.signal(signal.SIGTERM, previous_term_handler)
        lock.close()


if __name__ == "__main__":
    sys.exit(main())
