// Actual production PowerShell INI/recovery functions; isolated settings/files,
// process boundary mocks only. Never launches/stops HWiNFO or writes HKCU.
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import crypto from 'node:crypto';import {spawnSync} from 'node:child_process';
const root=process.cwd().replaceAll('\\','/'),out=path.resolve(process.env.YMCC_HWINFO_ACQUISITION_OUT||'../../../Build/Tasks/YMCC-Steam-HUD-Probe/hwinfo-producer-budget-20261016').replaceAll('\\','/');fs.mkdirSync(out,{recursive:true});
const base=fs.readFileSync('tools/hwinfo_recovery_selftest.ps1','utf8').replaceAll('\r\n','\n'),begin=base.indexOf("try {\n    Test-Case 'healthy start"),end=base.lastIndexOf('} finally {\n    # One shell');assert(begin>0&&end>begin);
let prefix=base.slice(0,begin).replace('function Reset-Test([bool]','function Reset-TestBase([bool]');prefix=prefix.replace("    Write-Output ('PASS ' + $name)","    [void]$script:names.Add($name)\n    Write-Output ('PASS ' + $name)");
const bt=String.fromCharCode(96);
const tests=String.raw`
$script:names = New-Object Collections.ArrayList
$script:settingsPath = Join-Path $tempRoot 'yeman-settings.json'
function Reset-Test([bool]$running = $true, [bool]$healthy = $false) {
    Reset-TestBase $running $healthy
    if ([IO.File]::Exists($settingsPath)) { [IO.File]::Delete($settingsPath) }
    foreach ($suffix in @('.ymcc-stage','.ymcc-original.bak','.ymcc-last.bak','.ymcc-original.bak.tmp','.ymcc-last.bak.tmp')) {
        $path = $dstPath + $suffix
        if ([IO.File]::Exists($path)) { [IO.File]::Delete($path) }
    }
}
function Set-Policy($policy) {
    $doc = [ordered]@{rtss=[ordered]@{monitorAcquisition=$policy;monitorOutput=@{enabled=$true;output='ymcc'}};tdp=@{tdpMax=30};fan=@{enabled=$true}}
    [IO.File]::WriteAllText($settingsPath, ($doc | ConvertTo-Json -Depth 8), (New-Object Text.UTF8Encoding($false)))
}
function Policy([bool]$presentMon=$false,[bool]$snapshotPolling=$true) {
    return @{version=1;enabled=$true;presentMon=$presentMon;snapshotPolling=$snapshotPolling}
}
function Values {
    $v=@{}; foreach($e in (Get-HWiNFOIniEntries (Read-HWiNFOIni $dstPath))) { $v[$e.Section+'/'+$e.Key]=$e.Value };return $v
}
function Reject-BeforeStop {
    $before = & $script:ProductionHash $dstPath
    Assert ((Invoke-HWiNFORecoveryEntry) -eq 8) 'Invalid policy did not fail explicitly'
    Assert (-not $state.Events.Contains('stop') -and $state.CopyCount -eq 0 -and $state.LaunchCount -eq 0) 'Rejected policy mutated upstream'
    Assert ((& $script:ProductionHash $dstPath) -eq $before) 'Rejected policy changed configuration'
}
try {
    [IO.File]::WriteAllText('__CAPTURE__', $ast.Extent.Text)
    Test-Case 'missing unified settings retains stable baseline and protects required HWiNFO FPS' {
        $v=Get-HWiNFOAcquisitionOverrides;Assert ($v.Count -eq 3 -and $v.PresentMon -eq 1 -and $v.NvClockMeasuring -eq 0 -and $v.USBC_PD -eq 0) 'Required FPS configuration missing'
        $state.Running.Clear();[IO.File]::Delete($dstPath)
        Assert ((Invoke-HWiNFORecoveryEntry) -eq 0) 'Unconfigured recovery failed'
        Assert ((Values)['Settings/PresentMon'] -eq '1') 'Default disabled required FPS'
    }
    foreach($doc in @('{}','{"rtss":{}}','{"rtss":{"monitorOutput":{"ymcc":{"fields":{"fps":false}}}}}')) {
        Test-Case ('no acquisition section ignores display-only settings: '+$doc) {
            [IO.File]::WriteAllText($settingsPath,$doc)
            Assert ((Get-HWiNFOAcquisitionOverrides).PresentMon -eq 1) 'Required FPS configuration was affected by display selection'
        }
    }
    foreach($pm in @($false,$true)) { foreach($snapshot in @($false,$true)) {
        Test-Case ('actual INI and full recovery apply static pm='+$pm+' snapshot='+$snapshot) {
            Set-Policy (Policy $pm $snapshot)
            $before = & $script:ProductionHash $settingsPath
            [IO.File]::WriteAllText($dstPath,"[Settings]${bt}r${bt}nSensorsSM=0${bt}r${bt}nPresentMon=7${bt}r${bt}nSnapshotPolling=7${bt}r${bt}nUnknown=keep${bt}r${bt}n")
            Assert ((Invoke-HWiNFORecoveryEntry) -eq 0) 'Static recovery failed'
            $v=Values;Assert ($v['Settings/PresentMon'] -eq '1' -and $v['Settings/SnapshotPolling'] -eq [string][int]$snapshot) 'Static flags not written'
            Assert ($v['Settings/SensorsSM'] -eq '1' -and $v['Settings/MinimalizeSensors'] -eq '1' -and $v['Settings/Unknown'] -eq 'keep') 'Shared/background/user config damaged'
            Assert ((& $script:ProductionHash $settingsPath) -eq $before) 'Recovery rewrote the unified document'
            Assert-Order @('stop','exit-confirmed','copy','verify','launch')
        }
    }}
    Test-Case 'unmanaged optional sampling still repairs a legacy disabled FPS choice' {
        $p=Policy;$p.enabled=$false;Set-Policy $p
        [IO.File]::WriteAllText($dstPath,"[Settings]${bt}r${bt}nPresentMon=0${bt}r${bt}nSnapshotPolling=1${bt}r${bt}n")
        Assert ((Invoke-HWiNFORecoveryEntry) -eq 0) 'Existing policy recovery failed'
        $v=Values;Assert ($v['Settings/PresentMon'] -eq '1' -and $v['Settings/SnapshotPolling'] -eq '1') 'Existing settings were overridden'
    }
    Test-Case 'saving a static choice does not rewrite or restart a healthy running producer' {
        Set-Policy (Policy);$script:Mode='start';$state.Healthy=$true
        $before=& $script:ProductionHash $dstPath
        Assert ((Invoke-HWiNFORecoveryEntry) -eq 0) 'Healthy no-op failed'
        Assert ($state.LaunchCount -eq 0 -and $state.CopyCount -eq 0 -and -not $state.Events.Contains('stop')) 'Static save implicitly restarted upstream'
        Assert ((& $script:ProductionHash $dstPath) -eq $before) 'Static save rewrote live INI'
    }
    Test-Case 'unused NVIDIA measured clocks are disabled while ordinary source choices survive' {
        [IO.File]::WriteAllText($dstPath,"[Settings]${bt}r${bt}nNvClockMeasuring=1${bt}r${bt}nPresentMon=0${bt}r${bt}nPreferAMDADL=1${bt}r${bt}nSnapshotPolling=0${bt}r${bt}n")
        Assert ((Invoke-HWiNFORecoveryEntry) -eq 0) 'Producer optimization failed'
        $v=Values
        Assert ($v['Settings/NvClockMeasuring'] -eq '0' -and $v['Settings/PresentMon'] -eq '1') 'Unused clock branch or required FPS misconfigured'
        Assert ($v['Settings/PreferAMDADL'] -eq '1' -and $v['Settings/SnapshotPolling'] -eq '0') 'Unrelated AMD acquisition was changed'
    }
    Test-Case 'unused USB-C observer is disabled without changing FPS or battery source choices' {
        [IO.File]::WriteAllText($dstPath,"[Settings]${bt}r${bt}nUSBC_PD=1${bt}r${bt}nPresentMon=0${bt}r${bt}nSnapshotPolling=0${bt}r${bt}nPreferAMDADL=1${bt}r${bt}n")
        Assert ((Invoke-HWiNFORecoveryEntry) -eq 0) 'Port observer optimization failed'
        $v=Values
        Assert ($v['Settings/USBC_PD'] -eq '0' -and $v['Settings/PresentMon'] -eq '1') 'Unused observer or required FPS misconfigured'
        Assert ($v['Settings/SnapshotPolling'] -eq '0' -and $v['Settings/PreferAMDADL'] -eq '1') 'Unrelated AMD acquisition changed'
    }
    Test-Case 'static choices survive elevated full recovery' {
        Set-Policy (Policy);$state.CopyDenied=$true
        Assert ((Invoke-HWiNFORecoveryEntry) -eq 0) 'Elevated static recovery failed'
        $v=Values;Assert ($v['Settings/PresentMon'] -eq '1' -and $v['Settings/SnapshotPolling'] -eq '1') 'Elevated retry lost static config'
        Assert-Order @('unlock','elevate','copy','verify','launch')
    }
    Test-Case 'static failure retains the prior exact INI and never launches' {
        Set-Policy (Policy);$state.CorruptCopies=5;$before=& $script:ProductionHash $dstPath
        Assert ((Invoke-HWiNFORecoveryEntry) -eq 5) 'Bad copy accepted'
        Assert ((& $script:ProductionHash $dstPath) -eq $before -and $state.LaunchCount -eq 0) 'Bad static copy lost original'
    }
    Test-Case 'display field edits never alter the fixed acquisition choices' {
        Set-Policy (Policy)
        foreach($fields in @(@{fps=$true;cpuPower=$false},@{fps=$false;cpuPower=$true},@{})) {
            $doc=Get-Content -LiteralPath $settingsPath -Raw | ConvertFrom-Json
            $doc.rtss.monitorOutput | Add-Member NoteProperty ymcc @{fields=$fields} -Force
            [IO.File]::WriteAllText($settingsPath,($doc | ConvertTo-Json -Depth 8))
            $v=Get-HWiNFOAcquisitionOverrides
            Assert ($v.PresentMon -eq 1 -and $v.SnapshotPolling -eq 1) 'HUD selections changed static upstream policy'
        }
    }
    foreach($bad in @(@{version=2;enabled=$true;presentMon=$true;snapshotPolling=$false},@{version='1';enabled=$true;presentMon=$true;snapshotPolling=$false},@{version=1;enabled='true';presentMon=$true;snapshotPolling=$false},@{version=1;enabled=$true;presentMon=$false;snapshotPolling='1'},@{version=1;enabled=$true;presentMon=$false},@('wrong-shape'),42,'wrong-shape')) {
        Test-Case ('malformed producer policy rejected before stopping: '+($bad | ConvertTo-Json -Compress)) { Set-Policy $bad;Reject-BeforeStop }
    }
    foreach($text in @('{','[]','null','true','"text"')) {
        Test-Case ('invalid unified document rejected before stop: '+$text) { [IO.File]::WriteAllText($settingsPath,$text);Reject-BeforeStop }
    }
    Test-Case 'legacy disabled FPS choice is ignored while optional sampling is applied' {
        Set-Policy (Policy $false $true)
        Assert ((Get-HWiNFOAcquisitionOverrides).PresentMon -eq 1) 'Legacy FPS-disable survived migration'
    }
    Test-Case 'UTF8 BOM and unrelated Unicode settings remain readable and unchanged' {
        Set-Policy (Policy);$text=[IO.File]::ReadAllText($settingsPath)
        [IO.File]::WriteAllText($settingsPath,$text,(New-Object Text.UTF8Encoding($true)))
        $before=& $script:ProductionHash $settingsPath
        Assert ((Invoke-HWiNFORecoveryEntry) -eq 0) 'UTF8 BOM policy failed'
        Assert ((& $script:ProductionHash $settingsPath) -eq $before) 'BOM settings changed'
    }
    Test-Case 'oversized document is bounded before allocation and before stopping' {
        [IO.File]::WriteAllText($settingsPath,(' ' * 16777217));Reject-BeforeStop
    }
    Test-Case 'invalid UTF8 cannot become a partial default configuration' {
        [IO.File]::WriteAllBytes($settingsPath,[byte[]]@(255,254,255));Reject-BeforeStop
    }
    [ordered]@{ok=$true;checks=$script:passed;cases=@($names.ToArray());usesActualProductionFunctions=$true;realHwinfoExecuted=$false;registryWrites=$false;hardwareSampling=$false} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath '__RESULT__' -Encoding UTF8
}
`;
const source=root+'/PowerControl/YeManHWiNFO.ps1';
const file=out+'/acquisition-fixtures.ps1';
fs.writeFileSync(file,'\uFEFF'+prefix+'\n'+tests.replaceAll('__REFERENCE__',out+'/../hwinfo-ready-progress-20261016/STABLE-TEMPLATE-REFERENCE.ini').replace('__CAPTURE__',out+'/ACQUISITION-TESTED-PRODUCTION.ps1').replace('__RESULT__',out+'/ACQUISITION-RESULT.json').slice(0,-2)+base.slice(end));
const r=spawnSync('powershell.exe',['-NoProfile','-ExecutionPolicy','Bypass','-File',file,'-RecoveryScript',source],{encoding:'utf8',windowsHide:true,timeout:45000,maxBuffer:4e6});const log=(r.stdout||'')+(r.stderr||'');fs.writeFileSync(out+'/ACQUISITION.log',log);console.log(log.slice(-8000));assert.equal(r.status,0,'static acquisition fixtures failed');
assert.equal(fs.readFileSync(out+'/ACQUISITION-TESTED-PRODUCTION.ps1','utf8').replace(/^\uFEFF/,''),fs.readFileSync(source,'utf8'));
const result=JSON.parse(fs.readFileSync(out+'/ACQUISITION-RESULT.json','utf8').replace(/^\uFEFF/,''));result.sourceSha256=crypto.createHash('sha256').update(fs.readFileSync(source)).digest('hex');fs.writeFileSync(out+'/ACQUISITION-RESULT.json',JSON.stringify(result,null,2)+'\n');console.log(JSON.stringify({ok:true,checks:result.checks},null,2));
