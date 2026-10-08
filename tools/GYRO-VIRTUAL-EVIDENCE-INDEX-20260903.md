# GyroVirtual Mainline Evidence Index

## Scope

Asset lock: `HC-GYRO-VIRTUAL-MAINLINE-LOCK-20260903`  
Physical Xbox 360/XInput controller is present for the current read-only observation run; no controller input is claimed beyond recorded evidence.

## F1 / Mock

| Evidence | Result | Coverage |
|---|---|---|
| `test:input-contracts` | PASS | v1 contract types, invalid revision/persona/asset/parity negatives |
| `test:input-route-evidence` | PASS | current LB+RB, B double-tap and Start+D-pad routes are recorded as migration candidates and remain UNENCLOSED until HC/owner/release evidence exists |
| `GyroTelemetryV1` / `normalizeGyroTelemetry` | PASS | realtime axis event schema, finite clamp, sequence/timestamp normalization, safe rejection of empty samples |
| `test:settings-cas` | PASS | stale revision rejection, atomic revision increment, unknown-field retention |
| `test:input-diagnostics` | PASS | default-off logging, clear/export, no records while disabled |
| `test:input-capability-gate` | PASS | centralized hidden/disabled/available fail-closed decision |
| `test:input-capability-manifest` | PASS | machine-readable capability manifest maps to hidden/UNENCLOSED runtime gate; authorization flag cannot bypass isolation or consumer gaps |
| `test:input-runtime-admission` | PASS | blocked capability cannot reach verified/admitted phase |
| `test:input-host-supervisor` | PASS | single Host, duplicate start rejection, crash journal and corrupt-journal fail-closed recovery |
| `test:input-power-pnp` | PASS | generation ordering, duplicate rejection, suspend/resume and device identity transitions |
| `test:physical-input-ownership` | PASS | summon/dismiss/suspend/release, single semantic consumer |
| `test:assisted-stick-assembler` | PASS | base right stick retained; T5 uses HC LayoutManager weighted blend; stale/invalid motion rejected |
| `test:input-lifecycle-mock` | PASS | enable/admit/frame, suspend, safe-zero, release proof, new epoch on resume |
| `test:hardware-coordinator` | PASS | existing coordinator owner/epoch/request contract |
| `test:input-coordinator-mock` | PASS | lifecycle + owner + canonical frame integration trace; epochs stay aligned, one frame has one destination, release proof completes |
| `S-20-input-coordinator-mock-trace.json` | PASS | raw replay trace: game-report → YMCC-semantic-only → modal drop → complete release |
| `S-11-input-fan-isolation-mock-trace.json` | PASS | input bridge/mock source set contains no FanHost/fan payload/EC/ACPI/WMI write token; no fan process or system mutation |
| `test:physical-input-ownership` (updated) | PASS | modal revokes owner; foreground frames are YMCC-semantic-only; dismiss restores game-report routing |
| `test:input-diagnostics` (updated) | PASS | contract-complete diagnostic record fields; default-off/clear/export behavior |
| `test:type-check` | PASS | routed ButtonMapping/GyroMotion pages and shared settings/log bridge compile |
| input mainline regression set (13 scripts) | PASS | assisted-stick, capability gates/manifest, contracts, coordinator, diagnostics, fan isolation, Host supervisor, lifecycle, power/PnP, route evidence, runtime admission and physical ownership |
| `test:input-coordinator-mock` (atomic-owner revision) | PASS | `game-consumer → revoke/admission stop → neutral → fresh epoch → ymcc-frontend`; semantic frame proves `gameInputCount=0`, `ymccActionCount>0`; close/blur/suspend/disconnect/crash clear held/chord/shift and end at explicit `none` |
| `test:input-owner-runtime` / `S-21-input-owner-runtime-trace.json` | PASS (renderer mirror) | real gamepad engine now mirrors native summon/semantic/hidden owner events with epoch and `gameInputCount=0` foreground accounting; mirror is observability only and cannot revoke another process' XInput access |
| `test:input-owner-runtime-integration` | PASS (static integration) | renderer owner mirror is wired to summon, semantic action, disconnect, suspend/restart, blur and Host-crash event paths; all remain observability-only and do not claim game isolation |
| `S-22-yemancc-owner-interactive-observation-20260903.json` | PASS_USER_OBSERVED | final installed build successfully summoned YMCC with LB+RB; user confirmed A and other controls operated the frontend. External game isolation remains NOT_PROVEN. |

## A1 / Real Probes

| Evidence | Result | Coverage |
|---|---|---|
| `hidmaestro_real_probe_evidence.json` | PASS | locked archive, driver install, X360 create, report submission, XInputGetState, cleanup |
| `hidmaestro_sdk_demo_reprobe_20260903.json` | PASS | current locked X360 SDK demo: 323 submitted frames, explicit dispose, post-cleanup PnP contains no HIDMaestro device |
| `hidmaestro_ds4_probe_evidence_20260903.json` | PASS | locked HIDMaestro DS4 v2 create, report/output activity, quit ACK and post-cleanup absence |
| `hidmaestro_ds4_reprobe_20260903.json` | PASS | current locked DS4 v2 probe: 250 Hz creation, explicit cleanup, post-cleanup PnP contains no HIDMaestro/DS4 virtual device |
| `A1-HIDHIDE-READONLY-20260903.json` | LOCATED | HidHide 1.5.230 CLI/version/config read without mutation; current baseline records four non-present hidden Flydigi/Xbox entries and one stale HIDMaestroTest allowlist path, with no cleanup performed |
| `A1-device-state-snapshot-20260903.json` | CONTROLLER_PRESENT | current HID/PnP/XInput scan found the attached Xbox 360 controller; no system mutation |
| `R1-xinput-physical-observation-20260903.json` | PASS | read-only XInput slot-0 observation captured nonzero A-button/stick activity; no system mutation |
| `hidhide_virtual_transaction_evidence_20260903.json` | PARTIAL | temporary virtual entry add/cloak/off/remove rollback; effective cloaking for the SWD fixture remains unproven |
| `hidhide_physical_probe.ps1` / `A1-B08-physical-hidhide-probe.json` | OBSERVED_NOT_SUPPRESSED | reversible real-device cloak transaction completed; explicit suppression flags are `primaryPnp=false`, `hidCompanion=false`, `xinput=false`; during the bounded cloak interval the user observed Cyberpunk 2077 still responded normally to the physical controller; rollback passed |
| `A1-B08-cyberpunk-consumer-observation-20260903.json` / `A1-B08-physical-hidhide-probe.json` | VISIBLE (3 observations) | synchronized interactive observations: Cyberpunk 2077 continued responding to joystick/A during HID-child and USB-parent bounded cloak intervals; PnP/XInput remained visible and rollback passed, so effective game suppression is consistently disproven for this path |
| `hc_reflection_load_probe_evidence_20260903.json` | BLOCKED | HC assembly closure and GamepadMotion load failure; no ManagerFactory invocation |
| `A1-hc-candidate-runtime-integrity.json` | PASS | all 91 locked runtime files match manifest SHA-256 |
| `A1-input-isolation-audit.json` | BLOCKED | manifest hash and source entrypoints verified; full HC graph isolation unproven |
| `A1-hc-manager-graph-audit.json` | BLOCKED | per-manager source paths, hashes, static constructors and direct power/fan/ACPI/WMI tokens recorded; Controller/Motion/Sensors/Virtual classes directly depend on six ManagerFactory services, so the broad graph remains non-input-only |
| `A1-hc-parity-ledger-audit-20260903.json` | PASS (referential) | all ten HC parity entries resolve to existing source and fixture paths with valid schema fields; this proves traceability only, behavioral parity remains separately unclosed |
| `A1-hc-input-order-audit-20260903.json` | PASS (static anchors) | controller/motion/virtual source call and cleanup anchors, line numbers and hashes recorded for four parity entries; no HC invocation or equivalence claim |
| `A1-hc-input-dependency-audit.json` | BLOCKED (read-only) | five known HC load dependencies absent from the locked runtime; ManagerFactory construction graph recorded; no DLL copied |
| `A1-hc-dependency-location-audit.json` | LOCATED_NOT_PROMOTABLE | all five same-named files were found only under quarantine/backup scopes with matching hashes; no file was copied, loaded, or promoted into the locked runtime |
| `A1-hc-isolated-dependency-probe.json` | SUPERSEDED (legacy loader) | the earlier Windows PowerShell loader treated native `GamepadMotion.dll` as managed and lacked supplemental framework binding; use `A1-hc-net10-probe.json` for the matched .NET 10 result; no ManagerFactory invocation or product runtime mutation |
| `A1-hc-net10-probe.json` | PARTIAL_LOAD_PASS | net10.0-windows probe loads HC identity and enumerates all 1702 types; metadata-only inspection lists ManagerFactory's 13 manager fields without running its static constructor; GamepadMotion is correctly identified as native and all 21 source-declared exports are present (exports not invoked); no product mutation |
| `A1-external-asset-metadata-20260903.json` | PARTIAL | locked HIDMaestro/HidHide hashes and license/package metadata recorded read-only; HidHide Authenticode verifies cryptographically but its signing certificate is expired at audit time; no installation, load, copy or packaging |
| `A1-third-party-input-isolation-readonly-20260903.json` | LOCATED_UNVERIFIED | reWASD/XInputPlus are not located; the locked HC ViGEm client and running ViGEmBus service are inventory-only observations. Neither proves per-game consumer isolation or recovery; runtime admission remains `UNENCLOSED`. |
| native Raw Input/XInput source inspection | NOT_AN_ISOLATION_MECHANISM | `RegisterRawInputDevices(...RIDEV_INPUTSINK...)` plus `XInputGetState` observes the attached slot and sends YMCC semantic actions; it cannot revoke a separate game's XInput access. Existing `gamepad.input-owner` only arbitrates YMCC/child dispatch. |
| `A1-gyro-virtual-release-boundary-20260903.json` | PASS | standard Release ZIP contains no GyroVirtual sidecar directories or HIDMaestro/HidHide/HandheldCompanion/GamepadMotion names; installed HC reference remains outside release by policy |
| `GYRO-VIRTUAL-MAINLINE-STATUS-20260903.json` | RUNTIME_BLOCKED | aggregated read-only status refuses runtime closure while HC input-only isolation, full physical acquisition and YMCC/game dual-consumer accounting remain incomplete; HidHide game suppression is explicitly disproven |
| `input-capability-manifest-20260903.json` | UNENCLOSED / TEST-SIDECAR-VISIBLE | machine-readable runtime gate remains hidden while local sidecar directories expose configuration-only test pages |
| `HCParityLedger.v1.json` | UNENCLOSED | ten HC C/B/G/lifecycle/persona/route/motion entries with source anchors; three native shortcut routes explicitly blocked pending parity/owner/release evidence |
| `input-route-evidence-20260903.json` | UNENCLOSED | three existing native shortcut routes recorded read-only; no route is claimed, consumed or admitted to virtual report |
| archive hash recheck | PASS | HIDMaestro 1.7.0 and HidHide 1.5.230 archive evidence matches asset lock |

## Build / Export

| Evidence | Result | Coverage |
|---|---|---|
| `pnpm run build` | PASS | current source compiled to `Mainline/Build/App` (FanHost build skipped by policy); native EXE SHA256 `B5AE33953163A0A95C56C7D303415DDED49089BFBD045814AFCE6F95941D833E` |
| `pnpm run package` | PASS | Release tree and `Release/Packages/YeManCC.zip` assembled with locked assets; package SHA256 `E6E8C604D9100D67C3598EDDF1AB0AFF9018BAC8DA2A37D021197881C0133AFC` |
| `deploy-installed.ps1` | PASS | installed tree updated with final owner-mirror build; timestamped rollback backup `.deployment-backup-20260903-230351` created |
| `YeManCC-export-verification.json` | PASS | Build, package staging, Release and `C:\SOFT\YeMan\YeManCC` executable hashes match |
| `src/components/GyroVirtualCapabilityCard.vue` | SCAFFOLDED (hidden) | retained as non-product fixture; test-side pages are directory-gated and runtime actions remain closed |
| `src/views/ButtonMappingView.vue` / `GyroMotionView.vue` | TEST-SIDECAR | configuration-only pages; route visibility requires matching `feature-assets` directory; no device IO |
| `src/views/GyroMotionView.vue` | HC-ALIGNED UI | page now exposes HC MotionInput/MotionMode/trigger, provider/calibration identity, gyro/accelerometer multipliers, gyrometer weight, velocity scaling, inner/outer/anti-deadzone, output shape, axis order/inversion, virtual-stick vs DS4-IMU output, plus a passive realtime gyro X/Y/Z + virtual-stick X/Y axis monitor driven only by `input:motion-telemetry`; no sample keeps the plot at safe zero |
| `tools/settings_cas_selftest.ts` | PASS | pure CAS stale/accepted paths plus failed-write rollback/retry; HC motion defaults, bounds, invalid-value fallback and unknown-field retention are covered |
| `PowerControl/feature-assets/*/.test-sidecar.json` | LOCAL TEST ONLY | both feature directories are present only in the source/test installation to expose the configuration pages; release ZIP contains zero matching entries |
| `GyroVirtual-sidecar-audit.json` | PASS | source and installed test markers present; release ZIP matching-entry count is 0 |
| `S-17-input-host-supervisor-mock-trace.json` | PASS | duplicate start rejected, corrupt journal fail-closed, valid release-proof recovery stops the Host |
| `S-18-input-power-pnp-mock-trace.json` | PASS | duplicate/old-generation ingress rejected; remove/insert rebinds device identity without OS mutation |
| `test:updater` | PASS | updater policy assertions aligned to the current schema-v2 FanHost manifest (8 files) and current payload manifest hash; GyroVirtual exclusion assertions pass |

## Gate State

- H1: `PARTIAL` — controller/motion source entrypoints located; isolated HC runtime load is not closed.
- H2: `PASS (mock)` — assisted-stick composition preserves physical base values.
- H3: `PASS (mock)` — single lifecycle/epoch/owner/release model covered.
- H4: `PARTIAL` — HIDMaestro X360 create/submit/remove and descriptor inspection observed; dedicated XUSB interface probe returned none.
- H5: `UNENCLOSED` — HidHide is not an XInput game-consumer blocker. Reversible real-device cloak/rollback is proven, but XnaComposite/XInput visibility remained unchanged and Cyberpunk 2077 stayed responsive; the tested path is explicitly not an isolation mechanism.
- H6: `PARTIAL` — physical XInput and Cyberpunk 2077 response are observed, and final installed YMCC semantic control is user-confirmed; HIDAPI/Steam observations and a quantitative dual-consumer trace remain pending.
- H7: `PASS (static boundary)` — no input probe touched FanHost or fan payload.
- F1 owner route: `PASS (mock)` — atomic revoke/admission-stop/neutral/new-epoch commit; `ymcc-frontend` receives semantic-only frames with zero game count; all terminal paths clear held/chord/shift and explicitly end at `none`.

## Required External Step

For the remaining T2/T4 evidence, keep the Xbox 360/XInput controller attached and perform real owner-transfer plus consumer observations under the authorized, reversible probe. Do not mark runtime closed from the mock or XInput-only results.

The locked HC runtime now contains the five supplemental managed dependencies and passes the candidate integrity audit (96/96); the matched .NET 10/native-aware probe enumerates HC managed types and confirms GamepadMotion native exports without invoking ManagerFactory or any motion export. This closes the dependency/toolchain gap only. The HC ManagerFactory graph, input-only composition, HIDMaestro/consumer receipt and runtime isolation remain UNENCLOSED; do not promote the partial probe to runtime closure.
