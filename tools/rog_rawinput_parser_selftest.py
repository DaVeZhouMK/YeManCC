"""Bounded offline regression for the production ROG RawInput parser/registration.

Example (use the bundled Python executable, not a WindowsApps alias)::
    python tools/rog_rawinput_parser_selftest.py --source native/main.cpp \
        --reports <R25-worker.jsonl> --baseline <main.before.cpp> --out <evidence>

Registration only (no historical reports required; add --registration-readback for the R34 snapshot)::
    python tools/rog_rawinput_parser_selftest.py --registration \
        --source native/main.cpp --baseline <main.before.cpp> --out <evidence>

Compiles only extracted production functions plus offline shims using vcvars64.
No product process, device API, settings change, full build, or packaging occurs.
This temporary diagnostic is not registered as an automatic/global acceptance gate.
"""
from __future__ import annotations

import argparse
import collections
import copy
import datetime as dt
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time
import uuid

ROOT = Path(__file__).resolve().parents[1]
FUNCTIONS = ("rogUsageToXInput", "rogAxisToXInput", "rogGetAxisRange",
             "rogGetScaledAxis", "rogPadHasActivity", "rogGetMi05Axis", "rogParseReport")
STICKS = {48: "sThumbLX", 49: "sThumbLY", 51: "sThumbRX", 52: "sThumbRY"}
HATS = (0, 1, 9, 8, 10, 2, 6, 4, 5)
TEST_ID = "controller.rog-rawinput-parser-replay"


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def identity(path: Path) -> dict:
    data = path.read_bytes()
    return {"path": str(path.resolve()), "bytes": len(data), "sha256": digest(data)}


def write_json(path: Path, value: object) -> None:
    path.write_text(json.dumps(value, indent=2, ensure_ascii=True) + "\n", encoding="utf-8")


def extract_functions(path: Path, baseline: bool = False, batch: bool = False, registration: bool = False,
                      registration_readback: bool = False) -> tuple[str, list[dict], dict]:
    data = path.read_bytes()
    text = data.decode("utf-8-sig")
    bodies, records, bodies_by_name = [], [], {}
    names = (("ymccFamilyName", "machineIsRogAllyFamily", "ensureRawInputSinkWindow", "gamepadRegisterRawInput")
             if registration else FUNCTIONS + (("rogCaptureRawInput",) if batch else ()))
    if registration_readback:
        names += ("rogRawInputRegistrationEvidence",)
    for name in names:
        # A forward declaration cannot match: semicolons/braces are excluded.
        matches = list(re.finditer(r"\bstatic\s+(?:const\s+)?\w+\s*\*?\s*" + name + r"\s*\([^;{}]*\)\s*\{", text))
        if not matches and baseline and name == "rogGetMi05Axis":
            continue
        if len(matches) != 1:
            raise ValueError(f"expected exactly one definition of {name} in {path}, found {len(matches)}")
        start = matches[0].start()
        pos = matches[0].end() - 1
        depth, state = 0, "code"
        while pos < len(text):
            char, following = text[pos], text[pos:pos + 2]
            if state == "line":
                if char == "\n": state = "code"
            elif state == "block":
                if following == "*/": state = "code"; pos += 1
            elif state in ('"', "'"):
                if char == "\\": pos += 1
                elif char == state: state = "code"
            elif following == "//": state = "line"; pos += 1
            elif following == "/*": state = "block"; pos += 1
            elif char in ('"', "'"): state = char
            elif char == "{": depth += 1
            elif char == "}":
                depth -= 1
                if depth == 0:
                    break
            pos += 1
        else:
            raise ValueError(f"unterminated definition {name}")
        body = text[start:pos + 1]
        bodies.append(body)
        bodies_by_name[name] = body
        records.append({"name": name, "line": text.count("\n", 0, start) + 1,
                        "sha256": digest(body.encode("utf-8")), "bytes": len(body.encode("utf-8"))})
    if registration:
        if re.search(r"\b(?:CreateFile[AW]?|ReadFile|WriteFile|DeviceIoControl|Hid[DP]_\w+|GetRawInputData|"
                     r"GetRawInputDeviceInfo[AW])\s*\(", "\n".join(bodies)):
            raise ValueError("unexpected device/file API in extracted registration functions")
        enum = re.search(r"\benum class YmccFamily\s*:\s*int\s*\{[^{}]*\};", text)
        if enum is None:
            raise ValueError("missing production YmccFamily enum")
        declaration = enum.group()
        records.insert(0, {"name": "YmccFamily", "line": text.count("\n", 0, enum.start()) + 1,
                           "sha256": digest(declaration.encode("utf-8")), "bytes": len(declaration.encode("utf-8"))})
        signature = bodies_by_name["ensureRawInputSinkWindow"].partition("{")[0]
        has_error = bool(re.search(r"\bDWORD\s*\*\s*createError", signature))
        header = ("#ifdef FIXTURE_REGISTRATION_TYPES\n" + declaration + "\n#else\n" +
                  f"#define FIXTURE_SINK_ERROR_PARAMETER {int(has_error)}\n" +
                  f"#define FIXTURE_REGISTRATION_READBACK {int(registration_readback)}\n" + "\n\n".join(bodies) + "\n#endif\n")
        return header, records, {"path": str(path.resolve()), "bytes": len(data), "sha256": digest(data)}
    parser_signature = bodies_by_name["rogParseReport"].partition("{")[0]
    has_parameter = bool(re.search(r"\bbool\s+rogMi05\s*=\s*false\b", parser_signature))
    if not baseline and not has_parameter:
        raise ValueError("candidate lacks rogParseReport optional MI05 parameter")
    header = f"#define FIXTURE_HAS_MI05_PARAMETER {int(has_parameter)}\n#define FIXTURE_TEST_CAPTURE {int(batch)}\n" + "\n\n".join(bodies) + "\n"
    return header, records, {"path": str(path.resolve()), "bytes": len(data), "sha256": digest(data)}


def normalized(value: int, minimum: int = 0, maximum: int = 65535, invert: bool = False) -> int:
    # Expected logical endpoints/midpoint, independently of HidP physical scaling.
    ratio = max(-1.0, min(1.0, 2.0 * (value - minimum) / (maximum - minimum) - 1.0))
    ratio = -ratio if invert else ratio
    return int(ratio * (32768 if ratio < 0 else 32767))


def expected_mi05(values: dict[int, int], buttons: list[int], hat: int) -> dict:
    pad = {name: normalized(values[u] & 65535, invert=u in (49, 52)) for u, name in STICKS.items()}
    z = values[50]
    pad.update(bLeftTrigger=((32768 - z) * 255 + 16384) // 32768 if z <= 32768 else 0,
               bRightTrigger=((z - 32768) * 255 + 16383) // 32767 if z > 32768 else 0,
               wButtons=(4096 if 1 in buttons else 0) | (HATS[hat] if 1 <= hat <= 8 else 0))
    return pad


def mi05_case(case_id: str, values: dict[int, int] | None = None,
              buttons: list[int] | None = None, hat: int = 0, phase: str = "boundary") -> dict:
    axes = dict.fromkeys(range(48, 53), 32768)
    axes.update(values or {})
    buttons = buttons or []
    # Synthetic report bytes are only identity tokens for the shim, not a HID decoder.
    raw = b"\0" + b"".join(v.to_bytes(4, "little") for v in axes.values()) + bytes([hat])
    return {"id": case_id, "phase": phase, "mi05": True, "rawHex": raw.hex(),
            "caps": [{"usage": u, "min": 0, "max": -1, "bits": 16} for u in axes] +
                    [{"usage": 57, "min": 1, "max": 8, "bits": 4}],
            "values": [{"usage": u, "raw": v, "scaled": 17} for u, v in axes.items()] +
                      [{"usage": 57, "raw": hat}], "buttons": buttons,
            "expected": expected_mi05(axes, buttons, hat), "masks": {"wButtons": 4096 | 15},
            "neutralAxes": [name for u, name in STICKS.items() if axes[u] & 65535 == 32768],
            "rawOnly": True}


def historical_cases(path: Path) -> tuple[list[dict], list[dict], dict]:
    data = path.read_bytes()
    cases, provenance = [], []
    fse, button_counts = collections.Counter(), collections.Counter()
    offset = 0
    for line_number, line in enumerate(data.splitlines(keepends=True), 1):
        if not line.strip():
            offset += len(line)
            continue
        row = json.loads(line.decode("utf-8-sig"))
        if row.get("kind") != "raw-input-report":
            offset += len(line)
            continue
        detail = row["detail"]
        state = detail["standardHidState"]
        axes = {a["usage"]: a["raw"] for a in state["axes"]}
        if set(axes) != set(range(48, 53)) or len(state["axes"]) != 5:
            raise ValueError(f"unexpected historical axes at line {line_number}")
        if any(a["logicalMin"] != 0 or a["logicalMax"] != -1 for a in state["axes"]):
            raise ValueError(f"descriptor differs from supplied unsigned16 contract: line {line_number}")
        if any(not 0 <= raw <= 65535 for raw in axes.values()):
            raise ValueError(f"historical raw axis outside unsigned16: line {line_number}")
        hat = state["hat"]
        if (hat["logicalMin"], hat["logicalMax"], hat["usage"]) != (1, 8, 57):
            raise ValueError(f"unexpected hat descriptor: line {line_number}")
        raw = bytes.fromhex(detail["rawHex"])
        if not detail["standardHidParsed"] or len(raw) != detail["reportBytes"] or not raw:
            raise ValueError(f"unparsed/incomplete historical report: line {line_number}")
        case = mi05_case(f"r25-line-{line_number}", axes, state["buttons"], hat["raw"], "historical")
        case["rawHex"] = detail["rawHex"]
        gaming = detail["gamingExperience"]
        if not gaming["activeKnown"]:
            raise ValueError(f"unknown FSE metadata at line {line_number}")
        label = "active" if gaming["active"] else "inactive"
        fse[label] += 1
        button_counts["A" if 1 in state["buttons"] else "A-released"] += 1
        cases.append(case)
        provenance.append({"id": case["id"], "line": line_number, "byteOffset": offset,
                           "lineBytes": len(line), "lineSha256": digest(line),
                           "sequence": row["sequence"], "localTime": row["localTime"],
                           "tick": row["tick"], "rawHex": detail["rawHex"],
                           "rawSha256": digest(raw), "fse": label,
                           "reportBytes": detail["reportBytes"], "reportCount": detail["reportCount"],
                           "reportIndex": detail["reportIndex"], "deviceName": detail["deviceName"],
                           "recordedState": state, "fixtureInputSha256": digest(json.dumps(case, sort_keys=True).encode())})
        offset += len(line)
    if len(cases) != 7821 or dict(fse) != {"active": 7072, "inactive": 749}:
        raise ValueError(f"incomplete/unexpected R25 history: rows={len(cases)}, FSE={dict(fse)}")
    return cases, provenance, {"rows": len(cases), "fse": dict(fse), "buttons": dict(button_counts),
                               "descriptorBitSize": 16, "descriptorBitSizeSource": "supplied MI05 descriptor contract",
                               "unknownButtonMappingsAsserted": False}


def boundary_cases() -> list[dict]:
    cases = []
    for values, label in [({}, "neutral"), ({u: 0 for u in range(48, 53)}, "minimum"),
                          ({u: 65535 for u in range(48, 53)}, "maximum"),
                          ({48: 16384, 49: 49152, 50: 16384, 51: 49152, 52: 16384}, "interior"),
                          ({u: 0x18000 for u in STICKS}, "mask-to-16-bits")]:
        cases.append(mi05_case("mi05-" + label, values))
    cases[0]["activity"] = False
    for hat in range(10):
        cases.append(mi05_case(f"mi05-hat-{hat}", hat=hat))
    for map_value in range(3):
        pressed = mi05_case(f"A-map-{map_value}", buttons=[1])
        pressed["map"] = map_value
        pressed["activity"] = True
        cases.append(pressed)
        released = mi05_case(f"release-map-{map_value}")
        released["map"] = map_value
        released["activity"] = False
        cases.append(released)
    ranged = mi05_case("mi05-range-cap-interior-usages", {48: 16384, 49: 49152, 51: 65535, 52: 0})
    ranged["caps"] = [{"usage": 48, "last": 52, "min": 0, "max": -1, "bits": 16, "link": 7},
                       {"usage": 57, "min": 1, "max": 8, "bits": 4}]
    for v in ranged["values"]:
        if v["usage"] != 57:
            # Centered Z trigger reads link zero in the retained parser.
            if v["usage"] != 50: v["link"] = 7
    cases.append(ranged)
    unsigned = mi05_case("mi05-positive-65535-maximum", {48: 16384, 49: 49152, 51: 65535, 52: 0})
    for c in unsigned["caps"]:
        if c["usage"] != 57: c["max"] = 65535
    cases.append(unsigned)
    for label, alteration in [("wrong-bit-size", {"bits": 8}), ("wrong-minimum", {"min": -32768}),
                               ("wrong-maximum", {"max": 32767}), ("wrong-usage-page", {"page": 2})]:
        rejected = mi05_case("mi05-reject-" + label, {48: 0, 49: 65535, 51: 0, 52: 65535})
        for c in rejected["caps"]:
            if c["usage"] != 57: c.update(alteration)
        for name in STICKS.values(): rejected["expected"][name] = 0
        rejected["neutralAxes"] = []
        cases.append(rejected)
    generic = mi05_case("generic-default-Z-Rz-old-mapping", phase="generic")
    generic["mi05"] = False
    generic["rawOnly"] = False
    values = {48: 64, 49: 192, 50: 255, 51: 0, 52: 255, 53: 0, 57: 8}
    generic["values"] = [{"usage": u, "raw": raw, "scaled": raw} for u, raw in values.items()]
    generic["caps"] = [{"usage": u, "bits": 8, "min": 0, "max": 255} for u in range(48, 54)] + \
                      [{"usage": 57, "bits": 4, "min": 1, "max": 8}]
    generic["expected"] = {"sThumbLX": normalized(64, 0, 255), "sThumbLY": normalized(192, 0, 255, True),
                           "sThumbRX": 32767, "sThumbRY": 32767,
                           "bLeftTrigger": 0, "bRightTrigger": 0, "wButtons": 0}
    generic["neutralAxes"] = []
    generic["activity"] = True
    cases.append(generic)
    fallback = copy.deepcopy(generic)
    fallback["id"] = "generic-retained-8bit-no-caps-fallback"
    fallback["caps"] = []
    cases.append(fallback)
    return cases


def batch_cases(history: list[dict]) -> list[dict]:
    moving = next(c for c in history if any(abs(c["expected"][name]) > 2000 for name in STICKS.values()))
    samples = [moving, history[0], history[1]] # actual recorded movement, A, then neutral release
    base = {"id": "actual-capture-batch-ordered-release", "phase": "batch", "batch": samples, "expectedCount": 3}
    result = [base]
    for label, fields in [("zero-count", {"count": 0}), ("zero-stride", {"stride": 0}),
                          ("truncated-payload", {"truncate": 1}), ("excessive-count", {"count": 4294967295})]:
        result.append(dict(base, id="actual-capture-reject-" + label, expectedCount=0, **fields))
    result.append(dict(base, id="actual-capture-name-query-failure-retries-next-report",
                       nameFailOnce=True, retry=True, expectedNameCalls=2))
    result.append(dict(base, id="actual-capture-remap-boundary-busy", expectedCount=0,
                       boundaryEntered=False, expectedReason="session-boundary-busy", expectedNameCalls=0))
    return result


def find_vcvars64(explicit: Path | None) -> Path:
    if explicit:
        return explicit.resolve(strict=True)
    vswhere = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / "Microsoft Visual Studio/Installer/vswhere.exe"
    result = subprocess.run([str(vswhere), "-latest", "-products", "*", "-requires",
                             "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"],
                            check=True, capture_output=True, text=True, timeout=15)
    return (Path(result.stdout.strip()) / "VC/Auxiliary/Build/vcvars64.bat").resolve(strict=True)


def vc_environment(vcvars: Path, run: Path) -> dict[str, str]:
    # A fixed .cmd file avoids nested cmd /c quote parsing of executable paths.
    script = run / "toolchain.cmd"
    script.write_text(f'@echo off\ncall "{vcvars}" >nul\nif errorlevel 1 exit /b 1\nset\n', encoding="utf-8")
    child_env = dict(os.environ)
    temporary = run / "temporary"
    temporary.mkdir()
    child_env.update(TEMP=str(temporary), TMP=str(temporary))
    proc = subprocess.run([os.environ.get("COMSPEC", "cmd.exe"), "/d", "/c", str(script)],
                          capture_output=True, timeout=30, env=child_env)
    (run / "toolchain.stderr.log").write_bytes(proc.stderr)
    if proc.returncode:
        raise RuntimeError(f"vcvars64 failed: {proc.returncode}")
    env: dict[str, str] = {}
    for line in proc.stdout.decode("mbcs", errors="replace").splitlines():
        if "=" in line and not line.startswith("="):
            key, value = line.split("=", 1)
            env[key] = value
    # Existing local fixtures document SDK discovery missing on this host.
    # Supply installed headers/libraries to this child environment only.
    sdk_root = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / "Windows Kits/10"
    versions = sorted((sdk_root / "Include").glob("10.*"),
                      key=lambda p: tuple(int(n) for n in p.name.split(".")), reverse=True)
    sdk = next((p for p in versions if (p / "um/Windows.h").is_file()
                and (p / "ucrt/stdio.h").is_file()
                and (sdk_root / "Lib" / p.name / "um/x64/kernel32.lib").is_file()), None)
    if sdk is None:
        raise FileNotFoundError("existing Windows SDK headers/libraries unavailable")
    env["INCLUDE"] = env.get("INCLUDE", "") + ";" + ";".join(str(sdk / n) for n in ("um", "shared", "ucrt", "winrt"))
    env["LIB"] = env.get("LIB", "") + ";" + ";".join(str(sdk_root / "Lib" / sdk.name / n / "x64") for n in ("um", "ucrt"))
    write_json(run / "sdk-paths.json", {"include": str(sdk), "lib": str(sdk_root / "Lib" / sdk.name)})
    return env


def executable(name: str, env: dict[str, str]) -> Path:
    paths = next(v for k, v in env.items() if k.lower() == "path")
    for directory in paths.split(os.pathsep):
        path = Path(directory) / name
        if path.is_file(): return path.resolve()
    raise FileNotFoundError(f"vcvars64 environment lacks {name}")


def build_and_run(label: str, source: Path, run: Path, env: dict[str, str], input_path: Path,
                  cl: Path, baseline: bool = False, batch: bool = False, registration: bool = False,
                  registration_readback: bool = False) -> dict:
    started = time.perf_counter()
    folder = run / label
    folder.mkdir()
    header, records, source_identity = extract_functions(source, baseline, batch, registration, registration_readback)
    (folder / "extracted_production.hpp").write_bytes(header.encode("utf-8"))
    if registration:
        (folder / "input-source.cpp").write_bytes(source.read_bytes())
        if identity(folder / "input-source.cpp")["sha256"] != source_identity["sha256"]:
            raise RuntimeError("source changed during extraction; frozen evidence would be inconsistent")
    exe = folder / "parser_fixture.exe"
    command = [str(cl), "/nologo", "/std:c++17", "/EHsc", "/W4", "/O2", "/MT", "/utf-8", "/showIncludes",
               f'/I{ROOT / "deps/json"}', f'/I{folder}', str(ROOT / "tools/rog_rawinput_parser_selftest.cpp"),
               f'/Fo{folder / "parser_fixture.obj"}', f'/Fe{exe}', "/link", "/VERBOSE:LIB"]
    if registration:
        command.insert(1, "/DFIXTURE_TEST_REGISTRATION=1")
    write_json(folder / "compile-command.json", command)
    compile_start = time.perf_counter()
    proc = subprocess.run(command, cwd=folder, env=env, capture_output=True, timeout=120)
    (folder / "compile.stdout.log").write_bytes(proc.stdout)
    (folder / "compile.stderr.log").write_bytes(proc.stderr)
    if proc.returncode:
        raise RuntimeError(f"{label} MSVC compilation failed; see {folder}")
    compile_seconds = time.perf_counter() - compile_start
    # Inventory actual included headers and the local libraries searched by the linker.
    includes = []
    for line in proc.stdout.decode("mbcs", errors="replace").splitlines():
        if re.search(r"\(\d+(?:,\d+)?\):", line):
            continue  # Diagnostics contain a source path followed by localized prose.
        found = re.search(r"([A-Za-z]:[\\/].+)$", line)
        if found:
            include = Path(found.group(1).strip().rstrip(":"))
            try:
                if include.is_file(): includes.append(include.resolve())
            except OSError:
                continue
    if not includes:
        raise RuntimeError("/showIncludes produced no dependency inventory")
    dependencies = [identity(p) for p in sorted(set(includes), key=str)]
    write_json(folder / "dependencies.json", dependencies)
    imports = None
    if registration:
        dumpbin = executable("dumpbin.exe", env)
        imports_command = [str(dumpbin), "/nologo", "/imports", str(exe)]
        write_json(folder / "imports-command.json", imports_command)
        audit = subprocess.run(imports_command, capture_output=True, timeout=30, env=env)
        (folder / "imports.stdout.log").write_bytes(audit.stdout)
        (folder / "imports.stderr.log").write_bytes(audit.stderr)
        imports_text = audit.stdout.decode("mbcs", errors="replace")
        forbidden = re.findall(r"(?im)^\s*(?:USER32|HID|XINPUT[^\s]*)\.dll\s*$|"
                               r"\b(?:RegisterRawInputDevices|GetRawInputData|GetRawInputDeviceInfo[AW]|"
                               r"GetRegisteredRawInputDevices|GetWindowThreadProcessId|"
                               r"CreateWindowEx[AW]|RegisterClassEx[AW]|DeviceIoControl|Hid[DP]_\w+|SendInput)\b", imports_text)
        imports = {"tool": identity(dumpbin), "exitCode": audit.returncode,
                   "log": identity(folder / "imports.stdout.log"), "forbidden": forbidden,
                   "crtEvidenceFileImports": "CreateFileW/ReadFile/WriteFile are CRT evidence I/O; extracted functions forbid these calls"}
        if audit.returncode or forbidden:
            raise RuntimeError(f"registration fixture has forbidden real OS imports: {forbidden}")
    command = [str(exe), str(input_path), str(folder / "results.jsonl")]
    write_json(folder / "execute-command.json", command)
    execute_start = time.perf_counter()
    proc = subprocess.run(command, cwd=folder, env=env, capture_output=True, timeout=60)
    (folder / "execute.stdout.log").write_bytes(proc.stdout)
    (folder / "execute.stderr.log").write_bytes(proc.stderr)
    summary = json.loads(proc.stdout)
    summary.update(exitCode=proc.returncode, source=source_identity, functions=records,
                   executable=identity(exe), dependencies=identity(folder / "dependencies.json"),
                   results=identity(folder / "results.jsonl"), compileSeconds=compile_seconds,
                   executeSeconds=time.perf_counter() - execute_start,
                   totalSeconds=time.perf_counter() - started)
    if registration:
        summary["extractedHeader"] = identity(folder / "extracted_production.hpp")
        summary["importsAudit"] = imports
        summary["sourceSnapshot"] = identity(folder / "input-source.cpp")
    # An unrelated concurrent main.cpp edit may advance the whole file. Only exact
    # extracted-body equality can preserve this parser run's source identity.
    _, current_records, after = extract_functions(source, baseline, batch, registration, registration_readback)
    summary["sourceAfter"] = after
    summary["extractedFunctionsUnchanged"] = records == current_records
    return summary



def registration_cases() -> list[dict]:
    # The same input and assertions execute against candidate and untouched baseline.
    cases = []
    for family in ("AsusRogAllyX", "AsusRogAlly", "AsusRogAllyClassic", "AsusRogAllyXClassic"):
        cases.append({"id": "registration.cold-unknown-to-" + family, "family": family,
                      "cold": True, "lateResolve": True, "sink": True})
    cases.append({"id": "registration.resolved-rog-reuse", "family": "AsusRogAllyX",
                  "sink": True, "repeats": 2, "probeErrorReset": True})
    for family in ("LenovoLegionGo", "MsiClaw", "ValveSteamDeck"):
        cases.append({"id": "registration.non-rog-" + family, "family": family, "sink": False})
    cases.extend([
        {"id": "registration.cold-non-rog", "family": "ValveSteamDeck", "cold": True, "sink": False},
        {"id": "registration.unresolved-unknown", "family": "Unknown", "cold": True, "sink": False},
        {"id": "registration.create-window-failed", "family": "AsusRogAllyX", "sink": False,
         "createError": 8},
        {"id": "registration.register-class-failed", "family": "AsusRogAllyX", "sink": False,
         "classError": 5},
        {"id": "registration.class-already-exists", "family": "AsusRogAllyX", "sink": True,
         "classError": 1410},
        {"id": "registration.raw-registration-failed", "family": "AsusRogAllyX", "sink": True,
         "rawError": 87},
        {"id": "registration.keyboard-mouse-registration-failed", "family": "AsusRogAllyX", "sink": True,
         "keyboardMouseError": 13},
        {"id": "registration.sink-and-raw-errors-independent", "family": "AsusRogAllyX", "sink": False,
         "createError": 8, "rawError": 87, "keyboardMouseError": 13},
        {"id": "registration.stale-sink-recreated", "family": "AsusRogAllyX", "sink": True, "stale": True},
    ])
    return cases



def registration_readback_cases() -> list[dict]:
    # Explicit expected observations; never call or reimplement the production filter.
    sink, main, other = 0x200, 0x100, 0x444
    flags = 0x100 | 0x2000  # RIDEV_INPUTSINK | RIDEV_DEVNOTIFY

    def rid(usage: int, target: int = sink, value: int = flags, page: int = 1) -> dict:
        return {"page": page, "usage": usage, "flags": value, "target": target}

    def observed(usage: int, target: int = sink, value: int = flags,
                 at_sink: bool = True, input_sink: bool = True, at_main: bool = False) -> dict:
        return {"page": 1, "usage": usage, "flags": value, "targetHwnd": target,
                "atExpectedSink": at_sink, "inputSinkFlag": input_sink, "atMainWindow": at_main}

    devices = [rid(5), rid(4), rid(8)]
    expected = [observed(5), observed(4), observed(8)]
    cases: list[dict] = []

    def add(name: str, table: list[dict], gamepads: list[dict], matched: int, all_three: bool,
            *, sink_hwnd: int = sink, live: bool = True, error: int = 0,
            required: int | None = None, returned: int | None = None) -> None:
        count = len(table) if returned is None else returned
        ok = error == 0 and count <= 32
        cases.append({"id": "registration.readback." + name, "phase": "registration-readback",
                      "devices": table, "sinkHwnd": sink_hwnd, "sinkLive": live,
                      "readError": error, "requiredCount": len(table) if required is None else required,
                      "returnedCount": count, "sinkPid": 7001 if live else 0,
                      "sinkThreadId": 7002 if live else 0,
                      "expected": {"evidenceVersion": 2,
                          "scope": "current-process raw-input registration snapshot;not OS report delivery",
                          "readOk": ok, "readError": error, "capacity": 32,
                          "requiredOrReturnedCount": len(table) if required is None else required,
                          "returnedCount": count if ok else None,
                          "sinkHwnd": sink_hwnd, "sinkLive": live, "sinkPid": 7001 if live else 0,
                          "sinkThreadId": 7002 if live else 0, "mainHwnd": main,
                          "gamepadRegistrations": gamepads, "matchingGamepadUsages": matched,
                          "allThreeAtExpectedSink": all_three, "controlsChanged": False}})

    add("three-usages-and-keyboard-mouse", devices + [rid(6, main), rid(2, other)], expected, 3, True)
    add("all-targets-overwritten-to-main", [rid(u, main) for u in (5, 4, 8)],
        [observed(u, main, at_sink=False, at_main=True) for u in (5, 4, 8)], 0, False)
    add("one-target-overwritten-to-other", [rid(5), rid(4, other), rid(8)],
        [observed(5), observed(4, other, at_sink=False), observed(8)], 2, False)
    add("missing-multi-axis", devices[:2], expected[:2], 2, False)
    add("api-error", devices, [], 0, False, error=5)
    add("over-fixed-capacity", devices, [], 0, False, error=122, required=40)
    add("exact-fixed-capacity", devices + [rid(5, page=0xff00)] * 29, expected, 3, True)
    add("defensive-success-count-over-capacity", devices, [], 0, False, required=33, returned=33)
    add("sink-absent-null-targets", [rid(u, 0) for u in (5, 4, 8)],
        [observed(u, 0, at_sink=False) for u in (5, 4, 8)], 0, False, sink_hwnd=0, live=False)
    add("sink-stale", [rid(u, 0x199) for u in (5, 4, 8)],
        [observed(u, 0x199, at_sink=False) for u in (5, 4, 8)], 0, False, sink_hwnd=0x199, live=False)
    add("keyboard-mouse-and-wrong-page-only", [rid(6), rid(2), rid(4, page=2), rid(5, page=2), rid(8, page=2)],
        [], 0, False)
    add("flags-preserved-and-inputsink-required", [rid(5, value=0x2000), rid(4, value=0x3000), rid(8, value=0x100)],
        [observed(5, value=0x2000, input_sink=False), observed(4, value=0x3000, input_sink=False),
         observed(8, value=0x100)], 1, False)
    add("duplicates-do-not-prove-three-usages", [rid(5)] * 3, [observed(5)] * 3, 3, False)
    add("empty-registration-table", [], [], 0, False)
    return cases


def registration_main(args: argparse.Namespace) -> int:
    started = time.perf_counter()
    run = args.out.resolve() / (dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%S-%fZ") + "-" + uuid.uuid4().hex[:8])
    run.mkdir(parents=True)
    report = {"testId": TEST_ID, "mode": "registration", "module": "controller", "status": "FAIL",
              "executedThisRun": True, "invocation": sys.argv, "run": str(run),
              "scope": "offline verbatim production gamepadRegisterRawInput + sink creation + family gate/name" +
                       (" + rogRawInputRegistrationEvidence readback" if args.registration_readback else ""),
              "parentGate": None, "automaticRegistration": False, "plannedCases": 17,
              "reuseSearch": ["reuse rog_rawinput_parser_selftest.py/.cpp extraction/compiler/evidence entry",
                              "legion_motion_consumer extracts gamepadRegisterRawInput but lacks ROG cold/sink diagnostics",
                              "input_owner_resident_selftest is static; MSI receipts tests a different observer",
                              "historical RegisterRawInputConflict exercises real OS registration, outside this offline-only scope"],
              "resourceClass": "one-shot owned MSVC and fixture children; no product/Host/device processes",
              "timeoutsSeconds": {"toolchain": 30, "compileEach": 120, "executeEach": 60, "importsEach": 30},
              "runner": identity(Path(__file__)), "fixture": identity(ROOT / "tools/rog_rawinput_parser_selftest.cpp"),
              "python": identity(Path(sys.executable)),
              "limits": ["identity resolver is a deterministic in-memory seam; no DMI/WMI/device reads",
                         "real resolver tail msiNativeForegroundIdentityReady and possible MSI observer message are not executed",
                         "this proves registration ordering only, not zero timing impact on other families or a purely read-only full call chain",
                         "real window creation and OS input registration are simulated and imports-audited",
                         "no WM_INPUT dispatch, HID report capture, FSE routing or physical-device acceptance",
                         "only this selected registration scope executes; no global gate, build or deployment",
                         "R34 dispatch counters exist only while detailed logging is enabled; dispatch/counter wiring is not executed",
                         "registration getter is a non-transactional current-process snapshot and never repairs registration policy",
                         "existing raw-window cadence/logging admission is not executed; this is diagnostic enhancement, not a physical fix"]}
    try:
        report["toolSnapshots"] = {}
        for key, path, name in (("runner", Path(__file__), "runner.snapshot.py"),
                                ("fixture", ROOT / "tools/rog_rawinput_parser_selftest.cpp", "fixture.snapshot.cpp")):
            (run / name).write_bytes(path.read_bytes())
            report["toolSnapshots"][key] = identity(run / name)
            if report["toolSnapshots"][key]["sha256"] != report[key]["sha256"]:
                raise RuntimeError(f"{key} changed before evidence snapshot")
        original_cases = registration_cases()
        readback_cases = registration_readback_cases() if args.registration_readback else []
        cases = original_cases + readback_cases
        report["registrationCases"] = len(original_cases)
        report["readbackCases"] = len(readback_cases)
        report["readbackControlsChangedExpected"] = False if readback_cases else None
        report["plannedCases"] = len(cases)
        input_path = run / "registration-input.jsonl"
        input_path.write_text("".join(json.dumps(c, separators=(",", ":")) + "\n" for c in cases), encoding="utf-8")
        report["caseIds"] = [c["id"] for c in cases]
        report["input"] = identity(input_path)
        report["prepareSeconds"] = time.perf_counter() - started
        vcvars = find_vcvars64(args.vcvars64)
        env = vc_environment(vcvars, run)
        cl = executable("cl.exe", env)
        paths = [vcvars, cl, executable("link.exe", env)]
        paths += [cl.parent / n for n in ("c1xx.dll", "c2.dll", "mspdb140.dll") if (cl.parent / n).is_file()]
        report["toolchain"] = [identity(p) for p in paths]
        report["candidate"] = build_and_run("candidate", args.source, run, env, input_path, cl,
                                             registration=True, registration_readback=args.registration_readback)
        candidate = report["candidate"]
        valid = (candidate["exitCode"] == 0 and candidate["failed"] == 0 and candidate["cases"] == len(cases)
                 and candidate["extractedFunctionsUnchanged"] and candidate["importsAudit"]["forbidden"] == [])
        if args.baseline:
            # Old sources have no readback helper; keep their original 17-case contract.
            baseline_input = run / "baseline-registration-input.jsonl"
            baseline_input.write_text("".join(json.dumps(c, separators=(",", ":")) + "\n" for c in original_cases), encoding="utf-8")
            report["baseline"] = build_and_run("baseline", args.baseline, run, env, baseline_input, cl,
                                               baseline=True, registration=True)
            old = report["baseline"]
            rows = [json.loads(line) for line in (run / "baseline/results.jsonl").read_text(encoding="utf-8").splitlines()]
            cold = next(r for r in rows if r["id"] == "registration.cold-unknown-to-AsusRogAllyX")
            # Require a behavioral wrong target and missing resolver invocation, independently
            # of newly added diagnostic fields. The delayed resolver cannot repair the old OS call.
            report["negativeControlProven"] = (
                old["exitCode"] == 1 and old["cases"] == len(original_cases) and old["extractedFunctionsUnchanged"]
                and "gamepad-target" in cold["failures"] and "identity-resolver-call" in cold["failures"]
                and cold["beforeIdentity"]["family"] == "unknown"
                and cold["registrationIdentity"]["family"] == "unknown"
                and cold["lateIdentity"]["family"] == "asus-rog-ally-x"
                and cold["registrations"][0]["devices"][0]["target"] == cold["mainHwnd"]
                and old["importsAudit"]["forbidden"] == [])
            report["negativeControlCase"] = cold
            valid = valid and report["negativeControlProven"]
        else:
            report["negativeControlProven"] = None
            report["negativeControlStatus"] = "NOT_RUN"
        report["toolFilesUnchanged"] = all(identity(Path(report[k]["path"]))["sha256"] == report[k]["sha256"]
                                           for k in ("runner", "fixture"))
        valid = valid and report["toolFilesUnchanged"]
        report["status"] = "PASS_SELECTED_SCOPE" if valid else "FAIL"
    except Exception as error:
        report["error"] = f"{type(error).__name__}: {error}"
    report["totalSeconds"] = time.perf_counter() - started
    write_json(run / "result.json", report)
    print(json.dumps({"status": report["status"], "result": str(run / "result.json"),
                      "totalSeconds": report["totalSeconds"], "error": report.get("error")}))
    return 0 if report["status"] == "PASS_SELECTED_SCOPE" else 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--source", type=Path, default=ROOT / "native/main.cpp")
    parser.add_argument("--reports", type=Path, help="unaltered R25 worker.jsonl")
    parser.add_argument("--out", type=Path, required=True, help="evidence parent; each execution creates a unique child")
    parser.add_argument("--baseline", type=Path, help="old source negative control: neutral axes in replay mode; cold-start wrong target in registration mode")
    parser.add_argument("--vcvars64", type=Path)
    parser.add_argument("--batch", action="store_true", help="also extract and execute actual rogCaptureRawInput with OS shims")
    parser.add_argument("--registration", action="store_true",
                        help="execute actual registration/sink/family functions with simulated OS APIs only")
    parser.add_argument("--registration-readback", action="store_true",
                        help="also execute rogRawInputRegistrationEvidence with fixed-capacity query/window shims")
    args = parser.parse_args()
    if args.registration_readback and not args.registration:
        parser.error("--registration-readback requires --registration")
    if args.registration and args.batch:
        parser.error("--registration and --batch are separate bounded modes")
    if args.registration:
        return registration_main(args)
    if args.reports is None:
        parser.error("parser replay requires --reports (registration mode does not)")
    started = time.perf_counter()
    run = args.out.resolve() / (dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%S-%fZ") + "-" + uuid.uuid4().hex[:8])
    run.mkdir(parents=True)
    report = {"testId": TEST_ID, "status": "FAIL", "executedThisRun": True,
              "scope": "offline extracted production parser + recorded HidP values; no hardware/runtime acceptance",
              "resourceClass": "one-shot owned compiler and fixture child processes; synchronous bounded exit",
              "invocation": sys.argv, "run": str(run), "parentGate": None, "automaticRegistration": False,
              "reuseSearch": "existing ROG discovery/adapter tests and controller minimum IDs do not execute this parser",
              "timeoutsSeconds": {"toolchain": 30, "compileEach": 120, "replayEach": 60},
              "runner": identity(Path(__file__)), "fixture": identity(ROOT / "tools/rog_rawinput_parser_selftest.cpp"),
              "python": identity(Path(sys.executable)), "reports": identity(args.reports),
              "limits": ["HidP shim supplies recorded values, not a raw HID descriptor decoder",
                         "only A and D-pad button expectations; other button mappings remain unvalidated",
                         "real capture admission, live FSE routing and output consumption are outside this offline test"]}
    try:
        cases, provenance, history = historical_cases(args.reports)
        historical = cases[:]
        cases.extend(boundary_cases())
        if args.batch:
            cases.extend(batch_cases(historical))
        report["batchCoverage"] = {"executed": args.batch, "cases": 7 if args.batch else 0,
                                   "productionFunction": "rogCaptureRawInput" if args.batch else None}
        report["history"] = history
        report["plannedCases"] = len(cases)
        report["parserCases"] = sum(c["phase"] != "batch" for c in cases)
        input_path = run / "replay-input.jsonl"
        input_path.write_text("".join(json.dumps(c, separators=(",", ":")) + "\n" for c in cases), encoding="utf-8")
        (run / "historical-provenance.jsonl").write_text("".join(json.dumps(p, separators=(",", ":")) + "\n" for p in provenance), encoding="utf-8")
        report["input"] = identity(input_path)
        report["provenance"] = identity(run / "historical-provenance.jsonl")
        report["prepareSeconds"] = time.perf_counter() - started
        vcvars = find_vcvars64(args.vcvars64)
        env = vc_environment(vcvars, run)
        cl = executable("cl.exe", env)
        tool_paths = [vcvars, cl, executable("link.exe", env)]
        for name in ("c1xx.dll", "c2.dll", "mspdb140.dll"):
            path = cl.parent / name
            if path.is_file(): tool_paths.append(path)
        report["toolchain"] = [identity(path) for path in tool_paths]
        report["candidate"] = build_and_run("candidate", args.source, run, env, input_path, cl, batch=args.batch)
        candidate = report["candidate"]
        valid = (candidate["exitCode"] == 0 and candidate["failed"] == 0 and
                 candidate["cases"] == len(cases) and candidate["historical"] == 7821 and
                 candidate["shimErrors"] == 0 and candidate["extractedFunctionsUnchanged"])
        if args.baseline:
            report["baseline"] = build_and_run("baseline", args.baseline, run, env, input_path, cl, True, args.batch)
            old = report["baseline"]
            report["negativeControlProven"] = (old["exitCode"] == 1 and old["neutralAxisFailures"] > 0 and
                old["failedByPhase"].get("historical", 0) > 0 and old["cases"] == len(cases) and
                old["historical"] == 7821 and old["shimErrors"] == 0 and old["extractedFunctionsUnchanged"])
            valid = valid and report["negativeControlProven"]
        else:
            report["negativeControlProven"] = None
            report["negativeControlStatus"] = "NOT_RUN"
        report["status"] = "PASS_SELECTED_SCOPE" if valid else "FAIL"
    except Exception as error:
        report["error"] = f"{type(error).__name__}: {error}"
    report["totalSeconds"] = time.perf_counter() - started
    write_json(run / "result.json", report)
    print(json.dumps({"status": report["status"], "result": str(run / "result.json"),
                      "totalSeconds": report["totalSeconds"], "error": report.get("error")}))
    return 0 if report["status"] == "PASS_SELECTED_SCOPE" else 1


if __name__ == "__main__":
    raise SystemExit(main())
