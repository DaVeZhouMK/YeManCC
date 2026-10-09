# T21 → T14 current-source static re-audit: RF00 P13 (2026-09-04)

状态：`T21/T14-STATIC-REAUDIT-COMPLETE / DOCS-ONLY / runtimeUpgrade=false / RUNTIME-BLOCKED`

## Provenance

- manifest: [T22-RF00-P13-C06-C07-C08-T21-current-source-manifest-20260904.json](T22-RF00-P13-C06-C07-C08-T21-current-source-manifest-20260904.json)
- line map: [T22-RF00-P13-C06-C07-C08-T21-current-source-line-map-20260904.md](T22-RF00-P13-C06-C07-C08-T21-current-source-line-map-20260904.md)
- manifestId: `T22-RF00-20260904-P13-C06-C07-C08-T21-CURRENT`
- sourceDigest: `7387F12F22F20B9BAC5E1AA7682E1408536297F26980DC2D18E452C17A04F260`
- porcelain: `333` entries; raw SHA-256 `26A79B1B85B5440010B4C8AB5F9D23D36085CB4B9A16D4CA7D07AD944D17B8CD`
- frozen HC: `0.32.4.0 / 06c0b9544db2b1f39abf9cd3796225ccfb096103`, worktree clean.

No test, build, runtime, driver, InputHost, HidHide, ROG feature report, Steam, game, virtual device, or hardware procedure occurred in this re-audit.

## Evidence procedure correction

P10 is retained only as a process negative: its actual PowerShell ordered-dictionary sort did not satisfy its declared lexical sort. P11/P12/P13 use explicit lexical `(scope, relativePath)` properties. P13 is independently reproducible from its manifest JSON and is the only current pointer for this cycle.

## T20 authority/provenance correction

`HistoricalClaimInventory.v1.json` retains all 21 historical claims and its `4 / 13 / 4` disposition. The stale `defer to 31` authority string now defers to `TASK.md` BUS sections; no deleted `31` or `32` file is an authority input. The inventory remains historical/docs-only and cannot establish runtime.

## T21: nine REC claims

All nine claims in `MainlineClaimReconciliation.v1.json` bind to P13 and preserve `runtimeUpgrade=false`:

| REC | Result after P13 static re-audit |
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

## T14 aggregation and C06/C07/C08 disposition

The DGF-01…DGF-15 mapping is structurally retained. C06 belongs to existing publication/activation safety context; C07 is an evidence-policy correction; C08 is a provenance-generation correction. None warrants a new DGF or `DGF-16`.

- **Evidence:** P13 captures the 20 directly relevant key files, including the frontend capture gate, package generator, updater policy selftest, task contract and HC references.
- **Interpretation:** C06/C07 remove two source-level contradictions in partial-package/test policy; C08 makes the static digest verifiable.
- **Unknown:** build/type-check/selftest, actual package generation, live updater copy/cleanup, InputHost startup, and all device/game behavior were not run.

P13 restores only the **current-source static** chain. T15/T16 stay design-only and T17/T18/R1 stay runtime-blocked.

