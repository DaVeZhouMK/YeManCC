# T22-RF00 current-source line map (2026-09-04)

This line map is static-only and is bound to `T22-RF00-20260904-P08-CURRENT` after the P08 source corrections.

- `native/main.cpp:4863-4872` — complete `gyro-motion` + `virtual-gamepad` asset gate (`T22-C02`).
- `native/main.cpp:5281-5297` — HC sensitivity fallback and LocalSpace output path.
- `native/main.cpp:5404-5414` — explicit `pairProven=false` safe-stop branch.
- `native/main.cpp:5515-5530` — direct Host submission admits GyroDps only when `pairProven && g_gmLocked` (`T22-C03`).
- `InputHost/Program.cs:122-136` — HIDMaestro state-file parser and `GyroDps` field sink; descriptor/wire parity remains unverified.
- `native/main.cpp:4968-5270` — HidHide helper is source-present but P-HID/P-XINPUT/consumer closure is not proven.
- `native/main.cpp:14673-14701` — ROG enable/restore helper; no new disable path admitted.

## Static disposition

`T22-C02/C03/C04` are local source corrections only. ROG identity, provider pairing, descriptor ABI, Host ACK, epoch, release/join, and Steam/game consumer observations remain `UNENCLOSED / SAFE_STOP / RUNTIME-BLOCKED`.