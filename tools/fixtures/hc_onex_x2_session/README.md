# OneX X2 Session offline contract tests

Run from the mainline source root:

```powershell
node tools/hc_onex_x2_session_selftest.mjs
```

All generated source copies, binaries, logs and JSON receipts are written to a fresh directory below `G:/YeManCC-Work/Build/Validation/HC-ModelCapabilities-20261017/OneXApplication/Session-R3`. `--out` is confined to that tree and refuses to overwrite an existing receipt. `--vcvars` may select an already installed local MSVC environment. No downloads or packages are required.

The tested objects are frozen copies of the actual `native/onex_x2_session.h` and its pure protocol dependency. MSVC Release and Checked are compiled with C++17 `/W4 /WX /permissive-`; Checked also uses `/RTC1` and checked iterators. Test checks remain enabled in Release. The candidate headers and all oracle/test inputs are hashed before and after execution; concurrent header edits invalidate the run instead of being silently accepted.

## Independent expected values

`expectations.mjs` contains the user's session timing/safety requirements as literals. It imports the previous protocol oracle, which independently parses four SHA-256/Git-blob-pinned HC C# files at `a250b19a4b7e29dac3b383f2d1ac517d1af43e4e`. Initialization page content, delays and final inherited button roles are derived from those files. The oracle does not read candidate headers or candidate output. Input fixture framing and expected output bytes are built in the harness from that independent contract; it never calls the production encoder/decoder to generate expected values.

The status contract applies a 200ms debounce after the later of the status time and the preceding reinitialization's 2s minimum gate. This and the initial 4000ms wait are driven by virtual timestamps, with no sleeping or wall-clock dependence.

## Coverage

- All three new OneX models, every source initialization page, output sizes 64/65, exact completion sizes, 50ms page delays measured from terminal completion and final initialization gate.
- Explicit input layout independently crossed with input/output lengths; Unverified, bad enums, bad lengths, opposite framing, nonzero report IDs and malformed payloads never guess a layout from output size.
- Exact pinned HC HidLibrary 3.2.49 DLL blob and static constructor evidence; all 256 observed raw Report IDs at raw64/raw65 independently crossed with output64/output65, general one-byte strip versus optional strict-zero layout, minimum capacity15, and fresh-binding frame/Report ID reset. The DLL is hashed, never loaded. Session LogicalFrame remains tested as an already-normalized pure-memory input; Windows ReadFile wrapper layout admission is outside this test.
- All 256 button IDs, repeated front clicks, all accepted press/release values, rear state/idempotence and two held rear bits. Event role/context/generation/time/vendorButtonId are checked against independent expectations.
- Queue capacity exactly 128, overflow revocation, full-queue duplicate DOWN idempotence, continuous clicks preserved, FIFO wrap across 960 events per model.
- Invalid/cross-direction tickets and duplicate completions leave every exposed snapshot field unchanged.
- Both read/write pending, stop/model removal/power/topology change, two completion orders, success/error/short completions and logical/raw-ID layouts: 864 combinations. Old callbacks cannot count frames, publish events or advance new initialization.
- Repeated stops and changing requested model/context during long retention cannot renew first retirement's generation, notice deadline or retryAt. Original tickets remain completable; cancellation is emitted once per direction.
- 250ms writer timeout retains ticket and backing storage indefinitely; 1000ms notice does not free I/O. Exact 2000ms rebind boundary and new generation ticket uniqueness.
- 0x20 status delayed reinitialization, burst non-postponement, 2s minimum cadence and maximum four retries; non-trigger statuses and new-session budget reset.
- Final-page status ordering: all three models, both output sizes, all three explicit usable input layouts, input64/input65 and due-1/due/due+1. Pages complete at source-timed 4000/4050/4100 (Mini final zero-delay page at4150). The status read completes before any final settlement tick. Due/due+1 must retain the first B8/20 and start reinit exactly 200ms later; due-1 must not schedule a stale retry. Mini due-1 is tested before the still-required final write instead of reversing virtual time. Status completion must not execute the writer watchdog; owner advance keeps that responsibility.
- R3 RearObserved contract: actual redundant rear UP emits an observation while redundant DOWN stays silent. Tests cover new-generation UP after stale UP completion, exact vendor IDs, retained other rear bit, repeated physical UP, changed release versus observed release, and absent observation from bind/reset/status/front release/short/stale/duplicate callbacks. 128 queued observations are retained; the next required observation overflows and revokes.
- R3 discardPreparedWrite contract: every model/page and both output sizes, invalid/read/stale/duplicate/completed tickets, preservation of full snapshot except writePending, pending read ticket/buffer/queued events/rear state, immediate fresh preparation of the same page with unique ticket, and normal page timing/watchdog after replacement. Retaining phase rejects even its exact old write ticket on stop/model/power/topology changes. Session has no submitted marker: tests only call valid withdrawal on simulated unsubmitted preparations; actual submitted-operation enforcement is explicitly delegated to the transport test.
- Every unsupported model tag produces zero read/write tickets, including maximal virtual time.

Each group runs independently and reports structured JSON, so a candidate defect does not hide unrelated coverage. Ten compiled fault copies must fail at their intended semantic group: premature init advancement, short-write acceptance, freeing outstanding operations during retirement, silent queue overflow, and omission of only the Status-branch final-initialization settlement. The fifth control must fail exclusively at `status-final-page-boundary`; all other groups must remain green. Five R3 controls additionally reject missing UP observation, spurious duplicate-DOWN observation, withdrawal advancing initialization, withdrawal clearing the pending read, and withdrawal releasing a retained writer. Baseline failures are preserved as failures and are not waived by successful mutation detection.

## Execution boundary

No Windows API, HID enumeration/read/write, HC object, WMI, EC, native wrapper, application, Host, Steam, real sleep, service, driver, registry, or device change is performed by the harness. Local compiler setup/build execution is the only platform-specific runner work. These tests prove in-memory protocol/session contracts, not actual Windows completion delivery or target-device sleep/wake acceptance. The Session API has no prepared-versus-submitted marker, so it cannot alone forbid a caller from withdrawing an already submitted Live ticket; that caller boundary is not claimed here. It also exposes no native handle/storage lifetime beyond Session-owned array addresses and pending tickets; the Windows wrapper must separately validate kernel terminal ownership and any published external snapshots.