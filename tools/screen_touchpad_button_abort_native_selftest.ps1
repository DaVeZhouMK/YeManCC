[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$OutputDirectory,
 [ValidateSet('default','wake','wake-timer','persistent','reentrant','late-fault','cumulative','independent-reset','channel-notify','channel-stop','channel-command','display-transition','channel-display','presentation-recovery','input-overflow','worker-retirement','worker-retirement-quota','manual-channel-recovery','channel-stage','channel-cleanup')][string]$CaseFilter='default')
$ErrorActionPreference='Stop'
function Set-IndependentSurfaceFixturePort([string]$Path) {
 $before=[IO.File]::ReadAllBytes($Path);$raw=[Text.Encoding]::UTF8.GetString($before)
 $old='class Surface {';$newline=if($raw.Contains(([string][char]13+[char]10))){[string][char]13+[char]10}else{[string][char]10}
 $next=$old+$newline+'    friend struct SurfaceFixtureAccess; // YMCC_TOUCHPAD_OVERFLOW_FIXTURE_PORT'
 if($raw.Split($old,[StringSplitOptions]::None).Count -ne 2 -or $raw.Contains('YMCC_TOUCHPAD_OVERFLOW_FIXTURE_PORT')){throw 'Fixture Surface port context not unique'}
 $ported=$raw.Replace($old,$next);$reverse=$ported.Replace($next,$old)
 $digest={param([byte[]]$b)[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($b)).ToLowerInvariant()}
 if((& $digest ([Text.Encoding]::UTF8.GetBytes($reverse))) -cne (& $digest $before)){throw 'Fixture port does not reverse to original Surface'}
 $bytes=[Text.Encoding]::UTF8.GetBytes($ported);[IO.File]::WriteAllBytes($Path,$bytes)
 [pscustomobject]@{productSha256=(& $digest $before);frozenSha256=(& $digest $bytes);reverseEqual=$true;port='Private friend exposes mailbox shared_ptr only; no product source port'}
}
function Set-ChannelCompletionFixturePort([string]$Path) {
 $before=[IO.File]::ReadAllBytes($Path);$raw=[Text.Encoding]::UTF8.GetString($before)
 $newline=if($raw.Contains(([string][char]13+[char]10))){[string][char]13+[char]10}else{[string][char]10}
 $pairs=@(
  @('namespace ymcc::screeninput {',"namespace ymcc::screeninput {`n#ifdef YMCC_CHANNEL_STAGE_COMPLETION_FIXTURE`ninline std::atomic<unsigned> stageFixtureDelayMs{0};`ninline std::atomic<bool> stageFixtureFailRect{false};`n#endif"),
  @('                SetEvent(command->done);',"#ifdef YMCC_CHANNEL_STAGE_COMPLETION_FIXTURE`n                const auto delay=stageFixtureDelayMs.exchange(0);if(delay)Sleep(delay);`n#endif`n                SetEvent(command->done);"),
  @('                    RECT rect{};if(!GetClientRect(input->target,&rect))winrt::throw_last_error();',"                    RECT rect{};`n#ifdef YMCC_CHANNEL_STAGE_COMPLETION_FIXTURE`n                    const HWND fixtureRectTarget=stageFixtureFailRect.exchange(false)?nullptr:input->target;`n                    if(!GetClientRect(fixtureRectTarget,&rect))winrt::throw_last_error();`n#else`n                    if(!GetClientRect(input->target,&rect))winrt::throw_last_error();`n#endif")
 )
 $ported=$raw
 foreach($pair in $pairs){$old=$pair[0];$next=$pair[1].Replace("`n",$newline);if($ported.Split($old,[StringSplitOptions]::None).Count -ne 2){throw 'Channel completion fixture context not unique'};$ported=$ported.Replace($old,$next)}
 $reverse=$ported;for($i=$pairs.Count-1;$i -ge 0;$i--){$reverse=$reverse.Replace($pairs[$i][1].Replace("`n",$newline),$pairs[$i][0])}
 if($reverse -cne $raw){throw 'Channel completion fixture port inverse mismatch'}
 [IO.File]::WriteAllBytes($Path,[Text.Encoding]::UTF8.GetBytes($ported))
 return @{inverseEqual=$true;scope='Private same-command failed GetClientRect and delayed completion notification; no product hooks'}
}

function Set-ChannelCleanupReceiptFixturePort([string]$Path) {
 $raw=[IO.File]::ReadAllText($Path);$newline=if($raw.Contains("`r`n")){"`r`n"}else{"`n"}
 $pairs=@(
  @('namespace ymcc::screeninput {',"namespace ymcc::screeninput {`ninline std::atomic<unsigned> cleanupFixtureFailures{0},cleanupFixtureControllerCalls{0};"),
  @('class InputChannel {',"class InputChannel {`n    friend struct ChannelCleanupFixtureAccess; // PRIVATE_CLEANUP_RECEIPT_FIXTURE"),
  @('stage="input-controller-close";if(input.controller)input.controller.Close();',"stage=`"input-controller-close`";if(input.controller){if(input.surface==0){++cleanupFixtureControllerCalls;const auto remaining=cleanupFixtureFailures.load();if(remaining){cleanupFixtureFailures=remaining-1;winrt::throw_hresult(remaining==2?E_ACCESSDENIED:E_INVALIDARG);}}input.controller.Close();}")
 )
 $ported=$raw;foreach($pair in $pairs){$next=$pair[1].Replace("`n",$newline);if($ported.Split($pair[0],[StringSplitOptions]::None).Count -ne 2){throw 'Cleanup receipt port context not unique'};$ported=$ported.Replace($pair[0],$next)}
 $reverse=$ported;for($i=$pairs.Count-1;$i -ge 0;$i--){$reverse=$reverse.Replace($pairs[$i][1].Replace("`n",$newline),$pairs[$i][0])};if($reverse -cne $raw){throw 'Cleanup receipt port inverse differs'}
 [IO.File]::WriteAllText($Path,$ported,[Text.UTF8Encoding]::new($false))
 return @{inverseEqual=$true;scope='Only selected canonical case: two controlled owning-STA Close exceptions, then original final attempt. Diagnostic cleanup can acknowledge only a joined empty State after strict refusal was checked.'}
}

$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
if(Test-Path -LiteralPath $OutputDirectory){throw 'Use a fresh evidence directory.'}
$frozen=Join-Path $OutputDirectory 'FrozenNative'
New-Item -ItemType Directory -Path $frozen -Force | Out-Null
$receipt=[ordered]@{ok=$false;scope='Production button configuration/recovery with real WinRT source dispatcher interruptions on a private desktop';sources=@();systemInputInjected=$false;deviceWrites=0;productStarted=$false;startedAt=(Get-Date).ToString('o')}
try{
 # Freeze only the quoted-header closure used by this consumer, so parallel
 # monitor/Steam changes are not mistaken for changes to the tested inputs.
 $pending=[Collections.Generic.Queue[string]]::new();$seen=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
 $pending.Enqueue((Join-Path $project 'native/screen_touchpad_owner.h'));$inputs=@()
 while($pending.Count){$file=$pending.Dequeue();if(!$seen.Add($file)){continue};$inputs+=Get-Item -LiteralPath $file
  foreach($match in [regex]::Matches([IO.File]::ReadAllText($file),'(?m)^\s*#include "([^"]+)"')){$child=Join-Path (Split-Path $file) $match.Groups[1].Value;if(Test-Path -LiteralPath $child -PathType Leaf){$pending.Enqueue([IO.Path]::GetFullPath($child))}}
 }
 $inputs+=Get-Item -LiteralPath (Join-Path $project 'deps/json/json.hpp'),(Join-Path $PSScriptRoot 'screen_touchpad_button_abort_native_selftest.cpp'),$PSCommandPath
 foreach($file in $inputs){$hash=(Get-FileHash -LiteralPath $file.FullName).Hash;$target=Join-Path $frozen $file.Name;Copy-Item -LiteralPath $file.FullName -Destination $target;if((Get-FileHash -LiteralPath $target).Hash -ne $hash){throw 'Source changed while freezing'};$receipt.sources+=@{path=$file.FullName;frozen=$target;sha256=$hash}}
 if($CaseFilter -eq 'channel-cleanup'){
  $extra=Get-Item -LiteralPath (Join-Path $PSScriptRoot 'screen_touchpad_cleanup_receipt_cases.inc');$extraTarget=Join-Path $frozen $extra.Name;Copy-Item -LiteralPath $extra.FullName -Destination $extraTarget;
  $extraHash=(Get-FileHash -LiteralPath $extra.FullName).Hash;if((Get-FileHash -LiteralPath $extraTarget).Hash -ne $extraHash){throw 'Cleanup fixture changed while freezing'};$receipt.sources+=@{path=$extra.FullName;frozen=$extraTarget;sha256=$extraHash}
 }
 $fixtureDefine=''
 if($CaseFilter -eq 'input-overflow' -or $CaseFilter -eq 'worker-retirement' -or $CaseFilter -eq 'worker-retirement-quota' -or $CaseFilter -eq 'manual-channel-recovery' -or $CaseFilter -eq 'channel-cleanup'){
  $source=@($receipt.sources|Where-Object {[IO.Path]::GetFileName($_.frozen) -ceq 'screen_touchpad_input_surface.h'})
  if($source.Count -ne 1){throw 'Fixture Surface source is not unique'}
  $port=Set-IndependentSurfaceFixturePort $source[0].frozen;$source[0].frozenSha256=$port.frozenSha256;$receipt.fixturePort=$port
  $fixtureDefine='/DYMCC_SCREEN_TOUCHPAD_OVERFLOW_FIXTURE=1'
 }
 if($CaseFilter -eq 'channel-cleanup'){
  $channelSource=@($receipt.sources|Where-Object {[IO.Path]::GetFileName($_.frozen) -ceq 'screen_touchpad_input_channel.h'});if($channelSource.Count -ne 1){throw 'Cleanup channel source is not unique'}
  $receipt.channelCleanupPort=Set-ChannelCleanupReceiptFixturePort $channelSource[0].frozen;$channelSource[0].frozenSha256=(Get-FileHash -LiteralPath $channelSource[0].frozen).Hash
  $fixtureDefine+=' /DYMCC_CHANNEL_CLEANUP_RECEIPT_FIXTURE=1'
 }
 if($CaseFilter -eq 'channel-stage'){
  $channelSource=@($receipt.sources|Where-Object {[IO.Path]::GetFileName($_.frozen) -ceq 'screen_touchpad_input_channel.h'})
  if($channelSource.Count -ne 1){throw 'Channel completion source is not unique'}
  $receipt.channelCompletionPort=Set-ChannelCompletionFixturePort $channelSource[0].frozen
  $channelSource[0].frozenSha256=(Get-FileHash -LiteralPath $channelSource[0].frozen).Hash
  $fixtureDefine='/DYMCC_CHANNEL_STAGE_COMPLETION_FIXTURE=1'
 }
 $exe=Join-Path $OutputDirectory 'native-button-abort.exe';$build=Join-Path $OutputDirectory 'build.cmd'
 @"
@echo off
set "PATH=C:\Windows\System32;C:\Windows;%PATH%"
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" 10.0.26100.0 >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /bigobj /DNDEBUG /DUNICODE /D_UNICODE $fixtureDefine /sourceDependencies "$OutputDirectory\compile-inputs.json" /I"$frozen" /I"C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\cppwinrt" "$frozen\screen_touchpad_button_abort_native_selftest.cpp" /Fo"$OutputDirectory\native-button-abort.obj" /Fe"$exe" /link user32.lib gdi32.lib ole32.lib shell32.lib
exit /b %errorlevel%
"@ | Set-Content -LiteralPath $build -Encoding ascii
 & $build *> (Join-Path $OutputDirectory 'compile.log')
 $receipt.compileExitCode=$LASTEXITCODE;if($LASTEXITCODE -ne 0){throw 'Native fixture compilation failed; see compile.log'}
 $report=Join-Path $OutputDirectory 'replay.json'
 $selectedArgs='"'+$report+'"'
 if($CaseFilter -eq 'channel-cleanup'){$selectedArgs+=' --manual-channel-recovery-only'}elseif($CaseFilter -ne 'default'){$selectedArgs+=' --'+$CaseFilter+'-only'}
 $receipt.caseFilter=$CaseFilter
 $run=Start-Process -FilePath $exe -ArgumentList $selectedArgs -WindowStyle Hidden -Wait -PassThru -RedirectStandardOutput (Join-Path $OutputDirectory 'stdout.txt') -RedirectStandardError (Join-Path $OutputDirectory 'stderr.txt')
 $receipt.runExitCode=$run.ExitCode;$r=Get-Content -LiteralPath $report -Raw|ConvertFrom-Json
 $receipt.checks=$r.checks;$receipt.failures=@($r.cases|Where-Object {!$_.ok}|ForEach-Object {$_.name})
 if($run.ExitCode -ne 0 -or !$r.ok -or !$r.runtimeClosed -or !$r.queueClosed -or !$r.inputDesktopUnchanged -or !$r.privateDesktopClosed){throw 'Native replay or cleanup assertions failed'}
 $receipt.sourcesUnchanged=$true
 foreach($file in $receipt.sources){$expectedFrozen=if($file.frozenSha256){$file.frozenSha256}else{$file.sha256};if((Get-FileHash -LiteralPath $file.path).Hash -ne $file.sha256 -or (Get-FileHash -LiteralPath $file.frozen).Hash -ne $expectedFrozen){$receipt.sourcesUnchanged=$false}}
 $receipt.exeSha256=(Get-FileHash -LiteralPath $exe).Hash;$receipt.ok=$receipt.sourcesUnchanged
 $receipt.limitations=@('The callback interruption is deliberately arranged on the real dispatcher; it is not a trace of the unknown original callback.','No real handheld, Steam consumption or physical input delivery proof.')
}catch{$receipt.error=$_.Exception.Message;Write-Output $receipt.error}finally{$receipt.finishedAt=(Get-Date).ToString('o');$receipt|ConvertTo-Json -Depth 10|Set-Content -LiteralPath (Join-Path $OutputDirectory 'receipt.json') -Encoding utf8}
if(!$receipt.ok){exit 1}
Write-Output ('PASS checks='+$receipt.checks+' receipt='+(Join-Path $OutputDirectory 'receipt.json'))
