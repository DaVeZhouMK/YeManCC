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
  verified `--input-owner=parent` child only while its window is visible,
  non-minimized and actually foreground. Process existence, background work
  and launching/returning hints do not suppress parent shortcuts. The child
  does not start a second XInput poller in parent mode. Background bridge
  stop never arms a new release barrier; a real foreground close still guards
  its final physical button. The 2026-10-23 regression adds 12 cases to the
  existing native consumer (152 total), including background parent navigation
  and current virtual/DSU output. New evidence defaults to Testing/YMCC/Evidence.
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


## Steam collection / reverse data checks (2026-10-16)

- Candidate search tries official compact StoreSearch before the rate-limited HTML path. Exact candidates still use the global ambiguity/content checks and official AppDetails; lookup failures never turn into cached negative identity. URL-scoped positive search caches last six hours and true empty inventories five minutes.
- Known scan/config game roots bound directory name evidence. Unconfigured diagnostic calls keep parent fallback.
- Landscape search includes current hashed header/header_2x and allows official 460x215 headers. Hero and portrait inventories stay separate; header-only library-assets caches remain useful.
- Local INI title hints are candidates, never official verification; historical local-only authority cache entries cannot promote new imports.
- `custom_steam_library_collection_live_probe.py` freezes both baseline/candidate executables and uses fresh Build data roots for cold/warm measurements, actual official artwork search and download. `custom_steam_library_collection_browser_selftest.mjs` separately verifies last-root removal and controller-X focus without relying on unrelated account tests. Real physical Deck output remains a device-validation boundary.

- Optional icon HTTP uses one eight-second scope for lookup plus download and one transport attempt per request. Its budget restores before other slots. Required artwork publication must not wait minutes for optional SteamGridDB availability; actual full-scrape validation is required in addition to metadata timing.


### 2026-10-16 Top100 validation

The isolated `tools/custom_steam_library_top100_probe.py` harness fixes the official `GetGamesByConcurrentPlayers` Top100 and runs exact title (A), installation/release directory (B), and localized/abbreviation (C) inputs against the production resolver. Expected AppIDs are evaluation-only and never supplied to the worker. The second pass reached 81/100 A, 81/100 B, and 44/100 C with zero wrong automatic identities; unresolved short aliases remain safe abstentions.

Steam artwork now accepts the official 40-hex `community_icon` hash as an exact-AppID candidate, keeps SteamGridDB exact-title selection and bounded caching optional, and leaves Logo assets out of the icon slot. Native 32x32 official icons are a low-quality fallback and do not block another provider.


### 2026-10-16 native Steam landscape and refresh position

Native landscape reads prefer `library_header[...].jpg/png` (including hashed subdirectories, localized and 2x variants), then legacy `header[...]`. Slot boundaries exclude unrelated prefix names and blurred assets. The portable cache carries `long` into the editor; startup/editor refresh remains local I/O. A read-only nine-game Steam cache capture reproduced eight missing landscape previews and passed after the reader change.

Library rendering patches keyed cards instead of clearing the grid. Display-only signatures preserve image nodes for metadata updates, with handlers rebound to the current record. Visible layout anchors preserve the viewport across insertion/removal and refresh; focus recovery uses preventScroll. Controller-directed navigation still reveals the target. The browser regression runs 100-card refresh scenarios against the shipping UI.


### 2026-10-16 actual local install/identity audit

`tools/custom_steam_library_local_identity_probe.py` freezes the production worker, configuration and actual EXE inventory before deciding the repair route. It scans both configured game roots, independently checks collection children, and tests real local PE/INI/appmanifest evidence without running games or injecting expected IDs. Final warm evidence includes actual URL candidate and AppDetails caches; old configuration-only warm runs are retained with an explicit correction note.

Directory inference now splits structurally independent sibling installs while retaining ordinary root executables, architecture folders, manual selections and native install boundaries. Shared built-in executable rules are sourced from `native/game_detection_blacklist_entries.h`; file rules and wildcard behavior match YMCC. Blacklisted EXEs never enter primary ranking or direct identity requests. Ownership aliases from old collapsed containers are limited to the exact old selected EXE.

Official cross-script title components may corroborate a single local ID only by exact normalized equality; the official AppDetails/type/conflict checks remain mandatory. Search dedup preserves real word boundaries. Packaging cleanup applies to both query names and independent directory evidence. The UI keeps officially verified Steam identity recognized while artwork is incomplete.


### Metadata requests on weak networks (2026-10-16)

The automatic comprehensive identity worker shares a 45-second logical network budget across keywords, locales, Steam and IGDB. Required artwork retains its independent budgets. Cached responses can still be consumed after the network deadline. Budget and transient failures remain retryable, including partial success (search 200 followed by failed verification).

Metadata endpoint cooldowns live in portable cache/network-backoff, keyed by scheme/host/port/path without query parameters. Automatic 429 is deferred rather than immediately retried; server Retry-After is retained for scheduling up to ten minutes. Temporary transport failures get short cooldowns. Concurrent worker writes are bounded and serialized; late success cannot erase newer throttling. This is scheduling state, not a negative identity cache. Manual requests can probe recovery. HTML-only throttling cannot disable later automatic StoreSearch queries.

Steam search positive inventories up to seven days old may be reused only after retryable failure, without refreshing age or granting identity. Empty/error/future/mismatched caches cannot provide this fallback. AppDetails still requires a valid twelve-hour cache or fresh official verification.

Playnite IGDB responses are cached by exact URL and GET/POST body: twelve-hour detail records, six-hour positive searches, five-minute empty searches. Errors are never cached as absence. Only positive searches have the bounded stale-on-error fallback; detail ID, platform and content type checks remain mandatory. Persistence failures do not invalidate a successful provider response.

Regression and shipping-EXE scheduling probes are in tools/custom_steam_library_worker_lifecycle_selftest.cpp and tools/custom_steam_library_weak_network_probe.py. Evidence: Mainline/Build/CustomSteamLibrary/weak-network-20261016/REPORT.md. No full-disk feature is enabled by this change.

Name rounds and path scoring now share the same owning-install boundary. When a native installation contains another independent game with its own selected EXE and distinct identity, targeting that child refines its root and cache ownership. Parent-native titles cannot become fuzzy evidence for the bundled game. This guard is required with the healthy-endpoint fallback changes; see the bundled-install lifecycle fixtures.


### Provider data-boundary audit (2026-10-17)

Fresh IGDB detail responses, not just cache admission, must bind to the exact requested positive integer ID. Automatic matching rechecks the detail content type so a search entry marked as a base game cannot mask DLC/expansion details. Valid metadata caches still require the existing platform/type checks.

Endpoint cooldown merging retains both the longest retry deadline and newest failure observation. A later short fault must survive an earlier request's late success; concurrent writes are exercised with tools/custom_steam_library_backoff_concurrency_selftest.py and 24 independent isolated test processes. Steam HTTP200 error/invalid candidate envelopes never become negative search caches, and prior malformed entries require a fresh response.

Captured Steam game/music/system-tool records are replayed through the production verifier with YMCC_STEAM_DATA_CASES. The local corpus replay includes authentic igdb-responses keyed by the exact request without redating them. Current shared process_efficiency and WebView efficiency headers are now part of build provenance; their policies were preserved. Evidence: Mainline/Build/CustomSteamLibrary/data-audit-20261017/REPORT.md.

### 共享壁纸后台入口

`CustomSteamLibrary.exe --media-background <YMCC PID> <进程创建标识> <会话令牌>` 仅由 YMCC 下载监管器启动。此入口在输入所有权解析、可见单实例、WebView2、窗口及手柄定时器初始化之前返回；不激活现有游戏库，不抑制主程序按键。实际素材与编辑器共用 `data/config/game-media.json` 和 `artwork/video`。队列为 `config/game-media-downloads.json`，每秒最多更新一次的小进度文件为 `config/game-media-download-progress.json`；完成即退出，重试期间不保留游戏库进程。进程卡死只回收监管器创建并持有句柄的子进程，原可见窗口保持可操作。正式 host 发布器写入 `backgroundMediaProtocol=1`，旧包不支持该入口时不会用后台参数拉起界面。

自动下载规则（2026-10-18）：YMCC 启动后先强制保护30秒；下载进程以 Idle + EcoQoS 和 Windows 后台进程模式运行。CPU 默认超过15%暂停，低于10%稳定15秒后恢复，阈值由 YMCC Steam 设置控制。电池模式停止自动下载；只等待原生供电变化事件，不为下载定时读取CPU。所有自动展示需求都转交该队列，手动编辑和缓存播放不绕行下载。进度状态含启动保护/CPU忙/电池/网络失败/无进展/进程退出原因；保留最近16次退出记录和精确子进程 CPU 时间回执。此规则不改变播放端原有开关和电池下动态播放意图。


## Metadata and artwork weak-network recovery (2026-10-20)

HTTP 200 alone is not proof of usable provider data. Malformed GET/POST JSON,
Steam candidate error envelopes and invalid IGDB search envelopes retain explicit
retryable receipts. Automatic semantic failures use the existing endpoint cooldown;
valid empty search results remain definitive. Only bounded positive search caches
may supply candidates during an outage. Their original timestamps and strict
Steam/IGDB detail binding and expiration remain intact.

Long server Retry-After values are handed back to scheduling rather than shortened
into another immediate request. IGDB/Baidu automatic artwork searches now export
their receipts even when they throw. Both inventory discovery and per-slot fallback
failures reach the existing artwork network retry queue before a URL is available;
healthy fallback providers can still supply images without a Steam ID. Baidu gateway
HTML, malformed JSON and invalid envelopes cannot become definitive empty artwork.

The existing worker/artwork self-tests run the actual consumers. A focused offline
positive/private-fault runner is available at
`tools/ymcc_sim/global/custom-steam-library-network/run.mjs`; see its README.
It is a targeted existing-simulator scenario, not a global or physical acceptance
claim. The regular worker build's RunSelfTests mode includes the new cases.

Image responses use the same persisted endpoint scheduling for 429 or transient
Retry-After, including manual downloads. Query variants share the image path
cooldown; unrelated images remain independent. Empty/HTML/XML error responses and
undecodable PNG/JPEG bytes after HTTP200 retain explicit recovery receipts without
entering image caches. Unsupported WebP/SVG and definitive 404 remain distinct from
network failure. The complete worker cases verify durable decoded recovery and
manual cover preservation. Follow-up evidence remains in the existing
network-20261020/followup-image-responses evidence directory.


The raw WinHTTP request/read/retry path also has an isolated API-port suite:
`tools/custom_steam_library_http_transport_selftest.cpp`. It includes the production
worker without its high-level transport replacement; every HTTP I/O API is bound
to an inert test port. Oversized error bodies preserve HTTP status/Retry-After
without reading their contents, successful zero-byte reads end unknown-length
bodies, and invalid server responses use bounded reconnect recovery. Declared
and streamed success-body limits, premature known-length EOF and certificate
failures retain their strict checks. Both the focused positive/fault runner and
the normal worker RunSelfTests entry execute this suite. Evidence continues under
network-20261020/followup-raw-http.


<!-- OPTI-CATALOG-SUPPLEMENT-20261022 -->
## YMCC 启动一次 OPT 反补与游戏库前台手柄接管（2026-10-22）

当前规则：YMCC 普通启动通过单实例准入后，从启动时间算 30 秒安排一次低优先级隐藏 --opti-startup；与动画/壁纸开关无关，每进程至多一次尝试，成功/缺项/运行中 OPT/缺组件/报错均不重试。首次错误立即静默终止，原子文件激活也不重试，无失败弹窗/通知/UI错误回执。仍只补 OPT 缺名称、目录、已确定 EXE 和本地封面，保留既有记录/非空字段/用户与安装状态，每项 50ms。普通游戏库窗口与 --media-background 不再触发反补；原媒体工作器还原，动画 OFF 不会因此开启。

native/opti_catalog_startup.h 提供主进程一次性 owner 与继承父句柄/创建时间/同包路径校验，退出取消/join，自有 Job 销毁，3 分钟预算；workspace_host.cpp 仅独立入口消费。原生手柄接管同时收紧：只有验证身份、可见、未最小化且当前前台的库窗口才接管；后台/隐藏/最小化/无窗口立即释放，后台启停桥接不设置释放屏障。无硬件屏蔽设置或其他模块变更。

启动/生命周期 11、目录消费者 27、原生输入 140、双比例真实 Chromium UI 37 场景通过；旧后台接管反例 12 项失败。既有壁纸/桥接/素材/生命周期/账号/OPT/Steam启动回归通过。主程序与子程序完整构建，主线子包更新；安装目录未替换，物理手柄/睡眠未声称验收。报告：`G:/YeManCC-Work/Build/Tasks/CustomLibrary-OptiBackfill-20261022/ymcc-once-foreground/REPORT.md`；索引 `customSteamLibraryOptiCatalog20261022`。本轮不改变睡眠恢复项目状态。
<!-- /OPTI-CATALOG-SUPPLEMENT-20261022 -->


<!-- OPTI-CATALOG-COST-20261023 -->
2026-10-23 启动OPT反补优化：直接匹配当前库EXE+目录/别名与既有加入计划；只在OPT缺封面时创建一次素材索引，完整条目跳过封面IO与50ms等待。保留每进程一次/动画OFF独立/静默首错终止/只补不删。成功或正常跳过回执新增一次首尾cost（CPU/耗时/IO/峰值工作集），普通日志被动导出，无常驻采样。成本基准与回归：`G:/YeManCC-Work/Testing/YMCC/Evidence/Build/OptiCatalogCost-20261023/REPORT.md`，本机夹具不代表ROG-X现场数值。测试：`powershell -NoProfile -ExecutionPolicy Bypass -File tools/custom_steam_library_opti_catalog_selftest.ps1 -Performance`；默认输出已接Testing核心Evidence/Build。
<!-- /OPTI-CATALOG-COST-20261023 -->
