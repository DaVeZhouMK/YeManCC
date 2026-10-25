"""Offline, bounded facts from an explicitly supplied ordinary YMCC log export.

No discovery, extraction, commands, network, application or device operations.
The output is an evidence index, never a recovery/input/Continue verdict. Run:
  python -B tools/steam_log_contact_timeline.py INPUT.zip --output OUTPUT.json
Only the standard library is required. See --help for the finite read limits.
"""
from __future__ import annotations

import argparse
import bisect
import datetime as dt
import hashlib
import io
import json
import math
import os
from pathlib import Path
import re
import stat
import struct
import sys
import zipfile

LIMITS = dict(archiveBytes=256*1024*1024, centralBytes=8*1024*1024,
              members=4096, memberBytes=8*1024*1024, totalBytes=32*1024*1024,
              lineBytes=1024*1024, rows=200000, events=20000,
              nodes=200000, depth=64, scalarChars=4096)
ROTATION = r"(?:\.(?:1|prev|previous))?"
JSON_MEMBERS = {"wake-recovery-current.json": "current",
                "wake-recovery-last.json": "last",
                "steam-game-link-current.json": "game-link-current"}
FACT_FIELDS = {
    "native": {"nativeAppFlags", "runningBitSet", "recordState",
               "matchingTargetRecordCount", "originalPrimaryMatched",
               "captureIdentityBound", "atomicSnapshot", "uiAndNativeAtomic",
               "nativePrimaryPid", "nativePrimaryVerified", "originalAllowed"},
    "js": {"localDisplayStatus", "selectedDisplayStatus", "mostAvailableDisplayStatus",
           "inRunningList", "canonicalInRunningList", "localRunning", "overviewAvailable"},
    "producerClaim": {"linked", "transportConnected", "contactConnected", "associationVerified",
                      "trackerExact", "nativePrimaryExact", "gameIdentitySame", "steamIdentitySame",
                      "accountMatched", "originalInstanceMatched", "jointBindingVerified",
                      "sharedCandidateMatched", "steamSessionMatched", "healthVerified",
                      "gameInputVerified", "continueButtonVerified", "inputVerified", "continueVerified"},
    "actionReceipt": {"ok", "accepted", "runtimeAccepted", "mutated", "refreshed", "repairTriggered"},
    "observation": {"appid", "gameInput",
                    "originalWindowFocused", "foregroundRole", "controllerCount", "xinputMask",
                    "xinputConnectedMask", "connectedMask", "boundary", "boundaryGeneration",
                    "kernelWakeConfirmed", "proofFresh", "eventFileTime", "boundaryFileTime",
                    "currentPowerGeneration", "enabled", "pending", "active", "readOnly",
                    "reportState", "implementation", "phase", "stage", "status", "reason"},
}
FIELD_GROUP = {field: group for group, fields in FACT_FIELDS.items() for field in fields}
# Keep these out of FIELD_GROUP: field names alone do not establish context.
# Scoped diagnostic edges stay out of the global field/container allowlists.
RECORD_PREDICATE_CONTAINER = ("nativeRecordDiagnostic", "predicateInputs")
RECORD_PREDICATE_RAW_FIELDS = {
    "lastRecordKeepDecisionRaw": 0xff, "comparisonLatchRaw": 0xff,
    "failedCheckCounterRaw": 0xffffffff, "alternatePidRaw": 0xffffffff,
}
# Native sanitizer output only, not the JS producer's rows transport object.
OVERLAY_POPUP_CONTAINER = "overlayPopupCacheDiagnostic"
OVERLAY_POPUP_IMPLEMENTATION = "steam-overlay-popup-cache-v1"
OVERLAY_POPUP_REASONS = {"source-qualified-cache-read", "no-target", "window-store-unavailable",
                         "map-unavailable", "map-budget", "getter-source-unverified",
                         "target-entry-unknown", "cache-changed", "cache-read-unavailable",
                         "diagnostic-unavailable"}
OVERLAY_POPUP_PROOFS = ("steamContactVerified", "actualInputVerified", "actualContinueVerified",
                        "recoveryAdmission", "pidMatchIsIdentityProof")
OVERLAY_POPUP_FIELDS = ("implementation", "readOnly", "appid", "sourceMatched", "available", "reason",
                        "popupCount", "pidCandidates", "expectedPidCandidateMatch",
                        "normalRaisePrerequisite") + OVERLAY_POPUP_PROOFS
OVERLAY_PROVIDER_CONTAINER = "overlayProviderCacheDiagnostic"
OVERLAY_PROVIDER_REASONS = OVERLAY_POPUP_REASONS | {"provider-store-unavailable", "provider-source-unverified",
    "data-path-unqualified", "map-brand-unqualified", "field-unavailable", "read-window-expired",
    "summary-budget", "original-identity-bracket-unqualified"}
OVERLAY_PROVIDER_TAGS = ("m_unPID", "m_nBrowserID", "m_eBrowserType", "m_unAppID", "m_gameID", "m_eUIMode")
OVERLAY_PROVIDER_PROOFS = OVERLAY_POPUP_PROOFS + ("snapshotAtomic", "originalProviderObjectVerified")
OVERLAY_PROVIDER_CANDIDATE_FIELDS = ("sameGameIdCandidateCount", "sameGameIdCandidatesComplete", "sameGameIdCandidatesTagged")
OVERLAY_PROVIDER_FIELDS = ("implementation", "readOnly", "sourceMatched", "sourcePathQualified", "available", "reason",
    "appid", "originalPid", "fullGameIdExactString", "mapEntryCount", "targetMembership", "samePidOtherGameIdCount",
    "originalIdentityBracketMatched") + OVERLAY_PROVIDER_PROOFS
TIME_FIELDS = ("time", "timestamp", "at", "recordEpoch", "observationFileTime",
               "observedAtTick", "snapshotAtTick", "atTick", "elapsedMs")
# Traversal is deliberately limited to known evidence containers. Never copy
# command lines, arbitrary strings, credentials, modules, settings or raw text.
CONTAINERS = set("""details before after receipt observations history timeline associationHistory
suspendCapture inputBefore inputAfter nativeRecordPrefetchBefore nativeRecordDiagnostic
nativeUiAppState uiOverview nativePrimary tracker originalGameRecovery steam steamUI overviews
overview from to association continueRecovery steamRecovery steamInputRecovery originalGame game
steamClient foreground input inputFocus inputControllers inputContext controller xinput
wakeAdmission wakeReadiness postReturnObservation contactRecovery contactProof targetSelection
originalIdentity original gameIdentity steamIdentity session off secondaryRejection log""".split())
STAGES = {"before", "after", "receipt", "observations", "history", "timeline", "associationHistory"}
CAPTURE_EVENTS = {"sleep-steam-original-captured", "sleep-steam-boundary-prepared"}


def unique(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("duplicate JSON key")
        result[key] = value
    return result


def invalid_constant(_):
    raise ValueError("nonfinite JSON number")


def finite_float(raw):
    value = float(raw)
    if not math.isfinite(value):
        raise ValueError("nonfinite JSON number")
    return value


def pointer(parts):
    return "".join("/" + str(p).replace("~", "~0").replace("/", "~1") for p in parts)


def member_kind(name):
    if name in JSON_MEMBERS:
        return "json", JSON_MEMBERS[name]
    if re.fullmatch("sleep-facts.log" + ROTATION, name):
        return "jsonl", "sleep-facts"
    if re.fullmatch(r"native-lifecycle\.log" + ROTATION, name):
        return "jsonl", "native-lifecycle"
    return None


def line_index(text, data, base_line, max_depth, max_nodes):
    """Map JSON pointers to actual value lines, including pretty printed JSON."""
    starts = [-1] + [m.start() for m in re.finditer("\n", text)]
    decoder = json.JSONDecoder()
    lines = {}

    def ws(pos):
        while pos < len(text) and text[pos] in " \t\r\n":
            pos += 1
        return pos

    def walk(value, pos, path, depth):
        if depth > max_depth or len(lines) >= max_nodes:
            raise ValueError("depth limit")
        pos = ws(pos)
        lines[path] = base_line + bisect.bisect_left(starts, pos) - 1
        if isinstance(value, dict):
            pos = ws(pos + 1)
            for key, child in value.items():
                _, pos = decoder.raw_decode(text, pos)
                pos = ws(pos)
                pos = walk(child, pos + 1, path + (key,), depth + 1)
                pos = ws(pos)
                if text[pos:pos+1] == ",":
                    pos = ws(pos + 1)
            return pos + 1
        if isinstance(value, list):
            pos = ws(pos + 1)
            for i, child in enumerate(value):
                pos = ws(walk(child, pos, path + (i,), depth + 1))
                if text[pos:pos+1] == ",":
                    pos = ws(pos + 1)
            return pos + 1
        return decoder.raw_decode(text, pos)[1]

    walk(data, 0, (), 0)
    return lines


def normalized_time(field, value):
    """No guessed timezone and no treating ticks/elapsed time as wall time."""
    try:
        if field == "recordEpoch" and type(value) in (int, float) and math.isfinite(value):
            date = dt.datetime.fromtimestamp(value, dt.timezone.utc)
        elif field == "observationFileTime" and str(value).isdigit():
            date = dt.datetime(1601, 1, 1, tzinfo=dt.timezone.utc) + dt.timedelta(microseconds=int(value)//10)
        elif field in {"time", "timestamp", "at"} and isinstance(value, str):
            date = dt.datetime.fromisoformat(value.replace("Z", "+00:00"))
            if date.tzinfo is None:
                return "unknown", "timezone-unknown"
            date = date.astimezone(dt.timezone.utc)
        else:
            return "unknown", "relative-or-unknown-clock"
        return date.isoformat(), "explicit-absolute-clock"
    except (ValueError, TypeError, OverflowError, OSError):
        return "unknown", "invalid-or-unknown-clock"


class Extractor:
    def __init__(self, limits=None):
        self.limits = dict(LIMITS)
        if limits:
            if any(k not in LIMITS or type(v) is not int or not 1 <= v <= LIMITS[k]
                   for k, v in limits.items()):
                raise ValueError("limits may only lower documented bounds")
            self.limits.update(limits)
        self.result = {"schema": "ymcc-steam-contact-timeline-v1", "limits": self.limits,
                       "status": "incomplete", "members": [], "events": [], "errors": [],
                       "ordering": {"output": "source-order", "crossMemberOrder": "unknown"},
                       "assessment": {"realSteamContactRecovery": "unknown", "actualGameInput": "unknown",
                                      "actualContinueButton": "unknown", "userReport": "unknown",
                                      "instanceCorrelation": "unknown", "instancesMerged": False}}
        self.total_bytes = self.rows = self.nodes = self.error_count = 0

    def error(self, code, member=None, line=None, path=None):
        self.error_count += 1
        if len(self.result["errors"]) < 128:
            self.result["errors"].append({"code": code, "member": member, "line": line,
                                          "jsonPointer": pointer(path) if path is not None else None})

    def source(self, member, path, lines):
        return {"member": member, "line": lines.get(path, "unknown"), "jsonPointer": pointer(path)}

    def clocks(self, node, path, member, lines):
        result = []
        for field in TIME_FIELDS:
            value = node.get(field)
            if type(value) not in (str, int, float):
                continue
            if isinstance(value, str) and len(value) > self.limits["scalarChars"]:
                self.error("SCALAR_LIMIT", member, lines.get(path+(field,)), path+(field,))
                continue
            utc, basis = normalized_time(field, value)
            result.append({"field": field, "value": value, "utc": utc, "basis": basis,
                           "source": self.source(member, path+(field,), lines)})
        return result

    def value(self, node, keys, path, member, lines):
        for key in keys:
            value = node.get(key)
            if type(value) in (str, int, float, bool) and value != "":
                if isinstance(value, str) and len(value) > self.limits["scalarChars"]:
                    self.error("SCALAR_LIMIT", member, lines.get(path+(key,)), path+(key,))
                    break
                return {"value": value, "source": self.source(member, path+(key,), lines)}
        return {"value": "unknown", "source": "unknown"}

    def identity(self, node, path, member, lines, event):
        identities = {}
        for role, keys in [("game", ("originalGame", "game")), ("steam", ("steamClient",))]:
            candidates = [(node[k], path+(k,)) for k in keys if isinstance(node.get(k), dict)]
            if not candidates and role == "game" and event in CAPTURE_EVENTS:
                candidates = [(node, path)]
            # Keep conflicting representations separately; do not choose one as truth.
            identities[role] = []
            for obj, p in candidates:
                item = {"source": self.source(member, p, lines)}
                for field, aliases in {"pid": ("pid",), "birth": ("created", "birth"),
                                       "path": ("path", "imagePath", "root"),
                                       "windowsSession": ("windowsSession", "sessionId")}.items():
                    item[field] = self.value(obj, aliases, p, member, lines)
                if role == "steam":
                    for key in ("identityToken", "accountKnown", "accountPinned", "accountSource", "liveAccountPinned"):
                        item[key] = self.value(obj, (key,), p, member, lines)
                    # Raw account identifiers are not needed in a shareable index.
                    for key in ("account", "accountId", "accountID", "accountId32"):
                        raw = obj.get(key)
                        if type(raw) in (str, int):
                            item["accountFingerprint"] = {"value": hashlib.sha256(str(raw).encode()).hexdigest(),
                                                          "source": self.source(member, p+(key,), lines)}
                identities[role].append(item)
            if not identities[role]:
                identities[role] = "unknown"
        return identities

    def overlay_popup_cache(self, obj, path, member, lines, item, clocks):
        # This leaf has no identity extraction, event emission or recursive edges.
        if obj.get("implementation") != OVERLAY_POPUP_IMPLEMENTATION or "rows" in obj:
            return
        pids = obj.get("pidCandidates")
        charge = sum(k in obj for k in OVERLAY_POPUP_FIELDS)
        if isinstance(pids, list) and len(pids) <= 64:
            charge += len(pids)
        self.nodes += charge
        if self.nodes > self.limits["nodes"]:
            self.error("NODE_OR_DEPTH_LIMIT", member, lines.get(path), path)
            return
        oversized = set()
        for key in OVERLAY_POPUP_FIELDS:
            value = obj.get(key)
            if isinstance(value, str) and len(value) > self.limits["scalarChars"]:
                oversized.add(key)
                self.error("SCALAR_LIMIT", member, lines.get(path+(key,)), path+(key,))

        def uint(value, maximum=0xffffffff):
            return type(value) is int and 0 <= value <= maximum

        appid = obj.get("appid")
        count = obj.get("popupCount")
        available = obj.get("available")
        source = obj.get("sourceMatched")
        why = obj.get("reason")
        match = obj.get("expectedPidCandidateMatch")
        prerequisite = obj.get("normalRaisePrerequisite")
        known_reason = type(why) is str and why in OVERLAY_POPUP_REASONS
        envelope = (not oversized and all(k in obj for k in OVERLAY_POPUP_FIELDS) and
                    obj.get("readOnly") is True and uint(appid) and appid > 0 and
                    type(available) is bool and type(source) is bool and known_reason and
                    all(obj.get(k) is False for k in OVERLAY_POPUP_PROOFS))
        consistent = False
        if envelope and available:
            consistent = (source is True and why == "source-qualified-cache-read" and uint(count, 64) and
                          isinstance(pids, list) and len(pids) <= 64 and len(pids) == count and
                          all(uint(pid) for pid in pids) and len(set(pids)) == len(pids) and
                          (match is None or type(match) is bool) and
                          (match is not True or any(pid != 0 for pid in pids)) and
                          prerequisite == ("empty" if count == 0 else "nonempty"))
        elif envelope:
            consistent = (why != "source-qualified-cache-read" and count is None and pids is None and
                          match is None and prerequisite == "unknown")
        valid = {"implementation": True, "readOnly": obj.get("readOnly") is True,
                 "appid": uint(appid) and appid > 0, "sourceMatched": type(source) is bool,
                 "available": consistent, "reason": known_reason,
                 "popupCount": consistent and available is True,
                 "pidCandidates": consistent and available is True,
                 "expectedPidCandidateMatch": consistent and available is True and type(match) is bool,
                 "normalRaisePrerequisite": consistent}
        valid.update({key: obj.get(key) is False for key in OVERLAY_POPUP_PROOFS})
        for key in OVERLAY_POPUP_FIELDS:
            if key not in obj:
                continue  # Missing is not fabricated as null or zero.
            raw = obj[key]
            present = valid[key] and key not in oversized
            availability = "present" if present else ("null" if raw is None else "unsupported-type-or-range")
            item["facts"].append({"namespace": "overlayPopupCacheDiagnostic", "field": key,
                                  "value": raw if present else "unknown", "availability": availability,
                                  "source": self.source(member, path+(key,), lines),
                                  "timeContext": clocks or "unknown"})

    def overlay_provider_cache(self, obj, path, member, lines, item, clocks):
        # A closed native summary only. No raw rows, identity merge or verdict.
        if obj.get("implementation") != "steam-overlay-provider-cache-v1" or "rows" in obj:
            return
        candidate_rows = obj.get("sameGameIdCandidatesTagged")
        self.nodes += sum(k in obj for k in OVERLAY_PROVIDER_FIELDS) + 13
        self.nodes += sum(k in obj for k in OVERLAY_PROVIDER_CANDIDATE_FIELDS)
        if isinstance(candidate_rows, list):
            self.nodes += min(len(candidate_rows), 2) * 18
        if self.nodes > self.limits["nodes"]:
            self.error("NODE_OR_DEPTH_LIMIT", member, lines.get(path), path)
            return

        def uint(v, maximum=0xffffffff):
            return type(v) is int and 0 <= v <= maximum

        def decimal(v, maximum=0xffffffffffffffff):
            return isinstance(v, str) and re.fullmatch(r"0|[1-9][0-9]{0,19}", v) is not None and int(v) <= maximum

        def short(v):
            try:
                return isinstance(v, str) and len(v.encode("utf-16-le"))//2 <= min(64, self.limits["scalarChars"])
            except UnicodeError:
                return False

        def tag(t):
            if not isinstance(t, dict) or set(t) != {"rawType", "value"}:
                return False
            kind, v = t["rawType"], t["value"]
            if kind in ("undefined", "null", "object", "function", "symbol"):
                return v is None
            if kind == "boolean":
                return type(v) is bool
            if kind == "number":
                return v is None or type(v) is int and abs(v) <= 9007199254740991
            if kind == "bigint":
                return isinstance(v, str) and re.fullmatch(r"-?(0|[1-9][0-9]{0,19})", v) is not None and v != "-0" and abs(int(v)) <= 0xffffffffffffffff
            return kind == "string" and short(v)

        def identity(t, maximum=0xffffffffffffffff):
            if t["rawType"] == "number":
                return t["value"] if uint(t["value"], min(maximum, 9007199254740991)) else None
            return int(t["value"]) if t["rawType"] in ("string", "bigint") and decimal(t["value"], maximum) else None

        app, pid, game = obj.get("appid"), obj.get("originalPid"), obj.get("fullGameIdExactString")
        full = int(game) if decimal(game) else None
        target = uint(app) and app > 0 and uint(pid) and pid > 0 and full is not None and (
            full == app and full <= 0xffffff or full >> 32 == app and full & 0xffffffff == 0x02000000)
        fields = obj.get("targetFieldsTagged")
        fields_valid = isinstance(fields, dict) and set(fields) == set(OVERLAY_PROVIDER_TAGS) and all(tag(fields[k]) for k in OVERLAY_PROVIDER_TAGS)
        def same_game_row(row):
            if not target or not isinstance(row, dict) or set(row) != set(OVERLAY_PROVIDER_TAGS) or not all(tag(row[k]) for k in OVERLAY_PROVIDER_TAGS):
                return False
            row_app = identity(row["m_unAppID"], 0xffffffff)
            raw_app = row["m_unAppID"]
            if raw_app["rawType"] == "number" and type(raw_app["value"]) is int and -0x80000000 <= raw_app["value"] <= 0xffffffff:
                row_app = raw_app["value"] & 0xffffffff
            return identity(row["m_unPID"], 0xffffffff) is not None and identity(row["m_gameID"]) == full and row_app == app

        fields_match = fields_valid and same_game_row(fields) and identity(fields["m_unPID"], 0xffffffff) == pid
        why, available = obj.get("reason"), obj.get("available")
        envelope = target and obj.get("readOnly") is True and all(obj.get(k) is False for k in OVERLAY_PROVIDER_PROOFS) and (
            type(obj.get("sourceMatched")) is bool and type(obj.get("sourcePathQualified")) is bool and type(available) is bool and
            isinstance(why, str) and why in OVERLAY_PROVIDER_REASONS)
        count, other, membership = obj.get("mapEntryCount"), obj.get("samePidOtherGameIdCount"), obj.get("targetMembership")
        consistent = False
        if envelope and available is True:
            consistent = obj.get("sourceMatched") is True and obj.get("sourcePathQualified") is True and obj.get("originalIdentityBracketMatched") is True and uint(count, 128) and other == 0 and type(other) is int and (
                membership == "present-in-two-cache-reads" and why == "source-qualified-cache-read" and count > 0 and fields_match or
                membership == "absent-in-two-cache-reads" and why == "no-target" and fields is None)
        elif envelope:
            consistent = membership == "unknown" and fields is None and why != "source-qualified-cache-read" and (count is None or uint(count, 128)) and (other is None or uint(other, 128))
        simple = {"implementation": True, "readOnly": obj.get("readOnly") is True, "appid": uint(app) and app > 0,
            "originalPid": uint(pid) and pid > 0, "fullGameIdExactString": decimal(game),
            "sourceMatched": type(obj.get("sourceMatched")) is bool, "sourcePathQualified": type(obj.get("sourcePathQualified")) is bool,
            "originalIdentityBracketMatched": type(obj.get("originalIdentityBracketMatched")) is bool,
            "available": consistent, "reason": isinstance(why, str) and why in OVERLAY_PROVIDER_REASONS,
            "targetMembership": consistent, "mapEntryCount": consistent and uint(count, 128),
            "samePidOtherGameIdCount": consistent and uint(other, 128)}
        simple.update({k: obj.get(k) is False for k in OVERLAY_PROVIDER_PROOFS})

        def fact(key, raw, valid, subpath):
            if isinstance(raw, str) and len(raw) > self.limits["scalarChars"]:
                valid = False
                self.error("SCALAR_LIMIT", member, lines.get(subpath), subpath)
            item["facts"].append({"namespace": OVERLAY_PROVIDER_CONTAINER, "field": key,
                "value": raw if valid else "unknown", "availability": "present" if valid else "null" if raw is None else "unsupported-type-or-range",
                "source": self.source(member, subpath, lines), "timeContext": clocks or "unknown"})
        for key in OVERLAY_PROVIDER_FIELDS:
            if key in obj:
                fact(key, obj[key], simple[key], path+(key,))
        if "targetFieldsTagged" in obj:
            if consistent and available and fields_match:
                for name in OVERLAY_PROVIDER_TAGS:
                    for key in ("rawType", "value"):
                        v = fields[name][key]
                        fact("targetFieldsTagged."+name+"."+key, v, v is not None, path+("targetFieldsTagged", name, key))
            else:
                fact("targetFieldsTagged", fields, False, path+("targetFieldsTagged",))
        if any(k in obj for k in OVERLAY_PROVIDER_CANDIDATE_FIELDS):
            n, complete = obj.get("sameGameIdCandidateCount"), obj.get("sameGameIdCandidatesComplete")
            cohort_valid = False
            if consistent and available and uint(n, 128) and n <= count and type(complete) is bool:
                if n <= 2 and complete is True and isinstance(candidate_rows, list) and len(candidate_rows) == n and all(same_game_row(row) for row in candidate_rows):
                    pids = [identity(row["m_unPID"], 0xffffffff) for row in candidate_rows]
                    cohort_valid = len(set(pids)) == len(pids) and (
                        membership == "absent-in-two-cache-reads" and pid not in pids or
                        membership == "present-in-two-cache-reads" and any(row == fields for row in candidate_rows))
                elif n > 2 and complete is False and candidate_rows is None:
                    cohort_valid = True
            elif consistent and available is False:
                cohort_valid = n is None and complete is False and candidate_rows is None
            fact("sameGameIdCandidateCount", n, cohort_valid and uint(n, 128), path+("sameGameIdCandidateCount",))
            fact("sameGameIdCandidatesComplete", complete, cohort_valid and type(complete) is bool, path+("sameGameIdCandidatesComplete",))
            if cohort_valid and available and complete and candidate_rows:
                for index, row in enumerate(candidate_rows):
                    for name in OVERLAY_PROVIDER_TAGS:
                        for key in ("rawType", "value"):
                            value = row[name][key]
                            fact(f"sameGameIdCandidatesTagged.{index}.{name}.{key}", value, value is not None, path+("sameGameIdCandidatesTagged", index, name, key))
            else:
                # Empty is qualified only as zero same-GameID candidates. No
                # PID domain, singleton identity or native result is inferred.
                fact("sameGameIdCandidatesTagged", candidate_rows, cohort_valid and available and complete and candidate_rows == [], path+("sameGameIdCandidatesTagged",))

    def emit(self, node, path, member, lines, scope, event, depth=0):
        if len(self.result["events"]) >= self.limits["events"]:
            self.error("EVENT_LIMIT", member, lines.get(path), path)
            return
        item = {"id": len(self.result["events"]), "event": event,
                "source": self.source(member, path, lines), "scope": scope,
                "generation": self.value(node, ("generation",), path, member, lines),
                "configEpoch": self.value(node, ("configEpoch",), path, member, lines),
                "identity": self.identity(node, path, member, lines, event),
                "time": [], "facts": []}
        item["time"] = self.clocks(node, path, member, lines) or "unknown"
        self.result["events"].append(item)

        def collect(obj, p, level, clocks=None):
            self.nodes += 1
            if self.nodes > self.limits["nodes"] or level > self.limits["depth"]:
                self.error("NODE_OR_DEPTH_LIMIT", member, lines.get(p), p)
                return
            if isinstance(obj, list):
                for i, v in enumerate(obj):
                    if self.nodes > self.limits["nodes"]:
                        break
                    if p[-1:] and p[-1] in STAGES and isinstance(v, dict):
                        self.emit(v, p+(i,), member, lines, scope, str(p[-1]), level+1)
                    else:
                        collect(v, p+(i,), level+1, clocks)
            elif isinstance(obj, dict):
                if p[-1:] == (OVERLAY_POPUP_CONTAINER,):
                    self.overlay_popup_cache(obj, p, member, lines, item, clocks)
                    return
                if p[-1:] == (OVERLAY_PROVIDER_CONTAINER,):
                    self.overlay_provider_cache(obj, p, member, lines, item, clocks)
                    return
                own_clocks = self.clocks(obj, p, member, lines)
                clocks = own_clocks or clocks
                if p[-2:] == RECORD_PREDICATE_CONTAINER:
                    # Saved bytes/uint32 inputs only; no current branch/return,
                    # Running, input, Continue or recovery verdict follows.
                    # Missing stays absent; explicit null keeps its own source.
                    for k, maximum in RECORD_PREDICATE_RAW_FIELDS.items():
                        if k not in obj:
                            continue
                        raw = obj[k]
                        valid = type(raw) is int and 0 <= raw <= maximum
                        availability = "present" if valid else ("null" if raw is None else "unsupported-type-or-range")
                        item["facts"].append({"namespace": "recordPredicateRaw", "field": k,
                                              "value": raw if valid else "unknown", "availability": availability,
                                              "source": self.source(member, p+(k,), lines),
                                              "timeContext": clocks or "unknown"})
                    # Do not descend into arbitrary fields, even old known ones.
                    return
                for k, v in obj.items():
                    if self.nodes > self.limits["nodes"]:
                        break
                    q = p+(k,)
                    if k in {"truncated", "runningTruncated", "moduleTruncated"} and v is True:
                        self.error("PRODUCER_TRUNCATED", member, lines.get(q), q)
                    if k in FIELD_GROUP and type(v) in (str, int, float, bool):
                        if isinstance(v, str) and len(v) > self.limits["scalarChars"]:
                            self.error("SCALAR_LIMIT", member, lines.get(q), q)
                            continue
                        item["facts"].append({"namespace": FIELD_GROUP[k], "field": k, "value": v,
                                              "source": self.source(member, q, lines), "timeContext": clocks or "unknown"})
                    predicate_container = (k == RECORD_PREDICATE_CONTAINER[1] and
                                           p[-1:] == RECORD_PREDICATE_CONTAINER[:1] and isinstance(v, dict))
                    popup_container = k in (OVERLAY_POPUP_CONTAINER, OVERLAY_PROVIDER_CONTAINER) and isinstance(v, dict)
                    if (k in CONTAINERS or predicate_container or popup_container) and isinstance(v, (dict, list)):
                        if k in STAGES and isinstance(v, dict):
                            self.emit(v, q, member, lines, scope, k, level+1)
                        else:
                            collect(v, q, level+1, clocks)
        collect(node, path, depth)
        item["signalFactIndexes"] = {key: [i for i, fact in enumerate(item["facts"]) if fact["field"] == key] or "unknown"
                                      for key in ("nativeAppFlags", "recordState", "localDisplayStatus", "inRunningList")}
        return item

    def parse_json(self, raw, member, role, line=1, jsonl=False):
        try:
            text = raw.decode("utf-8-sig")
            data = json.loads(text, object_pairs_hook=unique, parse_constant=invalid_constant, parse_float=finite_float)
            lines = line_index(text, data, line, self.limits["depth"], self.limits["nodes"])
        except (UnicodeError, ValueError, RecursionError, IndexError):
            self.error("INVALID_JSON_OR_DEPTH", member, line)
            return
        if not isinstance(data, dict):
            self.error("UNSUPPORTED_JSON_SHAPE", member, line)
            return
        if jsonl:
            event = data.get("event")
            if role == "native-lifecycle":
                # publish() logs the state at root, never a details envelope.
                if event == "steam-game-link-state":
                    self.emit(data, (), member, lines, {"role": role, "reportBoundary": "unknown"}, event)
                return
            if not isinstance(event, str) or len(event) > 160:
                self.error("UNSUPPORTED_LOG_RECORD", member, line)
                return
            if not (event.startswith(("sleep-", "steam-", "s0-", "s4-", "kernel-power")) or
                    event in {"power-broadcast", "user-standby", "user-standby-cleared", "user-initiated-sleep"}):
                return
            payload = data.get("details")
            p = ("details",) if isinstance(payload, dict) else ()
            payload = payload if p else data
            item = self.emit(payload, p, member, lines, {"role": role, "reportBoundary": "unknown"}, event)
            if item:
                envelope_times = self.clocks(data, (), member, lines)
                for t in envelope_times:
                    t["scope"] = "log-envelope"
                own = item["time"] if isinstance(item["time"], list) else []
                item["time"] = own + [t for t in envelope_times if t["source"] not in [v["source"] for v in own]] or "unknown"
            return
        if role == "single-json":
            if isinstance(data.get("event"), str):
                return self.parse_json(raw, member, role, line, jsonl=True)
            if "before" in data or "after" in data:
                role = "single-snapshot"
            elif "originalGame" in data and "steamUI" in data:
                role = "single-game-link"
            else:
                self.error("UNSUPPORTED_JSON_SHAPE", member, line)
                return
        if not data:
            self.error("EMPTY_SNAPSHOT", member, line)
            return
        boundary = {field: self.value(data, (field,), (), member, lines)
                    for field in ("generation", "configEpoch", "recordEpoch", "linkSerial")}
        self.emit(data, (), member, lines, {"role": role, "reportBoundary": boundary}, "snapshot")

    def parse(self, raw, member, kind, role):
        if kind == "json":
            self.parse_json(raw, member, role)
            return
        for line, record in enumerate(io.BytesIO(raw), 1):
            self.rows += 1
            if self.rows > self.limits["rows"]:
                self.error("ROW_LIMIT", member, line)
                break
            if len(record) > self.limits["lineBytes"]:
                self.error("LINE_LIMIT", member, line)
                continue
            if not record.strip():
                continue
            self.parse_json(record, member, role, line, jsonl=True)

    def read_member(self, stream, size, member):
        if size > self.limits["memberBytes"]:
            self.error("MEMBER_BYTE_LIMIT", member)
            return None
        if size > self.limits["totalBytes"] - self.total_bytes:
            self.error("TOTAL_BYTE_LIMIT", member)
            return None
        chunks = []
        count = 0
        while True:
            # Also bound actual decompressed reads, not just central metadata.
            cap = min(self.limits["memberBytes"]-count, self.limits["totalBytes"]-self.total_bytes)
            block = stream.read(min(65536, cap+1))
            self.total_bytes += len(block)
            count += len(block)
            if count > self.limits["memberBytes"] or self.total_bytes > self.limits["totalBytes"]:
                self.error("ACTUAL_BYTE_LIMIT", member)
                return None
            if not block:
                break
            chunks.append(block)
        if count != size:
            self.error("MEMBER_SIZE_MISMATCH", member)
            return None
        return b"".join(chunks)

    def zip_inventory(self, stream):
        stream.seek(0, 2)
        size = stream.tell()
        if size > self.limits["archiveBytes"]:
            raise ValueError("ARCHIVE_BYTE_LIMIT")
        stream.seek(max(0, size-65557))
        tail = stream.read(65557)
        at = tail.rfind(b"PK\x05\x06")
        if at < 0 or len(tail)-at < 22:
            raise ValueError("INVALID_ZIP_DIRECTORY")
        _, disk, startdisk, countdisk, count, central, offset, comment = struct.unpack_from("<4s4H2LH", tail, at)
        if disk or startdisk or countdisk != count or count == 65535 or central == 0xffffffff or offset == 0xffffffff:
            raise ValueError("ZIP64_OR_MULTIDISK_UNSUPPORTED")
        if count > self.limits["members"] or central > self.limits["centralBytes"]:
            raise ValueError("ZIP_DIRECTORY_LIMIT")
        if at+22+comment != len(tail) or offset+central != size-len(tail)+at:
            raise ValueError("INVALID_ZIP_DIRECTORY")
        stream.seek(0)
        return count

    def run(self, input_path):
        path = Path(input_path)
        self.result["input"] = {"path": str(path.absolute()), "sha256": "unknown", "stable": "unknown"}
        try:
            if str(path).startswith(("\\\\", "//")):
                raise ValueError("LOCAL_INPUT_REQUIRED")
            with path.open("rb") as stream:
                before = os.fstat(stream.fileno())
                if not stat.S_ISREG(before.st_mode):
                    raise ValueError("INPUT_NOT_REGULAR_FILE")
                if path.suffix.lower() == ".zip":
                    count = self.zip_inventory(stream)
                    with zipfile.ZipFile(stream) as archive:
                        infos = archive.infolist()
                        if len(infos) != count:
                            raise ValueError("ZIP_ENTRY_COUNT_MISMATCH")
                        selected = set()
                        for info in infos:
                            name = info.filename
                            kind = member_kind(name)
                            unsafe = ("\\" in name or name.startswith("/") or
                                      any(p in {"..", "."} or ":" in p for p in name.split("/")))
                            if unsafe:
                                self.error("UNSAFE_MEMBER_NAME", name)
                                continue
                            if not kind:
                                continue
                            entry = {"member": name, "role": kind[1], "declaredBytes": info.file_size,
                                     "status": "incomplete"}
                            self.result["members"].append(entry)
                            mode = stat.S_IFMT(info.external_attr >> 16)
                            if name in selected:
                                self.error("DUPLICATE_MEMBER", name)
                                continue
                            selected.add(name)
                            if info.flag_bits & 1 or mode not in (0, stat.S_IFREG):
                                self.error("ENCRYPTED_OR_NONREGULAR_MEMBER", name)
                                continue
                            start_errors = self.error_count
                            try:
                                with archive.open(info) as content:
                                    raw = self.read_member(content, info.file_size, name)
                                if raw is not None:
                                    entry["sha256"] = hashlib.sha256(raw).hexdigest()
                                    self.parse(raw, name, *kind)
                            except (OSError, ValueError, EOFError, zipfile.BadZipFile, RuntimeError, NotImplementedError):
                                self.error("CORRUPT_OR_UNSUPPORTED_MEMBER", name)
                            if self.error_count == start_errors:
                                entry["status"] = "read-complete"
                elif path.suffix.lower() in {".json", ".jsonl", ".log"}:
                    name = path.name
                    kind = member_kind(name) or ("json" if path.suffix.lower() == ".json" else "jsonl", "single-json" if path.suffix.lower() == ".json" else "single-jsonl")
                    entry = {"member": name, "role": kind[1], "declaredBytes": before.st_size, "status": "incomplete"}
                    self.result["members"].append(entry)
                    raw = self.read_member(stream, before.st_size, name)
                    if raw is not None:
                        entry["sha256"] = hashlib.sha256(raw).hexdigest()
                        self.parse(raw, name, *kind)
                        if not self.error_count:
                            entry["status"] = "read-complete"
                else:
                    raise ValueError("INPUT_EXTENSION_UNSUPPORTED")
                if before.st_size <= self.limits["archiveBytes"]:
                    stream.seek(0)
                    digest = hashlib.sha256()
                    remaining = before.st_size
                    while remaining:
                        block = stream.read(min(65536, remaining))
                        if not block:
                            self.error("INPUT_TRUNCATED_DURING_HASH")
                            break
                        remaining -= len(block)
                        digest.update(block)
                    self.result["input"]["sha256"] = digest.hexdigest()
                after = os.fstat(stream.fileno())
                self.result["input"]["stable"] = True
                if (before.st_size, before.st_mtime_ns, before.st_ino) != (after.st_size, after.st_mtime_ns, after.st_ino):
                    self.result["input"]["sha256"] = "unknown"
                    self.result["input"]["stable"] = False
                    self.error("INPUT_CHANGED")
        except ValueError as exc:
            self.error(str(exc) if str(exc).isupper() else "INVALID_INPUT")
        except (OSError, EOFError, zipfile.BadZipFile, RuntimeError, NotImplementedError):
            self.error("INPUT_UNREADABLE_OR_INVALID_ZIP")
        if not self.result["members"]:
            self.error("NO_RECOGNIZED_MEMBERS")
        if not self.result["events"]:
            self.error("NO_SUPPORTED_EVENTS")
        self.finish()
        return self.result

    def finish(self):
        events = self.result["events"]
        # Only sort direct event timestamps. A snapshot's recordEpoch is its
        # export/read time, never the time of its nested before/after evidence.
        ordered = []
        for event in events:
            if isinstance(event["time"], list):
                absolute = [t for t in event["time"] if t["utc"] != "unknown"]
                # Multiple differing clocks are retained but not resolved.
                if len({t["utc"] for t in absolute}) == 1:
                    ordered.append((absolute[0]["utc"], event["id"]))
        self.result["ordering"]["explicitAbsoluteTimeEventIds"] = [i for _, i in sorted(ordered)]
        placed = {i for _, i in ordered}
        self.result["ordering"]["unplacedEventIds"] = [e["id"] for e in events if e["id"] not in placed]
        roles = {m["role"] for m in self.result["members"]}
        missing = ["actualGameInput:user-report-or-unknown", "actualContinueButton:user-report-or-unknown",
                   "realSteamContactRecovery:no-verdict-from-record-pid-xinput-action-focus"]
        for role in ("current", "last", "sleep-facts", "game-link-current"):
            if role not in roles:
                missing.append("missing-member:"+role)
        for role in ("game", "steam"):
            for field in ("pid", "birth", "path", "windowsSession"):
                if not any(isinstance(e["identity"][role], list) and any(i[field]["value"] != "unknown" for i in e["identity"][role]) for e in events):
                    missing.append("unobserved-identity:"+role+"."+field)
        self.result["missingEvidence"] = missing
        self.result["summary"] = {"eventCount": len(events), "contentBytesRead": self.total_bytes,
                                  "rowsRead": self.rows, "errorCount": self.error_count,
                                  "errorDetailsOmitted": max(0, self.error_count-128),
                                  "coverageMeaning": "recognized-member-parsing-only;never-recovery-completeness"}
        self.result["status"] = "incomplete" if self.error_count else "parsed"


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, epilog="Limits: "+json.dumps(LIMITS))
    parser.add_argument("input", help="explicit ordinary YMCC ZIP or one JSON/JSONL log")
    parser.add_argument("--output", required=True, help="explicit JSON result path (parent must exist)")
    args = parser.parse_args(argv)
    source, output = Path(args.input), Path(args.output)
    if any(str(p).startswith(("\\\\", "//")) for p in (source, output)):
        parser.error("local input and output paths required")
    if source.resolve() == output.resolve() or (output.exists() and source.exists() and os.path.samefile(source, output)):
        parser.error("input and output must be different files")
    result = Extractor().run(source)
    try:
        output.write_text(json.dumps(result, ensure_ascii=True, indent=2, allow_nan=False)+"\n", encoding="utf-8")
    except OSError:
        print("OUTPUT_WRITE_FAILED", file=sys.stderr)
        return 2
    print(json.dumps({"status": result["status"], **result["summary"]}))
    return 0 if result["status"] == "parsed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
