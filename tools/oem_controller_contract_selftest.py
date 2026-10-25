"""Independent capability-audit negative controls; no hardware or real source tree."""
import json
import pathlib
import tempfile
import unittest
from oem_controller_contract_audit import audit, DECK, ZONE, LEGIONS

class ControllerContractTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.tmp.name)
        self.hc = self.root / "HC"
        files = {
            "HandheldCompanion/Controllers/Steam/NeptuneController.cs": """
Inputs.ButtonState[ButtonFlags.Special] |= input.State.ButtonState[NeptuneControllerButton.BtnSteam];
Inputs.ButtonState[ButtonFlags.Special2] |= input.State.ButtonState[NeptuneControllerButton.BtnQuickAccess];
Inputs.ButtonState[ButtonFlags.L4] = input.State.ButtonState[NeptuneControllerButton.BtnL4];
Inputs.ButtonState[ButtonFlags.R4] = input.State.ButtonState[NeptuneControllerButton.BtnR4];
Inputs.ButtonState[ButtonFlags.L5] = input.State.ButtonState[NeptuneControllerButton.BtnL5];
Inputs.ButtonState[ButtonFlags.R5] = input.State.ButtonState[NeptuneControllerButton.BtnR5];""",
            "HandheldCompanion/Controllers/Zotac/ZoneController.cs": """
Inputs.ButtonState[ButtonFlags.B5] = wheel.HasFlag(ButtonWheel.LeftAnti);
Inputs.ButtonState[ButtonFlags.B6] = wheel.HasFlag(ButtonWheel.LeftClock);
Inputs.ButtonState[ButtonFlags.B7] = wheel.HasFlag(ButtonWheel.RightAnti);
Inputs.ButtonState[ButtonFlags.B8] = wheel.HasFlag(ButtonWheel.RightClock);""",
            "HandheldCompanion/Controllers/Lenovo/LegionController.cs": """
Inputs.ButtonState[ButtonFlags.L4] = buttons.HasFlag(BackEnum.Y1);
Inputs.ButtonState[ButtonFlags.R4] = buttons.HasFlag(BackEnum.M3);
Inputs.ButtonState[ButtonFlags.L5] = buttons.HasFlag(BackEnum.Y2);
Inputs.ButtonState[ButtonFlags.R5] = buttons.HasFlag(BackEnum.Y3);""",
            "steam-hidapi.net/NeptuneController.cs": "(e.Buffer[0] != 1) || (e.Buffer[1] != 0); case (byte)SCEventType.DECK_INPUT_DATA: e.Buffer.ToStructure<NCInput>(); SetLizardMode(_lizard); _configureSignal.WaitOne(1000);",
            "steam-hidapi.net/Hid/HidEnums.cs": "DECK_INPUT_DATA = 0x09;",
            "steam-hidapi.net/SteamController.cs": "SCPacketType.DEFAULT_MAPPINGS; SCPacketType.DEFAULT_MOUSE; SCPacketType.CLEAR_MAPPINGS;",
            "hidapi.net/HidDevice.cs": "hid_read_timeout;",
            "HandheldCompanion/Managers/ControllerManager.cs": "case 0x1205: new NeptuneController(details); new ZoneController(details);",
        }
        for name, value in files.items():
            path = self.hc / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(value, encoding="utf-8")
        self.main = self.root / "main.cpp"
        self.main.write_text("""const char* js = R"( /* not a native comment )";
enum class OemProducerId : unsigned { Legion, Gpd, Count };
static unsigned long long oemLiveBackBits() {
    return live & 0x3ull;
}
""", encoding="utf-8")
        self.host = self.root / "Host.cs"
        self.host.write_text("""private static SteamDeckStateFields BuildSteamDeckStateFields(HMProfile p, CommandEnvelope envelope) {
        if ((envelope.BackButtons & 0x1) != 0) buttons |= HMButton.LeftPaddle;
        if ((envelope.BackButtons & 0x2) != 0) buttons |= HMButton.RightPaddle;
    }
""", encoding="utf-8")
        self.models = [
            {"deviceClass": DECK, "keys": [{"keyId": "special"}, {"keyId": "special2"}]},
            {"deviceClass": ZONE, "keys": [{"keyId": key} for key in ["zotac", "dots", "home", "m1", "m2"]]},
        ] + [{"deviceClass": name, "keys": [{"keyId": "first", "backIndex": 1}, {"keyId": "second", "backIndex": 2}, {"keyId": "third", "backIndex": 3}, {"keyId": "fourth", "backIndex": 4}]} for name in LEGIONS]
        self.matrix = self.root / "matrix.json"
        self.save_models()

    def tearDown(self):
        self.tmp.cleanup()

    def save_models(self):
        self.matrix.write_text(json.dumps({"models": self.models}), encoding="utf-8")

    def run_audit(self, binding=None, manifest=None):
        return audit(self.hc, self.matrix, self.main, self.host, binding, manifest)

    def test_catalog_only_audit_misses_eight_controller_inputs(self):
        result = self.run_audit()
        self.assertEqual(result["missingCatalogInputCount"], 8)
        self.assertEqual(len(result["findings"]), 4)
        self.assertEqual(result["summaryByFinding"]["virtual-rear-independent-output-limit"]["classCount"], 2)

    def test_catalog_completion_requires_review(self):
        self.models[0]["keys"].append({"keyId": "l4", "backIndex": 1})
        self.save_models()
        with self.assertRaisesRegex(ValueError, "Deck catalog changed"):
            self.run_audit()

    def test_new_wheel_action_requires_review(self):
        self.models[1]["keys"].append({"keyId": "wheel-left"})
        self.save_models()
        with self.assertRaisesRegex(ValueError, "Zone catalog changed"):
            self.run_audit()

    def test_new_reader_cannot_keep_old_missing_reader_claim(self):
        self.main.write_text(self.main.read_text().replace("Legion, Gpd", "Legion, Gpd, Neptune"))
        with self.assertRaisesRegex(ValueError, "New controller producer"):
            self.run_audit()

    def test_extended_native_mask_requires_review(self):
        self.main.write_text(self.main.read_text().replace("0x3ull", "0xfull"))
        with self.assertRaisesRegex(ValueError, "Rear output admission changed"):
            self.run_audit()

    def test_extended_host_codec_requires_review(self):
        self.host.write_text(self.host.read_text().replace("    }", "        if ((envelope.BackButtons & 0x4) != 0) buttons |= HMButton.LeftPaddle2;\n    }"))
        with self.assertRaisesRegex(ValueError, "Host paddle codec changed"):
            self.run_audit()

    def test_controller_parser_change_requires_review(self):
        path = self.hc / "HandheldCompanion/Controllers/Steam/NeptuneController.cs"
        path.write_text(path.read_text().replace("BtnQuickAccess", "BtnOptions"))
        with self.assertRaisesRegex(ValueError, "HC button assignment"):
            self.run_audit()

    def test_stale_inventory_source_is_rejected(self):
        binding = self.root / "binding.json"
        binding.write_text(json.dumps({"mainSha256": "incorrect"}))
        with self.assertRaisesRegex(ValueError, "source identity mismatch"):
            self.run_audit(binding)

    def test_reference_without_manifest_is_not_claimed_as_fixed_commit(self):
        result = self.run_audit()
        self.assertIsNone(result["HCCommit"])
        self.assertFalse(result["committedSourcesVerified"])
        self.assertFalse(result["hardwareIo"])
        self.assertFalse(result["physicalEffectsVerified"])
        self.assertFalse(result["sourceCompletenessVerified"])
        self.assertFalse(result["deckFrameContract"]["nativeWindowsReportIdOffsetVerified"])

    def test_source_manifest_rejects_content_drift(self):
        manifest = self.root / "manifest.json"
        manifest.write_text(json.dumps({"commit": "06c0b9544db2b1f39abf9cd3796225ccfb096103", "files": []}))
        with self.assertRaisesRegex(ValueError, "Committed HC source identity mismatch"):
            self.run_audit(manifest=manifest)

if __name__ == "__main__":
    unittest.main()
