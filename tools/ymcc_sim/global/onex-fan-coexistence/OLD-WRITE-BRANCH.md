# Exact old-write branch review

This independent metadata/source review closes the previous stage's 19 actual helper edges for the fixed Host a678, producer fe96, and synthetic HC 6d7 invocation. It does not execute an assembly, invoke the producer, or grant permission to run Host. The module uses only Node fs/path/crypto/url and does not spawn a managed process.

The original gate remains old-write: 6 cases, 9 golden writes, native attempts 0, budget 45000 ms. The current parent separately reports a 35-check actual run; that runtime receipt is not worker test evidence. The 119 original failures, 360000 ms parent budget, 7+2+3 physical ports and 3072 tool gate are unchanged.

## Entry and exact input schema

Keep these siblings together: old-write-branch-review.mjs, old-write-branch-basis.json, old-write-branch-review.test.mjs. The basis is an exact-byte review allowlist, not a source-discovery or simple-name permission mechanism.

CLI arguments must be exactly:

```powershell
& $ExactNode $BranchModule --inputs $ExactRequest --sha256 $RequestSha256 --evidence-dir $FreshOwnedReviewDirectory
```

The request schema is version 1, kind onex-old-write-branch-review. The reference BRANCH-REVIEW-INPUTS.json contains exact pins for priorSeal, preparedPlan, priorAnalysis, priorNextReview, priorPrepareInputs, rawReports, sourcePins and rawReaderValidation. The tool validates every input SHA, actual output/metadata/context pin, exact argv, launcher, raw-HC metadata role, 44 normalized Default managed pins/requests and the two exact custom Host/Backend targets. Unknown paths, changed identity/route/state/body hashes and occupied/redirected output locations fail closed.

For a relocated primary preparation, create a NEW request, change only preparedPlan to the primary exact path and full SHA, and invoke this review into a fresh directory. The owned portion of Default policy paths is normalized, while bytes/identities/requests, custom context and custom targets remain exact. This produces a new review binding the primary policy and argv. It does not make the sealed worker review valid for a different plan without rebinding. The worker plan hash is 03ff8fc0cebc40ad45ffb03216b8a92e52674e8a234c5fa282a02349631ce2bb; prior analysis is 4ae23135c34061ec23774f45db151724ac31ccf2e8d2f7922e52c4dfa1527e56. The parent reports it already rebound its primary 135bd7e... plan and passed this review; that separate parent review is not recreated here.

Example rebind script (parameters must be explicit full paths and a full SHA; this command performs only the Node static review):

```powershell
param(
  [Parameter(Mandatory)][string] $PrimaryPreparedPlan,
  [Parameter(Mandatory)][ValidatePattern('^[a-f0-9]{64}$')][string] $ExpectedPrimaryPlanSha256,
  [Parameter(Mandatory)][string] $FreshOwnedRoot
)
$ExactNode = 'G:/YeManCC-Work/Environment/Toolchains/nodejs/node-v22.14.0-win-x64/node.exe'
$BranchModule = 'G:/YeManCC-Work/Mainline/YeManCC-source/YeManCC/tools/ymcc_sim/global/onex-fan-coexistence/old-write-branch-review.mjs'
$Template = 'D:/Codex/2026-10-18/kai-s/work/global-fan-old-write-branch-close-20261021-r1/BRANCH-REVIEW-INPUTS.json'
if ((Get-FileHash -LiteralPath $Template -Algorithm SHA256).Hash.ToLowerInvariant() -ne '6fc6b9009c217b7c5292c1c4585eb1a12ed5fc9ffdc4d8f306b6d8f22b78ead9') { throw 'Changed template' }
if ((Get-FileHash -LiteralPath $PrimaryPreparedPlan -Algorithm SHA256).Hash.ToLowerInvariant() -ne $ExpectedPrimaryPlanSha256) { throw 'Changed primary plan' }
if (Test-Path -LiteralPath $FreshOwnedRoot) { throw 'Fresh owned root required' }
New-Item -ItemType Directory -Path $FreshOwnedRoot | Out-Null
$request = Get-Content -LiteralPath $Template -Raw | ConvertFrom-Json
$request.preparedPlan = [pscustomobject]@{path=$PrimaryPreparedPlan;sha256=$ExpectedPrimaryPlanSha256}
$ExactRequest = Join-Path $FreshOwnedRoot 'BRANCH-REVIEW-INPUTS.json'
[IO.File]::WriteAllText($ExactRequest, ($request | ConvertTo-Json -Depth 100), [Text.UTF8Encoding]::new($false))
$RequestSha256 = (Get-FileHash -LiteralPath $ExactRequest -Algorithm SHA256).Hash.ToLowerInvariant()
$FreshOwnedReviewDirectory = Join-Path $FreshOwnedRoot 'review'
& $ExactNode $BranchModule --inputs $ExactRequest --sha256 $RequestSha256 --evidence-dir $FreshOwnedReviewDirectory
if ($LASTEXITCODE -ne 0) { throw 'Exact old-write branch review refused' }
```

Use the overlay module path until the parent integrates these four additions. The exported API reviewOldWriteBranch({inputsPin,evidenceDir}) returns the review receipt pin and never launches the run vector. reviewOldWriteBranchDocuments is the pure document-review entry used by the ten gates. The caller must validate this new source/basis through the delivery manifest.

## Concrete fixed-state proof

- Route: all three actual OldWriteProbe.Run device newobj sites are OneXPlayerX1AMD. Backend uses an uninitialized HostOptions, actual RealHcBackend ctor, then injects device, typeof(IDevice).Assembly and eager Diagnostic. FanProvider is null; ctor IL 0x18d branches to 0x1b6 ldc.i4.0 and 0x1d9 stores useBackendFanWriter=false. FactoryType comes from actual device.GetType().FullName. BuildFanRoutes stores GenericDuty=0 / GenericEcControl=1, matches OneXPlayerX1AMD at offset360 and calls Add at404. Add prefixes HandheldCompanion.Devices. and constructs FanRoute. Enum constants and route assignments are checked, including no later relevant override. This conclusion does not follow from a class name alone.
- MSI/ASUS: actual restore1 skips the compare8 branches. ApplyPowerProfile compares at0x54 and branches at0x57 to0xc0; BeginMsiReleaseOperation/SettleMsiCloseRelease compare at0x12 and return at0x1a. UsesBackendFanWriter returns false at0x1e. Prior exact base-chain and IsMsiFamily proof remains. The 36 MSI plus 1 ASUS conservative edges were not recursively scanned.
- Diagnostic: DiagnosticLazy remains null and eager Diagnostic is supplied. EmitDiagnostic branches at0x08 to0x13, then actually evaluates Func.Invoke at0x20 and Action.Invoke at0x25. Exact own Backend callback performs only JSON serialization, string concatenation and own List.Add. JsonDefaults .cctor only configures serializer options without custom converters. Eager factories are reviewed, not assumed skipped.
- Fresh monitor: its constructor creates dictionaries/gates/a byte buffer and never assigns timer/cachedMappingView/cachedMapping. Original producer source writes monitor.sampleReadTick three times, with no other monitor writes or Start/Poll; reviewed synchronous methods create no timer/mapping. Backend temperatureWatchdog stays null. Timer Dispose guard/skip/call are0x2e/0x31/0x33, view0x1b/0x1e/0x20, mapping0x32/0x35/0x37. These guarded external leaves cannot run in this fresh-state invocation. A change that can populate any of them invalidates the proof.
- Close manager observation: ShouldObserveManagerAfterClose is arg0,arg1,or,ret. Close sets HcVirtualCloseReturned=true at0x9c/0x9d, so this observation is statically predicted to run. It reflects exact synthetic ManagerFactory.Managers (SettingsManager/DeviceManager/PowerProfileManager) and IManager.Status backing fields. Constructors perform dictionary/profile/object allocations. Observe tests the null SelfTestHcBoundary at0x0c and branches at0x13 to0xf1; no real manager Start/Stop is entered.
- Candidate reflection: approved raw metadata proves TryWaitCloseCleanupCompleted absent on OneXPlayerX1AMD, OneXPlayerX1, OneXAOKZOE and IDevice; no such method is invoked. Synchronous temperature readiness is checked to field/epoch/reference checks and exact synthetic IsOpen, without real curve work, timer start or manager bootstrap.

IMPORTANT: facts.close-manager-observation.actuallyExecuted=true in the JSON is the static branch classification described above. All worker target executions are 0. It is not a report of an earlier business execution. Parent actual execution evidence is separate.

NoIO here means no device, registry, network or foreign-process effects on this exact branch. Pinned PE/policy/metadata reads, fresh owned JSON/log writes, own thread/process/clock observations, pure managed work and existing synthetic OLS/EC/serial traces are explicit allowed terminals. No full CLR/native/Host isolation or whole-policy execution proof is claimed. There is no unresolved actual helper in this bounded scope. Parent policy Prepare/default44/TPA checks, exact authorized invocation, freshness/natural exit and managed/native post checks remain independent gates.

## Reviewed 19 MethodDefs

| Token | Exact method |
| --- | --- |
| `0x06000fbf` | `System.Void YeManFanHost.RealHcBackend::EnsureHcSessionReadyForControl()` |
| `0x06000ee8` | `System.String YeManFanHost.RealHcBackend::get_EffectiveExpectedHcSha256()` |
| `0x06000f07` | `System.Void YeManFanHost.RealHcBackend::set_HcDeviceManagerStopCompleted(System.Boolean)` |
| `0x06001744` | `System.Void YeManFanHost.RealHcBackend/<>c__DisplayClass580_0::.ctor()` |
| `0x06001745` | `System.Boolean YeManFanHost.RealHcBackend/<>c__DisplayClass580_0::<TryInvokeCached>b__0(System.Reflection.MethodInfo)` |
| `0x06001706` | `System.Void YeManFanHost.RealHcBackend/<>c__DisplayClass501_0::.ctor()` |
| `0x060016ac` | `System.Double YeManFanHost.RealHcBackend/<>c::<BuildFanDispatchEvidence>b__501_0(System.Double)` |
| `0x06000f49` | `System.Collections.Generic.IReadOnlyList`1<System.String> YeManFanHost.RealHcBackend::ResolveFanCommandChannels(System.Nullable`1<YeManFanHost.FanRestoreStrategy>)` |
| `0x06001707` | `<>f__AnonymousType86`10<System.String,System.Double[],System.Double[],System.String,System.String,System.String,System.String,System.String,System.String,System.String> YeManFanHost.RealHcBackend/<>c__DisplayClass501_0::<BuildFanDispatchEvidence>b__1(System.String)` |
| `0x06001172` | `System.String YeManFanHost.FanHostEngine::ReleaseActionKindName(YeManFanHost.OemReleaseAction)` |
| `0x060016c5` | `System.Boolean YeManFanHost.RealHcBackend/<>c::<ConfirmCandidateCloseCleanup>b__688_0(System.Reflection.MethodInfo)` |
| `0x060016c6` | `System.Boolean YeManFanHost.RealHcBackend/<>c::<ConfirmCandidateCloseCleanup>b__688_1(System.Reflection.MethodInfo)` |
| `0x060014f1` | `System.Int64 YeManFanHost.RealHcBackend/ExternalProfilePendingQueue::InvalidateSession()` |
| `0x06000f01` | `System.Boolean YeManFanHost.RealHcBackend::get_HcVirtualCloseReturned()` |
| `0x06000fe8` | `System.Boolean YeManFanHost.RealHcBackend::ShouldObserveManagerAfterClose(System.Boolean,System.Boolean)` |
| `0x06000f5f` | `System.Void YeManFanHost.RealHcBackend::ObserveManagerFactoryLifecycle()` |
| `0x06000fe9` | `YeManFanHost.RealHcBackend/ManagerObservationOutcome YeManFanHost.RealHcBackend::DescribeManagerObservation(System.String,System.String)` |
| `0x06000fea` | `YeManFanHost.RealHcBackend/ManagerCleanupOutcome YeManFanHost.RealHcBackend::DecideManagerCleanupAfterClose(System.Boolean,System.Boolean,YeManFanHost.RealHcBackend/ManagerObservationOutcome)` |
| `0x06000f30` | `System.Action`1<System.Exception> YeManFanHost.RealHcBackend::get_FanDispatchFailure()` |

## Evidence and finite gates

The old reports already cover301 unique metadata methods. Three bounded raw-reader processes add35 unique methods (union336); no target Assembly.Load is used. The exact basis locks76 reviewed body hashes. Final Node CLI exited0 naturally in1088ms. Ten pure document gates passed: exact positive, changed6/9/45s, different actual device newobj, non-null lazy diagnostic, monitor timer initialization, same class with changed route, manager getter extra effect, newly present close API, unknown Default path, and missing required helper body. Target executions0; original business cases/goldens executed0. Prototype receipts are retained but not counted as additional tests.

Reference worker review: evidence/final-cli-review/OLD-WRITE-BRANCH-REVIEW.json, SHA 3dee8d19ef2bb446d1f65af98cf63dd6617f5fdb0accd7082c1d3a68ffadd91c. Reference finite gates: evidence/finite-gates/FINITE-GATES.json. Parent reported primary branch review: phase32/fan-old-write-primary-branch-review-r1. Parent reported actual run: phase32/fan-old-write-primary-business-r1/SUMMARY.json (6cases/35checks/9goldens/native0,908ms,natural exit0). These parent paths were supplied by the user and not read or hashed by this worker.
