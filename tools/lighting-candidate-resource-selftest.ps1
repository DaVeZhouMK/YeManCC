param([Parameter(Mandatory=$true)][string]$ProjectRoot,[Parameter(Mandatory=$true)][string]$WorkspaceRoot,[Parameter(Mandatory=$true)][string]$EvidenceDir)
$ErrorActionPreference='Stop'
New-Item -ItemType Directory -Path $EvidenceDir -ErrorAction Stop|Out-Null
$repo=[IO.Path]::GetFullPath($ProjectRoot);$work=[IO.Path]::GetFullPath($WorkspaceRoot)
. (Join-Path $repo 'tools/lighting-build-contract.ps1')
. (Join-Path $repo 'tools/lighting-candidate-source.ps1')
$record=Get-Content -LiteralPath (Join-Path $work 'Build/App/LightSetter/CANDIDATE-ACCEPTANCE.json') -Raw -Encoding UTF8|ConvertFrom-Json
$helper=Join-Path $EvidenceDir 'helper.dll';Copy-Item -LiteralPath (Join-Path $work (@($record.runtimeFiles|Where-Object name -CEQ 'YeManLightSetter.dll')[0].path)) -Destination $helper
$refused=$false;$failure=$null
try{Write-YmccLightingCandidateSourceProof -ProjectRoot $repo -WorkspaceRoot $work -HelperPath $helper -PdbPath (Join-Path $EvidenceDir 'missing.pdb') -SourceInputs $record.sourceInputs -OutputPath (Join-Path $EvidenceDir 'should-not-exist.json')|Out-Null}catch{$refused=$true;$failure=$_.Exception.Message}
$released=$false;$stream=$null;$lockFailure=$null
try{$stream=[IO.File]::Open($helper,[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None);$released=$true}catch{$lockFailure=$_.Exception.Message}finally{if($null -ne $stream){$stream.Dispose()}}
$r=[ordered]@{status=$(if($refused -and $released){'PASS'}else{'FAIL'});missingPdbRefused=$refused;helperFileReleased=$released;failure=$failure;lockFailure=$lockFailure;approvalCreated=(Test-Path -LiteralPath (Join-Path $EvidenceDir 'should-not-exist.json'));verifierSha256=(Get-YmccLightingFileHash (Join-Path $repo 'tools/lighting-candidate-source.ps1'));scope='missing-symbol-error-terminal-and-owned-handle-release-only';actualGCanonicalWritten=$false;actualCWritten=$false;hardwareIo=$false}
[IO.File]::WriteAllText((Join-Path $EvidenceDir 'result.json'),($r|ConvertTo-Json),[Text.UTF8Encoding]::new($false))
$r|ConvertTo-Json
if($r.status -ne 'PASS'){exit 2}