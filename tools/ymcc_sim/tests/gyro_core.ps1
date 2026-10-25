# gyro_core.ps1 - S1–S3/S12 的共用 core「只读」检查（204-M）：合成链/阈值/校准/采样通道的可核对锚点。
# 说明：真正跑合成链需要 App + 钩子（owner: W3 前端）；本脚本只断言"通道与观测面存在 + 判读纪律"，不做合成注入。
[CmdletBinding()]
param([string]$OutDir = '', [string]$AssetsRoot = '', [string]$ObservationRoot = '', [string]$ProvenancePath = '')
$ErrorActionPreference = 'Continue'
try { [Console]::OutputEncoding = [Text.Encoding]::UTF8 } catch { }
$testsDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$simRoot = Split-Path -Parent $testsDir
. (Join-Path $simRoot 'lib\sim_core.ps1')
$D = Get-SimDefaults
if (-not $OutDir) { $OutDir = Join-Path $D.evidenceDir 'gyro-core' }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$script:Cases = New-Object System.Collections.ArrayList
function G([string]$id, [string]$name, [bool]$ok, [string]$actual) {
  $script:Cases.Add([ordered]@{ id = $id; name = $name; pass = [bool]$ok; actual = $actual }) | Out-Null
  Write-Host ("  [{0}] {1} :: {2}" -f $(if ($ok) { 'PASS' } else { 'FAIL' }), $id, $actual) -ForegroundColor $(if ($ok) { 'Green' } else { 'Red' })
}
$observationScope='installed-readonly-anchors-not-physical-verification'
$provenance=$null; $producer=$null; $producerIdentity=$null; $provenanceIdentity=$null
$explicitInputs=[bool]($AssetsRoot -or $ObservationRoot -or $ProvenancePath)
if($explicitInputs){
  if(-not $AssetsRoot -or -not $ObservationRoot -or -not $ProvenancePath){throw 'GYRO_EXPLICIT_INPUTS_REQUIRE_ASSETS_OBSERVATIONS_PROVENANCE'}
  foreach($inputPath in @($AssetsRoot,$ObservationRoot,$ProvenancePath)){
    if(-not [IO.Path]::IsPathFullyQualified($inputPath)){throw 'GYRO_EXPLICIT_INPUT_PATH_MUST_BE_ABSOLUTE'}
  }
  $AssetsRoot=[IO.Path]::GetFullPath($AssetsRoot).TrimEnd('\')
  $ObservationRoot=[IO.Path]::GetFullPath($ObservationRoot).TrimEnd('\')
  $realLocalApp=Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'YeManCC'
  foreach($forbidden in @('C:\SOFT\YeMan',$realLocalApp)){
    if($ObservationRoot -ieq $forbidden -or $ObservationRoot.StartsWith($forbidden+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'GYRO_SOURCE_OBSERVATIONS_MUST_NOT_READ_INSTALLED_STATE'}
  }
  if(-not(Test-Path -LiteralPath $ObservationRoot -PathType Container)){throw 'GYRO_OBSERVATION_DIRECTORY_MISSING'}
  $cursor=$ObservationRoot
  while($cursor){
    if((Test-Path -LiteralPath $cursor) -and ((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'GYRO_OBSERVATION_REPARSE_PATH'}
    $parent=Split-Path -Parent $cursor;if($parent -eq $cursor){break};$cursor=$parent
  }
  $provenance=Get-Content -LiteralPath $ProvenancePath -Raw -Encoding UTF8 -ErrorAction Stop|ConvertFrom-Json -ErrorAction Stop
  $observationScope='source-derived-memory-observation-not-installed-or-physical'
  if($provenance.schemaVersion -ne 1 -or $provenance.scope -cne $observationScope){throw 'GYRO_PROVENANCE_SCOPE_INVALID'}
  foreach($record in @($provenance.files)+@($provenance.producerEvidence)){
    if(-not $record.path -or -not [IO.Path]::IsPathFullyQualified($record.path) -or $record.sha256 -cnotmatch '^[a-f0-9]{64}$'){throw 'GYRO_PROVENANCE_IDENTITY_INVALID'}
    if(-not(Test-Path -LiteralPath $record.path -PathType Leaf) -or (Get-FileHash -LiteralPath $record.path -Algorithm SHA256 -ErrorAction Stop).Hash.ToLowerInvariant() -cne $record.sha256){throw 'GYRO_PROVENANCE_HASH_MISMATCH'}
  }
  if(-not $provenance.producerEvidence.path){throw 'GYRO_PRODUCER_EVIDENCE_MISSING'}
  $producer=Get-Content -LiteralPath $provenance.producerEvidence.path -Raw -Encoding UTF8 -ErrorAction Stop|ConvertFrom-Json -ErrorAction Stop
  $producerIdentity=Get-SimFileIdentity $provenance.producerEvidence.path 'actual diagnostic emitter subgroup; outer suite status retained separately'
  $provenanceIdentity=Get-SimFileIdentity $ProvenancePath 'source-derived observation provenance'
  $requiredFiles=@((Join-Path $AssetsRoot 'enabled.flag'),(Join-Path $AssetsRoot 'input-capture.flag'),(Join-Path $ObservationRoot 'input-capture.jsonl'))
  $optionalGyroLog=Join-Path $ObservationRoot 'gyro-motion.log'
  if(Test-Path -LiteralPath $optionalGyroLog -PathType Leaf){$requiredFiles+=$optionalGyroLog}
  foreach($inputFile in $requiredFiles){
    if(@($provenance.files|Where-Object {[IO.Path]::GetFullPath($_.path) -ieq $inputFile}).Count -ne 1){throw 'GYRO_CONSUMED_INPUT_PROVENANCE_MISSING_OR_DUPLICATE'}
  }
}
$gyroAssets = $(if($explicitInputs){$AssetsRoot}else{'C:\SOFT\YeMan\PowerControl\feature-assets\gyro-motion'})
$gyroFlag = Join-Path $gyroAssets 'mock-handshake.flag'
$gyroEnabled = Join-Path $gyroAssets 'enabled.flag'
$gyroCapFlag = Join-Path $gyroAssets 'input-capture.flag'
$observationDirectory=$(if($explicitInputs){$ObservationRoot}else{$D.localAppData})
$gyroLog = Join-Path $observationDirectory 'gyro-motion.log'
$capLog = Join-Path $observationDirectory 'input-capture.jsonl'
G 'G1' 'S1/S12 合成链通道存在（feature-assets\gyro-motion 资产目录 + enabled/input-capture 标记）' ((Test-Path -LiteralPath $gyroAssets) -and ((Test-Path -LiteralPath $gyroEnabled) -or (Test-Path -LiteralPath $gyroCapFlag))) ("assets=" + (Test-Path -LiteralPath $gyroAssets) + " enabled=" + (Test-Path -LiteralPath $gyroEnabled) + " capture=" + (Test-Path -LiteralPath $gyroCapFlag) + " mockFlag=" + (Test-Path -LiteralPath $gyroFlag))
G 'G2' 'S1 观测面存在（gyro-motion.log 或可创建路径）' ((Test-Path -LiteralPath $gyroLog) -or (Test-Path -LiteralPath (Split-Path -Parent $gyroLog))) ("log=" + $gyroLog + " exists=" + (Test-Path -LiteralPath $gyroLog))
G 'G3' 'S12 采样通道存在（input-capture.jsonl）' (Test-Path -LiteralPath $capLog) ("cap=" + (Test-Path -LiteralPath $capLog))
$capTail = @()
if (Test-Path -LiteralPath $capLog) { $capTail = @(Read-SimTailLines -Path $capLog -Count 50 | Where-Object { $_ -match 'kind' } | Select-Object -First 5) }
G 'G4' 'S12 采样记录含 kind 字段（start/sample 结构可判读）' (@($capTail).Count -gt 0) ("kindRows=" + @($capTail).Count)
$discriminators = @('mockHandshake', 'sensorFamily', 'providerBindingProven', 'pairProven', 'proofId')
$g5hit = @()
foreach ($f in @($gyroLog, $capLog) | Where-Object { Test-Path -LiteralPath $_ }) {
  $src = Get-Content -LiteralPath $f -Raw -Encoding UTF8
  foreach ($k in $discriminators) { if ($src -match ('"' + $k + '"')) { $g5hit += ($k + '@' + (Split-Path -Leaf $f)) } }
}
G 'G5' 'S1 判读纪律：观测面含可区分合成/真实的判据字段（mockHandshake/sensorFamily/providerBindingProven/pairProven/proofId）' (@($g5hit).Count -gt 0) ("hits=" + ((@($g5hit) | Select-Object -Unique) -join ','))
# Inspect an exact structured claim, not prose such as this test's own denial.
function Test-RealSensorClaim($Value) {
  if ($null -eq $Value) { return $false }
  if ($Value -is [string]) { return $Value -eq 'real-sensor-verified' }
  if ($Value -is [System.Collections.IDictionary]) {
    foreach ($key in $Value.Keys) { if (Test-RealSensorClaim $Value[$key]) { return $true } }
  } elseif ($Value -is [System.Collections.IEnumerable]) {
    foreach ($entry in $Value) { if (Test-RealSensorClaim $entry) { return $true } }
  } elseif ($Value -is [pscustomobject]) {
    foreach ($property in $Value.PSObject.Properties) { if (Test-RealSensorClaim $property.Value) { return $true } }
  }
  return $false
}
$denial = [pscustomobject]@{ actual = 'no real-sensor-verified claims'; pass = $true }
$badClaim = [pscustomobject]@{ mockHandshake = $true; evidence = [pscustomobject]@{ status = 'real-sensor-verified' } }
$claimControlsOk = -not (Test-RealSensorClaim $denial) -and (Test-RealSensorClaim $badClaim)
$claimHits = 0
$claimFiles=@(Get-ChildItem $D.evidenceDir -Recurse -Filter '*.json' -ErrorAction SilentlyContinue | Select-Object -First 12)
if($explicitInputs){$claimFiles+=@(Get-Item -LiteralPath $gyroLog,$capLog -ErrorAction SilentlyContinue)}
foreach ($f in $claimFiles) {
  $content = Get-Content -LiteralPath $f.FullName -Raw -Encoding UTF8
  try { if (Test-RealSensorClaim ($content | ConvertFrom-Json -ErrorAction Stop)) { $claimHits++ } }
  catch { if ($content -match '"[^"\\]+"\s*:\s*"real-sensor-verified"') { $claimHits++ } }
}
G 'G6' 'S2/S3 纪律：合成样本不得写成"真传感器已验证"（扫描结构化声明）' ($claimControlsOk -and $claimHits -eq 0) ("structuredClaimHits=" + $claimHits + " positiveNegativeControls=" + $claimControlsOk)
$failed = @($script:Cases | Where-Object { -not $_.pass })
$expectedCases = 6
$fakeGreen = (@($script:Cases).Count -ne $expectedCases)
$verdict = if ($failed.Count -eq 0 -and -not $fakeGreen) { 'PASS' } else { 'FAIL' }
$jf = Join-Path $OutDir ("gyro-core-$stamp.json")
[IO.File]::WriteAllText($jf, ([ordered]@{ at = (Get-Date).ToString('o'); cases = @($script:Cases); failed = $failed.Count; expectedCases = $expectedCases; fakeGreen = [bool]$fakeGreen; verdict = $verdict
  observationScope = $observationScope; provenance = $provenanceIdentity; producerEvidence = $producerIdentity; producerResult = $producer; fullGyroCapabilityAccepted = $false; installedGyroAccepted = $false; physicalSensorAccepted = $false
  note = 'S1–S3/S12 core 只读部分；合成注入需 App+钩子（owner W3），本脚本不做注入' } | ConvertTo-Json -Depth 20), [Text.UTF8Encoding]::new($false))
if ($fakeGreen) { Write-Host ("gyro-core: FAKE-GREEN cases={0} expected={1}（套件自身空转，判 FAIL）" -f @($script:Cases).Count, $expectedCases) -ForegroundColor Red }
Write-Host ("gyro-core: cases={0}/{1} failed={2} -> {3}" -f @($script:Cases).Count, $expectedCases, $failed.Count, $verdict) -ForegroundColor $(if ($verdict -eq 'PASS') { 'Green' } else { 'Red' })
Write-Host ("report=" + $jf)
if ($verdict -eq 'PASS') { exit 0 } else { exit 2 }
