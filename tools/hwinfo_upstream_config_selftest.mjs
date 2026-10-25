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
try {
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
        [IO.File]::WriteAllText(($dstPath + '.ymcc-last.bak'), 'unrelated old backup')
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
    $result = [ordered]@{ ok = $true; checks = $script:passed; cases = @($script:names.ToArray()); hardwareReads = $false; realProcessChanges = $false; realRegistryWrites = $false; scope = 'Actual production recovery/INI functions with isolated file fixtures and process/UAC mocks; not HWiNFO runtime setting acceptance or CPU savings.' }
    $result | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath '__RESULT__' -Encoding UTF8
}
`;
const capture="[IO.File]::WriteAllText('"+task+"/TESTED-PRODUCTION.ps1', $ast.Extent.Text)\n";
const script=prefix+'\n'+capture+tests.replace('__RESULT__',task+'/UPSTREAM-RESULT.json').slice(0,-2)+cleanup;const file=task+'/upstream-fixtures.ps1';fs.writeFileSync(file,'\uFEFF'+script);
const p=spawnSync('powershell.exe',['-NoProfile','-ExecutionPolicy','Bypass','-File',file,'-RecoveryScript',root+'/PowerControl/YeManHWiNFO.ps1'],{encoding:'utf8',windowsHide:true,timeout:45000,maxBuffer:4e6});fs.writeFileSync(task+'/UPSTREAM.log',(p.stdout||'')+(p.stderr||''));console.log(((p.stdout||'')+(p.stderr||'')).slice(-6500));assert.equal(p.status,0,'upstream fixtures failed');
const r=JSON.parse(fs.readFileSync(task+'/UPSTREAM-RESULT.json','utf8').replace(/^\uFEFF/,''));assert(r.ok);r.sourceSha256=Object.fromEntries(['PowerControl/YeManHWiNFO.ps1','tools/hwinfo_recovery_selftest.ps1'].map(f=>[f,crypto.createHash('sha256').update(fs.readFileSync(f)).digest('hex')]));fs.writeFileSync(task+'/UPSTREAM-RESULT.json',JSON.stringify(r,null,2)+'\n');console.log(JSON.stringify({ok:r.ok,checks:r.checks},null,2));
