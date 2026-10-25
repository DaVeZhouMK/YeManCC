[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$OutputPath)
$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')).TrimEnd('\')
$output=[IO.Path]::GetFullPath($OutputPath)
if($output -eq $project -or $output.StartsWith($project+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Host tool evidence must be outside product sources'}
$declarations=@('tools/ymcc_sim/global/decky-update-recovery/native-launch-noio/native-tools.json','tools/ymcc_sim/global/decky-persistent-lifecycle/owned-tools.json','tools/ymcc_sim/global/decky-update-recovery/package-fixture-tools.json')
$native=Get-Content -LiteralPath (Join-Path $project $declarations[0]) -Raw|ConvertFrom-Json
$owned=Get-Content -LiteralPath (Join-Path $project $declarations[1]) -Raw|ConvertFrom-Json
$package=Get-Content -LiteralPath (Join-Path $project $declarations[2]) -Raw|ConvertFrom-Json
$compiler=(Get-Command cl.exe -CommandType Application -ErrorAction Stop|Select-Object -First 1).Source
$node=(Get-Command node.exe -CommandType Application -ErrorAction Stop|Select-Object -First 1).Source
$toolRoot=[IO.Path]::GetFullPath($env:VCToolsInstallDir).TrimEnd('\')
$vsRoot=[IO.Path]::GetFullPath($env:VSINSTALLDIR).TrimEnd('\')
$sdkRoot=[IO.Path]::GetFullPath($env:WindowsSdkDir).TrimEnd('\')
$sdkVersion=$env:WindowsSDKVersion.TrimEnd('\')
if(-not $compiler.StartsWith($toolRoot+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Selected compiler differs from initialized MSVC tool root'}
$missing=[Collections.Generic.List[object]]::new();$mapping=[Collections.Generic.List[object]]::new()
$replaced=[Collections.Generic.List[object]]::new()
$oldVs='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools'
$oldTools=$oldVs+'\VC\Tools\MSVC\14.44.35207'
$oldSdk='C:\Program Files (x86)\Windows Kits\10'
$oldModules='C:\Users\DaVe\.cache\codex-runtimes\codex-primary-runtime\dependencies\node\node_modules'
function Mapped([string]$Path) {
 $p=$Path.Replace('/','\')
 if($p -ieq 'G:\YeManCC-Work\Environment\Toolchains\nodejs\node-v22.14.0-win-x64\node.exe'){return $node}
 if($p.StartsWith($oldModules+'\',[StringComparison]::OrdinalIgnoreCase) -and $env:YMCC_RELEASE_NODE_MODULES){return $env:YMCC_RELEASE_NODE_MODULES.TrimEnd('\')+$p.Substring($oldModules.Length)}
 if($p.StartsWith($oldTools+'\',[StringComparison]::OrdinalIgnoreCase)){return $toolRoot+$p.Substring($oldTools.Length)}
 if($p.StartsWith($oldVs+'\',[StringComparison]::OrdinalIgnoreCase)){return $vsRoot+$p.Substring($oldVs.Length)}
 if($p.StartsWith($oldSdk+'\',[StringComparison]::OrdinalIgnoreCase)){
  $tail=[regex]::Replace($p.Substring($oldSdk.Length),'(?i)(\\(?:Include|Lib)\\)10\.0\.26100\.0(?=\\)',('${1}'+$sdkVersion))
  return $sdkRoot+$tail
 }
 if($p.StartsWith('C:\Windows\',[StringComparison]::OrdinalIgnoreCase)){return $env:SystemRoot+$p.Substring(10)}
 return [IO.Path]::GetFullPath($p)
}
function Capture($Row,[string]$Group) {
 $path=Mapped ([string]$Row.path)
 if(-not (Test-Path -LiteralPath $path -PathType Leaf)){$missing.Add([ordered]@{group=$Group;originalPath=$Row.path;selectedPath=$path});return $null}
 $hash=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
 $mapping.Add([ordered]@{group=$Group;originalPath=$Row.path;originalSha256=$Row.sha256;selectedPath=$path;selectedSha256=$hash;sameBytes=($hash -ceq $Row.sha256)})
 return [ordered]@{path=$path;sha256=$hash;role='Fixed hosted tool input; original consumer dependency audit remains required'}
}
# Different MSVC releases have different complete bin inventories. Capture the host
# inventory and all host STL headers explicitly; do not pretend absent old tools exist.
$nativeRows=@(foreach($row in $native.files){
 $p=$row.path.Replace('/','\')
 if($p.StartsWith($oldTools+'\bin\Hostx64\x64\',[StringComparison]::OrdinalIgnoreCase)){$replaced.Add($row);continue}
 $pin=Capture $row 'native';if($pin){$pin}
})
$hostRows=@(foreach($directory in @((Join-Path $toolRoot 'bin/Hostx64/x64'),(Join-Path $toolRoot 'include'))){
 foreach($entry in Get-ChildItem -LiteralPath $directory -File -Recurse){[ordered]@{path=$entry.FullName;sha256=(Get-FileHash -LiteralPath $entry.FullName).Hash.ToLowerInvariant();role='Complete selected host MSVC x64 bin inventory or STL header inventory'}}
})
$nativeFiles=@(@($nativeRows)+@($hostRows)|Sort-Object -Property path -Unique)
$ownedFiles=@(foreach($row in $owned.files){$pin=Capture $row 'owned';if($pin){$pin}})
$packageFiles=@(foreach($row in $package.files){$pin=Capture $row 'package';if($pin){$pin}})
$powerShell=Capture $native.powershell 'powershell'
$selectedNative=[ordered]@{compiler=(Mapped $native.native.compiler);linker=(Mapped $native.native.linker);dumpbin=(Mapped $native.native.dumpbin);commandProcessor=(Mapped $native.native.commandProcessor);include=@($native.native.include|ForEach-Object{Mapped $_});lib=@($native.native.lib|ForEach-Object{Mapped $_})}
$signatures=@(foreach($file in @($selectedNative.compiler,$selectedNative.linker,$selectedNative.dumpbin,$selectedNative.commandProcessor,$node,$powerShell.path,(Mapped $package.compiler))|Sort-Object -Unique){if(Test-Path -LiteralPath $file){$sig=Get-AuthenticodeSignature -LiteralPath $file;[ordered]@{path=$file;status=[string]$sig.Status;signer=$(if($sig.SignerCertificate){$sig.SignerCertificate.Subject}else{''});sha256=(Get-FileHash -LiteralPath $file).Hash.ToLowerInvariant()}}})
$result=[ordered]@{schemaVersion=1;kind='ymcc-hosted-release-tool-declaration-candidate';status=$(if($missing.Count){'MISSING_HOST_FILES'}else{'CAPTURED_FOR_REVIEW'});imageOS=$env:ImageOS;imageVersion=$env:ImageVersion;runnerOS=$env:RUNNER_OS;runnerArchitecture=$env:RUNNER_ARCH;node=@{path=$node;version=(& $node --version);sha256=(Get-FileHash -LiteralPath $node).Hash.ToLowerInvariant()};visualStudioRoot=$vsRoot;msvcRoot=$toolRoot;windowsSdkRoot=$sdkRoot;windowsSdkVersion=$sdkVersion;nativeManifest=@{meaning='Independent hosted x64 compiler inventory; original local manifest unchanged';native=$selectedNative;files=$nativeFiles;powershell=$powerShell};ownedManifest=@{files=$ownedFiles};packageFixtureManifest=@{schemaVersion=1;compiler=(Mapped $package.compiler);referencedAssemblies=@($package.referencedAssemblies|ForEach-Object{Mapped $_});files=$packageFiles};originalDeclarations=@(foreach($relative in $declarations){@{path=$relative;sha256=(Get-FileHash -LiteralPath (Join-Path $project $relative)).Hash.ToLowerInvariant()}});replacedLocalCompilerBinInventory=@($replaced);hostCompilerInventoryCount=$hostRows.Count;mappings=@($mapping);missing=@($missing);authenticode=$signatures;consumerExecution='NOT_RUN';formalReleaseAccepted=$false;atUtc=[DateTime]::UtcNow.ToString('o')}
if($nativeFiles.Count -gt 3072 -or $ownedFiles.Count -gt 3072){throw 'Original tool declaration limit exceeded'}
[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($output))|Out-Null
[IO.File]::WriteAllText($output,($result|ConvertTo-Json -Depth 12),[Text.UTF8Encoding]::new($false))
Write-Output ('HOST_TOOL_DECLARATION_CAPTURED missing='+$missing.Count+' native='+$nativeFiles.Count)
if($missing.Count){throw 'Hosted release tool declaration has missing files; inspect diagnostics before enabling consumers'}