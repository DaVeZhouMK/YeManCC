# Custom Steam Library weak-network consumers

Run from the mainline source directory on Windows with the installed MSVC and Windows SDK:

```powershell
node tools/ymcc_sim/global/custom-steam-library-network/run.mjs --evidence-dir G:/YeManCC-Work/Mainline/Build/CustomSteamLibrary/<fresh-name>
```

The runner freezes current worker/header/JSON/test inputs, compiles and executes the existing lifecycle and artwork suites, then repeats the same cases against a private worker copy with semantic retry handling, envelope guards, long Retry-After deferral and discovery-to-retry accounting disconnected. It requires the expected business assertions to fail; a compile error, timeout or missing terminal report is a failure of the proof. Source hashes must stay stable during the run.

The lifecycle and artwork suites execute production consumers, including the real worker entry in the artwork suite. HTTP and process/file-lock boundaries use the existing isolated test runtime. Fixtures write only under a fresh workspace Build directory, never use live HTTP, and never launch games, installed YMCC or Steam. The compiler child uses the installed SDK without changing global environment settings.

Evidence includes complete frozen sources, private fault, compiler and fixture output, exit codes, process IDs, executable hashes, original failure assertions and an aggregate result.json. This direct scenario does not change the centrally owned global allowlist or claim whole-program acceptance. The normal build-custom-steam-library-worker.ps1 -RunSelfTests entry also runs the expanded lifecycle/artwork positive cases.

Optional real-response replay is separate: set YMCC_LIBRARY_NETWORK_CASES to a Build directory containing storesearch.body, appdetails.body and igdb-search-production.body captured from the production URLs/request shape, then run the lifecycle fixture EXE with another fresh Build root. Captures retain their original response bytes; they are not injected into user caches.

Image-response cases also run the full worker through empty/HTML/XML-error/truncated PNG/JPEG success responses, persist an active retry job, and recover without a Steam ID. Unsupported formats and definitive 404s remain distinct from connectivity loss; manual cover bytes are preserved. Request-boundary cases verify image 429/503 deferral and reload persisted per-image cooldowns in both automatic and manual mode, without blocking a different image path. The private fault disconnects these consumers too and must fail all corresponding business assertions.

The third suite, `custom_steam_library_http_transport_selftest.cpp`, binds the
WinHTTP I/O API itself and executes the production request/read/retry function
without enabling its high-level fixture transport. URL parsing, timeout/deadline
checks, response headers, body accumulation, retries, connection ownership and
persisted provider waits stay in the actual worker. Its 90 cases include large
429/503/404 error declarations, known/unknown-length short reads, zero-byte EOF,
truncated bodies, bounded success bodies, receive/read errors, reconnects after
invalid server headers, and certificate refusal. The private fault must reproduce
all 77 named business failures in the same frozen case set. No live
WinHTTP session/connection is opened by this suite.

The additional deadline cases use a monotonic clock advanced only by the API
ports. They execute the actual production phase boundaries, remaining receive
timeout, optional artwork/identity budgets, timeout-setting failure handling,
and retry trace. Fifteen original failures plus a healthy paced-download
control run in the same set; no real waiting or independent deadline model is
used. No extra blocking phase may start after its caller's budget expires.
These checks bound admission between synchronous WinHTTP calls; they do not
claim instant cancellation inside OS DNS, TLS or redirect processing.

The semantic-fallback cases execute the original direct-IP fallback and DNS
packet parser too; configured resolver lookup and UDP socket I/O are bound to
isolated ports, so no fixture packets leave the process. A gateway HTML response
can still recover into valid JSON. If the fallback returns 429/503, its original
canonical endpoint receives the same persisted provider wait as normal HTTP.
Automatic/manual metadata and images must observe that wait. Retryable transport
errors preserve their system codes, discard failed sessions, and enter the
existing recovery evidence contract; certificate failures remain nonretryable.
Expired identity/artwork budgets and existing waits must stop before DNS/HTTP.
The 31 new cases reproduce 30 failures in the original worker plus one successful
fallback control; the private fault must detect those same 30 named failures.


The complete-response cases reject HTTP 206 before consuming its body: these
production GET/POST requests never request byte ranges. Parseable partial JSON
must not become identity/absence data; partial artwork remains retryable. The
same guard applies to direct-IP semantic fallback. Four ordinary fallback cases
check the effective deadline before DNS and again after its simulated cost;
expired work cannot advertise a scheduled reconnect. Two healthy route controls
still recover through DNS and direct HTTP with available budget. These 17 new
cases reproduce 15 failures in the original consumer. All 15 are required in the
private fault; normal manual/automatic recovery still needs a new session.

## StoreBrowse asset envelopes and recovery

The artwork suite also covers HTTP 200 StoreBrowse envelopes, Steam EResult
failures, exact AppID/item binding and stale positive asset-index reuse. There
are 32 additional cases, including malformed/missing/duplicate/foreign records,
seven documented temporary EResult codes, persisted result-84 provider waits
across AppIDs and worker-memory reload, missing/expired cache, definitive empty
inventories, access denial and thumbnail-only inventories. A malformed inventory
must reach the existing retry queue through the full worker. An unusable result
cannot overwrite or refresh the timestamp of a verified positive index. A
verified index may remain available for the existing 30-day stale window, with
an explicit stale-cache receipt; it does not grant identity authority.

The frozen positive suites currently execute 295 lifecycle, 95 artwork and 90
raw-transport cases (480 total). The same cases against the private fault must
fail all 99/36/77 required business checks. Its StoreBrowse mutation restores the
original permissive envelope consumer, so compile/runtime setup errors cannot
count as detecting the defect. Independent final-case replay against the
pre-change worker reproduces 29 StoreBrowse business failures. Optional public
response captures and their isolated replay are retained in the evidence report;
synthetic transient EResult cases are not claimed as observed provider outages.
