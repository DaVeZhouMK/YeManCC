"""Thin planning/worker adapter for the existing typed consumers and owned Job.

Planning never compiles. Execution is entered only by bridge.mjs inside the
existing RawProgram host. No vcvars, alternate test engine, or default gate.
"""
from __future__ import annotations
import hashlib, importlib.util, json, os, re, sys, time
from pathlib import Path
sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
CAP_MS = 300000


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def read(path):
    return json.loads(Path(path).read_text(encoding="utf-8-sig"))


def pin(path):
    path = Path(path).resolve(strict=True)
    raw = path.read_bytes()
    return {"path": str(path), "bytes": len(raw), "sha256": hashlib.sha256(raw).hexdigest()}


def verify(rows):
    for row in rows:
        actual = pin(row["path"])
        # RawProgram's existing stdout/request pins use path+SHA; prepared
        # source rows additionally carry bytes. Always read/hash the actual
        # file, preserving the supplied format rather than inventing a size.
        if (actual["sha256"] != row["sha256"] or
                ("bytes" in row and actual["bytes"] != row["bytes"])):
            raise ValueError("declared input drift: " + row["path"])


def save(path, value):
    path = Path(path)
    with path.open("xb") as stream:
        stream.write((json.dumps(value, ensure_ascii=False, indent=2) + "\n").encode("utf-8"))


def regular_files(root):
    root = Path(root).resolve(strict=True)
    for path in sorted(root.rglob("*")):
        if path.is_symlink() or path.is_junction():
            raise ValueError("runtime reparse member: " + str(path))
        if path.is_file():
            yield path


def selected_toolchain(config):
    """Explicit installed files only; no discovery by PATH or bootstrap script."""
    names = {key: Path(config[key]).resolve(strict=True)
             for key in ("compiler", "linker", "python", "node", "powershell", "cmd")}
    expected = {"compiler": "cl.exe", "linker": "link.exe", "python": "python.exe",
                "node": "node.exe", "powershell": "pwsh.exe", "cmd": "cmd.exe"}
    if any(names[key].name.lower() != name for key, name in expected.items()):
        raise ValueError("explicit native/interpreter executable names required")
    if names["compiler"].parent != names["linker"].parent:
        raise ValueError("compiler/linker must use one explicit x64 tool directory")
    include = [Path(p).resolve(strict=True) for p in config["includeDirectories"]]
    lib = [Path(p).resolve(strict=True) for p in config["libraryDirectories"]]
    if not include or not lib or any(not p.is_dir() for p in include + lib):
        raise ValueError("explicit nonempty INCLUDE/LIB directories required")
    dependencies = []
    for field, roots in (("headers", include), ("libraries", lib)):
        declared = config[field]
        if not declared:
            raise ValueError("predeclare the actual selected " + field + " superset")
        for value in declared:
            path = Path(value).resolve(strict=True)
            if not path.is_file() or not any(path.is_relative_to(root) for root in roots):
                raise ValueError("dependency outside explicit environment: " + str(path))
            dependencies.append(path)
    tools = set(names.values())
    tools.update(regular_files(names["compiler"].parent))
    # Isolated embedded Python only. Every configured path must remain inside
    # its declared runtime; no site-packages/PYTHONPATH dependency is admitted.
    python_root = names["python"].parent
    pth = list(python_root.glob("python*._pth"))
    if len(pth) != 1:
        raise ValueError("explicit isolated embedded Python ._pth required")
    for line in pth[0].read_text(encoding="utf-8-sig").splitlines():
        line = line.strip()
        if line and not line.startswith("#"):
            if line.startswith("import ") or not (python_root / line).resolve().is_relative_to(python_root):
                raise ValueError("Python runtime permits an undeclared import/path")
    tools.update(regular_files(python_root))
    tools.update(p for p in names["node"].parent.iterdir() if p.is_file() and p.suffix.lower() == ".dll")
    ps = names["powershell"].parent
    for root in (ps, ps / "ref"):
        tools.update(p for p in root.iterdir() if p.is_file())
    for module in ("Management", "Utility", "Security"):
        tools.update(regular_files(ps / "Modules" / ("Microsoft.PowerShell." + module)))
    tools.add(names["cmd"].with_name("taskkill.exe"))
    cleanup = None
    if config.get("finalizePinnedVctip") is True:
        tool = pin(names["compiler"].with_name("vctip.exe"))
        cleanup = {"kind": "MSVC_VCTIP_AFTER_ROOT_EXIT", "maximumProcesses": 1,
                   "program": {"path": tool["path"], "sha256": tool["sha256"]}}
    return names, include, lib, sorted(tools), sorted(set(dependencies)), cleanup


def make_plan(prepared, output, config_path, ledger_path, phase, remaining_ms,
              correctness_receipt, api):
    """Pin one opt-in invocation; ledger/runtime admission is rechecked by Node."""
    if phase not in ("correctness", "fixed-cost"):
        raise ValueError("unknown typed execution phase")
    if type(remaining_ms) is not int or not 5000 < remaining_ms <= CAP_MS:
        raise ValueError("supply actual nonzero remaining budget including cleanup reserve")
    prepared, output = Path(prepared).resolve(), Path(output).resolve()
    evidence = (api["WORKSPACE"] / "Testing/YMCC/Evidence").resolve()
    if not output.is_relative_to(evidence) or output == evidence or output.exists():
        raise ValueError("execution plan requires fresh development Evidence output")
    prep = read(prepared / "PREPARATION.json")
    if prep["mode"] != "typed-telemetry-build":
        raise ValueError("wrong prepared mode")
    verify(prep["frozenInputs"])
    contract = api["typed_helpers"]().execution_contract(api)
    if any(prep["executionContract"][key] != contract[key] for key in ("core", "cost")):
        raise ValueError("prepared original selection/budgets/gates changed")
    # Bind today's public adapters to the exact prepared C++ bytes, rather than
    # silently ignoring stale tool pins from an earlier prepare-only invocation.
    regenerated = api["typed_extract"]((prepared / "source-candidate.cpp").read_bytes(),
                                      (prepared / "Core/gyro_telemetry_record.h").read_bytes(),
                                      empty_delivery_header=(prepared / "Core/gyro_telemetry_empty_delivery.h").read_bytes()
                                      if (prepared / "Core/gyro_telemetry_empty_delivery.h").exists() else None)
    for name, data in regenerated["files"].items():
        raw = data.encode("utf-8") if isinstance(data, str) else data
        if (prepared / name).read_bytes() != raw:
            raise ValueError("prepared compile projection changed: " + name)
    config_path, ledger_path = Path(config_path).resolve(), Path(ledger_path).resolve()
    config_pin, ledger_pin = pin(config_path), pin(ledger_path)
    config, ledger = read(config_path), read(ledger_path)
    correctness_scope = config.get("correctnessScope", "full")
    if correctness_scope not in ("full", "parity"):
        raise ValueError("unknown exact correctness scope")
    if phase != "correctness" and correctness_scope != "full":
        raise ValueError("partial correctness cannot qualify fixed-cost execution")
    if ledger.get("phase") != phase or ledger.get("capMs") != CAP_MS or ledger.get("remainingMs") != remaining_ms or not isinstance(ledger.get("entries"), list):
        raise ValueError("supplied cumulative ledger/cap/remaining budget differs")
    names, include, lib, tools, dependencies, cleanup = selected_toolchain(config)
    if phase == "fixed-cost" and correctness_receipt is None:
        raise ValueError("fixed-cost requires a completed owned public correctness receipt for these compile inputs")
    receipt_pin = pin(correctness_receipt) if correctness_receipt else None
    output.mkdir(parents=True, exist_ok=False)
    (output / "Temp").mkdir(exist_ok=False)
    parity_path = api["REPO"] / "tools/virtual_gyro_parity_gate_selftest.py"
    parity = load("_typed_public_parity_prepare", parity_path)
    parity_prep = parity.prepare_typed(prepared / "source-candidate.cpp",
        prepared / "Core/gyro_telemetry_record.h", output / "Parity", json_header=prepared / "Core/json.hpp")
    raw = api["REPO"] / "tools/ymcc_sim/global/decky-persistent-lifecycle/raw-owned-producer.mjs"
    host = api["REPO"] / "tools/ymcc_sim/global/host-process.ps1"
    sources = {Path(row["path"]) for row in prep["frozenInputs"]}
    sources.update((prepared / "PREPARATION.json", api["TOOL"], api["HARNESS"],
                    Path(__file__).resolve(), HERE / "bridge.mjs", HERE / "prepare.py",
                    HERE / "json_oracle.json", HERE / "lifecycle.inc", HERE / "opaque_sink.cpp",
                    parity_path, parity_path.with_suffix(".cpp"), raw, host, config_path, ledger_path))
    sources.update(dependencies)
    sources.update(p for p in (output / "Parity").rglob("*") if p.is_file())
    for entry in ledger["entries"]:
        for key in ("process", "intake"):
            verify([entry[key]])
            sources.add(Path(entry[key]["path"]))
        process = read(entry["process"]["path"])
        for row in [process["stdout"], process["stderr"],
                    *[process["rawExecution"][key] for key in ("terminal", "request", "arguments", "helperSource")]]:
            verify([row])
            sources.add(Path(row["path"]))
    if receipt_pin:
        sources.add(Path(receipt_pin["path"]))
        receipt = read(receipt_pin["path"])
        for row in [receipt["process"], receipt["workerResult"], *receipt["artifacts"]]:
            verify([row])
            sources.add(Path(row["path"]))
        process = read(receipt["process"]["path"])
        for row in [process["stdout"], process["stderr"],
                    *[process["rawExecution"][key] for key in ("terminal", "request", "arguments", "helperSource")]]:
            verify([row])
            sources.add(Path(row["path"]))
    compile_inputs = {name: pin(prepared / name)["sha256"] for name in sorted(regenerated["files"])}
    for name in ("Core/json.hpp", "Core/controller-persona.h", "Core/ipc_result_mailbox.h", "Core/host-inputs.json",
                 "Cost/json.hpp", "Cost/controller-persona.h", "Cost/ipc_result_mailbox.h", "Cost/gyro_telemetry_record.h", "Cost/host-inputs.json"):
        compile_inputs[name] = pin(prepared / name)["sha256"]
    for name, value in parity_prep["outputs"].items():
        compile_inputs["Parity/" + name] = value["sha256"]
    # Ambient compiler options are absent; there is no vcvars bootstrap command
    # or batch-script dependency to discover after a business launch.
    system = names["cmd"].parent.parent
    env = {key: os.environ[key] for key in ("USERPROFILE", "LOCALAPPDATA", "APPDATA", "ProgramFiles", "ProgramFiles(x86)", "ProgramW6432") if key in os.environ}
    env.update({"SystemRoot": str(system), "WINDIR": str(system), "SystemDrive": system.drive,
        "COMSPEC": str(names["cmd"]), "PATH": ";".join(map(str, (system / "System32", system, names["compiler"].parent))),
        "INCLUDE": ";".join(map(str, include)), "LIB": ";".join(map(str, lib)), "LIBPATH": "", "VSLANG": "1033",
        "PYTHONNOUSERSITE": "1", "PSModulePath": str(names["powershell"].parent / "Modules"),
        "TEMP": str(output / "Temp"), "TMP": str(output / "Temp")})
    plan = {"kind": "typed-hostframe-owned-execution-plan-v1", "phase": phase, "capMs": CAP_MS,
        "correctnessScope": correctness_scope,
        "remainingMs": remaining_ms, "ledger": ledger_pin, "config": config_pin,
        "prepared": str(prepared), "directory": str(output), "runner": str(api["TOOL"]),
        "worker": str(Path(__file__).resolve()), "bridge": str(HERE / "bridge.mjs"),
        "rawModule": str(raw), "host": str(host), "tools": {key: str(value) for key, value in names.items()},
        "environment": env, "toolCleanupPolicy": cleanup,
        "sources": [pin(p) for p in sorted(sources)], "toolArtifacts": [pin(p) for p in tools],
        "compileInputs": compile_inputs, "correctnessReceipt": receipt_pin,
        "core": contract["core"], "parity": parity_prep,
        "cost": contract["cost"],
        "compileExecuted": False, "businessExecuted": False, "runtimeExecuted": False,
        "sourceReady": False, "newGlobalIds": [], "newParentGate": False}
    verify([config_pin, ledger_pin])
    verify(plan["sources"] + plan["toolArtifacts"])
    save(output / "PLAN.json", plan)
    return plan


def check_core(report, variant, contract):
    failure = variant["expectedFailure"]
    if failure is not None:
        if report.get("pass") is not False or report.get("failure") != failure:
            raise ValueError("wrong core semantic rejection")
    elif (report.get("pass") is not True or report.get("scopeVersion") != "current-tree-build-v1"
          or report.get("typedPipelineExecuted") is not True or report.get("cases") != contract["positiveCases"]
          or [row["name"] for row in report.get("typedLifecycle", [])] != contract["lifecycleCases"]):
        raise ValueError("wrong complete typed core selection")


def check_parity(report, expected):
    for key, total in (("correctness", expected["originalCases"]), ("typedCorrectness", expected["typedCases"]),
                       ("formatterBoundaries", expected["formatterCases"]), ("lazyBroadcastBoundaries", expected["lazyCases"])):
        if report[key]["pass"] is not True or report[key]["cases"] != total:
            raise ValueError("wrong parity selection: " + key)
    ownership = report["typedOwnership"]
    if not (ownership["pass"] and ownership["owningJsonNodes"] > 0 and ownership["owningLongStrings"] == 3
            and ownership["allocationGenerationsCompared"] and not ownership["oracleTreeAddressesUsedAsOwnershipProof"]
            and report["performance"] is None):
        raise ValueError("parity ownership/performance boundary changed")


def worker(plan_path, deadline_ms):
    if os.name != "nt":
        raise ValueError("Windows native worker required")
    plan = read(plan_path)
    correctness_scope = plan.get("correctnessScope", "full")
    if (correctness_scope not in ("full", "parity") or
            correctness_scope != read(plan["config"]["path"]).get("correctnessScope", "full") or
            (plan["phase"] != "correctness" and correctness_scope != "full")):
        raise ValueError("worker exact correctness scope differs")
    module = load("_typed_existing_hostframe_runner", plan["runner"])
    contract = module.typed_helpers().execution_contract(vars(module))
    if any(plan[key] != contract[key] for key in ("core", "cost")):
        raise ValueError("worker original selection/budgets/gates differ")
    parity_api = load("_typed_worker_parity_contract", Path(plan["runner"]).with_name("virtual_gyro_parity_gate_selftest.py"))
    expected_variants = [{"name": name, "fixture": ("" if name == "Parity" else name + "/") + "fixture.cpp",
        "expectedFailures": parity_api.TYPED_FAILURES[name], "includeDirectories": ["."], "runArgv": ["0"],
        "expectedExitCode": 0 if name == "Parity" else 2} for name in parity_api.TYPED_VARIANTS]
    if plan["parity"]["variants"] != expected_variants or plan["parity"]["expected"] != {
            "originalCases": 2829, "typedCases": 2829, "formatterCases": 289, "lazyCases": 62}:
        raise ValueError("worker parity selection differs from public prepare API")
    stage, prepared = Path(plan["directory"]) / "Execution", Path(plan["prepared"])
    stage.mkdir(exist_ok=False)
    (stage / "Temp").mkdir(exist_ok=False)
    started = time.perf_counter()
    records, reports = [], []
    declared = {os.path.normcase(str(Path(row["path"]).resolve())): row for row in plan["sources"]}
    discovered_headers, discovered_libraries = set(), set()
    deadline = deadline_ms / 1000 - 5  # Existing outer Job retains its cleanup reserve.

    def verify_scope():
        selected = selected_toolchain(read(plan["config"]["path"]))
        if {str(p) for p in selected[3]} != {row["path"] for row in plan["toolArtifacts"]}:
            raise ValueError("selected tool runtime membership changed")
        verify(plan["sources"] + plan["toolArtifacts"])

    def invoke(command, label, allowed=(0,), timeout=180):
        remaining = deadline - time.time()
        if remaining <= 0:
            raise TimeoutError("supplied remaining phase budget exhausted")
        out, process = module.tree_invoke(command, stage, label, plan["environment"], min(timeout, remaining), allowed)
        records.append(pin(stage / (label + ".PROCESS.json")))
        return out, process

    def discover(label, source):
        dep = read(stage / (label + ".dependencies.json"))["Data"]
        if Path(dep["Source"]).resolve() != source.resolve():
            raise ValueError("compiler source dependency differs")
        for path in dep["Includes"]:
            discovered_headers.add(str(Path(path).resolve()))
        text = (stage / (label + "-build.stdout.log")).read_text(encoding="utf-8", errors="replace")
        for match in re.finditer(r"([A-Za-z]:\\[^\r\n]*?\.lib)(?=[:\s]|$)", text, re.I):
            path = Path(match.group(1).strip())
            if path.is_file():
                discovered_libraries.add(str(path.resolve()))
        actual = discovered_headers | discovered_libraries
        unknown = actual - {row["path"] for row in declared.values()}
        # Match Windows path identity, without a caller-provided skip list.
        unknown = [path for path in unknown if os.path.normcase(path) not in declared]
        if unknown:
            raise ValueError("undeclared compiler/linker dependencies: " + repr(unknown))
        verify([declared[os.path.normcase(path)] for path in actual])

    def build(label, source, includes, flags, defines=(), extra=(), compile_only=False):
        obj, exe = stage / (label + ".obj"), stage / (label + ".exe")
        args = [plan["tools"]["compiler"], "/nologo", "/std:c++20", "/utf-8", "/EHsc", "/fp:precise",
                *flags, *defines, *(["/c"] if compile_only else []), str(source), *map(str, extra),
                *["/I" + str(p) for p in includes], "/Fo" + str(obj),
                *([] if compile_only else ["/Fe" + str(exe)]),
                "/sourceDependencies", str(stage / (label + ".dependencies.json")),
                *([] if compile_only else ["/link", "/INCREMENTAL:NO", "/VERBOSE:LIB"])]
        if any(value.upper().startswith(("/GL", "/LTCG")) for value in args):
            raise ValueError("opaque cost contract forbids IR/LTCG")
        invoke(args, label + "-build")
        discover(label, source)
        if obj.read_bytes()[:2] != b"\x64\x86":
            raise ValueError("expected ordinary x64 COFF, not compiler IR")
        return exe, obj

    try:
        verify_scope()
        if plan["phase"] == "fixed-cost":
            receipt = read(plan["correctnessReceipt"]["path"])
            if receipt.get("status") != "OWNED_TYPED_CORRECTNESS_PASS" or receipt.get("compileInputs") != plan["compileInputs"]:
                raise ValueError("fixed-cost requires exact same-input owned core+parity acceptance")
            verify(receipt["artifacts"])
        for profile in plan["cost"]["profiles"]:
            flags = ["/O2", "/DNDEBUG", "/MT"] if profile == "Release" else ["/Od", "/RTC1", "/D_DEBUG", "/D_ITERATOR_DEBUG_LEVEL=2", "/MTd"]
            if plan["phase"] == "correctness":
                for variant in (plan["core"]["variants"] if correctness_scope == "full" else []):
                    label = profile + "-Core-" + variant["name"]
                    exe, _ = build(label, prepared / variant["fixture"], [prepared / "Core"], flags,
                        ["/DYMCC_CURRENT_TREE_BUILD", *variant["defines"]])
                    command = "negative" if variant["name"] in ("BadValue", "BadType") else "correctness"
                    stdout, _ = invoke([str(exe), str(prepared / "Core/host-inputs.json"), command], label + "-verify",
                        (0,) if variant["expectedFailure"] is None else (1,), 60)
                    report = json.loads(stdout.decode("utf-8"))
                    check_core(report, variant, plan["core"])
                    reports.append({"profile": profile, "kind": "core", "variant": variant["name"], "report": report})
                for variant in plan["parity"]["variants"]:
                    label = profile + "-Parity-" + variant["name"]
                    root = Path(plan["directory"]) / "Parity"
                    exe, _ = build(label, root / variant["fixture"], [root], flags)
                    stdout, _ = invoke([str(exe), *variant["runArgv"]], label + "-verify", (variant["expectedExitCode"],), 60)
                    if variant["expectedFailures"]:
                        report = {"failure": (stage / (label + "-verify.stderr.log")).read_text(encoding="utf-8").strip()}
                        if report["failure"] not in variant["expectedFailures"]:
                            raise ValueError("wrong parity semantic rejection")
                    else:
                        report = json.loads(stdout.decode("utf-8"))
                        check_parity(report, plan["parity"]["expected"])
                    reports.append({"profile": profile, "kind": "parity", "variant": variant["name"], "report": report})
            else:
                _, opaque = build(profile + "Opaque", prepared / "Cost/opaque_sink.cpp", [prepared / "Cost"], flags,
                                  ["/DYMCC_CURRENT_TREE_BUILD"], compile_only=True)
                exe, obj = build(profile, prepared / "Cost/cost.cpp", [prepared / "Cost"], flags,
                                 ["/DYMCC_CURRENT_TREE_BUILD"], [opaque])
                save(stage / (profile + "-OPAQUE-BUILD-PROOF.json"), {"opaqueObject": pin(opaque), "callerObject": pin(obj),
                    "separateTranslationUnits": True, "glDisabled": True, "ltcgRequested": False, "ordinaryX64CoffObjects": True})
                for campaign in (0, 1):
                    stdout, _ = invoke([str(exe), str(prepared / "Cost/host-inputs.json"), str(campaign)],
                                       profile + "-campaign-" + str(campaign), timeout=240)
                    report = json.loads(stdout.decode("utf-8"))
                    checks = module.tree_evaluate_campaign(report, campaign)
                    if not (report["batchSize"] == 32 and report["batchesPerArm"] == 24 and report["warmupBatches"] == 6
                            and report["allCaptureFieldsEscapeThroughSeparateTranslationUnit"]
                            and report["actualTypedEventBytes"] > report["actualBaselineEventBytes"] > 0):
                        raise ValueError("fixed cost shape/layout differs")
                    reports.append({"profile": profile, "kind": "cost", "campaign": campaign, "checks": checks, "report": report})
                    if checks["pass"] is not True:
                        raise ValueError("fixed cost gate rejected; stop, no retiming")
        if not discovered_headers or not discovered_libraries:
            raise ValueError("actual header/library dependency discovery incomplete")
        verify_scope()
        result = {"pass": True, "status": "TYPED_WORKER_PASS_AWAITING_JOB_INTAKE"}
    except Exception as error:
        result = {"pass": False, "status": "TYPED_WORKER_FAILED_OR_INCOMPLETE", "failure": repr(error)}
    result.update({"phase": plan["phase"], "correctnessScope": correctness_scope,
        "reports": reports, "compileInputs": plan["compileInputs"],
        "records": records, "wallMs": (time.perf_counter() - started) * 1000,
        "actualHeaders": [pin(p) for p in sorted(discovered_headers)],
        "actualLibraries": [pin(p) for p in sorted(discovered_libraries)],
        "artifacts": [pin(p) for p in sorted(stage.rglob("*")) if p.is_file()],
        "sourceReady": False, "wholeProcessCpuAcceptance": False})
    save(stage / "RESULT.json", result)
    print(json.dumps({"result": pin(stage / "RESULT.json")}))
    return 0 if result["pass"] else 1


if __name__ == "__main__":
    if len(sys.argv) != 4 or sys.argv[1] != "--owned-worker":
        raise SystemExit("Only the existing owned bridge may launch this worker")
    raise SystemExit(worker(Path(sys.argv[2]), int(sys.argv[3])))
