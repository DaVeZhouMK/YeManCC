[CmdletBinding()]
param(
 [Parameter(Mandatory=$true)][string]$ArtifactsPath,
 [Parameter(Mandatory=$true)][string]$FixtureSource,
 [Parameter(Mandatory=$true)][string]$BootstrapSource,
 [Parameter(Mandatory=$true)][string]$CecilPath,
 [Parameter(Mandatory=$true)][string]$NuGetConfig,
 [Parameter(Mandatory=$true)][string]$EmptyFeed,
 [Parameter(Mandatory=$true)][string]$PackagesPath
)
$ErrorActionPreference='Stop'
foreach($file in @($FixtureSource,$BootstrapSource,$CecilPath,$NuGetConfig)) {if(-not(Test-Path -LiteralPath $file -PathType Leaf)){throw "TOOL_BUILD_INPUT_MISSING:$file"}}
foreach($project in @('Harness.csproj','JoinedFactoryProbe.csproj','BootstrapFixture.csproj','load-authority/STA.csproj','load-authority/MTA.csproj')) {
 & dotnet build (Join-Path $PSScriptRoot $project) --artifacts-path $ArtifactsPath --source $EmptyFeed --packages $PackagesPath -c Release --nologo -v:q -m:1 -p:UseSharedCompilation=false -nodeReuse:false -p:ImportDirectoryBuildProps=false -p:ImportDirectoryBuildTargets=false -p:NuGetAudit=false "-p:NuGetConfigFile=$NuGetConfig" "-p:FixtureSource=$FixtureSource" "-p:BootstrapSource=$BootstrapSource" "-p:CecilPath=$CecilPath"
 if($LASTEXITCODE -ne 0){throw "TOOL_BUILD_FAILED:$project exit=$LASTEXITCODE"}
}
# Compile only. No producer/Host entry is invoked by this script.
