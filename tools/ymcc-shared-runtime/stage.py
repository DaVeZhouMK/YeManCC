#!/usr/bin/env python3
"""Stage only the pinned third-party bundle into a NEW isolated package directory."""
import argparse
import json
from pathlib import Path
import shutil
import sys
sys.dont_write_bytecode = True
from verify import digest, require, validate

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--project-root", type=Path, default=Path(__file__).resolve().parents[2])
parser.add_argument("--destination", type=Path, required=True, help="New isolated PowerControl/ymcc-shared-runtime directory")
parser.add_argument("--check", action="store_true", help="Verify a previously staged copy without writing")
args = parser.parse_args()
try:
    root = args.project_root.resolve()
    destination = args.destination.resolve()
    require(not destination.is_relative_to(root), "Staging must be outside the product source/production payload root")
    require(destination.name == "ymcc-shared-runtime" and destination.parent.name == "PowerControl", "Use the contracted PowerControl/ymcc-shared-runtime layout")
    validate(root, original=False)
    source = root / "deps/ymcc-shared-runtime"
    manifest = json.loads((source / "manifest.json").read_text(encoding="utf-8-sig"))
    packaged_binaries = [entry for entry in manifest["files"] if entry.get("packagedDirectory", manifest["packagedDirectory"]) == manifest["packagedDirectory"]]
    copies = [(entry["path"], entry["packagedPath"], entry["sha256"]) for entry in packaged_binaries]
    copies += [(entry["path"], entry["path"], entry["sha256"]) for group in ["licenses", "provenanceFiles"] for entry in manifest[group]]
    copies += [(name, name, digest(source / name)) for name in ["README.md", "manifest.json"]]
    if not args.check:
        require(not destination.exists(), "Destination must be new; existing directories are never overwritten")
        destination.mkdir(parents=True)
        for relative, target_name, _ in copies:
            target = destination / target_name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source / relative, target)
    require(destination.is_dir(), "Staged directory missing")
    expected = {target_name for _, target_name, _ in copies}
    actual = {p.relative_to(destination).as_posix() for p in destination.rglob("*") if p.is_file()}
    require(actual == expected, "Staged inventory mismatch: " + str(actual ^ expected))
    for _, target_name, sha256 in copies:
        require(digest(destination / target_name) == sha256, "Staged SHA256 mismatch: " + target_name)
    print(json.dumps({"status": "SHARED_RUNTIME_STAGE_PASS", "destination": str(destination), "manifestSha256": digest(source / "manifest.json"),
                      "binaryCount": len(packaged_binaries), "binaryBytes": sum(x["bytes"] for x in packaged_binaries),
                      "copiedFiles": len(copies), "productStarted": False, "deviceAccessed": False}))
except Exception as error:
    print(str(error), file=sys.stderr)
    sys.exit(1)
