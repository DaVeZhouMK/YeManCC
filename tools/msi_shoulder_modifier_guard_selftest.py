#!/usr/bin/env python3
"""Build and run only the memory-only MSI shoulder modifier policy fixture."""
from pathlib import Path
import datetime
import hashlib
import json
import os
import re
import subprocess
import sys

REPO = Path(__file__).resolve().parents[1]
OUT = REPO.parents[2] / "Build/Validation/MsiShoulderModifierGuard-20261017"
VC = Path("C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat")
SOURCES = ("native/msi_shoulder_modifier_guard.h", "tools/msi_shoulder_modifier_guard_selftest.cpp")


def save(path, value):
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def main():
    run = OUT / ("run-" + datetime.datetime.now().strftime("%Y%m%d-%H%M%S-%f") + "-" + str(os.getpid()))
    run.mkdir(parents=True)
    source_bytes = {name: (REPO / name).read_bytes() for name in SOURCES}
    for name, content in source_bytes.items():
        (run / Path(name).name).write_bytes(content)
    runner_bytes = Path(__file__).read_bytes()
    (run / "runner.snapshot.py").write_bytes(runner_bytes)
    result = {"pass": False, "run": str(run), "profiles": [], "actualOSInput": False,
              "sourceHashes": {name: hashlib.sha256(content).hexdigest()
                               for name, content in source_bytes.items()}}
    try:
        if not VC.is_file():
            raise RuntimeError("Missing MSVC environment script: " + str(VC))
        profiles = (("Release", ["/O2", "/DNDEBUG"]),
                    ("Checked", ["/Od", "/RTC1", "/D_DEBUG"]))
        for name, flags in profiles:
            exe = run / (name + ".exe")
            imports_path = run / (name + "-imports.txt")
            args = ["cl", "/nologo", "/std:c++17", "/utf-8", "/EHsc", "/W4", "/WX",
                    "/permissive-", "/MT", *flags,
                    str(run / "msi_shoulder_modifier_guard_selftest.cpp"), "/I" + str(run),
                    "/Fo" + str(run / (name + ".obj")), "/Fe" + str(exe)]
            batch = run / (name + ".cmd")
            batch.write_text(
                '@echo off\ncall "' + str(VC) + '" >nul\n'
                'if errorlevel 1 exit /b %errorlevel%\n' + subprocess.list2cmdline(args) + '\n'
                'if errorlevel 1 exit /b %errorlevel%\n'
                'dumpbin /imports "' + str(exe) + '" > "' + str(imports_path) + '"\n'
                'exit /b %errorlevel%\n', encoding="utf-8")
            compiled = subprocess.run(["cmd.exe", "/d", "/c", str(batch)], cwd=run,
                                      capture_output=True, encoding="utf-8", errors="replace", timeout=60)
            (run / (name + "-compile.log")).write_text(compiled.stdout + compiled.stderr, encoding="utf-8")
            if compiled.returncode:
                raise RuntimeError(name + " build failed:\n" + compiled.stdout + compiled.stderr)
            imports = imports_path.read_text(encoding="utf-8", errors="replace")
            forbidden = ("user32.dll", "win32u.dll", "sendinput", "keybd_event", "mouse_event",
                         "getasynckeystate", "getkeystate", "setupapi.dll", "hid.dll", "advapi32.dll")
            if any(token in imports.lower() for token in forbidden):
                raise RuntimeError(name + " has a forbidden input/device/service import")
            dlls = re.findall(r"^\s+([^\s]+\.dll)\s*$", imports, flags=re.MULTILINE | re.IGNORECASE)
            if any("vcruntime" in dll.lower() or "msvcp" in dll.lower() or
                   dll.lower() in ("ucrtbase.dll", "ucrtbased.dll", "msvcrtd.dll") or
                   dll.lower().startswith("api-ms-win-crt-") for dll in dlls):
                raise RuntimeError(name + " unexpectedly depends on a dynamic CRT")
            executed = subprocess.run([str(exe)], cwd=run, capture_output=True,
                                      encoding="utf-8", errors="replace", timeout=30)
            (run / (name + "-result.json")).write_text(executed.stdout, encoding="utf-8")
            (run / (name + "-stderr.txt")).write_text(executed.stderr, encoding="utf-8")
            if executed.returncode:
                raise RuntimeError(name + " tests failed:\n" + executed.stdout + executed.stderr)
            observed = json.loads(executed.stdout)
            if not observed.get("pass") or observed.get("actualOSInput") is not False:
                raise RuntimeError(name + " reported a failed or invalid receipt")
            result["profiles"].append({"profile": name, "runtime": "/MT", "imports": dlls, **observed})
            print(name + ": " + str(observed["checks"]) + " checks passed (/MT)", flush=True)
        for name, original in source_bytes.items():
            if (REPO / name).read_bytes() != original:
                raise RuntimeError("Source changed during validation: " + name)
        if Path(__file__).read_bytes() != runner_bytes:
            raise RuntimeError("Runner changed during validation")
        result["pass"] = True
    except Exception as error:
        result["error"] = str(error)
        print(str(error), file=sys.stderr)
    result["artifacts"] = {path.name: hashlib.sha256(path.read_bytes()).hexdigest()
                           for path in run.iterdir() if path.is_file()}
    save(run / "result.json", result)
    save(OUT / "latest.json", result)
    print(json.dumps({"pass": result["pass"], "run": str(run)}), flush=True)
    return 0 if result["pass"] else 1


if __name__ == "__main__":
    sys.exit(main())
