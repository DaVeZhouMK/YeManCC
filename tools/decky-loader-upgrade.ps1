<#
.SYNOPSIS
Consume unmodified official Decky. No MD is needed to upgrade this dependency.
.DESCRIPTION
1. Check shows the latest official stable Windows x64 noconsole release.
2. Prepare downloads to EvidenceDir and checks GitHub digest, PE architecture and source identity.
3. Apply backs up the old bundle and synchronizes EXE, license, lock and generated native header.
4. Review native/decky_sidebar_health.js against this release's actual error boundary Trace panel and
   DeckyPluginLoader lifecycle; regenerate with tools/decky_sidebar_health_embed.mjs. Run consumer tests,
   rebuild Native/Web/Decky, and commit those sources BEFORE creating a release tag.
5. ReleaseGate runs online immediately before GitHub publication; an old release or changed official
   asset blocks publication. Verify is offline for ordinary development/build/package checks.
YMCC does not patch/fork Loader internals. Windows v3.3.0 has no complete upstream self-update;
The ONLY shipped product update path is the existing YMCC whole-package download/install/rollback.
Do not restore a separate Decky updater. Run tools/decky_update_release_gate.ps1 at Build/Package/Verify.
No new runtime EXE, no live Steam access, no process restart, no user-setting changes.
.EXAMPLE
powershell -File tools/decky-loader-upgrade.ps1 -Action Check
.EXAMPLE
powershell -File tools/decky-loader-upgrade.ps1 -Action Prepare -EvidenceDir D:\task\decky
.EXAMPLE
powershell -File tools/decky-loader-upgrade.ps1 -Action Apply -EvidenceDir D:\task\decky
.EXAMPLE
powershell -File tools/decky-loader-upgrade.ps1 -Action ReleaseGate -EvidenceDir Build\Validation\DeckyUpdate
#>
[CmdletBinding()]
param(
 [ValidateSet('Status','Check','Prepare','Apply','Verify','ReleaseGate')][string]$Action='Status',
 [string]$EvidenceDir='',
 [string]$ProjectRoot=''
)
$ErrorActionPreference='Stop'
# Prefer this host's built-in modules even when launched from PowerShell 7/CI.
$ownModules=Join-Path $PSHOME 'Modules'
if((($env:PSModulePath -split ';')[0]).TrimEnd('\') -ine $ownModules.TrimEnd('\')){$env:PSModulePath=$ownModules+';'+$env:PSModulePath}
if(-not $ProjectRoot){$ProjectRoot=Split-Path -Parent $PSScriptRoot}
$ProjectRoot=[IO.Path]::GetFullPath($ProjectRoot)
$contract=Join-Path $ProjectRoot 'tools\decky-loader-contract.mjs'
$repo='SteamDeckHomebrew/decky-loader'
$headers=@{'User-Agent'='YMCC-Official-Decky-Consumer';'Accept'='application/vnd.github+json'}
# Keep authentication scoped to api.github.com; never forward it to binary/CDN URLs.
if($env:GITHUB_TOKEN){$headers.Authorization='Bearer '+$env:GITHUB_TOKEN}
function Run-Contract([string[]]$Arguments){
 $output=& node $contract @Arguments --root $ProjectRoot
 if($LASTEXITCODE -ne 0){throw 'DECKY_OFFICIAL_CONTRACT_FAILED'}
 return $output
}
if($Action -eq 'Status'){Run-Contract @('status');return}
if($Action -eq 'Verify'){Run-Contract @('verify');return}
if($Action -eq 'Apply'){
 if(-not $EvidenceDir){throw 'EvidenceDir from Prepare is required'}
 $EvidenceDir=[IO.Path]::GetFullPath($EvidenceDir)
 $prepared=Join-Path $EvidenceDir 'candidate.lock.json'
 $candidate=Get-Content -LiteralPath $prepared -Raw -Encoding UTF8|ConvertFrom-Json
 # Recreate the candidate from official receipts and ORIGINAL bytes before any mutation.
 $null=Run-Contract @('prepare-lock','--release',(Join-Path $EvidenceDir 'release.json'),'--commit',(Join-Path $EvidenceDir 'commit.json'),'--license',(Join-Path $EvidenceDir 'LICENSE'),'--asset',(Join-Path $EvidenceDir 'PluginLoader-win-noconsole.exe'),'--out',(Join-Path $EvidenceDir 'candidate.rechecked.json'))
 if((Get-FileHash -LiteralPath $prepared).Hash -ne (Get-FileHash -LiteralPath (Join-Path $EvidenceDir 'candidate.rechecked.json')).Hash){throw 'PREPARED_CANDIDATE_CHANGED'}
 $paths=@('PowerControl\decky\PluginLoader_noconsole.exe','PowerControl\decky\LICENSE.decky-loader','decky-plugin\vendor\decky-loader.lock.json','native\decky_loader_identity.h')
 $backup=Join-Path $EvidenceDir ('before-apply-'+[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fffffff'))
 New-Item -ItemType Directory -Path $backup -ErrorAction Stop|Out-Null
 foreach($relative in $paths){$file=Join-Path $ProjectRoot $relative;if(Test-Path -LiteralPath $file){$save=Join-Path $backup $relative;New-Item -ItemType Directory -Path (Split-Path -Parent $save) -Force|Out-Null;Copy-Item -LiteralPath $file -Destination $save}}
 try{
  # Only these four explicit source-owned files are changed; never installed files.
  Copy-Item -LiteralPath (Join-Path $EvidenceDir 'PluginLoader-win-noconsole.exe') -Destination (Join-Path $ProjectRoot $paths[0]) -Force
  Copy-Item -LiteralPath (Join-Path $EvidenceDir 'LICENSE') -Destination (Join-Path $ProjectRoot $paths[1]) -Force
  Copy-Item -LiteralPath $prepared -Destination (Join-Path $ProjectRoot $paths[2]) -Force
  $null=Run-Contract @('header','--write')
  Run-Contract @('verify')
 }catch{
  foreach($relative in $paths){$save=Join-Path $backup $relative;$target=Join-Path $ProjectRoot $relative;if(Test-Path -LiteralPath $save){Copy-Item -LiteralPath $save -Destination $target -Force}elseif(Test-Path -LiteralPath $target){Remove-Item -LiteralPath $target -Force}}
  throw
 }
 Write-Output ('DECKY_OFFICIAL_APPLIED backup='+$backup+' version='+$candidate.version)
 return
}
if($Action -eq 'ReleaseGate'){$null=Run-Contract @('verify')}
$release=Invoke-RestMethod -Uri ('https://api.github.com/repos/'+$repo+'/releases/latest') -Headers $headers -TimeoutSec 20
if($Action -eq 'Check'){
 $pin=Get-Content -LiteralPath (Join-Path $ProjectRoot 'decky-plugin\vendor\decky-loader.lock.json') -Raw -Encoding UTF8|ConvertFrom-Json
 [pscustomobject]@{bundled=$pin.version;latestStable=$release.tag_name;needsUpgrade=($pin.version -ne $release.tag_name);origin='official-release';asset='PluginLoader-win-noconsole.exe';action='Prepare, then Apply and rebuild before tagging'}|ConvertTo-Json
 return
}
if(-not $EvidenceDir){throw 'EvidenceDir required; outputs must be isolated from installed programs'}
$EvidenceDir=[IO.Path]::GetFullPath($EvidenceDir)
New-Item -ItemType Directory -Path $EvidenceDir -Force|Out-Null
$releasePath=Join-Path $EvidenceDir 'release.json'
$commitPath=Join-Path $EvidenceDir 'commit.json'
if($Action -eq 'Prepare' -and (Test-Path -LiteralPath (Join-Path $EvidenceDir 'candidate.lock.json'))){
 $previousCandidate=Get-Content -LiteralPath (Join-Path $EvidenceDir 'candidate.lock.json') -Raw -Encoding UTF8|ConvertFrom-Json
 if($previousCandidate.version -ne $release.tag_name){throw 'Use a fresh EvidenceDir when the official target version changes; old evidence is preserved.'}
}
$release|ConvertTo-Json -Depth 24|Set-Content -LiteralPath $releasePath -Encoding UTF8
# Strict release/tag/URL validation occurs again in the contract; do not run arbitrary assets.
if([string]$release.tag_name -notmatch '^v(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$'){throw 'OFFICIAL_STABLE_TAG_REQUIRED'}
$commit=Invoke-RestMethod -Uri ('https://api.github.com/repos/'+$repo+'/commits/'+$release.tag_name) -Headers $headers -TimeoutSec 20
$commit|ConvertTo-Json -Depth 24|Set-Content -LiteralPath $commitPath -Encoding UTF8
if($Action -eq 'ReleaseGate'){
 $result=Run-Contract @('latest','--release',$releasePath,'--commit',$commitPath)
 $receipt=$result|ConvertFrom-Json
 $receipt|Add-Member -NotePropertyName checkedAt -NotePropertyValue ([DateTime]::UtcNow.ToString('o'))
 $receipt|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $EvidenceDir 'UPSTREAM.json') -Encoding UTF8
 $result
 return
}
$asset=@($release.assets|Where-Object name -CEQ 'PluginLoader-win-noconsole.exe')
if($release.draft -or $release.prerelease -or $asset.Count -ne 1 -or $asset[0].digest -notmatch '^sha256:[a-f0-9]{64}$'){throw 'OFFICIAL_WINDOWS_ASSET_REQUIRED'}
$asset=$asset[0]
if($asset.url -cne ('https://api.github.com/repos/'+$repo+'/releases/assets/'+$asset.id)){throw 'OFFICIAL_ASSET_ORIGIN_REQUIRED'}
$download=Join-Path $EvidenceDir 'PluginLoader-win-noconsole.exe'
# Resume is transport-only. No incomplete file is accepted: exact official size/hash + PE are required.
$curl=Join-Path $env:SystemRoot 'System32\curl.exe'
$stalled=0
for($attempt=0;$attempt -lt 16;$attempt++){
 if((Test-Path -LiteralPath $download) -and (Get-Item -LiteralPath $download).Length -eq $asset.size){break}
 $beforeBytes=0;if(Test-Path -LiteralPath $download){$beforeBytes=(Get-Item -LiteralPath $download).Length}
 & $curl -L -C - --connect-timeout 10 --max-time 55 --fail --silent --show-error --output $download ($asset.url+'?download=1') -H 'Accept: application/octet-stream' -H 'User-Agent: YMCC-Official-Decky-Consumer'
 $downloadExit=$LASTEXITCODE;$afterBytes=0;if(Test-Path -LiteralPath $download){$afterBytes=(Get-Item -LiteralPath $download).Length}
 Write-Output ('DECKY_OFFICIAL_DOWNLOAD attempt='+($attempt+1)+' bytes='+$afterBytes+' expected='+$asset.size)
 if($downloadExit -notin @(0,18,28,56)){throw ('Official download rejected; previous bundle is unchanged. curl='+$downloadExit)}
 if($afterBytes -le $beforeBytes){$stalled++}else{$stalled=0}
 if($stalled -ge 3){throw 'Official download made no progress in three attempts; partial evidence retained for retry.'}
}
$licensePath=Join-Path $EvidenceDir 'LICENSE'
Invoke-WebRequest -Uri ('https://raw.githubusercontent.com/'+$repo+'/'+$release.tag_name+'/LICENSE') -OutFile $licensePath -TimeoutSec 20
Run-Contract @('prepare-lock','--release',$releasePath,'--commit',$commitPath,'--license',$licensePath,'--asset',$download,'--out',(Join-Path $EvidenceDir 'candidate.lock.json'))
