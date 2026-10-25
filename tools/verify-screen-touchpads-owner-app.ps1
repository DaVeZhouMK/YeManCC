[CmdletBinding()]
param([string]$ValidationRoot)
$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if(!$ValidationRoot){$ValidationRoot=[IO.Path]::GetFullPath((Join-Path $project '..\..\..\Build\Validation\ScreenMousePass-Goal\native-owner'))}
$receiptPath=Join-Path $ValidationRoot 'owner-app-verification.json'
$receipt=[ordered]@{ok=$false;featureAcceptancePassed=$false;productDeployed=$false;normalProductRunStarted=$false;driverOrInputHostStarted=$false;startedUtc=[DateTime]::UtcNow.ToString('o');runs=@()}
function Save-Receipt{$receipt|ConvertTo-Json -Depth 14|Set-Content -LiteralPath $receiptPath -Encoding UTF8}
Save-Receipt
try{
    $sourcePin=Join-Path $ValidationRoot 'candidate-snapshot-v2\source-pin.json';$pin=Get-Content -LiteralPath $sourcePin -Raw -Encoding UTF8|ConvertFrom-Json
    foreach($entry in $pin.files){$stored=Join-Path (Join-Path $ValidationRoot 'candidate-snapshot-v2\source') $entry.path;if((Get-FileHash -LiteralPath $stored).Hash -ne $entry.sha256){throw "Immutable source pin mismatch: $($entry.path)"}}
    $receipt.sourcePinPath=$sourcePin;$receipt.sourcePinSha256=(Get-FileHash -LiteralPath $sourcePin).Hash
    foreach($kind in @('candidate','legacy')){
        $native=Join-Path $ValidationRoot "standard-build-v2-$kind\Build\App\Native";$exe=Join-Path $native 'YeManCC.exe'
        if(!(Test-Path -LiteralPath $exe)){throw "Missing standard native recipe artifact: $exe"}
        $receipt["${kind}ExeSha256"]=(Get-FileHash -LiteralPath $exe).Hash
        foreach($variant in @('default','opt-in','conflict','explicit-legacy','core','wire','pointer')){
            $flag=if($variant -eq 'core'){'--screen-touchpads-selftest'}elseif($variant -eq 'wire'){'--screen-touchpads-wire-selftest'}elseif($variant -eq 'pointer'){'--screen-touchpads-pointer-selftest'}else{'--screen-touchpads-owner-selftest'}
            $extra=if($variant -eq 'opt-in'){' --screen-touchpads-native-input-candidate'}elseif($variant -eq 'conflict'){' --screen-touchpads-native-input-candidate --screen-touchpads-native-input-legacy'}elseif($variant -eq 'explicit-legacy'){' --screen-touchpads-native-input-legacy'}else{''}
            $reportName=if($variant -eq 'core'){'screen-touchpads-selftest.json'}elseif($variant -eq 'wire'){'screen-touchpads-wire-selftest.json'}elseif($variant -eq 'pointer'){'screen-touchpads-pointer-selftest.json'}else{'screen-touchpads-owner-selftest.json'}
            $path=Join-Path $native $reportName;if(Test-Path -LiteralPath $path){Remove-Item -LiteralPath $path}
            $si=[Diagnostics.ProcessStartInfo]::new();$si.FileName=$exe;$si.Arguments=$flag+$extra;$si.WorkingDirectory=$native;$si.UseShellExecute=$false;$si.CreateNoWindow=$true;$si.WindowStyle=[Diagnostics.ProcessWindowStyle]::Hidden
            $si.EnvironmentVariables['__COMPAT_LAYER']='RunAsInvoker' # Only early-return driver-free tests; manifest stays intact.
            $p=[Diagnostics.Process]::Start($si);if(!$p.WaitForExit(30000)){throw "App selftest still live PID=$($p.Id) ($kind/$variant); inspect SAME process, do not restart"}
            if(!(Test-Path -LiteralPath $path)){throw "No fresh app selftest report ($kind/$variant)"}
            $r=Get-Content -LiteralPath $path -Raw -Encoding UTF8|ConvertFrom-Json
            if($p.ExitCode -ne 0 -or !$r.ok){throw "Actual main EXE selftest failed ($kind/$variant) exit=$($p.ExitCode)"}
            if($variant -notin @('core','wire','pointer')){
                $compiled=$kind -eq 'candidate';$expected=$compiled -and $variant -eq 'opt-in'
                if([bool]$r.startup.compiled -ne $compiled -or [bool]$r.startup.active -ne $expected -or [bool]$r.restartStartup.active -ne $expected -or $r.deviceOperations -ne 0 -or $r.configurationWrites -ne 0 -or $r.systemInputInjected){throw "Actual EXE owner startup policy mismatch ($kind/$variant)"}
            }
            $saved=Join-Path $ValidationRoot "app-$kind-$variant.json";Copy-Item -LiteralPath $path -Destination $saved -Force
            $receipt.runs+=@([ordered]@{kind=$kind;variant=$variant;arguments=$si.Arguments;ok=$true;exit=$p.ExitCode;report=$saved;reportSha256=(Get-FileHash -LiteralPath $saved).Hash;cases=if($r.cases){@($r.cases).Count}else{0}})
            $p.Dispose();Write-Output "ACTUAL_APP_OWNER_SELFTEST_PASS $kind/$variant"
        }
    }
    $receipt.ok=$true;$receipt.finishedUtc=[DateTime]::UtcNow.ToString('o')
}catch{$receipt.error=$_.Exception.Message;throw}
finally{Save-Receipt}