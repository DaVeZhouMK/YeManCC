[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$OutputDirectory,[switch]$SourceOnly,[switch]$ManifestInvariantOnly,[string]$ManifestPath='',[string[]]$ManifestCaseFilter=@())
$ErrorActionPreference = 'Stop'
if(($PSBoundParameters.ContainsKey('ManifestCaseFilter') -or $PSBoundParameters.ContainsKey('ManifestPath')) -and -not $ManifestInvariantOnly){throw 'Manifest arguments require explicit ManifestInvariantOnly'}
. (Join-Path $PSScriptRoot 'custom_steam_library_build_guard.ps1')
$build = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..\Build')).TrimEnd('\')
$evidence = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..\..\Testing\YMCC\Evidence')).TrimEnd('\')
$root = [IO.Path]::GetFullPath($OutputDirectory)
if ((-not $root.StartsWith($build + '\', [StringComparison]::OrdinalIgnoreCase) -and -not $root.StartsWith($evidence + '\', [StringComparison]::OrdinalIgnoreCase)) -or (Test-Path -LiteralPath $root)) {
  throw 'Use a fresh isolated Mainline Build or core Evidence root for compiler guard fixtures'
}
New-Item -ItemType Directory -Path $root | Out-Null
if($ManifestInvariantOnly){
  if($SourceOnly){throw 'ManifestInvariantOnly is separate from SourceOnly'}
  $available=@([regex]::Matches([IO.File]::ReadAllText($PSCommandPath),"(?m)^\s*Check '([^']+)'")|ForEach-Object {$_.Groups[1].Value})
  if($PSBoundParameters.ContainsKey('ManifestCaseFilter') -and ($ManifestCaseFilter.Count -eq 0 -or @($ManifestCaseFilter|Where-Object {$_ -notin $available}).Count -or @($ManifestCaseFilter|Select-Object -Unique).Count -ne $ManifestCaseFilter.Count)){throw 'Unknown/empty/duplicate manifest invariant selection'}
  . (Join-Path $PSScriptRoot 'release-export-identity.ps1')
  if(-not $ManifestPath){$ManifestPath=Join-Path $PSScriptRoot '../CustomSteamLibrary/package-manifest.json'}
  $manifestBytes=[IO.File]::ReadAllBytes($ManifestPath)
  [IO.File]::WriteAllBytes((Join-Path $root 'input-manifest.json'),$manifestBytes)
  $manifest=[Text.Encoding]::UTF8.GetString($manifestBytes).TrimStart([char]0xFEFF)|ConvertFrom-Json
  function CloneManifest{return ([Text.Encoding]::UTF8.GetString($manifestBytes).TrimStart([char]0xFEFF)|ConvertFrom-Json)}
  function SetField($Value,[string]$Name,$Child){$Value|Add-Member -MemberType NoteProperty -Name $Name -Value $Child -Force}
  $baseHash=Get-CslWorkerManifestInvariant $manifest
  $cases=[Collections.Generic.List[object]]::new()
  function Check([string]$Name,[scriptblock]$Body){if($ManifestCaseFilter.Count -and $Name -notin $ManifestCaseFilter){return};try{$passed=[bool](& $Body);$errorText=$null}catch{$passed=$false;$errorText=$_.Exception.Message};$cases.Add([ordered]@{name=$Name;passed=$passed;error=$errorText})}
  Check 'unchanged manifest accepted' {(Get-CslWorkerManifestInvariant (CloneManifest)) -ceq $baseHash}
  Check 'object key order is canonical' {
    $reverse=[ordered]@{};$names=[string[]]@($manifest.PSObject.Properties.Name);[Array]::Reverse($names)
    foreach($name in $names){$reverse[$name]=$manifest.PSObject.Properties[$name].Value}
    (Get-CslWorkerManifestInvariant $reverse) -ceq $baseHash
  }
  Check 'only original host publication fields are excluded' {
    $m=CloneManifest;SetField $m 'generatedAt' 'new-time';SetField $m.sourceBuild 'host' 'new-host';SetField $m.sourceBuild 'hostSources' @('new-source')
    $hostRow=@($m.fileIndex|Where-Object path -CEQ 'CustomSteamLibrary.exe')[0];SetField $hostRow 'bytes' 123;SetField $hostRow 'sha256' 'new-hash'
    (Get-CslWorkerManifestInvariant $m) -ceq $baseHash
  }
  Check 'worker mutation still rejected' {$m=CloneManifest;SetField $m.sourceBuild 'worker' 'changed';(Get-CslWorkerManifestInvariant $m) -cne $baseHash}
  Check 'unknown root field is retained' {$m=CloneManifest;SetField $m 'unrecognizedField' 'retained';(Get-CslWorkerManifestInvariant $m) -cne $baseHash}
  Check 'unknown nested host field is retained' {$m=CloneManifest;$hostRow=@($m.fileIndex|Where-Object path -CEQ 'CustomSteamLibrary.exe')[0];SetField $hostRow 'unrecognizedField' 'retained';(Get-CslWorkerManifestInvariant $m) -cne $baseHash}
  Check 'array order is retained' {$m=CloneManifest;$array=@($m.fileIndex);if($array.Count -lt 2){throw 'Need two original file entries'};[Array]::Reverse($array);$m.fileIndex=$array;(Get-CslWorkerManifestInvariant $m) -cne $baseHash}
  Check 'null and empty string differ' {$m=CloneManifest;SetField $m 'retainedValue' $null;$a=Get-CslWorkerManifestInvariant $m;SetField $m 'retainedValue' '';(Get-CslWorkerManifestInvariant $m) -cne $a}
  Check 'null and empty array differ' {$m=CloneManifest;SetField $m 'retainedValue' $null;$a=Get-CslWorkerManifestInvariant $m;SetField $m 'retainedValue' @();(Get-CslWorkerManifestInvariant $m) -cne $a}
  Check 'escaped strings roundtrip without value changes' {
    $value=[ordered]@{html="<appRoot>&'";unicode=([string][char]0x6c49+[char]0x5b57);slash='a\b';number=1.05;large=1e20;empty=@();nil=$null}
    $json=ConvertTo-CslCanonicalManifestJson $value;$parsed=$json|ConvertFrom-Json
    [IO.File]::WriteAllText((Join-Path $root 'canonical-scalars.json'),$json,[Text.UTF8Encoding]::new($false))
    $parsed.html -ceq $value.html -and $parsed.unicode -ceq $value.unicode -and $parsed.slash -ceq $value.slash -and $parsed.number -eq 1.05 -and @($parsed.empty).Count -eq 0 -and $null -eq $parsed.nil
  }
  Check 'empty object and nested null array values are retained' {
    $value='{"emptyObject":{},"values":[null,[],{"k":"v"}]}'|ConvertFrom-Json
    $json=ConvertTo-CslCanonicalManifestJson $value;$parsed=$json|ConvertFrom-Json
    [IO.File]::WriteAllText((Join-Path $root 'canonical-nested.json'),$json,[Text.UTF8Encoding]::new($false))
    $parsed.emptyObject -is [Management.Automation.PSCustomObject] -and @($parsed.emptyObject.PSObject.Properties).Count -eq 0 -and
      $parsed.values.Count -eq 3 -and $null -eq $parsed.values[0] -and $parsed.values[1] -is [Array] -and $parsed.values[1].Count -eq 0 -and $parsed.values[2].k -ceq 'v'
  }
  $failed=@($cases|Where-Object {-not $_.passed}).Count
  if($ManifestCaseFilter.Count -and $cases.Count -ne $ManifestCaseFilter.Count){throw 'Manifest invariant selection did not execute exactly'}
  $report=[ordered]@{selection='manifest-invariant-only';requestedCases=@($ManifestCaseFilter);shell=$PSVersionTable.PSVersion.ToString();allPassed=($failed -eq 0);caseCount=$cases.Count;failedCount=$failed;cases=@($cases);canonicalInvariant=$baseHash;manifestSha256=(Get-FileHash -LiteralPath (Join-Path $root 'input-manifest.json') -Algorithm SHA256).Hash.ToLowerInvariant();scalarProjectionSha256=$(if(Test-Path -LiteralPath (Join-Path $root 'canonical-scalars.json')){(Get-FileHash -LiteralPath (Join-Path $root 'canonical-scalars.json') -Algorithm SHA256).Hash.ToLowerInvariant()}else{$null});defaultCasesExecuted=$false;productExecuted=$false;realSteamFilesModified=$false}
  $bytes=[Text.UTF8Encoding]::new($false).GetBytes(($report|ConvertTo-Json -Depth 8));$f=[IO.File]::Open((Join-Path $root 'summary.json'),[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None);try{$f.Write($bytes,0,$bytes.Length)}finally{$f.Dispose()}
  $report|ConvertTo-Json -Depth 8
  if($failed){exit 1};exit 0
}
[IO.File]::WriteAllText((Join-Path $root 'fixture.exe'), 'Fixture bytes only; never executed')
$cases = @()
if (-not $SourceOnly) {
foreach ($case in @(
  @{name='zero-exit-fatal-internal-compiler-error';exit=0;line='host.cpp(2162) : fatal error C1001: Internal compiler error';exists=$true;reject=$true},
  @{name='zero-exit-normal-compiler-error';exit=0;line='host.cpp(1): error C2065: undefined symbol';exists=$true;reject=$true},
  @{name='zero-exit-linker-error';exit=0;line='LINK : fatal error LNK1120: unresolved symbols';exists=$true;reject=$true},
  @{name='zero-exit-resource-error';exit=0;line='resources.rc(2) : error RC2135: missing resource';exists=$true;reject=$true},
  @{name='nonzero-exit-without-diagnostic';exit=2;line='compile failed';exists=$true;reject=$true},
  @{name='missing-fresh-executable';exit=0;line='OK';exists=$false;reject=$true},
  @{name='normal-compile-with-warning';exit=0;line='host.cpp(1): warning C4100: unused parameter';exists=$true;reject=$false}
)) {
  $rejected=$false
  try {
    Assert-CustomSteamLibraryCompilation -ExitCode $case.exit -OutputLines @($case.line) -CompilationDirectory $root -ExpectedExecutables @(if($case.exists){'fixture.exe'}else{'missing.exe'})
  } catch { $rejected=$true }
  $cases += [ordered]@{name=$case.name;passed=($rejected -eq $case.reject)}
}
}
# Exercise the real source index using only private copies, never real edits.
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$blacklist = 'native/game_library_blacklist_entries.h'
$sourceInputs = @()
foreach ($kind in @('host','worker')) {
  $fixture = Join-Path $root ('source-' + $kind)
  $steamDependencies = @('native/steam_startup_window_facts.h','native/steam_home_client_identity.h')
  if ($kind -eq 'host') {
    $steamDependencies += @('native/worker_thread_name.h','native/steam_listener_executor.h','native/steam_source_owner.h','native/steam_startup_capture_diagnostic.h','native/steam_session_registry_watch.h')
  } else {
    $steamDependencies += 'native/steamdeck_mouse_vdf.h'
  }
  $realIndex = @(Get-CustomSteamLibrarySourceIndex -ProjectRoot $projectRoot -Kind $kind)
  foreach ($relative in @(@($realIndex | ForEach-Object { $_.path }) + @($blacklist) + $steamDependencies | Sort-Object -Unique)) {
    $target = Join-Path $fixture $relative
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($target)) | Out-Null
    [IO.File]::WriteAllBytes($target,[IO.File]::ReadAllBytes((Join-Path $projectRoot $relative)))
  }
  $index = @(Get-CustomSteamLibrarySourceIndex -ProjectRoot $fixture -Kind $kind)
  $accepted = $true
  try { Assert-CustomSteamLibrarySourceIndex -ProjectRoot $fixture -Kind $kind -SourceIndex $index } catch { $accepted = $false }
  $cases += [ordered]@{name=($kind+'-same-source-is-accepted');passed=$accepted}
  $included = @($index | Where-Object { $_.path -ceq $blacklist }).Count -eq 1
  $cases += [ordered]@{name=($kind+'-transitive-library-blacklist-is-declared');passed=$included}
  $sourceInputs += [ordered]@{kind=$kind;index=$index;blacklist=$blacklist}
  $source = Join-Path $fixture $blacklist
  [IO.File]::AppendAllText($source,"`r`n// private source-change control`r`n")
  $rejected = $false;$reason = ''
  try { Assert-CustomSteamLibrarySourceIndex -ProjectRoot $fixture -Kind $kind -SourceIndex $index } catch { $rejected=$true;$reason=$_.Exception.Message }
  $cases += [ordered]@{name=($kind+'-library-blacklist-edit-invalidates-child');passed=($rejected -and $reason.Contains($blacklist));reason=$reason}
  [IO.File]::WriteAllBytes($source,[IO.File]::ReadAllBytes((Join-Path $projectRoot $blacklist)))
  $incomplete = @($index | Where-Object { $_.path -cne $blacklist })
  $rejected = $false;$reason = ''
  try { Assert-CustomSteamLibrarySourceIndex -ProjectRoot $fixture -Kind $kind -SourceIndex $incomplete } catch { $rejected=$true;$reason=$_.Exception.Message }
  $cases += [ordered]@{name=($kind+'-old-incomplete-declaration-is-refused');passed=($rejected -and $reason.Contains('coverage drifted'));reason=$reason}
  foreach ($dependency in $steamDependencies) {
    $leaf = [IO.Path]::GetFileNameWithoutExtension($dependency)
    $cases += [ordered]@{name="$kind-$leaf-is-declared";passed=(@($index | Where-Object path -CEQ $dependency).Count -eq 1)}
    $source = Join-Path $fixture $dependency
    $original = [IO.File]::ReadAllBytes($source)
    [IO.File]::AppendAllText($source,"`r`n// private transitive source-change control`r`n")
    $reason = ''
    try { Assert-CustomSteamLibrarySourceIndex -ProjectRoot $fixture -Kind $kind -SourceIndex $index } catch { $reason = $_.Exception.Message }
    finally { [IO.File]::WriteAllBytes($source,$original) }
    $cases += [ordered]@{name="$kind-$leaf-edit-invalidates-child";passed=$reason.Contains($dependency);reason=$reason}
    $incomplete = @($index | Where-Object path -CNE $dependency)
    $reason = ''
    try { Assert-CustomSteamLibrarySourceIndex -ProjectRoot $fixture -Kind $kind -SourceIndex $incomplete } catch { $reason = $_.Exception.Message }
    $cases += [ordered]@{name="$kind-$leaf-omission-is-refused";passed=$reason.Contains('coverage drifted');reason=$reason}
  }
}
$failed=@($cases|Where-Object {-not $_.passed})
$report=[ordered]@{selection=$(if($SourceOnly){'source-only'}else{'default'});sourceInputs=$sourceInputs;allPassed=($failed.Count -eq 0);caseCount=$cases.Count;failedCount=$failed.Count;cases=$cases;realSteamFilesModified=$false}
$report|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $root 'summary.json') -Encoding UTF8
$report|ConvertTo-Json -Depth 8
if($failed.Count){throw 'Compiler publication guard regression failed'}
