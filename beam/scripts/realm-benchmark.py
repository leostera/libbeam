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

"""Fresh-VM Realm measurements; observations, not performance/security acceptance."""
import argparse
import fcntl
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import platform
import signal
import statistics
import sys
import tempfile
import time

sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location("realm_validation", Path(__file__).with_name("realm-validation.py"))
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)
NAMES = {"clock_pair", "spawn_exit", "small_message", "large_binary_message", "alias_message",
         "priority_message", "registration", "private_ets", "process_memory", "realm_activation_stop",
         "atomic_add_get", "counter_atomic_add_get", "counter_write_add_get"}


def parse_result(log, mode, iterations, kind, flavor):
    rows = [json.loads(line[len("REALM_BENCH "):]) for line in log.read_text().splitlines()
            if line.startswith("REALM_BENCH ")]
    if len(rows) != 1:
        raise ValueError("expected exactly one benchmark result")
    result = rows[0]
    if (result["mode"], result["iterations"], result["build_type"], result["flavor"]) != (mode, iterations, kind, flavor):
        raise ValueError("benchmark configuration mismatch")
    measurements = result["measurements"]
    if len(measurements) != len(NAMES) or {m["name"] for m in measurements} != NAMES:
        raise ValueError("missing/duplicate/unexpected measurements")
    for m in measurements:
        if not m["supported"]:
            if m["name"] != "realm_activation_stop" and not (mode == "restricted" and m["name"] == "registration"):
                raise ValueError("unexpected unsupported workload")
        elif m["name"] == "process_memory":
            if m["processes"] != min(iterations, 100) or m["process_info_memory_sum"] <= 0:
                raise ValueError("invalid memory observation")
        elif (m["samples"] != (min(iterations, 100) if m["name"] == "realm_activation_stop" else iterations)
              or not 0 <= m["min_ns"] <= m["p50_ns"] <= m["p99_ns"] <= m["max_ns"] <= m["total_ns"]):
            raise ValueError("invalid timing observation")
    return result


def aggregate(results):
    groups = {}
    for result in results:
        for m in result["measurements"]:
            if m["supported"]:
                for metric in ("p50_ns", "p99_ns", "total_ns", "process_info_memory_sum"):
                    if metric in m:
                        groups.setdefault((result["mode"], m["name"], metric), []).append(m[metric])
    return [{"mode": mode, "workload": name, "metric": metric, "repetitions": len(values),
             "median": statistics.median(values), "min": min(values), "max": max(values),
             "population_stddev": statistics.pstdev(values)}
            for (mode, name, metric), values in sorted(groups.items())]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parent.parent)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--variant", choices=runner.VARIANTS, default="opt-jit")
    parser.add_argument("--modes", nargs="+", choices=("host", "restricted"), default=["host"])
    parser.add_argument("--iterations", type=int, default=1000)
    parser.add_argument("--repetitions", type=int, default=5)
    parser.add_argument("--schedulers", type=runner.positive, default=2)
    args = parser.parse_args()
    if not 100 <= args.iterations <= 10000 or args.repetitions < 3 or len(args.modes) != len(set(args.modes)):
        parser.error("require 100..10000 iterations, at least three repetitions, and unique modes")
    root, output = args.root.resolve(), args.output.resolve()
    fixture = Path(__file__).with_name("realm_bench.erl").resolve()
    env = os.environ.copy()
    for key in ("ERL_FLAGS", "ERL_AFLAGS", "ERL_ZFLAGS", "ERL_LIBS", "ERL_COMPILER_OPTIONS"):
        env.pop(key, None)
    env.update(ERL_TOP=str(root), PATH=str(root / "bin") + os.pathsep + env.get("PATH", ""))
    kind, flavor = runner.VARIANTS[args.variant]
    with (Path(tempfile.gettempdir()) / f"otp-realm-validation-{os.getuid()}.lock").open("a") as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            parser.error("another validation/benchmark job holds the user-wide lock")
        output.mkdir(parents=True, exist_ok=False)
        report = {"status": "running", "root": str(root), "platform": platform.platform(),
                  "revision": runner.git(root, "rev-parse", "HEAD").decode().strip(),
                  "diff_sha256": hashlib.sha256(runner.git(root, "diff", "HEAD", "--binary")).hexdigest(),
                  "fixture_sha256": hashlib.sha256(fixture.read_bytes()).hexdigest(),
                  "driver_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                  "variant": args.variant, "iterations": args.iterations, "repetitions": args.repetitions,
                  "schedulers": args.schedulers, "modes": args.modes, "results": [], "steps": [],
                  "started_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())}
        previous = signal.signal(signal.SIGTERM, runner.interrupt)
        runner.save(output, report)
        try:
            steps = [("compile", [str(root / "bin/erlc"), "-o", str(output), str(fixture)], None)]
            for repetition in range(args.repetitions):
                # Alternate order to expose rather than always favor one warm-up order.
                modes = args.modes if repetition % 2 == 0 else list(reversed(args.modes))
                for mode in modes:
                    expression = (f'{{{kind},{flavor}}}={{erlang:system_info(build_type),erlang:system_info(emu_flavor)}},'
                                  f'R=realm_bench:run({mode},{args.iterations}),'
                                  'io:format("REALM_BENCH ~s~n",[json:encode(R)]),halt().')
                    steps.append((f"{mode}-{repetition}", [str(root / "bin/erl"), "-emu_type", kind,
                                  "-emu_flavor", flavor, "+S", str(args.schedulers), "+SDcpu", "1", "+SDio", "1",
                                  "-noshell", "-pa", str(output), "-eval", expression], mode))
            for name, command, mode in steps:
                log = output / (name + ".log")
                step = {"name": name, "command": command, "log": str(log), "status": "running"}
                report["steps"].append(step)
                runner.save(output, report)
                step.update(runner.execute(command, root, env, log, 120))
                if step["status"] != "passed":
                    report["status"] = step["status"]
                    return 1
                if mode:
                    result = parse_result(log, mode, args.iterations, kind, flavor)
                    if result["schedulers"] != args.schedulers:
                        raise ValueError("scheduler count mismatch")
                    report["results"].append(result)
                runner.save(output, report)
            report.update(status="measured_not_accepted", aggregate=aggregate(report["results"]))
            return 0
        except KeyboardInterrupt:
            report["status"] = "interrupted"
            return 130
        except Exception as error:
            report.update(status="failed", error=str(error))
            return 1
        finally:
            if report["steps"] and report["steps"][-1]["status"] == "running":
                report["steps"][-1]["status"] = report["status"]
            report["finished_utc"] = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())
            runner.save(output, report)
            signal.signal(signal.SIGTERM, previous)
            print(report["status"], output / "summary.json")


if __name__ == "__main__":
    sys.exit(main())
