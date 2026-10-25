param([Parameter(Mandatory=$true)][string]$OutputPath)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$hcLogSources=@(
    @{Class='HandheldCompanion.Devices.XboxROGAllyX';Api='winrt';Archive='G:\YeManCC-Work\日志\ROG-Xbox-2025\YeManCC-logs-20261016-101103-ROGXbox2025.zip'},
    @{Class='HandheldCompanion.Devices.GPDWin5';Api='winrt';Archive='G:\YeManCC-Work\日志\Win5\YeManCC-logs-20261016-101717-Win5.zip'},
    @{Class='HandheldCompanion.Devices.ClawCG3EM';Api='sensorapi';Archive='G:\YeManCC-Work\日志\微星G3E\YeManCC-logs-20261016-184736-G3E-手柄+睡眠正常.zip'}
)
$hcLogResult=@()
foreach($hcLogSource in $hcLogSources){
    $hcLogFileHasher=[Security.Cryptography.SHA256]::Create();$hcLogFileStream=[IO.File]::OpenRead($hcLogSource.Archive)
    try{$hcLogArchiveHash=([BitConverter]::ToString($hcLogFileHasher.ComputeHash($hcLogFileStream))).Replace('-','').ToLowerInvariant()}finally{$hcLogFileStream.Dispose();$hcLogFileHasher.Dispose()}
    $hcLogZip=[IO.Compression.ZipFile]::OpenRead($hcLogSource.Archive)
    $hcLogSamples=@()
    try{
        foreach($hcLogEntry in @($hcLogZip.Entries | Where-Object {$_.FullName -match 'input-capture\.jsonl(?:\.prev)?$'} | Sort-Object FullName)){
            $hcLogReader=[IO.StreamReader]::new($hcLogEntry.Open(),[Text.Encoding]::UTF8)
            try{
                $hcLogLineNumber=0
                while(-not $hcLogReader.EndOfStream -and $hcLogSamples.Count -lt 2){
                    $hcLogLine=$hcLogReader.ReadLine();$hcLogLineNumber++
                    $hcLogJsonStart=$hcLogLine.IndexOf('{');if($hcLogJsonStart -lt 0){continue}
                    try{$hcLogFrame=$hcLogLine.Substring($hcLogJsonStart) | ConvertFrom-Json}catch{continue}
                    if(-not $hcLogFrame.imu.raw -or $hcLogFrame.imu.gyro -ne $true -or $hcLogFrame.imu.accel -ne $true){continue}
                    $hcLogRaw=@();$hcLogValid=$true
                    foreach($hcLogAxis in @('gx','gy','gz','ax','ay','az')){
                        $hcLogValue=$hcLogFrame.imu.raw.$hcLogAxis
                        if($null -eq $hcLogValue -or $hcLogValue -is [string] -or [double]::IsNaN([double]$hcLogValue) -or [double]::IsInfinity([double]$hcLogValue)){$hcLogValid=$false;break}
                        $hcLogRaw += [double]$hcLogValue
                    }
                    if(-not $hcLogValid){continue}
                    $hcLogSha=[Security.Cryptography.SHA256]::Create()
                    try{$hcLogLineHash=([BitConverter]::ToString($hcLogSha.ComputeHash([Text.Encoding]::UTF8.GetBytes($hcLogLine)))).Replace('-','').ToLowerInvariant()}finally{$hcLogSha.Dispose()}
                    $hcLogSamples += [pscustomobject]@{raw=$hcLogRaw;entry=$hcLogEntry.FullName;lineNumber=$hcLogLineNumber;lineContentSha256=$hcLogLineHash;originalTick=$hcLogFrame.t}
                }
            }finally{$hcLogReader.Dispose()}
            if($hcLogSamples.Count -ge 2){break}
        }
    }finally{$hcLogZip.Dispose()}
    if($hcLogSamples.Count -ne 2){throw ('Two complete raw samples not found: '+$hcLogSource.Archive)}
    $hcLogResult += [pscustomobject]@{deviceClass=$hcLogSource.Class;archive=$hcLogSource.Archive;sha256=$hcLogArchiveHash;originalApi=$hcLogSource.Api;replayScope='raw numeric stream through declared private WinRT recipe; does not establish API-coordinate equivalence or physical axes';samples=$hcLogSamples}
}
[IO.File]::WriteAllText([IO.Path]::GetFullPath($OutputPath),($hcLogResult | ConvertTo-Json -Depth 7),[Text.UTF8Encoding]::new($false))
