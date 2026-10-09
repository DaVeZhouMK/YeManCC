# v10 executor source/code freeze

Tool version: rog-portable-runner-2
Status: FROZEN_EXECUTOR_WRITE_SET; source hash/size drift = 0.
PS7 7.6.5: **140/140 cases PASS**, actual process exit **0**.
PS5.1: **6/6 source parser checks PASS**, actual parser exit **0**; default policy **Restricted**; inert -File sentinel exit **1**; full runner runtime **BLOCKED_BY_DEFAULT_EXECUTION_POLICY**. No Bypass/policy change; not PS7-equivalent coverage.

## Final selected reports
- G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\tools\cpu-rog\validation\runner-validation-v10-7.6.5-20261004T054526826Z-428dbd26.json
- G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\tools\cpu-rog\validation\runner-validation-v10-7.6.5-20261004T054526826Z-428dbd26.md
- G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\tools\cpu-rog\validation\runner-validation-v10-PS51-final-parser-20261004T054653896Z-b5b92c00.json
- G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\tools\cpu-rog\validation\runner-validation-v10-PS51-final-parser-20261004T054653896Z-b5b92c00.md

## Frozen modified source identities
| File | Bytes | SHA256 |
| --- | ---: | --- |
| Invoke-ROG-CPU.ps1 | 59160 | 55CD2B7DA11590B5659CD8F3DC524D243DA3BC838C3AD3822BC355BA4C666600 |
| Invoke-YMCC-AuthorizedWorker.ps1 | 5621 | 4BE38C7BF91811B40EAA739EBAC3B6FD2F47DC9A672A1448289868947EA9F21B |
| tests/Test-ROG-CPU-Runner.ps1 | 79945 | D58F0D5BEAD22CFFF89850312AB0AEDD4BCD7A5986330E5E08F50DF1326CEBBE |

## Bounded fixes
- Dedicated int/uint32/long PID 1..Int32Max checker; strict protocol sequence unchanged; typed CIM owner return status.
- Parent lexical descriptor/trusted-hash-presence/complete-UTC-selector gates precede token routing; no target file/hash/process/owner read on delegated low path.
- Worker proves own high token before all actual target validations, then finite operation; unverified token and wrong hash/path/birth/owner reject first.
- Read-only explicit PID without birth discovers actual UTC only; unknown fields and ambiguous/foreign roots not authorization.
- Three lexical rejection cases for directory wildcard, device namespaces and extra colon/ADS; ordinary local/UNC descriptors remain accepted without reads.
- Partial safe diagnostics/proof binding retained; discoveredHashIsAuthorization=false; no raw commandLine or opaque worker details projected.

## Limits / no acceptance claim
- All actual UAC/high-product access, start/stop/restart, CPU sampling, fan/controller/gyro and ROG runtime regression remain unverified and HOLD.
- PS7 gates use fake process/token/CIM/UAC/handles and inert file/collector; finite product actions in bootstrap routing cases are synthetic.
- PS5 parser only; default Restricted policy blocks inert -File sentinel with exit1; no runner execution and no policy bypass.
- CIM-only discovery; no native fallback, no guessed DMTF/local-time birth.
- windowObserved never establishes frontendReadyVerified or full restart/runtimeReady.
- Product version/hash has not been confirmed; raw ROG Results not received; performance NOT_RUN.
- Final gate has no actual product/native/UAC/sampling calls. A superseded preliminary gate may have queried the local invoking token in copied subprocess wrappers before fixture seam correction; it is not accepted as no-native evidence.

ROG actions HOLD; product version unconfirmed; ROG regression/performance NOT_RUN; rogRegressionPassed=false.
Attempt diagnostics refer to this tool invocation only, not other manual/AI activity on the machine.
No v10 packaging performed. v9 artifacts untouched. Collector/collector tests/product/parent docs/README not modified.

## Manifest contract
Adjacent kit-manifest.json, no self-hash. Exact root fields: version, sourceRole, localToolValidation, rogRegressionStatus, files. Each file entry has path, sha256, bytes. All copied docs are bound; future v10 package excludes historical v9 task.
Template below is documentation only: replace files with the complete actual copied inventory and exact numeric bytes/64-hex hash. PASSED means accepted PS7 offline gate only, not ROG or PS5 runtime acceptance.
```json
{
  "version": "10.20261004",
  "sourceRole": "portable-ROG-collector-toolkit",
  "localToolValidation": {
    "status": "PASSED",
    "scope": "local-software-tool-validation-only",
    "rogRegressionPassed": false,
    "coverage": {
      "runnerPS7": {
        "casesPassed": 140,
        "casesTotal": 140,
        "actualExitCode": 0,
        "type": "FULL_OFFLINE_FAKE_SEAMS"
      },
      "runnerPS51": {
        "parserPassed": 6,
        "parserTotal": 6,
        "actualParserExitCode": 0,
        "runtime": "BLOCKED_BY_DEFAULT_EXECUTION_POLICY",
        "equivalentToPS7Coverage": false
      },
      "collector": "DELEGATED_REPORTS_CONDITIONAL_NOT_REVALIDATED_BY_THIS_RUNNER_GATE"
    }
  },
  "rogRegressionStatus": "NOT_RUN",
  "files": [
    {
      "path": "relative/copied-file.ps1",
      "sha256": "REPLACE_WITH_64_HEX_COPY_SHA256",
      "bytes": 0
    }
  ]
}
```

## Entry syntax (documentation only, NOT executed against a target)
Use the full actual toolkit path for -File. Current real ROG operations remain HOLD.
- Default -Action validate: checks local tool/manifest, creates a new tool-output Results session only; no product/token/worker calls.
- Read-only -Action preflight -ExePath <normal-YMCC.exe>; optionally -TargetPid <pid> without birth for discovery.
- Read-only -Action status -ExePath <normal-YMCC.exe> -TargetPid <pid>.
- For either read-only action, only explicit -AllowElevation -AuthorizedAction <the exact same lowercase action> permits one finite Hidden RunAs worker. No trusted hash needed for discovery; its observed hash never becomes authorization.
- observe/capture/stop/restart/fan require independently trusted -ExpectedSha256 plus exact -TargetPid/-TargetCreationTimeUtc. Explicit elevation also needs the exact matching AuthorizedAction. Start requires trusted hash and action authorization, not pre-existing PID. Capture retains real control/runtime evidence gates.
- -SampleMilliseconds defaults to 250, overridable; -LogFile accepts explicit multi-values, max32, never scans or reads content.
- Offline test: pwsh -NoProfile -NonInteractive -File <toolkit>/tests/Test-ROG-CPU-Runner.ps1 (this final gate used source absolute path).
- PS5 runtime is currently blocked under default policy; no bypass recommended or used.
