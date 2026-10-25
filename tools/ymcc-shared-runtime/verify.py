#!/usr/bin/env python3
"""Validate pinned third-party bytes; optionally build a new shadow tree without HC.

Only the explicitly supplied new scratch directory receives build output. No product
host is run, no production payload is staged, and no existing directory is renamed.
"""
from __future__ import annotations
import argparse
import datetime as dt
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET

NAMES = {
    "InputHost": ["Newtonsoft.Json", "Nefarius.Utilities.DeviceManagement", "Nefarius.Drivers.HidHide",
                  "Nefarius.Vicius.Abstractions", "Microsoft.Extensions.DependencyInjection.Abstractions", "Microsoft.Extensions.Logging.Abstractions"],
    "BatteryHost": ["LibreHardwareMonitorLib", "RAMSPDToolkit-NDD"],
}
PROJECTS = {
    "InputHost": "InputHost/YeManInputHost.csproj",
    "BatteryHost": "native/BatteryHost/YeManBatteryHost.csproj",
}

def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()

def require(value: bool, message: str) -> None:
    if not value:
        raise RuntimeError(message)

def checked_child(root: Path, relative: str) -> Path:
    path = (root / relative).resolve()
    require(path.is_relative_to(root.resolve()), "Manifest path escapes bundle: " + relative)
    require(not path.is_symlink() and not path.is_junction(), "Redirected manifest file: " + relative)
    return path

def original_runtime(root: Path, manifest: dict) -> tuple[Path, str]:
    source = manifest["source"]
    historical = checked_child(root, source["manifestPath"])
    if historical.is_file():
        selected, location = historical, "historical-provenance"
    else:
        profile_path = root / "deps/ymcc-device-core-runtime/profile.json"
        profile = json.loads(profile_path.read_text(encoding="utf-8-sig"))
        require(profile["sourceRuntimeId"] == source["runtimeId"] and
                profile["sourceManifestSha256"] == source["manifestSha256"],
                "Relocated original runtime identity differs from shared provenance")
        selected = checked_child(root, profile["sourceDirectory"] + "/HandheldCompanion.runtime.json")
        location = "pinned-development-upstream"
    require(digest(selected) == source["manifestSha256"], "Original manifest changed")
    return selected, location


def copy_linked_sources(root: Path, shadow: Path) -> list[dict]:
    receipts = []
    seen = set()
    for relative in PROJECTS.values():
        project = root / relative
        for item in ET.fromstring(project.read_text(encoding="utf-8-sig")).findall(".//Compile[@Include]"):
            include = item.attrib["Include"].replace("\\", "/")
            require(not any(token in include for token in ("$", "*", "?", ";")),
                    "Unsupported linked source expression: " + include)
            original = (project.parent / include).resolve()
            require(original.is_relative_to(root.resolve()) and original.suffix.lower() == ".cs",
                    "Linked source escapes repository: " + include)
            require(original.is_file(), "Missing linked production source: " + str(original))
            if original in seen:
                continue
            seen.add(original)
            target = shadow / original.relative_to(root.resolve())
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(original, target)
            receipts.append({"source": str(original), "copy": str(target), "sha256": digest(target)})
    return receipts


def validate(root: Path, original: bool) -> dict:
    shared = root / "deps/ymcc-shared-runtime"
    manifest = json.loads((shared / "manifest.json").read_text(encoding="utf-8-sig"))
    require(manifest["schemaVersion"] == 1, "Unknown manifest schema")
    require(manifest["packagedDirectory"] == "PowerControl/ymcc-shared-runtime", "Unexpected packaged location")
    expected_names = {n + ".dll" for names in NAMES.values() for n in names}
    require({Path(x["path"]).name for x in manifest["files"]} == expected_names, "Unexpected binary inventory")
    require(len(manifest["files"]) == len(expected_names), "Duplicate binary entries")
    binary_receipts = []
    approved_files = {"README.md", "manifest.json"}
    for group in ["files", "licenses", "provenanceFiles"]:
        for entry in manifest[group]:
            path = checked_child(shared, entry["path"])
            require(path.is_file(), "Missing pinned file: " + str(path))
            require(digest(path) == entry["sha256"].lower(), "SHA256 mismatch: " + str(path))
            if group == "files":
                require(path.stat().st_size == entry["bytes"], "Byte-count mismatch: " + str(path))
                require(entry["path"] == "bin/" + path.name, "DLL outside bin: " + str(path))
                require(entry["packagedPath"] == path.name, "Unexpected packaged DLL location")
                expected_directory = ("PowerControl/handheldcompanion-runtime/YMCC-DEVICECORE-20261018-v1"
                                      if path.name == "LibreHardwareMonitorLib.dll" else manifest["packagedDirectory"])
                require(entry.get("packagedDirectory", manifest["packagedDirectory"]) == expected_directory,
                        "Unapproved shared distribution override: " + path.name)
                require(not path.name.lower().startswith(("handheldcompanion", "hidmaestro")), "SDK/HC binary is outside this bundle")
                require(entry["sourceCommit"] == entry["productVersion"].split("+", 1)[1], "Source commit mismatch")
                require(any(x["component"] == entry["upstreamProject"] for x in manifest["licenses"]), "Missing component license")
                binary_receipts.append({k: entry[k] for k in ["path", "bytes", "sha256", "sourceCommit"]})
            approved_files.add(entry["path"])
    actual_files = {p.relative_to(shared).as_posix() for p in shared.rglob("*") if p.is_file()}
    require(actual_files == approved_files, "Unlisted/missing bundle files: " + str(actual_files ^ approved_files))
    by_name = {Path(x["path"]).stem: x for x in manifest["files"]}
    for consumer, relative in PROJECTS.items():
        project = root / relative
        text = project.read_text(encoding="utf-8-sig")
        require("handheldcompanion-runtime" not in text.lower(), "HC reference remains in " + relative)
        xml = ET.fromstring(text)
        prop = xml.find(".//YmccSharedRuntimeRoot")
        require(prop is not None and "deps" in (prop.text or "") and "ymcc-shared-runtime" in (prop.text or ""), "Independent default root missing: " + relative)
        references = {e.attrib["Include"]: e for e in xml.findall(".//Reference")}
        for name in NAMES[consumer]:
            require(name in references, "Missing explicit reference: " + name)
            reference = references[name]
            hint = reference.findtext("HintPath") or ""
            require(hint.replace("\\", "/") == "$(YmccSharedRuntimeRoot)/bin/" + name + ".dll", "Unexpected HintPath: " + hint)
            require(reference.findtext("Private") == "true", "Dependency must be copied locally: " + name)
    resolver = (root / "InputHost/RuntimeSharedDependencies.cs").read_text(encoding="utf-8-sig")
    require('SharedDirectoryName = "ymcc-shared-runtime"' in resolver, "Shared resolver root missing")
    require('Path.Combine(power!.FullName, SharedDirectoryName, requested.Name + ".dll")' in resolver, "Shared resolver still uses legacy path")
    require('"handheldcompanion-runtime"' not in resolver, "Legacy HC runtime path remains in resolver")
    for name in NAMES["InputHost"][:3]:
        require(re.search(r'\("' + re.escape(name) + r'",\s*"' + by_name[name]["sha256"] + r'"\)', resolver, re.I) is not None, "Resolver hash differs from manifest: " + name)
    original_identity = None
    if original:
        original_manifest, location = original_runtime(root, manifest)
        locked = {x["path"]: x["sha256"].lower() for x in json.loads(original_manifest.read_text(encoding="utf-8-sig"))["files"]}
        provenance_parent = Path(manifest["source"]["manifestPath"]).parent
        for entry in manifest["files"]:
            original_path = Path(entry["originalPath"])
            require(original_path.parent == provenance_parent, "Original DLL provenance path differs")
            require(locked[original_path.name] == entry["sha256"], "Original lock differs")
            require(digest(checked_child(original_manifest.parent, original_path.name)) == entry["sha256"], "Original DLL differs")
        original_identity = {"manifest": str(original_manifest), "sha256": digest(original_manifest), "location": location}
    return {"manifestSha256": digest(shared / "manifest.json"), "binaryBytes": sum(x["bytes"] for x in manifest["files"]), "files": binary_receipts, "originalVerified": original, "originalIdentity": original_identity}


def copy_sources(source: Path, destination: Path, suffixes: set[str]) -> list[dict]:
    receipts = []
    for path in sorted(source.rglob("*")):
        if not path.is_file() or any(p.lower() in ({"obj", ".git"} if ".dll" in suffixes else {"bin", "obj", ".git"}) for p in path.relative_to(source).parts):
            continue
        if path.suffix.lower() not in suffixes:
            continue
        target = destination / path.relative_to(source)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(path, target)
        receipts.append({"source": str(path), "copy": str(target), "sha256": digest(target)})
    return receipts

def command(args: list[str], cwd: Path, log: Path, timeout: int = 240) -> dict:
    env = dict(os.environ, DOTNET_CLI_UI_LANGUAGE="en-US", DOTNET_CLI_TELEMETRY_OPTOUT="1", VSLANG="1033")
    proc = subprocess.run(args, cwd=cwd, env=env, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=timeout)
    log.write_text(proc.stdout + "\n" + proc.stderr, encoding="utf-8")
    require(proc.returncode == 0, "Command failed (exit %s); see %s\n%s" % (proc.returncode, log, (proc.stdout + proc.stderr)[-4500:]))
    return {"command": args, "exitCode": proc.returncode, "log": str(log)}

def build(root: Path, scratch: Path) -> dict:
    scratch = scratch.resolve()
    require(not scratch.exists(), "Scratch directory must be new: " + str(scratch))
    # Prevent accidental build output in any source/production directory.
    require(not scratch.is_relative_to(root.resolve()), "Scratch must be outside the product source root")
    scratch.mkdir(parents=True)
    shadow = scratch / "shadow"
    logs = scratch / "logs"
    logs.mkdir()
    captured = []
    for relative, suffixes in [
        ("InputHost", {".cs", ".csproj"}),
        ("native/BatteryHost", {".cs", ".csproj", ".json", ".md", ".txt"}),
        ("deps/ymcc-shared-runtime", {".dll", ".json", ".md", ".txt", ".nuspec"}),
        ("tools/ymcc-shared-runtime", {".cs", ".csproj", ".targets", ".py", ".md"}),
    ]:
        captured.extend(copy_sources(root / relative, shadow / relative, suffixes))
    linked_sources = copy_linked_sources(root, shadow)
    captured.extend(linked_sources)
    for relative in ["deps/hidmaestro/HIDMaestro.Core.dll"]:
        target = shadow / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(root / relative, target)
        captured.append({"source": str(root / relative), "copy": str(target), "sha256": digest(target)})
    require(not list(shadow.rglob("*handheldcompanion-runtime*")), "HC directory leaked into shadow")
    # Empty NuGet cache prevents accidental third-party reference recovery from the global cache.
    # Only BatteryHost's pre-existing System.Management package and framework packages may restore.
    config = shadow / "NuGet.Config"
    config.write_text('<?xml version="1.0" encoding="utf-8"?>\n<configuration><packageSources><clear/><add key="nuget.org" value="https://api.nuget.org/v3/index.json" /></packageSources></configuration>\n', encoding="utf-8")
    (scratch / "captured-source.json").write_text(json.dumps(captured, indent=2), encoding="utf-8")
    results = []
    resolved = {}
    published = {}
    for consumer, relative in PROJECTS.items():
        receipt = scratch / (consumer + ".references.txt")
        output = scratch / "published" / consumer
        args = ["dotnet", "publish", str(shadow / relative), "-c", "Release", "--nologo", "--self-contained", "false",
                "--artifacts-path", str(scratch / "artifacts"), "-o", str(output),
                "-p:RestorePackagesPath=" + str(scratch / "nuget-packages"),
                "-p:RestoreConfigFile=" + str(config), "-p:UseSharedCompilation=false",
                "-p:CustomAfterMicrosoftCommonTargets=" + str(shadow / "tools/ymcc-shared-runtime/no-hc-references.targets"),
                "-p:YmccReferenceReceipt=" + str(receipt)]
        print("Building isolated " + consumer + "...", flush=True)
        results.append(command(args, shadow, logs / (consumer + ".log")))
        require(receipt.is_file(), "MSBuild reference receipt missing")
        paths = sorted(set(receipt.read_text(encoding="utf-8-sig").splitlines()))
        require(paths and not any("handheldcompanion-runtime" in p.lower() for p in paths), "Resolved HC reference detected")
        for name in NAMES[consumer]:
            expected = (shadow / "deps/ymcc-shared-runtime/bin" / (name + ".dll")).resolve()
            require(any(Path(p).resolve() == expected for p in paths), "Dependency not resolved from independent shadow: " + name)
            require(digest(output / (name + ".dll")) == digest(expected), "Published dependency mismatch: " + name)
        published[consumer] = [{"path": p.name, "bytes": p.stat().st_size, "sha256": digest(p)} for p in sorted(output.iterdir()) if p.is_file()]
        resolved[consumer] = paths
    policy_project = shadow / "tools/ymcc-shared-runtime/DependencyPolicySelfTest.csproj"
    output = scratch / "policy-test"
    args = ["dotnet", "publish", str(policy_project), "-c", "Release", "--nologo", "--artifacts-path", str(scratch / "artifacts"),
            "-o", str(output), "-p:RestorePackagesPath=" + str(scratch / "nuget-packages"), "-p:RestoreConfigFile=" + str(config), "-p:UseSharedCompilation=false"]
    results.append(command(args, shadow, logs / "dependency-policy-build.log"))
    args = ["dotnet", str(output / "DependencyPolicySelfTest.dll"), str(shadow / "deps/ymcc-shared-runtime"), str(scratch / "policy-fixture")]
    results.append(command(args, shadow, logs / "dependency-policy-run.json"))
    policy = json.loads((logs / "dependency-policy-run.json").read_text(encoding="utf-8-sig"))
    require(policy["status"] == "DEPENDENCY_POLICY_PASS", "Dependency policy did not pass")
    # Capture source consistency after builds; do not claim the result for a changed live tree.
    for entry in captured:
        require(digest(Path(entry["source"])) == entry["sha256"], "Live source changed during verification: " + entry["source"])
    require(not list(shadow.rglob("*handheldcompanion-runtime*")), "HC directory appeared in shadow")
    return {"status": "NO_HC_SHADOW_BUILD_PASS", "scratch": str(scratch), "shadow": str(shadow), "hcDirectoriesPresent": False,
            "commands": results, "resolvedReferences": resolved, "linkedSources": linked_sources, "published": published, "policy": policy,
            "productStarted": False, "deviceAccessed": False,
            "remainingLegacyEc": "native/BatteryHost/Providers.cs:213 Resources/inpoutx64.dll remains for main-controller decision; no runtime/device acceptance claimed."}

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project-root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--verify-original", action="store_true", help="Also compare with the read-only old HC manifest and DLLs")
    parser.add_argument("--build", action="store_true", help="Publish two shadow projects and run only the dependency-policy test")
    parser.add_argument("--scratch", type=Path, help="New directory outside the product source root; required by --build")
    args = parser.parse_args()
    require(not args.build or args.scratch is not None, "--build requires --scratch")
    root = args.project_root.resolve()
    report = {"schemaVersion": 1, "status": "SHARED_RUNTIME_LOCK_PASS", "checkedAt": dt.datetime.now(dt.timezone.utc).isoformat(), "root": str(root)}
    report["lock"] = validate(root, args.verify_original)
    if args.build:
        report["build"] = build(root, args.scratch)
        report["status"] = "SHARED_RUNTIME_NO_HC_BUILD_AND_POLICY_PASS"
        path = args.scratch / "verification.json"
        path.write_text(json.dumps(report, indent=2), encoding="utf-8")
        print("Verification receipt: " + str(path))
    print(json.dumps({"status": report["status"], "binaryBytes": report["lock"]["binaryBytes"], "files": len(report["lock"]["files"])}))
    return 0

if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)
