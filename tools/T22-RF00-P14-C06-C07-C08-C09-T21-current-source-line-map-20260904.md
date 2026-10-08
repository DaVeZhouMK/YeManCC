# T22-RF00 P14 current-source line map (2026-09-04)

状态：`STATIC-ONLY / CURRENT-KEY-FILE-FREEZE / RUNTIME-BLOCKED`

- manifestId: `T22-RF00-20260904-P14-C06-C07-C08-C09-T21-CURRENT`
- sourceDigest: `B1C0CEEC314F2A9F73378943FE1630DD42C9E1078493E62888A35D8AD514E8A7`
- key files: `20`; YMCC HEAD: `aa8f38b83cc960267d2f68bf78d0e8adaca2234f`
- frozen HC: `0.32.4.0 / 06c0b9544db2b1f39abf9cd3796225ccfb096103`, clean worktree
- raw porcelain: `337` entries / `B0BF3BC08D505C2B1A515BABF3B7128060CAC47E309048D30E6030F7188419C3`
- canonical ordering: ordinal lexical `scope`, then ordinal lexical `relativePath`; P10 remains a manifest-generation negative only.
- operation: static reads/hashes only; no test, build, InputHost, HidHide, ROG report, Steam, game, virtual device, or hardware action.

| Pxx | Current source anchor | Static observation | Runtime limitation |
|---|---|---|---|
| P01 | `native/main.cpp::inputHostSubmitPad`, `InputHost/Program.cs::RunFile`, parameter ledger | Native state-file route remains fixed-route/no exact ACK candidate. | No request/ACK/first-frame proof. |
| P02 | `native/main.cpp:4830,4915,4919,4961,5422,5540`; HC `MotionManager`, `SensorsManager`, `GamepadMotion` | ROG matrix is identity-gated; pair-unproven is safe-zero on the direct Host state route. Sensor selection and scheduling differ from HC. | No provider/epoch/sample-pair proof. |
| P03 | `InputHost/Program.cs`; HC `GamepadMotion` | X360/DS4 transport remains descriptor-unverified. | No HIDMaestro descriptor, decoder, submit or consumer evidence. |
| P04 | `native/main.cpp::rogInspectDevice`, `rogTryReadHidGamepad`; HC `ROGAlly.cs` | YMCC discovers ROG stable HID but does not retain/open/read it; HC opens the bound interface and runs its read loop. | No coordinator, PnP, suspend, crash or recovery transaction. |
| P05 | `inputOwnerRuntime`, `inputCoordinatorMock`, XInput paths | Owner/mock counters are not external game-consumer measurements. | No Steam/game consumer proof. |
| P06 | `inputHostStop` and lifecycle candidates | Existing release candidates remain short of neutral receipt/ACK/join/recovery proof. | No runtime lifecycle evidence. |
| P09 | `src/bridge/gyroVirtualFeature.ts:56-61`; `tools/package-release.ps1`; `tools/updater_policy_selftest.ts`; `TASK.md` header | C06 aligns frontend diagnostic readiness to the native two-directory gate; C07 aligns static package policy; C08 makes the digest reproducible; C09 corrects the stale top-level status. | No build, selftest, package generation, live updater, or device observation. |

