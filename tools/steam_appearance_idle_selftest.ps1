# Compatibility entry for the retired timeout/kill policy. Exercise resident verification.
[CmdletBinding()]
param([Alias('Out')][string]$OutputDirectory='G:/YeManCC-Work/Testing/YMCC/Evidence/SteamAppearanceResidentCompatibility')
$ErrorActionPreference='Stop'
& (Join-Path $PSScriptRoot 'steam_appearance_manual_observation_selftest.ps1') -Out $OutputDirectory
if($LASTEXITCODE){exit $LASTEXITCODE}
