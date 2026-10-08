[CmdletBinding()]
param([string]$CaptureScript='G:\YeManCC-Work\Deliverables\ROG-CPU-TELEMETRY-CAPTURE-20261004.ps1',[Parameter(Mandatory=$true)][string]$OutDir)
$ErrorActionPreference='Stop'
if(Test-Path -LiteralPath $OutDir){throw 'NEW_OUTPUT_DIRECTORY_REQUIRED'}
New-Item -ItemType Directory -Path $OutDir|Out-Null
$errors=$null;$tokens=$null;$ast=[Management.Automation.Language.Parser]::ParseFile($CaptureScript,[ref]$tokens,[ref]$errors)
if(@($errors).Count){throw 'CAPTURE_PARSE_FAILED'}
# Import only top-level helper definitions; never run the capture dispatcher,
# product lifecycle or hardware APIs. The sole native probe is GetSystemTimes.
$functions=$ast.FindAll({param($node)$node -is [Management.Automation.Language.FunctionDefinitionAst]},$false)
foreach($fn in $functions){. ([scriptblock]::Create($fn.Extent.Text))}
$checks=New-Object System.Collections.Generic.List[object]
function Check([bool]$Pass,[string]$Name){$checks.Add([pscustomobject]@{name=$Name;pass=$Pass});if(-not $Pass){throw "ASSERTION_FAILED: $Name"}}
function Close([double]$A,[double]$B){return [math]::Abs($A-$B) -lt 0.000001}
function Fails([scriptblock]$Code){try{& $Code|Out-Null;return $false}catch{return $true}}
$latency=New-Object System.Collections.Generic.List[double]
$status='FAIL';$failure=$null
try{
 $b=[IO.File]::ReadAllBytes($CaptureScript);Check ($b[0]-eq239 -and $b[1]-eq187 -and $b[2]-eq191) 'capture-PS5-compatible-UTF8-BOM'
 $previous=@(100,200,300)
 $cases=@(
  @{current=@(400,500,300);expected=0;name='all-idle'},
  @{current=@(100,400,500);expected=100;name='all-busy'},
  @{current=@(700,800,500);expected=25;name='one-quarter-busy'},
  @{current=@(300,500,400);expected=50;name='kernel-includes-idle'}
 )
 foreach($c in $cases){$r=Get-SystemCpuInterval $previous $c.current;Check (Close $r.percent $c.expected) $c.name;Check ($r.intervalTicks.Count -eq 3) ($c.name+'-raw-deltas')}
 Check (Fails {Get-SystemCpuInterval $previous @(99,201,301)}) 'idle-counter-regression-rejected'
 Check (Fails {Get-SystemCpuInterval $previous @(100,199,301)}) 'kernel-counter-regression-rejected'
 Check (Fails {Get-SystemCpuInterval $previous @(100,201,299)}) 'user-counter-regression-rejected'
 Check (Fails {Get-SystemCpuInterval $previous $previous}) 'zero-span-rejected-not-filled-with-zero-percent'
 Check (Fails {Get-SystemCpuInterval $previous @(500,300,400)}) 'idle-greater-than-total-rejected'
 Check (Fails {Get-SystemCpuInterval @(0,0) @(1,1,1)}) 'counter-shape-rejected'
 $rng=New-Object Random 172910
 for($i=0;$i-lt512;$i++){$kernel=$rng.Next(1,100000);$idle=$rng.Next(0,$kernel+1);$user=$rng.Next(0,100000);$cur=@((100+$idle),(200+$kernel),(300+$user));$r=Get-SystemCpuInterval $previous $cur;Check (Close $r.percent (100.0*($kernel+$user-$idle)/($kernel+$user))) ('randomized-counter-arithmetic-'+$i)}
 $script:hostInfo=[pscustomobject]@{logicalProcessors=32}
 $script:systemCpuPrevious=$null
 $first=System-Cpu
 Check (-not $first.ok -and $null -eq $first.percent -and $first.error -eq 'baseline-required') 'first-native-read-is-baseline-not-zero-CPU'
 Check ($first.counterTicks.Count -eq 3) 'native-baseline-preserves-raw-counters'
 for($i=0;$i-lt15;$i++){
  Start-Sleep -Milliseconds 30;$sw=[Diagnostics.Stopwatch]::StartNew();$r=System-Cpu;$sw.Stop();$latency.Add($sw.Elapsed.TotalMilliseconds)
  Check ($r.ok -and $r.method -eq 'GetSystemTimes' -and $r.percent-ge0 -and $r.percent-le100) ('live-native-system-counter-'+$i)
  Check ($r.counterTicks.Count-eq3 -and $r.intervalTicks.Count-eq3) ('live-native-raw-counts-'+$i)
 }
 # This stub proves the topology guard without making a blocking WMI query.
 function Get-CimInstance {param($ClassName,$Filter,$ErrorAction) return [pscustomobject]@{PercentProcessorTime=17}}
 $script:hostInfo=[pscustomobject]@{logicalProcessors=128};$fallback=System-Cpu
 Check ($fallback.ok -and $fallback.percent-eq17 -and $fallback.method-like '*fallback*') 'more-than-64-logical-CPU-not-claimed-native-whole-machine'
 Remove-Item Function:Get-CimInstance
 $kinds=@(
  @{text='"msedgewebview2.exe" --embedded-browser-webview=1';kind='browser'},
  @{text='x --type=renderer';kind='renderer'},
  @{text='x --type="gpu-process"';kind='gpu-process'},
  @{text='x --type utility';kind='utility'},
  @{text='x --type=crashpad-handler';kind='crashpad-handler'},
  @{text='x --type=new-subtype';kind='other'},
  @{text='x --type=';kind='unknown'},
  @{text='';kind='unknown'}
 )
 foreach($c in $kinds){Check ((Get-WebView2Kind $c.text)-eq$c.kind) ('webview-subtype-'+$c.kind)}
 $absent=Metric @() 2 32 $true $false;Check ($null-eq$absent.cpuCores -and -not$absent.valid) 'unverified-absence-is-null-not-zero'
 $verified=Metric @() 2 32 $true $true;Check ($verified.cpuCores-eq0 -and $verified.valid -and $verified.verifiedAbsent) 'verified-absence-can-be-zero'
 $rows=@(
  [pscustomobject]@{pid=1;key='1|a';role='ymcc-native';cpuDeltaMs=500;firstCpuElapsedMs=0;lastCpuElapsedMs=1000},
  [pscustomobject]@{pid=2;key='2|b';role='webview2';cpuDeltaMs=250;firstCpuElapsedMs=0;lastCpuElapsedMs=1000;commandLine='x --type=renderer'},
  [pscustomobject]@{pid=3;key='3|c';role='webview2';cpuDeltaMs=125;firstCpuElapsedMs=0;lastCpuElapsedMs=1000;commandLine='x --type=gpu-process'}
 )
 $total=Metric $rows 1 32 $true $false
 Check (Close $total.cpuCores 0.875) 'unique-process-total-includes-native-and-all-WebView2-once'
 Check (Close $total.cpuPercentMachine 2.734375) 'machine-percentage-normalized-by-logical-processors'
 Check (Close $total.cpuPercentOneCore 87.5) 'one-core-percentage-distinct-from-machine-percentage'
 $bad=[pscustomobject]@{pid=4;cpuDeltaMs=$null;firstCpuElapsedMs=0;lastCpuElapsedMs=1000}
 $incomplete=Metric @($bad) 1 32 $false $false;Check ($null-eq$incomplete.cpuCores -and -not$incomplete.valid) 'unreadable-CPU-counter-not-filled-zero'
 $breakdown=Get-WebView2Breakdown $rows @() 1 32 $true $true
 Check ($breakdown.quality.countsMatchTotal -and $breakdown.quality.processCount-eq2) 'WebView2-subtypes-are-a-disjoint-subledger'
 Check (Close $breakdown.groups.renderer.cpuCores 0.25) 'renderer-subledger-value'
 Check (Close $breakdown.groups.'gpu-process'.cpuCores 0.125) 'GPU-subledger-value'
 $path=Join-Path $OutDir 'utf8-no-bom-roundtrip.json'
 Write-Utf8 $path ('{"title":"日志测试 处理器 引号 尾部"}')
 $parsed=Get-Content -LiteralPath $path -Raw -Encoding UTF8|ConvertFrom-Json
 Check ($parsed.title-eq'日志测试 处理器 引号 尾部') 'PS5-and-PS7-explicit-UTF8-JSON-roundtrip'
 Check ((Get-Content -LiteralPath $CaptureScript -Raw -Encoding UTF8)-match "reasons.Add\('insufficient-samples'\)") 'insufficient-sample-reason-is-explicit'
 $secrets=@(
  @{text='YeManFanHost.exe --confirm=secret-confirm --parent-pid 10';secret='secret-confirm'},
  @{text='x --session-token secret-session --confirm secret-session';secret='secret-session'},
  @{text='x --confirm "quoted secret" --type=renderer';secret='quoted secret'},
  @{text='x -confirm secret-short';secret='secret-short'},
  @{text='Authorization: Bearer secret-bearer';secret='secret-bearer'}
 )
 foreach($c in $secrets){$safe=Redact-Text $c.text;Check (-not $safe.Contains($c.secret) -and $safe.Contains('<redacted>')) ('CLI-secret-redacted-'+$c.secret)}
 $dict=Redact-EvidenceObject @{confirmationToken='sensitive-confirmation';targetPid=42;sessionId='public-session-id'}
 Check ($dict.confirmationToken-eq'<redacted>') 'dictionary-confirmation-token-redacted'
 Check ($dict.targetPid-eq42 -and $dict.sessionId-eq'public-session-id') 'redaction-preserves-PID-and-public-correlation-sessionId'
 $obj=Redact-EvidenceObject ([pscustomobject]@{confirm='sensitive-confirmation';tokenIsElevated=$true})
 Check ($obj.confirm-eq'<redacted>') 'PSObject-confirm-redacted'
 Check ($obj.tokenIsElevated-eq$true) 'redaction-preserves-elevation-evidence-boolean'
 $status='PASS'
}catch{$failure=$_.Exception.Message}
$result=[ordered]@{capturedAtUtc=[DateTime]::UtcNow.ToString('o');scope='collector helpers/read-only native system counter only; not integrated YMCC CPU';status=$status;powershell=$PSVersionTable.PSVersion.ToString();checks=@($checks.ToArray());nativeReadLatencyMs=@($latency.ToArray());failure=$failure;captureSha256=(Get-FileHash -LiteralPath $CaptureScript).Hash}
[IO.File]::WriteAllText((Join-Path $OutDir 'telemetry-selftest-results.json'),($result|ConvertTo-Json -Depth 12),[Text.UTF8Encoding]::new($false))
Write-Output "status=$status checks=$($checks.Count) result=$OutDir\telemetry-selftest-results.json"
if($status-ne'PASS'){Write-Error $failure;exit 1};exit 0