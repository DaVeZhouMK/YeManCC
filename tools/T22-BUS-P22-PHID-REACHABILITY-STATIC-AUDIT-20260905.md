# T22 BUS P22 — P-HID reachability and HC selected-controller audit

Status: `SOURCE-SCOPED-STATIC-AUDIT / LEGACY-HIDHIDE-PRODUCTION-UNREACHABLE / P-HID-DEFERRED / P-XINPUT-AND-P-OWNER-INDEPENDENT / RUNTIME-BLOCKED`

Date: 2026-09-05. Static source review only. No HIDMaestro, HidHide CLI, driver, virtual device, physical controller, Steam, game, build, or test was run.

## Scope identity

| Scope | Path | SHA-256 |
| --- | --- | --- |
| HC | `HandheldCompanion/Controllers/IController.cs` | `4A9DA4FE7E2F257DF5C78B9AF843F0BF424C80D92410A675E3E59C0C3AE68C8A` |
| HC | `HandheldCompanion/Managers/ControllerManager.cs` | `C656D93C3E105DC79F9B43171B90EB8DEA73809F5AFAEC927E481DEC52D6B2FF` |
| HC | `HandheldCompanion/Misc/HidHide.cs` | `9CD9B573343FF197F67169BF651541FB293397D6666E6E14E1CEAE5651830253` |
| YMCC | `native/main.cpp` | `8797428BE985E9311FE60F1CFEA2E98FAD8F8BF1EF2ADFA7BD1890F238D808FA` |

This is a source-scope record, not an RF00 manifest.

## Facts — locked HC P-HID semantics

1. HC derives visibility from an already selected `IController`. `IsHidden` reads the selected controller's base-container registration. `Hide()` invokes `HideHID()` then optional `CyclePort()`; `Unhide()` invokes `UnhideHID()` then optional `CyclePort()` (`IController.cs:528-599`).
2. `HideHID()` and `UnhideHID()` use both `Details.baseContainerDeviceInstanceId` and `Details.deviceInstanceId` (`IController.cs:605-639`). The selected-controller identity comes before the visibility action; it is not inferred from a global device-name or VID scan.
3. HC controller selection decides whether to hide a non-virtual target and invokes the potentially blocking hide/cycle after releasing `targetLock` (`ControllerManager.cs:2290-2355`). That is a lifecycle/lock ordering fact. It does not prove game/XInput isolation.
4. `CyclePort()` is controller/transport aware: Bluetooth can uninstall/refresh; USB/HID delegates to the selected controller's `Details.CyclePort()` (`IController.cs:561-599`). It is not a fixed handwritten disable/sleep/enable operation.
5. HC's HidHide API manipulates registered device paths and cloaking, but even its source-level return values do not substitute for an external consumer observation. Therefore a YMCC journal/readback requirement is an extra verification layer, not a claim that HC has the same journal protocol.

## Facts — current YMCC call graph

1. `hidHideLegacyUnadmittedGamingScan()` has exactly one occurrence in `native/main.cpp`: its static definition at line 5165. No production caller, IPC route, UI action, startup call, callback, or function-pointer reference was located.
2. The only writes to `g_hidHideHidden` and `g_hidHideCloakEnabledByUs` are inside that uncalled helper (`main.cpp:5191,5230`). Both variables are static and start empty/false (`main.cpp:4983-4984`).
3. `inputHostStop()` does call `hidHideRestorePhysical()` (`main.cpp:5253-5268`), but that function calls HidHide CLI only by iterating `g_hidHideHidden` or seeing the cloak flag (`main.cpp:5151-5163`). With the current call graph, those conditions are not populated; this reachable restore call is therefore a no-op for HidHide state.
4. `inputHostStart()` does not call the legacy scan. It records `hidhide.p-hid-deferred status=unadmitted reason=T13-not-ready` after Host process start (`main.cpp:5269-5301`). This is the only current production disposition for P-HID.
5. The dormant helper is unsafe to rewire: it classifies virtual DS4 from `VID_054C` or `HM-CTL`, scans broad gaming candidates, launches CLI commands without checking CLI exit code, stores no before snapshot, and cycles with a handwritten void helper (`main.cpp:4988-5039,5151-5240`). It is not HC selected-controller semantics and cannot establish restore proof.

## Disposition and non-inferences

```text
P-HID current product path = deferred / no mutation
P-XINPUT                  = unobserved / not inferred from P-HID
P-OWNER                   = separate Coordinator semantic plane
Steam or game isolation   = unobserved / not inferred from any of the above
```

The deferred state is not a failure to be hidden by a later helper; it is a deliberate fail-closed production boundary. A future caller or route to `hidHideLegacyUnadmittedGamingScan`, direct `hidHideRun`, global cloak, or its handwritten cycle path is `SAFE_STOP / T13-REACHABILITY-DRIFT`. It must not be treated as a harmless reactivation.

## Gaps required before any future mutation

1. Coordinator-approved selected physical identity, exact instance/container pair, virtual-target exclusion identity, and allowlisted YMCC/Host process identities.
2. Append-only before/diff/after `VisibilityMutationJournal`, operation result and readback, PnP cycle result, recovery owner, and restore receipt.
3. A separately authorized R1 observation of P-HID visibility and restoration. Even then, P-XINPUT/Steam/game input suppression remains a separately observed plane.

This audit creates no DGF number, changes no production reachability, and does not authorize HidHide or ROG OEM mutations.
