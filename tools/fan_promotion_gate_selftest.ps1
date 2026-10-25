[CmdletBinding()]
param(
  [string]$ProjectRoot = '',
  [string]$OutputRoot = '',
  [ValidateSet('LegacyFull', 'PackageSource')]
  [string]$Mode = 'LegacyFull',
  [ValidatePattern('^[a-fA-F0-9]{64}$')]
  [string]$ExpectedHostSha256 = '',
  [ValidatePattern('^[a-fA-F0-9]{64}$')]
  [string]$ExpectedBackendSha256 = '',
  [ValidatePattern('^[a-fA-F0-9]{64}$')]
  [string]$ExpectedManifestSha256 = '',
  [string]$LegacyPayloadRoot = ''
)

# 920 §8.0-F: mandatory negative cases for the candidate / promotion chain.
#
# LegacyFull (the compatible default) drives the REAL production logic:
#   - Assert-PublishPreGate is extracted verbatim from tools/build-workspace.ps1
#     and executed against constructed bad payload trees;
#   - the real Host binary is started to prove the load-time identity event;
#   - the real tools/verify-yemancc-export.ps1 is executed.
#
# A negative case passes only when the production logic REFUSES the inconsistent
# promotion, or explicitly reports it as unverifiable. A silent PASS is a
# failure. No real hardware or device operation is involved.
#
# PackageSource exits before the LegacyFull path and only executes extracted
# production gates/copy ASTs over a fresh scratch tree. It requires explicit
# expected hashes and an old self-consistent LegacyPayloadRoot.
#
# Keep this file pure ASCII: PS 5.1 misdecodes non-ASCII scripts without a BOM.

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($ProjectRoot)) { $ProjectRoot = Split-Path -Parent $PSScriptRoot }
$ProjectRoot = [IO.Path]::GetFullPath($ProjectRoot).TrimEnd('\')
# This branch must return before any LegacyFull gate, identity or Host start.
if ($Mode -eq 'PackageSource') {
  & {
    foreach ($pin in @($ExpectedHostSha256, $ExpectedBackendSha256, $ExpectedManifestSha256)) {
      if ($pin -notmatch '^[a-fA-F0-9]{64}$') { throw 'PackageSource requires explicit 64-hex ExpectedHostSha256, ExpectedBackendSha256 and ExpectedManifestSha256.' }
    }
    if ([string]::IsNullOrWhiteSpace($OutputRoot) -or [string]::IsNullOrWhiteSpace($LegacyPayloadRoot)) {
      throw 'PackageSource requires explicit OutputRoot and LegacyPayloadRoot.'
    }
    $officialRoot = Split-Path -Parent $PSScriptRoot
    $OutputRoot = [IO.Path]::GetFullPath($OutputRoot).TrimEnd('\')
    $LegacyPayloadRoot = [IO.Path]::GetFullPath($LegacyPayloadRoot).TrimEnd('\')
    foreach ($readOnlyRoot in @($officialRoot, $ProjectRoot, $LegacyPayloadRoot)) {
      $readOnlyFull = [IO.Path]::GetFullPath($readOnlyRoot).TrimEnd('\')
      if ($OutputRoot -ieq $readOnlyFull -or $OutputRoot.StartsWith($readOnlyFull + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw "PackageSource OutputRoot must be outside its input trees: $OutputRoot"
      }
    }
    # A fresh child is safe only when none of its existing ancestors is a link.
    $ancestor = $OutputRoot
    while ($ancestor) {
      if ((Test-Path -LiteralPath $ancestor) -and (Get-Item -LiteralPath $ancestor -Force).Attributes.HasFlag([IO.FileAttributes]::ReparsePoint)) {
        throw "PackageSource output ancestor is a reparse point: $ancestor"
      }
      $ancestor = Split-Path -Parent $ancestor
    }
    $sandboxRoot = Join-Path $OutputRoot ('fan-package-source-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $sandboxRoot | Out-Null
    $originalInputs = [ordered]@{}
    $extractions = [ordered]@{}
    $cases = New-Object 'System.Collections.Generic.List[object]'
    $failures = New-Object 'System.Collections.Generic.List[string]'

    function Assert-PackageSourceChild([string]$Path) {
      $full = [IO.Path]::GetFullPath($Path)
      if (-not $full.StartsWith($sandboxRoot + '\', [StringComparison]::OrdinalIgnoreCase)) { throw "PackageSource write outside fresh scratch: $full" }
      return $full
    }

    function Get-PackageSourceAst([string]$File) {
      $tokens = $null; $errors = $null
      $ast = [System.Management.Automation.Language.Parser]::ParseFile($File, [ref]$tokens, [ref]$errors)
      if ($errors.Count -ne 0) { throw ("Production script does not parse: $File : " + $errors[0].Message) }
      return $ast
    }

    function Get-PackageSourceBlock([string]$File, [string]$Name) {
      $ast = Get-PackageSourceAst $File
      if ($Name -eq 'FanHostCopyBranch') {
        $nodes = @($ast.FindAll({ param($node)
          $node -is [System.Management.Automation.Language.IfStatementAst] -and
          $node.Clauses.Count -eq 1 -and
          $node.Clauses[0].Item1.Extent.Text.Trim() -ceq '$includeFanHostInFullTestPackage' -and
          $node.Extent.Text.Contains('Copy-DirectoryContents') -and $node.Extent.Text.Contains("'fan-host-v2'")
        }, $true))
      } else {
        $nodes = @($ast.FindAll({ param($node)
          $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq $Name
        }, $true))
      }
      if ($nodes.Count -ne 1) { throw "Expected exactly one production AST for $Name in $File; found $($nodes.Count)" }
      $text = $nodes[0].Extent.Text
      $tokens = $null; $errors = $null
      # Preserve the real script filename. In source negatives this is the
      # byte-identical scratch copy, so its verifier resolves scratch sources.
      $parsed = [System.Management.Automation.Language.Parser]::ParseInput($text, $File, [ref]$tokens, [ref]$errors)
      if ($errors.Count -ne 0) { throw "Extracted production AST does not parse: $File : $Name" }
      $key = $File + '::' + $Name
      if (-not $extractions.Contains($key)) {
        $extractions[$key] = [ordered]@{ sourceFile = $File; name = $Name; exactText = $text }
      }
      return $parsed.GetScriptBlock()
    }

    # Only these definitions are evaluated, never build/package top-level code.
    . (Get-PackageSourceBlock (Join-Path $PSScriptRoot 'build-workspace.ps1') 'Get-Sha256')
    . (Get-PackageSourceBlock (Join-Path $PSScriptRoot 'package-release.ps1') 'Copy-DirectoryContents')

    function Remember-PackageSourceInput([string]$Path) {
      $full = [IO.Path]::GetFullPath($Path)
      if (-not (Test-Path -LiteralPath $full -PathType Leaf)) { throw "PackageSource input missing: $full" }
      if ((Get-Item -LiteralPath $full -Force).Attributes.HasFlag([IO.FileAttributes]::ReparsePoint)) { throw "PackageSource input is a reparse point: $full" }
      if (-not $full.StartsWith($sandboxRoot + '\', [StringComparison]::OrdinalIgnoreCase)) {
        $hash = (Get-Sha256 $full).ToLowerInvariant()
        if ($originalInputs.Contains($full) -and $originalInputs[$full] -cne $hash) { throw "PackageSource input changed during capture: $full" }
        $originalInputs[$full] = $hash
      }
    }

    function Copy-PackageSourceFile([string]$Source, [string]$Destination) {
      $target = Assert-PackageSourceChild $Destination
      Remember-PackageSourceInput $Source
      New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
      Copy-Item -LiteralPath $Source -Destination $target
      if ((Get-Sha256 $Source) -cne (Get-Sha256 $target)) { throw "PackageSource copy changed bytes: $Source" }
    }

    function Copy-PackageSourceTree([string]$Source, [string]$Destination) {
      $target = Assert-PackageSourceChild $Destination
      if (-not (Test-Path -LiteralPath $Source -PathType Container)) { throw "PackageSource input tree missing: $Source" }
      if ((Get-Item -LiteralPath $Source -Force).Attributes.HasFlag([IO.FileAttributes]::ReparsePoint)) { throw "PackageSource input tree is a reparse point: $Source" }
      foreach ($item in Get-ChildItem -LiteralPath $Source -Recurse -Force) {
        if ($item.Attributes.HasFlag([IO.FileAttributes]::ReparsePoint)) { throw "PackageSource input contains a reparse point: $($item.FullName)" }
        if (-not $item.PSIsContainer) { Remember-PackageSourceInput $item.FullName }
      }
      Copy-DirectoryContents $Source $target
    }

    function Copy-PackageSourcePayload([string]$Source, [string]$Destination, [string]$ManifestHash) {
      # Capture only the frozen payload, never a live session/lease directory.
      # Inspect unexpected names without opening or recursively copying them.
      $manifestPath = Join-Path $Source 'YeManFanHost.payload.json'
      Remember-PackageSourceInput $manifestPath
      if ((Get-Sha256 $manifestPath).ToLowerInvariant() -cne $ManifestHash.ToLowerInvariant()) { throw "PackageSource readiness: payload manifest identity mismatch: $Source" }
      $manifest = Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
      $names = @('YeManFanHost.payload.json') + @($manifest.files | ForEach-Object { [string]$_.path })
      $items = @(Get-ChildItem -LiteralPath $Source -Force)
      if (@($items | Where-Object { $_.PSIsContainer -or $_.Attributes.HasFlag([IO.FileAttributes]::ReparsePoint) }).Count -ne 0 -or
          (Compare-Object @($items.Name | Sort-Object) @($names | Sort-Object))) {
        throw "PackageSource input must contain only frozen payload members; extra paths were not read: $Source"
      }
      Copy-PackageSourceTree $Source $Destination
    }

    function Get-PackageSourceSnapshot([string]$Root) {
      foreach ($file in Get-ChildItem -LiteralPath $Root -Recurse -File -Force | Sort-Object FullName) {
        [pscustomobject]@{ path = $file.FullName.Substring($Root.Length + 1).Replace('\', '/'); sha256 = (Get-Sha256 $file.FullName).ToLowerInvariant() }
      }
    }

    function Compare-PackageSourceSnapshot($Before, $After) {
      $old = @{}; $new = @{}
      foreach ($row in @($Before)) { $old[$row.path] = $row.sha256 }
      foreach ($row in @($After)) { $new[$row.path] = $row.sha256 }
      foreach ($path in @(@($old.Keys) + @($new.Keys) | Sort-Object -Unique)) {
        if ($old[$path] -cne $new[$path]) {
          [pscustomobject]@{ path = $path; before = $old[$path]; after = $new[$path] }
        }
      }
    }

    function Invoke-PackageSourceProduction([string]$Root, [string]$Gate, [string]$Stage, [string]$ToolsRoot) {
      . (Get-PackageSourceBlock (Join-Path $ToolsRoot 'build-workspace.ps1') 'Get-Sha256')
      . (Get-PackageSourceBlock (Join-Path $ToolsRoot 'build-workspace.ps1') 'Assert-PublishPreGate')
      . (Get-PackageSourceBlock (Join-Path $ToolsRoot 'package-release.ps1') 'Get-Sha256')
      . (Get-PackageSourceBlock (Join-Path $ToolsRoot 'package-release.ps1') 'Assert-FanHostMainlinePayload')
      . (Get-PackageSourceBlock (Join-Path $ToolsRoot 'package-release.ps1') 'Copy-DirectoryContents')
      $branch = Get-PackageSourceBlock (Join-Path $ToolsRoot 'package-release.ps1') 'FanHostCopyBranch'
      $includeFanHostInFullTestPackage = $true
      $sourcePowerControl = Join-Path $Root 'PowerControl'
      $StagingPowerControl = Assert-PackageSourceChild $Stage
      $lines = New-Object 'System.Collections.Generic.List[string]'
      $rejected = $false; $message = ''; $errorId = ''; $position = ''
      try {
        & {
          switch ($Gate) {
            'BuildPreGate' { Assert-PublishPreGate $Root }
            'PackagePayload' { Assert-FanHostMainlinePayload (Join-Path $sourcePowerControl 'fan-host') }
            'PackageCopyBranch' { . $branch }
            default { throw "Unknown PackageSource production gate: $Gate" }
          }
        } *>&1 | ForEach-Object { $lines.Add($_.ToString()) }
      } catch {
        $rejected = $true; $message = $_.Exception.Message
        $errorId = $_.FullyQualifiedErrorId; $position = $_.InvocationInfo.PositionMessage
      }
      return [pscustomobject]@{ gate = $Gate; rejected = $rejected; actualProductionError = $message; errorId = $errorId; position = $position; output = $lines.ToArray(); toolsRoot = $ToolsRoot }
    }

    function Set-PackageSourceNativePin([string]$Root, [string]$Hash) {
      $path = Assert-PackageSourceChild (Join-Path $Root 'native\main.cpp')
      $text = [IO.File]::ReadAllText($path)
      $pattern = '(\$expectedFanHostV2ManifestSha256\s*=\s*'')[0-9a-fA-F]{64}('')'
      if ([regex]::Matches($text, $pattern).Count -ne 1) { throw 'Expected one native default manifest hash in scratch main.cpp.' }
      $text = [regex]::Replace($text, $pattern, { param($match) $match.Groups[1].Value + $Hash + $match.Groups[2].Value })
      [IO.File]::WriteAllText($path, $text, (New-Object Text.UTF8Encoding($false)))
    }

    function Test-PackageSourceCase([string]$Label, [scriptblock]$Mutate, [System.Collections.IDictionary]$ExpectedErrors, [bool]$UseOfficialTools = $false) {
      $root = Join-Path $sandboxRoot ('cases\' + $Label)
      Copy-PackageSourceTree $baseline $root
      $initial = @(Get-PackageSourceSnapshot $root)
      & $Mutate $root
      $before = @(Get-PackageSourceSnapshot $root)
      $gateResults = New-Object 'System.Collections.Generic.List[object]'
      $problems = New-Object 'System.Collections.Generic.List[string]'
      $toolsRoot = if ($UseOfficialTools) { $PSScriptRoot } else { Join-Path $root 'tools' }
      $stage = Join-Path $sandboxRoot ('staging\' + $Label)
      foreach ($gate in $ExpectedErrors.Keys) {
        $result = Invoke-PackageSourceProduction $root $gate $stage $toolsRoot
        $gateResults.Add($result)
        $pattern = [string]$ExpectedErrors[$gate]
        if ($pattern) {
          if (-not $result.rejected) { $problems.Add("$gate did not reject") }
          elseif ($result.actualProductionError -notmatch $pattern) { $problems.Add("$gate rejected for an unexpected reason: $($result.actualProductionError)") }
          if ($gate -eq 'PackageCopyBranch' -and (Test-Path -LiteralPath (Join-Path $stage 'fan-host-v2'))) { $problems.Add('Rejected production branch created its payload destination') }
        } else {
          if ($result.rejected) { $problems.Add("$gate rejected: $($result.actualProductionError)") }
          $marker = if ($gate -eq 'BuildPreGate') { 'PRE_GATE_OK' } else { 'FAN_HOST_V2_OK' }
          if (($result.output -join "`n") -notmatch $marker) { $problems.Add("$gate did not emit $marker") }
        }
      }
      $staged = @()
      if ($ExpectedErrors.Contains('PackageCopyBranch') -and -not $ExpectedErrors['PackageCopyBranch']) {
        $destination = Join-Path $stage 'fan-host-v2'
        if (Test-Path -LiteralPath $destination -PathType Container) {
          $staged = @(Get-PackageSourceSnapshot $destination)
          if (@(Compare-PackageSourceSnapshot @(Get-PackageSourceSnapshot (Join-Path $root 'PowerControl\fan-host')) $staged).Count -ne 0) { $problems.Add('Production copy file set or bytes differ from source payload') }
          foreach ($pair in $expectedPayload.GetEnumerator()) {
            if ((Get-Sha256 (Join-Path $destination $pair.Key)).ToLowerInvariant() -cne $pair.Value) { $problems.Add("Production copy expected identity mismatch: $($pair.Key)") }
          }
        } else { $problems.Add('Production copy destination is missing') }
      }
      $after = @(Get-PackageSourceSnapshot $root)
      $inputChanges = @(Compare-PackageSourceSnapshot $before $after)
      if ($inputChanges.Count -ne 0) { $problems.Add('Production gate mutated its input tree') }
      $record = [ordered]@{ label = $Label; passed = $problems.Count -eq 0; root = $root; expectedErrors = $ExpectedErrors; inputBaseline = $initial; inputBefore = $before; inputAfter = $after; fixtureChanges = @(Compare-PackageSourceSnapshot $initial $before); inputChanges = $inputChanges; gates = $gateResults.ToArray(); stagedFiles = $staged; failures = $problems.ToArray() }
      $cases.Add($record)
      $record | ConvertTo-Json -Depth 9 | Set-Content -LiteralPath (Join-Path $root 'case-evidence.json') -Encoding UTF8
      if ($problems.Count) { $failures.Add($Label + ': ' + ($problems -join '; ')) }
      Write-Output ((@('FAIL', 'PASS')[[int]($problems.Count -eq 0)]) + ' PackageSource ' + $Label)
    }

    $expectedPayload = [ordered]@{
      'YeManFanHost.dll' = $ExpectedHostSha256.ToLowerInvariant()
      'YeManFanBackend.dll' = $ExpectedBackendSha256.ToLowerInvariant()
      'YeManFanHost.payload.json' = $ExpectedManifestSha256.ToLowerInvariant()
    }
    try {
      Remember-PackageSourceInput (Join-Path $PSScriptRoot 'fan_promotion_gate_selftest.ps1')
      $baseline = Join-Path $sandboxRoot 'baseline'
      foreach ($name in @('build-workspace.ps1', 'package-release.ps1', 'verify-r5v9-fan-host-payload.ps1')) {
        Copy-PackageSourceFile (Join-Path $PSScriptRoot $name) (Join-Path $baseline ('tools\' + $name))
      }
      Copy-PackageSourceFile (Join-Path $PSScriptRoot 'hc-device-runtime\verify-host-source-bundle.ps1') (Join-Path $baseline 'tools\hc-device-runtime\verify-host-source-bundle.ps1')
      $verifierAst = Get-PackageSourceAst (Join-Path $baseline 'tools\verify-r5v9-fan-host-payload.ps1')
      $pinAssignments = @($verifierAst.FindAll({ param($node)
        $node -is [System.Management.Automation.Language.AssignmentStatementAst] -and
        $node.Left -is [System.Management.Automation.Language.VariableExpressionAst] -and $node.Left.VariablePath.UserPath -ceq 'compiledSourcePins'
      }, $true))
      if ($pinAssignments.Count -ne 1) { throw 'PackageSource readiness: verifier must declare one literal compiledSourcePins map.' }
      $maps = @($pinAssignments[0].Right.FindAll({ param($node) $node -is [System.Management.Automation.Language.HashtableAst] }, $true))
      if ($maps.Count -ne 1) { throw 'PackageSource requires a literal compiledSourcePins map.' }
      $compiledPins = [ordered]@{}
      foreach ($pair in $maps[0].KeyValuePairs) {
        $key = $pair.Item1
        $value = $pair.Item2.PipelineElements[0].Expression
        if ($key -isnot [System.Management.Automation.Language.StringConstantExpressionAst] -or $value -isnot [System.Management.Automation.Language.StringConstantExpressionAst] -or
            $key.Value -notmatch '^(FanLab|native|capabilities)/[^:]+\.(cs|csproj)$' -or $key.Value -match '(^|[\\/])\.\.([\\/]|$)' -or $value.Value -notmatch '^[a-fA-F0-9]{64}$' -or $compiledPins.Contains($key.Value)) {
          throw 'PackageSource compiledSourcePins must contain unique safe literal source paths and 64-hex hashes.'
        }
        $compiledPins[$key.Value] = $value.Value.ToLowerInvariant()
      }
      if ($compiledPins.Count -lt 29) { throw "PackageSource readiness: compiled source pins are incomplete ($($compiledPins.Count), expected at least 29)." }
      $componentBundleRelative = 'deps/fan-host-source/candidate-canonical-lhm-20261024'
      $componentBundleManifestSha256 = '561129d604ccbfd20621737d290fad691b127d1ce99b0ffdf4110955972ac03c'
      Copy-PackageSourceTree (Join-Path $ProjectRoot $componentBundleRelative) (Join-Path $baseline $componentBundleRelative)
      if ((Get-Sha256 (Join-Path $baseline ($componentBundleRelative + '/manifest.json'))).ToLowerInvariant() -cne $componentBundleManifestSha256) { throw 'PackageSource readiness: approved source bundle manifest differs.' }
      # Live-source controls are separate copies; the selected archive remains exact.
      foreach ($relative in $compiledPins.Keys) {
        $source = Join-Path (Join-Path $baseline ($componentBundleRelative + '/source')) $relative
        Copy-PackageSourceFile $source (Join-Path $baseline $relative)
        if ((Get-Sha256 (Join-Path $baseline $relative)).ToLowerInvariant() -cne $compiledPins[$relative]) { throw "PackageSource readiness: archived compiled source differs from verifier pin: $relative" }
      }
      $bridge = Join-Path $ProjectRoot 'src\bridge\fanHost.ts'
      if (-not (Test-Path -LiteralPath $bridge -PathType Leaf)) { $bridge = Join-Path $officialRoot 'src\bridge\fanHost.ts' }
      Copy-PackageSourceFile $bridge (Join-Path $baseline 'src\bridge\fanHost.ts')
      if ([IO.File]::ReadAllText($bridge).Contains("from './fanHost/deployment'")) {
        foreach ($leaf in @('startupConfiguration.ts','deployment.ts','capabilityAdmission.ts')) {
          Copy-PackageSourceFile (Join-Path (Split-Path -Parent $bridge) ('fanHost\' + $leaf)) (Join-Path $baseline ('src\bridge\fanHost\' + $leaf))
        }
      }
      Copy-PackageSourceFile (Join-Path $ProjectRoot 'native\main.cpp') (Join-Path $baseline 'native\main.cpp')
      Copy-PackageSourcePayload (Join-Path $ProjectRoot 'PowerControl\fan-host') (Join-Path $baseline 'PowerControl\fan-host') $ExpectedManifestSha256
      foreach ($name in @('handheldcompanion-runtime', 'ymcc-shared-runtime')) {
        Copy-PackageSourceTree (Join-Path $ProjectRoot ('PowerControl\' + $name)) (Join-Path $baseline ('PowerControl\' + $name))
      }
      foreach ($pair in $expectedPayload.GetEnumerator()) {
        if ((Get-Sha256 (Join-Path $baseline ('PowerControl\fan-host\' + $pair.Key))).ToLowerInvariant() -cne $pair.Value) { throw "PackageSource readiness: candidate identity mismatch: $($pair.Key)" }
      }
      $legacyManifest = Join-Path $LegacyPayloadRoot 'YeManFanHost.payload.json'
      $legacyHost = Join-Path $LegacyPayloadRoot 'YeManFanHost.dll'
      Remember-PackageSourceInput $legacyManifest
      Remember-PackageSourceInput $legacyHost
      if ((Get-Sha256 $legacyManifest).ToLowerInvariant() -cne '94ff83cbcc2f703cb44c1bb5ce61138faef05c78b26b3d5337a5ca407a7af6b6' -or
          (Get-Sha256 $legacyHost).ToLowerInvariant() -cne '7f4136c87fe97b10c375b1e2e53d2074f6fc1899e34a4b2a37203ea633adcc5f') { throw 'LegacyPayloadRoot must contain the explicit old 7f/94ff payload.' }
      $legacyJson = Get-Content -LiteralPath $legacyManifest -Raw -Encoding UTF8 | ConvertFrom-Json
      if (@($legacyJson.files).Count -ne 9) { throw 'LegacyPayloadRoot must contain the full old nine-file manifest.' }
      foreach ($entry in @($legacyJson.files)) {
        $relative = [string]$entry.path
        if ($relative -notmatch '^[^/\\:]+$' -or $relative -in @('.', '..')) { throw 'Unsafe legacy manifest file path.' }
        $path = Join-Path $LegacyPayloadRoot $relative
        Remember-PackageSourceInput $path
        if ((Get-Sha256 $path).ToLowerInvariant() -cne ([string]$entry.sha256).ToLowerInvariant()) { throw "Legacy payload is not self-consistent: $relative" }
      }
      Test-PackageSourceCase 'positive-official-source-and-copy' {} ([ordered]@{ BuildPreGate = ''; PackagePayload = ''; PackageCopyBranch = '' }) $true
      Test-PackageSourceCase 'positive-isolated-source-and-copy' {} ([ordered]@{ BuildPreGate = ''; PackagePayload = ''; PackageCopyBranch = '' })
      # Generated C# must stay outside the production compiled-source closure.
      # This positive control pairs with both real-source additions below.
      Test-PackageSourceCase 'positive-generated-bin-obj-excluded' {
        param($root)
        foreach ($lane in @('real-host', 'fan-backend')) {
          foreach ($generated in @('bin\Release', 'obj\Release')) {
            $path = Assert-PackageSourceChild (Join-Path $root ('FanLab\' + $lane + '\' + $generated + '\PackageSourceGenerated.cs'))
            New-Item -ItemType Directory -Path (Split-Path -Parent $path) -Force | Out-Null
            [IO.File]::WriteAllText($path, '// PackageSource generated file; excluded from the compiled closure')
          }
        }
      } ([ordered]@{ PackagePayload = ''; PackageCopyBranch = '' })
      Test-PackageSourceCase 'positive-live-source-drift-keeps-approved-package' {
        param($root)
        [IO.File]::AppendAllText((Assert-PackageSourceChild (Join-Path $root 'FanLab/real-host/Program.cs')), "`n// Incomplete live development is not the selected component source.`n")
      } ([ordered]@{ PackagePayload = ''; PackageCopyBranch = '' })
      Test-PackageSourceCase 'negative-source-bundle-missing' {
        param($root)
        Remove-Item -LiteralPath (Assert-PackageSourceChild (Join-Path $root ($componentBundleRelative + '/manifest.json')))
      } ([ordered]@{ PackagePayload = '^FAN_HOST_SOURCE_BUNDLE_UNAVAILABLE$'; PackageCopyBranch = '^FAN_HOST_SOURCE_BUNDLE_UNAVAILABLE$' })
      Test-PackageSourceCase 'negative-source-bundle-manifest-changed' {
        param($root)
        [IO.File]::AppendAllText((Assert-PackageSourceChild (Join-Path $root ($componentBundleRelative + '/manifest.json'))), "`n ")
      } ([ordered]@{ PackagePayload = '^FAN_HOST_SOURCE_BUNDLE_MANIFEST_CHANGED$'; PackageCopyBranch = '^FAN_HOST_SOURCE_BUNDLE_MANIFEST_CHANGED$' })
      Test-PackageSourceCase 'negative-source-bundle-artifact-pair-changed' {
        param($root)
        $path = Assert-PackageSourceChild (Join-Path $root ($componentBundleRelative + '/artifacts/YeManFanBackend.dll'))
        $stream = [IO.File]::Open($path, [IO.FileMode]::Append, [IO.FileAccess]::Write)
        try { $stream.WriteByte(0) } finally { $stream.Dispose() }
      } ([ordered]@{ PackagePayload = '^FAN_HOST_SOURCE_BUNDLE_MEMBER_CHANGED: artifacts/YeManFanBackend.dll$'; PackageCopyBranch = '^FAN_HOST_SOURCE_BUNDLE_MEMBER_CHANGED: artifacts/YeManFanBackend.dll$' })
      Test-PackageSourceCase 'negative-old-self-consistent-7f' {
        param($root)
        $payload = Join-Path $root 'PowerControl\fan-host'
        foreach ($file in Get-ChildItem -LiteralPath $payload -File -Force) { Remove-Item -LiteralPath (Assert-PackageSourceChild $file.FullName) }
        Copy-PackageSourcePayload $LegacyPayloadRoot $payload '94ff83cbcc2f703cb44c1bb5ce61138faef05c78b26b3d5337a5ca407a7af6b6'
        Set-PackageSourceNativePin $root '94ff83cbcc2f703cb44c1bb5ce61138faef05c78b26b3d5337a5ca407a7af6b6'
      } ([ordered]@{ BuildPreGate = ''; PackagePayload = 'manifest SHA-256 mismatch'; PackageCopyBranch = 'manifest SHA-256 mismatch' })
      Test-PackageSourceCase 'negative-new-manifest-old-host' {
        param($root)
        Copy-PackageSourceFile $legacyHost (Join-Path $root 'PowerControl\fan-host\YeManFanHost.dll')
      } ([ordered]@{ BuildPreGate = 'payload Host DLL'; PackagePayload = '(manifest file hash|pinned hash) mismatch: YeManFanHost.dll'; PackageCopyBranch = '(manifest file hash|pinned hash) mismatch: YeManFanHost.dll' })
      Test-PackageSourceCase 'negative-old-native-default-94ff' {
        param($root)
        Set-PackageSourceNativePin $root '94ff83cbcc2f703cb44c1bb5ce61138faef05c78b26b3d5337a5ca407a7af6b6'
      } ([ordered]@{ BuildPreGate = 'manifest' })
      Test-PackageSourceCase 'negative-mixed-backend' {
        param($root)
        Copy-PackageSourceFile (Join-Path $LegacyPayloadRoot 'YeManFanBackend.dll') (Join-Path $root 'PowerControl\fan-host\YeManFanBackend.dll')
      } ([ordered]@{ PackagePayload = '(manifest file hash|pinned hash) mismatch: YeManFanBackend.dll'; PackageCopyBranch = '(manifest file hash|pinned hash) mismatch: YeManFanBackend.dll' })
      Test-PackageSourceCase 'negative-missing-host' {
        param($root)
        Remove-Item -LiteralPath (Assert-PackageSourceChild (Join-Path $root 'PowerControl\fan-host\YeManFanHost.dll'))
      } ([ordered]@{ BuildPreGate = 'payload.*YeManFanHost.dll'; PackagePayload = 'manifest file missing: YeManFanHost.dll'; PackageCopyBranch = 'manifest file missing: YeManFanHost.dll' })
      Test-PackageSourceCase 'negative-extra-mock-flags' {
        param($root)
        foreach ($name in @('fan-host.mock.flag', 'fan-host.mock-test.flag')) {
          [IO.File]::WriteAllText((Assert-PackageSourceChild (Join-Path $root ('PowerControl\fan-host\' + $name))), 'scratch-only')
        }
      } ([ordered]@{ PackagePayload = 'files outside or missing from manifest'; PackageCopyBranch = 'files outside or missing from manifest' })
      $sourceToBreak = @($compiledPins.Keys | Where-Object { $_ -like 'FanLab/real-host/*.cs' } | Sort-Object)[0]
      if (-not $sourceToBreak) { throw 'Verifier compiledSourcePins has no real-host C# source for negative tests.' }
      Test-PackageSourceCase 'negative-compiled-source-missing' {
        param($root)
        Remove-Item -LiteralPath (Assert-PackageSourceChild (Join-Path $root ($componentBundleRelative + '/source/' + $sourceToBreak)))
      } ([ordered]@{ PackagePayload = ('^FAN_HOST_COMPILED_SOURCE_UNAVAILABLE: ' + [regex]::Escape($sourceToBreak) + '$'); PackageCopyBranch = ('^FAN_HOST_COMPILED_SOURCE_UNAVAILABLE: ' + [regex]::Escape($sourceToBreak) + '$') })
      Test-PackageSourceCase 'negative-compiled-source-changed' {
        param($root)
        [IO.File]::AppendAllText((Assert-PackageSourceChild (Join-Path $root ($componentBundleRelative + '/source/' + $sourceToBreak))), "`n// PackageSource scratch mutation`n")
      } ([ordered]@{ PackagePayload = ('^FAN_HOST_COMPILED_SOURCE_CHANGED: ' + [regex]::Escape($sourceToBreak) + '$'); PackageCopyBranch = ('^FAN_HOST_COMPILED_SOURCE_CHANGED: ' + [regex]::Escape($sourceToBreak) + '$') })
      foreach ($sourceLane in @('real-host', 'fan-backend')) {
        Test-PackageSourceCase ('negative-compiled-source-added-' + $sourceLane) {
          param($root)
          [IO.File]::WriteAllText((Assert-PackageSourceChild (Join-Path $root ($componentBundleRelative + '/source/FanLab/' + $sourceLane + '/PackageSourceUnexpected.cs'))), '// PackageSource scratch addition')
        } ([ordered]@{ PackagePayload = '^FAN_HOST_COMPILED_SOURCE_CLOSURE_CHANGED$'; PackageCopyBranch = '^FAN_HOST_COMPILED_SOURCE_CLOSURE_CHANGED$' })
      }
      foreach ($linkedSource in @('capabilities/FanCapabilityCatalog.cs', 'capabilities/AsusFanCapabilitySession.cs', 'capabilities/HcLightingCapabilityCatalog.cs', 'capabilities/HcLightingCapabilityCatalog.generated.cs')) {
        $linkedName = [IO.Path]::GetFileNameWithoutExtension($linkedSource)
        Test-PackageSourceCase ('negative-linked-source-missing-' + $linkedName) {
          param($root)
          Remove-Item -LiteralPath (Assert-PackageSourceChild (Join-Path $root ($componentBundleRelative + '/source/' + $linkedSource)))
        } ([ordered]@{ PackagePayload = ('^FAN_HOST_COMPILED_SOURCE_UNAVAILABLE: ' + [regex]::Escape($linkedSource) + '$'); PackageCopyBranch = ('^FAN_HOST_COMPILED_SOURCE_UNAVAILABLE: ' + [regex]::Escape($linkedSource) + '$') })
        Test-PackageSourceCase ('negative-linked-source-changed-' + $linkedName) {
          param($root)
          [IO.File]::AppendAllText((Assert-PackageSourceChild (Join-Path $root ($componentBundleRelative + '/source/' + $linkedSource))), "`n// PackageSource linked source mutation`n")
        } ([ordered]@{ PackagePayload = ('^FAN_HOST_COMPILED_SOURCE_CHANGED: ' + [regex]::Escape($linkedSource) + '$'); PackageCopyBranch = ('^FAN_HOST_COMPILED_SOURCE_CHANGED: ' + [regex]::Escape($linkedSource) + '$') })
      }
    } catch {
      $failures.Add('PackageSource setup/execution: ' + $_.Exception.Message)
    }
    $sharedInputs = @(
      foreach ($path in $originalInputs.Keys) {
        $after = if (Test-Path -LiteralPath $path -PathType Leaf) { (Get-Sha256 $path).ToLowerInvariant() } else { $null }
        if ($originalInputs[$path] -cne $after) { $failures.Add("Read-only input changed during PackageSource: $path") }
        [pscustomobject]@{ path = $path; before = $originalInputs[$path]; after = $after }
      }
    )
    $summary = [ordered]@{
      audit = 'FAN-PROMOTION-PACKAGE-SOURCE'; mode = 'PackageSource'; executedAtLocal = (Get-Date).ToString('o'); powerShellVersion = $PSVersionTable.PSVersion.ToString()
      projectRoot = $ProjectRoot; legacyPayloadRoot = $LegacyPayloadRoot; sandboxRoot = $sandboxRoot
      expectedPayload = $expectedPayload; compiledSourcePins = $compiledPins
      componentSourceSelection = [ordered]@{ archiveRelative = $componentBundleRelative; manifestSha256 = $componentBundleManifestSha256; liveSourceAcceptanceClaimed = $false }
      hostStarted = $false; fullBuildOrPackageInvoked = $false; realSessionOrLeaseRead = $false
      pass = @($cases | Where-Object { $_.passed }).Count; fail = $failures.Count
      cases = $cases.ToArray(); failed = $failures.ToArray(); readOnlyInputs = $sharedInputs; extractions = @($extractions.Values)
    }
    $resultPath = Join-Path $sandboxRoot 'fan-promotion-package-source-result.json'
    $summary | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $resultPath -Encoding UTF8
    Write-Output ("FAN promotion PackageSource: pass={0} fail={1}" -f $summary.pass, $summary.fail)
    foreach ($failure in $failures) { Write-Output ('  FAIL ' + $failure) }
    Write-Output ('Result: ' + $resultPath)
    if ($failures.Count -gt 0) { throw "PackageSource failed; inspect $resultPath" }
  }
  return
}

if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
  $OutputRoot = Join-Path (Split-Path -Parent $ProjectRoot) 'Build\Validation'
}
New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null

function Get-Sha256([string]$Path) {
  $sha = [Security.Cryptography.SHA256]::Create()
  try { return ([BitConverter]::ToString($sha.ComputeHash([IO.File]::ReadAllBytes($Path))).Replace('-', '')) }
  finally { $sha.Dispose() }
}

# ---------------------------------------------------------------- production gate
# Extract the production function definitions verbatim (AST-based, so no
# top-level build statements are executed) and run them against the sandboxes.
$gateFile = Join-Path $PSScriptRoot 'build-workspace.ps1'
$gateTokens = $null
$gateErrors = $null
$gateAst = [System.Management.Automation.Language.Parser]::ParseFile($gateFile, [ref]$gateTokens, [ref]$gateErrors)
if ($gateErrors.Count -ne 0) { throw ('build-workspace.ps1 does not parse: ' + $gateErrors[0].Message) }
$gateFunctions = $gateAst.FindAll({ param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst] }, $true)
$loaded = New-Object 'System.Collections.Generic.List[string]'
foreach ($function in $gateFunctions) {
  if ($function.Name -eq 'Get-Sha256' -or $function.Name -eq 'Assert-PublishPreGate') {
    Invoke-Expression $function.Extent.Text
    $loaded.Add($function.Name)
  }
}
if (-not (Get-Command Assert-PublishPreGate -ErrorAction SilentlyContinue) -or -not (Get-Command Get-Sha256 -ErrorAction SilentlyContinue)) {
  throw ('production pre-gate extraction did not define the required functions; loaded: ' + ($loaded -join ', '))
}

$passes = New-Object 'System.Collections.Generic.List[string]'
$failures = New-Object 'System.Collections.Generic.List[string]'

function Add-Pass([string]$Label) { $passes.Add($Label) }
function Add-Fail([string]$Label, [string]$Reason) { $failures.Add("$Label : $Reason") }

function Test-GateRefuses([string]$Label, [string]$Root, [string]$ExpectFragment) {
  $rejected = $false
  $message = ''
  try { Assert-PublishPreGate $Root } catch { $rejected = $true; $message = $_.Exception.Message }
  if (-not $rejected) { Add-Fail $Label 'production gate did NOT reject the inconsistent promotion'; return }
  if ($ExpectFragment.Length -gt 0 -and $message -notlike ("*" + $ExpectFragment + "*")) {
    Add-Fail $Label ("rejected but not for the expected reason: " + $message); return
  }
  Add-Pass $Label
}

# ---------------------------------------------------------------- sandbox helpers
$sandboxRoot = Join-Path $OutputRoot ('fan-promotion-negative-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N').Substring(0, 8))

function New-Sandbox([string]$Name) {
  $root = Join-Path $sandboxRoot $Name
  $fanHost = Join-Path $root 'PowerControl\fan-host'
  $nativeDir = Join-Path $root 'native'
  $bridgeDir = Join-Path $root 'src\bridge'
  New-Item -ItemType Directory -Force -Path $fanHost | Out-Null
  New-Item -ItemType Directory -Force -Path $nativeDir | Out-Null
  New-Item -ItemType Directory -Force -Path $bridgeDir | Out-Null
  Get-ChildItem -LiteralPath (Join-Path $ProjectRoot 'PowerControl\fan-host') -File -Force |
    Copy-Item -Destination $fanHost
  Copy-Item -LiteralPath (Join-Path $ProjectRoot 'src\bridge\fanHost.ts') -Destination $bridgeDir
  if ([IO.File]::ReadAllText((Join-Path $bridgeDir 'fanHost.ts')).Contains("from './fanHost/deployment'")) {
    $startupModules = Join-Path $bridgeDir 'fanHost'
    New-Item -ItemType Directory -Path $startupModules -Force | Out-Null
    foreach ($leaf in @('startupConfiguration.ts','deployment.ts','capabilityAdmission.ts')) {
      Copy-Item -LiteralPath (Join-Path $ProjectRoot ('src\bridge\fanHost\' + $leaf)) -Destination $startupModules
    }
  }
  Copy-Item -LiteralPath (Join-Path $ProjectRoot 'native\main.cpp') -Destination $nativeDir
  return $root
}

function Get-PayloadManifest([string]$Root) {
  $path = Join-Path $Root 'PowerControl\fan-host\YeManFanHost.payload.json'
  return (Get-Content -LiteralPath $path -Raw -Encoding UTF8).TrimStart([char]0xFEFF) | ConvertFrom-Json
}

function Set-PayloadDllDeclaredHash([string]$Root, [string]$HashLower) {
  $path = Join-Path $Root 'PowerControl\fan-host\YeManFanHost.payload.json'
  $manifest = Get-PayloadManifest $Root
  $changed = $false
  foreach ($entry in @($manifest.files)) {
    if ([string]$entry.path -eq 'YeManFanHost.dll') { $entry.sha256 = $HashLower; $changed = $true }
  }
  if (-not $changed) { throw 'sandbox payload manifest does not declare YeManFanHost.dll' }
  $manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $path -Encoding UTF8
}

function Set-MainCppManifestConstant([string]$Root, [string]$HashLower) {
  $path = Join-Path $Root 'native\main.cpp'
  $text = Get-Content -LiteralPath $path -Raw -Encoding UTF8
  $pattern = "expectedFanHostV2ManifestSha256 = '[0-9a-fA-F]{64}'"
  if ($text -notmatch $pattern) { throw 'sandbox main.cpp has no manifest constant' }
  $text = [regex]::Replace($text, $pattern, ("expectedFanHostV2ManifestSha256 = '" + $HashLower + "'"))
  Set-Content -LiteralPath $path -Value $text -Encoding UTF8 -NoNewline
}

function Get-PayloadManifestHash([string]$Root) {
  return (Get-Sha256 (Join-Path $Root 'PowerControl\fan-host\YeManFanHost.payload.json')).ToLowerInvariant()
}

# The pre-920 mainline payload identity (no H1 short-circuit), used as the "old"
# side of the mismatches. Recorded in EXECUTION.md / MANIFEST.json.
$oldDllHash = '14f16eb527f1175018ddfed3e628b68a06f5e67c9cbb701ade0b4c8f0be9f289'

# ------------------------------------------------------------------ case 1 and 2
# Case: old manifest declared hash + new DLL on disk (stale manifest).
$case1 = New-Sandbox 'case1-old-manifest-new-dll'
Set-PayloadDllDeclaredHash $case1 $oldDllHash
Set-MainCppManifestConstant $case1 (Get-PayloadManifestHash $case1)
Test-GateRefuses 'F01-old-manifest-declares-old-dll-while-disk-is-new' $case1 'payload Host DLL'

# Case: the package lost the Host DLL but still declares it.
$case2 = New-Sandbox 'case2-package-missing-dll'
Remove-Item -LiteralPath (Join-Path $case2 'PowerControl\fan-host\YeManFanHost.dll') -Force
Set-MainCppManifestConstant $case2 (Get-PayloadManifestHash $case2)
Test-GateRefuses 'F02-package-missing-host-dll' $case2 'payload'

# ------------------------------------------------------------------ case 3
# Case: new native (expects the approved manifest) + old payload on disk.
$case3 = New-Sandbox 'case3-new-native-old-payload'
Set-PayloadDllDeclaredHash $case3 $oldDllHash
Test-GateRefuses 'F03-new-native-with-old-payload' $case3 'manifest'

# ------------------------------------------------------------------ case 4
# Case: the payload passes the gate, then a file is replaced afterwards.
$case4 = New-Sandbox 'case4-file-replaced-after-check'
$case4Passed = $false
try { Assert-PublishPreGate $case4 | Out-Null; $case4Passed = $true } catch { $case4Passed = $false }
if (-not $case4Passed) {
  Add-Fail 'F04a-pristine-payload-passes-gate' 'the pristine reviewed payload did not pass the gate (positive control failed)'
} else {
  Add-Pass 'F04a-pristine-payload-passes-gate'
  $dllPath = Join-Path $case4 'PowerControl\fan-host\YeManFanHost.dll'
  $bytes = [IO.File]::ReadAllBytes($dllPath)
  $bytes[$bytes.Length - 1] = [byte](($bytes[$bytes.Length - 1] + 1) -band 0xFF)
  [IO.File]::WriteAllBytes($dllPath, $bytes)
  Test-GateRefuses 'F04b-file-replaced-after-gate-passed' $case4 'payload Host DLL'
}

# ------------------------------------------------------------------ case 5
# FAN-926R §2.2: an old Host keeps running while the disk copy is updated, so the
# session must be able to prove which binary it actually loaded: the Host records
# a load-time identity (assemblyMvid) separately from the startup disk hash.
#
# The check must consume a REAL production start. It must not use --self-test as
# the evidence source: the self-test never writes host.identity (it is emitted by
# Program.Main's production start) and it deliberately isolates its own log root.
# This case therefore starts ONE test-owned isolated instance through the same
# production entry (mock handshake; no HC/EC work without real hardware), with a
# test-owned port, token, log root, flag and state directory. The user's real
# log/flag/token are never read or written.
function Get-Sha256Hex([string]$Path) {
  $sha = [Security.Cryptography.SHA256]::Create()
  try { return ([BitConverter]::ToString($sha.ComputeHash([IO.File]::ReadAllBytes($Path)))).Replace('-', '').ToLowerInvariant() } finally { $sha.Dispose() }
}

# Returns the reasons this run's evidence is NOT acceptable (empty == acceptable).
function Test-HostIdentityEvidence([string]$LogText, [string]$ExpectedDllSha, [string]$ExpectedManifestSha) {
  $problems = @()
  if ([string]::IsNullOrWhiteSpace($LogText)) { return @('no runtime evidence for this run') }
  $rows = @([regex]::Matches($LogText, '(?m)^.*"eventName":"host\.identity".*$'))
  if ($rows.Count -lt 1) { return @('no host.identity row in this run') }
  $row = $rows[$rows.Count - 1].Value
  foreach ($needle in @('"assemblyMvid"', '"assemblyFileSha256"', '"payloadManifestSha256"', '"payloadDeclaredDllSha256"', '"assemblyMatchesPayload"', '"evidenceScope"')) {
    if (-not $row.Contains($needle)) { $problems += ('missing ' + $needle) }
  }
  $mvid = [regex]::Match($row, '"assemblyMvid":"([0-9a-fA-F]{32})"')
  if (-not $mvid.Success) { $problems += 'assemblyMvid is not a 32-hex load-time id' }
  $dll = [regex]::Match($row, '"assemblyFileSha256":"([0-9a-fA-F]{64})"')
  if (-not $dll.Success) { $problems += 'assemblyFileSha256 is not a 64-hex hash' }
  elseif ($ExpectedDllSha -and $dll.Groups[1].Value.ToLowerInvariant() -ne $ExpectedDllSha.ToLowerInvariant()) {
    $problems += 'assemblyFileSha256 does not match the expected DLL'
  }
  $manifest = [regex]::Match($row, '"payloadManifestSha256":"([0-9a-fA-F]{64})"')
  if (-not $manifest.Success) { $problems += 'payloadManifestSha256 is not a 64-hex hash' }
  elseif ($ExpectedManifestSha -and $manifest.Groups[1].Value.ToLowerInvariant() -ne $ExpectedManifestSha.ToLowerInvariant()) {
    $problems += 'payloadManifestSha256 does not match the expected manifest'
  }
  if (-not $row.Contains('"assemblyMatchesPayload":"true"')) { $problems += 'assemblyMatchesPayload is not true' }
  if (-not $row.Contains('assemblyMvid=load-time')) { $problems += 'evidenceScope does not separate load-time from startup-disk' }
  return $problems
}

$payloadDir = Join-Path $ProjectRoot 'PowerControl\fan-host'
$payloadExe = Join-Path $payloadDir 'YeManFanHost.exe'
$payloadDll = Join-Path $payloadDir 'YeManFanHost.dll'
$payloadManifest = Join-Path $payloadDir 'YeManFanHost.payload.json'
if (-not (Test-Path -LiteralPath $payloadExe -PathType Leaf) -or -not (Test-Path -LiteralPath $payloadDll -PathType Leaf)) {
  Add-Fail 'F05-host-load-time-identity' ('fan payload not found under ' + $payloadDir)
} else {
  $realFlag = Join-Path $env:LOCALAPPDATA 'YeManCC\fan-host\fan-logging-enabled.flag'
  $realFlagBefore = if (Test-Path -LiteralPath $realFlag) { Get-Sha256Hex $realFlag } else { 'ABSENT' }
  $isoRoot = Join-Path ([IO.Path]::GetTempPath()) ('fan-f05-iso-' + [guid]::NewGuid().ToString('N'))
  New-Item -ItemType Directory -Force -Path $isoRoot | Out-Null
  Set-Content -LiteralPath (Join-Path $isoRoot 'fan-logging-enabled.flag') -Value 'enabled' -Encoding ASCII -NoNewline
  $token = -join (1..32 | ForEach-Object { '{0:x2}' -f (Get-Random -Minimum 0 -Maximum 256) })
  $tokenFile = Join-Path $isoRoot 'session.token'
  Set-Content -LiteralPath $tokenFile -Value $token -Encoding ASCII -NoNewline
  $probe = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, 0)
  $probe.Start()
  $port = ([System.Net.IPEndPoint]$probe.LocalEndpoint).Port
  $probe.Stop()
  $runtimeLog = Join-Path $isoRoot 'yeman-fan-host-runtime.log'
  $previousRoot = $env:YEMANCC_SELFTEST_LOG_ROOT
  $env:YEMANCC_SELFTEST_LOG_ROOT = $isoRoot
  $instance = $null
  $healthy = $false
  try {
    $instance = Start-Process -FilePath $payloadExe -ArgumentList @('--port', "$port", '--protocol-version', '2', '--session-token-file', $tokenFile, '--mock-handshake') -PassThru -WindowStyle Hidden
    for ($i = 0; $i -lt 120 -and -not $healthy; $i += 1) {
      try {
        $health = Invoke-RestMethod -Uri ("http://127.0.0.1:" + $port + "/health") -Headers @{ 'X-YeMan-Fan-Session' = $token } -TimeoutSec 1
        if ($health.host -eq 'YeManFanHost') { $healthy = $true; break }
      } catch { }
      Start-Sleep -Milliseconds 250
    }
    for ($i = 0; $i -lt 80; $i += 1) {
      if ((Test-Path -LiteralPath $runtimeLog) -and (Get-Content -LiteralPath $runtimeLog -Raw).Contains('"eventName":"host.identity"')) { break }
      Start-Sleep -Milliseconds 250
    }
  } finally {
    if ($instance) {
      try { if (-not $instance.HasExited) { Stop-Process -Id $instance.Id -Force } } catch { }
      try { $instance.WaitForExit(8000) | Out-Null } catch { }
    }
    $env:YEMANCC_SELFTEST_LOG_ROOT = $previousRoot
  }
  $evidenceText = if (Test-Path -LiteralPath $runtimeLog) { Get-Content -LiteralPath $runtimeLog -Raw } else { '' }
  $dllSha = Get-Sha256Hex $payloadDll
  $manifestSha = if (Test-Path -LiteralPath $payloadManifest -PathType Leaf) { Get-Sha256Hex $payloadManifest } else { '' }
  $problems = @(Test-HostIdentityEvidence $evidenceText $dllSha $manifestSha)
  # Negative controls: a wrong expected identity, or evidence from another run,
  # must both be rejected by the same rule.
  $wrongAccepted = @(Test-HostIdentityEvidence $evidenceText ('0' * 64) ('0' * 64)).Count -eq 0
  $emptyAccepted = @(Test-HostIdentityEvidence '' $dllSha $manifestSha).Count -eq 0
  if ($wrongAccepted) { $problems += 'negative control: a wrong expected identity was accepted' }
  if ($emptyAccepted) { $problems += 'negative control: empty / other-run evidence was accepted' }
  if (-not $healthy) { $problems += 'the isolated production instance never became reachable on its own port' }
  $flagAfter = if (Test-Path -LiteralPath $realFlag) { Get-Sha256Hex $realFlag } else { 'ABSENT' }
  if ($flagAfter -ne $realFlagBefore) { $problems += 'the user real logging flag changed during F05' }
  $residual = @(Get-Process -Name YeManFanHost -ErrorAction SilentlyContinue | Where-Object {
    try { [IO.Path]::GetFullPath($_.Path) -ieq [IO.Path]::GetFullPath($payloadExe) } catch { $false } }).Count
  if ($residual -ne 0) { $problems += 'a Host process from this payload remained after the case' }
  if (-not $healthy) {
    Add-Fail 'F05-host-load-time-identity' ('F05_ENV_UNAVAILABLE: ' + ($problems -join '; ') + ' | isoRoot=' + $isoRoot)
  } elseif ($problems.Count -gt 0) {
    Add-Fail 'F05-host-load-time-identity' "host.identity evidence rejected for this run: $($problems -join '; ')"
  } else {
    Add-Pass 'F05-host-load-time-identity'
  }
  Write-Output ('F05 isolated production instance: port=' + $port + ' healthy=' + $healthy + ' dll=' + $dllSha.Substring(0, 16) + ' manifest=' + $manifestSha.Substring(0, 16) + ' isoRoot=' + $isoRoot)
}

# ------------------------------------------------------------------ case 6
# Case: the export only carries the apphost identity. Must be UNVERIFIABLE, not PASS.
$exportVerifier = Join-Path $PSScriptRoot 'verify-yemancc-export.ps1'
$exportEvidence = Join-Path $OutputRoot 'export-apphost-only-negative.json'
$nativeBuildExe = Join-Path (Split-Path -Parent (Split-Path -Parent $ProjectRoot)) 'Build\App\Native\YeManCC.exe'
if (-not (Test-Path -LiteralPath $exportVerifier -PathType Leaf)) {
  Add-Fail 'F06-export-apphost-only-is-unverifiable' "export verifier missing: $exportVerifier"
} elseif (-not (Test-Path -LiteralPath $nativeBuildExe -PathType Leaf)) {
  Add-Fail 'F06-export-apphost-only-is-unverifiable' "build native missing: $nativeBuildExe"
} else {
  & powershell -NoProfile -ExecutionPolicy Bypass -File $exportVerifier -Paths @($nativeBuildExe) -EvidencePath $exportEvidence | Out-Null
  $exportExit = $LASTEXITCODE
  $recorded = (Get-Content -LiteralPath $exportEvidence -Raw -Encoding UTF8).TrimStart([char]0xFEFF) | ConvertFrom-Json
  if ($exportExit -eq 0 -or [string]$recorded.status -ne 'UNVERIFIABLE') {
    Add-Fail 'F06-export-apphost-only-is-unverifiable' ("expected UNVERIFIABLE/exit!=0, got status=" + [string]$recorded.status + " exit=" + $exportExit)
  } else {
    Add-Pass 'F06-export-apphost-only-is-unverifiable'
  }
}

# ------------------------------------------------------- 920-v1.2 W3 staged gates
# Cases F07-F10 drive the REAL tools/verify-yemancc-export.ps1 against constructed
# workspaces, to prove the staged admission verdicts behave per section 14.4 -
# including that "candidate package correct but device not deployed" is NOT a
# circular dependency.
Add-Type -AssemblyName System.IO.Compression.FileSystem

$workspaceRoot = Split-Path -Parent (Split-Path -Parent $ProjectRoot)
$realRelease = Join-Path $workspaceRoot 'Release'
$realNative = $nativeBuildExe
$realDll = Join-Path $realRelease 'PowerControl\fan-host-v2\YeManFanHost.dll'
$realManifest = Join-Path $realRelease 'PowerControl\fan-host-v2\YeManFanHost.payload.json'
$realCredential = Join-Path $workspaceRoot 'Build\Validation\opt-gate\BUILD__build.json'
$realHc = @(Get-ChildItem -LiteralPath (Join-Path $realRelease 'PowerControl\handheldcompanion-runtime') -Recurse -Filter 'HandheldCompanion.dll' -File -ErrorAction SilentlyContinue) | Select-Object -First 1

function New-ExportSandbox([string]$Name) {
  $sb = Join-Path $sandboxRoot $Name
  $stage = Join-Path $sb 'stage'
  New-Item -ItemType Directory -Force -Path (Join-Path $stage 'YeManCC') | Out-Null
  New-Item -ItemType Directory -Force -Path (Join-Path $stage 'PowerControl\fan-host-v2') | Out-Null
  New-Item -ItemType Directory -Force -Path (Join-Path $sb 'Build\App\Native') | Out-Null
  New-Item -ItemType Directory -Force -Path (Join-Path $sb 'Build\Validation\opt-gate') | Out-Null
  Copy-Item -LiteralPath $realNative -Destination (Join-Path $stage 'YeManCC\YeManCC.exe')
  Copy-Item -LiteralPath $realNative -Destination (Join-Path $sb 'Build\App\Native\YeManCC.exe')
  Copy-Item -LiteralPath $realDll -Destination (Join-Path $stage 'PowerControl\fan-host-v2\YeManFanHost.dll')
  Copy-Item -LiteralPath $realManifest -Destination (Join-Path $stage 'PowerControl\fan-host-v2\YeManFanHost.payload.json')
  if (Test-Path -LiteralPath $realCredential -PathType Leaf) {
    Copy-Item -LiteralPath $realCredential -Destination (Join-Path $sb 'Build\Validation\opt-gate\BUILD__build.json')
  }
  if ($realHc) {
    $hcDir = Join-Path $stage ('PowerControl\handheldcompanion-runtime\' + $realHc.Directory.Name)
    New-Item -ItemType Directory -Force -Path $hcDir | Out-Null
    Copy-Item -LiteralPath $realHc.FullName -Destination (Join-Path $hcDir 'HandheldCompanion.dll')
  }
  return [pscustomobject]@{ root = $sb; stage = $stage }
}

function Complete-ExportSandbox($Sandbox, [string]$ZipSourceDir) {
  if (-not $ZipSourceDir) { $ZipSourceDir = $Sandbox.stage }
  $zipDir = Join-Path $Sandbox.root 'Release\Packages'
  New-Item -ItemType Directory -Force -Path $zipDir | Out-Null
  $zipPath = Join-Path $zipDir 'YeManCC.zip'
  if (Test-Path -LiteralPath $zipPath) { Remove-Item -LiteralPath $zipPath -Force }
  [IO.Compression.ZipFile]::CreateFromDirectory($ZipSourceDir, $zipPath, [IO.Compression.CompressionLevel]::Optimal, $false)
  # Delivery LOCATIONS always keep the approved (real) payload: only the zip content
  # is mutated by the negative cases, so the "approved" side never drifts with it.
  foreach ($tree in @('Release', 'Build\Package\UpdateRoot')) {
    $dst = Join-Path $Sandbox.root $tree
    New-Item -ItemType Directory -Force -Path $dst | Out-Null
    Copy-Item -LiteralPath (Join-Path $Sandbox.stage 'YeManCC') -Destination $dst -Recurse -Force
    Copy-Item -LiteralPath (Join-Path $Sandbox.stage 'PowerControl') -Destination $dst -Recurse -Force
  }
}

# A mutable copy of the approved tree, used ONLY as the zip's content source.
function New-ZipStage($Sandbox) {
  $zipStage = Join-Path $Sandbox.root 'zipstage'
  if (Test-Path -LiteralPath $zipStage) { Remove-Item -LiteralPath $zipStage -Recurse -Force }
  Copy-Item -LiteralPath $Sandbox.stage -Destination $zipStage -Recurse -Force
  return $zipStage
}

function Invoke-StagedVerdicts($Sandbox) {
  $evidence = Join-Path $Sandbox.root 'export-evidence.json'
  $installRoot = Join-Path $Sandbox.root 'not-deployed'
  & powershell -NoProfile -ExecutionPolicy Bypass -File $exportVerifier `
    -WorkspaceRoot $Sandbox.root -InstallRoot $installRoot -EvidencePath $evidence | Out-Null
  $script:stagedExit = $LASTEXITCODE
  return ((Get-Content -LiteralPath $evidence -Raw -Encoding UTF8).TrimStart([char]0xFEFF) | ConvertFrom-Json)
}

function Test-Stage([string]$Label, $Record, [string]$StageName, [bool]$ExpectedReady) {
  $actual = [bool]$Record.stagedVerdicts.$StageName.ready
  if ($actual -eq $ExpectedReady) { Add-Pass $Label }
  else { Add-Fail $Label ("stage " + $StageName + " expected ready=" + $ExpectedReady + " but got " + $actual + " :: " + [string]$Record.stagedVerdicts.$StageName.reason) }
}

function Set-EntryBytes([string]$Path) {
  $bytes = [IO.File]::ReadAllBytes($Path)
  $bytes[$bytes.Length - 1] = [byte](($bytes[$bytes.Length - 1] + 1) -band 0xFF)
  [IO.File]::WriteAllBytes($Path, $bytes)
}

if (-not (Test-Path -LiteralPath $realNative -PathType Leaf) -or -not (Test-Path -LiteralPath $realDll -PathType Leaf)) {
  Add-Fail 'F07-F10-staged-gates' 'fixtures unavailable (build native or Release payload missing)'
} else {
  # F07: every delivery location is old while a new build exists -> the package is
  # NOT ready even though a fresh build is present.
  $f07 = New-ExportSandbox 'f07-all-old-new-build-exists'
  $z07 = New-ZipStage $f07
  Set-EntryBytes (Join-Path $z07 'YeManCC\YeManCC.exe')
  Complete-ExportSandbox $f07 $z07
  $r07 = Invoke-StagedVerdicts $f07
  Test-Stage 'F07-all-old-with-new-build' $r07 'FullTestPackageReady' $false

  # F08: new native + a SELF-CONSISTENT old Host payload inside the zip. The old
  # payload must not pass just because it agrees with itself.
  $f08 = New-ExportSandbox 'f08-new-native-old-host'
  $z08 = New-ZipStage $f08
  Set-EntryBytes (Join-Path $z08 'PowerControl\fan-host-v2\YeManFanHost.dll')
  $oldDllHashInZip = (Get-Sha256 (Join-Path $z08 'PowerControl\fan-host-v2\YeManFanHost.dll')).ToLowerInvariant()
  $zipManifestPath = Join-Path $z08 'PowerControl\fan-host-v2\YeManFanHost.payload.json'
  $zipManifestObj = (Get-Content -LiteralPath $zipManifestPath -Raw -Encoding UTF8).TrimStart([char]0xFEFF) | ConvertFrom-Json
  foreach ($entry in @($zipManifestObj.files)) {
    if ([string]$entry.path -eq 'YeManFanHost.dll') { $entry.sha256 = $oldDllHashInZip }
  }
  $zipManifestObj | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $zipManifestPath -Encoding UTF8
  # prove the zip's payload is internally self-consistent (dll matches its manifest)
  $selfConsistent = ([string](@($zipManifestObj.files) | Where-Object { [string]$_.path -eq 'YeManFanHost.dll' })[0].sha256) -eq $oldDllHashInZip
  Complete-ExportSandbox $f08 $z08
  $r08 = Invoke-StagedVerdicts $f08
  Test-Stage 'F08-new-native-self-consistent-old-host' $r08 'FullTestPackageReady' $false
  if ($selfConsistent -and -not [bool]$r08.stagedVerdicts.FullTestPackageReady.ready) {
    Add-Pass 'F08b-zip-payload-was-self-consistent-yet-rejected'
  } else {
    Add-Fail 'F08b-zip-payload-was-self-consistent-yet-rejected' ('selfConsistent=' + $selfConsistent + ' ready=' + [bool]$r08.stagedVerdicts.FullTestPackageReady.ready)
  }

  # F09: HC runtime missing, then present but wrong -> both must be NOT ready.
  $f09 = New-ExportSandbox 'f09-hc-runtime-missing'
  $z09 = New-ZipStage $f09
  Remove-Item -LiteralPath (Join-Path $z09 'PowerControl\handheldcompanion-runtime') -Recurse -Force
  Complete-ExportSandbox $f09 $z09
  $r09 = Invoke-StagedVerdicts $f09
  Test-Stage 'F09a-hc-runtime-missing' $r09 'FullTestPackageReady' $false

  $f09b = New-ExportSandbox 'f09b-hc-runtime-wrong'
  $z09b = New-ZipStage $f09b
  Set-EntryBytes (Join-Path $z09b ('PowerControl\handheldcompanion-runtime\' + $realHc.Directory.Name + '\HandheldCompanion.dll'))
  Complete-ExportSandbox $f09b $z09b
  $r09b = Invoke-StagedVerdicts $f09b
  Test-Stage 'F09b-hc-runtime-wrong-hash' $r09b 'FullTestPackageReady' $false

  # F10: a correct candidate package with NO deployment must be allowed to report
  # the package ready while the device stays unverified - otherwise the gate would
  # depend on the deployment it is supposed to gate.
  $f10 = New-ExportSandbox 'f10-correct-package-not-deployed'
  Complete-ExportSandbox $f10
  $r10 = Invoke-StagedVerdicts $f10
  Test-Stage 'F10a-correct-package-is-ready-without-deployment' $r10 'FullTestPackageReady' $true
  Test-Stage 'F10b-device-still-unverified' $r10 'DeviceIdentityVerified' $false
  Test-Stage 'F10c-candidate-ready-without-deployment' $r10 'CandidateReady' $true
}

# ------------------------------------------------------------------ result
$summary = [ordered]@{
  audit = 'FAN-920-PROMOTION-NEGATIVE'
  contract = '920-v1.2'
  executedAtLocal = (Get-Date).ToString('yyyy-MM-dd HH:mm:ss')
  sandboxRoot = $sandboxRoot
  pass = $passes.Count
  fail = $failures.Count
  passed = @($passes)
  failed = @($failures)
}
$resultPath = Join-Path $OutputRoot 'fan-promotion-negative-result.json'
$summary | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $resultPath -Encoding UTF8

Write-Output ("FAN-920 promotion negative cases: pass={0} fail={1}" -f $passes.Count, $failures.Count)
foreach ($p in $passes) { Write-Output ("  PASS " + $p) }
foreach ($f in $failures) { Write-Output ("  FAIL " + $f) }
Write-Output ("Result: " + $resultPath)
if ($failures.Count -gt 0) { exit 1 }
