param(
    [ValidateSet('start', 'restart')]
    [string]$Mode = 'start',
    [switch]$Elevated
)

$ErrorActionPreference = 'Stop'

function Fail([int]$code, [string]$message, [switch]$NeedsElevation) {
    # Throw instead of exiting here so the transaction always releases its mutex
    # before an elevated child is started (and before tests observe the failure).
    $failure = New-Object System.InvalidOperationException($message)
    $failure.Data['HWiNFOExitCode'] = $code
    $failure.Data['HWiNFONeedsElevation'] = [bool]$NeedsElevation
    throw $failure
}

function Test-HWiNFOAccessDenied($errorRecord) {
    $exception = $errorRecord.Exception
    while ($exception) {
        if ($exception -is [System.UnauthorizedAccessException] -or
            $exception.HResult -eq -2147024891 -or
            ($exception -is [System.ComponentModel.Win32Exception] -and $exception.NativeErrorCode -eq 5)) {
            return $true
        }
        $exception = $exception.InnerException
    }
    return $false
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

function Stop-HWiNFO {
    $running = @(Get-Process -Name HWiNFO64 -ErrorAction SilentlyContinue)
    $watch = [System.Diagnostics.Stopwatch]::StartNew()
    foreach ($process in $running) {
        try {
            Stop-Process -InputObject $process -Force -ErrorAction Stop
        } catch {
            # A process that exited on its own is already stopped. Other errors
            # (notably an elevated HWiNFO) must not be silently ignored.
            if (@(Get-Process -Id $process.Id -ErrorAction SilentlyContinue).Count -gt 0) {
                Fail 4 ("HWiNFO64.exe could not be stopped: {0}" -f $_.Exception.Message) -NeedsElevation:(Test-HWiNFOAccessDenied $_)
            }
        }
    }
    foreach ($process in $running) {
        try {
            $remaining = [Math]::Max(0, 10000 - [int]$watch.ElapsedMilliseconds)
            if (-not $process.WaitForExit($remaining)) { Fail 4 'HWiNFO64.exe did not exit within 10 seconds; INI was not copied' }
        } catch {
            if ($_.Exception.Data.Contains('HWiNFOExitCode')) { throw }
            Fail 4 ("HWiNFO64.exe exit could not be confirmed; INI was not copied: {0}" -f $_.Exception.Message) -NeedsElevation:(Test-HWiNFOAccessDenied $_)
        }
    }
    # Re-enumerate as well: never copy if another instance appeared meanwhile.
    while (@(Get-Process -Name HWiNFO64 -ErrorAction SilentlyContinue).Count -gt 0) {
        if ($watch.ElapsedMilliseconds -ge 10000) { Fail 4 'HWiNFO64.exe is still running; INI was not copied' }
        Start-Sleep -Milliseconds 100
    }
}

function Get-HWiNFOConfigHash([string]$path) {
    $stream = [System.IO.File]::Open($path, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::Read)
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($sha.ComputeHash($stream)) }
    finally { $sha.Dispose(); $stream.Dispose() }
}

function Copy-HWiNFOConfig([string]$Source, [string]$Destination) {
    if (-not (Test-Path -LiteralPath $Source -PathType Leaf)) { Fail 3 'YeMan\HWiNFO64.INI was not found' }
    # Only bounded file-I/O retries belong here. A health retry must repeat the
    # entire stop -> copy -> launch transaction, never copy over a live process.
    for ($attempt = 0; $attempt -lt 5; $attempt++) {
        if (@(Get-Process -Name HWiNFO64 -ErrorAction SilentlyContinue).Count -gt 0) {
            Fail 4 'HWiNFO64.exe is running; refusing to overwrite HWiNFO64.INI'
        }
        try {
            $expectedHash = Get-HWiNFOConfigHash $Source
            Copy-Item -LiteralPath $Source -Destination $Destination -Force -ErrorAction Stop
            if ((Get-HWiNFOConfigHash $Destination) -ne $expectedHash) {
                throw 'HWiNFO64.INI verification failed after copying'
            }
            return
        } catch {
            if (Test-HWiNFOAccessDenied $_) {
                Fail 5 ("HWiNFO64.INI copy needs administrator permission: {0}" -f $_.Exception.Message) -NeedsElevation
            }
            if ($attempt -eq 4) { Fail 5 ("HWiNFO64.INI copy/verification failed: {0}" -f $_.Exception.Message) }
            Start-Sleep -Milliseconds 200
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

        # Read the first byte of the reading region, not just the mapping handle.
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

function Invoke-HWiNFORecovery {
    $mutex = $null
    $owned = $false
    try {
        $created = $false
        $mutex = New-Object System.Threading.Mutex($false, 'Global\YeManCC_HWiNFO_Recovery', [ref]$created)
        # The elevated handoff may race with another monitor's recovery, but the
        # parent is no longer holding this lock while it waits for its child.
        $lockWaitMs = 0
        if ($Elevated) { $lockWaitMs = 10000 }
        try { $owned = $mutex.WaitOne($lockWaitMs) } catch [System.Threading.AbandonedMutexException] { $owned = $true }
        if (-not $owned) { return 11 }

        if ($Mode -eq 'start' -and (Test-HWiNFOHealth)) {
            # Healthy start is a no-op: HWiNFO may save its in-memory settings on
            # exit, so writing its INI while it is running is never safe.
            foreach ($process in @(Get-Process -Name HWiNFO64 -ErrorAction SilentlyContinue)) {
                try { $process.ProcessorAffinity = [IntPtr]0xA0 } catch { }
            }
            return 0
        }

        $exe = Resolve-HWiNFO
        if (-not $exe) { Fail 2 'HWiNFO64.exe was not found' }
        $root = Split-Path -Parent $exe
        $src = Join-Path $root 'YeMan\HWiNFO64.INI'
        $dst = Join-Path $root 'HWiNFO64.INI'
        # Check dependencies before stopping an existing process.
        if (-not (Test-Path -LiteralPath $src -PathType Leaf)) { Fail 3 'YeMan\HWiNFO64.INI was not found' }

        Stop-HWiNFO
        Copy-HWiNFOConfig -Source $src -Destination $dst
        if (@(Get-Process -Name HWiNFO64 -ErrorAction SilentlyContinue).Count -gt 0) {
            Fail 4 'HWiNFO64.exe appeared during recovery; refusing to launch another instance'
        }
        # The copy and its byte-for-byte SHA-256 verification are complete before
        # launch. Preserve the original no-switch HWiNFO invocation.
        $process = Start-Process -FilePath $exe -WorkingDirectory $root -WindowStyle Hidden -PassThru
        try { $process.ProcessorAffinity = [IntPtr]0xA0 } catch { }
        for ($i = 0; $i -lt 120; $i++) {
            if (Test-HWiNFOHealth) { return 0 }
            if (@(Get-Process -Id $process.Id -ErrorAction SilentlyContinue).Count -eq 0) {
                Fail 6 'HWiNFO64.exe exited before shared memory became readable'
            }
            Start-Sleep -Milliseconds 250
        }
        Fail 7 'HWiNFO64.exe started but shared memory was not readable within 30 seconds'
    } finally {
        if ($owned -and $mutex) { try { $mutex.ReleaseMutex() } catch { } }
        if ($mutex) { $mutex.Dispose() }
    }
}

function Invoke-HWiNFORecoveryEntry {
    try { return (Invoke-HWiNFORecovery) }
    catch {
        $failure = $_.Exception
        if (-not $Elevated -and $failure.Data['HWiNFONeedsElevation']) {
            # Invoke-HWiNFORecovery has already run finally and released its lock.
            # Restart the full transaction, including exit confirmation. Quote the
            # script path because release/install directories can contain spaces.
            try {
                $arguments = '-NoProfile -ExecutionPolicy Bypass -File "{0}" -Mode restart -Elevated' -f $PSCommandPath
                $admin = Start-Process -FilePath 'powershell.exe' -Verb RunAs -ArgumentList $arguments -WorkingDirectory $PSScriptRoot -WindowStyle Hidden -Wait -PassThru
                return $admin.ExitCode
            } catch {
                [Console]::Error.WriteLine(("HWiNFO recovery needs administrator permission: {0}" -f $_.Exception.Message))
                return [int]$failure.Data['HWiNFOExitCode']
            }
        }
        [Console]::Error.WriteLine($failure.Message)
        if ($failure.Data.Contains('HWiNFOExitCode')) { return [int]$failure.Data['HWiNFOExitCode'] }
        return 1
    }
}

exit (Invoke-HWiNFORecoveryEntry)
