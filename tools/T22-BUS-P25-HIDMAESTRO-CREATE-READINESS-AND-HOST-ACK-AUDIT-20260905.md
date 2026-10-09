# T22 BUS P25 — HIDMaestro create readiness and Host ACK audit

Status: `SOURCE-SCOPED-STATIC-AUDIT / CREATE-WAIT-LOCATED / WAIT-RESULT-NOT-PROTOCOL-RECEIPT / IMPLICIT-DRIVER-DEPLOY-PRESENT / T10-ACK-GAP-RETAINED / RUNTIME-BLOCKED`

Date: 2026-09-05. Read-only source comparison only; no product source, build, process, HIDMaestro API, driver, virtual device, Steam, game, or hardware operation ran. This is not RF00.

## Scope

| Layer | Identity |
| --- | --- |
| HC lifecycle | `VirtualManager.cs` SHA-256 `46D0B0812BBA00A9A1E9DC8A16892154FC4F02EEBF5CB413B87C6FFBA1219106` |
| HID source | commit `46054b862830fcec7bc98d72ccb7c4f0c0179fb1`; `HMContext.cs` `4B58476D…CDEF95`; `Internal/DeviceOrchestrator.cs` `C4DA47DD…F75415` |
| locked DLL | `HIDMaestro.Core.dll` SHA-256 `BD42A99BCB260435CE25796C54A4B792F8A2CED6AB78659C0CF926011663938E` |
| YMCC Host/native | `InputHost/Program.cs` `9C855378…22A95B`; `native/main.cpp` `8797428B…D808FA` |

## Facts

1. HC only updates its virtual-controller status after `vTarget.Connect()` / `Disconnect()` returns success. The target lifecycle is serialized under `controllerLock`.

2. HIDMaestro `HMContext.CreateController` calls `DeviceOrchestrator.SetupController` before constructing `HMController`. That setup performs driver-store checking, device-node creation, `WaitForHidChild`, and `WaitForDeviceStarted`; the source documents this as PnP binding readiness work.

3. In the exact setup path, the boolean results of `WaitForHidChild` and `WaitForDeviceStarted` are logged or ignored: a timeout on either is not converted by that code into a `CreateController` failure/typed readiness result. Thus `CreateController` is more than a raw process launch, but it is not a Coordinator-facing proof that every desired device/consumer plane is ready.

4. `SetupController` checks `IsDriverInstalled` and calls `FullDeploy()` when absent. Current Host also wraps `ctx.InstallDriver()` in `try { } catch { }` before `CreateController`. Therefore the current path can attempt driver deployment both explicitly and from creation; it is not a preflight-only, user-reportable deployment transaction.

5. Current `EnsureController()` stores the returned controller but emits no host instance identity, PnP-start result, profile/descriptor tuple, first neutral/frame receipt, or error receipt. Native `inputHostStart()` only sees `CreateProcessW` success and writes a local neutral state-file. Neither side observes the other side's readiness.

## Verdict

```text
CreateProcessW success
or HMContext constructed
or HMController non-null
or CreateController returned
  != Coordinator host-active
  != first-frame receipt
  != game/Steam consumer observation
  != release proof
```

This is not a claim that HIDMaestro always returns an unusable controller: the source does attempt PnP waits and normally presents a ready controller. It is the narrower, static statement that the timeout outcomes and resulting readiness are not transported into YMCC's required T10 immutable tuple/ACK protocol. HC's success gate remains the behavioural reference; a cross-process ACK/epoch/receipt shell remains an `EXPLICIT_PROJECT_DELTA`, not an HC API.

## Required future evidence

1. Explicit deployment/preflight outcome before a target prepare command; do not let Host creation silently decide the driver-install transaction.
2. Host `PREPARE_TARGET` ACK containing Host instance, persona/profile/descriptor identity, driver/PnP outcome, epoch/revision/hash, and an explicit failure reason.
3. Same-tuple neutral then first-frame receipt; a receipt remains distinct from independent encoded-report and named consumer observations.
4. On release/crash/suspend/PnP, a release disposition and recovery record. HIDMaestro disposal alone is not a Steam/game release observation.

No DGF is created and no runtime task closes.
