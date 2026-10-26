[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$OutputPath)
$ErrorActionPreference='Stop'
if($env:GITHUB_ACTIONS -cne 'true' -or $env:RUNNER_ENVIRONMENT -cne 'github-hosted'){throw 'Disposable hosted capture required'}
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')).TrimEnd('\')
$baselineFile=Join-Path $repo 'tools/ymcc_sim/global/lighting-signals/tool-inputs.json'
$baseline=Get-Content -LiteralPath $baselineFile -Raw|ConvertFrom-Json
$python=(Get-Command python.exe -CommandType Application -ErrorAction Stop|Select-Object -First 1).Source
$pythonRoot=Split-Path -Parent $python
$pythonDll=@(Get-ChildItem -LiteralPath $pythonRoot -File -Filter 'python3*.dll'|Where-Object {$_.Name -match '^python3\d+\.dll$'})|Select-Object -First 1
$node=(Get-Command node.exe -CommandType Application -ErrorAction Stop|Select-Object -First 1).Source
$compiler=(Get-Command cl.exe -CommandType Application -ErrorAction Stop|Select-Object -First 1).Source
$dotnet=(Get-Command dotnet.exe -CommandType Application -ErrorAction Stop|Select-Object -First 1).Source
$edge='C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe'
$edgeRoot=Split-Path -Parent $edge
$edgeVersion=[regex]::Match((Get-Item -LiteralPath $edge).VersionInfo.FileVersion,'\d+\.\d+\.\d+\.\d+').Value
$nugetRoot=Join-Path $env:RUNNER_TEMP 'yc/Workspace/hosted-standard-user/.nuget/packages'
$env:NUGET_PACKAGES=$nugetRoot
$restoreRoot=Join-Path $env:RUNNER_TEMP 'ymcc-lighting-tool-restore'
[IO.Directory]::CreateDirectory($restoreRoot)|Out-Null
$restoreProject=Join-Path $restoreRoot 'tools.csproj'
[IO.File]::WriteAllText($restoreProject,'<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net10.0</TargetFramework></PropertyGroup><ItemGroup><PackageReference Include="Microsoft.NET.ILLink.Tasks" Version="10.0.11" /><PackageReference Include="System.Management" Version="10.0.9" /></ItemGroup></Project>')
& $dotnet restore $restoreProject --nologo
if($LASTEXITCODE -ne 0){throw 'Exact lighting tool NuGet restore failed'}
$oldTools='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207'
$oldSdk='C:\Program Files (x86)\Windows Kits\10'
$oldModules='C:\Users\DaVe\.cache\codex-runtimes\codex-primary-runtime\dependencies\node\node_modules'
$oldPython='C:\Users\DaVe\.cache\codex-runtimes\codex-primary-runtime\dependencies\python'
$oldNuget='C:\Users\DaVe\.nuget\packages'
$missing=[Collections.Generic.List[object]]::new();$mappings=[Collections.Generic.List[object]]::new();$rows=[Collections.Generic.List[object]]::new()
function Map-Path([string]$Path){
 $p=$Path.Replace('/','\')
 if($p -ieq $baseline.node){return $node}
 if($p.StartsWith($oldTools+'\',[StringComparison]::OrdinalIgnoreCase)){return $env:VCToolsInstallDir.TrimEnd('\')+$p.Substring($oldTools.Length)}
 if($p.StartsWith($oldSdk+'\',[StringComparison]::OrdinalIgnoreCase)){return $env:WindowsSdkDir.TrimEnd('\')+[regex]::Replace($p.Substring($oldSdk.Length),'(?i)(\\(?:Include|Lib)\\)10\.0\.26100\.0(?=\\)',('${1}'+$env:WindowsSDKVersion.TrimEnd('\')))}
 if($p.StartsWith($oldModules+'\',[StringComparison]::OrdinalIgnoreCase)){return $env:YMCC_RELEASE_NODE_MODULES.TrimEnd('\')+$p.Substring($oldModules.Length)}
 if($p.StartsWith($oldPython+'\',[StringComparison]::OrdinalIgnoreCase)){$value=$pythonRoot+$p.Substring($oldPython.Length);if($pythonDll){$value=[regex]::Replace($value,'(?i)python3\d+\.dll$',$pythonDll.Name)};return $value}
 if($p.StartsWith($oldNuget+'\',[StringComparison]::OrdinalIgnoreCase)){return $nugetRoot+$p.Substring($oldNuget.Length)}
 return $p
}
foreach($row in $baseline.files){
 if($row.path -and $row.path.StartsWith($edgeRoot+'\',[StringComparison]::OrdinalIgnoreCase)){continue}
 $file=if($row.repoRelative){Join-Path $repo $row.repoRelative}else{Map-Path $row.path}
 if(-not (Test-Path -LiteralPath $file -PathType Leaf) -and $file.Contains('\2052\')){$file=$file.Replace('\2052\','\1033\')}
 if(-not (Test-Path -LiteralPath $file -PathType Leaf)){$missing.Add(@{originalPath=$row.path;repoRelative=$row.repoRelative;selectedPath=$file});continue}
 $sha=(Get-FileHash -LiteralPath $file).Hash.ToLowerInvariant()
 $pin=[ordered]@{sha256=$sha;roles=@('Reviewed selected hosted lighting tool')}
 if($row.repoRelative){$pin.repoRelative=$row.repoRelative}else{$pin.path=[IO.Path]::GetFullPath($file)}
 $rows.Add($pin);$mappings.Add(@{originalPath=$row.path;repoRelative=$row.repoRelative;originalSha256=$row.sha256;selectedPath=$file;selectedSha256=$sha;sameBytes=($sha -ceq $row.sha256)})
}
$edgeFiles=@(Get-ChildItem -LiteralPath $edgeRoot -File)+@(Get-ChildItem -LiteralPath (Join-Path $edgeRoot $edgeVersion) -Recurse -File)
foreach($file in $edgeFiles){$rows.Add(@{path=$file.FullName;sha256=(Get-FileHash -LiteralPath $file.FullName).Hash.ToLowerInvariant();roles=@('Selected Microsoft Edge browser inventory')})}
$signatures=@(foreach($file in @($edge,(Join-Path $edgeRoot ($edgeVersion+'/msedge.dll')),$compiler,$python)){Get-AuthenticodeSignature -LiteralPath $file|Select-Object Path,@{n='status';e={[string]$_.Status}},@{n='signer';e={$_.SignerCertificate.Subject}}})
$result=[ordered]@{schemaVersion=1;kind=$baseline.kind;selectionStatus='CAPTURED_FOR_REVIEW';originalManifestSha256=(Get-FileHash -LiteralPath $baselineFile).Hash.ToLowerInvariant();maxArtifactInputs=3072;node=$node;dotnet=$dotnet;python=$python;cl=$compiler;edge=$edge;edgeVersion=$edgeVersion;runnerImageVersion=$env:ImageVersion;playwrightRequireAnchor=$env:YMCC_UI_TEST_REQUIRE_ANCHOR;files=@($rows);missing=@($missing);mappings=@($mappings);signatures=$signatures;scope=$baseline.scope;consumerExecution='NOT_RUN';formalReleaseAccepted=$false}
if($rows.Count+4 -gt 3072){throw 'Original lighting artifact budget exceeded'}
[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($OutputPath)))|Out-Null
[IO.File]::WriteAllText($OutputPath,($result|ConvertTo-Json -Depth 8),[Text.UTF8Encoding]::new($false))
Write-Output ('LIGHTING_HOST_CAPTURED files='+$rows.Count+' missing='+$missing.Count+' edge='+$edgeVersion)
if($missing.Count){throw 'Hosted lighting tool inputs are missing; inspect declaration'}