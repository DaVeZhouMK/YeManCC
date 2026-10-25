"""Read-only MSI recovery correlation audit; NOT a counterfactual controller replay.

Exports overlap in time and often contain the same rotated native log. Corpus
counts deduplicate canonical JSON events across all archives, retain provenance,
and reject cross-run/power/Host correlations. A 'last disruptive transaction'
label is only a historical diagnostic field, never proof of a current cause.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import re
import zipfile
from collections import Counter
from datetime import datetime
from pathlib import Path

OWNED = {"switch-dinput", "hidhide-cycle", "hidhide-cycle-async"}
WINDOW_MS = 10_000


def parse_time(value):
    try:
        return datetime.fromisoformat(value)
    except (TypeError, ValueError):
        return None


def fingerprint(row):
    canonical = json.dumps(row, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
    return hashlib.sha256(canonical.encode("utf-8")).hexdigest()


def collect(paths):
    events, archives, issues = {}, [], []
    for path in sorted(paths):
        count = 0
        try:
            with zipfile.ZipFile(path) as archive:
                for member in archive.namelist():
                    if not re.fullmatch(r"native-lifecycle\.log(?:\.\d+|\.prev)?", member):
                        continue
                    data = archive.read(member)
                    for number, line in enumerate(data.decode("utf-8-sig", "replace").splitlines(), 1):
                        if not line.strip():
                            continue
                        try:
                            row = json.loads(line)
                            if not isinstance(row, dict):
                                raise ValueError("not an object")
                        except (ValueError, TypeError) as error:
                            issues.append({"archive": str(path), "member": member, "line": number,
                                           "reason": "malformed-json", "detail": str(error)[:120]})
                            continue
                        stamp = parse_time(row.get("time"))
                        if not stamp or not isinstance(row.get("event"), str):
                            continue
                        count += 1
                        key = fingerprint(row)
                        provenance = {"archive": str(path), "member": member, "line": number}
                        if key not in events:
                            events[key] = {"id": key, "row": row, "sources": []}
                        if provenance not in events[key]["sources"]:
                            events[key]["sources"].append(provenance)
            archives.append({"path": str(path), "size": path.stat().st_size,
                             "sha256": hashlib.sha256(path.read_bytes()).hexdigest(), "parsedRows": count})
        except (OSError, zipfile.BadZipFile, RuntimeError) as error:
            issues.append({"archive": str(path), "reason": "archive-read-failed", "detail": str(error)})
    return sorted(events.values(), key=lambda x: (x["row"]["time"], x["id"])), archives, issues


def audit(events):
    sessions = {}
    counts = Counter()
    lost_kinds = Counter()
    old_label_ages = []
    examples = []
    for entry in events:
        row = entry["row"]
        event = row["event"]
        native_pid = row.get("pid")
        if native_pid is None:
            counts["missingNativePid"] += 1
            continue
        state = sessions.setdefault(native_pid, {"boot": "left-censored", "host": None,
                                               "power": None, "loss": None, "armed": None})
        if event == "boot-single-instance-acquired":
            state.update(boot=row["time"], host=None, power=None, loss=None, armed=None)
        power = row.get("powerGeneration")
        if power is None:
            counts["missingPowerGeneration"] += 1
            continue
        if state["power"] != power:
            state.update(power=power, loss=None, armed=None)
        explicit_host = row.get("hostEpoch", row.get("epoch"))
        # Never treat a zero/unbound epoch on a pre-start mode write as the
        # upcoming Host's identity. Host association starts at real receipts.
        if isinstance(explicit_host, int) and explicit_host > 0 and explicit_host != state["host"]:
            state.update(host=explicit_host, loss=None, armed=None)
        if event in ("input-host-stop-request", "input-host-stop-result", "input-host-persona-transition"):
            state["loss"] = None
        if event == "msi-claw-dinput-route" and row.get("phase") == "armed":
            state["loss"] = None
            state["armed"] = entry
        if event == "msi-dinput-transition" and row.get("phase") == "input-lost":
            counts["uniqueInputLost"] += 1
            kind = row.get("lastDisruptiveTxKind", "unknown")
            lost_kinds[kind] += 1
            age = row.get("lastDisruptiveTxAgeMs")
            recent = isinstance(age, (int, float)) and 0 <= age <= WINDOW_MS
            if kind in OWNED:
                counts["lastActionLabelOwned"] += 1
                if recent:
                    counts["recentOwnedLabel"] += 1
                else:
                    counts["ownedLabelOlderThanWindowOrUnknown"] += 1
                if isinstance(age, (int, float)):
                    old_label_ages.append(age)
            state["loss"] = {"entry": entry, "kind": kind, "recent": recent,
                             "boot": state["boot"], "host": state["host"], "power": power}
        if event == "msi-claw-dinput-route" and row.get("phase") == "mode-resent":
            counts["uniqueModeResents"] += 1
            previous = state["loss"]
            if previous is None:
                counts["resendWithoutSameContextLoss"] += 1
                continue
            delay = (parse_time(row["time"]) - parse_time(previous["entry"]["row"]["time"])).total_seconds() * 1000
            same = (previous["boot"] == state["boot"] and previous["host"] == state["host"]
                    and previous["power"] == power and state["host"] is not None)
            if not same or not 0 <= delay <= WINDOW_MS:
                counts["resendContextOrLossAgeUnproven"] += 1
                continue
            counts["resendWithin10sOfSameContextLoss"] += 1
            if previous["kind"] in OWNED:
                counts["lossLabelOwnedBeforeResend"] += 1
                if previous["recent"]:
                    counts["recentOwnedLabelBeforeResend"] += 1
                else:
                    counts["staleOwnedLabelBeforeResend"] += 1
            examples.append({"loss": previous["entry"], "resend": entry,
                             "delayMs": round(delay, 3), "lastActionAgeMs": previous["entry"]["row"].get("lastDisruptiveTxAgeMs"),
                             "sameBoot": state["boot"], "nativePid": native_pid, "hostEpoch": state["host"],
                             "powerGeneration": power, "attribution": "temporal-correlation-only"})
    ages = sorted(old_label_ages)
    return {"counts": dict(sorted(counts.items())), "lastActionKinds": dict(lost_kinds),
            "ownedLabelAgeMs": {"min": min(ages) if ages else None, "max": max(ages) if ages else None,
                                "median": ages[len(ages)//2] if ages else None},
            "pairs": examples,
            "scope": "deduplicated-observation; not production-decision replay; no cause or prevented-count claim"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path)
    parser.add_argument("--pattern", default="YeManCC-logs-2026101[12]*.zip")
    parser.add_argument("--json-out", type=Path)
    args = parser.parse_args()
    events, archives, issues = collect(args.root.glob(args.pattern))
    report = audit(events)
    report.update(schemaVersion=2, archives=archives, issues=issues,
                  uniqueNativeRows=len(events), parsedRowsAcrossArchives=sum(a["parsedRows"] for a in archives),
                  duplicateExportRows=sum(a["parsedRows"] for a in archives)-len(events), realMutations=0)
    if args.json_out:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(json.dumps(report, ensure_ascii=False, indent=2)+"\n", encoding="utf-8")
    print(json.dumps({k:v for k,v in report.items() if k not in ("pairs", "archives", "issues")}, ensure_ascii=False, indent=2))
    print(f"archiveCount={len(archives)} issueCount={len(issues)}")


if __name__ == "__main__":
    main()
