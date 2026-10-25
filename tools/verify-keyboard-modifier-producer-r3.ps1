#requires -Version 7.0
[CmdletBinding()]
param([string]$OutputRoot = 'G:\YeManCC-Work\Build\Validation\ROG-SD-Modifier-Evidence-R3-20261013\producers')
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$allowed='G:\YeManCC-Work\Build\Validation\ROG-SD-Modifier-Evidence-R3-20261013\producers'
$OutputRoot=[IO.Path]::GetFullPath($OutputRoot).TrimEnd('\','/')
if(-not ($OutputRoot.Equals($allowed,[StringComparison]::OrdinalIgnoreCase) -or $OutputRoot.StartsWith($allowed+'\',[StringComparison]::OrdinalIgnoreCase))) { throw 'Artifacts must stay in the authorized R3 producers directory.' }
if($OutputRoot -match '["%!\r\n&|<>^]'){throw 'Shell metacharacters are not permitted in the output path.'}
$ancestor=$OutputRoot
while($ancestor){
    if((Test-Path -LiteralPath $ancestor) -and ((Get-Item -LiteralPath $ancestor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)){throw "Reparse point in output ancestry: $ancestor"}
    $ancestor=Split-Path -Parent $ancestor
}
$repo=[IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$evidenceRoot='G:\YeManCC-Work\Build\Validation\ROG-SD-Modifier-Evidence-R3-20261013'
$beforeRoot=Join-Path $evidenceRoot 'source-before'
$frozenRoot=Join-Path $evidenceRoot 'source-after'
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
# Selected definitions contain no raw-string literal or conditional compilation.
function Extract-Function([string]$Text,[string]$Name){
    $matches=[regex]::Matches($Text,'(?m)^(?:static (?:void|json|DWORD|bool)|inline constexpr const char\*) '+[regex]::Escape($Name)+'\([^;{}]*\)[ \t]*(?:noexcept[ \t]*)?\{')
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
$actualNames=@('keyboardModifierObserveSelfSend','keyboardModifierRawNameAtDrain','keyboardModifierFlushEvidence','focusMainWindowIsForeground','refocusWebView','returnToDesktop','nativeSendTaskView')
$originalNames=@('focusMainWindowIsForeground','refocusWebView','returnToDesktop','nativeSendTaskView')
$passed=$false;$failure=$null;$result=$null;$stable=$false;$fragmentsMatch=$false;$importsVerified=$false
$ambientMainHashBefore=$null;$ambientMainHashAfter=$null;$ambientHeaderHashBefore=$null;$ambientHeaderHashAfter=$null
$sourceNameMatch=$false;$enumMatch=$false;$recordBindingValid=$false;$audit=$null
$binary=Join-Path $runRoot 'keyboard_modifier_producer_r3_selftest.exe'
$ambientHeader=Join-Path $repo 'native\keyboard_modifier_event_evidence.h'
function Enum-Definition([string]$Text){
    $m=[regex]::Matches($Text,'enum class SourceId\s*:\s*std::uint8_t\s*\{[^}]*\};')
    if($m.Count -ne 1){throw 'Expected one simple SourceId enum definition.'};$m[0].Value
}
function Bind-Input([string]$Source,[string]$Name,[string]$Role){
    $hash=(Get-FileHash -LiteralPath $Source -Algorithm SHA256).Hash;$copy=Join-Path $snapshot $Name
    Copy-Item -LiteralPath $Source -Destination $copy
    if((Get-FileHash -LiteralPath $copy -Algorithm SHA256).Hash -ne $hash){throw "Source changed during copy: $Source"}
    $bindings.Add([pscustomobject]@{source=$Source;snapshot=$copy;sha256=$hash;role=$Role})
}
try{
    Bind-Input $frozenMain 'source-after-main.cpp' 'R3 frozen main; only extracted definitions compiled'
    Bind-Input (Join-Path $frozenRoot 'native\keyboard_modifier_event_evidence.h') 'keyboard_modifier_event_evidence.h' 'R3 frozen actual recorder and sourceName'
    Bind-Input (Join-Path $beforeRoot 'native\main.cpp') 'source-before-main.cpp' 'Actual original caller definitions for behavioral comparison'
    Bind-Input (Join-Path $beforeRoot 'native\keyboard_modifier_event_evidence.h') 'source-before-header.h' 'Actual original SourceId enum'
    Bind-Input (Join-Path $repo 'deps\json\json.hpp') 'json.hpp' 'Actual JSON serializer header; unchanged current dependency'
    Bind-Input (Join-Path $PSScriptRoot 'keyboard_modifier_producer_r3_selftest.cpp') 'keyboard_modifier_producer_r3_selftest.cpp' 'Synthetic APIs and reference/candidate assertions'
    Bind-Input $PSCommandPath 'verify-keyboard-modifier-producer-r3.ps1' 'Verifier'
    Bind-Input (Join-Path $evidenceRoot 'source-change-record.json') 'source-change-record.json' 'Parent source change record; snapshot hash validation only'
    $record=Get-Content -LiteralPath $bindings[7].snapshot -Raw | ConvertFrom-Json
    $mainRecord=@($record.changes | Where-Object {$_.path -match 'native[/\\]main\.cpp$'})
    $headerRecord=@($record.changes | Where-Object {$_.path -match 'native[/\\]keyboard_modifier_event_evidence\.h$'})
    if($mainRecord.Count -ne 1 -or $headerRecord.Count -ne 1){throw 'Source-change record lacks expected paths.'}
    $recordBindingValid=$mainRecord[0].afterSha256 -eq $bindings[0].sha256 -and $mainRecord[0].beforeSha256 -eq $bindings[2].sha256 -and $headerRecord[0].afterSha256 -eq $bindings[1].sha256 -and $headerRecord[0].beforeSha256 -eq $bindings[3].sha256
    if(-not $recordBindingValid){throw 'Before/after main/header snapshot hashes do not match parent change record.'}
    $ambientMainHashBefore=(Get-FileHash -LiteralPath $ambientMain).Hash;$ambientHeaderHashBefore=(Get-FileHash -LiteralPath $ambientHeader).Hash
    $ambientText=[IO.File]::ReadAllText($ambientMain);$ambientHeaderText=[IO.File]::ReadAllText($ambientHeader)
    if((Get-FileHash -LiteralPath $ambientMain).Hash -ne $ambientMainHashBefore -or (Get-FileHash -LiteralPath $ambientHeader).Hash -ne $ambientHeaderHashBefore){throw 'Ambient source changed while reading; rerun to bind stable fragments.'}
    Write-Utf8 (Join-Path $snapshot 'ambient-main.cpp') $ambientText;Write-Utf8 (Join-Path $snapshot 'ambient-header.h') $ambientHeaderText
    $afterText=[IO.File]::ReadAllText($bindings[0].snapshot);$afterHeader=[IO.File]::ReadAllText($bindings[1].snapshot);$beforeText=[IO.File]::ReadAllText($bindings[2].snapshot);$beforeHeader=[IO.File]::ReadAllText($bindings[3].snapshot)
    $combined=[Text.StringBuilder]::new()
    foreach($name in $actualNames){
        $bound=Extract-Function $afterText $name;$ambient=Extract-Function $ambientText $name
        $path=Join-Path $snapshot ($name+'.after.fragment.cpp');Write-Utf8 $path $bound.text
        [void]$combined.Append("// Source-after native/main.cpp:$($bound.startLine) $name`n");[void]$combined.Append($bound.text);[void]$combined.Append("`n`n")
        $fragments.Add([pscustomobject]@{role='actual-after';name=$name;anchor=$bound.anchor;startLine=$bound.startLine;endLine=$bound.endLine;sha256=$bound.sha256;ambientSha256=$ambient.sha256;ambientStartLine=$ambient.startLine;exactTextMatch=($bound.text -ceq $ambient.text);path=$path})
    }
    Write-Utf8 (Join-Path $snapshot 'actual_producer_fragments.inc') $combined.ToString()
    $original=[Text.StringBuilder]::new();$originalEnum=Enum-Definition $beforeHeader
    [void]$original.Append($originalEnum);[void]$original.Append("`n`n")
    foreach($name in $originalNames){
        $bound=Extract-Function $beforeText $name;$path=Join-Path $snapshot ($name+'.before.fragment.cpp');Write-Utf8 $path $bound.text
        [void]$original.Append("// Source-before native/main.cpp:$($bound.startLine) $name`n");[void]$original.Append($bound.text);[void]$original.Append("`n`n")
        $fragments.Add([pscustomobject]@{role='reference-before';name=$name;anchor=$bound.anchor;startLine=$bound.startLine;endLine=$bound.endLine;sha256=$bound.sha256;path=$path})
    }
    Write-Utf8 (Join-Path $snapshot 'original_producer_fragments.inc') $original.ToString()
    $afterSourceName=Extract-Function $afterHeader 'sourceName';$currentSourceName=Extract-Function $ambientHeaderText 'sourceName'
    $sourceNameMatch=$afterSourceName.text -ceq $currentSourceName.text;$enumMatch=(Enum-Definition $afterHeader) -ceq (Enum-Definition $ambientHeaderText)
    Write-Utf8 (Join-Path $snapshot 'sourceName.after.fragment.cpp') $afterSourceName.text
    Write-Json (Join-Path $runRoot 'sourceName-and-enum-binding.json') @{sourceNameSha256=$afterSourceName.sha256;sourceNameStartLine=$afterSourceName.startLine;sourceNameEndLine=$afterSourceName.endLine;ambientSourceNameSha256=$currentSourceName.sha256;sourceNameExactMatch=$sourceNameMatch;sourceBeforeEnum=$originalEnum;sourceBeforeEnumSha256=Text-Hash $originalEnum;sourceAfterEnum=Enum-Definition $afterHeader;sourceAfterEnumSha256=Text-Hash (Enum-Definition $afterHeader);ambientEnumExactMatch=$enumMatch}
    $fragmentsMatch=@($fragments | Where-Object {$_.role -eq 'actual-after' -and -not $_.exactTextMatch}).Count -eq 0
    Write-Json (Join-Path $runRoot 'fragment-binding.json') $fragments.ToArray()
    if(-not $fragmentsMatch -or -not $sourceNameMatch -or -not $enumMatch){throw 'Relevant R3 function/enum differs from ambient source; no equivalence claim.'}
    $apiNames=@('GetLastError','SetLastError','GetTickCount64','GetCurrentThreadId','IsWindow','IsIconic','IsWindowVisible','ShowWindow','BringWindowToTop','SetForegroundWindow','SetActiveWindow','SetFocus','GetForegroundWindow','GetAncestor','SendInput','GetRawInputDeviceInfoW')
    $all=$combined.ToString()+$original.ToString()
    if($all -match 'SetWindowsHookEx|GetAsyncKeyState|RegisterRawInputDevices|CreateFile|DeviceIoControl|GetRawInputData|CallNextHookEx|OpenProcess|CreateProcess|AttachThreadInput'){throw 'Unexpected input/process/device API in fragments.'}
    Write-Json (Join-Path $runRoot 'synthetic-api-boundary.json') @{fakeDirectWin32UiInputCalls=$apiNames;fakeOtherDependencies=@('currentPowerGeneration','hideWindowAnimated (invocation/gate/order only)','g_ctrl MoveFocus (controlled result)','W2U (throws if unexpectedly reached)','appendDomainLog (memory sink)');actualHeader=$bindings[1];actualJson=$bindings[4];actualBeforeAfterDefinitionBodiesUnchanged=$true;actualSourceBeforeEnumUnchanged=$true;productionTranslationUnitsCompiled=$false;userLogsOrigin='mainline-export';fixtureOrigin='private-synthetic-R3-validation';historicalIncidentBuildEquivalenceClaimed=$false}
    $vcvars='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
    if(-not (Test-Path -LiteralPath $vcvars)){throw 'MSVC x64 compiler unavailable.'}
    $batchPath=Join-Path $runRoot 'build.cmd';$importsPath=Join-Path $runRoot 'binary-imports.txt';$object=Join-Path $runRoot 'keyboard_modifier_producer_r3_selftest.obj'
    $batch=@('@echo off',('call "'+$vcvars+'" >nul'),'if errorlevel 1 exit /b %errorlevel%',
        ('cl.exe /nologo /std:c++20 /utf-8 /EHsc /MT /Od /W4 /WX /I"'+$snapshot+'" "'+$snapshot+'\keyboard_modifier_producer_r3_selftest.cpp" /Fo"'+$object+'" /Fe"'+$binary+'" /link /INCREMENTAL:NO'),
        'if errorlevel 1 exit /b %errorlevel%',('dumpbin.exe /nologo /imports "'+$binary+'" >"'+$importsPath+'"'),'exit /b %errorlevel%')
    Write-Utf8 $batchPath (($batch -join "`r`n")+"`r`n")
    Write-Json (Join-Path $runRoot 'build-inputs.json') @{compilerEnvironment=$vcvars;compilerEnvironmentSha256=(Get-FileHash -LiteralPath $vcvars).Hash;command=$batchPath;flags='/std:c++20 /utf-8 /EHsc /MT /Od /W4 /WX';inputs=$bindings.ToArray();fragments=$fragments.ToArray();fullProductionMainCompiled=$false}
    $build=Run-PrivateChild (Join-Path $env:SystemRoot 'System32\cmd.exe') @('/d','/c',$batchPath) 'build'
    if($build.exitCode -ne 0){throw "Synthetic R3 build failed: $($build.stdout) $($build.stderr)"}
    $imports=[IO.File]::ReadAllText($importsPath)
    if($imports -match '(?i)USER32\.dll|SendInput|GetRawInputDeviceInfo|SetWindowsHookEx|RegisterRawInputDevices|GetAsyncKeyState|GetRawInputData|DeviceIoControl'){throw 'Unexpected real UI/input/device API import.'};$importsVerified=$true
    $run=Run-PrivateChild $binary @() 'synthetic-producer-selftest';$result=$run.stdout.Trim() | ConvertFrom-Json
    Write-Json (Join-Path $runRoot 'selftest-result.json') $result
    if($run.exitCode -ne 0 -or -not $result.passed -or $result.failed -ne 0 -or -not $result.allFragmentWin32UiInputCallsFaked){throw 'Actual before/after synthetic assertions failed; inspect named scenarios.'}
    $passed=$true
}catch{$failure=$_.Exception.Message;Write-Host $failure}
finally{
    $stable=$true
    $afterHashes=foreach($item in $bindings){
        $current=(Get-FileHash -LiteralPath $item.source -Algorithm SHA256).Hash;$copy=(Get-FileHash -LiteralPath $item.snapshot -Algorithm SHA256).Hash
        if($current -ne $item.sha256 -or $copy -ne $item.sha256){$stable=$false}
        [pscustomobject]@{source=$item.source;initialSha256=$item.sha256;currentSha256=$current;snapshotSha256=$copy}
    }
    $ambientAfter=[Collections.Generic.List[object]]::new()
    try{
        $ambientMainHashAfter=(Get-FileHash -LiteralPath $ambientMain).Hash;$ambientHeaderHashAfter=(Get-FileHash -LiteralPath $ambientHeader).Hash
        $ambientTextAfter=[IO.File]::ReadAllText($ambientMain);$ambientHeaderAfter=[IO.File]::ReadAllText($ambientHeader)
        foreach($item in $fragments | Where-Object {$_.role -eq 'actual-after'}){
            $current=Extract-Function $ambientTextAfter $item.name;$same=$current.sha256 -eq $item.sha256
            if(-not $same){$fragmentsMatch=$false}
            $ambientAfter.Add([pscustomobject]@{name=$item.name;sha256=$current.sha256;startLine=$current.startLine;endLine=$current.endLine;matchesSourceAfter=$same})
        }
        if($bindings.Count -ge 4){
            $boundHeader=[IO.File]::ReadAllText($bindings[1].snapshot)
            $sourceNameMatch=$sourceNameMatch -and (Extract-Function $ambientHeaderAfter 'sourceName').sha256 -eq (Extract-Function $boundHeader 'sourceName').sha256
            $enumMatch=$enumMatch -and (Enum-Definition $ambientHeaderAfter) -ceq (Enum-Definition $boundHeader)
        }
    }catch{$fragmentsMatch=$false;if(-not $failure){$failure=$_.Exception.Message}}
    $passed=$passed -and $stable -and $fragmentsMatch -and $sourceNameMatch -and $enumMatch -and $recordBindingValid -and $importsVerified
    Write-Json (Join-Path $runRoot 'source-hashes-after.json') @($afterHashes)
    Write-Json (Join-Path $runRoot 'ambient-fragments-after.json') $ambientAfter.ToArray()
    Write-Json (Join-Path $runRoot 'process-runs.json') $processRuns.ToArray()
    $children=foreach($child in $processRuns){
        $present=$false
        try{$owned=[Diagnostics.Process]::GetProcessById($child.pid);$present=-not $owned.HasExited;$owned.Dispose()}catch [ArgumentException]{}
        [pscustomobject]@{name=$child.name;pid=$child.pid;pidPresentAtAudit=$present;recordedHasExited=$child.hasExited;exitCode=$child.exitCode;lifetimeMs=$child.lifetimeMs;timedOut=$child.timedOut}
    }
    $audit=[ordered]@{auditAt=(Get-Date -Format o);ownChildCleanup=@($children);allOwnPidsAbsent=(@($children | Where-Object {$_.pidPresentAtAudit}).Count -eq 0);scope='Own child PID cleanup audit only; no other process termination or product execution.'}
    Write-Json (Join-Path $runRoot 'process-cleanup-audit.json') $audit
    $summary=[ordered]@{
        passed=$passed;failure=$failure;boundInputsStable=$stable;sourceChangeRecordHashesValidated=$recordBindingValid
        sourceAfterMain=$frozenMain;sourceAfterMainSha256=if($bindings.Count){$bindings[0].sha256}else{$null}
        sourceBeforeMainSha256=if($bindings.Count -gt 2){$bindings[2].sha256}else{$null}
        sourceAfterHeaderSha256=if($bindings.Count -gt 1){$bindings[1].sha256}else{$null}
        sourceBeforeHeaderSha256=if($bindings.Count -gt 3){$bindings[3].sha256}else{$null}
        ambientMain=$ambientMain;ambientMainFullSha256Before=$ambientMainHashBefore;ambientMainFullSha256After=$ambientMainHashAfter
        ambientHeaderFullSha256Before=$ambientHeaderHashBefore;ambientHeaderFullSha256After=$ambientHeaderHashAfter
        allSevenSourceAfterMainFunctionsMatchAmbient=$fragmentsMatch;sourceNameMatchesAmbient=$sourceNameMatch;sourceIdEnumMatchesAmbient=$enumMatch
        ambientWholeFileEqualityClaimed=$false;ambientWholeFileStabilityRequired=$false;referenceDefinitionsCopiedFromSourceBefore=$true
        sourceBinding=$bindings.ToArray();fragments=$fragments.ToArray();result=$result;processRuns=$processRuns.ToArray();processCleanup=$audit
        noUser32OrInputDeviceImports=$importsVerified;binary=$binary;outputRoot=$runRoot
        scope='Actual R3 before/after caller fragments under synthetic Win32/UI/input/context/log dependencies, actual R3 recorder/sourceName and JSON serializer. No full production translation unit or OS input observation.'
        fixtureOrigin='private-synthetic-R3-validation';userLogsOrigin='mainline-export';historicalIncidentBuildEquivalenceClaimed=$false
        realInputDeviceHookHostProductProbeServiceExecuted=$false;existingInputBehaviorChanged=$false;processesBoundedToMs=25000
        modifiedFiles=@('tools/keyboard_modifier_producer_r3_selftest.cpp','tools/verify-keyboard-modifier-producer-r3.ps1')
    }
    Write-Json (Join-Path $runRoot 'verification-summary.json') $summary
    $report=[Text.StringBuilder]::new()
    [void]$report.AppendLine("R3 producer synthetic verification: passed=$passed; failure=$failure.")
    [void]$report.AppendLine('')
    [void]$report.AppendLine("Source-after main: $frozenMain")
    [void]$report.AppendLine("Source-after main SHA256: $($summary.sourceAfterMainSha256)")
    [void]$report.AppendLine("Source-before main SHA256: $($summary.sourceBeforeMainSha256)")
    [void]$report.AppendLine("Source-after header SHA256: $($summary.sourceAfterHeaderSha256)")
    [void]$report.AppendLine("Source-before header SHA256: $($summary.sourceBeforeHeaderSha256)")
    [void]$report.AppendLine("Ambient main before/after SHA256: $ambientMainHashBefore / $ambientMainHashAfter")
    [void]$report.AppendLine("Ambient header before/after SHA256: $ambientHeaderHashBefore / $ambientHeaderHashAfter")
    [void]$report.AppendLine("Relevant main functions match ambient: $fragmentsMatch; sourceName match: $sourceNameMatch; SourceId match: $enumMatch. Whole-file equivalence is not claimed or required. Source snapshots match the parent's source-change record hashes: $recordBindingValid.")
    [void]$report.AppendLine('')
    [void]$report.AppendLine('The actual original focus, WinD, WinTab and foreground-check definitions from source-before are compiled in a separate namespace; actual R3 caller definitions are compared against them. No handwritten reference implementation is used. Tests compare every keyboard INPUT field, array/order/call counts, UI call sequence/arguments/LastError-at-entry, returns and exact exceptions.')
    [void]$report.AppendLine('')
    [void]$report.AppendLine('Every directly called Win32/UI/input API is macro redirected. Win32/COM focus outcomes are controlled fakes. hideWindowAnimated is a traced synthetic dependency: its invocation, gate and ordering are compared, not its full lifecycle implementation. Raw-name/conversion calls throw if reached; all producer scenarios assert neither is reached. Binary imports exclude USER32 and input/device APIs.')
    [void]$report.AppendLine('')
    [void]$report.AppendLine('User logs are mainline exports. This fixture and its outputs are private synthetic candidate validation and do not establish OS input delivery, modifier ownership, Steam mapping, SD keyboard-IN receipts, Host behavior, OEM correctness or equivalence to the historical incident binary.')
    [void]$report.AppendLine('')
    if($result){
        [void]$report.AppendLine("Executed $($result.scenarios.Count) named scenarios, $($result.checks) assertions, $($result.failed) failures; actual source-before caller runs=$($result.referenceCallerRuns), R3 caller runs=$($result.candidateCallerRuns).")
        [void]$report.AppendLine('')
        [void]$report.AppendLine('| Scenario | Assertions | Failures |');[void]$report.AppendLine('|---|---:|---:|')
        foreach($case in $result.scenarios){[void]$report.AppendLine("| $($case.name) | $($case.checks) | $($case.failed) |")}
        [void]$report.AppendLine('')
        foreach($limit in $result.limits){[void]$report.AppendLine("- $limit")}
    }
    [void]$report.AppendLine('')
    foreach($child in $processRuns){[void]$report.AppendLine("Own child $($child.name): PID $($child.pid), lifetime $($child.lifetimeMs)ms, exit $($child.exitCode), hasExited=$($child.hasExited), timedOut=$($child.timedOut), CreateNoWindow=true, UseShellExecute=false; Process disposed.")}
    [void]$report.AppendLine("Own child PIDs absent at cleanup audit: $($audit.allOwnPidsAbsent). Deadline per child: 25 seconds; timeout handler targets only its own child tree.")
    [void]$report.AppendLine('')
    [void]$report.AppendLine('Only two new R3 tool files and the authorized producers output directory were written. Production source, old R2 tests/evidence, dependency source files and packages were read only. Full build and global source inventory remain parent scope.')
    Write-Utf8 (Join-Path $runRoot 'verification-report.md') $report.ToString()
    $artifacts=foreach($file in Get-ChildItem -LiteralPath $runRoot -Recurse -File | Where-Object {$_.Name -ne 'artifact-binding.json'}){
        [pscustomobject]@{path=$file.FullName;bytes=$file.Length;sha256=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash}
    }
    Write-Json (Join-Path $runRoot 'artifact-binding.json') @($artifacts)
    [IO.File]::WriteAllText((Join-Path $OutputRoot 'producer-verification-handoff.json'),(ConvertTo-Json -InputObject @{passed=$passed;summary=(Join-Path $runRoot 'verification-summary.json');report=(Join-Path $runRoot 'verification-report.md');artifactBinding=(Join-Path $runRoot 'artifact-binding.json');failure=$failure} -Depth 8),$utf8)
    Write-Host (ConvertTo-Json -InputObject @{passed=$passed;failure=$failure;summary=(Join-Path $runRoot 'verification-summary.json');report=(Join-Path $runRoot 'verification-report.md');functionsMatchAmbient=$fragmentsMatch;scenarios=if($result){$result.scenarios.Count}else{0};checks=if($result){$result.checks}else{0};failed=if($result){$result.failed}else{$null}} -Depth 6)
}
if(-not $passed){exit 1}
