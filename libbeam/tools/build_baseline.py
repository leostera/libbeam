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

"""Build/test a clean relocated OTP baseline; this does not build libbeam isolates."""
import argparse
import fcntl
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import platform
import signal
import subprocess
import sys
import tempfile
import time

sys.dont_write_bytecode = True


def positive(value):
    number = int(value)
    if number < 1:
        raise argparse.ArgumentTypeError("must be positive")
    return number


def clean_environment(source, output, inherited):
    env = dict(inherited)
    removed = [k for k in ("ERL_FLAGS", "ERL_AFLAGS", "ERL_ZFLAGS", "ERL_LIBS",
                           "ERL_COMPILER_OPTIONS", "ERL_TOP", "ERL_ROOTDIR") if k in env]
    for key in removed:
        del env[key]
    env.update(ERL_TOP=str(source), PATH=str(source / "bin") + os.pathsep + env.get("PATH", ""),
               ERL_CRASH_DUMP=str(output / "erl_crash.dump"))
    return env, removed


def build_plan(source, jobs, configure_args):
    return [
        ("configure", [str(source / "otp_build"), "configure", *configure_args]),
        ("bootstrap-build", ["make", f"-j{jobs}"]),
        ("update-preloaded", [str(source / "otp_build"), "update_preloaded", "--no-commit"]),
        ("rebuilt-preloaded", ["make", f"-j{jobs}"]),
    ]


def git(root, *args):
    return subprocess.check_output(["git", "-C", str(root), *args]).decode().strip()


def check_source(root):
    if Path(git(root, "rev-parse", "--show-toplevel")).resolve() != root:
        raise ValueError("--source-root must be the repository root, not beam/")
    if git(root, "status", "--porcelain"):
        raise ValueError("use a clean detached worktree, not a dirty development checkout")
    source = root / "beam"
    if not (source / "otp_build").is_file():
        raise ValueError("missing beam/otp_build")
    if any((source / p).exists() for p in ("Makefile", "config.status", "bin/erl")):
        raise ValueError("source is already configured/built; create a fresh worktree")
    return source


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--jobs", type=positive, default=4)
    parser.add_argument("--configure-arg", action="append", default=[])
    parser.add_argument("--variant", choices=("opt-jit", "debug-jit", "opt-emu", "debug-emu"),
                        default="debug-emu")
    parser.add_argument("--timeout", type=positive, default=1800, help="seconds per build/test step")
    args = parser.parse_args()
    root, output = args.source_root.resolve(), args.output.resolve()
    try:
        source = check_source(root)
        if output.is_relative_to(root):
            raise ValueError("output must be outside the source worktree")
        if output.exists():
            raise ValueError("output directory must be new")
    except (ValueError, subprocess.CalledProcessError) as error:
        parser.error(str(error))
    runner_path = source / "scripts/realm-validation.py"
    spec = importlib.util.spec_from_file_location("realm_validation", runner_path)
    runner = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(runner)
    env, removed = clean_environment(source, output, os.environ)
    report = {"schema": 1, "kind": "relocated_standalone_otp_baseline",
              "isolate_acceptance": "not_implemented", "status": "running",
              "source_root": str(root), "revision": git(root, "rev-parse", "HEAD"),
              "driver_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
              "runner_sha256": hashlib.sha256(runner_path.read_bytes()).hexdigest(),
              "platform": platform.platform(), "variant": args.variant,
              "removed_environment_keys": removed, "steps": [],
              "started_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())}
    lock_path = Path(tempfile.gettempdir()) / f"otp-realm-validation-{os.getuid()}.lock"
    def interrupted(signum, frame):
        raise KeyboardInterrupt
    signal.signal(signal.SIGTERM, interrupted)
    try:
        with lock_path.open("a") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            output.mkdir(parents=True)
            runner.save(output, report)
            for name, command in build_plan(source, args.jobs, args.configure_arg):
                print(name, flush=True)
                step = {"name": name, "command": command, "status": "running"}
                report["steps"].append(step)
                runner.save(output, report)
                step.update(runner.execute(command, source, env, output / f"{name}.log", args.timeout))
                runner.save(output, report)
                if step["status"] != "passed":
                    raise RuntimeError(f"{name}: {step['status']}")
        # Each validation subprocess takes the same lock itself, including its build.
        # Never hold it in this parent while starting a validation subprocess.
        for profile in ("all", "resources", "public-api"):
            command = [sys.executable, "-B", str(runner_path), "--root", str(source),
                       "--output", str(output / profile), "--variants", args.variant,
                       "--profile", profile, "--jobs", str(args.jobs),
                       "--build-timeout", str(args.timeout), "--test-timeout", str(args.timeout)]
            if profile != "all":
                command.append("--skip-build")
            print(profile, flush=True)
            step = {"name": profile, "command": command, "status": "running"}
            report["steps"].append(step)
            runner.save(output, report)
            step.update(runner.execute(command, source, env, output / f"{profile}.log",
                                       args.timeout * 12))
            child = output / profile / "summary.json"
            if step["status"] != "passed" or not child.is_file():
                raise RuntimeError(f"{profile}: missing or failed validation")
            summary = json.loads(child.read_text())
            if summary["status"] != "passed":
                raise RuntimeError(f"{profile}: {summary['status']}")
            step["summary"] = str(child)
            step["summary_sha256"] = hashlib.sha256(child.read_bytes()).hexdigest()
            runner.save(output, report)
        report["status"] = "passed"
    except (Exception, KeyboardInterrupt) as error:
        report.update(status="failed", error=repr(error))
    finally:
        report["finished_utc"] = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())
        report["final_worktree"] = git(root, "status", "--porcelain")
        if output.exists():
            runner.save(output, report)
    print(report["status"], report.get("error", ""), flush=True)
    return 0 if report["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
