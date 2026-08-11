$ErrorActionPreference = 'Stop'

function Resolve-RTSS {
    $candidates = @()
    try {
        $p = @(Get-Process -Name RTSS -ErrorAction SilentlyContinue | Select-Object -First 1)
        if ($p) { $candidates += $p.Path }
    } catch { }
    $candidates += @(
        (Join-Path ${env:ProgramFiles(x86)} 'RivaTuner Statistics Server\RTSS.exe'),
        (Join-Path ${env:ProgramFiles} 'RivaTuner Statistics Server\RTSS.exe'),
        (Join-Path ${env:LOCALAPPDATA} 'RivaTuner Statistics Server\RTSS.exe')
    )
    foreach ($k in @('HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*','HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\*','HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*')) {
        Get-ItemProperty $k -ErrorAction SilentlyContinue | Where-Object { $_.DisplayName -match 'RivaTuner Statistics Server|RTSS' } | ForEach-Object {
            if ($_.InstallLocation) { $candidates += (Join-Path $_.InstallLocation 'RTSS.exe') }
            if ($_.DisplayIcon) { $candidates += ($_.DisplayIcon -replace ',.*$','').Trim('"') }
        }
    }
    foreach ($candidate in ($candidates | Where-Object { $_ } | Select-Object -Unique)) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            $dir = Split-Path -Parent (Resolve-Path -LiteralPath $candidate).Path
            if (Test-Path -LiteralPath (Join-Path $dir 'RTSSHooks64.dll') -PathType Leaf) { return @{ Exe=(Join-Path $dir 'RTSS.exe'); Dir=$dir } }
        }
    }
    throw 'RTSS.exe was not found; install RivaTuner Statistics Server'
}

$rtss = Resolve-RTSS
$global = Join-Path $rtss.Dir 'Profiles\Global'
$overlay = Join-Path $rtss.Dir 'Plugins\Client\OverlayEditor.cfg'
if ((Test-Path -LiteralPath $global -PathType Leaf) -and (Select-String -LiteralPath $global -Pattern '^EnableOSD=1' -Quiet) -and (Test-Path -LiteralPath (Split-Path -Parent $overlay))) {
    Set-Content -LiteralPath $overlay -Value "[Settings]`r`nLayout=YeManOBS-W-1.ovl" -Encoding Default
}
if (-not (Get-Process -Name RTSS -ErrorAction SilentlyContinue)) {
    Start-Process -FilePath $rtss.Exe -WorkingDirectory $rtss.Dir -WindowStyle Hidden | Out-Null
}
for ($i = 0; $i -lt 80; $i++) {
    if (Get-Process -Name RTSS -ErrorAction SilentlyContinue) { exit 0 }
    Start-Sleep -Milliseconds 100
}
throw 'RTSS.exe did not stay running after launch'
