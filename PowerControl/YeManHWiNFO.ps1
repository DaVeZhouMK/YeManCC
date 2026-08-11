param(
    [ValidateSet('start', 'restart')]
    [string]$Mode = 'start',
    [switch]$Elevated
)

$ErrorActionPreference = 'Stop'

function Fail([int]$code, [string]$message) {
    [Console]::Error.WriteLine($message)
    exit $code
}

function Resolve-HWiNFO {
    $candidates = @()
    try {
        $p = @(Get-Process -Name HWiNFO64 -ErrorAction SilentlyContinue | Select-Object -First 1)
        if ($p) { $candidates += $p.Path }
    } catch { }
    $candidates += @(
        (Join-Path ${env:ProgramFiles} 'HWiNFO64\HWiNFO64.exe'),
        (Join-Path ${env:ProgramFiles(x86)} 'HWiNFO64\HWiNFO64.exe'),
        (Join-Path ${env:LOCALAPPDATA} 'HWiNFO64\HWiNFO64.exe')
    )
    try {
        $keys = @(
            'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*',
            'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\*',
            'HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*'
        )
        foreach ($key in $keys) {
            Get-ItemProperty $key -ErrorAction SilentlyContinue |
                Where-Object { $_.DisplayName -match 'HWiNFO' } |
                ForEach-Object {
                    if ($_.InstallLocation) { $candidates += (Join-Path $_.InstallLocation 'HWiNFO64.exe') }
                    if ($_.DisplayIcon) { $candidates += ($_.DisplayIcon -replace ',.*$','').Trim('"') }
                }
        }
    } catch { }
    foreach ($candidate in ($candidates | Where-Object { $_ } | Select-Object -Unique)) {
        try {
            if (Test-Path -LiteralPath $candidate -PathType Leaf) {
                return (Resolve-Path -LiteralPath $candidate).Path
            }
        } catch { }
    }
    return $null
}

function Copy-HWiNFOConfig {
    if (-not (Test-Path -LiteralPath $src -PathType Leaf)) {
        Fail 3 'YeMan\HWiNFO64.INI was not found'
    }
    try {
        Copy-Item -LiteralPath $src -Destination $dst -Force -ErrorAction Stop
    } catch {
        if ($Elevated) { Fail 5 ("HWiNFO64.INI copy failed: {0}" -f $_.Exception.Message) }
        try {
            $args = @(
                '-NoProfile', '-ExecutionPolicy', 'Bypass',
                '-File', $PSCommandPath, '-Mode', $Mode, '-Elevated'
            )
            $admin = Start-Process -FilePath 'powershell.exe' -Verb RunAs -ArgumentList $args -Wait -PassThru
            exit $admin.ExitCode
        } catch {
            Fail 5 ("HWiNFO64.INI copy needs administrator permission: {0}" -f $_.Exception.Message)
        }
    }
}

function Test-HWiNFOSharedMemory {
    $mmf = $null
    $accessor = $null
    try {
        foreach ($name in @('Global\HWiNFO_SENS_SM2', 'HWiNFO_SENS_SM2', 'Global\HWiNFO_SENS_SM', 'HWiNFO_SENS_SM')) {
            try {
                $mmf = [System.IO.MemoryMappedFiles.MemoryMappedFile]::OpenExisting($name)
                if ($mmf) { break }
            } catch { $mmf = $null }
        }
        if (-not $mmf) { return $false }

        $accessor = $mmf.CreateViewAccessor(0, 0)
        if ($accessor.Capacity -lt 44) { return $false }
        if ($accessor.ReadUInt32(0) -ne 0x53695748) { return $false }
        $offset = [uint64]$accessor.ReadUInt32(32)
        $size = [uint64]$accessor.ReadUInt32(36)
        $count = [uint64]$accessor.ReadUInt32(40)
        if ($size -lt 316 -or $count -eq 0 -or $count -gt 100000) { return $false }
        $end = $offset + ($size * $count)
        if ($offset -ge [uint64]$accessor.Capacity -or $end -gt [uint64]$accessor.Capacity) { return $false }

        # 真实读取 reading 区间的首字节，避免只验证文件映射句柄。
        [void]$accessor.ReadByte([long]$offset)
        return $true
    } catch {
        return $false
    } finally {
        if ($accessor) { try { $accessor.Dispose() } catch { } }
        if ($mmf) { try { $mmf.Dispose() } catch { } }
    }
}

function Test-HWiNFOHealth {
    return (@(Get-Process -Name HWiNFO64 -ErrorAction SilentlyContinue).Count -gt 0) -and (Test-HWiNFOSharedMemory)
}

$exe = Resolve-HWiNFO
if (-not $exe) { Fail 2 'HWiNFO64.exe was not found' }
$root = Split-Path -Parent $exe
$src = Join-Path $root 'YeMan\HWiNFO64.INI'
$dst = Join-Path $root 'HWiNFO64.INI'
$mutex = $null
$owned = $false

try {
    $created = $false
    $mutex = New-Object System.Threading.Mutex($false, 'Global\YeManCC_HWiNFO_Recovery', [ref]$created)
    try { $owned = $mutex.WaitOne(0) } catch [System.Threading.AbandonedMutexException] { $owned = $true }
    if (-not $owned) { exit 11 }

    $running = @(Get-Process -Name HWiNFO64 -ErrorAction SilentlyContinue)
    $needsRestart = $Mode -eq 'restart'
    if ($Mode -eq 'start' -and $running.Count -gt 0 -and -not (Test-HWiNFOHealth)) {
        # start 模式发现进程在但共享内存不可读时，不能误报成功，必须重启恢复。
        $needsRestart = $true
    }
    if ($needsRestart -and $running.Count -gt 0) {
        $running | Stop-Process -Force -ErrorAction SilentlyContinue
        for ($i = 0; $i -lt 20; $i++) {
            if (@(Get-Process -Name HWiNFO64 -ErrorAction SilentlyContinue).Count -eq 0) { break }
            Start-Sleep -Milliseconds 50
        }
        if (@(Get-Process -Name HWiNFO64 -ErrorAction SilentlyContinue).Count -gt 0) {
            Fail 4 'HWiNFO64.exe did not exit'
        }
    } elseif ($Mode -eq 'start' -and $running.Count -gt 0) {
        Copy-HWiNFOConfig
        foreach ($p in $running) { try { $p.ProcessorAffinity = [IntPtr]0xA0 } catch { } }
        if (Test-HWiNFOHealth) { exit 0 }
        Fail 7 'HWiNFO64.exe is running but shared memory is not readable'
    }

    # Copy only YeMan\HWiNFO64.INI, then launch HWiNFO without switches.
    Copy-HWiNFOConfig
    $p = Start-Process -FilePath $exe -WorkingDirectory $root -PassThru
    try { $p.ProcessorAffinity = [IntPtr]0xA0 } catch { }
    for ($i = 0; $i -lt 120; $i++) {
        if (Test-HWiNFOHealth) { exit 0 }
        if (@(Get-Process -Id $p.Id -ErrorAction SilentlyContinue).Count -eq 0) {
            Fail 6 'HWiNFO64.exe exited before shared memory became readable'
        }
        Start-Sleep -Milliseconds 250
    }
    Fail 7 'HWiNFO64.exe started but shared memory was not readable within 30 seconds'
} catch { Fail 1 $_.Exception.Message }
finally {
    if ($owned -and $mutex) { try { $mutex.ReleaseMutex() } catch { } }
    if ($mutex) { $mutex.Dispose() }
}
