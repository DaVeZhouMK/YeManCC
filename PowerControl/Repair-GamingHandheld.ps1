# Legacy task compatibility only: SYSTEM must NEVER write HKCU startup settings.
# The YMCC UI writes StartupToGamingHome=1 in the interactive user's own hive.
[CmdletBinding()]
param([switch]$Inspect)
$ErrorActionPreference = 'Stop'
$oemPath = 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\OEM'
$biosPath = 'HKLM:\HARDWARE\DESCRIPTION\System\BIOS'
$bios = Get-ItemProperty -LiteralPath $biosPath
$current = $null
if (Test-Path -LiteralPath $oemPath) {
    $key = Get-Item -LiteralPath $oemPath
    if ($key.GetValueNames() -contains 'DeviceForm') {
        if ($key.GetValueKind('DeviceForm') -ne [Microsoft.Win32.RegistryValueKind]::DWord) {
            throw 'DeviceForm is not a DWORD; refusing to overwrite an unknown registry type.'
        }
        $current = $key.GetValue('DeviceForm')
    }
}
$isRog = $bios.SystemManufacturer -match 'ASUS' -and
    $bios.SystemProductName -match 'ROG.*Ally|\bRC7[123][A-Z0-9_]*\b'
if (-not $isRog -and $current -ne 46) {
    throw 'No recognized ROG handheld/OEM handheld identity; desktop classification is unchanged.'
}
if ($Inspect -or $current -eq 46) {
    [pscustomobject]@{ DeviceForm = $current; Target = 46; Changed = $false } | ConvertTo-Json -Compress
    return
}
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Administrator permission is required; no changes have been made.'
}
$directory = Join-Path $env:ProgramData 'YeMan\gaming-home-backups'
if (-not (Test-Path -LiteralPath $directory)) {
    New-Item -Path $directory -ItemType Directory | Out-Null
}
$backup = Join-Path $directory ('DeviceForm-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '.json')
[pscustomobject]@{
    Schema = 1; Value = 'DeviceForm'; Before = $current; Target = 46
    Manufacturer = $bios.SystemManufacturer; Product = $bios.SystemProductName
    UserSid = $identity.User.Value
} | ConvertTo-Json | Set-Content -LiteralPath $backup -Encoding UTF8
if (-not (Test-Path -LiteralPath $oemPath)) {
    New-Item -Path $oemPath | Out-Null
}
New-ItemProperty -LiteralPath $oemPath -Name DeviceForm -PropertyType DWord -Value 46 -Force | Out-Null
if ((Get-ItemProperty -LiteralPath $oemPath).DeviceForm -ne 46) {
    throw ('DeviceForm readback failed; original value is recorded in ' + $backup)
}
[pscustomobject]@{ DeviceForm = 46; Changed = $true; Backup = $backup } | ConvertTo-Json -Compress