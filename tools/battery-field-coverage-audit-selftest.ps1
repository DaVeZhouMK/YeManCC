$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$scratch = [IO.Directory]::CreateTempSubdirectory('ymcc-battery-field-').FullName
$scan = Join-Path $PSScriptRoot 'battery-field-coverage-audit.ps1'

function Write-SyntheticArchive([string]$name, [string[]]$lines, [string[]]$rotated = @(),
    [string]$extraMember = '', [string[]]$extraLines = @()) {
    $zip = [IO.Compression.ZipFile]::Open((Join-Path $scratch $name), [IO.Compression.ZipArchiveMode]::Create)
    try {
        $writer = [IO.StreamWriter]::new($zip.CreateEntry('battery-charge-host.log').Open())
        try { foreach ($line in $lines) { $writer.WriteLine($line) } } finally { $writer.Dispose() }
        if ($rotated.Count) {
            $writer = [IO.StreamWriter]::new($zip.CreateEntry('battery-charge-host.log.1').Open())
            try { foreach ($line in $rotated) { $writer.WriteLine($line) } } finally { $writer.Dispose() }
        }
        if ($extraMember) {
            $writer = [IO.StreamWriter]::new($zip.CreateEntry($extraMember).Open())
            try { foreach ($line in $extraLines) { $writer.WriteLine($line) } } finally { $writer.Dispose() }
        }
    } finally { $zip.Dispose() }
}

try {
    $a = '{"event":"identity","pid":1,"detail":{"manufacturer":"MICRO-STAR INTERNATIONAL CO., LTD.","boardProduct":"MS-1T52"}}'
    $b = '{"event":"identity","pid":1,"detail":{"manufacturer":"LENOVO","systemProduct":"83E1"}}'
    $start = '{"event":"host-start","pid":1,"detail":{}}'
    $packet = '{"event":"msi-packet","pid":1,"detail":{"method":"Get_Data","flag":1,"raw":128,"length":32}}'
    $unbound = '{"event":"request-result","pid":1,"detail":{"operation":"get","currentLimit":83}}'
    $good = '{"event":"request-result","pid":1,"detail":{"operation":"get","currentLimit":80}}'
    Write-SyntheticArchive 'YeManCC-logs-a.zip' @($a, $packet, $start, $unbound, $b, $good)
    Write-SyntheticArchive 'YeManCC-logs-b.zip' @($packet, $a, $packet)
    $out = Join-Path $scratch 'field.json'
    $pwsh = Join-Path $PSHOME 'pwsh.exe'
    & $pwsh -NoProfile -File $scan -LogRoot $scratch -Out $out
    if ($LASTEXITCODE -ne 0) { throw 'valid scan failed' }
    $data = Get-Content -LiteralPath $out -Raw | ConvertFrom-Json -AsHashtable
    $msi = @($data.groups | Where-Object event -ceq 'msi-packet')
    $gets = @($data.groups | Where-Object event -ceq 'request-result')
    if ($data.unlinkedUniqueLines -ne 1 -or $msi.Count -ne 1 -or $msi[0].count -ne 1 -or $msi[0].identity.boardProduct -cne 'MS-1T52' -or $msi[0].fact.raw -ne 128 -or $gets.Count -ne 1 -or $gets[0].identity.systemProduct -cne '83E1') {
        throw 'copied records, reused PID or raw response was misclassified'
    }
    # Distinct raw lines in two archives collapse to one fact. A third copy must not inflate count or provenance.
    $sharedId = $b.Replace('"pid":1','"pid":90')
    $sharedReply = $good.Replace('"pid":1','"pid":90').Replace('"currentLimit":80','"currentLimit":68')
    $otherReply = $sharedReply.Replace('"pid":90','"pid":90,"time":"2026-01-01T00:00:02Z"')
    $firstArchive = 'YeManCC-logs-01-contributor.zip'
    $secondArchive = 'YeManCC-logs-02-contributor.zip'
    $copyArchive = 'YeManCC-logs-03-contributor-copy.zip'
    Write-SyntheticArchive $firstArchive @($sharedId,$sharedReply)
    Write-SyntheticArchive $secondArchive @($sharedId,$otherReply)
    Write-SyntheticArchive $copyArchive @($sharedId,$otherReply)
    & $pwsh -NoProfile -File $scan -LogRoot $scratch -Out $out
    if ($LASTEXITCODE -ne 0) { throw 'contributor scan failed' }
    $data = Get-Content -LiteralPath $out -Raw -Encoding utf8 | ConvertFrom-Json -AsHashtable
    $fact = @($data.groups | Where-Object { $_.event -ceq 'request-result' -and $_.sample.pid -eq 90 })
    if ($fact.Count -ne 1 -or $fact[0].count -ne 2 -or $fact[0].sample.archive -cne $firstArchive -or
        $data.pinnedSampleArchives.Contains($secondArchive) -or !$data.Contains('pinnedContributingArchives') -or
        (@($fact[0].contributors).Count -ne 2) -or $data.pinnedContributingArchives.Contains($copyArchive)) {
        throw 'distinct counted lines are not fully attributable to pinned archives'
    }
    for ($i=0; $i -lt 2; $i++) {
        $name = @($firstArchive, $secondArchive)[$i]
        $line = @($sharedReply, $otherReply)[$i]
        $hash = (Get-FileHash -LiteralPath (Join-Path $scratch $name)).Hash.ToLowerInvariant()
        $lineHash = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes($line))).ToLowerInvariant()
        $loc = $fact[0].contributors[$i]
        if ($data.pinnedContributingArchives[$name] -cne $hash -or $loc.archive -cne $name -or
            $loc.member -cne 'battery-charge-host.log' -or $loc.line -ne 2 -or $loc.eventLineSha256 -cne $lineHash -or $loc.identityEvidence.line -ne 1) {
            throw 'count witness lost its archive, event or linked identity locator'
        }
    }
    foreach ($name in @($firstArchive, $secondArchive, $copyArchive)) { [IO.File]::Delete((Join-Path $scratch $name)) }
    $legacyStop = '{"event":"host-stop","pid":1,"detail":{}}'
    $inProcessStop = '{"event":"in-process-stop","pid":2,"detail":{"cleanupError":"not restored"}}'
    $inProcessId = '{"event":"in-process-start","pid":2,"detail":{"manufacturer":"ONE-NETBOOK","boardProduct":"ONEXPLAYER X2Mini PRO","bios":"0.22"}}'
    $freshId = $inProcessId.Replace('0.22','0.23')
    $closedResult = '{"event":"request-result","pid":2,"detail":{"operation":"set","ok":true,"supported":true,"batteryPresent":true,"kind":"firmware-percent","verification":"readback","currentLimit":75,"requestedLimit":75,"model":"ONEXPLAYER X2Mini PRO"}}'
    $freshResult = $closedResult.Replace('"set"','"get"').Replace('75','80')
    $legacyClosed = $good.Replace('80','72')
    $legacyFresh = $good.Replace('80','85')
    $otherPid = $packet.Replace('"pid":1','"pid":3').Replace('128','129')
    $otherId = $a.Replace('"pid":1','"pid":3')
    $cycle = @($a, $inProcessId, $otherId, $legacyStop, $legacyClosed, $inProcessStop,
        $closedResult, $b, $legacyClosed, $otherPid, $start, $b, $legacyFresh, $freshId, $freshResult)
    Write-SyntheticArchive 'YeManCC-logs-c-stop.zip' $cycle
    Write-SyntheticArchive 'YeManCC-logs-d-stop-copy.zip' $cycle
    & $pwsh -NoProfile -File $scan -LogRoot $scratch -Out $out
    if ($LASTEXITCODE -ne 0) { throw 'stop-boundary scan failed' }
    $data = Get-Content -LiteralPath $out -Raw | ConvertFrom-Json -AsHashtable
    $gets = @($data.groups | Where-Object event -ceq 'request-result')
    if (@($gets | Where-Object { $_.fact.currentLimit -in 72,75 }).Count) { throw 'a post-stop result was attributed to the closed session' }
    $next = @($gets | Where-Object { $_.sample.pid -eq 2 })
    $legacyNext = @($gets | Where-Object { $_.fact.currentLimit -eq 85 })
    $unaffected = @($data.groups | Where-Object { $_.event -ceq 'msi-packet' -and $_.sample.pid -eq 3 })
    if ($data.replyLinkExclusions.afterStopUniqueLines -ne 3 -or $data.unlinkedUniqueLines -ne 1 -or
        $next.Count -ne 1 -or $next[0].identity.bios -cne '0.23' -or $next[0].count -ne 1 -or
        $legacyNext.Count -ne 1 -or $legacyNext[0].identity.systemProduct -cne '83E1' -or $legacyNext[0].count -ne 1 -or
        $unaffected.Count -ne 1 -or $unaffected[0].fact.raw -ne 129 -or $unaffected[0].count -ne 1) {
        throw 'stopped PID, new start, unaffected PID or copied post-stop diagnostics were misclassified'
    }
    $copyIdentity = $inProcessId.Replace('"pid":2','"pid":4')
    $copyReply = $closedResult.Replace('"pid":2','"pid":4').Replace('75','70')
    $uniqueReply = $copyReply.Replace('70','85')
    Write-SyntheticArchive 'YeManCC-logs-e-conflict.zip' @($copyIdentity,$copyReply,$uniqueReply)
    Write-SyntheticArchive 'YeManCC-logs-f-conflict.zip' @($copyIdentity.Replace('0.22','0.23'),$copyReply)
    Write-SyntheticArchive 'YeManCC-logs-g-conflict-copy.zip' @($copyIdentity,$copyReply,$uniqueReply)
    & $pwsh -NoProfile -File $scan -LogRoot $scratch -Out $out
    if ($LASTEXITCODE -ne 0) { throw 'conflicting copy scan failed' }
    $data = Get-Content -LiteralPath $out -Raw | ConvertFrom-Json -AsHashtable
    $linkedCopies = @($data.groups | Where-Object { $_.sample.pid -eq 4 -and $_.event -ceq 'request-result' })
    if ($data.replyLinkExclusions.copiedLineIdentityConflictUniqueLines -ne 1 -or
        $linkedCopies.Count -ne 1 -or $linkedCopies[0].fact.currentLimit -ne 85 -or
        $linkedCopies[0].identity.bios -cne '0.22' -or $linkedCopies[0].count -ne 1) {
        throw 'same reply was assigned to competing identities or lost unrelated evidence'
    }
    $rotatedIdentity = $inProcessId.Replace('"pid":2','"pid":5')
    $rotatedReply = $freshResult.Replace('"pid":2','"pid":5').Replace('80','55')
    $orphanActiveReply = $rotatedReply.Replace('55','60')
    Write-SyntheticArchive 'YeManCC-logs-h-rotation.zip' @($orphanActiveReply) @($rotatedIdentity,$rotatedReply)
    Write-SyntheticArchive 'YeManCC-logs-i-rotation-copy.zip' @($rotatedIdentity,$rotatedReply)
    & $pwsh -NoProfile -File $scan -LogRoot $scratch -Out $out
    if ($LASTEXITCODE -ne 0) { throw 'rotation scan failed' }
    $data = Get-Content -LiteralPath $out -Raw | ConvertFrom-Json -AsHashtable
    $acrossRotation = @($data.groups | Where-Object { $_.sample.pid -eq 5 -and $_.event -ceq 'request-result' })
    if ($data.hostMembers -ne 10 -or $data.unlinkedUniqueLines -ne 2 -or
        $acrossRotation.Count -ne 1 -or $acrossRotation[0].count -ne 1 -or
        $acrossRotation[0].fact.currentLimit -ne 55 -or $acrossRotation[0].sample.member -cne 'battery-charge-host.log.1' -or
        $acrossRotation[0].sample.identityEvidence.line -ne 1) {
        throw 'rotated member missing, duplicated, or allowed a cross-member PID join'
    }
    Write-SyntheticArchive 'YeManCC-logs-invalid.zip' @('{"event":"request-result",')
    & $pwsh -NoProfile -File $scan -LogRoot $scratch -Out $out
    $invalid = $LASTEXITCODE
    $data = Get-Content -LiteralPath $out -Raw | ConvertFrom-Json -AsHashtable
    if ($invalid -ne 1 -or $data.errors.Count -ne 1 -or $data.errors[0].type -cne 'invalid-event-json') { throw 'malformed evidence did not fail the scan' }
    $activeIdentity = $rotatedIdentity.Replace('"pid":5','"pid":6')
    $activeReply = $rotatedReply.Replace('"pid":5','"pid":6').Replace('55','67')
    $otherIdentity = $rotatedIdentity.Replace('"pid":5','"pid":7')
    $otherReply = $rotatedReply.Replace('"pid":5','"pid":7').Replace('55','65')
    Write-SyntheticArchive 'YeManCC-logs-dup-active.zip' @($activeIdentity,$activeReply) @() 'battery-charge-host.log' @($otherIdentity,$otherReply)
    Write-SyntheticArchive 'YeManCC-logs-dup-rotated.zip' @($activeIdentity,$activeReply) @($otherIdentity,$otherReply) 'battery-charge-host.log.1' @($otherIdentity,$otherReply)
    Write-SyntheticArchive 'YeManCC-logs-distinct-paths.zip' @($otherIdentity,$otherReply) @() 'nested/battery-charge-host.log' @($activeIdentity,$activeReply)
    & $pwsh -NoProfile -File $scan -LogRoot $scratch -Out $out
    $invalid = $LASTEXITCODE
    $data = Get-Content -LiteralPath $out -Raw | ConvertFrom-Json -AsHashtable
    $duplicates = @($data.errors | Where-Object type -ceq 'duplicate-host-member')
    $extraGets = @($data.groups | Where-Object { $_.event -ceq 'request-result' -and $_.sample.pid -in 6,7 })
    if ($invalid -ne 1 -or $data.errors.Count -ne 3 -or $duplicates.Count -ne 2 -or
        @($duplicates | Where-Object { $_.archive -ceq 'YeManCC-logs-dup-active.zip' -and $_.member -ceq 'battery-charge-host.log' }).Count -ne 1 -or
        @($duplicates | Where-Object { $_.archive -ceq 'YeManCC-logs-dup-rotated.zip' -and $_.member -ceq 'battery-charge-host.log.1' }).Count -ne 1 -or
        $data.pinnedSampleArchives.Contains('YeManCC-logs-dup-active.zip') -or $data.pinnedSampleArchives.Contains('YeManCC-logs-dup-rotated.zip') -or
        $extraGets.Count -ne 2 -or @($extraGets | Where-Object { $_.count -ne 1 -or $_.sample.archive -cne 'YeManCC-logs-distinct-paths.zip' }).Count -ne 0 -or
        @($extraGets | Where-Object { $_.sample.pid -eq 6 -and $_.sample.member -ceq 'nested/battery-charge-host.log' }).Count -ne 1) {
        throw 'member path collision admitted evidence, or a distinct complete path was lost'
    }
    $typedId = '{"event":"identity","pid":8,"detail":{"manufacturer":"ONE-NETBOOK","boardProduct":"ONEXPLAYER X2Mini PRO","systemProduct":null,"processor":null,"bios":"0.22","boardProductRaw":null}}'
    $typedReply = $good.Replace('"pid":1','"pid":8')
    $malformedIds = @(
        $typedId.Replace('"ONE-NETBOOK"','["ONE-NETBOOK"]'),
        $typedId.Replace('"ONEXPLAYER X2Mini PRO"','["ONEXPLAYER X2Mini PRO"]'),
        $typedId.Replace('"systemProduct":null','"systemProduct":{"name":"ONEXPLAYER X2Mini PRO"}'),
        $typedId.Replace('"processor":null','"processor":["HX 370"]'),
        $typedId.Replace('"0.22"','0.22'),
        $typedId.Replace('"boardProductRaw":null','"boardProductRaw":["ONEXPLAYER X2Mini PRO"]'),
        '{"event":"in-process-start","pid":8,"detail":[{"manufacturer":"ONE-NETBOOK","boardProduct":"ONEXPLAYER X2Mini PRO"}]}'
    )
    $typedLines = @($typedId, $typedReply)
    for ($i = 0; $i -lt $malformedIds.Count; $i++) {
        $typedLines += @($malformedIds[$i], $typedReply.Replace('80', [string](51 + $i)))
    }
    $typedLines += @($typedId, $typedReply.Replace('80','85'))
    Write-SyntheticArchive 'YeManCC-logs-identity-types.zip' $typedLines
    Write-SyntheticArchive 'YeManCC-logs-identity-types-copy.zip' $typedLines
    & $pwsh -NoProfile -File $scan -LogRoot $scratch -Out $out
    $invalid = $LASTEXITCODE
    $data = Get-Content -LiteralPath $out -Raw | ConvertFrom-Json -AsHashtable
    $typedGets = @($data.groups | Where-Object { $_.event -ceq 'request-result' -and $_.sample.pid -eq 8 })
    if ($invalid -ne 1 -or $data.errors.Count -ne 3 -or
        $data.replyLinkExclusions.invalidIdentityTextUniqueLines -ne $malformedIds.Count -or $data.unlinkedUniqueLines -ne 9 -or
        $typedGets.Count -ne 2 -or @($typedGets | Where-Object { $_.fact.currentLimit -in 80,85 -and $_.count -eq 1 }).Count -ne 2) {
        throw 'malformed identity text was coerced, inherited a prior identity or suppressed a valid control'
    }
    $ruleIdLine = '{"event":"identity","pid":91,"detail":{"manufacturer":"ONE-NETBOOK","boardProduct":"ONEXPLAYER X2Mini PRO","systemProduct":"ONEXPLAYER X2Mini PRO"}}'
    $mini = 'battery.rule.oxp-x2-mini-pro-direct-ec.OneXPlayerX2MiniPro'
    $historical = 'battery.rule.historical-route.OneXPlayerX2MiniPro'
    $sha1 = 'a' * 64
    $sha2 = 'b' * 64
    $claimFields = @(
        [ordered]@{capabilityRuleId=$mini; capabilityCatalogSha256=$sha1},
        [ordered]@{capabilityRuleId=$historical; capabilityCatalogSha256=$sha1},
        [ordered]@{capabilityRuleId=$mini; capabilityCatalogSha256=$sha2},
        [ordered]@{},
        [ordered]@{capabilityRuleId=$mini},
        [ordered]@{capabilityRuleId=$null; capabilityCatalogSha256=$sha1},
        [ordered]@{capabilityRuleId=@($mini); capabilityCatalogSha256=$sha1},
        [ordered]@{capabilityRuleId=$mini; capabilityCatalogSha256=42},
        [ordered]@{capabilityRuleId=$mini; capabilityCatalogSha256=('g' * 64)},
        [ordered]@{capabilityRuleId=$mini; capabilityCatalogSha256=($sha1 + "`n")}
    )
    $ruleReplies = for ($i = 0; $i -lt $claimFields.Count; $i++) {
        $detail = [ordered]@{operation='get'; ok=$true; supported=$true; batteryPresent=$true; kind='firmware-percent'; verification='readback'; currentLimit=80}
        foreach ($key in $claimFields[$i].Keys) { $detail[$key] = $claimFields[$i][$key] }
        [ordered]@{event='request-result'; pid=91; time="2026-01-01T00:00:0${i}Z"; detail=$detail} | ConvertTo-Json -Depth 4 -Compress
    }
    $unlinkedRule = $ruleReplies[0].Replace('"pid":91','"pid":92')
    Write-SyntheticArchive 'YeManCC-logs-rule-claims.zip' (@($ruleIdLine) + $ruleReplies + @($ruleReplies[0], $unlinkedRule))
    & $pwsh -NoProfile -File $scan -LogRoot $scratch -Out $out
    $invalid = $LASTEXITCODE
    $data = Get-Content -LiteralPath $out -Raw | ConvertFrom-Json -AsHashtable
    $ruleFacts = @($data.groups | Where-Object { $_.event -ceq 'request-result' -and $_.sample.pid -eq 91 })
    $logged = @($ruleFacts | Where-Object { $_.fact.capabilityAttribution.status -ceq 'logged-pair' })
    $unresolved = @($ruleFacts | Where-Object { $_.fact.capabilityAttribution.status -ceq 'unresolved' })
    $legacy = @($ruleFacts | Where-Object { !$_.fact.Contains('capabilityAttribution') })
    if ($invalid -ne 1 -or $data.errors.Count -ne 3 -or $data.unlinkedUniqueLines -ne 10 -or
        @($data.groups | Where-Object { $_.sample.pid -eq 92 }).Count -ne 0 -or
        $ruleFacts.Count -ne 5 -or $logged.Count -ne 3 -or
        @($logged | Where-Object { $_.count -ne 1 -or $_.contributors.Count -ne 1 }).Count -ne 0 -or
        @($logged | Where-Object { $_.fact.capabilityAttribution.capabilityRuleId -ceq $mini -and $_.fact.capabilityAttribution.capabilityCatalogSha256 -ceq $sha1 }).Count -ne 1 -or
        @($logged | Where-Object { $_.fact.capabilityAttribution.capabilityRuleId -ceq $historical -and $_.fact.capabilityAttribution.capabilityCatalogSha256 -ceq $sha1 }).Count -ne 1 -or
        @($logged | Where-Object { $_.fact.capabilityAttribution.capabilityRuleId -ceq $mini -and $_.fact.capabilityAttribution.capabilityCatalogSha256 -ceq $sha2 }).Count -ne 1 -or
        $legacy.Count -ne 1 -or $legacy[0].count -ne 1 -or $unresolved.Count -ne 1 -or $unresolved[0].count -ne 6 -or
        $unresolved[0].fact.capabilityAttribution.Count -ne 1) {
        throw 'logged rule/catalog claims merged, dropped, substituted, or promoted without a complete linked pair'
    }
    Write-Output 'Battery field audit: counted-line provenance, duplicates, PID reset, raw packet, stop boundary, rotation, malformed evidence and logged attribution cases passed.'
} finally {
    foreach ($file in @('YeManCC-logs-a.zip', 'YeManCC-logs-b.zip', 'YeManCC-logs-c-stop.zip', 'YeManCC-logs-d-stop-copy.zip',
        'YeManCC-logs-e-conflict.zip', 'YeManCC-logs-f-conflict.zip', 'YeManCC-logs-g-conflict-copy.zip',
        'YeManCC-logs-h-rotation.zip', 'YeManCC-logs-i-rotation-copy.zip', 'YeManCC-logs-invalid.zip',
        'YeManCC-logs-dup-active.zip', 'YeManCC-logs-dup-rotated.zip', 'YeManCC-logs-distinct-paths.zip',
        'YeManCC-logs-identity-types.zip', 'YeManCC-logs-identity-types-copy.zip', 'YeManCC-logs-rule-claims.zip', 'field.json',
        'YeManCC-logs-01-contributor.zip', 'YeManCC-logs-02-contributor.zip', 'YeManCC-logs-03-contributor-copy.zip')) {
        [IO.File]::Delete((Join-Path $scratch $file))
    }
    [IO.Directory]::Delete($scratch)
}
