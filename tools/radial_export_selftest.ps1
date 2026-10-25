[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$EvidenceDir)
$ErrorActionPreference='Stop'
$EvidenceDir=[IO.Path]::GetFullPath($EvidenceDir)
if(Test-Path -LiteralPath $EvidenceDir){throw 'Fresh evidence directory required'}
New-Item -ItemType Directory -Path $EvidenceDir | Out-Null
. (Join-Path $PSScriptRoot 'monitor-hud-export.ps1')
$pages=@('index.html','hud-output.html','radial-output.html');$results=[Collections.Generic.List[object]]::new()
function Tree([string]$Name){$dir=Join-Path $EvidenceDir $Name;New-Item -ItemType Directory -Path (Join-Path $dir assets) -Force | Out-Null;foreach($name in $pages){[IO.File]::WriteAllText((Join-Path $dir $name),'<script type="module" src="./assets/'+$name+'.js"></script><link rel="stylesheet" href="./assets/shared.css">')};foreach($name in $pages){[IO.File]::WriteAllText((Join-Path $dir ('assets/'+$name+'.js')),'export const fixture=true;')};[IO.File]::WriteAllText((Join-Path $dir 'assets/shared.css'),'body{background:transparent}');return $dir}
function Check([string]$Name,[scriptblock]$Run){try{& $Run;$results.Add(@{name=$Name;pass=$true})}catch{$results.Add(@{name=$Name;pass=$false;error=$_.Exception.Message})}}
function Reject([scriptblock]$Run,[string]$Pattern){$errorText='';try{& $Run | Out-Null}catch{$errorText=$_.Exception.Message};if($errorText -notmatch $Pattern){throw "Wrong or absent refusal: $errorText"}}
Check 'all three HTML entries are validated' {$r=Assert-MonitorHudWebExport (Tree positive) -EntryPages $pages;if($r.pages.Count -ne 3 -or $r.referencedAssets.Count -ne 4){throw 'Missing radial entry or shared asset'}}
Check 'missing radial HTML is rejected' {$dir=Tree missing;Remove-Item -LiteralPath (Join-Path $dir 'radial-output.html');Reject {Assert-MonitorHudWebExport $dir -EntryPages $pages} 'entry missing'}
Check 'missing radial module is rejected' {$dir=Tree missingModule;Remove-Item -LiteralPath (Join-Path $dir 'assets/radial-output.html.js');Reject {Assert-MonitorHudWebExport $dir -EntryPages $pages} 'reference missing'}
Check 'mismatched radial HTML cannot pass an old source identity' {$before=Tree before;$after=Tree after;Add-Content -LiteralPath (Join-Path $after 'radial-output.html') -Value 'changed';Reject {Assert-MonitorHudWebExport $after $before -EntryPages $pages} 'HTML differs'}
Check 'shared asset drift is rejected' {$before=Tree beforeShared;$after=Tree afterShared;Add-Content -LiteralPath (Join-Path $after 'assets/shared.css') -Value 'changed';Reject {Assert-MonitorHudWebExport $after $before -EntryPages $pages} 'asset differs'}
Check 'empty entry contract is rejected' {$dir=Tree empty;Reject {Assert-MonitorHudWebExport $dir -EntryPages @()} 'contract is empty'}
function PolicyProject([string]$Name,[string]$Frontend,[string]$Native) {
  $dir=Join-Path $EvidenceDir $Name
  New-Item -ItemType Directory -Path (Join-Path $dir 'src/radial'),(Join-Path $dir 'native') -Force | Out-Null
  [IO.File]::WriteAllText((Join-Path $dir 'src/radial/availability.ts'),"export const RADIAL_USER_AVAILABLE = $Frontend;")
  [IO.File]::WriteAllText((Join-Path $dir 'native/radial_release.h'),"inline constexpr bool kUserAvailable = $Native;")
  return $dir
}
function ProductTree([string]$Name,[bool]$Wheel=$false) {
  $dir=Join-Path $EvidenceDir $Name
  New-Item -ItemType Directory -Path (Join-Path $dir assets) -Force | Out-Null
  $entries=@('index.html','hud-output.html','notice-output.html')
  if($Wheel){$entries+='radial-output.html'}
  foreach($entry in $entries){[IO.File]::WriteAllText((Join-Path $dir $entry),'<script type="module" src="./assets/'+$entry+'.js"></script>');[IO.File]::WriteAllText((Join-Path $dir ('assets/'+$entry+'.js')),'export const fixture=true;')}
  return $dir
}
Check 'disabled policy omits wheel and retains ordinary pages' {$project=PolicyProject offPolicy false false;$entries=@(Get-YmccProductWebEntryPages $project);if($entries.Count-ne 3 -or $entries-contains 'radial-output.html'){throw 'Wrong disabled product contract'};Assert-YmccProductWebExport (ProductTree offProduct) $project}
Check 'disabled product refuses a leaked wheel page' {$project=PolicyProject leakPolicy false false;Reject {Assert-YmccProductWebExport (ProductTree leakedProduct $true) $project} 'RADIAL_USER_ENTRY_MUST_NOT_BE_PACKAGED'}
Check 'frontend/native policy divergence fails closed' {$project=PolicyProject divergentPolicy false true;Reject {Get-YmccProductWebEntryPages $project} 'RADIAL_RELEASE_POLICY_INVALID_OR_DIVERGENT'}
Check 'unrecognized policy fails closed' {$project=PolicyProject malformedPolicy undefined false;Reject {Get-YmccProductWebEntryPages $project} 'RADIAL_RELEASE_POLICY_INVALID_OR_DIVERGENT'}
Check 'missing policy cannot silently enable wheel' {Reject {Get-YmccProductWebEntryPages (Join-Path $EvidenceDir noSuchProject)} 'Could not find|not find|不存在|找不到'}
Check 'explicit enabled policy still requires wheel HTML' {$project=PolicyProject onPolicy true true;Reject {Assert-YmccProductWebExport (ProductTree onMissing) $project} 'entry missing'}
Check 'explicit enabled complete product remains valid' {$project=PolicyProject onCompletePolicy true true;Assert-YmccProductWebExport (ProductTree onComplete $true) $project}
$failed=@($results | Where-Object {-not $_.pass});$report=[ordered]@{pass=($failed.Count -eq 0);cases=$results.Count;results=@($results.ToArray());productionApplicationChanged=$false;hardwareIo=$false}
$report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $EvidenceDir 'verification.json') -Encoding utf8
$report | ConvertTo-Json -Depth 6
if($failed.Count){throw 'Radial export contract failed'}
