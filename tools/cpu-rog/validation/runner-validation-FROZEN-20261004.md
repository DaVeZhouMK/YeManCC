# Runner/control source freeze and integration contract — 2026-10-04

Scope: local-software-tool-validation-only. rogRegressionPassed=false. ROG tool/product/CPU A-B: NOT_RUN.
Source/code frozen after PS7 7.6.5 full offline 68/68, exit 0. Matching hashes are in runner-validation-FROZEN-20261004.json and its selected PS7 report.
PS5.1 5.1.26100.8875 parser 6/6, exit 0. Full suite launch exit 1: Restricted script execution policy prevents loading the test script. No policy setting, Bypass or alternate execution of restricted script text was used. PS5.1 function/wrapper/UAC-mock/collector-mock coverage remains NOT_VERIFIED, not equivalent to PS7.
All runtime tests used fake process/control/native seams and an inert collector in temporary copies; no product, hardware, UAC, real sampling or port calls. Temporary fixture copies are not package content.

## Exactly one manifest name/schema

Adjacent kit-manifest.json (UTF-8), top-level keys only:

```json
{
  "version": "9.20261004",
  "sourceRole": "portable-ROG-collector-toolkit",
  "localToolValidation": {
    "status": "PASSED",
    "scope": "local-software-tool-validation-only",
    "rogRegressionPassed": false,
    "coverage": {
      "ps7FullOffline": "68/68 exit 0",
      "ps51Parser": "6/6 exit 0",
      "ps51FullOffline": "BLOCKED_EXECUTION_POLICY"
    }
  },
  "rogRegressionStatus": "NOT_RUN",
  "files": [
    {"path": "Invoke-ROG-CPU.ps1", "sha256": "64 hexadecimal digits", "bytes": 12345}
  ]
}
```

The example files array is illustrative, not a valid manifest. Enumerate actual relative paths with SHA256 and byte lengths, no manifest self-hash. Required core: Invoke-ROG-CPU.ps1, Invoke-YMCC-AuthorizedWorker.ps1, Control-YMCC-Lifecycle.ps1, Send-YMCC-AI-Fan-Mock.ps1, Collect-YMCC-CPU.ps1, config.example.json.
Required docs: docs/ROG-CAPTURE-AI-TASK-v9-20261004.md, docs/KNOWN-CAPTURE-ERRORS-AND-FIXES.md, docs/OPERATOR-COMMANDS.md. Every adjacent docs file must be listed, including START-HERE, ROG-RETURN-CHECKLIST, LEGACY-ROG-DATA-STATUS.json and any further final docs. Flat docs directory only; reject subdirectories/reparse. Bind README.md, tests, and selected current-hash validation reports as well.
Validator rejects absolute/traversal/Results/private-state paths, duplicates, hash/size mismatch, unsupported extensions or falsely claimed ROG status. No manifest in source means NOT_PRESENT_SOURCE_CHECK_ONLY, never verified portable kit. Local PASSED applies only to the documented PS7 gate and PS5 parser coverage, not PS5 full execution.
No Results, raw worker-request, old failed reports, settings backups, tokens/leases/profiles or inert fixtures in the package. Keep old failed reports in source for audit. Only the final PS7 report named by the freeze record and matching PS5 partial report are selected; do not reclassify earlier reports as PASSED.

## Entry/command contract (examples; real actions NOT executed here)

In a policy-permitted PowerShell 7 or Windows PowerShell 5.1 session, set $kit to the actual copied kit root. No ExecutionPolicy switch is added.

```powershell
# Default and explicit validate: no target queries, elevation, CPU or product effects.
& "$kit\Invoke-ROG-CPU.ps1" -Action validate
$LASTEXITCODE
# Read-only discovery: installed path is explicitly provided; no full-disk search.
& "$kit\Invoke-ROG-CPU.ps1" -Action preflight -ExePath $exe
& "$kit\Invoke-ROG-CPU.ps1" -Action status -ExePath $exe
# Exact read-only identity; creation time is ISO UTC, never culture-cast JSON DateTime.
& "$kit\Invoke-ROG-CPU.ps1" -Action preflight -ExePath $exe -ExpectedSha256 $sha -TargetPid $targetPid -TargetCreationTimeUtc $birth -SessionId $session
# Later, separately authorized ROG sampling: missing controls do not block observation.
& "$kit\Invoke-ROG-CPU.ps1" -Action observe -ExePath $exe -ExpectedSha256 $sha -TargetPid $targetPid -TargetCreationTimeUtc $birth -WarmupSeconds 15 -Seconds 30 -SampleMilliseconds 250 -LogFile $log1 $log2
# Future authorized normal restart, no force route.
& "$kit\Control-YMCC-Lifecycle.ps1" -Action restart -AuthorizedAction restart -AllowElevation -ExePath $exe -ExpectedSha256 $sha -TargetPid $targetPid -TargetCreationTimeUtc $birth -SessionId $session
# Strict mock only, after a trusted exact-session process exists.
& "$kit\Send-YMCC-AI-Fan-Mock.ps1" -Action probe -AuthorizedAction probe -AllowElevation -ExePath $exe -ExpectedSha256 $sha -ParentPid $targetPid -ParentCreationTimeUtc $birth -SessionId $session
# capture additionally requires the current exact root, trusted SHA, fresh lifecycle start and real feature evidence.
& "$kit\Invoke-ROG-CPU.ps1" -Action capture -ExePath $exe -ExpectedSha256 $sha -TargetPid $targetPid -TargetCreationTimeUtc $birth -LifecycleJsonl $life -VirtualHandshakeJsonl $handshake -ExpectedSessionId $session -Label $scene -FanState $fan -GamepadState $pad -GyroState $gyro
```

$sha is independently trusted final-product identity, NOT a hash discovered by the runner. Without it only validate/preflight/status are allowed. $targetPid/$birth must be copied from exact current readback, not guessed/reused. capture requires a lifecycle ymcc.start row with targetSha256 equal to $sha, applied/targetPrivilegeVerified boolean true, matching path/PID/birth/session and timestamp within 600 seconds; the real collector still adjudicates restart/cleanup/features. Missing or opaque evidence must fail, never generate feature ACKs.

All failures after script entry get a unique Results/session-*/repeat-*/result.json and nonzero exit. If output bootstrap is denied, emit safe JSON to stdout. Host script execution denial happens before entry; the toolkit cannot write a result in that case and will not bypass policy. Raw exception text/commandLine/start private args/evidence/settings are not copied. Only finite public mock/session arguments are accepted. Max LogFile=32; paths explicitly supplied, forwarded only to collector metadata, not content or recursive scanning. LogFile is not a config field. Default sample interval 250ms; actual jitter remains collector evidence, never assumed from the requested interval.

windowObserved is not frontend readiness. frontendReadyVerified=false and runtimeReadyVerified=false remain explicit; normal process/control success does NOT certify frontend-ready or full product regression. Ordinary argument/environment inheritance is recorded as NOT_ROG_VERIFIED.
Fan state derives only from exact mock-session CLI; optional paired isolated SID derives exeDir/ai-cpu-sessions/SID/shared/ai-fan-sessions/SID/fan-host. No StateDir override. Parent identity/CIM birth/current user, host path/parent/FILETIME, reparse boundaries and zero-physical-write readback are checked. Capability failure is CAPABILITY_UNVERIFIED/PRODUCT_UPDATE_REQUIRED, not a claim that YMCC has no virtual Fan. No gamepad/gyro runtime certification is synthesized.

Read-only review of final OPERATOR-COMMANDS/README: parameter names and public paired StartArgument syntax match the frozen parser; explicit LogFile and 250ms defaults match collector contract. On this machine PS5 commands cannot execute while policy is Restricted; that is a concrete release limitation, not a successful PS5 gate. ROG may run them only under its legitimate applicable policy. No parent docs/README were edited.

Packaging is NOT performed. Wait for explicit parent instruction and a stable collector. Bind all final docs and current file hashes when that instruction arrives.