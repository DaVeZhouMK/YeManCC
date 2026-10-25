"""Reverse-check existing controller log signals without executing product/device code.

Evidence levels stay separate: published content, locally bound Host ACK,
source read window, physical endpoint identity, encoded bytes and external use.
Five-second input-activity drains are not joined to per-frame ACKs by timestamp.
"""
from __future__ import annotations
import argparse
import collections
import hashlib
import json
import math
import struct
import pathlib
import re
import zipfile

FILES = re.compile(r"^(?:native-(?:lifecycle|detail)|virtual-gamepad|input-host-signal)\.log(?:\.(?:\d+|prev))?$", re.I)
EVENTS = {
    "input-native-signal-window", "input-host-signal",
    "input-host-frame-evidence", "wake-input-chain-first-frame", "wake-input-chain-sustained",
    "input-host-frame-reject", "input-idle-window", "input-signal-activity",
    "msi-backkey-frame-link", "msi-backkey-edge", "oem-producer-edge",
    "gpd-back-chord-match", "virtual-pad-backbuttons", "msi-wmi-key", "oem-shortcut-click",
    "rog-oem-key", "machine-identity", "gamepad-setLed", "gamepad-vibrate",
    "gamepad-vibrate-request", "gpd-back-buttons", "oem-key-catalog-query",
}
MAX_FILE_BYTES = 64 * 1024 * 1024
MAX_ROW_BYTES = 2 * 1024 * 1024
SOURCE_AGE_MS = 1500

def integer(value, minimum=0):
    return type(value) is int and value >= minimum

def object_value(value):
    return value if isinstance(value, dict) else {}

def token(value):
    return isinstance(value, str) and bool(value.strip()) and len(value) <= 256

def location(record):
    return {"file": record["file"], "line": record["line"], "time": record["data"].get("time")}

def frame_check(row):
    proof = object_value(row.get("frameProof"))
    ack = object_value(proof.get("hostReceipt"))
    source = object_value(row.get("source"))
    issues = []
    if type(row.get("formatVersion")) is not int or row.get("formatVersion") != 2:
        issues.append("unsupported-frame-schema")
    if row.get("hostSubmission") != "frame-accepted" or ack.get("hostAckAccepted") is not True:
        issues.append("no-accepted-local-ack")
    if ack.get("tupleCapturedBeforeRequest") is not True:
        issues.append("request-binding-not-captured")
    for name in ["epoch", "commandSequence", "sampleSequence"]:
        if not integer(ack.get(name), 1):
            issues.append("invalid-ack-" + name)
    for name in ["runId", "hostInstanceId", "persona"]:
        if not token(ack.get(name)):
            issues.append("missing-ack-" + name)
    if not integer(proof.get("sampleSequence"), 1) or proof.get("sampleSequence") != ack.get("sampleSequence"):
        issues.append("sample-sequence-not-bound")
    if not integer(row.get("pid"), 1):
        issues.append("native-process-unbound")
    if "writerPid" in row and (not integer(row["writerPid"], 1) or row["writerPid"] != row.get("pid")):
        issues.append("observer-process-identity-conflict")
    contradictions = []
    if integer(proof.get("sampleSequence"), 1) and integer(ack.get("sampleSequence"), 1) and proof["sampleSequence"] != ack["sampleSequence"]:
        contradictions.append("conflicting-sample-sequences")
    if ack.get("hostAckAccepted") is True and row.get("hostSubmission") != "frame-accepted":
        contradictions.append("conflicting-ack-status")
    source_issues = []
    if not token(source.get("inputSource")) or source.get("inputSource") == "unknown":
        source_issues.append("read-source-kind-unbound")
    if source.get("snapshotKnown") is not True or source.get("live") is not True:
        source_issues.append("source-not-observed-live")
    if source.get("readContextStable") is not True:
        source_issues.append("read-context-not-stable")
    if not integer(source.get("readAttemptSequence"), 1):
        source_issues.append("read-attempt-unbound")
    if not integer(row.get("powerGeneration"), 1) or not integer(source.get("powerGeneration"), 1) or source.get("powerGeneration") != row.get("powerGeneration"):
        source_issues.append("read-power-generation-unbound")
    start, end, now = source.get("readStartedTick"), source.get("readCompletedTick"), row.get("observedTick")
    if not all(integer(v, 1) for v in [start, end, now]) or not (start <= end <= now and now - start <= SOURCE_AGE_MS):
        source_issues.append("read-window-not-current")
    return {
        "localAckBound": not issues,
        "localAckIssues": issues, "contradictions": contradictions,
        "currentReadCallWindow": not source_issues,
        "sourceReadIssues": source_issues,
        "semanticNeutral": row.get("semanticNeutral") if type(row.get("semanticNeutral")) is bool else None,
        "source": source.get("inputSource"),
        "tuple": {name: ack.get(name) for name in ["runId", "hostInstanceId", "epoch", "commandSequence", "sampleSequence", "persona"]},
        "frameSampleSequence": proof.get("sampleSequence"),
        # Identity here is explicitly a cache in the production event.
        "physicalEndpointSampleBindingProven": False,
        "hardwareSampleAgeProven": False,
        "encodedBytesVerified": False,
        "steamConsumptionProven": False,
    }

def chain_ack_check(row):
    ack = object_value(row.get("ackIdentity"))
    if row.get("inputChainEvidenceSchema") != 3 or ack.get("schema") != 1:
        return False
    return (ack.get("authority") == "native-local-ack-diagnostic" and
            ack.get("nativeAccepted") is True and ack.get("tupleVerified") is True and
            ack.get("requestBindingCapturedBeforeRequest") is True and
            ack.get("status") == "verified-local-ack" and
            all(integer(ack.get(k), 1) for k in ["protocolPowerGeneration", "hostEpoch", "wireSequence"]) and
            all(token(ack.get(k)) for k in ["runId", "hostInstanceId", "requestId"]))

def channel(value):
    value = object_value(value)
    fields = ["samples", "rawActiveFrames", "meaningfulFrames", "smallAnalogOnlyFrames"]
    if not all(integer(value.get(k)) for k in fields):
        return None
    if any(value[k] > value["samples"] for k in fields[1:]):
        return None
    return {k: value[k] for k in fields}

def content_snapshot(value):
    value = object_value(value)
    sticks, triggers = value.get("sticks"), value.get("triggers")
    if not (isinstance(sticks, list) and len(sticks) == 4 and all(type(v) is int and -32768 <= v <= 32767 for v in sticks)):
        return None
    if not (isinstance(triggers, list) and len(triggers) == 2 and all(type(v) is int and 0 <= v <= 255 for v in triggers)):
        return None
    if not all(integer(value.get(k)) for k in ["buttons", "back", "screen"]):
        return None
    return {k: value[k] for k in ["buttons", "back", "screen", "sticks", "triggers"]}

def window_check(row):
    physical = channel(row.get("physicalBeforePublication"))
    attempted = channel(row.get("actualSubmitAttempts"))
    accepted = channel(row.get("hostAcceptedContent"))
    tags = []
    if physical and physical["smallAnalogOnlyFrames"]:
        tags.append("subthreshold-analog-observed")
    if accepted and accepted["meaningfulFrames"]:
        tags.append("meaningful-content-locally-acknowledged")
    if row.get("virtualTargetEnabled") is True and physical and attempted:
        if physical["meaningfulFrames"] and attempted["samples"] == 0:
            tags.append("physical-active-no-submit-observed-in-window")
    if attempted and accepted and attempted["samples"] and accepted["samples"] == 0:
        tags.append("submit-attempts-without-accepted-content-in-window")
    last_physical = content_snapshot(object_value(row.get("physicalBeforePublication")).get("lastContent"))
    last_attempted = content_snapshot(object_value(row.get("actualSubmitAttempts")).get("lastContent"))
    last_accepted = content_snapshot(object_value(row.get("hostAcceptedContent")).get("lastContent"))
    if accepted and accepted["smallAnalogOnlyFrames"]:
        tags.append("small-analog-reached-local-ack")
    if accepted and accepted["meaningfulFrames"] == 0 and accepted["smallAnalogOnlyFrames"] and last_accepted:
        if last_accepted["sticks"] == [0, -1, 0, -1] and last_accepted["triggers"] == [0, 0] and not any(last_accepted[k] for k in ["buttons", "back", "screen"]):
            tags.append("last-accepted-both-y-minus-one-no-meaningful-window")
    if row.get("displayRequired") is True:
        tags.append("display-required-observed-owner-unattributed")
    rear = object_value(row.get("msiRearWindow"))
    press = rear.get("pressEdgesM1M2")
    if isinstance(press, list) and len(press) == 2 and all(integer(x) for x in press) and sum(press):
        tags.append("msi-rear-presses-observed")
    return {
        "tags": tags, "physical": physical, "attempted": attempted, "accepted": accepted,
        "mixedContext": row.get("mixedSourceOrTargetWindow") is True or rear.get("mixedSourceGeneration") is True,
        "heldRearDecisions": object_value(rear.get("heldRearDecisions")),
        "lastContent": {"physical": last_physical, "attempted": last_attempted, "accepted": last_accepted},
        "environment": {key: row.get(key) for key in ["sessionInputIdleValid", "sessionInputIdleMs", "sessionLastInputTickChanged", "sessionInputIdleAgeDecreased", "displayRequired", "executionStateKnown", "executionStateFlags", "selectedOffTimeoutKnown", "selectedOffTimeoutSec"]},
        "scope": "independent window aggregates; gaps are review candidates, never per-frame loss proof",
        "sameFrameCausalityProven": False,
        "absenceOrLeakCauseProven": False,
    }

# These fields describe two logged boundaries, never an inferred device decoder.
SIGNAL_CONTRACT = {
    "key": ["runId", "hostInstanceId", "epoch", "commandSequence", "sampleSequence",
            "protocolPowerGeneration", "wireMonotonicTick", "wireSampleSequence", "persona", "profileIdentity"],
    "native": "request tuple; protocolPowerGeneration is the actual request generation; wire* are serialized values",
    "host": "request (older writer: binding); powerGeneration is protocol power; request.sampleSequence when present must equal canonical.sampleSequence and wireSampleSequence",
    "consistency": "Host canonical sample/tick must equal explicit wire*; both sampleBucket values must equal wire tick // 1000",
    "selection": "Only attempted Native and joinEligible live-frame Host witnesses with known wire clocks can join",
    "source": "source.pad is selected-native-source, not raw DInput; source.powerGeneration is independent of protocol power",
    "receiptCapturedSampleSequence": "diagnostic only; never a fallback for serialized sampleSequence",
    "sdk": "Actual call arguments and returned/threw only; no driver/game consumption or guessed report decoder",
    "numericComparison": "canonical float fields compare IEEE-754 binary32 values; integer/raw fields compare exactly",
    "controlMapScope": "explicit scalar map only; touchpad normalization, quaternion and encoded controls remain UNKNOWN",
    "observer": "Existing independent observer behavior is not assessed; no missed/not-missed claim",
    "targetCoverage": "Record-level SDK boundary only; Sony/Elite wrappers cannot establish SteamDeck partial-raw coverage; all paths UNKNOWN",
    "timeProximityUsed": False,
}

# Native assembled JSON -> Host parsed canonical envelope. SDK layout is separate.
CONTROL_MAP = [(k, k, "float") for k in ["lx", "ly", "rx", "ry", "lt", "rt", "ltRaw", "rtRaw"]] + [
    ("buttons", "buttons", "int"), ("backButtons", "backButtons", "int"),
    ("screenButtons", "screenButtons", "int"),
    ("raw.buttons", "rawButtons", "int"),
    ("raw.sThumbLX", "rawThumbLX", "short"), ("raw.sThumbLY", "rawThumbLY", "short"),
    ("raw.sThumbRX", "rawThumbRX", "short"), ("raw.sThumbRY", "rawThumbRY", "short"),
    ("raw.bLeftTrigger", "rawLeftTrigger", "byte"), ("raw.bRightTrigger", "rawRightTrigger", "byte"),
    ("imu.admitted", "imuAdmitted", "bool"),
] + [("imu." + group + "." + axis, prefix + axis.upper(), "number")
     for group, prefix in [("gyroDps", "gyro"), ("accelG", "accel") ] for axis in "xyz"]
PAD_MAP = [("buttons", "buttons"), ("ltByte", "bLeftTrigger"), ("rtByte", "bRightTrigger"),
           ("lxShort", "sThumbLX"), ("lyShort", "sThumbLY"), ("rxShort", "sThumbRX"), ("ryShort", "sThumbRY")]


def nested(value, path):
    for name in path.split("."):
        value = object_value(value).get(name)
    return value


def control_valid(value, kind):
    if kind == "bool":
        return type(value) is bool
    if kind in ["float", "number"]:
        return type(value) in [int, float] and abs(value) <= 3.4028234663852886e38 and math.isfinite(value)
    if type(value) is not int:
        return False
    return (-32768 <= value <= 32767 if kind == "short" else
            0 <= value <= 255 if kind == "byte" else 0 <= value <= 0xffffffff)


def compare_controls(left, right, mapping):
    fields, differences, unknown = {}, [], []
    for native_name, host_name, kind in mapping:
        a, b = nested(left, native_name), nested(right, host_name)
        valid = control_valid(a, kind) and control_valid(b, kind)
        equal = (struct.pack("!f", a) == struct.pack("!f", b) if kind == "float" else a == b) if valid else False
        status = "MATCH" if valid and equal else "DIFFERENT" if valid else "UNKNOWN"
        fields[native_name] = {"native": a, "host": b, "hostField": host_name, "status": status}
        if status == "DIFFERENT": differences.append(native_name)
        if status == "UNKNOWN": unknown.append(native_name)
    return {"status": "DIFFERENT" if differences else "UNKNOWN" if unknown else "MATCH",
            "fields": fields, "differences": differences, "unknown": unknown,
            "scope": "logged-values-only;differences-do-not-establish-loss-or-cause"}


def signal_key(row, host=False):
    issues = []
    request = object_value(row.get("request", row.get("binding") if host else None))
    canonical = object_value(row.get("canonical"))
    version = row.get("schema" if host else "formatVersion")
    if type(version) is not int or version != 1: issues.append("unsupported-signal-schema")
    for name in ["runId", "hostInstanceId", "persona", "profileIdentity"]:
        if not token(request.get(name)): issues.append("missing-request-" + name)
    for name in ["epoch", "commandSequence"]:
        if not integer(request.get(name), 1): issues.append("invalid-request-" + name)
    sample = canonical.get("sampleSequence") if host else request.get("sampleSequence")
    power = request.get("powerGeneration") if host else row.get("protocolPowerGeneration")
    tick, wire_sample = row.get("wireMonotonicTick"), row.get("wireSampleSequence")
    for name, value in [("sampleSequence", sample), ("protocolPowerGeneration", power),
                        ("wireMonotonicTick", tick), ("wireSampleSequence", wire_sample)]:
        if not integer(value, 1): issues.append("invalid-" + name)
    if integer(sample, 1) and integer(wire_sample, 1) and sample != wire_sample:
        issues.append("wire-sample-sequence-conflict")
    bucket = row.get("sampleBucket")
    if not integer(bucket) or not integer(tick, 1) or bucket != tick // 1000:
        issues.append("invalid-wire-sample-bucket")
    if host:
        if row.get("wireTickKnown") is not True: issues.append("wire-clock-unknown")
        if row.get("reason") != "live-frame": issues.append("not-live-frame")
        if row.get("joinEligible") is not True: issues.append("host-witness-not-join-eligible")
        if row.get("selection") not in ["binding-wire-bucket-first-live", "wire-bucket-first-live"]:
            issues.append("not-first-live-wire-bucket-witness")
        if "commandSequenceSource" in request and request["commandSequenceSource"] != "actual-command-envelope":
            issues.append("host-command-sequence-not-actual-request")
        if request.get("bindingTextTruncated") is not False: issues.append("request-text-completeness-unknown")
        if not integer(canonical.get("monotonicTick"), 1) or canonical.get("monotonicTick") != tick:
            issues.append("canonical-wire-tick-conflict-or-missing")
        if "sampleSequence" in request and (not integer(request["sampleSequence"], 1) or request["sampleSequence"] != sample):
            issues.append("request-canonical-sample-conflict")
    else:
        if row.get("wireClockKnown") is not True: issues.append("wire-clock-unknown")
        if row.get("submitAttempted") is not True: issues.append("no-native-submit-attempt")
        if "writerPid" in row and (not integer(row["writerPid"], 1) or row["writerPid"] != row.get("pid")):
            issues.append("observer-process-identity-conflict")
    key = (request.get("runId"), request.get("hostInstanceId"), request.get("epoch"),
           request.get("commandSequence"), sample, power, tick, wire_sample, request.get("persona"), request.get("profileIdentity"))
    return (None if issues else key), issues


def sdk_argument_check(row):
    args = object_value(row.get("sdkArguments"))
    result = {"kind": args.get("kind"), "actualArguments": args or None,
              "status": "UNKNOWN", "byteArgumentHashVerified": False,
              "decodedControls": "UNKNOWN", "hostEncodedNeutral": "UNKNOWN", "issues": [],
              "scope": "SDK-call-argument-only;not-driver-or-game-delivery"}
    if row.get("sampleOmitted"):
        result["issues"].append("sdk-arguments-omitted-" + str(row["sampleOmitted"]))
    if args.get("kind") == "SubmitState-argument":
        if isinstance(args.get("state"), dict) and args["state"]:
            result["status"] = "observed-state-argument"
        else: result["issues"].append("state-argument-unavailable")
    elif args.get("kind") == "SubmitRawReport-argument":
        length, size, text, digest = (args.get(k) for k in ["length", "hexBytes", "hex", "sha256"])
        valid = (integer(length, 1) and integer(size, 1) and size <= length and isinstance(text, str) and
                 len(text) == size * 2 and re.fullmatch(r"[0-9a-fA-F]*", text) is not None and
                 isinstance(digest, str) and re.fullmatch(r"[0-9a-fA-F]{64}", digest) is not None and
                 type(args.get("hexTruncated")) is bool and args["hexTruncated"] == (size < length))
        if valid:
            result["status"] = "observed-byte-prefix" if size < length else "observed-byte-argument"
            if size == length:
                result["byteArgumentHashVerified"] = hashlib.sha256(bytes.fromhex(text)).hexdigest() == digest.lower()
                if not result["byteArgumentHashVerified"]:
                    result["status"] = "DIFFERENT"
                    result["issues"].append("byte-argument-hash-conflict")
        else: result["issues"].append("byte-argument-incomplete-or-invalid")
    else: result["issues"].append("sdk-arguments-unavailable-or-unsupported")
    return result


def host_reason(reason):
    if reason == "live-frame": return "live-frame"
    if reason in ["touch", "internal-touch"]: return "internal-touch"
    if reason in ["idle", "internal-idle"]: return "internal-idle"
    if reason in ["submit-neutral-command", "release-neutral", "neutral"]: return "neutral-command"
    return "other-or-unknown"


def signal_analysis(records):
    summary = collections.Counter({name: 0 for name in [
        "nativeSignalRows", "hostSignalRows", "exactRequestMatches", "unmatchedNativeSignalRows",
        "unmatchedLiveHostSignalRows", "internalHostSignalRows", "ambiguousSignalKeys",
        "repeatedSignalRows", "matchedControlDifferences", "matchedControlUnknownRows",
        "matchedSdkArgumentUnknownRows", "sdkReturnedRows", "sdkThrewRows",
        "hostOutcomeUnknownRows", "selectedPadUnknownRows", "selectedPadDifferenceRows",
        "nativeSemanticNeutralUnknownRows", "supplementalRequestConflicts", "wireBucketGapCandidates"]})
    native, hosts, groups = [], [], [collections.defaultdict(list), collections.defaultdict(list)]
    seen = [set(), set()]
    for record in records:
        row = record["data"]
        is_host = row.get("event") == "input-host-signal" or row.get("kind") == "host-virtual-signal"
        if not is_host and row.get("event") != "input-native-signal-window": continue
        index = int(is_host)
        digest = json.dumps(mirror_payload(row), sort_keys=True, separators=(",", ":"))
        if digest in seen[index]:
            summary["repeatedSignalRows"] += 1
            continue
        seen[index].add(digest)
        key, issues = signal_key(row, is_host)
        item = {**location(record), "key": dict(zip(SIGNAL_CONTRACT["key"], key)) if key else None,
                "issues": issues, "join": "UNKNOWN", "driverDeliveryProven": False, "gameConsumptionProven": False}
        if is_host:
            category = host_reason(row.get("reason"))
            item.update({"reason": row.get("reason"), "category": category, "selection": row.get("selection"),
                         "request": row.get("request", row.get("binding")), "canonical": row.get("canonical"),
                         "declaredCoverage": row.get("coverage"), "evidenceScope": row.get("evidenceScope"),
                         "exceptionType": row.get("exceptionType"), "exceptionHResult": row.get("exceptionHResult"),
                         "outcome": row.get("outcome"), "sdk": sdk_argument_check(row), "window": row.get("window"),
                         "userKeyPressProven": False})
            summary["hostSignalRows"] += 1
            if category != "live-frame": summary["internalHostSignalRows"] += 1
            if row.get("outcome") in ["returned", "threw"]:
                summary["sdkReturnedRows" if row["outcome"] == "returned" else "sdkThrewRows"] += 1
            else: summary["hostOutcomeUnknownRows"] += 1
            hosts.append(item)
        else:
            source = object_value(row.get("source"))
            pad_mapping = [(a, b, "byte" if a.endswith("Byte") else "short" if a.endswith("Short") else "int") for a,b in PAD_MAP]
            item.update({"request": row.get("request"), "protocolPowerGeneration": row.get("protocolPowerGeneration"),
                         "wireMonotonicTick": row.get("wireMonotonicTick"), "wireSampleSequence": row.get("wireSampleSequence"),
                         "source": source, "sourcePadScope": "selected-native-source;not-raw-DInput",
                         "sampleBucket": row.get("sampleBucket"),
                         "nativeFinalFrame": row.get("nativeFinalFrame"),
                         "selectedPadToNativeRaw": compare_controls(source.get("pad"), object_value(row.get("nativeFinalFrame")).get("raw"), pad_mapping),
                         "semanticNeutral": row.get("semanticNeutral") if type(row.get("semanticNeutral")) is bool else None,
                         "submitAttempted": row.get("submitAttempted"), "hostAckAccepted": row.get("hostAckAccepted"),
                         "hostEncodedNeutral": "UNKNOWN", "window": row.get("window")})
            item["selectedPadToNativeRaw"]["boundaries"] = ["selected-native-source.pad", "nativeFinalFrame.raw"]
            summary["nativeSignalRows"] += 1
            if item["selectedPadToNativeRaw"]["unknown"]: summary["selectedPadUnknownRows"] += 1
            if item["selectedPadToNativeRaw"]["differences"]: summary["selectedPadDifferenceRows"] += 1
            if item["semanticNeutral"] is None: summary["nativeSemanticNeutralUnknownRows"] += 1
            native.append(item)
        if key: groups[index][key].append(item)
    matches = []
    for key in groups[0].keys() | groups[1].keys():
        ns, hs = groups[0].get(key, []), groups[1].get(key, [])
        if len(ns) > 1 or len(hs) > 1:
            summary["ambiguousSignalKeys"] += 1
            for item in ns + hs: item["join"] = "ambiguous-exact-key"
            continue
        if not ns or not hs: continue
        n, h = ns[0], hs[0]
        native_request, host_request = object_value(n["request"]), object_value(h["request"])
        conflicts = [field for field in ["persona", "requestId", "targetId", "ownerIdentity", "profileIdentity", "configHash", "configRevision"]
                     if field in native_request and field in host_request and
                     (type(native_request[field]) is not type(host_request[field]) or native_request[field] != host_request[field])]
        if conflicts:
            summary["supplementalRequestConflicts"] += 1
            for item in [n,h]:
                item["join"] = "conflicting-request-context"
                item["issues"].extend("request-context-conflict-" + field for field in conflicts)
            continue
        n["join"] = h["join"] = "exact-request-tuple-and-wire"
        comparison = compare_controls(n["nativeFinalFrame"], h["canonical"], CONTROL_MAP)
        # Missing raw-domain admission cannot be filled from equal scalar defaults.
        if object_value(h["canonical"]).get("hasRawDomain") is not True:
            comparison["unknown"].append("hasRawDomain")
            if comparison["status"] == "MATCH": comparison["status"] = "UNKNOWN"
        comparison["unmappedDomains"] = {
            "touchpads": {"native": object_value(n["nativeFinalFrame"]).get("touchpads"),
                          "host": {k: object_value(h["canonical"]).get(k) for k in ["screenPadsPresent", "screenPadLeft", "screenPadRight"]},
                          "status": "UNKNOWN"},
            "imu.quaternion": {"native": nested(n["nativeFinalFrame"], "imu.quaternion"), "host": None, "status": "UNKNOWN"}}
        comparison["scope"] = "explicit-native-to-canonical-scalar-map-only;not-all-controls-or-SDK-encoding"
        match = {"key": n["key"], "native": {k:n[k] for k in ["file", "line", "time"]},
                 "host": {k:h[k] for k in ["file", "line", "time"]}, "controls": comparison,
                 "sdkArgumentsStatus": h["sdk"]["status"], "sdkOutcome": h["outcome"],
                 "nativeSemanticNeutral": n["semanticNeutral"], "nativeHostAckAccepted": n["hostAckAccepted"],
                 "nativeSubmitAttempted": n["submitAttempted"], "hostEncodedNeutral": "UNKNOWN",
                 "scope": "same-request-native-to-Host-SDK-call-boundary-only",
                 "driverDeliveryProven": False, "gameConsumptionProven": False}
        matches.append(match)
        summary["exactRequestMatches"] += 1
        if comparison["differences"]: summary["matchedControlDifferences"] += 1
        if comparison["unknown"]: summary["matchedControlUnknownRows"] += 1
        if h["sdk"]["status"] in ["UNKNOWN", "DIFFERENT"]: summary["matchedSdkArgumentUnknownRows"] += 1
    summary["unmatchedNativeSignalRows"] = sum(n["join"] != "exact-request-tuple-and-wire" for n in native)
    summary["unmatchedLiveHostSignalRows"] = sum(h["category"] == "live-frame" and h["join"] != "exact-request-tuple-and-wire" for h in hosts)
    # Sampling skips are coverage candidates, not lost frames or missed observer events.
    witness_gaps = []
    for boundary, grouped in zip(["native", "host-live"], groups):
        streams = collections.defaultdict(set)
        for key in grouped:
            streams[(key[0], key[1], key[2], key[5], key[8], key[9])].add(key[6] // 1000)
        for identity, buckets in streams.items():
            ordered = sorted(buckets)
            for before, after in zip(ordered, ordered[1:]):
                if after - before > 1:
                    witness_gaps.append({"boundary": boundary,
                        "binding": dict(zip(["runId", "hostInstanceId", "epoch", "protocolPowerGeneration", "persona", "profileIdentity"], identity)),
                        "beforeBucket": before, "afterBucket": after, "unobservedWitnessBuckets": after-before-1,
                        "scope": "selected-witness-coverage-only;not-proof-of-input-loss-or-observer-failure"})
    summary["wireBucketGapCandidates"] = len(witness_gaps)
    gaps = []
    if not native: gaps.append("missing-native-signal-window-records")
    if not hosts: gaps.append("missing-host-sdk-signal-records")
    if summary["unmatchedNativeSignalRows"]: gaps.append("native-witnesses-without-unique-exact-live-host-witness")
    if summary["unmatchedLiveHostSignalRows"]: gaps.append("live-host-witnesses-without-unique-exact-native-witness")
    if summary["matchedControlUnknownRows"]: gaps.append("native-to-canonical-control-values-unknown")
    if summary["matchedControlDifferences"]: gaps.append("native-to-canonical-control-values-differ")
    if summary["matchedSdkArgumentUnknownRows"]: gaps.append("matched-sdk-arguments-unavailable-or-inconsistent")
    if witness_gaps: gaps.append("unobserved-selected-wire-witness-buckets;not-proven-input-loss")
    if summary["selectedPadUnknownRows"]: gaps.append("selected-source-pad-values-unknown")
    if summary["hostOutcomeUnknownRows"]: gaps.append("sdk-call-outcome-unknown")
    gaps += ["all-target-path-coverage-UNKNOWN;recorded-SDK-boundaries-only", "touchpad-and-quaternion-comparison-unavailable", "encoded-control-decoder-unavailable", "driver-and-game-consumption-unobserved"]
    boundaries = collections.Counter()
    reasons = collections.Counter()
    for h in hosts:
        r = object_value(h["request"])
        values = [r.get("persona"), r.get("profileIdentity"), h["sdk"]["kind"]]
        boundaries[tuple(v if token(v) else "UNKNOWN" for v in values)] += 1
        reasons[h["category"]] += 1
    coverage = {"scope": "observed-records-and-declared-coverage-only;not-all-product-paths",
                "allTargetPathsCovered": "UNKNOWN", "steamDeckPartialRawPathComplete": "UNKNOWN",
                "declaredCoverage": sorted({h["declaredCoverage"] for h in hosts if token(h["declaredCoverage"])}),
                "observedSdkBoundaries": [{**dict(zip(["persona", "profileIdentity", "sdkArgumentKind"], key)), "rows": count}
                                          for key, count in sorted(boundaries.items())]}
    return {"status": "partial-sdk-boundary-evidence" if matches else "unproven-from-export",
            "contract": SIGNAL_CONTRACT, "summary": dict(summary), "sdkCoverage": coverage,
            "hostReasonCounts": dict(reasons), "nativeEvidence": native,
            "hostEvidence": hosts, "exactMatches": sorted(matches, key=lambda m: tuple(m["key"].values())),
            "gaps": gaps, "witnessGaps": witness_gaps, "encodedControls": "UNKNOWN", "hostEncodedNeutral": "UNKNOWN",
            "driverDeliveryProven": False, "gameConsumptionProven": False}


def mirror_payload(row):
    # appendDomainLog adds writerPid and gets its own wall-clock timestamp.
    # Drop wall time only when an existing event-internal monotonic/request
    # token identifies the observation; do not deduplicate by temporal proximity.
    payload = dict(row)
    if "writerPid" not in row or (integer(row.get("writerPid"), 1) and row.get("writerPid") == row.get("pid")):
        payload.pop("writerPid", None)
    event = row.get("event")
    identifiable = (
        event == "input-host-frame-evidence" and integer(row.get("observedTick"), 1) or
        event == "input-idle-window" and integer(row.get("windowStartTick"), 1) and integer(row.get("windowEndTick"), 1) or
        event == "input-signal-activity" and integer(row.get("windowId"), 1) and integer(row.get("drainTick"), 1) or
        event in ["wake-input-chain-first-frame", "wake-input-chain-sustained"] and chain_ack_check(row) or
        event == "input-native-signal-window" and signal_key(row)[0] is not None or
        event == "input-host-signal" and signal_key(row, True)[0] is not None
    )
    if identifiable:
        payload.pop("time", None)
    return payload

def read_archive(archive):
    archive = pathlib.Path(archive)
    records = []
    counts = collections.Counter()
    coverage = {"malformedRows": 0, "oversizedRows": 0, "skippedFiles": [], "crossFileDuplicates": 0, "truncatedTailRows": 0, "readFiles": []}
    seen = collections.defaultdict(collections.Counter)
    with zipfile.ZipFile(archive) as bundle:
        for info in sorted(bundle.infolist(), key=lambda x: x.filename):
            base = info.filename.replace("\\", "/").rsplit("/", 1)[-1]
            if not FILES.fullmatch(base):
                continue
            if info.file_size > MAX_FILE_BYTES:
                coverage["skippedFiles"].append(info.filename)
                continue
            coverage["readFiles"].append(info.filename)
            with bundle.open(info) as stream:
                for line_number, line in enumerate(stream, 1):
                    if len(line) > MAX_ROW_BYTES:
                        coverage["oversizedRows"] += 1
                        continue
                    if not line.strip():
                        continue
                    try:
                        row = json.loads(line.decode("utf-8-sig"))
                    except (ValueError, UnicodeError):
                        coverage["malformedRows"] += 1
                        if not line.endswith(b"\n"): coverage["truncatedTailRows"] += 1
                        continue
                    if isinstance(row, dict) and base.lower().startswith("input-host-signal.log") and row.get("kind") == "host-virtual-signal":
                        row["event"] = "input-host-signal"
                    if not isinstance(row, dict) or not isinstance(row.get("event"), str) or row.get("event") not in EVENTS:
                        continue
                    # Native/domain mirroring and rotated overlap are duplicate
                    # rows, not independent signal counts. Preserve same-file repeats.
                    digest = hashlib.sha256(json.dumps(mirror_payload(row), sort_keys=True, separators=(",", ":"), ensure_ascii=False).encode()).digest()
                    occurrences = seen[digest]
                    occurrences[info.filename] += 1
                    mirrored_occurrences = max((count for filename, count in occurrences.items() if filename != info.filename), default=0)
                    if occurrences[info.filename] <= mirrored_occurrences:
                        coverage["crossFileDuplicates"] += 1
                        continue
                    counts[row["event"]] += 1
                    records.append({"file": info.filename, "line": line_number, "data": row})
    return records, counts, coverage

def analyze_records(records):
    summary = collections.Counter()
    frame_rows = []
    windows = []
    gap_examples = []
    frames_by_partial_key = collections.defaultdict(list)
    for record in records:
        row = record["data"]
        event = row.get("event")
        if event == "input-host-frame-evidence":
            check = frame_check(row)
            item = {**location(record), **check, "pid": row.get("pid"), "powerGeneration": row.get("powerGeneration")}
            frame_rows.append(item)
            summary["frameEvidenceRows"] += 1
            for contradiction in check["contradictions"]:
                summary[contradiction] += 1
            if check["localAckBound"]:
                summary["boundLocalAckEvidenceRows"] += 1
                if check["semanticNeutral"] is True:
                    summary["neutralLocalAckEvidenceRows"] += 1
                elif check["semanticNeutral"] is False:
                    summary["unmutedLocalAckEvidenceRows"] += 1
                if check["currentReadCallWindow"]:
                    summary["boundAckWithCurrentReadWindowRows"] += 1
            for reason in check["localAckIssues"] + check["sourceReadIssues"]:
                summary[reason] += 1
            if check["localAckBound"]:
                key = (row.get("pid"), row.get("powerGeneration"), check["tuple"]["epoch"], check["tuple"]["sampleSequence"])
                frames_by_partial_key[key].append(item)
        elif event in ["wake-input-chain-first-frame", "wake-input-chain-sustained"]:
            summary["boundAggregateAckRows" if chain_ack_check(row) else "unboundOrLegacyAggregateAckRows"] += 1
        elif event == "input-idle-window":
            checked = window_check(row)
            item = {**location(record), **checked, "source": row.get("source"), "windowStartTick": row.get("windowStartTick"), "windowEndTick": row.get("windowEndTick")}
            windows.append(item)
            for tag in checked["tags"]:
                summary[tag] += 1
        elif event == "input-signal-activity":
            # Copied drain-time content is intentionally not added to the 1Hz
            # content totals or joined to a frame by timestamp.
            summary["activityDrainRows"] += 1
        elif event in ["msi-wmi-key", "msi-backkey-edge", "gpd-back-chord-match", "rog-oem-key"]:
            if event == "msi-wmi-key" and row.get("accepted") is True:
                summary["acceptedMsiDedicatedKeyNotifications"] += 1
            if event == "msi-backkey-edge" and row.get("transition") == "press":
                summary["msiRearPressEvidenceRows"] += 1
            if event == "gpd-back-chord-match" and row.get("reason") == "press":
                summary["gpdRearChordPressEvidenceRows"] += 1
        if event in ["gamepad-setLed", "gamepad-vibrate", "gamepad-vibrate-request"]:
            summary["manualFeedbackRequestRows"] += 1
    links = []
    for record in records:
        row = record["data"]
        if row.get("event") != "msi-backkey-frame-link":
            continue
        if not integer(row.get("frameBackButtons"), 1):
            continue
        key = (row.get("pid"), row.get("powerGeneration"), row.get("epoch"), row.get("frameSequence"))
        key_valid = all(integer(value, 1) for value in key) and row.get("source") == "msi-dinput"
        candidates = [item for item in frames_by_partial_key.get(key, []) if item.get("source") == "msi-dinput"] if key_valid else []
        identities = {(c["tuple"]["runId"], c["tuple"]["hostInstanceId"], c["tuple"]["commandSequence"]) for c in candidates}
        item = {**location(record), "frameSequence": row.get("frameSequence"), "sourceGeneration": row.get("sourceGeneration"),
                "frameBackButtons": row.get("frameBackButtons"), "submitOutcome": row.get("submitOutcome"),
                "dropReason": row.get("dropReason"), "candidateHostReceipts": len(identities),
                "join": "unique-partial-native-frame-key" if len(identities) == 1 else "ambiguous" if identities else "no-same-frame-receipt-in-export",
                "fullSourceToHostBindingProven": False,
                "reason": "native rear link lacks runId/hostInstanceId and per-edge sample token; timestamps are not causal joins",
                "encodedBytesVerified": False, "steamConsumptionProven": False}
        if candidates and len(identities) == 1:
            item["candidateReceipt"] = {**{k: candidates[0][k] for k in ["file", "line", "time"]}, "tuple": candidates[0]["tuple"]}
        links.append(item)
    for frame in frame_rows:
        if frame["localAckIssues"] or frame["sourceReadIssues"]:
            if len(gap_examples) < 12:
                gap_examples.append(frame)
    signals = signal_analysis(records)
    summary.update(signals["summary"])
    has_bound_ack = bool(summary["boundLocalAckEvidenceRows"] or summary["boundAggregateAckRows"])
    verdict = "partial-local-evidence" if has_bound_ack or signals["summary"]["exactRequestMatches"] else "unproven-from-export"
    return {"verdict": verdict, "summary": dict(summary), "frameEvidence": frame_rows,
            "windowEvidence": windows, "rearFrameCandidates": links, "gapExamples": gap_examples,
            "signalEvidence": signals,
            "proof": {"localHostAckObserved": has_bound_ack, "sameFrameAckObserved": summary["boundLocalAckEvidenceRows"] > 0,
                      "physicalEndpointSampleBindingProven": False, "hardwareSampleAgeProven": False,
                      "encodedBytesVerified": False, "steamConsumptionProven": False,
                      "allDedicatedKeysVerified": False, "physicalLightingOrVibrationProven": False,
                      "sameRequestNativeToSdkBoundaryObserved": signals["summary"]["exactRequestMatches"] > 0,
                      "driverDeliveryProven": False},
            "nextEvidenceNeeded": [
                "Source sample token carried through canonical key and submitted-frame tuple; same token in Host ACK.",
                "For rear links: runId, hostInstanceId, epoch, command/sample sequence and source generation captured at one owned boundary.",
                "Actual encoded-report observation bound to the same request, separate from an API-return receipt.",
                "External Steam/game observation to prove consumption; current local logs cannot prove it."]}

def audit_archive(archive):
    records, counts, coverage = read_archive(archive)
    result = analyze_records(records)
    if coverage["malformedRows"] or coverage["oversizedRows"] or coverage["skippedFiles"]:
        result["signalEvidence"]["gaps"].append("archive-log-read-gaps;capture-completeness-UNKNOWN")
    identities = sorted({row["data"]["deviceClass"] for row in records if row["data"].get("event") == "machine-identity" and isinstance(row["data"].get("deviceClass"), str)})
    result.update({"schema": 1, "archive": str(pathlib.Path(archive).resolve()),
                   "archiveSha256": hashlib.sha256(pathlib.Path(archive).read_bytes()).hexdigest(),
                   "identities": identities, "eventRowsAfterCrossFileDedup": dict(counts), "coverage": coverage})
    return result

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--logs", required=True, help="ZIP path or directory of ZIP exports")
    p.add_argument("--output", required=True)
    args = p.parse_args()
    logs, output = pathlib.Path(args.logs), pathlib.Path(args.output)
    archives = [logs] if logs.is_file() else sorted(logs.rglob("*.zip"))
    output.mkdir(parents=True, exist_ok=True)
    results = []
    log_rows = []
    for archive in archives:
        result = audit_archive(archive)
        digest = result["archiveSha256"][:12]
        filename = archive.stem + "-" + digest + ".json"
        (output / filename).write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
        compact = {k: result[k] for k in ["archive", "archiveSha256", "identities", "verdict", "summary", "coverage", "proof"]}
        compact["signalEvidence"] = {k: result["signalEvidence"][k] for k in ["status", "summary", "gaps", "encodedControls", "hostEncodedNeutral", "sdkCoverage", "hostReasonCounts"]}
        compact["detailFile"] = filename
        results.append(compact)
        log_rows.append({"event": "controller-signal-reverse-result", **compact})
    report = {"schema": 1, "scope": "reverse evidence audit of existing exported controller signals; direction deferred",
              "archiveCount": len(results), "results": results, "hardwareIo": False, "signalContract": SIGNAL_CONTRACT,
              "productOrSettingsModified": False, "absenceOfEvidenceIsFailure": False,
              "timestampsUsedAsCausalJoin": False, "crossArchiveSessionsJoined": False,
              "completeOrGreenVerdictAvailable": False}
    (output / "signal-detection.json").write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    (output / "signal-detection.log").write_text("".join(json.dumps(row, ensure_ascii=False) + "\n" for row in log_rows), encoding="utf-8")
    markdown = ["# 手柄数据信号反向检测", "", "按导出包独立核对；跨文件镜像去重，不把相邻时间当因果。Host ACK、物理身份、编码字节和游戏消费分别判定。", "", "| 导出 | 帧证据行 | 绑定本地 ACK | 其中中性标记 | 有效读取窗口+ACK | 背键按压证据 |", "| --- | ---: | ---: | ---: | ---: | ---: |"]
    for row in results:
        s = row["summary"]
        markdown.append("| " + pathlib.Path(row["archive"]).name.replace("|", r"\|") + " | " + " | ".join(str(s.get(k, 0)) for k in ["frameEvidenceRows", "boundLocalAckEvidenceRows", "neutralLocalAckEvidenceRows", "boundAckWithCurrentReadWindowRows"]) + " | " + str(s.get("msiRearPressEvidenceRows", 0) + s.get("gpdRearChordPressEvidenceRows", 0)) + " |")
    markdown += ["", "| 导出 | Native witness | Host SDK witness | 精确匹配 | Native 缺对端 | live Host 缺对端 | 内部 Host | 控件差异 | 控件 UNKNOWN |", "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |"]
    for row in results:
        ss = row["signalEvidence"]["summary"]
        markdown.append("| " + pathlib.Path(row["archive"]).name.replace("|", r"\|") + " | " + " | ".join(str(ss[k]) for k in ["nativeSignalRows", "hostSignalRows", "exactRequestMatches", "unmatchedNativeSignalRows", "unmatchedLiveHostSignalRows", "internalHostSignalRows", "matchedControlDifferences", "matchedControlUnknownRows"]) + " |")
    for row in results:
        markdown += ["", "- " + pathlib.Path(row["archive"]).name + ": " + row["signalEvidence"]["status"] + "; gaps: " + ", ".join(row["signalEvidence"]["gaps"])]
    markdown += ["", "新日志只按完整 request tuple、protocolPowerGeneration 与 wire sample/tick 精确关联；source.powerGeneration 不作请求代次。source.pad 是 selected-native-source，不能当 raw DInput。内部 touch/idle/neutral 单列，SDK returned 不证明驱动或游戏消费；无 decoder 的实际编码控件及 Host encoded neutral 均为 UNKNOWN。旧包缺少两侧 witness 时保持 unproven，不推断 observer 漏报或未漏报。", "", "计数是去重后的诊断证据行，不是总提交帧数或独立实体按压次数。未静音 ACK 也可能携带零输入，不能当作按键有效。", "", "逐帧反查目前停在物理端点与样本绑定、设备内部样本年龄、同请求实际编码和外部消费的证据边界。窗口缺口只是复核候选，不直接判丢输入；没有按压/灯光/震动测试请求的项目保持待证。"]
    (output / "signal-detection.md").write_text("\n".join(markdown) + "\n", encoding="utf-8")
    print(json.dumps({"archives": len(results), "output": str(output.resolve()), "localAckArchives": sum(r["proof"]["localHostAckObserved"] for r in results), "hardwareIo": False, "externalConsumptionProven": False}, ensure_ascii=False))

if __name__ == "__main__":
    main()
