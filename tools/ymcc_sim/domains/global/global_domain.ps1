<# Unified source inventory, reviewed desktop simulations, sleep replay and result differences.
   Reuses the existing ymcc_sim environment; no OS sleep or installed-product control. #>
[CmdletBinding()]
param(
 [ValidateSet('inventory','plan','run','replay','compare','corpus','replay-plan','log-check','signal-check','audit')][string]$Mode='inventory',
 [ValidateSet('desktop','full','sleep')][string]$Profile='desktop',
 [string]$Domains='',
 [string]$Tests='',
 [ValidateScript({ $_.Length -le 1024 -and $_ -cmatch '^[a-z][a-z0-9]*(?:[.-][a-z0-9]+)*(?:,[a-z][a-z0-9]*(?:[.-][a-z0-9]+)*)*$' })][string[]]$Capabilities,
 [string]$EvidenceDir='',
 [string]$PayloadRoot='',
 [string]$CompleteHostContract='',
 [string]$RuntimeRoot='',
 [string]$SharedRuntimeRoot='',
 [string]$LightingPayloadRoot='',
 [string]$ExpectedHelperSha256='',
 [string]$HcProfile='',
 [string]$HcVersion='',
 [string]$LegacyHostRoot='',
 [string]$FanToolLoadPlan='',
 [string]$FanToolLoadPlanSha256='',
 [string]$MonitorFieldLogInput='',
 [string]$LogPath='',
 [string]$Baseline='',
 [string]$Candidate='',
 [string]$LogRoot='',
 [string]$Archive='',
 [string]$Python='',
 [string]$Receipts='',
 [switch]$AuditRegistered,
 [switch]$RequireAll
)
$ErrorActionPreference='Stop'
if($PSBoundParameters.ContainsKey('Capabilities')){
 if(@($Capabilities).Count -eq 0 -or @($Capabilities).Count -gt 64){throw 'CAPABILITY_INVALID_ARGUMENT'}
 if($Mode -notin @('inventory','plan','run')){throw 'CAPABILITY_UNSUPPORTED_MODE'}
}
$simRoot=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$entry=Join-Path $simRoot 'global\cli.mjs'
$node=Get-Command node.exe -CommandType Application -ErrorAction Stop | Select-Object -First 1
$arguments=@($entry,'--mode',$Mode,'--profile',$Profile)
foreach($capabilityValue in $Capabilities){$arguments+=@('--capability',$capabilityValue)}
foreach($pair in @(
 @('--domain',$Domains),@('--test',$Tests),@('--out-dir',$EvidenceDir),
 @('--payload-root',$PayloadRoot),@('--complete-host-contract',$CompleteHostContract),@('--runtime-root',$RuntimeRoot),@('--shared-runtime-root',$SharedRuntimeRoot),@('--lighting-payload-root',$LightingPayloadRoot),@('--expected-helper-sha256',$ExpectedHelperSha256),@('--hc-profile',$HcProfile),@('--hc-version',$HcVersion),@('--legacy-host-root',$LegacyHostRoot),@('--fan-tool-load-plan',$FanToolLoadPlan),@('--fan-tool-load-plan-sha256',$FanToolLoadPlanSha256),@('--monitor-field-log-input',$MonitorFieldLogInput),@('--log-path',$LogPath),@('--baseline',$Baseline),@('--candidate',$Candidate),@('--log-root',$LogRoot),@('--archive',$Archive),@('--python',$Python)
)) {if(-not [string]::IsNullOrWhiteSpace([string]$pair[1])){$arguments+=@([string]$pair[0],[string]$pair[1])}}
foreach($receipt in ($Receipts -split ',')){if(-not [string]::IsNullOrWhiteSpace($receipt)){$arguments+=@('--receipt',$receipt.Trim())}}
$pwsh=Join-Path $PSHOME 'pwsh.exe'
if(-not(Test-Path -LiteralPath $pwsh)){
 $command=Get-Command pwsh.exe -ErrorAction SilentlyContinue
 if($command){$pwsh=$command.Source}else{$pwsh=Join-Path $PSHOME 'powershell.exe'}
}
$arguments+=@('--powershell',$pwsh)
if($AuditRegistered){$arguments+='--audit-registered'}
if($RequireAll){$arguments+='--require-all'}
& $node.Source @arguments
exit $LASTEXITCODE