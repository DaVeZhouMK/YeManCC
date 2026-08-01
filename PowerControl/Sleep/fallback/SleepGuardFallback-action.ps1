param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('recover', 'end')]
    [string]$Mode
)

# Wake fallback: act on orphan frozen games left by YeManCC sleep guard.
# Runs only when YeManCC.exe is NOT running (the bat pre-checks that).
# Marker convention (must match native main.cpp sgSuspendTarget / sgResumeAll):
#   file:  C:\SOFT\YeMan\PowerControl\Sleep\suspended\<pid>.txt
#   content: name=<exe>|epoch=<ts>|tdplocked=<watts>

$ErrorActionPreference = 'SilentlyContinue'

$SG_DIR   = 'C:\SOFT\YeMan\PowerControl\Sleep'
$SUSP_DIR = Join-Path $SG_DIR 'suspended'
$TDP_FILE = 'C:\SOFT\YeMan\PowerControl\tdp-dc.txt'
$TDP_EXE  = 'C:\SOFT\YeMan\PowerControl\pawnio\YeManTdpCtl.exe'

if (-not (Test-Path $SUSP_DIR)) { exit 0 }
$files = @(Get-ChildItem -Path "$SUSP_DIR\*.txt" -ErrorAction SilentlyContinue)
if ($files.Count -eq 0) { exit 0 }

# P/Invoke ntdll!NtResumeProcess + kernel32 OpenProcess/CloseHandle.
# (Add-Type may fail on exotic setups; wrapped so recovery degrades gracefully.)
$ntType = $null
try {
    $ntType = Add-Type -MemberDefinition @'
    [DllImport("ntdll.dll")] public static extern int NtResumeProcess(IntPtr ProcessHandle);
    [DllImport("kernel32.dll")] public static extern IntPtr OpenProcess(uint dwDesiredAccess, bool bInheritHandle, int dwProcessId);
    [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr hObject);
'@ -Name 'SleepGuardNt' -Namespace 'YM' -PassThru
} catch { $ntType = $null }

function Get-ProcName($pid) {
    try { (Get-CimInstance Win32_Process -Filter "ProcessId = $pid" -ErrorAction SilentlyContinue).Name } catch { $null }
}

function Restore-Tdp {
    if (-not (Test-Path $TDP_FILE)) { return }
    $raw = (Get-Content $TDP_FILE -Raw -ErrorAction SilentlyContinue).Trim()
    if (-not $raw) { return }
    $w = 0
    if (-not [int]::TryParse($raw, [ref]$w) -or $w -le 0) { return }
    if (-not (Test-Path $TDP_EXE)) { return }
    $cpu = (Get-CimInstance Win32_Processor -ErrorAction SilentlyContinue | Select-Object -First 1).Name
    $vendor = ''
    if ($cpu -match 'AMD') { $vendor = 'amd' } elseif ($cpu -match 'Intel') { $vendor = 'intel' }
    $arg = "set $w"
    if ($vendor) { $arg += " --vendor $vendor" }
    try { & $TDP_EXE @($arg -split ' ') } catch { }
}

foreach ($f in $files) {
    $fn = $f.BaseName
    if ($fn -notmatch '^(\d+)$') { Remove-Item $f.FullName -Force; continue }
    $pid = [int]$Matches[1]
    $content = Get-Content $f.FullName -Raw -Encoding UTF8 -ErrorAction SilentlyContinue
    $storedName = ''
    if ($content -match 'name=([^|]+)') { $storedName = $Matches[1] }

    $proc = Get-Process -Id $pid -ErrorAction SilentlyContinue
    if (-not $proc) { Remove-Item $f.FullName -Force; continue }   # process gone -> clear marker

    # PID reuse guard: only act if the live image name still matches the marker.
    $liveName = Get-ProcName $pid
    if ($liveName -and $storedName -and ($liveName -ne $storedName)) {
        Remove-Item $f.FullName -Force; continue                  # PID reused by a different exe -> clear
    }

    if ($Mode -eq 'recover') {
        if ($ntType) {
            # PROCESS_SUSPEND_RESUME (0x800) | PROCESS_QUERY_LIMITED_INFORMATION (0x1000)
            $h = $ntType::OpenProcess(0x800 -bor 0x1000, $false, $pid)
            if ($h -and $h -ne [IntPtr]::Zero) {
                $ntType::NtResumeProcess($h)
                $ntType::CloseHandle($h)
            }
        }
        Remove-Item $f.FullName -Force
    } elseif ($Mode -eq 'end') {
        try { Stop-Process -Id $pid -Force } catch { }
        Remove-Item $f.FullName -Force
    }
}

# On recover, also undo the 12W TDP lock left behind by the (absent) app.
if ($Mode -eq 'recover') { Restore-Tdp }

exit 0
