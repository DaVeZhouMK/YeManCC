// Actual recovery + actual SM probe on isolated named mappings. Mock only process/UAC edges.
// Compare the frozen readable-only baseline with producer-period confirmation.
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import crypto from 'node:crypto';import {spawnSync} from 'node:child_process';
const root=process.cwd().replaceAll('\\','/'),out=path.resolve(process.env.YMCC_HWINFO_PERIOD_OUT||'../../../Build/Tasks/YMCC-Steam-HUD-Probe/hwinfo-period-ack-20261016').replaceAll('\\','/');fs.mkdirSync(out,{recursive:true});
const base=fs.readFileSync('tools/hwinfo_recovery_selftest.ps1','utf8').replaceAll('\r\n','\n');const start=base.indexOf("try {\n    Test-Case 'healthy start"),end=base.lastIndexOf('} finally {\n    # One shell');assert(start>0&&end>start);
let prefix=base.slice(0,start);const cleanup=base.slice(end);
prefix=prefix.replace('function Reset-Test([bool]', 'function Reset-TestBase([bool]');
prefix=prefix.replace("    Write-Output ('PASS ' + $name)","    [void]$script:names.Add($name)\n    Write-Output ('PASS ' + $name)");
const tests=String.raw`
$script:names = New-Object Collections.ArrayList
$script:identity = 'YMCC_HWINFO_PERIOD_' + [Guid]::NewGuid().ToString('N')
$body = $script:SharedMemorySource.Substring(1, $script:SharedMemorySource.Length - 2)
$body = $body.Replace('Global\HWiNFO_SENS_SM2', ($identity + '_0')).Replace('HWiNFO_SENS_SM2', ($identity + '_1')).Replace('Global\HWiNFO_SENS_SM', ($identity + '_2')).Replace('HWiNFO_SENS_SM', ($identity + '_3'))
$script:realProbe = [scriptblock]::Create($body)
$script:mapping = [IO.MemoryMappedFiles.MemoryMappedFile]::CreateNew(($identity + '_1'), 2048)
$script:view = $mapping.CreateViewAccessor()
function Reset-Test([bool]$running = $true, [bool]$healthy = $false) {
    Reset-TestBase $running $healthy
    $script:SensorIntervalMs = 0
    $script:state.ExpectedPolls = New-Object Collections.ArrayList
    $script:state.AdoptOnPoll = 0
    $view.Write(0, [uint32]0x53695748); $view.Write(4, [uint32]2); $view.Write(8, [uint32]1)
    $view.Write(12, [int64]1700000000); $view.Write(20, [uint32]48); $view.Write(24, [uint32]392); $view.Write(28, [uint32]1)
    $view.Write(32, [uint32]512); $view.Write(36, [uint32]316); $view.Write(40, [uint32]1); $view.Write(44, [uint32]3000)
}
function Get-HWiNFOClockMs { return 1700000000000L + $script:state.HealthPolls * 1000L }
function Test-HWiNFOSharedMemory {
    param([int]$ExpectedIntervalMs = 0, [ref]$ProducerProof)
    if ($state.LaunchCount -eq 0) { return $state.Healthy }
    $state.HealthPolls++
    [void]$state.ExpectedPolls.Add($ExpectedIntervalMs)
    if ($state.AdoptOnPoll -gt 0 -and $state.HealthPolls -ge $state.AdoptOnPoll) { $view.Write(44, [uint32]$script:SensorIntervalMs) }
    if (-not $state.HealthyAfterStart -or $state.HealthPolls -le $state.HealthDelay) { return $false }
    $view.Write(12, [int64](1700000000L + $state.HealthPolls))
    if ($null -ne $ProducerProof) { return (& $script:realProbe -ExpectedIntervalMs $ExpectedIntervalMs -ProducerProof $ProducerProof) }
    return (& $script:realProbe -ExpectedIntervalMs $ExpectedIntervalMs)
}
try {
    __CAPTURE__
    if ('__SIDE__' -eq 'before') {
        Test-Case 'frozen baseline falsely accepts a readable producer still reporting 3000 ms after a 1000 ms request' {
            $script:SensorIntervalMs = 1000
            Assert ((Invoke-HWiNFORecoveryEntry) -eq 0) 'Frozen mismatch did not reproduce'
            Assert ($state.HealthPolls -eq 1 -and $state.ExpectedPolls[0] -eq 0) 'Frozen path unexpectedly checks a period'
        }
    } else {
        foreach ($period in @(1000, 1500, 2000, 3000)) {
            Test-Case ('actual probe accepts verified producer period ' + $period) {
                $view.Write(44, [uint32]$period)
                Assert ([bool](& $realProbe -ExpectedIntervalMs $period)) 'Exact producer period rejected'
            }
            foreach ($other in @(1000, 1500, 2000, 3000)) {
                if ($other -eq $period) { continue }
                Test-Case ('actual probe rejects producer ' + $period + ' for request ' + $other) {
                    $view.Write(44, [uint32]$period)
                    Assert (-not (& $realProbe -ExpectedIntervalMs $other)) 'Mismatched period accepted'
                }
            }
        }
        foreach ($version in @(0, 1, 3, 4294967295)) {
            Test-Case ('unknown layout ' + $version + ' is readable only in compatibility mode') {
                $view.Write(4, [uint32]$version)
                Assert ([bool](& $realProbe)) 'Legacy readability changed'
                Assert (-not (& $realProbe -ExpectedIntervalMs 3000)) 'Unknown header proved a period'
            }
        }
        foreach ($offset in @(0, 44, 516, 4096)) {
            Test-Case ('unproved sensor offset ' + $offset + ' cannot prove a period') {
                $view.Write(20, [uint32]$offset)
                Assert (-not (& $realProbe -ExpectedIntervalMs 3000)) 'Unsafe extension accepted'
            }
        }
        foreach ($pollTime in @(0, -1)) {
            Test-Case ('uninitialized poll time ' + $pollTime + ' cannot prove a period') {
                $view.Write(12, [int64]$pollTime)
                Assert (-not (& $realProbe -ExpectedIntervalMs 3000)) 'Uninitialized producer accepted'
            }
        }
        foreach ($invalid in @(-1, 500, 5000, 2147483647)) {
            Test-Case ('invalid period request ' + $invalid + ' cannot become implicit compatibility success') {
                Assert (-not (& $realProbe -ExpectedIntervalMs $invalid)) 'Invalid expected period accepted'
            }
        }
        Test-Case 'missing or corrupted reading region remains unhealthy with a matching period' {
            $view.Write(40, [uint32]0); Assert (-not (& $realProbe -ExpectedIntervalMs 3000)) 'Missing readings accepted'
            $view.Write(40, [uint32]1); $view.Write(32, [uint32]($view.Capacity - 8)); Assert (-not (& $realProbe -ExpectedIntervalMs 3000)) 'OOB readings accepted'
        }
        # Keep production reads/checks intact; replace only mapping open with a
        # deterministic accessor boundary that changes one header field after
        # the payload byte is read. No real hardware mappings are opened.
        $openPattern = '\[System.IO.MemoryMappedFiles.MemoryMappedFile\]::OpenExisting\(\s*\$name(?:\s*,\s*\[System.IO.MemoryMappedFiles.MemoryMappedFileRights\]::Read)?\s*\)'
        $openMatches = [regex]::Matches($body, $openPattern)
        Assert ($openMatches.Count -eq 1) 'Missing or ambiguous mapped-file boundary'
        $raceBody = $body.Replace($openMatches[0].Value, '(Get-FakePeriodMapping)')
        $script:raceProbe = [scriptblock]::Create($raceBody)
        $script:fakeAccessor = [pscustomobject]@{ Capacity = $view.Capacity }
        $fakeAccessor | Add-Member ScriptMethod ReadUInt32 {
            param([long]$offset)
            $value = $script:view.ReadUInt32($offset)
            if ($script:flipAfterByte -and $offset -eq $script:corruptOffset) { return [uint32]($value + 1) }
            return $value
        }
        $fakeAccessor | Add-Member ScriptMethod ReadInt64 {
            param([long]$offset)
            $value = $script:view.ReadInt64($offset)
            if ($script:flipAfterByte -and $offset -eq $script:corruptOffset) { return [int64]($value + 1) }
            return $value
        }
        $fakeAccessor | Add-Member ScriptMethod ReadByte { param([long]$offset) $script:flipAfterByte = $true; return $script:view.ReadByte($offset) }
        $fakeAccessor | Add-Member ScriptMethod Dispose {}
        $script:fakeMapping = [pscustomobject]@{}
        $fakeMapping | Add-Member ScriptMethod CreateViewAccessor { return $script:fakeAccessor }
        $fakeMapping | Add-Member ScriptMethod Dispose {}
        function Get-FakePeriodMapping { return $script:fakeMapping }
        Test-Case 'stable accessor boundary proves the period without false race rejection' {
            $script:corruptOffset = -1; $script:flipAfterByte = $false
            Assert ([bool](& $raceProbe -ExpectedIntervalMs 3000)) 'Stable header rejected'
        }
        foreach ($field in @(0, 4, 12, 20, 32, 36, 40, 44)) {
            Test-Case ('header mutation at offset ' + $field + ' during the same probe cannot claim an applied period') {
                $script:corruptOffset = $field; $script:flipAfterByte = $false
                Assert (-not (& $raceProbe -ExpectedIntervalMs 3000)) 'Inconsistent header/period accepted'
            }
        }
        Test-Case 'legacy request retains readability without requiring a known period' {
            $view.Write(4, [uint32]0); $view.Write(20, [uint32]44)
            Assert ((Invoke-HWiNFORecoveryEntry) -eq 0) 'Default compatibility regressed'
            Assert ($state.HealthPolls -eq 2) 'Default path started polling again'
        }
        Test-Case 'actual recovery forwards requested period to the actual parser and confirms matching producer' {
            $script:SensorIntervalMs = 1500; $view.Write(44, [uint32]1500)
            Assert ((Invoke-HWiNFORecoveryEntry) -eq 0) 'Matching explicit recovery failed'
            Assert ($state.ExpectedPolls[0] -eq 1500 -and $state.HealthPolls -eq 2) 'Expected interval lost in health wiring'
        }
        Test-Case 'mismatched producer is bounded, returns 13 and never relaunches or claims applied' {
            $script:SensorIntervalMs = 1000
            Assert ((Invoke-HWiNFORecoveryEntry) -eq 13) 'Mismatch reported success or wrong failure'
            Assert ($state.HealthPolls -eq 30 -and $state.LaunchCount -eq 1) 'Unbounded polling/restart'
            Assert (@($state.ExpectedPolls | Where-Object { $_ -ne 1000 }).Count -eq 0) 'A poll bypassed the expected period'
        }
        Test-Case 'delayed adoption requires two matching observations after adoption on the third check without a restart' {
            $script:SensorIntervalMs = 2000; $state.AdoptOnPoll = 3
            Assert ((Invoke-HWiNFORecoveryEntry) -eq 0) 'Delayed period was never confirmed'
            Assert ($state.HealthPolls -eq 4 -and $state.LaunchCount -eq 1) 'Extra collection/restart for delayed period'
        }
        Test-Case 'unknown format times out as unconfirmed without a false application receipt' {
            $script:SensorIntervalMs = 3000; $view.Write(4, [uint32]3)
            Assert ((Invoke-HWiNFORecoveryEntry) -eq 13) 'Unknown format incorrectly proved application'
            Assert ($state.HealthPolls -eq 30) 'Unknown format bypassed bounded polling'
        }
        Test-Case 'initialization without a mapping still uses original 7 when no explicit interval is requested' {
            $state.HealthyAfterStart = $false
            Assert ((Invoke-HWiNFORecoveryEntry) -eq 7) 'Original failure code changed'
        }
        Test-Case 'healthy start with an explicit interval remains not-applied code 12 without writes or restart' {
            $script:Mode = 'start'; $state.Healthy = $true; $script:SensorIntervalMs = 1000
            Assert ((Invoke-HWiNFORecoveryEntry) -eq 12) 'Healthy live process was reconfigured'
            Assert ($state.CopyCount -eq 0 -and $state.LaunchCount -eq 0) 'Healthy start touched upstream'
        }
        Test-Case 'process exit remains code 6 instead of a period timeout' {
            $script:SensorIntervalMs = 1000; $state.ExitOnStart = $true
            Assert ((Invoke-HWiNFORecoveryEntry) -eq 6) 'Exited process incorrectly diagnosed'
        }
    }
    [ordered]@{ok=$true;side='__SIDE__';checks=$script:passed;cases=@($names.ToArray());syntheticNamedMappings=$true;realProcessChanges=$false;registryWrites=$false;hardwareReads=$false} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath '__RESULT__' -Encoding UTF8
} finally {
    $view.Dispose(); $mapping.Dispose()
}
`;
const results={};
for(const side of ['before','after']){
 const script=prefix+'\ntry {\n'+tests.replaceAll('__SIDE__',side).replace('__RESULT__',out+'/'+side+'-PERIOD-RESULT.json').replace('__CAPTURE__',"[IO.File]::WriteAllText('"+out+'/'+side+"-TESTED-PRODUCTION.ps1', $ast.Extent.Text)")+'\n'+cleanup;
 const file=out+'/'+side+'-period-fixtures.ps1';fs.writeFileSync(file,'\uFEFF'+script);
 const source=side==='before'?out+'/before/PowerControl/YeManHWiNFO.ps1':root+'/PowerControl/YeManHWiNFO.ps1';
 const r=spawnSync('powershell.exe',['-NoProfile','-ExecutionPolicy','Bypass','-File',file,'-RecoveryScript',source],{encoding:'utf8',windowsHide:true,timeout:45000,maxBuffer:3e6});fs.writeFileSync(out+'/'+side+'-PERIOD.log',(r.stdout||'')+(r.stderr||''));console.log(side+': '+((r.stdout||'')+(r.stderr||'')).slice(-4200));assert.equal(r.status,0,side+' period test failed');
 const result=JSON.parse(fs.readFileSync(out+'/'+side+'-PERIOD-RESULT.json','utf8').replace(/^\uFEFF/,''));assert(result.ok);
 const tested=fs.readFileSync(out+'/'+side+'-TESTED-PRODUCTION.ps1','utf8').replace(/^\uFEFF/,'');assert.equal(tested,fs.readFileSync(source,'utf8').replace(/^\uFEFF/,''),'tested production AST drift');
 result.sourceSha256=crypto.createHash('sha256').update(fs.readFileSync(source)).digest('hex');result.exactTestedSource=true;fs.writeFileSync(out+'/'+side+'-PERIOD-RESULT.json',JSON.stringify(result,null,2)+'\n');results[side]=result;
}
fs.writeFileSync(out+'/PERIOD-COMPARISON.json',JSON.stringify({ok:true,before:results.before.checks,after:results.after.checks,baselineFalseAcceptanceReproduced:true,realHWiNFOTested:false,sourceSha256:results.after.sourceSha256},null,2)+'\n');
