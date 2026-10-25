"""R18 current-source hostFrame ownership and performance selftest.

The runner reads the current native/main.cpp at execution time, binds the exact
source SHA, extracts the actual initializer and owned event path, then compiles
an isolated fixture using the repository's real nlohmann/json.hpp. It writes
only this tool's run directory and never writes production source or old proof.
"""
from __future__ import annotations
import argparse, datetime, difflib, hashlib, json, re, shutil, subprocess, sys
from pathlib import Path

TOOL = Path(__file__).resolve()
REPO = TOOL.parent.parent
WORKSPACE = REPO.parents[2]
SOURCE = REPO / "native" / "main.cpp"
JSON_HPP = REPO / "deps" / "json" / "json.hpp"
HARNESS = REPO / "tools" / "virtual_telemetry_hostframe_current_perf_selftest.cpp"
DEFAULT_OUTPUT = WORKSPACE / "Build" / "Validation" / "VirtualInputPerf-R18-20261015" / "hostframe-local"
EXPECTED_SOURCE_SHA256 = "2c745ba4badccf691b1468193600e66c01f4b239b8f137dd37427f0396ee3d75"


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def close_brace(text: str, start: int) -> int:
    if text[start] != "{":
        raise ValueError("expected opening brace")
    depth = 0
    mode = "code"
    i = start
    while i < len(text):
        c = text[i]
        two = text[i:i + 2]
        if mode == "line":
            if c == "\n":
                mode = "code"
        elif mode == "block":
            if two == "*/":
                mode = "code"
                i += 1
        elif mode == "string":
            if c == "\\":
                i += 1
            elif c == '"':
                mode = "code"
        elif mode == "char":
            if c == "\\":
                i += 1
            elif c == "'":
                mode = "code"
        elif two == "//":
            mode = "line"
            i += 1
        elif two == "/*":
            mode = "block"
            i += 1
        elif c == '"':
            mode = "string"
        elif c == "'":
            mode = "char"
        elif c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return i
        i += 1
    raise ValueError("unclosed source brace")


def function_body(text: str, marker: str) -> tuple[int, int, str]:
    candidates = []
    for match in re.finditer(re.escape(marker), text):
        start = match.start()
        opening = text.find("{", start)
        semicolon = text.find(";", start)
        if opening < 0 or (semicolon >= 0 and semicolon < opening):
            continue
        end = close_brace(text, opening) + 1
        candidates.append((start, end, text[start:end]))
    if len(candidates) != 1:
        raise ValueError(f"expected exactly one definition for {marker!r}, got {len(candidates)}")
    return candidates[0]


def extract(text: str) -> dict:
    callback_start, callback_end, callback = function_body(text, "static void inputCaptureOnGamepad(")
    event_rel = callback.index('gamepadSerialPostOwnedEvent("gyro.telemetry", {')
    opening = callback.index("{", event_rel)
    closing = close_brace(callback, opening)
    if callback[closing + 1:closing + 3] != ");":
        raise ValueError("gyro.telemetry call did not close as expected")
    initializer = callback[opening:closing + 1]
    call = callback[event_rel:closing + 3]
    entry = '{"hostFrame", hostFrame}'
    if initializer.count(entry) != 1:
        raise ValueError("current initializer does not contain exactly one unowned hostFrame entry")
    candidate = initializer.replace(entry, '{"hostFrame", std::move(hostFrame)}', 1)
    if candidate.replace('{"hostFrame", std::move(hostFrame)}', entry, 1) != initializer:
        raise ValueError("candidate changed more than the hostFrame entry")
    plane_start = callback.index("const auto buildGamepadMotionPlane = [&]() -> json {")
    plane_open = callback.index("{", plane_start)
    plane_end = close_brace(callback, plane_open) + 1
    if callback[plane_end] != ";":
        raise ValueError("motion plane lambda terminator changed")
    plane = callback[plane_start:plane_end + 1]
    wrapper_bodies = {}
    for marker in ("static void gamepadSerialPostOwnedEvent(", "static void ipc_emit_owned_gyro_telemetry("):
        _, _, body = function_body(text, marker)
        wrapper_bodies[marker] = body
    struct_start = text.index("struct GamepadSerialEvent {")
    struct_open = text.index("{", struct_start)
    struct_end = close_brace(text, struct_open) + 1
    if text[struct_end] != ";":
        raise ValueError("GamepadSerialEvent type terminator changed")
    event_struct = text[struct_start:struct_end + 1]
    observers = {}
    observer_markers = (
        "static void inputHostSubmitOutcomeNote(",
        "static void inputCaptureCommonFrameEvidence(",
        "static void steamDeckUiOutputNote(",
        "static void msiUiOutputNote(",
        "static void ps5UiOutputNote(",
    )
    for marker in observer_markers:
        start, _, body = function_body(text, marker)
        observers[marker] = {"line": text[:start].count("\n") + 1, "source": body, "sha256": sha(body.encode())}
    sensor_start, _, sensor_body = function_body(text, "static bool inputCaptureGyroPresentSnapshot(")
    local_decl = "json hostFrame = json::object();"
    local_pos = callback.index(local_decl)
    host_pos = initializer.index(entry)
    value_reads = [m.start() for m in re.finditer(r"\bhostFrame\.value\(", initializer)]
    tail_after_call = callback[closing + 3:]
    fields = re.findall(r'^\s*\{"([^"]+)",', initializer, re.MULTILINE)
    line_at = lambda pos: text[:callback_start + pos].count("\n") + 1
    proof = {
        "sourceBinding": "current-production-source-only",
        "callbackLine": text[:callback_start].count("\n") + 1,
        "callbackEndLine": text[:callback_end].count("\n") + 1,
        "eventCallLine": line_at(event_rel),
        "initializerStartLine": line_at(opening),
        "initializerEndLine": line_at(closing),
        "hostFrameEntryLine": line_at(opening + host_pos),
        "localJsonOwner": callback[local_pos:local_pos + len(local_decl)],
        "eventFields": fields,
        "eventFieldCount": len(fields),
        "hostFrameValueReads": len(value_reads),
        "allValueReadsBeforeHostFrameEntry": all(pos < host_pos for pos in value_reads),
        "hostFrameEntryIsFinalInitializerEntry": initializer[host_pos:].replace("\r\n", "\n").strip() == '{"hostFrame", hostFrame}\n        }',
        "noHostFrameIdentifierAfterCall": re.search(r"\bhostFrame\b", tail_after_call) is None,
        "beforeInitializerSha256": sha(initializer.encode()),
        "candidateInitializerSha256": sha(candidate.encode()),
        "candidateOnlyOneEntryChanged": initializer.replace(entry, '{"hostFrame", std::move(hostFrame)}', 1) == candidate,
        "planeLambdaLine": text[:callback_start + plane_start].count("\n") + 1,
        "planeLambdaSha256": sha(plane.encode()),
        "ownedWrapperLocations": {marker: text.index(body) for marker, body in wrapper_bodies.items()},
        "ownedWrappers": {marker: {"source": body, "sha256": sha(body.encode())} for marker, body in wrapper_bodies.items()},
        "eventStructLine": text[:struct_start].count("\n") + 1,
        "eventStructSha256": sha(event_struct.encode()),
        "gyroSnapshotLine": text[:sensor_start].count("\n") + 1,
        "observers": observers,
        "jsonCppSequencing": "The initializer entries remain in source order; all nine hostFrame.value() reads precede the final hostFrame entry.",
        "ownershipRule": "Only the final hostFrame initializer entry changes to std::move(hostFrame).",
    }
    if proof["eventFieldCount"] != 27 or proof["hostFrameValueReads"] != 9:
        raise ValueError(f"unexpected current schema shape: {proof['eventFieldCount']} fields, {proof['hostFrameValueReads']} value reads")
    if not proof["allValueReadsBeforeHostFrameEntry"] or not proof["hostFrameEntryIsFinalInitializerEntry"]:
        raise ValueError("hostFrame sequencing/final-entry proof failed")
    return {
        "callback": callback,
        "initializer": initializer,
        "candidate": candidate,
        "call": call,
        "plane": plane,
        "wrappers": wrapper_bodies,
        "struct": event_struct,
        "sensorSnapshot": sensor_body,
        "observers": observers,
        "proof": proof,
    }


def generated_headers(parts: dict) -> tuple[str, str]:
    methods = []
    for prefix, initializer in (("before", parts["initializer"]), ("candidate", parts["candidate"])):
        call = parts["call"] if prefix == "before" else parts["call"].replace('{"hostFrame", hostFrame}', '{"hostFrame", std::move(hostFrame)}', 1)
        methods.append(f"json {prefix}Initializer() {{\n{parts['plane']}\nreturn {initializer};\n}}\n")
        methods.append(f"void {prefix}Post() {{\n{parts['plane']}\n{call}\n}}\n")
        methods.append(f"void {prefix}Gated() {{\nif (g_hwnd && now - g_inputCaptureLastUi >= 80) {{\ng_inputCaptureLastUi = now;\n{prefix}Post();\n}}\n}}\n")
    wrappers = "\n\n".join(body.replace("static void ", "void ", 1) for body in parts["wrappers"].values())
    return "\n".join(methods), wrappers


def inert_sample() -> dict:
    return {"imu": {"gx": 0.125, "gy": -0.25, "gz": 0.5, "orientation": {"w": 0.0, "x": 0.1, "y": -0.2, "z": 0.3}}, "fixture": "R18-inert-source-schema"}


def bind_run(run: Path, raw: bytes, parts: dict, harness_raw: bytes, json_raw: bytes, sample_raw: bytes) -> None:
    methods, wrappers = generated_headers(parts)
    (run / "source-main.cpp").write_bytes(raw)
    (run / "json.hpp").write_bytes(json_raw)
    (run / "fixture.cpp").write_bytes(harness_raw)
    (run / "event-extracted.h").write_text(methods, encoding="utf-8", newline="\n")
    (run / "owned-wrappers-extracted.h").write_text(wrappers, encoding="utf-8", newline="\n")
    (run / "event-struct-extracted.h").write_text(parts["struct"], encoding="utf-8", newline="\n")
    (run / "callback-current.cpp").write_text(parts["callback"], encoding="utf-8", newline="\n")
    (run / "initializer-before.cpp").write_text(parts["initializer"], encoding="utf-8", newline="\n")
    (run / "initializer-candidate.cpp").write_text(parts["candidate"], encoding="utf-8", newline="\n")
    (run / "motion-plane-current.cpp").write_text(parts["plane"], encoding="utf-8", newline="\n")
    (run / "owned-wrappers-current.cpp").write_text("\n\n".join(parts["wrappers"].values()), encoding="utf-8", newline="\n")
    (run / "observers-current.json").write_text(json.dumps(parts["observers"], ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    (run / "gyro-snapshot-current.cpp").write_text(parts["sensorSnapshot"], encoding="utf-8", newline="\n")
    (run / "initializer.diff").write_text("".join(difflib.unified_diff(parts["initializer"].splitlines(True), parts["candidate"].splitlines(True), fromfile="current/before", tofile="current/candidate")), encoding="utf-8")
    (run / "ownership-proof.json").write_text(json.dumps(parts["proof"], ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    (run / "sample-inert.json").write_bytes(sample_raw)
    binding = {
        "sourcePath": str(SOURCE),
        "sourceSha256AtEntry": sha(raw),
        "expectedSourceSha256": EXPECTED_SOURCE_SHA256,
        "jsonHeaderPath": str(JSON_HPP),
        "jsonHeaderSha256": sha(json_raw),
        "harnessPath": str(HARNESS),
        "harnessSha256": sha(harness_raw),
        "runnerPath": str(TOOL),
        "runnerSha256AtEntry": sha(TOOL.read_bytes()),
        "samplePath": str(run / "sample-inert.json"),
        "sampleSha256": sha(sample_raw),
        "sampleUse": "Inert source fixture only; no MSI/ROG/older scan, device, Host or Steam execution.",
        "candidateOnly": True,
        "productionWriteSet": [],
        "extractedFragmentSha256": {key: sha(value.encode()) for key, value in parts.items() if isinstance(value, str)},
        "scope": "Current actual initializer plus owned event wrapper, owned observer/emitter, GamepadSerialEvent type, motion-plane lambda and observer context.",
    }
    (run / "source-binding.json").write_text(json.dumps(binding, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def compile_profile(run: Path, profile: str) -> Path:
    options = ["/O2", "/DNDEBUG", "/MT"] if profile == "release" else ["/Od", "/RTC1", "/D_DEBUG", "/D_ITERATOR_DEBUG_LEVEL=2", "/MTd"]
    exe = run / f"{profile}.exe"
    obj = run / f"{profile}.obj"
    command = ["cl", "/nologo", "/std:c++20", "/utf-8", "/EHsc", "/fp:precise", *options, str(run / "fixture.cpp"), "/I" + str(run), "/Fo" + str(obj), "/Fe" + str(exe)]
    vcvars = Path(r"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat")
    if not vcvars.exists():
        raise RuntimeError(f"MSVC vcvars64.bat missing: {vcvars}")
    batch = run / f"{profile}-build.bat"
    batch.write_text("@echo off\nset VSLANG=1033\ncall \"" + str(vcvars) + "\" >nul\nif errorlevel 1 exit /b %errorlevel%\n" + subprocess.list2cmdline(command) + "\n", encoding="utf-8")
    result = subprocess.run(["cmd.exe", "/d", "/c", str(batch)], cwd=run, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=180)
    (run / f"{profile}-build.log").write_text(result.stdout + result.stderr, encoding="utf-8")
    if result.returncode:
        raise RuntimeError(f"{profile} compile failed; see {run / (profile + '-build.log')}")
    return exe


def run_json(command: list[str], cwd: Path, output_path: Path, timeout: int = 600) -> dict:
    result = subprocess.run(command, cwd=cwd, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=timeout)
    output_path.with_suffix(".stdout.txt").write_text(result.stdout, encoding="utf-8")
    output_path.with_suffix(".stderr.txt").write_text(result.stderr, encoding="utf-8")
    if result.returncode:
        raise RuntimeError(f"command failed ({result.returncode}); see {output_path.with_suffix('.stderr.txt')}")
    try:
        data = json.loads(result.stdout)
    except json.JSONDecodeError as exc:
        raise RuntimeError(f"command did not return JSON; see {output_path.with_suffix('.stdout.txt')}") from exc
    output_path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return data


def ensure_source_binding() -> tuple[bytes, dict]:
    raw = SOURCE.read_bytes()
    current = sha(raw)
    if current != EXPECTED_SOURCE_SHA256:
        raise RuntimeError(f"current source SHA changed: expected {EXPECTED_SOURCE_SHA256}, got {current}")
    text = raw.decode("utf-8-sig")
    return raw, extract(text)


def correctness_phase(output: Path) -> Path:
    raw, parts = ensure_source_binding()
    harness_raw = HARNESS.read_bytes()
    json_raw = JSON_HPP.read_bytes()
    sample_raw = (json.dumps(inert_sample(), ensure_ascii=False, indent=2) + "\n").encode("utf-8")
    output.mkdir(parents=True, exist_ok=True)
    run = output / ("run-" + datetime.datetime.now().strftime("%Y%m%d-%H%M%S"))
    suffix = 1
    while run.exists():
        suffix += 1
        run = output / ("run-" + datetime.datetime.now().strftime("%Y%m%d-%H%M%S") + f"-{suffix}")
    run.mkdir(parents=True)
    bind_run(run, raw, parts, harness_raw, json_raw, sample_raw)
    profiles = {}
    faults = {}
    for profile in ("release", "checked"):
        exe = compile_profile(run, profile)
        profiles[profile] = run_json([str(exe), str(run / "sample-inert.json"), "correctness"], run, run / f"{profile}-correctness.json")
        sampled = {}
        warm = {}
        for variant in ("before", "candidate"):
            warm[variant] = run_json([str(exe), str(run / "sample-inert.json"), "fault", variant, "-1"], run, run / f"{profile}-fault-{variant}-warm.json", timeout=60)
        for variant in ("before", "candidate"):
            limit = int(warm[variant]["eventAllocationCalls"])
            positions = sorted({0, 1, max(0, limit // 2), max(0, limit - 1), limit})
            sampled[variant] = []
            for pos in positions:
                artifact = run / f"{profile}-fault-{variant}-{pos}.json"
                fault = subprocess.run([str(exe), str(run / "sample-inert.json"), "fault", variant, str(pos)], cwd=run, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=60)
                artifact.with_suffix(".stdout.txt").write_text(fault.stdout, encoding="utf-8")
                artifact.with_suffix(".stderr.txt").write_text(fault.stderr, encoding="utf-8")
                if fault.returncode:
                    # Preserve fail-fast/terminate evidence. No recovery/ownership inference.
                    row = {"variant":variant,"faultIndex":pos,"returnCode":fault.returncode,"classification":"process-exit-without-recovery-proof","liveAllocationBalanceKnown":False,"fullOomExceptionParityClaimed":False}
                else:
                    row = json.loads(fault.stdout)
                    if row["liveAllocationBalance"] != 0:
                        raise RuntimeError(f"{profile} {variant} fault {pos} leaked allocations")
                    if row["badAllocCaught"] != (pos < limit):
                        raise RuntimeError(f"{profile} {variant} fault {pos} caught-state mismatch")
                    row["classification"]="caught-unwound" if row["badAllocCaught"] else "successful-boundary"
                artifact.write_text(json.dumps(row,indent=2)+"\n",encoding="utf-8")
                sampled[variant].append(row)
        faults[profile] = sampled
    final_raw = SOURCE.read_bytes()
    if sha(final_raw) != sha(raw):
        raise RuntimeError("production main.cpp changed during correctness phase")
    result = {
        "status": "READY_FOR_CAMPAIGNS",
        "pass": True,
        "runDirectory": str(run),
        "binding": json.loads((run / "source-binding.json").read_text(encoding="utf-8")),
        "profiles": profiles,
        "allocationFaults": faults,
        "fullOomExceptionParityClaimed":False,
        "allocationFaultLimitation":"Fail-fast exits retain process exit evidence only; no unwind/recovery equivalence claim.",
        "productionModified": False,
        "sourceSha256AtEnd": sha(final_raw),
        "boundSourceStable": True,
        "wholeProcessClaim": False,
        "scope": "Isolated extracted fixture only; correctness includes current initializer, move ownership, event/wrapper/observer behavior, UTF8 dump exceptions, gates and allocation fault cleanup. No device/Host/Steam execution.",
    }
    (run / "correctness-result.json").write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    (output / "latest-correctness.json").write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"status": result["status"], "runDirectory": str(run), "profiles": profiles, "productionModified": False}, ensure_ascii=False, indent=2))
    return run


def evaluate_campaign(campaign: dict) -> dict:
    rows = campaign.get("rows", [])
    row_checks = []
    for row in rows:
        check = {
            "name": row["name"],
            "pairedFaster": row["pairedFasterCount"] >= 10,
            "pairedMedianAtLeast5Percent": row["pairedMedianReduction"] >= 0.05,
            "ratioMedianAtLeast5Percent": row["ratioOfMediansReduction"] >= 0.05,
            "allocationRequestsLower": row["candidateAllocationRequestsPerEvent"] < row["beforeAllocationRequestsPerEvent"],
            "allocationBytesLower": row["candidateAllocationBytesPerEvent"] < row["beforeAllocationBytesPerEvent"],
        }
        check["pass"] = all(check.values())
        row_checks.append(check)
    return {"campaign": campaign["campaign"], "rowCount": len(rows), "rows": row_checks, "pass": len(rows) == 11 and all(x["pass"] for x in row_checks)}


def benchmark_phase(run: Path, batches: int) -> dict:
    raw, _ = ensure_source_binding()
    binding = json.loads((run / "source-binding.json").read_text(encoding="utf-8"))
    if binding["sourceSha256AtEntry"] != sha(raw):
        raise RuntimeError("run binding does not match current production source")
    sample = run / "sample-inert.json"
    all_profiles = {}
    all_pass = True
    for profile in ("release", "checked"):
        exe = run / f"{profile}.exe"
        campaigns = []
        checks = []
        for campaign in (0, 1):
            result = run_json([str(exe), str(sample), "benchmark", str(campaign), str(batches)], run, run / f"{profile}-campaign-{campaign}.json", timeout=1800)
            campaigns.append(result)
            check = evaluate_campaign(result)
            checks.append(check)
            all_pass = all_pass and check["pass"]
        all_profiles[profile] = {"campaigns":campaigns,"checks":checks}
    final_raw = SOURCE.read_bytes()
    if sha(final_raw) != sha(raw):
        raise RuntimeError("production main.cpp changed during benchmark phase")
    result = {
        "status": "READY_CANDIDATE_FOR_PARENT_REVIEW" if all_pass else "REJECTED_BY_FIXED_CRITERIA",
        "pass": all_pass,
        "runDirectory": str(run),
        "binding": binding,
        "profiles": all_profiles,
        "productionModified": False,
        "sourceSha256AtEnd": sha(final_raw),
        "boundSourceStable": True,
        "wholeProcessClaim": False,
        "criteria": "Two independent campaigns per Release/Checked profile; 11 rows per campaign; every row requires paired faster count >=10/11, paired and ratio median reductions >=5%, and lower allocation requests and bytes.",
        "scope": "Performance is for the extracted current initializer and owned path with real json.hpp; setup excluded, owned object and source destruction included, in-memory seams only.",
    }
    # Preserve campaign data and checks after the status object is assembled.
    for profile in ("release", "checked"):
        profile_campaigns = []
        profile_checks = []
        for campaign in (0, 1):
            profile_campaigns.append(json.loads((run / f"{profile}-campaign-{campaign}.json").read_text(encoding="utf-8")))
            profile_checks.append(evaluate_campaign(profile_campaigns[-1]))
        result["profiles"][profile] = {"campaigns": profile_campaigns, "criteriaChecks": profile_checks, "pass": all(x["pass"] for x in profile_checks)}
    (run / "benchmark-result.json").write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    (run / "result.json").write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    (run.parent / "latest-result.json").write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"status": result["status"], "runDirectory": str(run), "profiles": {k: v["pass"] for k, v in result["profiles"].items()}, "productionModified": False}, ensure_ascii=False, indent=2))
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--phase", choices=("correctness", "benchmark"), default="correctness")
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--run-dir", type=Path)
    parser.add_argument("--batches", type=int, default=24)
    args = parser.parse_args()
    if args.phase == "correctness":
        correctness_phase(args.output)
    else:
        if not args.run_dir:
            latest = args.output / "latest-correctness.json"
            if not latest.exists():
                raise RuntimeError("benchmark requires --run-dir or a completed correctness phase")
            args.run_dir = Path(json.loads(latest.read_text(encoding="utf-8"))["runDirectory"])
        benchmark_phase(args.run_dir, args.batches)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"R18 FAILED: {exc}", file=sys.stderr)
        raise
