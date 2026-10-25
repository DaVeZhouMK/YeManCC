#!/usr/bin/env python3
"""Offline negative/scope tests using synthetic ZIP bytes; never runs the product."""
import argparse
from copy import deepcopy
from datetime import datetime, timezone
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import zipfile

# Do not leave __pycache__ outside the two-file source write scope.
sys.dont_write_bytecode = True
import msi_controller_visibility_log_audit as audit

BASE = 1791763200000  # 2026-10-12T00:00:00Z / 08:00 local
PATH = r"\\?\hid#vid_0db0&pid_1902&mi_00&col01#fixture#{hid}"
EXE = r"C:\SOFT\YeMan\YeManCC\YeManControllerVisibilityProbe.exe"


def stamp(ms):
    return 116444736000000000 + ms * 10000


def local(ms):
    return datetime.fromtimestamp((ms + 480 * 60000) / 1000, timezone.utc).replace(tzinfo=None).isoformat(timespec="milliseconds")


def start(observer=10, parent=20, creation=BASE, when=BASE + 1000, tick=1000):
    return {"event": "start", "observerPid": observer, "parentPid": parent,
            "parentCreationTime": stamp(creation), "utcEpochMs": when, "tick": tick,
            "inventoryId": 0, "observerExecutable": EXE, "formatVersion": 5, "readOnly": True}


def device(observer=10, when=BASE + 1100, inv=1, path=PATH, **extra):
    r = {"event": "directinput-device", "observerPid": observer, "utcEpochMs": when,
         "tick": when - BASE, "inventoryId": inv, "interfacePath": path,
         "instanceGuid": "{fixture-guid}", "enumerated": True,
         "createHresult": 0, "dataFormatHresult": 0, "cooperativeHresult": 0,
         "acquireHresult": 0, "stateHresult": 0, "stateApiSucceeded": True,
         "decodedContentChanged": False, "havePriorReading": False, "buttonsDownCount": 0,
         "freshHidReadHandleOpen": True, "freshHidReadHandleError": 0,
         "controllerState": {"axisValues": [0] * 8, "povValues": [4294967295] * 4, "buttonsDown": []}}
    r.update(extra)
    return r


def native(event, when, pid=20, **extra):
    r = {"event": event, "time": local(when), "pid": pid}
    r.update(extra)
    return r


def jsonl(rows):
    return "\n".join(json.dumps(r, ensure_ascii=False) for r in rows) + "\n"



def raw_controls(value=0, complete=True):
    axis = {"reportID": 1, "linkCollection": 0, "usagePage": 1, "usage": 0x30,
            "logicalMin": -32768, "logicalMax": 32767, "bitSize": 16, "HasNull": False,
            "rawUnsigned": value & 65535, "signedValue": value, "interpretedValue": value,
            "isSigned": True, "isAbsolute": True, "isNull": False, "controlValueKnown": True, "state": "decoded"}
    button = {"reportID": 1, "linkCollection": 0, "usagePage": 9, "usage": 1,
              "logicalMin": None, "logicalMax": None, "bitSize": None, "HasNull": None,
              "rawUnsigned": 1, "signedValue": 1, "state": "decoded", "valueSource": "HidP_GetUsages-presence"}
    return {"state": "decoded" if complete else "partial", "decodedControlsKnown": True, "decodeComplete": complete,
            "buttonsDown": [button], "buttons": [deepcopy(button)], "values": [axis],
            "unknown": [] if complete else ["unsupported-field"], "apiErrors": [], "limitsExceeded": False}


def raw_witness(tick=1600, sequence=1, delay=10, controls=None):
    return {"version": 1, "selectionSequence": sequence, "receiptTick": tick,
            "queueTimestampKnown": delay is not None, "postedTick": tick - delay if delay is not None else None,
            "queueDelayMs": delay, "hardwareSampleTimeKnown": False, "batchReportIndex": 0, "batchReportCount": 1,
            "reportSize": 8, "reportId": 1, "payloadReceiptHash": 123,
            "controls": raw_controls() if controls is None else controls, "reportBytesLogged": False}


def raw_collection(witnesses=None, path=PATH, handle=101, generation=1, **extra):
    witnesses = [raw_witness()] if witnesses is None else witnesses
    count = max(1, len(witnesses))
    ticks = [w["receiptTick"] for w in witnesses] or [1600]
    known = [w for w in witnesses if w["queueTimestampKnown"] is True]
    unknown = [w for w in witnesses if w["queueTimestampKnown"] is False]
    row = {"interfacePath": path, "rawDeviceHandle": handle, "deviceGeneration": generation,
           "vid": 0x0db0, "pid": 0x1902, "usagePage": 1, "usage": 5, "collectionRole": "msi-dinput-main-collection",
           "messageReceipts": count, "reportReceipts": sum(w["batchReportCount"] for w in witnesses) or count,
           "changedPayloadReports": 0, "firstReceiptTick": min(ticks), "lastReceiptTick": max(ticks),
           "queueTimestampsKnown": len(known), "queueTimestampsUnknown": len(unknown) if witnesses else count,
           "firstQueuedTick": min((w["postedTick"] for w in known), default=0),
           "lastQueuedTick": max((w["postedTick"] for w in known), default=0),
           "maxQueueDelayMs": max((w["queueDelayMs"] for w in known), default=0),
           "delayedMessages": sum(w["queueDelayMs"] > 3000 for w in known),
           "reportBytesLogged": False, "semanticInputDecoded": any(w["controls"].get("decodedControlsKnown") is True for w in witnesses),
           "semanticWitnessVersion": 1, "semanticWitnesses": witnesses,
           "semanticWitnessCandidates": count, "semanticWitnessSelections": len(witnesses),
           "semanticWitnessSuppressed": count - len(witnesses), "semanticWitnessClockRollbacks": 0,
           "semanticWitnessRecordsOverwritten": 0,
           "decodedWitnesses": sum(w["controls"].get("decodedControlsKnown") is True for w in witnesses),
           "completeWitnesses": sum(w["controls"].get("decodeComplete") is True for w in witnesses)}
    row.update(extra)
    return row


def raw_window(rows=None, begin=1000, end=2000, observer=10, **extra):
    row = {"event": audit.RAW_RECEIPT_EVENT, "observerPid": observer, "utcEpochMs": BASE + end, "tick": end,
           "windowStartTick": begin, "windowEndTick": end, "semanticWitnessVersion": 1,
           "collections": [raw_collection()] if rows is None else rows}
    row.update(extra)
    return row


class OfflineScopeTests(unittest.TestCase):
    workspace = None

    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="observer-audit-selftest-", dir=self.workspace)
        self.root = Path(self.directory.name).resolve()
        # Verify the automatic recursive cleanup target is inside the designated test workspace.
        self.assertTrue(self.root.is_relative_to(Path(self.workspace).resolve()))
        self.sequence = 0

    def tearDown(self):
        self.directory.cleanup()

    def zip(self, members, when=BASE + 10000, pid=20, manifest=True):
        self.sequence += 1
        path = self.root / f"fixture-{self.sequence}.zip"
        with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
            if manifest:
                z.writestr("YeManCC-export-manifest.txt", f"exportedAtLocal={local(when)}\r\nprocessId={pid}\r\ntimeZoneBiasMinutes=-480\r\n")
            for name, data in members.items():
                z.writestr(name, jsonl(data) if isinstance(data, list) else data)
        return path

    def report(self, rows, native_rows=None):
        members = {audit.OBSERVER_BASE: rows}
        if native_rows is not None:
            members[audit.NATIVE_BASE] = native_rows
        return audit.audit([self.zip(members)])

    def test_01_rotations_numeric_prev_and_exact_basename(self):
        r = device()
        z = self.zip({"logs/" + audit.OBSERVER_BASE + ".2": [start()],
                      "logs/" + audit.OBSERVER_BASE: [r],
                      "logs/" + audit.OBSERVER_BASE + ".prev": [r],
                      audit.OBSERVER_BASE + ".previous": [device(inv=99)],
                      audit.OBSERVER_BASE + ".bak": [device(inv=100)]})
        result = audit.audit([z])
        self.assertEqual(result["observerUniqueRows"], 2)
        self.assertEqual(result["duplicateObserverOccurrencesRemoved"], 1)
        self.assertEqual(len(result["archives"][0]["observerMembers"]), 3)
        self.assertEqual(len(result["sessions"]), 1)

    def test_02_canonical_repeated_zip_preserves_every_original_provenance(self):
        rows = [start(), device()]
        reverse = [dict(reversed(list(r.items()))) for r in rows]
        a = self.zip({audit.OBSERVER_BASE: rows})
        b = self.zip({audit.OBSERVER_BASE: jsonl(reverse).replace(": ", ":")})
        result = audit.audit([a, b, a])
        self.assertEqual(result["observerRawValidRows"], 6)
        self.assertEqual(result["observerUniqueRows"], 2)
        self.assertEqual(result["duplicateObserverOccurrencesRemoved"], 4)
        r = next(r for r in result["observerRecords"] if r["record"]["event"] == "directinput-device")
        self.assertEqual(len(r["provenance"]), 3)
        self.assertEqual({p["archiveId"] for p in r["provenance"]}, {0, 1, 2})
        self.assertEqual({p["line"] for p in r["provenance"]}, {2})
        self.assertGreater(len({p["rawLine"] for p in r["provenance"]}), 1)
        self.assertTrue(all(Path(p["archive"]).is_absolute() for p in r["provenance"]))

    def test_03_different_observers_not_merged_by_device_identity(self):
        result = self.report([start(), device(), start(observer=11), device(observer=11)])
        self.assertEqual(len(result["sessions"]), 2)
        self.assertEqual([s["interfaces"][0]["directInput"]["observations"] for s in result["sessions"]], [1, 1])
        self.assertEqual(len(result["unassignedObserverRecords"]), 0)

    def test_04_reused_observer_and_parent_pid_new_creation_lifetime(self):
        second = BASE + 4000
        result = self.report([start(), device(),
                              start(creation=second, when=second + 100, tick=4100),
                              device(when=second + 200, inv=1)])
        self.assertEqual(len(result["sessions"]), 2)
        self.assertNotEqual(result["sessions"][0]["sessionId"], result["sessions"][1]["sessionId"])
        self.assertNotEqual(result["sessions"][0]["identity"]["parentCreationTime"], result["sessions"][1]["identity"]["parentCreationTime"])
        self.assertEqual([s["uniqueRows"] for s in result["sessions"]], [2, 2])

    def test_05_neutral_cached_and_v5_hash_change_cannot_prove_delivery(self):
        result = self.report([start(), device(), device(when=BASE + 1200, inv=2, decodedContentChanged=True, havePriorReading=True)])
        d = result["sessions"][0]["interfaces"][0]["directInput"]
        self.assertEqual(d["stateApiSucceeded"], 2)
        self.assertEqual(d["legacyDecodedContentChangedTrue"], 1)
        self.assertEqual(d["recordedControlComparison"]["changedPairs"], 0)
        self.assertEqual(d["recordedControlComparison"]["loggedChangedButRecordedUnchangedPairs"], 1)
        self.assertFalse(d["freshPhysicalInputDeliveryProven"])
        self.assertFalse(d["steamConsumptionProven"])
        self.assertFalse(result["darkScreenCausationProven"])
        self.assertFalse(result["sessions"][0]["interfaces"][0]["opens"]["readFileStreamProven"])

    def test_06_pnp_only_and_qualified_read_open_not_directinput_or_stream(self):
        r = {"event": "msi-pnp-interface", "observerPid": 10, "utcEpochMs": BASE + 1100,
             "tick": 1100, "inventoryId": 1, "interfacePath": PATH, "enumeratedPnpInterface": True,
             "pnpStarted": True, "pnpProblem": 0, "freshMetadataHandleOpen": True,
             "freshMainHidReadProbeAttempted": True, "freshMainHidReadHandleOpen": True}
        result = self.report([start(), r])
        p = result["sessions"][0]["interfaces"][0]
        self.assertEqual(p["pnp"]["observations"], 1)
        self.assertEqual(p["directInput"]["enumDeviceObservations"], 0)
        self.assertEqual(p["directInput"]["getState"]["attempted"], 0)
        self.assertEqual(p["opens"]["qualifiedMainReadAccessSucceeded"], 1)
        self.assertFalse(p["opens"]["readFileStreamProven"])

    def test_07_latest_missing_never_borrows_old_session(self):
        old = self.zip({audit.OBSERVER_BASE: [start(), device()]}, when=BASE + 5000)
        new = self.zip({audit.NATIVE_BASE: [native("hidhide.visibility-snapshot", BASE + 8000, cloakOn=True)]}, when=BASE + 10000)
        result = audit.audit([new, old])
        self.assertEqual(result["latestArchiveIds"], [0])
        self.assertTrue(result["latestObserverMissing"])
        self.assertFalse(result["latestObserverEvidenceUsable"])
        self.assertEqual(result["archives"][0]["observerSessionIds"], [])
        self.assertIn("missing", audit.markdown(result))
        self.assertTrue(result["olderObserverSessionsAreNotLatestEvidence"])

    def test_08_wgi_same_vidpid_name_stays_unmapped(self):
        w = {"event": "wgi-device", "observerPid": 10, "utcEpochMs": BASE + 1150, "tick": 1150,
             "inventoryId": 1, "nonRoamableId": "opaque-wgi-id", "vid": 3504, "pid": 6402,
             "name": "same name", "stateApiSucceeded": True, "decodedContentChanged": False,
             "buttonsDownCount": 0, "readingTimestamp": 0}
        result = self.report([start(), device(vid=3504, pid=6402, name="same name"), w])
        s = result["sessions"][0]
        self.assertEqual(len(s["interfaces"]), 1)
        self.assertEqual(s["wgi"][0]["mappingStatus"], "unmapped")
        self.assertEqual(s["wgi"][0]["miColAttribution"], "not-made")
        self.assertEqual(s["interfaces"][0]["directInput"]["observations"], 1)

    def test_09_malformed_partial_tail_nonobject_nan_and_duplicate_keys(self):
        raw = jsonl([start(), device()]) + '[]\n{"x":NaN}\n{"x":1,"x":2}\n{"event":'
        result = audit.audit([self.zip({audit.OBSERVER_BASE: raw})])
        self.assertEqual(result["observerUniqueRows"], 2)
        self.assertEqual(result["malformedRowCount"], 4)
        last = result["malformedRows"][-1]
        self.assertTrue(last["terminalLine"])
        self.assertTrue(last["unterminatedTail"])
        self.assertEqual(last["provenance"]["line"], 6)
        self.assertEqual(last["provenance"]["rawLine"], '{"event":')

    def test_10_stage_failures_unsigned_hresult_and_missing_stage(self):
        failed = device(createHresult=2147500037)
        for key in ("acquireHresult", "stateHresult", "stateApiSucceeded", "controllerState"):
            failed.pop(key)
        ok = device(when=BASE + 1200, inv=2, acquireHresult=1)
        result = self.report([start(), failed, ok])
        d = result["sessions"][0]["interfaces"][0]["directInput"]
        self.assertEqual(d["create"]["failed"], 1)
        self.assertEqual(d["acquire"]["attempted"], 1)
        self.assertEqual(d["acquire"]["succeeded"], 1)
        self.assertEqual(d["getState"]["notRecorded"], 1)
        self.assertFalse(audit.hresult_succeeded(-2147467259))
        self.assertFalse(audit.hresult_succeeded(True))

    def test_11_no_start_and_post_stop_do_not_get_assigned(self):
        no_start = self.report([device()])
        self.assertEqual(len(no_start["sessions"]), 0)
        self.assertEqual(len(no_start["unassignedObserverRecords"]), 1)
        stop = {**start(), "event": "stop", "utcEpochMs": BASE + 1150, "tick": 1150, "reason": "parent-exited"}
        result = self.report([start(), device(), stop, device(when=BASE + 1200, inv=2)])
        self.assertEqual(result["sessions"][0]["uniqueRows"], 3)
        self.assertEqual(len(result["unassignedObserverRecords"]), 1)
        self.assertFalse(result["sessions"][0]["rightCensored"])

    def test_12_native_config_rejects_wrong_parent_and_old_lifetime(self):
        current = BASE + 4000
        rows = [start(creation=current, when=current + 100, tick=4100), device(when=current + 200)]
        config = [native("boot-single-instance-acquired", BASE + 20),
                  native("hidhide.visibility-snapshot", BASE + 100, cloakOn=True, inverseApplicationList="off"),
                  native("boot-single-instance-acquired", current + 20),
                  native("hidhide.visibility-snapshot", current + 150, cloakOn=True, inverseApplicationList="off"),
                  native("hidhide.visibility-snapshot", current + 160, pid=99, cloakOn=False),
                  native("hidhide.visibility-snapshot", current + 500, cloakOn=False)]
        result = self.report(rows, config)
        scope = result["sessions"][0]["nativeScope"]
        self.assertTrue(scope["nativeBootAnchorObserved"])
        self.assertEqual(scope["config"]["visibilitySnapshots"], 1)
        self.assertEqual(scope["config"]["points"][0]["timeLocal"], local(current + 150))

    def test_13_text_application_list_and_inverse_cloak_not_resolved(self):
        config = [native("boot-single-instance-acquired", BASE + 20),
                  native("hidhide-app-list", BASE + 1099, applications=[r"C:\Steam\steam.exe", EXE], phase="hide"),
                  native("hidhide.visibility-snapshot", BASE + 1100, cloakOn=True, inverseApplicationList="off", phase="hide")]
        result = self.report([start(), device(when=BASE + 1200)], config)
        cfg = result["sessions"][0]["nativeScope"]["config"]
        self.assertEqual(cfg["textApplicationListRows"], 1)
        self.assertEqual(cfg["completeSameSnapshotCount"], 0)
        snap = next(p for p in cfg["points"] if p["event"] == "hidhide.visibility-snapshot")
        self.assertFalse(snap["steamExemptionResolved"])
        self.assertFalse(snap["observerExemptionResolved"])
        self.assertFalse(snap["nearbySeparateApplicationLists"][0]["atomicWithSnapshot"])
        self.assertEqual(snap["nearbySeparateApplicationLists"][0]["deltaMs"], -1)

    def test_14_embedded_applications_are_config_not_input_or_resolved_process(self):
        config = [native("hidhide.visibility-snapshot", BASE + 1100,
                         cloakOn=True, applications={"readbackKnown": True, "paths": [EXE, r"C:\Steam\steam.exe"],
                                                     "inverseState": "off", "inverseRaw": "--inv-off"})]
        result = self.report([start(), device(when=BASE + 1200)], config)
        cfg = result["sessions"][0]["nativeScope"]["config"]
        self.assertEqual(cfg["completeSameSnapshotCount"], 1)
        self.assertTrue(cfg["points"][0]["observerExecutableTextMembership"])
        self.assertFalse(cfg["steamExemptionResolved"])
        self.assertFalse(result["steamConsumptionProven"])

    def test_15_exact_path_case_is_not_silently_merged(self):
        result = self.report([start(), device(), device(when=BASE + 1150, path=PATH.upper())])
        self.assertEqual(len(result["sessions"][0]["interfaces"]), 2)

    def test_16_failed_or_missing_inventory_gaps_break_recorded_prior(self):
        fail = device(when=BASE + 1200, inv=2, stateApiSucceeded=False, stateHresult=-2147467259)
        fail.pop("controllerState")
        rows = [start(), device(), fail, device(when=BASE + 1300, inv=3, decodedContentChanged=True),
                device(when=BASE + 1500, inv=5, decodedContentChanged=True)]
        for inv in (1, 3, 5):
            rows.append({"event": "directinput-inventory", "observerPid": 10, "utcEpochMs": BASE + 1000 + inv * 100,
                         "tick": 1000 + inv * 100, "inventoryId": inv, "count": 128, "limit": 128, "hresult": 0})
        result = self.report(rows)
        s = result["sessions"][0]
        self.assertEqual(s["interfaces"][0]["directInput"]["recordedControlComparison"]["comparedPairs"], 0)
        self.assertEqual(s["inventoryCoverage"]["directinput-inventory"]["missingInventoryIdRanges"], [[2, 2], [4, 4]])
        self.assertEqual(s["inventoryCoverage"]["directinput-inventory"]["limitReachedObservations"], 3)

    def test_17_empty_latest_log_is_not_missing_or_usable(self):
        result = audit.audit([self.zip({audit.OBSERVER_BASE: ""})])
        self.assertFalse(result["latestObserverMissing"])
        self.assertFalse(result["latestObserverEvidenceUsable"])
        self.assertEqual(result["archives"][0]["observerPresence"], "present-empty-or-unparseable")

    def test_18_timezone_manifest_and_explicit_latest_no_mtime_guess(self):
        z = self.zip({audit.OBSERVER_BASE: [start()]})
        result = audit.audit([z])
        self.assertEqual(result["archives"][0]["exportEpochMs"], BASE + 10000)
        self.assertEqual(result["archives"][0]["timeZoneOffsetMinutes"], 480)
        no_manifest = self.zip({audit.OBSERVER_BASE: [start()]}, manifest=False)
        with self.assertRaisesRegex(ValueError, "specify --latest-zip"):
            audit.audit([no_manifest])
        self.assertEqual(audit.audit([no_manifest], latest_zip=no_manifest)["latestArchiveIds"], [0])
        with self.assertRaisesRegex(ValueError, "must also"):
            audit.audit([z], latest_zip=self.root / "not-an-input.zip")

    def test_19_cli_roundtrip_uses_only_this_bundled_python_and_auditor(self):
        z = self.zip({audit.OBSERVER_BASE: [start(), device()]})
        output = self.root / "observer-audit-cli.json"
        command = [sys.executable, "-B", str(Path(audit.__file__).resolve()), "--zip", str(z), "--output", str(output)]
        proc = subprocess.run(command, capture_output=True, text=True, encoding="utf-8", timeout=30)
        self.assertEqual(proc.returncode, 0, proc.stderr)
        result = json.loads(output.read_text(encoding="utf-8"))
        self.assertEqual(result["observerUniqueRows"], 2)
        self.assertTrue(output.with_suffix(".md").is_file())
        self.assertEqual(json.loads(proc.stdout)["sessions"], 1)
        before = audit.file_sha256(z)
        reject = subprocess.run(command[:-1] + [str(z)], capture_output=True, text=True, encoding="utf-8", timeout=30)
        self.assertEqual(reject.returncode, 2)
        self.assertEqual(audit.file_sha256(z), before)


    def test_20_same_parent_lifetime_observer_restart_start_time_separates(self):
        result = self.report([start(), device(),
            start(when=BASE + 2000, tick=2000), device(when=BASE + 2100)])
        self.assertEqual(len(result["sessions"]), 2)
        self.assertEqual([s["uniqueRows"] for s in result["sessions"]], [2, 2])
        self.assertNotEqual(result["sessions"][0]["sessionId"], result["sessions"][1]["sessionId"])

    def test_21_old_parent_scope_stops_at_pid_reuse_boot(self):
        config = [native("boot-single-instance-acquired", BASE + 20),
                  native("hidhide.visibility-snapshot", BASE + 1100, cloakOn=True),
                  native("boot-single-instance-acquired", BASE + 3000),
                  native("hidhide.visibility-snapshot", BASE + 3100, cloakOn=False)]
        result = self.report([start(), device(when=BASE + 4000)], config)
        scope = result["sessions"][0]["nativeScope"]
        self.assertTrue(scope["nextSamePidBootBounded"])
        self.assertEqual(scope["config"]["visibilitySnapshots"], 1)
        self.assertTrue(scope["config"]["points"][0]["cloakOn"])
        context = result["sessions"][0]["interfaces"][0]["nativeVisibilityPointContext"][0]
        self.assertEqual(context["snapshotAgeMs"], 2900)
        self.assertIn("NOT proof", context["scope"])

    def test_22_invalid_schema_and_utf8_tail_retained_without_crash(self):
        raw = jsonl([start(), device()]).encode() + b'{"event":[]}\n{"event":"x","observerPid":{}}\n\xff'
        result = audit.audit([self.zip({audit.OBSERVER_BASE: raw})])
        self.assertEqual(result["observerUniqueRows"], 2)
        self.assertEqual(result["malformedRowCount"], 3)
        self.assertTrue(result["malformedRows"][-1]["unterminatedTail"])

    def test_23_native_exact_path_snapshot_point_context_not_persistent_proof(self):
        config = [native("hidhide.visibility-snapshot", BASE + 1090, cloakOn=True,
                         inverseApplicationList="off", targetInventory=[{"interfacePath": PATH, "gameInput": True}])]
        result = self.report([start(), device()], config)
        p = result["sessions"][0]["interfaces"][0]
        context = p["nativeVisibilityPointContext"][0]
        self.assertEqual(context["snapshotAgeMs"], 10)
        self.assertTrue(context["exactPathTargetInventoryMatches"][0]["gameInput"])
        self.assertFalse(context["applicationsAtSameSnapshot"])
        self.assertFalse(result["sessions"][0]["nativeScope"]["scopeIsContinuousConfigProof"])

    def test_24_inconsistent_success_flag_cannot_be_recorded_success_pair(self):
        row = device(when=BASE + 1200, inv=2, stateApiSucceeded=True, stateHresult=-2147467259)
        result = self.report([start(), device(), row])
        comp = result["sessions"][0]["interfaces"][0]["directInput"]["recordedControlComparison"]
        self.assertEqual(comp["comparedPairs"], 0)


    def raw_summary(self, windows, native_rows=None, prefix=None):
        result = self.report(([start()] if prefix is None else prefix) + windows, native_rows)
        self.assertEqual(result["malformedRowCount"], 0)
        return result, result["sessions"][0]["rawInputReceipts"]

    def assert_no_raw_conclusions(self, raw):
        for flag in ("freshPhysicalInputDeliveryProven", "steamConsumptionProven", "hardwareSampleTimeKnown",
                     "selfHealingTriggered", "steamExemptionResolved", "observerExemptionResolved"):
            self.assertIs(raw[flag], False)
        for state in ("idle", "phantom", "suppressionLeak"):
            self.assertEqual(raw[state], "unknown")

    def test_25_raw_old_format_changed_payload_is_unknown(self):
        row = raw_collection([], changedPayloadReports=1)
        for field in audit.WITNESS_COUNTERS + ("semanticWitnessVersion", "semanticWitnesses"):
            row.pop(field)
        window = raw_window([row])
        window.pop("semanticWitnessVersion")
        result, raw = self.raw_summary([window])
        obs = raw["collections"][0]["observations"][0]
        self.assertEqual(obs["witnessFormatStatus"], "legacy-no-witness-unknown")
        self.assertEqual(obs["validationStatus"], "valid")
        self.assertEqual(raw["semanticEvidenceStatus"], "unknown")
        self.assertEqual(raw["retainedWitnessObservations"], 0)
        self.assertEqual(raw["collections"][0]["loggedCounterSums"]["changedPayloadReports"], 1)
        self.assertIsNone(raw["collections"][0]["loggedCounterSums"]["decodedWitnesses"])
        self.assertIn(obs["recordId"], [r["recordId"] for r in result["observerRecords"]])
        self.assert_no_raw_conclusions(raw)

    def test_26_raw_normal_gamepad_signed_axis_and_buttons(self):
        row = raw_collection([raw_witness(controls=raw_controls(-123))])
        result, raw = self.raw_summary([raw_window([row])])
        self.assertEqual((raw["validUniqueWitnesses"], raw["decodedWitnesses"], raw["completeWitnesses"]), (1, 1, 1))
        group = raw["collections"][0]
        self.assertEqual((group["interfacePath"], group["rawDeviceHandle"], group["deviceGeneration"]), (PATH, 101, 1))
        self.assertEqual(group["sessionId"], result["sessions"][0]["sessionId"])
        witness = group["observations"][0]["semanticWitnesses"][0]
        self.assertEqual(witness["validationErrors"], [])
        self.assertEqual(witness["loggedWitness"]["controls"]["values"][0]["signedValue"], -123)
        self.assertIn("WM_INPUT", audit.markdown(result))
        self.assert_no_raw_conclusions(raw)

    def test_27_raw_xinput_and_joystick_are_semantic_eligible(self):
        for pid, path, role in ((0x1902, PATH, "msi-dinput-main-collection"),
                                (0x1901, PATH.replace("1902", "1901"), "msi-xinput-game-collection")):
            with self.subTest(pid=pid):
                _, raw = self.raw_summary([raw_window([raw_collection(path=path, pid=pid, usage=4, collectionRole=role)])])
                self.assertEqual(raw["completeWitnesses"], 1)

    def test_28_raw_vendor_consumer_are_counts_only_and_cannot_spoof_game(self):
        cases = [(PATH.replace("col01", "col02"), 0x1902, 0xfff0, 0x40, "msi-vendor-collection"),
                 (PATH.replace("mi_00&col01", "mi_01&col03"), 0x1902, 12, 1, "msi-consumer-collection"),
                 (PATH.replace("1902&mi_00&col01", "1901&mi_01"), 0x1901, 0xffa0, 1, "msi-xinput-vendor-collection"),
                 (PATH.replace("1902&mi_00&col01", "1901&mi_02&col03"), 0x1901, 12, 1, "msi-xinput-consumer-collection")]
        for path, pid, page, usage, role in cases:
            with self.subTest(role=role):
                row = raw_collection([], path=path, pid=pid, usagePage=page, usage=usage, collectionRole=role,
                                     semanticWitnessCandidates=0, semanticWitnessSuppressed=0)
                _, raw = self.raw_summary([raw_window([row])])
                self.assertEqual(raw["collections"][0]["observations"][0]["validationStatus"], "valid")
                self.assertEqual(raw["collections"][0]["loggedCounterSums"]["messageReceipts"], 1)
                self.assertEqual(raw["decodedWitnesses"], 0)
                for forge_metadata in (False, True):
                    forged = raw_collection(path=path, pid=pid, usagePage=1 if forge_metadata else page,
                                            usage=5 if forge_metadata else usage,
                                            collectionRole="msi-xinput-game-collection" if pid == 0x1901 else "msi-dinput-main-collection")
                    _, bad = self.raw_summary([raw_window([forged])])
                    self.assertEqual(bad["invalidWitnessObservations"], 1)
                    self.assertEqual(bad["decodedWitnesses"], 0)
                    self.assert_no_raw_conclusions(bad)

    def test_29_raw_queue_delay_is_not_hardware_sample_time(self):
        row = raw_collection([raw_witness(tick=6000, delay=4500)])
        _, raw = self.raw_summary([raw_window([row], begin=5000, end=6500)])
        self.assertEqual(raw["completeWitnesses"], 1)  # Posting before the receipt window is legitimate.
        w = raw["collections"][0]["observations"][0]["semanticWitnesses"][0]
        self.assertEqual(w["queueStatus"], "known")
        self.assertEqual(w["loggedWitness"]["queueDelayMs"], 4500)
        self.assert_no_raw_conclusions(raw)

    def test_30_raw_unknown_queue_remains_unknown_but_controls_can_be_logged(self):
        _, raw = self.raw_summary([raw_window([raw_collection([raw_witness(delay=None)])])])
        w = raw["collections"][0]["observations"][0]["semanticWitnesses"][0]
        self.assertEqual(raw["completeWitnesses"], 1)
        self.assertEqual(w["queueStatus"], "unknown")
        self.assertIsNone(w["loggedWitness"]["postedTick"])
        self.assert_no_raw_conclusions(raw)

    def test_31_raw_bad_queue_fields_are_never_trusted(self):
        changes = [("queueTimestampKnown", 1), ("postedTick", True), ("postedTick", 1700),
                   ("queueDelayMs", 9), ("queueDelayMs", "10"), ("queueDelayMs", 900001)]
        for field, value in changes:
            with self.subTest(field=field, value=value):
                row = raw_collection()
                row["semanticWitnesses"][0][field] = value
                _, raw = self.raw_summary([raw_window([row])])
                self.assertEqual(raw["invalidWitnessObservations"], 1)
                self.assertEqual(raw["decodedWitnesses"], 0)
        row = raw_collection([raw_witness(delay=None)])
        row["semanticWitnesses"][0]["postedTick"] = 0
        _, raw = self.raw_summary([raw_window([row])])
        self.assertEqual(raw["invalidWitnessObservations"], 1)

    def test_32_raw_queue_aggregate_inconsistency_invalidates_witness(self):
        for field, value in (("queueTimestampsKnown", 0), ("queueTimestampsUnknown", 1), ("maxQueueDelayMs", 9),
                             ("lastQueuedTick", 1589), ("delayedMessages", 1), ("messageReceipts", True)):
            with self.subTest(field=field):
                _, raw = self.raw_summary([raw_window([raw_collection(**{field: value})])])
                self.assertEqual(raw["invalidWitnessObservations"], 1)
                self.assertEqual(raw["completeWitnesses"], 0)

    def test_33_raw_sample_outside_window_or_batch_shape_invalid(self):
        for field, value in (("receiptTick", 999), ("receiptTick", 2001), ("receiptTick", True),
                             ("batchReportIndex", 1), ("batchReportIndex", False), ("batchReportCount", 0),
                             ("batchReportCount", 1025), ("reportSize", 0), ("reportSize", 4097),
                             ("reportId", 256), ("payloadReceiptHash", True), ("version", True),
                             ("hardwareSampleTimeKnown", True), ("reportBytesLogged", 0)):
            with self.subTest(field=field, value=value):
                row = raw_collection()
                row["semanticWitnesses"][0][field] = value
                _, raw = self.raw_summary([raw_window([row])])
                self.assertEqual(raw["invalidWitnessObservations"], 1)
                self.assertEqual(raw["decodedWitnesses"], 0)
        row = raw_collection()
        row["semanticWitnesses"][0].update(batchReportCount=2, batchReportIndex=1)
        _, raw = self.raw_summary([raw_window([row])])
        self.assertEqual(raw["invalidWitnessObservations"], 1)

    def test_34_raw_missing_required_fields_preserve_originals(self):
        for field in raw_witness():
            with self.subTest(witnessMissing=field):
                row = raw_collection()
                del row["semanticWitnesses"][0][field]
                result, raw = self.raw_summary([raw_window([row])])
                self.assertEqual(raw["invalidWitnessObservations"], 1)
                self.assertEqual(raw["decodedWitnesses"], 0)
                logged = raw["collections"][0]["observations"][0]["semanticWitnesses"][0]["loggedWitness"]
                self.assertNotIn(field, logged)
                self.assertEqual(result["observerUniqueRows"], 2)
        for field in ("interfacePath", "rawDeviceHandle", "deviceGeneration", "vid", "pid", "usagePage", "usage",
                      "collectionRole", "semanticWitnessVersion", "queueTimestampsKnown") + audit.WITNESS_COUNTERS:
            with self.subTest(collectionMissing=field):
                row = raw_collection()
                row.pop(field)
                _, raw = self.raw_summary([raw_window([row])])
                self.assertEqual(raw["decodedWitnesses"], 0)
                self.assertEqual(raw["invalidWitnessObservations"], 1)

    def test_35_raw_forged_flags_and_hid_field_types(self):
        for field, value in (("decodedControlsKnown", 1), ("decodeComplete", "true"), ("state", "unknown"),
                             ("buttonsDown", [1]), ("values", {}), ("limitsExceeded", 0), ("unknown", ["unsupported"])):
            with self.subTest(field=field):
                row = raw_collection()
                row["semanticWitnesses"][0]["controls"][field] = value
                _, raw = self.raw_summary([raw_window([row])])
                self.assertEqual(raw["decodedWitnesses"], 0)
                self.assertEqual(raw["invalidWitnessObservations"], 1)
        for field, value in (("reportID", True), ("usagePage", True), ("usage", 0xff00), ("bitSize", True),
                             ("HasNull", 0), ("rawUnsigned", True), ("signedValue", 1), ("logicalMin", None), ("isNull", True)):
            with self.subTest(hidField=field):
                row = raw_collection()
                row["semanticWitnesses"][0]["controls"]["values"][0][field] = value
                _, raw = self.raw_summary([raw_window([row])])
                self.assertEqual(raw["decodedWitnesses"], 0)
                self.assertEqual(raw["invalidWitnessObservations"], 1)

    def test_36_raw_partial_unknown_and_hid_null_are_distinct(self):
        unknown = {"state": "unknown", "decodedControlsKnown": False, "decodeComplete": False, "buttonsDown": [], "values": []}
        for controls, decoded, complete in ((unknown, 0, 0), (raw_controls(complete=False), 1, 0)):
            _, raw = self.raw_summary([raw_window([raw_collection([raw_witness(controls=controls)])])])
            self.assertEqual(raw["validUniqueWitnesses"], 1)
            self.assertEqual((raw["decodedWitnesses"], raw["completeWitnesses"]), (decoded, complete))
            self.assert_no_raw_conclusions(raw)
        controls = raw_controls()
        controls["buttons"] = controls["buttonsDown"] = []
        controls["values"][0].update(logicalMin=0, logicalMax=7, bitSize=4, rawUnsigned=8, signedValue=-8,
                                     interpretedValue=8, HasNull=True, isSigned=False, isNull=True,
                                     controlValueKnown=False, state="null")
        _, raw = self.raw_summary([raw_window([raw_collection([raw_witness(controls=controls)])])])
        self.assertEqual(raw["completeWitnesses"], 1)
        self.assert_no_raw_conclusions(raw)

    def test_37_raw_duplicate_sample_across_observations_counts_once(self):
        first, second = raw_window(), raw_window(end=2100)
        result, raw = self.raw_summary([first, second])
        self.assertEqual((raw["retainedWitnessObservations"], raw["validUniqueWitnesses"], raw["decodedWitnesses"]), (2, 1, 1))
        self.assertEqual(raw["duplicateWitnessObservations"], 1)
        observations = raw["collections"][0]["observations"]
        self.assertEqual(len(observations), 2)
        self.assertNotEqual(observations[0]["recordId"], observations[1]["recordId"])
        self.assertEqual(observations[1]["semanticWitnesses"][0]["duplicateOf"]["recordId"], observations[0]["recordId"])
        self.assertEqual(result["observerUniqueRows"], 3)

    def test_38_raw_reused_sequence_across_generation_or_path_is_conflict(self):
        for extra in ({"generation": 2}, {"handle": 102}, {"path": PATH.upper()}):
            with self.subTest(extra=extra):
                _, raw = self.raw_summary([raw_window(), raw_window([raw_collection(**extra)], end=2100)])
                self.assertEqual(len(raw["collections"]), 2)
                self.assertEqual(raw["invalidWitnessObservations"], 2)
                self.assertEqual(raw["decodedWitnesses"], 0)
        row = raw_collection()
        row["semanticWitnesses"][0]["controls"] = raw_controls(1)
        _, raw = self.raw_summary([raw_window(), raw_window([row], end=2100)])
        self.assertEqual(raw["invalidWitnessObservations"], 2)

    def test_39_raw_exact_tuple_joins_and_separate_generations(self):
        rows = [raw_collection(), raw_collection([raw_witness(2200, 2)], generation=2),
                raw_collection([raw_witness(2800, 3)], handle=102),
                raw_collection([raw_witness(3400, 4)], path=PATH.upper())]
        _, raw = self.raw_summary([raw_window(rows, end=4000)])
        self.assertEqual(len(raw["collections"]), 4)
        self.assertEqual(raw["completeWitnesses"], 4)
        # Same tuple in a later window remains within observations.
        later = raw_collection([raw_witness(4600, 5)])
        _, raw = self.raw_summary([raw_window(rows, end=4000), raw_window([later], begin=4000, end=5000)])
        self.assertEqual(len(raw["collections"]), 4)
        self.assertEqual(len(raw["collections"][0]["observations"]), 2)
        self.assertEqual(raw["completeWitnesses"], 5)

    def test_40_raw_pid_reuse_separates_sessions_even_with_same_sample_tuple(self):
        result = self.report([start(), raw_window(), start(creation=BASE + 3000, when=BASE + 4000, tick=1000),
                              dict(raw_window(), utcEpochMs=BASE + 5000)])
        self.assertEqual(len(result["sessions"]), 2)
        for session in result["sessions"]:
            self.assertEqual(session["rawInputReceipts"]["completeWitnesses"], 1)
        self.assertNotEqual(result["sessions"][0]["rawInputReceipts"]["collections"][0]["sessionId"],
                            result["sessions"][1]["rawInputReceipts"]["collections"][0]["sessionId"])

    def test_41_raw_config_snapshot_never_resolves_exemption_or_filter_leak(self):
        config = [native("hidhide.visibility-snapshot", BASE + 1500, cloakOn=True,
                         applications={"readbackKnown": True, "paths": [EXE, r"C:\Steam\steam.exe"], "inverseState": "off"},
                         configurationVerified=True, externalVisibility=False, filterAttached=True)]
        result, raw = self.raw_summary([raw_window()], config)
        cfg = result["sessions"][0]["nativeScope"]["config"]
        self.assertEqual(cfg["completeSameSnapshotCount"], 1)
        self.assertIs(cfg["observerExemptionResolved"], False)
        self.assertIs(cfg["steamExemptionResolved"], False)
        self.assertEqual(raw["completeWitnesses"], 1)
        self.assert_no_raw_conclusions(raw)

    def test_42_raw_generic_read_open_does_not_supply_missing_receipt_or_witness(self):
        result, raw = self.raw_summary([], prefix=[start(), device()])
        self.assertEqual(raw["windowObservations"], 0)
        self.assertEqual(raw["semanticEvidenceStatus"], "unknown")
        self.assertEqual(result["sessions"][0]["interfaces"][0]["opens"]["directInputFreshReadAccessSucceeded"], 1)
        self.assert_no_raw_conclusions(raw)

    def test_43_raw_overwritten_and_suppressed_counters_are_not_extra_decoded_samples(self):
        witnesses = [raw_witness(1600, 1), raw_witness(3600, 5)]
        row = raw_collection(witnesses, messageReceipts=8, reportReceipts=8, queueTimestampsKnown=8,
                             semanticWitnessCandidates=8, semanticWitnessSelections=5, semanticWitnessSuppressed=3,
                             semanticWitnessClockRollbacks=1, semanticWitnessRecordsOverwritten=3, decodedWitnesses=5, completeWitnesses=5)
        _, raw = self.raw_summary([raw_window([row], end=4000)])
        self.assertEqual((raw["decodedWitnesses"], raw["completeWitnesses"]), (2, 2))
        self.assertEqual(raw["collections"][0]["loggedCounterSums"]["decodedWitnesses"], 5)
        for field, value in (("semanticWitnessCandidates", 9), ("semanticWitnessClockRollbacks", 4),
                             ("semanticWitnessRecordsOverwritten", 2), ("decodedWitnesses", 6), ("completeWitnesses", True)):
            with self.subTest(field=field):
                bad = deepcopy(row)
                bad[field] = value
                _, raw = self.raw_summary([raw_window([bad], end=4000)])
                self.assertEqual(raw["decodedWitnesses"], 0)

    def test_44_raw_global_budget_rollback_and_retained_limit(self):
        for ticks in ((1600, 1700), (1700, 1600)):
            rows = [raw_collection([raw_witness(ticks[0], 1)]), raw_collection([raw_witness(ticks[1], 2)], generation=2)]
            _, raw = self.raw_summary([raw_window(rows)])
            self.assertEqual(raw["invalidWitnessObservations"], 2)
        row = raw_collection([raw_witness(1600, 1), raw_witness(2200, 2), raw_witness(2800, 3)])
        _, raw = self.raw_summary([raw_window([row], end=3000)])
        self.assertEqual(raw["invalidWitnessObservations"], 3)
        self.assertEqual(len(raw["collections"][0]["observations"][0]["semanticWitnesses"]), 3)

    def test_45_raw_malformed_nested_shapes_and_future_version_are_retained(self):
        for collection in (None, 1, [], "bad"):
            _, raw = self.raw_summary([raw_window([collection])])
            self.assertEqual(raw["unassociatedObservations"][0]["loggedCollection"], collection)
            self.assertEqual(raw["decodedWitnesses"], 0)
        for controls in (None, [], 1, "bad"):
            row = raw_collection()
            row["semanticWitnesses"][0]["controls"] = controls
            _, raw = self.raw_summary([raw_window([row])])
            self.assertEqual(raw["invalidWitnessObservations"], 1)
        for witnesses in (None, {}, [None], [True]):
            row = raw_collection()
            row["semanticWitnesses"] = witnesses
            _, raw = self.raw_summary([raw_window([row])])
            self.assertEqual(raw["decodedWitnesses"], 0)
        row = raw_collection(semanticWitnessVersion=2)
        _, raw = self.raw_summary([raw_window([row])])
        self.assertEqual(raw["collections"][0]["observations"][0]["witnessFormatStatus"], "unsupported-or-invalid")
        self.assertEqual(raw["decodedWitnesses"], 0)
        for rows in (None, {}):
            window = raw_window()
            window["collections"] = rows
            _, raw = self.raw_summary([window])
            self.assertIn("collections-not-array", raw["windows"][0]["validationErrors"])

    def test_46_raw_duplicate_exports_keep_all_provenance_without_recount(self):
        first = self.zip({audit.OBSERVER_BASE: [start(), raw_window()]})
        second = self.zip({audit.OBSERVER_BASE: [start(), raw_window()]})
        result = audit.audit([first, second])
        raw = result["sessions"][0]["rawInputReceipts"]
        self.assertEqual(raw["completeWitnesses"], 1)
        self.assertEqual(raw["windowObservations"], 1)
        rid = raw["recordIds"][0]
        original = next(x for x in result["observerRecords"] if x["recordId"] == rid)
        self.assertEqual(len(original["provenance"]), 2)

    def test_47_raw_tuple_metadata_conflict_and_duplicate_tuple_in_window(self):
        changed = raw_collection([raw_witness(2600, 2)], usage=4)
        _, raw = self.raw_summary([raw_window(), raw_window([changed], end=3000)])
        self.assertEqual(len(raw["collections"]), 1)
        self.assertEqual(raw["invalidWitnessObservations"], 2)
        _, raw = self.raw_summary([raw_window([raw_collection(), raw_collection([raw_witness(2200, 2)])], end=2500)])
        self.assertEqual(raw["invalidWitnessObservations"], 2)



    def test_48_raw_window_can_start_before_start_record_but_sample_cannot(self):
        _, raw = self.raw_summary([raw_window(begin=900)])
        self.assertEqual(raw["completeWitnesses"], 1)
        row = raw_collection([raw_witness(950, 1)])
        _, raw = self.raw_summary([raw_window([row], begin=900)])
        self.assertEqual(raw["invalidWitnessObservations"], 1)
        self.assertEqual(raw["decodedWitnesses"], 0)

    def test_49_raw_batched_receipts_neutral_and_undecoded_changes_prove_no_activity(self):
        controls = raw_controls()
        controls["buttons"] = controls["buttonsDown"] = []
        witness = raw_witness(controls=controls)
        witness.update(batchReportCount=3, batchReportIndex=2)
        row = raw_collection([witness], changedPayloadReports=2)
        _, raw = self.raw_summary([raw_window([row])])
        self.assertEqual(raw["completeWitnesses"], 1)
        self.assertEqual(raw["collections"][0]["loggedCounterSums"]["reportReceipts"], 3)
        self.assert_no_raw_conclusions(raw)
        controls = {"state": "unknown", "decodedControlsKnown": False, "decodeComplete": False, "buttonsDown": [], "values": []}
        row = raw_collection([raw_witness(controls=controls)], changedPayloadReports=1)
        _, raw = self.raw_summary([raw_window([row])])
        self.assertEqual(raw["validUniqueWitnesses"], 1)
        self.assertEqual(raw["semanticEvidenceStatus"], "unknown")
        self.assert_no_raw_conclusions(raw)

    def test_50_raw_missing_hid_metadata_or_empty_complete_claim_is_invalid(self):
        for collection_field in ("buttonsDown", "values"):
            for field in raw_controls()[collection_field][0]:
                with self.subTest(collectionField=collection_field, missing=field):
                    row = raw_collection()
                    del row["semanticWitnesses"][0]["controls"][collection_field][0][field]
                    _, raw = self.raw_summary([raw_window([row])])
                    # Producer-specific optional annotations may be omitted.
                    if field in ("valueSource", "interpretedValue", "isSigned", "isAbsolute", "isNull", "controlValueKnown"):
                        continue
                    self.assertEqual(raw["invalidWitnessObservations"], 1)
                    self.assertEqual(raw["decodedWitnesses"], 0)
        row = raw_collection()
        row["semanticWitnesses"][0]["controls"].update(buttonsDown=[], buttons=[], values=[])
        _, raw = self.raw_summary([raw_window([row])])
        self.assertEqual(raw["invalidWitnessObservations"], 1)

    def test_51_raw_malformed_json_types_are_retained_and_do_not_crash(self):
        # Exercise the actual summary with a Cartesian set of malformed JSON field types.
        malformed = [None, False, True, -1, 0.5, "1", [], {}, [1], {"unexpected": True}, "", 1.0]
        session = {"sessionId": "fixture", "identity": {"tick": 1000}}
        cases = 0
        for target in ("window", "collection", "witness", "controls", "value"):
            template = raw_window()
            nodes = {"window": template, "collection": template["collections"][0],
                     "witness": template["collections"][0]["semanticWitnesses"][0],
                     "controls": template["collections"][0]["semanticWitnesses"][0]["controls"],
                     "value": template["collections"][0]["semanticWitnesses"][0]["controls"]["values"][0]}
            for field in nodes[target]:
                for bad in malformed:
                    with self.subTest(target=target, field=field, value=bad):
                        window = deepcopy(template)
                        current = {"window": window, "collection": window["collections"][0],
                                   "witness": window["collections"][0]["semanticWitnesses"][0],
                                   "controls": window["collections"][0]["semanticWitnesses"][0]["controls"],
                                   "value": window["collections"][0]["semanticWitnesses"][0]["controls"]["values"][0]}[target]
                        current[field] = deepcopy(bad)
                        # Envelope fields are validated by the existing JSON reader, not this summary.
                        if target == "window" and field in ("event", "observerPid", "utcEpochMs", "tick"):
                            continue
                        raw = audit.summarize_raw_input_receipts([{"recordId": "fixture-record", "record": window}], session)
                        self.assert_no_raw_conclusions(raw)
                        json.dumps(raw, allow_nan=False)
                        cases += 1
        self.assertGreater(cases, 800)

    def test_52_raw_selection_sequence_gaps_require_elapsed_budget(self):
        row = raw_collection([raw_witness(1600, 1), raw_witness(2200, 5)])
        _, raw = self.raw_summary([raw_window([row], end=2500)])
        self.assertEqual(raw["invalidWitnessObservations"], 2)
        self.assertEqual(raw["decodedWitnesses"], 0)



    def test_53_raw_complete_message_samples_must_match_aggregate_claims(self):
        for field, value in (("maxQueueDelayMs", 11), ("firstQueuedTick", 1589), ("lastQueuedTick", 1591),
                             ("firstReceiptTick", 1599), ("lastReceiptTick", 1601), ("reportReceipts", 2),
                             ("reportBytesLogged", 0), ("semanticInputDecoded", 1)):
            with self.subTest(field=field):
                _, raw = self.raw_summary([raw_window([raw_collection(**{field: value})])])
                self.assertEqual(raw["invalidWitnessObservations"], 1)
                self.assertEqual(raw["decodedWitnesses"], 0)
                w = raw["collections"][0]["observations"][0]["semanticWitnesses"][0]
                self.assertEqual(w["queueStatus"], "invalid")



    def test_54_wake_names_are_exact_and_limited_to_runtime_budget(self):
        for episode in (1, 2):
            base = f"msi-controller-visibility-wake-{episode}.log"
            for prefix in ("", "logs/", "nested\\logs\\"):
                for suffix in ("", ".1", ".2", ".prev"):
                    with self.subTest(accepted=prefix + base + suffix):
                        self.assertTrue(audit.member_matches(prefix + base + suffix, audit.OBSERVER_BASE))
        invalid = [f"msi-controller-visibility-wake-{episode}.log" for episode in (0, 3, 10, "01", "02", "-1", "1a", "1.0")]
        invalid += ["msi-controller-visibility-wake.log", "msi-controller-visibility-wake-1.LOG",
                    "MSI-controller-visibility-wake-1.log", "msi-controller-visibility-wake-1.logx",
                    "prefix-msi-controller-visibility-wake-1.log", "msi-controller-visibility-wake-1.log.bak",
                    "msi-controller-visibility-wake-1.log.previous", "msi-controller-visibility-wake-1.log.prev.1",
                    "msi-controller-visibility-wake-1.log.1.prev", "msi-controller-visibility-wake-1.log.1x",
                    "msi-controller-visibility-wake-2.log.prev.bak"]
        for name in invalid:
            with self.subTest(rejected=name):
                self.assertFalse(audit.member_matches("logs/" + name, audit.OBSERVER_BASE))
        self.assertFalse(audit.member_matches("msi-controller-visibility-wake-1.log", audit.NATIVE_BASE))
        self.assertTrue(audit.member_matches(audit.NATIVE_BASE + ".prev", audit.NATIVE_BASE))
        # Invalid filenames cannot introduce even a start row into the audit.
        result = audit.audit([self.zip({name: [start(), raw_window()] for name in invalid})])
        self.assertEqual(result["observerUniqueRows"], 0)
        self.assertEqual(result["archives"][0]["observerMembers"], [])
        self.assertEqual(result["sessions"], [])
        self.assertTrue(result["latestObserverMissing"])

    def test_55_wake_only_zip_and_rotations_load_normal_observer_evidence(self):
        for episode in (1, 2):
            for suffix in ("", ".1", ".prev"):
                name = f"logs/msi-controller-visibility-wake-{episode}.log{suffix}"
                with self.subTest(member=name):
                    result = audit.audit([self.zip({name: [start(), raw_window()]})])
                    self.assertEqual(result["archives"][0]["observerPresence"], "present")
                    self.assertFalse(result["latestObserverMissing"])
                    self.assertEqual(result["observerUniqueRows"], 2)
                    self.assertEqual(len(result["sessions"]), 1)
                    session = result["sessions"][0]
                    self.assertEqual(session["identity"]["parentCreationTime"], stamp(BASE))
                    raw = session["rawInputReceipts"]
                    self.assertEqual(raw["completeWitnesses"], 1)
                    self.assertEqual(raw["collections"][0]["sessionId"], session["sessionId"])
                    self.assertTrue(all(p["member"] == name for r in result["observerRecords"] for p in r["provenance"]))
                    self.assert_no_raw_conclusions(raw)

    def test_56_wake_main_rotations_and_exports_share_canonical_dedup(self):
        rows = [start(), raw_window()]
        first = self.zip({audit.OBSERVER_BASE: rows, "logs/msi-controller-visibility-wake-1.log": rows,
                          "logs/msi-controller-visibility-wake-1.log.prev": [rows[1]],
                          "other/msi-controller-visibility-wake-2.log.1": rows})
        second = self.zip({"msi-controller-visibility-wake-2.log.prev": rows})
        result = audit.audit([first, second])
        self.assertEqual(result["observerRawValidRows"], 9)
        self.assertEqual(result["observerUniqueRows"], 2)
        self.assertEqual(result["duplicateObserverOccurrencesRemoved"], 7)
        self.assertEqual(len(result["sessions"]), 1)
        self.assertEqual(result["sessions"][0]["rawInputReceipts"]["completeWitnesses"], 1)
        receipt = next(r for r in result["observerRecords"] if r["record"]["event"] == audit.RAW_RECEIPT_EVENT)
        self.assertEqual(len(receipt["provenance"]), 5)
        self.assertEqual({p["archiveId"] for p in receipt["provenance"]}, {0, 1})
        self.assertEqual(len({p["member"] for p in receipt["provenance"]}), 5)
        self.assertEqual(result["sessions"][0]["rawInputReceipts"]["recordIds"], [receipt["recordId"]])

    def test_57_wake_pid_reuse_is_bound_by_start_and_parent_filetime(self):
        newer_start = start(creation=BASE + 3000, when=BASE + 4000, tick=4000)
        newer_window = raw_window([raw_collection([raw_witness(4600, 1)])], begin=4000, end=5000,
                                  parentPid=20, parentCreationTime=stamp(BASE + 3000))
        stale_identity = dict(newer_window, utcEpochMs=BASE + 5100, tick=5100, parentCreationTime=stamp(BASE))
        archive = self.zip({"msi-controller-visibility-wake-1.log.prev": [start(), raw_window()],
                            "msi-controller-visibility-wake-2.log": [newer_start, newer_window, stale_identity]})
        result = audit.audit([archive])
        self.assertEqual(len(result["sessions"]), 2)
        self.assertNotEqual(result["sessions"][0]["sessionId"], result["sessions"][1]["sessionId"])
        self.assertEqual([s["identity"]["parentCreationTime"] for s in result["sessions"]], [stamp(BASE), stamp(BASE + 3000)])
        self.assertEqual([s["uniqueRows"] for s in result["sessions"]], [2, 2])
        self.assertEqual([s["rawInputReceipts"]["completeWitnesses"] for s in result["sessions"]], [1, 1])
        self.assertEqual(len(result["unassignedObserverRecords"]), 1)
        unassigned = result["unassignedObserverRecords"][0]
        self.assertEqual(unassigned["reason"], "missing-valid-start-or-time/identity")
        original = next(r for r in result["observerRecords"] if r["recordId"] == unassigned["recordId"])
        self.assertEqual(original["record"], stale_identity)
        self.assertNotIn("sessionId", original)

    def test_58_wake_missing_or_invalid_start_stays_unassigned_unknown(self):
        incomplete_start = start()
        incomplete_start.pop("parentCreationTime")
        for prefix in ([], [incomplete_start]):
            with self.subTest(invalidStart=bool(prefix)):
                rows = prefix + [device(), raw_window()]
                result = audit.audit([self.zip({"msi-controller-visibility-wake-1.log": rows})])
                self.assertEqual(result["archives"][0]["observerPresence"], "present")
                self.assertEqual(result["observerUniqueRows"], len(rows))
                self.assertEqual(result["sessions"], [])
                self.assertEqual(len(result["unassignedObserverRecords"]), len(rows))
                self.assertTrue(all(r["reason"] == "missing-valid-start-or-time/identity" for r in result["unassignedObserverRecords"]))
                self.assertTrue(all("sessionId" not in r for r in result["observerRecords"]))
                self.assertEqual(len(result["invalidStartRecordIds"]), len(prefix))
                self.assertFalse(result["freshPhysicalInputDeliveryProven"])
                self.assertFalse(result["steamConsumptionProven"])
                self.assertFalse(result["darkScreenCausationProven"])
                receipt = next(r for r in result["observerRecords"] if r["record"]["event"] == audit.RAW_RECEIPT_EVENT)
                self.assertEqual(receipt["record"], rows[-1])

    def test_59_wake_filename_does_not_supply_episode_or_success(self):
        # Identical bytes produce identical evidence even under a different wake slot.
        logged_start = dict(start(), phase="msi-evidence", wakeGeneration=99, wakeEpisode=1)
        rows = [logged_start, raw_window()]
        main = audit.audit([self.zip({audit.OBSERVER_BASE: rows})])
        wake = audit.audit([self.zip({"msi-controller-visibility-wake-2.log": rows})])
        self.assertEqual(main["sessions"], wake["sessions"])
        self.assertEqual([r["recordId"] for r in main["observerRecords"]], [r["recordId"] for r in wake["observerRecords"]])
        original_start = next(r["record"] for r in wake["observerRecords"] if r["record"]["event"] == "start")
        self.assertEqual(original_start, logged_start)
        self.assertEqual(original_start["wakeEpisode"], 1)
        self.assertTrue(wake["sessions"][0]["rightCensored"])
        self.assert_no_raw_conclusions(wake["sessions"][0]["rawInputReceipts"])
        self.assertIn("successful wake", " ".join(wake["limitations"]))

    def test_60_wake_filename_cannot_replace_missing_or_wrong_pid_start(self):
        for start_name, initial in (("msi-controller-visibility-wake-3.log", start()),
                                    ("msi-controller-visibility-wake-1.log", start(observer=11))):
            with self.subTest(startName=start_name):
                result = audit.audit([self.zip({start_name: [initial], "msi-controller-visibility-wake-2.log": [raw_window()]})])
                self.assertEqual(len(result["unassignedObserverRecords"]), 1)
                self.assertEqual(sum(s["rawInputReceipts"]["completeWitnesses"] for s in result["sessions"]), 0)
                if start_name.endswith("-3.log"):
                    self.assertEqual(result["sessions"], [])
                else:
                    self.assertEqual(result["sessions"][0]["identity"]["observerPid"], 11)
                    self.assertEqual(result["sessions"][0]["uniqueRows"], 1)

    def test_61_wake_same_parent_pid_restart_uses_start_identity_not_slot(self):
        newer_start = start(when=BASE + 4000, tick=4000)
        newer_window = raw_window([raw_collection([raw_witness(4600, 1)])], begin=4000, end=5000)
        # Filenames deliberately do not follow chronological episode order.
        archive = self.zip({"msi-controller-visibility-wake-2.log.prev": [start(), raw_window()],
                            "msi-controller-visibility-wake-1.log.1": [newer_start, newer_window]})
        result = audit.audit([archive])
        self.assertEqual(len(result["sessions"]), 2)
        self.assertEqual(result["unassignedObserverRecords"], [])
        self.assertEqual([s["identity"]["parentCreationTime"] for s in result["sessions"]], [stamp(BASE), stamp(BASE)])
        self.assertNotEqual(result["sessions"][0]["sessionId"], result["sessions"][1]["sessionId"])
        self.assertEqual([s["uniqueRows"] for s in result["sessions"]], [2, 2])
        self.assertEqual([s["rawInputReceipts"]["completeWitnesses"] for s in result["sessions"]], [1, 1])


class RecordingResult(unittest.TextTestResult):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.records = []

    def addSuccess(self, test):
        super().addSuccess(test)
        self.records.append({"test": test.id(), "status": "passed"})

    def addFailure(self, test, err):
        super().addFailure(test, err)
        self.records.append({"test": test.id(), "status": "failed", "detail": self._exc_info_to_string(err, test)})

    def addError(self, test, err):
        super().addError(test, err)
        self.records.append({"test": test.id(), "status": "error", "detail": self._exc_info_to_string(err, test)})


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True, help="selftest JSON; also emits same-stem Markdown")
    args = parser.parse_args(argv)
    output = args.output.resolve()
    if output.suffix.lower() != ".json":
        parser.error("--output must end in .json")
    output.parent.mkdir(parents=True, exist_ok=True)
    OfflineScopeTests.workspace = str(output.parent)
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(OfflineScopeTests)
    stream = io.StringIO()
    result = unittest.TextTestRunner(stream=stream, verbosity=2, resultclass=RecordingResult).run(suite)
    payload = {"schemaVersion": 1, "scope": "OFFLINE_SYNTHETIC_ZIPS_ONLY_NO_PRODUCT_OR_PROBE",
               "pythonExecutable": sys.executable, "passed": result.wasSuccessful(), "testsRun": result.testsRun,
               "passedTests": len([r for r in result.records if r["status"] == "passed"]),
               "testedSources": [{"path": str(p.resolve()), "sha256": audit.file_sha256(p)}
                                 for p in (Path(audit.__file__), Path(__file__))],
               "failures": len(result.failures), "errors": len(result.errors), "tests": result.records,
               "commandOutput": stream.getvalue()}
    output.write_text(json.dumps(payload, ensure_ascii=True, indent=2) + "\n", encoding="utf-8")
    md = ["# Observer audit 离线 negative/scope selftests", "",
          f"Python: `{sys.executable}`", f"Tests: {result.testsRun}; failures: {len(result.failures)}; errors: {len(result.errors)}", ""]
    md += [f"- **{r['status']}** `{r['test']}`" for r in result.records]
    output.with_suffix(".md").write_text("\n".join(md) + "\n", encoding="utf-8")
    print(stream.getvalue(), end="")
    print(json.dumps({"passed": result.wasSuccessful(), "testsRun": result.testsRun, "json": str(output)}, ensure_ascii=True))
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    raise SystemExit(main())
