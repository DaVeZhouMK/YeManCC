"""Regression controls for relocated upstream provenance and real linked consumers."""
from __future__ import annotations
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import sys
sys.dont_write_bytecode = True


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--project-root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    repo, out = args.project_root.resolve(), args.out.resolve()
    if out.exists() or out.is_relative_to(repo) or repo.is_relative_to(out):
        raise RuntimeError("Fresh evidence outside product sources required")
    out.mkdir(parents=True)
    spec = importlib.util.spec_from_file_location("shared_verify", repo / "tools/ymcc-shared-runtime/verify.py")
    verify = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(verify)
    cases = []
    def check(name, action):
        action()
        cases.append({"id": name, "status": "PASS"})
    def reject(name, action, expected):
        try:
            action()
        except (RuntimeError, FileNotFoundError) as error:
            if expected not in str(error):
                raise
            cases.append({"id": name, "status": "PASS", "rejected": str(error)})
            return
        raise RuntimeError("Negative control accepted: " + name)
    manifest = json.loads((repo / "deps/ymcc-shared-runtime/manifest.json").read_text(encoding="utf-8-sig"))
    profile = json.loads((repo / "deps/ymcc-device-core-runtime/profile.json").read_text(encoding="utf-8-sig"))
    fixture = out / "relocated"
    profile_file = fixture / "deps/ymcc-device-core-runtime/profile.json"
    profile_file.parent.mkdir(parents=True)
    profile_file.write_text(json.dumps(profile), encoding="utf-8")
    upstream = fixture / profile["sourceDirectory"] / "HandheldCompanion.runtime.json"
    upstream.parent.mkdir(parents=True)
    shutil.copyfile(repo / profile["sourceDirectory"] / upstream.name, upstream)
    def relocated():
        selected, location = verify.original_runtime(fixture, manifest)
        assert selected == upstream and location == "pinned-development-upstream"
    check("relocated-pinned-upstream-without-old-production-directory", relocated)
    original = upstream.read_bytes()
    upstream.write_bytes(original + b" ")
    reject("changed-upstream-manifest", lambda: verify.original_runtime(fixture, manifest), "Original manifest changed")
    upstream.write_bytes(original)
    changed = dict(profile, sourceRuntimeId="unapproved-runtime")
    profile_file.write_text(json.dumps(changed), encoding="utf-8")
    reject("relocation-identity-mismatch", lambda: verify.original_runtime(fixture, manifest), "identity differs")
    changed = dict(profile, sourceDirectory="../outside")
    profile_file.write_text(json.dumps(changed), encoding="utf-8")
    reject("relocation-path-escape", lambda: verify.original_runtime(fixture, manifest), "escapes bundle")
    historical = fixture / manifest["source"]["manifestPath"]
    historical.parent.mkdir(parents=True)
    historical.write_bytes(b"wrong manifest")
    reject("corrupt-existing-historical-manifest-cannot-fall-through", lambda: verify.original_runtime(fixture, manifest), "Original manifest changed")
    shadow = out / "linked-shadow"
    linked = verify.copy_linked_sources(repo, shadow)
    expected = {"native/ProcessEfficiency.cs", "capabilities/HcDeviceCapabilityCatalog.cs", "capabilities/HcDeviceCapabilityCatalog.generated.cs", "capabilities/HcBatteryCapabilityCatalog.cs", "capabilities/HcBatteryCapabilityCatalog.generated.cs"}
    def exact_linked():
        assert {Path(row["source"]).relative_to(repo).as_posix() for row in linked} == expected
        assert all(verify.digest(Path(row["source"])) == row["sha256"] == verify.digest(Path(row["copy"])) for row in linked)
    check("all-five-actual-linked-sources-copied-exactly", exact_linked)
    for relative in verify.PROJECTS.values():
        dest = shadow / relative
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(repo / relative, dest)
    missing = shadow / "capabilities/HcBatteryCapabilityCatalog.generated.cs"
    missing.unlink()
    reject("missing-linked-capability-source", lambda: verify.copy_linked_sources(shadow, out / "missing-copy"), "Missing linked production source")
    project = shadow / verify.PROJECTS["InputHost"]
    project.write_text('<Project><ItemGroup><Compile Include="../../../outside.cs" /></ItemGroup></Project>', encoding="utf-8")
    reject("linked-source-path-escape", lambda: verify.copy_linked_sources(shadow, out / "escape-copy"), "escapes repository")
    project.write_text('<Project><ItemGroup><Compile Include="$(Unresolved)/source.cs" /></ItemGroup></Project>', encoding="utf-8")
    reject("unresolved-linked-source-expression", lambda: verify.copy_linked_sources(shadow, out / "expression-copy"), "Unsupported linked source expression")
    result = {"schemaVersion": 1, "status": "PASS", "cases": cases, "linkedSources": linked, "sourceHashes": [{"path": "tools/ymcc-shared-runtime/verify.py", "sha256": verify.digest(repo / "tools/ymcc-shared-runtime/verify.py")}, {"path": "tools/ymcc-shared-runtime/selftest.py", "sha256": verify.digest(Path(__file__))}], "hardwareIo": False, "productStarted": False}
    (out / "result.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(json.dumps({"status": "PASS", "cases": len(cases), "negativeControls": sum("rejected" in c for c in cases)}))
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
