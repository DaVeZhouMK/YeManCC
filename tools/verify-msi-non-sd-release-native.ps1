[CmdletBinding()]
param([Parameter(Mandatory = $true)][string]$OutputRoot)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repo = Split-Path -Parent $PSScriptRoot
$out = [IO.Path]::GetFullPath($OutputRoot)
[void][IO.Directory]::CreateDirectory($out)
$utf8 = [Text.UTF8Encoding]::new($false)
function Save-Json([string]$Name, $Value) {
    [IO.File]::WriteAllText((Join-Path $out $Name), ($Value | ConvertTo-Json -Depth 30), $utf8)
}
function Binding([string]$Path) {
    [ordered]@{ path = [IO.Path]::GetFullPath($Path); sha256 = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash }
}
# Mask comments and literals while retaining every source offset. Raw C++ strings
# are recognized so braces in unrelated embedded scripts cannot break extraction.
$tokenPattern = '(?s)//[^\r\n]*|/\*.*?\*/|(?:u8|u|U|L)?R"(?<raw>[^ ()\\\t\r\n]{0,16})\(.*?\)\k<raw>"|"(?:\\.|[^"\\])*"|''(?:\\.|[^''\\])*'''
function Code-Mask([string]$Text) {
    [regex]::Replace($Text, $tokenPattern, [Text.RegularExpressions.MatchEvaluator]{
        param($m)
        [regex]::Replace($m.Value, '[^\r\n]', ' ')
    })
}
function Without-Comments([string]$Text) {
    [regex]::Replace($Text, $tokenPattern, [Text.RegularExpressions.MatchEvaluator]{
        param($m)
        if ($m.Value.StartsWith('//') -or $m.Value.StartsWith('/*')) {
            [regex]::Replace($m.Value, '[^\r\n]', ' ')
        } else { $m.Value }
    })
}
function Block-End([string]$Mask, [int]$Open) {
    $depth = 0
    for ($i = $Open; $i -lt $Mask.Length; ++$i) {
        if ($Mask[$i] -eq '{') { ++$depth }
        elseif ($Mask[$i] -eq '}') { --$depth; if ($depth -eq 0) { return $i + 1 } }
    }
    throw 'Unbalanced source block; refusing extraction.'
}
function Extract-Function([string]$Text, [string]$Mask, [string]$Name, [string]$ReturnType) {
    $pattern = '\bstatic\s+' + $ReturnType + '\s+' + [regex]::Escape($Name) + '\s*\([^;{}]*\)\s*(?:noexcept\s*)?\{'
    $found = [regex]::Matches($Mask, $pattern)
    if ($found.Count -ne 1) { throw "Expected one definition of $Name; found $($found.Count)." }
    $start = $found[0].Index
    $open = $start + $found[0].Length - 1
    $end = Block-End $Mask $open
    [pscustomobject]@{ start = $start; end = $end; open = $open; text = $Text.Substring($start, $end - $start); mask = $Mask.Substring($start, $end - $start); signature = $Text.Substring($start, $open - $start).Trim() }
}
function Declaration($Function, [string]$Name) {
    $found = [regex]::Matches($Function.mask, '\bconst\s+(?:bool|std::string)\s+' + [regex]::Escape($Name) + '\s*=')
    if ($found.Count -ne 1) { throw "Expected one declaration of $Name." }
    $start = $found[0].Index
    $end = $Function.mask.IndexOf(';', $start)
    if ($end -lt 0) { throw "Missing declaration terminator for $Name." }
    $Function.text.Substring($start, $end - $start + 1)
}
function Source-Line([string]$Text, [int]$Offset) {
    1 + ([regex]::Matches($Text.Substring(0, $Offset), '\n')).Count
}
$main = Join-Path $repo 'native\main.cpp'
$test = Join-Path $repo 'tools\msi_non_sd_release_native_selftest.cpp'
$jsonHeader = Join-Path $repo 'deps\json\json.hpp'
$inputs = @($main, $test, $PSCommandPath, $jsonHeader)
$before = @($inputs | ForEach-Object { Binding $_ })
Save-Json 'source-bindings-before.json' $before
$text = [IO.File]::ReadAllText($main)
$mask = Code-Mask $text
$logger = Extract-Function $text $mask 'msiNonSdReleaseReceiptLog' 'void'
$wrapper = Extract-Function $text $mask 'inputHostCommandResultLog' 'void'
$request = Extract-Function $text $mask 'inputHostRequestPreparedLocked' 'bool'
$sink = Extract-Function $text $mask 'appendNativeLifecycleLog' 'void'
[IO.File]::WriteAllText((Join-Path $out 'actual-msi-non-sd-release-receipt-log.inc'), $logger.text, $utf8)
[IO.File]::WriteAllText((Join-Path $out 'actual-input-host-command-result-log.inc'), $wrapper.text, $utf8)
$declarations = @('helloCommand', 'tupleMatches', 'hostInstanceId', 'identityOk', 'requiresTargetPnp', 'targetPnpAdmitted')
$parts = @{}
foreach ($name in $declarations) { $parts[$name] = Declaration $request $name }
$business = [regex]::Matches($request.mask, 'if\s*\(\s*!response\.value\([^;{}]*?\)\s*\{')
if ($business.Count -ne 1) { throw 'Cannot uniquely extract the actual business acceptance predicate.' }
$businessText = $request.text.Substring($business[0].Index, $business[0].Length - 1).TrimEnd()
$predicate = $businessText.Substring($businessText.IndexOf('(') + 1)
$predicate = $predicate.Substring(0, $predicate.LastIndexOf(')'))
$control = @"
// Actual production declarations and business rejection expression, assembled
// into a pure evaluator. This does not execute the Native request/transport.
static FakeDecision actualReceiptControl(const json& response, const char* command,
                                        const char* expectedStatus) {
    $($parts['helloCommand'])
    $($parts['tupleMatches'])
    $($parts['hostInstanceId'])
    $($parts['identityOk'])
    const bool tupleAccepted = tupleMatches && identityOk;
    $($parts['requiresTargetPnp'])
    $($parts['targetPnpAdmitted'])
    const bool businessRejected = $predicate;
    return {tupleAccepted, tupleAccepted && !businessRejected};
}
"@
[IO.File]::WriteAllText((Join-Path $out 'actual-receipt-control.inc'), $control, $utf8)
$loggerCalls = [regex]::Matches($request.mask, '\bmsiNonSdReleaseReceiptLog\s*\(\s*(?<row>rejectedLine|acceptedLine)\s*,\s*response\s*,\s*command\s*,\s*true\s*\)\s*;')
if ($loggerCalls.Count -ne 2) { throw 'Expected exactly the accepted/rejected tuple-verified logger calls.' }
$handoff = [Text.StringBuilder]::new()
[void]$handoff.AppendLine('// Production sink signature, with its body replaced by a fake; no file/device sink is run.')
[void]$handoff.AppendLine($sink.signature + ' {')
[void]$handoff.AppendLine('    ++g_calls.sinkBody;')
[void]$handoff.AppendLine('    fakeLifecycleSinkBody(event, detail);')
[void]$handoff.AppendLine('}')
[void]$handoff.AppendLine('#include "actual-input-host-command-result-log.inc"')
$callEvidence = @()
foreach ($call in $loggerCalls) {
    $row = $call.Groups['row'].Value
    $afterCall = $call.Index + $call.Length
    $sinkCall = [regex]::Match($request.mask.Substring($afterCall), ('^\s*inputHostCommandResultLog\s*\(\s*' + [regex]::Escape($row) + '\s*\)\s*;'))
    if (-not $sinkCall.Success) { throw "No adjacent actual wrapper call after $row logger." }
    $statement = $request.text.Substring($afterCall + $sinkCall.Index, $sinkCall.Length).Trim()
    [void]$handoff.AppendLine("static void actual$($row)Handoff(json& $row) {")
    [void]$handoff.AppendLine('    ++g_calls.wrapper;')
    [void]$handoff.AppendLine('    ' + $statement)
    [void]$handoff.AppendLine('}')
    $callEvidence += [ordered]@{ row = $row; line = (Source-Line $text ($request.start + $call.Index)); logger = $request.text.Substring($call.Index, $call.Length); wrapper = $statement }
}
[IO.File]::WriteAllText((Join-Path $out 'actual-lifecycle-handoff.inc'), $handoff.ToString(), $utf8)
$tupleGuard = [regex]::Match($request.mask, 'if\s*\(\s*!tupleMatches\s*\|\|\s*!identityOk\s*\)\s*\{')
if (-not $tupleGuard.Success) { throw 'Missing original tuple/identity rejection guard.' }
$tupleEnd = Block-End $request.mask ($tupleGuard.Index + $tupleGuard.Length - 1)
$businessEnd = Block-End $request.mask ($business[0].Index + $business[0].Length - 1)
$refs = @()
$fieldPattern = '"(?:msiNonSdReleaseEvidence(?:Version|Present|TupleAccepted)?|legacyTargetPnpFieldsAreFreshReleaseProof)"'
$nativeFiles = @(Get-ChildItem -LiteralPath (Join-Path $repo 'native') -File -Recurse | Where-Object { $_.Extension -in @('.cpp', '.h', '.hpp') })
foreach ($file in $nativeFiles) {
    $raw = if ($file.FullName -eq $main) { $text } else { [IO.File]::ReadAllText($file.FullName) }
    if ($raw -notmatch 'msiNonSdReleaseEvidence|legacyTargetPnpFieldsAreFreshReleaseProof') { continue }
    $clean = Without-Comments $raw
    foreach ($match in [regex]::Matches($clean, $fieldPattern)) {
        $inside = $file.FullName -eq $main -and $match.Index -ge $logger.start -and $match.Index -lt $logger.end
        $refs += [ordered]@{ path = $file.FullName; line = (Source-Line $raw $match.Index); field = $match.Value; insideLogger = $inside }
    }
}
$checks = @(
    [ordered]@{ name = 'actual command-result wrapper accepts const row by reference'; passed = ($wrapper.signature -match '\bvoid\s+inputHostCommandResultLog\s*\(\s*const\s+json\s*&\s*row\s*\)' -and $wrapper.signature -notmatch '\bnoexcept\b') },
    [ordered]@{ name = 'wrapper branches on existing opt-in flag and owns catch-all without querying board authority'; passed = ($wrapper.mask -match 'if\s*\(\s*!?\s*g_inputHostMsiStageRequested\s*\)' -and $wrapper.mask -match 'catch\s*\(\s*\.\.\.\s*\)' -and $wrapper.mask -notmatch '\bmsiClawKnownBoardAuthority\b') },
    [ordered]@{ name = 'both command-result handoffs use the extracted wrapper'; passed = ([regex]::Matches($request.mask, '\binputHostCommandResultLog\s*\(\s*(?:rejectedLine|acceptedLine)\s*\)\s*;').Count -eq 2) },
    [ordered]@{ name = 'actual helper has const response, void result and noexcept'; passed = ($logger.signature -match 'void\s+msiNonSdReleaseReceiptLog\(json&\s*row,\s*const json&\s*response,\s*const char\*\s*command,\s*bool\s*tupleAccepted\)\s*noexcept') },
    [ordered]@{ name = 'helper owns a catch-all exception boundary'; passed = ($logger.mask -match 'catch\s*\(\s*\.\.\.\s*\)') },
    [ordered]@{ name = 'flag and cached board authority gate precede logging'; passed = ($logger.mask -match 'if\s*\(\s*!g_inputHostMsiStageRequested\s*\|\|\s*!msiClawKnownBoardAuthority\(\)\s*\|\|\s*!command\s*\)\s*return') },
    [ordered]@{ name = 'tuple/identity failures return before either logger call'; passed = ($tupleEnd -lt $loggerCalls[0].Index -and $tupleEnd -lt $loggerCalls[1].Index -and $request.mask.Substring($tupleGuard.Index, $tupleEnd - $tupleGuard.Index) -match 'return\s+false\s*;') },
    [ordered]@{ name = 'business rejection predicate precedes both logger calls'; passed = ($business[0].Index -lt $loggerCalls[0].Index -and $business[0].Index -lt $loggerCalls[1].Index) },
    [ordered]@{ name = 'rejected logger is inside business rejection block and retains false return'; passed = ($loggerCalls[0].Groups['row'].Value -eq 'rejectedLine' -and $loggerCalls[0].Index -lt $businessEnd -and $request.mask.Substring($loggerCalls[0].Index, $businessEnd - $loggerCalls[0].Index) -match 'return\s+false\s*;') },
    [ordered]@{ name = 'accepted logger follows rejection block and retains true return'; passed = ($loggerCalls[1].Groups['row'].Value -eq 'acceptedLine' -and $loggerCalls[1].Index -gt $businessEnd -and $request.mask.Substring($loggerCalls[1].Index) -match 'return\s+true\s*;\s*\}\s*$') },
    [ordered]@{ name = 'all direct diagnostic field keys in native sources are confined to logger'; passed = ($refs.Count -gt 0 -and @($refs | Where-Object { -not $_.insideLogger }).Count -eq 0) }
)
$static = [ordered]@{
    scope = 'Read-only native C++ direct field references and actual request callsite ordering; no whole-TU compile or product execution.'
    passed = (@($checks | Where-Object { -not $_.passed }).Count -eq 0)
    checks = $checks; helperLine = (Source-Line $text $logger.start); wrapperLine = (Source-Line $text $wrapper.start); calls = $callEvidence
    nativeFilesScanned = $nativeFiles.Count; directFieldReferences = $refs
    sinkSignature = $sink.signature
    exceptionScope = 'Actual helper and actual wrapper catch boundaries are tested; production sink signature is retained, its body replaced by a fake that can throw. Sink parameter-copy exceptions occur before entering that fake body.'
    helloScope = 'HELLO logs a capability version after MSI/opt-in gating; non-SD persona gating applies to RELEASE_TARGET/SHUTDOWN evidence.'
}
Save-Json 'source-review.json' $static
$generated = @('actual-msi-non-sd-release-receipt-log.inc', 'actual-input-host-command-result-log.inc', 'actual-receipt-control.inc', 'actual-lifecycle-handoff.inc')
$generatedBindings = @($generated | ForEach-Object { Binding (Join-Path $out $_) })
$mainHash = $before[0].sha256
$helperHash = $generatedBindings[0].sha256
$wrapperHash = $generatedBindings[1].sha256
$meta = "#pragma once`ninline constexpr char kMainSha256[] = `"$mainHash`";`ninline constexpr char kHelperSha256[] = `"$helperHash`";`ninline constexpr char kWrapperSha256[] = `"$wrapperHash`";`n"
[IO.File]::WriteAllText((Join-Path $out 'actual-source-metadata.hpp'), $meta, $utf8)
Save-Json 'extraction.json' ([ordered]@{ main = $before[0]; generated = $generatedBindings; declarations = $declarations; businessRejection = $predicate; loggerCopiedVerbatim = $true; wrapperCopiedVerbatim = $true; controlScope = 'Actual declarations and predicate assembled in a fake evaluator; no transport, observer or Native request is executed.' })
$vcvars = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
$envLines = & $env:ComSpec /d /s /c "`"$vcvars`" >nul && set"
if ($LASTEXITCODE -ne 0) { throw 'Existing vcvars64 environment initialization failed.' }
foreach ($line in $envLines) {
    if ($line -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process') }
}
$binary = Join-Path $out 'msi-non-sd-release-native-selftest.exe'
$report = Join-Path $out 'report.json'
$buildArgs = @('/nologo', '/std:c++20', '/utf-8', '/EHsc', '/MT', '/O2', '/DNDEBUG', '/W4', '/WX', '/permissive-', "/I$out", "/I$repo\deps\json", $test, "/Fo$out\native-logger.obj", "/Fe$binary")
Save-Json 'build-command.json' ([ordered]@{ environment = $vcvars; executable = (Get-Command cl.exe).Source; arguments = $buildArgs; scope = 'Standalone pure fake C++ selftest only; extracted source includes; never native/main.cpp TU.' })
& cl.exe @buildArgs *> (Join-Path $out 'build.log')
$buildExit = $LASTEXITCODE
$runExit = $null
$testSummary = $null
if ($buildExit -eq 0) {
    Save-Json 'run-command.json' ([ordered]@{ executable = $binary; arguments = @($report) })
    & $binary $report *> (Join-Path $out 'run.log')
    $runExit = $LASTEXITCODE
    if (Test-Path -LiteralPath $report) { $testSummary = Get-Content -LiteralPath $report -Raw | ConvertFrom-Json }
}
$after = @($inputs | ForEach-Object { Binding $_ })
Save-Json 'source-bindings-after.json' $after
$stable = $true
for ($i = 0; $i -lt $before.Count; ++$i) { if ($before[$i].sha256 -ne $after[$i].sha256) { $stable = $false } }
$binaryBinding = if ($buildExit -eq 0) { Binding $binary } else { $null }
$passed = $static.passed -and $stable -and $buildExit -eq 0 -and $runExit -eq 0
Save-Json 'validation-summary.json' ([ordered]@{
    utc = [DateTime]::UtcNow.ToString('o'); passed = $passed
    scope = 'Read-only source verification + verbatim extracted logger/wrapper, actual control expressions and handoff statements with all dependencies faked.'
    sourceStable = $stable; source = $before; extraction = $generatedBindings
    staticReviewPassed = $static.passed; sourceReview = (Join-Path $out 'source-review.json')
    buildExitCode = $buildExit; runExitCode = $runExit; binary = $binaryBinding; report = $report
    tests = if ($null -ne $testSummary) { [ordered]@{ caseCount = $testSummary.caseCount; checkCount = $testSummary.checkCount; failureCount = $testSummary.failureCount; apiCallTotals = $testSummary.apiCallTotals; failedCases = @($testSummary.cases | Where-Object { -not $_.passed }) } } else { $null }
    productExecuted = $false; wholeNativeTuCompiled = $false; liveDeviceApis = 0; hardwareSdkCalls = 0; registryCalls = 0; serviceCalls = 0
    limits = 'No hardware/Host/observer execution. Direct key/reference audit is source evidence, not an arbitrary dynamic-code proof. Fake sink replaces production sink body; standard report file IO is the only executed IO.'
})
if (-not $passed) {
    Write-Output "MSI_NON_SD_RELEASE_NATIVE_VALIDATION_FAILED build=$buildExit run=$runExit stable=$stable out=$out"
    exit 1
}
Write-Output "MSI_NON_SD_RELEASE_NATIVE_PURE_FAKE_OK $out"
exit 0