# T22-RF00 current-source line map (2026-09-04)

Bound to `T22-RF00-20260904-P08-C05-CURRENT` (static-only).

- `native/main.cpp:4863-4872` — two-directory asset gate (T22-C02).
- `native/main.cpp:5281-5297` — HC sensitivity fallback.
- `native/main.cpp:5393-5404` — ROG matrix only for matched identity (T22-C05).
- `native/main.cpp:5404-5414` — pairProven safe-stop branch.
- `native/main.cpp:5515-5530` — GyroDps admission requires pairProven and calibration lock (T22-C03).
- `InputHost/Program.cs:122-136` — HIDMaestro GyroDps sink; descriptor/wire parity remains unverified.
- `native/main.cpp:4968-5270` — HidHide source-present/P-HID unadmitted.
- `native/main.cpp:14673-14701` — ROG enable/restore helper; no new disable path.

Disposition: C02-C05 are local source corrections; remaining identity/provider/descriptor/consumer/recovery claims are UNENCLOSED / SAFE_STOP / RUNTIME-BLOCKED.
