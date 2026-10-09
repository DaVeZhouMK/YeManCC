# T21 → T14 current-source static re-audit: RF00 P11 (2026-09-04)

状态：`T21/T14-STATIC-REAUDIT-COMPLETE / DOCS-ONLY / runtimeUpgrade=false / RUNTIME-BLOCKED`

## Provenance

- manifest: [T22-RF00-P11-C06-C07-T21-current-source-manifest-20260904.json](T22-RF00-P11-C06-C07-T21-current-source-manifest-20260904.json)
- line map: [T22-RF00-P11-C06-C07-T21-current-source-line-map-20260904.md](T22-RF00-P11-C06-C07-T21-current-source-line-map-20260904.md)
- manifestId: `T22-RF00-20260904-P11-C06-C07-T21-CURRENT`
- sourceDigest: `9AC35A3D3C476B53296FE0F87A7AD1683C2175C3DF87CD076BA6B3B08EFF9F9A`
- porcelain: `325` entries; raw SHA-256 `F6C3D58F26B39827A884440FA1330555BC0021533C05732D7FB9DC52DB12B41F`
- frozen HC: `0.32.4.0 / 06c0b9544db2b1f39abf9cd3796225ccfb096103`, worktree clean.

No test, build, runtime, driver, InputHost, HidHide, ROG feature report, Steam, game, virtual device, or hardware procedure occurred in this re-audit.

## Corrected freeze procedure

P10 is retained only as a process negative: its implementation sorted PowerShell ordered dictionaries, while its declared algorithm required lexical key ordering. P11 regenerates the same 20-key scope with explicit lexical `(scope, relativePath)` properties. The P11 digest is independently reproducible from the manifest JSON.

## T20 authority/provenance correction

`HistoricalClaimInventory.v1.json` retains all 21 historical claims and its `4 / 13 / 4` disposition. The stale `defer to 31` authority string was corrected to defer to `TASK.md` BUS sections; no deleted `31` or `32` file is an authority input. The inventory remains historical/docs-only and cannot establish runtime.

## T21: nine REC claims

All nine current claims in `MainlineClaimReconciliation.v1.json` are rebound to P11 and preserve `runtimeUpgrade=false`:

| REC | Result after P11 static re-audit |
|---|---|
| REC-01 | T10–T13 remain contract-not-implemented / legacy partial candidate / runtime-blocked. |
| REC-02 | Limited YMCC P-OWNER wording only; no P-HID/P-XINPUT or external-consumer conclusion. |
| REC-03 | Historical ROG/package observations do not demonstrate current game isolation. |
| REC-04 | Pair-unproven and confidence safety wording remains source-located only. |
| REC-05 | Parameter route remains ACK-absent / parameter-not-consumed. |
| REC-06 | DS4 IMU descriptor/wire/consumer parity remains unverified. |
| REC-07 | ROG/HidHide/owner recovery remains independently unclosed. |
| REC-08 | Historical/mock/static artifacts remain below runtime evidence. |
| REC-09 | C1 option 2 remains resolved; the valid mainline range is only DGF-01…DGF-15. |

## T14 aggregation and C06/C07 disposition

The DGF-01…DGF-15 mapping is structurally retained. C06 belongs to existing publication/activation safety context; C07 is an evidence-policy correction. Neither warrants a new DGF or `DGF-16`.

- **Evidence:** P11 captures the 20 directly relevant key files, including the frontend capture gate, package generator, and updater policy selftest.
- **Interpretation:** C06/C07 remove two source-level contradictions in a partial package/test policy.
- **Unknown:** build/type-check/selftest, actual package generation, live updater copy/cleanup, InputHost startup, and any device/game behavior were not run.

P11 restores only the **current-source static** chain. T15/T16 stay design-only and T17/T18/R1 stay runtime-blocked.

