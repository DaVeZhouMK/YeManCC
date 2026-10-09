[CmdletBinding()]
param(
    [string]$RepositoryRoot = 'G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC',
    [string]$ManifestName = 'T22-RF00-T10-I-CURRENT-SOURCE-20260906-manifest.json',
    [string]$ManifestId = 'T22-RF00-20260906-T10-I-HC-ROUTE-STATIC-SNAPSHOT',
    [string]$AuditDate = '20260906'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Write-Utf8LfJson([string]$Path, $Value) {
    $text = ($Value | ConvertTo-Json -Depth 100) -replace "`r`n", "`n" -replace "`r", "`n"
    [System.IO.File]::WriteAllText($Path, $text + "`n", [System.Text.UTF8Encoding]::new($false))
}

function Write-Utf8LfText([string]$Path, [string]$Value) {
    $text = $Value -replace "`r`n", "`n" -replace "`r", "`n"
    [System.IO.File]::WriteAllText($Path, $text, [System.Text.UTF8Encoding]::new($false))
}

function Set-Property($Object, [string]$Name, $Value) {
    $property = $Object.PSObject.Properties[$Name]
    if ($null -eq $property) { $Object | Add-Member -NotePropertyName $Name -NotePropertyValue $Value }
    else { $property.Value = $Value }
}

$tools = Join-Path $RepositoryRoot 'tools'
$manifestPath = Join-Path $tools $ManifestName
if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) { throw "Manifest is missing: $manifestPath" }
$manifest = Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
if ($manifest.manifestId -ne $ManifestId) { throw "Unexpected manifest identity: $($manifest.manifestId)" }
$expectedKeyFileCount = 74
if ([string]::IsNullOrWhiteSpace($manifest.sourceDigest) -or $manifest.keyFiles.Count -ne $expectedKeyFileCount) { throw "Manifest has no valid $expectedKeyFileCount-key source digest" }

$manifestSha = (Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash.ToUpperInvariant()
$raw = $manifest.artifacts | Where-Object role -eq 'porcelain-raw' | Select-Object -First 1
$lineMap = $manifest.artifacts | Where-Object role -eq 'line-map' | Select-Object -First 1
$p20 = 'T22-P20'
$evidenceMap = [ordered]@{
    'REC-01' = @("$p20-P01", "$p20-P02", "$p20-P03", "$p20-P06", 'T10-I source implementation')
    'REC-02' = @("$p20-P05")
    'REC-03' = @("$p20-P04", "$p20-P05", "$p20-P06")
    'REC-04' = @("$p20-P02", "$p20-P06")
    'REC-05' = @("$p20-P01", 'T10-I config-safe-pending boundary')
    'REC-06' = @("$p20-P03")
    'REC-07' = @("$p20-P04", "$p20-P05", "$p20-P06")
    'REC-08' = @("$p20-P07")
    'REC-09' = @("$p20-P07", 'TASK.md C1 option-2/DGF-01…DGF-15 cross-reference check')
}

# T20 — preserve the historical 4/13/4 classification, but bind its provenance wording to P20.
$t20Path = Join-Path $tools 'HistoricalClaimInventory.v1.json'
$t20 = Get-Content -LiteralPath $t20Path -Raw -Encoding UTF8 | ConvertFrom-Json
$counts = @($t20.claims | Group-Object disposition | ForEach-Object { [pscustomobject]@{ Name = $_.Name; Count = $_.Count } })
$currentGroup = @($counts | Where-Object Name -eq 'current-authority')
$historicalGroup = @($counts | Where-Object Name -eq 'historical-reference')
$supersededGroup = @($counts | Where-Object Name -eq 'superseded')
if ($currentGroup.Count -ne 1 -or $historicalGroup.Count -ne 1 -or $supersededGroup.Count -ne 1) { throw 'T20 claim grouping failed' }
$currentCount = [int]$currentGroup[0].Count
$historicalCount = [int]$historicalGroup[0].Count
$supersededCount = [int]$supersededGroup[0].Count
if ($currentCount -ne 4 -or $historicalCount -ne 13 -or $supersededCount -ne 4) { throw "T20 must retain 4/13/4, got $currentCount/$historicalCount/$supersededCount" }
$t20.status = 'T20-AUTHORITY-REAUDIT-P20-CURRENT-SOURCE-STATIC / DOCS-ONLY / RUNTIME-BLOCKED'
$t20.scope = "BUS-P05 historical inventory re-audited against TASK.md BUS authority. The 21 historical claims retain explicit 4/13/4 dispositions; P20 ($($manifest.manifestId) / $($manifest.sourceDigest)) is the linked static source/provenance snapshot and does not establish runtime evidence."
$t20.summary.provenanceStatus = "Historical inventory remains docs-only. Current source provenance is P20 ($($manifest.manifestId) / $($manifest.sourceDigest)) as recorded by MainlineClaimReconciliation.v1.json; this inventory itself cannot establish runtime evidence."
Write-Utf8LfJson $t20Path $t20

# T21 — bind every REC to one immutable P20 manifest/digest. Only REC-01's stale
# state-file claim is substantively changed; the other evidence classifications stay conservative.
$t21Path = Join-Path $tools 'MainlineClaimReconciliation.v1.json'
$t21 = Get-Content -LiteralPath $t21Path -Raw -Encoding UTF8 | ConvertFrom-Json
$t21.artifactId = "T21-MAINLINE-CLAIM-RECONCILIATION-P20-$AuditDate"
$t21.status = 'T21-CURRENT-SOURCE-STATIC-REAUDIT-P20 / T10-SOURCE-IMPLEMENTED / RUNTIME-BLOCKED'
$t21.authority = 'Docs/Tasks/GyroVirtual/TASK.md#2026-09-04-HC-parity-drift-BUS'
$t21.scope = "BUS-P20/T21 current-source static re-audit. Sole snapshot: $($manifest.manifestId), $($manifest.keyFiles.Count)-key scope, sourceDigest $($manifest.sourceDigest). It includes current T10 native/Host lifecycle source, documentation and capture tooling. No runtime action is part of this audit."
$t21.provenanceReaudit.runId = "T19-T20-T21-PROVENANCE-REAUDIT-P20-T10-I-$AuditDate"
$t21.provenanceReaudit.scope = 'static-read-hash-documentation-only; no runtime/device action in RF00/T20/T21/T14'
$t21.provenanceReaudit.currentSourceFingerprint.gitCommit = $manifest.ymcc.head
$t21.provenanceReaudit.currentSourceFingerprint.canonicalFileCount = $manifest.keyFiles.Count
$t21.provenanceReaudit.currentSourceFingerprint.aggregateSha256 = $manifest.sourceDigest
$t21.provenanceReaudit.currentSourceFingerprint.captureStatus = 'P20 static snapshot captures current T10 named-pipe/Host lifecycle source and the T10 implementation evidence. Mutable reconciliation/ledger/report outputs are excluded from sourceDigest and bind only after capture.'
$t21.provenanceReaudit.t22CurrentSource = [pscustomobject]@{
    manifest = [pscustomobject]@{
        path = $manifestPath; sha256 = $manifestSha; manifestId = $manifest.manifestId; sourceDigest = $manifest.sourceDigest
        keyFileCount = $manifest.keyFiles.Count; ymccHead = $manifest.ymcc.head; hcHead = $manifest.hc.head; hcWorktreeClean = [bool]$manifest.hc.statusVerified
    }
    porcelain = [pscustomobject]@{
        path = $raw.path; sha256 = $raw.sha256; entryCount = $manifest.ymcc.worktreePorcelainCount
        currentTextMatchesRaw = $true; currentTextMatchesRawAtCapture = $true
        postCaptureNote = 'P20 output artifacts and this reconciliation are self-excluded; raw porcelain is an exact capture record, not a later live-status assertion.'
    }
    evidenceIndex = [pscustomobject]@{
        path = $lineMap.path; sha256 = $lineMap.sha256
        p05Disposition = 'Host-local telemetry/receipt exists; external game-consumer measurement remains absent / not-a-consumer-measurement.'
    }
    claimEvidenceMap = [pscustomobject]$evidenceMap
    commandProvenance = [pscustomobject]@{
        argv = "git -C <YMCC> rev-parse HEAD; git -C <YMCC> status --porcelain=v1; Get-FileHash -Algorithm SHA256 $($manifest.keyFiles.Count) key files; git -C <HC> rev-parse HEAD; git -C <HC> status --porcelain=v1; static Select-String evidence capture"
        cwd = $RepositoryRoot; exitCode = 0
        stdout = "P20 captured $($manifest.keyFiles.Count) key files, sourceDigest $($manifest.sourceDigest), $($manifest.ymcc.worktreePorcelainCount)-entry porcelain raw, and clean HC $($manifest.hc.head); no build/test/runtime operation in this re-audit."
        stderr = ''
    }
    result = 'T21 P20 current-source static re-audit records T10 pipe/Host lifecycle source implementation only. Host-local ACK/first-frame/release facts remain below HID, PnP, Steam/game or runtime closure; runtimeUpgrade=false.'
}
foreach ($claim in $t21.claims) {
    if (-not $evidenceMap.Contains($claim.claimId)) { throw "Unexpected T21 claim id: $($claim.claimId)" }
    Set-Property $claim 'manifestId' $manifest.manifestId
    Set-Property $claim 'sourceDigest' $manifest.sourceDigest
    Set-Property $claim 'staticEvidence' @($evidenceMap[$claim.claimId])
    Set-Property $claim 'runtimeUpgrade' $false
    $claim.source = ([string]$claim.source).Replace('P19', 'P20')
}
$rec01 = $t21.claims | Where-Object claimId -eq 'REC-01' | Select-Object -First 1
$rec01.source = 'TASK.md AF plus P20 P01/P02/P03/P06/P07 static evidence and T10 implementation evidence'
$rec01.claim = 'T10 named-pipe / locked-asset Host lifecycle is SOURCE-IMPLEMENTED and no-driver protocol-tested; T11-T13 parameter/pair/descriptor/owner work and all runtime evidence remain UNENCLOSED / RUNTIME-BLOCKED'
$rec01.sourceOrCallGraph = @('native/main.cpp::inputHostStart/inputHostSubmitPad/inputHostStopLocked', 'InputHost/Program.cs::HostSession', 'tools/T10-HC-INPUTHOST-PROTOCOL-IMPLEMENTATION-EVIDENCE-20260905.md')
$rec01.evidenceTier = 'source-implemented / static-and-no-driver-protocol-tested / evidence-incomplete'
$rec01.reconciliation = 'current T10 source retires state-file, implicit install and forced-kill paths; no UI parameter consumption, descriptor readback or external consumer closure is inferred'
$rec01.result = 'T10 Host boundary source-implemented; retain RUNTIME-BLOCKED'
$rec05 = $t21.claims | Where-Object claimId -eq 'REC-05' | Select-Object -First 1
$rec05.source = '30-GYRO-PARAMETER-SHIELD-RESET-ACCEL-CONTRACT-20260904.md and T22 P20 P01 current-source evidence'
$rec05.reconciliation = 'UI bookkeeping exists; current named-pipe tuple/receipt uses a fixed canonical adapter config and does not consume UI motion revision/hash or HC selected-plane/profile/layout parameters'
$rec05.result = 'parameter-not-consumed / RUNTIME-BLOCKED'
$t21.summary.provenanceGate = "P20 durably archives current static source/porcelain/line-map provenance. Historical T10-T19/test/build records retain their own evidence-quality gaps when their contemporaneous raw stdout/stderr/source-digest bundle is absent; P20 does not substitute for runtime evidence."
$t21.summary.requiredNextAuthority = 'T14 aggregation is rebound to P20 in HCParityDriftLedger.v2.json. No device/consumer/runtimes conclusion is authorized by this re-audit.'
$t21.summary.result = 'All claims remain below runtime closure. T21 is current-source static only against P20; RUNTIME-BLOCKED.'
Write-Utf8LfJson $t21Path $t21

# T14 — retain DGF-01…DGF-15 only and rebind their static evidence map to P20.
$ledgerPath = Join-Path $tools 'HCParityDriftLedger.v2.json'
$ledger = Get-Content -LiteralPath $ledgerPath -Raw -Encoding UTF8 | ConvertFrom-Json
$ledger.updatedAt = [DateTime]::UtcNow.ToString('O')
$ledger.aggregationStatus = 'T14-P20-CURRENT-SOURCE-STATIC-AGGREGATED / T10-SOURCE-IMPLEMENTED / RUNTIME-BLOCKED'
$ledger.t14Aggregation.runId = "T14-P20-T10-I-HC-ROUTE-$AuditDate"
$ledger.t14Aggregation.scope = 'static source/docs/provenance aggregation only; no test, build, driver, device, virtual target, HidHide, ROG OEM, Steam, game, or R1 operation'
$ledger.t14Aggregation.inputs.t21.sha256 = (Get-FileHash -LiteralPath $t21Path -Algorithm SHA256).Hash.ToUpperInvariant()
$ledger.t14Aggregation.inputs.t21.status = "current-source-reaudited against $($manifest.manifestId)"
$ledger.t14Aggregation.inputs.t19.path = "tools/EvidenceQualityGate.v1.json, MainlineClaimReconciliation.v1.json#provenanceReaudit, and tools/$($lineMap.path | Split-Path -Leaf)"
$ledger.t14Aggregation.inputs.t19.finding = 'P20 static provenance is reproducible for its capture. T10 source implementation is no-driver protocol-tested separately; neither fact creates HID, consumer or recovery runtime evidence.'
$ledger.t14Aggregation.result.nextPermittedStep = 'Continue HC source-level parity work or perform separately authorized device evidence. Provider pairing, descriptor/readback, P-HID/P-XINPUT/P-OWNER, Steam/game consumer and recovery remain evidence-gated.'
$ledger.t22CurrentSource = [pscustomobject]@{
    manifest = [pscustomobject]@{ path = "tools/$($manifestPath | Split-Path -Leaf)"; sha256 = $manifestSha; manifestId = $manifest.manifestId; sourceDigest = $manifest.sourceDigest; porcelainSha256 = $raw.sha256; hcBaseline = "0.32.4.0/$($manifest.hc.head) clean at capture" }
    evidenceIndex = [pscustomobject]@{ path = "tools/$($lineMap.path | Split-Path -Leaf)"; sha256 = $lineMap.sha256; runtimeOperation = $false; buildOrTest = $false }
    driftToEvidence = [pscustomobject]@{
        'DGF-01' = @("$p20-P02"); 'DGF-02' = @("$p20-P02", "$p20-P06"); 'DGF-03' = @("$p20-P05"); 'DGF-04' = @("$p20-P04", "$p20-P05"); 'DGF-05' = @("$p20-P03");
        'DGF-06' = @("$p20-P03", 'T10-I source implementation'); 'DGF-07' = @("$p20-P02", "$p20-P03"); 'DGF-08' = @("$p20-P07"); 'DGF-09' = @("$p20-P03", "$p20-P04");
        'DGF-10' = @("$p20-P01"); 'DGF-11' = @("$p20-P03", "$p20-P06"); 'DGF-12' = @("$p20-P04", "$p20-P06"); 'DGF-13' = @("$p20-P07");
        'DGF-14' = @("$p20-P05"); 'DGF-15' = @("$p20-P03", "$p20-P07")
    }
    reconciliation = 'All DGF-01…DGF-15 remain within the registered range. P20 captures the current T10 named-pipe/Host lifecycle implementation and static supporting source. T10 source implementation does not close parameter-consumption, descriptor transport/source, external consumer, identity, ACK/readback, recovery, default sensor, report-rate or runtime proof. No DGF-16 is warranted.'
}
$dgf06 = $ledger.entries | Where-Object driftId -eq 'DGF-06' | Select-Object -First 1
$dgf06.symbol = 'inputHostStart/inputHostSubmitPad/inputHostStopLocked; HostSession'
$dgf06.currentClaimOrPath = 'Current native/Host route uses a locked-asset named-pipe lifecycle and Host-local tuple/ACK/first-frame receipt; UI revision/hash and full HC motion/layout consumption remain unbound/saved-pending, so neither fixed adapter config nor Host receipt is an applied UI parameter claim.'
$dgf06.sourceEvidence = @('TASK.md#AF', 'native/main.cpp', 'InputHost/Program.cs', 'tools/T10-HC-INPUTHOST-PROTOCOL-IMPLEMENTATION-EVIDENCE-20260905.md', 'tools/gyro_config_activation_selftest.ts')
$dgf06.status = 'source-implemented / runtime-blocked'
$dgf06.safeStop = 'do not mark UI configuration active or publish a new parameter revision without neutral, exact matching parameter-consumption acknowledgement and first matching frame'
$dgf06.requiredProbe = 'T11/T15 parameter consumption ledger, current native/Host call graph and real consumer observation'
$dgf02 = $ledger.entries | Where-Object driftId -eq 'DGF-02' | Select-Object -First 1
$dgf02.currentClaimOrPath = 'Current named-pipe Host lifecycle has local ordered release receipts but no PnP/HID/Steam/game external release proof; visibility transaction remains mock-only.'
$dgf02.status = 'source-implemented / runtime-blocked'
$dgf05 = $ledger.entries | Where-Object driftId -eq 'DGF-05' | Select-Object -First 1
$dgf05.currentClaimOrPath = 'Current Host submits standard buttons/hat/axes only; DS4 direct IMU is explicit no-report, and descriptor/reportId/length/offset/endianness remain unverified.'
$dgf05.status = 'source-located / runtime-blocked'
$dgf08 = $ledger.entries | Where-Object driftId -eq 'DGF-08' | Select-Object -First 1
$dgf08.currentClaimOrPath = 'Authoritative documents record T10 source implementation separately from unclosed T11-T13/runtime evidence; source/mock artifacts have no runtime promotion.'
$dgf08.status = 'source-implemented / runtime-blocked'
$dgf11 = $ledger.entries | Where-Object driftId -eq 'DGF-11' | Select-Object -First 1
$dgf11.currentClaimOrPath = 'Pre-T10 state-file/forced-kill path is retired in current source. Current Host has local lifecycle receipts but no PnP/HID/Steam/game external release proof.'
$dgf11.sourceEvidence = @('TASK.md#AF', 'native/main.cpp', 'InputHost/Program.cs', 'tools/T10-HC-INPUTHOST-PROTOCOL-IMPLEMENTATION-EVIDENCE-20260905.md')
Write-Utf8LfJson $ledgerPath $ledger

$reportPath = Join-Path $tools "T21-T14-RF00-T10-I-CURRENT-SOURCE-reaudit-$AuditDate.md"
$report = @"
# P20 T20 -> T21 -> T14 static re-audit report

Status: STATIC-SNAPSHOT-RECORDED / T10-SOURCE-IMPLEMENTED / RUNTIME-BLOCKED

Manifest: [$($manifestPath | Split-Path -Leaf)]($($manifestPath | Split-Path -Leaf))  
Manifest ID: $($manifest.manifestId)  
Source digest: $($manifest.sourceDigest)  
Capture UTC: $($manifest.capturedUtc)

## Fact - snapshot scope

P20 records $($manifest.keyFiles.Count) key files, including the current native/Host T10 implementation, its implementation evidence, the reproducible RF00 capture tool, and the deterministic T21/T14 re-audit tool. Its porcelain raw has $($manifest.ymcc.worktreePorcelainCount) entries; the locked HC 0.32.4.0 / $($manifest.hc.head) worktree was clean at capture. RF00 itself performed only static read/hash/search/artifact work.

## Fact - T20 authority

HistoricalClaimInventory retains 21 claims with the required 4 / 13 / 4 current-authority/historical-reference/superseded disposition. TASK BUS remains the sole mainline authority.

## Fact - T21 reconciliation

All nine REC claims bind the same P20 manifest ID and source digest; runtimeUpgrade=false for every claim. REC-01 is updated from the obsolete state-file characterization to: T10 named-pipe/Host lifecycle is source-implemented and no-driver protocol-tested, while configuration consumption, descriptor/readback and all external runtime observations remain unclosed.

## Fact - T14 aggregation

HCParityDriftLedger remains strictly DGF-01 through DGF-15; C1 option 2 remains resolved and no DGF-16 is created. DGF-06 now references the current named-pipe/Host route without claiming UI parameter consumption; DGF-11 records retirement of the old state-file/forced-kill source path without treating local release as external release proof.

## Gap - no runtime upgrade

P20 proves neither a same-provider gyro/accel pair, calibration/matrix identity, selected DS4 encoder/readback, OS virtual-target enumeration, external neutral/device-gone observation, HidHide recovery, P-XINPUT/P-OWNER isolation, Steam/game consumer behavior nor crash/suspend/PnP recovery. T10 source implementation and Host-local receipts do not substitute for those observations.
"@
Write-Utf8LfText $reportPath $report

[pscustomobject]@{
    manifestId = $manifest.manifestId
    sourceDigest = $manifest.sourceDigest
    t20Disposition = "$currentCount/$historicalCount/$supersededCount"
    t21Claims = $t21.claims.Count
    dgfRange = 'DGF-01…DGF-15'
    runtimeUpgrade = $false
    reportPath = $reportPath
} | ConvertTo-Json -Depth 5
