"""Compile exact production valve writers/readers with deterministic bounded I/O.

The fixture preserves the ROG 20261023-125040 source functions verbatim. The
current functions are extracted afresh from native/main.cpp. Only the compiler
and owned offline executable run; the field failure's exact guard subreason is
not established by this regression. Run with --output <fresh external folder>.
"""
from pathlib import Path
import argparse
import hashlib
import importlib.util
import json
import os
import re
import subprocess
import sys

FROZEN_EXPORT = "Build/Exports/20261023-125040-quick-install/Mainline/YeManCC-source/YeManCC/native/main.cpp"
FROZEN_HASHES = {'sgBaseName': 'e34697ed7bdbfda6e99c1974c166fefc67909f13ab3f85bb037578cde26eba4b', 'sgNamePatternMatches': '5865b64bb48526d2055e2a0474ba16e9b3edfc016029e68344f8cbe9c225388d', 'sgNameExcludedBy': '079e82fd9ad5cdc62759db35fedae7d7dd2e85d629c7138e04367e545184035b', 'nativeValveTargetStillValid': 'e8743659c37ac7b48d769cf3b3237d9ece7e740aff5076fd92082fa7d5e67a45', 'nativeValveCommitLocked': '38a19a8b895af9deed5f1e30996309d80805ec25f188abff4378b2979fa613b2', 'nativeValveTouchLocked': 'be8f7d16833fa78189fe72c1d0b1252df27c7a75291954d4bd1aea12bd15f4df', 'nativeValveClearLocked': '22ee79767dc02d0a934a28de51c686a86c42fb2e5d5113862dc59f8229644fd4', 'nativeValveAcquire': 'e63da8ca50f1415d50d9981be401cadd9d8f378f8fa87c0d902c43d8a9dc0abe', 'nativeValveReadCurrentTarget': '21c787e9c546398d109b9fe8b513692fbf40847527ffd135820fd3f8a78b9578', 'NativeGameValveSnapshot': '707dde7fbc5a50afb9d471d9200c5795e6c1261c80a375b8b101dbc954a953ef'}
FUNCTIONS = [name for name in FROZEN_HASHES if name != "NativeGameValveSnapshot"]

def sha(text):
    return hashlib.sha256(text.encode("utf-8")).hexdigest()

def replace_once(source, old, new, label):
    if source.count(old) != 1:
        raise RuntimeError("semantic control anchor missing/ambiguous: " + label)
    return source.replace(old, new)

def valve_definition(extractor, source, name):
    if name != "nativeValveAcquire":
        return extractor.function(source, name)
    masked = extractor.masked_cpp(source)

    def end_of_group(start, opening, closing):
        depth = 1
        for index in range(start + 1, len(masked)):
            ch = masked[index]
            depth += (ch == opening) - (ch == closing)
            if depth == 0:
                return index + 1
        raise RuntimeError("unterminated nativeValveAcquire definition")

    definitions = []
    for match in re.finditer(r"^static NativeDetectedGame nativeValveAcquire\s*\(", masked, re.MULTILINE):
        parameters_end = end_of_group(match.end() - 1, "(", ")")
        body = re.match(r"\s*\{", masked[parameters_end:])
        if body:
            end = end_of_group(parameters_end + body.end() - 1, "{", "}")
            definitions.append(source[match.start():end])
    if len(definitions) != 1:
        raise RuntimeError("nativeValveAcquire definition missing or ambiguous")
    # The default admission = {} is part of the original signature.
    return definitions[0]


def overlay_qualification(extractor, main):
    caller = extractor.function(main, "gameOverlayTryBegin")
    call = "if(gameContext&&!fse)gameContext=gameOverlayQualifyCapturedGame(target);"
    if caller.count(call) != 1 or "nativeDetectGame(" in caller:
        raise RuntimeError("overlay caller is no longer the local qualification path")
    focus = extractor.extract(main, r"\bstruct\s+FocusTargetSnapshot\s*\{", True)
    qualify = extractor.function(main, "gameOverlayQualifyCapturedGame")
    detect = extractor.function(main, "nativeDetectGame")
    old = '''static bool overlayOldControl(const FocusTargetSnapshot& target){
    bool gameContext=true; bool fse=false;
    if(gameContext&&!fse){
        const auto game=nativeDetectGame(target.pid);
        if(game.pid!=target.pid||game.processCreated!=target.processCreated||_wcsicmp(game.path.c_str(),target.path.c_str())!=0)gameContext=false;
    }
    return gameContext;
}'''
    return focus + "\nnamespace production {\n" + qualify + "\n" + detect + "\n" + old + "\n}", sha(caller)


def build_environment():
    env = {k: v for k, v in os.environ.items() if k.upper() not in ("CL", "_CL_", "LINK", "_LINK_")}
    vc = Path(os.environ.get("VCToolsInstallDir", r"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207"))
    sdk = Path(os.environ.get("WindowsSdkDir", r"C:\Program Files (x86)\Windows Kits\10"))
    sdk_version = os.environ.get("WindowsSDKVersion", "10.0.26100.0").strip("/\\")
    compiler = vc / "bin/Hostx64/x64/cl.exe"
    if not compiler.is_file():
        raise RuntimeError("MSVC unavailable: " + str(compiler))
    env["INCLUDE"] = ";".join(str(p) for p in (vc / "include", sdk / "Include" / sdk_version / "um", sdk / "Include" / sdk_version / "shared", sdk / "Include" / sdk_version / "ucrt"))
    env["LIB"] = ";".join(str(p) for p in (vc / "lib/x64", sdk / "Lib" / sdk_version / "um/x64", sdk / "Lib" / sdk_version / "ucrt/x64"))
    env["PATH"] = str(compiler.parent) + os.pathsep + env.get("PATH", "")
    return compiler, env

def run(output, config_only=False, normal_only=False, identity_path_only=False, empty_only=False, overlay_only=False):
    if sum(map(bool, (config_only, normal_only, identity_path_only, empty_only, overlay_only))) > 1:
        raise RuntimeError("case selections are mutually exclusive")
    repo = Path(__file__).resolve().parents[1]
    out = Path(output).resolve()
    if out == repo or repo in out.parents or out in repo.parents:
        raise RuntimeError("fresh output outside source tree required")
    if out.exists() and any(out.iterdir()):
        raise RuntimeError("output is not fresh")
    out.mkdir(parents=True, exist_ok=True)
    spec = importlib.util.spec_from_file_location("valve_extract", repo / "tools/steam_original_recovery_authority_selftest.py")
    extractor = importlib.util.module_from_spec(spec)
    sys.dont_write_bytecode = True
    spec.loader.exec_module(extractor)
    main_path = repo / "native/main.cpp"
    main_raw = main_path.read_bytes()
    main = main_raw.decode("utf-8-sig").replace("\r\n", "\n")
    fixture_path = repo / "tools/fixtures/steam_original_recovery_valve_guard/concurrency-cases.cpp.in"
    template = fixture_path.read_text(encoding="utf-8")
    frozen = template.split("// BEGIN PRESERVED ROG 20261023-125040 FUNCTIONS\n", 1)[1].split("// END PRESERVED ROG 20261023-125040 FUNCTIONS", 1)[0]
    for name in FUNCTIONS:
        if sha(extractor.function(frozen, name)) != FROZEN_HASHES[name]:
            raise RuntimeError("preserved ROG production function drift: " + name)
    struct_pattern = r"\bstruct\s+NativeGameValveSnapshot\s*\{"
    if sha(extractor.extract(frozen, struct_pattern, True)) != FROZEN_HASHES["NativeGameValveSnapshot"]:
        raise RuntimeError("preserved ROG snapshot structure drift")
    functions = {name: valve_definition(extractor, main, name) for name in FUNCTIONS}
    functions["nativeGameTargetJson"] = extractor.function(main, "nativeGameTargetJson")
    functions["nativeValveValidateCurrentTarget"] = extractor.function(main, "nativeValveValidateCurrentTarget")
    functions["focusLightNoGameQualified"] = extractor.function(main, "focusLightNoGameQualified")
    publisher = extractor.function(main, "nativeValvePublish")
    discovery_patterns = {
        name: r"\benum\s+class\s+" + name + r"\s*\{"
        for name in ("NativeGameDiscoveryState", "NativeOnePidResult")
    }
    discovery_types = {name: extractor.extract(main, pattern, True) for name, pattern in discovery_patterns.items()}
    discovery_name = extractor.function(main, "nativeGameDiscoveryStateName")
    json_header = repo / "deps/json/json.hpp"
    (out / "json.hpp").write_bytes(json_header.read_bytes())
    snapshot = extractor.extract(main, struct_pattern, True)
    game = extractor.extract(main, r"\bstruct\s+NativeDetectedGame\s*\{", True)
    if "identityGeneration" not in snapshot:
        raise RuntimeError("production identityGeneration patch missing; baseline reproduction remains preserved")
    identity_path = repo / "native/monitor_identity.h"
    identity = identity_path.read_bytes()
    (out / "monitor_identity.h").write_bytes(identity)
    literal_path = repo / "native/game_rule_literal.h"
    literal = literal_path.read_bytes()
    (out / "game_rule_literal.h").write_bytes(literal)
    overlay_source, overlay_caller_sha, overlay_cases = "", None, ""
    overlay_path = fixture_path.with_name("overlay-cases.cpp.in")
    if overlay_only:
        overlay_source, overlay_caller_sha = overlay_qualification(extractor, main)
        overlay_cases = overlay_path.read_text(encoding="utf-8")
    compiler, env = build_environment()
    variants = [("positive", None, None, None)]
    read = "nativeValveReadCurrentTarget"
    commit = "nativeValveCommitLocked"
    variants += [
        ("reader-reuses-publication-generation", read, "g_gameValveTarget.identityGeneration != snapshot.identityGeneration", "g_gameValveTarget.generation != snapshot.generation"),
        ("reader-ignores-identity-lease", read, "g_gameValveTarget.identityGeneration != snapshot.identityGeneration", "false"),
        ("reader-ignores-late-global-rules", read, "g_gameRulesEpoch.load(std::memory_order_acquire) != rulesEpoch", "false"),
        ("reader-ignores-final-path", read, "_wcsicmp(g_gameValveTarget.game.path.c_str(), snapshot.game.path.c_str()) != 0", "false"),
        ("touch-revokes-identity", "nativeValveTouchLocked", "g_gameValveTarget.generation = ++g_gameValveGeneration;", "g_gameValveTarget.generation = ++g_gameValveGeneration;\n    g_gameValveTarget.identityGeneration = g_gameValveTarget.generation;"),
        ("commit-keeps-old-identity", commit, "if (!sameSelection || !g_gameValveTarget.identityGeneration)", "if (!g_gameValveTarget.identityGeneration)"),
        ("clear-loses-identity-advance", "nativeValveClearLocked", "g_gameValveTarget.identityGeneration = g_gameValveTarget.generation;", "g_gameValveTarget.identityGeneration = 0;"),
    ]
    variants += [
        ("response-drops-captured-snapshot", "nativeValveAcquire", "if (observation) *observation = published;", "if (observation) *observation = {};"),
        ("response-drops-identity-generation", "nativeGameTargetJson", '{"identityGeneration", target.identityGeneration}', '{"identityGeneration", 0}'),
    ]
    for field in ("path", "rules", "customConfigured", "whitelisted", "whitelistRule"):
        condition = {
            "path": "_wcsicmp(g_gameValveTarget.game.path.c_str(), game.path.c_str()) == 0",
            "rules": "g_gameValveTarget.rulesEpoch == rulesEpoch",
        }.get(field, "g_gameValveTarget.game." + field + " == game." + field)
        variants.append(("commit-ignores-" + field, commit, condition, "true"))
    results = []
    if overlay_only:
        variants = [("overlay-qualification", None, None, None)]
    if empty_only:
        variants = [("empty-observations", None, None, None),
                    ("empty-ignore-rules", "nativeValveAcquire",
                     "g_gameValveTarget.qualificationRulesEpoch != rulesEpoch", "false")]
    elif identity_path_only:
        variants = [("identity-path-recovery", None, None, None)]
    elif config_only:
        variants = [("config-read-errors", None, None, None)]
    elif normal_only:
        variants = [("positive", None, None, None)]
    for name, function, old, new in variants:
        case_dir = out / name
        case_dir.mkdir()
        selected = dict(functions)
        if function:
            selected[function] = replace_once(selected[function], old, new, name)
        code = '#include "game_rule_literal.h"\n' + template.replace("@@DISCOVERY_TYPES@@", "\n\n".join(discovery_types.values()) + "\n\n" + discovery_name).replace("@@GAME_STRUCT@@", game).replace("@@CURRENT_STRUCT@@", snapshot).replace("@@CURRENT_PUBLISHER@@", publisher).replace("@@CURRENT_FUNCTIONS@@", "\n\n".join(selected.values()))
        code = code.replace("@@OVERLAY_QUALIFICATION@@", overlay_source).replace("@@OVERLAY_CASES@@", overlay_cases)
        if re.search(r"@@[A-Z_]+@@", code):
            raise RuntimeError("unexpanded fixture token")
        (case_dir / "fixture.cpp").write_text(code, encoding="utf-8")
        item = {"name": name, "sourceSha256": sha(code), "expectedReturnCode": 0 if not function else 1}
        try:
            command = [str(compiler), "/nologo", "/std:c++20", "/Od" if identity_path_only else "/O2", "/EHsc", "/MT", "/utf-8", "fixture.cpp", "/I" + str(out), "/Fe:fixture.exe"]
            if overlay_only:
                command[1:1] = ["/W4", "/WX", "/DYMCC_TEST_OVERLAY"]
            built = subprocess.run(command, cwd=case_dir, env=env, capture_output=True, timeout=60)
            (case_dir / "compile.stdout").write_bytes(built.stdout)
            (case_dir / "compile.stderr").write_bytes(built.stderr)
            item["compileReturnCode"] = built.returncode
            if built.returncode == 0:
                ran = subprocess.run([str(case_dir / "fixture.exe")] + (["--overlay-qualification-only"] if overlay_only else ["--empty-observation-only"] if empty_only else ["--identity-path-recovery-only"] if identity_path_only else ["--config-read-errors-only"] if config_only else []), cwd=case_dir, env=env, capture_output=True, timeout=10)
                (case_dir / "run.stdout").write_bytes(ran.stdout)
                (case_dir / "run.stderr").write_bytes(ran.stderr)
                item["returnCode"] = ran.returncode
                item["passed"] = ran.returncode == item["expectedReturnCode"]
                if function:
                    item["semanticRejection"] = ran.stderr.decode("utf-8", errors="replace").strip()
                    item["passed"] = item["passed"] and bool(item["semanticRejection"])
                    if empty_only:
                        raw = json.loads(ran.stdout)
                        rejected = [row for row in raw["cases"] if not row["ok"]]
                        item["result"] = raw
                        item["passed"] = item["passed"] and len(rejected) == 1 and rejected[0]["name"] == "empty qualification rule boundary advances generation" and rejected[0]["error"] == "new rules must publish empty qualification"
                elif ran.returncode == 0:
                    item["result"] = json.loads(ran.stdout)
                if overlay_only:
                    old = subprocess.run([str(case_dir / "fixture.exe"), "--overlay-old-control"], cwd=case_dir, env=env, capture_output=True, timeout=10)
                    (case_dir / "old-control.stdout").write_bytes(old.stdout)
                    (case_dir / "old-control.stderr").write_bytes(old.stderr)
                    error = old.stderr.decode("utf-8", errors="replace").strip()
                    item["oldControl"] = {"exitCode": old.returncode, "error": error}
                    item["passed"] = item["passed"] and item.get("result") == {"cases": 13, "assertions": 65, "result": "PASS"} and old.returncode == 1 and not old.stdout and error == "field alternate foreground rejected: existing game identity was changed"
                if empty_only and ran.stdout:
                    raw = json.loads(ran.stdout)
                    if item.get("result") is None:
                        item["result"] = raw
                    case_source = template.split("void emptyObservations(){", 1)[1].split("\nint main(", 1)[0]
                    expected_cases = re.findall(r'scenario\("([^"]+)"', case_source)
                    actual_cases = [row["name"] for row in raw["cases"]]
                    item["exactSelection"] = actual_cases == expected_cases and raw["caseCount"] == len(expected_cases) and raw["selection"] == "empty-observations"
                    item["passed"] = item["passed"] and item["exactSelection"]
            else:
                item["passed"] = False
        except subprocess.TimeoutExpired as exc:
            item.update(passed=False, timeoutSeconds=exc.timeout)
        results.append(item)
    now = main_path.read_text(encoding="utf-8-sig")
    # Exact unchanged full source already proves every captured callable/type unchanged.
    # Preserve the original region checks when any source text changed.
    drift = []
    if now != main:
        drift = [name for name, value in functions.items() if valve_definition(extractor, now, name) != value]
        if extractor.extract(now, struct_pattern, True) != snapshot:
            drift.append("NativeGameValveSnapshot")
        if extractor.extract(now, r"\bstruct\s+NativeDetectedGame\s*\{", True) != game:
            drift.append("NativeDetectedGame")
        if extractor.function(now, "nativeValvePublish") != publisher:
            drift.append("nativeValvePublish")
        for name, pattern in discovery_patterns.items():
            if extractor.extract(now, pattern, True) != discovery_types[name]:
                drift.append(name)
        if extractor.function(now, "nativeGameDiscoveryStateName") != discovery_name:
            drift.append("nativeGameDiscoveryStateName")
    if identity_path.read_bytes() != identity:
        drift.append("monitor_identity.h")
    if literal_path.read_bytes() != literal:
        drift.append("game_rule_literal.h")
    if fixture_path.read_text(encoding="utf-8") != template:
        drift.append("concurrency-cases.cpp.in")
    if overlay_only:
        if now != main and overlay_qualification(extractor, now) != (overlay_source, overlay_caller_sha):
            drift.append("gameOverlayTryBegin/qualification")
        if overlay_path.read_text(encoding="utf-8") != overlay_cases:
            drift.append("overlay-cases.cpp.in")
    result = {
        "schema": "ymcc.production-valve-concurrency.v1", "passed": all(x.get("passed", False) for x in results) and not drift,
        "variants": results, "productionMain": str(main_path), "productionMainSha256AtStart": sha(main),
        "productionMainRawSha256AtStart": hashlib.sha256(main_raw).hexdigest(),
        "productionExtracts": {name: sha(value) for name, value in functions.items()},
        "snapshotSha256": sha(snapshot), "nativeDetectedGameSha256": sha(game),
        "discoveryExtracts": {**{name: sha(value) for name, value in discovery_types.items()}, "nativeGameDiscoveryStateName": sha(discovery_name)},
        "jsonHeaderSha256": hashlib.sha256(json_header.read_bytes()).hexdigest(),
        "preservedBefore": {"export": FROZEN_EXPORT, "functions": FROZEN_HASHES},
        "publisherSourceSha256": sha(publisher), "publisherActuallyCompiled": True,
        "sourceDrift": drift, "compiler": str(compiler),
        "controlledPorts": ["disk rule refresh", "process identity/session/memory", "game scan result", "publication sink", "clock"],
        "schedule": "one-shot synchronous I/O interleavings at released valve-lock boundaries; no random sleeps",
        "maximumPortStepsPerCase": 256, "compileTimeoutSeconds": 60, "fixtureTimeoutSeconds": 10,
        "realWindowsOrSteamActions": 0, "productWrites": 0,
        "fieldExactGuardSubreasonProven": False, "fieldUniqueRootCauseProven": False,
        "selection": "overlay-qualification" if overlay_only else "empty-observations" if empty_only else "identity-path-recovery" if identity_path_only else "config-read-errors" if config_only else "normal" if normal_only else "original-default-matrix",
    }
    if overlay_only:
        result["overlaySources"] = {"callerSha256": overlay_caller_sha, "compiledDefinitionsSha256": sha(overlay_source), "casesSha256": sha(overlay_cases)}
        result["controlledPorts"] += ["foreground window", "shared power capture/current/revocation"]
    (out / "result.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    return result

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True, help="Fresh directory outside the source tree")
    selections = parser.add_mutually_exclusive_group()
    selections.add_argument("--config-read-errors-only", action="store_true", help="Only original consumer config-failure caller scenarios; no preserved normal/negative matrix rerun")
    selections.add_argument("--normal-only", action="store_true", help="Only preserved original normal consumer, without rerunning unrelated semantic variants")
    selections.add_argument("--identity-path-recovery-only", action="store_true", help="Only the original valve consumer path recovery and heartbeat assertions; no normal or semantic-matrix rerun")
    selections.add_argument("--empty-observation-only", action="store_true", help="Only exact empty-observation cases and their rule-boundary semantic negative")
    selections.add_argument("--overlay-qualification-only", action="store_true", help="Only read-only foreground qualification and its old mutating control")
    args = parser.parse_args()
    result = run(args.output, args.config_read_errors_only, args.normal_only, args.identity_path_recovery_only, args.empty_observation_only, args.overlay_qualification_only)
    print(json.dumps(result, ensure_ascii=False))
    raise SystemExit(0 if result["passed"] else 1)
