# T10 HC-route InputHost implementation evidence — 2026-09-05

Status: `SOURCE-IMPLEMENTED / STATIC-AND-NO-DRIVER-PROTOCOL-TESTED / RUNTIME-BLOCKED`

This is an implementation-evidence record, not a second lifecycle authority. The sole lifecycle and Coordinator authority remains [24](../../../../Docs/Tasks/GyroVirtual/24-UNIFIED-LIFECYCLE-AND-COORDINATION-CONTRACT-20260903.md); the HIDMaestro boundary remains [the T10 supplement](../../../../Docs/Tasks/GyroVirtual/T10-HC-CORE-BOUNDARY-AND-INPUTHOST-ARCHITECTURE-AUDIT-20260905.md).

## 1. Scope and source identity

This record covers the T10 replacement of the retired one-way state-file path with a Coordinator-owned, local named-pipe lifecycle for the locked HIDMaestro Host. It does **not** claim a HID report readback, PnP enumeration, Steam/game observation, physical-device isolation, or release proof.

Current source files after the final source correction in this record:

| file | bytes | SHA-256 |
|---|---:|---|
| `native/main.cpp` | 1,096,431 | `90DCBE3067A3A1446D58657D9CD027F72A02708BD87C508F76CB08A4D14CDB90` |
| `native/build_native.bat` | 2,663 | `C2833F652533CA777A8B4E860D8201A922675BE373AFD312EEE910DC944BC7A3` |
| `InputHost/Program.cs` | 37,714 | `1ABDBFAD84B56CBB979DE5094E0BBA39AF8129518940C6C2B7494BE22B3F617E` |
| `src/bridge/inputContracts.ts` | 13,680 | `557DB1A0A792D2BB74BD7793577A5E2598EB9E8B32082344EC79D7D417384209` |
| `src/views/GyroMotionView.vue` | 24,776 | `A9D547C058C02BB050447058378FA45D59B81966B18D421C1D0FDB8FF530281A` |
| `tools/calibration_safety_delta_selftest.ts` | 9,174 | `B823922C287AE273B0F929B58FE2A78526BFC105B48C3E6199BBB9210ED20A7A` |

These hashes are local file facts only. They are not an RF00 manifest and become historical once any listed file or this authority record changes.

## 2. Fact — HC semantics reproduced at the Host boundary

The locked HC baseline is `0.32.4.0 / 06c0b9544db2b1f39abf9cd3796225ccfb096103`.

| HC invariant | Current YMCC source implementation | Boundary |
|---|---|---|
| one serialized target lifecycle | native protects start/stop/submit with `g_inputHostMx`; Host serializes commands with `_lifecycleGate` | HC-aligned lifecycle semantics |
| target is not active merely because it exists | `HELLO → PREPARE_TARGET → SUBMIT_NEUTRAL → first accepted frame`; native sets `host-active` only after the same-tuple `SUBMIT_FRAME` receipt | Host-local receipt only |
| backend availability gates target creation | Host verifies locked Core bytes/SHA-256/MVID, loads profile, checks `IsDriverInstalled`, then creates only `dualshock-4-v2`; no driver install/repair route exists | adapter-local preflight, not OS enumeration |
| failed writer leaves connected state | a `SubmitState` exception now releases controller/context locally and moves Host to `Released`; native receives a rejected frame and stops the session | local dispose only |
| sleeping/listening writer must not publish | a power callback only revokes admission and queues a release request; the capture/Coordinator lane performs ordered `QUIESCE → neutral → RELEASE_TARGET`, and only a later committed same-generation resume queues a new Host epoch/re-prepare/neutral/first frame. Separately, the existing foreground shortcut recorder maps to HC `InputsManager.IsListening`: `inputHostSubmitPad` returns `publication-suppressed` before `SUBMIT_FRAME`, without a neutral/release/rearm transition. | source-level HC lifecycle alignment; external device result still unobserved |
| Windows sensor selection and subscription | HC uses `Gyrometer.GetDefault()` / `Accelerometer.GetDefault()`, `max(MinimumReportInterval, TimerManager.GetPeriod())`, and `ReadingChanged` subscribe/unsubscribe; native now uses the equivalent typed WinRT API, locked HC default period 8 ms, DeviceId logging, MTA check, and token teardown | selection/lifecycle parity only; not provider-pair or matrix/calibration proof |
| stop order must not be fabricated after a failed backend step | native only sends `RELEASE_TARGET` after successful `QUIESCE` and `SUBMIT_NEUTRAL`; a failed ordered step becomes `release-unproven` and is left to the bounded Host lease cleanup | stronger project safety shell, not HC API |

The reusable HC source anchors are `VirtualManager.cs:405-617` and `VIIPERTarget.cs:78-252`. The named-pipe tuple, startup nonce, run/epoch/revision/hash, receipt, lease and crash recovery fields are explicitly **YMCC project deltas**, required because HIDMaestro is a separate managed process. They are not claimed as HC IPC features.

## 3. Fact — current protocol and retirement scope

The active production protocol is:

```text
Coordinator named-pipe server
  → Host client: HELLO
  → PREPARE_TARGET
  → SUBMIT_NEUTRAL
  ↔ KEEPALIVE (transport-only during deliberate no-write)
  → SUBMIT_FRAME (first accepted receipt permits host-active)
  → QUIESCE → SUBMIT_NEUTRAL → RELEASE_TARGET → SHUTDOWN
```

Every request/receipt binds protocol version, request/sequence, run/epoch/power generation, target/owner/persona/profile, startup nonce, configuration revision/hash, locked asset identity, and Host instance identity. The Host rejects duplicate JSON fields, unknown fields/commands, sequence gaps, tuple mismatch, nonce mismatch, `-0`, non-finite/range-invalid axes, and normalized-frame hash mismatch.

Static retirement check after implementation found no production-source occurrences of:

```text
input-host-state.json
--state
File.ReadAllText(
InstallDriver()
inputHostWrite
```

## 4. Commands actually executed

All commands below were local build/static/protocol checks. No command performed a HIDMaestro driver install, virtual-controller creation, HidHide mutation, Steam/game action, physical-device access, sleep operation, or consumer observation.

| command | result | proof scope |
|---|---|---|
| `dotnet build .\InputHost\YeManInputHost.csproj -c Release --nologo` | exit `0`, 0 warnings / 0 errors | managed source compiles |
| `YeManInputHost.exe --selftest-protocol` | exit `0` | pure Host protocol: HELLO; unknown field; nonce mismatch; `-0`; frame-before-prepare rejection |
| `cmd.exe /d /c native\build_native.bat` | exit `0`, `BUILD_OK` | native source compiles |
| `YeManCC.exe --inputhost-protocol-selftest` | exit `0` | native server + controlled Host client exchange; intentionally no `PREPARE_TARGET`, driver, or virtual target |
| `pnpm exec vue-tsc --noEmit` | exit `0` | bridge/UI type contract |
| `pnpm run test:input-contracts` | exit `0`, `input contracts selftest: PASS` | telemetry/UI schema only; validates both local suppression reasons (`shortcut-recording`, `physical-source-absent`) |
| `pnpm run test:calibration-safety-delta` | exit `0`, `PASS` | static safe-zero, ordered release/rearm, HC default-sensor selection, endpoint normalization, listening no-write, physical-source no-write and no-dormant-motion guard; no device or Host target |
| `git diff --check -- native/main.cpp src/bridge/inputContracts.ts tools/input_contracts_selftest.ts tools/calibration_safety_delta_selftest.ts` | exit `0` | changed C17 source/telemetry/static-test scope whitespace check |

## 5. Inference — what this closes, and what it does not

### Source-level corrections made

1. The former state-file/implicit-install/forced-kill candidate is no longer the production T10 path.
2. Current Host lifecycle is serialized and has an explicit target/neutral/first-frame/release protocol.
3. The HC no-write condition has a concrete power/input-admission guard in the canonical writer route.
4. A failed Host writer cannot remain in the Host `Active` phase and accept later frames.
5. The fixed `(Z,X) / 2000 / *1000 / 1.2-stickNorm` gyro-to-stick candidate is retired. Until a complete HC-equivalent selected-plane/profile/layout route is admitted, canonical Host frames carry physical stick input only and report no gyro contribution.
6. UI telemetry deliberately labels accepted frames and `host-active` as Host-local facts, never as HID, Steam, or game readback.
7. Legacy COM `GetSensorsByType(...).GetAt(0)` and `ISensorEvents::SetEventSink` are retired from the capture path. The source now follows the locked HC Windows selection/subscription shape without treating equal DeviceId values, callback timing, or a typed event as pair proof.
8. A full-negative XInput stick endpoint now normalizes to exactly `-1.0`, rather than `-1.00003`; the Host's strict range validation no longer converts that valid physical state into a frame rejection and local release.
9. Suspend/recovery is no longer dependent on a later sampled frame to discover that admission was revoked. The power callback queues release; only a committed or explicitly cancelled same-generation transaction queues rearm, and `inputHostStart()` allocates the new epoch.
10. HC `VirtualManager.UpdateInputs()` skips `vTarget.UpdateInputs` while `InputsManager.IsListening`. YMCC now applies that exact writer-side rule to its existing, foreground-only shortcut recorder before the only production `SUBMIT_FRAME` call. `publication-suppressed` is local telemetry for an intentionally unsubmitted frame; it is not a target neutral, Host ACK, HID report, Steam/game observation, or a general P-OWNER closure.
11. HC `ControllerManager` clears a removed selected physical controller and its next tick returns before `VirtualManager.UpdateInputs`; it does not turn the disappeared source into periodic all-zero virtual reports. YMCC now passes the current source admission into the same canonical writer. After power admission remains open, `physicalSourceAdmitted=false` returns local `publication-suppressed / physical-source-absent` before `SUBMIT_FRAME`, preserving the Host target/session. This is source-level no-write parity only; it is not a matched-PnP identity, neutral/release, device-gone, Steam, or game observation.
12. Because this independent Host has a bounded lease, YMCC may send `KEEPALIVE` during a deliberate source/listening no-write interval. InputHost accepts it only after target preparation, updates no report, and never calls `SubmitState` from that branch. It is a transport-only `EXPLICIT_PROJECT_DELTA`; a failed receipt still exits active and invokes ordered release. No HC IPC or external consumer closure is claimed.
13. HC `SensorsManager.Suspend/Resume` stops and restores sensor listener state around power transitions. Current native performs the same WinRT subscription cancel/restart and clears sink samples on the Coordinator lane after same-generation Host release/rearm. Logs (`sensor-suspend`, `sensor-resume`) are local lifecycle facts; they do not prove provider pairing or consumer recovery.

### Not a runtime conclusion

`PREPARE_TARGET`, `SUBMIT_NEUTRAL`, `frame-accepted`, `release-local-dispose-complete`, process exit, and any log line remain **local source/Host facts**. They do not prove virtual-device enumeration, encoded bytes, zero reports observed by a consumer, target removal, Steam behavior, actual-game behavior, or external physical-input isolation.

## 6. Remaining gaps, separated by kind

### Evidence / real-device gaps

- T11: same physical provider gyro/accel pair, HC-equivalent calibration/matrix identity and epoch.
- T12: active DS4 encoder path, six-axis envelope, descriptor-level independent decode/readback; X360 motion remains rejected.
- T15/T16/T18: driver/PnP target observation; neutral/release/crash/suspend/PnP recovery observations; Steam/HIDAPI/game consumer evidence.
- Exact physical/provider identity binding for a remove notification, ROG XInput/Raw HID topology, and whether a source-lost no-write must later escalate to a neutral/release/fresh-epoch transaction remain separate T16/T18 evidence gaps. A global `WM_DEVICECHANGE → inputHostStop()` patch is prohibited because current source cannot prove it matches the active physical input source.
- `KEEPALIVE` is only a local process-boundary lease guard. Its ACK does not prove the virtual device remains enumerated, its prior report was neutralized, or Steam/game consumers observed no input.
- T13: P-HID, P-XINPUT and P-OWNER remain independent observations; HidHide is not an XInput/game isolation proof.

### Source boundary that remains deliberately fail-closed

- The current canonical adapter has one fixed, explicitly safe configuration: standard physical buttons/hat/axes, no gyro contribution and DS4 direct-IMU no-report. It is not a substitute for the UI's saved-pending motion settings. A real parameter-consumption route can only be added with T11's same-provider/matrix/calibration evidence and HC's complete selected-plane/profile/layout semantics; until then, no new formula or constant ACK is permitted.

### Deliberately unclaimed integration boundaries

- The page's durable `input.revision` / `pendingConfigHash` is still `saved-pending` until a complete HC-backed parameter-consumption path is available. The current Host's fixed canonical physical-frame tuple is **not** called an acknowledgement of UI motion parameters.
- Renderer owner/mock state is not promoted to a native physical-consumer or game-isolation mechanism. Any production P-OWNER work remains a separate Coordinator integration and cannot be inferred from this Host receipt protocol.

Neither boundary is papered over with a constant ACK or a new gyro formula. They remain safe-stop/evidence-gated work under T11–T13 and T15–T18.

## 7. C37 current-source correction — backend failure teardown is an explicit delta

The locked HC baseline does not perform the same local teardown as the current Host on a backend writer failure. In `VIIPERTarget.SendInput()`, a failed `SetInput()` invalidates the bus and calls `HandleDisconnect()` (`VIIPERTarget.cs:78-90,108-116`); that path marks the target disconnected and prevents further writes, but does not itself `RemoveDevice` or `Dispose` the target.

The current Host path is deliberately stronger: a failed `SUBMIT_FRAME` calls `ReleaseLocalLocked()` and enters `Released` (`InputHost/Program.cs:315-332`); `ReleaseLocalLocked()` attempts a neutral report and then disposes `HMController` and `HMContext` (`Program.cs:379-404`). This is `EXPLICIT_PROJECT_DELTA`, not HC-native parity. It is retained because the independent HIDMaestro process has no source-proven disconnect-only equivalent and the authoritative lifecycle contract requires a Host fault to revoke the writer and release local target/context handles.

The HC-parity claim is therefore limited to the shared invariant **backend failure stops further publication and removes active-writer admission**. Local neutral/dispose receipts remain Host-local facts; they do not prove HID/PnP device-gone, external release, Steam/game behavior, or runtime recovery. No source change is authorized by this finding; changing the branch back to “disconnect only” would conflict with the fault safe-zero/release contract without a proven HIDMaestro alternative.

## 8. BUS-P45/P46/P47 lifecycle corrections

The following source-level corrections are now part of the implementation evidence:

1. `ReleaseLocalLocked()` checks the boolean result of `TrySubmitNeutral()` and returns an incomplete release when neutral is not accepted; a successful local `Dispose` alone is no longer treated as a safe-zero receipt.
2. Renderer semantic actions carry the enqueue-time `ymcc-frontend` epoch into the deferred callback and are discarded if the owner/epoch changed before execution. Blur/hidden/close reset the controller edge baseline and local modifier/edit cadence; focus/visible recovery creates a new `game-consumer` epoch only when no summon session is active; `stopGamepad()` releases the owner runtime.
3. `inputHostStart()` checks a non-null Host process handle with `WaitForSingleObject(..., 0)` and clears an exited/unprovable session before allocating a new epoch. A stale process handle can no longer make an old `prepared/neutralized` tuple appear reusable.

These are `SOURCE-IMPLEMENTED` local lifecycle corrections aligned with the HC direction of no-write during listening/suspend and explicit state clearing on lifecycle boundaries. They do not prove another process' XInput access, PnP/device-gone, HID readback, Steam/game consumer isolation, or crash recovery. The renderer owner remains an observability/local-admission layer until the product-native owner→Host admission path and external consumer evidence are closed.

## 9. BUS-C43 lifecycle parity clarification

The locked HC source was re-read after C40/C41/C42. No additional native/InputHost defect was found that can be safely corrected from HC source alone. The remaining distinctions are now explicit:

- HC `InputsManager.IsListening` is a writer-side no-write gate; it is not proof that another process' XInput/game consumer was revoked. Current YMCC `shortcut-recording → publication-suppressed` is the corresponding source-level rule.
- Current `QUIESCE → neutral → RELEASE_TARGET → SHUTDOWN`, epoch/powerGeneration, named-pipe receipts and bounded lease are `EXPLICIT_PROJECT_DELTA` for the separate HIDMaestro process. They are not HC IPC and do not prove HID/PnP/Steam/game release.
- HC `VIIPERTarget.SendInput()` failure is `InvalidateBusId() → HandleDisconnect()`; YMCC's Host-local neutral plus HMController/HMContext disposal remains a stronger safety shell, not an HC-identical teardown.
- HC source-loss clearing is identity-bound (`ClearTargetIfMatch(instanceId)`). YMCC `physical-source-absent` currently suppresses canonical publication only; a generic device-change → target release patch remains prohibited until provider/PnP identity and external target/consumer observations exist.

Remaining unknowns are unchanged and evidence-gated: product owner→Host admission, same-provider gyro/accel pairing, HC DMI/SKU→matrix/calibration identity, HID descriptor/report readback, PnP/device-gone, Host crash/pipe loss, ROG multi-face identity, and Steam/actual-game consumer/recovery observations.
