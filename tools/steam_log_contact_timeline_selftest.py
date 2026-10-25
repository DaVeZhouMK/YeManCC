"""Own offline tests only; fixtures and receipts stay in --output-dir.

The default suite requires a registered ordinary historical export.
--native-lifecycle-only runs bounded synthetic cases without that archive.
No native compilation, application, network or process control is performed.
"""
from __future__ import annotations

import argparse
import copy
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import struct
import sys
import warnings
import zipfile
from unittest.mock import patch

sys.dont_write_bytecode = True
SPEC = importlib.util.spec_from_file_location("contact_timeline", Path(__file__).with_name("steam_log_contact_timeline.py"))
TOOL = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(TOOL)


def require(condition, message="assertion failed"):
    if not condition:
        raise AssertionError(message)


def write_results(out, checks, history):
    result = {"caseCount": len(checks), "checks": checks, "passed": sum(c["pass"] for c in checks), "failed": sum(not c["pass"] for c in checks),
              "historicalReplay": history, "offlineOnly": True,
              "sources": [{"path": str(p.resolve()), "sha256": hashlib.sha256(p.read_bytes()).hexdigest()}
                          for p in [Path(__file__), Path(TOOL.__file__)]]}
    (out/"selftest-results.json").write_text(json.dumps(result, ensure_ascii=True, indent=2)+"\n", encoding="utf-8")
    (out/"RESULT.json").write_text(json.dumps(result, ensure_ascii=True, indent=2)+"\n", encoding="utf-8")
    print(json.dumps(result, ensure_ascii=True, indent=2))
    return 1 if result["failed"] else 0


def native_lifecycle_cases(case, archive, errors, facts, original, client):
    # publish() passes the state itself; appendNativeLifecycleLog adds root
    # time/event/pid metadata. This is deliberately not a details envelope.
    row = {"event": "steam-game-link-state", "time": "2026-10-20 01:02:03.004",
           "pid": 999, "hwnd": 1234, "powerLifecycle": "awake", "powerGeneration": 5,
           "schemaVersion": 1, "implementation": "steam-running-link-v1", "readOnly": True,
           "phase": "linked", "generation": 41, "configEpoch": "3", "observedAtTick": 123456,
           "originalGame": original, "steamClient": client, "linked": True,
           "contactConnected": True, "transportConnected": True,
           "gameInputVerified": True, "continueButtonVerified": True,
           "nativeRecordDiagnostic": {"recordState": "present-original-primary",
                                      "nativeUiAppState": {"nativeAppFlags": 8196}},
           "steamUI": {"localDisplayStatus": 4}}
    text = json.dumps(row)+"\n"
    def run(name, records, limits=None):
        return TOOL.Extractor(limits).run(archive(name+".zip", [("native-lifecycle.log", records)]))

    def members():
        allowed = ["native-lifecycle.log"+suffix for suffix in ("", ".1", ".prev", ".previous")]
        rejected = ["native-detail.log", "native-lifecycleXlog", "native-lifecycle.log.2",
                    "native-lifecycle.log.01", "native-lifecycle.log.PREV", "native-lifecycle.log.bak",
                    "native-lifecycle.log.previous.extra", "native-lifecycle.log\n",
                    "NATIVE-LIFECYCLE.log", "private/native-lifecycle.log", "x-native-lifecycle.log"]
        opened = []
        original_open = zipfile.ZipFile.open
        def tracked(z, member, *a, **kw):
            opened.append(member.filename)
            return original_open(z, member, *a, **kw)
        p = archive("native-member-allowlist.zip", [(n, text) for n in allowed]+
                    [(n, "PRIVATE_NATIVE_MEMBER_MUST_NOT_OPEN") for n in rejected])
        with patch.object(zipfile.ZipFile, "open", tracked):
            d = TOOL.Extractor().run(p)
        require(d["status"] == "parsed", str(d["errors"]))
        require(opened == allowed and [m["member"] for m in d["members"]] == allowed)
        require({m["role"] for m in d["members"]} == {"native-lifecycle"})
        require(len(d["events"]) == 4 and all(e["source"]["jsonPointer"] == "" for e in d["events"]))
    case("native-lifecycle-exact-member-and-three-rotations-only", members)

    def provenance():
        newer = copy.deepcopy(row)
        newer.update(generation=42, configEpoch="4", time="2026-10-20T01:02:04+08:00")
        newer["originalGame"]["created"] = "134367300000000999"
        newer["steamClient"].update(created="134367200000000999", account=54321)
        # A details object must not replace the producer's actual root payload.
        newer["details"] = {"event": "some-other-event", "generation": 999, "originalGame": {"pid": 666}}
        d = run("native-root-shape", "\n"+text+json.dumps(newer)+"\n")
        require(d["status"] == "parsed", str(d["errors"]))
        require(len(d["events"]) == 2)
        for e, line, generation, epoch in zip(d["events"], (2, 3), (41, 42), ("3", "4")):
            require(e["event"] == "steam-game-link-state")
            require(e["source"] == {"member": "native-lifecycle.log", "line": line, "jsonPointer": ""})
            for field, value in (("generation", generation), ("configEpoch", epoch)):
                require(e[field] == {"value": value, "source": {"member": "native-lifecycle.log",
                        "line": line, "jsonPointer": "/"+field}})
            require(e["identity"]["game"][0]["pid"]["value"] == 101)
            require(e["identity"]["game"][0]["birth"]["source"]["jsonPointer"] == "/originalGame/created")
            require(e["identity"]["steam"][0]["birth"]["source"]["jsonPointer"] == "/steamClient/created")
            require(all(t["source"]["line"] == line for t in e["time"]))
            require({t["source"]["jsonPointer"] for t in e["time"]} == {"/time", "/observedAtTick"})
        require(len({e["identity"]["game"][0]["birth"]["value"] for e in d["events"]}) == 2)
        require(len({e["identity"]["steam"][0]["accountFingerprint"]["value"] for e in d["events"]}) == 2)
        require(d["ordering"]["explicitAbsoluteTimeEventIds"] == [1])
        require(d["ordering"]["unplacedEventIds"] == [0])
        f = facts(d, "nativeAppFlags")[0]
        require(f["source"] == {"member": "native-lifecycle.log", "line": 2,
                "jsonPointer": "/nativeRecordDiagnostic/nativeUiAppState/nativeAppFlags"})
        require({f["namespace"] for e in d["events"] for f in e["facts"] if f["field"] in
                 ("linked", "contactConnected", "transportConnected", "gameInputVerified", "continueButtonVerified")}
                == {"producerClaim"})
        require(all(v == "unknown" for k, v in d["assessment"].items() if k != "instancesMerged"))
        require(d["assessment"]["instancesMerged"] is False and '54321' not in json.dumps(d))
    case("native-lifecycle-root-shape-provenance-generations-clocks-and-claims", provenance)

    def spoofing():
        other = ["thread-role", "steam-other-event", "steam-game-link-state-extra", "sleep-steam-original-captured",
                 "power-broadcast", "Steam-game-link-state", "", None, 123, True]
        records = [{"event": event, "details": row} for event in other]+[{"details": row}]
        records += [{"event": "thread-role", "history": [row], "private": row}]
        with patch.object(TOOL.Extractor, "emit", side_effect=AssertionError("unrelated native event emitted")):
            d = run("native-unrelated-and-spoofed", "".join(json.dumps(v)+"\n" for v in records))
        require(not d["events"] and errors(d) == {"NO_SUPPORTED_EVENTS"}, str(d["errors"]))
        require(d["summary"]["rowsRead"] == len(records))
        require(d["summary"]["contentBytesRead"] > 0)
    case("native-lifecycle-other-events-and-details-spoofs-never-emit", spoofing)

    def private_cache():
        cache = {"implementation": "steam-overlay-popup-cache-v1", "readOnly": True, "appid": 333,
                 "sourceMatched": True, "available": True, "reason": "source-qualified-cache-read",
                 "popupCount": 1, "pidCandidates": [101], "expectedPidCandidateMatch": True,
                 "normalRaisePrerequisite": "nonempty", **dict.fromkeys(TOOL.OVERLAY_POPUP_PROOFS, False)}
        cache.update(originalGame=original, steamClient=client, history=[row], secret="PRIVATE_NATIVE_CACHE")
        value = {"event": row["event"], "contactConnected": True, "gameInputVerified": True,
                 "overlayPopupCacheDiagnostic": cache,
                 "overlayProviderCacheDiagnostic": {"implementation": "steam-overlay-provider-cache-v1",
                    "originalPid": 101, "originalGame": original, "steamClient": client,
                    "history": [row], "secret": "PRIVATE_NATIVE_PROVIDER"},
                 "private": {"originalGame": original, "history": [row], "nativeAppFlags": 4},
                 "secret": "PRIVATE_NATIVE_ROOT"}
        d = run("native-private-cache", json.dumps(value)+"\n")
        require(d["status"] == "parsed", str(d["errors"]))
        require(len(d["events"]) == 1 and d["events"][0]["identity"] == {"game": "unknown", "steam": "unknown"})
        require(not facts(d, "nativeAppFlags") and "PRIVATE_NATIVE" not in json.dumps(d))
        require(facts(d, "expectedPidCandidateMatch")[0]["value"] is True)
        require(all(v == "unknown" for k, v in d["assessment"].items() if k != "instancesMerged"))
    case("native-lifecycle-private-and-cache-never-supply-identity-or-contact-admission", private_cache)

    def budgets():
        p = archive("native-budget.zip", [("native-lifecycle.log", text+text)])
        for limits, code in [({"archiveBytes": 1}, "ARCHIVE_BYTE_LIMIT"),
                             ({"centralBytes": 1}, "ZIP_DIRECTORY_LIMIT"),
                             ({"memberBytes": len(text.encode())}, "MEMBER_BYTE_LIMIT"),
                             ({"totalBytes": len(text.encode())}, "TOTAL_BYTE_LIMIT"),
                             ({"lineBytes": len(text.encode())-1}, "LINE_LIMIT"),
                             ({"rows": 1}, "ROW_LIMIT"), ({"events": 1}, "EVENT_LIMIT"),
                             ({"nodes": 5}, "INVALID_JSON_OR_DEPTH"), ({"depth": 1}, "INVALID_JSON_OR_DEPTH"),
                             ({"scalarChars": 3}, "SCALAR_LIMIT")]:
            d = TOOL.Extractor(limits).run(p)
            require(code in errors(d) and d["status"] == "incomplete", str((limits, d["errors"])))
        d = run("native-cumulative-nodes", (json.dumps({"event": row["event"], "phase": "idle"})+"\n")*4, {"nodes": 3})
        require("NODE_OR_DEPTH_LIMIT" in errors(d))
        d = run("native-ignored-rows-still-charged", '{"event":"thread-role"}\n'+text, {"rows": 1})
        require("ROW_LIMIT" in errors(d) and not d["events"])
        p = archive("native-member-budget.zip", [("native-lifecycle.log", text), ("native-lifecycle.log.1", text)])
        require("ZIP_DIRECTORY_LIMIT" in errors(TOOL.Extractor({"members": 1}).run(p)))
    case("native-lifecycle-existing-byte-row-event-node-depth-scalar-and-directory-budgets", budgets)

    def invalid():
        for i, record in enumerate(['[]', '{"event":"steam-game-link-state","event":"thread-role"}',
                                    '{"event":"steam-game-link-state","linked":NaN}', '{"event":']):
            d = run("native-invalid-"+str(i), record+"\n")
            require(not d["events"] and d["status"] == "incomplete")
            require(errors(d) & {"UNSUPPORTED_JSON_SHAPE", "INVALID_JSON_OR_DEPTH"})
    case("native-lifecycle-nonobjects-duplicate-keys-nonfinite-and-damaged-records-rejected", invalid)

    def legacy():
        payload = {"generation": 7, "originalGame": original, "nativeAppFlags": 4}
        envelope = json.dumps({"event": row["event"], "time": "2026-10-20T01:02:03Z", "details": payload})+"\n"
        p = archive("native-legacy-members.zip", [("sleep-facts.log"+s, envelope) for s in ("", ".1", ".prev", ".previous")]+
                    [(n, {"generation": 7}) for n in TOOL.JSON_MEMBERS])
        d = TOOL.Extractor().run(p)
        require(d["status"] == "parsed" and len(d["events"]) == 7)
        require(all(e["source"]["jsonPointer"] == "/details" and e["generation"]["value"] == 7
                    for e in d["events"] if e["scope"]["role"] == "sleep-facts"))
        require(len(facts(d, "nativeAppFlags")) == 4)
    case("native-lifecycle-legacy-sleep-envelopes-and-three-json-members-retained", legacy)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", required=True)
    parser.add_argument("--historical-zip")
    parser.add_argument("--registration", help="explicit project's archive move/registration JSON")
    parser.add_argument("--native-lifecycle-only", action="store_true",
                        help="only native lifecycle admission/budget cases; no historical replay")
    args = parser.parse_args(argv)
    if not args.native_lifecycle_only and not (args.historical_zip and args.registration):
        parser.error("--historical-zip and --registration are required for the default suite")
    out = Path(args.output_dir).resolve()
    out.mkdir(parents=True, exist_ok=True)
    fixtures = out / "fixtures"
    fixtures.mkdir(exist_ok=True)
    checks = []

    def case(name, fn):
        try:
            fn()
            checks.append({"name": name, "pass": True})
        except Exception as exc:
            checks.append({"name": name, "pass": False, "error": type(exc).__name__+": "+str(exc)[:300]})

    def archive(name, members, compression=zipfile.ZIP_DEFLATED):
        path = fixtures / name
        with warnings.catch_warnings():
            warnings.simplefilter("ignore", UserWarning)
            with zipfile.ZipFile(path, "w", compression=compression) as z:
                for key, value in members:
                    if isinstance(value, dict):
                        value = json.dumps(value, indent=2)
                    z.writestr(key, value)
        return path

    def errors(result):
        return {e["code"] for e in result["errors"]}

    def facts(result, field):
        return [f for e in result["events"] for f in e["facts"] if f["field"] == field]

    original = {"pid": 101, "created": "134367300000000001", "path": "D:\\Games\\Original.exe", "windowsSession": 2}
    client = {"pid": 202, "created": "134367200000000001", "root": "D:\\Steam", "windowsSession": 2,
              "accountKnown": True, "accountPinned": True, "identityToken": "opaque-account-bound-token", "account": 12345}
    native_lifecycle_cases(case, archive, errors, facts, original, client)
    if args.native_lifecycle_only:
        return write_results(out, checks, {"skipped": True, "reason": "native-lifecycle-only; no historical archive read"})

    before = {"generation": 8, "game": original, "steamClient": client,
              "nativeRecordDiagnostic": {"recordState": "present-original-primary", "nativeUiAppState": {"nativeAppFlags": 8196}},
              "steam": {"overviews": [{"appid": 333, "localDisplayStatus": 4, "inRunningList": True}]}}
    after = copy.deepcopy(before)
    after["nativeRecordDiagnostic"]["nativeUiAppState"]["nativeAppFlags"] = 4
    after["steam"]["overviews"][0].update(localDisplayStatus=11, inRunningList=False)
    report = {"schema": 4, "generation": 8, "configEpoch": 3, "recordEpoch": 1792257563.0,
              "before": before, "after": after, "gameInput": "not-tested"}
    older = {**copy.deepcopy(report), "generation": 7, "configEpoch": 2}
    older["before"]["game"]["created"] = "134367100000000002"
    older["before"]["steamClient"]["account"] = 67890
    log = json.dumps({"time": "2026-10-18 01:20:17.731", "event": "sleep-steam-original-captured",
                      "details": {"generation": 8, **original, "steamClient": client}})+"\n"
    shape = archive("ordinary-shape.zip", [("wake-recovery-current.json", report), ("wake-recovery-last.json", older),
                    ("sleep-facts.log", log), ("steam-game-link-current.json", {"originalGame": original, "steamClient": client,
                      "steamUI": after["steam"], "configEpoch": "3", "gameInputVerified": False}),
                    ("app.log", "unrelated private log"), ("settings.json", '{"secret":"must-not-read"}')])
    base = TOOL.Extractor().run(shape)

    def ordinary():
        require(base["status"] == "parsed", str(base["errors"]))
        require(len(base["members"]) == 4)
        require(base["assessment"]["actualGameInput"] == "unknown")
        require(not base["assessment"]["instancesMerged"])
        require(all("Collected" not in m["member"] for m in base["members"]))
    case("ordinary-export-without-Collected-or-CMD", ordinary)

    def namespaces():
        require({f["value"] for f in facts(base, "nativeAppFlags")} == {8196, 4})
        require({f["value"] for f in facts(base, "localDisplayStatus")} == {4, 11})
        require({f["namespace"] for f in facts(base, "nativeAppFlags")} == {"native"})
        require({f["namespace"] for f in facts(base, "localDisplayStatus")} == {"js"})
        require({f["value"] for f in facts(base, "inRunningList")} == {True, False})
    case("raw-native-flags-and-JS-statuses-remain-independent", namespaces)

    def generations():
        roots = [e for e in base["events"] if e["event"] == "snapshot"]
        require({e["scope"]["role"]: e["generation"]["value"] for e in roots if e["scope"]["role"] in {"current", "last"}} == {"current": 8, "last": 7})
        stages = [e for e in base["events"] if e["event"] == "before"]
        require(len({e["identity"]["game"][0]["birth"]["value"] for e in stages}) == 2)
        require(len({e["identity"]["steam"][0]["accountFingerprint"]["value"] for e in stages}) == 2)
        require('12345' not in json.dumps(base))
    case("current-last-generation-birth-and-account-boundaries", generations)

    def provenance():
        raw = json.dumps(report, indent=2).splitlines()
        for fact in facts(base, "nativeAppFlags"):
            source = fact["source"]
            require('"nativeAppFlags"' in raw[source["line"]-1])
            require(source["jsonPointer"].endswith("/nativeUiAppState/nativeAppFlags"))
        row = next(e for e in base["events"] if e["event"] == "sleep-steam-original-captured")
        require(row["identity"]["game"][0]["pid"]["value"] == 101)
        require(row["generation"]["value"] == 8)
        require(row["time"][0]["source"]["line"] == 1)
        require(row["time"][0]["utc"] == "unknown")
    case("pretty-JSON-and-JSONL-member-line-pointer-provenance", provenance)

    def no_verdict():
        p = archive("record-alone.zip", [("wake-recovery-current.json", {"generation": 1,
                    "nativeRecordDiagnostic": {"recordState": "present-original-primary"},
                    "receipt": {"ok": True}, "game": {"pid": 101}, "originalWindowFocused": True,
                    "input": {"xinputMask": 1}, "gameInputVerified": True, "continueButtonVerified": True})])
        d = TOOL.Extractor().run(p)
        require(all(d["assessment"][k] == "unknown" for k in ["realSteamContactRecovery", "actualGameInput", "actualContinueButton"]))
        e = d["events"][0]
        require(e["identity"]["game"][0]["birth"]["value"] == "unknown")
        require(e["signalFactIndexes"]["nativeAppFlags"] == "unknown")
    case("record-PID-XInput-focus-action-and-producer-claim-give-no-success-verdict", no_verdict)

    def changed_instance():
        current = copy.deepcopy(report)
        current["after"]["game"]["created"] = "134367300000000999"
        d = TOOL.Extractor().run(archive("new-instance.zip", [("wake-recovery-current.json", current)]))
        require(len({e["identity"]["game"][0]["birth"]["value"] for e in d["events"] if e["identity"]["game"] != "unknown"}) == 2)
        require(d["assessment"]["instanceCorrelation"] == "unknown")
    case("new-birth-is-never-merged-with-original-instance", changed_instance)

    def clocks():
        root = next(e for e in base["events"] if e["scope"]["role"] == "current" and e["event"] == "snapshot")
        stage = next(e for e in base["events"] if e["scope"]["role"] == "current" and e["event"] == "before")
        require(root["id"] in base["ordering"]["explicitAbsoluteTimeEventIds"])
        require(stage["time"] == "unknown")
        require(stage["id"] in base["ordering"]["unplacedEventIds"])
        p = fixtures/"clock.jsonl"
        p.write_text('\n'.join(json.dumps({"event": "sleep-steam-inspected", "time": t}) for t in ["2026-10-19T02:00:00Z", "2026-10-19T01:00:00+00:00", "2026-10-19 03:00:00"]), encoding="utf-8")
        d = TOOL.Extractor().run(p)
        require(d["ordering"]["explicitAbsoluteTimeEventIds"] == [1, 0])
        require(d["ordering"]["unplacedEventIds"] == [2])
    case("only-explicit-absolute-times-sort-no-export-time-inheritance", clocks)

    def allowlist():
        original_open = zipfile.ZipFile.open
        opened = []
        def tracked(z, name, *a, **kw):
            opened.append(name.filename if isinstance(name, zipfile.ZipInfo) else name)
            return original_open(z, name, *a, **kw)
        with patch.object(zipfile.ZipFile, "open", tracked):
            d = TOOL.Extractor().run(shape)
        require(set(opened) == set(TOOL.JSON_MEMBERS)|{"sleep-facts.log"})
        require("must-not-read" not in json.dumps(d))
    case("only-identified-member-payloads-opened", allowlist)

    def limits():
        d = TOOL.Extractor({"memberBytes": 32}).run(shape)
        require("MEMBER_BYTE_LIMIT" in errors(d) and d["status"] == "incomplete")
        p = archive("total.zip", [("sleep-facts.log", log), ("sleep-facts.log.1", log)])
        d = TOOL.Extractor({"totalBytes": len(log.encode())+1}).run(p)
        require("TOTAL_BYTE_LIMIT" in errors(d))
        require(d["summary"]["contentBytesRead"] <= len(log.encode())+1)
        reader = TOOL.Extractor({"memberBytes": 8})
        require(reader.read_member(io.BytesIO(b"x"*30), 1, "sleep-facts.log") is None)
        require("ACTUAL_BYTE_LIMIT" in errors(reader.result))
        require(reader.total_bytes == 9)
        d = TOOL.Extractor({"members": 1}).run(shape)
        require("ZIP_DIRECTORY_LIMIT" in errors(d))
        d = TOOL.Extractor({"lineBytes": 10}).run(p)
        require("LINE_LIMIT" in errors(d))
    case("declared-actual-member-total-directory-and-line-bounds", limits)

    def malformed():
        bad = archive("malformed.zip", [("sleep-facts.log", log+'{"event":'), ("wake-recovery-current.json", '{"generation":1,"generation":2}')])
        d = TOOL.Extractor().run(bad)
        require("INVALID_JSON_OR_DEPTH" in errors(d) and d["status"] == "incomplete")
        require(len(d["events"]) == 1)
        require(any(e["line"] == 2 for e in d["errors"]))
        for literal in ("NaN", "1e999"):
            p = fixtures/"invalid-number.json"
            p.write_text('{"generation":'+literal+'}', encoding="utf-8")
            require("INVALID_JSON_OR_DEPTH" in errors(TOOL.Extractor().run(p)))
    case("damaged-truncated-duplicate-key-and-nonfinite-JSON", malformed)

    def crc():
        p = archive("crc.zip", [("sleep-facts.log", log)], zipfile.ZIP_STORED)
        b = bytearray(p.read_bytes())
        name_len, extra_len = struct.unpack_from("<HH", b, 26)
        b[30+name_len+extra_len] ^= 1
        p.write_bytes(b)
        d = TOOL.Extractor().run(p)
        require("CORRUPT_OR_UNSUPPORTED_MEMBER" in errors(d))
        require(not d["events"] and d["status"] == "incomplete")
        p = fixtures/"broken.zip"
        p.write_bytes(b"PK broken directory")
        require(TOOL.Extractor().run(p)["status"] == "incomplete")
    case("CRC-corruption-and-broken-archive-never-complete", crc)

    def unsafe():
        link = zipfile.ZipInfo("wake-recovery-current.json")
        link.create_system = 3
        link.external_attr = 0o120777 << 16
        for name, members, expected in [
            ("duplicate.zip", [("sleep-facts.log", log), ("sleep-facts.log", log)], "DUPLICATE_MEMBER"),
            ("traversal.zip", [("../sleep-facts.log", log)], "UNSAFE_MEMBER_NAME"),
            ("symlink.zip", [(link, report)], "ENCRYPTED_OR_NONREGULAR_MEMBER")]:
            d = TOOL.Extractor().run(archive(name, members))
            require(expected in errors(d))
        require(not (out/"sleep-facts.log").exists())
    case("duplicates-traversal-and-symlinks-with-no-extraction", unsafe)

    def truncation():
        d = TOOL.Extractor().run(archive("producer-truncated.zip", [("wake-recovery-current.json", {**report, "truncated": True})]))
        require("PRODUCER_TRUNCATED" in errors(d) and d["status"] == "incomplete")
        d = TOOL.Extractor({"events": 1}).run(shape)
        require("EVENT_LIMIT" in errors(d))
        d = TOOL.Extractor({"depth": 2}).run(shape)
        require("INVALID_JSON_OR_DEPTH" in errors(d))
    case("producer-truncation-event-and-JSON-depth-limits", truncation)

    def single_and_cli():
        p = fixtures/"standalone.json"
        p.write_text(json.dumps(report), encoding="utf-8")
        require(TOOL.Extractor().run(p)["status"] == "parsed")
        output = out/"cli-fixture-output.json"
        with patch("sys.stdout", io.StringIO()):
            require(TOOL.main([str(shape), "--output", str(output)]) == 0)
        require(json.loads(output.read_text())["status"] == "parsed")
        before_bytes = p.read_bytes()
        with patch("sys.stderr", io.StringIO()):
            try:
                TOOL.main([str(p), "--output", str(p)])
            except SystemExit as exc:
                require(exc.code == 2)
            else:
                raise AssertionError("input overwrite accepted")
        require(p.read_bytes() == before_bytes)
    case("explicit-single-JSON-JSONL-and-output-protection", single_and_cli)

    # Exercise the ZIP entry point, not emit()/collect() in isolation. Keep the
    # expected field list independent of the implementation's scoped allowlist.
    raw_fields = ("lastRecordKeepDecisionRaw", "comparisonLatchRaw",
                  "failedCheckCounterRaw", "alternatePidRaw")
    raw_values = dict(zip(raw_fields, (1, 1, 0xffffffff, 0)))
    sensitive = {"account": "fixture-private-account", "nativeAddress": "0x7fff1234abcd",
                 "gameIdExactString": "17746352246272032999",
                 "gameidString": "17746352246272032998", "secret": "fixture-private-secret"}
    predicate = {**raw_values, **sensitive, "time": "2026-10-19T03:00:00Z",
                 "actualPredicateBranch": "unknown", "actualPredicateReturn": None,
                 "nativeAppFlags": 8196, "inputVerified": True, "continueVerified": True,
                 "details": {"gameIdExactString": sensitive["gameIdExactString"],
                             "nativeAppFlags": 8196, "inputVerified": True},
                 "history": [{"gameidString": sensitive["gameidString"]}]}
    predicate_report = {"schema": 4, "generation": 19, "recordEpoch": 1792368000,
        "before": {"nativeRecordDiagnostic": {
            "recordState": "present-original-primary", "timestamp": "2026-10-19T02:00:00Z",
            "nativeUiAppState": {"timestamp": "2026-10-19T04:00:00Z", "nativeAppFlags": 4},
            "predicateInputs": predicate},
            "steam": {"overviews": [{"localDisplayStatus": 11, "inRunningList": False}]}},
        "after": {"nativeRecordDiagnostic": {"predicateInputs": {"alternatePidRaw": 0}}}}
    # Full GameID aliases must also stay private in the older global paths.
    predicate_report["gameidString"] = sensitive["gameidString"]
    predicate_report["before"]["nativeRecordDiagnostic"].update(sensitive)
    predicate_last = {"schema": 4, "generation": 18, "recordEpoch": 1792367000,
        "before": {"nativeRecordDiagnostic": {"observedAtTick": 444, "predicateInputs": {
            **raw_values, "failedCheckCounterRaw": 7}}}}
    predicate_rows = [
        {"event": "unrelated-private-log", "time": "2026-10-19T10:00:00Z"},
        {"event": "sleep-steam-native-record-inspected", "time": "2026-10-19T11:00:00Z",
         "details": {"timestamp": "2026-10-19T12:00:00Z", "nativeRecordDiagnostic": {
             "timestamp": "2026-10-19T13:00:00Z", "predicateInputs": {
                 **raw_values, "failedCheckCounterRaw": 8}},
             "input": {"time": "2026-10-19T15:00:00Z", "xinputMask": 1}}},
        {"event": "sleep-steam-native-record-inspected", "time": "2026-10-19T16:00:00Z",
         "details": {"nativeRecordDiagnostic": {"predicateInputs": raw_values}}}]
    predicate_texts = {"wake-recovery-current.json": json.dumps(predicate_report, indent=2),
        "wake-recovery-last.json": json.dumps(predicate_last, indent=2),
        "sleep-facts.log": "\n".join(json.dumps(row) for row in predicate_rows)+"\n",
        "steam-game-link-current.json": json.dumps({"originalGame": original, "steamClient": client})}
    predicate_zip = archive("ordinary-predicate-inputs.zip", list(predicate_texts.items()))
    predicate_result = TOOL.Extractor().run(predicate_zip)
    (out/"predicate-export-timeline.json").write_text(
        json.dumps(predicate_result, ensure_ascii=True, indent=2)+"\n", encoding="utf-8")

    def raw_facts(result):
        return [f for e in result["events"] for f in e["facts"] if f["field"] in raw_fields]

    def raw_groups_and_values():
        d = predicate_result
        require(d["status"] == "parsed", str(d["errors"]))
        require({m["role"] for m in d["members"]} == {"current", "last", "sleep-facts", "game-link-current"})
        require(len(raw_facts(d)) == 17)
        require({f["namespace"] for f in raw_facts(d)} == {"recordPredicateRaw"})
        current = [f for f in raw_facts(d) if f["source"]["member"] == "wake-recovery-current.json"
                   and f["source"]["jsonPointer"].startswith("/before/")]
        require({f["field"]: f["value"] for f in current} == raw_values)
        require(all(type(f["value"]) is int and f["availability"] == "present" for f in raw_facts(d)))
        require({f["value"] for f in facts(d, "failedCheckCounterRaw")} == {7, 8, 0xffffffff})
        require({f["value"] for f in facts(d, "alternatePidRaw")} == {0})
    case("predicate-export-ZIP-four-raw-fields-counter-MAX-and-alternate-zero", raw_groups_and_values)

    def predicate_null_missing():
        nulls = {key: None for key in raw_fields}
        data = {"before": {"nativeRecordDiagnostic": {"predicateInputs": nulls}},
                "after": {"nativeRecordDiagnostic": {"predicateInputs": {}}},
                "history": [{"nativeRecordDiagnostic": {}},
                            {"nativeRecordDiagnostic": {"predicateInputs": None}}, {}]}
        d = TOOL.Extractor().run(archive("predicate-null-missing.zip", [("wake-recovery-current.json", data)]))
        require(d["status"] == "parsed", str(d["errors"]))
        ff = raw_facts(d)
        require(len(ff) == 4, "missing raw fields must not be synthesized")
        require({f["field"] for f in ff} == set(raw_fields))
        require(all(f["value"] == "unknown" and f["availability"] == "null" for f in ff))
        require(all(f["source"]["jsonPointer"] == "/before/nativeRecordDiagnostic/predicateInputs/"+f["field"] for f in ff))
        require(all(f["source"]["line"] > 1 and f["timeContext"] == "unknown" for f in ff))
        require(not [f for e in d["events"] if e["event"] != "before" for f in e["facts"]])
    case("predicate-explicit-null-provenance-versus-missing-no-zero-fill", predicate_null_missing)

    def predicate_privacy():
        encoded = json.dumps(predicate_result)
        require(all(value not in encoded for value in sensitive.values()))
        require(not facts(predicate_result, "gameIdExactString") and not facts(predicate_result, "gameidString"))
        require({f["value"] for f in facts(predicate_result, "nativeAppFlags")} == {4})
        require(not facts(predicate_result, "inputVerified") and not facts(predicate_result, "continueVerified"))
        require(not facts(predicate_result, "actualPredicateBranch") and not facts(predicate_result, "actualPredicateReturn"))
        require(all(f["field"] in raw_fields for e in predicate_result["events"] for f in e["facts"]
                    if "/predicateInputs/" in f["source"]["jsonPointer"]))
    case("predicate-closed-allowlist-blocks-secrets-GameID-address-and-nested-known-fields", predicate_privacy)

    def predicate_scopes():
        raw = {"lastRecordKeepDecisionRaw": 1}
        data = {"predicateInputs": raw, "lastRecordKeepDecisionRaw": 1,
            "nativeRecordDiagnostic": {**raw, "unknown": {"predicateInputs": raw}},
            "details": {"predicateInputs": raw},
            "before": {"nativeRecordDiagnostic": {"predicateInputs": raw},
                       "predicateInputs": raw, "nativeUiAppState": {"predicateInputs": raw},
                       "unknown~wrapper/private": {"nativeRecordDiagnostic": {"predicateInputs": raw}}},
            "after": {"details": {"nativeRecordDiagnostic": {"predicateInputs": raw}}},
            "history": [{"nativeRecordDiagnostic": {"predicateInputs": raw}},
                        {"predicateInputs": {"nativeRecordDiagnostic": {"predicateInputs": raw}}},
                        {"nativeRecordDiagnostic": [{"predicateInputs": raw}]}]}
        d = TOOL.Extractor().run(archive("predicate-context-scopes.zip", [("wake-recovery-current.json", data)]))
        require(d["status"] == "parsed", str(d["errors"]))
        require([f["source"]["jsonPointer"] for f in raw_facts(d)] == [
            "/before/nativeRecordDiagnostic/predicateInputs/lastRecordKeepDecisionRaw",
            "/after/details/nativeRecordDiagnostic/predicateInputs/lastRecordKeepDecisionRaw",
            "/history/0/nativeRecordDiagnostic/predicateInputs/lastRecordKeepDecisionRaw"])
    case("predicate-only-direct-known-parent-no-global-name-or-unknown-wrapper-matching", predicate_scopes)

    def predicate_provenance():
        for fact in raw_facts(predicate_result):
            source = fact["source"]
            text = predicate_texts[source["member"]]
            row = text.splitlines()[source["line"]-1]
            require('"'+fact["field"]+'"' in row, str(source))
            value = json.loads(row) if source["member"] == "sleep-facts.log" else json.loads(text)
            for part in source["jsonPointer"].split("/")[1:]:
                key = part.replace("~1", "/").replace("~0", "~")
                value = value[int(key)] if isinstance(value, list) else value[key]
            require(type(value) is int and value == fact["value"], str(source))
        log_facts = [f for f in raw_facts(predicate_result) if f["source"]["member"] == "sleep-facts.log"]
        require({f["source"]["line"] for f in log_facts} == {2, 3})
        require(all(f["source"]["jsonPointer"].startswith("/details/nativeRecordDiagnostic/predicateInputs/") for f in log_facts))
    case("predicate-pretty-JSON-and-nested-JSONL-exact-pointer-and-original-line", predicate_provenance)

    def predicate_clocks():
        for fact in raw_facts(predicate_result):
            source = fact["source"]
            context = fact["timeContext"]
            if source["member"] == "wake-recovery-current.json":
                if source["jsonPointer"].startswith("/after/"):
                    require(context == "unknown", "export clock inherited into after")
                    continue
                expected = ("time", "2026-10-19T03:00:00Z", "/before/nativeRecordDiagnostic/predicateInputs/time")
            elif source["member"] == "wake-recovery-last.json":
                expected = ("observedAtTick", 444, "/before/nativeRecordDiagnostic/observedAtTick")
            elif source["line"] == 3:
                require(context == "unknown", "envelope clock attached to untimed raw field")
                continue
            else:
                expected = ("timestamp", "2026-10-19T13:00:00Z", "/details/nativeRecordDiagnostic/timestamp")
            require(len(context) == 1 and (context[0]["field"], context[0]["value"], context[0]["source"]["jsonPointer"]) == expected)
            clock_source = context[0]["source"]
            require(clock_source["member"] == source["member"])
            clock_row = predicate_texts[clock_source["member"]].splitlines()[clock_source["line"]-1]
            require('"'+expected[0]+'"' in clock_row)
            if source["member"] == "sleep-facts.log":
                require(clock_source["line"] == source["line"])
            if expected[0] == "observedAtTick":
                require(context[0]["utc"] == "unknown")
        before_event = next(e for e in predicate_result["events"] if e["event"] == "before" and e["scope"]["role"] == "current")
        require(before_event["time"] == "unknown")
        require(before_event["id"] in predicate_result["ordering"]["unplacedEventIds"])
        log_event = next(e for e in predicate_result["events"] if e["source"]["member"] == "sleep-facts.log" and e["source"]["line"] == 2)
        require({t["value"] for t in log_event["time"]} == {"2026-10-19T11:00:00Z", "2026-10-19T12:00:00Z"})
        native = facts(predicate_result, "nativeAppFlags")[0]
        require(native["timeContext"][0]["value"] == "2026-10-19T04:00:00Z")
        require(native["timeContext"][0]["source"]["jsonPointer"] == "/before/nativeRecordDiagnostic/nativeUiAppState/timestamp")
    case("predicate-clocks-keep-own-source-not-export-envelope-or-sibling-time", predicate_clocks)

    def predicate_no_verdict():
        require(predicate_result["assessment"] == base["assessment"])
        require({f["value"] for f in facts(predicate_result, "nativeAppFlags")} == {4})
        require({f["value"] for f in facts(predicate_result, "localDisplayStatus")} == {11})
        require({f["value"] for f in facts(predicate_result, "inRunningList")} == {False})
        require(not facts(predicate_result, "runningBitSet"))
        require(not any(f["namespace"] in {"producerClaim", "actionReceipt"} for f in raw_facts(predicate_result)))
        require(all(set(e["signalFactIndexes"]) == {"nativeAppFlags", "recordState", "localDisplayStatus", "inRunningList"}
                    for e in predicate_result["events"]))
    case("predicate-retain-one-does-not-imply-Running-input-Continue-or-success", predicate_no_verdict)

    def predicate_raw_types():
        invalid = [True, False, 1.5, "17746352246272032997", "", {}, [], -1, 0x100000000]
        history_rows = [{"nativeRecordDiagnostic": {"predicateInputs": dict.fromkeys(raw_fields, value)}} for value in invalid]
        history_rows.append({"nativeRecordDiagnostic": {"predicateInputs": dict(zip(raw_fields, (256, 256, 0x100000000, 0x100000000)))}})
        limits = dict(zip(raw_fields, (255, 255, 0xffffffff, 0xffffffff)))
        data = {"before": {"nativeRecordDiagnostic": {"predicateInputs": limits}}, "history": history_rows}
        d = TOOL.Extractor().run(archive("predicate-raw-types.zip", [("wake-recovery-current.json", data)]))
        require(d["status"] == "parsed", str(d["errors"]))
        ff = raw_facts(d)
        require(len(ff) == 44)
        require({f["field"]: f["value"] for f in ff if f["source"]["jsonPointer"].startswith("/before/")} == limits)
        rejected = [f for f in ff if not f["source"]["jsonPointer"].startswith("/before/")]
        require(all(f["value"] == "unknown" and f["availability"] == "unsupported-type-or-range" for f in rejected))
        require("17746352246272032997" not in json.dumps(d))
    case("predicate-byte-and-uint32-raw-only-no-bool-string-float-object-or-overflow-coercion", predicate_raw_types)

    def predicate_container_types():
        values = [None, [], [raw_values], {"details": raw_values}, {"before": raw_values},
                  {"nativeRecordDiagnostic": {"predicateInputs": raw_values}}, "private-raw-container", 1]
        d = TOOL.Extractor().run(archive("predicate-container-types.zip", [("wake-recovery-current.json", {
            "history": [{"nativeRecordDiagnostic": {"predicateInputs": value}} for value in values]})]))
        require(d["status"] == "parsed", str(d["errors"]))
        require(not raw_facts(d))
        require("private-raw-container" not in json.dumps(d))
    case("predicate-container-is-direct-object-no-list-or-recursive-subcontainer-expansion", predicate_container_types)

    # The native closed summary has no producer rows, raw account/address or
    # identity edge. All fixtures still enter through ordinary ZIP/log parsing.
    popup_key = "overlayPopupCacheDiagnostic"
    popup_fields = ("implementation", "readOnly", "appid", "sourceMatched", "available", "reason",
                    "popupCount", "pidCandidates", "expectedPidCandidateMatch", "normalRaisePrerequisite",
                    "steamContactVerified", "actualInputVerified", "actualContinueVerified",
                    "recoveryAdmission", "pidMatchIsIdentityProof")
    popup_proofs = popup_fields[-5:]

    def popup(pids=(), known=True, reason=None, source=True):
        return {"implementation": "steam-overlay-popup-cache-v1", "readOnly": True,
                "appid": 333, "sourceMatched": source, "available": known,
                "reason": reason or ("source-qualified-cache-read" if known else "diagnostic-unavailable"),
                "popupCount": len(pids) if known else None,
                "pidCandidates": list(pids) if known else None,
                "expectedPidCandidateMatch": False if known else None,
                "normalRaisePrerequisite": ("empty" if not pids else "nonempty") if known else "unknown",
                **dict.fromkeys(popup_proofs, False)}

    def popup_facts(data):
        return [f for e in data["events"] for f in e["facts"] if f["namespace"] == popup_key]

    def popup_at(data, prefix):
        return {f["field"]: f for f in popup_facts(data) if f["source"]["jsonPointer"].startswith(prefix)}

    popup_texts = {
        "wake-recovery-current.json": json.dumps({
            "before": {"time": "2026-10-19T11:00:00Z", popup_key: popup()},
            "after": {popup_key: popup((0, 101))}}, indent=2),
        "wake-recovery-last.json": json.dumps({"before": {popup_key: popup(known=False)}}, indent=2),
        "sleep-facts.log": json.dumps({"event": "unrelated-private-log"})+"\n"+json.dumps({
            "event": "steam-game-link-state", "time": "2026-10-19T12:00:00Z",
            "details": {"time": "2026-10-19T12:01:00Z", popup_key: popup(known=False,
                reason="getter-source-unverified", source=False)}})+"\n",
        "steam-game-link-current.json": json.dumps({"originalGame": original, "steamClient": client,
                                                     popup_key: popup((101,))}, indent=2)}
    popup_zip = archive("ordinary-popup-summary.zip", list(popup_texts.items()))
    popup_result = TOOL.Extractor().run(popup_zip)
    (out/"popup-export-timeline.json").write_text(json.dumps(popup_result, ensure_ascii=True, indent=2)+"\n", encoding="utf-8")

    def popup_counts():
        require(popup_result["status"] == "parsed", str(popup_result["errors"]))
        current = [e for e in popup_result["events"] if e["source"]["member"] == "wake-recovery-current.json"]
        empty = {f["field"]: f for e in current for f in e["facts"] if f["source"]["jsonPointer"].startswith("/before/"+popup_key+"/")}
        nonempty = {f["field"]: f for e in current for f in e["facts"] if f["source"]["jsonPointer"].startswith("/after/"+popup_key+"/")}
        require(empty["popupCount"]["value"] == 0 and empty["popupCount"]["availability"] == "present")
        require(empty["pidCandidates"]["value"] == [] and empty["normalRaisePrerequisite"]["value"] == "empty")
        require(nonempty["popupCount"]["value"] == 2 and nonempty["pidCandidates"]["value"] == [0, 101])
        require(nonempty["normalRaisePrerequisite"]["value"] == "nonempty")
        require({f["field"] for f in popup_facts(popup_result)} == set(popup_fields))
        d = TOOL.Extractor().run(archive("popup-64.zip", [("wake-recovery-current.json", {popup_key: popup(range(64))})]))
        ff = popup_at(d, "/"+popup_key+"/")
        require(ff["popupCount"]["value"] == 64 and ff["pidCandidates"]["value"] == list(range(64)))
    case("popup-zero-nonempty-zero-PID-and-64-candidates-remain-cache-only", popup_counts)

    def popup_unknown():
        null = popup(known=False)
        missing = copy.deepcopy(null); missing.pop("popupCount")
        d = TOOL.Extractor().run(archive("popup-null-missing.zip", [("wake-recovery-current.json", {
            "before": {popup_key: null}, "after": {popup_key: missing}})]))
        ff = popup_at(d, "/before/"+popup_key+"/")
        require(ff["available"]["value"] is False)
        for field in ("popupCount", "pidCandidates", "expectedPidCandidateMatch"):
            require(ff[field]["value"] == "unknown" and ff[field]["availability"] == "null")
        require(ff["normalRaisePrerequisite"]["value"] == "unknown")
        require("popupCount" not in popup_at(d, "/after/"+popup_key+"/"))
        require(popup_at(d, "/after/"+popup_key+"/")["available"]["value"] == "unknown")
    case("popup-explicit-null-and-missing-stay-unknown-never-zero-filled", popup_unknown)

    def popup_numeric_types():
        invalid = [True, False, -1, 1.0, 0.5, "101", None, [], {}, 0x100000000]
        rows = []
        for field in ("appid", "popupCount"):
            for value in invalid + ([0] if field == "appid" else [65]):
                node = popup((101,)); node[field] = value; rows.append({popup_key: node})
        for value in invalid:
            node = popup((101,)); node["pidCandidates"] = [value]; rows.append({popup_key: node})
        for field in ("available", "sourceMatched", "readOnly"):
            for value in (0, 1, "true", None):
                node = popup((101,)); node[field] = value; rows.append({popup_key: node})
        d = TOOL.Extractor().run(archive("popup-invalid-scalars.zip", [("wake-recovery-current.json", {"history": rows})]))
        require(d["status"] == "parsed", str(d["errors"]))
        for index in range(len(rows)):
            ff = popup_at(d, "/history/"+str(index)+"/"+popup_key+"/")
            require(ff["available"]["value"] == "unknown", str(index))
            require(ff["popupCount"]["value"] == "unknown" and ff["pidCandidates"]["value"] == "unknown", str(index))
            require(ff["normalRaisePrerequisite"]["value"] == "unknown", str(index))
        node = popup((0, 0xffffffff)); node["appid"] = 0xffffffff
        d = TOOL.Extractor().run(archive("popup-uint32-max.zip", [("wake-recovery-current.json", {popup_key: node})]))
        ff = popup_at(d, "/"+popup_key+"/")
        require(ff["appid"]["value"] == 0xffffffff and ff["pidCandidates"]["value"] == [0, 0xffffffff])
    case("popup-appid-count-pid-and-bool-types-have-no-coercion-or-overflow", popup_numeric_types)

    def popup_array_constraints():
        nodes = [popup((101, 101)), popup((0, 0)), popup(range(65))]
        node = popup((101,)); node["popupCount"] = 0; nodes.append(node)
        node = popup(range(65)); node["popupCount"] = 64; nodes.append(node)
        for value in (None, {}, "private-popup-array", [True], [{"pid": 101}], [101.0]):
            node = popup((101,)); node["pidCandidates"] = value; nodes.append(node)
        node = popup(); node["expectedPidCandidateMatch"] = True; nodes.append(node)
        node = popup((0,)); node["expectedPidCandidateMatch"] = True; nodes.append(node)
        node = popup((101,)); node["expectedPidCandidateMatch"] = 1; nodes.append(node)
        node = popup((101,)); node["normalRaisePrerequisite"] = "empty"; nodes.append(node)
        d = TOOL.Extractor().run(archive("popup-array-constraints.zip", [("wake-recovery-current.json", {
            "history": [{popup_key: node} for node in nodes]})]))
        for index in range(len(nodes)):
            ff = popup_at(d, "/history/"+str(index)+"/"+popup_key+"/")
            require(ff["available"]["value"] == "unknown" and ff["pidCandidates"]["value"] == "unknown", str(index))
        require("private-popup-array" not in json.dumps(d))
    case("popup-duplicate-length-budget-and-candidate-prerequisite-consistency", popup_array_constraints)

    def popup_scope():
        good = popup((101,))
        producer = {"implementation": "steam-overlay-popup-cache-v1", "readOnly": True, "sourceMatched": True,
                    "rows": [{"appid": 333, "available": True, "popupCount": 1, "pidCandidates": [101]}]}
        hybrids = {**good, "rows": producer["rows"]}
        data = {popup_key: good, "before": {"details": {popup_key: good}}, "history": [
            {popup_key: good}, {popup_key: [good]}, {popup_key: producer}, {popup_key: hybrids},
            {popup_key: {"implementation": "wrong-version", "details": {popup_key: good}}}],
            "steamUI": {popup_key: producer}, "unknown-wrapper": {popup_key: good},
            "predicateInputs": {popup_key: good}, "nativeRecordDiagnostic": {"predicateInputs": {popup_key: good}},
            "input": {popup_key: [good]}, "pidCandidates": [good]}
        d = TOOL.Extractor().run(archive("popup-container-scope.zip", [("wake-recovery-current.json", data)]))
        actual = {f["source"]["jsonPointer"] for f in popup_facts(d) if f["field"] == "popupCount"}
        require(actual == {"/"+popup_key+"/popupCount", "/before/details/"+popup_key+"/popupCount", "/history/0/"+popup_key+"/popupCount"}, str(actual))
        require(popup_key not in TOOL.CONTAINERS and "popupCount" not in TOOL.FIELD_GROUP and "pidCandidates" not in TOOL.FIELD_GROUP)
    case("popup-only-direct-known-container-leaf-not-producer-rows-or-global-field", popup_scope)

    def popup_privacy():
        secrets = {"account": "popup-private-account", "address": "0x7fff89abcd01", "gameIdExactString": "17746352246272032991",
                   "raw": "popup-private-extra"}
        node = {**popup((101,)), **secrets, "nativeAppFlags": 8196, "inputVerified": True, "continueVerified": True,
                "originalGame": {"pid": 999, "created": "private-birth"},
                "details": {"nativeAppFlags": 8196, "secret": "popup-private-nested"},
                "history": [{"time": "2026-10-19T22:00:00Z", "inputVerified": True}],
                "time": "2026-10-19T23:00:00Z"}
        d = TOOL.Extractor().run(archive("popup-private.zip", [("wake-recovery-current.json", {
            "before": {"time": "2026-10-19T10:00:00Z", popup_key: node, "nativeUiAppState": {"nativeAppFlags": 4}}})]))
        encoded = json.dumps(d)
        require(all(v not in encoded for v in secrets.values()) and "private-birth" not in encoded and "popup-private-nested" not in encoded)
        require({f["field"] for f in popup_facts(d)} == set(popup_fields))
        require({f["value"] for f in facts(d, "nativeAppFlags")} == {4})
        require(not facts(d, "inputVerified") and not facts(d, "continueVerified"))
        require(all(f["timeContext"][0]["value"] == "2026-10-19T10:00:00Z" for f in popup_facts(d)))
    case("popup-closed-leaf-never-copies-private-extra-nested-identity-or-clocks", popup_privacy)

    def popup_proof_flags():
        baseline = {"originalGame": original, "steamClient": client, "steamUI": {"overviews": [{"appid": 333, "localDisplayStatus": 11, "inRunningList": False}]}}
        base = TOOL.Extractor().run(archive("popup-no-proof-base.zip", [("steam-game-link-current.json", baseline)]))
        for flag in popup_proofs:
            node = popup((101,)); node["expectedPidCandidateMatch"] = True
            for value in (True, 0, 1, None):
                forged = {**node, flag: value}
                d = TOOL.Extractor().run(archive("popup-proof-"+flag+"-"+str(value)+".zip", [("steam-game-link-current.json", {**baseline, popup_key: forged})]))
                ff = popup_at(d, "/"+popup_key+"/")
                require(ff[flag]["value"] == "unknown" and ff["available"]["value"] == "unknown")
                require(d["assessment"] == base["assessment"] and d["events"][0]["identity"] == base["events"][0]["identity"])
        node = popup((101,)); node["expectedPidCandidateMatch"] = True
        d = TOOL.Extractor().run(archive("popup-true-candidate-not-proof.zip", [("steam-game-link-current.json", {**baseline, popup_key: node})]))
        require(popup_at(d, "/"+popup_key+"/")["expectedPidCandidateMatch"]["value"] is True)
        require(d["assessment"] == base["assessment"] and d["assessment"]["instanceCorrelation"] == "unknown")
        require(d["events"][0]["identity"] == base["events"][0]["identity"])
    case("popup-candidate-match-and-forged-proof-flags-never-upgrade-any-assessment", popup_proof_flags)

    def popup_provenance():
        for fact in popup_facts(popup_result):
            source = fact["source"]; text = popup_texts[source["member"]]
            require('"'+fact["field"]+'"' in text.splitlines()[source["line"]-1], str(source))
            value = json.loads(text.splitlines()[source["line"]-1]) if source["member"] == "sleep-facts.log" else json.loads(text)
            for part in source["jsonPointer"].split("/")[1:]:
                key = part.replace("~1", "~").replace("~0", "/")
                value = value[int(key)] if isinstance(value, list) else value[key]
            if fact["availability"] == "present":
                require(value == fact["value"] and type(value) is type(fact["value"]), str(source))
            else:
                require(value is None and fact["availability"] == "null", str(source))
        ff = [f for f in popup_facts(popup_result) if f["source"]["member"] == "sleep-facts.log"]
        require({f["source"]["line"] for f in ff} == {2})
        require(all(f["source"]["jsonPointer"].startswith("/details/"+popup_key+"/") for f in ff))
    case("popup-pretty-JSON-and-JSONL-original-line-and-exact-field-pointer", popup_provenance)

    def popup_reasons():
        labels = ["no-target", "window-store-unavailable", "map-unavailable", "map-budget", "getter-source-unverified",
                  "target-entry-unknown", "cache-changed", "cache-read-unavailable", "diagnostic-unavailable"]
        nodes = [popup(known=False, reason=label, source=label != "getter-source-unverified") for label in labels]
        bad = [popup((101,), reason="popup-private-reason"), popup((101,), reason="cache-changed"),
               popup((101,), source=False), popup(known=False, reason="source-qualified-cache-read")]
        forged_zero = popup(known=False, reason="no-target"); forged_zero.update(popupCount=0, pidCandidates=[]); bad.append(forged_zero)
        d = TOOL.Extractor().run(archive("popup-reason-consistency.zip", [("wake-recovery-current.json", {"history": [{popup_key: v} for v in nodes+bad]})]))
        for index, label in enumerate(labels):
            ff = popup_at(d, "/history/"+str(index)+"/"+popup_key+"/")
            require(ff["available"]["value"] is False and ff["reason"]["value"] == label)
            require(ff["popupCount"]["availability"] == "null" and ff["pidCandidates"]["availability"] == "null")
        for index in range(len(nodes), len(nodes+bad)):
            ff = popup_at(d, "/history/"+str(index)+"/"+popup_key+"/")
            require(ff["available"]["value"] == "unknown" and ff["popupCount"]["value"] == "unknown")
        require("popup-private-reason" not in json.dumps(d))
    case("popup-known-reason-source-availability-count-consistency-is-closed", popup_reasons)

    def popup_old_output():
        data = {"before": {"originalGame": original, "steamClient": client, "nativeRecordDiagnostic": {
            "predicateInputs": raw_values, "nativeUiAppState": {"nativeAppFlags": 4}},
            "steamUI": {"overviews": [{"appid": 333, "localDisplayStatus": 11, "inRunningList": False}]}}}
        base = TOOL.Extractor().run(archive("popup-compat-base.zip", [("wake-recovery-current.json", data)]))
        changed = copy.deepcopy(data); changed["before"][popup_key] = popup((0, 101))
        d = TOOL.Extractor().run(archive("popup-compat-added.zip", [("wake-recovery-current.json", changed)]))
        old_events = copy.deepcopy(d["events"])
        for event in old_events:
            event["facts"] = [f for f in event["facts"] if f["namespace"] != popup_key]
        require(old_events == base["events"], "old events/facts/identity/clocks/indexes changed")
        require(raw_facts(d) == raw_facts(base))
        require(d["assessment"] == base["assessment"] and d["missingEvidence"] == base["missingEvidence"] and d["ordering"] == base["ordering"])
    case("popup-addition-preserves-old-predicate-facts-identities-and-all-assessments", popup_old_output)

    def popup_limits():
        line = json.dumps({"event": "steam-game-link-state", "details": {popup_key: popup(range(64))}})+"\n"
        z = archive("popup-limits.zip", [("sleep-facts.log", line+line)])
        require("LINE_LIMIT" in errors(TOOL.Extractor({"lineBytes": 32}).run(z)))
        require("ROW_LIMIT" in errors(TOOL.Extractor({"rows": 1}).run(z)))
        require("MEMBER_BYTE_LIMIT" in errors(TOOL.Extractor({"memberBytes": 32}).run(z)))
        require("TOTAL_BYTE_LIMIT" in errors(TOOL.Extractor({"totalBytes": 32}).run(z)))
        d = TOOL.Extractor({"nodes": 20}).run(z)
        require("INVALID_JSON_OR_DEPTH" in errors(d) or "NODE_OR_DEPTH_LIMIT" in errors(d))
        require(not popup_facts(d))
        d = TOOL.Extractor({"scalarChars": 8}).run(z)
        require("SCALAR_LIMIT" in errors(d))
        require(all(f["value"] == "unknown" for f in popup_facts(d) if f["field"] in ("available", "popupCount", "pidCandidates", "normalRaisePrerequisite")))
    case("popup-summary-obeys-existing-global-byte-row-node-and-scalar-budgets", popup_limits)

    provider_key = "overlayProviderCacheDiagnostic"
    provider_game = str((333 << 32) | 0x02000000)
    def provider(present=True):
        def tagged(kind, value):
            return {"rawType": kind, "value": value}
        fields = {"m_unPID": tagged("number", 101), "m_nBrowserID": tagged("number", 7),
            "m_eBrowserType": tagged("number", 1), "m_unAppID": tagged("number", 333),
            "m_gameID": tagged("string", provider_game), "m_eUIMode": tagged("number", 4)}
        return {"implementation": "steam-overlay-provider-cache-v1", "readOnly": True, "sourceMatched": True,
            "sourcePathQualified": True, "available": True, "reason": "source-qualified-cache-read" if present else "no-target",
            "appid": 333, "originalPid": 101, "fullGameIdExactString": provider_game,
            "mapEntryCount": 1 if present else 0, "targetMembership": "present-in-two-cache-reads" if present else "absent-in-two-cache-reads",
            "samePidOtherGameIdCount": 0, "targetFieldsTagged": fields if present else None,
            "snapshotAtomic": False, "originalProviderObjectVerified": False, "steamContactVerified": False,
            "actualInputVerified": False, "actualContinueVerified": False, "recoveryAdmission": False,
            "pidMatchIsIdentityProof": False, "originalIdentityBracketMatched": True}
    def provider_run(value, name="provider", outer=None, limits=None):
        data = {"event": "steam-game-link-state", "details": {provider_key: value}} if outer is None else outer
        return TOOL.Extractor(limits).run(archive(name+".zip", [("steam-game-link-current.json", data)]))
    def provider_facts(result):
        return [f for event in result["events"] for f in event["facts"] if f.get("namespace") == provider_key]
    def provider_field(result, field):
        return next(f for f in provider_facts(result) if f["field"] == field)

    def provider_present():
        d = provider_run(provider())
        require(provider_field(d, "targetMembership")["value"] == "present-in-two-cache-reads")
        f = provider_field(d, "targetFieldsTagged.m_gameID.value")
        require(f["value"] == provider_game and f["source"]["member"] == "steam-game-link-current.json" and f["source"]["line"] == 36)
        require(f["source"]["jsonPointer"] == "/details/overlayProviderCacheDiagnostic/targetFieldsTagged/m_gameID/value")
        require(all(v == "unknown" for k, v in d["assessment"].items() if k != "instancesMerged"))
    case("provider-original-typed-membership-and-provenance-without-recovery-verdict", provider_present)

    def provider_zero_unknown():
        absent = provider_run(provider(False), "provider-zero")
        require(provider_field(absent, "mapEntryCount")["value"] == 0)
        require(provider_field(absent, "targetMembership")["value"] == "absent-in-two-cache-reads")
        value = provider(False); value.update(available=False, reason="map-brand-unqualified", sourcePathQualified=False,
            targetMembership="unknown", mapEntryCount=None, samePidOtherGameIdCount=None)
        unknown = provider_run(value, "provider-unknown")
        require(provider_field(unknown, "mapEntryCount")["value"] == "unknown" and provider_field(unknown, "mapEntryCount")["availability"] == "null")
        del value["mapEntryCount"]
        require(not [f for f in provider_facts(provider_run(value, "provider-missing")) if f["field"] == "mapEntryCount"])
    case("provider-two-read-absence-zero-versus-null-and-missing", provider_zero_unknown)

    def provider_identity_rejects():
        changes = [{"originalPid": True}, {"appid": 0}, {"appid": 333.0}, {"fullGameIdExactString": "0333"},
            {"fullGameIdExactString": "18446744073709551616"}, {"mapEntryCount": 129}, {"samePidOtherGameIdCount": 1},
            {"originalIdentityBracketMatched": False}, {"sourceMatched": False}, {"targetMembership": "absent-in-two-cache-reads"}]
        for i, change in enumerate(changes):
            value = provider(); value.update(change)
            d = provider_run(value, "provider-invalid-"+str(i))
            require(provider_field(d, "targetMembership")["value"] == "unknown")
            require(not [f for f in provider_facts(d) if f["field"].startswith("targetFieldsTagged.")])
    case("provider-invalid-or-unqualified-original-identity-never-publishes-membership", provider_identity_rejects)

    def provider_raw_types():
        value = provider(); value["targetFieldsTagged"]["m_gameID"] = {"rawType": "bigint", "value": provider_game}
        value["targetFieldsTagged"]["m_eBrowserType"] = {"rawType": "undefined", "value": None}
        value["targetFieldsTagged"]["m_eUIMode"] = {"rawType": "null", "value": None}
        d = provider_run(value, "provider-tagged")
        require(provider_field(d, "targetFieldsTagged.m_gameID.rawType")["value"] == "bigint")
        require(provider_field(d, "targetFieldsTagged.m_eBrowserType.rawType")["value"] == "undefined")
        require(provider_field(d, "targetFieldsTagged.m_eUIMode.rawType")["value"] == "null")
        value["targetFieldsTagged"]["m_gameID"] = {"rawType": "number", "value": None}
        require(provider_field(provider_run(value, "provider-lossy"), "targetMembership")["value"] == "unknown")
    case("provider-primitive-raw-types-lossless-bigint-and-lossy-number-rejection", provider_raw_types)

    def provider_privacy():
        value = provider(); value["secret"] = "PRIVATE_PROVIDER_TOKEN"; value["steamUI"] = {"nativeAppFlags": 8196}
        d = provider_run(value, "provider-private")
        require("PRIVATE_PROVIDER_TOKEN" not in json.dumps(d) and not facts(d, "nativeAppFlags"))
        value["targetFieldsTagged"]["secret"] = {"rawType": "string", "value": "PRIVATE_FIELDS"}
        d = provider_run(value, "provider-tag-private")
        require("PRIVATE_FIELDS" not in json.dumps(d) and provider_field(d, "targetMembership")["value"] == "unknown")
        value = provider(); value["rows"] = []
        require(not provider_facts(provider_run(value, "provider-wire-rejected")))
        require(not provider_facts(provider_run(None, "provider-wrong-wrapper", {"unknownPrivate": {provider_key: provider()}})))
    case("provider-summary-is-closed-no-raw-rows-private-fields-or-unknown-wrappers", provider_privacy)

    def provider_proofs():
        for flag in TOOL.OVERLAY_PROVIDER_PROOFS:
            value = provider(); value[flag] = True; d = provider_run(value, "provider-proof-"+flag)
            require(provider_field(d, flag)["value"] == "unknown")
            require(provider_field(d, "targetMembership")["value"] == "unknown")
            require(all(v == "unknown" for k, v in d["assessment"].items() if k != "instancesMerged"))
    case("provider-forged-contact-input-continue-atomicity-or-identity-proof-does-not-upgrade", provider_proofs)

    def provider_old_output():
        data = {"event": "steam-game-link-state", "details": {"nativeRecordDiagnostic": {"predicateInputs": {
            "lastRecordKeepDecisionRaw": 1, "comparisonLatchRaw": 1, "failedCheckCounterRaw": 0, "alternatePidRaw": 0}},
            popup_key: popup((0, 101)), "nativeAppFlags": 4}}
        base = provider_run(None, "provider-old-base", data)
        extra = copy.deepcopy(data); extra["details"][provider_key] = provider()
        d = provider_run(None, "provider-old-added", extra); old_events = copy.deepcopy(d["events"])
        for event in old_events:
            event["facts"] = [f for f in event["facts"] if f.get("namespace") != provider_key]
        require(old_events == base["events"] and d["assessment"] == base["assessment"] and d["missingEvidence"] == base["missingEvidence"])
    case("provider-leaf-preserves-popup-predicate-events-and-assessments", provider_old_output)

    def provider_limits():
        value = provider(); value["targetFieldsTagged"]["m_eUIMode"] = {"rawType": "string", "value": "x"*65}
        require(provider_field(provider_run(value, "provider-string-limit"), "targetMembership")["value"] == "unknown")
        d = provider_run(provider(), "provider-node-limit", limits={"nodes": 20})
        require("INVALID_JSON_OR_DEPTH" in errors(d) or "NODE_OR_DEPTH_LIMIT" in errors(d))
        require(not provider_facts(d))
    case("provider-existing-node-and-bounded-tag-budget-enforced", provider_limits)

    def cohort(pids, present=False):
        value = provider(present)
        template = provider()["targetFieldsTagged"]
        rows = []
        for raw_pid in pids:
            row = copy.deepcopy(template); row["m_unPID"]["value"] = raw_pid; rows.append(row)
        value.update(mapEntryCount=max(len(rows), value["mapEntryCount"]), sameGameIdCandidateCount=len(rows),
            sameGameIdCandidatesComplete=len(rows) <= 2, sameGameIdCandidatesTagged=rows if len(rows) <= 2 else None)
        return value

    def candidate_helper_domain():
        value = cohort([202]); result = provider_run(value, "candidate-helper")
        require(provider_field(result, "targetMembership")["value"] == "absent-in-two-cache-reads")
        hit = provider_field(result, "sameGameIdCandidatesTagged.0.m_unPID.value")
        require(hit["value"] == 202 and hit["source"]["jsonPointer"].endswith("/sameGameIdCandidatesTagged/0/m_unPID/value"))
        base = provider_run(provider(False), "candidate-helper-base")
        require(result["assessment"] == base["assessment"] and result["missingEvidence"] == base["missingEvidence"])
        require(provider_field(result, "steamContactVerified")["value"] is False)
    case("provider-same-gameid-helper-pid-candidate-is-not-original-membership-or-recovery", candidate_helper_domain)

    def candidate_cardinality():
        for ids in ([], [202], [101, 202]):
            value = cohort(ids, 101 in ids); result = provider_run(value, "candidate-count-"+str(len(ids)))
            require(provider_field(result, "sameGameIdCandidateCount")["value"] == len(ids))
            require(provider_field(result, "sameGameIdCandidatesComplete")["value"] is True)
        result = provider_run(cohort([202, 203, 204]), "candidate-overflow")
        require(provider_field(result, "sameGameIdCandidateCount")["value"] == 3)
        require(provider_field(result, "sameGameIdCandidatesComplete")["value"] is False)
        require(provider_field(result, "sameGameIdCandidatesTagged")["value"] == "unknown")
        require(provider_field(result, "targetMembership")["value"] == "absent-in-two-cache-reads")
    case("provider-candidates-zero-two-and-overflow-preserve-target-diagnostic", candidate_cardinality)

    def candidate_rejects():
        for index in range(9):
            value = cohort([202]); row = value["sameGameIdCandidatesTagged"][0]
            if index == 0: row["m_gameID"]["value"] = str(int(provider_game) + 1)
            elif index == 1: row["m_unAppID"]["value"] = 334
            elif index == 2: row["m_unPID"]["value"] = True
            elif index == 3: row["m_unPID"]["value"] = 0x100000000
            elif index == 4: row["foreign"] = {"rawType": "string", "value": "DO_NOT_EXPORT_CANDIDATE_SECRET"}
            elif index == 5: value["originalIdentityBracketMatched"] = False
            elif index == 6: value["sourceMatched"] = False
            elif index == 7: value["sameGameIdCandidateCount"] = 2
            else: row["m_unPID"]["value"] = 101  # contradicts absent target membership
            result = provider_run(value, "candidate-invalid-"+str(index))
            require(provider_field(result, "sameGameIdCandidateCount")["value"] == "unknown")
            require(not any(f["field"].startswith("sameGameIdCandidatesTagged.") for f in provider_facts(result)))
            require("DO_NOT_EXPORT_CANDIDATE_SECRET" not in json.dumps(result))
        duplicate = cohort([202, 202]); result = provider_run(duplicate, "candidate-duplicate")
        require(provider_field(result, "sameGameIdCandidateCount")["value"] == "unknown")
    case("provider-candidate-private-foreign-conflicting-or-unqualified-rows-never-escape", candidate_rejects)

    def candidate_full64():
        value = cohort([202]); exact = str((0xffffffff << 32) | 0x02000000)
        value.update(appid=0xffffffff, fullGameIdExactString=exact)
        row = value["sameGameIdCandidatesTagged"][0]
        row["m_unAppID"]["value"] = -1; row["m_gameID"] = {"rawType": "bigint", "value": exact}
        result = provider_run(value, "candidate-full64")
        require(provider_field(result, "sameGameIdCandidatesTagged.0.m_gameID.value")["value"] == exact)
        row["m_gameID"] = {"rawType": "number", "value": int(exact)}
        result = provider_run(value, "candidate-full64-lossy")
        require(provider_field(result, "sameGameIdCandidateCount")["value"] == "unknown")
    case("provider-candidate-full64-bigint-signed-app-preserved-lossy-number-rejected", candidate_full64)

    def candidate_unknown_and_budgets():
        value = cohort([]); value.update(available=False, reason="original-identity-bracket-unqualified",
            originalIdentityBracketMatched=False, targetMembership="unknown", mapEntryCount=None,
            samePidOtherGameIdCount=None, sameGameIdCandidateCount=None,
            sameGameIdCandidatesComplete=False, sameGameIdCandidatesTagged=None)
        result = provider_run(value, "candidate-bracket-rejected")
        require(provider_field(result, "sameGameIdCandidateCount")["value"] == "unknown")
        require(provider_field(result, "sameGameIdCandidatesComplete")["value"] is False)
        value = cohort([202, 203]); value["sameGameIdCandidatesTagged"][1]["m_eUIMode"] = {"rawType": "string", "value": "x" * 65}
        require(provider_field(provider_run(value, "candidate-tag-budget"), "sameGameIdCandidateCount")["value"] == "unknown")
        limited = provider_run(cohort([202, 203]), "candidate-node-budget", limits={"nodes": 20})
        require("NODE_OR_DEPTH_LIMIT" in errors(limited) or "INVALID_JSON_OR_DEPTH" in errors(limited))
    case("provider-candidate-unknown-and-existing-node-scalar-budgets-remain-explicit", candidate_unknown_and_budgets)

    history = {}
    def historical():
        source = Path(args.historical_zip).resolve()
        registration = json.loads(Path(args.registration).read_text(encoding="utf-8-sig"))
        require(Path(registration["target"]).resolve() == source, "registered archive path mismatch")
        digest = hashlib.sha256(source.read_bytes()).hexdigest()
        require(digest.lower() == registration["sha256"].lower(), "registered archive hash mismatch")
        d = TOOL.Extractor().run(source)
        require(d["status"] == "parsed", str(d["errors"]))
        require({m["role"] for m in d["members"]} - {"native-lifecycle"} == {"current", "last", "sleep-facts", "game-link-current"})
        require(facts(d, "nativeAppFlags") and facts(d, "recordState") and facts(d, "localDisplayStatus"))
        require(d["assessment"]["actualContinueButton"] == "unknown")
        history.update({"source": str(source), "registration": str(Path(args.registration).resolve()), "sha256": digest,
                        "summary": d["summary"], "roles": [m["role"] for m in d["members"]],
                        "nativeAppFlagsRawValues": sorted({f["value"] for f in facts(d, "nativeAppFlags")}),
                        "jsLocalDisplayStatusValues": sorted({f["value"] for f in facts(d, "localDisplayStatus")}),
                        "recordStates": sorted({f["value"] for f in facts(d, "recordState")}),
                        "assessment": d["assessment"], "missingEvidence": d["missingEvidence"]})
    case("one-project-registered-ordinary-historical-ZIP-replay", historical)
    return write_results(out, checks, history)


if __name__ == "__main__":
    raise SystemExit(main())
