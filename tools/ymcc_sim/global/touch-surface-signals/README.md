# Touch surface signals

This bounded extension reuses the existing global simulator. `touch-surface.mjs`
exports `runTouchSurfaceRegressions({repoRoot?,workDir?})`. `adapters.mjs` exports
pure static `getTouchSurfaceAdapters()` and `TOUCH_SOURCE_INPUTS`; it performs no
imports, IO or process operations. The parent owns registry/CLI integration.

The proposed adapter is `controller.touch-surface`, domain controller, node,
behavioral-mock, desktop/full, timeout 180000ms. It runs:

```text
node tools/ymcc_sim/global/touch-surface-signals/touch-surface.mjs --work-dir <scratch> --output <touch-surface-result.json>
node --test tools/ymcc_sim/global/touch-surface-signals/touch-surface.test.mjs
```

Use `YMCC_TOUCH_WORK_DIR` to give the tool tests an explicit scratch directory.
The descriptor uses `--work-dir {outDir}/touch-work --output
{outDir}/touch-surface-result.json`, resultFiles `['touch-surface-result.json']`.
All 37 conservative source fingerprints are enumerated statically.

The native consumer suite runs byte-identical production Core/config/transport,
output ownership, release, configure, shutdown and owner resume functions. It
includes the actual auto-hide, visibility, display policy, haptic dispatch and
feedback lease headers. Only platform IO, compositor/render/cursor and button
surfaces outside this slice are memory leaves. Eligibility, config parsing,
source generation, output prefix ownership, cancellation, timer identity and
reorder logic are production implementations, not substitute state machines.
Each new scenario records input, actual consumer, state and feedback evidence.

Original assertions are retained unchanged:

- `tools/screen_touchpads_event_time_selftest.cpp`: all 10 assertions, actual full
  production header. Three unused bitmap-cleanup leaves are memory substitutes,
  preventing GDI singleton destructors from importing graphics APIs.
- `tools/screen_touchpads_feedback_lease_selftest.cpp`: all 44 assertions, actual
  FeedbackLease/dispatch/recovery with the original MockOps.
- `tools/screen_touchpad_partial_send_selftest.cpp`: all 37 assertions and 230400
  fault cases, using the same existing source extraction boundaries and IO fakes.
  Haptics is deliberately outside that old output-prefix fixture; the new native
  suite separately executes the actual haptic state and a counted memory sink.
- `tools/screen_touchpads_selftest.ts`: unchanged assertions directly bundled
  against the production bridge. A separate native-IPC test runs the unmodified
  screenTouchpads/ipc modules and the actual C++ ProfileBank/parseConfig.

Builds use the installed cached MSVC/C++20/Unicode SDK configuration, reused from
`tools/fixtures/hc_reviewed_calibration/toolchain.mjs`. Strict linkage plus an
import audit rejects actual input, hook, GDI, shell/OLE and device libraries
before every executable is run. Program IO is limited to owned fixture/report
files and stdin/stdout. No historical time is waited; time/timers are in memory.

The native-IPC replies explicitly say `settingsSaved:false`, physical target
capabilities false and reason `owned-memory-config-only`. A successful parser,
report-prefix or memory feedback acknowledgment does not prove physical input,
rumble, target delivery or actual window visibility. No settings/session files,
process command lines, normal App, Host, OS sleep or hardware are used.

The real WinRT independent pointer-source class, actual HWND/composition hit-test
and mouse-pass behavior, device output, cursor rendering and button dispatch
remain unexecuted boundaries. Unsafe old physical/topology/owner scripts that
call InjectTouchInput or manipulate real windows are deliberately not run.
No product defect was reproduced in this slice; `sourceChanges` stays empty.
