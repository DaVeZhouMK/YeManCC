$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..\..')).Path
$root = Join-Path $PSScriptRoot '..\deps\handheldcompanion-runtime\source\Managers'
function Get-Sha256([string]$path) { $sha=[Security.Cryptography.SHA256]::Create(); try {$stream=[IO.File]::OpenRead($path); try {return ([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-','')} finally {$stream.Dispose()}} finally {$sha.Dispose()} }
function Find-Anchor([string]$path, [string]$pattern) {
  $match = Select-String -LiteralPath $path -Pattern $pattern | Select-Object -First 1
  if ($null -eq $match) { throw "Missing source anchor: $(Split-Path $path -Leaf) :: $pattern" }
  return [ordered]@{ pattern=$pattern; line=[int]$match.LineNumber; text=$match.Line.Trim() }
}
$controller=Join-Path $root 'ControllerManager.cs'; $motion=Join-Path $root 'MotionManager.cs'; $virtual=Join-Path $root 'VirtualManager.cs'
$traces=@(
  [ordered]@{ parityId='HC-CONTROLLER-UPDATE-001'; source=$controller; anchors=@(
    (Find-Anchor $controller 'SensorsManager\.UpdateReport\(controllerState'),
    (Find-Anchor $controller 'MotionManager\.UpdateReport\(controllerState'),
    (Find-Anchor $controller 'VirtualManager\.UpdateInputs\(mapped')
  ) },
  [ordered]@{ parityId='HC-MOTION-CALIBRATED-002'; source=$motion; anchors=@(
    (Find-Anchor $motion 'SetupMotion\(controllerState, gamepadMotion\)'),
    (Find-Anchor $motion 'ProcessMotion\(controllerState, gamepadMotion, delta\)')
  ) },
  [ordered]@{ parityId='HC-CONTROLLER-CLEAR-004'; source=$controller; anchors=@(
    (Find-Anchor $controller 'ControllerUnplugged\?\.Invoke\(controller'),
    (Find-Anchor $controller 'Controllers\.TryRemove\('),
    (Find-Anchor $controller 'controller\.Dispose\(\)')
  ) },
  [ordered]@{ parityId='HC-VIRTUAL-TARGET-005'; source=$virtual; anchors=@(
    (Find-Anchor $virtual 'bool success = vTarget\.Disconnect\(\)'),
    (Find-Anchor $virtual 'vTarget\.Dispose\(\)'),
    (Find-Anchor $virtual 'vTarget\?\.UpdateInputs\(controllerState, gamepadMotion\)')
  ) }
)
foreach($trace in $traces){$trace.sha256=Get-Sha256 $trace.source; $trace.source=$trace.source; $trace.referentialOrderOnly=$true}
$out=Join-Path $repo 'Mainline\Build\Validation\HC-Parity\A1-hc-input-order-audit-20260903.json'
$result=[ordered]@{evidenceId='A1-HC-INPUT-ORDER-AUDIT-20260903';status='PASS';systemMutation=$false;traceCount=$traces.Count;traces=$traces;conclusion='Static source anchors record observed HC call/cleanup ordering only. They do not invoke HC, prove adapter equivalence, or close the input-only ManagerFactory isolation gate.';generatedUtc=(Get-Date).ToUniversalTime().ToString('o')}
$result|ConvertTo-Json -Depth 8|Set-Content -LiteralPath $out -Encoding UTF8
Write-Output "A1 HC INPUT ORDER AUDIT: PASS traces=$($traces.Count)"
Write-Output "Evidence: $out"
