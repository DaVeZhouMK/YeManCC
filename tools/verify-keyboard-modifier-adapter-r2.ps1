#requires -Version 7.0
[CmdletBinding()]
param([string]$OutputRoot = 'G:\YeManCC-Work\Build\Validation\ROG-SD-Modifier-Evidence-R2-20261013\adapter')
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$allowed='G:\YeManCC-Work\Build\Validation\ROG-SD-Modifier-Evidence-R2-20261013\adapter'
$OutputRoot=[IO.Path]::GetFullPath($OutputRoot).TrimEnd('\','/')
if(-not ($OutputRoot.Equals($allowed,[StringComparison]::OrdinalIgnoreCase) -or $OutputRoot.StartsWith($allowed+'\',[StringComparison]::OrdinalIgnoreCase))) { throw 'Artifacts must stay in the authorized adapter directory.' }
if($OutputRoot -match '["%!\r\n&|<>^]'){throw 'Shell metacharacters are not permitted in the output path.'}
$ancestor=$OutputRoot
while($ancestor){
    if((Test-Path -LiteralPath $ancestor) -and ((Get-Item -LiteralPath $ancestor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)){throw "Reparse point in output ancestry: $ancestor"}
    $ancestor=Split-Path -Parent $ancestor
}
$repo=[IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$frozenRoot='G:\YeManCC-Work\Build\Validation\Controller-Diagnostics-R10-20261013\native-probe-r2\source'
$frozenMain=Join-Path $frozenRoot 'native\main.cpp'
$ambientMain=Join-Path $repo 'native\main.cpp'
$runRoot=Join-Path $OutputRoot ('run-'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff')+'-'+[Guid]::NewGuid().ToString('N'))
$snapshot=Join-Path $runRoot 'snapshot'
New-Item -ItemType Directory -Path $snapshot,(Join-Path $runRoot 'temp') -Force | Out-Null
$utf8=[Text.UTF8Encoding]::new($false)
function Write-Utf8([string]$Path,[string]$Value){
    if(-not [IO.Path]::GetFullPath($Path).StartsWith($runRoot+'\',[StringComparison]::OrdinalIgnoreCase)){throw "Write escapes run: $Path"}
    [IO.File]::WriteAllText($Path,$Value,$utf8)
}
function Write-Json([string]$Path,$Value){Write-Utf8 $Path (ConvertTo-Json -InputObject $Value -Depth 30)}
function Text-Hash([string]$Text){[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($utf8.GetBytes($Text)))}
# Extract a complete definition, respecting C++ strings/chars/comments/braces.
# These eight anchors contain no raw-string literal or conditional compilation.
function Extract-Function([string]$Text,[string]$Name){
    $matches=[regex]::Matches($Text,'(?m)^static (?:void|json|DWORD|bool) '+[regex]::Escape($Name)+'\(')
    if($matches.Count -ne 1){throw "Expected one definition anchor for $Name; got $($matches.Count)"}
    $start=$matches[0].Index;$brace=$Text.IndexOf('{',$start)
    $depth=0;$state='code';$end=-1
    for($i=$brace;$i -lt $Text.Length;$i++){
        $c=$Text[$i];$next=if($i+1 -lt $Text.Length){$Text[$i+1]}else{[char]0}
        if($state -eq 'line'){if($c -eq "`n"){$state='code'};continue}
        if($state -eq 'block'){if($c -eq '*' -and $next -eq '/'){$state='code';$i++};continue}
        if($state -eq 'string' -or $state -eq 'char'){
            if($c -eq '\'){$i++;continue}
            if(($state -eq 'string' -and $c -eq '"') -or ($state -eq 'char' -and $c -eq "'")){$state='code'};continue
        }
        if($c -eq '/' -and $next -eq '/'){$state='line';$i++;continue}
        if($c -eq '/' -and $next -eq '*'){$state='block';$i++;continue}
        if($c -eq '"'){$state='string';continue}
        if($c -eq "'"){$state='char';continue}
        if($c -eq '{'){$depth++}
        if($c -eq '}'){$depth--;if($depth -eq 0){$end=$i+1;break}}
    }
    if($end -lt 0){throw "Unterminated definition: $Name"}
    $fragment=$Text.Substring($start,$end-$start)
    if($fragment -cmatch '\bR"|(?m)^\s*#'){throw "Extraction needs unsupported raw-string/preprocessor handling: $Name"}
    [pscustomobject]@{name=$Name;text=$fragment;sha256=Text-Hash $fragment;startLine=1+[regex]::Matches($Text.Substring(0,$start),"`n").Count;endLine=1+[regex]::Matches($Text.Substring(0,$end),"`n").Count;anchor=$fragment.Split("`n")[0].TrimEnd("`r")}
}
$processRuns=[Collections.Generic.List[object]]::new()
function Run-PrivateChild([string]$Exe,[string[]]$Arguments,[string]$Name){
    $psi=[Diagnostics.ProcessStartInfo]::new();$psi.FileName=$Exe
    foreach($argument in $Arguments){$psi.ArgumentList.Add($argument)}
    $psi.WorkingDirectory=$runRoot;$psi.CreateNoWindow=$true;$psi.UseShellExecute=$false
    $psi.RedirectStandardOutput=$true;$psi.RedirectStandardError=$true
    $psi.Environment['TEMP']=Join-Path $runRoot 'temp';$psi.Environment['TMP']=Join-Path $runRoot 'temp'
    $timer=[Diagnostics.Stopwatch]::StartNew();$process=[Diagnostics.Process]::Start($psi)
    $outTask=$process.StandardOutput.ReadToEndAsync();$errTask=$process.StandardError.ReadToEndAsync();$timedOut=$false
    try{
        if(-not $process.WaitForExit(25000)){$timedOut=$true;$process.Kill($true);if(-not $process.WaitForExit(1000)){throw 'Own child did not terminate within one second.'}}
        $timer.Stop();$stdout=$outTask.GetAwaiter().GetResult();$stderr=$errTask.GetAwaiter().GetResult()
        Write-Utf8 (Join-Path $runRoot ($Name+'.stdout.txt')) $stdout;Write-Utf8 (Join-Path $runRoot ($Name+'.stderr.txt')) $stderr
        $processRuns.Add([pscustomobject]@{name=$Name;pid=$process.Id;executable=$Exe;arguments=$Arguments;createNoWindow=$true;useShellExecute=$false;deadlineMs=25000;lifetimeMs=$timer.ElapsedMilliseconds;exitCode=$process.ExitCode;timedOut=$timedOut;hasExited=$process.HasExited;cleanup='owned Process disposed; no persistent process'})
        if($timedOut){throw "Own child exceeded 25 seconds: $Name"}
        [pscustomobject]@{exitCode=$process.ExitCode;stdout=$stdout;stderr=$stderr}
    }finally{$process.Dispose()}
}
$bindings=[Collections.Generic.List[object]]::new();$fragments=[Collections.Generic.List[object]]::new()
$names=@('keyboardModifierObserveLowLevel','keyboardModifierObserveRaw','keyboardModifierObserveSelfSend','keyboardModifierRawNameAtDrain','keyboardModifierFlushEvidence','nativeKeyboardShortcutInputFlags','nativeKeyboardShortcutSend','nativeFrontendButtonSendGamebar')
$passed=$false;$failure=$null;$result=$null;$stable=$false;$fragmentsMatch=$false;$binary=Join-Path $runRoot 'keyboard_modifier_adapter_r2_selftest.exe'
$ambientHashBefore=$null;$ambientHashAfter=$null;$helperMatches=$false;$jsonMatches=$false;$importsVerified=$false
try{
    $inputs=@(
        @{source=$frozenMain;name='frozen-main.cpp';role='R10 frozen main, only extracted functions compiled'},
        @{source=(Join-Path $frozenRoot 'native\keyboard_modifier_event_evidence.h');name='keyboard_modifier_event_evidence.h';role='R10 frozen actual recorder'},
        @{source=(Join-Path $frozenRoot 'deps\json\json.hpp');name='json.hpp';role='R10 frozen actual JSON serializer dependency'},
        @{source=(Join-Path $PSScriptRoot 'keyboard_modifier_adapter_r2_selftest.cpp');name='keyboard_modifier_adapter_r2_selftest.cpp';role='synthetic fake APIs and assertions'},
        @{source=$PSCommandPath;name='verify-keyboard-modifier-adapter-r2.ps1';role='verifier'})
    foreach($item in $inputs){
        $hash=(Get-FileHash -LiteralPath $item.source -Algorithm SHA256).Hash;$copy=Join-Path $snapshot $item.name
        Copy-Item -LiteralPath $item.source -Destination $copy
        if((Get-FileHash -LiteralPath $copy -Algorithm SHA256).Hash -ne $hash){throw "Source changed during copy: $($item.source)"}
        $bindings.Add([pscustomobject]@{source=$item.source;snapshot=$copy;sha256=$hash;role=$item.role})
    }
    $ambientHashBefore=(Get-FileHash -LiteralPath $ambientMain -Algorithm SHA256).Hash
    $ambientText=[IO.File]::ReadAllText($ambientMain)
    Write-Utf8 (Join-Path $snapshot 'ambient-main.cpp') $ambientText
    if((Get-FileHash -LiteralPath $ambientMain -Algorithm SHA256).Hash -ne $ambientHashBefore){throw 'Ambient main changed while reading; rerun to bind stable fragments.'}
    $frozenText=[IO.File]::ReadAllText((Join-Path $snapshot 'frozen-main.cpp'))
    $helperMatches=(Get-FileHash -LiteralPath (Join-Path $repo 'native\keyboard_modifier_event_evidence.h')).Hash -eq $bindings[1].sha256
    $jsonMatches=(Get-FileHash -LiteralPath (Join-Path $repo 'deps\json\json.hpp')).Hash -eq $bindings[2].sha256
    if(-not $helperMatches -or -not $jsonMatches){throw 'Ambient recorder or JSON differs from frozen R10 dependency; explicit review needed.'}
    $combined=[Text.StringBuilder]::new()
    foreach($name in $names){
        $bound=Extract-Function $frozenText $name;$ambient=Extract-Function $ambientText $name
        $fragmentPath=Join-Path $snapshot ($name+'.fragment.cpp');Write-Utf8 $fragmentPath $bound.text
        [void]$combined.Append("// Bound frozen source: native/main.cpp:$($bound.startLine) $name`n")
        [void]$combined.Append($bound.text);[void]$combined.Append("`n`n")
        $fragments.Add([pscustomobject]@{name=$name;anchor=$bound.anchor;frozenStartLine=$bound.startLine;frozenEndLine=$bound.endLine;frozenFragmentSha256=$bound.sha256;ambientStartLine=$ambient.startLine;ambientEndLine=$ambient.endLine;ambientFragmentSha256=$ambient.sha256;exactTextMatch=($bound.text -ceq $ambient.text);path=$fragmentPath})
    }
    $fragmentsMatch=@($fragments | Where-Object {-not $_.exactTextMatch}).Count -eq 0
    Write-Json (Join-Path $runRoot 'fragment-binding.json') $fragments.ToArray()
    if(-not $fragmentsMatch){throw 'At least one extracted frozen function differs from current main; no equivalence claim.'}
    $fragmentText=$combined.ToString();Write-Utf8 (Join-Path $snapshot 'actual_adapter_fragments.inc') $fragmentText
    if($fragmentText -notmatch '\(std::min\)\(requested,UINT\(64\)\)'){throw 'Frozen self adapter is missing the UINT inspection-bound fix.'}
    # Source-fragment input/Win32 dependencies are allowlisted and macro redirected.
    $apiNames=@('GetLastError','SetLastError','GetTickCount64','GetCurrentThreadId','GetRawInputDeviceInfoW','SendInput')
    if($fragmentText -match 'SetWindowsHookEx|GetAsyncKeyState|RegisterRawInputDevices|CreateFile|DeviceIoControl|GetRawInputData|CallNextHookEx|OpenProcess|CreateProcess'){throw 'Unexpected API in extracted fragments.'}
    Write-Json (Join-Path $runRoot 'synthetic-api-boundary.json') @{fakedDirectWin32InputCalls=$apiNames;fakeOtherDependencies=@('currentPowerGeneration','W2U (ASCII-only, including injected exception)','appendDomainLog (memory sink, including injected exception)');realRecorder=$bindings[1];realJson=$bindings[2];fragmentBodiesUnchanged=$true;fullProductionTranslationUnitCompiled=$false;inputApiExecution=$false;userLogsOrigin='mainline-export';fixtureOrigin='private-synthetic-candidate-validation';historicalIncidentNativeSha256='610cbc95c8a98e492ed34015a6f552602e75d056b732bccfd356fa77b7747670';historicalBuildEquivalenceClaimed=$false}
    $vcvars='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
    if(-not (Test-Path -LiteralPath $vcvars)){throw 'MSVC x64 compiler unavailable.'}
    $batchPath=Join-Path $runRoot 'build.cmd';$importsPath=Join-Path $runRoot 'binary-imports.txt';$object=Join-Path $runRoot 'keyboard_modifier_adapter_r2_selftest.obj'
    $batch=@('@echo off',('call "'+$vcvars+'" >nul'),'if errorlevel 1 exit /b %errorlevel%',
        ('cl.exe /nologo /std:c++20 /utf-8 /EHsc /MT /Od /W4 /WX /I"'+$snapshot+'" "'+$snapshot+'\keyboard_modifier_adapter_r2_selftest.cpp" /Fo"'+$object+'" /Fe"'+$binary+'" /link /INCREMENTAL:NO'),
        'if errorlevel 1 exit /b %errorlevel%',('dumpbin.exe /nologo /imports "'+$binary+'" >"'+$importsPath+'"'),'exit /b %errorlevel%')
    Write-Utf8 $batchPath (($batch -join "`r`n")+"`r`n")
    Write-Json (Join-Path $runRoot 'build-inputs.json') @{compilerEnvironment=$vcvars;compilerEnvironmentSha256=(Get-FileHash -LiteralPath $vcvars).Hash;command=$batchPath;compilerFlags='/std:c++20 /utf-8 /EHsc /MT /Od /W4 /WX';inputs=$bindings.ToArray();fragments=$fragments.ToArray();entireFrozenMainCompiled=$false}
    $build=Run-PrivateChild (Join-Path $env:SystemRoot 'System32\cmd.exe') @('/d','/c',$batchPath) 'build'
    if($build.exitCode -ne 0){throw "Synthetic build failed: $($build.stdout) $($build.stderr)"}
    $imports=[IO.File]::ReadAllText($importsPath)
    if($imports -match '(?i)USER32\.dll|SendInput|GetRawInputDeviceInfo|SetWindowsHookEx|RegisterRawInputDevices|GetAsyncKeyState|GetRawInputData|DeviceIoControl'){throw 'Unexpected real input/device API import.'}
    $importsVerified=$true
    $run=Run-PrivateChild $binary @() 'synthetic-adapter-selftest'
    $result=$run.stdout.Trim() | ConvertFrom-Json
    Write-Json (Join-Path $runRoot 'selftest-result.json') $result
    if($run.exitCode -ne 0 -or -not $result.passed -or $result.failed -ne 0 -or -not $result.allFragmentWin32InputCallsFaked){throw 'Actual-fragment synthetic assertions failed; inspect named scenarios.'}
    $passed=$true
}catch{$failure=$_.Exception.Message;Write-Host $failure}
finally{
    $stable=$true
    $after=foreach($item in $bindings){
        $current=(Get-FileHash -LiteralPath $item.source -Algorithm SHA256).Hash;$copy=(Get-FileHash -LiteralPath $item.snapshot -Algorithm SHA256).Hash
        if($current -ne $item.sha256 -or $copy -ne $item.sha256){$stable=$false}
        [pscustomobject]@{source=$item.source;initialSha256=$item.sha256;currentSha256=$current;snapshotSha256=$copy}
    }
    $ambientAfter=[Collections.Generic.List[object]]::new()
    try{
        $ambientHashAfter=(Get-FileHash -LiteralPath $ambientMain -Algorithm SHA256).Hash;$currentText=[IO.File]::ReadAllText($ambientMain)
        foreach($item in $fragments){
            $current=Extract-Function $currentText $item.name;$same=$current.sha256 -eq $item.frozenFragmentSha256
            if(-not $same){$fragmentsMatch=$false}
            $ambientAfter.Add([pscustomobject]@{name=$item.name;sha256=$current.sha256;startLine=$current.startLine;endLine=$current.endLine;matchesFrozen=$same})
        }
    }catch{$fragmentsMatch=$false;if(-not $failure){$failure=$_.Exception.Message}}
    $helperCurrent=(Get-FileHash -LiteralPath (Join-Path $repo 'native\keyboard_modifier_event_evidence.h')).Hash
    $jsonCurrent=(Get-FileHash -LiteralPath (Join-Path $repo 'deps\json\json.hpp')).Hash
    if($bindings.Count -ge 3){$helperMatches=$helperMatches -and $helperCurrent -eq $bindings[1].sha256;$jsonMatches=$jsonMatches -and $jsonCurrent -eq $bindings[2].sha256}
    $passed=$passed -and $stable -and $fragmentsMatch -and $helperMatches -and $jsonMatches -and $importsVerified
    Write-Json (Join-Path $runRoot 'source-hashes-after.json') @($after)
    Write-Json (Join-Path $runRoot 'ambient-fragments-after.json') $ambientAfter.ToArray()
    Write-Json (Join-Path $runRoot 'process-runs.json') $processRuns.ToArray()
    $summary=[ordered]@{
        passed=$passed;failure=$failure;frozenInputsStable=$stable;allEightFrozenFunctionsMatchAmbient=$fragmentsMatch
        frozenMain=$frozenMain;frozenMainSha256=if($bindings.Count){$bindings[0].sha256}else{$null}
        ambientMain=$ambientMain;ambientMainFullSha256Before=$ambientHashBefore;ambientMainFullSha256After=$ambientHashAfter
        ambientWholeFileEqualsFrozenClaimed=$false;ambientFullFileStabilityRequired=$false
        ambientRecorderMatchesFrozen=$helperMatches;ambientJsonMatchesFrozen=$jsonMatches
        noUser32OrInputDeviceImports=$importsVerified;binary=$binary
        sourceBinding=$bindings.ToArray();fragments=$fragments.ToArray();result=$result;processRuns=$processRuns.ToArray()
        scope='Actual frozen R10 function fragments under synthetic Win32/input/context/log dependencies, with actual frozen recorder and JSON serializer. Does not execute the production translation unit or observe Windows input.'
        fixtureOrigin='private-synthetic-candidate-validation';userLogsOrigin='mainline-export'
        historicalIncidentNativeSha256='610cbc95c8a98e492ed34015a6f552602e75d056b732bccfd356fa77b7747670';historicalIncidentBuildEquivalenceClaimed=$false
        realInputDeviceHookProductProbeServiceExecuted=$false;existingInputBehaviorChanged=$false
        processesBoundedToMs=25000;allOwnedChildrenExited=(@($processRuns | Where-Object {-not $_.hasExited}).Count -eq 0)
        outputRoot=$runRoot;modifiedFiles=@('tools/keyboard_modifier_adapter_r2_selftest.cpp','tools/verify-keyboard-modifier-adapter-r2.ps1')
    }
    Write-Json (Join-Path $runRoot 'verification-summary.json') $summary
    $report=[Text.StringBuilder]::new()
    [void]$report.AppendLine("R2 adapter synthetic verification: passed=$passed; failure=$failure.")
    [void]$report.AppendLine('')
    [void]$report.AppendLine("Frozen R10 source: $frozenMain")
    [void]$report.AppendLine("Frozen main SHA256: $($summary.frozenMainSha256)")
    [void]$report.AppendLine("Ambient main SHA256 before: $ambientHashBefore")
    [void]$report.AppendLine("Ambient main SHA256 after: $ambientHashAfter")
    [void]$report.AppendLine("Eight function bodies match frozen source and ambient source: $fragmentsMatch. Whole-file equivalence is not claimed; independent installer changes are outside this test.")
    [void]$report.AppendLine('')
    [void]$report.AppendLine('User exports are mainline logs. This harness and its outputs are private synthetic candidate evidence, not the historical incident binary. No claim of Ctrl ownership, Steam consumption, SD keyboard-IN reports, G3E or OEM correctness follows.')
    [void]$report.AppendLine('')
    [void]$report.AppendLine('Every directly called Win32/input API in extracted bodies is macro redirected. The actual recorder and JSON header are frozen copies. W2U is a synthetic ASCII converter; appendDomainLog is a memory sink. Binary imports are checked for absence of USER32 and input/device API imports. Full hooks, WM_INPUT acquisition, timer wiring, lifecycle owners and OEM replay call site are outside scope.')
    [void]$report.AppendLine('')
    if($result){
        [void]$report.AppendLine("Executed $($result.scenarios.Count) meaningful scenarios, $($result.checks) assertions, $($result.failed) failures.")
        [void]$report.AppendLine('')
        [void]$report.AppendLine('| Scenario | Assertions | Failures |')
        [void]$report.AppendLine('|---|---:|---:|')
        foreach($case in $result.scenarios){[void]$report.AppendLine("| $($case.name) | $($case.checks) | $($case.failed) |")}
        [void]$report.AppendLine('')
        foreach($limit in $result.limits){[void]$report.AppendLine("- $limit")}
    }
    [void]$report.AppendLine('')
    [void]$report.AppendLine('The raw-name inconsistent-success-result case describes synthetic boundary behavior, not a demonstrated Microsoft API failure or production defect. Name identity remains explicitly unproven. Missing/empty events are not absence proof. Count-based inserted masks are not per-key receipts; error hints do not establish UIPI.')
    [void]$report.AppendLine('')
    foreach($process in $processRuns){[void]$report.AppendLine("Own child $($process.name): PID $($process.pid), lifetime $($process.lifetimeMs)ms, exit $($process.exitCode), hasExited=$($process.hasExited), timeout=$($process.timedOut), CreateNoWindow=true, UseShellExecute=false; Process disposed.")}
    [void]$report.AppendLine('')
    [void]$report.AppendLine('Only the two new tool files and this private adapter output directory were written. Production source/header/dependency files and packages were read only.')
    Write-Utf8 (Join-Path $runRoot 'verification-report.md') $report.ToString()
    $artifacts=foreach($file in Get-ChildItem -LiteralPath $runRoot -Recurse -File | Where-Object {$_.Name -ne 'artifact-binding.json'}){
        [pscustomobject]@{path=$file.FullName;bytes=$file.Length;sha256=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash}
    }
    Write-Json (Join-Path $runRoot 'artifact-binding.json') @($artifacts)
    # Pointer is allowed only inside the authorized adapter output root.
    [IO.File]::WriteAllText((Join-Path $OutputRoot 'adapter-verification-handoff.json'),(ConvertTo-Json -InputObject @{passed=$passed;summary=(Join-Path $runRoot 'verification-summary.json');report=(Join-Path $runRoot 'verification-report.md');artifactBinding=(Join-Path $runRoot 'artifact-binding.json');failure=$failure} -Depth 8),$utf8)
    Write-Host (ConvertTo-Json -InputObject @{passed=$passed;failure=$failure;summary=(Join-Path $runRoot 'verification-summary.json');report=(Join-Path $runRoot 'verification-report.md');frozenFunctionsMatchAmbient=$fragmentsMatch;checks=if($result){$result.checks}else{0};failed=if($result){$result.failed}else{$null}} -Depth 6)
}
if(-not $passed){exit 1}