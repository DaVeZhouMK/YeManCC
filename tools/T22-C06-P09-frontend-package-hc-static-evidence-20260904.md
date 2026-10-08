# T22-C06 / BUS-P09 static evidence (2026-09-04)

状态：`STATIC-EVIDENCE-RECORDED / C06-SOURCE-FIX-APPLIED / RF00-REQUIRED / RUNTIME-BLOCKED`

## Provenance

- runId: `T22-C06-P09-20260904`
- operator: `Codex BUS / delegated read-only probes`
- scope: current YMCC source, current test package, frozen HC `0.32.4.0 / 06c0b954`
- runtimeOperation: `false`
- buildOrTest: `false`
- InputHost/HidHide/ROG feature report/Steam/game/hardware: `not invoked`
- source mutation: only the explicitly authorized local C06 gate correction in `src/bridge/gyroVirtualFeature.ts`

## Evidence (observed source/package facts)

1. Native capture/InputHost gate is atomic: `native/main.cpp:4870-4879` requires both `feature-assets\gyro-motion` and `feature-assets\virtual-gamepad`; the predicate is used by `inputHostStart`, `inputCaptureStart`, and `inputCaptureInit`.
2. Sidebar and route visibility are intentionally per-feature: `src/bridge/gyroVirtualFeature.ts:16-17,44-50`, `src/components/NavRail.vue:24-29`, and `src/router.ts:69-71` independently read the two directories.
3. Before C06, frontend diagnostic readiness at `gyroVirtualFeature.ts:56-58` returned `virtualGamepad || gyroMotion`; `inputDiagnosticsUi.ts:13-29` used that value to enable the diagnostics bus and persisted logging setting. This was inconsistent with the native atomic gate.
4. C06 source correction changed only the diagnostic readiness predicate to `virtualGamepad && gyroMotion`, with an inline explanation that a partial package must not advertise capture native will refuse.
5. `package-release.ps1:228-240,458-460,571-572` copies and requires both test asset directories in the complete test package. The current `Mainline/Release/Packages/YeManCC.zip` contains six related entries (`enabled.flag`, `input-capture.flag`, `.test-sidecar.json` for each directory).
6. The generated updater template in `native/main.cpp:16686-16694,16777-16779` excludes both directories from rollback registration and installation copy. This proves install-copy exclusion in the template, not a completed live-updater run.
7. Frozen HC ROG evidence: `XboxROGAllyX.cs` uses gyro `(1,1,-1)` and accelerometer `(-1,-1,1)` with Y/Z exchange; current native ROG matrix matches this only when `g_rog.identityMatched` is true.
8. Current native ROG stable HID path only discovers and records the input path/length (`native/main.cpp:14638`); `rogTryReadHidGamepad` requires a live handle/preparsed data (`:14426`) and has no call site. HC `ROGAlly.cs:275,294,353` opens the bound HID and runs a report loop. Therefore stable HID read is not connected in YMCC.

## Interpretation (not runtime proof)

- Sidebar per-feature visibility is aligned with the folder-presence rule.
- After C06, frontend diagnostic readiness and native capture/InputHost readiness use the same two-directory atomic condition.
- The package boundary currently means “complete test ZIP contains the two directories; updater installation copy excludes them.” It does **not** prove that a live upgrade run removes stale directories already present in an installation, nor that a separately published production ZIP cannot contain them.
- HC matrix parity is statically shown for the identified ROG model only. It is not evidence for MSI/Lenovo or unknown devices.

## Unknown / gaps (remain open)

- No build/type-check/selftest or runtime execution was performed after C06.
- ROG stable HID open/read, Coordinator ownership, suspend/PnP/crash recovery, and XInput consumer isolation remain `UNENCLOSED / T13-ROG-STABLE-HID-READ-MISSING`.
- HC `GetDefault()` sensor selection versus YMCC “first sensor” selection and HC event scheduling versus YMCC polling remain `UNENCLOSED`; no axis-offset change is inferred.
- `GYRO-VIRTUAL-EVIDENCE-INDEX-20260903.md` is a historical 2026-09-03 package record whose “zero matching entries” result cannot be used as a current-package claim. Its history was retained. The current `tools/updater_policy_selftest.ts` had an obsolete opposite assertion; `T22-C07` updates that static policy assertion to require the two directories in the complete test ZIP and the updater-exclude declaration. It is not a live updater test.
- T15/T16/T17/T18/R1 remain runtime-blocked; no consumer, descriptor, ACK, epoch, release, or recovery proof is created by this artifact.
