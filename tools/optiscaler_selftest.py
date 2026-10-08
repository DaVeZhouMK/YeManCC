#!/usr/bin/env python3
"""Offline OptiScaler cache/template/transaction regression. Never touches games or real AppData.
Run: python tools/optiscaler_selftest.py
"""
import configparser
import importlib.util
import contextlib
import hashlib
import io
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("ymcc_optiscaler_test", ROOT / "PowerControl/pawnio/YeManTdpCtl.py")
ctl = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ctl)


def dll(path, marker=b"runtime"):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    data = bytearray(1024)
    data[:2] = b"MZ"
    data[60:64] = (64).to_bytes(4, "little")
    data[64:70] = b"PE\0\0\x64\x86"
    data[70:72] = (1).to_bytes(2, "little")
    data[84:86] = (240).to_bytes(2, "little")
    data[86:88] = (0x2002).to_bytes(2, "little")
    data[88:90] = b"\x0b\x02"
    data[328:336] = b".text\0\0\0"
    data[344:348] = (512).to_bytes(4, "little")
    data[348:352] = (512).to_bytes(4, "little")
    data[512:512 + len(marker)] = marker
    path.write_bytes(data)
    return path


def package(root, version):
    base = root / "OptiScaler" / version
    for name in ("OptiScaler.dll", "amd_fidelityfx_dx12.dll", "amd_fidelityfx_upscaler_dx12.dll",
                 "amd_fidelityfx_framegeneration_dx12.dll", "amd_fidelityfx_vk.dll",
                 "libxess.dll", "libxess_dx11.dll", "libxess_fg.dll", "libxell.dll",
                 "fakenvapi.dll", "dlssg_to_fsr3_amd_is_better.dll"):
        dll(base / name, version.encode())
    (base / "OptiScaler.ini").write_text("; preserve comment\n[Upscalers]\nDx12Upscaler=auto\n[FrameGen]\nEnabled=auto\n[Custom]\nKeep=yes\n", encoding="utf-8")
    dll(base / "D3D12_Optiscaler/D3D12Core.dll")
    return base


class OptiScalerTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="YMCC-opti-test-")
        self.root = Path(self.temp.name)
        self.cache = self.root / "Cache"
        self.backup = self.root / "Backups"
        self.game = self.root / "Game"
        self.game.mkdir()
        self.patches = [patch.object(ctl, "_optiscaler_cache_roots", return_value=(str(self.root / "Client"), [str(self.cache)])),
                        patch.object(ctl, "_ymcc_backup_root", return_value=str(self.backup)),
                        patch.object(ctl, "_find_client_backup", return_value=None),
                        patch.object(ctl, "_olog")]
        for p in self.patches:
            p.start()
        self.base = package(self.cache, "0.9.5-pre4")
        package(self.cache, "0.9.4")
        self.extra = dll(self.cache / "Extras/FSR_4.1.1b/amd_fidelityfx_upscaler_dx12.dll", b"new-INT8")
        dll(self.cache / "Extras/FSR_4.1.1/amd_fidelityfx_upscaler_dx12.dll", b"old-INT8")
        dll(self.cache / "OptiPatcher/0.41/OptiPatcher.asi")

    def tearDown(self):
        for p in reversed(self.patches):
            p.stop()
        self.temp.cleanup()

    def cfg(self):
        return ctl._optiscaler_cfg()

    def snapshot(self):
        return {str(p.relative_to(self.game)): p.read_bytes() for p in self.game.rglob("*") if p.is_file()}

    def read_ini(self):
        ini = configparser.ConfigParser()
        ini.read(self.game / "OptiScaler.ini", encoding="utf-8")
        return ini

    def test_pinned_versions_and_extra_wins_over_old_sdk(self):
        cfg = self.cfg()
        self.assertEqual(cfg["opti_version"], "0.9.5-pre4")
        self.assertEqual(cfg["extra_version"], "FSR_4.1.1b")
        self.assertEqual(cfg["runtime_files"]["fsr"]["amd_fidelityfx_upscaler_dx12.dll"], str(self.extra))
        self.assertEqual(cfg["runtime_files"]["fsr"].get("amd_fidelityfx_framegeneration_dx12.dll"), str(self.base / "amd_fidelityfx_framegeneration_dx12.dll"))
        for backend in ("fsr", "xess"):
            plan = {x["rel"]: x for x in ctl._optiscaler_plan(str(self.game), cfg, backend)}
            self.assertEqual(plan["amd_fidelityfx_upscaler_dx12.dll"]["src"], str(self.extra))
            for name in ("libxell.dll", "fakenvapi.dll", "libxess_fg.dll", "amd_fidelityfx_framegeneration_dx12.dll"):
                self.assertIn(name, plan)
            self.assertIn("plugins\\OptiPatcher.asi", plan)

    def test_corrupt_new_release_falls_back(self):
        (self.base / "OptiScaler.dll").write_bytes(b"MZ-truncated")
        self.extra.write_bytes(b"bad")
        cfg = self.cfg()
        self.assertEqual(cfg["opti_version"], "0.9.4")
        self.assertEqual(cfg["extra_version"], "FSR_4.1.1")
        self.assertEqual(len(cfg["warnings"]), 2)

    def test_incomplete_new_release_falls_back(self):
        (self.base / "libxess.dll").unlink()
        self.assertEqual(self.cfg()["opti_version"], "0.9.4")

    def test_missing_new_release_falls_back(self):
        (self.base / "OptiScaler.dll").unlink()
        self.extra.unlink()
        self.assertEqual(self.cfg()["opti_version"], "0.9.4")
        self.assertEqual(self.cfg()["extra_version"], "FSR_4.1.1")

    def test_future_nightly_and_fp8_are_ignored(self):
        package(self.cache, "0.10.999-nightly")
        dll(self.cache / "Extras/FSR_4.1.1b_FP8/amd_fidelityfx_upscaler_dx12.dll", b"FP8")
        self.assertEqual(self.cfg()["opti_version"], "0.9.5-pre4")
        self.extra.unlink()
        (self.cache / "Extras/FSR_4.1.1/amd_fidelityfx_upscaler_dx12.dll").unlink()
        cfg = self.cfg()
        self.assertIsNone(cfg["src_extra"])
        template, _ = ctl._optiscaler_template("fsr", {"api": "dx12"}, cfg)
        self.assertEqual(template["FSR"]["UpscalerIndex"], "1")
        self.assertEqual(template["FSR"]["Fsr4Update"], "false")

    def test_pinned_release_in_second_root_beats_older_first_root(self):
        second = self.root / "Cache2"
        package(second, "0.9.5-pre4")
        (self.base / "OptiScaler.dll").unlink()
        with patch.object(ctl, "_optiscaler_cache_roots", return_value=(str(self.root / "Client"), [str(self.cache), str(second)])):
            cfg = self.cfg()
            self.assertEqual(cfg["opti_version"], "0.9.5-pre4")
            self.assertEqual(cfg["source_root"], str(second))

    def test_both_modes_install_and_exact_uninstall_roundtrip(self):
        for backend in ("fsr", "xess"):
            with self.subTest(backend=backend):
                (self.game / "Game.exe").write_bytes(b"my-game")
                (self.game / "d3d12.dll").write_bytes(b"native-api")
                (self.game / "dxgi.dll").write_bytes(b"original-inject")
                (self.game / "OptiScaler.ini").write_bytes(b"original-settings")
                (self.game / "libxess.dll").write_bytes(b"original-xess")
                before = self.snapshot()
                cfg = self.cfg()
                result = ctl._optiscaler_install(str(self.game), cfg, False, backend)
                self.assertTrue(result["ok"], result)
                self.assertEqual(result["version"], "0.9.5-pre4/FSR_4.1.1b")
                self.assertEqual((self.game / "amd_fidelityfx_upscaler_dx12.dll").read_bytes(), self.extra.read_bytes())
                ini = self.read_ini()
                self.assertEqual(ini["Upscalers"]["Dx12Upscaler"], "fsr31" if backend == "fsr" else "xess")
                self.assertEqual(ini["Upscalers"]["Dx11Upscaler"], "fsr31_12" if backend == "fsr" else "xess_12")
                self.assertEqual(ini["FrameGen"]["FGInput"], "upscaler")
                self.assertEqual(ini["FrameGen"]["FGOutput"], "fsrfg" if backend == "fsr" else "xefg")
                self.assertEqual(ini["XeFG"]["InterpolationCount"], "1")
                self.assertEqual(ini["OptiFG"]["HUDFix"], "true")
                self.assertNotIn("HUDFix", ini.sections())
                self.assertEqual(ini["FSR"]["Fsr4ForceEnableInt8"], "true")
                self.assertEqual(ini["QualityOverrides"]["QualityRatioOverrideEnabled"], "false")
                self.assertEqual(ini["Custom"]["Keep"], "yes")
                self.assertTrue(ctl._optiscaler_status(str(self.game), cfg)["installed"])
                # Our newly installed split FG DLL is not a native game input.
                self.assertIsNone(ctl._optiscaler_analyze(str(self.game), cfg)["native_fg_input"])
                repeat = ctl._optiscaler_install(str(self.game), cfg, False, backend)
                self.assertFalse(repeat["ok"])
                result = ctl._optiscaler_uninstall(str(self.game), cfg, False)
                self.assertTrue(result["ok"], result)
                self.assertEqual(self.snapshot(), before)

    def test_native_fg_routes_are_preferred(self):
        cfg = self.cfg()
        for native, expected in (("nukems", "nukems"), ("fsrfg", "xefg"), ("fsrfg30", "xefg")):
            template, _ = ctl._optiscaler_template("xess", {"api": "dx12", "native_fg_input": native}, cfg)
            self.assertEqual(template["FrameGen"]["FGInput"], native)
            self.assertEqual(template["FrameGen"]["FGOutput"], expected)

    def test_native_fsr30_and_fsr31_use_distinct_inputs(self):
        for filename, expected in (("ffx_fsr3_api_x64.dll", "fsrfg30"),
                                   ("ffx_fsr3_api_dx12_x64.dll", "fsrfg30"),
                                   ("amd_fidelityfx_framegeneration_dx12.dll", "fsrfg")):
            with self.subTest(filename=filename):
                native = self.game / filename
                native.write_bytes(b"native-game-runtime")
                analysis = ctl._optiscaler_analyze(str(self.game), self.cfg())
                self.assertEqual(analysis["native_fg_input"], expected)
                template, _ = ctl._optiscaler_template("xess", analysis, self.cfg())
                self.assertEqual(template["FrameGen"]["FGInput"], expected)
                native.unlink()

    def test_templates_use_only_supported_legacy_section_key_pairs(self):
        # Independent vocabulary from v0.9.4 Config.cpp and the pre4 shipped INI.
        # Do not blindly adopt schema changes from the latest client/nightly.
        supported = {
            "Upscalers": {"Dx11Upscaler", "Dx12Upscaler", "VulkanUpscaler"},
            "FSR": {"UpscalerIndex", "Fsr4Update", "Fsr4ForceEnableInt8", "Fsr4DoNotLoadAmdxc64", "FGIndex"},
            "UpscaleRatio": {"UpscaleRatioOverrideEnabled"},
            "QualityOverrides": {"QualityRatioOverrideEnabled"},
            "Spoofing": {"Dxgi", "StreamlineSpoofing", "Vulkan", "VulkanExtensionSpoofing"},
            "Plugins": {"LoadAsiPlugins"},
            "Libraries": {"OptiDllPath", "FfxDx12Path", "FfxDx12SRPath", "FfxDx12FGPath", "FfxVkPath", "XeSSPath", "XeFGPath", "XeLLPath", "XeSSDx11Path"},
            "XeFG": {"InterpolationCount", "IgnoreInitChecks"},
            "FrameGen": {"Enabled", "FGInput", "FGOutput"},
            "OptiFG": {"HUDFix"},
        }
        for backend in ("fsr", "xess"):
            for api in ("dx11", "dx12", "vulkan", "unknown"):
                template, _ = ctl._optiscaler_template(backend, {"api": api}, self.cfg())
                for section, values in template.items():
                    self.assertIn(section, supported)
                    self.assertTrue(set(values) <= supported[section], (section, values))

    def test_safe_fg_degrade_for_dx11_vulkan_unknown(self):
        for api in ("dx11", "vulkan", "unknown"):
            template, warnings = ctl._optiscaler_template("fsr", {"api": api}, self.cfg())
            self.assertEqual(template["FrameGen"]["Enabled"], "false")
            self.assertEqual(template["FrameGen"]["FGInput"], "nofg")
            self.assertTrue(warnings)

    def test_missing_xefg_dependency_falls_back_to_fsr31_x2(self):
        (self.base / "libxell.dll").unlink()
        template, warnings = ctl._optiscaler_template("xess", {"api": "dx12"}, self.cfg())
        self.assertEqual(template["FrameGen"]["FGOutput"], "fsrfg")
        self.assertEqual(template["FSR"]["FGIndex"], "1")
        self.assertTrue(warnings)

    def test_no_fg_dependencies_disables_fg(self):
        for name in ("libxell.dll", "amd_fidelityfx_framegeneration_dx12.dll"):
            (self.base / name).unlink()
        template, _ = ctl._optiscaler_template("xess", {"api": "dx12"}, self.cfg())
        self.assertEqual(template["FrameGen"]["Enabled"], "false")

    def test_anticheat_never_writes(self):
        (self.game / "start_protected_game.exe").write_bytes(b"protected")
        before = self.snapshot()
        self.assertFalse(ctl._optiscaler_install(str(self.game), self.cfg(), False, "fsr")["ok"])
        self.assertEqual(self.snapshot(), before)
        self.assertFalse(self.backup.exists())

    def test_dry_run_never_creates_backup_or_game_files(self):
        before = self.snapshot()
        result = ctl._optiscaler_install(str(self.game), self.cfg(), True, "fsr")
        self.assertTrue(result["ok"], result)
        self.assertEqual(self.snapshot(), before)
        self.assertFalse(self.backup.exists())

    def test_failed_ini_write_rolls_back_every_file(self):
        (self.game / "dxgi.dll").write_bytes(b"original")
        before = self.snapshot()
        with patch.object(ctl, "_optiscaler_configure_backend", return_value={"ok": False, "msgs": ["fixture failure"]}):
            result = ctl._optiscaler_install(str(self.game), self.cfg(), False, "fsr")
        self.assertFalse(result["ok"])
        self.assertEqual(self.snapshot(), before)
        self.assertFalse(Path(ctl._ymcc_backup_dir(str(self.game))).exists())

    def test_partial_rollback_retains_original_backups(self):
        (self.game / "dxgi.dll").write_bytes(b"precious-original")
        restore = ctl._opti_atomic_restore
        def failed_restore(src, dst, *args, **kwargs):
            if Path(dst) == self.game / "dxgi.dll":
                raise PermissionError("simulate locked destination during rollback")
            return restore(src, dst, *args, **kwargs)
        with patch.object(ctl, "_optiscaler_configure_backend", return_value={"ok": False, "msgs": ["fixture failure"]}), \
             patch.object(ctl, "_opti_atomic_restore", side_effect=failed_restore):
            result = ctl._optiscaler_install(str(self.game), self.cfg(), False, "fsr")
        self.assertFalse(result["ok"])
        pending = Path(ctl._ymcc_backup_dir(str(self.game)) + ".pending")
        self.assertTrue((pending / "manifest.pending.json").exists())
        self.assertTrue(any(p.read_bytes() == b"precious-original" for p in (pending / "files").iterdir()))
        self.assertIn("回滚未完成", result["msgs"][0])
        self.assertIn(str(pending), result["msgs"][0])

    def test_interrupted_pending_backup_is_preserved(self):
        pending = Path(ctl._ymcc_backup_dir(str(self.game)) + ".pending")
        pending.mkdir(parents=True)
        (pending / "valuable-original").write_bytes(b"original")
        result = ctl._optiscaler_install(str(self.game), self.cfg(), False, "fsr")
        self.assertFalse(result["ok"])
        self.assertEqual((pending / "valuable-original").read_bytes(), b"original")
        self.assertEqual(self.snapshot(), {})

    def test_missing_original_backup_refuses_uninstall_without_deleting(self):
        (self.game / "dxgi.dll").write_bytes(b"original")
        cfg = self.cfg()
        self.assertTrue(ctl._optiscaler_install(str(self.game), cfg, False, "fsr")["ok"])
        before = self.snapshot()
        bdir = Path(ctl._ymcc_backup_dir(str(self.game)))
        manifest = json.loads((bdir / "manifest.json").read_text(encoding="utf-8"))
        original = next(x for x in manifest["items"] if x["had_original"])
        (bdir / "files" / original["backup"]).unlink()
        result = ctl._optiscaler_uninstall(str(self.game), cfg, False)
        self.assertFalse(result["ok"])
        self.assertEqual(self.snapshot(), before)
        self.assertTrue((bdir / "manifest.json").exists())

    def test_ini_preserves_comments_custom_keys_and_no_unknown_schema_keys(self):
        template, _ = ctl._optiscaler_template("fsr", {"api": "dx12"}, self.cfg())
        text = "; keep comment\r\n[Upscalers]\r\nDx12Upscaler=auto\r\n[Custom]\r\nKeep=yes\r\n"
        for section, values in template.items():
            text = ctl._ini_set_section_values(text, section, values)
        self.assertIn("; keep comment", text)
        self.assertIn("Keep=yes", text)
        for bad in ("Fsr4ForceModel", "LoadCustomAmdxc64OnRdna2", "FGNvngxReplacement", "ffx_12"):
            self.assertNotIn(bad, text)
        for section, values in template.items():
            self.assertEqual(text, ctl._ini_set_section_values(text, section, values))

    def test_split_fsr_effects_and_current_name_override_exact_slots(self):
        names = ("amd_fidelityfx_upscaler_dx12.dll", "amdxcffx64.dll",
                 "amd_fidelityfx_radiancecache_dx12.dll", "amd_fidelityfx_loader_dx12.dll",
                 "amd_fidelityfx_framegeneration_dx12.dll", "amd_fidelityfx_denoiser_dx12.dll")
        for name in names:
            dll(self.base / name, b"old-sdk-" + name.encode())
            dll(self.extra.parent / name, b"int8-extra-" + name.encode())
        dll(self.extra.parent / "amdxc64.dll", b"unsupported-rdna2-shim")
        dll(self.base / "amdxc64.dll", b"unsupported-base-shim")
        (self.base / "unknown-installer.cmd").write_text("never execute", encoding="utf-8")
        cfg = self.cfg()
        for backend in ("fsr", "xess"):
            with self.subTest(backend=backend):
                plan = {p["rel"].lower(): p for p in ctl._optiscaler_plan(str(self.game), cfg, backend)}
                self.assertEqual(plan["dxgi.dll"]["src"], str(self.base / "OptiScaler.dll"))
                for name in names:
                    self.assertEqual(plan[name]["src"], str(self.extra.parent / name))
                self.assertNotIn("amdxc64.dll", plan)
                self.assertNotIn("unknown-installer.cmd", plan)
                self.assertTrue(ctl._optiscaler_install(str(self.game), cfg, False, backend)["ok"])
                for name in names:
                    self.assertEqual((self.game / name).read_bytes(), (self.extra.parent / name).read_bytes())
                self.assertTrue(ctl._optiscaler_uninstall(str(self.game), cfg, False)["ok"])
                self.assertEqual(self.snapshot(), {})

    def test_optional_effect_alone_cannot_enable_fsr4(self):
        self.extra.unlink()
        dll(self.extra.parent / "amd_fidelityfx_radiancecache_dx12.dll")
        self.assertEqual(self.cfg()["extra_version"], "FSR_4.1.1")

    def test_corrupt_optional_effect_rejects_whole_extra(self):
        (self.extra.parent / "amd_fidelityfx_denoiser_dx12.dll").write_bytes(b"truncated")
        self.assertEqual(self.cfg()["extra_version"], "FSR_4.1.1")

    def test_conflicting_duplicate_extra_name_refuses_candidate(self):
        dll(self.extra.parent / "nested/amd_fidelityfx_upscaler_dx12.dll", b"conflicting")
        self.assertEqual(self.cfg()["extra_version"], "FSR_4.1.1")

    def test_xess_sr_preserves_int8_extra_configuration(self):
        # XeSS is the chosen SR output, not a request to disable the installed INT8 swap.
        template, _ = ctl._optiscaler_template("xess", {"api": "dx12"}, self.cfg())
        self.assertEqual(template["Upscalers"]["Dx11Upscaler"], "xess_12")
        self.assertEqual(template["FSR"]["Fsr4Update"], "true")
        self.assertEqual(template["FSR"]["Fsr4ForceEnableInt8"], "true")
        self.assertEqual(template["FSR"]["Fsr4DoNotLoadAmdxc64"], "auto")
        self.assertEqual(template["FSR"]["FGIndex"], "1")

    def test_nukem_pairing_and_optipatcher_spoofing(self):
        for backend in ("fsr", "xess"):
            template, _ = ctl._optiscaler_template(backend, {"native_fg_input": "nukems"}, self.cfg())
            self.assertEqual(template["FrameGen"], {"Enabled": "true", "FGInput": "nukems", "FGOutput": "nukems"})
            self.assertEqual(template["Spoofing"]["Dxgi"], "true")
            self.assertEqual(template["Spoofing"]["StreamlineSpoofing"], "true")
        cfg = self.cfg()
        cfg["src_patch"] = None
        template, _ = ctl._optiscaler_template("fsr", {"native_fg_input": "nukems"}, cfg)
        self.assertEqual(template["Spoofing"]["Dxgi"], "auto")

    def test_same_hash_untracked_runtime_is_never_deleted(self):
        (self.game / "amd_fidelityfx_upscaler_dx12.dll").write_bytes(self.extra.read_bytes())
        (self.game / "dxgi.dll").write_bytes((self.base / "OptiScaler.dll").read_bytes())
        before = self.snapshot()
        for dry in (True, False):
            result = ctl._optiscaler_uninstall(str(self.game), self.cfg(), dry)
            self.assertFalse(result["ok"])
            self.assertEqual(result["removed"], 0)
            self.assertEqual(self.snapshot(), before)

    def client_fixture(self):
        cdir = self.root / "ClientRecord"
        (cdir / "files").mkdir(parents=True)
        original, installed = b"original-client-game-dll", b"client-installed-dll"
        (cdir / "files/renamed-original.bin").write_bytes(original)
        (self.game / "dxgi.dll").write_bytes(installed)
        (self.game / "OptiScaler.ini").write_bytes(b"settings-adjusted-in-game")
        sha = lambda data: hashlib.sha256(data).hexdigest().upper()
        manifest = {
            "ManifestVersion": 2, "InstalledGameDirectory": str(self.game), "OperationStatus": "committed",
            "InstalledFiles": ["dxgi.dll", "OptiScaler.ini"], "BackedUpFiles": ["DXGI.DLL"],
            "FilesOverwritten": [{"RelativePath": "dxgi.dll", "BackupRelativePath": "renamed-original.bin",
                                  "ExistedBefore": True, "PreInstallSha256": sha(original), "PostInstallSha256": sha(installed)}],
            "FilesCreated": [{"RelativePath": "OptiScaler.ini", "ExistedBefore": False,
                              "PostInstallSha256": sha(b"initial-settings")}],
            "PreInstallKeyFiles": [{"RelativePath": "dxgi.dll", "Existed": True, "Sha256": sha(original)},
                                   {"RelativePath": "OptiScaler.ini", "Existed": False}],
        }
        return cdir, manifest, original

    def test_official_client_manifest_case_hash_and_backup_mapping(self):
        cdir, manifest, original = self.client_fixture()
        before = self.snapshot()
        with patch.object(ctl, "_find_client_backup", return_value=(manifest, str(cdir))):
            result = ctl._optiscaler_uninstall(str(self.game), self.cfg(), True)
            self.assertTrue(result["ok"], result)
            self.assertEqual(self.snapshot(), before)
            result = ctl._optiscaler_uninstall(str(self.game), self.cfg(), False)
            self.assertTrue(result["ok"], result)
            self.assertEqual((self.game / "dxgi.dll").read_bytes(), original)
            self.assertFalse((self.game / "OptiScaler.ini").exists())
            # Do not delete a backup store owned by the client.
            self.assertTrue((cdir / "files/renamed-original.bin").exists())

    def test_client_missing_original_refuses_all_mutation(self):
        cdir, manifest, _ = self.client_fixture()
        (cdir / "files/renamed-original.bin").unlink()
        before = self.snapshot()
        with patch.object(ctl, "_find_client_backup", return_value=(manifest, str(cdir))):
            self.assertFalse(ctl._optiscaler_uninstall(str(self.game), self.cfg(), False)["ok"])
        self.assertEqual(self.snapshot(), before)

    def test_client_updated_binary_is_preserved(self):
        cdir, manifest, _ = self.client_fixture()
        (self.game / "dxgi.dll").write_bytes(b"other-mod-or-game-update")
        before = self.snapshot()
        with patch.object(ctl, "_find_client_backup", return_value=(manifest, str(cdir))):
            self.assertFalse(ctl._optiscaler_uninstall(str(self.game), self.cfg(), False)["ok"])
        self.assertEqual(self.snapshot(), before)

    def test_client_conflicting_snapshot_is_rejected(self):
        cdir, manifest, _ = self.client_fixture()
        manifest["PreInstallKeyFiles"][0]["Existed"] = False
        with self.assertRaises(ValueError):
            ctl._opti_client_restore_plan(str(self.game), str(cdir), manifest)

    def test_client_v1_without_post_hash_cannot_delete_binary(self):
        cdir, manifest, _ = self.client_fixture()
        manifest["FilesOverwritten"] = []
        manifest["FilesCreated"] = []
        with self.assertRaises(ValueError):
            ctl._opti_client_restore_plan(str(self.game), str(cdir), manifest)

    def test_legacy_ymcc_manifest_and_modified_ini_restore(self):
        (self.game / "dxgi.dll").write_bytes(b"old-original")
        cfg = self.cfg()
        self.assertTrue(ctl._optiscaler_install(str(self.game), cfg, False, "fsr")["ok"])
        manifest_file = Path(ctl._ymcc_backup_dir(str(self.game))) / "manifest.json"
        m = json.loads(manifest_file.read_text('utf-8'))
        m.pop("schema_version")
        for item in m["items"]:
            item.pop("installed_sha256")
        manifest_file.write_text(json.dumps(m), encoding='utf-8')
        (self.game / "OptiScaler.ini").write_bytes(b"changed-user-settings")
        result = ctl._optiscaler_uninstall(str(self.game), cfg, False)
        self.assertTrue(result["ok"], result)
        self.assertEqual(self.snapshot(), {"dxgi.dll": b"old-original"})

    def test_pending_recovery_is_retriable_after_failed_rollback(self):
        (self.game / "dxgi.dll").write_bytes(b"valuable-original")
        before = self.snapshot()
        cfg = self.cfg()
        with patch.object(ctl, "_optiscaler_configure_backend", return_value={"ok": False, "msgs": ["fixture"]}), \
             patch.object(ctl, "_opti_atomic_restore", side_effect=PermissionError("locked")):
            self.assertFalse(ctl._optiscaler_install(str(self.game), cfg, False, "fsr")["ok"])
        result = ctl._optiscaler_uninstall(str(self.game), cfg, False)
        self.assertTrue(result["ok"], result)
        self.assertEqual(result["via"], "pending_recovery")
        self.assertEqual(self.snapshot(), before)

    def test_backup_traversal_record_refuses_all_deletion(self):
        cfg = self.cfg()
        self.assertTrue(ctl._optiscaler_install(str(self.game), cfg, False, "fsr")["ok"])
        before = self.snapshot()
        fp = Path(ctl._ymcc_backup_dir(str(self.game))) / "manifest.json"
        m = json.loads(fp.read_text('utf-8'))
        m["items"][-1]["rel"] = "../outside.dll"
        fp.write_text(json.dumps(m), encoding='utf-8')
        self.assertFalse(ctl._optiscaler_uninstall(str(self.game), cfg, False)["ok"])
        self.assertEqual(self.snapshot(), before)

    def test_target_hardlink_is_not_overwritten(self):
        precious = self.root / "original-outside-game"
        precious.write_bytes(b"never-overwrite-this")
        os.link(precious, self.game / "dxgi.dll")
        result = ctl._optiscaler_install(str(self.game), self.cfg(), False, "fsr")
        self.assertFalse(result["ok"])
        self.assertEqual(precious.read_bytes(), b"never-overwrite-this")

    def test_ancestor_junction_is_refused(self):
        # Model lstat's reparse bit without requiring Windows symlink privilege.
        normal = ctl._opti_is_reparse
        ancestor = os.path.normcase(str(self.root))
        with patch.object(ctl, "_opti_is_reparse", side_effect=lambda p: os.path.normcase(str(p)) == ancestor or normal(p)):
            self.assertFalse(ctl._optiscaler_install(str(self.game), self.cfg(), False, "fsr")["ok"])
        self.assertEqual(self.snapshot(), {})

    def test_lock_conflict_is_explicit_and_never_writes(self):
        cfg = self.cfg()
        with ctl._opti_lock(str(self.game)):
            result = ctl._optiscaler_install(str(self.game), cfg, False, "fsr")
        self.assertFalse(result["ok"])
        self.assertIn("事务正在执行", result["msgs"][0])
        self.assertEqual(self.snapshot(), {})

    def test_scan_incomplete_is_refused(self):
        cfg = self.cfg()
        def inaccessible_walk(*args, **kwargs):
            kwargs['onerror'](PermissionError("inaccessible game subtree"))
            return iter(())
        with patch.object(ctl.os, "walk", side_effect=inaccessible_walk):
            result = ctl._optiscaler_install(str(self.game), cfg, False, "fsr")
        self.assertFalse(result["ok"])
        self.assertEqual(self.snapshot(), {})

    def test_nonexistent_game_analyze_always_returns_structured_result(self):
        result = ctl._optiscaler_analyze(str(self.root / 'missing'), self.cfg())
        self.assertFalse(result["ok"])
        self.assertFalse(result["scan_complete"])

    def test_dry_run_rejects_pending_transaction(self):
        pending = Path(ctl._ymcc_backup_dir(str(self.game)) + '.pending')
        pending.mkdir(parents=True)
        self.assertFalse(ctl._optiscaler_install(str(self.game), self.cfg(), True, 'fsr')["ok"])

    def test_restore_post_preflight_binary_change_is_preserved(self):
        cfg = self.cfg()
        self.assertTrue(ctl._optiscaler_install(str(self.game), cfg, False, "fsr")["ok"])
        bdir = Path(ctl._ymcc_backup_dir(str(self.game)))
        m = json.loads((bdir / 'manifest.json').read_text('utf-8'))
        prepared = ctl._opti_ymcc_restore_plan(str(self.game), str(bdir), m)
        (self.game / 'dxgi.dll').write_bytes(b"changed-after-preflight")
        result = ctl._opti_apply_restore(str(self.game), prepared, False)
        self.assertFalse(result["ok"])
        self.assertEqual((self.game / 'dxgi.dll').read_bytes(), b"changed-after-preflight")

    def test_unexpected_error_and_unknown_options_are_json_errors(self):
        for argv in (["install", str(self.game), "--unknown"], ["install", str(self.game), "--backend"]):
            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                self.assertEqual(ctl.optiscaler_cmd(argv), 7)
            self.assertFalse(json.loads(output.getvalue())["ok"])
        output = io.StringIO()
        with patch.object(ctl, '_optiscaler_cfg', side_effect=RuntimeError("fixture unexpected error")), contextlib.redirect_stdout(output):
            self.assertEqual(ctl.optiscaler_cmd(['analyze', str(self.game)]), 7)
        self.assertIn("fixture unexpected error", json.loads(output.getvalue())["msgs"][0])

    def test_ini_missing_final_newline_does_not_merge_keys(self):
        text = ctl._ini_set_section_values("[FSR]\nUpscalerIndex=auto", "FSR", {"FGIndex": "1"})
        ini = configparser.ConfigParser()
        ini.read_string(text)
        self.assertEqual(ini['FSR']['UpscalerIndex'], 'auto')
        self.assertEqual(ini['FSR']['FGIndex'], '1')

    def test_case_duplicate_ini_sections_fall_back(self):
        with (self.base / 'OptiScaler.ini').open('a', encoding='utf-8') as f:
            f.write('[upscalers]\nDx12Upscaler=xess\n')
        self.assertEqual(self.cfg()['opti_version'], '0.9.4')

    def test_unreadable_installed_binary_is_never_deleted(self):
        cfg = self.cfg()
        self.assertTrue(ctl._optiscaler_install(str(self.game), cfg, False, 'fsr')['ok'])
        before = self.snapshot()
        real_hash = ctl._sha256
        def unreadable(path):
            return None if Path(path) == self.game / 'dxgi.dll' else real_hash(path)
        with patch.object(ctl, '_sha256', side_effect=unreadable):
            self.assertFalse(ctl._optiscaler_uninstall(str(self.game), cfg, False)['ok'])
        self.assertEqual(self.snapshot(), before)

    def test_existing_empty_plugin_directory_is_preserved(self):
        (self.game / 'plugins').mkdir()
        cfg = self.cfg()
        self.assertTrue(ctl._optiscaler_install(str(self.game), cfg, False, 'fsr')['ok'])
        self.assertTrue(ctl._optiscaler_uninstall(str(self.game), cfg, False)['ok'])
        self.assertTrue((self.game / 'plugins').is_dir())
        self.assertFalse((self.game / 'D3D12_Optiscaler').exists())

    def test_pending_status_requests_recovery_not_fresh_install(self):
        pending = Path(ctl._ymcc_backup_dir(str(self.game)) + '.pending')
        pending.mkdir(parents=True)
        result = ctl._optiscaler_status(str(self.game), self.cfg())
        self.assertEqual(result['reason'], 'pending_transaction')
        self.assertTrue(result['recovery_required'])

    def test_ui_restores_entry_three_modes_and_no_process_toggle(self):
        for rel in ("src/components/GameQuickActions.vue", "src/views/QuickAppView.vue"):
            text = (ROOT / rel).read_text(encoding="utf-8")
            self.assertIn("FSR4.1/Xess-OPT自动导入", text)
            self.assertIn("OPT 客户端</button>", text)
            self.assertIn("['fsr', 'xess']", text)
            self.assertLess(text.index("if (picked === 'client')"), text.index("const preflight ="))
            for forbidden in ("closeOptiConsole", "optiConsoleRunning", "optiConsoleOn", "startOptiConsolePoll", "onToggleOptiConsole"):
                self.assertNotIn(forbidden, text)
        bridge = (ROOT / "src/bridge/quickapp.ts").read_text(encoding="utf-8")
        self.assertIn("app.powerControlDir()", bridge)
        self.assertIn("OptiscalerClient.exe", bridge)
        self.assertNotIn("terminateTree", bridge)


if __name__ == "__main__":
    unittest.main(verbosity=2)
