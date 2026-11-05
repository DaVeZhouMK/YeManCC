#!/usr/bin/env python3
"""Offline regression checks for YeManTdpCtl family and command routing."""

import contextlib
import importlib.util
import io
import json
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
        # Handheld priority: actual CPUID drives routing, never model year/name alone.
        ("AMD Ryzen 5 4500U", 0x17, 0x60, "apu_fp6"),
        ("AMD Ryzen 7 4800U", 0x17, 0x60, "apu_fp6"),
        ("AMD Ryzen 5 5500U", 0x17, 0x68, "apu_fp6"),
        ("AMD Ryzen 5 5560U", 0x19, 0x50, "apu_fp6"),
        ("AMD Ryzen 7 5800U", 0x19, 0x50, "apu_fp6"),
        ("AMD Ryzen 7 5825U", 0x19, 0x50, "apu_fp6"),
        ("AMD Ryzen 3 7320U", 0x17, 0xA0, "apu_ft6_old"),
        ("AMD Ryzen 5 7520U", 0x17, 0xA0, "apu_ft6_old"),
        ("AMD Ryzen 5 6600U", 0x19, 0x44, "apu_ft6_old"),
        ("AMD Ryzen 7 6800U", 0x19, 0x44, "apu_ft6_old"),
        ("AMD Ryzen 7 6800U", 0x19, 0x3F, "apu_ft6_old"),
        ("AMD Ryzen 5 7640U", 0x19, 0x74, "apu_ft6_old"),
        ("AMD Ryzen 5 7640U", 0x19, 0x78, "apu_ft6_old"),
        ("AMD Ryzen 7 7840U", 0x19, 0x74, "apu_ft6_old"),
        ("AMD Ryzen 5 8640U", 0x19, 0x7C, "apu_ft6_old"),
        ("AMD Ryzen 7 8840U", 0x19, 0x75, "apu_ft6_old"),
        ("AMD Ryzen Z1 Extreme", 0x19, 0x74, "apu_ft6_old"),
        ("AMD RYZEN AI Z2 EXTREME", 0x1A, 0x20, "apu_ft6_new_generic"),
        ("AMD RYZEN AI Z2 EXTREME", 0x1A, 0x24, "apu_ft6_new_generic"),
        ("AMD RYZEN AI Z2 EXTREME", 0, 0, "unknown"),
        ("AMD Custom APU", 0x17, 0x90, "apu_ff3"),
        ("AMD Ryzen 7 7840U", 0x19, 0x7F, "unknown"),
        ("AMD Ryzen 7 7840U", 0, 0, "unknown"),
        ("AMD Ryzen 7 1800X", 23, 1, "desktop_am4_v1"),
        ("AMD Ryzen 7 2700X", 23, 8, "desktop_am4_v1"),
        ("AMD Ryzen 9 3900X", 23, 113, "desktop_am4_v2"),
        ("AMD Ryzen 7 5800X3D", 25, 33, "desktop_am4_v2"),
        ("AMD portable Ryzen", 26, 68, "desktop"),
        ("AMD Ryzen 9 9950X", 26, 68, "desktop"),
        ("AMD Ryzen 9 9955HX", 26, 68, "hx"),
       ("AMD Ryzen 9 7945HX", 25, 97, "hx"),
        ("AMD Ryzen AI 9 HX 370", 26, 68, "hx"),
        ("AMD Ryzen Mobile", 26, 68, "desktop"),
       ("AMD Ryzen 7 7840U", 25, 116, "apu_ft6_old"),
        ("Unknown AMD", 26, 127, "unknown"),
        ("AMD RYZEN AI MAX+ 388", 26, 112, "apu_ft6_new"),
        ("AMD RYZEN AI MAX+ 395", 26, 112, "apu_ft6_new"),
        ("Unknown AMD marketing name", 26, 112, "apu_ft6_new"),
    )
    cases += tuple(("AMD future CPU", 26, model, "hsmp_fp10_am5")
                   for model in (*range(128, 144), *range(224, 228)))
    original = module._read_cpu_info
    original_battery = module._has_battery_device
    try:
        for name, family, model, expected in cases:
            ident = f"AMD64 Family {family} Model {model} Stepping 0"
            module._read_cpu_info = lambda n=name, i=ident: (n, i)
            module._has_battery_device = lambda e=(expected == "apu_ft6_new_generic"): e
            actual = module.detect_amd_family()["tag"]
            assert actual == expected, (name, expected, actual)
        # Cover every concrete upstream CPU rule with both battery states; battery is never queried.
        for identity in module.AMD_CPU_IDENTITIES:
            names = identity.get("nameContainsAny", [])
            name = "AMD CPU " + (names[0] if names else "")
            for model in identity["models"]:
                module._read_cpu_info = lambda n=name, f=identity["family"], m=model: (n, f"AMD64 Family {f} Model {m} Stepping 0")
                def reject_battery_probe():
                    raise AssertionError("CPU capability must not probe battery or use DMI admission")
                module._has_battery_device = reject_battery_probe
                assert module.detect_amd_family() is getattr(module, identity["recipe"]), identity
    finally:
        module._read_cpu_info = original
        module._has_battery_device = original_battery


    # Intel model names remain diagnostics; known/unknown CPU models use the
    # same source-declared candidates and still need their runtime MSR gates.
    saved = module._read_cpu_info
    try:
        for family, model, label in [(6, 0x3C, "Haswell"), (6, 0x5E, "Skylake"),
                                     (6, 0xB7, "Raptor"), (6, 0xC6, "Arrow"),
                                     (6, 0xFE, "Unknown future")]:
            reads = []
            def cpu_read(f=family, m=model, n=label):
                reads.append(1)
                return "Intel " + n, f"Intel64 Family {f} Model {m} Stepping 0"
            module._read_cpu_info = cpu_read
            cap = module.detect_intel_capability()
            assert cap["rapl_supported"] and cap["uv_core"] and cap["uv_cache"], (label, cap)
            assert not cap["uv_igpu"] and not cap["uv_sa"] and not cap["modelAdmission"]
            assert cap["identityVerified"] and len(reads) == 1
        for name, identifier in [("Intel 356H", ""), ("AMD CPU", "AMD64 Family 26 Model 68 Stepping 0"), ("Unknown CPU", "x86 Family 6 Model 94 Stepping 0")]:
            module._read_cpu_info = lambda n=name, i=identifier: (n, i)
            cap = module.detect_intel_capability()
            assert not cap["rapl_supported"] and not cap["uv_core"] and not cap["uv_cache"]
    finally:
        module._read_cpu_info = saved

def capture_route(module, cfg, values):
    calls = []
    original = module._send_mailbox
    module._send_mailbox = lambda p, cmd, val, c, transport, tries=300, diagnostics=None: (
        calls.append((transport, cmd, val)) or 1
    )
    try:
        rc = module.amd_set_with(object(), cfg, *values)
    finally:
        module._send_mailbox = original
    assert rc == 0, (cfg["name"], rc)
    return calls


def check_amd_routes(module):
    check_strix_halo_pm_compatibility(module)
    assert capture_route(module, module.FAM_DESKTOP_AM4_V1, (40000, 41000, 42000)) == [
        ("mp1", 0x31, 40000), ("rsmu", 0x64, 40000)
    ]
    assert capture_route(module, module.FAM_DESKTOP_AM4_V2, (40000, 41000, 42000)) == [
        ("mp1", 0x3D, 40000), ("rsmu", 0x53, 40000)
    ]
    assert capture_route(module, module.FAM_DESKTOP_AM5, (40000, 41000, 42000)) == [
        ("mp1", 0x4F, 40000), ("mp1", 0x3E, 41000), ("mp1", 0x5F, 42000)
    ]
    # Preserve the known working AM5 path. Source-only desktop 0xCB is deferred.
    # The archive's HX early-return branch also has no desktop-only 0xCB mirror.
    assert capture_route(module, module.FAM_DRAGON_FIRE, (40000, 41000, 42000)) == [
        ("mp1", 0x4F, 40000), ("mp1", 0x3E, 41000), ("mp1", 0x5F, 42000)
    ]
    assert capture_route(module, module.FAM_APU_FT6_NEW_GENERIC, (40000, 41000, 42000)) == [
        ("mp1", 0x14, 40000), ("rsmu", 0x31, 40000),
        ("mp1", 0x15, 41000), ("rsmu", 0x32, 41000),
        ("mp1", 0x16, 42000), ("rsmu", 0x33, 42000),
    ]

    # UXTU 26.3.1 declares only STAPM on the RSMU side of these sockets.
    # FAST/SLOW remain MP1 operations; a different socket's command numbers
    # must never become additional writes for these families.
    assert capture_route(module, module.FAM_APU_FP6, (15000, 16000, 17000)) == [
        ("mp1", 0x14, 15000), ("rsmu", 0x31, 15000),
        ("mp1", 0x15, 16000), ("mp1", 0x16, 17000),
    ]
    assert capture_route(module, module.FAM_APU_FT5_FP5, (15000, 16000, 17000)) == [
        ("mp1", 0x1A, 15000), ("rsmu", 0x2E, 15000),
        ("mp1", 0x1B, 16000), ("mp1", 0x1C, 17000),
    ]
    assert capture_route(module, module.FAM_APU_FT6_OLD, (15000, 16000, 17000)) == [
        ("mp1", 0x14, 15000), ("rsmu", 0x31, 15000),
        ("mp1", 0x15, 16000), ("rsmu", 0x32, 16000),
        ("mp1", 0x16, 17000), ("rsmu", 0x33, 17000),
    ]
    assert capture_route(module, module.FAM_APU_VANGOGH, (15000, 16000, 17000)) == [
        ("mp1", 0x14, 15000), ("rsmu", 0x31, 15000),
        ("mp1", 0x15, 16000), ("mp1", 0x16, 17000),
    ]
    # UXTU26.3.1 FP10/AM5 has a distinct HSMP mailbox, never an MP1 alias.
    assert capture_route(module, module.FAM_HSMP_FP10_AM5, (15000, 16000, 17000)) == [
        ("hsmp", 0x15, 15000), ("hsmp", 0x09, 16000), ("hsmp", 0x16, 17000)]
    assert module._resolve_mailbox_addr(module.FAM_HSMP_FP10_AM5, "hsmp") == (
        0x03B10A8C, 0x03B10A90, 0x03B10A94)
    assert module._resolve_mailbox_addr(module.FAM_HSMP_FP10_AM5, "mp1") == (None, None, None)
    # HSMP stops on its first rejected command and never replays a partial request.
    original_hsmp = module._send_mailbox
    hsmp_expected = [("hsmp", 0x15, 15000), ("hsmp", 0x09, 16000), ("hsmp", 0x16, 17000)]
    try:
        for failure in range(3):
            hsmp_calls = []
            def send_hsmp(p, cmd, value, cfg, transport, tries=300, diagnostics=None):
                hsmp_calls.append((transport, cmd, value))
                return 0xFC if len(hsmp_calls) - 1 == failure else 1
            module._send_mailbox = send_hsmp
            assert module.amd_set_with(object(), module.FAM_HSMP_FP10_AM5, 15000, 16000, 17000) != 0
            assert hsmp_calls == hsmp_expected[:failure + 1]
    finally:
        module._send_mailbox = original_hsmp

    # Each generated source set starts with the unchanged legacy primary.
    for cfg in module._AMD_FAMILIES:
        if cfg.get("co_supported"):
            assert cfg["co_commands"] and cfg["co_commands"][0] == {
                "transport": cfg["co_transport"], "command": cfg["coall"]}, cfg["name"]
        else:
            assert cfg["co_commands"] == [], cfg["name"]
    source_candidates = [{"transport": "mp1", "command": 0x4C},
                         {"transport": "rsmu", "command": 0x5D}]
    original_cpu = module._read_cpu_info
    try:
        for name, model in (("AMD Ryzen Z2 Extreme", 36),
                            ("AMD Ryzen AI 9 HX 370", 32),
                            ("AMD Ryzen AI 9 HX 370", 36)):
            module._read_cpu_info = lambda n=name, m=model: (
                n, f"AMD64 Family 26 Model {m} Stepping 0")
            cfg = module.detect_amd_family()
            assert cfg["co_commands"] == source_candidates, (name, model, cfg)
            assert (cfg["co_transport"], cfg["coall"]) == ("mp1", 0x4C)
    finally:
        module._read_cpu_info = original_cpu
    assert (module.FAM_DESKTOP_AM5["co_transport"], module.FAM_DESKTOP_AM5["coall"]) == ("mp1", 0x36)
    assert module.FAM_DESKTOP_AM5["co_commands"][0] == {"transport": "mp1", "command": 0x36}

    # Every upstream-declared all-core CPU route uses exactly its declared transport.
    original_send = module._send_mailbox
    try:
        for cfg in module._AMD_FAMILIES:
            calls = []
            module._send_mailbox = lambda p, cmd, value, c, transport, tries=300, diagnostics=None: (calls.append((transport, cmd, value)) or 1)
            rc = module.amd_uv_set_with(object(), cfg, -1)
            if cfg.get("co_supported"):
                assert rc == 0 and calls == [(cfg["co_transport"], cfg["coall"], 0xFFFF if cfg["co_transport"] == "hsmp" else 0xFFFFF)], (cfg["name"], calls)
                module._send_mailbox = lambda *args, **kwargs: (calls.append("failure") or 0xFC)
                assert module.amd_uv_set_with(object(), cfg, -1) != 0
                assert calls[-1] == "failure" and calls.count("failure") == 1, "No replay or cross-transport fallback"
            else:
                assert rc != 0 and calls == []
        # Fixed upstream actual manual all-core producers encode signed20;
        # HSMP translator then narrows to signed16. Keep the legacy u32 utility.
        assert module._co_arg_encode(-1, {"co_enc": "u32"}) == 0xFFFFFFFF
        assert module._co_arg_encode(-1, module.FAM_DESKTOP_AM5) == 0xFFFFF
        for cfg in module._AMD_FAMILIES:
            if not cfg.get("co_supported"):
                continue
            assert cfg["co_enc"] == ("u16" if cfg["co_transport"] == "hsmp" else "u20")
            for offset in range(module.UV_AMD_MIN, module.UV_AMD_MAX + 1):
                # Independent source formula from MainWindow EncodeCurveOptimiserOffset
                # and CustomPresets desktop --set-coall producer (pinned in policy).
                expected = offset if offset >= 0 else 0x100000 - (-offset)
                if cfg["co_transport"] == "hsmp":
                    expected &= 0xFFFF
                calls = []
                module._send_mailbox = lambda p, cmd, value, c, transport, tries=300, diagnostics=None: (calls.append((transport, cmd, value)) or 1)
                assert module.amd_uv_set_with(object(), cfg, offset) == 0
                assert calls == [(cfg["co_transport"], cfg["coall"], expected)], (cfg["name"], offset, calls, expected)
        assert module._co_arg_encode(-1, module.FAM_APU_FT6_NEW) == 0xFFFFF
        assert module._co_arg_encode(-1, module.FAM_HSMP_FP10_AM5) == 0xFFFF
        assert module._co_arg_encode(1, module.FAM_HSMP_FP10_AM5) == 1
    finally:
        module._send_mailbox = original_send

    # An untyped FE must stop, even after a genuine UNKNOWN_CMD on the primary.
    cfg = module.FAM_APU_FT6_NEW_GENERIC
    original_send = module._send_mailbox
    try:
        for unknown_first in (False, True):
            for missing_origin in (False, True):
                calls, containers, diagnostic = [], [], {}
                def untyped_send(p, cmd, value, c, transport, tries=300, diagnostics=None):
                    assert diagnostics == {}, "every candidate owns fresh typed evidence"
                    assert all(diagnostics is not previous for previous in containers)
                    containers.append(diagnostics)
                    calls.append((transport, cmd, value))
                    if unknown_first and len(calls) == 1:
                        module._amd_uv_failure(diagnostics, "firmware_response", "response", mailbox_status=0xFE)
                    elif missing_origin:
                        diagnostics["failure"] = {"stage": "response", "helperExitCode": 6, "mailboxStatus": 0xFE}
                    return 0xFE
                module._send_mailbox = untyped_send
                assert module.amd_uv_set_with(object(), cfg, -20, diagnostic) == 6
                assert calls == [(route["transport"], route["command"], 0xFFFEC)
                                 for route in cfg["co_commands"][:2 if unknown_first else 1]]
                assert diagnostic["attempts"][-1]["origin"] is None
                assert diagnostic["attempts"][-1]["mailboxStatus"] == (0xFE if missing_origin else None)
                assert diagnostic.get("failure", {}).get("origin") is None
        # Missing co_commands means exactly one legacy primary; explicit empty
        # or invalid declarations must not silently restore that write permission.
        legacy = dict(cfg)
        legacy.pop("co_commands")
        calls = []
        def legacy_send(p, cmd, value, c, transport, tries=300, diagnostics=None):
            calls.append((transport, cmd, value))
            module._amd_uv_failure(diagnostics, "firmware_response", "response", mailbox_status=0xFE)
            return 0xFE
        module._send_mailbox = legacy_send
        assert module.amd_uv_set_with(object(), legacy, -20) == 6
        assert calls == [(cfg["co_transport"], cfg["coall"], 0xFFFEC)]
        for candidates in ([], None, [cfg["co_commands"][1]],
                           [{"transport": "mp1", "command": "76"}]):
            calls.clear()
            assert module.amd_uv_set_with(object(), dict(cfg, co_commands=candidates), -20) == 6
            assert calls == []
    finally:
        module._send_mailbox = original_send

    # Every retained MP1/mirror failure must still fail the whole request.
    original = module._send_mailbox
    try:
        for cfg in (module.FAM_DESKTOP_AM5, module.FAM_DRAGON_FIRE,
                    module.FAM_APU_FP6, module.FAM_APU_FT5_FP5,
                    module.FAM_APU_FT6_OLD, module.FAM_APU_VANGOGH):
            expected = capture_route(module, cfg, (15000, 16000, 17000))
            for failed_step in range(len(expected)):
                calls = []
                def send(p, cmd, val, c, transport, tries=300, diagnostics=None):
                    calls.append((transport, cmd, val))
                    return 0 if len(calls) - 1 == failed_step else 1
                module._send_mailbox = send
                rc = module.amd_set_with(object(), cfg, 15000, 16000, 17000)
                assert rc != 0, (cfg["tag"], failed_step, rc)
                assert calls == expected, (cfg["tag"], failed_step, calls)
                module._send_mailbox = original
    finally:
        module._send_mailbox = original


    # Exercise the real mailbox flow through fake PawnIO register IO. The same
    # legacy status byte can be a firmware response OR a locally made timeout.
    cfg = module.FAM_APU_FT6_NEW_GENERIC
    msg, rsp, arg = module._resolve_mailbox_addr(cfg, "mp1")
    secondary_msg, secondary_rsp, secondary_arg = module._resolve_mailbox_addr(cfg, "rsmu")
    addresses = {"mp1": (msg, rsp, arg), "rsmu": (secondary_msg, secondary_rsp, secondary_arg)}
    class MailboxPort:
        def __init__(self, case, status=1, secondary=None):
            self.case, self.status = case, status
            self.secondary = secondary or (case, status)
            self.sent = set()
            self.calls = []
        def execute(self, name, values, out_count):
            self.calls.append((name, tuple(values), out_count))
            reg = values[0]
            for transport, (route_msg, route_rsp, route_arg) in addresses.items():
                if reg in (route_msg, route_rsp) or route_arg <= reg <= route_arg + 20:
                    break
            else:
                raise AssertionError((name, reg))
            case, status = (self.case, self.status) if transport == "mp1" else self.secondary
            sent = transport in self.sent
            if name == "ioctl_read_smu_register":
                if transport == "mp1":
                    assert reg == rsp
                assert reg == route_rsp
                if case == ("response_io" if sent else "ready_io"):
                    return 0x80070005, []
                if case == ("response_timeout" if sent else "ready_timeout"):
                    return 0, [0]
                return 0, [status if sent else 1]
            assert name == "ioctl_write_smu_register"
            failed = {"clear_io": route_rsp, "argument_io": route_arg,
                      "argument_tail_io": route_arg + 8, "command_io": route_msg}
            if failed.get(case) == reg:
                return 0x80070005, []
            if reg == route_msg:
                self.sent.add(transport)
            return 0, []
    saved_sleep = module.time.sleep
    try:
        module.time.sleep = lambda _: None
        diagnostics = {}
        cases = [("firmware", status, "firmware_response", "response", status)
                 for status in (0xFE, 0xFD, 0xFC, 0xFF, 6)]
        cases += [
            ("ready_timeout", 1, "ready_wait", "mailbox_ready", None),
            ("response_timeout", 1, "response_wait", "mailbox_response", None),
            ("ready_io", 1, "transport", "ready_read", None),
            ("clear_io", 1, "transport", "clear_response", None),
            ("argument_io", 1, "transport", "argument_write", None),
            ("argument_tail_io", 1, "transport", "argument_write", None),
            ("command_io", 1, "transport", "command_write", None),
            ("response_io", 1, "transport", "response_read", None),
            ("firmware", 0x100, "transport", "invalid_response", None),
        ]
        for case, status, origin, stage, mailbox_status in cases:
            backend = MailboxPort(case, status)
            assert module.amd_uv_set_with(backend, cfg, -8, diagnostics) == 6
            assert diagnostics["failure"] == {
                "origin": origin, "stage": stage, "helperExitCode": 6,
                "mailboxStatus": mailbox_status}, (case, diagnostics)
            commands = [row for row in backend.calls
                        if row[0] == "ioctl_write_smu_register" and row[1][0] == msg]
            assert len(commands) <= 1, "diagnostics must not replay a command"
            if origin == "firmware_response":
                assert commands == [("ioctl_write_smu_register", (msg, cfg["coall"]), 0)]
            retry_statuses = module.UV_POLICY["amd"]["candidateSelection"]["retryOnly"]["mailboxStatuses"]
            assert retry_statuses == [0xFE, 0xFF]
            expected_routes = cfg["co_commands"] if origin == "firmware_response" and status in retry_statuses else cfg["co_commands"][:1]
            assert diagnostics["attempts"] == [dict(route, origin=origin, mailboxStatus=mailbox_status, accepted=False)
                                               for route in expected_routes], (case, diagnostics)
            assert diagnostics["route"] == expected_routes[-1]
            secondary_calls = [row for row in backend.calls if row[1][0] in
                               (secondary_msg, secondary_rsp, *range(secondary_arg, secondary_arg + 24, 4))]
            if len(expected_routes) == 1:
                assert secondary_calls == [], (case, secondary_calls)
        # The same value is preserved across the declared candidates after a
        # genuine terminal UNKNOWN_CMD or FAILED; neither is a local I/O byte.
        for firmware_status in (0xFE, 0xFF):
            for offset in (module.UV_AMD_MIN, -20, -1, 0, 1, 20, 60, module.UV_AMD_MAX):
                backend = MailboxPort("firmware", firmware_status, secondary=("success", 1))
                if offset > 0:
                    # Preserve the encoding assertion, but a positive request must never reach a mailbox.
                    assert module._co_arg_encode(offset, cfg) == offset
                    assert module.amd_uv_set_with(backend, cfg, offset, diagnostics) == 2
                    assert backend.calls == []
                    assert diagnostics["failure"] == {"origin": "validation", "stage": "offset",
                        "helperExitCode": 2, "mailboxStatus": None}
                    continue
                assert module.amd_uv_set_with(backend, cfg, offset, diagnostics) == 0
                expected = offset if offset >= 0 else 0x100000 - (-offset)
                argument_writes = [row for row in backend.calls
                                   if row[0] == "ioctl_write_smu_register" and row[1][0] in (arg, secondary_arg)]
                assert argument_writes == [("ioctl_write_smu_register", (arg, expected), 0),
                                           ("ioctl_write_smu_register", (secondary_arg, expected), 0)]
                assert diagnostics["attempts"] == [
                    dict(cfg["co_commands"][0], origin="firmware_response", mailboxStatus=firmware_status, accepted=False),
                    dict(cfg["co_commands"][1], origin="firmware_response", mailboxStatus=1, accepted=True)]
                assert diagnostics.get("failure") is None
                assert diagnostics["route"] == cfg["co_commands"][1]
            # Callers without an outward diagnostic still consume each real
            # mailbox's typed provenance, rather than just its numeric status.
            backend = MailboxPort("firmware", firmware_status, secondary=("success", 1))
            assert module.amd_uv_set_with(backend, cfg, -20) == 0
            assert backend.sent == {"mp1", "rsmu"}
        # Reusing an explicit diagnostic container cannot retain earlier failures.
        backend = MailboxPort("success")
        assert module.amd_uv_set_with(backend, cfg, -8, diagnostics) == 0
        assert diagnostics.get("failure") is None
        assert diagnostics["attempts"] == [dict(cfg["co_commands"][0], origin="firmware_response", mailboxStatus=1, accepted=True)]
        assert diagnostics["route"] == cfg["co_commands"][0]
        assert backend.sent == {"mp1"}
        assert module._send_mailbox(MailboxPort("ready_timeout"), cfg["coall"], 0, cfg, tries=1) == 0xFC
        assert module._send_mailbox(MailboxPort("firmware", 0xFC), cfg["coall"], 0, cfg, tries=1) == 0xFC
        assert module.amd_uv_set_with(object(), cfg, -70, diagnostics) == 2
        assert diagnostics["failure"]["origin"] == "validation"
        assert module.amd_uv_set_with(object(), module.FAM_UNKNOWN, -8, diagnostics) == 6
        assert diagnostics["failure"]["origin"] == "capability"
        missing_command = dict(cfg, coall=None)
        assert module.amd_uv_set_with(object(), missing_command, -8, diagnostics) == 6
        assert diagnostics["failure"]["stage"] == "all_core_command"
        missing_addresses = dict(cfg, msg=None)
        assert module.amd_uv_set_with(object(), missing_addresses, -8, diagnostics) == 6
        assert diagnostics["failure"]["stage"] == "mailbox_addresses"
    finally:
        module.time.sleep = saved_sleep


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


def check_intel_single_targets(module):
    """Single-target CLI/daemon share PL1=W, PL2=W+1 without hardware calls."""
    calls = []
    previous = (module.intel_set, module.intel_set_pawn, module.amd_set)
    try:
        module.intel_set = lambda p1, p2, dry=False: (calls.append(("cli", p1, p2, dry)) or 0)
        module.intel_set_pawn = lambda backend, p1, p2, dry=False: (calls.append(("daemon", p1, p2, dry)) or 0)
        module.amd_set = lambda *args: (calls.append(("amd",) + args) or 0)
        for watts in (2, 8, 16, 23, 30, 35, 65, 120, 200, 30.5, 31.5):
            expected = (int(round(watts)), int(round(watts)) + 1)
            assert module.apply_tdp("intel", watts) == 0
            assert calls[-1] == ("cli", *expected, False), (watts, calls[-1])
            response, stop = module._daemon_dispatch_request(
                "intel", {"intel": object()},
                {"version": 1, "requestId": "single-target-fixture", "op": "set", "args": {"watts": watts}})
            assert not stop and response["ok"] and response["rc"] == 0
            assert calls[-1] == ("daemon", *expected, False), (watts, calls[-1])
        assert module.apply_tdp("intel", 30, dry=True) == 0
        assert calls[-1] == ("cli", 30, 31, True)
        assert module.apply_tdp("intel", 30, intel_delta=5) == 0
        assert calls[-1] == ("cli", 30, 35, False)
        assert module.apply_tdp("intel", 30, intel_delta=0) == 0
        assert calls[-1] == ("cli", 30, 31, False)
        assert module.apply_tdp("amd", 30) == 0
        assert calls[-1] == ("amd", 30000, 30000, 30000, False)
        for invalid in (0, 1, 201, -1, float("nan"), float("inf")):
            before = len(calls)
            response, stop = module._daemon_dispatch_request(
                "intel", {"intel": object()},
                {"version": 1, "requestId": "invalid-target-fixture", "op": "set", "args": {"watts": invalid}})
            assert not stop and response["rc"] == 2 and len(calls) == before
    finally:
        module.intel_set, module.intel_set_pawn, module.amd_set = previous



    # Use the actual backend initializer and actual RAPL setter with an in-memory
    # MSR port. Static CPU identity is read once, never in repeated float writes.
    old_cpu, old_log = module._read_cpu_info, module.log
    identities, io = [], []
    try:
        def cpu_identity():
            identities.append("read")
            return "Intel future CPU", "Intel64 Family 6 Model 254 Stepping 0"
        module._read_cpu_info = cpu_identity
        module.log = lambda *args: None
        backend = module.IntelPawnBackend()
        backend.msr = object()
        backend._enter_tx = lambda: True
        backend._leave_tx = lambda: None
        values = {module.MSR_RAPL_POWER_UNIT: 3, module.MSR_PKG_POWER_LIMIT: 0x0010000000100000}
        def read_msr(msr):
            io.append(("read", msr))
            return values.get(msr)
        def write_msr(msr, value):
            io.append(("write", msr, value))
            values[msr] = value
            return True
        backend.read_msr_raw = read_msr
        backend.read_msr = read_msr
        backend.write_msr_raw = write_msr
        assert backend.cpu_capability["identityVerified"] is True
        assert backend.cpu_capability["rapl_supported"] is True
        for watts in (20, 30, 25):
            assert module.intel_set_pawn(backend, watts, watts + 1) == 0
        assert identities == ["read"], identities
        assert len([x for x in io if x[0] == "write"]) == 3
        io.clear()
        values[module.MSR_PKG_POWER_LIMIT] |= module.MSR_PKG_POWER_LOCK
        assert module.intel_set_pawn(backend, 20, 21) == 6
        assert not [x for x in io if x[0] == "write"]
        assert identities == ["read"]
        # A forged vendor/absent identity must stop before any MSR read/write.
        module._read_cpu_info = lambda: ("AMD CPU", "AMD64 Family 26 Model 68 Stepping 0")
        unverified = module.IntelPawnBackend()
        unverified.msr = object()
        unverified._enter_tx = lambda: True
        unverified._leave_tx = lambda: None
        unverified.read_msr_raw = read_msr
        unverified.read_msr = read_msr
        unverified.write_msr_raw = write_msr
        values[module.MSR_PKG_POWER_LIMIT] = 0
        io.clear()
        assert module.intel_set_pawn(unverified, 20, 21) == 6
        assert io == [], io
    finally:
        module._read_cpu_info, module.log = old_cpu, old_log


def check_uv_receipts(module):
    """Exercise the actual new UV describe/receipt/dispatch functions without hardware."""
    original_amd = module.detect_amd_family
    original_intel = module.detect_intel_capability
    original_cpu = module._read_cpu_info
    try:
        amd_cfg = dict(module.FAM_APU_FT6_NEW)
        module.detect_amd_family = lambda: amd_cfg
        module._read_cpu_info = lambda: (
            "AMD Ryzen AI 9 HX 370", "AMD64 Family 26 Model 68 Stepping 0")

        described = module._uv_describe("amd", {
            "supported": True, "vendor": "amd", "current": 0,
            "readback": False, "current_semantics": "not_readable"})
        assert described["available"] is True
        assert described["unit"] == "co_step"
        assert described["offset"] == {"min": -60, "max": 0, "step": 1, "signed": True}
        assert described["scope"] == {"kind": "amd_all_core", "planes": []}
        assert described["readbackCapability"] is False
        assert described["encoding"] == "u20"
        assert described["identity"]["familyTag"] == "apu_ft6_new"
        assert described["identity"]["cpuid"] == {"family": 26, "model": 68}

        success = module._amd_uv_receipt(-30, 0, False)
        assert success["ok"] is True and success["accepted"] is True
        assert success["applicationState"] == "unknown"
        assert success["applied"] is None
        assert success["readback"] is False and success["observed"] is None
        assert success["reason"] == "smu_command_accepted_no_readback"

        rejected = module._amd_uv_receipt(-30, 6, False)
        assert rejected["ok"] is False and rejected["accepted"] is False
        assert rejected["applicationState"] == "failed"
        assert rejected["applied"] is None and rejected["readback"] is False

        capture = io.StringIO()
        with contextlib.redirect_stdout(capture):
            rc = module.uv_set_dispatch("amd", -30, dry=True, receipt=True)
        assert rc == 0
        payloads = [json.loads(line) for line in capture.getvalue().splitlines()
                    if line.strip().startswith("{")]
        assert payloads and payloads[-1]["vendor"] == "amd"
        assert payloads[-1]["accepted"] is False
        assert payloads[-1]["applicationState"] == "unknown"
        assert payloads[-1]["applied"] is None
        assert payloads[-1]["readback"] is False
        assert payloads[-1]["reason"] == "dry_run_no_write"

        assert module.amd_uv_set(-70, dry=True) == 2
        assert module.amd_uv_set(0, dry=True) == 0

        assert rejected["reason"] == "smu_write_failed_rc_6"
        assert rejected["failure"] is None and success["failure"] is None
        # Keep structured cause evidence separate from the helper process rc.
        for origin, stage, status, reason in (
                ("firmware_response", "response", 0xFE, "amd_uv_firmware_response_0xFE"),
                ("firmware_response", "response", 0xFC, "amd_uv_firmware_response_0xFC"),
                ("ready_wait", "mailbox_ready", None, "amd_uv_ready_wait_mailbox_ready"),
                ("response_wait", "mailbox_response", None, "amd_uv_response_wait_mailbox_response"),
                ("transport", "response_read", None, "amd_uv_transport_response_read"),
                ("transport", "command_write", None, "amd_uv_transport_command_write"),
                ("transport", "invalid_response", None, "amd_uv_transport_invalid_response")):
            diagnostic = {"failure": {"origin": origin, "stage": stage,
                          "helperExitCode": 6, "mailboxStatus": status}}
            result = module._amd_uv_receipt(-30, 6, diagnostics=diagnostic)
            assert result["reason"] == reason and result["failure"] == diagnostic["failure"]
            uncertain = origin == "response_wait" or (
                origin == "transport" and stage in ("command_write", "response_read", "invalid_response"))
            assert result["applicationState"] == ("unknown" if uncertain else "failed")
            assert result["accepted"] is False and result["readback"] is False
            assert result["applied"] is None
            diagnostic["failure"]["stage"] = "changed_after_receipt"
            assert result["failure"]["stage"] == stage, "receipt owns its evidence snapshot"
            assert module._amd_uv_receipt(-30, 0, diagnostics=diagnostic)["failure"] is None
            assert module._amd_uv_receipt(-30, 0, True, diagnostic)["failure"] is None

        assert success["attempts"] == [] and success["route"] is None
        assert rejected["attempts"] == [] and rejected["route"] is None
        assert payloads[-1]["attempts"] == [] and payloads[-1]["route"] is None
        legacy_cfg = dict(amd_cfg)
        legacy_cfg.pop("co_commands", None)
        saved_set = {name: getattr(module, name) for name in
                     ("_SmuGate", "_amd_open", "_send_mailbox")}
        class SetGate:
            acquired = True
            def __enter__(self): return self
            def __exit__(self, *args): pass
        class SetPort:
            def __init__(self): self.closed = False
            def close(self): self.closed = True
        opens, sends = [], []
        def open_port():
            backend = SetPort()
            opens.append(backend)
            return backend, 0
        def send_port(p, cmd, value, cfg, transport, tries=300, diagnostics=None):
            sends.append((p, cmd, value, transport, diagnostics))
            module._amd_uv_failure(diagnostics, "firmware_response", "response", mailbox_status=0xFE)
            return 0xFE
        def dispatch(value=-30, dry=False):
            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                rc = module.uv_set_dispatch("amd", value, dry=dry, receipt=True)
            rows = [json.loads(line) for line in output.getvalue().splitlines()
                    if line.strip().startswith("{")]
            assert len(rows) == 1
            return rc, rows[0]
        try:
            module._SmuGate = SetGate
            module.detect_amd_family = lambda: legacy_cfg
            module._amd_open = open_port
            module._send_mailbox = send_port
            for positive in (1, 20, 60):
                for dry in (False, True):
                    rc, result = dispatch(positive, dry=dry)
                    assert rc == 2 and result["accepted"] is False
                    assert result["readback"] is False and result["observed"] is None
                    assert opens == [] and sends == []
            rc, result = dispatch(-70)
            assert rc == 2 and result["failure"] == {
                "origin": "validation", "stage": "offset", "helperExitCode": 2, "mailboxStatus": None}
            assert opens == [] and sends == []
            module.detect_amd_family = lambda: dict(module.FAM_UNKNOWN)
            rc, result = dispatch()
            assert rc == 6 and result["reason"] == "amd_uv_capability_family"
            assert opens == [] and sends == []
            module.detect_amd_family = lambda: legacy_cfg
            SetGate.acquired = False
            rc, result = dispatch()
            assert rc == 6 and result["failure"] == {
                "origin": "gate", "stage": "acquire", "helperExitCode": 6, "mailboxStatus": None}
            assert opens == [] and sends == []
            SetGate.acquired = True
            for open_rc, origin, stage in ((3, "driver", "open"), (4, "module", "load")):
                module._amd_open = lambda code=open_rc: (None, code)
                rc, result = dispatch()
                assert rc == open_rc and result["failure"] == {
                    "origin": origin, "stage": stage, "helperExitCode": open_rc, "mailboxStatus": None}
                assert sends == []
            module._amd_open = open_port
            for _ in range(2):
                rc, result = dispatch()
                assert rc == 6 and result["reason"] == "amd_uv_firmware_response_0xFE"
                assert opens[-1].closed and len(sends) == len(opens)
            failed_diagnostic = sends[-1][-1]
            def send_success(p, cmd, value, cfg, transport, tries=300, diagnostics=None):
                sends.append((p, cmd, value, transport, diagnostics))
                assert diagnostics == {}, "each dispatch gets a new local diagnostic container"
                return 1
            module._send_mailbox = send_success
            rc, result = dispatch()
            assert rc == 0 and result["accepted"] is True and result["failure"] is None
            assert result["reason"] == "smu_command_accepted_no_readback"
            assert opens[-1].closed and len(sends) == len(opens)
            assert failed_diagnostic["failure"]["mailboxStatus"] == 0xFE
            rc, result = dispatch(dry=True)
            assert rc == 0 and result["failure"] is None and result["reason"] == "dry_run_no_write"
            assert len(sends) == len(opens) == 3

            # The same dispatch/receipt path now covers source-declared candidates.
            module.detect_amd_family = lambda: amd_cfg
            assert amd_cfg["co_commands"] == [
                {"transport": "mp1", "command": 0x4C}, {"transport": "rsmu", "command": 0x5D}]
            selection = module.UV_POLICY["amd"]["candidateSelection"]
            assert selection["retryOnly"] == {
                "origin": "firmware_response", "mailboxStatuses": [0xFE, 0xFF]}
            unknown = (0xFE, "firmware_response", "response", 0xFE)
            failed = (0xFF, "firmware_response", "response", 0xFF)
            completed = (1, None, None, None)
            stopped = [(status, "firmware_response", "response", status) for status in (0xFC, 0xFD)]
            stopped += [
                (0xFC, "ready_wait", "mailbox_ready", None),
                (0xFC, "response_wait", "mailbox_response", None),
                *((0xFE, "transport", stage, None) for stage in
                  ("ready_read", "clear_response", "argument_write", "command_write", "response_read", "invalid_response")),
                (0xFE, None, None, None),
                (0xFE, None, "response", 0xFE),
                (0xFF, "transport", "command_write", None),
                (0xFF, "transport", "response_read", None),
                (0xFF, None, None, None),
                (0xFF, "firmware_response", "response", 0xFE),
                (0xFE, "firmware_response", "response", 0xFF),
                (0xFF, "firmware_response", "wrong_stage", 0xFF),
            ]
            routes = amd_cfg["co_commands"]
            scenarios = [[completed], [unknown, completed], [failed, completed],
                         [unknown, unknown], [failed, failed], [unknown, failed], [failed, unknown]]
            scenarios += [[failure] for failure in stopped]
            scenarios += [[retryable, failure] for retryable in (unknown, failed) for failure in stopped]
            for outcomes in scenarios:
                sends.clear()
                expected_attempts = []
                def send_candidates(p, cmd, value, cfg, transport, tries=300, diagnostics=None):
                    assert diagnostics == {}, "candidate evidence must not retain an earlier rejection"
                    assert all(diagnostics is not row[-1] for row in sends)
                    index = len(sends)
                    assert index < len(outcomes), "uncertain results must stop candidate consumption"
                    sends.append((p, cmd, value, transport, diagnostics))
                    status, origin, stage, mailbox_status = outcomes[index]
                    if origin is not None:
                        module._amd_uv_failure(diagnostics, origin, stage, mailbox_status=mailbox_status)
                    elif mailbox_status is not None:
                        diagnostics["failure"] = {"stage": stage, "helperExitCode": 6, "mailboxStatus": mailbox_status}
                    expected_attempts.append(dict(routes[index], origin="firmware_response" if status == 1 else origin,
                                                  mailboxStatus=1 if status == 1 else mailbox_status, accepted=status == 1))
                    return status
                module._send_mailbox = send_candidates
                rc, result = dispatch(-20)
                accepted = outcomes[-1][0] == 1
                assert rc == (0 if accepted else 6)
                assert result["accepted"] is accepted and result["ok"] is accepted
                assert result["attempts"] == expected_attempts
                assert result["route"] == routes[len(outcomes) - 1]
                assert result["command"] == "set-coall-%s-0x%02X" % (
                    routes[len(outcomes) - 1]["transport"], routes[len(outcomes) - 1]["command"])
                assert [(row[3], row[1], row[2]) for row in sends] == [
                    (route["transport"], route["command"], 0xFFFEC) for route in routes[:len(outcomes)]]
                assert all(row[0] is opens[-1] for row in sends) and opens[-1].closed
                origin, stage = outcomes[-1][1:3]
                uncertain = origin == "response_wait" or (origin == "transport" and stage in
                            ("command_write", "response_read", "invalid_response"))
                assert result["applicationState"] == ("unknown" if accepted or uncertain else "failed")
                assert result["applied"] is None and result["observed"] is None and result["readback"] is False
                if accepted:
                    assert result["failure"] is None and result["reason"] == "smu_command_accepted_no_readback"
                elif origin is not None:
                    assert result["failure"] == {
                        "origin": origin, "stage": stage, "helperExitCode": 6, "mailboxStatus": outcomes[-1][3]}
                elif outcomes[-1][3] is None:
                    assert result["failure"] is None and result["reason"] == "smu_write_failed_rc_6"
                # After each failure, the next manual dispatch starts from the
                # primary and cannot mutate the receipt's previous snapshots.
                snapshot = json.loads(json.dumps(result))
                failed_containers = [row[-1] for row in sends]
                sends.clear()
                module._send_mailbox = send_success
                rc, fresh = dispatch()
                assert rc == 0 and fresh["accepted"] is True and fresh["failure"] is None
                assert len(sends) == 1 and fresh["route"] == routes[0]
                assert fresh["attempts"] == [dict(routes[0], origin="firmware_response", mailboxStatus=1, accepted=True)]
                assert all(sends[0][-1] is not previous for previous in failed_containers)
                assert result == snapshot
                send_count, open_count = len(sends), len(opens)
                rc, dry_result = dispatch(dry=True)
                assert rc == 0 and dry_result["attempts"] == [] and dry_result["route"] is None
                assert dry_result["accepted"] is False and dry_result["applicationState"] == "unknown"
                assert (len(sends), len(opens)) == (send_count, open_count)

            # The generated library declaration, rather than a second runtime
            # status table, must actually decide which terminal response advances.
            original_rule = selection["retryOnly"]
            try:
                for rule, first, expected_count in (
                    ({"origin": "firmware_response", "mailboxStatuses": [0xFE]}, failed, 1),
                    ({"origin": "firmware_response", "mailboxStatuses": [0xFF]}, unknown, 1),
                    ({"origin": "firmware_response", "mailboxStatuses": [0xFF]}, failed, 2),
                    ({"origin": "transport", "mailboxStatuses": [0xFF]}, failed, 1),
                    ({"origin": "firmware_response", "mailboxStatuses": [True]}, failed, 1),
                    ({"origin": "firmware_response", "mailboxStatuses": []}, failed, 1),
                    ({"origin": "firmware_response", "mailboxStatus": 0xFE}, unknown, 2),
                ):
                    selection["retryOnly"] = rule
                    sends.clear()
                    def send_policy(p, cmd, value, cfg, transport, tries=300, diagnostics=None):
                        index = len(sends)
                        sends.append((p, cmd, value, transport, diagnostics))
                        if index == 0:
                            module._amd_uv_failure(diagnostics, first[1], first[2], mailbox_status=first[3])
                            return first[0]
                        return 1
                    module._send_mailbox = send_policy
                    rc, result = dispatch(-20)
                    assert len(sends) == expected_count, "helper must consume generated retryOnly"
                    assert rc == (0 if expected_count == 2 else 6)
                    assert result["route"] == routes[expected_count - 1]
                    assert all(row[2] == 0xFFFEC for row in sends)
            finally:
                selection["retryOnly"] = original_rule

            # Corrupt duplicated candidate data must never replay one route.
            sends.clear()
            for commands in ([routes[0], routes[0]], routes * 5):
                invalid_cfg = dict(amd_cfg, co_commands=commands)
                module.detect_amd_family = lambda: invalid_cfg
                module._send_mailbox = send_success
                rc, result = dispatch(-20)
                assert rc == 6 and result["reason"] == "amd_uv_capability_all_core_command"
                assert sends == [] and result["attempts"] == []
            module.detect_amd_family = lambda: amd_cfg

            evidence = {"attempts": [dict(routes[1], origin="firmware_response", mailboxStatus=1, accepted=True)],
                        "route": dict(routes[1])}
            snapshot = module._amd_uv_receipt(-20, 0, diagnostics=evidence)
            evidence["attempts"][0]["origin"] = "changed_after_receipt"
            evidence["route"]["transport"] = "changed_after_receipt"
            assert snapshot["attempts"][0]["origin"] == "firmware_response"
            assert snapshot["route"] == routes[1] and snapshot["command"] == "set-coall-rsmu-0x5D"
            dry_result = module._amd_uv_receipt(-20, 0, True, evidence)
            assert dry_result["attempts"] == [] and dry_result["route"] is None
        finally:
            for name, value in saved_set.items(): setattr(module, name, value)
            module.detect_amd_family = lambda: amd_cfg

        intel_protocol = module.UV_POLICY["intel"]["protocol"]
        assert intel_protocol == {
            "msr": 0x150, "planeDomains": {"core": 0, "cache": 2},
            "setCommand": 0x11, "offsetScale": 1.024,
            "offsetBits": 11, "offsetShift": 21, "settleMs": 100}
        assert module.MSR_VOLTAGE_CTL == intel_protocol["msr"]
        assert module.UV_PLANE_DOMAINS == intel_protocol["planeDomains"]
        assert module.UV_PLANES_SAFE == tuple(module.UV_POLICY["intel"]["planes"])
        assert module.INTEL_OC_CMD_SET_VF == intel_protocol["setCommand"]
        assert module.INTEL_OC_OFFSET_MASK == 0xFFE00000
        assert (module.UV_AMD_MIN, module.UV_AMD_MAX) == (-60, 0)
        assert (module.UV_INTEL_MIN, module.UV_INTEL_MAX) == (-150, 0)
        for mv, payload in ((-150, 0xECC00000), (-75, 0xF6600000), (-25, 0xFCC00000), (0, 0)):
            assert module._uv_mv_to_data32(mv) == payload
            assert abs(module._msr_data_to_mv(payload) - mv) <= 1
        intel_cap = dict(original_intel())
        intel_cap.update({"name": "fixture-intel", "uv_core": True,
                          "uv_cache": True, "uv_igpu": False, "uv_sa": False,
                          "family": 6, "model": 0xBA})
        module.detect_intel_capability = lambda: intel_cap
        intel_described = module._uv_describe("intel", {
            "supported": True, "vendor": "intel", "readback": True, "current": -75})
        assert intel_described["available"] is True
        assert intel_described["unit"] == "mv"
        assert intel_described["offset"] == {"min": -150, "max": 0, "step": 1, "signed": True}
        assert intel_described["scope"] == {
            "kind": "intel_fivr_plane", "planes": ["core", "cache"]}
        assert intel_described["readbackCapability"] is True

        partial = module._intel_uv_receipt(-75, {
            "ok": False, "error": "write_failed",
            "planes": {
                "core": {"ioctl_written": True, "reason": "uxtu_direct_write"},
                "cache": {"ioctl_written": False, "reason": "write_failed"},
            }}, 6, False)
        assert partial["ok"] is False and partial["accepted"] is False
        assert partial["applicationState"] == "unknown", "partial writes cannot prove no domain changed"
        assert partial["applied"] is None and partial["readback"] is False
        assert partial["planes"]["core"]["ioctl_written"] is True
        assert partial["planes"]["cache"]["ioctl_written"] is False
        assert all(p["firmware_accepted"] is None and p["readback_mv"] is None
                   for p in partial["planes"].values())

        completed = module._intel_uv_receipt(-75, {
            "ok": True,
            "planes": {"core": {"ioctl_written": True},
                       "cache": {"ioctl_written": True}}}, 0, False)
        assert completed["ok"] is True and completed["accepted"] is True
        assert completed["applicationState"] == "unknown"
        assert completed["applied"] is None and completed["readback"] is False
        assert completed["planes"]["core"]["ioctl_written"] is True
        assert completed["planes"]["cache"]["ioctl_written"] is True
    finally:
        module.detect_amd_family = original_amd
        module.detect_intel_capability = original_intel
        module._read_cpu_info = original_cpu


    # HSMP must read only its explicit response address. A busy/unreadable
    # channel must not become capability-ready and probing must never zero CO.
    saved_smu = {name: getattr(module, name) for name in
                 ("_SmuGate", "_amd_open", "detect_amd_family", "_smu_rd", "_smu_exec")}
    class ProbeGate:
        acquired = True
        def __enter__(self): return self
        def __exit__(self, *args): pass
    class ReadonlySmu:
        def __init__(self): self.closed = False
        def close(self): self.closed = True
        def execute(self, *args): raise AssertionError("probe attempted command/write")
    try:
        module._SmuGate = ProbeGate
        module.detect_amd_family = lambda: dict(module.FAM_HSMP_FP10_AM5)
        def forbidden_version(*args):
            raise AssertionError("HSMP probe must not use internal legacy CPU lookup")
        module._smu_exec = forbidden_version
        for response, supported in ((1, True), (0xFC, True), (0, False), (None, False)):
            backend, addresses = ReadonlySmu(), []
            module._amd_open = lambda: (backend, 0)
            def readonly_response(p, address):
                assert p is backend
                addresses.append(address)
                return response
            module._smu_rd = readonly_response
            probed = module.amd_uv_probe()
            assert addresses == [0x03B10A90] and backend.closed
            assert probed["supported"] is supported, (response, probed)
            assert probed["readback"] is False and probed["current_semantics"] == "not_readable"
            assert probed["probe_mode"] == "source_recipe_plus_readonly_hsmp_channel"
            if not supported:
                assert probed["reason"] == "hsmp_channel_unavailable_or_busy"
        module._amd_open = lambda: (None, 6)
        assert module.amd_uv_probe()["reason"] == "pawnio_open_failed"
        ProbeGate.acquired = False
        assert module.amd_uv_probe()["reason"] == "smu_gate_timeout"
    finally:
        for name, value in saved_smu.items(): setattr(module, name, value)

    # The generic candidate never overrides the original runtime OC readiness
    # failure. The actual setter still writes only core/cache and never treats
    # ioctl success as firmware/readback confirmation.
    saved = {name: getattr(module, name) for name in
             ("_read_cpu_info", "is_admin", "IntelPawnBackend", "_intel_oc_get_plane_raw")}
    old_sleep = module.time.sleep
    class CpuBackend:
        writes = []
        msr = object()
        def open(self): return 0
        def close(self): pass
        def _enter_tx(self): return True
        def _leave_tx(self): pass
        def write_msr_raw(self, msr, value):
            self.writes.append((msr, value)); return True
    try:
        module._read_cpu_info = lambda: ("Intel future CPU", "Intel64 Family 6 Model 254 Stepping 0")
        module.is_admin = lambda: True
        module.IntelPawnBackend = CpuBackend
        module._intel_oc_get_plane_raw = lambda backend, plane: {"ok": False, "reason": "firmware_unsupported", "completion": 0xFF}
        unavailable = module.intel_uv_probe()
        assert unavailable["supported"] is False and unavailable["readback"] is False
        assert unavailable["reason"] == "oc_mailbox_firmware_unsupported"
        assert CpuBackend.writes == []
        module._intel_oc_get_plane_raw = lambda backend, plane: {"ok": True, "value": module._uv_mv_to_data32(-20 if plane == "core" else -10)}
        available = module.intel_uv_probe()
        assert available["supported"] is True and available["readback"] is True
        assert available["current"] == -20 and available["planes"] == {"core": -20, "cache": -10}
        settling = []
        module.time.sleep = lambda seconds: settling.append(seconds)  # memory-only write port
        backend = CpuBackend()
        result = module.intel_uv_set_pawn(backend, -25)
        bits = module._uv_mv_to_data32(-25)
        assert backend.writes == [(0x150, (0x80000011 << 32) | bits),
                                   (0x150, (0x80000211 << 32) | bits)]
        assert result["ok"] and result["rc"] == 0
        assert settling == [module.UV_POLICY["intel"]["protocol"]["settleMs"] / 1000.0] * 2
        assert all(v["firmware_accepted"] is None and v["readback"] is False for v in result["planes"].values())
        for plane in ("igpu", "sa"):
            backend.writes.clear()
            assert module.intel_uv_set_pawn(backend, -25, [plane])["ok"] is False
            assert backend.writes == []
        module.is_admin = lambda: False
        assert module.intel_uv_probe()["reason"] == "need_admin"
    finally:
        for name, value in saved.items(): setattr(module, name, value)
        module.time.sleep = old_sleep

def check_daemon_resume_completion(module):
    """Generation receipts survive a lost observer; repeated resume never reopens a successful generation."""
    original = module._daemon_resume_hardware
    calls = []
    try:
        module._daemon_resume_hardware = lambda vendor, state: calls.append(vendor) or 0
        state = {"intel": object()}
        def request(op, args):
            return module._daemon_dispatch_request("intel", state, {"version": 1, "requestId": "resume-test", "op": op, "args": args})[0]
        first = request("resume", {"generation": 7})
        assert first["ok"] and first["result"]["resumeGeneration"] == 7 and len(calls) == 1
        observed_late = request("ping", {})
        assert observed_late["result"]["reinitialized"] is True and observed_late["result"]["resumeGeneration"] == 7
        again = request("resume", {"generation": 7})
        assert again["result"]["reusedCompletion"] is True and len(calls) == 1
        assert not request("resume", {"generation": 6})["ok"] and len(calls) == 1
        assert not request("resume", {"generation": True})["ok"] and len(calls) == 1
        assert request("resume", {"generation": 8})["ok"] and len(calls) == 2
        module._daemon_resume_hardware = lambda vendor, state: calls.append(vendor) or 6
        assert not request("resume", {"generation": 9})["ok"]
        assert request("ping", {})["result"]["reinitialized"] is False
        assert request("set", {"watts": 30})["error"] == "hardware_resume_not_ready"
    finally:
        module._daemon_resume_hardware = original


def check_daemon_client_allowlist(module):
    parent = str(Path(__file__).resolve().parents[1] / "native" / "testrun" / "sleep_cycles_current" / "YeManCC-sleep-cycles.exe")
    allowed = module._daemon_allowed_client_paths(parent)
    assert module._canonical_exe_path(parent) in allowed
    assert module._canonical_exe_path(r"C:\arbitrary\other-host.exe") not in allowed
    assert len(allowed) == len(module.DAEMON_ALLOWED_CLIENTS) + 1
    module.DAEMON_PARENT_CLIENT_IMAGE = module._canonical_exe_path(parent)
    assert module._canonical_exe_path(parent) in module._daemon_allowed_client_paths()
    module.DAEMON_PARENT_CLIENT_IMAGE = ""


def check_daemon_pipe_session(module):
    import copy
    saved = {name: getattr(module, name) for name in (
        "_daemon_verify_pipe_client", "_daemon_pipe_read_json", "_daemon_pipe_write_json",
        "_daemon_dispatch_request", "_write_log_lines", "_daemon_pipe_kernel32")}
    def exercise(requests, write_ok=True, authorized=True):
        events, responses, writes = [], [], []
        queue = list(copy.deepcopy(requests))
        module._daemon_verify_pipe_client = lambda pipe: events.append("auth") or authorized
        module._daemon_pipe_read_json = lambda pipe: queue.pop(0) if queue else None
        def dispatch(vendor, state, request):
            events.append(request["op"])
            module.log("first", request["requestId"])
            module.log("second", request["requestId"])
            return {"version": 1, "requestId": request["requestId"], "ok": True, "rc": 0}, request["op"] == "quit"
        module._daemon_dispatch_request = dispatch
        module._daemon_pipe_write_json = lambda pipe, response: responses.append(response) or write_ok
        module._write_log_lines = lambda lines: writes.append(list(lines))
        class Kernel:
            def FlushFileBuffers(self, pipe):
                events.append("drain")
        module._daemon_pipe_kernel32 = lambda: Kernel()
        stopped = module._daemon_serve_connection("intel", {}, 123)
        assert module._DAEMON_LOG_BATCH is None
        return events, responses, writes, stopped
    def req(op, n, session=2):
        return {"version": 1, "requestId": str(n), "op": op, "args": {}, "sessionVersion": session}
    try:
        events, responses, writes, stopped = exercise([req("set", 1), req("ping", 2), req("quit", 3)])
        assert events == ["auth", "set", "ping", "quit", "drain"] and stopped
        assert len(responses) == 3 and all(x["sessionVersion"] == 2 for x in responses)
        assert len(writes) == 3 and all(len(lines) == 3 for lines in writes)
        assert all("first" in lines[0] and "second" in lines[1] and "daemon rpc" in lines[2] for lines in writes)
        events, responses, writes, stopped = exercise([req("ping", 1, 0), req("set", 2)])
        assert events == ["auth", "ping", "drain"] and not stopped and len(responses) == 1
        assert "sessionVersion" not in responses[0]
        events, responses, writes, stopped = exercise([req("set", 1), req("set", 2)], write_ok=False)
        assert events == ["auth", "set"] and len(responses) == 1 and not stopped
        events, responses, writes, stopped = exercise([req("quit", 1)], write_ok=False)
        assert stopped and events == ["auth", "quit"]
        events, responses, writes, stopped = exercise([req("set", 1)], authorized=False)
        assert events == ["auth"] and not responses and not writes and not stopped
        events, responses, writes, stopped = exercise([])
        assert events == ["auth"] and not responses and not writes and not stopped
    finally:
        for name, value in saved.items():
            setattr(module, name, value)


def check_strix_halo_pm_compatibility(module):
    """Extend the existing AMD protocol group; all hardware ports remain in memory."""
    import struct
    compat = module.YMCC_TDP_COMPATIBILITY["amdStrixHalo"]
    assert compat["cpu"] == {"family": 26, "models": [112]}
    assert compat["familyRecipe"] == "FAM_APU_FT6_NEW"
    assert set(compat["readback"]["tableVersions"]) == {0x64010C, 0x64020C}
    assert module._amd_tdp_pm_compatibility(module.FAM_APU_FT6_NEW_GENERIC) is None
    assert module._amd_tdp_pm_compatibility(module.FAM_UNKNOWN) is None
    assert capture_route(module, module.FAM_APU_FT6_NEW, (31000, 53000, 47000)) == [
        ("mp1", 0x14, 31000), ("rsmu", 0x31, 31000),
        ("mp1", 0x15, 53000), ("rsmu", 0x32, 53000),
        ("mp1", 0x16, 47000), ("rsmu", 0x33, 47000)]
    class PmPort:
        def __init__(self, version=0x64010C, values=(31, 53, 47), failed=None):
            self.version, self.values, self.failed = version, values, failed
            self.calls, self.updated = [], False
        def execute(self, command, args, outputs):
            self.calls.append((command, args, outputs))
            if command == self.failed:
                return 0x80004005, []
            if command == "ioctl_resolve_pm_table":
                assert outputs == 2
                return 0, [self.version, 0x180000000]
            if command == "ioctl_update_pm_table":
                assert outputs == 0
                self.updated = True
                return 0, []
            assert command == "ioctl_read_pm_table" and outputs == 3 and self.updated
            raw = struct.pack("<6f", self.values[0], 7.5, self.values[1], 9.5, self.values[2], 8.5)
            return 0, list(struct.unpack("<3Q", raw))
    target = {"stapm": 31, "fast": 53, "slow": 47}
    for version in (0x64010C, 0x64020C):
        port = PmPort(version)
        observed = module._amd_tdp_readback(port, module.FAM_APU_FT6_NEW, target)
        assert observed["readbackMatched"] is True and observed["unit"] == "w"
        assert observed["observedLimitsW"] == target and observed["physicalPowerVerified"] is False
        assert [call[0] for call in port.calls] == ["ioctl_resolve_pm_table", "ioctl_update_pm_table", "ioctl_read_pm_table"]
    port = PmPort(0x64030C)
    unknown = module._amd_tdp_readback(port, module.FAM_APU_FT6_NEW, target)
    assert unknown["readbackState"] == "unavailable" and unknown["readbackMatched"] == "not-observed"
    assert len(port.calls) == 1, "unknown table must not be decoded or used as permission"
    for failed in ("ioctl_resolve_pm_table", "ioctl_update_pm_table", "ioctl_read_pm_table"):
        result = module._amd_tdp_readback(PmPort(failed=failed), module.FAM_APU_FT6_NEW, target)
        assert result["readbackMatched"] == "not-observed" and "observedLimitsW" not in result
    for invalid in (0, float("nan"), float("inf"), 1001):
        result = module._amd_tdp_readback(PmPort(values=(invalid, 53, 47)), module.FAM_APU_FT6_NEW, target)
        assert result["readbackState"] == "unavailable" and "observedLimitsW" not in result
    assert module._amd_tdp_readback(PmPort(), module.FAM_APU_FT6_NEW)["readbackMatched"] == "not-observed"
    saved = {name: getattr(module, name) for name in ("amd_set_with", "_daemon_amd_gate_call", "log")}
    writes = []
    try:
        module.log = lambda *args: None
        module.amd_set_with = lambda p, cfg, *values: writes.append(values) or 0
        module._daemon_amd_gate_call = lambda callback: callback()
        port = PmPort(values=(55, 55, 55))
        hardware = {"p": port, "cfg": module.FAM_APU_FT6_NEW}
        def request(watts, verify=True):
            return module._daemon_dispatch_request("amd", hardware, {"version": 1, "requestId": "fixture-current", "op": "set", "args": {"watts": watts, "verifyTdp": verify}})[0]
        mismatch = request(31)
        assert mismatch["rc"] == 6 and mismatch["ok"] is False and "amd_tdp_readback_mismatch" in mismatch["error"]
        assert mismatch["result"]["amdObservation"]["observedLimitsW"]["slow"] == 55
        assert len(writes) == 1, "readback mismatch cannot replay a write"
        port.values = (31, 31, 31)
        recovered = request(31)
        assert recovered["ok"] and recovered["result"]["amdObservation"]["readbackMatched"] is True
        assert len(writes) == 2, "a fresh explicit request must recover without a permanent latch"
        port.calls.clear()
        floating = request(32, False)
        assert floating["ok"] and not port.calls and "amdObservation" not in floating["result"]
        before_count = len(writes)
        for bad in (1, "true", None):
            rejected = request(32, bad)
            assert rejected["rc"] == 2 and len(writes) == before_count
        port.version = 0x64030C
        unknown = request(33)
        assert unknown["ok"] and unknown["result"]["amdObservation"]["readbackMatched"] == "not-observed"
        hardware["cfg"] = module.FAM_APU_FT6_NEW_GENERIC
        port.calls.clear()
        generic = request(30)
        assert generic["ok"] and not port.calls and "amdObservation" not in generic["result"]
        module.amd_set_with = lambda *args: 6
        hardware["cfg"] = module.FAM_APU_FT6_NEW
        port.calls.clear()
        failed_write = request(30)
        assert failed_write["rc"] == 6 and not port.calls and "amdObservation" not in failed_write["result"]
    finally:
        for name, value in saved.items():
            setattr(module, name, value)

def main():
    module = load_module()
    check_families(module)
    check_amd_routes(module)
    check_intel_encoding(module)
    check_intel_single_targets(module)
    check_uv_receipts(module)
    check_daemon_client_allowlist(module)
    check_daemon_resume_completion(module)
    check_daemon_pipe_session(module)
    print("tdp protocol self-test: PASS")


if __name__ == "__main__":
    main()
