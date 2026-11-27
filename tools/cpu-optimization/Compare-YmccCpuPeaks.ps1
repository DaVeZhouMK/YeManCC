[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidateCount(2,8)][string[]]$PeakReport,
    [Parameter(Mandatory)][string]$OutputPath
)
$ErrorActionPreference = 'Stop'

function Read-Json([string]$path) { Get-Content -LiteralPath $path -Raw | ConvertFrom-Json -AsHashtable -DateKind String }

$runs = @(foreach ($file in $PeakReport) {
    $file = (Resolve-Path -LiteralPath $file).ProviderPath
    $data = Read-Json $file
    $etl = (Resolve-Path -LiteralPath $data.etl).ProviderPath
    $folder = Split-Path -Parent $etl
    $identity = Read-Json (Join-Path $folder 'IDENTITY.json')
    $window = Read-Json (Join-Path $folder 'PEAK-SWEEP-WINDOW.json')
    $stop = Read-Json (Join-Path $folder 'WPR-STOP.json')
    $report = $data.report
    if ($data.eventsLost -cne 0 -or $report.status -cnotin @('COMPLETE_SCHEDULER_INTERVAL','PARTIAL_INTERVAL_OBSERVED_PEAKS_ONLY') -or
        $data.sha256 -notmatch '^[0-9a-f]{64}$' -or $data.sha256 -cne $stop.sha256 -or (Get-FileHash -LiteralPath $etl).Hash -ine $data.sha256 -or
        !$stop.traceSaved -or !$identity.peakSweep -or !$window.heldAlive -or $stop.onlyOwnInstanceStopped -cne $identity.instanceName -or
        $identity.diskImageSha256 -notmatch '^[0-9a-f]{64}$' -or $identity.diskImageSha256 -cne $window.diskImageSha256 -or
        $identity.pid -ne $window.pid -or $identity.pid -ne $report.targetPid -or $identity.birthFileTime -cne $window.birthFileTime -or
        $report.beginUtc -cne $window.beginUtc -or $report.endUtc -cne $window.endUtc) {
        throw "Trace, identity or window mismatch: $file"
    }
    [ordered]@{file=$file; sha256=$data.sha256; imageHash=$identity.diskImageSha256; profileHash=$identity.profileSha256;
        pid=$identity.pid; birth=$identity.birthFileTime; begin=$window.beginUtc; end=$window.endUtc; report=$report}
})
$base = $runs[0]
if (@($runs | Group-Object {$_.sha256}).Count -ne $runs.Count -or
    @($runs | Where-Object {$_.imageHash -cne $base.imageHash -or $_.profileHash -cne $base.profileHash -or $_.pid -ne $base.pid -or $_.birth -cne $base.birth}).Count) {
    throw 'Replay requires distinct traces and a common installed instance and recording profile.'
}
$sorted = @($runs | Sort-Object { [datetimeoffset]$_.begin })
for ($i = 1; $i -lt $sorted.Count; ++$i) {
    if ([datetimeoffset]$sorted[$i].begin -lt [datetimeoffset]$sorted[$i-1].end) { throw 'Replay windows overlap.' }
}

$byRun = @(foreach ($r in $sorted) {
    $index = @{}
    $ranked = @(foreach ($peak in $r.report.peaks) {
        $scope = "$($peak.scope)/$($peak.windowMs)"
        $rank = 1 + [int]$index[$scope]
        $index[$scope] = $rank
        $top = $peak.threads[0]
        [pscustomobject]@{scope=$peak.scope; windowMs=$peak.windowMs; rank=$rank; scheduledMs=$peak.scheduledMs;
            startUtc=$peak.startUtc; pid=$top.pid; tid=$top.tid; process=$top.process; role=$top.role; dominantThreadMs=$top.scheduledMs;
            sampledFrameWeight=($peak.samples | Where-Object {$_.pid -eq $top.pid -and $_.tid -eq $top.tid -and $_.threadInstance -ceq $top.threadInstance -and $_.frames.Count} | Measure-Object -Property weight -Sum).Sum}
    })
    [ordered]@{capture=$r.file; sha256=$r.sha256; beginUtc=$r.begin; endUtc=$r.end; quality=$r.report.status;
        unobservedCpuMs=$r.report.unobservedCpuMs; unboundCpuMs=$r.report.unboundCpuMs;
        processRoles=@($r.report.processes | Group-Object role | Sort-Object Name | ForEach-Object { [ordered]@{role=$_.Name; instances=$_.Count} }); peaks=$ranked}
})
$signals = @(foreach ($group in ($byRun.peaks | Group-Object scope,windowMs,process,role,pid,tid)) {
    $sample = $group.Group[0]
    $hits = @(foreach ($row in $byRun) {
        $match = $row.peaks | Where-Object {$_.scope -ceq $sample.scope -and $_.windowMs -eq $sample.windowMs -and $_.process -ceq $sample.process -and $_.role -ceq $sample.role -and $_.pid -eq $sample.pid -and $_.tid -eq $sample.tid} | Select-Object -First 1
        if ($match) { [ordered]@{trace=$row.sha256; rank=$match.rank; scheduledMs=$match.scheduledMs; dominantThreadMs=$match.dominantThreadMs; sampledFrameWeight=$match.sampledFrameWeight} }
    })
    if ($hits.Count -ge 2) {
        [ordered]@{scope=$sample.scope; windowMs=$sample.windowMs; process=$sample.process; role=$sample.role;
            pid=$sample.pid; tid=$sample.tid; support=$hits.Count; hits=$hits}
    }
})
$result = [ordered]@{
    status=if($signals.Count){'REPEATED_OWNER_SIGNAL'}else{'NO_REPEATED_OWNER_SIGNAL'}
    repairGate='CLOSED_UNTIL_CAUSAL_BRANCH_AND_MATCHED_CONTROL'
    next=if($signals.Count){'Bind the repeated owner to its operation and trigger; isolate a matched control before changing behavior.'}else{'Name the missing evidence and improve collection or stop this branch.'}
    limitation='Repeated PID/TID and role is an observation, not proof of a continuous thread lifetime, call path or cause. A missing top-three peak does not prove that work was absent.'
    imageSha256=$base.imageHash; pid=$base.pid; birthFileTime=$base.birth; captures=$byRun; repeatedOwners=$signals
}
$result | ConvertTo-Json -Depth 10 | Out-File -LiteralPath $OutputPath -Encoding utf8 -NoClobber
$result | Select-Object @{n='status';e={$_.status}},@{n='repairGate';e={$_.repairGate}},@{n='next';e={$_.next}} | ConvertTo-Json -Compress
