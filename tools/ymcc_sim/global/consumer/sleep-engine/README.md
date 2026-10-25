# Sleep engine consumption probe

This is a local test entrypoint for an explicit, already-built `YeManFanHost.dll`. It reuses the loading/isolation pattern from `tools/FanFinalizeContractSelfTest` and the existing sleep-domain fixtures. It never invokes Host `Main`, acquires or weakens `HostInstanceLease`, attaches to an existing Host, or changes a payload. All new code stays in this directory.

The HTTP modes construct the production `LocalFanHostServer` around the production `FanHostEngine`, with `RealBackend=false`, `AllowHardwareWrites=false`, `MockHandshake=true`, and `MockZeroHardwareEvidence=true`. The engine's private `realBackend` must remain null. Actual requests go through production authentication, JSON parsing, `HandleAsync`, power-transition ingress and engine methods. The tool does not reproduce their state/recovery algorithms.

## Entry

Use PowerShell 7 without changing execution policy:

```powershell
pwsh -NoProfile -File tools/ymcc_sim/global/consumer/sleep-engine/Invoke-SleepEngine.ps1 `
  -HostPayloadRoot <explicit-current-payload-directory> `
  -ExpectedHostSha256 <64-hex-current-host-hash> `
  -ExpectedBackendSha256 <64-hex-current-backend-hash> `
  -EvidenceRoot <fresh-evidence-directory> -Mode all
```

`ExpectedHostSha256` is required; Backend hash is optional for generic use and supplied for paired-candidate acceptance. There is no default installation directory, selected HC version, or fallback payload. The explicit Host is hashed before loading and after execution, and its actually loaded path/hash/MVID is reported. Backend and other dependencies are loaded only if the real CLR requests them; a pinned-but-unloaded dependency is not represented as executed.

Modes: `all`, `forms`, `typeseries`, `rapidwake`, `probe`, `memory`. The single HTTP modes use a new task-owned child process; all runs forms/typeseries/rapidwake in three sequential fresh children, preserving the original fixture mode boundary. Each child uses a randomly reserved `127.0.0.1` port and a fresh synthetic session token. The token is generated inside that child, never supplied through command arguments or read from disk, and never returned in evidence. Existing Host ports and credentials are never consulted. Per-child LOCALAPPDATA, TEMP and self-test logs are isolated under the fresh evidence directory.

`memory` constructs neither server, listener nor HTTP client and binds no socket. It executes real engine methods directly. It must report `http.actualDispatch=false`, `networkUsed=false`, and `localServerConstructed=false`; it does not prove authentication or server generation admission.

The wrapper builds the small reflection tool into the evidence directory, then records the exact child process creation time, executable, exit code and natural exit. Timeout cleanup can target only its own retained process handle and makes acceptance fail. No external Host is stopped by name or PID.

## Fixture mapping

| Group | Existing input source | Real consumption |
| --- | --- | --- |
| 20 forms P1–P7/B1–B6/C1–C4/D1–D3 | `domains/sleep/sleep_forms.json` and `sleep_domain.ps1` | Actual authenticated API rejects malformed/stale/unauthorized input, consumes sleep/wake and handles software curve ownership/Close |
| S3, S4a/S4b, S0ix | `lib_sleep_typeseries.ps1` | S3 with lease and real 3s quiet; S4 no-curve then curve with real 25s quiet each; S0ix emits only state observation after 3s |
| 100/500/1000/2000ms, curve-100ms, stress×5 | `lib_sleep_rapidwake.ps1` | Same real routes and actual short quiet intervals; no OS sleep |
| Four extra invalid inputs | Existing auth/JSON/curve contracts | Malformed JSON, illegal generation type, unknown route, invalid curve |
| Pure-memory unit group | `FanFinalizeContractSelfTest` owned-gate pattern | Real Open/lease/curve rejection/release/Close; authoritative vs published snapshot comparison |

B2 executes only the compressed edge sequence. It does **not** validate the thirty-minute long-sleep threshold and does not falsify the engine's monotonic clock. D3 validates ordered Close followed by a late wake, rather than claiming native scheduler concurrency. The S0ix/S3/S4 names are fixture labels, not detected platform sleep capabilities or evidence of physical sleep.

Current type-series code asserts S4a no-curve wake is 200, despite older description text suggesting 409. S4b after a curve and 25s quiet accepts convergence or an explicit 409 and must never claim physical readback. These actual assertions, rather than the old descriptive string, determine the mapping.

Legacy mock fixtures sometimes use `hardwareWritesObserved=true` for a fictional write. This entry requires strict zero-hardware options and checks the actual `mockControlEnabled` / `mockWritesObserved` evidence instead. The three physical write fields `hardwareWrites`, `hardwareWritesEnabled`, `hardwareWritesObserved` must stay false. Mock support/capability advertisement is separately labelled and is not physical hardware acceptance.

## State observations and results

The tool reads its own engine's authoritative `Snapshot()` for before/after mutation assertions. The published `SnapshotForRead()` cache is allowed to predate a just-granted lease when another thread holds the gate; comparing that cache with a later fresh state can falsely blame a rejected request. The memory group demonstrates this with the original engine and the existing owned-gate test pattern. No product code is changed to accommodate the test.

The structured result at `<EvidenceRoot>/sleep-engine-result.json` contains:

- `actualHost.loaded`: actual assembly paths, SHA-256, identity and module IDs.
- `sourceInputs` and `fixtureIdentities`: tool and reused fixture source hashes.
- `inputConsumerStateFeedback`: each real request, production consumer, actual state before/after and feedback code.
- `scenarios`: exact mapping and limits for the twenty forms, type series and rapidwake.
- `process`, `wrapperVerification`, `safety`: natural exit, selected artifacts unchanged, no Main/HC/target-native/physical writes or existing-Host stop.

PASS is scoped to the selected software consumption contract. `INCOMPLETE`/exit 10 preserves an explicit compatibility difference; FAIL/exit 1 preserves execution or assertion failure. Original blocked Main-based tests remain blocked until the parent integrates this distinct software entrypoint; those prior receipts are not rewritten.

This does not validate native OS event delivery, installed YMCC dispatch, actual system sleep, physical EC/OEM/HID/driver operations, fan speed or physical restore. It does not replace the separate real-HC construction/connectivity gate.

## Final wrapper guards and parent integration

`Mode all` intentionally uses three independent processes/engines/servers. Reusing one engine carries the S4 curve history into rapidwake's initial no-curve premise. It would test a different input contract. Original failures are retained rather than clearing internal product state to obtain a pass.

Each original group also runs the same four bad-input kinds. Aggregate all therefore reports 20 forms, 3 type-series cases, 6 rapidwake cases, 12 negative executions (4 distinct kinds), 41 scenarios. Rapid stress after its curve case preserves the existing fixture's explicit 200-or-409 outcome and one consistent final state; a 409 is never described as successful recovery.

Every child must exit **zero**, report PASS and complete its engine/server path naturally. A nonzero exit with a forged PASS receipt is rejected as `CHILD_NONZERO_EXIT`. `receipt-contract-selftest.ps1` directly tests this actual guard along with source set/hash changes, failed completion and forced cleanup. No Host or network is used for those tests.

The wrapper hashes its actual build/runtime sources before building and compares them after execution. A changed/deleted/inserted source blocks acceptance as `SOURCE_CHANGED_DURING_BUILD_OR_RUN`. Results contain `sourceInputs` (before), `sourceInputsAfter`, and `sourceStability`. The default `auditSleepEngineReceipt` from `receipt-audit.mjs` also requires this final wrapper contract and current source hashes. Explicit `verifySourceHashes=false` is only a historical core-data audit and reports `currentWrapperAcceptance=false`; it cannot certify the final wrapper.

On 2026-10-19 the selected installed candidate completed all 688 engine/API checks, 150 real HTTP requests and three natural exit-zero children. The final wrapper-only guards were then validated using memory mode and pure contract negatives. At the parent's instruction, the final guarded all-mode acceptance is delegated to its registered global `sleep.engine-http-all` call; the older all receipt is **not** rewritten with newer wrapper hashes. The parent's `sleep.engine-memory` and `sleep.engine-http-all` adapters call this CLI with explicit payload and paired hashes. This directory does not edit global CLI/registry/sleep domain sources.
