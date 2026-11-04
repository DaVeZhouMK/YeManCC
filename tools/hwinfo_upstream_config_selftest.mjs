// Existing recovery functions + existing OS-boundary mocks, with upstream
// configuration fixtures only. Never launches/stops a real HWiNFO or touches HKCU.
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import {spawnSync} from 'node:child_process';import crypto from 'node:crypto';
const root=process.cwd().replaceAll('\\','/'),task=path.resolve(process.env.YMCC_HWINFO_UPSTREAM_OUT||'../../../Build/Tasks/YMCC-Steam-HUD-Probe/hwinfo-upstream-control-20261016').replaceAll('\\','/');fs.mkdirSync(task,{recursive:true});
const base=fs.readFileSync('tools/hwinfo_recovery_selftest.ps1','utf8').replaceAll('\r\n','\n'),start=base.indexOf("try {\n    Test-Case 'healthy start"),last=base.lastIndexOf('} finally {\n    # One shell');assert(start>0&&last>start);let prefix=base.slice(0,start),cleanup=base.slice(last);
prefix=prefix.replace('function Reset-Test([bool]', 'function Reset-TestBase([bool]');
prefix=prefix.replace("'-File \"[^\"]+\" -Mode restart -Elevated$'",()=>"'-File \"[^\"]+\" -Mode restart -Elevated(?: -SensorIntervalMs \\d+)?$'");
// The base harness verifies the exact post-copy hash before launch.
prefix=prefix.replace("        $oldMode = $script:Mode",()=>"        $oldInterval = $script:SensorIntervalMs\n        $script:SensorIntervalMs = 0\n        if ($ArgumentList -match '-SensorIntervalMs (\\d+)$') { $script:SensorIntervalMs = [int]$Matches[1] }\n        $script:state.ElevationArguments = $ArgumentList\n        $oldMode = $script:Mode").replace('$script:Mode = $oldMode }', '$script:Mode = $oldMode; $script:SensorIntervalMs = $oldInterval }');
prefix=prefix.replace("    Write-Output ('PASS ' + $name)",()=>"    [void]$script:names.Add($name)\n    Write-Output ('PASS ' + $name)");
const bt=String.fromCharCode(96);
const tests=String.raw`
$script:names = New-Object Collections.ArrayList
# Inject variable source fixtures into the actual merge/transaction functions.
function Get-HWiNFOManagedIni { return Read-HWiNFOIni $script:srcPath }
function Reset-Test([bool]$running = $true, [bool]$healthy = $false) {
    Reset-TestBase $running $healthy
    $script:SensorIntervalMs = 0
    foreach ($suffix in @('.ymcc-stage','.ymcc-original.bak','.ymcc-last.bak','.ymcc-original.bak.tmp','.ymcc-last.bak.tmp')) {
        $file = $script:dstPath + $suffix
        if ([IO.File]::Exists($file)) { [IO.File]::Delete($file) }
    }
}
function Config([string]$source, [string]$destination) {
    [IO.File]::WriteAllText($script:srcPath, $source)
    [IO.File]::WriteAllText($script:dstPath, $destination)
}
function ConfigValues([string]$path) {
    $values = @{}
    foreach ($e in (Get-HWiNFOIniEntries (Read-HWiNFOIni $path))) { $values[$e.Section + '/' + $e.Key] = $e.Value }
    return $values
}
function Assert-RejectedBeforeStop([int]$code = 8) {
    $before = & $script:ProductionHash $script:dstPath
    Assert ((Invoke-HWiNFORecoveryEntry) -eq $code) 'Invalid plan did not fail explicitly'
    Assert (-not $state.Events.Contains('stop') -and $state.CopyCount -eq 0 -and $state.LaunchCount -eq 0) 'Invalid plan mutated process or configuration'
    Assert ((& $script:ProductionHash $script:dstPath) -eq $before) 'Rejected plan changed destination'
}

function New-RootAcquisitionFixture {
    $script:acqRoot=Join-Path $script:installRoot 'PowerControl'
    [IO.Directory]::CreateDirectory($script:acqRoot)|Out-Null
    $script:acqPath=Join-Path $script:acqRoot 'yeman-settings.json'
    $script:acqLibrary=Join-Path $script:acqRoot 'Acquisition Functions.ps1'
    $only=@('Get-HWiNFOAcquisitionOverrides','Initialize-HWiNFORootAcquisitionJson','Get-HWiNFOManagedIni')
    $source=@($ast.FindAll({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -in $only},$false)|ForEach-Object{$_.Extent.Text}) -join "${bt}r${bt}n"
    [IO.File]::WriteAllText($script:acqLibrary,$source,(New-Object Text.UTF8Encoding($true)))
    $script:acqUtf8=New-Object Text.UTF8Encoding($false,$true)
}
function Write-RootAcquisition([string]$Text) {[IO.File]::WriteAllText($script:acqPath,$Text,$script:acqUtf8)}
function Assert-RootAcquisitionDefaults($Value) {
    Assert ($Value -is [Collections.IDictionary] -and $Value.PresentMon -ceq 1 -and $Value.NvClockMeasuring -ceq 0 -and $Value.USBC_PD -ceq 0 -and !$Value.ContainsKey('SnapshotPolling')) 'Bad root did not yield consumable product defaults'
}
function Assert-RootAcquisitionRead([string]$ExpectedState='default:*') {
    $before=[IO.File]::ReadAllBytes($script:acqPath);$status=$null
    $actual=Get-HWiNFOAcquisitionOverrides ([ref]$status)
    Assert-RootAcquisitionDefaults $actual
    Assert ($status -like $ExpectedState) ('Wrong root read classification: '+$status)
    Assert ([Convert]::ToBase64String([IO.File]::ReadAllBytes($script:acqPath)) -ceq [Convert]::ToBase64String($before)) 'Root read changed exact original bytes'
    Assert ($state.SensorWrites -eq 0 -and $state.CopyCount -eq 0 -and $state.LaunchCount -eq 0 -and $state.Events.Count -eq 0) 'Root read authorized registry/INI/process side effects'
}
try {
    Test-Case 'root-acquisition-invalid-defaults-and-complete-preparation' {
        New-RootAcquisitionFixture;. $script:acqLibrary
        foreach($text in @('','{','[]','null','42','{"rtss":42}','{"rtss":{"monitorAcquisition":[]}}','{"rtss":{"monitorAcquisition":{"version":1,"enabled":"true","snapshotPolling":true}}}','{"rtss":{"monitorAcquisition":{"version":1,"enabled":true,"snapshotPolling":0}}}','{"n":NaN}','{"n":Infinity}','{"n":1e999}','{"n":01}','{"n":+1}','{"n":1,}','{/*comment*/"n":1}','{n:1}','{"u":"\ud800"}')) {
            Write-RootAcquisition $text;Assert-RootAcquisitionRead
            $managed=Get-HWiNFOManagedIni
            Assert ($managed.Text -match '(?m)^PresentMon=1' -and $managed.Text -notmatch '(?m)^SnapshotPolling=') 'Bad root blocked or altered actual managed preparation'
        }
    }
    Test-Case 'root-acquisition-duplicates-rejected-without-overwrite' {
        New-RootAcquisitionFixture;. $script:acqLibrary
        foreach($text in @('{"rtss":{"monitorAcquisition":{"version":1,"enabled":false,"enabled":true,"snapshotPolling":true}}}','{"schemaVersion":2,"schemaVersion":1,"rtss":{"monitorAcquisition":{"version":1,"enabled":true,"snapshotPolling":true}}}','{"u":1,"\u0075":2,"rtss":{"monitorAcquisition":{"version":1,"enabled":true,"snapshotPolling":true}}}')) {Write-RootAcquisition $text;Assert-RootAcquisitionRead 'default:corrupt'}
    }
    Test-Case 'root-acquisition-legal-preferences-unknown-fields-and-root-location' {
        New-RootAcquisitionFixture;. $script:acqLibrary
        $decoy=Join-Path $script:installRoot 'yeman-settings.json';[IO.File]::WriteAllText($decoy,'{"rtss":{"monitorAcquisition":{"version":1,"enabled":true,"snapshotPolling":false}}}');$decoyHash=& $script:ProductionHash $decoy
        foreach($setting in @('true','false')) {
            $text='{"schemaVersion":1,"configurationBaseline":{"id":"ymcc-settings","revision":1,"legacyImported":true,"unknown":"keep"},"unknown":{"A":1,"a":2,"unicode":"中文"},"rtss":{"other":"keep","monitorAcquisition":{"version":1,"enabled":true,"snapshotPolling":'+$setting+',"unknown":[1,2]}}}'
            Write-RootAcquisition $text;$before=& $script:ProductionHash $script:acqPath;$status=$null;$value=Get-HWiNFOAcquisitionOverrides ([ref]$status)
            Assert ($status -eq 'configured' -and $value.SnapshotPolling -ceq [int]($setting -eq 'true') -and $value.PresentMon -ceq 1) 'Legal preference not consumed from actual PowerControl root'
            Assert ((& $script:ProductionHash $script:acqPath) -eq $before -and (& $script:ProductionHash $decoy) -eq $decoyHash) 'Read normalized or overwrote root/unknown fields'
            $managed=Get-HWiNFOManagedIni;Assert ($managed.Text -match ('(?m)^SnapshotPolling='+$value.SnapshotPolling)) 'Actual managed template did not consume preference'
        }
        foreach($text in @('{}','{"schemaVersion":0}','{"rtss":null}','{"rtss":{"monitorAcquisition":null}}','{"rtss":{"monitorAcquisition":{"version":1,"enabled":false}}}')) {Write-RootAcquisition $text;Assert-RootAcquisitionRead}
    }
    Test-Case 'root-acquisition-future-baseline-schema-and-policy-preserve-originals' {
        New-RootAcquisitionFixture;. $script:acqLibrary
        foreach($stamp in @('"schemaVersion":2','"schemaVersion":"1"','"schemaVersion":null','"configurationBaseline":{"id":"future-settings","revision":1,"legacyImported":true}','"configurationBaseline":{"id":"ymcc-settings","revision":2,"legacyImported":true}','"configurationBaseline":{"id":"ymcc-settings","revision":1,"legacyImported":"true"}')) {
            Write-RootAcquisition ('{'+$stamp+',"rtss":{"monitorAcquisition":{"version":1,"enabled":true,"snapshotPolling":true}}}');Assert-RootAcquisitionRead
        }
        Write-RootAcquisition '{"rtss":{"monitorAcquisition":{"version":2,"enabled":true,"snapshotPolling":true}}}';Assert-RootAcquisitionRead 'default:future'
    }
    Test-Case 'root-acquisition-UTF8-BOM-encoding-and-depth-budgets' {
        New-RootAcquisitionFixture;. $script:acqLibrary
        $good='{"rtss":{"monitorAcquisition":{"version":1,"enabled":true,"snapshotPolling":true}}}'
        [IO.File]::WriteAllText($script:acqPath,$good,(New-Object Text.UTF8Encoding($true,$true)));$status=$null;$value=Get-HWiNFOAcquisitionOverrides ([ref]$status);Assert ($status -eq 'configured' -and $value.SnapshotPolling -ceq 1) 'Valid UTF8 BOM rejected'
        foreach($bytes in @([byte[]]@(0xc3,0x28),[byte[]]@(0xff,0xfe,0x7b,0x00,0x7d,0x00),[byte[]]@(0xef,0xbb,0xbf,0xef,0xbb,0xbf,0x7b,0x7d))) {[IO.File]::WriteAllBytes($script:acqPath,$bytes);Assert-RootAcquisitionRead}
        $nested='0';for($i=0;$i -lt 64;$i++){$nested='['+$nested+']'};Write-RootAcquisition ('{"unknown":'+$nested+',"rtss":{"monitorAcquisition":{"version":1,"enabled":true,"snapshotPolling":true}}}');$status=$null;$value=Get-HWiNFOAcquisitionOverrides ([ref]$status);Assert ($value.SnapshotPolling -ceq 1) 'Legal depth64 discarded'
        Write-RootAcquisition ('{"unknown":['+$nested+'],"rtss":{"monitorAcquisition":{"version":1,"enabled":true,"snapshotPolling":true}}}');Assert-RootAcquisitionRead 'default:corrupt'
    }
    Test-Case 'root-acquisition-size-boundary-and-explicit-retry' {
        New-RootAcquisitionFixture;. $script:acqLibrary
        $good='{"rtss":{"monitorAcquisition":{"version":1,"enabled":true,"snapshotPolling":true}}}'
        Write-RootAcquisition ($good+(' '*((4*1024*1024)-$good.Length)));$status=$null;$value=Get-HWiNFOAcquisitionOverrides ([ref]$status);Assert ($value.SnapshotPolling -ceq 1) 'Legal4MiB boundary rejected'
        Write-RootAcquisition ($good+(' '*((4*1024*1024)+1-$good.Length)));Assert-RootAcquisitionRead 'default:oversize'
        $large=[IO.File]::Open($script:acqPath,'Create','ReadWrite','None');try{$large.SetLength(16777217)}finally{$large.Dispose()};Assert-RootAcquisitionRead 'default:oversize'
        Write-RootAcquisition $good;$status=$null;$value=Get-HWiNFOAcquisitionOverrides ([ref]$status);Assert ($status -eq 'configured' -and $value.SnapshotPolling -ceq 1) 'Explicit read failed after size fault cleared'
    }
    Test-Case 'root-acquisition-missing-is-distinct-from-IO-and-retry-is-event-owned' {
        New-RootAcquisitionFixture;. $script:acqLibrary
        $status=$null;Assert-RootAcquisitionDefaults (Get-HWiNFOAcquisitionOverrides ([ref]$status));Assert ($status -eq 'default:missing') 'Missing root not separately classified'
        [IO.Directory]::CreateDirectory($script:acqPath)|Out-Null;try{$status=$null;Assert-RootAcquisitionDefaults (Get-HWiNFOAcquisitionOverrides ([ref]$status));Assert ($status -eq 'default:read-fault') 'Directory was treated as missing'}finally{[IO.Directory]::Delete($script:acqPath)}
        Assert ([IO.Path]::GetFullPath($script:acqRoot).StartsWith([IO.Path]::GetFullPath($script:tempRoot)+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) 'Not an owned fixture directory'
        $moved=$script:acqRoot+' moved';[IO.Directory]::Move($script:acqRoot,$moved);[IO.File]::WriteAllText($script:acqRoot,'not a directory')
        try{$status=$null;Assert-RootAcquisitionDefaults (Get-HWiNFOAcquisitionOverrides ([ref]$status));Assert ($status -eq 'default:read-fault') 'Non-directory parent was treated as missing'}finally{[IO.File]::Delete($script:acqRoot);[IO.Directory]::Move($moved,$script:acqRoot)}
        $good='{"rtss":{"monitorAcquisition":{"version":1,"enabled":true,"snapshotPolling":false}}}';Write-RootAcquisition $good
        $hold=[IO.File]::Open($script:acqPath,'Open','ReadWrite','None');try{$watch=[Diagnostics.Stopwatch]::StartNew();$status=$null;Assert-RootAcquisitionDefaults (Get-HWiNFOAcquisitionOverrides ([ref]$status));Assert ($status -eq 'default:read-fault' -and $watch.ElapsedMilliseconds -lt 3000) 'Locked root blocked or became missing'}finally{$hold.Dispose()}
        $status=$null;$value=Get-HWiNFOAcquisitionOverrides ([ref]$status);Assert ($status -eq 'configured' -and $value.SnapshotPolling -ceq 0) 'Unlocked original did not recover on explicit read'
    }
    Test-Case 'root-acquisition-explicit-storm-recovers-with-no-write-authority' {
        New-RootAcquisitionFixture;. $script:acqLibrary
        Write-RootAcquisition '{';for($i=0;$i -lt 12;$i++){Assert-RootAcquisitionRead 'default:corrupt'}
        Write-RootAcquisition '{"rtss":{"monitorAcquisition":{"version":1,"enabled":true,"snapshotPolling":true}}}';$status=$null;$value=Get-HWiNFOAcquisitionOverrides ([ref]$status);Assert ($status -eq 'configured' -and $value.SnapshotPolling -ceq 1) 'Explicit retry after storm stayed faulted'
        Assert ($state.SensorWrites -eq 0 -and $state.CopyCount -eq 0 -and $state.LaunchCount -eq 0 -and $state.Events.Count -eq 0) 'Read/storm started registry/INI/hardware work'
    }
    Test-Case 'explicit interval is written and verified before the existing launch' {
        Config "[Settings]${bt}r${bt}nSensorsSM=1${bt}r${bt}nSensorInterval=3000${bt}r${bt}n" "[Settings]${bt}r${bt}nSensorsSM=0${bt}r${bt}nSensorInterval=3000${bt}r${bt}nCustomSetting=keep${bt}r${bt}n[User]${bt}r${bt}nValue=custom${bt}r${bt}n"
        $script:SensorIntervalMs = 1000
        Assert ((Invoke-HWiNFORecoveryEntry) -eq 0) 'Requested upstream period failed'
        $v = ConfigValues $dstPath
        Assert ($v['Settings/SensorInterval'] -eq '1000' -and $v['Settings/SensorsSM'] -eq '1') 'Controlled settings were not applied'
        Assert ($v['Settings/CustomSetting'] -eq 'keep' -and $v['User/Value'] -eq 'custom') 'Unknown user settings were lost'
        Assert-Order @('stop','exit-confirmed','copy','verify','launch')
        Assert (-not [IO.File]::Exists($dstPath + '.ymcc-stage')) 'Stage survived success'
    }
    Test-Case 'healthy start reports a deferred explicit request without any write or restart' {
        $script:SensorIntervalMs = 1000; $script:Mode = 'start'; $state.Healthy = $true
        $before = & $script:ProductionHash $dstPath
        Assert ((Invoke-HWiNFORecoveryEntry) -eq 12) 'Deferred live request was reported applied'
        Assert (-not $state.Events.Contains('stop') -and $state.CopyCount -eq 0 -and $state.LaunchCount -eq 0) 'Healthy request restarted the process'
        Assert ((& $script:ProductionHash $dstPath) -eq $before) 'Healthy request edited live INI'
    }
    Test-Case 'existing valid upstream period survives a recovery with no requested override' {
        Config "[Settings]${bt}r${bt}nSensorsSM=1${bt}r${bt}nSensorInterval=3000${bt}r${bt}n" "[Settings]${bt}r${bt}nSensorsSM=0${bt}r${bt}nSensorInterval=2500${bt}r${bt}n"
        Assert ((Invoke-HWiNFORecoveryEntry) -eq 0) 'Default recovery failed'
        Assert ((ConfigValues $dstPath)['Settings/SensorInterval'] -eq '2500') 'Existing valid period was reset'
    }
    Test-Case 'out of policy existing period falls back to the managed template' {
        Config "[Settings]${bt}r${bt}nSensorInterval=3000${bt}r${bt}n" "[Settings]${bt}r${bt}nSensorInterval=100${bt}r${bt}n"
        Assert ((Invoke-HWiNFORecoveryEntry) -eq 0) 'Fallback failed'
        Assert ((ConfigValues $dstPath)['Settings/SensorInterval'] -eq '3000') 'Unbounded fast period was preserved'
    }
    Test-Case 'all supported explicit intervals reach the prepared configuration' {
        foreach ($period in @(1000,1500,2000,3000)) {
            Config "[Settings]${bt}r${bt}nSensorInterval=3000${bt}r${bt}n" "[Settings]${bt}r${bt}nCustom=keep${bt}r${bt}n"
            $plan = Get-HWiNFOConfigPlan -Source $srcPath -Destination $dstPath -RequestedIntervalMs $period
            [IO.File]::WriteAllBytes((Join-Path $tempRoot 'plan.ini'), $plan.Bytes)
            Assert ((ConfigValues (Join-Path $tempRoot 'plan.ini'))['Settings/SensorInterval'] -eq [string]$period) 'Period was not prepared'
        }
    }
    Test-Case 'a requested period can be inserted into a template that has none' {
        $script:SensorIntervalMs = 1500
        Assert ((Invoke-HWiNFORecoveryEntry) -eq 0) 'Interval insertion failed'
        Assert ((ConfigValues $dstPath)['Settings/SensorInterval'] -eq '1500') 'Inserted key is missing'
        Assert (@((Read-HWiNFOIni $dstPath).Lines | Where-Object { $_ -eq '[Settings]' }).Count -eq 1) 'Duplicate Settings header added'
    }
    Test-Case 'a cold missing destination uses the managed template and one interval override' {
        $state.Running.Clear(); [IO.File]::Delete($dstPath); $script:SensorIntervalMs = 2000
        Assert ((Invoke-HWiNFORecoveryEntry) -eq 0) 'Missing destination failed'
        Assert ((ConfigValues $dstPath)['Settings/SensorInterval'] -eq '2000') 'Cold override missing'
        Assert (-not [IO.File]::Exists($dstPath + '.ymcc-original.bak')) 'Invented backup for a nonexistent original'
    }
    Test-Case 'new keys join existing sections without duplicate headers' {
        Config "[Settings]${bt}r${bt}nSensorsSM=1${bt}r${bt}nSensorInterval=3000${bt}r${bt}n[Extra]${bt}r${bt}nManaged=1${bt}r${bt}n" "[Settings]${bt}r${bt}nUser=keep${bt}r${bt}n[Extra]${bt}r${bt}nUnknown=preserved${bt}r${bt}n"
        Assert ((Invoke-HWiNFORecoveryEntry) -eq 0) 'Section merge failed'
        $v = ConfigValues $dstPath
        Assert ($v['Settings/SensorsSM'] -eq '1' -and $v['Settings/User'] -eq 'keep' -and $v['Extra/Managed'] -eq '1' -and $v['Extra/Unknown'] -eq 'preserved') 'Section merge lost entries'
        Assert (@((Read-HWiNFOIni $dstPath).Lines | Where-Object { $_ -eq '[Extra]' }).Count -eq 1) 'Duplicate section added'
    }
    Test-Case 'original backup remains original and last backup advances once per transaction' {
        Config "[Settings]${bt}r${bt}nSensorInterval=3000${bt}r${bt}n" "[Settings]${bt}r${bt}nSensorInterval=2000${bt}r${bt}nUnknown=first${bt}r${bt}n"
        $original = & $script:ProductionHash $dstPath; $script:SensorIntervalMs = 1000
        Assert ((Invoke-HWiNFORecoveryEntry) -eq 0) 'First transaction failed'
        $firstApplied = & $script:ProductionHash $dstPath; $script:SensorIntervalMs = 2000
        Assert ((Invoke-HWiNFORecoveryEntry) -eq 0) 'Second transaction failed'
        Assert ((& $script:ProductionHash ($dstPath + '.ymcc-original.bak')) -eq $original) 'Original backup was overwritten'
        Assert ((& $script:ProductionHash ($dstPath + '.ymcc-last.bak')) -eq $firstApplied) 'Last backup did not advance'
        Assert (-not [IO.File]::Exists($dstPath + '.ymcc-last.bak.tmp')) 'Backup temp survived'
    }
    Test-Case 'permanent verification failure restores the exact prior bytes and never launches' {
        $state.CorruptCopies = 5; $before = & $script:ProductionHash $dstPath
        Assert ((Invoke-HWiNFORecoveryEntry) -eq 5) 'Corruption was not a failure'
        Assert ((& $script:ProductionHash $dstPath) -eq $before) 'Prior configuration was not restored'
        Assert ($state.CopyCount -eq 5 -and $state.LaunchCount -eq 0) 'Unbounded retries or launch on failure'
        Assert (-not [IO.File]::Exists($dstPath + '.ymcc-stage')) 'Failure left stage file'
    }
    Test-Case 'failed cold copy removes its partial destination instead of using an older backup' {
        $state.Running.Clear(); [IO.File]::Delete($dstPath); $state.CorruptCopies = 5
        [IO.File]::WriteAllText(($dstPath + '.ymcc-last.bak'), "[Settings]${bt}r${bt}nUnrelated=old${bt}r${bt}n")
        Assert ((Invoke-HWiNFORecoveryEntry) -eq 5) 'Expected failed cold copy'
        Assert (-not [IO.File]::Exists($dstPath) -and $state.LaunchCount -eq 0) 'Old backup was misapplied'
    }
    Test-Case 'requested interval survives the full elevated retry' {
        $script:SensorIntervalMs = 1000; $state.CopyDenied = $true
        Assert ((Invoke-HWiNFORecoveryEntry) -eq 0) 'Elevated retry failed'
        Assert ($state.ElevationArguments -match '-SensorIntervalMs 1000$') 'Request lost in UAC handoff'
        Assert ((ConfigValues $dstPath)['Settings/SensorInterval'] -eq '1000') 'Elevated plan lost requested interval'
        Assert-Order @('unlock','elevate','copy','verify','launch')
    }
    Test-Case 'duplicate key rejection occurs before stopping an existing process' {
        Config "[Settings]${bt}r${bt}nSensorInterval=3000${bt}r${bt}n" "[Settings]${bt}r${bt}nSensorInterval=1000${bt}r${bt}nsensorinterval=2000${bt}r${bt}n"
        Assert-RejectedBeforeStop
    }
    Test-Case 'duplicate section rejection occurs before stopping an existing process' {
        Config "[Settings]${bt}r${bt}nSensorsSM=1${bt}r${bt}n" "[Settings]${bt}r${bt}nA=1${bt}r${bt}n[settings]${bt}r${bt}nB=2${bt}r${bt}n"
        Assert-RejectedBeforeStop
    }
    Test-Case 'oversized source is rejected before stopping or reading it into the plan' {
        [IO.File]::WriteAllText($srcPath, ('x' * 262145))
        Assert-RejectedBeforeStop
    }
    Test-Case 'oversized destination is rejected before stopping' {
        [IO.File]::WriteAllText($dstPath, ('x' * 262145))
        Assert-RejectedBeforeStop
    }
    Test-Case 'line limit rejection does not stop an existing process' {
        [IO.File]::WriteAllText($srcPath, (";line${bt}n" * 8193))
        Assert-RejectedBeforeStop
    }
    Test-Case 'embedded zero rejection does not stop an existing process' {
        [IO.File]::WriteAllText($srcPath, ('[Settings]' + [char]0))
        Assert-RejectedBeforeStop
    }
    Test-Case 'UTF16 destination retains its encoding and unknown Unicode values' {
        $custom = [string][char]0x4E2D + [char]0x6587
        [IO.File]::WriteAllText($dstPath, ("[Settings]${bt}r${bt}nSharedMemorySupport=0${bt}r${bt}n[User]${bt}r${bt}nName=" + $custom + "${bt}r${bt}n"), [Text.Encoding]::Unicode)
        Assert ((Invoke-HWiNFORecoveryEntry) -eq 0) 'Unicode merge failed'
        $bytes = [IO.File]::ReadAllBytes($dstPath)
        Assert ($bytes[0] -eq 255 -and $bytes[1] -eq 254 -and (ConfigValues $dstPath)['User/Name'] -eq $custom) 'Unicode codec/value changed'
    }
    Test-Case 'UTF8 BOM and LF remain readable after merging' {
        [IO.File]::WriteAllText($dstPath, "[Settings]${bt}nSharedMemorySupport=0${bt}n[User]${bt}nUnknown=yes${bt}n", (New-Object Text.UTF8Encoding($true)))
        Assert ((Invoke-HWiNFORecoveryEntry) -eq 0) 'UTF8 merge failed'
        $bytes = [IO.File]::ReadAllBytes($dstPath)
        Assert ($bytes[0] -eq 239 -and $bytes[1] -eq 187 -and $bytes[2] -eq 191) 'UTF8 BOM lost'
        Assert (-not (Read-HWiNFOIni $dstPath).Text.Contains([string][char]13)) 'LF was changed'
    }
    Test-Case 'merged output budget is checked before stopping' {
        Config ("[Settings]${bt}r${bt}nA=" + ('a' * 140000)) ("[Settings]${bt}r${bt}nB=" + ('b' * 140000))
        Assert-RejectedBeforeStop
    }
    Test-Case 'merged line budget is rejected before it can create an unreadable next configuration' {
        $src = New-Object Text.StringBuilder; $dst = New-Object Text.StringBuilder
        [void]$src.AppendLine('[Settings]'); [void]$dst.AppendLine('[Settings]')
        for ($i=0; $i -lt 4200; $i++) { [void]$src.AppendLine(('Managed'+$i+'=1')); [void]$dst.AppendLine(('User'+$i+'=1')) }
        Config $src.ToString() $dst.ToString()
        Assert-RejectedBeforeStop
    }
    Test-Case 'unsupported requested period is rejected before stopping' {
        $script:SensorIntervalMs = 50
        Assert-RejectedBeforeStop
    }
    Test-Case 'periods beyond the existing HUD freshness budget are rejected before stop' {
        foreach ($period in @(5000,10000)) { $script:SensorIntervalMs = $period; Assert-RejectedBeforeStop }
    }
    Test-Case 'an oversized unmanaged period is not silently retained' {
        Config "[Settings]${bt}r${bt}nSharedMemorySupport=1${bt}r${bt}n" "[Settings]${bt}r${bt}nSensorInterval=10000${bt}r${bt}n"
        Assert-RejectedBeforeStop
    }
    Test-Case 'a large section set merges once without duplicate sections' {
        $src = New-Object Text.StringBuilder; $dst = New-Object Text.StringBuilder
        for ($i = 0; $i -lt 500; $i++) { [void]$src.AppendLine(('[S' + $i + ']')); [void]$src.AppendLine('Managed=1'); [void]$dst.AppendLine(('[S' + $i + ']')); [void]$dst.AppendLine('User=keep') }
        Config $src.ToString() $dst.ToString()
        Assert ((Invoke-HWiNFORecoveryEntry) -eq 0) 'Large bounded merge failed'
        Assert ((ConfigValues $dstPath).Count -eq 1000) 'Large merge lost or duplicated keys'
    }

    foreach($kind in @('original','last','cstate-original','rtss-original')){foreach($defect in @('truncated','future')){
        Test-Case ('storm-json-'+$kind+'-'+$defect) {
            Enable-TestSensorBudget;$file=$dstPath+'.ymcc-sensor-groups-'+$kind+'.json';$doc=New-TestSensorReceipt $kind
            $text=$doc|ConvertTo-Json -Depth 12 -Compress;if($defect -eq 'truncated'){$text='{'}else{$doc.schema=2;$text=$doc|ConvertTo-Json -Depth 12 -Compress}
            [IO.File]::WriteAllText($file,$text);$before=& $script:ProductionHash $file;$status=Invoke-HWiNFOSensorBudget $exePath $dstPath
            Assert ($status -clike 'unconfirmed*') 'unknown receipt was accepted';Assert ($state.SensorWrites -eq 0) 'unknown receipt authorized registry changes';Assert ((& $script:ProductionHash $file) -eq $before) 'unknown receipt original overwritten'
        }
    }}
    foreach($defect in @('wrong-root','schema-string','duplicate','exists-string','value-string','image','foreign-group','missing-group','future-phase','foreign-owner','foreign-ini')){
        Test-Case ('storm-json-last-'+$defect) {
            Enable-TestSensorBudget;$file=$dstPath+'.ymcc-sensor-groups-last.json';$doc=New-TestSensorReceipt;$text=$null
            switch($defect){
                'wrong-root'{$text='[]'} 'schema-string'{$doc.schema='1'} 'duplicate'{$text='{"schema":2,'+($doc|ConvertTo-Json -Depth 12 -Compress).Substring(1)}
                'exists-string'{$doc.values['F0000101_0'].Exists='false'} 'value-string'{$doc.values['F0000101_0'].Value='1'} 'image'{$doc.executableSha256='0'*64}
                'foreign-group'{$doc.values['foreign']=[pscustomobject]@{Exists=$true;Value=1}} 'missing-group'{$doc.values.Remove('F0000101_0')}
                'future-phase'{$doc|Add-Member NoteProperty phase 'future'} 'foreign-owner'{$doc|Add-Member NoteProperty owner 'foreign'} 'foreign-ini'{$doc|Add-Member NoteProperty iniPath (Join-Path $installRoot 'foreign.INI')}
            }
            if($null -eq $text){$text=$doc|ConvertTo-Json -Depth 12 -Compress};[IO.File]::WriteAllText($file,$text);$before=& $script:ProductionHash $file
            Assert ((Invoke-HWiNFOSensorBudget $exePath $dstPath) -clike 'unconfirmed*') 'malformed receipt was accepted';Assert ($state.SensorWrites -eq 0 -and (& $script:ProductionHash $file) -eq $before) 'malformed original changed or supplied defaults'
        }
    }
    Test-Case 'storm-json-valid-legacy-annotations-survive' {
        Enable-TestSensorBudget;$file=$dstPath+'.ymcc-sensor-groups-last.json';$doc=New-TestSensorReceipt;$doc|Add-Member NoteProperty annotation ([pscustomobject]@{keep=$true});$doc.values['F0000101_0']|Add-Member NoteProperty note 'keep';[IO.File]::WriteAllText($file,($doc|ConvertTo-Json -Depth 12 -Compress))
        Assert ((Invoke-HWiNFOSensorBudget $exePath $dstPath) -eq 'configured') 'valid legacy rejected';$saved=Get-Content -LiteralPath $file -Raw|ConvertFrom-Json;Assert ($saved.annotation.keep -and $saved.values.F0000101_0.note -ceq 'keep') 'valid annotations lost'
    }
    Test-Case 'storm-json-unsigned-DWORD-legacy-remains-exact' {
        Enable-TestSensorBudget;$state.SensorValues['F0000101_0'].Value=-1;$file=$dstPath+'.ymcc-sensor-groups-last.json';$doc=New-TestSensorReceipt;$doc.values['F0000101_0'].Value=[long]4294967295;[IO.File]::WriteAllText($file,($doc|ConvertTo-Json -Depth 12 -Compress))
        Assert ((Invoke-HWiNFOSensorBudget $exePath $dstPath) -eq 'configured') 'native unsigned DWORD legacy rejected';$saved=Get-Content -LiteralPath $file -Raw|ConvertFrom-Json;Assert ([long]$saved.values.F0000101_0.Value -in @(-1,[long]4294967295)) 'DWORD original lost'
    }
    Test-Case 'storm-debt-retry-does-not-snapshot-owned-zero-as-original' {
        Enable-TestSensorBudget;$state.FailAfterSensorDisable=$true;$state.FailSensorRollback=$true
        Assert ((Invoke-HWiNFOSensorBudget $exePath $dstPath) -clike 'unconfirmed*') 'lost acknowledgement did not fail';$file=$dstPath+'.ymcc-sensor-groups-last.json';$first=Get-Content -LiteralPath $file -Raw|ConvertFrom-Json
        $state.FailSensorRollback=$false;Assert ((Invoke-HWiNFOSensorBudget $exePath $dstPath) -eq 'configured') 'fault release did not retry';$last=Get-Content -LiteralPath $file -Raw|ConvertFrom-Json
        foreach($name in $first.values.PSObject.Properties.Name){Assert ([long]$last.values.$name.Value -eq [long]$first.values.$name.Value) 'retry reset original debt to owned state'}
    }
    Test-Case 'storm-debt-concurrent-foreign-choice-is-not-restored-or-disabled' {
        Enable-TestSensorBudget;$file=$dstPath+'.ymcc-sensor-groups-last.json';$doc=New-TestSensorReceipt 'last' 'prepared';[IO.File]::WriteAllText($file,($doc|ConvertTo-Json -Depth 12 -Compress));$before=& $script:ProductionHash $file;$state.SensorValues['F0000101_0'].Value=2
        Assert ((Invoke-HWiNFOSensorBudget $exePath $dstPath) -clike 'unconfirmed*') 'foreign choice accepted as settled';Assert ($state.SensorWrites -eq 0 -and $state.SensorValues['F0000101_0'].Value -eq 2 -and (& $script:ProductionHash $file) -eq $before) 'foreign choice/debt was overwritten'
    }
    Test-Case 'storm-debt-live-producer-keeps-exact-last-marker' {
        Enable-TestSensorBudget;$file=$dstPath+'.ymcc-sensor-groups-last.json';$doc=New-TestSensorReceipt 'last' 'rollback-pending';[IO.File]::WriteAllText($file,($doc|ConvertTo-Json -Depth 12 -Compress));$before=& $script:ProductionHash $file;[void]$state.Running.Add((New-TestProcess 101))
        Assert ((Invoke-HWiNFOSensorBudget $exePath $dstPath) -clike 'unconfirmed*') 'producer guard not reported';Assert ($state.SensorWrites -eq 0 -and (& $script:ProductionHash $file) -eq $before) 'live producer reset durable marker'
    }
    Test-Case 'storm-backup-locked-original-is-not-successful-keep' {
        $file=$dstPath+'.ymcc-original.bak';[IO.File]::WriteAllText($file,'[Settings]'+[Environment]::NewLine+'Keep=original');$hold=[IO.File]::Open($file,'Open','ReadWrite','None')
        try{$failed=$false;try{Write-HWiNFOBackup $file ([Text.Encoding]::UTF8.GetBytes('[Settings]'+[Environment]::NewLine+'Keep=new')) -KeepExisting}catch{$failed=$true};Assert $failed 'locked original silently accepted'}finally{$hold.Dispose()}
    }
    Test-Case 'storm-backup-bad-original-is-not-successful-keep' {
        $file=$dstPath+'.ymcc-original.bak';[IO.File]::WriteAllText($file,'{');$before=& $script:ProductionHash $file;$failed=$false;try{Write-HWiNFOBackup $file ([Text.Encoding]::UTF8.GetBytes('[Settings]'+[Environment]::NewLine+'Keep=new')) -KeepExisting}catch{$failed=$true}
        Assert ($failed -and (& $script:ProductionHash $file) -eq $before) 'bad backup was accepted or replaced'
    }
    Test-Case 'storm-backup-invalid-incoming-cannot-replace-valid-last' {
        $file=$dstPath+'.ymcc-last.bak';[IO.File]::WriteAllText($file,'[Settings]'+[Environment]::NewLine+'Keep=original');$before=& $script:ProductionHash $file;$failed=$false;try{Write-HWiNFOBackup $file ([Text.Encoding]::UTF8.GetBytes('{'))}catch{$failed=$true};Assert ($failed -and (& $script:ProductionHash $file) -eq $before) 'invalid bytes replaced valid backup'
    }
    Test-Case 'storm-ini-directory-is-not-missing' {
        [IO.File]::Delete($dstPath);[IO.Directory]::CreateDirectory($dstPath)|Out-Null;$rc=Invoke-HWiNFORecoveryEntry;Assert ($rc -ne 0 -and -not $state.Events.Contains('stop') -and $state.CopyCount -eq 0 -and $state.LaunchCount -eq 0) 'directory was treated as missing'
    }
    Test-Case 'storm-ini-bomless-ANSI-is-preserved' {
        $encoding=[Text.Encoding]::GetEncoding([Globalization.CultureInfo]::CurrentCulture.TextInfo.ANSICodePage);$word=[string][char]0x4e2d+[char]0x6587
        [IO.File]::WriteAllBytes($dstPath,$encoding.GetBytes('[Settings]'+[Environment]::NewLine+'Custom='+$word));$ini=Read-HWiNFOIni $dstPath;Assert ($ini.Text.Contains($word)) 'ANSI bytes decoded using host UTF8 default'
    }
    Test-Case 'storm-ini-stale-stage-is-not-owned-or-promoted' {
        $stage=$dstPath+'.ymcc-stage';[IO.File]::WriteAllText($stage,'unknown-stage-original');Assert ((Invoke-HWiNFORecoveryEntry) -eq 0) 'valid recovery failed';Assert ([IO.File]::ReadAllText($stage) -ceq 'unknown-stage-original') 'stale stage was overwritten or deleted'
    }
    Test-Case 'storm-ini-foreign-change-before-commit-survives' {
        $state.AfterConfigCopy={param($path) [IO.File]::WriteAllText($script:dstPath,'[Settings]'+[Environment]::NewLine+'Foreign=keep')};$rc=Invoke-HWiNFORecoveryEntry
        Assert ($rc -ne 0 -and $state.LaunchCount -eq 0 -and [IO.File]::ReadAllText($dstPath).Contains('Foreign=keep')) 'foreign bytes overwritten by commit/rollback'
    }
    Test-Case 'storm-ini-explicit-repair-preserves-malformed-bytes' {
        $script:RepairInvalidConfig=$true;$raw=[byte[]]@(239,187,191,195,40);[IO.File]::WriteAllBytes($dstPath,$raw);$sha=[Security.Cryptography.SHA256]::Create();try{$hex=([BitConverter]::ToString($sha.ComputeHash($raw))).Replace('-','').ToLowerInvariant()}finally{$sha.Dispose()}
        Assert ((Invoke-HWiNFORecoveryEntry) -eq 0) 'explicit repair failed';$archive=$dstPath+'.ymcc-corrupt-'+$hex+'.bak';Assert ([IO.File]::Exists($archive) -and [Convert]::ToBase64String([IO.File]::ReadAllBytes($archive)) -ceq [Convert]::ToBase64String($raw)) 'malformed original not preserved';Assert (-not [IO.File]::Exists($dstPath+'.ymcc-original.bak')) 'malformed bytes became confirmed original backup'
    }
    Test-Case 'storm-json-entry-does-not-ACK-bad-original' {
        Enable-TestSensorBudget;$file=$dstPath+'.ymcc-sensor-groups-original.json';[IO.File]::WriteAllText($file,'{');$rc=Invoke-HWiNFORecoveryEntry;Assert ($rc -ne 0 -and $state.SensorWrites -eq 0) 'entry ACKed unknown backup or changed sensors'
    }


    Test-Case 'storm-entry-locked-backup-fault-clears' {
        $file=$dstPath+'.ymcc-original.bak';[IO.File]::WriteAllText($file,'[Settings]'+[Environment]::NewLine+'Keep=first');$before=& $script:ProductionHash $file
        $hold=[IO.File]::Open($file,'Open','ReadWrite','None');try{Assert ((Invoke-HWiNFORecoveryEntry) -ne 0) 'locked backup ACKed success';Assert (-not $state.Events.Contains('stop') -and $state.CopyCount -eq 0 -and $state.LaunchCount -eq 0) 'IO was treated as missing'}finally{$hold.Dispose()}
        Assert ((Invoke-HWiNFORecoveryEntry) -eq 0 -and (& $script:ProductionHash $file) -eq $before) 'released lock did not retry or preserved first bytes changed'
    }
    Test-Case 'storm-entry-deferred-sensor-debt-is-not-success-ACK' {
        Enable-TestSensorBudget;$state.FailAfterSensorDisable=$true;$state.FailSensorRollback=$true;$rc=Invoke-HWiNFORecoveryEntry
        Assert ($rc -eq 16 -and $state.LaunchCount -eq 1) 'optional data recovery hid pending sensor debt';$last=Get-Content -LiteralPath ($dstPath+'.ymcc-sensor-groups-last.json') -Raw|ConvertFrom-Json;Assert ($last.phase -ceq 'rollback-pending') 'pending debt was marked completed'
    }
    Test-Case 'storm-explicit-repair-requires-restart-not-healthy-noop' {
        $script:Mode='start';$script:RepairInvalidConfig=$true;$state.Healthy=$true;Assert ((Invoke-HWiNFORecoveryEntry) -eq 8 -and $state.CopyCount -eq 0 -and $state.LaunchCount -eq 0) 'explicit repair silently became healthy no-op'
    }
    Test-Case 'storm-explicit-repair-survives-exact-elevated-handoff' {
        $script:RepairInvalidConfig=$true;$state.CopyDenied=$true;[IO.File]::WriteAllText($dstPath,'malformed-original');Assert ((Invoke-HWiNFORecoveryEntry) -eq 0) 'explicit repair elevated retry failed';Assert ($state.ElevationArguments -match ' -RepairInvalidConfig$') 'explicit intent lost at handoff';Assert-Order @('unlock','elevate','copy','verify','launch')
    }


    foreach($defect in @('empty','oversized','bad-utf8','locked','readonly')){
        Test-Case ('storm-io-last-'+$defect) {
            Enable-TestSensorBudget;$file=$dstPath+'.ymcc-sensor-groups-last.json';$doc=New-TestSensorReceipt;$text=$doc|ConvertTo-Json -Depth 12 -Compress
            if($defect -eq 'empty'){$text=''};if($defect -eq 'oversized'){$text=' ' * 16385};[IO.File]::WriteAllText($file,$text)
            if($defect -eq 'bad-utf8'){[IO.File]::WriteAllBytes($file,[byte[]]@(239,187,191,255))};$before=& $script:ProductionHash $file;$hold=$null
            try{
                if($defect -eq 'locked'){$hold=[IO.File]::Open($file,'Open','ReadWrite','None')};if($defect -eq 'readonly'){[IO.File]::SetAttributes($file,[IO.FileAttributes]::ReadOnly)}
                Assert ((Invoke-HWiNFOSensorBudget $exePath $dstPath) -clike 'unconfirmed*') 'IO/corrupt last ACKed';Assert ($state.SensorWrites -eq 0) 'failed last publication allowed sensors'
            }finally{if($hold){$hold.Dispose()};[IO.File]::SetAttributes($file,[IO.FileAttributes]::Normal)}
            Assert ((& $script:ProductionHash $file) -eq $before) 'failed last original was overwritten'
            if($defect -in @('locked','readonly')){Assert ((Invoke-HWiNFOSensorBudget $exePath $dstPath) -eq 'configured') 'fault cleared but original consumer cannot retry'}
        }
    }
    foreach($defect in @('duplicate','empty','bad-utf8')){
        Test-Case ('storm-ini-invalid-original-preserved-'+$defect) {
            $text=if($defect -eq 'duplicate'){'[Settings]'+[Environment]::NewLine+'X=1'+[Environment]::NewLine+'x=2'}else{''};$raw=[Text.Encoding]::UTF8.GetBytes($text)
            if($defect -eq 'bad-utf8'){$raw=[byte[]]@(239,187,191,195,40)};[IO.File]::WriteAllBytes($dstPath,$raw);$before=[Convert]::ToBase64String($raw)
            Assert ((Invoke-HWiNFORecoveryEntry) -ne 0 -and -not $state.Events.Contains('stop') -and $state.CopyCount -eq 0) 'bad active INI changed without explicit repair'
            Assert ([Convert]::ToBase64String([IO.File]::ReadAllBytes($dstPath)) -ceq $before) 'bad active original lost'
            $script:RepairInvalidConfig=$true;Assert ((Invoke-HWiNFORecoveryEntry) -eq 0) 'explicit active INI repair failed';$files=@(Get-ChildItem -LiteralPath $installRoot -Filter 'HWiNFO64.INI.ymcc-corrupt-*.bak');Assert ($files.Count -eq 1 -and [Convert]::ToBase64String([IO.File]::ReadAllBytes($files[0].FullName)) -ceq $before) 'raw original archive missing or normalized'
        }
    }
    Test-Case 'storm-backup-temporary-file-is-not-reused-or-cleared' {
        $path=$dstPath+'.ymcc-original.bak';[IO.File]::WriteAllText($path+'.tmp','old-unknown-temp');Write-HWiNFOBackup $path ([Text.Encoding]::UTF8.GetBytes('[Settings]'+[Environment]::NewLine+'X=1')) -KeepExisting;Assert ([IO.File]::ReadAllText($path+'.tmp') -ceq 'old-unknown-temp') 'old temp was deleted or reused'
    }
    Test-Case 'storm-debt-explicit-retry-keeps-unknown-original-bytes' {
        Enable-TestSensorBudget;$path=$dstPath+'.ymcc-sensor-groups-last.json';[IO.File]::WriteAllText($path,'{');for($i=0;$i -lt 12;$i++){Assert ((Invoke-HWiNFOSensorBudget $exePath $dstPath) -clike 'unconfirmed*') 'storm accepted corrupt debt'};Assert ($state.SensorWrites -eq 0 -and [IO.File]::ReadAllText($path) -ceq '{') 'storm reset unknown debt'
    }


    Test-Case 'storm-healthy-start-does-not-ACK-pending-debt' {
        Enable-TestSensorBudget;$file=$dstPath+'.ymcc-sensor-groups-last.json';$doc=New-TestSensorReceipt 'last' 'rollback-pending';[IO.File]::WriteAllText($file,($doc|ConvertTo-Json -Depth 12 -Compress));$before=& $script:ProductionHash $file;$script:Mode='start';$state.Healthy=$true;[void]$state.Running.Add((New-TestProcess 101))
        Assert ((Invoke-HWiNFORecoveryEntry) -eq 16) 'healthy no-op ACKed pending config debt';Assert ($state.CopyCount -eq 0 -and $state.SensorWrites -eq 0 -and $state.LaunchCount -eq 0 -and (& $script:ProductionHash $file) -eq $before) 'healthy pending receipt changed'
    }


    Test-Case 'pair-policy-committed-last-missing-first-healthy-unknown-explicit-saved-repair' {
        foreach($family in @('original','cstate-original','rtss-original')) {
            Reset-Test;Enable-TestSensorBudget
            $last=New-TestSensorReceipt 'last' 'committed';$last|Add-Member NoteProperty annotation 'preserve-saved-original'
            $lastPath=$dstPath+'.ymcc-sensor-groups-last.json';[IO.File]::WriteAllText($lastPath,($last|ConvertTo-Json -Depth 12 -Compress))
            foreach($other in @('original','cstate-original','rtss-original')){if($other -ne $family){$receipt=New-TestSensorReceipt $other;[IO.File]::WriteAllText(($dstPath+'.ymcc-sensor-groups-'+$other+'.json'),($receipt|ConvertTo-Json -Depth 12 -Compress))}}
            $before=& $script:ProductionHash $lastPath
            foreach($name in @($state.SensorValues.Keys)){$state.SensorValues[$name].Value=0}
            $script:Mode='start';$state.Healthy=$true;[void]$state.Running.Add((New-TestProcess 101))
            Assert ((Invoke-HWiNFORecoveryEntry) -eq 16) 'healthy committed-last without first ACKed ownership'
            Assert ($state.CopyCount -eq 0 -and $state.SensorWrites -eq 0 -and $state.LaunchCount -eq 0 -and !$state.Events.Contains('stop')) 'healthy missing pair mutated producer'
            $missing=$dstPath+'.ymcc-sensor-groups-'+$family+'.json';Assert (![IO.File]::Exists($missing)) 'healthy inspection invented first'
            $state.Running.Clear();$state.Healthy=$false
            Assert ((Invoke-HWiNFOSensorBudget $exePath $dstPath) -eq 'already-configured') 'explicit saved-last repair did not retain already configured state'
            $repaired=Read-HWiNFOSensorReceipt $missing;Assert ($null -ne $repaired) 'explicit retry did not reconstruct missing first from trusted last'
            foreach($property in $repaired.values.PSObject.Properties){Assert (([long]$property.Value.Value) -eq 1) 'current owned zero replaced saved original'}
            Assert ((& $script:ProductionHash $lastPath) -eq $before -and $state.SensorWrites -eq 0) 'saved committed last or registry changed during first repair'
        }
    }
    Test-Case 'pair-policy-first-missing-last-healthy-unknown-explicit-owned0-refused' {
        foreach($family in @('original','cstate-original','rtss-original')) {
            Reset-Test;Enable-TestSensorBudget
            $receipt=New-TestSensorReceipt $family;$first=$dstPath+'.ymcc-sensor-groups-'+$family+'.json';[IO.File]::WriteAllText($first,($receipt|ConvertTo-Json -Depth 12 -Compress));$before=& $script:ProductionHash $first
            foreach($name in @($state.SensorValues.Keys)){$state.SensorValues[$name].Value=0}
            $script:Mode='start';$state.Healthy=$true;[void]$state.Running.Add((New-TestProcess 101))
            Assert ((Invoke-HWiNFORecoveryEntry) -eq 16) 'healthy first without last ACKed ownership'
            Assert ($state.CopyCount -eq 0 -and $state.SensorWrites -eq 0 -and $state.LaunchCount -eq 0 -and !$state.Events.Contains('stop')) 'healthy first-only record changed producer'
            $state.Running.Clear();$state.Healthy=$false
            foreach($nonzero in @($false,$true)) {
                if($nonzero){$state.SensorValues['F0000101_0'].Value=1}
                Assert ((Invoke-HWiNFOSensorBudget $exePath $dstPath) -clike 'unconfirmed*') 'explicit retry made current owned values into missing evidence'
                Assert ((& $script:ProductionHash $first) -eq $before -and ![IO.File]::Exists($dstPath+'.ymcc-sensor-groups-last.json') -and $state.SensorWrites -eq 0) 'first-only debt or current state changed'
                foreach($other in @('original','cstate-original','rtss-original')){if($other -ne $family){Assert (![IO.File]::Exists($dstPath+'.ymcc-sensor-groups-'+$other+'.json')) 'missing last caused a new invented original'}}
            }
        }
    }

    $result = [ordered]@{ ok = ($script:failures -eq 0 -and $script:caseResults.Count -gt 0); checks = $script:caseResults.Count; passed=$script:passed; failed=$script:failures; details=@($script:caseResults.ToArray()); cases = @($script:names.ToArray()); powershellVersion = $PSVersionTable.PSVersion.ToString(); powershellEdition = [string]$PSVersionTable.PSEdition; hardwareReads = $false; realProcessChanges = $false; realRegistryWrites = $false; scope = 'Actual production recovery/INI functions with isolated file fixtures and process/UAC mocks; not HWiNFO runtime setting acceptance or CPU savings.' }
    $result | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath '__RESULT__' -Encoding UTF8
}
`;
const capture="[IO.File]::WriteAllText('"+task+"/TESTED-PRODUCTION.ps1', $ast.Extent.Text)\n";
const script=prefix+'\n'+capture+tests.replace('__RESULT__',task+'/UPSTREAM-RESULT.json').slice(0,-2)+cleanup;const file=task+'/upstream-fixtures.ps1';fs.writeFileSync(file,'\uFEFF'+script);
const production=process.env.YMCC_HWINFO_PRODUCTION_SOURCE||root+'/PowerControl/YeManHWiNFO.ps1';
const pin=p=>({path:path.resolve(p),sha256:crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex')});
const inputs=[production,'tools/hwinfo_upstream_config_selftest.mjs','tools/hwinfo_recovery_selftest.ps1','PowerControl/settings-baseline.json'].map(pin),started=performance.now();
const p=spawnSync(process.env.YMCC_HWINFO_POWERSHELL||'powershell.exe',['-NoProfile','-ExecutionPolicy','Bypass','-File',file,'-RecoveryScript',(process.env.YMCC_HWINFO_PRODUCTION_SOURCE||root+'/PowerControl/YeManHWiNFO.ps1'),'-ConfigurationOnly','-KeepEvidence','-CaseFilter',(process.env.YMCC_HWINFO_CASE_FILTER||'*'),'-EvidenceDir',task+'/case-evidence'],{encoding:'utf8',windowsHide:true,timeout:45000,maxBuffer:4e6});fs.writeFileSync(task+'/UPSTREAM.log',(p.stdout||'')+(p.stderr||''));console.log(((p.stdout||'')+(p.stderr||'')).slice(-6500));const after=inputs.map(v=>pin(v.path)),drift=after.filter((v,i)=>v.sha256!==inputs[i].sha256);
fs.writeFileSync(task+'/PROCESS.json',JSON.stringify({atUtc:new Date().toISOString(),pid:p.pid,executable:process.env.YMCC_HWINFO_POWERSHELL||'powershell.exe',executableSha256:process.env.YMCC_HWINFO_POWERSHELL?pin(process.env.YMCC_HWINFO_POWERSHELL).sha256:null,exitCode:p.status,signal:p.signal,error:p.error?.message,elapsedMs:performance.now()-started,caseFilter:process.env.YMCC_HWINFO_CASE_FILTER||'*',inputs,sourceDrift:drift,node:{path:process.execPath,version:process.version,...pin(process.execPath)},generated:pin(file)},null,2));
assert.equal(p.status,0,'upstream fixtures failed');assert.equal(drift.length,0,'input drift during actual consumer');
const r=JSON.parse(fs.readFileSync(task+'/UPSTREAM-RESULT.json','utf8').replace(/^\uFEFF/,''));r.executedProduction={path:(process.env.YMCC_HWINFO_PRODUCTION_SOURCE||root+'/PowerControl/YeManHWiNFO.ps1'),sha256:crypto.createHash('sha256').update(fs.readFileSync(process.env.YMCC_HWINFO_PRODUCTION_SOURCE||root+'/PowerControl/YeManHWiNFO.ps1')).digest('hex')};r.sourceSha256=Object.fromEntries(['PowerControl/YeManHWiNFO.ps1','tools/hwinfo_recovery_selftest.ps1'].map(f=>[f,crypto.createHash('sha256').update(fs.readFileSync(f)).digest('hex')]));fs.writeFileSync(task+'/UPSTREAM-RESULT.json',JSON.stringify(r,null,2)+'\n');assert(r.ok);console.log(JSON.stringify({ok:r.ok,checks:r.checks},null,2));
