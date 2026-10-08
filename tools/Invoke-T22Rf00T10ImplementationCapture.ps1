[CmdletBinding()]
param(
    [string]$RepositoryRoot = 'G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC',
    [string]$DocsRoot = 'G:\YeManCC-Work\Docs',
    [string]$HcRoot = 'G:\YeManCC-Work\Isolated\HC-Candidate-0.32.4.0-06c0b954-20260902',
    [string]$Prefix = 'T22-RF00-T10-I-CURRENT-SOURCE-20260906',
    [string]$ManifestId = 'T22-RF00-20260906-T10-I-HC-ROUTE-STATIC-SNAPSHOT'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Write-Utf8Lf([string]$Path, [string]$Text) {
    $normalized = $Text -replace "`r`n", "`n" -replace "`r", "`n"
    [System.IO.File]::WriteAllText($Path, $normalized, [System.Text.UTF8Encoding]::new($false))
}

function Get-GitOutput([string[]]$Arguments) {
    $psi = [System.Diagnostics.ProcessStartInfo]::new()
    $psi.FileName = 'git.exe'
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.StandardOutputEncoding = [System.Text.UTF8Encoding]::new($false)
    # Windows PowerShell 5.1 lacks ProcessStartInfo.ArgumentList. Git inputs
    # here are fixed local paths/verbs; quote every argument for the legacy
    # Arguments string instead of shelling out through cmd.exe.
    $psi.Arguments = (($Arguments | ForEach-Object {
        '"' + ([string]$_).Replace('"', '\"') + '"'
    }) -join ' ')
    $process = [System.Diagnostics.Process]::new()
    $process.StartInfo = $psi
    if (-not $process.Start()) { throw "Unable to start git.exe" }
    $stdout = $process.StandardOutput.ReadToEnd()
    $stderr = $process.StandardError.ReadToEnd()
    $process.WaitForExit()
    if ($process.ExitCode -ne 0) { throw "git failed ($($process.ExitCode)): $stderr" }
    return [pscustomobject]@{ stdout = $stdout; stderr = $stderr; exitCode = $process.ExitCode }
}

function Get-OrdinalSorted([object[]]$Items) {
    $list = [System.Collections.Generic.List[object]]::new()
    foreach ($item in $Items) { $list.Add($item) }
    $comparison = [System.Comparison[object]]{
        param($left, $right)
        $scope = [string]::Compare([string]$left.scope, [string]$right.scope, [System.StringComparison]::Ordinal)
        if ($scope -ne 0) { return $scope }
        return [string]::Compare([string]$left.relativePath, [string]$right.relativePath, [System.StringComparison]::Ordinal)
    }
    $list.Sort($comparison)
    return @($list)
}

$templatePath = Join-Path $RepositoryRoot 'tools\T22-RF00-SIDEBAR-P19-P12-ENCODER-RENDER-SAFE-current-source-20260905-manifest.json'
if (-not (Test-Path -LiteralPath $templatePath)) { throw "P19 manifest not found: $templatePath" }
$template = Get-Content -LiteralPath $templatePath -Raw -Encoding UTF8 | ConvertFrom-Json

$captureTool = Join-Path $RepositoryRoot 'tools\Invoke-T22Rf00T10ImplementationCapture.ps1'
$reAuditTool = Join-Path $RepositoryRoot 'tools\Invoke-T21T14P20T10Reaudit.ps1'
$t10Evidence = Join-Path $RepositoryRoot 'tools\T10-HC-INPUTHOST-PROTOCOL-IMPLEMENTATION-EVIDENCE-20260905.md'
$nativeBuildRecipe = Join-Path $RepositoryRoot 'native\build_native.bat'
$safetySelfTest = Join-Path $RepositoryRoot 'tools\calibration_safety_delta_selftest.ts'
foreach ($required in @($captureTool, $reAuditTool, $t10Evidence, $nativeBuildRecipe, $safetySelfTest)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw "Missing RF00 key file: $required" }
}

$candidateKeys = @()
foreach ($key in $template.keyFiles) {
    $candidateKeys += [pscustomobject]@{
        scope = [string]$key.scope
        relativePath = [string]$key.relativePath
        absolutePath = [string]$key.absolutePath
    }
}
$candidateKeys += [pscustomobject]@{ scope = 'YMCC'; relativePath = 'tools\Invoke-T22Rf00T10ImplementationCapture.ps1'; absolutePath = $captureTool }
$candidateKeys += [pscustomobject]@{ scope = 'YMCC'; relativePath = 'tools\Invoke-T21T14P20T10Reaudit.ps1'; absolutePath = $reAuditTool }
$candidateKeys += [pscustomobject]@{ scope = 'YMCC'; relativePath = 'tools\T10-HC-INPUTHOST-PROTOCOL-IMPLEMENTATION-EVIDENCE-20260905.md'; absolutePath = $t10Evidence }
$candidateKeys += [pscustomobject]@{ scope = 'YMCC'; relativePath = 'native\build_native.bat'; absolutePath = $nativeBuildRecipe }
$candidateKeys += [pscustomobject]@{ scope = 'YMCC'; relativePath = 'tools\calibration_safety_delta_selftest.ts'; absolutePath = $safetySelfTest }
$ledgerSelfTest = Join-Path $RepositoryRoot 'tools\hc_parity_drift_ledger_selftest.ts'
if (-not (Test-Path -LiteralPath $ledgerSelfTest -PathType Leaf)) { throw "Missing ledger selftest: $ledgerSelfTest" }
$candidateKeys += [pscustomobject]@{ scope = 'YMCC'; relativePath = 'tools\hc_parity_drift_ledger_selftest.ts'; absolutePath = $ledgerSelfTest }
$residentOwnerSelfTest = Join-Path $RepositoryRoot 'tools\input_owner_resident_selftest.ps1'
if (-not (Test-Path -LiteralPath $residentOwnerSelfTest -PathType Leaf)) { throw "Missing resident owner selftest: $residentOwnerSelfTest" }
$candidateKeys += [pscustomobject]@{ scope = 'YMCC'; relativePath = 'tools\input_owner_resident_selftest.ps1'; absolutePath = $residentOwnerSelfTest }
$ownerIntegrationSelfTest = Join-Path $RepositoryRoot 'tools\input_owner_runtime_integration_selftest.ts'
if (-not (Test-Path -LiteralPath $ownerIntegrationSelfTest -PathType Leaf)) { throw "Missing owner integration selftest: $ownerIntegrationSelfTest" }
$candidateKeys += [pscustomobject]@{ scope = 'YMCC'; relativePath = 'tools\input_owner_runtime_integration_selftest.ts'; absolutePath = $ownerIntegrationSelfTest }
$lifecycleMockKeys = @(
    [pscustomobject]@{ relativePath = 'src\bridge\inputLifecycleMock.ts'; absolutePath = (Join-Path $RepositoryRoot 'src\bridge\inputLifecycleMock.ts') },
    [pscustomobject]@{ relativePath = 'src\bridge\inputPowerPnpMock.ts'; absolutePath = (Join-Path $RepositoryRoot 'src\bridge\inputPowerPnpMock.ts') },
    [pscustomobject]@{ relativePath = 'tools\input_lifecycle_mock_selftest.ts'; absolutePath = (Join-Path $RepositoryRoot 'tools\input_lifecycle_mock_selftest.ts') },
    [pscustomobject]@{ relativePath = 'tools\input_power_pnp_selftest.ts'; absolutePath = (Join-Path $RepositoryRoot 'tools\input_power_pnp_selftest.ts') },
    [pscustomobject]@{ relativePath = 'tools\input_coordinator_mock_selftest.ts'; absolutePath = (Join-Path $RepositoryRoot 'tools\input_coordinator_mock_selftest.ts') }
)
foreach ($key in $lifecycleMockKeys) {
    if (-not (Test-Path -LiteralPath $key.absolutePath -PathType Leaf)) { throw "Missing lifecycle mock key file: $($key.absolutePath)" }
    $candidateKeys += [pscustomobject]@{ scope = 'YMCC'; relativePath = $key.relativePath; absolutePath = $key.absolutePath }
}
$gyroBridgeKeys = @(
    [pscustomobject]@{ relativePath = 'src\bridge\motionSample.ts'; absolutePath = (Join-Path $RepositoryRoot 'src\bridge\motionSample.ts') },
    [pscustomobject]@{ relativePath = 'src\bridge\rogGyroProtocol.ts'; absolutePath = (Join-Path $RepositoryRoot 'src\bridge\rogGyroProtocol.ts') },
    [pscustomobject]@{ relativePath = 'src\bridge\specialControllerProtocols.ts'; absolutePath = (Join-Path $RepositoryRoot 'src\bridge\specialControllerProtocols.ts') }
)
foreach ($key in $gyroBridgeKeys) {
    if (-not (Test-Path -LiteralPath $key.absolutePath -PathType Leaf)) { throw "Missing gyro bridge key file: $($key.absolutePath)" }
    $candidateKeys += [pscustomobject]@{ scope = 'YMCC'; relativePath = $key.relativePath; absolutePath = $key.absolutePath }
}

$seen = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::Ordinal)
$keyFiles = @()
foreach ($key in $candidateKeys) {
    $identity = "$($key.scope)`t$($key.relativePath)"
    if ($seen.Add($identity)) {
        if (-not (Test-Path -LiteralPath $key.absolutePath -PathType Leaf)) { throw "Missing key file: $($key.absolutePath)" }
        $item = Get-Item -LiteralPath $key.absolutePath
        $keyFiles += [pscustomobject]@{
            scope = $key.scope
            relativePath = $key.relativePath
            absolutePath = $key.absolutePath
            sha256 = (Get-FileHash -LiteralPath $key.absolutePath -Algorithm SHA256).Hash.ToUpperInvariant()
            bytes = [int64]$item.Length
        }
    }
}
$keyFiles = Get-OrdinalSorted $keyFiles
$canonical = [System.Text.StringBuilder]::new()
foreach ($key in $keyFiles) {
    [void]$canonical.Append($key.scope).Append("`t").Append($key.relativePath).Append("`t").Append($key.sha256).Append("`t").Append($key.bytes).Append("`n")
}
$sourceDigest = (Get-FileHash -InputStream ([System.IO.MemoryStream]::new([System.Text.UTF8Encoding]::new($false).GetBytes($canonical.ToString()))) -Algorithm SHA256).Hash.ToUpperInvariant()

$ymccHead = Get-GitOutput @('-C', $RepositoryRoot, 'rev-parse', 'HEAD')
$ymccPorcelain = Get-GitOutput @('-C', $RepositoryRoot, 'status', '--porcelain=v1')
$hcHead = Get-GitOutput @('-C', $HcRoot, 'rev-parse', 'HEAD')
$hcPorcelain = Get-GitOutput @('-C', $HcRoot, 'status', '--porcelain=v1')

$tools = Join-Path $RepositoryRoot 'tools'
$rawPath = Join-Path $tools "$Prefix-porcelain.raw.txt"
$searchPath = Join-Path $tools "$Prefix-raw-search.txt"
$lineMapPath = Join-Path $tools "$Prefix-line-map.md"
$manifestPath = Join-Path $tools "$Prefix-manifest.json"

Write-Utf8Lf $rawPath $ymccPorcelain.stdout
$searchSources = @(
    (Join-Path $RepositoryRoot 'native\main.cpp'),
    (Join-Path $RepositoryRoot 'InputHost\Program.cs'),
    (Join-Path $DocsRoot 'Tasks\GyroVirtual\TASK.md'),
    (Join-Path $DocsRoot 'Tasks\GyroVirtual\T10-HC-CORE-BOUNDARY-AND-INPUTHOST-ARCHITECTURE-AUDIT-20260905.md')
)
    $searchPatterns = @('input-host-state\.json', '--state', 'InstallDriver\(', 'TerminateProcess\(g_inputHostProcess', 'CreateNamedPipeW', 'PIPE_REJECT_REMOTE_CLIENTS', 'PREPARE_TARGET', 'SUBMIT_NEUTRAL', 'SUBMIT_FRAME', 'KEEPALIVE', 'QUIESCE', 'RELEASE_TARGET', 'g_inputReady', 'PowerLifecycle::Ready', 'inputHostRequestPowerRelease', 'inputHostRequestPowerRearm', 'physicalSourceAdmitted', 'physical-source-absent', 'inputCaptureSuspendSensorsForPower', 'inputCaptureResumeSensorsForPower', 'Gyrometer::GetDefault', 'Accelerometer::GetDefault', 'release-unproven')
$rawSearch = [System.Text.StringBuilder]::new()
foreach ($path in $searchSources) {
    [void]$rawSearch.AppendLine("===== $path")
    foreach ($pattern in $searchPatterns) {
        $matches = Select-String -LiteralPath $path -Pattern $pattern
        foreach ($match in $matches) { [void]$rawSearch.AppendLine("$($match.LineNumber):$($match.Line)") }
    }
}
Write-Utf8Lf $searchPath $rawSearch.ToString()

$lineMap = [System.Text.StringBuilder]::new()
[void]$lineMap.AppendLine("# $Prefix line map")
[void]$lineMap.AppendLine()
[void]$lineMap.AppendLine("Status: static read/hash capture only; no build, test, Host, driver, HID, Steam, game, or device operation.")
[void]$lineMap.AppendLine()
[void]$lineMap.AppendLine("| scope | relative path | bytes | SHA-256 |")
[void]$lineMap.AppendLine("|---|---|---:|---|")
foreach ($key in $keyFiles) {
    [void]$lineMap.AppendLine(('| ' + $key.scope + ' | `' + $key.relativePath + '` | ' + $key.bytes + ' | `' + $key.sha256 + '` |'))
}
[void]$lineMap.AppendLine()
[void]$lineMap.AppendLine("Current T10 anchors: `native/main.cpp` named-pipe server/request lifecycle and `InputHost/Program.cs` HostSession state machine. The line map identifies source inputs only; it is not runtime evidence.")
Write-Utf8Lf $lineMapPath $lineMap.ToString()

$rawItem = Get-Item -LiteralPath $rawPath
$searchItem = Get-Item -LiteralPath $searchPath
$lineMapItem = Get-Item -LiteralPath $lineMapPath
$manifest = [ordered]@{
    schemaVersion = 'T22-RF00-CurrentSourceManifest.v9'
    manifestId = $ManifestId
    capturedUtc = [DateTime]::UtcNow.ToString('O')
    captureMode = 'static-read-hash-documentation-only'
    runtimeOperation = $false
    buildOrTest = $false
    operator = 'Codex BUS / T10 HC-route implementation RF00'
    cwd = $RepositoryRoot
    argv = @(
        'git -C <YMCC> rev-parse HEAD',
        'git -C <YMCC> status --porcelain=v1',
        'Get-FileHash -Algorithm SHA256 <current key files>',
        'git -C <HC> rev-parse HEAD',
        'git -C <HC> status --porcelain=v1',
        'static Select-String evidence capture',
        'UTF-8/NUL/fence validation'
    )
    ymcc = [ordered]@{
        repository = $RepositoryRoot
        head = $ymccHead.stdout.Trim()
        worktreePorcelainCount = @($ymccPorcelain.stdout -split "`n" | Where-Object { $_ -ne '' }).Count
        worktreePorcelainRawPath = $rawPath
        worktreePorcelainCanonicalSha256 = (Get-FileHash -LiteralPath $rawPath -Algorithm SHA256).Hash.ToUpperInvariant()
        worktreePorcelainArtifactSha256 = (Get-FileHash -LiteralPath $rawPath -Algorithm SHA256).Hash.ToUpperInvariant()
        porcelainBytes = [int64]$rawItem.Length
        porcelainEncoding = 'UTF-8 without BOM; LF; complete git status --porcelain=v1 output'
        exitCode = $ymccPorcelain.exitCode
    }
    hc = [ordered]@{
        root = $HcRoot
        head = $hcHead.stdout.Trim()
        expectedVersion = '0.32.4.0'
        expectedCommit = '06c0b9544db2b1f39abf9cd3796225ccfb096103'
        worktreePorcelainCount = @($hcPorcelain.stdout -split "`n" | Where-Object { $_ -ne '' }).Count
        statusVerified = (@($hcPorcelain.stdout -split "`n" | Where-Object { $_ -ne '' }).Count -eq 0)
    }
    sourceDigest = $sourceDigest
    sourceDigestAlgorithm = 'SHA256(UTF8(join(keyFiles sorted lexically by ordinal scope then ordinal relativePath, scope + TAB + relativePath + TAB + fileSha256 + TAB + bytes + LF)))'
    keyFileSetChange = 'P20 reuses P19 coverage and adds the T10 implementation evidence, reproducible static RF00 capture tool, deterministic T21/T14 re-audit tool, explicit C++/WinRT native build recipe, T10/T11 safety selftest, HC parity ledger selftest, resident-owner contract selftest, owner-runtime integration selftest, lifecycle ingress/coordinator mock sources and selftests, plus motionSample/ROG gyro/special-controller bridge sources. P19 remains a historical snapshot; this capture records the current key-file set and source state.'
    coverage = @(
        'P01 Windows sensor/default/threshold authority',
        'P02 paired raw motion/calibration source',
        'P03 InputHost DS4 descriptor/encoder/transport/lifecycle',
        'P04 HidHide/ROG P-HID boundary',
        'P05 owner and consumer-observation boundary',
        'P06 neutral/release/recovery boundary',
        'P07 current-source provenance/render quality'
    )
    artifactExclusion = 'This manifest, its raw porcelain/search/line-map artifacts, the later T21/T14 report and reconciliation JSON/ledger are excluded from keyFiles/sourceDigest. The capture script itself and the T10 implementation evidence are included.'
    artifacts = @(
        [ordered]@{ role = 'porcelain-raw'; path = $rawPath; bytes = [int64]$rawItem.Length; sha256 = (Get-FileHash -LiteralPath $rawPath -Algorithm SHA256).Hash.ToUpperInvariant() },
        [ordered]@{ role = 'raw-search'; path = $searchPath; bytes = [int64]$searchItem.Length; sha256 = (Get-FileHash -LiteralPath $searchPath -Algorithm SHA256).Hash.ToUpperInvariant() },
        [ordered]@{ role = 'line-map'; path = $lineMapPath; bytes = [int64]$lineMapItem.Length; sha256 = (Get-FileHash -LiteralPath $lineMapPath -Algorithm SHA256).Hash.ToUpperInvariant() },
        [ordered]@{ role = 'manifest-self'; path = $manifestPath; bytes = 'self-excluded'; sha256 = 'self-excluded' }
    )
    keyFiles = @($keyFiles)
}
Write-Utf8Lf $manifestPath ($manifest | ConvertTo-Json -Depth 12)

foreach ($path in @($rawPath, $searchPath, $lineMapPath, $manifestPath)) {
    $bytes = [System.IO.File]::ReadAllBytes($path)
    if ($bytes -contains 0) { throw "NUL detected in generated artifact: $path" }
}
$fenceCount = @(Get-Content -LiteralPath $lineMapPath | Where-Object { $_ -match '^```' }).Count
if (($fenceCount % 2) -ne 0) { throw "Unpaired Markdown fence in $lineMapPath" }

[pscustomobject]@{
    manifestPath = $manifestPath
    manifestId = $manifest.manifestId
    sourceDigest = $sourceDigest
    keyFileCount = $keyFiles.Count
    porcelainCount = $manifest.ymcc.worktreePorcelainCount
    hcClean = $manifest.hc.statusVerified
    runtimeOperation = $false
    buildOrTest = $false
} | ConvertTo-Json -Depth 4
