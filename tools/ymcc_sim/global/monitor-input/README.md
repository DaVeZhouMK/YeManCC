# Monitor input regression

## Current native bounded input (2026-10-23)

The normal global adapter compiles the production Carrier, PanelInput and Gesture into owned HWNDs on a private desktop which is never shown. It does not inject system input. The current monitor path creates no Composition input site, independent input source, dispatcher or child, and never explicitly acquires mouse capture. Touch/pen use WM_POINTER on the bounded HWND; mouse uses Windows' legacy compatibility path and a leave notification. The touchpad/radial shared Surface implementation is unchanged.

The default runner selects monitor-bounded-input-consumer.cpp for the new product, while retaining monitor-input-consumer.cpp for historical hybrid-source builds. Private cases cover native mouse/touch/pen, double tap A/B, explicit lock, configured radial release, lost/cancelled contacts, game gating, expiry, bitmap reuse, bounded geometry, input destruction/recovery, notice hide/close and final resource destruction. Only pointer metadata and WebView controller terminals are recorded; production methods are not replaced.

Use the existing global test ID touch.monitor-drag-pass. To compare the frozen hybrid source against the new invariants use --bounded-input --baseline-native <saved-native> --expect-bounded-input-failure. Its actual behavior must fail the three named no-capture/no-input-child/no-independent-runtime assertions; build errors are not a negative result. Old failure flags continue to select the retained historical consumer.

The native carrier retains one bounded input-withdrawal diagnostic before hiding/releasing its live input. The existing desktopHud.closed lifecycle event carries that record, so the normal native-lifecycle.log export preserves it after OFF. It is not sampled on sensor refresh or repeatedly hidden cleanup. Allocation failure in this best-effort diagnostic must never prevent input release; the private fixture faults the ordinary C++ allocator and verifies cleanup. --bounded-input --baseline-native <pre-withdrawal-native> --expect-withdrawal-evidence-failure requires the two actual missing-evidence behavior failures without a compile/infrastructure error.

Real OS routing is a separate explicit command, never included in unattended global runs:

```text
node tools/ymcc_sim/global/monitor-input/monitor-routing-consumer.mjs --desktop-input --real-webview --production-vue --runtime <existing-runtime-directory> --evidence-dir <fresh-directory>
```

This freezes and builds the actual HUD/notice entries, Vue components and native Carrier on the specified installed runtime. The lower receiver is a second owned process; real SendInput/InjectTouchInput packets stay inside its rectangle. Cursor movement or unowned targeting stops the test. It checks panel geometry from the actual renderer, full-screen display with/without drag input, real touch/mouse dragging, outside clicks after a deliberately dropped monitor UP, notice visible/hidden/destroyed and browser natural exit. A dropped delivery is injected only into the owned monitor queue; Windows still receives the physical button release. No input is forwarded/replayed to another application by product code. Without --production-vue this explicit runner uses a minimal handshake document and must not be described as an actual Vue layout test.

Runtime bytes and native/frontend source hashes are preserved and checked for drift. Failed trials remain in their original directories. This is not an actual UU session or physical G3E acceptance; an old build can also pass ordinary outside clicks, so local PASS does not itself explain every field failure. The October 23 field archive really has a newer native EXE (576d3189...), unlike the older October 22 archive; its failure must not be dismissed as the old binary.

## Pointer metadata loss cancellation (2026-10-23)

The native bounded replay also injects failure at the existing GetPointerInfo and ScreenToClient boundaries for fixture-owned touch/pen IDs. An unreadable UP/UPDATE for the current contact must withdraw that contact through the existing stopInteraction cleanup, hide the input window, clear drag/radial state, and permit a later normal refresh/new contact. An unrelated ID cannot cancel the owner. An armed long press followed by metadata loss and late timer/UP cannot summon the radial. The preserved original source fails the contact-release/new-contact cases; failures do not establish that this was the sole cause of earlier physical/remote desktop blocking.

The existing private runner accepts --bounded-input --baseline-native <pre-pointer-metadata-native> --expect-pointer-metadata-failure for this regression. It requires the eight touch/pen UP/UPDATE owner/next-contact failures plus coordinate and armed-radial failures, rejects unrelated or infrastructure failures, and records the exact frozen sources. This explicit negative mode is not a product input behavior.

## Actual production monitor haptic ingress (2026-10-23)

The existing native --screen-touchpads-haptics-output-selftest now holds g_gamepadReadMx on an owned fixture thread and calls the actual screenpads::monitorHaptic -> nativeScreenTouchpadHapticPulse -> nativePhysicalFeedbackReady path. The callback must return while the sampling lock remains held. A timeout releases the test lock so a blocking baseline fails behavior rather than hanging. No physical controller I/O, normal app startup, installed app restart or CPU benchmark occurs. The XInput readiness check now uses try_to_lock and returns the existing source-pending result if sampling is busy; there is no polling or queued replay. Shared touchpad/haptic modules are unchanged.

Explicit reviewed native-candidate consumer:

```text
node tools/ymcc_sim/global/monitor-input/monitor-haptic-ingress-consumer.mjs --candidate-native <reviewed-build-directory> --evidence-dir <fresh-directory>
```

The wrapper requires a matching build receipt, frozen main.cpp and EXE hash, and the existing early-return test entry. It never runs an installed EXE or normal startup. --expect-blocking-baseline requires the new monitor ingress assertion to fail. Two DInput acquisition assertions also fail in the earlier unmodified native candidate; the original failures and complete native exit code 3 remain preserved. The explicit --known-suite-baseline-native option identifies those exact pre-existing failures from that candidate's real output and exposes fullHapticsSuitePassed=false. A scoped monitor PASS must never be described as the complete haptics suite passing, and the normal full-suite verifier still rejects it.

## Input rectangle and touch targeting boundary (2026-10-23)

The monitor registers client touch hit testing and rejects a touch point outside both the actual input HWND and the visible panel model, even if its contact ellipse overlaps. The existing placement callback verifies SetWindowPos results and verifies geometry again after layered input work. A mismatch withdraws that input and uses the existing bounded recovery path on the next admitted update. No added timer or process/game scan is involved.

The private consumer uses an owned window-position callback to enlarge only one native placement and checks that the input is withdrawn rather than retained outside the panel. The explicit real-routing consumer performs the same fault on its owned input HWND, then sends real mouse/touch packets outside the visible panel into the second owned process. Before code receives no DOWN/UP there; corrected code delivers both. This is a controlled Windows placement fault, not proof of how G3E/UU produced the field error. Its live failure geometry is emitted through the existing inputUnavailable event before successful recovery can replace the later close snapshot. --expect-input-boundary-failure validates the preserved pre-boundary source against the four touch-target assertions and placement withdrawal assertion; infrastructure failures do not count.

## Full-screen display admission (2026-10-23)

A mouse-mode status object is not a reason to make the native display HWND monitor-sized. The native Carrier now derives full-screen geometry only from an actual crosshair preset (dot/cross/gap/ring/ring-dot). Ordinary monitoring and mouse-status packets keep the display HWND content-sized; this removes a full-screen disabled/transparent window from the normal click path. The Vue renderer follows the same rule. Crosshair mode retains the full-screen display required for screen-center alignment.

The real WebView2 routing fixture records this case with mouse mode present and crosshair none, and requires the native display rectangle to remain panel-sized.

## Historical hybrid-source fixtures (through 2026-10-22)

The following describes the retained historical consumer and receipts, not the current monitor input mechanism.

The default global test directly compiles and executes the production `Carrier`, `screeninput::Surface` and `huddrag::Gesture` on a private desktop that is never switched into view. It injects no system input and writes no hardware output. The fixture exposes private test access by inserting a single `public:` label; it replaces no production method bodies.

`pwsh -NoProfile -File tools/ymcc_sim/ymcc_sim.ps1 -Domain global -Mode run -Tests touch.monitor-drag-pass -EvidenceDir <fresh-output>`

The same consumer can optionally exercise real Windows input on a small owned desktop window with `node tools/ymcc_sim/global/monitor-input/monitor-input-consumer.mjs --evidence-dir <fresh-output> --desktop-input`. It sends mouse/touch packets only inside that window, stops on unrelated focus or cursor movement, closes its windows and restores the pointer when still owned. This explicit mode is never in the global unattended adapter. Its callbacks and actual panel rectangles prove small touch displacement, continuous dragging beyond the original input rectangle, mouse clicks/wheel/drag reaching the lower window, and double-tap template switching and explicit settings lock/unlock.

A preserved pre-fix native directory can be supplied with `--baseline-native <directory> --expect-baseline-failure`. Exactly the two original behavior failures are required: mouse is admitted to drag, and changing input-window origin amplifies touch coordinates. A compile error or unavailable desktop does not count as reproducing the bug.

No physical touch hardware, gameplay, haptic motor, install or release is claimed by these software tests.

## Monitor hold release contract (2026-10-21)

The same private consumer now verifies that the monitor hold timer only arms the current contact. Only the matching pointer UP after the configured duration may summon the wheel. It also covers repeated timers, late/duplicate UP, a second contact, cancellation/hide, drag after arming, locked monitors, custom durations and pen input. No additional polling or real input injection is used.

For a private fault copy that wrongly summons from the threshold timer, run the existing consumer with `--baseline-native <fault-directory> --expect-radial-hold-failure`. It must compile and exit through the two named behavior assertions (threshold waits for release; repeated timer cannot summon). Infrastructure/compile errors do not count as rejecting this fault.

The existing feedback consumer also samples the actual monitor keyframes: hold, hide and return must preserve the panel rectangle. The armed border remains until release/cancellation; the display renderer never requests a wheel itself. Trusted touch submenu return, stationary wheel entry/exit and both 720/800 layouts are covered by the existing ui-navigation radialTouchCases.

## Display/notice hidden and destruction boundary (2026-10-22)

The same private native consumer now records only the `ICoreWebView2Controller` COM terminal and executes the unmodified production Carrier hide, refresh, lease timer, frame and close methods. It checks hidden pre-ownership, fresh layout, explicit hide, expiry, external hide and destruction while another actual controlled input surface stays active. Main HUD game visibility/expiry also revokes renderer visibility and bounded input. The COM recorder is not an actual WebView2 or physical input acceptance.

Use `--baseline-native <old-native-directory> --expect-notice-lifecycle-failure` to prove the former controller-visible-after-hide/close policy fails the named behavior checks. Build failure is not accepted as a negative result.

The existing feedback consumer also runs `notice-lifecycle-consumer.mjs` against the real notice entry, Vue and geometry lane. Hidden same-size and same-receipt appearance refreshes must confirm each new frame sequence; stale sequences stay rejected. No telemetry, input hook, polling or installed-app writes are involved.
## Monitor capture withdrawal (2026-10-22)

The existing native consumer also acquires real independent-source capture on its never-switched private desktop. Ordinary hide, external input-window hide, WM_CANCELMODE, game gating and lease expiry must release that exact source; a queued DOWN cannot restart a hidden input owner. Separate source cancellation and destruction must preserve another active source's capture. Full-screen mouse-status display must retain a panel-sized input rectangle. These are actual Carrier/Surface/Win32 calls without system input injection.

Use `--baseline-native <preserved-native-directory> --expect-capture-withdrawal-failure` to require the eight named behavior failures. Compilation or infrastructure failures are rejected as negative evidence. The optional visible owned-window scenario keeps a stationary injected touch alive with UPDATE packets and checks the current contract: long hold with radial disabled stays unlocked, while only explicit settings lock removes interaction.

Capture is released only after confirming ownership. Calling an independent source's ReleasePointerCapture unconditionally can revoke another source on the same UI thread; that failed candidate and the pre-fix failures remain in the task evidence. These tests do not reproduce or certify the user's physical G3E Explorer desktop.


## Independent HWND destruction (2026-10-22 follow-up)

The same private native consumer now destroys the actual monitor input HWND or display HWND in three independent rounds. Input-window destruction must cancel its contact/capture, close its owned controlled source and retire the stored HWND; a later admitted refresh can recreate the bounded input. Display destruction must close its controller terminal and independent input together, and a late refresh must not recreate that retired owner. Another real monitor input owner remains active throughout. No system input is injected. The WebView controller in these additional cases is a COM recorder, so this is not actual WebView, physical Explorer desktop or Xbox acceptance.

`--baseline-native <preserved-native-directory> --expect-window-destruction-failure` must compile, execute and fail the named window-loss behavior assertions. The frozen before source reproduced twelve failures across three rounds; the current consumer passes 88 cases. Infrastructure failures remain failures and are not accepted as negatives.


## Ordinary mouse route and remote message compatibility (2026-10-22)

The private native consumer sends real Win32 mouse messages only to its own monitor input root and child, without system input injection. The monitor uses Touch/Pen on the existing independent source and ordinary mouse messages on the bounded HWND; the radial/pad default source modes remain unchanged. Down is admitted within the panel, own Win32 capture lasts through the press, and UP, capture transfer, cancel, a released-button move or a replacement outside press retire it. It verifies child-addressed messages, double click, explicit lock, five hide/show cycles, no activation, outside point hit testing and preservation of another controlled source's capture. Touch-promoted mouse messages are excluded to avoid double delivery.

`--baseline-native <saved-native> --expect-native-mouse-failure` must compile and execute the named behavior failures. Old direct Carrier event replays formerly passed screen coordinates where the actual input API publishes local coordinates; those two initial cases now use local coordinates, with their movement assertions retained. The new checks cover the actual window message boundary rather than counting direct gesture calls as remote client acceptance. The fixture now enables MouseInPointer as production does, and executes the production native PT_MOUSE branch with only frame metadata provided by an inert GetPointerInfo terminal for one fixture pointer id. Normal HWND/capture and independent touch sources stay real. It covers root/child Pointer delivery, Pointer cancellation and capture loss, and rejects touch duplication/secondary-button capture. This metadata terminal is not actual OS pointer injection or UU traffic.

This is neither a real UU session nor physical Explorer click acceptance. The provided archive still reports native hash b57102ebcbfea3ec0f7ec7607427eb0b7b0115b351826ce5303d58efb536b9ab and process 6300, also present in the earlier export. No installed program is restarted or modified by this fixture.


## Input geometry between telemetry updates (2026-10-23)

The bounded native consumer now mutates the actual input HWND independently of `Carrier::refresh`: combined placement/size, size-only and move-only changes must remain inside the current visible panel immediately. `WM_WINDOWPOSCHANGING` constrains the proposed rectangle. `WM_WINDOWPOSCHANGED` withdraws the input owner synchronously if `SWP_NOSENDCHANGING` or a downstream window procedure bypasses the pre-guard. The existing final placement checks remain, and refresh must not reuse an input HWND retired by the synchronous callback. There is no added timer, thread, scanner, input hook or telemetry request.

The existing placement fault procedures deliberately alter geometry **after** forwarding `WM_WINDOWPOSCHANGING`, so the fallback and live mismatch diagnostic are still tested rather than being silently corrected by the new pre-guard. The default private consumer also checks recovery and preservation of another monitor owner. `--baseline-native <saved-native> --expect-window-mutation-failure` requires actual execution with the three named external-mutation behavior failures; compilation/infrastructure failure is rejected.

The explicit real-WebView/production-Vue routing fixture adds actual lower-process mouse/touch delivery checks after both ordinary independent geometry mutation and a pre-guard bypass, before another telemetry refresh. It retains the ordinary drag, lost-UP, edge-touch, notice-hide/destroy and natural-browser-exit checks. Only fixture-owned windows receive injected input; this remains distinct from a physical touchscreen or UU remote session.

`desktopHud.closed` now includes `inputRelease` after verified native HWND destruction and controller release, alongside the earlier `inputWithdrawal` snapshot. The private and actual WebView consumers require both input/display destruction, released input terminal/controller, cleared contacts and no retained capture. An archive lacking this new receipt cannot prove post-destruction cleanup merely from its pre-cleanup snapshot. The earlier ROGX 08:34 archive remains a field baseline, not acceptance of this subsequent patch.


## Display/input alignment and external display loss (2026-10-23)

The displayed monitor and interactive panel are independent top-level windows. The native consumer now moves the display alone while a mouse contact is admitted: the old input rectangle must immediately become hidden and its contact must be cancelled. An ordinary data update may restore input only after actual display placement matches the intended panel (or the full screen when a crosshair is active). A deliberately altered display placement must keep both display and input hidden. External display destruction must close the input/controller and a late refresh must not revive that retired owner; another monitor remains active.

The production display `WM_WINDOWPOSCHANGED` handles independent movement without a new poll. The existing refresh verifies actual display placement before syncing interaction. `--baseline-native <saved-native> --expect-display-movement-failure` requires the named independent-movement failure from actual product code, not an infrastructure error. The explicit real-WebView routing consumer also moves its actual native display, proves the vacated panel area receives mouse/touch DOWN and UP in the independent lower process, and then verifies aligned recovery. These software cases still do not certify UU/physical touchscreen field behavior.


The routing fixture accepts `--browser-exit-fault` only in its explicit desktop-input/real-WebView mode. It identifies the browser returned by its own newly created WebView environment/profile, opens only that browser with termination rights, admits an actual monitor mouse DOWN, and terminates that fixture-owned browser. It requires the production ProcessFailed path to destroy both HWNDs and clear contacts, verifies actual lower-process mouse/touch delivery, and rejects late owner revival. This never targets an installed YMCC or an unrelated browser. Its exit check is named as a fixture-terminated exit, not natural browser exit. Normal routing separately requires natural browser exit.

A retained 2026-10-23 attempt exited with a browser crash after the new display-movement cases. Its dump exception address is in RTSSHooks64.dll (0xc0000005, offset 0x14916f); that address alone does not identify the cause of memory corruption or prove field causality. The attempt remains failed and separate from the subsequent normal and explicit browser-exit-fault receipts.
