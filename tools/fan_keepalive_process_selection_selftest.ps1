param([string]$EvidenceRoot,[string]$NativeSource,[string]$SelectionHeader)
$ErrorActionPreference='Stop'
$project=Split-Path -Parent $PSScriptRoot
if(-not$NativeSource){$NativeSource=Join-Path $project 'native\main.cpp'}
if(-not$EvidenceRoot){$workspace=[IO.Path]::GetFullPath((Join-Path $project '..\..\..'));$EvidenceRoot=Join-Path $workspace ('_scratch\fan-keepalive-selection-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))}
New-Item -ItemType Directory -Force -Path $EvidenceRoot|Out-Null
$raw=[IO.File]::ReadAllText($NativeSource)
$sourceSha=(Get-FileHash -LiteralPath $NativeSource).Hash
$header=if($SelectionHeader){[IO.Path]::GetFullPath($SelectionHeader)}else{Join-Path $project 'native\fan_keepalive_process_selection.h'}
$headerSha=(Get-FileHash -LiteralPath $header).Hash
Copy-Item -LiteralPath $header -Destination (Join-Path $EvidenceRoot 'fan_keepalive_process_selection.h') -Force
if((Get-FileHash -LiteralPath (Join-Path $EvidenceRoot 'fan_keepalive_process_selection.h')).Hash -ne $headerSha){throw 'Copied selection header identity mismatch'}
function Extract([string]$signature){$start=$raw.IndexOf($signature,[StringComparison]::Ordinal);if($start-lt0){throw "Missing production function: $signature"};$open=$raw.IndexOf('{',$start);$depth=0;for($i=$open;$i-lt$raw.Length;$i++){if($raw[$i]-eq'{'){$depth++};if($raw[$i]-eq'}'){$depth--;if($depth-eq0){return $raw.Substring($start,$i-$start+1)}}};throw 'Unbalanced production function'}
$probe=Extract 'static FanHostIdentityProbe fanHostIdentityProbe() {'
$legacy=Extract 'static bool fanHostExactProcessRunning() {'
$hasSelection=$raw.Contains('static bool fanHostExactProcessRunningForKeepalive() {')
$selected=if($hasSelection){Extract 'static bool fanHostExactProcessRunningForKeepalive() {'}else{'static bool fanHostExactProcessRunningForKeepalive() { return fanHostExactProcessRunning(); }'}
foreach($entry in @(@('ProbeExtract.inc',$probe),@('LegacyExactExtract.inc',$legacy),@('KeepaliveExactExtract.inc',$selected))){[IO.File]::WriteAllText((Join-Path $EvidenceRoot $entry[0]),$entry[1],[Text.UTF8Encoding]::new($false))}
if($hasSelection){
 $post=Extract 'static bool fanLeaseKeepalivePostHeartbeat('
 if(-not$post.Contains('if (!fanHostExactProcessRunningForKeepalive())')){throw 'Warm probe disconnected from actual keepalive transport'}
 $worker=Extract 'static void fanLeaseKeepaliveThreadProc()'
 if(-not$worker.Contains('if (!g_fanLeaseKeepaliveArmed.load(std::memory_order_acquire)) fanKeepaliveProcessSelection().reset();')){throw 'Idle handle cleanup disconnected'}
 if(-not$worker.Contains("    fanKeepaliveProcessSelection().reset();`r`n}")){throw 'Worker-exit handle cleanup disconnected'}
}
$compile=Join-Path $EvidenceRoot 'compile.cmd'
@"
@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /WX /utf-8 /MT /I"$EvidenceRoot" /I"$project\native" /I"$project\deps\json" "$PSScriptRoot\fan_keepalive_process_selection_selftest.cpp" /Fo"$EvidenceRoot\fixture.obj" /Fe"$EvidenceRoot\fixture.exe"
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /WX /utf-8 /MT /DYMCC_FAULT_INJECT /I"$EvidenceRoot" /I"$project\native" /I"$project\deps\json" "$PSScriptRoot\fan_keepalive_process_selection_selftest.cpp" /Fo"$EvidenceRoot\fault.obj" /Fe"$EvidenceRoot\fault.exe"
"@ | Set-Content -LiteralPath $compile -Encoding ASCII
& $compile *> (Join-Path $EvidenceRoot 'compile.txt')
if($LASTEXITCODE-ne0){Get-Content -LiteralPath (Join-Path $EvidenceRoot 'compile.txt') -Tail 18;throw 'Selection test failed to compile; no behavioral conclusion'}
& (Join-Path $EvidenceRoot 'fixture.exe') *> (Join-Path $EvidenceRoot 'checks.txt')
$code=$LASTEXITCODE
& (Join-Path $EvidenceRoot 'fault.exe') *> (Join-Path $EvidenceRoot 'fault-checks.txt')
$faultCode=$LASTEXITCODE
if((Get-FileHash -LiteralPath $NativeSource).Hash-ne$sourceSha-or(Get-FileHash -LiteralPath $header).Hash-ne$headerSha){throw 'Source changed during verification'}
[pscustomobject]@{sourceSha256=$sourceSha;headerSha256=$headerSha;hasSelection=$hasSelection;exit=$code;faultExit=$faultCode;scope='actual extracted native inventory/warm-adapter and selection header; fixture discovery; real inert child kernel lifetime'}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $EvidenceRoot 'binding.json') -Encoding UTF8
Get-Content -LiteralPath (Join-Path $EvidenceRoot 'checks.txt')
Get-Content -LiteralPath (Join-Path $EvidenceRoot 'fault-checks.txt')
if($code-ne0-or$faultCode-ne0){throw 'Selection behavior failed'}