[CmdletBinding()]
param([switch]$CaptureOnly)
$ErrorActionPreference='Stop'
if($env:GITHUB_ACTIONS -cne 'true'){throw 'Hosted release setup requires a disposable GitHub runner'}
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')).TrimEnd('\')
$tempRoot=[IO.Path]::GetFullPath($env:RUNNER_TEMP).TrimEnd('\')
if($tempRoot -eq [IO.Path]::GetPathRoot($tempRoot).TrimEnd('\') -or $tempRoot.StartsWith($repo+'\',[StringComparison]::OrdinalIgnoreCase) -or $repo.StartsWith($tempRoot+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Hosted release temporary root must be outside sources'}
$core=Join-Path $tempRoot 'yc'
[IO.Directory]::CreateDirectory($core)|Out-Null
$markerPath=Join-Path $core '.ymcc-release-check-core.json'
if(Test-Path -LiteralPath $markerPath){$prior=Get-Content -LiteralPath $markerPath -Raw|ConvertFrom-Json;if($prior.owner -cne 'YMCC-release-check' -or $prior.repo -ine $repo){throw 'Hosted release core has another owner'}}
[IO.File]::WriteAllText($markerPath,(@{schemaVersion=1;owner='YMCC-release-check';repo=$repo}|ConvertTo-Json),[Text.UTF8Encoding]::new($false))
foreach($child in @('Evidence','Workspace')){[IO.Directory]::CreateDirectory((Join-Path $core $child))|Out-Null}
& (Join-Path $PSScriptRoot 'release-check-host/initialize-historical-inputs.ps1') -CoreRoot $core
$toolRoot=Join-Path $tempRoot 'ymcc-release-tools'
[IO.Directory]::CreateDirectory($toolRoot)|Out-Null
foreach($name in @('package.json','package-lock.json')){Copy-Item -LiteralPath (Join-Path $PSScriptRoot ('release-check-host/'+$name)) -Destination (Join-Path $toolRoot $name)}
$npm=(Get-Command npm.cmd -CommandType Application -ErrorAction Stop|Select-Object -First 1).Source
Push-Location $toolRoot
try{& $npm ci --ignore-scripts --no-audit --no-fund;if($LASTEXITCODE -ne 0){throw 'Locked release browser tools restore failed'}}finally{Pop-Location}
$modules=Join-Path $toolRoot 'node_modules'
$settings=[ordered]@{YMCC_SIM_CORE_ROOT=$core;YMCC_RELEASE_NODE_MODULES=$modules;YMCC_UI_BROWSER_MODULE_DIR=$modules;YMCC_UI_TEST_REQUIRE_ANCHOR=(Join-Path $modules '_release.cjs')}
$lightingManifest=Join-Path $repo 'tools/ymcc_sim/global/lighting-signals/hosted-tool-inputs.json'
if(Test-Path -LiteralPath $lightingManifest -PathType Leaf){
 $lighting=Get-Content -LiteralPath $lightingManifest -Raw|ConvertFrom-Json
 $baseline=Join-Path $repo 'tools/ymcc_sim/global/lighting-signals/tool-inputs.json'
 if($lighting.selectionStatus -cne 'REVIEWED_HOST_DECLARATION' -or $lighting.originalManifestSha256 -cne (Get-FileHash -LiteralPath $baseline).Hash.ToLowerInvariant()){throw 'Reviewed lighting host declaration required'}
 $settings['YMCC_LIGHTING_TOOLS_MANIFEST']=$lightingManifest
 $settings['YMCC_LIGHTING_TOOLS_SHA256']=(Get-FileHash -LiteralPath $lightingManifest).Hash.ToLowerInvariant()
}
if(-not $CaptureOnly){$settings['YMCC_RELEASE_HOSTED_ENVIRONMENT']='1'}
foreach($entry in $settings.GetEnumerator()){[Environment]::SetEnvironmentVariable($entry.Key,$entry.Value,'Process');($entry.Key+'='+$entry.Value)|Out-File -LiteralPath $env:GITHUB_ENV -Encoding utf8 -Append}
Write-Output ('HOSTED_RELEASE_CHECK_ENVIRONMENT_READY captureOnly='+[bool]$CaptureOnly)