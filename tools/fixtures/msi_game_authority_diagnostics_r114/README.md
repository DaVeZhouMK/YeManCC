# R114 one-shot gameAuthority failure diagnostics

Production source: `InputHost/MsiNativePortCycle.cs`. This task adds observed facts only; the existing predicates, CM/SDK/lifetime/BIOS call order, retries and action/result behavior remain authoritative.

Wire format is one attempted stdout line:

```text
YMCC_MSI_GAME_AUTHORITY_DIAGNOSTIC {compact JSON object}\r\n
```

The fixed prefix includes its trailing space. The complete UTF-8 line, including prefix and newline, is capped at **2048 bytes**. Only the first failing gameAuthority guard emits a record, immediately before its unchanged return 8. Successful checks and root-only runs emit no diagnostic. No CLI argument, environment/registry read, timer, file log or extra device read is introduced. A missing facts object disables diagnostics; creation failure is caught. Serialization, byte-limit rejection or stdout failure never changes the return code and never causes a diagnostic retry. Existing exceptions still return 6 without being reclassified as an authority rejection.

`diagnostic.schema.json` describes the JSON suffix. Field names are case-sensitive:

| Facts | Meaning |
| --- | --- |
| `Schema=1`, `Kind=ymcc.gameAuthority.failure`, `Result=8` | Record identity and original refusal result. |
| `Stage=first` or `after-prepare` | Which existing guard returned false; no extra guard invocation. The second check resets all observations. |
| `Xusb`, `InputsValid`, `LocateResult` | Existing route, combined input validation result, and raw CM_Locate_DevNode result. |
| `CompatibleSize`, `CompatibleData` | Existing property-query `Result`, `Type`, `Bytes` outputs, including outputs returned on error; null when not called. Key remains a45c254e-df1c-4efd-8020-67d146a850e0 / 4. |
| `CompatibleCount` | Count from the existing UTF-16 decode/split; no additional compatible-ID scan or values are logged. |
| `ParentChain` (max 16) | Existing per-step `DeviceIdResult`, `IsSelectedRoot`, `ParentResult`; no identifier strings or additional PnP calls. A failing DeviceId still contributes its result. |
| `ChainEnd` | Observed control-flow termination: `selected-root`, `device-id-error`, `parent-error`, `depth-limit`, or null before the loop. |
| `RootInChain`, `NoVirtualParent`, `GameUsage` | Booleans returned by the original short-circuit predicates; null if skipped. `NoVirtualParent=false` is the existing virtual-marker rejection. `GameUsage` is the exact HID usage-4/5 test. |
| `ActiveXusb`, `InterfaceAttempts` (max 2) | Existing XUSB active result and size/list attempt outputs (`SizeResult`, `Characters`, `ListResult`). HID records leave these unobserved. |

Null is not false and must not be converted into a guessed failure cause. Property result/type/size and parent errors are observations of those exact calls, not proof of a persistent device condition or hardware cause.

Native consumer hook (parent's final design): direct the existing helper stdout to a DELETE_ON_CLOSE temporary handle using an explicit inheritance list; after helper termination, perform the bounded read and merge the recognized prefix/JSON suffix into the ordinary native receipt. No live-process polling or device read is needed. Keep the helper exit status authoritative. An absent, truncated, invalid or unsupported record means unavailable diagnostic evidence; it grants no additional read, retry, device action or admission. Native transport/capture is owned by the parent and is not implemented here.

Run the isolated actual-source regression from the repository root:

```powershell
node tools/msi_game_authority_diagnostics_r114_selftest.mjs --baseline G:/YeManCC-Work/Build/Validation/VirtualInput-OwnedExtensions-R114-20261016/helper-diagnostics/before/InputHost/MsiNativePortCycle.cs --output G:/YeManCC-Work/Build/Validation/VirtualInput-OwnedExtensions-R114-20261016/helper-diagnostics
```

The runner copies the complete baseline and candidate source into the evidence directory. Its only executable substitutions replace eight CM extern declarations, the BIOS registry/lifetime/SDK boundaries, and the serializer fault-injection boundary (which forwards real System.Text.Json by default). No product Host, DLL or SDK is loaded. It compiles and runs only inert offline fixture assemblies outside the checkout. Source hashes, exact substitutions, unchanged-method spans, compiler logs and full observations are saved with the results.

Coverage: 151 execution scenarios compared against frozen R50 source, 18 exact predicate enumeration traces, and the unchanged 101-case production SelfTest. Both guards exercise Locate/property size/data/type, each DeviceId depth boundary, parent errors, missing/virtual roots, invalid game usages, XUSB size/list/empty/retry failure, and exceptions. Tests cover all existing exit-code families, root-only compatibility, MSI/ROG/GPD synthetic successes, disabled diagnostics, serializer/stdout/partial-write failure, one-shot behavior, count caps, exact 2048-byte acceptance and larger or multibyte byte-limit rejection. Synthetic family cases are not field acceptance.

## MSI HID capability authority update (2026-10-18)

The named R114 differential runner above preserves the original observability-only source-generation contract; its exact-trace result must not be used as acceptance for changed device-authority logic. The current helper additionally revalidates the real HID capabilities of MSI native-mode collections, independently of an empty CompatibleIds property. The MSI-only implementation and current actual-source regression are in `InputHost/MsiNativePortCycle.cs` and `tools/msi_native_hid_authority_selftest.mjs`. No HID capability result is borrowed from the parent process.

Three optional fields extend Schema 1: `HidCapabilityState` (bounded state text), `HidUsagePage` and `HidUsage` (unsigned 16-bit capability values). They describe observations from the new MSI metadata route and are absent when unobserved; `GameUsage` remains the final observed predicate. An absent value is not false or a persistent failure diagnosis. The existing 2048-byte record cap, failure-only output, one-shot behavior and parent exit-code checks remain in place. Other families retain their previous authority route. New hardware reads are part of the MSI authority check, not diagnostic-only reads. The original R114 historical description above applies to its frozen generation.

## Explicit current-source failure-status mode (2026-10-26)

The default `r114-frozen` mode still requires baseline SHA256 `fc5b4dc9c80defb80a9772b9426d0d64588b9cd157d32fe3606aaef393969a05` and retains its original tests, negative cases, exact traces and 2048-byte cap. It is not silently redirected to current source. The opt-in `--mode current-status` mode requires both `--baseline-sha256 <64 hex>` and `--candidate-sha256 <64 hex>`; missing or incorrect hashes reject before compilation. `--source <path>` is allowed only in that mode. Use the pre-edit helper as `--baseline`, the explicit hashes of both complete files, and a new evidence `--output` directory.

This extends the same consumer and inert boundaries. Both complete helper versions are compiled with every OS declaration replaced; unused HID boundaries throw if reached. The current mode executes XUSB MSI/ROG/GPD refresh paths, root success, the unchanged product SelfTest, the original 18 policy enumeration traces and serialization guards, plus failed Disable state/CONFIGRET/problem, pre/post identity mismatch or locate error, status error, diagnostic exceptions, null facts, successful Disable/failed Enable, success with zero new queries, serializer/stdout faults and the exact byte limit. It compares the actual Disable/Enable/SDK write traces exactly and all pre-existing calls exactly after separating the explicitly logged diagnostic reads. Original R114 assertions remain in their frozen mode; the current mode does not claim full HID-path acceptance.

The optional `GameRefreshFacts.DisableFailureStatus` object records `LocateResult`, `SameNode`, one `StatusResult` CONFIGRET with raw `Status` and `Problem` outputs, and `PostLocateResult`/`PostSameNode`. `State=observed` means both instance/DEVINST matches and the status query succeeded at those observation points. Any locate/status error, mismatch or diagnostic exception retains an explicit `unknown-*` state. Error outputs are raw evidence, not established node state. The snapshots do not identify a veto actor, establish caller permissions, prove absence of a device write or justify any retry. The original record-wide 2048-byte cap includes prefix and newline. No native consumer/header is changed.

Canonical consumer remains `tools/msi_game_authority_diagnostics_r114_selftest.mjs`; this is an explicit mode of that consumer, not a new global gate or framework. Its closure consists of both SHA-bound complete helper files, the existing Fixture.cs/Boundaries.cs, this runner, schema/readme, generated offline project, Node/.NET tools and recorded build/runtime outputs. Source bindings, exact substitutions, build/run logs, full calls, device-write-traces.json, timings and validation.json are emitted. This bounded invocation has no parent/global test run and no cache claim. Real Win5 state, actor, permissions, physical device effects and HID OS-path acceptance remain UNKNOWN without separately authorized hardware evidence.
