# T22 RF00-SIDEBAR P16 line map — C14/C15 and InputHost lifecycle (2026-09-05)

状态：`STATIC-CURRENT-SOURCE-INDEX / DOCS-ONLY / runtimeUpgrade=false / RUNTIME-BLOCKED`

## Frozen provenance

- manifest: [T22-RF00-SIDEBAR-P16-C14-INPUTHOST-current-source-manifest-20260905.json](T22-RF00-SIDEBAR-P16-C14-INPUTHOST-current-source-manifest-20260905.json)
- manifestId: `T22-RF00-20260905-SIDEBAR-P16-C14-INPUTHOST-STATIC-CURRENT`
- sourceDigest: `596BA944944E0477C378DD3BF7FDB04A297B69397F238F1A7A4B821BDE37DEDD`
- manifest SHA-256: `0E5E74C31073D2703D9B62AB7E5DCBF16DF71D92D20DCE45F8C51CD72CFAE22A`
- key set: 47 files; YMCC HEAD `aa8f38b83cc960267d2f68bf78d0e8adaca2234f`; full porcelain count `346`, SHA-256 `C3A0E64C0E431BC4A244A6993098EC321E749050351796C59B23DD48C5ADBA15`.
- locked HC: `0.32.4.0 / 06c0b9544db2b1f39abf9cd3796225ccfb096103`, worktree clean.

No build, test, YeManCC/InputHost, driver install, HIDMaestro, HidHide, ROG feature report, Steam, game, virtual-device, or hardware operation occurred.

## P01 — Windows IMU event/default/threshold boundary

- `native/main.cpp:4749-4840,5390-5443`: legacy COM event sink is the only current sample source; provider default selection remains `GetSensorsByType()[0]`; native clips with fixed `2000` dps and now labels it as a reference default, not complete per-device HC policy.
- HC `Sensors/IMUGyrometer.cs:12-49,95-133`; `Sensors/IMUCalibration.cs:17,41-53`; `Managers/SensorsManager.cs:336` show `GetDefault()`, report interval setup and a gyrometer threshold read from current device calibration.

Result: C10 is source-aligned only; C15 removes a false complete-parity claim. Default selection, interval, physical calibration identity and cadence remain `UNENCLOSED`.

## P02 — calibration, pair and motion mapping

- `native/main.cpp:4926-4946,5459-5585`: no DLL-load calibration mode; `pairProven=false` keeps GamepadMotion/direct GyroDps fail-closed; fixed LocalSpace route remains `PARTIAL_HC_CANDIDATE`.
- HC `Managers/SensorsManager.cs:366-425`; `Helpers/GamepadMotion.cs`; `Managers/MotionManager.cs`; `Utils/InputUtils.cs:304-342` supply the reference session and mapping prerequisites.

Result: no pair, epoch, profile or first-frame proof; no runtime admission.

## P03 — InputHost one-way state-file lifecycle

- `native/main.cpp:5241-5296,5341-5351,5574-5586`: native atomically replaces a state file, starts a fixed DS4 `--state` host, sends a best-effort neutral stop record, waits 1500 ms and may terminate the process.
- `InputHost/Program.cs:30-55,112-159`: host immediately ensures a controller, swallows `InstallDriver()` failures, only polls file content, calls void `SubmitState`, and has no response channel in `--state` mode.
- `InputHost/YeManInputHost.csproj:12-15`: reference provenance remains archive-path based; P16 records no DLL hash/MVID/profile/descriptor proof.
- HC `Managers/VirtualManager.cs:25,405-457,529-617`; `Targets/VIIPERTarget.cs:123-208` provide in-process serialized target connection/status discipline, not a YMCC state-file ACK protocol.

Result: no `hostInstanceId / epoch / revision / configHash / applied ACK / first-frame receipt / neutral receipt / releaseProof`. CreateProcess or non-throwing SubmitState is not active evidence.

## P04 — HidHide selected-controller semantics versus legacy global scan

- `native/main.cpp:5037-5239`: C14 renames the source-present helpers to `hidHideLooksLikeVirtualDs4Legacy`, `hidHideCollectLegacyGamingCandidates`, and `hidHideLegacyUnadmittedGamingScan`; comments/logging state legacy/unadmitted scope. No caller to the scan was located. `inputHostStart` keeps P-HID deferred.
- HC `Managers/DeviceManager.cs:520-529` matches only native `hcIsGamingHid`'s predicate fragment.
- HC `Managers/ControllerManager.cs:2276-2349`; `Controllers/IController.cs:536-639` require selected-controller admission and controller-specific `baseContainerDeviceInstanceId + deviceInstanceId` Hide/Unhide plus transport-aware cycle.
- HC `Managers/ControllerManager.cs:257-258` enables cloaking in its manager lifecycle; it is not proof that native global CLI cloak ordering is equivalent.
- HC `Misc/HidHide.cs` and P16 source show no authority for `VID_054C` or `HM-CTL` as a sufficient virtual identity proof.

Result: `source-present / production-unreachable / HC-reference fragment only / NOT-HC-PARITY / NOT-game-consumer-isolation-proof`.

## P05 — ROG, sidebar and owner boundary

- P16 retains P15's sidebar/owner key set: `src/router.ts`, `NavRail.vue`, gyro/button views and bridges, `inputOwnerRuntime.ts`, `physicalInputOwnership.ts`, and `gamepad/engine.ts`.
- `native/main.cpp` retains raw/no-transform for unresolved ROG PID family; `ROGAlly.cs`, `IDevice.cs` and the four ROG device classes remain frozen authority.

Result: P-OWNER scope is limited to the named YMCC route; P-HID/P-XINPUT/external consumers remain independent.

## P06 — release/recovery boundary

- `native/main.cpp:5252-5266,5579-5601`: stop order is best effort and lacks receipt/journal; no sensor-only PnP/suspend rebind route exists.
- HC `Managers/SensorsManager.cs:114-170`, `Managers/VirtualManager.cs:142-176,435-457`, and `Controllers/IController.cs:547-599` show separate lifecycle components that cannot be inferred from native stop calls.

Result: close, crash, PnP, sleep/resume and consumer restoration remain `UNENCLOSED`.

## P07 — provenance and DGF range

- P16 expands P15's 41-key set to 47 keys so C14/C15/InputHost claims include their additional HC and project-reference authorities.
- The manifest, porcelain raw, this line map, T21/T14 report and reconciliation artifacts are self-excluded; every 47 key file is SHA-256/byte recorded in the manifest.
- Registered drift range remains exactly `DGF-01…DGF-15`; P16 introduces no DGF-16 and no runtime upgrade.
