[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$EvidenceRoot,[string]$NativeSource,[switch]$SharedPowerOnly,[switch]$ReceiptBoundaryOnly)
if($SharedPowerOnly -and $ReceiptBoundaryOnly){throw 'Select one original HTTP subset'}
$ErrorActionPreference='Stop'
$fanProject=Split-Path -Parent $PSScriptRoot
if(-not $NativeSource){$NativeSource=Join-Path $fanProject 'native\main.cpp'}
$fanSourceHash=(Get-FileHash -LiteralPath $NativeSource).Hash
$fanHeaderHashes=@{}
foreach($fanHeaderName in @('http_request_deadline.h','fan_response_contract.h','fan_http_body_contract.h','sleep_power_state.h','sleep_power_observation.h','sleep_power_revocation.h','sleep_power_remote_provider.h','sleep_power_remote_transport.h','log_snapshot_state.h')){$fanHeaderHashes[$fanHeaderName]=(Get-FileHash -LiteralPath (Join-Path $fanProject ('native\'+$fanHeaderName))).Hash}
$fanRaw=[IO.File]::ReadAllText($NativeSource).Replace("`r`n","`n")
$fanStart=$fanRaw.IndexOf('static bool crackHttpUrl(',[StringComparison]::Ordinal)
$fanRegStart=$fanRaw.IndexOf('static void reg_http() {',$fanStart,[StringComparison]::Ordinal)
$fanEnd=$fanRaw.IndexOf("`n}`n",$fanRegStart,[StringComparison]::Ordinal)+3
if($fanStart -lt 0 -or $fanEnd -le $fanStart){throw 'HTTP production boundary missing'}
$fanStartObserver=$fanRaw.LastIndexOf('static void fanLeaseKeepaliveObserveHttp(const std::string& method,',[StringComparison]::Ordinal)
$fanEndObserver=$fanRaw.IndexOf("`nstatic void fanLeaseKeepaliveShutdown()",$fanStartObserver,[StringComparison]::Ordinal)
if($fanStartObserver -lt 0 -or $fanEndObserver -le $fanStartObserver){throw 'Fan observer production boundary missing'}
$fanExtract=$fanRaw.Substring($fanStart,$fanEnd-$fanStart)+"`n"+$fanRaw.Substring($fanStartObserver,$fanEndObserver-$fanStartObserver)
$fanOriginalExtract=$fanExtract
$fanReceiver=''
if($fanRaw.Contains('if (cmd == "http.request") fanHttpCapturePowerAdmission(args);')){
    $receiverStart=$fanRaw.IndexOf('        if (cmd == "http.request") fanHttpCapturePowerAdmission(args);',[StringComparison]::Ordinal)
    $receiverEnd=$fanRaw.IndexOf("`n    } catch (...) {}",$receiverStart,[StringComparison]::Ordinal)
    if($receiverStart -lt 0 -or $receiverEnd -le $receiverStart){throw 'Actual receiver dispatch fragment missing'}
    $fanReceiver=$fanRaw.Substring($receiverStart,$receiverEnd-$receiverStart)
    if(-not $fanReceiver.Contains('if (ipc_cmd_async(cmd)) {') -or -not $fanReceiver.Contains('it->second(args)')){throw 'Both original receiver routes required'}
}

function ExtractFanFunction([string]$signature) {
    $start=$fanRaw.IndexOf($signature,[StringComparison]::Ordinal)
    if($start -lt 0){throw ('Missing current Fan metadata helper: '+$signature)}
    $open=$fanRaw.IndexOf('{',$start);$depth=0
    for($i=$open;$i -lt $fanRaw.Length;$i++){
        if($fanRaw[$i] -eq '{'){$depth++}
        if($fanRaw[$i] -eq '}'){$depth--;if($depth -eq 0){return $fanRaw.Substring($start,$i-$start+1)}}
    }
    throw 'Unbalanced metadata helper'
}
$fanMetadata=(ExtractFanFunction 'static std::string fanLeaseKeepaliveRendererModeBody(')+"`n"+(ExtractFanFunction 'static void fanLeaseKeepaliveAddRendererMode(std::string& body, const json& headers, ULONGLONG receiptEpoch) noexcept {')
$fanHeaderProbe='        std::wstring allHeaders;'
if($fanExtract.Split($fanHeaderProbe).Count -ne 2){throw 'Header work probe boundary changed'}
$fanExtract=$fanExtract.Replace($fanHeaderProbe,"        fixtureObserveHeaderStorage(hdrs, a);`n"+$fanHeaderProbe)
$fanResponseLine=[regex]::Match($fanExtract,'(?m)^        json response\{\{"status", statusCode\},[^\n]+;$').Value
if(-not $fanResponseLine){throw 'Response work probe boundary changed'}
$fanExtract=$fanExtract.Replace($fanResponseLine,"        const char* fixtureBodyStorage = respBody.data();`n        const char* fixtureHeaderStorage = rawResponseHeaders.data();`n"+$fanResponseLine+"`n        fixtureObserveResponseStorage(response, fixtureBodyStorage, fixtureHeaderStorage);")
$fanExtract=$fanMetadata+"`n"+$fanExtract
$bodyRead='try { respBody = request.readAll(8u << 20); }'
if($fanExtract.Split($bodyRead).Count -ne 2){throw 'Actual final body boundary missing'}
$fanExtract=$fanExtract.Replace($bodyRead,'try { respBody = request.readAll(8u << 20); fixtureAfterBody(); }')
if($fanReceiver){$fanExtract+="`nstatic void fixtureReceive(json args) {`n const std::string cmd=`"http.request`";json resp=json::object();const auto generation=1ULL;const int targetChildId=0;const bool traceRenderHandshake=false;auto it=fixtureHandlers.find(cmd);`n"+$fanReceiver+"`n}`n"}

$fanExtract=$fanExtract.Replace('uc.nPort == 8765','uc.nPort == fixturePort').Replace('port != 8765','port != fixturePort').Replace('components.nPort!=8765','components.nPort!=fixturePort')
$fanEvidence=[IO.Path]::GetFullPath($EvidenceRoot)
New-Item -ItemType Directory -Path $fanEvidence -Force | Out-Null
[IO.File]::WriteAllText((Join-Path $fanEvidence 'HttpExtract.inc'),$fanExtract,[Text.UTF8Encoding]::new($false))
$fanSharedDefine=if($fanRaw.Contains('static void fanHttpCapturePowerAdmission(json& args) {')){'/DYMCC_FAN_SHARED_HTTP_CAPTURE /DYMCC_FAN_RECEIVER_FRAGMENT'}else{''}
$fanBuild=Join-Path $fanEvidence 'compile.cmd'
@"
@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
if not defined WindowsSdkDir set "WindowsSdkDir=C:\Program Files (x86)\Windows Kits\10\"
if "%WindowsSDKVersion%"=="\" set "WindowsSDKVersion=10.0.26100.0\"
if not defined WindowsSDKVersion set "WindowsSDKVersion=10.0.26100.0\"
set "INCLUDE=%WindowsSdkDir%Include\%WindowsSDKVersion%um;%WindowsSdkDir%Include\%WindowsSDKVersion%shared;%WindowsSdkDir%Include\%WindowsSDKVersion%ucrt;%INCLUDE%"
set "LIB=%WindowsSdkDir%Lib\%WindowsSDKVersion%um\x64;%WindowsSdkDir%Lib\%WindowsSDKVersion%ucrt\x64;%LIB%"
cl /nologo /W4 /WX $fanSharedDefine /std:c++20 /EHsc /utf-8 /MT /I"$fanProject\native" /I"$fanProject\deps\json" /I"$fanEvidence" "$PSScriptRoot\fan_renderer_http_transport_selftest.cpp" /Fo"$fanEvidence\fixture.obj" /Fe"$fanEvidence\fixture.exe"
"@ | Set-Content -LiteralPath $fanBuild -Encoding ascii
& $fanBuild *> (Join-Path $fanEvidence 'compile.txt')
if($LASTEXITCODE -ne 0){Get-Content -LiteralPath (Join-Path $fanEvidence 'compile.txt') -Tail 12;throw 'HTTP fixture compile failed'}
@{source=$fanSourceHash; extractSha256=[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes($fanOriginalExtract))); headers=$fanHeaderHashes; metadataSha256=[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes($fanMetadata))); receiverSha256=if($fanReceiver){[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes($fanReceiver)))}else{''}; replacements='Fan port/passive storage/body-completion hook; actual receiver async/sync dispatch with fixture queue/mailbox and exact capture; actual reg_http/crackHttpUrl/observer plus current metadata helpers (revision0 fixture); fake power/activity/display/AI hooks; real WinHTTP; no Host/hardware/CPU benchmark'} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $fanEvidence 'source-binding.json') -Encoding utf8
node (Join-Path $PSScriptRoot 'fan_renderer_http_transport_selftest.mjs') $fanEvidence @($(if($SharedPowerOnly){'--shared-power-only'}elseif($ReceiptBoundaryOnly){'--receipt-boundary-only'}))
if($LASTEXITCODE -ne 0){throw 'Renderer HTTP receipt checks failed'}
if((Get-FileHash -LiteralPath $NativeSource).Hash -ne $fanSourceHash){throw 'Native source drifted while validating'}
foreach($fanHeaderName in $fanHeaderHashes.Keys){if((Get-FileHash -LiteralPath (Join-Path $fanProject ('native\'+$fanHeaderName))).Hash -ne $fanHeaderHashes[$fanHeaderName]){throw ('Header drifted: '+$fanHeaderName)}}
