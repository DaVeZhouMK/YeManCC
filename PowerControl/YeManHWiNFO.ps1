param(
    [ValidateSet('start', 'restart')]
    [string]$Mode = 'start',
    [switch]$Elevated,
    # 0 retains a valid existing period, otherwise the managed template's period.
    [ValidateSet(0, 1000, 1500, 2000, 3000)]
    [int]$SensorIntervalMs = 0,
    # Optional path for an unregistered portable HWiNFO64 installation.
    [string]$ExecutablePath = ''
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
    if ($ExecutablePath) {
        # Never reinterpret an explicit path as a command line or fall back to a
        # different installation. The same checked value is forwarded on UAC.
        if ($ExecutablePath.IndexOfAny([char[]]@([char]0, [char]10, [char]13, [char]34)) -ge 0 -or
            -not [IO.Path]::IsPathRooted($ExecutablePath) -or
            [IO.Path]::GetFileName($ExecutablePath) -ine 'HWiNFO64.exe' -or
            -not (Test-Path -LiteralPath $ExecutablePath -PathType Leaf)) {
            Fail 2 'The explicit HWiNFO64.exe path is invalid or missing'
        }
        return (Resolve-Path -LiteralPath $ExecutablePath).Path
    }
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

function Read-HWiNFOIni([string]$Path) {
    $stream = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
    try {
        if ($stream.Length -gt 262144) { Fail 8 'HWiNFO INI exceeds the 256 KiB configuration limit' }
        $bytes = New-Object byte[] ([int]$stream.Length)
        $offset = 0
        while ($offset -lt $bytes.Length) {
            $read = $stream.Read($bytes, $offset, $bytes.Length - $offset)
            if ($read -eq 0) { Fail 8 'HWiNFO INI changed while it was read' }
            $offset += $read
        }
        if ($stream.ReadByte() -ne -1) { Fail 8 'HWiNFO INI grew while it was read' }
    } finally { $stream.Dispose() }
    $skip = 0
    $encoding = [Text.Encoding]::GetEncoding([Text.Encoding]::Default.CodePage, [Text.EncoderFallback]::ExceptionFallback, [Text.DecoderFallback]::ExceptionFallback)
    if ($bytes.Length -ge 3 -and $bytes[0] -eq 239 -and $bytes[1] -eq 187 -and $bytes[2] -eq 191) {
        $skip = 3; $encoding = New-Object Text.UTF8Encoding($false, $true)
    } elseif ($bytes.Length -ge 2 -and $bytes[0] -eq 255 -and $bytes[1] -eq 254) {
        $skip = 2; $encoding = New-Object Text.UnicodeEncoding($false, $false, $true)
    } elseif ($bytes.Length -ge 2 -and $bytes[0] -eq 254 -and $bytes[1] -eq 255) {
        $skip = 2; $encoding = New-Object Text.UnicodeEncoding($true, $false, $true)
    }
    $text = $encoding.GetString($bytes, $skip, $bytes.Length - $skip)
    if ($text.IndexOf([char]0) -ge 0) { Fail 8 'HWiNFO INI contains an embedded zero' }
    $lines = @([regex]::Split($text, '\r\n|\n|\r'))
    if ($lines.Count -gt 8192) { Fail 8 'HWiNFO INI exceeds the line limit' }
    return [pscustomobject]@{ Bytes = $bytes; Text = $text; Lines = $lines; Encoding = $encoding; PreambleLength = $skip }
}

function Get-HWiNFOIniEntries($Ini) {
    $entries = New-Object Collections.ArrayList
    $seen = @{}; $sections = @{}; $section = ''
    for ($i = 0; $i -lt $Ini.Lines.Count; $i++) {
        $line = $Ini.Lines[$i]
        if ($line -match '^\s*\[([^\]]+)\]\s*(?:[;#].*)?$') {
            $section = $Matches[1].Trim()
            if ($sections.ContainsKey($section)) { Fail 8 'HWiNFO INI contains a repeated section' }
            $sections[$section] = $true
            continue
        }
        if ($line -match '^\s*([^;#=][^=]*)=(.*)$') {
            if (-not $section) { Fail 8 'HWiNFO INI contains a value outside a section' }
            $key = $Matches[1].Trim(); $value = $Matches[2]
            $identity = $section + [char]31 + $key
            if ($seen.ContainsKey($identity)) { Fail 8 'HWiNFO INI contains a duplicate section/key pair' }
            $seen[$identity] = $true
            [void]$entries.Add([pscustomobject]@{ Section = $section; Key = $key; Value = $value; Index = $i; Identity = $identity })
        }
    }
    return ,$entries
}

function Get-HWiNFOAcquisitionOverrides {
    # Same authoritative document as the whole YMCC application. Read only for
    # configuration preparation/recheck, never on the monitoring refresh path.
    $path = Join-Path $PSScriptRoot 'yeman-settings.json'
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { return @{ PresentMon = 1; NvClockMeasuring = 0; USBC_PD = 0 } }
    $stream = $null
    try {
        $stream = [IO.File]::Open($path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
        if ($stream.Length -gt 16777216) { Fail 8 'Unified YMCC settings exceed the 16 MiB read limit' }
        $bytes = New-Object byte[] ([int]$stream.Length)
        $offset = 0
        while ($offset -lt $bytes.Length) {
            $read = $stream.Read($bytes, $offset, $bytes.Length - $offset)
            if ($read -eq 0) { Fail 8 'Unified YMCC settings changed while being read' }
            $offset += $read
        }
        if ($stream.ReadByte() -ne -1) { Fail 8 'Unified YMCC settings grew while being read' }
        $encoding = New-Object Text.UTF8Encoding($false, $true)
        $text = $encoding.GetString($bytes).TrimStart([char]0xfeff)
        $document = ConvertFrom-Json -InputObject $text -ErrorAction Stop
        if ($null -eq $document -or $document -is [array] -or $document.GetType() -ne [System.Management.Automation.PSCustomObject]) { Fail 8 'Unified YMCC settings must be an object' }
        $policy = $document.rtss.monitorAcquisition
        if ($null -eq $policy) { return @{ PresentMon = 1; NvClockMeasuring = 0; USBC_PD = 0 } }
        if ($policy -is [array] -or $policy.GetType() -ne [System.Management.Automation.PSCustomObject] -or ($policy.version -isnot [int] -and $policy.version -isnot [long]) -or $policy.version -ne 1 -or
            $policy.enabled -isnot [bool]) { Fail 8 'Unsupported HWiNFO acquisition settings' }
        if (-not $policy.enabled) { return @{ PresentMon = 1; NvClockMeasuring = 0; USBC_PD = 0 } }
        if ($policy.snapshotPolling -isnot [bool]) {
            Fail 8 'HWiNFO acquisition choices must be booleans'
        }
        # FPS is mandatory: even a legacy disabled choice cannot turn it off.
        return @{ PresentMon = 1; NvClockMeasuring = 0; USBC_PD = 0; SnapshotPolling = [int]$policy.snapshotPolling }
    } catch {
        if ($_.Exception.Data.Contains('HWiNFOExitCode')) { throw }
        Fail 8 ('HWiNFO acquisition configuration could not be read: ' + $_.Exception.Message)
    } finally { if ($stream) { $stream.Dispose() } }
}

function Get-HWiNFOManagedIni {
    # Baseline supplied by the user: YeMan/HWiNFO64.INI (2026-10-16).
    # Historical Average is unused by our consumers. Use the default incremental
    # average, preserving current values and Presented FPS/Low/frame-time metrics.
    # Keep background/no-dialog settings in code so recovery does not depend
    # on a mutable external template. This enables sharing; it does not alter
    # HWiNFO licensing or guarantee an unlimited shared-memory lifetime.
    $text = @'
[Settings]
SensorsOnly=1
NegativeTempsOK=1
MinimalizeSensors=1
MinimalizeSensorsClose=1
AutoUpdate=0
ShowFans0RPM=1
MinimalizeMainWnd=1
ShowWelcomeAndProgress=0
PreferAMDADL=1
Theme=1
SensorsFontFace=Segoe UI
SensorsFontHeight=17
SensorsSM=1
WakeGPUs=0
AC97CodecID=0
iMEsupport=0
SVID_VR=0
HighestIdeAddress=-1
CSMI_SAS_Support=0
SWSMI=0
UseHPET=0
BusClkPolling=0
LPC=0
EC=0
SMART=0
ATAStats=0
SupportCorsairAsetek=0
DebugDirect=0
SMBus=0
GPUI2C=0
EcScanRate=5000
SmartScanRate=5000
SensorInterval=3000
SensorAveragingInterval=0
'@
    $text = ([regex]::Replace($text, '\r\n|\n|\r', "`r`n")) + "`r`n"
    $choices = Get-HWiNFOAcquisitionOverrides
    # Extra NVIDIA measured clocks are not consumed by YMCC or its RTSS
    # templates. Disable that producer work, retain the ordinary GPU clock path.
    # USB-C port telemetry is unused by all current consumers. The battery
    # and Windows power-source paths remain independent of this port observer.
    foreach ($key in @('PresentMon', 'NvClockMeasuring', 'USBC_PD', 'SnapshotPolling')) {
        if ($choices.ContainsKey($key)) { $text += $key + '=' + $choices[$key] + "`r`n" }
    }
    $encoding = New-Object Text.UTF8Encoding($false, $true)
    $bytes = $encoding.GetBytes($text)
    return [pscustomobject]@{ Bytes = $bytes; Text = $text; Lines = @([regex]::Split($text, '\r\n|\n|\r')); Encoding = $encoding; PreambleLength = 0 }
}

function Get-HWiNFOConfigPlan([string]$Source, [string]$Destination, [int]$RequestedIntervalMs = 0) {
    if ($RequestedIntervalMs -notin @(0, 1000, 1500, 2000, 3000)) { Fail 8 'Unsupported HWiNFO polling interval' }
    $sourceIni = if ($Source) { Read-HWiNFOIni $Source } else { Get-HWiNFOManagedIni }
    $sourceEntries = Get-HWiNFOIniEntries $sourceIni
    $destinationIni = $sourceIni
    if (Test-Path -LiteralPath $Destination -PathType Leaf) { $destinationIni = Read-HWiNFOIni $Destination }
    $destinationEntries = Get-HWiNFOIniEntries $destinationIni
    $lines = New-Object 'Collections.Generic.List[string]'
    foreach ($line in $destinationIni.Lines) { $lines.Add($line) }
    $positions = @{}; $existingValues = @{}
    foreach ($entry in $destinationEntries) { $positions[$entry.Identity] = $entry.Index; $existingValues[$entry.Identity] = $entry.Value }
    # The managed template keeps authority over its keys, except an existing
    # valid period. Other user sections/values and the encoding are retained.
    $periodSeen = $false
    $missing = New-Object Collections.ArrayList
    foreach ($entry in $sourceEntries) {
        $value = $entry.Value
        if ($entry.Section -ieq 'Settings' -and $entry.Key -ieq 'SensorInterval') {
            $periodSeen = $true
            if ($RequestedIntervalMs -gt 0) { $value = [string]$RequestedIntervalMs }
            elseif ($positions.ContainsKey($entry.Identity)) {
                $period = 0
                if ([int]::TryParse($existingValues[$entry.Identity].Trim(), [ref]$period) -and $period -ge 1000 -and $period -le 3000) { $value = $existingValues[$entry.Identity] }
            }
        }
        if ($entry.Section -ieq 'Settings' -and $entry.Key -ieq 'SensorInterval') {
            $effectivePeriod = 0
            if (-not [int]::TryParse($value.Trim(), [ref]$effectivePeriod) -or $effectivePeriod -lt 1000 -or $effectivePeriod -gt 3000) { Fail 8 'HWiNFO period is outside the 1-3 second monitor freshness budget' }
        }
        $replacement = $entry.Key + '=' + $value
        if ($positions.ContainsKey($entry.Identity)) { $lines[$positions[$entry.Identity]] = $replacement }
        else { [void]$missing.Add([pscustomobject]@{ Section = $entry.Section; Line = $replacement }) }
    }
    if (-not $periodSeen -and $RequestedIntervalMs -eq 0) {
        $identity = 'Settings' + [char]31 + 'SensorInterval'
        if ($existingValues.ContainsKey($identity)) {
            $effectivePeriod = 0
            if (-not [int]::TryParse($existingValues[$identity].Trim(), [ref]$effectivePeriod) -or $effectivePeriod -lt 1000 -or $effectivePeriod -gt 3000) { Fail 8 'Unmanaged HWiNFO period exceeds the monitor freshness budget' }
        }
    }
    if ($RequestedIntervalMs -gt 0 -and -not $periodSeen) {
        $identity = 'Settings' + [char]31 + 'SensorInterval'
        if ($positions.ContainsKey($identity)) { $lines[$positions[$identity]] = 'SensorInterval=' + $RequestedIntervalMs }
        else { [void]$missing.Add([pscustomobject]@{ Section = 'Settings'; Line = 'SensorInterval=' + $RequestedIntervalMs }) }
    }
    # Index once so merge work is linear in the bounded lines/entries.
    $missingSections = @{}; $missingOrder = New-Object Collections.ArrayList
    foreach ($entry in $missing) {
        if (-not $missingSections.ContainsKey($entry.Section)) {
            $missingSections[$entry.Section] = New-Object 'Collections.Generic.List[string]'
            [void]$missingOrder.Add($entry.Section)
        }
        $missingSections[$entry.Section].Add($entry.Line)
    }
    $merged = New-Object 'Collections.Generic.List[string]'
    $finished = @{}; $currentSection = ''
    foreach ($line in $lines) {
        if ($line -match '^\s*\[([^\]]+)\]\s*(?:[;#].*)?$') {
            if ($currentSection) {
                if ($missingSections.ContainsKey($currentSection)) { foreach ($pending in $missingSections[$currentSection]) { $merged.Add($pending) } }
                $finished[$currentSection] = $true
            }
            $currentSection = $Matches[1].Trim()
        }
        $merged.Add($line)
    }
    if ($currentSection) {
        if ($missingSections.ContainsKey($currentSection)) { foreach ($pending in $missingSections[$currentSection]) { $merged.Add($pending) } }
        $finished[$currentSection] = $true
    }
    foreach ($section in $missingOrder) {
        if (-not $finished.ContainsKey($section)) {
            $merged.Add('[' + $section + ']')
            foreach ($pending in $missingSections[$section]) { $merged.Add($pending) }
        }
    }
    if ($merged.Count -gt 8192) { Fail 8 'Merged HWiNFO INI exceeds the line limit' }
    $cr = [string][char]13; $lf = [string][char]10
    $newline = if ($destinationIni.Text.Contains($cr + $lf)) { $cr + $lf } elseif ($destinationIni.Text.Contains($cr)) { $cr } else { $lf }
    $text = [string]::Join($newline, $merged)
    $body = $destinationIni.Encoding.GetBytes($text)
    $bytes = New-Object byte[] ($destinationIni.PreambleLength + $body.Length)
    if ($destinationIni.PreambleLength) { [Array]::Copy($destinationIni.Bytes, 0, $bytes, 0, $destinationIni.PreambleLength) }
    [Array]::Copy($body, 0, $bytes, $destinationIni.PreambleLength, $body.Length)
    if ($bytes.Length -gt 262144) { Fail 8 'Merged HWiNFO INI exceeds the configuration limit' }
    return [pscustomobject]@{ Bytes = $bytes; RequestedIntervalMs = $RequestedIntervalMs }
}

function Write-HWiNFOBackup([string]$Path, [byte[]]$Bytes, [switch]$KeepExisting) {
    if ($KeepExisting -and (Test-Path -LiteralPath $Path -PathType Leaf)) { return }
    $temporary = $Path + '.tmp'
    try {
        [IO.File]::WriteAllBytes($temporary, $Bytes)
        if ($KeepExisting -or -not (Test-Path -LiteralPath $Path -PathType Leaf)) { [IO.File]::Move($temporary, $Path) }
        else { [IO.File]::Replace($temporary, $Path, [NullString]::Value) }
    } finally { if (Test-Path -LiteralPath $temporary -PathType Leaf) { [IO.File]::Delete($temporary) } }
}

function Copy-HWiNFOConfig([string]$Source, [string]$Destination) {
    if ($Source -and -not (Test-Path -LiteralPath $Source -PathType Leaf)) { Fail 3 'The explicit HWiNFO template was not found' }
    if (@(Get-Process -Name HWiNFO64 -ErrorAction SilentlyContinue).Count -gt 0) { Fail 4 'HWiNFO64.exe is running; refusing to overwrite HWiNFO64.INI' }
    $plan = Get-HWiNFOConfigPlan -Source $Source -Destination $Destination -RequestedIntervalMs $SensorIntervalMs
    $stage = $Destination + '.ymcc-stage'
    $originalBackup = $Destination + '.ymcc-original.bak'
    $lastBackup = $Destination + '.ymcc-last.bak'
    $previous = $null
    try {
        [IO.File]::WriteAllBytes($stage, $plan.Bytes)
        $expectedHash = Get-HWiNFOConfigHash $stage
        if (Test-Path -LiteralPath $Destination -PathType Leaf) {
            $previous = (Read-HWiNFOIni $Destination).Bytes
            Write-HWiNFOBackup -Path $originalBackup -Bytes $previous -KeepExisting
            Write-HWiNFOBackup -Path $lastBackup -Bytes $previous
        }
        for ($attempt = 0; $attempt -lt 5; $attempt++) {
            if (@(Get-Process -Name HWiNFO64 -ErrorAction SilentlyContinue).Count -gt 0) { Fail 4 'HWiNFO64.exe appeared; refusing to overwrite its INI' }
            try {
                Copy-Item -LiteralPath $stage -Destination $Destination -Force -ErrorAction Stop
                if ((Get-HWiNFOConfigHash $Destination) -ne $expectedHash) { throw 'HWiNFO64.INI verification failed after copying' }
                return
            } catch {
                if (Test-HWiNFOAccessDenied $_) { Fail 5 ("HWiNFO64.INI copy needs administrator permission: {0}" -f $_.Exception.Message) -NeedsElevation }
                if ($attempt -eq 4) {
                    if (@(Get-Process -Name HWiNFO64 -ErrorAction SilentlyContinue).Count -eq 0) {
                        if ($null -ne $previous) { [IO.File]::WriteAllBytes($Destination, $previous) }
                        elseif (Test-Path -LiteralPath $Destination -PathType Leaf) { [IO.File]::Delete($Destination) }
                    }
                    Fail 5 ("HWiNFO64.INI copy/verification failed: {0}" -f $_.Exception.Message)
                }
                Start-Sleep -Milliseconds 200
            }
        }
    } catch {
        if ($_.Exception.Data.Contains('HWiNFOExitCode')) { throw }
        if (Test-HWiNFOAccessDenied $_) { Fail 5 'HWiNFO configuration preparation needs administrator permission' -NeedsElevation }
        throw
    } finally { if (Test-Path -LiteralPath $stage -PathType Leaf) { [IO.File]::Delete($stage) } }
}

function Test-HWiNFOSensorBudgetImage([string]$Executable) {
    # Registry layout and disk/C-state/duplicate-RTSS group gates are verified
    # for this upstream image. Unknown builds keep the ordinary acquisition.
    $info = [Diagnostics.FileVersionInfo]::GetVersionInfo($Executable)
    if ($info.FileMajorPart -ne 8 -or $info.FileMinorPart -ne 54) { return $false }
    $file = Get-Item -LiteralPath $Executable -ErrorAction Stop
    if ($file.Length -le 0 -or $file.Length -gt 67108864) { return $false }
    $hash = (Get-HWiNFOConfigHash $Executable).Replace('-', '').ToLowerInvariant()
    return $hash -ceq 'e4ce0c85c4288fa7e8eb12ebeece99efff4d81e0b13de53f091daabc81a3c9d3'
}

function Assert-HWiNFOSensorBudgetGroup([string]$Group) {
    # Never accept a registry path, sensor-table index or a display label here.
    if ($Group -cnotmatch '^(?:F000(?:0101|0302)_(?:[0-9]|[12][0-9]|3[01])|F00F5000_0)$') { throw 'Unsupported unused-sensor group identity' }
}

function Read-HWiNFOSensorBudgetState([string]$Group) {
    Assert-HWiNFOSensorBudgetGroup $Group
    $baseKey = $null; $key = $null
    try {
        $baseKey = [Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::CurrentUser, [Microsoft.Win32.RegistryView]::Registry64)
        $key = $baseKey.OpenSubKey(('Software\HWiNFO64\Sensors\' + $Group), $false)
        if ($null -eq $key) { return [pscustomobject]@{ Exists = $false; Value = 0 } }
        $missing = [object]::new()
        $value = $key.GetValue('Enabled', $missing, [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
        if ([object]::ReferenceEquals($value, $missing)) { return [pscustomobject]@{ Exists = $false; Value = 0 } }
        if ($key.GetValueKind('Enabled') -ne [Microsoft.Win32.RegistryValueKind]::DWord) { throw 'Unexpected sensor Enabled registry type' }
        return [pscustomobject]@{ Exists = $true; Value = [int]$value }
    } finally { if ($key) { $key.Dispose() }; if ($baseKey) { $baseKey.Dispose() } }
}

function Write-HWiNFOSensorBudgetState([string]$Group, $State) {
    Assert-HWiNFOSensorBudgetGroup $Group
    $baseKey = $null; $key = $null
    try {
        $baseKey = [Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::CurrentUser, [Microsoft.Win32.RegistryView]::Registry64)
        $path = 'Software\HWiNFO64\Sensors\' + $Group
        if ($State.Exists) {
            $key = $baseKey.CreateSubKey($path)
            $key.SetValue('Enabled', [int]$State.Value, [Microsoft.Win32.RegistryValueKind]::DWord)
        } else {
            $key = $baseKey.OpenSubKey($path, $true)
            if ($key) { $key.DeleteValue('Enabled', $false) }
            # Do not remove keys: an empty new key is harmless, and another
            # application may concurrently add labels/layout/child keys.
        }
    } finally { if ($key) { $key.Dispose() }; if ($baseKey) { $baseKey.Dispose() } }
}

function Test-HWiNFOSensorBudgetState($Left, $Right) {
    return $Left.Exists -eq $Right.Exists -and (-not $Left.Exists -or $Left.Value -eq $Right.Value)
}

function Invoke-HWiNFOSensorBudget([string]$Executable, [string]$Destination) {
    $changed = New-Object Collections.ArrayList
    $before = @{}; $disabled = [pscustomobject]@{ Exists = $true; Value = 0 }
    try {
        if (-not (Test-HWiNFOSensorBudgetImage $Executable)) { return 'unsupported-image' }
        foreach ($entry in (Get-HWiNFOIniEntries (Read-HWiNFOIni $Destination))) {
            if ($entry.Section -ieq 'Settings' -and $entry.Key -ieq 'RememberPreferences') {
                $remember = 0
                if (-not [int]::TryParse($entry.Value.Trim(), [ref]$remember) -or $remember -eq 0) { return 'preferences-disabled' }
            }
        }
        # Fixed unused groups only: disk output uses Windows FileSystem, while
        # C-state residency has no current YMCC/RTSS consumer. This skips its
        # per-reading projection, not the shared CPU hardware acquisition.
        # HWiNFO's single RTSS mirror repeats YMCC's existing RTSS reader.
        # PresentMon (F0000FF0_0) and YMCC's rescue/curve reader stay enabled.
        $groups = @('F0000101', 'F0000302' | ForEach-Object {
            $family = $_; 0..31 | ForEach-Object { $family + '_' + $_ }
        }) + @('F00F5000_0')
        foreach ($group in $groups) { $before[$group] = Read-HWiNFOSensorBudgetState $group }
        $pending = @($groups | Where-Object { -not (Test-HWiNFOSensorBudgetState $before[$_] $disabled) })
        if ($pending.Count -eq 0) { return 'already-configured' }
        $saved = [ordered]@{ schema = 1; executableSha256 = 'e4ce0c85c4288fa7e8eb12ebeece99efff4d81e0b13de53f091daabc81a3c9d3'; values = $before }
        $encoding = New-Object Text.UTF8Encoding($false, $true)
        $bytes = $encoding.GetBytes(($saved | ConvertTo-Json -Depth 5 -Compress))
        if ($bytes.Length -gt 16384) { throw 'Unused sensor backup exceeds its fixed budget' }
        # Preserve the original disk-only receipt format across policy upgrades.
        $diskBefore = @{}
        foreach ($group in $groups) { if ($group -clike 'F0000101_*') { $diskBefore[$group] = $before[$group] } }
        $diskSaved = [ordered]@{ schema = 1; executableSha256 = $saved.executableSha256; values = $diskBefore }
        $diskBytes = $encoding.GetBytes(($diskSaved | ConvertTo-Json -Depth 5 -Compress))
        if ($diskBytes.Length -gt 16384) { throw 'Disk original backup exceeds its fixed budget' }
        Write-HWiNFOBackup -Path ($Destination + '.ymcc-sensor-groups-original.json') -Bytes $diskBytes -KeepExisting
        Write-HWiNFOBackup -Path ($Destination + '.ymcc-sensor-groups-last.json') -Bytes $bytes
        if (@($pending | Where-Object { $_ -clike 'F0000302_*' }).Count -gt 0) {
            # A previous disk-only original backup must stay byte-for-byte intact.
            # Keep the newly managed family's first state in its own receipt.
            $cpuBefore = @{}
            foreach ($group in $groups) { if ($group -clike 'F0000302_*') { $cpuBefore[$group] = $before[$group] } }
            $cpuSaved = [ordered]@{ schema = 1; executableSha256 = $saved.executableSha256; values = $cpuBefore }
            $cpuBytes = $encoding.GetBytes(($cpuSaved | ConvertTo-Json -Depth 5 -Compress))
            if ($cpuBytes.Length -gt 16384) { throw 'C-state original backup exceeds its fixed budget' }
            Write-HWiNFOBackup -Path ($Destination + '.ymcc-sensor-groups-cstate-original.json') -Bytes $cpuBytes -KeepExisting
        }
        if ($pending -ccontains 'F00F5000_0') {
            # Preserve the mirror's first state independently of older receipts.
            $rtssBefore = @{ F00F5000_0 = $before['F00F5000_0'] }
            $rtssSaved = [ordered]@{ schema = 1; executableSha256 = $saved.executableSha256; values = $rtssBefore }
            $rtssBytes = $encoding.GetBytes(($rtssSaved | ConvertTo-Json -Depth 5 -Compress))
            if ($rtssBytes.Length -gt 16384) { throw 'RTSS mirror original backup exceeds its fixed budget' }
            Write-HWiNFOBackup -Path ($Destination + '.ymcc-sensor-groups-rtss-original.json') -Bytes $rtssBytes -KeepExisting
        }
        foreach ($group in $pending) {
            if (@(Get-Process -Name HWiNFO64 -ErrorAction SilentlyContinue).Count -gt 0) { throw 'Producer appeared during sensor configuration' }
            if (-not (Test-HWiNFOSensorBudgetState (Read-HWiNFOSensorBudgetState $group) $before[$group])) { throw 'Sensor preference changed during configuration' }
            [void]$changed.Add($group)
            Write-HWiNFOSensorBudgetState $group $disabled
            if (-not (Test-HWiNFOSensorBudgetState (Read-HWiNFOSensorBudgetState $group) $disabled)) { throw 'Sensor Enabled verification failed' }
        }
        return 'configured'
    } catch {
        $reason = $_.Exception.Message; $rollbackOk = $true
        for ($i = $changed.Count - 1; $i -ge 0; $i--) {
            $group = $changed[$i]
            try {
                if (@(Get-Process -Name HWiNFO64 -ErrorAction SilentlyContinue).Count -gt 0) { throw 'Producer is running; sensor rollback deferred' }
                $current = Read-HWiNFOSensorBudgetState $group
                if (Test-HWiNFOSensorBudgetState $current $before[$group]) { continue }
                if (-not (Test-HWiNFOSensorBudgetState $current $disabled)) { throw 'Concurrent sensor preference must be preserved' }
                Write-HWiNFOSensorBudgetState $group $before[$group]
                if (-not (Test-HWiNFOSensorBudgetState (Read-HWiNFOSensorBudgetState $group) $before[$group])) { throw 'Sensor rollback verification failed' }
            } catch { $rollbackOk = $false }
        }
        # This optional optimization cannot stop required data/FPS recovery.
        # Original/last preference receipts permit investigation or restoration.
        [Console]::Error.WriteLine(('HWiNFO unused sensor groups were not confirmed; rollback={0}: {1}' -f $rollbackOk, $reason))
        return 'unconfirmed'
    }
}

function Get-HWiNFOClockMs {
    return [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()
}

function Test-HWiNFOSharedMemory {
    param([int]$ExpectedIntervalMs = 0, [ref]$ProducerProof)
    $strictProof = $null -ne $ProducerProof
    if ($strictProof) { $ProducerProof.Value = $null }
    $mmf = $null
    $accessor = $null
    try {
        foreach ($name in @('Global\HWiNFO_SENS_SM2', 'HWiNFO_SENS_SM2', 'Global\HWiNFO_SENS_SM', 'HWiNFO_SENS_SM')) {
            try {
                # Health reads never require write access to the producer.
                $mmf = [System.IO.MemoryMappedFiles.MemoryMappedFile]::OpenExisting(
                    $name, [System.IO.MemoryMappedFiles.MemoryMappedFileRights]::Read)
                if ($mmf) { break }
            } catch { $mmf = $null }
        }
        if (-not $mmf) { return $false }

        $accessor = $mmf.CreateViewAccessor(0, 0, [System.IO.MemoryMappedFiles.MemoryMappedFileAccess]::Read)
        if ($accessor.Capacity -lt 44) { return $false }
        if ($accessor.ReadUInt32(0) -ne 0x53695748) { return $false }
        $version = $accessor.ReadUInt32(4)
        $revision = $accessor.ReadUInt32(8)
        $sensorOffset = [uint64]$accessor.ReadUInt32(20)
        $sensorSize = $accessor.ReadUInt32(24)
        $sensorCount = $accessor.ReadUInt32(28)
        $offset = [uint64]$accessor.ReadUInt32(32)
        $size = [uint64]$accessor.ReadUInt32(36)
        $count = [uint64]$accessor.ReadUInt32(40)
        # Match the existing sole native parser's bounded layout admission.
        # Readability alone cannot confirm a source the consumer must reject.
        if ($size -lt 316 -or $size -gt 16384 -or $count -eq 0 -or $count -gt 100000 -or
            $sensorOffset -lt 44 -or $sensorOffset -gt [uint64]$accessor.Capacity) { return $false }
        $readingBytes = $size * $count
        if ($readingBytes -gt 33554432L) { return $false }
        $end = $offset + $readingBytes
        if ($offset -ge [uint64]$accessor.Capacity -or $end -gt [uint64]$accessor.Capacity) { return $false }

        $periodKnown = $version -eq 2 -and $sensorOffset -ge 48 -and $accessor.Capacity -ge 48
        $period = 0
        if ($periodKnown) { $period = $accessor.ReadUInt32(44) }
        if ($ExpectedIntervalMs -ne 0) {
            # Same verified v2 extension gate as the existing native reader.
            # An unknown layout cannot prove an explicitly requested interval.
            if ($ExpectedIntervalMs -notin @(1000, 1500, 2000, 3000)) { return $false }
            if (-not $periodKnown -or $sensorOffset -gt $offset -or $period -ne $ExpectedIntervalMs) { return $false }
        }
        if ($ExpectedIntervalMs -ne 0 -or $strictProof) {
            $pollTime = $accessor.ReadInt64(12)
            if ($pollTime -le 0 -or $pollTime -gt 9223372036854775) { return $false }
        }

        # Read the existing region; do not infer health from non-zero sensor values.
        [void]$accessor.ReadByte([long]$offset)
        if ($ExpectedIntervalMs -ne 0 -or $strictProof) {
            # A receipt owns one coherent header and one producer identity key.
            if ($accessor.ReadUInt32(0) -ne 0x53695748 -or $accessor.ReadUInt32(4) -ne $version -or
                $accessor.ReadUInt32(8) -ne $revision -or $accessor.ReadUInt32(20) -ne $sensorOffset -or
                $accessor.ReadUInt32(24) -ne $sensorSize -or $accessor.ReadUInt32(28) -ne $sensorCount -or
                $accessor.ReadUInt32(32) -ne $offset -or $accessor.ReadUInt32(36) -ne $size -or
                $accessor.ReadUInt32(40) -ne $count -or $accessor.ReadInt64(12) -ne $pollTime -or
                ($periodKnown -and $accessor.ReadUInt32(44) -ne $period)) { return $false }
        }
        if ($strictProof) {
            # Match the native producer-age budget, using the same existing header.
            $budget = 5000L
            if ($periodKnown -and $period -gt 0) { $budget = [Math]::Max(5000L, [Math]::Min(60000L, [Math]::Max(500L, [long]$period)) + 3000L) }
            $age = (Get-HWiNFOClockMs) - $pollTime * 1000L
            if ($age -lt -1000L -or $age -ge $budget) { return $false }
            $key = @($name, $version, $revision, $sensorOffset, $sensorSize, $sensorCount, $offset, $size, $count, $periodKnown, $period) -join '|'
            $ProducerProof.Value = [pscustomobject]@{ PollTime = $pollTime; Layout = $key }
        }
        return $true
    } catch {
        return $false
    } finally {
        if ($accessor) { try { $accessor.Dispose() } catch { } }
        if ($mmf) { try { $mmf.Dispose() } catch { } }
    }
}

function Test-HWiNFOHealth {
    param([int]$ExpectedIntervalMs = 0)
    $proof = $null
    return (@(Get-Process -Name HWiNFO64 -ErrorAction SilentlyContinue).Count -gt 0) -and (Test-HWiNFOSharedMemory -ExpectedIntervalMs $ExpectedIntervalMs -ProducerProof ([ref]$proof))
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
            if ($SensorIntervalMs -gt 0) { return 12 } # Deferred: live settings were not changed.
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
        $dst = Join-Path $root 'HWiNFO64.INI'
        # Preflight before stopping, then re-merge after exit because HWiNFO may
        # save settings while it closes. The source is YMCC's embedded baseline.
        [void](Get-HWiNFOConfigPlan -Destination $dst -RequestedIntervalMs $SensorIntervalMs)

        Stop-HWiNFO
        Copy-HWiNFOConfig -Destination $dst
        [void](Invoke-HWiNFOSensorBudget -Executable $exe -Destination $dst)
        if (@(Get-Process -Name HWiNFO64 -ErrorAction SilentlyContinue).Count -gt 0) {
            Fail 4 'HWiNFO64.exe appeared during recovery; refusing to launch another instance'
        }
        # The prepared configuration and its byte-for-byte SHA-256 verification are complete before
        # launch. Preserve the original no-switch HWiNFO invocation.
        $process = Start-Process -FilePath $exe -WorkingDirectory $root -WindowStyle Hidden -PassThru
        try { $process.ProcessorAffinity = [IntPtr]0xA0 } catch { }
        $previousProof = $null
        $sawProof = $false
        # Reuse the process handle returned by Start-Process. Polling the global
        # process list at 4 Hz is neither necessary nor an exact lifetime proof.
        for ($i = 0; $i -lt 30; $i++) {
            try { $exited = $process.HasExited }
            catch { Fail 6 'HWiNFO64.exe process lifetime could not be confirmed after launch' }
            if ($exited) { Fail 6 'HWiNFO64.exe exited before shared memory became readable' }
            $proof = $null
            if (Test-HWiNFOSharedMemory -ExpectedIntervalMs $SensorIntervalMs -ProducerProof ([ref]$proof)) {
                $sawProof = $true
                if ($null -ne $previousProof -and $proof.Layout -ceq $previousProof.Layout -and $proof.PollTime -gt $previousProof.PollTime) { return 0 }
                $previousProof = $proof
            } else { $previousProof = $null }
            Start-Sleep -Milliseconds 1000
        }
        if ($sawProof) { Fail 14 'HWiNFO64.exe started but continuing fresh producer updates were not confirmed within 30 seconds' }
        if ($SensorIntervalMs -gt 0) { Fail 13 'HWiNFO64.exe started but shared memory and the requested polling period were not both confirmed within 30 seconds' }
        Fail 7 'HWiNFO64.exe started but fresh shared memory was not confirmed within 30 seconds'
    } finally {
        if ($owned -and $mutex) { try { $mutex.ReleaseMutex() } catch { } }
        if ($mutex) { $mutex.Dispose() }
    }
}

function Wait-HWiNFORecoveryChild($Child) {
    if ($null -eq $Child) { Fail 15 'The elevated HWiNFO recovery process was not returned' }
    try {
        # Bind the exact returned process handle before waiting. Do not wait on
        # a process tree: HWiNFO is a deliberately long-lived descendant.
        $handle = $Child.Handle
        if ($handle -eq [IntPtr]::Zero) { Fail 15 'The elevated HWiNFO recovery process handle is unavailable' }
        if (-not $Child.WaitForExit(60000)) { Fail 15 'The elevated HWiNFO recovery process did not finish within 60 seconds; it was not terminated' }
        $code = $Child.ExitCode
        if ($null -eq $code -or ($code -isnot [int] -and $code -isnot [long])) {
            Fail 15 'The elevated HWiNFO recovery process exit code could not be confirmed'
        }
        return [int]$code
    } catch {
        if ($_.Exception.Data['HWiNFOExitCode'] -eq 15) { throw }
        Fail 15 ("The elevated HWiNFO recovery process result could not be read: {0}" -f $_.Exception.Message)
    } finally { $Child.Dispose() }
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
                if ($SensorIntervalMs -gt 0) { $arguments += ' -SensorIntervalMs ' + $SensorIntervalMs }
                if ($ExecutablePath) { $arguments += ' -ExecutablePath "{0}"' -f $ExecutablePath }
                $admin = Start-Process -FilePath 'powershell.exe' -Verb RunAs -ArgumentList $arguments -WorkingDirectory $PSScriptRoot -WindowStyle Hidden -PassThru
                return (Wait-HWiNFORecoveryChild $admin)
            } catch {
                if ($_.Exception.Data['HWiNFOExitCode'] -eq 15) {
                    [Console]::Error.WriteLine(("HWiNFO elevated recovery was not confirmed: {0}" -f $_.Exception.Message))
                    return 15
                }
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
