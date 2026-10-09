# SteamDeck desktop mouse sensitivity — 2026-10-05

## Implemented

- Controller virtual-output card shows SteamDeck mouse sensitivity only for enabled `steamdeck` persona: 1–300%, step 5, initial default 100%. The range/default footnote and reset button are removed.
- Mouse backend display order: Microsoft mouse / JoyXoff / SteamDeck mouse (微软鼠标 / JoyXoff / SteamDeck鼠标). The duplicate Steam mouse entry is removed. SteamDeck mouse remains read-only and highlights the selected mode without adding an unsupported native mouse-backend enum.
- Existing Steam overlay/sleep-fix helpers locate Steam and account data and detect `steam.exe`.
- Writes target ONLY an existing selected Neptune desktop autosave: `configset_controller_neptune.vdf` must select `413080` with `autosave=1`, no external template/workshop reference; corresponding `413080/controller_neptune.vdf` must identify `controller_neptune`.
- The Default/id=0 preset's unique `right_joystick active` binding selects the group dynamically (no hard-coded group id). Only its `joystick_mouse/settings/sensitivity` scalar and the top-level layout `revision` save metadata are changed. Revision increments once for a real value change, matching the supplied Steam save; no-op leaves bytes unchanged. Shared groups, mode shifts, malformed/ambiguous data, unsupported modes and invalid scalars are refused.
- Read actual file values (including an existing 323%); missing scalar uses requested default 100. UI writes remain 1–300%. No initialization/default write or silent clamping.
- Steam running: prefer the verified Steam UI API, with runtime and Steam-owned file readback. Persist intent before any mutation; only unavailable debugging/API/device permits deferred fallback. Dirty editors, conflicting layouts and uncertain mutations are refused rather than queued. Steam stopped: revalidate account/path/group/baseline, back up the full original to `.ymcc-bak`, atomically write, verify actual readback. No Steam stop/restart, no default-template edits, no unrelated bindings rewritten.
- Account/layout changes while waiting cancel instead of overwriting Steam/user edits. Native-owned pending receipts survive stale full-document UI writes. AI isolation cannot access ambient Steam files.

## Evidence and limitations

Local Steam's `controller_base/desktop_neptune.vdf` identifies `controller_neptune` and Default preset binds group 25 to `right_joystick active`; group mode is `joystick_mouse`. Existing local controller autosaves use numeric `settings/sensitivity` scalars. Local Steam UI associates joystick-mouse sensitivity with percent display.

User supplied `config.zip` at displayed **100%** and `config137%.zip` at displayed **137%** on 2026-10-05. Both archives contain 89 files; only `config/413080/controller_neptune.vdf` differs. The sole two scalar changes are top-level `revision` **41 -> 42** and Default/right-joystick group 25 `settings/sensitivity` **100 -> 137**. `configset_controller_neptune.vdf` is byte-identical and selects app **413080**, `autosave=1`. The layout autosave URL agrees with that account-specific location. Therefore the actual selected file, group/mode and direct integer percentage encoding at both values are now verified, not inferred from default templates.

Redacted real-layout fixtures are saved under `tools/fixtures/steamdeck-mouse`. The production transformation from the actual 100% file to 137% is **byte-for-byte identical** to Steam's actual 137% save (after identical account/URL redaction), including the revision increment. Production runtime pending/apply/backup/readback is also tested against those fixtures.

Remaining boundary: the test runs against copied layouts and mocked process/registry/file APIs. It does not prove physical ROG stick movement, or that a live Steam installation loads YMCC's external edits. No more configuration samples are needed for the field encoding. The currently implemented file-based path still requires normal Steam exit for the queued write, then Steam restart and UI/physical-stick verification on the ROG. Those fixture/build tests did not overwrite an installed release or original Steam config. A separate live API experiment below subsequently changed and restored the generic-controller config using Steam itself; the verified route has now been integrated into the YMCC source, as documented below.

## Validation

- `node tools/controller_shortcut_ui_merge_selftest.mjs`: 24 checks, mocked native/storage; 0 hardware operations.
- `node tools/controller_shortcut_ui_merge_browser_selftest.mjs`: 13 checks in real Chromium/Vue; layout at 1280x720, 1280x800, 1920x1080, 580x800, 420x800; disabled Deck indicator cannot receive focus; native/storage mocked.
- `powershell -File tools/steamdeck_mouse_native_selftest.ps1`: production runtime extracted verbatim plus production VDF header, mocked process/registry/file APIs; includes the real 100%/137% archives, exact Steam-save reproduction and revision handling; covers percent boundaries, narrow edits, pending/success/failure, account drift, concurrent Steam edits, durable write failure, isolation and corrupt receipts.
- `powershell -File tools/steamdeck_mouse_evidence_selftest.ps1`: mocked registry/process and fake Steam files; validates ZIP contents, redaction, last-account fallback and byte-identical original files.
- `npx vue-tsc --noEmit`; Vite production build into the isolated scratch directory.
- Native Windows build into the isolated scratch directory; does not deploy to `C:\SOFT\YeMan`.

## Real-time Steam UI API experiment — 2026-10-05

Status of initial experiment: verified on this PC before integration. **Now integrated into source** (see production section below), still not deployed to `C:\SOFT\YeMan`. No additional ROG configuration samples are needed. Real SteamDeck hardware and physical cursor velocity remain untested.

### References and live route

- SteamDeckHomebrew's own internal SteamClient.Input type declarations list `StartEditingControllerConfigurationForAppIDAndControllerIndex`, `SetEditingControllerConfigurationSourceMode`, `SaveEditingControllerConfiguration`, and `SetSelectedConfigForApp`: https://github.com/SteamDeckHomebrew/decky-frontend-lib/blob/main/src/globals/steam-client/Input.ts
- Chrome DevTools Protocol `Runtime.evaluate` documentation: https://chromedevtools.github.io/devtools-protocol/tot/Runtime/
- Exact installed store behavior was inspected in `D:\GAME\STEAM\steamui\chunk~2dcc5aaf7.js`; this is an internal Steam UI interface, not a documented stable public desktop-sensitivity SDK.
- Steam already had CEF remote debugging enabled. The loopback listener at `127.0.0.1:8080` belonged to Steam's own main webhelper process. No debugging option was enabled, and Steam was not stopped or restarted for the experiment.
- Select the `SharedJSContext` page under `https://steamloopback.host/`, not an arbitrary UI target. Call the existing `controllerConfiguratorStore` wrappers to let Steam serialize native messages and perform its own save/reselect/apply flow:
  1. `EnsureEditingConfiguration(413080, controllerIndex)`; await its update promise.
  2. Inspect a JSON-cloned `EditedConfiguration`, verifying app/controller/autosave URL and the current sensitivity baseline.
  3. `SetControllerSourceMode(413080, { action_set_key: 'Default', source_binding_key: 12, modeid, mode_shift, new_setting: { key: 30, int_value: desired } })`; await native update and verify readback.
  4. `SaveEditingConfiguration(413080, false, callback)`.

Runtime source key `12` is the right joystick; setting key `30` is sensitivity; runtime mode `7` is joystick mouse. These keys are not VDF group IDs. The tested generic VDF uses group 6, while the user's supplied Neptune layout uses group 25.

### Observed results, not inferred activation

- Steam identified the user's local virtual device as `controller_generic`, controller index 0/type 30, name `STK-7024X`; this was not a Neptune-device test.
- Baseline: right joystick sensitivity 323%, VDF revision 3.
- Steam-native mutation 323% -> 322% returned native readback and the save callback in approximately 201 ms; Steam saved sensitivity 322/revision 4.
- Restore through the same native route 322% -> 323% took approximately 145 ms; Steam saved sensitivity 323/revision 5. Final runtime readback was 323, with no dirty editor state.
- At 11:43:00 and 11:43:01, Steam's controller log recorded in-place active mapping modification, activation requests and activation queuing for desktop app 413080. Steam PID 17572 and its start time remained unchanged throughout the experiment.
- Baseline and restored file differ only in revision 3 -> 5; all original layout bindings and sensitivity are restored. No external VDF write or manual cloud-cache edit was used.
- This verifies runtime acceptance, Steam-owned persistence and activation requests. It does not measure physical mouse velocity or prove a specific end-to-end latency for a moving stick.

Local evidence: `G:\YeManCC-Work\_scratch\steamdeck-mouse-20261005\live-cdp` contains the before/temporary/restored VDFs, mutation result JSONs, `final-live-readback.json`, filtered Steam logs and `live-proof-report.json`.

### Safety constraints and remaining hardware boundaries

- Match the connected device dynamically by `ControllerStore.GetControllerTypeString(type) === 'controller_neptune'`; do not ship generic-controller/index/type hardcoding from this experiment.
- Validate the debugger listener's owning Steam process, loopback-only transport, exact desktop account/layout identity, and the user's current baseline; expose only fixed, whitelisted operations, not arbitrary JS evaluation.
- Refuse dirty/pending Steam editor state, preview mode, layout/mode ambiguity and concurrent account/controller/layout/user changes. Bound all waits and run transport off the UI thread.
- Verify native runtime readback and Steam-owned persisted file. Report accepted/saved/activation evidence distinctly; never treat an HTTP response or file change alone as proof of physical effect.
- Keep the requested UI write range 1–300/default 100. Do not silently clamp an existing value like 323. The tested generic runtime advertises min 10/max 10000/default 275; Neptune API lower-bound acceptance still requires its own test, so requested 1% live acceptance must not be assumed from generic metadata or mocked boundary tests. Native readback rejects an unaccepted/clamped value without saving it; a failed/uncertain edit is not replayed through file writes.
- If CEF debugging is unavailable or internal APIs change, do not silently enable it, open network ports or restart Steam. Report realtime unavailable and retain an accurately labeled queued/offline fallback where safe.

## Production realtime integration — 2026-10-05

### Included source

- `native/steam_live_cdp.h`: WinHTTP HTTP/WebSocket client dynamically discovering this Steam installation's own loopback listeners (IPv4/IPv6, no fixed port), validated TCP ownership by its webhelper and Steam parent, nonredirecting requests, a single verified SharedJSContext, bounded response size/stage deadlines and a socket cancellation watchdog. No Node/dependency process is launched by YMCC.
- `native/steam_live.js` and generated `native/steam_live_script.h`: fixed whitelisted Steam-store operations. Verify Steam's own CM account ID, exact autosave URL, dynamically selected controller type/index, Default/right-joystick binding, unchanged sensitivity, clean editor and unchanged controller before/after operations. A dirty/previewed/game editor is not hijacked.
- `native/main.cpp`: mouse IPC always runs on the worker pool, including when optional async mode is disabled; startup/timer retries and the sleep-switch kick only enqueue workers. The sleep switch uses `settingsStore.GetClientSetting('enable_overlay')[1](enabled)`, Steam's own protobuf serializer and native settings callback, not hand-written protobuf or a running-Steam file patch.
- Steam save clears the controller editor's active app ID to `-1`. Post-save readback therefore additionally validates `StableAppId === 413080`, controller index/type and exact autosave URL, rather than incorrectly requiring the editor to remain open. This was discovered and corrected with actual live testing.
- The mouse's durable `live-inflight` phase is written before a native edit. A crash, uncertain transport or failed completion receipt cannot replay that edit or turn into an offline VDF overwrite. Storage recovery can safely cancel the receipt. Disabling SteamDeck also cancels pending intent.
- `src/components/SteamDeckMouseSensitivity.vue`: 180ms trailing debounce while dragging plus immediate commit on release; serialization/coalescing keeps only the newest waiting target. The slider remains draggable while saving; disabled/unmounted views cancel unsent requests. Mount/read/events never write a default or clamp 323%.
- `src/components/SteamOverlayFixStatus.vue`: distinguishes realtime overlay off/on, offline save, pending fallback and error/uncertain status. Enabling the repair means overlay off; disabling it restores overlay on, preserving the existing switch semantics. Realtime overlay preference confirmation is not proof that an already-running game's injected overlay is unloaded or that the physical sleep/wake failure is solved.

### Subsequent actual-PC evidence

- Production JS changed the actual generic-controller desktop sensitivity **323 -> 300**, with native readback/save callback in approximately **118ms**, then restored **323** through Steam. A preliminary guard test also performed the same temporary change and restore; only legitimate revision increments remain. No sensitivity change was left behind.
- The same production C++ WinHTTP transport and embedded JS read back generic sensitivity **323** and overlay **0** from the real Steam process.
- An explicit C++ transport overlay roundtrip successfully switched **0 -> 1 -> 0**, with readback after each step. The first independent settings-wrapper roundtrip took approximately 144ms for the first change. No Steam restart or debug-option change was required; debugging was already enabled on this PC.
- Final live controller state is clean, sensitivity 323%; final overlay preference is off. Physical mouse velocity, ROG/Neptune live hardware, and actual wake-after-suspend recovery remain unmeasured.
- Tests and builds remain isolated. The installed YMCC release at `C:\SOFT\YeMan` was not replaced.

### Added validation

- `node tools/steam_live_script_embed.mjs --check`: embedded/source byte agreement.
- `node tools/steam_live_script_selftest.mjs`: 24 production-JS contract checks with mocked Steam stores; covers source/setting keys, no-op reads, requested boundaries, account/layout/editor/device conflicts, native rejection, save closing the editor, concurrent changes and timeout/lock release. These boundary checks are not Neptune hardware evidence.
- `tools/steamdeck_mouse_native_selftest.ps1`: 80 checks; extracts both production mouse and overlay runtime blocks verbatim; additionally covers live versus deferred/offline status, backups, isolation, inflight durability, completion-receipt failure, no replay on uncertainty and persona cancellation.
- `tools/controller_shortcut_ui_merge_selftest.mjs`: 24 real Vue/mock tests; covers debounce/coalescing, slider enabled during saves, unmount/game-lock cancellation, 323% display and sleep-switch status.
- `tools/controller_shortcut_ui_merge_browser_selftest.mjs`: 13 real Chromium layout/interaction tests with mocked native/storage; no page errors. Includes live drag and queued-drag differentiation, asynchronous latest-value serialization and existing controller UI regressions.
- `tools/steam_live_transport_selftest.ps1`: 14 native transport-selection/policy checks, no network by default. Live readonly and overlay roundtrip require explicit flags/root/account; the roundtrip restores the original setting. Actual live requests on this PC passed.
- `npx vue-tsc --noEmit`, isolated Vite production build and isolated full native Windows build pass. Vite retains the existing large-chunk warning.

Remaining deployment requirement: realtime requires a pre-enabled, loopback-only Steam CEF debugging endpoint (port discovered automatically) on the target machine. YMCC never silently enables debugging or restarts Steam. If that capability is absent, the UI explicitly reports pending/offline behavior rather than claiming realtime success.


## Portability / correct SteamDeck target — 2026-10-05

- The PC's generic-controller 323% is only test data for API discovery and roundtrip experiments, never calibration data or a production target. Production mouse calls always pass `controller_neptune`, choose only `configset_controller_neptune.vdf` / `413080/controller_neptune.vdf`, and verify both VDF controller identity and Steam's dynamic controller type. There is no fallback to `controller_generic` or to an arbitrary attached controller.
- The actual SteamDeck parameter encoding remains the user's 100%/137% ROG evidence: Default/right-joystick `joystick_mouse/settings/sensitivity`, direct integer percent, with Steam save revision increment. The VDF group number is looked up from the binding, not fixed at 25; runtime source/setting/mode keys 12/30/7 are protocol enums, not machine IDs or VDF group IDs.
- Steam install root and active/last account are discovered, not fixed to `D:\GAME\STEAM` or account 89250578. Desktop autosaves may live in a separate Steam library: read only Steam's declared `steamapps/libraryfolders.vdf` roots, modern object and legacy scalar formats. Dedupe case/slash variants; refuse ambiguous selected autosaves instead of guessing a current file. No whole-drive scan or timestamp heuristic for layouts.
- Debug transport now enumerates only Steam-owned TCP listeners under the discovered installation and validates the `/json/list` context. Port 8080 is not a production constant. IPv4/IPv6 loopback aliases are handled; externally bound or ambiguous contexts are refused. No debugging option is enabled and no Steam restart is attempted.
- Native cross-machine fixtures passed C:/E:/F: roots including Chinese folder names, different accounts, a right-joystick VDF group changed to 901, and a separate E: Steam library. Files belonging to the original fake machine stay untouched. Duplicate selected layouts and a generic-only machine are negative tests, not fallback targets.
- Production JS fixtures passed varying controller indices (0/3/8), numeric controller types, native mode IDs, account IDs and paths. The exact autosave URL/type/baseline remains guarded at every target; an uninitialized controller list is treated as no connected target, not a reason to choose generic.
- Transport fixtures passed non-default ports 7777/39217/65000 and IPv6 loopback URL parsing. Actual dynamic discovery on this PC passed readonly controller/settings queries.
- An additional actual-PC **Neptune-only readonly request** returned `live-controller-not-connected` before any edit; the generic VDF's SHA-256 remained unchanged. Steam PID 17572/start time also remained unchanged, sensitivity stayed 323%, and overlay preference stayed off.

Scope: these results establish code/data portability and correct rejection on this generic-only PC; they are **not** a claim that every Steam version or a physical ROG/Neptune live device was tested. ROG realtime acceptance, minimum-value acceptance, physical cursor velocity and actual sleep/wake recovery still require target-hardware validation. Existing ROG archives already suffice for the saved-field encoding; no additional archives are needed.


## Slider presentation adjustment — 2026-10-06

- Pending text is now exactly `已排队100% 重新启动Steam后应用` (with the requested percentage substituted), without current-file details or automatic-restart explanation.
- Removed the `1%–300% · 默认100%` footnote, the `恢复100%` button, and their footer styling. Default initialization remains 100%; reads never write it to Steam.
- User adjustments use a 5% grid anchored at 0, with the 1% lower endpoint and 300% upper endpoint retained. Explicit `Slider` step-grid/keyboard-acceleration options apply only to this component; other sliders retain their native step and accelerated keyboard behavior.
- Pointer changes snap to 5/10/.../300 (plus endpoint 1), not the shifted 1/6/.../296 grid. SteamDeck keyboard direction changes advance one 5% grid point at a time. Existing off-grid saved values such as 137% display unchanged until an explicit user operation.
- No change to the native SteamDeck target, Steam files, live API, offline fallback, or installed application.
- Validation: 28 real Vue/mock tests and 17 Chromium layout/interaction checks; exact pending wording, removed controls, pointer/keyboard 5% behavior, existing-value reads and both endpoints verified. `vue-tsc --noEmit` and isolated Vite build pass.

## Shared event-driven session recovery — 2026-10-06

This section supersedes the previous feature-local 30-second retry timers.

### Root cause and recovery policy

- Previously, a missing/zero `ActiveProcess\ActiveUser` was treated as a different account, even during Steam startup/shutdown. It is now a transient `steam-session-starting` state. Native live preflight also detects PID/root changes across a request.
- `native/steam_session_observer.h` owns one shared waiting thread. It arms asynchronous `RegNotifyChangeKeyValue` before reading Steam state and waits for registry changes, the verified Steam process's exit handle, an explicit user-action event, or shutdown. Deleted/recreated Steam registry keys recover through an ancestor subscription. No WMI instance polling, permanent timer, background Node, fixed Steam installation/PID/port, or idle process/port scans are added.
- Verify the process image against this installation's `steam.exe` and include its creation time in identity, so reused PIDs are not mistaken for the previous session. Unrelated registry changes only re-read the small registry fingerprint; they do not rediscover processes, dispatch CDP work, or replenish the retry window.
- Only an actual session identity transition or explicit user request opens a bounded retry window: up to four extra attempts at 1/3/8/20 seconds. Exhaustion returns to an infinite event wait even while a request remains queued. Late IO/resume skips elapsed deadlines instead of issuing a burst. A failed attempt does not open another window.
- The observer callback only admits worker jobs; discovery, CDP, file confirmation and reconciliation remain off the UI thread. Both features serialize their own live calls against the shared Steam JS context. Serial/generation follow-ups retain events and latest toggles arriving during IO. Startup also retains durable mouse demand if initial worker admission is rejected.
- Same-account Steam restart: rediscover current listeners/context, validate the original account/Neptune desktop autosave/group/baseline, then resume a not-yet-submitted mouse request. The sleep repair follows the current switch intent for the new verified session, including a genuinely different Steam account.
- Different account: keep the mouse request bound to its original account and pause automatic retries for it, without writing the new account's layout or cancelling the request. Returning to the original account is an event-driven resume. An explicit new slider adjustment can replace old intent as before. A changed binding/path/baseline is still refused, not silently adapted.
- Fixed GET operations cannot change the sensitivity scalar. Interrupted transport reads can safely reconnect and remain queued; possibly submitted SET operations cannot replay or fall back to VDF writes. The durable `live-inflight` fence remains intact.
- A late Steam save or matching readback may heal an uncertain receipt by READ ONLY: require the same account/path/group, the desired persisted value and, when Steam is running, the matching live value. This updates only YMCC's receipt, never calls `mouse.set` or rewrites VDF. `via=confirmed` does not claim this particular request caused the change or that physical mouse movement is verified. A nonmatching uncertain result remains protected from replay.
- Remove the old account-cancellation sentence from the frontend mapper. Transient session state reads `等待 Steam 连接后应用。`; a different-account mouse request reads `待办已保留，等待原 Steam 账号连接。`; overlay deferred state reads `已排队，等待 Steam 连接后自动应用。`. The existing mouse queued-percent text and 5% slider behavior are preserved. A recovered native receipt clears a stale local save failure.
- Stop/join the observer before pool shutdown on normal exit, window destruction and the final message-loop fallback. Isolated/mock sessions do not start it or access real Steam.

### Validation and boundaries

- `tools/steam_session_observer_selftest.ps1`: 33 checks against the actual production observer/Win32 notification APIs, using only a unique temporary HKCU test subtree and synthetic child executables in scratch. Covers PID replacement/old-PID exit/new-PID exit, account-zero/recovery, key deletion/recreation, unrelated writes, stop/restart, PID birth identity, bounded retry exhaustion and overdue coalescing. No actual Steam operations. The actual observer also exhausted its 1/3/8/20-second window with pending demand still true, then stopped dispatching retries. It recorded **0 ms process CPU over a 600 ms idle interval**, and **0 ms over a further 1100 ms idle interval after pending-budget exhaustion**. These are short, timer-resolution-limited observations, not a claim that the entire application uses zero CPU on every machine.
- `tools/steamdeck_mouse_native_selftest.ps1`: 101 checks extracting the production mouse, overlay, live preflight and deferral policy. Includes interrupted reads versus uncertain setters, read-only receipt healing, missing account, cross-account suspension/resume, replacement PID, worker rejection, in-flight fencing, baseline protection and the existing cross-machine ROG fixtures/library/path/group matrix.
- Production JS mock suite: 24 checks; transport policy suite: 14 checks; real Vue/mock UI suite: 31 checks; existing real Chromium UI regression suite: 17 checks with no page errors. Typecheck and isolated frontend/native builds pass.
- Scratch output root: `G:\YeManCC-Work\_scratch\steam-session-20261006`. Production installation is not deployed, Steam is not restarted, local CEF debugging is not enabled on the user's behalf, and the generic 323% test layout is not the production write target.
- Real Neptune runtime/hardware mouse movement and physical sleep/wake recovery still require a device test. Event delivery and recovery logic are verified independently; live recovery still needs a usable existing Steam local interface. If it remains unavailable, the request is retained without endless retries; subsequent Steam events or explicit user operations can try again, and a safe unsubmitted request can use the offline fallback on Steam exit.
## Sleep page status text removal — 2026-10-06

- User requested removal of the entire bottom Steam repair status text, not replacement with another hint. Removed the status component import/mount from `SleepGuardView.vue` and deleted the now-unused `SteamOverlayFixStatus.vue` frontend component.
- The repair toggle, its existing description and config-save handler remain. Native live application, pending intent, event-driven self-healing and backend receipt APIs are unchanged. SteamDeck mouse sensitivity UI is unchanged.
- The sleep page no longer makes status-only `steamOverlayFix.get` calls or subscribes to `steamOverlayFix.updated` through this removed component. Removed obsolete visible-status tests and added a regression that asserts complete absence of the status mount while retaining the toggle and save path.
- Validation: 30 real Vue/mock checks, `vue-tsc --noEmit`, and isolated Vite build passed. No deployment to the installed application.