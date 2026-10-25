[CmdletBinding()]
param(
    [string]$GameName = '',
    [ValidateSet('Prepare','RefreshData')][string]$Mode = 'Prepare',
    [Parameter(Mandatory = $true)][string]$CacheRoot,
    [Parameter(Mandatory = $true)][string]$ResultPath,
    [Parameter(Mandatory = $true)][string]$RequestId,
    [string]$GameExecutablePath = '',
    [int]$GameProcessId = 0,
    [string]$GameProcessCreated = '',
    [switch]$Force
)
$ErrorActionPreference = 'Stop'
$env:PSModulePath = (Join-Path $PSHOME 'Modules') + ';' + $env:PSModulePath
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
Import-Module (Join-Path $PSScriptRoot 'Trainer/TrainerBackend.psm1') -Force -DisableNameChecking
if ($Mode -eq 'RefreshData') {
    Invoke-TrainerDataRefresh -CacheRoot $CacheRoot -ResultPath $ResultPath -RequestId $RequestId
    return
}
Invoke-TrainerPrepare -GameName $GameName -CacheRoot $CacheRoot -ResultPath $ResultPath -RequestId $RequestId -Force:$Force -GameExecutablePath $GameExecutablePath -GameProcessId $GameProcessId -GameProcessCreated $GameProcessCreated