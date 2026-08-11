"""Offline regression tests for the UXTU-compatible Intel UV write path."""

import sys
from pathlib import Path


SOURCE_DIR = Path(__file__).resolve().parents[1] / "PowerControl" / "pawnio"
sys.path.insert(0, str(SOURCE_DIR))
import YeManTdpCtl as ctl  # noqa: E402


class FakeIntelBackend:
    def __init__(self):
        self.msr = object()
        self.writes = []
        self.offset_by_domain = {0: 0, 2: 0}
        self.response = 0

    def _enter_tx(self):
        return True

    def _leave_tx(self):
        return None

    def read_msr_raw(self, _msr):
        return self.response

    def write_msr_raw(self, _msr, value):
        value = int(value)
        self.writes.append(value)
        command_word = (value >> 32) & 0xFFFFFFFF
        domain = (command_word >> 8) & 0xFF
        command = command_word & 0xFF
        payload = value & 0xFFFFFFFF
        if command == ctl.INTEL_OC_CMD_SET_VF:
            self.offset_by_domain[domain] = payload & ctl.INTEL_OC_OFFSET_MASK
        data = self.offset_by_domain.get(domain, 0) | 0x00012345
        self.response = data
        return True


def main():
    original_detect = ctl.detect_intel_capability
    ctl.detect_intel_capability = lambda: {
        "name": "offline-test",
        "uv_core": True,
        "uv_cache": True,
    }
    try:
        backend = FakeIntelBackend()
        result = ctl.intel_uv_set_pawn(
            backend, -20, planes=["core", "cache"], dry=False
        )
    finally:
        ctl.detect_intel_capability = original_detect

    assert result["ok"] is True, result
    expected_offset = ctl._uv_mv_to_data32(-20)
    set_writes = [
        value
        for value in backend.writes
        if ((value >> 32) & 0xFF) == ctl.INTEL_OC_CMD_SET_VF
    ]
    assert len(set_writes) == 2, backend.writes
    assert len(backend.writes) == 2, backend.writes
    for value in set_writes:
        assert (value & 0xFFFFFFFF) == expected_offset, hex(value)
        assert (value & 0x001FFFFF) == 0, hex(value)

    assert {((value >> 40) & 0xFF) for value in set_writes} == {0, 2}, backend.writes
    assert {((value >> 32) & 0xFFFFFFFF) for value in set_writes} == {
        0x80000011, 0x80000211
    }, backend.writes
    assert all(plane["ioctl_written"] for plane in result["planes"].values())
    assert all(plane["firmware_accepted"] is None
               for plane in result["planes"].values())
    assert all(plane["readback"] is False for plane in result["planes"].values())

    assert ctl._intel_oc_completion_reason(0x13) == "request_rejected_by_firmware"
    print("intel_oc_protocol_selftest: PASS")


if __name__ == "__main__":
    main()
