# Runtime connectivity contract

This gate executes real CLR loading, the supplied shipped Host as a library, and a fresh build of the formal Host project. It never starts Host, constructs a manager/device, calls Open/Start, writes EC/OEM/HID state, reads user configuration, or deploys a payload. It supplements the existing `fan.runtime-connection` gate; it does not replace its 13-manager or mock-public-API evidence.

## Integration

```js
import { runRuntimeConnectivityRegressions, compareRuntimeCompatibility }
  from './runtime-connectivity/runtime-connectivity.mjs';
const result = await runRuntimeConnectivityRegressions({
  repoRoot,
  evidenceDir, // new directory outside the repository; parent must exist
  runtimeRoot, sharedRuntimeRoot, hostPayloadRoot,
  timeoutMs: 120000,
});
// Only result.status === 'PASS' is accepted.
```

```powershell
node tools/ymcc_sim/global/runtime-connectivity/runtime-connectivity.mjs --repo-root <repo> --evidence-dir <fresh-dir> --runtime-root <runtime> --shared-runtime-root <shared> --host-payload-root <host-payload>
node --test tools/ymcc_sim/global/runtime-connectivity/runtime-connectivity.test.mjs
```

Optional arguments: `--pwsh-path`, `--timeout-ms` (1000..600000), `--prior-result` (previous global result, evidence comparison only), `--baseline-result` (previous result compatibility contract), and `--probe-assembly` (defaults to the manifest-declared Newtonsoft.Json public API probe). Different HC versions/runtime IDs are read from the supplied profile and manifest; no HC version is encoded in the new module or probe. The current existing production resolver's pinned allowlist still applies. A new candidate must satisfy that approved contract or show an explicit compatibility failure, and must not silently replace it.

Exit 0 means PASS, 1 means a contract/behavior failure, 2 means BLOCKED infrastructure or absent evidence. Reusing an evidence directory fails. A missing real CLR, SDK, payload, required interface, receipt, owned job or timeout cannot pass; there is no null/mock alternative. Child processes are launched with the existing gated Windows Job helper; timeout closes only the owned child handle and its contained descendants.

The module returns and writes `result.json`. The real child also writes `verification.json`, `probe-result.json`, a configuration file and bounded build/process logs. The result includes `sourceChanges`, `sourceHashes`, `payloadHashes`, `compiledProduct`, `executedProductModules`, `inputConsumerStateFeedback`, `compatibility`, `remainingGaps`, and separate physical/manager/Host safety claims. Product build inputs and supplied runtime/shared identities are rehashed after execution; changed inputs fail the run.

## Formal source

`FanLab/RuntimeDependencyContract.cs` exposes `LoadPinned(context, requestedIdentity, path, approvedSha256)`, `InspectDisk(path)` and allowlisted `DescribeFailure(exception,stage)`. It checks actual managed image identity, hash, MVID, context, load location and verified load provenance. A previously unknown assembly cannot acquire an approved disk hash merely because its managed identity/MVID matches. A repeated load through the same contract reuses the verified instance. Newly approved but different bytes in the same context require a new process/context.

The reviewed hook patches show the one resolver call, project includes and one initialization diagnostic payload field. The parent integrates those shared files. The test builds the actual formal Host project and reflects its resolver and initialization stage; removing either hook causes failures. A separately linked probe also ensures source dependencies compile, but is not used as a substitute for the formal Host execution.

## Behavioral cases

The gate validates approved actual loading and repeated requests, then injects incorrect production layout, removal after validation, requested version mismatch, local shared-library shadowing, manifest byte mismatch, on-disk byte changes, same-identity different-MVID preload, changed approved candidate in an existing context, invalid managed PE, and same-identity/same-MVID different executing IL. Each fixture copies only the manifest-declared eight shared libraries into a dedicated small directory. Original DLLs remain unchanged.

The last negative changes the actual Newtonsoft.Json Boolean formatter's IL branch in a copy while preserving identity, MVID and file size. The old supplied Host accepts that loaded image and reports the approved current disk hash, yet `ToString(true)` executes as `false`. The formal hooked Host must reject it with `LOADED_ORIGIN_UNVERIFIED`; the real initialization failure event must include stage, code and requested/disk/loaded identities. Missing dependencies must identify the actual library in that same production event.

All fault injections are labeled `source-derived-fixture`; they are not device logs. The real public library API executes in CLR; HC constructors and physical operations do not execute in this gate.

## Candidate compatibility

`compareRuntimeCompatibility(baseline,candidate)` accepts completed result objects or complete compatibility snapshots. Missing fields, a failed result, missing API, changed core identity, changed shared identity/hash/MVID or changed runtime ID yields precise `INCOMPATIBLE` differences. `--baseline-result` makes such differences block the gate. A candidate is checked with the same entry and scenarios; this does not install or upgrade HC.

Current disk integrity, actual CLR-loaded shared assemblies, real manager constructors, mock Host API, and physical EC/RPM are separate claims. The existing manager/mock gate and physical device acceptance remain separate. Old evidence is only reusable for its exact source/payload hashes.
