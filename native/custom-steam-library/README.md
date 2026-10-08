# Custom Steam Library canonical native source

This is the canonical, in-repository source for `CustomSteamLibrary/SteamArtworkLab.exe`.
The matching Host source has also been restored into this directory. Host and
Worker have separate build recipes; neither replaces unrelated workspace UI edits.

## Provenance

- Worker/header restored from `G:\YeManCC-Work\Backup\csl-stray-20260910\native-source`.
- That backup's `build-horizontal-artwork/SteamArtworkLab.exe` exactly matches the
  deployed pre-fix worker: SHA-256 `0a995f2e9e148bd753271b8f820e7bc5b9dfbee3b8a57a7f496732b948932050`.
- Worker resource restored from the earlier migration backup and repointed to the
  existing, managed `CustomSteamLibrary/assets/custom-steam-library.ico`.
- Backup/archive originals were not edited. The September 8 worker source and its
  horizontal-artwork behavior were retained instead of reverting to August source.

## Build and regression

From the YeManCC project root, run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/build-custom-steam-library-worker.ps1 -RunSelfTests
powershell -NoProfile -ExecutionPolicy Bypass -File tools/build-custom-steam-library-worker.ps1 -Publish
```

Outputs and retained regression fixtures go to `Mainline/Build/CustomSteamLibrary`.
`-Publish` updates the canonical worker EXE, validates/refreshes the current managed
child-file hashes and sizes, and updates worker provenance and timestamp. It never deploys to the installed program or touches
Steam/user data. The mainline packager rebuilds this source after legacy child
synchronization so an old worker cannot silently replace the fix.

## localconfig.vdf import fix (2026-09-30)

`localconfig.vdf` is optional recent-game metadata, not an import prerequisite.
Missing, unreadable, non-file, or unparseable metadata is left untouched and only
recent-game enrichment is skipped. Shortcut backup, staging, verification and
Steam-closed safety checks remain required. `recentSignalStatus`,
`recentSignalSkipped`, and `recentSignalSkipDetail` record the degraded result.
A usable config still receives the existing recent signal with an exact backup.
The test binary alone virtualizes process enumeration, allowing real-target
transaction regressions in isolated fixtures without stopping the user's Steam.
The shipping worker uses the unmodified OS process guard.

## Data-chain hardening (2026-10-01)

The normal `-RunSelfTests` entry now builds and runs both isolated C++ suites,
the UI snapshot/fuzz suite, and shipping-worker CLI fixtures. The tests do not
stop Steam, import into a real account, or change installed program files.

- Config/scan/retry state validate persisted shapes and prefer valid backups.
- Missing config initialization recovers backups before writing defaults.
- Root availability and traversal completeness are explicit. Empty scans keep
  configured roots; offline/partial roots retain game data without deletion
  transitions. An incomplete automatic primary selection requires review.
- Add/delete/artwork writes serialize under one Steam writer and data mutex,
  prepare all inputs before mutation, and persist pending/completion journals.
  Failed rollback retains its journal; unresolved recovery blocks a new write
  to the same target. Result publication is not the commit boundary.
- Cover, horizontal cover and wallpaper remain independent: `p`, no suffix
  (short/long IDs), and `_hero` respectively.
- Snapshots require exact inventories, supported relative paths, readable
  contents, and no reparse points. Temp cleanup preserves user temp files and
  backups, and refuses to race active writes.
- UI snapshots reject malformed roots, filter corrupt rows, retain the last
  usable list when a response is invalid, and reload durable state after a
  per-account Steam-write error. Multiple accounts are independent commits,
  not one cross-account atomic transaction.

The broad `custom_steam_library_integration_selftest.ts` now checks the
current contract: the nested child path is canonical and the removed legacy
sibling fallback must stay absent. The assertion was stale after the 2026-09-17
path-policy change and has been corrected; the test is not waived.

Run `npm run test:custom-steam-library-integration` from the repository root.
The suite includes 83 checks, executing the real bridge resolver/cache readers
with mocked filesystem/IPC, and the exact UI import/artwork helpers extracted
through the TypeScript AST. Missing SteamID/artwork does not block a waiting
shortcut; this never grants a verified Steam identity. Long artwork and hero
wallpaper remain distinct slots. The JSON summary is written to
`Mainline/Build/Validation/SelfTests/custom_steam_library_integration_summary.json`.
All fixture IO is mocked and unexpected process/window/IPC/timer calls fail.
This source/fixture integration test does not replace a real-account installed
YMCC + WebView2 + Steam end-to-end run. No full YMCC release is regenerated here.


## Steam summary cache validation (2026-10-02)

The first-level Steam page summary now validates `steam-add-plan.json` against
its `dataRoot`/`libraryState` before using it. Waiting counts include both
`ready-to-add` and `ready-not-selected`; joined counts include
`already-in-steam` and post-commit `added-to-steam`. When item rows exist, their
statuses override stale planning counters. Invalid, foreign-root, or malformed
plans fall through to the next data root or a validated `library-scan.json`
cache. Scan-cache `nonGames` are not subtracted twice from the game count.
The bridge integration self-test covers these cases.

## Per-slot artwork tiers (2026-10-01)

- Host source restored from the September 10 backup. Its original three-artwork
  build matched the previous canonical Host SHA-256
  `24eab22e7bff3bc9ba2c37786bbf415974c21d34b851f43f3350599fc553859d`.
- Each cover/long/wallpaper slot follows manual > automatic > no image.
  Explicit manual deletion is an opt-out for that slot, not a failed download.
  One manual slot must never skip the entire game's artwork or identity queue.
- Automatic downloads prefer usable Steam assets, then IGDB, then relevant Baidu
  image candidates. Search hits never grant identity authority. Artwork-only
  fallback remains usable when both database IDs are unavailable.
- Partial manifests remain visible with optional null identity fields. Counts and
  required-slot flags are derived from final readable image files, including
  preserved images. Identity-only retries never rewrite durable artwork.
- Host recovers absent or stale manifest links by executable, attaches previews
  without a SteamID gate, and applies manual priority independently per slot.
  UI uses the saved local copy before a remote download URL, and marks a missing
  manual file as eligible for automatic fallback. Existing explicit user import
  selection behavior is retained; artwork does not fabricate verified Steam IDs.
- Existing images survive offline retries. Durable image replacement is atomic.
  Missing-game fallback enters the visual round without exhausting the entire
  per-game budget on repeated identity requests.

Build the restored Host and its isolated preview fixtures:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/build-custom-steam-library-host.ps1 -RunSelfTests
powershell -NoProfile -ExecutionPolicy Bypass -File tools/build-custom-steam-library-host.ps1 -Publish
```

The Worker entry also runs `custom_steam_library_artwork_tier_selftest.cpp`:
HTTP responses are intercepted in test builds only. Fixtures cannot contact live
providers. Shipping binaries retain real WinHTTP transport and Steam guards.
The release packager now rebuilds both binaries after legacy synchronization.


## Lifecycle and Steam response hardening (2026-10-01)

- Host JSON and local artwork copies stage in the destination directory,
  share the Worker's data transaction mutex, retry transient Win32 5/32/33
  conflicts, and never delete/truncate the destination to bypass a lock.
  Adjacent backups are atomic as well; invalid UTF-8 serialization fails
  before touching the filesystem. Persistent errors retain the Win32 code.
- Workers start suspended and join the host job before executing. Their handle
  inheritance is restricted to their own stdout/stderr and NUL input.
  Output retention is bounded (4 MiB); a flooding worker is terminated.
  Pipe drains are nonblocking and bounded even when a descendant retains stdout.
  Diagnostics are replacement-decoded into valid UTF-8 for WebView messages.
- Top-level host tasks are owned and joined before process/static teardown.
  Shutdown refuses new tasks, cancels active workers, closes their kill-on-close
  job, drains queued result objects, and rejects late posts. Artwork replies
  from cancelled/superseded generations cannot publish stale successful data.
- Native Steam grid replacement activates the new image before deleting old
  extensions. A failed activation retains all old files and isolated backups.
  Portable manifest lookup is bounded, error-code based, and avoids reparse
  traversal. Cover/header/wallpaper slots remain independent.
- Steam AppDetails requires the exact requested envelope, boolean success,
  object data, a positive integer canonical ID, and typed title/content type.
  Missing IDs are not invented. Storefront aliases require an independent
  canonical query; cycles, disagreement and mismatched caches are rejected.
  DLC/demo/tool/music retain their types, rather than being coerced into game.
- Metadata redirects must remain on the HTTPS Steam store AppDetails endpoint;
  HTTPS-to-HTTP redirects are disabled. Public Steam TLS retains certificate
  checks; local transparent acceleration and IP bypass exceptions are scoped.
- Downloaded PNG/JPEG files must actually decode, not merely contain a plausible
  size header. Decoding has dimension/pixel limits to reject corrupt images
  and decompression bombs. Retry-After overflows clamp to the retry budget.
- HTTP fixture injection now drives the production retry loop. Raw HTTP and
  accelerator DNS/network probes fail closed in test builds. Shipping behavior
  never includes the test transport/process overrides.

`-RunSelfTests` also builds/runs:

- `tools/custom_steam_library_host_lifecycle_selftest.cpp`
- `tools/custom_steam_library_worker_lifecycle_selftest.cpp`

Both require a fresh fixture root under Mainline/Build. The Worker suite accepts
an optional directory of previously captured official AppDetails JSON samples.
No fixture stops/starts Steam, modifies a real account, or deploys an installed app.
A complete machine-specific end-to-end Steam/UI lifecycle test is still separate
from these deterministic fixtures and the hidden WebView health handshake.

Long-path regression found by deeper fixtures: IO boundaries now use extended
Win32 drive/UNC paths while saved paths remain portable. Atomic byte writers
use CreateFile/WriteFile/FlushFileBuffers; Host readers share delete access so
they no longer block the Worker snapshot activation. Backup/staging suffixes
are included in the long-path cases.

Publication guard: each wrapper compiles into a fresh directory, validates MSVC
error diagnostics as well as the process exit code, and requires all fresh
EXEs before promotion/publication. This catches a reproduced MSVC C1001 failure
that left the old Host EXE behind while the command returned zero. The guard
fixtures cover zero-exit compiler/linker/resource failures and missing outputs.
`tools/custom_steam_library_steam_readonly_probe.cpp` is an explicitly opt-in
live production HTTP probe; it only reads official AppDetails and writes under
a fresh Build root. It is not invoked by offline fixture tests.


## Scan roots and controller text entry (2026-10-08)

- Removing the last scan root is supported. The Host sends the explicit
  `--clear-roots` operation; the Worker persists `scanRootsExplicitlyCleared`.
  Reopening or refreshing an intentionally empty list does not rediscover roots.
  Legacy empty configurations still retry first-run discovery, and adding a
  new root clears the explicit-empty flag. Manual game/trainer scopes remain
  independent. Removing a root never deletes game files.
- Editor name/AppID fields start in read-only navigation mode. Controller A
  enters text mode and requests the touch keyboard; B/Escape exits text mode
  without discarding the editor. Blur or controller directional navigation
  restores the read-only state. Mouse/pointer clicks and physical Enter can
  edit directly without issuing a controller keyboard request. Native Steam
  AppIDs remain permanently read-only.
- `pnpm run test:custom-steam-library-controller-input` runs the complete
  shipping HTML/JS in isolated headless Edge with a fixture-only native bridge.
  It checks X/A/B/navigation, physical typing, real mouse clicks, native AppID
  immutability, and last-root removal/cancellation without touching real Steam.
  Native build self-tests include explicit-empty refresh and re-add regressions.


## Virtual-controller UI output parity (2026-10-08)

- The parent already routes unchanged physical controller samples to a
  verified `--input-owner=parent` child and suppresses parent-global shortcut
  evaluation during that session. The child does not start a second XInput
  poller. These admission, edge/repeat, and lifecycle rules are unchanged.
- The parent's Steam Deck/PS5 final-output quiet gates now both call the
  existing `gamepadUiInputEligible()` predicate. The previous child-session
  early return was allowing virtual output to reach Steam desktop mappings
  while the same physical press was also forwarded as a library action.
  Only final virtual frames are neutralized; physical samples, bound personas,
  source sampling, target residency, epochs, and external-foreground output
  retain their established behavior. Xbox and other personas are unchanged.
- `pnpm run test:custom-steam-library-virtual-input` compiles and executes exact
  production ownership, action-routing, and frame-assembly helpers with mocked
  Win32 probes. It also reintroduces the original child bypass as a mutation
  to prove the regressions detect it. No device/Steam I/O occurs in fixtures.
- This fix is in `native/main.cpp`: rebuild and update the parent YeManCC
  executable, not just the green child package. Real Steam Deck desktop-mapping
  behavior still requires device verification.
