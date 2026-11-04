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
# The narrow ledger mode compiles only original Opti functions. It never imports
# the hardware module or executes top-level controller initialization.
import ast
import sys
import types
import builtins
import ntpath
import struct
import re
import time
import argparse
import shutil

LEDGER_MODE = "--ledger-storm" in sys.argv
LEDGER_PINS = []
LEDGER_CASES = []
LEDGER_PORT = None
LEDGER_OUT = None


class LedgerPorts:
    def __init__(self, root):
        self.root = Path(root).resolve()
        self.case = self.root
        self.events = []
        self.fd_paths = {}
        self.deny_read = self.fail_replace = self.silent_replace = self.fail_remove = None
        self.short_write = self.fail_fsync = False
        self.after_replace = self.before_replace = self.after_fsync = None
        self.environ = {"APPDATA": str(self.root / "Roaming"), "LOCALAPPDATA": str(self.root / "Local")}
        port = self

        class PathPort:
            def __getattr__(self, name):
                original = getattr(os.path, name)
                if name in ("exists", "lexists", "isfile", "isdir", "getsize", "islink"):
                    def bound(path):
                        port.guard(path, metadata=name in ("islink",))
                        return original(path)
                    return bound
                return original

        class OsPort:
            path = PathPort()
            name = os.name
            environ = port.environ
            def __getattr__(self, name):
                if name == "replace": return port.replace
                if name in ("remove", "unlink"):
                    def remove(path):
                        port.guard(path)
                        if port.fail_remove and port.fail_remove(Path(path)):
                            raise PermissionError("injected remove refusal")
                        port.events.append({"api": "remove", "path": str(path), "nativeCalled": True})
                        return os.remove(path)
                    return remove
                if name == "fsync":
                    def sync(fd):
                        os.fsync(fd)
                        port.events.append({"api": "fsync", "nativeCalled": True, "injectedAckFailure": bool(port.fail_fsync)})
                        if port.after_fsync: port.after_fsync()
                        if port.fail_fsync: raise OSError("injected fsync acknowledgement failure")
                    return sync
                if name == "fdopen":
                    return lambda fd, *args, **kwargs: port.wrap(os.fdopen(fd, *args, **kwargs), port.fd_paths[fd])
                original = getattr(os, name)
                if name in ("lstat", "stat", "scandir", "listdir", "walk", "makedirs", "mkdir", "rmdir"):
                    def bound(path, *args, **kwargs):
                        port.guard(path, metadata=name in ("lstat", "stat"))
                        return original(path, *args, **kwargs)
                    return bound
                if name in ("close", "fstat", "fsdecode", "fspath", "sep", "pardir", "curdir"):
                    return original
                raise AssertionError("Unexpected OS capability " + name)

        class ShutilPort:
            def copy2(self, source, destination, **kwargs):
                port.guard(source); port.guard(destination)
                port.events.append({"api": "copy2", "source": str(source), "path": str(destination), "nativeCalled": True})
                return shutil.copy2(source, destination, **kwargs)
            def rmtree(self, path, **kwargs):
                port.guard(path)
                assert Path(path).resolve() != port.case
                port.events.append({"api": "rmtree", "path": str(path), "nativeCalled": True})
                return shutil.rmtree(path, **kwargs)

        class TempPort:
            def mkstemp(self, *args, **kwargs):
                port.guard(kwargs.get("dir"))
                fd, path = tempfile.mkstemp(*args, **kwargs)
                port.fd_paths[fd] = path
                port.events.append({"api": "mkstemp", "path": path, "nativeCalled": True})
                return fd, path
        self.os, self.shutil, self.tempfile = OsPort(), ShutilPort(), TempPort()

    def guard(self, path, metadata=False):
        assert path is not None, "No implicit temporary directory"
        p = Path(path).absolute()
        owned = p.is_relative_to(self.case)
        ancestor = self.case.is_relative_to(p)
        if not owned and not (metadata and ancestor):
            self.events.append({"api": "fence", "path": str(p), "nativeCalled": False})
            raise AssertionError("Unowned ledger path " + str(p))
        return p

    def wrap(self, handle, path):
        port = self
        class FilePort:
            def __enter__(self): return self
            def __exit__(self, *args): return handle.__exit__(*args)
            def __getattr__(self, name): return getattr(handle, name)
            def write(self, data):
                payload = data[:max(1, len(data)//2)] if port.short_write else data
                used = handle.write(payload)
                port.events.append({"api": "write", "path": str(path), "requested": len(data), "transferred": used,
                                    "nativeCalled": True, "injected": bool(port.short_write)})
                return used
        return FilePort()

    def open(self, path, *args, **kwargs):
        p = self.guard(path)
        mode = args[0] if args else kwargs.get("mode", "r")
        if self.deny_read and "r" in mode and self.deny_read(p):
            self.events.append({"api": "open", "path": str(path), "nativeCalled": False, "injected": True})
            raise PermissionError("injected read sharing refusal")
        self.events.append({"api": "open", "path": str(path), "mode": mode, "nativeCalled": True})
        return self.wrap(builtins.open(path, *args, **kwargs), p)

    def replace(self, source, destination):
        self.guard(source); p = self.guard(destination)
        if self.before_replace: self.before_replace(Path(source), p)
        if self.fail_replace and self.fail_replace(p):
            self.events.append({"api": "replace", "path": str(p), "nativeCalled": False, "injected": True})
            raise PermissionError("injected atomic replace refusal")
        if self.silent_replace and self.silent_replace(p):
            self.events.append({"api": "replace", "path": str(p), "nativeCalled": False, "silentAck": True})
            return
        self.events.append({"api": "replace", "path": str(p), "nativeCalled": True})
        os.replace(source, destination)
        if self.after_replace: self.after_replace(p)


def ledger_extract(source, out, ports):
    text = source.read_text(encoding="utf-8-sig")
    tree = ast.parse(text, filename=str(source))
    explicit = {"_sha256", "_write_json_file", "_safe_join", "_ini_set_section_values", "_canonical_game_dir", "optiscaler_cmd"}
    selected = [node for node in tree.body if
                (isinstance(node, ast.FunctionDef) and (node.name in explicit or node.name.startswith(("_opti", "_ymcc_backup", "_find_client_backup", "_find_ymcc_backup")))) or
                (isinstance(node, ast.Assign) and isinstance(node.targets[0], ast.Name) and node.targets[0].id.startswith("OPTI_"))]
    module = types.ModuleType("ymcc_opti_actual_ast")
    safe_builtins = dict(vars(builtins), open=ports.open)
    def imports(name, *args, **kwargs):
        if name not in ("msvcrt", "fcntl"):
            raise AssertionError("Unapproved import in extracted ledger function " + name)
        return builtins.__import__(name, *args, **kwargs)
    safe_builtins["__import__"] = imports
    module.__dict__.update({"__builtins__": safe_builtins, "__file__": str(source), "os": ports.os,
                           "shutil": ports.shutil, "tempfile": ports.tempfile, "json": json,
                           "hashlib": hashlib, "time": time, "ntpath": ntpath, "struct": struct,
                           "configparser": configparser, "re": re, "sys": types.SimpleNamespace(executable=str(out / "fake-python.exe")),
                           "contextmanager": contextlib.contextmanager, "_olog": lambda *a: None})
    for node in selected:
        body = ast.get_source_segment(text, node)
        LEDGER_PINS.append({"name": getattr(node, "name", getattr(node.targets[0], "id", "") if isinstance(node, ast.Assign) else ""),
                            "line": node.lineno, "endLine": node.end_lineno,
                            "sha256": hashlib.sha256(body.encode("utf-8")).hexdigest()})
    assert not any(isinstance(node, (ast.Import, ast.ImportFrom, ast.Expr, ast.If, ast.Try)) for node in selected)
    extracted = ast.fix_missing_locations(ast.Module(body=selected, type_ignores=[]))
    (out / "actual-opti-functions.py").write_text(ast.unparse(extracted), encoding="utf-8")
    exec(compile(extracted, str(source), "exec"), module.__dict__)
    return module


if LEDGER_MODE:
    LEDGER_OUT = Path(os.environ["YMCC_OPTI_LEDGER_OUT"]).resolve()
    LEDGER_OUT.mkdir(parents=True, exist_ok=False)
    (LEDGER_OUT / "owned").mkdir()
    LEDGER_PORT = LedgerPorts(LEDGER_OUT / "owned")
    LEDGER_SOURCE = Path(os.environ.get("YMCC_OPTI_LEDGER_SOURCE", ROOT / "PowerControl/pawnio/YeManTdpCtl.py")).resolve()
    ctl = ledger_extract(LEDGER_SOURCE, LEDGER_OUT, LEDGER_PORT)
else:
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
        if LEDGER_MODE:
            case = LEDGER_OUT / "owned" / ("case-" + str(len(LEDGER_CASES) + 1))
            case.mkdir()
            self.temp = types.SimpleNamespace(name=str(case), cleanup=lambda: None)
            LEDGER_PORT.case = case
            LEDGER_PORT.events = []
            LEDGER_PORT.deny_read = LEDGER_PORT.fail_replace = LEDGER_PORT.silent_replace = LEDGER_PORT.fail_remove = None
            LEDGER_PORT.short_write = LEDGER_PORT.fail_fsync = False
            LEDGER_PORT.after_replace = LEDGER_PORT.before_replace = LEDGER_PORT.after_fsync = None
            LEDGER_PORT.environ.update(APPDATA=str(case / "Roaming"), LOCALAPPDATA=str(case / "Local"))
        else:
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

    def test_anticheat_analysis_reports_warning_not_safety(self):
        (self.game / "start_protected_game.exe").write_bytes(b"protected")
        result = ctl._optiscaler_analyze(str(self.game), self.cfg())
        self.assertTrue(result["ok"])
        self.assertTrue(result["anti_cheat"])
        self.assertTrue(result["requires_anti_cheat_confirmation"])
        self.assertTrue(result["scan_complete"])

    def test_anticheat_explicit_ack_deploys_without_bypass_and_restores(self):
        (self.game / "start_protected_game.exe").write_bytes(b"protected")
        (self.game / "Game.exe").write_bytes(b"original-game")
        (self.game / "steam_appid.txt").write_bytes(b"existing-id\r\n")
        before = self.snapshot()
        cfg = self.cfg()
        result = ctl._optiscaler_install(str(self.game), cfg, False, "fsr", acknowledge_anticheat=True)
        self.assertTrue(result["ok"], result)
        self.assertTrue(result["anti_cheat"])
        self.assertTrue(any("未执行绕过" in warning for warning in result["warnings"]))
        manifest = json.loads((Path(result["backup"]) / "manifest.json").read_text(encoding="utf-8"))
        self.assertTrue(manifest["anti_cheat_risk_acknowledged"])
        self.assertTrue(manifest["analysis"]["anti_cheat"])
        for rel, content in before.items():
            self.assertEqual((self.game / rel).read_bytes(), content)
        self.assertTrue(ctl._optiscaler_uninstall(str(self.game), cfg, False)["ok"])
        self.assertEqual(self.snapshot(), before)

    def test_anticheat_ack_requires_boolean_true(self):
        (self.game / "start_protected_game.exe").write_bytes(b"protected")
        before = self.snapshot()
        for value in (False, 1, "true", None):
            result = ctl._optiscaler_install(str(self.game), self.cfg(), False, "fsr", acknowledge_anticheat=value)
            self.assertFalse(result["ok"])
            self.assertEqual(self.snapshot(), before)
        self.assertFalse(self.backup.exists())

    def test_anticheat_ack_does_not_override_incomplete_scan(self):
        (self.game / "start_protected_game.exe").write_bytes(b"protected")
        before = self.snapshot()
        features = ctl._optiscaler_game_features(str(self.game))
        features["scan_complete"] = False
        with patch.object(ctl, "_optiscaler_game_features", return_value=features):
            result = ctl._optiscaler_install(str(self.game), self.cfg(), False, "fsr", acknowledge_anticheat=True)
        self.assertFalse(result["ok"])
        self.assertEqual(self.snapshot(), before)
        self.assertFalse(self.backup.exists())

    def test_anticheat_ack_dry_run_never_writes(self):
        (self.game / "start_protected_game.exe").write_bytes(b"protected")
        before = self.snapshot()
        result = ctl._optiscaler_install(str(self.game), self.cfg(), True, "xess", acknowledge_anticheat=True)
        self.assertTrue(result["ok"], result)
        self.assertEqual(self.snapshot(), before)
        self.assertFalse(self.backup.exists())

    def test_anticheat_cli_ack_is_install_only_and_never_creates_appid(self):
        (self.game / "start_protected_game.exe").write_bytes(b"protected")
        before = self.snapshot()
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            rc = ctl.optiscaler_cmd(["install", str(self.game), "--backend", "fsr", "--acknowledge-anticheat-risk", "--dry-run"])
        self.assertEqual(rc, 0, output.getvalue())
        self.assertTrue(json.loads(output.getvalue())["ok"])
        self.assertEqual(self.snapshot(), before)
        self.assertFalse((self.game / "steam_appid.txt").exists())
        for sub in ("analyze", "status", "uninstall"):
            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                self.assertEqual(ctl.optiscaler_cmd([sub, str(self.game), "--acknowledge-anticheat-risk"]), 7)
            self.assertFalse(json.loads(output.getvalue())["ok"])

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
        (cdir / "files").mkdir(parents=True, exist_ok=True)
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
        (cdir / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
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

    def ledger_seed(self, recovery=False):
        original, installed = b"ledger-original", b"ledger-installed"
        (self.game / "dxgi.dll").write_bytes(installed)
        root = Path(ctl._ymcc_backup_dir(str(self.game)) + (".pending" if recovery else ""))
        (root / "files").mkdir(parents=True, exist_ok=True)
        (root / "files/original.bin").write_bytes(original)
        item = {"rel": "dxgi.dll", "had_original": True, "backup": "original.bin",
                "original_sha256": hashlib.sha256(original).hexdigest(),
                "source_sha256": hashlib.sha256(installed).hexdigest(),
                "installed_sha256": hashlib.sha256(installed).hexdigest(), "touched": True}
        manifest = {"schema_version": 2, "game_dir": str(self.game), "state": "pending" if recovery else "committed",
                    "prepared": True, "items": [item], "created_directories": [], "FutureNote": {"keep": True}}
        file = root / ("manifest.pending.json" if recovery else "manifest.json")
        file.write_text(json.dumps(manifest), encoding="utf-8")
        return root, file, manifest

    @unittest.skipUnless(LEDGER_MODE, "requires --ledger-storm owned AST mode")
    def test_ledger_bad_json_original_and_target_are_retained(self):
        for bad in (b"", b"{", b"[]", b"null", b'{"items":null}', b'{"state":"bad","state":"committed"}'):
            with self.subTest(bad=bad):
                _, file, _ = self.ledger_seed()
                file.write_bytes(bad)
                before = self.snapshot()
                result = ctl._optiscaler_uninstall(str(self.game), self.cfg(), False)
                self.assertFalse(result["ok"], result)
                self.assertEqual(file.read_bytes(), bad)
                self.assertEqual(self.snapshot(), before)

    @unittest.skipUnless(LEDGER_MODE, "requires --ledger-storm owned AST mode")
    def test_ledger_duplicate_and_escaped_keys_never_authorize_restore(self):
        for escaped in (False, True):
            with self.subTest(escaped=escaped):
                _, file, manifest = self.ledger_seed()
                key = r'"\u0073tate"' if escaped else '"state"'
                bad = json.dumps(manifest)[:-1] + ', ' + key + ':"committed"}'
                file.write_text(bad, encoding="utf-8")
                before = self.snapshot()
                self.assertFalse(ctl._optiscaler_uninstall(str(self.game), self.cfg(), False)["ok"])
                self.assertEqual(self.snapshot(), before)
                self.assertEqual(file.read_text("utf-8"), bad)

    @unittest.skipUnless(LEDGER_MODE, "requires --ledger-storm owned AST mode")
    def test_ledger_future_and_wrong_schema_types_preserve_debt(self):
        for schema in (999, True, "2", None, 2.0):
            with self.subTest(schema=schema):
                _, file, manifest = self.ledger_seed()
                manifest["schema_version"] = schema
                file.write_text(json.dumps(manifest), encoding="utf-8")
                before = self.snapshot()
                self.assertFalse(ctl._optiscaler_uninstall(str(self.game), self.cfg(), False)["ok"])
                self.assertEqual(self.snapshot(), before)
                self.assertTrue(file.exists())

    @unittest.skipUnless(LEDGER_MODE, "requires --ledger-storm owned AST mode")
    def test_ledger_missing_original_hash_cannot_borrow_backup_identity(self):
        _, file, manifest = self.ledger_seed()
        manifest["items"][0].pop("original_sha256")
        file.write_text(json.dumps(manifest), encoding="utf-8")
        before = self.snapshot()
        self.assertFalse(ctl._optiscaler_uninstall(str(self.game), self.cfg(), False)["ok"])
        self.assertEqual(self.snapshot(), before)

    @unittest.skipUnless(LEDGER_MODE, "requires --ledger-storm owned AST mode")
    def test_ledger_unknown_owner_and_wrong_target_cannot_be_defaulted(self):
        for changes in ({"owner": "foreign"}, {"game_dir": str(self.root / "Elsewhere")}, {"game_dir": []}):
            with self.subTest(changes=changes):
                _, file, manifest = self.ledger_seed()
                manifest.update(changes)
                file.write_text(json.dumps(manifest), encoding="utf-8")
                before = self.snapshot()
                self.assertFalse(ctl._optiscaler_uninstall(str(self.game), self.cfg(), False)["ok"])
                self.assertEqual(self.snapshot(), before)
                self.assertTrue(file.exists())

    @unittest.skipUnless(LEDGER_MODE, "requires --ledger-storm owned AST mode")
    def test_ledger_pending_touched_must_be_a_real_boolean(self):
        root, file, manifest = self.ledger_seed(recovery=True)
        manifest["items"][0]["touched"] = "false"
        file.write_text(json.dumps(manifest), encoding="utf-8")
        before = self.snapshot()
        self.assertFalse(ctl._optiscaler_uninstall(str(self.game), self.cfg(), False)["ok"])
        self.assertEqual(self.snapshot(), before)
        self.assertTrue(root.exists())

    @unittest.skipUnless(LEDGER_MODE, "requires --ledger-storm owned AST mode")
    def test_ledger_client_requires_explicit_status_supported_version_and_owner(self):
        for changes in ({"OperationStatus": None}, {"ManifestVersion": 999}, {"ManifestVersion": True}, {"Owner": "foreign"}):
            with self.subTest(changes=changes):
                cdir, manifest, _ = self.client_fixture()
                manifest.update(changes)
                if changes.get("OperationStatus", "sentinel") is None: manifest.pop("OperationStatus")
                before = self.snapshot()
                with self.assertRaises(ValueError):
                    ctl._opti_client_restore_plan(str(self.game), str(cdir), manifest)
                self.assertEqual(self.snapshot(), before)

    @unittest.skipUnless(LEDGER_MODE, "requires --ledger-storm owned AST mode")
    def test_ledger_client_conflicting_target_and_duplicate_lists_are_unknown(self):
        for change in ("target", "duplicate", "original-hash"):
            with self.subTest(change=change):
                cdir, manifest, _ = self.client_fixture()
                if change == "target": manifest["GameDirectory"] = str(self.root / "Other")
                if change == "duplicate": manifest["InstalledFiles"].append("DXGI.DLL")
                if change == "original-hash":
                    manifest["FilesOverwritten"][0].pop("PreInstallSha256")
                    manifest["PreInstallKeyFiles"][0].pop("Sha256")
                with self.assertRaises(ValueError):
                    ctl._opti_client_restore_plan(str(self.game), str(cdir), manifest)

    @unittest.skipUnless(LEDGER_MODE, "requires --ledger-storm owned AST mode")
    def test_ledger_json_limit_and_strict_numeric_contract(self):
        file = self.root / "manifest.json"
        for text in ('{"v":' + '['*66 + '0' + ']'*66 + '}', '{"v":NaN}', '{"v":Infinity}', '{"v":1e999}', '{"v":"' + 'x'*(4*1024*1024) + '"}'):
            with self.subTest(prefix=text[:30]):
                file.write_text(text, encoding="utf-8")
                with self.assertRaises((ValueError, OSError)):
                    ctl._opti_load_json(str(file))
                self.assertEqual(file.read_text("utf-8"), text)

    @unittest.skipUnless(LEDGER_MODE, "requires --ledger-storm owned AST mode")
    def test_ledger_json_silent_publish_is_not_a_saved_receipt_and_retries(self):
        file = self.root / "manifest.json"
        file.write_text('{"Keep":1}', encoding="utf-8")
        LEDGER_PORT.silent_replace = lambda p: p == file
        with self.assertRaises((OSError, ValueError)):
            ctl._write_json_file(str(file), {"Keep":2})
        self.assertEqual(json.loads(file.read_text("utf-8")), {"Keep":1})
        LEDGER_PORT.silent_replace = None
        ctl._write_json_file(str(file), {"Keep":2})
        self.assertEqual(ctl._opti_load_json(str(file)), {"Keep":2})

    @unittest.skipUnless(LEDGER_MODE, "requires --ledger-storm owned AST mode")
    def test_ledger_json_short_write_and_flush_failure_do_not_publish(self):
        file = self.root / "manifest.json"
        for mode in ("short", "flush"):
            with self.subTest(mode=mode):
                file.write_text('{"Keep":1}', encoding="utf-8")
                LEDGER_PORT.short_write = mode == "short"
                LEDGER_PORT.fail_fsync = mode == "flush"
                with self.assertRaises((OSError, ValueError)):
                    ctl._write_json_file(str(file), {"Keep":2})
                self.assertEqual(file.read_text("utf-8"), '{"Keep":1}')
                LEDGER_PORT.short_write = LEDGER_PORT.fail_fsync = False
                ctl._write_json_file(str(file), {"Keep":2})

    @unittest.skipUnless(LEDGER_MODE, "requires --ledger-storm owned AST mode")
    def test_ledger_json_failed_replace_and_readback_denial_retry(self):
        file = self.root / "manifest.json"
        file.write_text('{"Keep":1}', encoding="utf-8")
        LEDGER_PORT.fail_replace = lambda p: p == file
        with self.assertRaises((OSError, ValueError)):
            ctl._write_json_file(str(file), {"Keep":2})
        self.assertEqual(file.read_text("utf-8"), '{"Keep":1}')
        LEDGER_PORT.fail_replace = None
        LEDGER_PORT.after_replace = lambda p: setattr(LEDGER_PORT, "deny_read", lambda q: q == file)
        with self.assertRaises((OSError, ValueError)):
            ctl._write_json_file(str(file), {"Keep":2})
        LEDGER_PORT.after_replace = LEDGER_PORT.deny_read = None
        ctl._write_json_file(str(file), {"Keep":2})

    @unittest.skipUnless(LEDGER_MODE, "requires --ledger-storm owned AST mode")
    def test_ledger_json_external_change_during_flush_is_preserved(self):
        file = self.root / "manifest.json"
        file.write_text('{"Keep":1}', encoding="utf-8")
        LEDGER_PORT.after_fsync = lambda: file.write_text('{"External":9}', encoding="utf-8")
        with self.assertRaises((OSError, ValueError)):
            ctl._write_json_file(str(file), {"Keep":2})
        self.assertEqual(json.loads(file.read_text("utf-8")), {"External":9})
        LEDGER_PORT.after_fsync = None

    @unittest.skipUnless(LEDGER_MODE, "requires --ledger-storm owned AST mode")
    def test_ledger_json_future_and_corrupt_preimage_are_not_overwritten(self):
        file = self.root / "manifest.json"
        for original in ('{bad', '{"schema_version":999,"Debt":"KEEP"}'):
            with self.subTest(original=original):
                file.write_text(original, encoding="utf-8")
                with self.assertRaises((OSError, ValueError)):
                    ctl._write_json_file(str(file), {"schema_version":2,"items":[]})
                self.assertEqual(file.read_text("utf-8"), original)

    @unittest.skipUnless(LEDGER_MODE, "requires --ledger-storm owned AST mode")
    def test_ledger_ini_keeps_custom_fields_and_requires_final_readback(self):
        path = self.game / "OptiScaler.ini"
        original = '; keep\n[Upscalers]\nDx12Upscaler=auto\n[FrameGen]\nEnabled=auto\n[Custom]\nFuture=KEEP\n'
        path.write_text(original, encoding="utf-8")
        LEDGER_PORT.silent_replace = lambda p: p == path
        result = ctl._optiscaler_configure_backend(str(self.game), "fsr", {"api":"dx12"}, self.cfg())
        self.assertFalse(result["ok"], result)
        self.assertEqual(path.read_text("utf-8"), original)
        LEDGER_PORT.silent_replace = None
        result = ctl._optiscaler_configure_backend(str(self.game), "fsr", {"api":"dx12"}, self.cfg())
        self.assertTrue(result["ok"], result)
        self.assertIn('Future=KEEP', path.read_text("utf-8"))
        self.assertIn('; keep', path.read_text("utf-8"))

    @unittest.skipUnless(LEDGER_MODE, "requires --ledger-storm owned AST mode")
    def test_ledger_ini_empty_duplicate_and_growth_limit_are_refused(self):
        path = self.root / "OptiScaler.ini"
        for content in ('', '[Upscalers]\nx=1\nx=2\n[FrameGen]\nEnabled=true', '[Upscalers]\nx='+'x'*(1<<20)+'\n[FrameGen]\nEnabled=true'):
            with self.subTest(prefix=content[:30]):
                path.write_text(content, encoding="utf-8")
                with patch.object(ctl.os.path, "getsize", return_value=0):
                    with self.assertRaises((ValueError, OSError, configparser.Error)):
                        ctl._opti_read_ini(str(path))
                self.assertEqual(path.read_text("utf-8"), content)

    @unittest.skipUnless(LEDGER_MODE, "requires --ledger-storm owned AST mode")
    def test_ledger_restore_detects_target_change_during_copy(self):
        root, _, manifest = self.ledger_seed()
        source = root / "files/original.bin"
        target = self.game / "dxgi.dll"
        copy = ctl.shutil.copy2
        def race(src, dst, **kwargs):
            result = copy(src, dst, **kwargs)
            if str(dst).endswith('.tmp'): target.write_bytes(b'EXTERNAL-NEW-ORIGINAL')
            return result
        with patch.object(ctl.shutil, "copy2", side_effect=race):
            with self.assertRaises((ValueError, OSError)):
                ctl._opti_atomic_restore(str(source), str(target), manifest["items"][0]["original_sha256"])
        self.assertEqual(target.read_bytes(), b'EXTERNAL-NEW-ORIGINAL')
        self.assertTrue(source.exists())

    @unittest.skipUnless(LEDGER_MODE, "requires --ledger-storm owned AST mode")
    def test_ledger_clear_failure_retains_debt_then_explicit_restore_retries(self):
        root, file, _ = self.ledger_seed()
        remove = ctl.shutil.rmtree
        with patch.object(ctl.shutil, "rmtree", side_effect=PermissionError("owned cleanup denied")):
            self.assertFalse(ctl._optiscaler_uninstall(str(self.game), self.cfg(), False)["ok"])
        self.assertTrue(file.exists())
        self.assertEqual((self.game / 'dxgi.dll').read_bytes(), b'ledger-original')
        self.assertTrue(ctl._optiscaler_uninstall(str(self.game), self.cfg(), False)["ok"])
        self.assertFalse(root.exists())

    @unittest.skipUnless(LEDGER_MODE, "requires --ledger-storm owned AST mode")
    def test_ledger_install_manifest_and_directory_publish_require_receipts(self):
        for target in ("manifest", "directory"):
            with self.subTest(target=target):
                game = self.root / ("Game-"+target)
                game.mkdir()
                (game / 'dxgi.dll').write_bytes(b'ORIGINAL')
                before = {p.name:p.read_bytes() for p in game.iterdir()}
                bdir = Path(ctl._ymcc_backup_dir(str(game)))
                LEDGER_PORT.silent_replace = (lambda p: p.name == 'manifest.json') if target == "manifest" else (lambda p: p == bdir)
                result = ctl._optiscaler_install(str(game), self.cfg(), False, "fsr")
                self.assertFalse(result["ok"], result)
                self.assertEqual({p.name:p.read_bytes() for p in game.iterdir() if p.is_file()}, before)
                LEDGER_PORT.silent_replace = None

    @unittest.skipUnless(LEDGER_MODE, "requires --ledger-storm owned AST mode")
    def test_ledger_actual_owned_win32_sharing_denial_and_release(self):
        if os.name != 'nt': self.skipTest('Win32 only')
        import ctypes
        from ctypes import wintypes
        _, file, _ = self.ledger_seed()
        kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        kernel.CreateFileW.argtypes = [wintypes.LPCWSTR,wintypes.DWORD,wintypes.DWORD,ctypes.c_void_p,wintypes.DWORD,wintypes.DWORD,wintypes.HANDLE]
        kernel.CreateFileW.restype = wintypes.HANDLE
        kernel.CloseHandle.argtypes = [wintypes.HANDLE]
        LEDGER_PORT.guard(file)
        handle = kernel.CreateFileW(str(file),0x80000000,0,None,3,0x80,None)
        self.assertNotEqual(handle, ctypes.c_void_p(-1).value)
        try:
            ctypes.set_last_error(0)
            observer = kernel.CreateFileW(str(file),0x80000000,7,None,3,0x80,None)
            winerror = ctypes.get_last_error()
            if observer != ctypes.c_void_p(-1).value: kernel.CloseHandle(observer)
            self.assertEqual(observer, ctypes.c_void_p(-1).value)
            self.assertEqual(winerror, 32)
            with self.assertRaises(OSError) as caught: ctl._opti_load_json(str(file))
            self.assertEqual(caught.exception.errno, 13)
            LEDGER_PORT.events.append({"api":"actual-Win32-share-denial","winerror":winerror,
                                       "pythonErrno":caught.exception.errno,"nativeCalled":True})
        finally: self.assertTrue(kernel.CloseHandle(handle))
        self.assertEqual(ctl._opti_load_json(str(file))["schema_version"],2)

    @unittest.skipUnless(LEDGER_MODE, "requires --ledger-storm owned AST mode")
    def test_ledger_writer_refuses_malformed_or_duplicate_existing_debt(self):
        _, file, manifest = self.ledger_seed()
        for change in ("type", "item-type", "duplicate", "case-alias", "bool", "hash"):
            with self.subTest(change=change):
                previous = json.loads(json.dumps(manifest))
                if change == "type": previous["items"] = {}
                if change == "item-type": previous["items"].append(None)
                if change in ("duplicate", "case-alias"):
                    alias = dict(previous["items"][0])
                    if change == "case-alias": alias["rel"] = alias["rel"].upper()
                    previous["items"].append(alias)
                if change == "bool": previous["items"][0]["had_original"] = "false"
                if change == "hash": previous["items"][0].pop("original_sha256")
                raw = json.dumps(previous).encode()
                file.write_bytes(raw)
                with self.assertRaises((OSError, ValueError)):
                    ctl._write_json_file(str(file), json.loads(json.dumps(manifest)))
                self.assertEqual(file.read_bytes(), raw)
        # A successful write keeps legal future/user fields at both levels.
        previous = json.loads(json.dumps(manifest))
        previous['items'][0]['FutureItem'] = {'user': 'KEEP'}
        file.write_text(json.dumps(previous), encoding='utf-8')
        candidate = json.loads(json.dumps(manifest))
        candidate.pop('FutureNote')
        ctl._write_json_file(str(file), candidate)
        saved = ctl._opti_load_json(str(file))
        self.assertEqual(saved.get('FutureNote'), previous['FutureNote'])
        self.assertEqual(saved['items'][0].get('FutureItem'), {'user': 'KEEP'})
        self.assertEqual(candidate, saved)

    @unittest.skipUnless(LEDGER_MODE, "requires --ledger-storm owned AST mode")
    def test_ledger_install_rollback_keeps_externally_replaced_pending(self):
        for raw in (b'{bad', b'{"schema_version":999,"Debt":"KEEP"}', b'{"schema_version":2,"owner":"foreign"}'):
            with self.subTest(raw=raw):
                game = self.root / ("Game-foreign-" + str(len(list(self.root.glob('Game-foreign-*')))))
                game.mkdir()
                (game / 'dxgi.dll').write_bytes(b'ORIGINAL')
                pending = Path(ctl._ymcc_backup_dir(str(game)) + '.pending')
                ledger = pending / 'manifest.pending.json'
                original_writer = ctl._write_json_file
                writes = []
                def write_then_external(path, value):
                    original_writer(path, value)
                    writes.append(path)
                    if len(writes) == 2:
                        ledger.write_bytes(raw)
                        raise OSError('external replacement after preparation acknowledgement')
                with patch.object(ctl, '_write_json_file', side_effect=write_then_external):
                    result = ctl._optiscaler_install(str(game), self.cfg(), False, 'fsr')
                self.assertFalse(result['ok'], result)
                self.assertTrue(ledger.exists(), 'failed install cleared unknown replacement ledger')
                self.assertEqual(ledger.read_bytes(), raw)
                self.assertTrue(any(p.read_bytes() == b'ORIGINAL' for p in (pending / 'files').iterdir()))
                self.assertEqual((game / 'dxgi.dll').read_bytes(), b'ORIGINAL')

    @unittest.skipUnless(LEDGER_MODE, "requires --ledger-storm owned AST mode")
    def test_ledger_empty_preparation_read_denial_releases_for_explicit_retry(self):
        (self.game / 'dxgi.dll').write_bytes(b'ORIGINAL')
        pending = Path(ctl._ymcc_backup_dir(str(self.game)) + '.pending')
        ledger = pending / 'manifest.pending.json'
        def deny_receipt(path):
            if path == ledger: LEDGER_PORT.deny_read = lambda p: p == ledger
        LEDGER_PORT.after_replace = deny_receipt
        result = ctl._optiscaler_install(str(self.game), self.cfg(), False, 'fsr')
        self.assertFalse(result['ok'], result)
        self.assertTrue(ledger.exists())
        self.assertEqual((self.game / 'dxgi.dll').read_bytes(), b'ORIGINAL')
        LEDGER_PORT.after_replace = LEDGER_PORT.deny_read = None
        self.assertTrue(ctl._optiscaler_install(str(self.game), self.cfg(), False, 'fsr')['ok'])
        self.assertTrue(ctl._optiscaler_uninstall(str(self.game), self.cfg(), False)['ok'])
        self.assertEqual((self.game / 'dxgi.dll').read_bytes(), b'ORIGINAL')

    @unittest.skipUnless(LEDGER_MODE, "requires --ledger-storm owned AST mode")
    def test_ledger_client_named_unknown_slot_blocks_install(self):
        with patch.object(ctl, '_find_client_backup', self.patches[2].temp_original):
            for kind in ('missing', 'target-type', 'wrong-target', 'conflicting-target'):
                with self.subTest(kind=kind):
                    game = self.root / ('Game-unknown-' + kind)
                    game.mkdir()
                    (game / 'dxgi.dll').write_bytes(b'CLIENT-TARGET')
                    key = hashlib.sha256(ctl._canonical_game_dir(str(game)).encode()).hexdigest()[:8]
                    slot = Path(LEDGER_PORT.environ['APPDATA']) / 'OptiscalerClient' / 'Backups' / ('owned_' + key)
                    (slot / 'files').mkdir(parents=True)
                    (slot / 'files' / 'debt-original').write_bytes(b'EXTERNAL-DEBT')
                    file = slot / 'manifest.json'
                    if kind != 'missing':
                        data = {'ManifestVersion':2,'InstalledGameDirectory':str(game),'OperationStatus':'committed'}
                        if kind == 'target-type': data['InstalledGameDirectory'] = []
                        if kind == 'wrong-target': data['InstalledGameDirectory'] = str(self.root / 'Other')
                        if kind == 'conflicting-target': data['GameDirectory'] = str(self.root / 'Other')
                        file.write_text(json.dumps(data),encoding='utf-8')
                    before = {p.name:p.read_bytes() for p in game.iterdir()}
                    slot_before = {str(p.relative_to(slot)):p.read_bytes() for p in slot.rglob('*') if p.is_file()}
                    self.assertFalse(ctl._optiscaler_install(str(game), self.cfg(), False, 'fsr')['ok'])
                    self.assertEqual({p.name:p.read_bytes() for p in game.iterdir()}, before)
                    self.assertEqual({str(p.relative_to(slot)):p.read_bytes() for p in slot.rglob('*') if p.is_file()}, slot_before)

    @unittest.skipUnless(LEDGER_MODE, "requires --ledger-storm owned AST mode")
    def test_ledger_legacy_directory_without_manifest_keeps_original_debt(self):
        (self.game / 'dxgi.dll').write_bytes(b'ORIGINAL')
        supplied = str(self.game).replace(chr(92), '/')
        key = hashlib.md5(supplied.lower().encode('utf-8')).hexdigest()[:12]
        legacy = self.backup / key
        self.assertNotEqual(legacy, Path(ctl._ymcc_backup_dir(supplied)))
        (legacy / 'files').mkdir(parents=True)
        (legacy / 'files' / 'original-debt').write_bytes(b'KNOWN-ORIGINAL')
        before = self.snapshot()
        self.assertFalse(ctl._optiscaler_install(supplied, self.cfg(), False, 'fsr')['ok'])
        self.assertEqual(self.snapshot(), before)
        self.assertEqual((legacy / 'files' / 'original-debt').read_bytes(), b'KNOWN-ORIGINAL')

    def ledger_physical_client(self):
        cdir, manifest, original = self.client_fixture()
        key = hashlib.sha256(ctl._canonical_game_dir(str(self.game)).encode()).hexdigest()[:8]
        actual = Path(LEDGER_PORT.environ['APPDATA']) / 'OptiscalerClient' / 'Backups' / ('owned_' + key)
        actual.parent.mkdir(parents=True, exist_ok=True)
        cdir.rename(actual)
        return actual, manifest, original

    @unittest.skipUnless(LEDGER_MODE, "requires --ledger-storm owned AST mode")
    def test_ledger_client_actual_lookup_denial_release_restore_keeps_external_store(self):
        cdir, manifest, original = self.ledger_physical_client()
        file = cdir / 'manifest.json'
        saved = {str(p.relative_to(cdir)):p.read_bytes() for p in cdir.rglob('*') if p.is_file()}
        game_before = self.snapshot()
        with patch.object(ctl, '_find_client_backup', self.patches[2].temp_original):
            for bad in (b'{bad', json.dumps(dict(manifest, ManifestVersion=999)).encode(), json.dumps(dict(manifest, Owner='foreign')).encode()):
                with self.subTest(bad=bad[:40]):
                    file.write_bytes(bad)
                    self.assertFalse(ctl._optiscaler_uninstall(str(self.game), self.cfg(), False)['ok'])
                    self.assertEqual(self.snapshot(), game_before)
                    self.assertEqual(file.read_bytes(), bad)
            file.write_bytes(saved['manifest.json'])
            LEDGER_PORT.deny_read = lambda p: p == file
            self.assertFalse(ctl._optiscaler_uninstall(str(self.game), self.cfg(), False)['ok'])
            self.assertEqual(self.snapshot(), game_before)
            LEDGER_PORT.deny_read = None
            self.assertTrue(ctl._optiscaler_uninstall(str(self.game), self.cfg(), False)['ok'])
        self.assertEqual((self.game / 'dxgi.dll').read_bytes(), original)
        self.assertEqual({str(p.relative_to(cdir)):p.read_bytes() for p in cdir.rglob('*') if p.is_file()}, saved)

    @unittest.skipUnless(LEDGER_MODE, "requires --ledger-storm owned AST mode")
    def test_ledger_client_late_manifest_change_never_authorizes_restore(self):
        cdir, _, _ = self.ledger_physical_client()
        file = cdir / 'manifest.json'
        before = self.snapshot()
        plan = ctl._opti_client_restore_plan
        def changed(*args):
            result = plan(*args)
            file.write_bytes(b'{"ManifestVersion":999,"Debt":"KEEP"}')
            return result
        with patch.object(ctl, '_find_client_backup', self.patches[2].temp_original), patch.object(ctl, '_opti_client_restore_plan', side_effect=changed):
            self.assertFalse(ctl._optiscaler_uninstall(str(self.game), self.cfg(), False)['ok'])
        self.assertEqual(self.snapshot(), before)
        self.assertEqual(file.read_bytes(), b'{"ManifestVersion":999,"Debt":"KEEP"}')

    @unittest.skipUnless(LEDGER_MODE, "requires --ledger-storm owned AST mode")
    def test_ledger_multiple_ymcc_owners_are_unconfirmed(self):
        root, _, _ = self.ledger_seed()
        duplicate = self.backup / 'legacy-other-owner'
        shutil.copytree(root, duplicate)
        before = self.snapshot()
        self.assertFalse(ctl._optiscaler_uninstall(str(self.game), self.cfg(), False)['ok'])
        self.assertEqual(self.snapshot(), before)
        self.assertTrue((root / 'manifest.json').exists())
        self.assertTrue((duplicate / 'manifest.json').exists())

    @unittest.skipUnless(LEDGER_MODE, "requires --ledger-storm owned AST mode")
    def test_ledger_rollback_delete_ack_failure_retains_pending_and_retries(self):
        (self.game / 'dxgi.dll').write_bytes(b'ORIGINAL')
        pending = Path(ctl._ymcc_backup_dir(str(self.game)) + '.pending')
        remove = ctl.os.remove
        def silent(path):
            if Path(path) != self.game / 'libxess.dll': return remove(path)
            LEDGER_PORT.events.append({'api':'remove','path':str(path),'nativeCalled':False,'silentAck':True})
        with patch.object(ctl, '_optiscaler_configure_backend', return_value={'ok':False,'msgs':['injected configuration failure']}), patch.object(ctl.os, 'remove', side_effect=silent):
            self.assertFalse(ctl._optiscaler_install(str(self.game), self.cfg(), False, 'fsr')['ok'])
        self.assertTrue((self.game / 'libxess.dll').exists())
        self.assertTrue((pending / 'manifest.pending.json').exists(), 'unconfirmed deletion cleared recovery debt')
        self.assertTrue(ctl._optiscaler_uninstall(str(self.game), self.cfg(), False)['ok'])
        self.assertFalse((self.game / 'libxess.dll').exists())
        self.assertFalse(pending.exists())
        self.assertEqual((self.game / 'dxgi.dll').read_bytes(), b'ORIGINAL')

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
    if LEDGER_MODE:
        import platform
        names = [name for name in dir(OptiScalerTests) if name.startswith("test_ledger_")]
        names += ["test_both_modes_install_and_exact_uninstall_roundtrip", "test_failed_ini_write_rolls_back_every_file",
                  "test_partial_rollback_retains_original_backups", "test_pending_recovery_is_retriable_after_failed_rollback",
                  "test_missing_original_backup_refuses_uninstall_without_deleting", "test_legacy_ymcc_manifest_and_modified_ini_restore",
                  "test_official_client_manifest_case_hash_and_backup_mapping", "test_client_missing_original_refuses_all_mutation",
                  "test_client_updated_binary_is_preserved", "test_backup_traversal_record_refuses_all_deletion",
                  "test_restore_post_preflight_binary_change_is_preserved", "test_lock_conflict_is_explicit_and_never_writes"]
        if os.environ.get("YMCC_OPTI_LEDGER_CASE"):
            names = [name for name in names if name == os.environ["YMCC_OPTI_LEDGER_CASE"]]
        assert names
        source_before = LEDGER_SOURCE.read_bytes()
        test_before = Path(__file__).read_bytes()
        class ReceiptResult(unittest.TextTestResult):
            def startTest(self, test):
                super().startTest(test)
                self.case_failed = False
            def addFailure(self, test, err): self.case_failed = True; super().addFailure(test, err)
            def addError(self, test, err): self.case_failed = True; super().addError(test, err)
            def addSubTest(self, test, subtest, err):
                if err: self.case_failed = True
                super().addSubTest(test, subtest, err)
            def stopTest(self, test):
                files = [{"path": str(p.relative_to(LEDGER_PORT.case)), "bytes": p.stat().st_size,
                          "sha256": hashlib.sha256(p.read_bytes()).hexdigest()}
                         for p in LEDGER_PORT.case.rglob("*") if p.is_file()]
                LEDGER_CASES.append({"name": test._testMethodName, "pass": not self.case_failed,
                                     "ownedRoot": str(LEDGER_PORT.case), "trace": list(LEDGER_PORT.events), "files": files})
                super().stopTest(test)
        suite = unittest.TestSuite(OptiScalerTests(name) for name in names)
        result = unittest.TextTestRunner(verbosity=2, resultclass=ReceiptResult).run(suite)
        changed = source_before != LEDGER_SOURCE.read_bytes() or test_before != Path(__file__).read_bytes()
        runtime_modules = []
        for module_name, module in sorted(sys.modules.copy().items()):
            for role in ('__file__', '__cached__'):
                path = getattr(module, role, None)
                if isinstance(path, str) and Path(path).is_file():
                    actual = Path(path).resolve()
                    runtime_modules.append({'module': module_name, 'role': role, 'path': str(actual),
                                            'sha256': hashlib.sha256(actual.read_bytes()).hexdigest()})
        receipt = {"pass": result.wasSuccessful() and not changed, "selection": "original optiscaler consumer --ledger-storm",
                   "cases": len(LEDGER_CASES), "failed": sum(not x["pass"] for x in LEDGER_CASES), "receipts": LEDGER_CASES,
                   "source": str(LEDGER_SOURCE), "sourceSha256": hashlib.sha256(source_before).hexdigest(),
                   "consumerSha256": hashlib.sha256(test_before).hexdigest(), "functionPins": LEDGER_PINS,
                   "sourceDrift": changed, "runtimeModules": runtime_modules,
                   "wholeScriptImported": False, "realHardwareProcessRegistryNetworkActions": 0,
                   "python": {"path": sys.executable, "version": platform.python_version(),
                              "sha256": hashlib.sha256((Path(sys.base_prefix) / "python.exe").read_bytes()).hexdigest()}}
        (LEDGER_OUT / "RESULT.json").write_text(json.dumps(receipt, ensure_ascii=False, indent=2), encoding="utf-8")
        sys.exit(0 if receipt["pass"] else 1)
    else:
        unittest.main(verbosity=2)
