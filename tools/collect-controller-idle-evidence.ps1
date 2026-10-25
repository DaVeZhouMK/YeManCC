[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][ValidateSet('off','on','off-repeat','smoke')][string]$Phase,
    [ValidateRange(5,900)][int]$DurationSeconds=240,
    [string]$OutputRoot=(Join-Path ([Environment]::GetFolderPath('Desktop')) 'YeManCC-ControllerIdle'),
    [string]$ProbePath=(Join-Path $PSScriptRoot 'YeManControllerIdleProbe.exe'),
    [string]$HidHideCliPath
)
# READ ONLY: never change power plans, screen state, whitelist, cloak, devices,
# controller output, drivers or services. Label each phase explicitly instead of
# trying to toggle an installed application or guessing its state from a filename.
$ErrorActionPreference='Stop'
$ProbePath=[IO.Path]::GetFullPath($ProbePath)
if(!(Test-Path -LiteralPath $ProbePath -PathType Leaf)){throw "Read-only observer not found: $ProbePath. Supply -ProbePath or use the prepared collector folder."}
$OutputRoot=[IO.Path]::GetFullPath($OutputRoot)
New-Item -ItemType Directory -Path $OutputRoot -Force|Out-Null
$run=Join-Path $OutputRoot ((Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+$Phase+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Path $run -Force|Out-Null

$script:rows=Join-Path $run 'system-requests-and-hidhide.jsonl'
$script:queryResults=New-Object Collections.Generic.List[object]
function Save-Row($row){$row|Add-Member -NotePropertyName atLocal -NotePropertyValue (Get-Date).ToString('o') -Force; $row|ConvertTo-Json -Depth 9 -Compress|Add-Content -LiteralPath $script:rows -Encoding UTF8}
function Read-Command([string]$File,[string]$Arguments,[string]$Label){
    # These command strings are fixed read-only queries, never assembled from log data.
    $info=New-Object Diagnostics.ProcessStartInfo
    $info.FileName=$File;$info.Arguments=$Arguments;$info.UseShellExecute=$false;$info.CreateNoWindow=$true
    $info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
    $process=New-Object Diagnostics.Process;$process.StartInfo=$info
    try{
        if(!$process.Start()){throw 'process did not start'}
        $stdout=New-Object IO.MemoryStream;$stderr=New-Object IO.MemoryStream
        $outTask=$process.StandardOutput.BaseStream.CopyToAsync($stdout)
        $errTask=$process.StandardError.BaseStream.CopyToAsync($stderr)
        $timedOut=!$process.WaitForExit(5000)
        if($timedOut){try{$process.Kill()}catch{};[void]$process.WaitForExit(1000)}
        # Drain only this owned read-only utility's output. Timed out means unknown.
        $outDone=$outTask.Wait(1000);$errDone=$errTask.Wait(1000)
        $o=$stdout.ToArray();$e=$stderr.ToArray()
        $encoding=[Text.Encoding]::GetEncoding([Globalization.CultureInfo]::CurrentCulture.TextInfo.OEMCodePage)
        if($o.Length -ge 2 -and (($o[0] -eq 255 -and $o[1] -eq 254) -or $o[1] -eq 0)){$encoding=[Text.Encoding]::Unicode}
        $queryExit=$(if($process.HasExited){$process.ExitCode}else{$null})
        $queryOk=!$timedOut -and $outDone -and $errDone -and $queryExit -eq 0
        $script:queryResults.Add([pscustomobject]@{label=$Label;command=$Arguments;ok=$queryOk;exitCode=$queryExit})
        if(!$queryOk){Write-Warning "$Label ($Arguments) unavailable or failed; result remains UNKNOWN (exit=$queryExit). /requests may require administrator PowerShell."}
        Save-Row ([pscustomobject]@{event=$Label;command=$Arguments;executable=$File;timedOut=$timedOut;exitCode=$(if($process.HasExited){$process.ExitCode}else{$null});outputComplete=$outDone -and $errDone;encoding=$encoding.WebName;text=$encoding.GetString($o);stderr=$encoding.GetString($e);stdoutBase64=[Convert]::ToBase64String($o);stderrBase64=[Convert]::ToBase64String($e)})
    }catch{$script:queryResults.Add([pscustomobject]@{label=$Label;command=$Arguments;ok=$false;exitCode=$null});Save-Row ([pscustomobject]@{event=$Label;command=$Arguments;ok=$false;error=$_.Exception.Message})}
    finally{if($process){$process.Dispose()}}
}
if(!$HidHideCliPath){
    $candidates=@(
      (Join-Path ${env:ProgramFiles} 'Nefarius Software Solutions\HidHide\x64\HidHideCLI.exe'),
      (Join-Path ${env:ProgramFiles} 'Nefarius Software Solutions\HidHide\HidHideCLI.exe')
    )
    $HidHideCliPath=$candidates|Where-Object {Test-Path -LiteralPath $_ -PathType Leaf}|Select-Object -First 1
}
Save-Row ([pscustomobject]@{event='collector-start';phase=$Phase;phaseIsOperatorLabelNotVerifiedAppState=$true;durationSeconds=$DurationSeconds;readOnly=$true;probePath=$ProbePath;probeSha256=(Get-FileHash -LiteralPath $ProbePath -Algorithm SHA256).Hash;collectorPath=$PSCommandPath;collectorSha256=(Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash;hidhideObserverExemption='not assumed; compare exact probe path and app-list, interpret inverse-state before drawing external suppression conclusions'})
$powercfg=Join-Path $env:SystemRoot 'System32\powercfg.exe'
Read-Command $powercfg '/requests' 'powercfg-requests-before'
Read-Command $powercfg '/getactivescheme' 'powercfg-active-scheme'
Read-Command $powercfg '/query SCHEME_CURRENT SUB_VIDEO' 'powercfg-video-policy'
if($HidHideCliPath){foreach($arg in @('--app-list','--cloak-state','--inv-state','--dev-list')){Read-Command $HidHideCliPath $arg 'hidhide-read-only-before'}}
else{Save-Row ([pscustomobject]@{event='hidhide-query-unavailable';exemptionVerified=$false;externalSuppressionConclusion='unknown; observer may still collect useful idle/raw delivery evidence'})}
$probe=New-Object Diagnostics.Process
$info=New-Object Diagnostics.ProcessStartInfo
$info.FileName=$ProbePath;$info.UseShellExecute=$false;$info.CreateNoWindow=$true
$info.Arguments='"'+(Join-Path $run 'external-observer.jsonl')+'" '+$Phase+' '+$DurationSeconds
$probe.StartInfo=$info
try{
    if(!$probe.Start()){throw 'Observer did not start'}
    Write-Output "Started read-only phase $Phase ($DurationSeconds seconds): $run"
    Write-Output 'Leave the device untouched. This collector does not toggle the virtual pad or power policy.'
    $deadline=[DateTime]::UtcNow.AddSeconds($DurationSeconds+15)
    $nextRequests=[DateTime]::UtcNow.AddSeconds(30)
    while(!$probe.WaitForExit(1000)){
        if([DateTime]::UtcNow -ge $deadline){try{$probe.Kill()}catch{};throw 'Observer exceeded its own bounded collection window'}
        if([DateTime]::UtcNow -ge $nextRequests){Read-Command $powercfg '/requests' 'powercfg-requests-during';$nextRequests=[DateTime]::UtcNow.AddSeconds(30)}
    }
    $exit=$probe.ExitCode
    Save-Row ([pscustomobject]@{event='observer-exited';exitCode=$exit;probePid=$probe.Id})
    if($exit -ne 0){throw "Read-only observer failed: $exit"}
}finally{try{if(!$probe.HasExited){$probe.Kill();[void]$probe.WaitForExit(1000)}}catch{};$probe.Dispose()}
Read-Command $powercfg '/requests' 'powercfg-requests-after'
if($HidHideCliPath){foreach($arg in @('--app-list','--cloak-state','--inv-state','--dev-list')){Read-Command $HidHideCliPath $arg 'hidhide-read-only-after'}}
Save-Row ([pscustomobject]@{event='collector-stop';phase=$Phase;readOnly=$true;completed=$true;output=$run})
[pscustomobject]@{completed=$true;phase=$Phase;phaseIsOperatorLabel=$true;readOnly=$true;probePath=$ProbePath;probeSha256=(Get-FileHash -LiteralPath $ProbePath -Algorithm SHA256).Hash;queryResults=@($script:queryResults.ToArray());powerRequestsAvailable=@($script:queryResults|Where-Object {$_.command -eq '/requests' -and $_.ok}).Count -gt 0;hidhideQueriesAvailable=@($script:queryResults|Where-Object {$_.command -eq '--app-list' -and $_.ok}).Count -gt 0;externalSteamSuppressionProven=$false;autoDimFixProven=$false;output=$run}|ConvertTo-Json -Depth 7|Set-Content -LiteralPath "$run\collector-result.json" -Encoding UTF8
Write-Output "COLLECTION_COMPLETE $run"
