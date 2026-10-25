[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][Alias('EvidenceDirectory')][string]$OutputDirectory,
    [ValidateSet('Signals','All')][string]$Stage='All',
    [ValidateSet('Production','OmitShutdownQuiesce')][string]$Variant='Production',
    [string]$VcVars='C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat'
)
$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
if(Test-Path -LiteralPath (Join-Path $OutputDirectory 'receipt.json')){throw 'Use a fresh evidence directory; previous receipts are never overwritten.'}
New-Item -ItemType Directory -Force -Path $OutputDirectory|Out-Null
$frozen=Join-Path $OutputDirectory 'frozen'
$segmentsDir=Join-Path $OutputDirectory 'segments'
New-Item -ItemType Directory -Force -Path $frozen,$segmentsDir|Out-Null
$utf8=[Text.UTF8Encoding]::new($false)
$receipt=[ordered]@{ok=$false;stage=$Stage;variant=$Variant;scope='Verbatim production bodies; in-memory Windows/WinRT/sender boundaries; touchpad sender-owned projection only';actualOSInput=$false;realWindowsCreated=$false;hooksInstalled=$false;devicesOpened=$false;productStarted=$false;sources=@();segments=@();sourceAssertions=@();limitations=@('No OS/WinRT implementation or downstream delivery proof.','Core cancellation, artwork, cursor, haptics and auto-hide collaborators are minimal mocks; existing scheduling is not exercised or changed.','Queued settings/rebind protection is source-asserted, not an actual message-loop replay.');startedUtc=[DateTime]::UtcNow.ToString('o')}
$sourceTexts=@{}
function Write-Utf8([string]$path,[string]$text){[IO.File]::WriteAllText($path,$text,$utf8)}
function Sha-Text([string]$text){$sha=[Security.Cryptography.SHA256]::Create();try{return ([BitConverter]::ToString($sha.ComputeHash($utf8.GetBytes($text)))).Replace('-','').ToLowerInvariant()}finally{$sha.Dispose()}}
function Save-Segment([string]$source,[string]$name,[int]$start,[int]$length){
    $full=$sourceTexts[$source];$text=$full.Substring($start,$length)
    $path=Join-Path $segmentsDir ($name+'.inc');Write-Utf8 $path $text
    $line=1+[regex]::Matches($full.Substring(0,$start),'\n').Count
    $receipt.segments+=@{name=$name;source=$source;sourceSha256=(Get-FileHash -LiteralPath (Join-Path $frozen $source) -Algorithm SHA256).Hash.ToLowerInvariant();startLine=$line;endLine=$line+[regex]::Matches($text,'\n').Count;characterOffset=$start;characterLength=$length;sha256=Sha-Text $text;path=$path}
    return $path
}
function Extract-Body([string]$source,[string]$name,[string]$pattern,[switch]$Struct){
    $text=$sourceTexts[$source];$matches=[regex]::Matches($text,$pattern,[Text.RegularExpressions.RegexOptions]::Multiline)
    if($matches.Count -ne 1){throw "Expected one production definition for $name, found $($matches.Count)."}
    $start=$matches[0].Index;$open=$text.IndexOf('{',$start);if($open -lt 0){throw "Missing body: $name"}
    $depth=0;$state='code';$end=-1
    for($i=$open;$i -lt $text.Length;$i++){
        $c=$text[$i];$next=if($i+1 -lt $text.Length){$text[$i+1]}else{[char]0}
        if($state -eq 'line'){if($c -eq "`n"){$state='code'};continue}
        if($state -eq 'block'){if($c -eq '*' -and $next -eq '/'){$state='code';$i++};continue}
        if($state -eq 'string' -or $state -eq 'char'){if($c -eq '\'){$i++;continue};if(($state -eq 'string' -and $c -eq '"') -or ($state -eq 'char' -and $c -eq "'")){$state='code'};continue}
        if($c -eq '/' -and $next -eq '/'){$state='line';$i++;continue}
        if($c -eq '/' -and $next -eq '*'){$state='block';$i++;continue}
        if($c -eq '"'){$state='string';continue};if($c -eq "'"){$state='char';continue}
        if($c -eq '{'){$depth++};if($c -eq '}'){$depth--;if($depth -eq 0){$end=$i+1;break}}
    }
    if($end -lt 0){throw "Unbalanced production body: $name"}
    if($Struct){while($end -lt $text.Length -and [char]::IsWhiteSpace($text[$end])){$end++};if($text[$end] -ne ';'){throw "Missing struct terminator: $name"};$end++}
    return Save-Segment $source $name $start ($end-$start)
}
function Aggregate([string]$name,[string[]]$paths){$lines=@($paths|ForEach-Object{'#include "segments/'+[IO.Path]::GetFileName($_)+'"'});Write-Utf8 (Join-Path $OutputDirectory $name) (($lines -join "`n")+"`n")}
function Assert-Source([string]$name,[bool]$ok,[string]$detail){$receipt.sourceAssertions+=@{name=$name;ok=$ok;detail=$detail};Write-Output ((@{True='PASS ';False='FAIL '}[$ok.ToString()])+$name)}
try{
    if($Variant -ne 'Production' -and $Stage -ne 'All'){throw 'The shutdown negative control requires Stage All.'}
    foreach($name in @('screen_touchpads.h','screen_touchpad_input_surface.h','screen_button_overlay.h','screen_touchpad_owner.h')){
        $src=Join-Path $project ('native/'+$name);$copy=Join-Path $frozen $name
        Copy-Item -LiteralPath $src -Destination $copy
        $sourceTexts[$name]=[IO.File]::ReadAllText($copy)
        $receipt.sources+=@{path=$src;frozen=$copy;sha256=(Get-FileHash -LiteralPath $copy -Algorithm SHA256).Hash.ToLowerInvariant()}
    }
    # Only this relevant switch arm is retained from main; main is never compiled or executed.
    $mainPath=Join-Path $project 'native/main.cpp';$main=[IO.File]::ReadAllText($mainPath)
    $settings=[regex]::Match($main,'(?s)case ymcc::screenpads::kRefreshMessage:\s*\{.*?\n\s*\}')
    if(!$settings.Success){throw 'Queued settings handler not found in current main.cpp.'}
    Write-Utf8 (Join-Path $frozen 'queued_settings.txt') $settings.Value
    $receipt.sources+=@{path=$mainPath;sha256=(Get-FileHash -LiteralPath $mainPath -Algorithm SHA256).Hash.ToLowerInvariant();segmentSha256=Sha-Text $settings.Value;frozen=(Join-Path $frozen 'queued_settings.txt')}
    $cpp=Join-Path $PSScriptRoot 'screen_touchpad_exit_release_selftest.cpp'
    Copy-Item -LiteralPath $cpp -Destination (Join-Path $OutputDirectory 'fixture.cpp')
    Copy-Item -LiteralPath (Join-Path $project 'deps/json/json.hpp') -Destination (Join-Path $OutputDirectory 'json.hpp')
    foreach($p in @($PSCommandPath,$cpp,(Join-Path $OutputDirectory 'json.hpp'))){$receipt.sources+=@{path=$p;sha256=(Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLowerInvariant()}}
    Aggregate 'action.inc' @(Extract-Body 'screen_touchpads.h' 'Action' '^struct Action\s*\{' -Struct)
    $t=$sourceTexts['screen_touchpads.h'];$start=$t.IndexOf('// Fixed, terminal-only evidence.');$end=$t.IndexOf('inline unsigned buttonsOpenInputSources();',$start)
    if($start -lt 0 -or $end -lt 0){throw 'Terminal evidence declarations not found.'}
    Aggregate 'evidence_declarations.inc' @(Save-Segment 'screen_touchpads.h' 'evidence_declarations' $start ($end-$start))
    Aggregate 'sender.inc' @(Extract-Body 'screen_touchpads.h' 'output' '^inline void output\([^\r\n]*\)\s*\{')
    Aggregate 'open_sources.inc' @(Extract-Body 'screen_button_overlay.h' 'buttonsOpenInputSources' '^inline unsigned buttonsOpenInputSources\(\)\s*\{')
    $signalPaths=@();foreach($name in @('checkExitInputs','confirmExitInputs','exitInputReleaseState')){$signalPaths+=Extract-Body 'screen_touchpads.h' $name ('^inline [^\r\n{]+\b'+$name+'\(\)\s*\{')}
    Aggregate 'signals.inc' $signalPaths
    if($Stage -eq 'All'){
        $t=$sourceTexts['screen_touchpad_input_surface.h'];$start=$t.IndexOf('class Surface {')+'class Surface {'.Length;$end=$t.IndexOf('    static LRESULT CALLBACK childProc',$start)
        if($start -lt 'class Surface {'.Length -or $end -lt 0){throw 'Surface declaration boundary not found.'}
        Aggregate 'surface_fields.inc' @(Save-Segment 'screen_touchpad_input_surface.h' 'surface_fields' $start ($end-$start))
        $surfacePaths=@();foreach($name in @('child','root','hasContacts','active','policyEnabled','terminalReleased','ensureMousePass','cancel','quiesce','close')){
            $surfacePaths+=Extract-Body 'screen_touchpad_input_surface.h' ('Surface_'+$name) ('\b(?:HWND|bool|void) '+$name+'\([^\r\n]*?\)(?:const)?(?: noexcept)?\s*\{')
        }
        Aggregate 'surface_methods.inc' $surfacePaths
        $paths=@();foreach($name in @('buttonsNativeInputCancel','buttonsNativeInputDisable','buttonsReleaseMapped','buttonsRelease','buttonsQuiesce','buttonsShutdown')){$paths+=Extract-Body 'screen_button_overlay.h' $name ('^inline void '+$name+'\([^\r\n]*?\)\s*\{')}
        foreach($name in @('nativeInputCancel','nativeInputClose','releaseMappedState','release','quiesce','shutdown')){
            $path=Extract-Body 'screen_touchpads.h' $name ('^inline void '+$name+'\([^\r\n]*?\)\s*\{')
            if($name -eq 'shutdown' -and $Variant -eq 'OmitShutdownQuiesce'){
                $original=[IO.File]::ReadAllText($path)
                if($original -notmatch '\Ainline void shutdown\(\)\s*\{\s*quiesce\(\);'){throw 'Negative control requires quiesce() as the first shutdown statement.'}
                $offset=$original.IndexOf('quiesce();')
                $mutated=$original.Remove($offset,'quiesce();'.Length)
                $variantPath=Join-Path $segmentsDir 'shutdown_without_quiesce.inc';Write-Utf8 $variantPath $mutated
                $receipt.variantBinding=@{identity='shutdown-remove-first-quiesce-only';transformation='Remove first statement quiesce(); from extracted production shutdown(), no other edits';originalPath=$path;originalSha256=Sha-Text $original;variantPath=$variantPath;variantSha256=Sha-Text $mutated;removedOffset=$offset;removedText='quiesce();'}
                $receipt.expectedFailures=@('all-two-pads-seven-buttons-withdraw-before-any-cancel-or-Close','shutdown-records-original-root-child-and-decoration-HWNDs','shutdown-null-owner-array-does-not-hide-live-original-window','checkExitInputs-retains-one-terminal-failure','buttonsOpenInputSources-retains-one-terminal-failure')
                $path=$variantPath
            }
            $paths+=$path
        }
        Aggregate 'withdrawal.inc' $paths
    }
    # These are source gates only; all selected bodies remain byte-for-byte intact.
    $configure=Extract-Body 'screen_touchpads.h' 'source_configure' '^inline bool configure\([^\r\n]*\)\s*\{'
    $refresh=Extract-Body 'screen_touchpads.h' 'source_refresh' '^inline void refresh\([^\r\n]*\)\s*\{'
    $rebind=Extract-Body 'screen_touchpad_owner.h' 'source_consumeOwnerRebind' '^inline bool consumeOwnerRebind\([^\r\n]*\)\s*\{'
    $ownerQ=Extract-Body 'screen_touchpad_owner.h' 'source_quiesceForOwner' '^inline void quiesceForOwner\([^\r\n]*\)\s*\{'
    $configText=[IO.File]::ReadAllText($configure);$refreshText=[IO.File]::ReadAllText($refresh);$rebindText=[IO.File]::ReadAllText($rebind);$ownerQText=[IO.File]::ReadAllText($ownerQ)
    Assert-Source 'queued-settings-calls-terminal-guarded-configure' ($settings.Value.Contains('screenpads::configure') -and $configText -match '(?s)\{\s*if\s*\([^)]*inputTeardown[^)]*\)\s*return false;') 'Current main settings arm delegates to configure; configure rejects before mutation.'
    Assert-Source 'refresh-rejects-after-quiesce' ($refreshText -match '(?s)\{\s*if\s*\([^)]*!initialized[^)]*\)\s*return;') 'quiesce clears initialized; refresh returns before window creation.'
    Assert-Source 'queued-rebind-rejects-stale-or-uninitialized-owner' ($rebindText -match '(?s)\{\s*if\s*\([^\r\n]*epoch!=ownerLifecycleEpoch[^\r\n]*!initialized[^\r\n]*\)\s*return false;') 'Actual rebind admission checks lifecycle and initialization before release/recreation.'
    Assert-Source 'owner-quiesce-invalidates-queued-rebind' ($ownerQText.Contains('quiesce();') -and $ownerQText.Contains('++ownerLifecycleEpoch') -and $ownerQText.Contains('ownerRebindQueued=false')) 'Owner quiesce closes admission and invalidates queued resume epoch.'
    $policy=@($receipt.segments|Where-Object{$_.name -in @('confirmExitInputs','checkExitInputs','output','Surface_quiesce','Surface_close')}|ForEach-Object{[IO.File]::ReadAllText($_.path)}) -join "`n"
    Assert-Source 'no-sleep-busywait-or-new-scheduler-in-executed-exit-methods' ($policy -notmatch '\b(Sleep|sleep|sleep_for|sleep_until|SetTimer|CreateThread|while)\s*\(') 'Bounded production rounds; no wait/sleep/timer in selected exit methods.'
    $manifest=Join-Path $OutputDirectory 'source-bindings.json';Write-Utf8 $manifest ($receipt|ConvertTo-Json -Depth 12)
    $define=if($Stage -eq 'Signals'){'/DEXIT_RELEASE_SIGNALS_ONLY'}else{'/DYMCC_SCREEN_NATIVE_INPUT_CANDIDATE'}
    $exe=Join-Path $OutputDirectory 'exit-release.exe';$build=Join-Path $OutputDirectory 'build.cmd'
    $buildText=@"
@echo off
set "PATH=C:\Windows\System32;C:\Windows;%PATH%"
call "$VcVars" 10.0.26100.0 >nul
if errorlevel 1 exit /b 10
cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /W4 /DNOMINMAX $define /I"$OutputDirectory" "$OutputDirectory\fixture.cpp" /Fo"$OutputDirectory\fixture.obj" /Fe"$exe" /link /INCREMENTAL:NO
if errorlevel 1 exit /b 11
dumpbin /nologo /imports "$exe" > "$OutputDirectory\imports.txt"
if errorlevel 1 exit /b 12
"@
    Write-Utf8 $build $buildText
    & $build 2>&1|Tee-Object -FilePath (Join-Path $OutputDirectory 'compile.log')
    $receipt.compileExitCode=$LASTEXITCODE;if($LASTEXITCODE -ne 0){throw 'Offline fixture compilation failed; original production segments and diagnostics retained.'}
    $imports=[IO.File]::ReadAllText((Join-Path $OutputDirectory 'imports.txt'))
    $receipt.noInputWindowOrWinRTImports=$imports -notmatch '(?im)\b(user32\.dll|combase\.dll|windowsapp\.dll|SendInput|CreateWindowExW|SetWindowsHookExW|RoActivateInstance)\b'
    if(!$receipt.noInputWindowOrWinRTImports){throw 'Unexpected Windows UI/input/WinRT import in offline executable.'}
    $report=Join-Path $OutputDirectory 'replay.json'
    & $exe $report 2>&1|Tee-Object -FilePath (Join-Path $OutputDirectory 'run.log')
    $receipt.replayExitCode=$LASTEXITCODE
    $r=Get-Content -LiteralPath $report -Raw|ConvertFrom-Json
    $receipt.checks=$r.checks;$receipt.failed=$r.failed;$receipt.report=$report;$receipt.executableSha256=(Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant()
    $receipt.failingCases=@($r.cases|Where-Object{!$_.ok}|ForEach-Object{$_.name})
    $receipt.sourceAssertionsOk=@($receipt.sourceAssertions|Where-Object{!$_.ok}).Count -eq 0
    $receipt.replayOk=[bool]$r.ok
    if($Variant -eq 'OmitShutdownQuiesce'){
        $difference=@(Compare-Object @($receipt.expectedFailures|Sort-Object) @($receipt.failingCases|Sort-Object))
        $receipt.negativeControlDetected=$receipt.replayExitCode -eq 1 -and !$r.ok -and $difference.Count -eq 0
        $receipt.ok=$receipt.negativeControlDetected -and $receipt.sourceAssertionsOk
    }else{$receipt.ok=$receipt.replayExitCode -eq 0 -and $r.ok -and $receipt.sourceAssertionsOk}
    if(!$receipt.ok){throw 'Offline regression failed or the negative control did not match its exact expected failures; see replay.json and receipt.json.'}
}catch{$receipt.error=$_.Exception.Message;Write-Error $_ -ErrorAction Continue}
finally{
    $receipt.finishedUtc=[DateTime]::UtcNow.ToString('o')
    Write-Utf8 (Join-Path $OutputDirectory 'receipt.json') ($receipt|ConvertTo-Json -Depth 12)
    Write-Utf8 (Join-Path $OutputDirectory 'source-bindings.json') ($receipt|ConvertTo-Json -Depth 12)
}
if(!$receipt.ok){exit 1}
Write-Output ('PASS receipt='+ (Join-Path $OutputDirectory 'receipt.json'))