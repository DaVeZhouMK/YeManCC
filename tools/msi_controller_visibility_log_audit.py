#!/usr/bin/env python3
"""Offline ZIP audit of independent controller-observer evidence (stdlib only).

No extraction, device APIs, product subprocesses, network, or configuration writes.
Counts are unique JSON observations, not simultaneous devices or physical reports.
The parsed JSON and every original archive/member/line remain in the report.
"""
from __future__ import annotations
import argparse
from collections import Counter, defaultdict
from datetime import datetime, timedelta, timezone
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import sys
import zipfile

SCHEMA_VERSION = 1
OBSERVER_BASE = "msi-controller-visibility.log"
# Runtime permits only two wake observer episodes; filenames only select input bytes.
OBSERVER_BASES = (OBSERVER_BASE, "msi-controller-visibility-wake-1.log", "msi-controller-visibility-wake-2.log")
NATIVE_BASE = "native-lifecycle.log"
CONFIG_EVENTS = {"hidhide.visibility-snapshot", "hidhide-app-list", "hidhide.cloak-resident",
                 "hidhide.pre-cloak", "hidhide.app-reg-resident"}
LIFETIME_EVENTS = {"boot-single-instance-acquired", "session-clean-exit"}
LIMITATIONS = [
    "read-access-open/GENERIC_READ handle success is not a ReadFile stream; handle-only events do not record HID reports.",
    "stateApiSucceeded plus unchanged/zero/neutral-looking values does not prove fresh physical input delivery.",
    "formatVersion 5 DI decodedContentChanged hashes the whole DIJOYSTATE2 (including unlogged velocity/acceleration); GUID prior survives failed reads/disappearance. It is a legacy comparison, NOT proof the recorded axes/buttons changed.",
    "Recorded-control comparisons require adjacent successful inventoryIds and the same exact path/GUID, but even changed values lack operator-action/physical-freshness correlation.",
    "The independent DirectInput probe is not Steam. Neither DI nor unmapped WGI proves Steam input consumption or suppression.",
    "PnP inventory, properties, metadata handles and HidHide configuration membership are separate facts, not input delivery or filter-attachment proof.",
    "HidHide applications/inverse/cloak are point-in-time, parent-lifetime-scoped evidence. A separate text application list is not an atomic snapshot or resolved Steam/observer exemption.",
    "No evidence here automatically proves a cause of the reported dark screen; no product or physical probe was run by this audit.",
    "Overlapping exports/rotations are deduplicated; counts are observations, not distinct devices, packets, polling frequency, or complete lifetime coverage.",
    "WM_INPUT payload changes are receipt hashes, not effective controls. Valid decoded/complete samples never establish idle, phantom, suppression leakage, Steam consumption, resolved exemption, or permission to self-heal.",
    "WM_INPUT queue timestamps describe message posting/delay, never hardware sample time. Missing, invalid, duplicate or unsupported witnesses remain inspectable and cannot supply decoded evidence.",
    "Observer input names are limited to the base log and wake-1/wake-2 logs with existing rotations. Names are provenance only: they do not establish a session, wake generation/episode, successful wake, or healthy recovery; a valid start is still required.",
    "Latest-archive coverage is independent; older E/F observer sessions never substitute for missing latest logs.",
]


def canonical(value):
    """Stable JSON key/whitespace canonicalization; not RFC8785 numeric normalization."""
    return json.dumps(value, ensure_ascii=True, sort_keys=True, separators=(",", ":"), allow_nan=False)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def file_sha256(path):
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def integer(value):
    return isinstance(value, int) and not isinstance(value, bool)


def hresult_succeeded(value):
    return integer(value) and -2147483648 <= value <= 4294967295 and (value & 0x80000000) == 0


def local_epoch_ms(text, offset_minutes):
    if not isinstance(text, str):
        return None
    try:
        dt = datetime.fromisoformat(text.replace("Z", "+00:00"))
        if dt.tzinfo is None:
            dt = dt.replace(tzinfo=timezone(timedelta(minutes=offset_minutes)))
        elapsed = dt.astimezone(timezone.utc) - datetime(1970, 1, 1, tzinfo=timezone.utc)
        return (elapsed.days * 86400 + elapsed.seconds) * 1000 + elapsed.microseconds // 1000
    except (ValueError, OverflowError):
        return None


def utc_text(value):
    if not integer(value):
        return None
    try:
        return datetime.fromtimestamp(value / 1000, timezone.utc).isoformat(timespec="milliseconds")
    except (ValueError, OverflowError, OSError):
        return None


def filetime_epoch_ms(value):
    return (value - 116444736000000000) // 10000 if integer(value) and value > 0 else None


def member_matches(name, base):
    """Exact basenames/rotations; observer input has a fixed wake-1/2 allowlist."""
    leaf = PurePosixPath(name.replace("\\", "/")).name
    bases = OBSERVER_BASES if base == OBSERVER_BASE else (base,)
    return any(re.fullmatch(re.escape(candidate) + r"(?:\.(?:\d+|prev))?", leaf) is not None for candidate in bases)


def reject_constant(value):
    raise ValueError("non-finite JSON number: " + value)


def unique_keys(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("duplicate JSON object key: " + key)
        result[key] = value
    return result


def read_json_lines(zf, info, archive, member_index, pool, malformed):
    data = zf.read(info)
    lines = data.splitlines()
    valid = []
    counts = Counter()
    for number, raw in enumerate(lines, 1):
        if not raw.strip():
            counts["blankLines"] += 1
            continue
        provenance = {"archiveId": archive["archiveId"], "archive": archive["path"],
                      "archiveSha256": archive["sha256"], "member": info.filename,
                      "memberIndex": member_index, "line": number, "rawLineSha256": digest(raw)}
        try:
            text = raw.decode("utf-8-sig" if number == 1 else "utf-8")
            provenance["rawLine"] = text
            row = json.loads(text, parse_constant=reject_constant, object_pairs_hook=unique_keys)
            if not isinstance(row, dict):
                raise ValueError("JSON log row is not an object")
            if "event" in row and not isinstance(row["event"], str):
                raise ValueError("event is not a string")
            for field in ("observerPid", "parentPid", "parentCreationTime", "utcEpochMs", "tick", "inventoryId", "pid"):
                if field in row and not integer(row[field]):
                    raise ValueError(field + " is not an integer")
            key = canonical(row)
        except (ValueError, UnicodeError, RecursionError) as exc:
            provenance.setdefault("rawLine", raw.decode("utf-8", errors="replace"))
            malformed.append({"provenance": provenance, "error": str(exc),
                              "terminalLine": number == len(lines),
                              "unterminatedTail": number == len(lines) and not data.endswith((b"\n", b"\r"))})
            counts["malformedRows"] += 1
            continue
        counts["validJsonRows"] += 1
        if key not in pool:
            pool[key] = {"recordId": digest(key.encode("ascii")), "record": row, "provenance": []}
        pool[key]["provenance"].append(provenance)
        valid.append(pool[key]["recordId"])
    return {"member": info.filename, "memberIndex": member_index, "uncompressedBytes": len(data),
            "lineCount": len(lines), "terminalNewline": data.endswith((b"\n", b"\r")), **dict(counts)}, valid


def native_ms(item, archives):
    offsets = {archives[p["archiveId"]]["timeZoneOffsetMinutes"] for p in item["provenance"]}
    return local_epoch_ms(item["record"].get("time"), offsets.pop()) if len(offsets) == 1 else None


def observation_key(item):
    r = item["record"]
    return (r.get("utcEpochMs") if integer(r.get("utcEpochMs")) else -1,
            r.get("tick") if integer(r.get("tick")) else -1, item["recordId"])


def histogram(items, field):
    return dict(sorted(Counter(canonical(x["record"][field]) if field in x["record"] else "<missing>"
                               for x in items).items()))


def stage(items, field):
    values = [x["record"].get(field) for x in items]
    return {"attempted": sum(integer(v) for v in values), "succeeded": sum(hresult_succeeded(v) for v in values),
            "failed": sum(integer(v) and not hresult_succeeded(v) for v in values),
            "notRecorded": sum(v is None for v in values), "hresults": histogram(items, field)}


def state_counts(items):
    rows = [x["record"] for x in items]
    return {"stateApiSucceeded": sum(r.get("stateApiSucceeded") is True for r in rows),
            "loggedDecodedContentChangedTrue": sum(r.get("decodedContentChanged") is True for r in rows),
            "successfulLoggedUnchanged": sum(r.get("stateApiSucceeded") is True and r.get("decodedContentChanged") is False for r in rows),
            "buttonsNonzeroObservations": sum(integer(r.get("buttonsDownCount")) and r["buttonsDownCount"] > 0 for r in rows),
            "successfulButtonsNonzeroObservations": sum(r.get("stateApiSucceeded") is True and integer(r.get("buttonsDownCount")) and r["buttonsDownCount"] > 0 for r in rows),
            "freshPhysicalInputDeliveryProven": False, "steamConsumptionProven": False}


def recorded_controls(row):
    state = row.get("controllerState")
    if row.get("stateApiSucceeded") is not True or not hresult_succeeded(row.get("stateHresult")) or not isinstance(state, dict):
        return None
    fields = ("axisValues", "povValues", "buttonsDown")
    if not all(isinstance(state.get(k), list) and all(integer(v) for v in state[k]) for k in fields):
        return None
    if len(state["axisValues"]) != 8 or len(state["povValues"]) != 4:
        return None
    return canonical({k: state[k] for k in fields})


def recorded_comparisons(items):
    """Do not trust the v5 cross-gap GUID/hash prior; compare only logged values."""
    pairs = []
    multiplicity = Counter(x["record"].get("inventoryId") for x in items if integer(x["record"].get("inventoryId")))
    previous = None
    for item in sorted(items, key=observation_key):
        row = item["record"]
        current = recorded_controls(row)
        inv = row.get("inventoryId")
        if previous is not None and current is not None and integer(inv) and multiplicity[inv] == 1:
            old, old_state = previous
            old_r = old["record"]
            if (integer(old_r.get("inventoryId")) and inv == old_r["inventoryId"] + 1
                    and multiplicity[old_r["inventoryId"]] == 1
                    and row.get("instanceGuid") == old_r.get("instanceGuid") and row.get("instanceGuid") is not None):
                pairs.append({"previousRecordId": old["recordId"], "recordId": item["recordId"],
                              "recordedControlsChanged": old_state != current,
                              "loggedDecodedContentChanged": row.get("decodedContentChanged")})
        previous = (item, current) if current is not None else None
    return {"scope": "same exact path/GUID, adjacent successful inventoryIds, recorded axes/POV/buttons only; no physical freshness proof",
            "comparedPairs": len(pairs), "changedPairs": sum(p["recordedControlsChanged"] for p in pairs),
            "unchangedPairs": sum(not p["recordedControlsChanged"] for p in pairs),
            "loggedChangedButRecordedUnchangedPairs": sum(p["loggedDecodedContentChanged"] is True and not p["recordedControlsChanged"] for p in pairs),
            "pairs": pairs}


def summarize_interfaces(items, format_version):
    groups = defaultdict(list)
    unmapped = []
    for item in items:
        row = item["record"]
        if row.get("event") not in ("directinput-device", "msi-pnp-interface"):
            continue
        path = row.get("interfacePath")
        if isinstance(path, str) and path:
            groups[path].append(item)  # Exact; no case/MI/COL/VID-PID merging.
        else:
            unmapped.append(item["recordId"])
    result = []
    metadata_fields = ("pnpInstanceId", "usbRootInstance", "pnpInstanceConfigRet", "pnpLocateConfigRet",
        "pnpStatusConfigRet", "pnpStatus", "pnpProblem", "pnpStarted", "hidAttributesKnown",
        "hidCapabilitiesKnown", "hidVid", "hidPid", "hidUsagePage", "hidUsage",
        "hidProductStringKnown", "hidProductString", "hidProductUtf16", "productUtf16",
        "freshMetadataHandleOpen", "freshMetadataHandleError")
    for path, observations in sorted(groups.items()):
        di = [x for x in observations if x["record"].get("event") == "directinput-device"]
        pnp = [x for x in observations if x["record"].get("event") == "msi-pnp-interface"]
        rows = [x["record"] for x in observations]
        result.append({"interfacePath": path, "uniqueObservations": len(observations),
            "recordIds": [x["recordId"] for x in sorted(observations, key=observation_key)],
            "directInput": {"observations": len(di),
                "enumDeviceObservations": sum(x["record"].get("enumerated") is True for x in di),
                "enumScope": "per-path enumeration callbacks; overall EnumDevices HRESULT is in inventory coverage",
                "create": stage(di, "createHresult"), "dataFormat": stage(di, "dataFormatHresult"),
                "cooperative": stage(di, "cooperativeHresult"), "acquire": stage(di, "acquireHresult"),
                "poll": stage(di, "pollHresult"), "getState": stage(di, "stateHresult"), **state_counts(di),
                "decodedContentChangedInterpretation": "legacy-whole-DIJOYSTATE2-hash-with-GUID-prior-across-gaps" if format_version == 5 else "logged-comparison-scope-not-assumed; inspect original start/device scope",
                "legacyDecodedContentChangedTrue": sum(x["record"].get("decodedContentChanged") is True for x in di) if format_version == 5 else None,
                "recordedControlComparison": recorded_comparisons(di)},
            "pnp": {"observations": len(pnp),
                    "enumeratedInterfaceObservations": sum(x["record"].get("enumeratedPnpInterface") is True for x in pnp),
                    "meaning": "PnP-only rows are not DI Enum/Create/Acquire/GetState"},
            "metadataProperties": {field: histogram(observations, field) for field in metadata_fields},
            "opens": {
                "metadataHandleSuccess": sum(r.get("freshMetadataHandleOpen") is True for r in rows),
                "directInputFreshReadAccessAttempted": sum(isinstance(x["record"].get("freshHidReadHandleOpen"), bool) for x in di),
                "directInputFreshReadAccessSucceeded": sum(x["record"].get("freshHidReadHandleOpen") is True for x in di),
                "directInputFreshReadAccessErrors": histogram(di, "freshHidReadHandleError"),
                "qualifiedMainReadAccessAttempted": sum(r.get("freshMainHidReadProbeAttempted") is True for r in rows),
                "qualifiedMainReadAccessSucceeded": sum(r.get("freshMainHidReadProbeAttempted") is True and r.get("freshMainHidReadHandleOpen") is True for r in rows),
                "qualifiedMainReadAccessErrors": histogram(observations, "freshMainHidReadHandleError"),
                "scope": "GENERIC_READ access/open only, immediately closed; no ReadFile stream evidence in these events",
                "readFileStreamProven": False}})
    return result, unmapped


def summarize_wgi(items):
    groups = defaultdict(list)
    for item in items:
        r = item["record"]
        if r.get("event") == "wgi-device":
            nrid = r.get("nonRoamableId")
            key = nrid if isinstance(nrid, str) and nrid else "<missing-id>:" + item["recordId"]
            groups[key].append(item)
    result = []
    for nrid, observations in sorted(groups.items()):
        paths = sorted({x["record"]["interfacePath"] for x in observations
                        if isinstance(x["record"].get("interfacePath"), str) and x["record"]["interfacePath"]})
        result.append({"nonRoamableId": nrid, "observations": len(observations),
                       "recordIds": [x["recordId"] for x in sorted(observations, key=observation_key)],
                       "mappingStatus": "path-logged-but-not-independently-verified" if paths else "unmapped",
                       "loggedInterfacePaths": paths, "miColAttribution": "not-made",
                       "mappingScope": "no VID/PID/name matching to DI or MI/COL; WGI stays a separate API identity",
                       "readingTimestampHistogram": histogram(observations, "readingTimestamp"), **state_counts(observations)})
    return result


def inventory_coverage(items):
    result = {}
    for event in ("directinput-inventory", "wgi-inventory", "msi-pnp-inventory"):
        observations = [x for x in items if x["record"].get("event") == event]
        rows = [x["record"] for x in observations]
        ids = sorted({r["inventoryId"] for r in rows if integer(r.get("inventoryId"))})
        gaps = [[a + 1, b - 1] for a, b in zip(ids, ids[1:]) if b > a + 1]
        result[event] = {"observations": len(rows), "inventoryIdMin": ids[0] if ids else None,
                         "inventoryIdMax": ids[-1] if ids else None, "missingInventoryIdRanges": gaps,
                         "reportedCountHistogram": histogram(observations, "count"),
                         "limitReachedObservations": sum(integer(r.get("count")) and integer(r.get("limit")) and r["count"] >= r["limit"] for r in rows),
                         "enumHresult": stage(observations, "hresult") if event == "directinput-inventory" else None,
                         "okTrue": sum(r.get("ok") is True for r in rows),
                         "scope": "logged sample inventories only; gaps/caps/partial tails limit coverage"}
    return result


def build_sessions(observers):
    starts = sorted([x for x in observers if x["record"].get("event") == "start"], key=observation_key)
    sessions = []
    invalid = []
    for start in starts:
        r = start["record"]
        if not all(integer(r.get(k)) for k in ("observerPid", "parentPid", "parentCreationTime", "utcEpochMs", "tick")):
            invalid.append(start["recordId"])
            continue
        identity = {k: r[k] for k in ("observerPid", "parentPid", "parentCreationTime", "utcEpochMs", "tick")}
        sid = digest(canonical(identity).encode("ascii"))[:24]
        sessions.append({"sessionId": sid, "identity": identity, "startRecordId": start["recordId"],
                         "formatVersion": r.get("formatVersion"), "observerExecutable": r.get("observerExecutable"),
                         "parentCreationEpochMs": filetime_epoch_ms(r["parentCreationTime"]),
                         "startEpochMs": r["utcEpochMs"], "startUtc": utc_text(r["utcEpochMs"]),
                         "stopRecordIds": [], "stopEpochMs": None, "items": []})
    unassigned = []
    for item in sorted(observers, key=observation_key):
        r = item["record"]
        candidates = []
        for session in sessions:
            ident = session["identity"]
            if r.get("observerPid") != ident["observerPid"] or not integer(r.get("utcEpochMs")) or not integer(r.get("tick")):
                continue
            if r["utcEpochMs"] < session["startEpochMs"] or r["tick"] < ident["tick"]:
                continue
            if any(k in r and r[k] != ident[k] for k in ("parentPid", "parentCreationTime")):
                continue
            next_starts = [s["startEpochMs"] for s in sessions if s["identity"]["observerPid"] == ident["observerPid"] and s["startEpochMs"] > session["startEpochMs"]]
            if next_starts and r["utcEpochMs"] >= min(next_starts):
                continue
            if session["stopEpochMs"] is not None and r["utcEpochMs"] > session["stopEpochMs"]:
                continue
            if r.get("event") == "start" and item["recordId"] != session["startRecordId"]:
                continue
            candidates.append(session)
        if len(candidates) != 1:
            unassigned.append({"recordId": item["recordId"], "reason": "missing-valid-start-or-time/identity" if not candidates else "ambiguous-observer-lifetime"})
            continue
        session = candidates[0]
        session["items"].append(item)
        item["sessionId"] = session["sessionId"]
        if r.get("event") == "stop":
            session["stopEpochMs"] = r["utcEpochMs"]
            session["stopRecordIds"].append(item["recordId"])
    return sessions, unassigned, invalid


def app_paths(row):
    value = row.get("applications")
    if isinstance(value, list) and all(isinstance(x, str) for x in value):
        return value, False
    if isinstance(value, dict) and value.get("readbackKnown") is True:
        paths = value.get("paths")
        if isinstance(paths, list) and all(isinstance(x, str) for x in paths):
            return paths, True
    return None, False


def config_summary(items, archives, executable=None):
    points = []
    text_lists = []
    for item in sorted(items, key=lambda x: (native_ms(x, archives) or -1, x["recordId"])):
        r = item["record"]
        paths, structured_readback = app_paths(r)
        normalized = [x.replace("/", "\\").casefold() for x in paths] if paths is not None else None
        observer = executable.replace("/", "\\").casefold() if isinstance(executable, str) else None
        embedded = r.get("applications") if isinstance(r.get("applications"), dict) else {}
        inverse = embedded.get("inverseState", r.get("inverseApplicationList"))
        cloak = r.get("cloakOn", r.get("cloak"))
        point = {"recordId": item["recordId"], "event": r.get("event"), "pid": r.get("pid"),
                 "timeLocal": r.get("time"), "epochMs": native_ms(item, archives),
                 "phase": r.get("phase"), "powerGeneration": r.get("powerGeneration"),
                 "applications": paths, "structuredApplicationReadback": structured_readback,
                 "inverseState": inverse, "cloakOn": cloak if isinstance(cloak, bool) else None,
                 "configurationVerified": r.get("configurationVerified"), "readback": r.get("readback"),
                 "externalVisibility": r.get("externalVisibility"), "filterAttached": r.get("filterAttached"),
                 "sameSnapshotApplicationsInverseCloak": structured_readback and inverse in ("on", "off") and isinstance(cloak, bool),
                 "observerExecutableTextMembership": observer in normalized if observer and normalized is not None else None,
                 "steamExecutableTextPaths": [x for x in paths or [] if x.replace("/", "\\").rsplit("\\", 1)[-1].casefold() in ("steam.exe", "steamwebhelper.exe")],
                 "steamExemptionResolved": False, "observerExemptionResolved": False,
                 "scope": "point-in-time native configuration only; no resolved process exemption, filter attachment, or Steam delivery inference"}
        points.append(point)
        if r.get("event") == "hidhide-app-list":
            text_lists.append(point)
    snapshots = [x for x in points if x["event"] == "hidhide.visibility-snapshot"]
    for point in snapshots:
        nearby = [x for x in text_lists if x["pid"] == point["pid"] and x["phase"] == point["phase"]
                  and x["epochMs"] is not None and point["epochMs"] is not None and abs(x["epochMs"] - point["epochMs"]) <= 1000]
        point["nearbySeparateApplicationLists"] = [{"recordId": x["recordId"], "timeLocal": x["timeLocal"],
                                                    "deltaMs": x["epochMs"] - point["epochMs"], "atomicWithSnapshot": False} for x in nearby]
    return {"nativeConfigRows": len(points), "visibilitySnapshots": len(snapshots),
            "textApplicationListRows": len(text_lists),
            "completeSameSnapshotCount": sum(x["sameSnapshotApplicationsInverseCloak"] for x in snapshots),
            "steamExemptionResolved": False, "observerExemptionResolved": False, "points": points}


def session_native_scope(session, native, archives):
    begin = session["parentCreationEpochMs"]
    end = max(x["record"]["utcEpochMs"] for x in session["items"])
    parent = session["identity"]["parentPid"]
    if begin is None:
        return {"binding": "missing-parent-creation-time", "config": config_summary([], archives)}
    lifetime_rows = [x for x in native if x["record"].get("pid") == parent and native_ms(x, archives) is not None]
    boots = sorted(native_ms(x, archives) for x in lifetime_rows if x["record"].get("event") == "boot-single-instance-acquired")
    anchor = [t for t in boots if begin <= t <= min(end, begin + 30000)]
    later_boots = [t for t in boots if t > (min(anchor) if anchor else begin + 30000)]
    lifetime_end = min(later_boots) - 1 if later_boots else end
    exits = [native_ms(x, archives) for x in lifetime_rows
             if x["record"].get("event") == "session-clean-exit" and begin <= native_ms(x, archives) <= lifetime_end]
    if exits:
        lifetime_end = min(lifetime_end, min(exits))
    stop = min(end, lifetime_end)
    scoped = [x for x in lifetime_rows if x["record"].get("event") in CONFIG_EVENTS and begin <= native_ms(x, archives) <= stop]
    config = config_summary(scoped, archives, session["observerExecutable"])
    for point in config["points"]:
        point["relativeToObserverStart"] = "before-observer" if point["epochMs"] < session["startEpochMs"] else "during-observed-window"
    return {"binding": "logged-parentPid+exact-FILETIME; native PID/time window (not independently verified OS identity)",
            "parentPid": parent, "parentCreationTime": session["identity"]["parentCreationTime"],
            "parentCreationUtc": utc_text(begin), "nativeBootAnchorObserved": bool(anchor),
            "nativeBootUtc": utc_text(min(anchor)) if anchor else None,
            "scopeBeginUtc": utc_text(begin), "scopeEndUtc": utc_text(stop),
            "nextSamePidBootBounded": bool(later_boots), "scopeIsContinuousConfigProof": False, "config": config}



RAW_RECEIPT_EVENT = "msi-rawinput-receipt-window"
RAW_COUNTERS = ("messageReceipts", "reportReceipts", "changedPayloadReports", "queueTimestampsKnown",
                "queueTimestampsUnknown", "firstQueuedTick", "lastQueuedTick", "maxQueueDelayMs", "delayedMessages")
WITNESS_COUNTERS = ("semanticWitnessCandidates", "semanticWitnessSelections", "semanticWitnessSuppressed",
                    "semanticWitnessClockRollbacks", "semanticWitnessRecordsOverwritten", "decodedWitnesses", "completeWitnesses")


def uint(value, maximum=(1 << 64) - 1):
    return integer(value) and 0 <= value <= maximum


def raw_collection_role(row):
    """Qualify metadata and path together; the logged role alone grants nothing."""
    if not all(uint(row.get(k), 65535) for k in ("vid", "pid", "usagePage", "usage")):
        return "unqualified"
    path = row.get("interfacePath")
    if not isinstance(path, str) or row["vid"] != 0x0db0:
        return "unqualified"
    path = path.casefold()
    pid, page, usage = row["pid"], row["usagePage"], row["usage"]
    prefix = r"\\?\hid#vid_0db0&pid_"
    if pid == 0x1902:
        if path.startswith(prefix + "1902&mi_00&col01#") and page == 1 and usage in (4, 5):
            return "msi-dinput-main-collection"
        if path.startswith(prefix + "1902&mi_00&col02#") and page == 0xfff0 and usage == 0x40:
            return "msi-vendor-collection"
        if path.startswith(prefix + "1902&mi_01&col03#") and page == 12 and usage == 1:
            return "msi-consumer-collection"
    if pid == 0x1901 and path.startswith(prefix + "1901&"):
        # Reject known non-game interfaces even if their logged usage/role is forged.
        if path.startswith(prefix + "1901&mi_01#"):
            return "msi-xinput-vendor-collection" if page == 0xffa0 and usage == 1 else "unqualified"
        if path.startswith(prefix + "1901&mi_02&col03#"):
            return "msi-xinput-consumer-collection" if page == 12 and usage == 1 else "unqualified"
        if page == 1 and usage in (4, 5):
            return "msi-xinput-game-collection"
    return "unqualified"


def validate_witness_controls(controls, report_id):
    """Check logged HID fields, without independently decoding or declaring activity."""
    errors = []
    def require(ok, reason):
        if not ok:
            errors.append(reason)
    if not isinstance(controls, dict):
        return ["controls-not-object"]
    known, complete = controls.get("decodedControlsKnown"), controls.get("decodeComplete")
    require(type(known) is bool and type(complete) is bool, "controls-flags-not-bool")
    state = controls.get("state")
    require(isinstance(state, str) and bool(state), "controls-state-missing")
    if complete is True:
        require(known is True and state == "decoded", "complete-controls-inconsistent")
    elif known is True:
        require(state == "partial", "partial-controls-state-inconsistent")
    else:
        require(state not in ("decoded", "partial"), "unknown-controls-state-inconsistent")
    decoded_fields = 0
    for field in ("buttonsDown", "values", "buttons"):
        if field == "buttons" and field not in controls:
            continue
        fields = controls.get(field)
        require(isinstance(fields, list), field + "-not-array")
        if not isinstance(fields, list):
            continue
        require(len(fields) <= (32 if field == "values" else 128), field + "-limit-exceeded")
        keys = set()
        for index, value in enumerate(fields):
            tag = field + "[" + str(index) + "]:"
            if not isinstance(value, dict):
                errors.append(tag + "not-object")
                continue
            before = len(errors)
            require(uint(value.get("reportID"), 255) and value["reportID"] == report_id, tag + "reportID-mismatch")
            require(all(uint(value.get(k), 65535) for k in ("linkCollection", "usagePage", "usage")), tag + "invalid-hid-identity")
            key = canonical([value.get(k) for k in ("reportID", "linkCollection", "usagePage", "usage")])
            require(key not in keys, tag + "duplicate-hid-field")
            keys.add(key)
            if field != "values":
                require(value.get("usagePage") == 9 and uint(value.get("usage"), 65535) and value["usage"] > 0,
                        tag + "not-standard-button")
                require(value.get("state") == "decoded", tag + "button-state-invalid")
                require(all(k in value and value[k] is None for k in ("logicalMin", "logicalMax", "bitSize", "HasNull")),
                        tag + "button-metadata-not-unknown")
                require(uint(value.get("rawUnsigned"), 1) and integer(value.get("signedValue"))
                        and value.get("signedValue") == value.get("rawUnsigned"), tag + "button-value-invalid")
                if field == "buttonsDown":
                    require(value.get("rawUnsigned") == 1, tag + "button-not-down")
                if "valueSource" in value:
                    require(value["valueSource"] == "HidP_GetUsages-presence", tag + "button-source-invalid")
            else:
                page, usage = value.get("usagePage"), value.get("usage")
                require(integer(page) and integer(usage) and ((page == 1 and 0x30 <= usage <= 0x39)
                        or (page == 2 and usage in (0xba, 0xbb, 0xc4, 0xc5))), tag + "unsupported-value-usage")
                lo, hi, bits = value.get("logicalMin"), value.get("logicalMax"), value.get("bitSize")
                metadata = (integer(lo) and integer(hi) and -(1 << 31) <= lo <= hi < (1 << 31)
                            and uint(bits, 32) and bits > 0 and type(value.get("HasNull")) is bool)
                require(metadata, tag + "invalid-value-metadata")
                for flag in ("isSigned", "isAbsolute", "controlValueKnown"):
                    if flag in value:
                        require(type(value[flag]) is bool, tag + flag + "-not-bool")
                vstate, raw, signed = value.get("state"), value.get("rawUnsigned"), value.get("signedValue")
                require(vstate in ("decoded", "null", "unknown"), tag + "invalid-value-state")
                if vstate in ("decoded", "null") and metadata:
                    numeric = uint(raw, (1 << bits) - 1) and integer(signed)
                    require(numeric, tag + "invalid-value-number")
                    if numeric:
                        expected = raw - (1 << bits) if raw & (1 << (bits - 1)) else raw
                        interpreted = signed if lo < 0 else raw
                        outside = interpreted < lo or interpreted > hi
                        require(signed == expected, tag + "signed-value-inconsistent")
                        require((not outside and vstate == "decoded") or (outside and value["HasNull"] is True and vstate == "null"),
                                tag + "logical-range-or-null-inconsistent")
                        if "interpretedValue" in value:
                            require(integer(value["interpretedValue"]) and value["interpretedValue"] == interpreted,
                                    tag + "interpreted-value-inconsistent")
                        if "isSigned" in value:
                            require(value["isSigned"] is (lo < 0), tag + "signed-flag-inconsistent")
                        if "isNull" in value:
                            require(value["isNull"] is outside, tag + "null-flag-inconsistent")
                        if "controlValueKnown" in value:
                            require(value["controlValueKnown"] is (not outside), tag + "known-flag-inconsistent")
                elif vstate == "unknown":
                    require(all(k in value and (value[k] is None or integer(value[k])) for k in ("rawUnsigned", "signedValue")),
                            tag + "unknown-value-number-invalid")
                    require(complete is not True, tag + "complete-with-unknown-value")
            if len(errors) == before and value.get("state") in ("decoded", "null"):
                decoded_fields += 1
    if "buttons" in controls and isinstance(controls.get("buttons"), list) and isinstance(controls.get("buttonsDown"), list):
        down = [v for v in controls["buttons"] if isinstance(v, dict) and v.get("rawUnsigned") == 1]
        require(canonical(sorted(down, key=canonical)) == canonical(sorted(controls["buttonsDown"], key=canonical)), "buttons-down-inconsistent")
    require((known is True) == (decoded_fields > 0), "decoded-flag-without-matching-fields")
    for field in ("unknown", "apiErrors"):
        if field in controls:
            require(isinstance(controls[field], list), field + "-not-array")
            if complete is True:
                require(controls[field] == [], "complete-with-" + field)
    if "limitsExceeded" in controls:
        require(type(controls["limitsExceeded"]) is bool, "limitsExceeded-not-bool")
        if complete is True:
            require(controls["limitsExceeded"] is False, "complete-with-limit-exceeded")
    return errors


def assess_raw_witness(witness, row, window, inherited_errors):
    result = {"loggedWitness": witness, "validationErrors": list(inherited_errors)}
    errors = result["validationErrors"]
    def require(ok, reason):
        if not ok:
            errors.append(reason)
    if not isinstance(witness, dict):
        errors.append("witness-not-object")
        return result
    require(integer(witness.get("version")) and witness["version"] == 1, "unsupported-or-invalid-witness-version")
    for field in ("selectionSequence", "receiptTick", "batchReportIndex", "batchReportCount", "reportSize", "reportId", "payloadReceiptHash"):
        require(uint(witness.get(field)), field + "-invalid-or-missing")
    require(uint(witness.get("selectionSequence")) and witness["selectionSequence"] > 0, "selection-sequence-not-positive")
    tick = witness.get("receiptTick")
    if uint(tick):
        require(uint(window.get("windowStartTick")) and uint(window.get("windowEndTick"))
                and window["windowStartTick"] <= tick <= window["windowEndTick"], "receipt-outside-window")
        require(uint(row.get("firstReceiptTick")) and uint(row.get("lastReceiptTick"))
                and row["firstReceiptTick"] <= tick <= row["lastReceiptTick"], "receipt-outside-collection-range")
    count, index, size = (witness.get(k) for k in ("batchReportCount", "batchReportIndex", "reportSize"))
    require(uint(count, 1024) and count > 0 and uint(size, 4096) and size > 0
            and size * count <= 65536, "report-batch-shape-invalid")
    require(uint(index) and uint(count) and count > 0 and index == count - 1, "report-index-not-last-in-batch")
    require(uint(count) and uint(row.get("reportReceipts")) and count <= row["reportReceipts"], "batch-exceeds-report-receipts")
    require(uint(witness.get("reportId"), 255), "reportId-out-of-range")
    require(witness.get("hardwareSampleTimeKnown") is False, "hardware-sample-time-not-explicitly-unknown")
    require(witness.get("reportBytesLogged") is False, "report-bytes-not-explicitly-unlogged")
    qknown = witness.get("queueTimestampKnown")
    require(type(qknown) is bool, "queueTimestampKnown-not-bool")
    posted, delay = witness.get("postedTick"), witness.get("queueDelayMs")
    if qknown is True:
        require(uint(tick) and uint(posted) and uint(delay, 900000) and posted + delay == tick, "queue-time-inconsistent")
        require(uint(row.get("queueTimestampsKnown")) and row["queueTimestampsKnown"] > 0, "known-queue-not-counted")
        require(uint(posted) and uint(row.get("firstQueuedTick")) and uint(row.get("lastQueuedTick"))
                and row["firstQueuedTick"] <= posted <= row["lastQueuedTick"], "posted-tick-outside-queue-range")
        require(uint(delay) and uint(row.get("maxQueueDelayMs")) and delay <= row["maxQueueDelayMs"], "queue-delay-exceeds-maximum")
        if uint(delay) and delay > 3000:
            require(uint(row.get("delayedMessages")) and row["delayedMessages"] > 0, "delayed-queue-not-counted")
    elif qknown is False:
        require("postedTick" in witness and "queueDelayMs" in witness and posted is None and delay is None, "unknown-queue-has-time-or-missing-null")
        require(uint(row.get("queueTimestampsUnknown")) and row["queueTimestampsUnknown"] > 0, "unknown-queue-not-counted")
    errors.extend(validate_witness_controls(witness.get("controls"), witness.get("reportId")))
    result["queueStatus"] = "known" if qknown is True else "unknown" if qknown is False else "invalid"
    return result


def summarize_raw_input_receipts(items, session):
    """Only session/path/handle/generation identity joins; retain every assessment."""
    windows, groups, unassociated, all_observations = [], {}, [], []
    by_sequence = defaultdict(list)
    for item in sorted(items, key=observation_key):
        window = item["record"]
        if window.get("event") != RAW_RECEIPT_EVENT:
            continue
        rid = item["recordId"]
        werrors = []
        begin, end = window.get("windowStartTick"), window.get("windowEndTick")
        if not (uint(begin) and uint(end) and begin <= end <= window["tick"]):
            werrors.append("invalid-window-ticks-or-session-boundary")
        if "semanticWitnessVersion" in window and not (integer(window["semanticWitnessVersion"]) and window["semanticWitnessVersion"] == 1):
            werrors.append("unsupported-or-invalid-window-witness-version")
        rows = window.get("collections")
        if not isinstance(rows, list):
            werrors.append("collections-not-array")
        windows.append({"recordId": rid, "windowStartTick": begin, "windowEndTick": end,
                        "validationStatus": "invalid" if werrors else "valid", "validationErrors": werrors})
        if not isinstance(rows, list):
            continue
        for row_index, row in enumerate(rows):
            errors = list(werrors)
            obs = {"recordId": rid, "collectionIndex": row_index, "windowStartTick": begin, "windowEndTick": end,
                   "loggedCollection": row, "validationErrors": errors, "semanticWitnesses": []}
            all_observations.append(obs)
            if not isinstance(row, dict):
                errors.append("collection-not-object")
                unassociated.append(obs)
                continue
            identity_ok = (isinstance(row.get("interfacePath"), str) and bool(row["interfacePath"])
                           and all(uint(row.get(k)) and row[k] > 0 for k in ("rawDeviceHandle", "deviceGeneration")))
            if identity_ok:
                key = (row["interfacePath"], row["rawDeviceHandle"], row["deviceGeneration"])
                if key not in groups:
                    groups[key] = {"sessionId": session["sessionId"], "interfacePath": key[0], "rawDeviceHandle": key[1],
                                   "deviceGeneration": key[2], "observations": []}
                groups[key]["observations"].append(obs)
            else:
                key = None
                errors.append("missing-or-invalid-exact-collection-identity")
                unassociated.append(obs)
            role = raw_collection_role(row)
            main = role in ("msi-dinput-main-collection", "msi-xinput-game-collection")
            obs["qualifiedCollectionRole"] = role
            obs["semanticEligible"] = main
            if role == "unqualified" or row.get("collectionRole") != role:
                errors.append("unqualified-or-inconsistent-collection-role")
            for field in RAW_COUNTERS:
                if not uint(row.get(field)):
                    errors.append(field + "-invalid-or-missing")
            if all(uint(row.get(k)) for k in RAW_COUNTERS):
                if not (row["changedPayloadReports"] <= row["reportReceipts"] and row["reportReceipts"] >= row["messageReceipts"]
                        and row["queueTimestampsKnown"] + row["queueTimestampsUnknown"] == row["messageReceipts"]
                        and row["delayedMessages"] <= row["queueTimestampsKnown"]):
                    errors.append("receipt-or-queue-counts-inconsistent")
                if row["queueTimestampsKnown"]:
                    if not (row["firstQueuedTick"] <= row["lastQueuedTick"] <= (end if uint(end) else -1)
                            and row["maxQueueDelayMs"] <= 900000 and (row["maxQueueDelayMs"] > 3000) == (row["delayedMessages"] > 0)):
                        errors.append("queue-range-or-delay-inconsistent")
                elif any(row[k] for k in ("firstQueuedTick", "lastQueuedTick", "maxQueueDelayMs", "delayedMessages")):
                    errors.append("unknown-queue-has-aggregate-time")
            if not (uint(row.get("firstReceiptTick")) and uint(row.get("lastReceiptTick")) and uint(begin) and uint(end)
                    and max(begin, session["identity"]["tick"]) <= row["firstReceiptTick"] <= row["lastReceiptTick"] <= end):
                errors.append("invalid-collection-receipt-range")
            if "reportBytesLogged" in row and row["reportBytesLogged"] is not False:
                errors.append("collection-report-bytes-not-explicitly-unlogged")
            if "semanticInputDecoded" in row and (type(row["semanticInputDecoded"]) is not bool
                    or uint(row.get("decodedWitnesses")) and row["semanticInputDecoded"] is not (row["decodedWitnesses"] > 0)):
                errors.append("semanticInputDecoded-inconsistent")
            version = row.get("semanticWitnessVersion")
            legacy = "semanticWitnessVersion" not in row and "semanticWitnesses" not in row and not any(k in row for k in WITNESS_COUNTERS)
            obs["witnessFormatStatus"] = "legacy-no-witness-unknown" if legacy else "version-1" if integer(version) and version == 1 else "unsupported-or-invalid"
            witnesses = row.get("semanticWitnesses", []) if legacy else row.get("semanticWitnesses")
            if not legacy and not (integer(version) and version == 1):
                errors.append("unsupported-or-invalid-collection-witness-version")
            if not isinstance(witnesses, list):
                errors.append("semanticWitnesses-not-array")
                witnesses = []
            if len(witnesses) > 2:
                errors.append("more-than-two-retained-witnesses")
            if not legacy:
                for field in WITNESS_COUNTERS:
                    if not uint(row.get(field)):
                        errors.append(field + "-invalid-or-missing")
                if all(uint(row.get(k)) for k in WITNESS_COUNTERS):
                    c, selected, suppressed, rollback, overwritten, decoded, complete = (row[k] for k in WITNESS_COUNTERS)
                    if not (c == selected + suppressed and rollback <= suppressed and len(witnesses) == min(2, selected)
                            and overwritten == max(0, selected - 2) and complete <= decoded <= selected
                            and (c == row.get("messageReceipts") if main else c == selected == decoded == complete == 0)):
                        errors.append("semantic-witness-counts-inconsistent")
                    for counter, flag in ((decoded, "decodedControlsKnown"), (complete, "decodeComplete")):
                        retained = sum(isinstance(w, dict) and isinstance(w.get("controls"), dict) and w["controls"].get(flag) is True for w in witnesses)
                        if not retained <= counter <= retained + overwritten:
                            errors.append(flag + "-counter-inconsistent")
            if not main and witnesses:
                errors.append("non-game-collection-has-semantic-witness")
            # When all messages are represented, their exact aggregates must agree too.
            known_samples = [w for w in witnesses if isinstance(w, dict) and w.get("queueTimestampKnown") is True
                             and uint(w.get("postedTick")) and uint(w.get("queueDelayMs"))]
            if known_samples and len(known_samples) == row.get("queueTimestampsKnown"):
                if (min(w["postedTick"] for w in known_samples) != row.get("firstQueuedTick")
                        or max(w["postedTick"] for w in known_samples) != row.get("lastQueuedTick")
                        or max(w["queueDelayMs"] for w in known_samples) != row.get("maxQueueDelayMs")
                        or sum(w["queueDelayMs"] > 3000 for w in known_samples) != row.get("delayedMessages")):
                    errors.append("fully-sampled-queue-aggregates-inconsistent")
            if witnesses and len(witnesses) == row.get("messageReceipts") and all(
                    isinstance(w, dict) and uint(w.get("receiptTick")) and uint(w.get("batchReportCount")) for w in witnesses):
                if (min(w["receiptTick"] for w in witnesses) != row.get("firstReceiptTick")
                        or max(w["receiptTick"] for w in witnesses) != row.get("lastReceiptTick")
                        or sum(w["batchReportCount"] for w in witnesses) != row.get("reportReceipts")):
                    errors.append("fully-sampled-receipt-aggregates-inconsistent")
            for wi, witness in enumerate(witnesses):
                assessed = assess_raw_witness(witness, row, window, errors)
                assessed.update({"recordId": rid, "collectionIndex": row_index, "witnessIndex": wi})
                obs["semanticWitnesses"].append(assessed)
                if isinstance(witness, dict) and uint(witness.get("selectionSequence")) and witness["selectionSequence"] > 0:
                    by_sequence[witness["selectionSequence"]].append((key, assessed))
            retained_known = sum(isinstance(w, dict) and w.get("queueTimestampKnown") is True for w in witnesses)
            retained_unknown = sum(isinstance(w, dict) and w.get("queueTimestampKnown") is False for w in witnesses)
            batch_counts = [w.get("batchReportCount") for w in witnesses if isinstance(w, dict)]
            if (uint(row.get("queueTimestampsKnown")) and retained_known > row["queueTimestampsKnown"]
                    or uint(row.get("queueTimestampsUnknown")) and retained_unknown > row["queueTimestampsUnknown"]
                    or all(uint(c) for c in batch_counts) and uint(row.get("reportReceipts")) and sum(batch_counts) > row["reportReceipts"]):
                errors.append("retained-witness-receipts-exceed-counters")
                for assessed in obs["semanticWitnesses"]:
                    assessed["validationErrors"].append("retained-witness-receipts-exceed-counters")
            if len(witnesses) > 1 and all(isinstance(w, dict) and uint(w.get("selectionSequence")) for w in witnesses):
                if any(b["selectionSequence"] <= a["selectionSequence"] for a, b in zip(witnesses, witnesses[1:])):
                    errors.append("retained-witness-order-inconsistent")
                    for assessed in obs["semanticWitnesses"]:
                        assessed["validationErrors"].append("retained-witness-order-inconsistent")
    for collection in groups.values():
        observations = collection["observations"]
        metadata = {canonical({k: o["loggedCollection"].get(k) for k in ("vid", "pid", "usagePage", "usage", "collectionRole")}) for o in observations}
        window_counts = Counter(o["recordId"] for o in observations)
        for obs in observations:
            reason = ("same-tuple-metadata-conflict" if len(metadata) > 1 else
                      "duplicate-collection-tuple-in-window" if window_counts[obs["recordId"]] > 1 else None)
            if reason:
                obs["validationErrors"].append(reason)
                for w in obs["semanticWitnesses"]:
                    w["validationErrors"].append(reason)
    # A selectionSequence belongs to the whole observer session, including all generations.
    unique = []
    for sequence, occurrences in sorted(by_sequence.items()):
        key, first = next(((k, w) for k, w in occurrences if not w["validationErrors"]), occurrences[0])
        same = key is not None and all(k == key and canonical(w["loggedWitness"]) == canonical(first["loggedWitness"]) for k, w in occurrences)
        if len(occurrences) > 1 and not same:
            for _, w in occurrences:
                w["validationErrors"].append("selection-sequence-conflict-across-observations")
        elif same and not first["validationErrors"]:
            for _, w in occurrences:
                if w is first or w["validationErrors"]:
                    continue
                w["duplicateOf"] = {k: first[k] for k in ("recordId", "collectionIndex", "witnessIndex")}
        if not first["validationErrors"]:
            unique.append(first)
    for previous, current in zip(unique, unique[1:]):
        sequence_gap = current["loggedWitness"]["selectionSequence"] - previous["loggedWitness"]["selectionSequence"]
        if current["loggedWitness"]["receiptTick"] < previous["loggedWitness"]["receiptTick"] + 500 * sequence_gap:
            for w in (previous, current):
                w["validationErrors"].append("selection-clock-or-global-budget-inconsistent")
    for occurrences in by_sequence.values():
        if any(w["validationErrors"] for _, w in occurrences if "duplicateOf" not in w):
            for _, w in occurrences:
                if "duplicateOf" in w:
                    w["validationErrors"].append("duplicate-source-not-valid")
    for obs in all_observations:
        for w in obs["semanticWitnesses"]:
            controls = w["loggedWitness"].get("controls", {}) if isinstance(w["loggedWitness"], dict) else {}
            if not isinstance(controls, dict):
                controls = {}
            w["validationStatus"] = "invalid" if w["validationErrors"] else "duplicate" if "duplicateOf" in w else "valid"
            if w["validationErrors"]:
                w["queueStatus"] = "invalid"
            valid = w["validationStatus"] == "valid"
            w["decodedControlsKnown"] = valid and controls.get("decodedControlsKnown") is True
            w["decodeComplete"] = valid and controls.get("decodeComplete") is True
            w["semanticStatus"] = "complete" if w["decodeComplete"] else "decoded" if w["decodedControlsKnown"] else "unknown"
        obs["validationStatus"] = "invalid" if obs["validationErrors"] or any(
            w["validationStatus"] == "invalid" for w in obs["semanticWitnesses"]) else "valid"
    collections = list(groups.values())
    def witness_totals(observations):
        witnesses = [w for o in observations for w in o["semanticWitnesses"]]
        decoded = sum(w["decodedControlsKnown"] for w in witnesses)
        return {"retainedWitnessObservations": len(witnesses), "validUniqueWitnesses": sum(w["validationStatus"] == "valid" for w in witnesses),
                "duplicateWitnessObservations": sum(w["validationStatus"] == "duplicate" for w in witnesses),
                "invalidWitnessObservations": sum(w["validationStatus"] == "invalid" for w in witnesses),
                "decodedWitnesses": decoded, "completeWitnesses": sum(w["decodeComplete"] for w in witnesses),
                "semanticEvidenceStatus": "decoded-samples-only" if decoded else "unknown"}
    for collection in collections:
        collection.update(witness_totals(collection["observations"]))
        collection["loggedCounterSums"] = {field: sum(o["loggedCollection"][field] for o in collection["observations"])
            if all(uint(o["loggedCollection"].get(field)) for o in collection["observations"]) else None
            for field in RAW_COUNTERS + WITNESS_COUNTERS}
        collection["counterScope"] = "sums of logged observation counters only, including invalid claims; not unique packets or trusted semantic counts"
        collection["recordIds"] = list(dict.fromkeys(o["recordId"] for o in collection["observations"]))
    return {"scope": "independent WM_INPUT receipt windows; exact session/path/handle/generation only; no time-proximity or DI/PnP/open/config joins",
            "windowObservations": len(windows), "invalidWindowObservations": sum(bool(w["validationErrors"]) for w in windows),
            "invalidCollectionObservations": sum(o["validationStatus"] == "invalid" for o in all_observations),
            "recordIds": [w["recordId"] for w in windows], "windows": windows,
            "collections": collections, "unassociatedObservations": unassociated, **witness_totals(all_observations),
            "hardwareSampleTimeKnown": False, "freshPhysicalInputDeliveryProven": False, "steamConsumptionProven": False,
            "idle": "unknown", "phantom": "unknown", "suppressionLeak": "unknown", "selfHealingTriggered": False,
            "steamExemptionResolved": False, "observerExemptionResolved": False}


def summarize_session(session, native, archives):
    items = session["items"]
    last = max(x["record"]["utcEpochMs"] for x in items)
    first = min(x["record"]["utcEpochMs"] for x in items)
    session["nativeScope"] = session_native_scope(session, native, archives)
    session.pop("items")
    interfaces, unmapped = summarize_interfaces(items, session["formatVersion"])
    add_native_path_context(interfaces, items, session["nativeScope"]["config"], native)
    session.update({"observedBeginUtc": utc_text(first), "observedEndUtc": utc_text(last),
                    "observedEndEpochMs": last, "observedSpanMs": last - first,
                    "stopUtc": utc_text(session["stopEpochMs"]), "rightCensored": not bool(session["stopRecordIds"]),
                    "rightCensorScope": "no stop observed; neither forced-exit nor completion is inferred",
                    "archives": sorted({p["archiveId"] for x in items for p in x["provenance"]}),
                    "uniqueRows": len(items), "recordIds": [x["recordId"] for x in items],
                    "eventCounts": dict(sorted(Counter(x["record"].get("event", "<missing>") for x in items).items())),
                    "inventoryCoverage": inventory_coverage(items), "interfaces": interfaces,
                    "unmappedDiOrPnpRecordIds": unmapped, "wgi": summarize_wgi(items),
                    "rawInputReceipts": summarize_raw_input_receipts(items, session),
                    "freshPhysicalInputDeliveryProven": False, "steamConsumptionProven": False,
                    "darkScreenCausationProven": False})
    return session


def archive_native_scope(archive, native, archives):
    pid = archive["manifest"].get("processId")
    try:
        pid = int(pid)
    except (TypeError, ValueError):
        pid = None
    export = archive["exportEpochMs"]
    source = [x for x in native if any(p["archiveId"] == archive["archiveId"] for p in x["provenance"])]
    pid_rows = [x for x in source if pid is not None and x["record"].get("pid") == pid
                and native_ms(x, archives) is not None and export is not None and native_ms(x, archives) <= export]
    boots = [native_ms(x, archives) for x in pid_rows if x["record"].get("event") == "boot-single-instance-acquired"]
    begin = max(boots) if boots else min((native_ms(x, archives) for x in pid_rows), default=None)
    config = [x for x in pid_rows if x["record"].get("event") in CONFIG_EVENTS and begin is not None and native_ms(x, archives) >= begin]
    return {"manifestPid": pid, "beginEpochMs": begin, "beginUtc": utc_text(begin),
            "endEpochMs": export, "endUtc": utc_text(export), "leftCensored": not bool(boots),
            "basis": "manifest processId, latest same-PID boot through export; never other archived PID",
            "allArchiveVisibilitySnapshotCount": sum(x["record"].get("event") == "hidhide.visibility-snapshot" for x in source),
            "config": config_summary(config, archives)}


def audit(zip_paths, latest_zip=None, default_offset_minutes=480):
    archives = []
    observer_pool, native_pool = {}, {}
    malformed = []
    for index, value in enumerate(zip_paths):
        path = Path(value).resolve()
        archive = {"archiveId": index, "path": str(path), "sha256": file_sha256(path),
                   "manifest": {}, "manifestMembers": [], "observerMembers": [], "nativeMembers": [],
                   "observerRecordIds": [], "nativeRecordIds": [], "timeZoneOffsetMinutes": default_offset_minutes}
        with zipfile.ZipFile(path) as zf:
            for member_index, info in enumerate(zf.infolist()):
                if info.is_dir():
                    continue
                if PurePosixPath(info.filename.replace("\\", "/")).name == "YeManCC-export-manifest.txt":
                    text = zf.read(info).decode("utf-8-sig", errors="replace")
                    kv = dict(line.split("=", 1) for line in text.splitlines() if "=" in line)
                    archive["manifestMembers"].append({"member": info.filename, "memberIndex": member_index, "values": kv})
                    if not archive["manifest"]:
                        archive["manifest"] = kv
            try:
                archive["timeZoneOffsetMinutes"] = -int(archive["manifest"]["timeZoneBiasMinutes"])
                if not -1439 <= archive["timeZoneOffsetMinutes"] <= 1439:
                    raise ValueError("timezone outside valid offset")
                archive["timeZoneBasis"] = "manifest timeZoneBiasMinutes (Windows bias sign reversed)"
            except (KeyError, ValueError):
                archive["timeZoneOffsetMinutes"] = default_offset_minutes
                archive["timeZoneBasis"] = "CLI fallback; manifest offset absent/invalid"
            archive["exportEpochMs"] = local_epoch_ms(archive["manifest"].get("exportedAtLocal"), archive["timeZoneOffsetMinutes"])
            for member_index, info in enumerate(zf.infolist()):
                if info.is_dir():
                    continue
                if member_matches(info.filename, OBSERVER_BASE):
                    summary, ids = read_json_lines(zf, info, archive, member_index, observer_pool, malformed)
                    archive["observerMembers"].append(summary)
                    archive["observerRecordIds"].extend(ids)
                elif member_matches(info.filename, NATIVE_BASE):
                    scratch = {}
                    summary, _ = read_json_lines(zf, info, archive, member_index, scratch, malformed)
                    archive["nativeMembers"].append(summary)
                    for key, item in scratch.items():
                        if item["record"].get("event") not in CONFIG_EVENTS | LIFETIME_EVENTS:
                            continue
                        native_pool.setdefault(key, {"recordId": item["recordId"], "record": item["record"], "provenance": []})["provenance"].extend(item["provenance"])
                        archive["nativeRecordIds"].append(item["recordId"])
        archives.append(archive)
    if not archives:
        raise ValueError("at least one ZIP is required")
    if latest_zip is not None:
        target = str(Path(latest_zip).resolve()).casefold()
        latest_ids = [a["archiveId"] for a in archives if a["path"].casefold() == target]
        if not latest_ids:
            raise ValueError("--latest-zip must also be included among --zip inputs")
        latest_basis = "explicit --latest-zip; latest only among the supplied/identified archives"
    else:
        dated = [a for a in archives if a["exportEpochMs"] is not None]
        if not dated:
            raise ValueError("manifest export dates absent; specify --latest-zip rather than guessing from file mtime")
        newest = max(a["exportEpochMs"] for a in dated)
        latest_ids = [a["archiveId"] for a in dated if a["exportEpochMs"] == newest]
        latest_basis = "maximum manifest exportedAtLocal converted using per-archive timezone; ties retained"
    observers = sorted(observer_pool.values(), key=observation_key)
    native = sorted(native_pool.values(), key=lambda x: (native_ms(x, archives) or -1, x["recordId"]))
    sessions, unassigned, invalid_starts = build_sessions(observers)
    sessions = [summarize_session(s, native, archives) for s in sessions]
    session_map = {s["sessionId"]: s for s in sessions}
    item_by_id = {x["recordId"]: x for x in observers}
    for archive in archives:
        ids = set(archive["observerRecordIds"])
        sids = sorted({item_by_id[rid]["sessionId"] for rid in ids if "sessionId" in item_by_id[rid]})
        current = archive_native_scope(archive, native, archives)
        current_sessions = []
        for sid in sids:
            s = session_map[sid]
            begin, end = current["beginEpochMs"], current["endEpochMs"]
            if (s["identity"]["parentPid"] == current["manifestPid"] and begin is not None and end is not None
                    and s["parentCreationEpochMs"] is not None and begin - 30000 <= s["parentCreationEpochMs"] <= begin
                    and begin <= s["startEpochMs"] <= end):
                current_sessions.append(sid)
        archive.update({"observerPresence": "missing" if not archive["observerMembers"] else "present" if ids else "present-empty-or-unparseable",
                        "observerRawValidRows": len(archive["observerRecordIds"]), "observerUniqueRows": len(ids),
                        "observerSessionIds": sids, "currentNativeProcessObserverSessionIds": current_sessions,
                        "nativeCurrentExportScope": current, "latest": archive["archiveId"] in latest_ids})
    raw_count = sum(a["observerRawValidRows"] for a in archives)
    observation_summary = summarize_all_observations(sessions)
    return {"schemaVersion": SCHEMA_VERSION, "scope": "ZIP_BYTES_ONLY_OFFLINE_NO_PRODUCT_NO_ACTUAL_PROBE_NO_SYSTEM_MUTATION",
        "generatedAtUtc": datetime.now(timezone.utc).isoformat(timespec="seconds"),
        "canonicalization": "strict finite JSON, unique object keys, sorted keys/compact whitespace; full original lines retained; not RFC8785 numeric normalization",
        "deduplicationKey": "canonical full JSON per observer/native domain, not archive name or device VID/PID; observer starts and exact parent FILETIME bound session scope",
        "provenanceScope": "all occurrences, including repeated ZIPs/duplicate member names (memberIndex distinguishes them); line is one-based",
        "archives": archives, "latestArchiveIds": latest_ids, "latestSelectionBasis": latest_basis,
        "latestObserverMissing": all(archives[i]["observerPresence"] == "missing" for i in latest_ids),
        "latestObserverEvidenceUsable": any(archives[i]["observerUniqueRows"] > 0 for i in latest_ids),
        "olderObserverSessionsAreNotLatestEvidence": True,
        "observerRawValidRows": raw_count, "observerUniqueRows": len(observers),
        "duplicateObserverOccurrencesRemoved": raw_count - len(observers),
        "uniqueObserverEventCounts": dict(sorted(Counter(x["record"].get("event", "<missing>") for x in observers).items())),
        "sessions": sessions, "observationSummary": observation_summary, "unassignedObserverRecords": unassigned, "invalidStartRecordIds": invalid_starts,
        "observerRecords": observers, "nativeEvidenceRecords": native, "malformedRows": malformed,
        "malformedRowCount": len(malformed), "limitations": LIMITATIONS,
        "freshPhysicalInputDeliveryProven": False, "steamConsumptionProven": False, "darkScreenCausationProven": False}



def add_native_path_context(interfaces, items, config, native):
    by_id = {x["recordId"]: x for x in items}
    native_by_id = {x["recordId"]: x for x in native}
    snapshots = [p for p in config["points"] if p["event"] == "hidhide.visibility-snapshot"]
    for interface in interfaces:
        contexts = []
        for rid in interface["recordIds"]:
            r = by_id[rid]["record"]
            if r.get("event") != "directinput-device" and r.get("freshMainHidReadProbeAttempted") is not True:
                continue
            preceding = [p for p in snapshots if p["epochMs"] is not None and p["epochMs"] <= r["utcEpochMs"]]
            if not preceding:
                continue
            point = max(preceding, key=lambda p: p["epochMs"])
            raw = native_by_id[point["recordId"]]["record"]
            inventory = raw.get("targetInventory")
            matches = [v for v in inventory if isinstance(v, dict) and v.get("interfacePath") == interface["interfacePath"]] if isinstance(inventory, list) else []
            contexts.append({"observerRecordId": rid, "snapshotRecordId": point["recordId"],
                             "snapshotTimeLocal": point["timeLocal"], "snapshotEpochMs": point["epochMs"],
                             "observationEpochMs": r["utcEpochMs"], "snapshotAgeMs": r["utcEpochMs"] - point["epochMs"],
                             "exactPathTargetInventoryMatches": matches,
                             "cloakOnAtSnapshot": point["cloakOn"], "inverseAtSnapshot": point["inverseState"],
                             "applicationsAtSameSnapshot": point["sameSnapshotApplicationsInverseCloak"],
                             "scope": "nearest preceding POINT snapshot in this parent lifetime; age retained, NOT proof configuration persisted until this read/open"})
        interface["nativeVisibilityPointContext"] = contexts


def summarize_all_observations(sessions):
    interfaces = [p for s in sessions for p in s["interfaces"]]
    di = [p["directInput"] for p in interfaces]
    wgi = [w for s in sessions for w in s["wgi"]]
    summary = {"scope": "assigned unique observations, not simultaneous devices or fresh physical reports",
               "directInput": {key: sum(d[key] for d in di) for key in
                   ("observations", "enumDeviceObservations", "stateApiSucceeded", "loggedDecodedContentChangedTrue", "successfulLoggedUnchanged", "buttonsNonzeroObservations")},
               "recordedControls": {key: sum(d["recordedControlComparison"][key] for d in di) for key in
                   ("comparedPairs", "changedPairs", "unchangedPairs", "loggedChangedButRecordedUnchangedPairs")},
               "wgi": {key: sum(w[key] for w in wgi) for key in
                   ("observations", "stateApiSucceeded", "loggedDecodedContentChangedTrue", "successfulLoggedUnchanged", "buttonsNonzeroObservations")},
               "pnpObservations": sum(p["pnp"]["observations"] for p in interfaces),
               "qualifiedMainInterfaces": []}
    for key in ("create", "acquire", "getState"):
        summary["directInput"][key] = {k: sum(d[key][k] for d in di) for k in ("attempted", "succeeded", "failed", "notRecorded")}
    for s in sessions:
        for p in s["interfaces"]:
            if p["opens"]["qualifiedMainReadAccessAttempted"]:
                summary["qualifiedMainInterfaces"].append({"sessionId": s["sessionId"],
                    "observerPid": s["identity"]["observerPid"], "parentPid": s["identity"]["parentPid"],
                    "parentCreationTime": s["identity"]["parentCreationTime"], "interfacePath": p["interfacePath"],
                    "directInputObservations": p["directInput"]["observations"],
                    "directInputStateApiSucceeded": p["directInput"]["stateApiSucceeded"],
                    "pnpObservations": p["pnp"]["observations"], "opens": p["opens"],
                    "nativeVisibilityPointContext": p["nativeVisibilityPointContext"],
                    "meaning": "qualified-main is the probe's logged attempted flag; read-open/DI values are not fresh physical or Steam delivery"})
    return summary


def markdown(report):
    lines = ["# 外部 controller observer 离线证据审计", "",
             "只读取提供的 ZIP 字节；未启动产品/探测，未修改系统服务、任务、注册表、设备或 HidHide。", "",
             "## 最新 ZIP 与采集覆盖", "",
             "| ZIP | 导出本地时间 | observer 日志 | 原始有效行 | 去重行 | 当前 native 会话 observer |",
             "|---|---|---|---:|---:|---:|"]
    for a in report["archives"]:
        name = Path(a["path"]).name.replace("|", "\\|")
        lines.append(f"| {'**最新** ' if a['latest'] else ''}{name} | {a['manifest'].get('exportedAtLocal', 'unknown')} | {a['observerPresence']} | {a['observerRawValidRows']} | {a['observerUniqueRows']} | {len(a['currentNativeProcessObserverSessionIds'])} |")
    lines += ["", f"最新判定：{report['latestSelectionBasis']}。"]
    if report["latestObserverMissing"]:
        lines.append("**最新 ZIP 的 msi-controller-visibility.log 及轮转日志均为 missing。E/F 旧 observer 会话不能替代最新证据。**")
    totals = report["observationSummary"]
    d, c, w = totals["directInput"], totals["recordedControls"], totals["wgi"]
    lines += ["", "## 关键发现", "",
              f"- DI Enum {d['enumDeviceObservations']}；Create 成功 {d['create']['succeeded']}；Acquire 成功 {d['acquire']['succeeded']}；GetState 成功 {d['getState']['succeeded']}。",
              f"- DI 记录的 hash changed：{d['loggedDecodedContentChangedTrue']}（v5 是旧全结构 hash，不能解释为实际记录轴/按钮变化）；成功且 logged unchanged：{d['successfulLoggedUnchanged']}；按钮非零：{d['buttonsNonzeroObservations']}。",
              f"- 独立重算相邻成功库存的记录控制值：{c['comparedPairs']} 对，变化 {c['changedPairs']} 对；不能证明物理新鲜度。",
              f"- WGI 成功 {w['stateApiSucceeded']} / {w['observations']}；logged changed {w['loggedDecodedContentChangedTrue']}；按钮非零 {w['buttonsNonzeroObservations']}；不做 MI/COL 映射。PnP 独立观察 {totals['pnpObservations']}。"]
    for m in totals["qualifiedMainInterfaces"]:
        lines.append(f"- Qualified-main exact path `{m['interfacePath']}`，observer {m['observerPid']} / parent {m['parentPid']}：DI 成功 {m['directInputStateApiSucceeded']} / {m['directInputObservations']}；PnP {m['pnpObservations']}；main read-access open {m['opens']['qualifiedMainReadAccessSucceeded']} / {m['opens']['qualifiedMainReadAccessAttempted']}（无 ReadFile 流证据）。")
    lines += ["", "## 真正去重后的统计", "",
              f"observer 原始有效行 **{report['observerRawValidRows']}**；canonical 唯一行 **{report['observerUniqueRows']}**；去除重叠/重复出现 **{report['duplicateObserverOccurrencesRemoved']}**。",
              f"有效会话 **{len(report['sessions'])}**；未归属行 **{len(report['unassignedObserverRecords'])}**；malformed 行 **{report['malformedRowCount']}**（细节保留原始 provenance）。", "",
              "事件唯一计数：`" + canonical(report["uniqueObserverEventCounts"]) + "`。", "",
              "## 会话与 exact interfacePath", ""]
    for s in report["sessions"]:
        i = s["identity"]
        lines += [f"### observer {i['observerPid']} / parent {i['parentPid']} / creationTime {i['parentCreationTime']}", "",
                  f"- sessionId：`{s['sessionId']}`；formatVersion：{s['formatVersion']}。",
                  f"- UTC 观察范围：{s['observedBeginUtc']} → {s['observedEndUtc']}；{s['uniqueRows']} 唯一行；stop {'已捕获' if s['stopRecordIds'] else '未捕获/right-censored'}。",
                  f"- 库存覆盖：`{canonical({k: v['observations'] for k, v in s['inventoryCoverage'].items()})}`；不是同时设备数或完整采样频率。",
                  "- **v5 decodedContentChanged = 旧 DIJOYSTATE2 全结构 hash + 可跨间断 GUID prior；不推导已记录轴/按钮变化。**" if s['formatVersion'] == 5 else "- decodedContentChanged 按原始 scope 保存，不凭版本号猜新口径。", "",
                  "| exact interfacePath | DI Enum | Create 成功 | Acquire 成功 | GetState 成功 | logged changed（v5旧口径） | 连续记录值变化/比较 | 按钮非零 | PnP 行 | DI read-open | main read-open/attempt |",
                  "|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|"]
        for path in s["interfaces"]:
            di, opens = path["directInput"], path["opens"]
            comp = di["recordedControlComparison"]
            lines.append(f"| `{path['interfacePath']}` | {di['enumDeviceObservations']} | {di['create']['succeeded']} | {di['acquire']['succeeded']} | {di['getState']['succeeded']} | {di['loggedDecodedContentChangedTrue']} | {comp['changedPairs']}/{comp['comparedPairs']} | {di['buttonsNonzeroObservations']} | {path['pnp']['observations']} | {opens['directInputFreshReadAccessSucceeded']} | {opens['qualifiedMainReadAccessSucceeded']}/{opens['qualifiedMainReadAccessAttempted']} |")
        wgi_count = sum(x["observations"] for x in s["wgi"])
        lines += ["", f"WGI：{wgi_count} 观察行、{len(s['wgi'])} 个记录 identity；保持独立，**不猜 MI/COL**。成功、变化、按钮计数及 readingTimestamp 见 JSON。", ""]
        raw = s["rawInputReceipts"]
        lines += [f"WM_INPUT：{raw['windowObservations']} 窗口、{len(raw['collections'])} 个精确 path/handle/generation；有效唯一见证 {raw['validUniqueWitnesses']}，decoded {raw['decodedWitnesses']}，complete {raw['completeWitnesses']}，invalid {raw['invalidWitnessObservations']}，重复 {raw['duplicateWitnessObservations']}。",
                  "旧格式/缺少见证保持 unknown；payload changed、队列时间、完整解码均不推导 idle、phantom、suppressionLeak、Steam 消费、resolved exemption 或自愈。每个观察及无效原因、recordId 见 JSON。", ""]
        scope = s["nativeScope"]
        cfg = scope["config"]
        lines += [f"HidHide native scope：{scope.get('scopeBeginUtc')} → {scope.get('scopeEndUtc')}；{cfg['visibilitySnapshots']} snapshots、{cfg['textApplicationListRows']} 独立 applications lists；同 snapshot applications+inverse+cloak 完整次数 **{cfg['completeSameSnapshotCount']}**。",
                  "各点保留 local/UTC 时间、phase、powerGeneration、原始 recordId/provenance；不声明 Steam/observer 豁免 resolved，不把点快照扩展为持续生效证明。", ""]
    lines += ["## 最新 native 配置（不能补齐 observer）", ""]
    for aid in report["latestArchiveIds"]:
        a = report["archives"][aid]
        scope = a["nativeCurrentExportScope"]
        cfg = scope["config"]
        lines += [f"- `{Path(a['path']).name}`：manifest PID {scope['manifestPid']}；{scope['beginUtc']} → {scope['endUtc']}；当前 native lifetime 有 {cfg['visibilitySnapshots']} 个 snapshots、{cfg['textApplicationListRows']} 个独立 application lists（整个 ZIP 的 snapshots 为 {scope['allArchiveVisibilitySnapshotCount']}，不混合历史 PID）。",
                  f"- 同 snapshot applications/inverse/cloak 完整次数：{cfg['completeSameSnapshotCount']}；Steam/observer resolved 仍未证实。"]
    lines += ["", "## 缺证与解释边界", ""] + ["- " + x for x in report["limitations"]]
    lines += ["", "## 可复查性", "",
              "JSON 保存 canonical recordId、完整 parsed record、所有 archive/memberIndex/member/line、原始行与 SHA256；native 保留所需配置/boot/exit 行，所有日志成员记录 bytes/lineCount/尾部状态及 malformed provenance。",
              "没有根据 E/F 正常值自动证明物理新鲜输入、Steam 消费、屏蔽失败或暗屏原因。", ""]
    return "\n".join(lines)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--zip", dest="zips", action="append", type=Path, required=True,
                        help="repeat for each ZIP; duplicate inputs retain provenance")
    parser.add_argument("--latest-zip", type=Path, help="explicit latest archive; must also be a --zip input")
    parser.add_argument("--timezone-offset-minutes", type=int, default=480,
                        help="fallback local-minus-UTC offset when manifest bias is absent (default +480)")
    parser.add_argument("--output", type=Path, required=True, help="JSON path; Markdown uses the same stem")
    args = parser.parse_args(argv)
    if not -1439 <= args.timezone_offset_minutes <= 1439:
        parser.error("timezone offset must be between -1439 and 1439 minutes")
    json_path = args.output.resolve()
    if json_path.suffix.lower() != ".json":
        parser.error("--output must end in .json")
    md_path = json_path.with_suffix(".md")
    if json_path == md_path or any(p.resolve() in (json_path, md_path) for p in args.zips):
        parser.error("output must be a distinct JSON path and must not overwrite an input ZIP")
    try:
        report = audit(args.zips, args.latest_zip, args.timezone_offset_minutes)
        json_path.parent.mkdir(parents=True, exist_ok=True)
        json_path.write_text(json.dumps(report, ensure_ascii=True, indent=2, allow_nan=False) + "\n", encoding="utf-8")
        md_path.write_text(markdown(report), encoding="utf-8")
    except (OSError, ValueError, zipfile.BadZipFile, RuntimeError) as exc:
        print("offline audit failed: " + str(exc), file=sys.stderr)
        return 1
    print(json.dumps({"json": str(json_path), "markdown": str(md_path),
                      "sessions": len(report["sessions"]), "uniqueObserverRows": report["observerUniqueRows"],
                      "duplicateOccurrencesRemoved": report["duplicateObserverOccurrencesRemoved"],
                      "latestObserverMissing": report["latestObserverMissing"],
                      "malformedRows": report["malformedRowCount"]}, ensure_ascii=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
