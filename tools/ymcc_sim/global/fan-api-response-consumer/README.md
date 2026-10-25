# Public fan API response contract consumer

`getFanApiResponseConsumerAdapters()` exports `fan.api-response-consumer` for the existing global environment. The unchanged original `tools/fan_api_response_contract_selftest.ts` executes the actual `HttpFanApiAdapter`, semantic API wrappers, diagnostics and IPC module initialization. Its original terminal replaces `http.request` with in-memory responses; no native bridge is provided. All original assertions and the original81-check final report are preserved.

The suite covers lifecycle mutation receipts, missing/malformed state and lease data, legacy compatible root forms, future optional fields, strict wire types, heartbeat identity rotation, malformed JSON,401/403/409 refusal semantics, transport unknown-result classification, intent cancellation, invalid power source without sending, and a final valid call after malformed replies. It does not execute a real HTTP/WinHTTP server, Host Main, EC/HID, user settings or a device. It does not depend on native/main.cpp or add/change a product protocol/provider.

A deliberate fault only in the private `fanApi.ts` copy removes the heartbeat lease-ID equality check. The unchanged original suite must naturally exit1 with80 successful checks and exactly one failed original check: heartbeat cannot rotate identity. Its complete original error string is required. Another failure, compilation error, timeout, signal, missing/duplicate summary or explicit early exit cannot substitute. This is a current private-source counterexample, not a claim to have replayed a historical incident.

The existing Windows owned Job contains compiler and test descendants. The original fan-global runtime guard is reused unchanged and denies network, child processes, native addons, workers and external writes. Even a caught forbidden operation invalidates the case. Positive boundary status is PASS/exit0; the deliberately failing producer has FAIL/exit1 with no denied IO. A negative runtime receipt is not rewritten to success.

Original sources and the exact actual esbuild inputs are frozen/recorded, checked before/after, and compared to the reviewed graph. Node, esbuild, PowerShell and resolver/library inputs are pinned in the toolchain profile. All compiled inputs are observed as original bytes; the only external runtime import is node:assert/strict. Enabling/disabling compiler metadata must produce identical program bytes; metadata-disabled rebuilds are not executed. Compiler junctions are excluded from evidence walking. Imported Vue/compiler branches are provenance, not complete branch or module coverage.

```powershell
node --test tools/ymcc_sim/global/fan-api-response-consumer/runner.test.mjs
node tools/ymcc_sim/global/fan-api-response-consumer/run.mjs --evidence-dir D:/evidence/fresh-response
```

Nine tool contracts validate report completeness, exact fault identity, natural termination, duplicate/borrowed summaries, scope and IO refusal. Coordinator import+spread, original public global execution, current registered audit and actual artifact-byte mutation controls remain required after standalone verification.81 checks do not close full HC/product/physical acceptance. Product/Main/CPU-TDP-GPU consumer/provider changes are zero.