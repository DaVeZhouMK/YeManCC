[CmdletBinding()]
param([string]$CaseRoot,[string]$FixtureRoot,[ValidateSet('helper','legacy','none')][string]$Phase,[string]$Tool,[string]$ArgumentsPath,[string]$TracePath)
$ErrorActionPreference='Stop';$ownModules=Join-Path $PSHOME 'Modules';$env:PSModulePath=$ownModules+';'+$env:PSModulePath
$index=Microsoft.PowerShell.Management\Get-Content -LiteralPath (Join-Path $FixtureRoot 'fixture-index.json') -Raw -Encoding UTF8|ConvertFrom-Json
$global:YmccLightingArchiveIo=[pscustomobject]@{root=[IO.Path]::GetFullPath($CaseRoot).TrimEnd('\');fixture=$FixtureRoot;repo=(Split-Path -Parent (Split-Path -Parent $Tool));maps=@{};observed=@{};phase=$Phase}
if($Phase -cne 'none'){
 foreach($row in $index.maps.$Phase){
  $key=[IO.Path]::GetFullPath($row.originalPath).ToLowerInvariant();$file=Join-Path $FixtureRoot $row.path
  # The selected fixed legacy contract admits 7bf; its earlier C-path image is
  # retained in the archive but never used to qualify that fixed contract.
  if($Phase -ceq 'legacy' -and $key -ieq 'C:\SOFT\YeMan\PowerControl\light-setter\YeManLightSetter.dll' -and $row.sha256 -cne '7bfce707580fd70edb808ec4b864dd4315ddbfe3e2cf5bf36fd956249622a2fe'){continue}
  if($global:YmccLightingArchiveIo.maps.ContainsKey($key) -and $global:YmccLightingArchiveIo.maps[$key].sha256 -cne $row.sha256){throw 'ARCHIVE_PATH_VERSION_AMBIGUOUS'}
  $global:YmccLightingArchiveIo.maps[$key]=[pscustomobject]@{file=$file;sha256=$row.sha256}
 }
}
function Resolve-ArchiveRead([string]$Path){
 $full=[IO.Path]::GetFullPath($Path);$key=$full.ToLowerInvariant();$state=$global:YmccLightingArchiveIo
 if($state.maps.ContainsKey($key)){
  $row=$state.maps[$key];$actual=(Microsoft.PowerShell.Utility\Get-FileHash -LiteralPath $row.file -Algorithm SHA256).Hash.ToLowerInvariant();if($actual -cne $row.sha256){throw 'ARCHIVE_MAPPED_INPUT_CHANGED'}
  $state.observed[$key]=[pscustomobject]@{originalPath=$full;actualPath=$row.file;sha256=$actual;phase=$state.phase};return $row.file
 }
 if($full.StartsWith($state.root+'\',[StringComparison]::OrdinalIgnoreCase) -or $full.StartsWith($state.repo+'\',[StringComparison]::OrdinalIgnoreCase)){return $full}
 throw ('UNDECLARED_ARCHIVE_READ:'+ $full)
}
function Get-FileHash{[CmdletBinding()]param([string]$LiteralPath,[string]$Path,[string]$Algorithm='SHA256');$p=if($LiteralPath){$LiteralPath}else{$Path};Microsoft.PowerShell.Utility\Get-FileHash -LiteralPath (Resolve-ArchiveRead $p) -Algorithm $Algorithm}
function Get-Content{[CmdletBinding()]param([string]$LiteralPath,[string]$Path,[switch]$Raw,[string]$Encoding='UTF8');$p=if($LiteralPath){$LiteralPath}else{$Path};Microsoft.PowerShell.Management\Get-Content -LiteralPath (Resolve-ArchiveRead $p) -Raw:$Raw -Encoding $Encoding}
function Get-Item{[CmdletBinding()]param([string]$LiteralPath,[string]$Path,[switch]$Force);$p=if($LiteralPath){$LiteralPath}else{$Path};Microsoft.PowerShell.Management\Get-Item -LiteralPath (Resolve-ArchiveRead $p) -Force:$Force}
function Test-Path{[CmdletBinding()]param([string]$LiteralPath,[string]$Path,[string]$PathType='Any');$p=if($LiteralPath){$LiteralPath}else{$Path};Microsoft.PowerShell.Management\Test-Path -LiteralPath (Resolve-ArchiveRead $p) -PathType $PathType}
function Copy-Item{[CmdletBinding()]param([string]$LiteralPath,[string]$Path,[string]$Destination,[switch]$Force);$p=if($LiteralPath){$LiteralPath}else{$Path};$target=[IO.Path]::GetFullPath($Destination);if(-not$target.StartsWith($global:YmccLightingArchiveIo.root+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'ARCHIVE_COPY_OUTSIDE_CASE'};Microsoft.PowerShell.Management\Copy-Item -LiteralPath (Resolve-ArchiveRead $p) -Destination $target -Force:$Force}
$invokeArgs=Microsoft.PowerShell.Management\Get-Content -LiteralPath $ArgumentsPath -Raw -Encoding UTF8|ConvertFrom-Json
$namedArguments=@{}
for($i=0;$i -lt $invokeArgs.Count;$i+=2){if($i+1 -ge $invokeArgs.Count -or $invokeArgs[$i] -notmatch '^-[A-Za-z][A-Za-z0-9]*$'){throw 'ARCHIVE_ARGUMENT_PAIRS_REQUIRED'};$key=$invokeArgs[$i].Substring(1);if($namedArguments.ContainsKey($key)){throw 'ARCHIVE_DUPLICATE_ARGUMENT'};$namedArguments[$key]=$invokeArgs[$i+1]}
try{& $Tool @namedArguments;if($LASTEXITCODE -and $LASTEXITCODE -ne 0){exit $LASTEXITCODE}}finally{
 $rows=@($global:YmccLightingArchiveIo.observed.Values);$record=@{schemaVersion=1;phase=$Phase;count=$rows.Count;files=$rows;hashResultsSynthesized=$false;originalReceiptsRewritten=$false;allCopiesUnderOwnedCase=$true}
 [IO.File]::WriteAllText($TracePath,($record|ConvertTo-Json -Depth 12),[Text.UTF8Encoding]::new($false))
}
