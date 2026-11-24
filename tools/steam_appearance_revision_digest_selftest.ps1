param([Parameter(Mandatory)][string]$Out,[string]$ComponentRoot='')
$ErrorActionPreference='Stop'
if(!$ComponentRoot){$ComponentRoot=Join-Path (Split-Path -Parent $PSScriptRoot) 'PowerControl'}
$Out=[IO.Path]::GetFullPath($Out)
if(Test-Path -LiteralPath $Out){throw 'Fresh fixture output required'}
$null=[IO.Directory]::CreateDirectory($Out)
. (Join-Path $PSScriptRoot 'ymcc_sim/global/decky-update-recovery/appearance-library-fixture.ps1') -ComponentRoot $ComponentRoot -EvidenceRoot $Out
$null=[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($script:ThemeRoot))
Copy-Item -LiteralPath (Join-Path $script:PackRoot 'themes') -Destination $script:ThemeRoot -Recurse
function Read-RevisionRows { @(Get-ThemeRows -RevisionOnly -DigestOnly) }
$expected=@(Read-RevisionRows)
Initialize-ResidentAppearanceInterop
Set-Item Function:\Get-RevisionFixtureHash (Get-Command Get-ConfigHash).ScriptBlock
function Get-ConfigHash([string]$Path,[long]$MaximumBytes=0){
 $script:PerFileCalls++
 Get-RevisionFixtureHash @PSBoundParameters
}
function Assert-DigestPath([bool]$Batch){
 $script:PerFileCalls=0
 $rows=@(Read-RevisionRows)
 if(($script:PerFileCalls -eq 0) -ne $Batch){throw 'Unexpected revision digest path'}
 $rows
}
$healthy=@(Assert-DigestPath $true)
if(($healthy|ConvertTo-Json -Depth 4 -Compress) -cne ($expected|ConvertTo-Json -Depth 4 -Compress)){throw 'Healthy digest rows changed'}
foreach($name in 'config_USER.json','theme.json'){
 $file=Join-Path $script:ThemeRoot ('Round/'+$name)
 $item=[IO.FileInfo]::new($file);$modified=$item.LastWriteTimeUtc;$created=$item.CreationTimeUtc
 $original=[IO.File]::ReadAllBytes($file);$changed=[byte[]]$original.Clone();$changed[0]=$changed[0] -bxor 1
 try{
  [IO.File]::WriteAllBytes($file,$changed);[IO.File]::SetLastWriteTimeUtc($file,$modified)
  $item.Refresh()
  if($item.LastWriteTimeUtc -ne $modified -or $item.CreationTimeUtc -ne $created -or $item.Length -ne $original.Length){throw 'Fixture metadata drift'}
  if((Get-ThemeRevision @(Assert-DigestPath $true)) -ceq (Get-ThemeRevision $healthy)){throw "Same-stamp revision change missed: $name"}
  $lock=[IO.File]::Open($file,'Open','Read','None')
  try{if(!(@(Assert-DigestPath $false|Where-Object name -eq 'Round')[0].error)){throw 'Sharing error lost'}}finally{$lock.Dispose()}
 }finally{[IO.File]::WriteAllBytes($file,$original);[IO.File]::SetLastWriteTimeUtc($file,$modified)}
}
$dir=Join-Path $script:ThemeRoot 'Round';$moved=$dir+'.fixture-moved'
[IO.Directory]::Move($dir,$moved)
try{
 $null=New-Item -ItemType Junction -Path $dir -Target $moved
 if(!(@(Assert-DigestPath $false|Where-Object name -eq 'Round')[0].error)){throw 'Reparse-point error lost'}
}finally{if([IO.Directory]::Exists($dir)){[IO.Directory]::Delete($dir)};[IO.Directory]::Move($moved,$dir)}
if((Get-ThemeRevision @(Assert-DigestPath $true)) -cne (Get-ThemeRevision $healthy)){throw 'Valid revision did not recover'}
'PASS: healthy revision parity, same-stamp config and manifest, locked config and manifest, junction, and recovery'

Set-Item Function:\Read-RevisionFixtureInput (Get-Command Read-AppearanceThemeConfig).ScriptBlock
function Read-AppearanceThemeConfig([string]$Path,[switch]$Manifest){
 $script:InputReads++
 Read-RevisionFixtureInput @PSBoundParameters
}
function Compare-InputProjection([bool]$Batch,[switch]$RevisionOnly){
 $script:InputReads=0
 $plain=@(Get-ThemeRows -RevisionOnly:$RevisionOnly)
 if($RevisionOnly -and !$script:InputReads){throw 'Full revision reader changed its path'}
 $script:InputReads=0
 $projected=@(Get-ThemeRows -RevisionOnly:$RevisionOnly -ReadOnlyProjection -AutomaticPlanMemo @{})
 if(($script:InputReads -eq 0) -ne $Batch){throw 'Unexpected first input snapshot path'}
 if((ConvertTo-Json -InputObject $projected -Depth 24 -Compress) -cne (ConvertTo-Json -InputObject $plain -Depth 24 -Compress)){throw 'First input snapshot changed the rows'}
 $projected
}
$inputs=@(Compare-InputProjection $true)
$revisionInputs=@(Compare-InputProjection $true -RevisionOnly)
if(($revisionInputs|Where-Object {$null -ne $_.renderPlan}) -or (Get-ThemeRevision $revisionInputs) -cne (Get-ThemeRevision $inputs)){throw 'Cold revision projection changed the plans or revision'}
$config=Join-Path $dir 'config_USER.json'
$item=[IO.FileInfo]::new($config);$written=$item.LastWriteTimeUtc;$created=$item.CreationTimeUtc
$original=[IO.File]::ReadAllBytes($config)
$text=[IO.File]::ReadAllText($config)
try{
 [IO.File]::WriteAllText($config,$text,[Text.UnicodeEncoding]::new($false,$true))
 if((Get-ThemeRevision @(Compare-InputProjection $true)) -ceq (Get-ThemeRevision $inputs)){throw 'BOM replacement digest missed'}
 if((Get-ThemeRevision @(Compare-InputProjection $true -RevisionOnly)) -ceq (Get-ThemeRevision $revisionInputs)){throw 'Cold revision BOM replacement missed'}
 [IO.File]::WriteAllBytes($config,$original)
 $changed=[byte[]]$original.Clone();$changed[$changed.Length-1]=$changed[$changed.Length-1] -bxor 1
 [IO.File]::WriteAllBytes($config,$changed);[IO.File]::SetLastWriteTimeUtc($config,$written)
 $item.Refresh()
 if($item.LastWriteTimeUtc -ne $written -or $item.CreationTimeUtc -ne $created -or $item.Length -ne $original.Length){throw 'First snapshot fixture metadata drift'}
 if((Get-ThemeRevision @(Compare-InputProjection $true)) -ceq (Get-ThemeRevision $inputs)){throw 'Same-stamp input change missed'}
 if((Get-ThemeRevision @(Compare-InputProjection $true -RevisionOnly)) -ceq (Get-ThemeRevision $revisionInputs)){throw 'Cold revision same-stamp change missed'}
 $lock=[IO.File]::Open($config,'Open','Read','None')
 try{
  if(!(Compare-InputProjection $false|Where-Object name -eq 'Round').error){throw 'First input snapshot lost sharing diagnostic'}
  if(!(Compare-InputProjection $false -RevisionOnly|Where-Object name -eq 'Round').error){throw 'Cold revision snapshot lost sharing diagnostic'}
 }finally{$lock.Dispose()}
}finally{[IO.File]::WriteAllBytes($config,$original);[IO.File]::SetLastWriteTimeUtc($config,$written)}
'PASS: first plan and cold revision snapshot parity, independent byte digest, BOM decoding, same-stamp change and locked fallback'

# Exercise the actual UI Status dispatcher, not just the row projection.
function Get-BackendProcesses {}
function Get-AppearanceSteamSession {}
function Test-BackendOwner {$false}
function Test-CssDesktopEditing {$false}
$script:Action='Status'
$script:Payload=[Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes('{"profile":"ui-summary-v1","lockWait":"single-v1"}'))
# Switch just the input path; compare the same dispatcher and wire fields.
$statusRows=(Get-Command Get-ThemeRows).ScriptBlock
function Get-ThemeRows {
    param([switch]$RevisionOnly,[switch]$DigestOnly,[hashtable]$AutomaticPlanMemo,[switch]$ValidateRenderOnly,[switch]$ReadOnlyProjection,[hashtable]$UnchangedConfigHashes,[switch]$BatchStatusInputs)
    if($script:ForcePerFile){$null=$PSBoundParameters.Remove('BatchStatusInputs')}
    & $statusRows @PSBoundParameters
}
function Read-AppearanceThemeConfig([string]$Path,[switch]$Manifest){
    if($Path.StartsWith($script:ThemeRoot+'\',[StringComparison]::OrdinalIgnoreCase)){$script:InputReads++}
    Read-RevisionFixtureInput @PSBoundParameters
}
function Assert-StatusInputProjection([bool]$Batch){
    $script:InputReads=0
    $script:ForcePerFile=$true
    try{$expected=Invoke-AppearanceControl 'Status'}finally{$script:ForcePerFile=$false}
    if(!$script:InputReads){throw 'Expected unbatched reference Status'}
    $script:InputReads=0
    $actual=Invoke-AppearanceControl 'Status'
    if(!$actual.ok -or $actual.snapshot.themes.Count -ne 7 -or (($script:InputReads -eq 0) -ne $Batch)){throw "Unexpected UI Status input path: actual reads=$script:InputReads, expected batch=$Batch"}
    if((ConvertTo-Json -InputObject $actual -Depth 24 -Compress) -cne (ConvertTo-Json -InputObject $expected -Depth 24 -Compress)){throw 'UI Status input projection changed the receipt'}
    $actual.snapshot
}
$status=Assert-StatusInputProjection $true
$lock=[IO.File]::Open($config,'Open','Read','None')
try{
    $fallback=Assert-StatusInputProjection $false
    if(!($fallback.themes|Where-Object name -eq 'Round').error){throw 'UI Status lost the locked config diagnostic'}
}finally{$lock.Dispose()}
if((Assert-StatusInputProjection $true).revision -cne $status.revision){throw 'UI Status did not recover the previous revision'}
$script:Payload=''
$script:InputReads=0
$full=Invoke-AppearanceControl 'Status'
if(!$full.ok -or !$script:InputReads -or !($full.snapshot.themes[0].PSObject.Properties['renderPlan'])){throw 'Legacy full Status unexpectedly used the UI summary path'}
'PASS: UI Status batch hits, locked fallback, complete receipt parity, recovery and legacy full bypass'
