// Actual PowerShell recovery functions with isolated INI files and named mappings.
// No real HWiNFO process, sensor mapping, registry write or UAC dialog is used.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import { spawnSync } from 'node:child_process';

const root = process.cwd().replaceAll('\\', '/');
const out = path.resolve(process.env.YMCC_HWINFO_READY_OUT || '../../../Build/Tasks/YMCC-Steam-HUD-Probe/hwinfo-ready-progress-20261016').replaceAll('\\', '/');
fs.mkdirSync(out, { recursive: true });
const base = fs.readFileSync('tools/hwinfo_recovery_selftest.ps1', 'utf8').replaceAll('\r\n', '\n');
const begin = base.indexOf("try {\n    Test-Case 'healthy start");
const end = base.lastIndexOf('} finally {\n    # One shell');
assert(begin > 0 && end > begin);
let prefix = base.slice(0, begin).replace('function Reset-Test([bool]', 'function Reset-TestBase([bool]');
prefix = prefix.replace("    Write-Output ('PASS ' + $name)", "    [void]$script:names.Add($name)\n    Write-Output ('PASS ' + $name)");
const cleanup = base.slice(end);
const bt = String.fromCharCode(96);
const tests = String.raw`
$script:names = New-Object Collections.ArrayList
$script:measurements = New-Object Collections.ArrayList
$script:realResolver = ($ast.Find({ param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq 'Resolve-HWiNFO' }, $false)).Body.Extent.Text
$script:realResolver = [scriptblock]::Create($realResolver.Substring(1, $realResolver.Length - 2))
$script:identity = 'YMCC_HWINFO_READY_' + [Guid]::NewGuid().ToString('N')
$body = $script:SharedMemorySource.Substring(1, $script:SharedMemorySource.Length - 2)
$body = $body.Replace('Global\HWiNFO_SENS_SM2', ($identity + '_0')).Replace('HWiNFO_SENS_SM2', ($identity + '_1')).Replace('Global\HWiNFO_SENS_SM', ($identity + '_2')).Replace('HWiNFO_SENS_SM', ($identity + '_3'))
$script:realProbe = [scriptblock]::Create($body)
$script:mapping = [IO.MemoryMappedFiles.MemoryMappedFile]::CreateNew(($identity + '_1'), 2048)
$script:view = $mapping.CreateViewAccessor()
function Reset-Test([bool]$running = $true, [bool]$healthy = $false) {
    New-Item -ItemType Directory -Path (Join-Path $installRoot 'YeMan') -Force | Out-Null
    Reset-TestBase $running $healthy
    $state.Polls = @(); $state.Layouts = @(); $state.Gaps = @(); $state.ClockMs = 1700000000000L; $state.ProbePrelaunch = $false
    foreach ($suffix in @('.ymcc-stage', '.ymcc-original.bak', '.ymcc-last.bak', '.ymcc-original.bak.tmp', '.ymcc-last.bak.tmp')) {
        $file = $dstPath + $suffix
        if ([IO.File]::Exists($file)) { [IO.File]::Delete($file) }
    }
    $view.Write(0, [uint32]0x53695748); $view.Write(4, [uint32]2); $view.Write(8, [uint32]1)
    $view.Write(12, [int64]1700000000); $view.Write(20, [uint32]48); $view.Write(24, [uint32]392); $view.Write(28, [uint32]1)
    $view.Write(32, [uint32]512); $view.Write(36, [uint32]316); $view.Write(40, [uint32]1); $view.Write(44, [uint32]3000)
    # Valid sensor values may all be zero; health depends on producer progression.
    $view.Write(512, [byte]0)
}
function Get-HWiNFOClockMs { return $script:state.ClockMs }
function Start-Sleep {
    param([int]$Milliseconds)
    [void]$state.Events.Add('sleep-' + $Milliseconds)
    $state.ClockMs += $Milliseconds
}
function Test-HWiNFOSharedMemory {
    param([int]$ExpectedIntervalMs = 0, [ref]$ProducerProof)
    if ($state.LaunchCount -eq 0) {
        if (-not $state.ProbePrelaunch) { return $state.Healthy }
        if ($null -ne $ProducerProof) { return (& $realProbe -ExpectedIntervalMs $ExpectedIntervalMs -ProducerProof $ProducerProof) }
        return (& $realProbe -ExpectedIntervalMs $ExpectedIntervalMs)
    }
    $state.HealthPolls++
    $i = $state.HealthPolls - 1
    if ($state.Gaps -contains $state.HealthPolls -or -not $state.HealthyAfterStart) { return $false }
    if ($state.Polls.Count) { $view.Write(12, [int64]$state.Polls[[Math]::Min($i, $state.Polls.Count - 1)]) }
    else { $view.Write(12, [int64]([Math]::Floor($state.ClockMs / 1000))) }
    if ($state.Layouts.Count) { $view.Write(8, [uint32]$state.Layouts[[Math]::Min($i, $state.Layouts.Count - 1)]) }
    if ($null -ne $ProducerProof) { return (& $realProbe -ExpectedIntervalMs $ExpectedIntervalMs -ProducerProof $ProducerProof) }
    return (& $realProbe -ExpectedIntervalMs $ExpectedIntervalMs)
}
function Value-Map($ini) {
    $values = @{}
    foreach ($entry in (Get-HWiNFOIniEntries $ini)) { $values[$entry.Section + '/' + $entry.Key] = $entry.Value }
    return $values
}
function Observe([string]$name) {
    [void]$measurements.Add([ordered]@{name=$name;checks=$state.HealthPolls;processEnumerationsAfterLaunch=$state.ProcessEnumerationsAfterLaunch;launches=$state.LaunchCount})
}
function Assert-ResolverRejected([string]$candidate) {
    $script:ExecutablePath = $candidate
    try { & $realResolver; throw 'Explicit invalid path was accepted' }
    catch { Assert ($_.Exception.Data['HWiNFOExitCode'] -eq 2) 'Wrong explicit-path error' }
}
try {
    __CAPTURE__
    if ('__SIDE__' -eq 'before') {
        Test-Case 'frozen start treats a stale readable mapping as an already healthy process' {
            $script:Mode = 'start'; $state.ProbePrelaunch = $true; $view.Write(12,[int64]1699999900)
            Assert ((Invoke-HWiNFORecoveryEntry) -eq 0 -and $state.LaunchCount -eq 0) 'Stale healthy-start acceptance not reproduced'
        }
        Test-Case 'frozen readable-only readiness accepts a stale producer' {
            $state.Polls = @(1699999900)
            Assert ((Invoke-HWiNFORecoveryEntry) -eq 0) 'Stale acceptance not reproduced'
            Assert ($state.HealthPolls -eq 1) 'Frozen readiness unexpectedly checks progression'
            Observe 'stale-readable'
        }
        Test-Case 'frozen readiness accepts one sample with no subsequent producer progress' {
            $state.Polls = @(1700000000)
            Assert ((Invoke-HWiNFORecoveryEntry) -eq 0) 'Single-sample acceptance not reproduced'
            Observe 'frozen-fresh'
        }
        Test-Case 'frozen recovery depends on the external YeMan template' {
            [IO.File]::Delete($srcPath)
            Assert ((Invoke-HWiNFORecoveryEntry) -eq 3) 'Missing external template dependency not reproduced'
            Assert ($state.LaunchCount -eq 0) 'Frozen code launched without source'
        }
        Test-Case 'frozen missing mapping performs 120 probes and repeated process enumeration' {
            $state.HealthyAfterStart = $false
            Assert ((Invoke-HWiNFORecoveryEntry) -eq 7) 'Expected missing-memory failure'
            Assert ($state.HealthPolls -eq 120 -and $state.ProcessEnumerationsAfterLaunch -eq 240) 'Frozen operation counts changed'
            Observe 'missing-mapping'
        }
    } else {
        Test-Case 'start recovers stale readable sharing instead of reporting a healthy no-op' {
            $script:Mode = 'start'; $state.ProbePrelaunch = $true; $view.Write(12,[int64]1699999900)
            Assert ((Invoke-HWiNFORecoveryEntry) -eq 0) 'Stale start did not recover'
            Assert ($state.LaunchCount -eq 1 -and $state.HealthPolls -eq 2) 'Stale start bypassed recovery/progression'
        }
        Test-Case 'fresh healthy start remains a no-op with actual header parsing' {
            $script:Mode = 'start'; $state.ProbePrelaunch = $true
            Assert ((Invoke-HWiNFORecoveryEntry) -eq 0) 'Fresh healthy start failed'
            Assert ($state.LaunchCount -eq 0 -and $state.CopyCount -eq 0 -and -not $state.Events.Contains('stop')) 'Fresh healthy start mutated upstream'
        }
        Test-Case 'embedded baseline preserves supplied entries, FPS and excludes unused clock/port queries' {
            $embedded = Get-HWiNFOManagedIni
            $reference = Read-HWiNFOIni '__REFERENCE__'
            $a = Value-Map $embedded; $b = Value-Map $reference
            Assert ($a.Count -eq $b.Count + 3 -and $a['Settings/PresentMon'] -eq '1' -and $a['Settings/NvClockMeasuring'] -eq '0' -and $a['Settings/USBC_PD'] -eq '0') 'Required FPS/base key count changed'
            Assert ($b['Settings/SensorAveragingInterval'] -ceq '1000') 'Stable reference averaging baseline drifted'
            foreach ($key in $b.Keys) {
                $expected = if ($key -ceq 'Settings/SensorAveragingInterval') { '0' } else { $b[$key] }
                Assert ($a[$key] -ceq $expected) ('Baseline differs at ' + $key)
            }
            # One explicit managed exception; every other byte must retain the stable template.
            $referenceText = [Text.Encoding]::UTF8.GetString($reference.Bytes)
            Assert ([regex]::Matches($referenceText, '(?m)^SensorAveragingInterval=1000\r?$').Count -eq 1) 'Reference averaging key missing/duplicated'
            $expectedBytes = [Text.Encoding]::UTF8.GetBytes($referenceText.Replace('SensorAveragingInterval=1000', 'SensorAveragingInterval=0') + "PresentMon=1${bt}r${bt}nNvClockMeasuring=0${bt}r${bt}nUSBC_PD=0${bt}r${bt}n")
            Assert ([Convert]::ToBase64String($embedded.Bytes) -ceq [Convert]::ToBase64String($expectedBytes)) 'Embedded baseline bytes differ outside the explicit averaging exception'
        }
        Test-Case 'recovery works when the complete external YeMan folder is absent' {
            # Remove only this known fixture file and its empty fixture directory.
            [IO.File]::Delete($srcPath); [IO.Directory]::Delete((Join-Path $installRoot 'YeMan'))
            Assert ((Invoke-HWiNFORecoveryEntry) -eq 0) 'Embedded recovery still depends on YeMan'
            $v = Value-Map (Read-HWiNFOIni $dstPath)
            Assert ($v['Settings/SensorsSM'] -eq '1' -and $v['Settings/SensorsOnly'] -eq '1') 'Sharing/background baseline missing'
            Assert (-not [IO.Directory]::Exists((Join-Path $installRoot 'YeMan'))) 'Recovery recreated a template dependency'
        }
        Test-Case 'a corrupt or disabled external legacy template cannot disable the embedded baseline' {
            [IO.File]::WriteAllText($srcPath, "[Settings]${bt}r${bt}nSensorsSM=0${bt}r${bt}nSensorsSM=0${bt}r${bt}n")
            Assert ((Invoke-HWiNFORecoveryEntry) -eq 0) 'Ignored legacy template was still parsed'
            Assert ((Value-Map (Read-HWiNFOIni $dstPath))['Settings/SensorsSM'] -eq '1') 'Legacy template defeated sharing'
        }
        Test-Case 'disabled shared-memory and foreground flags are restored while unknown user settings survive' {
            [IO.File]::WriteAllText($dstPath, "[Settings]${bt}r${bt}nSensorsSM=0${bt}r${bt}nSensorsOnly=0${bt}r${bt}nMinimalizeSensors=0${bt}r${bt}nMinimalizeSensorsClose=0${bt}r${bt}nMinimalizeMainWnd=0${bt}r${bt}nShowWelcomeAndProgress=1${bt}r${bt}nAutoUpdate=1${bt}r${bt}nCustom=keep${bt}r${bt}n[User]${bt}r${bt}nName=preserved${bt}r${bt}n")
            Assert ((Invoke-HWiNFORecoveryEntry) -eq 0) 'Baseline restoration failed'
            $v = Value-Map (Read-HWiNFOIni $dstPath)
            foreach ($key in @('SensorsSM','SensorsOnly','MinimalizeSensors','MinimalizeSensorsClose','MinimalizeMainWnd')) { Assert ($v['Settings/'+$key] -eq '1') ('Background key not restored: '+$key) }
            Assert ($v['Settings/ShowWelcomeAndProgress'] -eq '0' -and $v['Settings/AutoUpdate'] -eq '0') 'Popup baseline changed'
            Assert ($v['Settings/Custom'] -eq 'keep' -and $v['User/Name'] -eq 'preserved') 'Unknown user values lost'
        }
        Test-Case 'missing destination uses the stable baseline plus required FPS protection' {
            $state.Running.Clear(); [IO.File]::Delete($dstPath)
            Assert ((Invoke-HWiNFORecoveryEntry) -eq 0) 'Cold embedded recovery failed'
            Assert ([Convert]::ToBase64String([IO.File]::ReadAllBytes($dstPath)) -ceq [Convert]::ToBase64String((Get-HWiNFOManagedIni).Bytes)) 'Cold configuration differs from managed stable baseline'
        }
        foreach ($period in @(1000,1500,2000,3000)) {
            Test-Case ('dynamic interval ' + $period + ' retains all background baseline settings') {
                $script:SensorIntervalMs = $period; $view.Write(44, [uint32]$period)
                Assert ((Invoke-HWiNFORecoveryEntry) -eq 0) 'Dynamic baseline recovery failed'
                $v = Value-Map (Read-HWiNFOIni $dstPath)
                Assert ($v['Settings/SensorInterval'] -eq [string]$period -and $v['Settings/SensorsSM'] -eq '1') 'Dynamic override disabled sharing'
                Assert ($v['Settings/MinimalizeSensors'] -eq '1' -and $v['Settings/WakeGPUs'] -eq '0') 'Dynamic override changed background settings'
            }
        }
        Test-Case 'existing valid 2000 ms period survives embedded default recovery' {
            [IO.File]::WriteAllText($dstPath, "[Settings]${bt}r${bt}nSensorInterval=2000${bt}r${bt}n")
            Assert ((Invoke-HWiNFORecoveryEntry) -eq 0) 'Default period recovery failed'
            Assert ((Value-Map (Read-HWiNFOIni $dstPath))['Settings/SensorInterval'] -eq '2000') 'Existing valid period discarded'
        }
        Test-Case 'embedded default bounds an out-of-policy period back to stable 3000 ms' {
            [IO.File]::WriteAllText($dstPath, "[Settings]${bt}r${bt}nSensorInterval=50${bt}r${bt}n")
            Assert ((Invoke-HWiNFORecoveryEntry) -eq 0) 'Invalid period fallback failed'
            Assert ((Value-Map (Read-HWiNFOIni $dstPath))['Settings/SensorInterval'] -eq '3000') 'Invalid period survived'
        }
        Test-Case 'embedded recovery preserves UTF16 user data and exact original backup bytes' {
            $custom = [string][char]0x4E2D + [char]0x6587
            [IO.File]::WriteAllText($dstPath, ("[Settings]${bt}r${bt}nSensorsSM=0${bt}r${bt}n[User]${bt}r${bt}nName=" + $custom), [Text.Encoding]::Unicode)
            $before = & $script:ProductionHash $dstPath
            Assert ((Invoke-HWiNFORecoveryEntry) -eq 0) 'UTF16 baseline merge failed'
            $ini = Read-HWiNFOIni $dstPath
            Assert ($ini.PreambleLength -eq 2 -and (Value-Map $ini)['User/Name'] -eq $custom) 'Unicode value/codec changed'
            Assert ((& $script:ProductionHash ($dstPath+'.ymcc-original.bak')) -eq $before) 'Backup not byte exact'
        }
        Test-Case 'damaged destination is rejected before stopping a running process' {
            [IO.File]::WriteAllText($dstPath, "[Settings]${bt}r${bt}nSensorsSM=0${bt}r${bt}nsensorssm=1${bt}r${bt}n")
            Assert ((Invoke-HWiNFORecoveryEntry) -eq 8) 'Ambiguous destination accepted'
            Assert (-not $state.Events.Contains('stop') -and $state.LaunchCount -eq 0) 'Invalid destination still stopped/launched'
        }
        Test-Case 'embedded copy verification failure rolls back exact original bytes' {
            $before = & $script:ProductionHash $dstPath; $state.CorruptCopies = 5
            Assert ((Invoke-HWiNFORecoveryEntry) -eq 5) 'Corrupt copy was accepted'
            Assert ((& $script:ProductionHash $dstPath) -eq $before -and $state.LaunchCount -eq 0) 'Failed verification lost original config'
        }
        Test-Case 'portable explicit path resolves literally and does not enumerate registry or processes' {
            $script:ExecutablePath = $exePath
            Assert ((& $realResolver) -ceq $exePath) 'Explicit portable path ignored'
            Assert ($state.Events.Count -eq 0) 'Explicit path performed unrelated resolution'
        }
        foreach ($candidate in @('relative\HWiNFO64.exe', (Join-Path $installRoot 'missing\HWiNFO64.exe'), $srcPath, ($exePath + '"'), ($exePath + [char]10))) {
            Test-Case ('invalid explicit portable path is rejected: ' + $candidate.Replace([string][char]10, '<LF>')) { Assert-ResolverRejected $candidate }
        }
        Test-Case 'full recovery uses actual resolution in a portable directory without an external template' {
            $saved = @{Root=$script:installRoot;Exe=$script:exePath;Src=$script:srcPath;Dst=$script:dstPath}
            try {
                $script:installRoot = Join-Path $tempRoot 'portable & edition!'
                [void][IO.Directory]::CreateDirectory($installRoot)
                $script:exePath = Join-Path $installRoot 'HWiNFO64.exe'
                $script:srcPath = Join-Path $installRoot 'YeMan\HWiNFO64.INI'
                $script:dstPath = Join-Path $installRoot 'HWiNFO64.INI'
                [IO.File]::WriteAllText($exePath, 'never run')
                $state.Running.Clear(); $script:ExecutablePath = $exePath
                function Resolve-HWiNFO { return (& $script:realResolver) }
                Assert ((Invoke-HWiNFORecoveryEntry) -eq 0) 'Portable full transaction failed'
                Assert ((Value-Map (Read-HWiNFOIni $dstPath))['Settings/SensorsSM'] -eq '1') 'Portable target not configured'
                Assert (-not [IO.Directory]::Exists((Join-Path $installRoot 'YeMan'))) 'Portable recovery depended on YeMan'
                Assert ($state.LaunchCount -eq 1 -and -not $state.Events.Contains('resolve')) 'Portable recovery used mocked resolution'
            } finally {
                $script:installRoot=$saved.Root; $script:exePath=$saved.Exe; $script:srcPath=$saved.Src; $script:dstPath=$saved.Dst
            }
        }
        Test-Case 'portable path with spaces and punctuation survives UAC together with requested period' {
            $script:ExecutablePath = Join-Path $installRoot 'portable & edition!\HWiNFO64.exe'
            New-Item -ItemType Directory -Path (Split-Path -Parent $ExecutablePath) -Force | Out-Null
            [IO.File]::WriteAllText($ExecutablePath, 'never run')
            $script:SensorIntervalMs = 1500; $view.Write(44,[uint32]1500); $state.CopyDenied = $true
            Assert ((Invoke-HWiNFORecoveryEntry) -eq 0) 'Elevated portable request failed'
            Assert ($state.ElevationArguments.EndsWith((' -SensorIntervalMs 1500 -ExecutablePath "{0}"' -f $ExecutablePath))) 'Portable path/period lost in handoff'
            Assert-Order @('unlock','elevate','copy','verify','launch')
        }
        foreach ($expected in @(0, 1000)) {
            Test-Case ('stale but readable producer is not ready, request=' + $expected) {
                $script:SensorIntervalMs = $expected; if ($expected) { $view.Write(44,[uint32]$expected) }
                $state.Polls = @(1699999900)
                $code = if ($expected) { 13 } else { 7 }
                Assert ((Invoke-HWiNFORecoveryEntry) -eq $code) 'Stale producer became ready'
                Assert ($state.HealthPolls -eq 30 -and $state.LaunchCount -eq 1) 'Stale timeout unbounded/relaunched'
            }
        }
        Test-Case 'one fresh but frozen producer times out as unconfirmed progress' {
            $state.Polls = @(1700000000)
            Assert ((Invoke-HWiNFORecoveryEntry) -eq 14) 'One sample incorrectly became ready'
            Assert ($state.HealthPolls -eq 30) 'Frozen producer wait unbounded'
            Observe 'frozen-fresh'
        }
        Test-Case 'stable zero values are healthy when the producer timestamp advances' {
            Assert ((Invoke-HWiNFORecoveryEntry) -eq 0) 'Zero-valued sensors wrongly rejected'
            Assert ($state.HealthPolls -eq 2 -and $state.ProcessEnumerationsAfterLaunch -eq 0) 'Ready check enumerated processes or missed proof'
            Observe 'valid-progress'
        }
        Test-Case 'normal three-second producer waits for advancement without falsely timing out' {
            $state.Polls = @(1700000000,1700000000,1700000000,1700000003)
            Assert ((Invoke-HWiNFORecoveryEntry) -eq 0) 'Three-second producer failed'
            Assert ($state.HealthPolls -eq 4 -and $state.LaunchCount -eq 1) 'Readiness did not wait for actual advancement'
        }
        Test-Case 'backward producer timestamp cannot count as progress' {
            $state.Polls = @(1700000000,1699999999)
            Assert ((Invoke-HWiNFORecoveryEntry) -eq 14) 'Backward time became progress'
        }
        Test-Case 'layout change resets progression and requires two proofs of the new layout' {
            $state.Polls = @(1700000000,1700000001,1700000002); $state.Layouts = @(1,2,2)
            Assert ((Invoke-HWiNFORecoveryEntry) -eq 0) 'New coherent layout failed'
            Assert ($state.HealthPolls -eq 3) 'Different layouts combined into one receipt'
        }
        Test-Case 'gap clears the previous proof before subsequent progress is accepted' {
            $state.Gaps = @(2)
            Assert ((Invoke-HWiNFORecoveryEntry) -eq 0) 'Recoverable gap failed'
            Assert ($state.HealthPolls -eq 4) 'Proof crossed an invalid observation'
        }
        Test-Case 'missing mapping uses thirty one-second checks and zero process enumeration after launch' {
            $state.HealthyAfterStart = $false
            Assert ((Invoke-HWiNFORecoveryEntry) -eq 7) 'Missing mapping incorrectly accepted'
            Assert ($state.HealthPolls -eq 30 -and $state.ProcessEnumerationsAfterLaunch -eq 0) 'Operation budget regressed'
            Observe 'missing-mapping'
        }
        Test-Case 'exit is detected from the launched process handle before any readiness probe' {
            $state.ExitOnStart = $true
            Assert ((Invoke-HWiNFORecoveryEntry) -eq 6) 'Exited process became ready'
            Assert ($state.HealthPolls -eq 0 -and $state.ProcessEnumerationsAfterLaunch -eq 0) 'Exited process was probed'
        }
        foreach ($age in @(-2000, 6000, 100000)) {
            Test-Case ('strict parser rejects out-of-budget producer age ' + $age) {
                $view.Write(12,[int64](1700000000 - $age/1000))
                $proof = 'old'
                Assert (-not (& $realProbe -ProducerProof ([ref]$proof))) 'Out-of-budget producer accepted'
                Assert ($null -eq $proof) 'Rejected proof was not cleared'
            }
        }
        foreach ($age in @(0, -1000, 5000)) {
            Test-Case ('strict parser accepts bounded age ' + $age + ' for a 3000 ms producer') {
                $view.Write(12,[int64](1700000000 - $age/1000))
                $proof = $null
                Assert ([bool](& $realProbe -ProducerProof ([ref]$proof))) 'Bounded age rejected'
                Assert ($proof.PollTime -gt 0 -and $proof.Layout.Length -lt 256) 'Unbounded/missing producer receipt'
            }
        }
        foreach ($pollTime in @(0L, -1L, [long]::MaxValue)) {
            Test-Case ('strict parser rejects invalid timestamp ' + $pollTime) {
                $view.Write(12,[int64]$pollTime); $proof = $null
                Assert (-not (& $realProbe -ProducerProof ([ref]$proof))) 'Invalid timestamp accepted'
            }
        }
        Test-Case 'strict parser default unknown-period compatibility still requires fresh advancing timestamps' {
            $view.Write(4,[uint32]0); $view.Write(20,[uint32]44)
            Assert ((Invoke-HWiNFORecoveryEntry) -eq 0) 'Legacy default rejected coherent progress'
            Assert ($state.HealthPolls -eq 2) 'Legacy layout bypassed progression'
        }
    }
    [ordered]@{ok=$true;side='__SIDE__';checks=$script:passed;cases=@($names.ToArray());measurements=@($measurements.ToArray());syntheticNamedMappings=$true;realHWiNFOChanges=$false;registryWrites=$false;hardwareReads=$false} | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath '__RESULT__' -Encoding UTF8
} finally {
    $view.Dispose(); $mapping.Dispose()
}
`;
const results = {};
for (const side of ['before', 'after']) {
    const source = side === 'before' ? out + '/before/PowerControl/YeManHWiNFO.ps1' : root + '/PowerControl/YeManHWiNFO.ps1';
    const fixture = out + '/' + side + '-ready-fixtures.ps1';
    const script = prefix + '\ntry {\n' + tests.replaceAll('__SIDE__', side)
        .replaceAll('__REFERENCE__', out + '/STABLE-TEMPLATE-REFERENCE.ini')
        .replace('__RESULT__', out + '/' + side + '-READY-RESULT.json')
        .replace('__CAPTURE__', "[IO.File]::WriteAllText('" + out + '/' + side + "-TESTED-PRODUCTION.ps1', $ast.Extent.Text)") + '\n' + cleanup;
    fs.writeFileSync(fixture, '\uFEFF' + script);
    const result = spawnSync('powershell.exe', ['-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', fixture, '-RecoveryScript', source], { encoding: 'utf8', windowsHide: true, timeout: 45000, maxBuffer: 4e6 });
    const log = (result.stdout || '') + (result.stderr || '');
    fs.writeFileSync(out + '/' + side + '-READY.log', log);
    console.log(side + ': ' + log.slice(-5800));
    assert.equal(result.status, 0, side + ' readiness fixtures failed');
    const tested = fs.readFileSync(out + '/' + side + '-TESTED-PRODUCTION.ps1', 'utf8').replace(/^\uFEFF/, '');
    assert.equal(tested, fs.readFileSync(source, 'utf8').replace(/^\uFEFF/, ''), 'Production drift during test');
    results[side] = JSON.parse(fs.readFileSync(out + '/' + side + '-READY-RESULT.json', 'utf8').replace(/^\uFEFF/, ''));
    results[side].sourceSha256 = crypto.createHash('sha256').update(fs.readFileSync(source)).digest('hex');
    results[side].exactTestedSource = true;
    fs.writeFileSync(out + '/' + side + '-READY-RESULT.json', JSON.stringify(results[side], null, 2) + '\n');
}
fs.writeFileSync(out + '/READY-COMPARISON.json', JSON.stringify({ ok: true, baselineDefectsReproduced: true, before: results.before, after: results.after, realHWiNFORuntimeAcceptance: false, cpuPercentageMeasured: false }, null, 2) + '\n');
console.log(JSON.stringify({ ok: true, before: results.before.checks, after: results.after.checks, realUpstreamChanges: false }, null, 2));
