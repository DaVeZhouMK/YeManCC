[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][ValidateSet('Trainer','Manager','CheatEngine')][string]$Mode,
    [Parameter(Mandatory=$true)][string]$CacheRoot,
    [Parameter(Mandatory=$true)][string]$ResultPath,
    [Parameter(Mandatory=$true)][string]$RequestId,
    [string]$TrainerPath='', [string]$TrainerFolder='', [string]$ExpectedSha256='', [string]$GameExecutablePath='',
    [int]$GameProcessId=0, [string]$GameProcessCreated='', [string]$ExpectedGameSha256='',
    [string]$TrainerGameName='', [string]$TrainerOrigin='', [string]$GcmResourceId='', [string]$AppDirectory='',
    [switch]$InteractiveShell
)
$ErrorActionPreference='Stop'
$env:PSModulePath=(Join-Path $PSHOME 'Modules')+';'+$env:PSModulePath
Import-Module (Join-Path $PSScriptRoot 'Trainer/TrainerBackend.psm1') -Force -DisableNameChecking
Invoke-TrainerLaunch @PSBoundParameters
