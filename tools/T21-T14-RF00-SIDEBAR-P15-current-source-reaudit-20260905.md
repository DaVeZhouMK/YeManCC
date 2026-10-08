# T21 → T14 current-source static re-audit: RF00-SIDEBAR P15 (2026-09-05)

状态：`T21/T14-STATIC-REAUDIT-COMPLETE / DOCS-ONLY / runtimeUpgrade=false / RUNTIME-BLOCKED`

## Provenance

- manifest: [T22-RF00-SIDEBAR-P15-C10-C11-C12-C13-current-source-manifest-20260905.json](T22-RF00-SIDEBAR-P15-C10-C11-C12-C13-current-source-manifest-20260905.json)
- porcelain: [T22-RF00-SIDEBAR-P15-C10-C11-C12-C13-current-source-porcelain-20260905.raw.txt](T22-RF00-SIDEBAR-P15-C10-C11-C12-C13-current-source-porcelain-20260905.raw.txt)
- line map: [T22-RF00-SIDEBAR-P15-C10-C11-C12-C13-current-source-line-map-20260905.md](T22-RF00-SIDEBAR-P15-C10-C11-C12-C13-current-source-line-map-20260905.md)
- manifestId: `T22-RF00-20260905-SIDEBAR-P15-C10-C11-C12-C13-STATIC-CURRENT`
- sourceDigest: `C963D7F2047D2F89E2713FE9C6C3EECD884265FC692C544AC8C35BE1E6A98860`
- manifest SHA-256: `2034DD7384383E7E1FA01D7D357EDD0CE12789AA2485910EDB369DF13F0390C9`
- 41 canonical key files; YMCC HEAD `aa8f38b83cc960267d2f68bf78d0e8adaca2234f`.
- porcelain: 342 entries; raw SHA-256 `FEB2F6C37D37B803250A16ACC7D256DCEE2278DBB85D8AF4805FFFBF3F3CE797`.
- frozen HC: `0.32.4.0 / 06c0b9544db2b1f39abf9cd3796225ccfb096103`, worktree clean.

No test, build, runtime, driver, InputHost, HidHide, ROG feature report, Steam, game, virtual-device, or hardware procedure occurred.

## T20 authority/provenance

`HistoricalClaimInventory.v1.json` retains the already-audited 21 historical claims and its `4 / 13 / 4` disposition. `TASK.md` remains the sole mainline authority; no deleted `31`/`32` document is reintroduced. This remains docs-only and does not create runtime evidence.

## T21 result

All nine REC claims in [MainlineClaimReconciliation.v1.json](MainlineClaimReconciliation.v1.json) now bind to the same P15 `manifestId + sourceDigest + P15 Pxx` map. `runtimeUpgrade=false` remains unchanged.

- C10 removes COM event/poll dual source; it does not prove `GetDefault`, provider identity, pairing or cadence.
- C11 withdraws the PID-only Xbox ROG matrix claim; it does not identify a ROG model or solve the HID read lifecycle.
- C12 removes automatic DLL-load calibration mode; it does not implement an admitted calibration session.
- C13 correctly downgrades fixed LocalSpace/zero-gate code from complete `EXACT_HC` to a partial candidate/project delta.

## T14 result

The existing `DGF-01…DGF-15` map is structurally re-bound in [HCParityDriftLedger.v2.json](HCParityDriftLedger.v2.json) to P15. No new DGF is created, especially no `DGF-16`.

The following remain `UNENCLOSED / RUNTIME-BLOCKED`: default Windows sensor selection, report-interval readback/actual cadence, sensor identity and gyro+accel pair proof, PnP/suspend rebind, ROG model identity/stable HID lifecycle, parameter/ACK path, HIDMaestro wire/consumer evidence, and P-HID/P-XINPUT/P-OWNER plus Steam/game recovery.

P15 restores only a current-source **static** chain for the captured 41-key snapshot. It does not close T10–T18 or R1 and it cannot be substituted for earlier test/build/runtime provenance.
