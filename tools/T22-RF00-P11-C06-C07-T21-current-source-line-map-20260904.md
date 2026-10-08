# T22-RF00 P11 current-source line map (2026-09-04)

状态：`STATIC-ONLY / CURRENT-KEY-FILE-FREEZE / RUNTIME-BLOCKED`

- manifestId: `T22-RF00-20260904-P11-C06-C07-T21-CURRENT`
- sourceDigest: `9AC35A3D3C476B53296FE0F87A7AD1683C2175C3DF87CD076BA6B3B08EFF9F9A`
- key files: `20`; YMCC HEAD: `aa8f38b83cc960267d2f68bf78d0e8adaca2234f`
- frozen HC: `0.32.4.0 / 06c0b9544db2b1f39abf9cd3796225ccfb096103`, clean worktree
- raw porcelain: `325` entries / `F6C3D58F26B39827A884440FA1330555BC0021533C05732D7FB9DC52DB12B41F`
- canonical ordering: ordinal lexical `scope`, then ordinal lexical `relativePath`; P10's ordered-dictionary sort behavior is a historical invalid manifest-generation negative.
- operation: static reads/hashes only; no test, build, InputHost, HidHide, ROG report, Steam, game, virtual device, or hardware action.

| Pxx | Current source anchor | Static observation | Runtime limitation |
|---|---|---|---|
| P01 | `native/main.cpp::inputHostSubmitPad`, `InputHost/Program.cs::RunFile`, parameter ledger | Native state-file route remains fixed-route/no exact ACK candidate; C06/C07 do not close parameter consumption. | No request/ACK/first-frame proof. |
| P02 | `native/main.cpp:4830,4915,4919,4961,5422,5540`; HC `MotionManager`, `SensorsManager`, `GamepadMotion` | ROG matrix is identity-gated; pair-unproven is safe-zero on the direct Host state route. Sensor selection and scheduling differ from HC. | No provider/epoch/sample-pair proof. |
| P03 | `InputHost/Program.cs`; HC `GamepadMotion` | X360/DS4 transport remains descriptor-unverified. | No HIDMaestro descriptor, decoder, submit or consumer evidence. |
| P04 | `native/main.cpp::rogInspectDevice`, `rogTryReadHidGamepad`; HC `ROGAlly.cs` | YMCC discovers ROG stable HID but does not retain/open/read it; HC opens the bound interface and runs its read loop. | No coordinator, PnP, suspend, crash or recovery transaction. |
| P05 | `inputOwnerRuntime`, `inputCoordinatorMock`, XInput paths | Owner/mock counters are not external game-consumer measurements. | No Steam/game consumer proof. |
| P06 | `inputHostStop` and lifecycle candidates | Existing release candidates remain short of neutral receipt/ACK/join/recovery proof. | No runtime lifecycle evidence. |
| P09 | `src/bridge/gyroVirtualFeature.ts:56-61`; `tools/package-release.ps1`; `tools/updater_policy_selftest.ts` | C06 aligns frontend diagnostic readiness to the native two-directory gate; C07 aligns the static package policy test with complete-test-package + updater-exclude semantics. | No build, selftest, package generation, or live updater observation. |

## Evidence versus interpretation

Evidence is limited to the pinned files and hashes in the manifest. The statements that C06/C07 improve consistency are source interpretations only; they do not prove UI behavior, InputHost startup, ZIP contents after a new package build, upgrade cleanup, or any physical/game consumer effect.

