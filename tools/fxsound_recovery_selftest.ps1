[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$EvidenceDir,
    [switch]$Inventory,
    [switch]$NegativeControls,
    [switch]$ConfigStormOnly,
    [switch]$ProcessInventoryOnly,
    [string]$ProcessInventoryRepoRoot,
    [string]$OwnedPrivateRoot,
    [string]$SourceOverride,
    [string]$MainGetStateHunk,
    [switch]$ProcessInventoryPrepareOnly,
    [string]$ExecutionBudgetContext,
    [string]$ExecutionBudgetNonce
)
$ErrorActionPreference = 'Stop'
# Optional software-only leaf of this original consumer. Its caller owns the Job/deadline.
if ($ProcessInventoryOnly) {
    if ($Inventory -or $NegativeControls -or $ConfigStormOnly) { throw 'ProcessInventoryOnly cannot combine with original selections' }
    if (-not $OwnedPrivateRoot) { throw 'ProcessInventoryOnly requires an explicit owned private root' }
    $selectedRepo = if ($ProcessInventoryRepoRoot) { [IO.Path]::GetFullPath($ProcessInventoryRepoRoot) } else { [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')) }
    $leafArguments = @((Join-Path $PSScriptRoot 'fxsound_process_inventory_selftest.mjs'),'--repo-root',$selectedRepo,'--owned-private-root',$OwnedPrivateRoot,'--output-dir',$EvidenceDir)
    if ($SourceOverride) { $leafArguments += @('--source-override',$SourceOverride) }
    if ($MainGetStateHunk) { $leafArguments += @('--main-hunk',$MainGetStateHunk) }
    if ($ProcessInventoryPrepareOnly) { $leafArguments += '--prepare' }
    else {
        if (-not $ExecutionBudgetContext -or -not $ExecutionBudgetNonce) { throw 'Execution requires the existing outer execution-budget ownerJob context and nonce' }
        $leafArguments += @('--execute','--budget-context',$ExecutionBudgetContext,'--budget-nonce',$ExecutionBudgetNonce)
    }
    & node @leafArguments
    if ($LASTEXITCODE -ne 0) { throw "ProcessInventoryOnly failed ($LASTEXITCODE)" }
    return
}
if ($ProcessInventoryRepoRoot -or $OwnedPrivateRoot -or $SourceOverride -or $MainGetStateHunk -or $ProcessInventoryPrepareOnly -or $ExecutionBudgetContext -or $ExecutionBudgetNonce) { throw 'Process inventory options require ProcessInventoryOnly' }
if($ConfigStormOnly -and $NegativeControls){throw 'ConfigStormOnly and original recovery NegativeControls are separate selections'}
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$evidence = [IO.Path]::GetFullPath($EvidenceDir)
if ($evidence.StartsWith($repo.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Evidence must be outside source' }
if (Test-Path -LiteralPath $evidence) { throw 'Evidence directory must be new' }
New-Item -ItemType Directory -Path $evidence | Out-Null
$vcRoot = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC'
$compiler = Get-ChildItem -LiteralPath (Join-Path $vcRoot 'Tools\MSVC') -Directory | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
$sdkRoot = 'C:\Program Files (x86)\Windows Kits\10'
$sdk = Get-ChildItem -LiteralPath (Join-Path $sdkRoot 'Include') -Directory | Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'ucrt\crtdbg.h') } | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
if (-not $compiler -or -not $sdk) { throw 'MSVC and Windows SDK are required' }
$include = @((Join-Path $compiler.FullName 'include')) + @('ucrt','shared','um','winrt' | ForEach-Object { Join-Path $sdk.FullName $_ })
$libraries = @((Join-Path $compiler.FullName 'lib\x64'),(Join-Path $sdkRoot "Lib\$($sdk.Name)\ucrt\x64"),(Join-Path $sdkRoot "Lib\$($sdk.Name)\um\x64"))
$inputs = @('native/fxsound_recovery.h','native/fxsound_recovery_policy.h','native/fxsound_control.h','native/process_inventory.h','tools/fxsound_recovery_selftest.cpp','tools/fxsound_recovery_selftest.ps1')
$hashes = [ordered]@{}
foreach ($file in $inputs) { $hashes[$file] = (Get-FileHash -LiteralPath (Join-Path $repo $file) -Algorithm SHA256).Hash.ToLowerInvariant() }
function Invoke-RecoveryBuild([string]$Root,[string]$Name,[bool]$ExpectPass) {
    $output = Join-Path $evidence $Name
    New-Item -ItemType Directory -Path $output | Out-Null
    $script = @"
@echo off
call "$vcRoot\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
set "INCLUDE=$($include -join ';')"
set "LIB=$($libraries -join ';')"
cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /DUNICODE /D_UNICODE "$Root\tools\fxsound_recovery_selftest.cpp" /Fo"$output\test.obj" /Fe"$output\test.exe" /link ole32.lib oleaut32.lib uuid.lib advapi32.lib
"@
    $batch = Join-Path $output 'compile.cmd'
    [IO.File]::WriteAllText($batch,$script,[Text.Encoding]::Default)
    & $env:ComSpec /d /c $batch *> (Join-Path $output 'compile.log')
    if ($LASTEXITCODE -ne 0) { throw "Compilation failed: $Name" }
    if($ConfigStormOnly){& (Join-Path $output 'test.exe') --config-storm (Join-Path $output 'owned-config') *> (Join-Path $output 'test.log')}else{& (Join-Path $output 'test.exe') *> (Join-Path $output 'test.log')}
    $exitCode = $LASTEXITCODE
    if (($ExpectPass -and $exitCode -ne 0) -or (-not $ExpectPass -and $exitCode -eq 0)) { throw "Unexpected test result: $Name ($exitCode)" }
    Write-Host ([IO.File]::ReadAllText((Join-Path $output 'test.log')).Trim())
    return [pscustomobject]@{name=$Name;exitCode=$exitCode;expectedPass=$ExpectPass}
}
$results = @()
$results += Invoke-RecoveryBuild -Root $repo -Name 'production' -ExpectPass $true
if ($Inventory) {
    & (Join-Path $evidence 'production\test.exe') --inventory *> (Join-Path $evidence 'audio-inventory.txt')
    if ($LASTEXITCODE -ne 0) { throw 'Read-only audio inventory failed' }
}
if ($NegativeControls) {
    $mutations = @(
        @{name='must-not-rearm';from='if (finished) return results;';to='if (finished) { finished = false; started_ = false; }'},
        @{name='must-wait-three-seconds';from='confirmationMs = 3000';to='confirmationMs = 0'},
        @{name='must-cancel-on-process-start';from='s.process != ProcessState::stopped || !s.hasFxDefault()';to='false || !s.hasFxDefault()'},
        @{name='must-skip-virtual-output';from='return d && !d->fx && d->usable;';to='return d != nullptr;'},
        @{name='must-not-treat-read-error-as-missing';header='native\fxsound_control.h';from='reason=L"FxSound 配置读取失败（文件被占用或权限不足），未修改";return false;';to='return true;'},
        @{name='must-not-reset-empty-existing-config';header='native\fxsound_control.h';from='const bool recover=before.kind==FxConfigImage::Kind::oversized||(before.kind==FxConfigImage::Kind::readable&&!valid);';to='const bool recover=false;'}
    )
    foreach ($mutation in $mutations) {
        $root = Join-Path $evidence ('mutant-'+$mutation.name)
        New-Item -ItemType Directory -Path (Join-Path $root 'native'),(Join-Path $root 'tools') | Out-Null
        foreach ($file in $inputs | Where-Object { $_ -notlike '*.ps1' }) { Copy-Item -LiteralPath (Join-Path $repo $file) -Destination (Join-Path $root $file) }
        $mutationHeader = if ($mutation.header) { $mutation.header } else { 'native\fxsound_recovery_policy.h' }
        $header = Join-Path $root $mutationHeader
        $text = [IO.File]::ReadAllText($header)
        if (-not $text.Contains($mutation.from)) { throw 'Mutation anchor missing' }
        [IO.File]::WriteAllText($header,$text.Replace($mutation.from,$mutation.to))
        $results += Invoke-RecoveryBuild -Root $root -Name $mutation.name -ExpectPass $false
    }
}
foreach ($file in $inputs) {
    if ((Get-FileHash -LiteralPath (Join-Path $repo $file) -Algorithm SHA256).Hash.ToLowerInvariant() -ne $hashes[$file]) { throw 'Source changed during tests' }
}
[ordered]@{pass=$true;configurationOnly=[bool]$ConfigStormOnly;sourceHashes=$hashes;results=$results;inventoryReadOnly=[bool]$Inventory;audioDefaultsWritten=$false;installedApplicationMutated=$false;scope='production recovery/control consumers with memory audio/process ports, isolated real configuration/preset files and optional read-only live audio inventory'} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $evidence 'result.json') -Encoding utf8
if($ConfigStormOnly){Write-Output 'PASS FxSound configuration storm'}else{Write-Output 'PASS FxSound startup recovery'}
