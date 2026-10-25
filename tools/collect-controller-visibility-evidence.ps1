[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][ValidateSet('off','on','off-repeat','smoke','transition')][string]$Phase,
    [ValidateRange(5,900)][int]$DurationSeconds=60,
    [string]$OutputRoot,
    [string]$ProbePath,
    [string]$HidHideCliPath
)
# READ ONLY: never change power plans, screen state, whitelist, cloak, devices,
# controller output, drivers or services. Label each phase explicitly instead of
# trying to toggle an installed application or guessing its state from a filename.
$ErrorActionPreference='Stop'
if([string]::IsNullOrWhiteSpace($ProbePath)){
    $scriptRoot=$PSScriptRoot
    if([string]::IsNullOrWhiteSpace($scriptRoot) -and $PSCommandPath){$scriptRoot=Split-Path -Path $PSCommandPath -Parent}
    if([string]::IsNullOrWhiteSpace($scriptRoot)){throw 'Run the saved collector file with -File, or supply -ProbePath.'}
    $ProbePath=Join-Path $scriptRoot 'YeManControllerVisibilityProbe.exe'
}
if([string]::IsNullOrWhiteSpace($OutputRoot)){
    $desktop=[Environment]::GetFolderPath('Desktop')
    if([string]::IsNullOrWhiteSpace($desktop)){throw 'Desktop path unavailable; supply -OutputRoot.'}
    $OutputRoot=Join-Path $desktop 'YeManCC-ControllerVisibility'
}
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
# CTW's DetectActive is process/service/task based, NOT foreground based.
# This collector records both axes without stopping, starting or blocking OEM.
if(!('YMCC.ControllerVisibilityForeground' -as [type])){
    Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
namespace YMCC { public static class ControllerVisibilityForeground {
 [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h,out uint pid);
}}
"@
}
$script:lastOemContext=''
function Read-OemContext {
    $fgPid=0;[void][YMCC.ControllerVisibilityForeground]::GetWindowThreadProcessId([YMCC.ControllerVisibilityForeground]::GetForegroundWindow(),[ref]$fgPid)
    $fgName=$null;try{if($fgPid){$fgName=[Diagnostics.Process]::GetProcessById($fgPid).ProcessName}}catch{}
    try{
        $processes=@(Get-Process -ErrorAction Stop|Where-Object {$_.ProcessName -match '^(MSI_Center_M_|MSI Center M$|MCMOSDInfo$|MSI Center OSD Info$|mongMode$|Gamebar_Widget$)'}|Sort-Object Id|ForEach-Object {
            [pscustomobject]@{name=$_.ProcessName;pid=$_.Id;foreground=$_.Id -eq $fgPid}
        })
        $context=[pscustomobject]@{event='oem-process-and-foreground';foregroundPid=$fgPid;foregroundProcess=$fgName;foregroundKnown=$null -ne $fgName;processQueryOk=$true;processes=$processes;scope='Correlation only. Presence, task enablement, foreground and mode ownership are distinct; culprit remains unproven.'}
    }catch{$context=[pscustomobject]@{event='oem-process-and-foreground';processQueryOk=$false;error=$_.Exception.Message;culprit='unknown'}}
    $fingerprint=$context|ConvertTo-Json -Depth 6 -Compress
    if($fingerprint -ne $script:lastOemContext){Save-Row $context;$script:lastOemContext=$fingerprint}
}
function Read-OemServiceTasks {
    try{$services=@(Get-Service -ErrorAction Stop|Where-Object {$_.DisplayName -eq 'MSI Foundation Service' -or $_.Name -eq 'MSI Foundation Service'}|ForEach-Object {[pscustomobject]@{name=$_.Name;displayName=$_.DisplayName;status=[string]$_.Status}});Save-Row ([pscustomobject]@{event='oem-service-query';ok=$true;services=$services})}
    catch{Save-Row ([pscustomobject]@{event='oem-service-query';ok=$false;error=$_.Exception.Message})}
    try{$tasks=@(Get-ScheduledTask -ErrorAction Stop|Where-Object {$_.TaskName -in @('MSI_Center_M_Server','MSI_Center_M_Updater')}|ForEach-Object {[pscustomobject]@{name=$_.TaskName;path=$_.TaskPath;state=[string]$_.State;enabled=$_.Settings.Enabled}});Save-Row ([pscustomobject]@{event='oem-task-query';ok=$true;tasks=$tasks})}
    catch{Save-Row ([pscustomobject]@{event='oem-task-query';ok=$false;error=$_.Exception.Message})}
}
if(!$HidHideCliPath){
    $candidates=@(
      (Join-Path ${env:ProgramFiles} 'Nefarius Software Solutions\HidHide\x64\HidHideCLI.exe'),
      (Join-Path ${env:ProgramFiles} 'Nefarius Software Solutions\HidHide\HidHideCLI.exe')
    )
    $HidHideCliPath=$candidates|Where-Object {Test-Path -LiteralPath $_ -PathType Leaf}|Select-Object -First 1
}
Save-Row ([pscustomobject]@{event='collector-start';phase=$Phase;phaseIsOperatorLabelNotVerifiedAppState=$true;durationSeconds=$DurationSeconds;readOnly=$true;probePath=$ProbePath;probeSha256=(Get-FileHash -LiteralPath $ProbePath -Algorithm SHA256).Hash;collectorPath=$PSCommandPath;collectorSha256=(Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash;hidhideObserverExemption='not assumed; compare exact probe path and app-list, interpret inverse-state before drawing external suppression conclusions'})
if($HidHideCliPath){foreach($arg in @('--app-list','--cloak-state','--inv-state','--dev-list')){Read-Command $HidHideCliPath $arg 'hidhide-read-only-before'}}
else{Save-Row ([pscustomobject]@{event='hidhide-query-unavailable';exemptionVerified=$false;externalSuppressionConclusion='unknown; observer may still collect useful controller backend/mapping evidence'})}
Read-OemContext;Read-OemServiceTasks
Read-Command (Join-Path $env:SystemRoot 'System32\powercfg.exe') '/requests' 'power-requests-read-only-before'
$probe=New-Object Diagnostics.Process
$info=New-Object Diagnostics.ProcessStartInfo
$info.FileName=$ProbePath;$info.UseShellExecute=$false;$info.CreateNoWindow=$true
$info.Arguments='"'+(Join-Path $run 'external-observer.jsonl')+'" '+$Phase+' '+$DurationSeconds
$probe.StartInfo=$info
try{
    if(!$probe.Start()){throw 'Observer did not start'}
    Write-Output "Started read-only phase $Phase ($DurationSeconds seconds): $run"
    if($Phase -eq 'transition'){Write-Output 'Continuous transition capture: start with virtual OFF; after 15s manually ON; after 30s OFF; repeat once. The collector never toggles the application.'}else{Write-Output 'During the first 15 seconds press A once and move each stick briefly; then release. No app state is toggled.'}
    $deadline=[DateTime]::UtcNow.AddSeconds($DurationSeconds+15)
    while(!$probe.WaitForExit(1000)){
        Read-OemContext
        if([DateTime]::UtcNow -ge $deadline){try{$probe.Kill()}catch{};throw 'Observer exceeded its own bounded collection window'}
        
    }
    $exit=$probe.ExitCode
    Save-Row ([pscustomobject]@{event='observer-exited';exitCode=$exit;probePid=$probe.Id})
    if($exit -ne 0){throw "Read-only observer failed: $exit"}
}finally{try{if(!$probe.HasExited){$probe.Kill();[void]$probe.WaitForExit(1000)}}catch{};$probe.Dispose()}
if($HidHideCliPath){foreach($arg in @('--app-list','--cloak-state','--inv-state','--dev-list')){Read-Command $HidHideCliPath $arg 'hidhide-read-only-after'}}
Read-OemContext;Read-OemServiceTasks
Read-Command (Join-Path $env:SystemRoot 'System32\powercfg.exe') '/requests' 'power-requests-read-only-after'
Save-Row ([pscustomobject]@{event='collector-stop';phase=$Phase;readOnly=$true;completed=$true;output=$run})
[pscustomobject]@{completed=$true;phase=$Phase;phaseIsOperatorLabel=$true;readOnly=$true;probePath=$ProbePath;probeSha256=(Get-FileHash -LiteralPath $ProbePath -Algorithm SHA256).Hash;queryResults=@($script:queryResults.ToArray());hidhideQueriesAvailable=@($script:queryResults|Where-Object {$_.command -eq '--app-list' -and $_.ok}).Count -gt 0;externalSteamSuppressionProven=$false;controllerFixProven=$false;output=$run}|ConvertTo-Json -Depth 7|Set-Content -LiteralPath "$run\collector-result.json" -Encoding UTF8
Write-Output "COLLECTION_COMPLETE $run"
