<# The shared global runner invokes this entry beneath its existing gated,
   kill-on-close Windows Job. Keep all descendants in that Job; the full
   window/WebView selftest is excluded. #>
[CmdletBinding()]
param(
 [Parameter(Mandatory=$true)][string]$EvidenceDir,
 [Parameter(Mandatory=$true)][string]$NodePath
)
$ErrorActionPreference='Stop'
$radialEnvironmentKeys=@('NODE_OPTIONS','NODE_PATH','PSModulePath')
$radialPreviousEnvironment=@{}
foreach($radialKey in $radialEnvironmentKeys){$radialPreviousEnvironment[$radialKey]=[Environment]::GetEnvironmentVariable($radialKey,'Process')}
$radialExit=2
try {
 $env:PSModulePath=Join-Path $PSHOME 'Modules'
 foreach($radialKey in @('NODE_OPTIONS','NODE_PATH')){[Environment]::SetEnvironmentVariable($radialKey,$null,'Process')}
 $radialManifestPath=Join-Path $PSScriptRoot 'toolchain-inputs.json'
 $radialManifest=Get-Content -LiteralPath $radialManifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
 $radialActualPowerShell=[IO.Path]::GetFullPath([Environment]::ProcessPath)
 if(-not $radialActualPowerShell.Equals([IO.Path]::GetFullPath($radialManifest.executables.powershell.path),[StringComparison]::OrdinalIgnoreCase)){throw 'RADIAL_POWERSHELL_EXECUTABLE_MISMATCH'}
 if(-not ([IO.Path]::GetFullPath($NodePath)).Equals([IO.Path]::GetFullPath($radialManifest.executables.node.path),[StringComparison]::OrdinalIgnoreCase)){throw 'RADIAL_NODE_EXECUTABLE_MISMATCH'}
 foreach($radialExecutable in @($radialManifest.executables.node,$radialManifest.executables.powershell)){
  if((Get-FileHash -LiteralPath $radialExecutable.path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $radialExecutable.sha256){throw 'RADIAL_BOOTSTRAP_EXECUTABLE_HASH_MISMATCH'}
 }
 # All node/cl/link/native descendants inherit the shared runner's Job.
 & $NodePath (Join-Path $PSScriptRoot 'run.mjs') --evidence-dir $EvidenceDir --powershell-path $radialActualPowerShell
 $radialNodeExit=$LASTEXITCODE
 $radialResultPath=Join-Path $EvidenceDir 'result.json'
 $radialResult=Get-Content -LiteralPath $radialResultPath -Raw -Encoding UTF8 | ConvertFrom-Json
 $radialPass=$radialNodeExit -eq 0 -and $radialResult.schemaVersion -eq 2 -and $radialResult.scopeVersion -eq 2 -and $radialResult.status -eq 'PASS' -and $radialResult.passed -eq $true -and $radialResult.postCreateStage.passed -eq $true -and $radialResult.mainReturnStage.passed -eq $true
 $radialWrapper=[ordered]@{
  schemaVersion=2
  scopeVersion=2
  id='controller.radial-native-consumer'
  status=$(if($radialPass){'PASS'}else{'FAIL'})
  passed=$radialPass
  nodePid=$radialResult.node.pid
  nodeExitCode=$radialNodeExit
  powershellPid=$PID
  powershellPath=$radialActualPowerShell
  ownedJobExpectedFromGlobalPowershellDescriptor=$true
  sourceHashes=@(@{path=$radialResultPath;sha256=(Get-FileHash -LiteralPath $radialResultPath -Algorithm SHA256).Hash.ToLowerInvariant()})
 }
 $radialTerminalPath=Join-Path $EvidenceDir 'wrapper-terminal.json'
 $radialStream=[IO.File]::Open($radialTerminalPath,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
 try {$radialBytes=[Text.UTF8Encoding]::new($false).GetBytes(($radialWrapper | ConvertTo-Json -Depth 8)+[Environment]::NewLine);$radialStream.Write($radialBytes,0,$radialBytes.Length)} finally {$radialStream.Dispose()}
 if($radialPass){Write-Output 'RADIAL_NATIVE_CONSUMER_V2_COMPLETED postCreate=12+2 mainReturn=12+2';$radialExit=0}
} catch {
 [Console]::Error.WriteLine($_.Exception.ToString())
 $radialExit=2
} finally {
 foreach($radialKey in $radialEnvironmentKeys){[Environment]::SetEnvironmentVariable($radialKey,$radialPreviousEnvironment[$radialKey],'Process')}
}
exit $radialExit
