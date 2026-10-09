#!/usr/bin/env python3
"""Offline regression checks for YeManTdpCtl family and command routing."""

import importlib.util
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MODULE_PATH = ROOT / "PowerControl" / "pawnio" / "YeManTdpCtl.py"


def load_module():
    spec = importlib.util.spec_from_file_location("yeman_tdp_protocol", MODULE_PATH)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def check_families(module):
    cases = (
        ("AMD Ryzen 7 1800X", 23, 1, "desktop_am4_v1"),
        ("AMD Ryzen 7 2700X", 23, 8, "desktop_am4_v1"),
        ("AMD Ryzen 9 3900X", 23, 113, "desktop_am4_v2"),
        ("AMD Ryzen 7 5800X3D", 25, 33, "desktop_am4_v2"),
        ("AMD portable Ryzen", 26, 68, "apu_ft6_new_generic"),
        ("AMD Ryzen 9 9950X", 26, 68, "desktop"),
        ("AMD Ryzen 9 9955HX", 26, 68, "hx"),
       ("AMD Ryzen 9 7945HX", 25, 97, "hx"),
        ("AMD Ryzen AI 9 HX 370", 26, 68, "hx"),
        ("AMD Ryzen Mobile", 26, 68, "apu_ft6_new_generic"),
       ("AMD Ryzen 7 7840U", 25, 116, "apu_ft6_old"),
        ("Unknown AMD", 26, 127, "unknown"),
    )
    original = module._read_cpu_info
    original_battery = module._has_battery_device
    try:
        for name, family, model, expected in cases:
            ident = f"AMD64 Family {family} Model {model} Stepping 0"
            module._read_cpu_info = lambda n=name, i=ident: (n, i)
            module._has_battery_device = lambda e=(expected == "apu_ft6_new_generic"): e
            actual = module.detect_amd_family()["tag"]
            assert actual == expected, (name, expected, actual)
    finally:
        module._read_cpu_info = original
        module._has_battery_device = original_battery


def capture_route(module, cfg, values):
    calls = []
    original = module._send_mailbox
    module._send_mailbox = lambda p, cmd, val, c, transport, tries=300: (
        calls.append((transport, cmd, val)) or 1
    )
    try:
        rc = module.amd_set_with(object(), cfg, *values)
    finally:
        module._send_mailbox = original
    assert rc == 0, (cfg["name"], rc)
    return calls


def check_amd_routes(module):
    assert capture_route(module, module.FAM_DESKTOP_AM4_V1, (40000, 41000, 42000)) == [
        ("mp1", 0x31, 40000), ("rsmu", 0x64, 40000)
    ]
    assert capture_route(module, module.FAM_DESKTOP_AM4_V2, (40000, 41000, 42000)) == [
        ("mp1", 0x3D, 40000), ("rsmu", 0x53, 40000)
    ]
    assert capture_route(module, module.FAM_DESKTOP_AM5, (40000, 41000, 42000)) == [
        ("mp1", 0x4F, 40000), ("mp1", 0x3E, 41000), ("mp1", 0x5F, 42000)
    ]
    assert capture_route(module, module.FAM_APU_FT6_NEW_GENERIC, (40000, 41000, 42000)) == [
        ("mp1", 0x14, 40000), ("rsmu", 0x31, 40000),
        ("mp1", 0x15, 41000), ("rsmu", 0x32, 41000),
        ("mp1", 0x16, 42000), ("rsmu", 0x33, 42000),
    ]


def check_intel_encoding(module):
    class Backend:
        _tx_depth = 1
        msr = object()

        @staticmethod
        def read_msr_raw(msr):
            assert msr == module.MSR_RAPL_POWER_UNIT
            return 3  # 1 / 2^3 = 0.125 W per unit

    old = 0x002A1234007F5678
    value, pl1, pl2, unit = module._intel_power_limit_value(Backend(), 40, 41, old)
    target_mask = 0xFFFF | (0xFFFF << 32)
    assert (value & ~target_mask) == (old & ~target_mask)
    assert (value & 0x7FFF) == 320
    assert ((value >> 32) & 0x7FFF) == 328
    assert (pl1, pl2, unit) == (40, 41, 0.125)


def check_daemon_client_allowlist(module):
    parent = str(Path(__file__).resolve().parents[1] / "native" / "testrun" / "sleep_cycles_current" / "YeManCC-sleep-cycles.exe")
    allowed = module._daemon_allowed_client_paths(parent)
    assert module._canonical_exe_path(parent) in allowed
    assert module._canonical_exe_path(r"C:\arbitrary\other-host.exe") not in allowed
    assert len(allowed) == len(module.DAEMON_ALLOWED_CLIENTS) + 1
    module.DAEMON_PARENT_CLIENT_IMAGE = module._canonical_exe_path(parent)
    assert module._canonical_exe_path(parent) in module._daemon_allowed_client_paths()
    module.DAEMON_PARENT_CLIENT_IMAGE = ""


def main():
    module = load_module()
    check_families(module)
    check_amd_routes(module)
    check_intel_encoding(module)
    check_daemon_client_allowlist(module)
    print("tdp protocol self-test: PASS")


if __name__ == "__main__":
    main()
