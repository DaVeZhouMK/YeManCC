# Legacy task compatibility only: SYSTEM must NEVER write HKCU startup settings.
# The YMCC UI writes StartupToGamingHome=1 in the interactive user's own hive.
# Explicit repair is available on all machines, without a manufacturer/model gate.
[CmdletBinding()]
param([switch]$Inspect)
$ErrorActionPreference = 'Stop'
$oemPath = 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\OEM'
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
if ($Inspect -or $current -eq 46) {
    [pscustomobject]@{ DeviceForm = $current; Target = 46; Changed = $false } | ConvertTo-Json -Compress
    return
}
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Administrator permission is required; no changes have been made.'
}
function Read-GamingBackupText {
    param([string]$Path)
    $file = [IO.FileStream]::new($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
    try {
        if ($file.Length -le 0 -or $file.Length -gt 65536) { throw 'Backup is empty or exceeds its limit.' }
        $length = [int]$file.Length
        $bytes = [byte[]]::new($length)
        $offset = 0
        while ($offset -lt $length) {
            $read = $file.Read($bytes, $offset, $length - $offset)
            if ($read -le 0) { throw 'Backup read is incomplete.' }
            $offset += $read
        }
        if ($file.Length -ne $length -or $file.ReadByte() -ne -1) { throw 'Backup changed during read.' }
        return ([Text.UTF8Encoding]::new($false, $true).GetString($bytes)).TrimStart([char]0xfeff)
    } finally { $file.Dispose() }
}
function Save-GamingDeviceFormBackup {
    param([string]$Directory, $Before, [string]$UserSid)
    # RegistryKey.GetValue returns DWORD high bits as signed Int32.
    if ($Before -is [int] -and $Before -lt 0) {
        $Before = [BitConverter]::ToUInt32([BitConverter]::GetBytes($Before), 0)
    }
    # A target default never supplies an unknown original registry value.
    if ($null -ne $Before -and ($Before -is [bool] -or
        ($Before -isnot [int] -and $Before -isnot [long] -and $Before -isnot [uint32]) -or
        $Before -lt 0 -or $Before -gt [uint32]::MaxValue)) {
        throw 'Original DeviceForm DWORD is unconfirmed; no changes have been made.'
    }
    if ($UserSid -notmatch '^S-1-\d+(?:-\d+)+$') { throw 'Backup account identity is unconfirmed.' }
    $payload = [pscustomobject]@{
        Schema = 1; Value = 'DeviceForm'; Before = $Before; Target = 46; UserSid = $UserSid
    } | ConvertTo-Json -Depth 4
    $backup = Join-Path $Directory ('DeviceForm-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '.json')
    $base = $backup
    for ($attempt = 0; Test-Path -LiteralPath $backup; ++$attempt) {
        if ($attempt -ge 16) { throw 'No unused backup path; originals were retained.' }
        $backup = $base.Substring(0, $base.Length - 5) + '-' + $attempt + '.json'
    }
    $temp = $backup + '.' + [guid]::NewGuid().ToString('N') + '.tmp'
    try {
        # Create the staging name exclusively; an existing backup is never opened for write.
        $reservation = [IO.FileStream]::new($temp, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
        $reservation.Dispose()
        Set-Content -LiteralPath $temp -Value $payload -Encoding UTF8 -NoNewline
        $flush = [IO.FileStream]::new($temp, [IO.FileMode]::Open, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
        try { $flush.Flush($true) } finally { $flush.Dispose() }
        if ((Read-GamingBackupText $temp) -cne $payload) { throw 'Staging backup readback mismatch.' }
        # File.Move has no overwrite option on the supported Windows PowerShell runtime.
        [IO.File]::Move($temp, $backup)
        if ((Read-GamingBackupText $backup) -cne $payload) { throw 'Published backup readback mismatch.' }
        return $backup
    } finally {
        if (Test-Path -LiteralPath $temp) {
            try { Remove-Item -LiteralPath $temp -Force } catch { }
        }
    }
}
$directory = Join-Path $env:ProgramData 'YeMan\gaming-home-backups'
if (-not (Test-Path -LiteralPath $directory)) {
    New-Item -Path $directory -ItemType Directory | Out-Null
}
$backup = Save-GamingDeviceFormBackup -Directory $directory -Before $current -UserSid $identity.User.Value
if (-not (Test-Path -LiteralPath $oemPath)) {
    New-Item -Path $oemPath | Out-Null
}
New-ItemProperty -LiteralPath $oemPath -Name DeviceForm -PropertyType DWord -Value 46 -Force | Out-Null
if ((Get-ItemProperty -LiteralPath $oemPath).DeviceForm -ne 46) {
    throw ('DeviceForm readback failed; original value is recorded in ' + $backup)
}
[pscustomobject]@{ DeviceForm = 46; Changed = $true; Backup = $backup } | ConvertTo-Json -Compress