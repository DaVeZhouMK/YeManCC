"""MSVC Release/Checked isolated tests of current backend cost headers and JSON.

Copies exact bytes into a SHA-bound fixture; executes only locally built fixtures.
All evidence stays below Build/Validation/VirtualInputPerf-R10-20261015/backend-local.
No product entrypoint, controller/device, service, registry or dependency install.
"""
from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
EVIDENCE = ROOT.parents[2] / "Build/Validation/VirtualInputPerf-R10-20261015/backend-local"
FILES = {
    "native/gamepad_backend_cost.h": "gamepad_backend_cost.h",
    "native/gamepad_backend_cost_json.h": "gamepad_backend_cost_json.h",
    "deps/json/json.hpp": "json.hpp",
    "tools/gamepad_backend_cost_selftest.cpp": "fixture.cpp",
    "tools/gamepad_backend_cost_selftest.py": "runner-bound.py",
}
PROFILES = {
    "release": ["/O2", "/DNDEBUG", "/MT"],
    "checked": ["/Od", "/RTC1", "/D_DEBUG", "/D_ITERATOR_DEBUG_LEVEL=2", "/MTd"],
}
DISABLED_PROOF = r'''// Compile only: exact header, fake clock/gate side effects, forced disabled Span.
#include "gamepad_backend_cost.h"
struct ProofClock {
    using duration = std::chrono::nanoseconds;
    using time_point = std::chrono::time_point<ProofClock>;
    static time_point now() noexcept;
};
bool proofGate() noexcept;
extern "C" __declspec(noinline) void disabledSpanProof(
    ymcc::input::backendCost::Aggregate& aggregate) noexcept {
    ymcc::input::backendCost::Span<ProofClock> span(false);
    span.finish(aggregate, 0x80004005u, false, []() noexcept { return proofGate(); });
}
'''


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def save_json(path: Path, value: object) -> None:
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


def vcvars(explicit: Path | None) -> Path:
    candidates = [explicit] if explicit else [
        Path("C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Auxiliary/Build/vcvars64.bat"),
        Path("C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat"),
    ]
    for candidate in candidates:
        if candidate and candidate.is_file():
            return candidate.resolve()
    raise RuntimeError("vcvars64.bat unavailable at inspected paths; no dependencies installed")


def invoke(command: list[str], cwd: Path, timeout: int) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, cwd=cwd, capture_output=True, text=True,
                          encoding="utf-8", errors="replace", timeout=timeout)


def build_batch(out: Path, compiler: Path, name: str, command: list[str]) -> dict:
    batch = out / (name + "-build.bat")
    batch.write_text('@echo off\nset VSLANG=1033\ncall "' + str(compiler) +
                     '" >nul\nif errorlevel 1 exit /b %errorlevel%\n' +
                     subprocess.list2cmdline(command) + '\nexit /b %errorlevel%\n', encoding="utf-8")
    began = dt.datetime.now(dt.timezone.utc)
    result = invoke(["cmd.exe", "/d", "/c", str(batch)], out, 120)
    (out / (name + "-build.log")).write_text(result.stdout + result.stderr, encoding="utf-8")
    if result.returncode:
        raise RuntimeError(f"{name} compilation failed ({result.returncode}); see {name}-build.log")
    return {"command": command, "exitCode": result.returncode,
            "wallSeconds": (dt.datetime.now(dt.timezone.utc) - began).total_seconds(),
            "log": str(out / (name + "-build.log"))}


def disabled_assembly_proof(out: Path, compiler: Path) -> dict:
    source = out / "disabled-span-proof.cpp"
    source.write_text(DISABLED_PROOF, encoding="utf-8")
    listing = out / "disabled-span-proof.asm"
    command = ["cl", "/nologo", "/std:c++20", "/utf-8", "/EHsc", "/O2", "/DNDEBUG", "/MT", "/W4",
               "/c", "/FA", "/Fa" + str(listing), "/Fo" + str(out / "disabled-span-proof.obj"),
               "/I" + str(out), str(source)]
    built = build_batch(out, compiler, "disabled-span-proof", command)
    text = listing.read_text(encoding="utf-8", errors="replace")
    match = re.search(r"^disabledSpanProof\s+PROC[^\n]*\n(.*?)^disabledSpanProof\s+ENDP", text, re.M | re.S)
    if not match:
        raise RuntimeError("Cannot locate disabledSpanProof assembly; zero-atomic proof unconfirmed")
    body = match.group(1)
    instructions = []
    for raw in body.splitlines():
        line = raw.split(";", 1)[0].strip()
        if not line or line.endswith(":") or line.startswith(("$", ".")):
            continue
        instructions.append(line)
    # This is stronger than searching for LOCK: no loads, stores, calls, allocations or gate access.
    if len(instructions) != 1 or not re.fullmatch(r"ret(?:\s+0)?", instructions[0], re.I):
        raise RuntimeError("Disabled probe was not a bare return: " + repr(instructions))
    result = {"pass": True, "exactCopiedHeader": True, "instructions": instructions,
              "clockCalls": 0, "gateCalls": 0, "atomicLoadsStoresOrRmw": 0,
              "scope": "Release forced-disabled production Span, with fake clock/gate declarations; runtime disabled state tested in both profiles",
              "sourceSha256": sha(source.read_bytes()), "assemblySha256": sha(listing.read_bytes()), "build": built}
    save_json(out / "disabled-span-proof.json", result)
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=EVIDENCE)
    parser.add_argument("--vcvars", type=Path)
    parser.add_argument("--iterations-per-thread", type=int, default=6000)
    parser.add_argument("--stress-rounds", type=int, default=6)
    parser.add_argument("--overhead-iterations", type=int, default=200000)
    parser.add_argument("--overhead-trials", type=int, default=5)
    parser.add_argument("--json-iterations", type=int, default=300)
    args = parser.parse_args()
    output = args.output.resolve()
    if not output.is_relative_to(EVIDENCE.resolve()):
        parser.error("--output must stay inside the authorized backend-local evidence directory")
    if not (64 <= args.iterations_per_thread <= 100000 and 1 <= args.stress_rounds <= 30 and
            64 <= args.overhead_iterations <= 1000000 and 1 <= args.overhead_trials <= 9 and
            args.overhead_trials % 2 == 1 and 10 <= args.json_iterations <= 2000):
        parser.error("parameters exceed bounded fixture limits")
    if os.name != "nt":
        parser.error("this runner requires the already installed Windows MSVC toolchain")
    compiler = vcvars(args.vcvars)
    run = output / ("run-" + dt.datetime.now().strftime("%Y%m%d-%H%M%S-%f"))
    run.mkdir(parents=True, exist_ok=False)
    binding = {"algorithm": "SHA-256", "copiedExactBytes": True, "vcvars64": str(compiler), "files": {}}
    for relative, fixture_name in FILES.items():
        original = ROOT / relative
        data = original.read_bytes()
        copied = run / fixture_name
        copied.write_bytes(data)
        binding["files"][relative] = {"source": str(original), "fixture": str(copied),
                                      "sha256": sha(data), "bytes": len(data)}
    header_text = (run / "gamepad_backend_cost.h").read_text(encoding="utf-8-sig")
    binding["bucketCountReleasePublication"] = bool(re.search(
        r"bucket\.n\.fetch_add\(1,\s*std::memory_order_release\)", header_text))
    binding["identitySha256"] = sha(json.dumps({key: row["sha256"] for key, row in binding["files"].items()},
                                              sort_keys=True, separators=(",", ":")).encode())
    save_json(run / "binding.json", binding)
    receipt = {"pass": False, "run": str(run), "binding": binding, "profiles": [],
               "scope": "Exact current native headers and vendored JSON in isolated fixtures only; no main/Host/Steam/device/service/registry execution",
               "notWholeProcessCpu": True, "perDrainCoherenceAssumed": False}
    try:
        print("Fixture binding: " + binding["identitySha256"], flush=True)
        receipt["disabledSpanAssembly"] = disabled_assembly_proof(run, compiler)
        arguments = ["--iterations-per-thread", str(args.iterations_per_thread), "--stress-rounds", str(args.stress_rounds),
                     "--overhead-iterations", str(args.overhead_iterations), "--overhead-trials", str(args.overhead_trials),
                     "--json-iterations", str(args.json_iterations)]
        for name, flags in PROFILES.items():
            print("Building/running " + name, flush=True)
            exe = run / (name + ".exe")
            command = ["cl", "/nologo", "/std:c++20", "/utf-8", "/EHsc", "/W4", *flags,
                       str(run / "fixture.cpp"), "/I" + str(run), "/Fo" + str(run / (name + ".obj")), "/Fe" + str(exe)]
            built = build_batch(run, compiler, name, command)
            result = invoke([str(exe), *arguments], run, 90)
            (run / (name + "-result.json")).write_text(result.stdout, encoding="utf-8")
            (run / (name + "-stderr.txt")).write_text(result.stderr, encoding="utf-8")
            if result.returncode:
                raise RuntimeError(f"{name} fixture failed ({result.returncode}): {result.stderr.strip()}")
            row = json.loads(result.stdout)
            if row.get("pass") is not True or row.get("inputHostNativeMainOrHardwareExecuted") is not False:
                raise RuntimeError(name + " did not report isolated success")
            receipt["profiles"].append({"profile": name, "executableSha256": sha(exe.read_bytes()),
                                        "build": built, "runExitCode": result.returncode, "results": row})
            print(name + ": PASS; " + str(row["checks"]) + " checks", flush=True)
        changed = []
        for relative, row in binding["files"].items():
            if sha((ROOT / relative).read_bytes()) != row["sha256"]:
                changed.append(relative)
            if sha(Path(row["fixture"]).read_bytes()) != row["sha256"]:
                raise RuntimeError("fixture byte identity changed: " + relative)
        receipt["currentSourcesMatchFixture"] = not changed
        receipt["changedSourcesDuringRun"] = changed
        if changed:
            raise RuntimeError("current source changed during validation; rerun to bind current headers: " + ", ".join(changed))
        receipt["pass"] = True
    except Exception as error:
        receipt["error"] = str(error)
    save_json(run / "receipt.json", receipt)
    save_json(output / "result.json", receipt)
    summary = {"pass": receipt["pass"], "run": str(run), "identitySha256": binding["identitySha256"],
               "profiles": [{"profile": entry["profile"], "checks": entry["results"]["checks"],
                             "overhead": entry["results"]["overhead"],
                             "jsonBenchmark": {
                                 key: ({field: value for field, value in metric.items() if field != "sample"}
                                       if isinstance(metric, dict) else metric)
                                 for key, metric in entry["results"]["jsonBenchmark"].items()}} for entry in receipt["profiles"]]}
    if "error" in receipt:
        summary["error"] = receipt["error"]
    save_json(run / "summary.json", summary)
    print(json.dumps(summary, indent=2))
    return 0 if receipt["pass"] else 1


if __name__ == "__main__":
    sys.exit(main())
