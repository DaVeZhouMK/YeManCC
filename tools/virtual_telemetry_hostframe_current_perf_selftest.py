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
        if marker in ("static void gamepadSerialPostEvent(", "static void gamepadSerialPostOwnedEvent("):
            parameters = (r"const std::string& event, const json& data = \{\}" if marker.endswith("PostEvent(") else r"std::string event, json data")
            signature = re.match(re.escape(marker) + parameters + r"\)\s*\{", text[start:])
            if signature is None:
                continue  # Declaration, not the complete current definition.
            opening = start + signature.end() - 1
        else:
            opening = text.find("{", start)
        semicolon = text.find(";", start)
        if opening < 0 or (semicolon >= 0 and semicolon < opening):
            continue
        end = close_brace(text, opening) + 1
        candidates.append((start, end, text[start:end]))
    if len(candidates) != 1:
        raise ValueError(f"expected exactly one definition for {marker!r}, got {len(candidates)}")
    return candidates[0]


def serial_mailbox_binding(text):
    if "g_gamepadSerialEventMailbox" not in text:
        return ""
    roots = re.findall(r"^static auto\* const g_gamepadSerialEventMailbox = new ymcc::ipc::ResultMailbox<GamepadSerialEvent>\(\);[^\n]*", text, re.M)
    includes = re.findall(r'^#include "ipc_result_mailbox.h"[^\n]*', text, re.M)
    if len(roots) != 1 or len(includes) != 1:
        raise ValueError("actual serial mailbox declaration/include must bind exactly once")
    return roots[0]


def serial_action_power_binding(text):
    if "g_gamepadSerialActionPower" not in text:
        return ""
    declarations = re.findall(r"^static thread_local unsigned long long g_gamepadSerialActionPower\s*=\s*0;[^\n]*", text, re.M)
    if len(declarations) != 1:
        raise ValueError("actual serial action-power TLS declaration must bind exactly once")
    return declarations[0]


def serial_support(event_struct, root, action_power=""):
    # Compile the actual type and zero-initialized TLS; metadata never enters JSON.
    support = event_struct
    if action_power:
        support += "\n#define YMCC_SERIAL_ACTION_POWER 1\n" + action_power + "\n"
    if not root:
        return support
    # Header is loaded globally by the C++ fixture, before any namespace.
    return support + "\n#define YMCC_SERIAL_EVENT_MAILBOX 1\n" + root + "\n" + (
        "struct SerialMailboxRetirement { ~SerialMailboxRetirement() { g_gamepadSerialEventMailbox->retire(); } };\n"
        "static SerialMailboxRetirement serialMailboxRetirement;\n")


def serial_wrapper(body, root):
    member = body.replace("static void ", "void ", 1)
    if root and "gamepadSerialPostOwnedEvent(" in member:
        marker = "[](std::uintptr_t token)"
        if member.count(marker) != 1:
            raise ValueError("one mailbox send callback is required")
        # Only captures fixture window/exit/PostMessage members; body is exact.
        member = member.replace(marker, "[this](std::uintptr_t token)", 1)
    return member


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
        "mailboxRoot": serial_mailbox_binding(text),
        "actionPowerDeclaration": serial_action_power_binding(text),
        "sensorSnapshot": sensor_body,
        "observers": observers,
        "proof": proof,
    }


def generated_headers(parts: dict) -> tuple[str, str]:
    methods = []
    for prefix, initializer in (("before", parts["initializer"]), ("candidate", parts["candidate"])):
        call = parts["call"] if prefix == "before" else parts["call"].replace('{"hostFrame", hostFrame}', '{"hostFrame", std::move(hostFrame)}', 1)
        methods.append(f"json {prefix}Initializer() {{\n{parts['plane']}\nreturn {initializer};\n}}\n")
        methods.append(f"void {prefix}Post() {{\n{parts['plane']}\n{call}\n" + ("capturePosted();\n" if parts["mailboxRoot"] else "") + "}\n")
        methods.append(f"void {prefix}Gated() {{\nif (g_hwnd && now - g_inputCaptureLastUi >= 80) {{\ng_inputCaptureLastUi = now;\n{prefix}Post();\n}}\n}}\n")
    wrappers = "\n\n".join(serial_wrapper(body, parts["mailboxRoot"]) for body in parts["wrappers"].values())
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
    (run / "event-struct-extracted.h").write_text(serial_support(parts["struct"], parts["mailboxRoot"], parts["actionPowerDeclaration"]), encoding="utf-8", newline="\n")
    (run / "ipc_result_mailbox.h").write_bytes((REPO / "native/ipc_result_mailbox.h").read_bytes())
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
        "mailboxHeaderPath": str(REPO / "native/ipc_result_mailbox.h"),
        "mailboxHeaderSha256": sha((run / "ipc_result_mailbox.h").read_bytes()),
        "serialAdapters": "Member send captures this; real mailbox take runs after the original post returns, before the original observation/delete path. One process-lifetime root retires at exit.",
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
                    expected_caught = pos < limit - (1 if parts["mailboxRoot"] else 0)
                    if parts["mailboxRoot"] and pos == limit - 1:
                        if row["deliveryQueued"] or row["postCalls"] != 0:
                            raise RuntimeError("mailbox insertion failure sent or retained an event")
                    elif pos >= limit and not row["deliveryQueued"]:
                        raise RuntimeError("successful fault boundary lost its event")
                    if row["badAllocCaught"] != expected_caught:
                        raise RuntimeError(f"{profile} {variant} fault {pos} caught-state mismatch")
                    row["classification"]="caught-unwound" if row["badAllocCaught"] else "successful-boundary"
                artifact.write_text(json.dumps(row,indent=2)+"\n",encoding="utf-8")
                sampled[variant].append(row)
        faults[profile] = sampled
    if sha((REPO / "native/ipc_result_mailbox.h").read_bytes()) != json.loads((run / "source-binding.json").read_text(encoding="utf-8"))["mailboxHeaderSha256"]:
        raise RuntimeError("mailbox header changed during phase")
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
    if binding.get("mailboxHeaderSha256") != sha((REPO / "native/ipc_result_mailbox.h").read_bytes()):
        raise RuntimeError("run mailbox binding does not match current header")
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
    if sha((REPO / "native/ipc_result_mailbox.h").read_bytes()) != json.loads((run / "source-binding.json").read_text(encoding="utf-8"))["mailboxHeaderSha256"]:
        raise RuntimeError("mailbox header changed during phase")
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


# This opt-in mode does not relax the historical R18 source/ownership contract.
# Its source, outputs, process accounting and performance decision are separate.
CURRENT_TREE_SHA256 = "33b9736f83f23d8f09e45985c6b249e4355cf9f962b390861386e7c548fa6c8b"
CURRENT_TREE_OUTPUT = WORKSPACE / "Testing/YMCC/Evidence/RogCpu-20261104/TelemetryTreeBuild-20261108/Prepared"
TREE_ROWS = [
    "inactive-tree", "elite-tree", "steamdeck-yaw-tree", "edge-roll-tree",
    "inactive-message", "steamdeck-message", "dualsense-edge-message",
    "inactive-emitter", "steamdeck-children-emitter",
    "edge-diagnostic-emitter", "edge-children-diagnostic-emitter",
]
TREE_PLAN = {
    "profiles": ["Release", "Checked"], "campaignsPerProfile": 2,
    "pairsPerCampaign": 11, "batchSize": 32, "batchesPerArm": 24, "warmupBatches": 6,
    "minimumFasterCyclePairs": 10, "minimumFasterWallPairs": 10,
    "minimumPairedMedianCycleReduction": 0.05, "minimumRatioOfMedianCycleReduction": 0.05,
    "minimumPairedMedianWallReduction": 0.05, "minimumRatioOfMedianWallReduction": 0.05,
    "allocationRequestsStrictlyLower": True, "requestedBytesStrictlyLower": True,
    "rows": TREE_ROWS, "retryOnMeasuredFailure": False,
}
TREE_SCOPE = [
    "Real json.hpp; complete current telemetry initializer and unchanged motion-plane lambda.",
    "Current owned post/emitter/broadcast and serial-event case are compiled with named in-memory seams; mailbox take is hoisted after post return for existing payload observations.",
    "Inactive cache function is extracted; active complete Host-shaped documents are fixture inputs.",
    "Host producer, sensor provider/lock, Windows message queue and UI/Host/device integration are not executed.",
    "Renderer seam records UTF16 bytes; actual broadcast dump and Win32 UTF8 conversion execute offline.",
    "No bad_alloc recovery/exception-unwind equivalence or whole-process CPU acceptance is claimed.",
]


def tree_write(path: Path, data) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("xb") as stream:
        stream.write(data.encode("utf-8") if isinstance(data, str) else data)


def tree_save(path: Path, data) -> None:
    tree_write(path, json.dumps(data, ensure_ascii=False, indent=2, allow_nan=False) + "\n")


def tree_pin(path: Path) -> dict:
    raw = path.read_bytes()
    return {"path": str(path.resolve()), "bytes": len(raw), "sha256": sha(raw)}


def tree_verify_pins(rows: list[dict]) -> None:
    for row in rows:
        if tree_pin(Path(row["path"])) != row:
            raise RuntimeError("input/evidence identity changed: " + row["path"])


def tree_helpers():
    import importlib.util
    sys.dont_write_bytecode = True
    path = REPO / "tools/virtual_sample_tree_perf_selftest.py"
    spec = importlib.util.spec_from_file_location("_telemetry_existing_tree_helpers", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def tree_extract(raw: bytes) -> dict:
    text = raw.decode("utf-8")
    helpers = tree_helpers()
    cb_start, cb_end, callback = function_body(text, "static void inputCaptureOnGamepad(")
    marker = 'gamepadSerialPostOwnedEvent("gyro.telemetry", {'
    if callback.count(marker) != 1:
        raise ValueError("one complete current telemetry initializer is required")
    call_start = callback.index(marker)
    opening = callback.index("{", call_start)
    closing = close_brace(callback, opening)
    if callback[closing + 1:closing + 3] != ");":
        raise ValueError("telemetry call terminator changed")
    initializer = callback[opening:closing + 1]
    entries = helpers.pairs(initializer)
    if len(entries) != 27 or entries[-1] != ('"hostFrame"', "hostFrame"):
        raise ValueError("current complete 27-field read-only Host schema required")
    if initializer.count("inputCaptureGyroPresentSnapshot()") != 1:
        raise ValueError("one sensor snapshot expression required")
    binding = "const json& hostFrame = *hostFrameView;"
    if callback.count(binding) != 1 or callback.index(binding) >= call_start:
        raise ValueError("current const-ref Host ownership binding required")
    plane_start = callback.index("const auto buildGamepadMotionPlane = [&]() -> json {")
    plane_end = close_brace(callback, callback.index("{", plane_start)) + 1
    if callback[plane_end] != ";":
        raise ValueError("motion-plane lambda terminator changed")
    plane = callback[plane_start:plane_end + 1]
    zero_axes = ""
    if "telemetryZeroAxes()" in initializer:
        zero_marker = "const auto telemetryZeroAxes = []() -> const json& {"
        if callback.count(zero_marker) != 1:
            raise ValueError("one actual telemetryZeroAxes local declaration is required")
        zero_start = callback.index(zero_marker)
        zero_end = close_brace(callback, callback.index("{", zero_start)) + 1
        if callback[zero_end] != ";" or zero_end >= call_start:
            raise ValueError("telemetryZeroAxes declaration must close before the event call")
        zero_axes = callback[zero_start:zero_end + 1]
    created = []
    assignments = helpers.explicit_object("telemetryTree", initializer, created)
    candidate = "([&]() -> json {\n" + "\n".join(assignments) + "\n        return telemetryTree;\n    }())"
    # The original field value expressions remain in their original recursive order.
    leaves = []
    def walk(expr, prefix=()):
        for key, value in helpers.pairs(expr):
            name = json.loads(key)
            if value.startswith("{"):
                walk(value, prefix + (name,))
            else:
                leaves.append({"field": list(prefix + (name,)), "expression": value})
    walk(initializer)
    if any("std::move(hostFrame)" in line for line in assignments):
        raise ValueError("shared/read-only Host may not be moved")
    if sum(line.count("inputCaptureGyroPresentSnapshot()") for line in assignments) != 1:
        raise ValueError("candidate duplicated sensor snapshot")
    if not all(candidate.index(row["expression"]) >= 0 for row in leaves):
        raise ValueError("candidate lost a field expression")
    eol = "\r\n" if "\r\n" in text else "\n"
    replacement = candidate.replace("\n", eol)
    start = cb_start + opening
    end = cb_start + closing + 1
    candidate_source = text[:start] + replacement + text[end:]
    if candidate_source[:start] != text[:start] or candidate_source[start + len(replacement):] != text[end:]:
        raise ValueError("candidate modified bytes outside the one initializer")
    gate_start = callback.index("const bool uiTelemetryCostOn =")
    gate_if = callback.index("if (g_hwnd && now - g_inputCaptureLastUi >= 80)", gate_start)
    gate_end = close_brace(callback, callback.index("{", gate_if)) + 1
    gate = callback[gate_start:gate_end]
    if gate.count(initializer) != 1 or gate.count("g_inputCaptureLastUi = now;") != 1:
        raise ValueError("exact 80ms production gate must enclose the initializer")
    if zero_axes and gate.count(zero_axes) != 1:
        raise ValueError("actual telemetryZeroAxes local must remain inside the original gate")
    wrapper_markers = [
        "static void gamepadSerialPostOwnedEvent(",
        "static void ipc_emit_owned_gyro_telemetry(",
        "static void ipc_broadcast_controller_snapshot(",
    ]
    wrappers = [function_body(text, item)[2] for item in wrapper_markers]
    inactive = function_body(text, "static const json& inputCaptureInactiveHostFrame(")[2]
    snapshot = function_body(text, "static bool inputCaptureGyroPresentSnapshot(")[2]
    struct_start = text.index("struct GamepadSerialEvent {")
    struct_end = close_brace(text, text.index("{", struct_start)) + 1
    if text[struct_end] != ";":
        raise ValueError("event type terminator changed")
    event_struct = text[struct_start:struct_end + 1]
    mailbox_root = serial_mailbox_binding(text)
    case_start = text.index("    case WM_GAMEPAD_SERIAL_EVENT: {")
    case_open = text.index("{", case_start)
    case_end = close_brace(text, case_open)
    dispatch = text[case_open + 1:case_end]
    dispatch_signature = "int dispatchSerial(LPARAM l) {\n"
    if mailbox_root:
        take = "        auto event = g_gamepadSerialEventMailbox->take(static_cast<std::uintptr_t>(l));"
        if dispatch.count(take) != 1:
            raise ValueError("exact production serial take must bind once")
        # Hoist only the real take to capturePosted, after post returns, so the
        # original payload assertions can observe it before the business branch.
        # No re-post, second queue, pointer-token cast or substitute owner.
        dispatch = dispatch.replace(take, "", 1)
        dispatch_signature = "int dispatchSerial(std::unique_ptr<GamepadSerialEvent> event) {\n"
    host_function = function_body(text, "static json inputHostSubmitPad(")[2]
    host_start = host_function.index("    json frameProof = json::object();")
    host_end = host_function.index("    return result;", host_start) + len("    return result;")
    host_context = host_function[host_start:host_end]
    host_fields = list(dict.fromkeys(re.findall(r'result\["(\w+)"\]', host_context)))
    methods = []
    for name, expression in (("before", initializer), ("candidate", candidate)):
        setup = binding + "\n" + plane + "\n"
        # Gated() includes the actual gate with this declaration already in it.
        # Standalone Initializer/Post need the exact same source-local dependency.
        value_setup = setup + (zero_axes + "\n" if zero_axes else "")
        body = "json result = " + expression + ";\n"
        if name == "candidate":
            body += '#if defined(YMCC_TREE_BAD_VALUE)\nresult["schemaVersion"] = 2;\n'
            body += '#elif defined(YMCC_TREE_BAD_TYPE)\nresult["schemaVersion"] = 1.0;\n#endif\n'
        methods.append("__declspec(noinline) json " + name + "Initializer() {\n" + value_setup + body + "return result;\n}\n")
        call = callback[call_start:closing + 3].replace(initializer, expression, 1)
        methods.append("__declspec(noinline) void " + name + "Post() {\n" + value_setup + call + ("\ncapturePosted();" if mailbox_root else "") + "\n}\n")
        selected_gate = gate.replace(initializer, expression, 1)
        methods.append("void " + name + "Gated() {\n" + setup + selected_gate + ("\ncapturePosted();" if mailbox_root else "") + "\n}\n")
    proof = {
        "callbackStartLine": text[:cb_start].count("\n") + 1,
        "callbackEndLine": text[:cb_end].count("\n") + 1,
        "initializerStartLine": text[:start].count("\n") + 1,
        "initializerEndLine": text[:end].count("\n") + 1,
        "eventFields": [json.loads(key) for key, _ in entries],
        "orderedLeafExpressions": leaves, "generatedChildren": created,
        "sourceOwnership": binding, "hostFrameMoved": False,
        "sensorExpressionCountBefore": 1, "sensorExpressionCountCandidate": 1,
        "motionPlaneByteUnchanged": True, "gateByteUnchangedExceptOneInitializer": True,
        "telemetryZeroAxesLocalSha256": sha(zero_axes.encode()),
        "telemetryZeroAxesLocalSourceExact": True,
        "onlyInitializerBytesChanged": True, "hostInputFieldsFromSource": host_fields,
        "sourceSha256": sha(raw), "candidateSourceSha256": sha(candidate_source.encode("utf-8")),
        "beforeInitializerSha256": sha(initializer.encode()), "candidateExpressionSha256": sha(candidate.encode()),
        "serialMailboxRoot": mailbox_root,
        "serialDispatchAdapter": "Actual take hoisted to capturePosted after producer returns; handler accepts that unique_ptr; post-take business body unchanged" if mailbox_root else "none",
        "scope": TREE_SCOPE,
    }
    return {
        "source-main.cpp": raw, "main.candidate.cpp": candidate_source.encode("utf-8"),
        "callback-current.cpp": callback, "initializer-before.cpp": initializer,
        "initializer-candidate.cpp": candidate, "motion-plane-current.cpp": plane,
        "gate-current.cpp": gate, "sensor-snapshot-context.cpp": snapshot,
        "telemetry-zero-axes-current.cpp": zero_axes,
        "host-input-schema-context.cpp": host_context,
        "tree-support-extracted.h": serial_support(event_struct, mailbox_root, serial_action_power_binding(text)) + "\n" + inactive + "\n",
        "tree-methods-extracted.h": "\n".join(methods),
        "tree-wrappers-extracted.h": "\n\n".join(serial_wrapper(item, mailbox_root) for item in wrappers) + "\n",
        "tree-dispatch-extracted.h": dispatch_signature + dispatch + "\n}\n",
        "wrappers-current.cpp": "\n\n".join(wrappers),
        "serial-case-current.cpp": text[case_start:case_end + 1],
        "initializer.diff": "".join(difflib.unified_diff(initializer.splitlines(True), candidate.splitlines(True), fromfile="current/initializer", tofile="private/explicit-object")),
        "proof": proof,
    }


def tree_host_inputs() -> dict:
    # Complete Host result shape is fixture data, not a receipt from a running Host.
    result = {}
    for persona in ("elite", "steamdeck", "dualsense-edge"):
        result[persona] = {
            "hostSubmission": "frame-accepted", "hostActive": True, "firstFrame": True,
            "lifecycle": "active", "persona": persona, "targetStick": "right",
            "focusIsolationSemanticNeutral": False, "motionAdmission": "hc-motion-admitted",
            "analogAdmission": "stable", "frameProof": {
                "sampleSequence": 30556, "motionPairProofId": "fixture-pair-input",
                "hostReceipt": {"runId": "r" * 64, "epoch": 7, "powerGeneration": 5,
                    "hostInstanceId": "a" * 32, "sampleSequence": 30556,
                    "hostAckAccepted": True, "receiptCompletedTick": 258671}},
            "leftStick": {"x": -0.125, "y": 0.25}, "rightStick": {"x": 0.5, "y": -0.75},
            "gyroDps": {"x": 0.25, "y": -0.5, "z": 1.0},
            "motionContribution": {"x": 0.125, "y": -0.25},
            "layoutContribution": {"x": 0.5, "y": -0.75},
            "motionManager": {"mapped": True, "triggered": True, "velocityMode": False,
                "gyroShort": {"x": 4096, "y": -8192}, "modifiedShort": {"x": 4000, "y": -8000}},
            "layoutManager": {"physicalStick": {"x": 0.25, "y": -0.5}, "stickNorm": 0.5,
                "gyroWeight": 1.0, "weightFactor": 0.75,
                "mergeOrder": "physical-stick-plus-motion-contribution"},
        }
    return result


def tree_environment_script() -> str:
    # Same explicit x64 compiler/backslash CMD recipe as the proven idle-IMU run.
    vc = r"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
    msvc = r"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207"
    sdk = r"C:\Program Files (x86)\Windows Kits\10"
    include = [msvc + r"\include"] + [sdk + "\\Include\\10.0.26100.0\\" + item for item in ("ucrt", "shared", "um", "winrt", "cppwinrt")]
    lib = [msvc + r"\lib\x64"] + [sdk + "\\Lib\\10.0.26100.0\\" + item + "\\x64" for item in ("ucrt", "um")]
    return "\r\n".join([
        "@echo off", "setlocal", "set VSLANG=1033",
        'call "' + vc + '" >nul', "if errorlevel 1 exit /b 1",
        'set "PATH=C:\\Windows\\System32;' + msvc + '\\bin\\Hostx64\\x64;C:\\Windows"',
        'set "INCLUDE=' + ";".join(include) + '"', 'set "LIB=' + ";".join(lib) + '"',
        'set "LIBPATH="', "set PATH", "set INCLUDE", "set LIB", "set VCToolsInstallDir",
        "set WindowsSdkDir", "set WindowsSDKVersion", "exit /b 0", "",
    ])


def tree_prepare(output: Path, expected_source_sha256: str = CURRENT_TREE_SHA256) -> dict:
    output = output.resolve()
    evidence = (WORKSPACE / "Testing/YMCC/Evidence").resolve()
    if not output.is_relative_to(evidence) or output == evidence:
        raise ValueError("current mode output must be a fresh development Evidence directory")
    if output.exists():
        raise FileExistsError(output)
    raw = SOURCE.read_bytes()
    if not re.fullmatch(r"[a-f0-9]{64}", expected_source_sha256) or sha(raw) != expected_source_sha256:
        raise RuntimeError("current-tree-build requires the declared actual source SHA; no historical fallback")
    parts = tree_extract(raw)
    output.mkdir(parents=True, exist_ok=False)
    for name, data in parts.items():
        if name != "proof":
            tree_write(output / name, data)
    tree_write(output / "fixture.cpp", HARNESS.read_bytes())
    tree_write(output / "json.hpp", JSON_HPP.read_bytes())
    mailbox_header = REPO / "native/ipc_result_mailbox.h"
    tree_write(output / "ipc_result_mailbox.h", mailbox_header.read_bytes())
    persona_header = REPO / "native/controller_persona.h"
    tree_write(output / "controller-persona.h", persona_header.read_bytes())
    tree_write(output / "runner-frozen.py", TOOL.read_bytes())
    helper = REPO / "tools/virtual_sample_tree_perf_selftest.py"
    tree_write(output / "existing-tree-helpers.py", helper.read_bytes())
    tree_save(output / "extraction-proof.json", parts["proof"])
    host = tree_host_inputs()
    required = set(parts["proof"]["hostInputFieldsFromSource"]) - {"sourceContextReason"}
    if any(set(row) != required for row in host.values()):
        raise ValueError("complete active Host fixture fields differ from the current result builder")
    tree_save(output / "host-inputs.json", host)
    tree_save(output / "acceptance-plan.json", TREE_PLAN)
    tree_write(output / "environment.cmd", tree_environment_script())
    live = [tree_pin(path) for path in (SOURCE, TOOL, HARNESS, JSON_HPP, helper, persona_header, mailbox_header)]
    frozen = [tree_pin(path) for path in sorted(output.iterdir()) if path.is_file()]
    manifest = {
        "mode": "current-tree-build", "atUtc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "status": "PREPARED_NOT_COMPILED", "liveInputs": live, "frozenInputs": frozen,
        "expectedSourceSha256": expected_source_sha256,
        "sourceBindingPolicy": "Freeze the full actual main at preparation; later unrelated peer edits require exact regenerated affected-block CAS. Never overwrite current main with the private full-source candidate.",
        "privateCandidateOnly": True, "productionWrites": [], "scope": TREE_SCOPE,
        "compiler": r"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\cl.exe",
        "linker": r"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\link.exe",
        "sdkVersion": "10.0.26100.0", "cmd": r"C:\Windows\System32\cmd.exe",
        "correctnessBudgetSeconds": 600, "compileTimeoutSeconds": 180,
        "correctnessTimeoutSeconds": 60, "benchmarkBudgetSeconds": 1800,
        "benchmarkTimeoutPerCampaignSeconds": 600,
        "correctnessCommand": [sys.executable, str(TOOL), "--repo-root", str(REPO), "--source-main", str(SOURCE), "--current-tree-build", "--phase", "correctness", "--run-dir", str(output)],
        "benchmarkCommand": [sys.executable, str(TOOL), "--repo-root", str(REPO), "--source-main", str(SOURCE), "--current-tree-build", "--phase", "benchmark", "--run-dir", str(output)],
        "compilationExecuted": False, "correctnessExecuted": False, "costBenchmarkExecuted": False,
        "toolDependencyClosure": "Selected tools and actual compiler-discovered headers/libraries are pinned during the later correctness phase; not claimed complete by preparation.",
    }
    tree_save(output / "PREPARATION.json", manifest)
    print(json.dumps({"status": manifest["status"], "runDirectory": str(output), "costBenchmarkExecuted": False}, indent=2))
    return manifest


def tree_invoke(command: list[str], cwd: Path, name: str, env: dict, timeout: float, allowed=(0,)):
    import os, time
    began = datetime.datetime.now(datetime.timezone.utc).isoformat()
    started = time.perf_counter()
    stdout = stderr = b""
    code = None
    timed_out = False
    spawn_error = None
    cleanup = None
    process = None
    try:
        process = subprocess.Popen(command, cwd=cwd, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            stdout, stderr = process.communicate(timeout=timeout)
        except subprocess.TimeoutExpired:
            timed_out = True
            # Only the PID created above is eligible; no name-based process termination.
            killed = subprocess.run([r"C:\Windows\System32\taskkill.exe", "/PID", str(process.pid), "/T", "/F"],
                                    capture_output=True, timeout=10, env=env)
            cleanup = {"exitCode": killed.returncode, "stdout": killed.stdout.decode(errors="replace"), "stderr": killed.stderr.decode(errors="replace")}
            if process.poll() is None:
                process.kill()
            stdout, stderr = process.communicate(timeout=10)
        code = process.returncode
    except Exception as exc:
        spawn_error = repr(exc)
        if process is not None and process.poll() is None:
            process.kill()
            stdout, stderr = process.communicate(timeout=10)
    finally:
        tree_write(cwd / (name + ".stdout.log"), stdout)
        tree_write(cwd / (name + ".stderr.log"), stderr)
        metadata = {"startedAtUtc": began, "endedAtUtc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
                    "wallMs": (time.perf_counter() - started) * 1000, "timeoutSeconds": timeout,
                    "exitCode": code, "timedOut": timed_out, "spawnError": spawn_error, "signal": None,
                    "pid": process.pid if process is not None else None,
                    "command": command, "cwd": str(cwd), "cleanup": cleanup,
                    "cleanupBoundary": "Owned root and taskkill /T on timeout; no separate all-descendants Job-lifetime acceptance claim."}
        tree_save(cwd / (name + ".PROCESS.json"), metadata)
    if timed_out or spawn_error or code not in allowed:
        raise RuntimeError(name + " failed; original stdout/stderr/process metadata retained")
    return stdout, metadata


def tree_validate_prepared(run: Path) -> dict:
    run = run.resolve()
    if not run.is_relative_to((WORKSPACE / "Testing/YMCC/Evidence").resolve()):
        raise ValueError("run directory outside development Evidence")
    prep = json.loads((run / "PREPARATION.json").read_text(encoding="utf-8"))
    if prep["mode"] != "current-tree-build":
        raise ValueError("wrong prepared mode")
    # Explicit block-CAS contract: unrelated peer main.cpp changes may continue.
    # Every extracted/generated block used by this fixture must still match exactly.
    tree_verify_pins([row for row in prep["liveInputs"] if Path(row["path"]) != SOURCE])
    tree_verify_pins(prep["frozenInputs"])
    current_raw = SOURCE.read_bytes()
    parts = tree_extract(current_raw)
    compared = []
    for name, data in parts.items():
        if name == "proof" or name.startswith("main.") or name == "source-main.cpp":
            continue
        current = data.encode("utf-8") if isinstance(data, str) else data
        if (run / name).read_bytes() != current:
            raise RuntimeError("affected source block CAS differs: " + name)
        compared.append(name)
    prep["currentSourceObservation"] = {
        "sha256": sha(current_raw), "frozenSha256": prep["expectedSourceSha256"],
        "wholeSourceSame": sha(current_raw) == prep["expectedSourceSha256"],
        "exactEqualSourceBlocks": compared, "wholeMainAcceptanceClaimed": False,
    }
    if json.loads((run / "acceptance-plan.json").read_text()) != TREE_PLAN:
        raise ValueError("fixed current-tree cost gate changed")
    return prep


def tree_child_environment(run: Path, stage: Path, invoke) -> dict:
    import os
    env = os.environ.copy()
    for key in list(env):
        if key.upper() in {"PATH", "INCLUDE", "LIB", "LIBPATH", "CL", "_CL_", "LINK", "_LINK_"} or key.upper().startswith(("VSCMD", "__VSCMD", "VCTOOLS", "VCINSTALL", "VSINSTALL", "WINDOWSSDK", "UNIVERSALCRT", "UCRTVERSION")):
            env.pop(key, None)
    env.update({"PATH": r"C:\Windows\System32;C:\Windows", "VSLANG": "1033",
                "TEMP": str(stage), "TMP": str(stage)})
    stdout, _ = invoke([r"C:\Windows\System32\cmd.exe", "/d", "/c", str(run / "environment.cmd")], stage, "environment", env, 30)
    for line in stdout.decode("utf-8", errors="replace").splitlines():
        if "=" in line and not line.startswith("="):
            key, value = line.split("=", 1)
            previous = next((item for item in env if item.lower() == key.lower()), key)
            env[previous] = value
    selected = next((value for key, value in env.items() if key.lower() == "vctoolsinstalldir"), "")
    expected = Path(r"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207")
    if Path(selected).resolve() != expected.resolve():
        raise RuntimeError("vcvars selected a different compiler toolset")
    return env


def tree_dependency_closure(stage: Path, prep: dict, env: dict) -> dict:
    # /sourceDependencies records the actual selected headers; linker verbose output
    # additionally retains every searched existing library as an explicit superset.
    headers, libraries = set(), set()
    for dep in stage.glob("*.dependencies.json"):
        data = json.loads(dep.read_text(encoding="utf-8-sig"))
        headers.update(data["Data"]["Includes"])
    for log in stage.glob("*-build.stdout.log"):
        text = log.read_text(encoding="utf-8", errors="replace")
        for line in text.splitlines():
            for match in re.finditer(r"([A-Za-z]:\\[^\r\n]*?\.lib)(?=[:\s]|$)", line, re.IGNORECASE):
                path = Path(match.group(1).strip())
                if path.is_file():
                    libraries.add(str(path.resolve()))
    if not headers or not libraries:
        raise RuntimeError("compiler include or linker library discovery is incomplete")
    bin_dir = Path(prep["compiler"]).parent
    tools = [Path(sys.executable), Path(prep["cmd"]), Path(prep["compiler"]), Path(prep["linker"])]
    tools += sorted(bin_dir.glob("*.dll"))
    tools += sorted(Path(sys.executable).parent.glob("python*.dll"))
    tools += sorted(Path(sys.executable).parent.glob("python*.zip"))
    tools += sorted(Path(sys.executable).parent.glob("*._pth"))
    tools += [Path(r"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat")]
    return {
        "tools": [tree_pin(path) for path in dict.fromkeys(tools)],
        "actualHeaders": [tree_pin(Path(path)) for path in sorted(headers)],
        "searchedExistingLibraries": [tree_pin(Path(path)) for path in sorted(libraries)],
        "toolSelection": "Explicit compiler/linker, finite child PATH/INCLUDE/LIB, compiler-directory DLL superset.",
        "libraryBoundary": "Existing paths observed in actual /VERBOSE:LIB search output; searched superset, not proof every library member linked.",
        "effectiveEnvironment": {key: value for key, value in env.items() if key.upper() in {"PATH", "INCLUDE", "LIB", "LIBPATH", "VSLANG", "VCTOOLSINSTALLDIR", "WINDOWSSDKDIR", "WINDOWSSDKVERSION"}},
    }


def tree_correctness(run: Path) -> dict:
    import time
    prep = tree_validate_prepared(run)
    stage = run / "Correctness"
    stage.mkdir(exist_ok=False)
    started = time.perf_counter()
    deadline = started + prep["correctnessBudgetSeconds"]
    reports = []
    def invoke(command, cwd, name, env, timeout, allowed=(0,)):
        remaining = deadline - time.perf_counter()
        if remaining <= 0:
            raise TimeoutError("shared correctness deadline exhausted")
        return tree_invoke(command, cwd, name, env, min(timeout, remaining), allowed)
    try:
        env = tree_child_environment(run, stage, invoke)
        tree_save(stage / "TOOLCHAIN-IDENTITY.json", {
            "selectedExecutables": [tree_pin(Path(prep[key])) for key in ("compiler", "linker", "cmd")],
            "interpreter": tree_pin(Path(sys.executable)),
            "sdkVersion": prep["sdkVersion"],
            "environment": {key: value for key, value in env.items() if key.upper() in {"PATH", "INCLUDE", "LIB", "LIBPATH", "VSLANG", "VCTOOLSINSTALLDIR", "WINDOWSSDKDIR", "WINDOWSSDKVERSION"}},
        })
        for profile in TREE_PLAN["profiles"]:
            flags = ["/O2", "/DNDEBUG", "/MT"] if profile == "Release" else ["/Od", "/RTC1", "/D_DEBUG", "/D_ITERATOR_DEBUG_LEVEL=2", "/MTd"]
            for mutation, macro in (("", ""), ("BadValue", "/DYMCC_TREE_BAD_VALUE"), ("BadType", "/DYMCC_TREE_BAD_TYPE")):
                name = profile + mutation
                executable = stage / (name + ".exe")
                args = [prep["compiler"], "/nologo", "/std:c++20", "/utf-8", "/EHsc", "/fp:precise",
                        "/DYMCC_CURRENT_TREE_BUILD", *flags, *([macro] if macro else []),
                        str(run / "fixture.cpp"), "/I" + str(run),
                        "/Fo" + str(stage / (name + ".obj")), "/Fe" + str(executable),
                        "/sourceDependencies", str(stage / (name + ".dependencies.json")),
                        "/link", "/INCREMENTAL:NO", "/VERBOSE:LIB"]
                invoke(args, stage, name + "-build", env, prep["compileTimeoutSeconds"])
                command = "negative" if mutation else "correctness"
                stdout, process = invoke([str(executable), str(run / "host-inputs.json"), command], stage,
                                         name + "-verify", env, prep["correctnessTimeoutSeconds"], (1,) if mutation else (0,))
                report = json.loads(stdout.decode("utf-8"))
                if mutation:
                    if report.get("pass") is not False or report.get("failure") != "full telemetry types/values/float-bits":
                        raise RuntimeError("negative was not rejected by the intended semantic assertion")
                elif report.get("pass") is not True or report.get("scopeVersion") != "current-tree-build-v1":
                    raise RuntimeError("current correctness report missing or rejected")
                tree_save(stage / (name + "-report.json"), report)
                reports.append({"configuration": name, "executable": tree_pin(executable), "report": report,
                                "process": tree_pin(stage / (name + "-verify.PROCESS.json")), "exitCode": process["exitCode"]})
                print(name + ": " + ("EXPECTED_SEMANTIC_REJECTION" if mutation else "PASS"), flush=True)
        closure = tree_dependency_closure(stage, prep, env)
        tree_save(stage / "DEPENDENCY-CLOSURE.json", closure)
        final_prep = tree_validate_prepared(run)
        result = {"status": "CORRECTNESS_PASS_COST_NOT_RUN", "pass": True, "profiles": reports,
                  "scope": TREE_SCOPE, "costBenchmarkExecuted": False, "productionModified": False,
                  "sourceObservation": final_prep["currentSourceObservation"]}
    except Exception as exc:
        result = {"status": "CORRECTNESS_INCOMPLETE_OR_FAILED", "pass": False, "profiles": reports,
                  "failure": repr(exc), "costBenchmarkExecuted": False, "productionModified": False}
        raise
    finally:
        if "result" in locals():
            result.update({"wallMs": (time.perf_counter() - started) * 1000,
                           "budgetSeconds": prep["correctnessBudgetSeconds"], "atUtc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
                           "artifacts": [tree_pin(path) for path in sorted(stage.iterdir()) if path.is_file()]})
            tree_save(stage / "RESULT.json", result)
    return result


def tree_evaluate_campaign(report: dict, campaign: int) -> dict:
    rows = report.get("rows", [])
    if report.get("campaign") != campaign or report.get("rounds") != 11 or [row.get("name") for row in rows] != TREE_ROWS:
        raise ValueError("fixed campaign/scenario selection differs")
    checks = []
    for row in rows:
        valid_samples = all(len(row[key]) == 11 and all(value > 0 for value in row[key])
                            for key in ("beforeCycles", "candidateCycles", "beforeNs", "candidateNs"))
        # Recompute decisions from raw paired measurements, not a producer PASS.
        import statistics
        bc, cc, bw, cw = (row[key] for key in ("beforeCycles", "candidateCycles", "beforeNs", "candidateNs"))
        values = {
            "positive11Pairs": valid_samples,
            "cycleFaster10": sum(a < b for b, a in zip(bc, cc)) >= 10,
            "wallFaster10": sum(a < b for b, a in zip(bw, cw)) >= 10,
            "pairedMedianCycles5": statistics.median([(b-a)/b for b, a in zip(bc, cc)]) >= 0.05,
            "ratioMedianCycles5": 1 - statistics.median(cc)/statistics.median(bc) >= 0.05,
            "pairedMedianWall5": statistics.median([(b-a)/b for b, a in zip(bw, cw)]) >= 0.05,
            "ratioMedianWall5": 1 - statistics.median(cw)/statistics.median(bw) >= 0.05,
            "allocationRequestsLower": row["candidateAllocationRequestsPerEvent"] < row["beforeAllocationRequestsPerEvent"],
            "allocationBytesLower": row["candidateAllocationBytesPerEvent"] < row["beforeAllocationBytesPerEvent"],
        }
        checks.append({"name": row["name"], "checks": values, "pass": all(values.values())})
    return {"campaign": campaign, "rows": checks, "pass": all(row["pass"] for row in checks)}


def tree_benchmark(run: Path) -> dict:
    import os, time
    prep = tree_validate_prepared(run)
    correct_path = run / "Correctness/RESULT.json"
    correct = json.loads(correct_path.read_text(encoding="utf-8"))
    if correct["status"] != "CORRECTNESS_PASS_COST_NOT_RUN" or not correct["pass"]:
        raise RuntimeError("actual complete correctness is required before cost")
    tree_verify_pins(correct["artifacts"])
    closure = json.loads((run / "Correctness/DEPENDENCY-CLOSURE.json").read_text(encoding="utf-8"))
    for key in ("tools", "actualHeaders", "searchedExistingLibraries"):
        tree_verify_pins(closure[key])
    stage = run / "Benchmark"
    stage.mkdir(exist_ok=False)
    started = time.perf_counter()
    deadline = started + prep["benchmarkBudgetSeconds"]
    checks = []
    env = os.environ.copy()
    # EXEs link the selected static CRT; retained tool/header/library pins remain strict.
    for key in list(env):
        if key.upper() in {"PATH", "CL", "_CL_", "LINK", "_LINK_"}:
            env.pop(key, None)
    env["PATH"] = r"C:\Windows\System32;C:\Windows"
    try:
        for profile in TREE_PLAN["profiles"]:
            for campaign in (0, 1):
                timeout = min(prep["benchmarkTimeoutPerCampaignSeconds"], deadline - time.perf_counter())
                if timeout <= 0:
                    raise TimeoutError("shared benchmark deadline exhausted")
                command = [str(run / "Correctness" / (profile + ".exe")), str(run / "host-inputs.json"), "benchmark", str(campaign)]
                stdout, _ = tree_invoke(command, stage, profile + "-campaign-" + str(campaign), env, timeout)
                report = json.loads(stdout.decode("utf-8"))
                check = tree_evaluate_campaign(report, campaign)
                tree_save(stage / (profile + "-campaign-" + str(campaign) + ".json"), report)
                checks.append({"profile": profile, **check})
                if not check["pass"]:
                    raise RuntimeError("fixed cost gate failed; no timing retries or threshold relaxation")
        tree_validate_prepared(run)
        tree_verify_pins(correct["artifacts"])
        result = {"status": "READY_FOR_PARENT_REVIEW", "pass": True, "campaigns": checks}
    except Exception as exc:
        result = {"status": "COST_REJECTED_OR_INCOMPLETE", "pass": False, "campaigns": checks, "failure": repr(exc)}
        raise
    finally:
        if "result" in locals():
            result.update({"wallMs": (time.perf_counter() - started) * 1000, "budgetSeconds": prep["benchmarkBudgetSeconds"],
                           "scope": TREE_SCOPE, "plan": TREE_PLAN, "wholeProcessCpuAcceptance": False, "productionModified": False,
                           "correctness": tree_pin(correct_path),
                           "artifacts": [tree_pin(path) for path in sorted(stage.iterdir()) if path.is_file()]})
            tree_save(stage / "RESULT.json", result)
    return result


def typed_helpers():
    # Keep the historical import/default path inert. This branch only prepares
    # the existing consumer; it cannot enter tree_invoke's weaker process owner.
    import importlib.util
    sys.dont_write_bytecode = True
    path = TOOL.parent / "fixtures/virtual_sample_tree/telemetry_typed/prepare.py"
    spec = importlib.util.spec_from_file_location("_hostframe_typed_prepare", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def typed_extract(raw: bytes, record_header: bytes, consumer_raw: bytes | None = None, empty_delivery_header: bytes | None = None) -> dict:
    """Pure typed Core/Cost/negative extraction; no process or output writes."""
    return typed_helpers().typed_extract(raw, record_header,
        HARNESS.read_bytes() if consumer_raw is None else consumer_raw, globals(), empty_delivery_header)


def typed_prepare(output: Path, expected_source_sha256: str | None = None,
                  record_header: Path | None = None,
                  expected_record_sha256: str | None = None) -> dict:
    """Prepare immutable typed inputs with exact source/header pins; never run."""
    helper = typed_helpers()
    return helper.typed_prepare(output, source=SOURCE,
        record=record_header or SOURCE.parent / "gyro_telemetry_record.h",
        expected_source_sha256=expected_source_sha256 or helper.SOURCE_SHA256,
        expected_record_sha256=expected_record_sha256 or helper.RECORD_SHA256,
        api=globals())


def typed_runtime():
    import importlib.util
    sys.dont_write_bytecode = True
    path = TOOL.parent / "fixtures/virtual_sample_tree/telemetry_typed/runtime.py"
    spec = importlib.util.spec_from_file_location("_hostframe_typed_runtime", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def typed_plan(prepared: Path, output: Path, config: Path, ledger: Path,
               phase: str, remaining_budget_ms: int,
               correctness_receipt: Path | None = None) -> dict:
    """Plan only: prepare parity and pin the explicit owned-worker closure."""
    return typed_runtime().make_plan(prepared, output, config, ledger, phase,
                                    remaining_budget_ms, correctness_receipt, globals())


def typed_execute(plan_path: Path, phase: str) -> int:
    """Explicit opt-in only; the existing raw producer supplies Job ownership."""
    plan = json.loads(plan_path.read_text(encoding="utf-8"))
    if plan.get("kind") != "typed-hostframe-owned-execution-plan-v1" or plan.get("phase") != phase:
        raise ValueError("execution plan kind/phase differs")
    # No bootstrap or native process is started by prepare/plan. The adapter
    # rechecks the supplied ledger and all pins before its one owned worker.
    process = subprocess.run([plan["tools"]["node"], plan["bridge"], "--execute", str(plan_path.resolve())],
                             timeout=plan["remainingMs"] / 1000 + 10)
    return process.returncode


def main() -> int:
    global REPO, WORKSPACE, SOURCE, JSON_HPP, HARNESS
    parser = argparse.ArgumentParser()
    parser.add_argument("--current-tree-build", action="store_true", help="Explicit current const-ref Host tree build; never changes historical R18 contracts")
    parser.add_argument("--typed-telemetry-build", action="store_true", help="Opt-in typed prepare/plan or explicit owned correctness/fixed-cost execution")
    parser.add_argument("--record-header", type=Path, help="Typed prepare only: actual gyro_telemetry_record.h (defaults beside --source-main)")
    parser.add_argument("--expected-record-sha256", help="Typed prepare only: exact actual record-header identity")
    parser.add_argument("--expected-source-sha256", help="Explicit exact current-mode prepare identity; historical source gates are unchanged")
    parser.add_argument("--phase", choices=("prepare", "plan", "correctness", "benchmark", "fixed-cost"), default="correctness")
    parser.add_argument("--execution-phase", choices=("correctness", "fixed-cost"), help="Typed plan only: original phase to allocate")
    parser.add_argument("--execution-config", type=Path, help="Typed plan only: explicit native/interpreter files, INCLUDE/LIB and header/library superset")
    parser.add_argument("--budget-ledger", type=Path, help="Typed plan only: actual cumulative raw-process/intake ledger, original cap 300000 ms")
    parser.add_argument("--remaining-budget-ms", type=int, help="Typed plan only: supplied actual remaining budget, including cleanup reserve")
    parser.add_argument("--correctness-receipt", type=Path, help="Typed fixed-cost plan only: exact-input owned core+parity intake")
    parser.add_argument("--execution-plan", type=Path, help="Typed correctness/fixed-cost only: explicit immutable PLAN.json")
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--run-dir", type=Path)
    parser.add_argument("--batches", type=int, default=24)
    parser.add_argument("--repo-root", type=Path, default=REPO)
    parser.add_argument("--source-main", type=Path, help="Explicit private main; historical SHA gate is retained")
    args = parser.parse_args()
    REPO = args.repo_root.resolve()
    WORKSPACE = REPO.parents[2]
    SOURCE = (args.source_main or REPO / "native/main.cpp").resolve()
    JSON_HPP = REPO / "deps/json/json.hpp"
    HARNESS = TOOL.with_suffix(".cpp")
    if args.typed_telemetry_build:
        if args.current_tree_build or args.batches != 24:
            parser.error("typed mode excludes current-tree-build and retains fixed 24 batches")
        if args.phase == "prepare":
            if args.run_dir or args.output == DEFAULT_OUTPUT or any((args.execution_plan, args.execution_phase,
                    args.execution_config, args.budget_ledger, args.remaining_budget_ms, args.correctness_receipt)):
                parser.error("typed prepare requires only a fresh explicit --output and source/header options")
            prepared = typed_prepare(args.output, args.expected_source_sha256,
                                     args.record_header, args.expected_record_sha256)
            print(json.dumps({"status": prepared["status"], "runDirectory": str(args.output.resolve()),
                              "compilationExecuted": False, "costBenchmarkExecuted": False,
                              "sourceReady": False}, indent=2))
            return 0
        if any((args.source_main, args.record_header, args.expected_source_sha256, args.expected_record_sha256)):
            parser.error("typed source/header options are prepare-only; later phases use frozen prepared inputs")
        if args.phase == "plan":
            if not all((args.run_dir, args.execution_phase, args.execution_config, args.budget_ledger,
                        args.remaining_budget_ms)) or args.execution_plan or args.output == DEFAULT_OUTPUT:
                parser.error("typed plan requires --run-dir --execution-phase --execution-config --budget-ledger --remaining-budget-ms and fresh --output")
            plan = typed_plan(args.run_dir, args.output, args.execution_config, args.budget_ledger,
                              args.execution_phase, args.remaining_budget_ms, args.correctness_receipt)
            print(json.dumps({"status": "OWNED_EXECUTION_PLANNED_NOT_EXECUTED", "phase": plan["phase"],
                              "plan": str(args.output.resolve() / "PLAN.json"), "runtimeExecuted": False}, indent=2))
            return 0
        if args.phase in ("correctness", "fixed-cost") and args.execution_plan:
            if any((args.run_dir, args.execution_phase, args.execution_config, args.budget_ledger,
                    args.remaining_budget_ms, args.correctness_receipt)) or args.output != DEFAULT_OUTPUT:
                parser.error("typed execution consumes only an explicit --execution-plan; no override of its frozen allocation")
            return typed_execute(args.execution_plan, args.phase)
        parser.error("typed execution requires --phase correctness|fixed-cost --execution-plan; first prepare and plan with an actual cumulative ledger")
    if args.phase in ("plan", "fixed-cost") or any((args.execution_phase, args.execution_config,
            args.budget_ledger, args.remaining_budget_ms, args.correctness_receipt, args.execution_plan)):
        parser.error("owned execution options require --typed-telemetry-build")
    if args.record_header or args.expected_record_sha256:
        parser.error("record-header options require --typed-telemetry-build --phase prepare")
    if args.current_tree_build:
        if args.batches != 24:
            parser.error("current-tree-build has a fixed 24-batch gate")
        if args.phase == "prepare":
            if args.run_dir:
                parser.error("prepare requires fresh --output, not --run-dir")
            tree_prepare(CURRENT_TREE_OUTPUT if args.output == DEFAULT_OUTPUT else args.output,
                         args.expected_source_sha256 or CURRENT_TREE_SHA256)
        else:
            if args.expected_source_sha256:
                parser.error("--expected-source-sha256 is prepare-only; later phases use frozen live-input pins")
            if not args.run_dir:
                parser.error("current correctness/benchmark requires explicit frozen --run-dir")
            (tree_correctness if args.phase == "correctness" else tree_benchmark)(args.run_dir.resolve())
        return 0
    if args.expected_source_sha256:
        parser.error("--expected-source-sha256 requires --current-tree-build --phase prepare")
    if args.phase == "prepare":
        parser.error("prepare is only available with --current-tree-build")
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
