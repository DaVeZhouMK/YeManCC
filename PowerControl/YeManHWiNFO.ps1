param(
    [ValidateSet('start', 'restart')]
    [string]$Mode = 'start'
)

$ErrorActionPreference = 'Stop'
$exe = 'C:\Program Files\HWiNFO64\HWiNFO64.exe'
$root = 'C:\Program Files\HWiNFO64'
$src = 'C:\Program Files\HWiNFO64\YeMan'
$mutexName = 'Global\YeManCC_HWiNFO_Recovery'
$mutex = $null
$owned = $false

function Fail([int]$code, [string]$message) {
    [Console]::Error.WriteLine($message)
    exit $code
}

try {
    if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) { Fail 2 'HWiNFO64.exe is missing' }
    if (-not (Test-Path -LiteralPath $src -PathType Container)) { Fail 3 'HWiNFO YeMan configuration directory is missing' }
    $created = $false
    $mutex = New-Object System.Threading.Mutex($false, $mutexName, [ref]$created)
    try { $owned = $mutex.WaitOne(0) } catch [System.Threading.AbandonedMutexException] { $owned = $true }
    if (-not $owned) { exit 11 }

    $running = @(Get-Process -Name HWiNFO64 -ErrorAction SilentlyContinue)
    if ($Mode -eq 'restart' -and $running.Count -gt 0) {
        $running | Stop-Process -Force -ErrorAction SilentlyContinue
        for ($i = 0; $i -lt 20; $i++) {
            if (@(Get-Process -Name HWiNFO64 -ErrorAction SilentlyContinue).Count -eq 0) { break }
            Start-Sleep -Milliseconds 50
        }
        if (@(Get-Process -Name HWiNFO64 -ErrorAction SilentlyContinue).Count -gt 0) { Fail 4 'HWiNFO64.exe did not exit' }
    } elseif ($Mode -eq 'start' -and $running.Count -gt 0) {
        foreach ($p in $running) { try { $p.ProcessorAffinity = [IntPtr]0xA0 } catch { } }
        exit 0
    }

    $rc = 0
    & robocopy.exe $src $root /E /COPYALL /R:0 /W:0 /NFL /NDL /NJH /NJS /NP | Out-Null
    $rc = $LASTEXITCODE
    if ($rc -gt 7) { Fail 5 ("HWiNFO configuration copy failed, robocopy exit code {0}" -f $rc) }

    $p = Start-Process -FilePath $exe -WorkingDirectory $root -PassThru
    try { $p.ProcessorAffinity = [IntPtr]0xA0 } catch { }
    exit 0
} catch { Fail 1 $_.Exception.Message }
finally {
    if ($owned -and $mutex) { try { $mutex.ReleaseMutex() } catch { } }
    if ($mutex) { $mutex.Dispose() }
}
