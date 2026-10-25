param([string]$RecoveryScript = (Join-Path $PSScriptRoot '../PowerControl/YeManHWiNFO.ps1'), [string]$ResultPath = (Join-Path $PSScriptRoot '../../../../Build/Tasks/YMCC-Steam-HUD-Probe/hwinfo-group-plan-20261016/ISOLATED-REGISTRY-RESULT.json'))
$ErrorActionPreference = 'Stop'
$tokens = $null; $errors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile((Resolve-Path -LiteralPath $RecoveryScript), [ref]$tokens, [ref]$errors)
if ($errors.Count) { throw 'Production parse errors' }
$sandboxPath = 'Software\YMCC-HWiNFO-RegistrySelftest-' + [Guid]::NewGuid().ToString('N')
if ($sandboxPath -cnotmatch '^Software\\YMCC-HWiNFO-RegistrySelftest-[0-9a-f]{32}$') { throw 'Unbounded test path' }
$sandboxBase = [Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::CurrentUser, [Microsoft.Win32.RegistryView]::Registry64)
$prior = $sandboxBase.OpenSubKey($sandboxPath)
if ($prior) { $prior.Dispose(); throw 'Isolated test key already exists' }
$names = @('Assert-HWiNFOSensorBudgetGroup','Read-HWiNFOSensorBudgetState','Write-HWiNFOSensorBudgetState','Test-HWiNFOSensorBudgetState')
$source = New-Object Collections.ArrayList
foreach ($name in $names) {
    $fn = $ast.Find({ param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq $name }, $false)
    if (-not $fn) { throw 'Missing production function' }
    # Only replace the fixed registry root; all adapter code is otherwise exact.
    [void]$source.Add($fn.Extent.Text.Replace('Software\HWiNFO64\Sensors\', ($sandboxPath + '\')))
}
. ([scriptblock]::Create(($source -join [Environment]::NewLine)))
$checks = New-Object Collections.ArrayList
function Check([string]$Name, [scriptblock]$Body) { & $Body; [void]$checks.Add($Name) }
function Assert([bool]$Value, [string]$Message) { if (-not $Value) { throw $Message } }
try {
    Check 'absent value reads as absent without creating a group key' {
        $v = Read-HWiNFOSensorBudgetState 'F0000101_0'
        Assert (-not $v.Exists) 'Missing value changed'
        $k = $sandboxBase.OpenSubKey($sandboxPath)
        Assert ($null -eq $k) 'Read created a registry key'
    }
    Check 'actual Windows adapter writes and reads Enabled as a DWORD zero' {
        Write-HWiNFOSensorBudgetState 'F0000101_0' ([pscustomobject]@{Exists=$true;Value=0})
        $v = Read-HWiNFOSensorBudgetState 'F0000101_0'
        Assert ($v.Exists -and $v.Value -eq 0) 'DWORD not read'
        $k = $sandboxBase.OpenSubKey($sandboxPath + '\F0000101_0')
        try { Assert ($k.GetValueKind('Enabled') -eq [Microsoft.Win32.RegistryValueKind]::DWord) 'Wrong kind' } finally { $k.Dispose() }
    }
    Check 'signed original DWORD values are restored without numeric coercion' {
        Write-HWiNFOSensorBudgetState 'F0000101_0' ([pscustomobject]@{Exists=$true;Value=-1})
        Assert ((Read-HWiNFOSensorBudgetState 'F0000101_0').Value -eq -1) 'Original DWORD changed'
    }
    Check 'restoring absence preserves labels and child settings' {
        $k = $sandboxBase.CreateSubKey($sandboxPath + '\F0000101_0')
        try { $k.SetValue('Label','owned test label'); $child=$k.CreateSubKey('Temp0');try{$child.SetValue('Show',1)}finally{$child.Dispose()} } finally { $k.Dispose() }
        Write-HWiNFOSensorBudgetState 'F0000101_0' ([pscustomobject]@{Exists=$false;Value=0})
        Assert (-not (Read-HWiNFOSensorBudgetState 'F0000101_0').Exists) 'Absent Enabled not restored'
        $k = $sandboxBase.OpenSubKey($sandboxPath + '\F0000101_0')
        try { Assert ($k.GetValue('Label') -eq 'owned test label') 'Label clobbered'; $child=$k.OpenSubKey('Temp0');try{Assert ($child.GetValue('Show') -eq 1) 'Child lost'}finally{$child.Dispose()} } finally { $k.Dispose() }
    }
    Check 'C-state group uses the same actual DWORD adapter without touching adjacent families' {
        Write-HWiNFOSensorBudgetState 'F0000302_31' ([pscustomobject]@{Exists=$true;Value=0})
        $value=Read-HWiNFOSensorBudgetState 'F0000302_31'
        Assert ($value.Exists -and $value.Value -eq 0) 'C-state DWORD not retained'
        $k=$sandboxBase.OpenSubKey($sandboxPath + '\F0000301_31')
        Assert ($null -eq $k) 'Adjacent system family was created'
    }
    Check 'non-DWORD Enabled is rejected and preserved' {
        $k=$sandboxBase.CreateSubKey($sandboxPath + '\F0000101_7')
        try{$k.SetValue('Enabled','legacy-string',[Microsoft.Win32.RegistryValueKind]::String)}finally{$k.Dispose()}
        $rejected=$false;try{Read-HWiNFOSensorBudgetState 'F0000101_7'|Out-Null}catch{$rejected=$true}
        Assert $rejected 'Wrong kind accepted'
        $k=$sandboxBase.OpenSubKey($sandboxPath + '\F0000101_7')
        try{Assert ($k.GetValue('Enabled') -eq 'legacy-string') 'Rejected original changed'}finally{$k.Dispose()}
    }
    Check 'only the single mirror instance round-trips and restores through the actual registry adapter' {
        $name = 'F00F5000_0'
        $old = Read-HWiNFOSensorBudgetState $name
        Assert (-not $old.Exists) 'Fixture mirror already exists'
        Write-HWiNFOSensorBudgetState $name ([pscustomobject]@{Exists=$true;Value=0})
        $v = Read-HWiNFOSensorBudgetState $name
        Assert ($v.Exists -and $v.Value -eq 0) 'Mirror DWORD zero lost'
        Write-HWiNFOSensorBudgetState $name $old
        Assert (-not (Read-HWiNFOSensorBudgetState $name).Exists) 'Mirror absence not restored'
    }
    Check 'unapproved FPS and GPU identities never reach registry access' {
        foreach($name in @('F0000FF0_0','F0000400_0','F0000101_32','F00F5000_1','F00F5000_00','f00f5000_0','F00F5001_0','..\Other')){
            $bad=$false;try{Write-HWiNFOSensorBudgetState $name ([pscustomobject]@{Exists=$true;Value=0})}catch{$bad=$true};Assert $bad 'Unapproved identity accepted'
        }
    }
} finally {
    # Exact GUID sandbox checked above. Never target the real HWiNFO key.
    $sandboxBase.DeleteSubKeyTree($sandboxPath, $false)
    $sandboxBase.Dispose()
}
$sourceHash = [Security.Cryptography.SHA256]::Create()
try { $sourceDigest = [BitConverter]::ToString($sourceHash.ComputeHash([IO.File]::ReadAllBytes($RecoveryScript))).Replace('-', '').ToLowerInvariant() } finally { $sourceHash.Dispose() }
[ordered]@{ok=$true;checks=$checks.Count;cases=@($checks.ToArray());productionSourceSha256=$sourceDigest;realRegistryApiExercised=$true;isolatedGuidRoot=$true;realHWiNFORegistryWrites=$false;cleanupComplete=$true;hardwareReads=$false} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $ResultPath -Encoding UTF8
Write-Output ('ISOLATED_REGISTRY ' + $checks.Count + ' checks')
