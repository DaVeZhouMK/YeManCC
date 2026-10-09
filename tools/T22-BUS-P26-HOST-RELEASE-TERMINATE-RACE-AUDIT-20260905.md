# T22 BUS P26 — Host release versus forced-termination audit

Status: `SOURCE-SCOPED-STATIC-AUDIT / NORMAL-RELEASE-LOCATED / FORCE-TERMINATION-RACE-LOCATED / RELEASE-RECEIPT-ABSENT / T16-GAP-RETAINED / RUNTIME-BLOCKED`

Date: 2026-09-05. Read-only source audit. No runtime, driver, virtual device, HidHide, Steam, game, physical controller, build, or test operation ran; no product source changed.

## Facts

1. In state-file mode, Host sees `{neutral:true, alive:false}`, calls `SubmitNeutral()`, returns from `RunFile`, and enters `Main`'s `finally { Release(); }`. `Release()` attempts `SubmitNeutral`, `controller.Dispose`, and `ctx.Dispose`, swallowing exceptions and emitting no result to native.

2. Native writes that stop record once, waits only 1,500 ms for the Host process, then calls `TerminateProcess` on timeout before immediately closing the handle and calling physical-visibility restore (`main.cpp:5253-5267`). The state-file protocol has no neutral ACK, release request ID, completion status, or deadline disposition.

3. Locked HIDMaestro `HMController.Dispose()` enters `HMContext.OnControllerDisposing`, which calls synchronous `DeviceOrchestrator.TeardownController`. That teardown holds a per-index gate so a new setup cannot reuse the index while removal is in flight. Its main parent removal uses a 120,000 ms budget; source comments specifically say short timeout can return while kernel cleanup is still proceeding. Internal removal outcomes are logged/caught rather than returned as a Host protocol receipt.

4. HIDMaestro documentation gives ordinary plain-HID switching as commonly fast, but that is not a bounded guarantee for this machine, current driver state, crash path, or release request. No runtime timing assertion was performed here.

## Required interpretation

```text
normal Host exit after Release()  != external device-gone proof
1.5-second process wait           != HIDMaestro teardown-complete proof
TerminateProcess on timeout       != neutral/release receipt
```

If the normal release takes longer than 1.5 seconds, the current parent may terminate the Host while HIDMaestro teardown is still executing. This is a conditional race identified from source; it does not assert that every current run leaves a device behind. It does prove that current control flow cannot claim orderly release, PnP removal, safe physical restore, or game recovery after the timeout branch.

HC supplies the reference ordering of retiring the old target before entering the next active state. It does not provide this cross-process receipt. Therefore YMCC needs an `EXPLICIT_PROJECT_DELTA` release transaction: revoke writer → Host quiesce/neutral ACK → release request → Host completion/indeterminate disposition → independently observed PnP/consumer recovery. On timeout/crash, Coordinator must remain `none / release-unproven`; it must not infer game restoration.

Current P-HID is deferred/no-mutation, so this audit does not claim a real HidHide restore race occurred. The ordering is nevertheless a T13/T16 design constraint before any future visibility mutation is admitted.

No DGF is created and no runtime closure is upgraded.
