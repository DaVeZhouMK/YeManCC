"""Read-only audit for controller-layer capabilities missing from OEM chord tables.

This supplements oem_key_source_audit: a complete source for every catalog key
cannot prove that the catalog contains every key in HC's controller parsers.
No HID APIs, runtime HC assemblies, registry, or device constructors are invoked.
"""
import argparse
import hashlib
import json
import pathlib
import re

HC_COMMIT = "06c0b9544db2b1f39abf9cd3796225ccfb096103"
DECK = "HandheldCompanion.Devices.SteamDeck"
ZONE = "HandheldCompanion.Devices.GamingZone"
LEGIONS = ["HandheldCompanion.Devices.LegionGoTablet", "HandheldCompanion.Devices.LegionGoTablet2"]
DECK_FLAGS = {"Special": "BtnSteam", "Special2": "BtnQuickAccess", "L4": "BtnL4", "R4": "BtnR4", "L5": "BtnL5", "R5": "BtnR5"}
ZONE_FLAGS = {"B5": "LeftAnti", "B6": "LeftClock", "B7": "RightAnti", "B8": "RightClock"}

def read(path):
    return pathlib.Path(path).read_text(encoding="utf-8-sig")

def sha(path):
    return hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()

def require(condition, message):
    if not condition:
        raise ValueError(message)

def without_comments(source):
    return re.sub(r"//[^\n]*", "", re.sub(r"/\*[\s\S]*?\*/", "", source))

def assignments(source, flags):
    clean = without_comments(source)
    result = {}
    for flag, value in flags.items():
        pattern = r"Inputs\.ButtonState\[ButtonFlags\." + flag + r"\]\s*\|?=\s*([^;]+);"
        matches = re.findall(pattern, clean)
        exact = [expression.strip() for expression in matches if re.search(r"\b" + value + r"\b", expression)]
        require(len(exact) == 1, "Review changed HC button assignment: " + flag)
        result[flag] = exact[0]
    return result

def audit(hc_root, native_matrix, native_main, input_host, matrix_binding=None, hc_manifest=None):
    hc_root = pathlib.Path(hc_root)
    paths = {
        "deckController": hc_root / "HandheldCompanion/Controllers/Steam/NeptuneController.cs",
        "deckParser": hc_root / "steam-hidapi.net/NeptuneController.cs",
        "deckEnums": hc_root / "steam-hidapi.net/Hid/HidEnums.cs",
        "deckModeWriter": hc_root / "steam-hidapi.net/SteamController.cs",
        "deckHid": hc_root / "hidapi.net/HidDevice.cs",
        "zoneController": hc_root / "HandheldCompanion/Controllers/Zotac/ZoneController.cs",
        "legionController": hc_root / "HandheldCompanion/Controllers/Lenovo/LegionController.cs",
        "controllerSelection": hc_root / "HandheldCompanion/Managers/ControllerManager.cs",
        "nativeMain": pathlib.Path(native_main), "inputHost": pathlib.Path(input_host),
        "compiledInventory": pathlib.Path(native_matrix),
    }
    source = {name: read(path) for name, path in paths.items()}
    if matrix_binding:
        binding = json.loads(read(matrix_binding))
        require(binding["mainSha256"] == sha(native_main), "Native matrix source identity mismatch")
    committed_sources_verified = False
    if hc_manifest:
        manifest = json.loads(read(hc_manifest))
        require(manifest["commit"] == HC_COMMIT, "Unexpected HC reference commit")
        entries = {entry["path"]: entry["sha256"] for entry in manifest["files"]}
        for role, path in paths.items():
            if role in ["nativeMain", "inputHost", "compiledInventory"]:
                continue
            relative = path.relative_to(hc_root).as_posix()
            require(entries.get(relative) == sha(path), "Committed HC source identity mismatch: " + relative)
        committed_sources_verified = True
    matrix = json.loads(source["compiledInventory"])
    models = {m["deviceClass"]: m for m in matrix["models"]}
    for name in [DECK, ZONE] + LEGIONS:
        require(name in models, "Required reference capability query absent: " + name)
    deck_map = assignments(source["deckController"], DECK_FLAGS)
    zone_map = assignments(source["zoneController"], ZONE_FLAGS)
    legion_map = assignments(source["legionController"], {"L4": "Y1", "R4": "M3", "L5": "Y2", "R5": "Y3"})
    # Native main embeds JavaScript/raw strings. Strip comments only within the
    # narrow C++ function below; a whole-file regex can consume unrelated code.
    main = source["nativeMain"]
    host = source["inputHost"]
    scope = re.search(r"enum class OemProducerId[^\{]+\{([^}]+)\}", main)
    require(scope is not None, "OEM producer declaration moved; review source selection")
    producers = [x.strip() for x in scope[1].split(",") if x.strip() != "Count"]
    require(not any(re.search(r"deck|neptune|zone|zotac", p, re.I) for p in producers),
            "New controller producer needs review before declaring missing readers")
    live = re.search(r"static unsigned long long oemLiveBackBits\(\)\s*\{([\s\S]*?)\n\}", main)
    require(live is not None and "return live & 0x3ull;" in without_comments(live[1]),
            "Rear output admission changed; review independent slot capacity")
    host_deck = re.search(r"private static SteamDeckStateFields BuildSteamDeckStateFields\([^)]*\)\s*\{([\s\S]*?)\n    \}", host)
    require(host_deck is not None, "Host Deck codec moved")
    paddle_mappings = re.findall(r"if \(\(envelope\.BackButtons & (0x[0-9a-fA-F]+)\) != 0\) buttons \|= HMButton\.(\w+);", host_deck[1])
    require(paddle_mappings == [("0x1", "LeftPaddle"), ("0x2", "RightPaddle")],
            "Host paddle codec changed; review before reporting a two-slot limit")
    parser = source["deckParser"]
    require("(e.Buffer[0] != 1) || (e.Buffer[1] != 0)" in parser and
            "case (byte)SCEventType.DECK_INPUT_DATA:" in parser and
            "e.Buffer.ToStructure<NCInput>()" in parser, "HC Deck frame contract changed")
    require("SetLizardMode(_lizard)" in parser and "_configureSignal.WaitOne(1000)" in parser,
            "HC Deck configuration ownership changed")
    require(re.search(r"DECK_INPUT_DATA\s*=\s*0x0?9\b", source["deckEnums"]), "HC Deck input event changed")
    require(all(token in source["deckModeWriter"] for token in ["SCPacketType.DEFAULT_MAPPINGS", "SCPacketType.DEFAULT_MOUSE", "SCPacketType.CLEAR_MAPPINGS"]), "HC Deck mode writer changed")
    require("hid_read_timeout" in source["deckHid"], "Review HC HID transport/framing")
    require("case 0x1205:" in source["controllerSelection"] and "new NeptuneController(details)" in source["controllerSelection"] and
            "new ZoneController(details)" in source["controllerSelection"], "HC specialized selection changed")
    deck = models[DECK]
    require({k["keyId"] for k in deck["keys"]} == {"special", "special2"},
            "Deck catalog changed; review missing rear entries")
    require(not any(k.get("backIndex", 0) for k in deck["keys"]), "Deck rear catalog now present")
    zone_keys = models[ZONE]["keys"]
    require({k["keyId"] for k in zone_keys} == {"zotac", "dots", "home", "m1", "m2"},
            "Zone catalog changed; review wheel input coverage")
    findings = [
        {"deviceClass": DECK, "id": "hc-controller-rear-keys-absent-from-catalog", "status": "missing",
         "referenceInputs": ["L4", "R4", "L5", "R5"], "missingInputCount": 4,
         "scope": "HC Neptune decodes four independent rear inputs; native Deck catalog has only two front special keys. True-Deck rear mapping remains deliberately excluded; report unsupported, do not enable by changing family predicates."},
        {"deviceClass": ZONE, "id": "hc-controller-wheel-inputs-absent-from-catalog", "status": "missing",
         "referenceInputs": list(ZONE_FLAGS), "referenceActions": list(ZONE_FLAGS.values()), "missingInputCount": 4,
         "scope": "HC Zone vendor reader publishes four wheel actions. Native Zone OEM catalog covers five keyboard chords only; no dedicated wheel action/catalog path is established."},
    ]
    for name in LEGIONS:
        additional = [k["keyId"] for k in models[name]["keys"] if k.get("backIndex", 0) > 2]
        require(len(additional) == 2, "Legion additional paddle catalog changed")
        findings.append({"deviceClass": name, "id": "virtual-rear-independent-output-limit", "status": "partial",
                         "affectedCatalogKeys": additional, "referenceIndependentPaddles": 4,
                         "nativeOutputSlots": 2, "hostDeckOutputSlots": 2,
                         "scope": "Additional keys remain in physical OEM ledger/editor and explicit remapping remains possible. Current native-to-Host rear channel cannot preserve four independent paddle states."})
    return {
        "schema": 1, "HCCommit": HC_COMMIT if committed_sources_verified else None, "committedSourcesVerified": committed_sources_verified,
        "scope": "controller-layer supplement to committed OEM chords; dedicated/rear inputs and virtual rear output capacity; no direction correction",
        "referenceControllerAssignments": {"Deck": deck_map, "Zone": zone_map, "Legion": legion_map},
        "selectedNativeProducerNames": producers,
        "deckFrameContract": {"callbackHeader": [1, 0], "eventType": 9, "structOffsetInHidapiBuffer": 0,
                              "nativeWindowsReportIdOffsetVerified": False, "passiveFreshStreamVerified": False,
                              "HCOpenMayWriteMode": True, "modeOwnershipAndRestoreImplemented": False,
                              "readerImplementedInYMCC": False},
        "findings": findings,
        "summaryByFinding": {code: {"classCount": len({f["deviceClass"] for f in findings if f["id"] == code}),
                                     "classes": sorted({f["deviceClass"] for f in findings if f["id"] == code})}
                             for code in sorted({f["id"] for f in findings})},
        "missingCatalogInputCount": sum(f.get("missingInputCount", 0) for f in findings),
        "sourceBindings": [{"role": name, "file": str(path.resolve()), "sha256": sha(path)} for name, path in paths.items()],
        "limits": ["Reference source availability is not current hardware readiness or physical verification.",
                   "HC device OEMChords do not include all controller-layer inputs.",
                   "These three controller contracts supplement the existing matrix; external detachable controllers and sensor/touch axes are not audited by this tool.",
                   "Two virtual rear slots is a software path limit, not a statement about hardware paddle count.",
                   "No passive Deck reader is added: exact physical endpoint, framing, fresh stream, mode ownership and release remain to prove."],
        "hardwareIo": False, "HCAssemblyLoaded": False, "directionChanged": False,
        "sourceCompletenessVerified": False, "physicalEffectsVerified": False,
    }

def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ["hc-root", "native-matrix", "native-main", "input-host", "output"]:
        p.add_argument("--" + name, required=True)
    p.add_argument("--matrix-binding")
    p.add_argument("--hc-manifest")
    args = p.parse_args()
    result = audit(args.hc_root, args.native_matrix, args.native_main, args.input_host, args.matrix_binding, args.hc_manifest)
    output = pathlib.Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps({"missingCatalogInputCount": result["missingCatalogInputCount"],
                      "summaryByFinding": result["summaryByFinding"], "hardwareIo": False}, ensure_ascii=False, indent=2))

if __name__ == "__main__":
    main()
