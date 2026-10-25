# Read-only deployment verification. Never installs, restarts or accesses charging hardware.
[CmdletBinding()]
param([string]$InstallRoot='C:\SOFT\YeMan',[string]$ExpectedReceipt='',[switch]$IsolatedPackage)
$ErrorActionPreference='Stop'
if(-not $ExpectedReceipt){$ExpectedReceipt=Join-Path $PSScriptRoot 'battery-runtime.expected.json'}
$expected=Get-Content -LiteralPath $ExpectedReceipt -Raw -Encoding UTF8 | ConvertFrom-Json
if($expected.schema -ne 1 -or $expected.protocol -ne 2 -or $expected.sourceId -notmatch '^[a-f0-9]{64}$'){throw 'Invalid battery delivery receipt'}
$root=[IO.Path]::GetFullPath($InstallRoot).TrimEnd('\')
$problems=New-Object 'Collections.Generic.List[string]'
foreach($file in $expected.files){
  $full=[IO.Path]::GetFullPath((Join-Path $root $file.path))
  if(-not $full.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Receipt path escapes installation root'}
  if(-not (Test-Path -LiteralPath $full -PathType Leaf)){$problems.Add('Missing: '+$file.path);continue}
  if((Get-FileHash -LiteralPath $full -Algorithm SHA256).Hash -ine $file.sha256){$problems.Add('Outdated or mixed: '+$file.path)}
}
# Do not execute an old binary: it does not recognize the identity-only switch.
if($problems.Count){throw ("充电修复尚未完整安装。退出 YMCC 后，将完整包的 YeManCC 和 PowerControl 两个文件夹同时合并到 $root，再运行本检查。`n"+($problems -join "`n"))}
function Read-Identity([string]$Exe,[string]$Arguments) {
  $start=New-Object Diagnostics.ProcessStartInfo
  $start.FileName=$Exe;$start.Arguments=$Arguments;$start.UseShellExecute=$false
  $start.CreateNoWindow=$true;$start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
  # The app manifest normally requests elevation. This exact, hash-verified mode
  # only prints identity, so run this child with the caller's existing privileges.
  $start.EnvironmentVariables['__COMPAT_LAYER']='RunAsInvoker'
  $start.StandardOutputEncoding=[Text.Encoding]::UTF8;$start.StandardErrorEncoding=[Text.Encoding]::UTF8
  if($IsolatedPackage){$start.EnvironmentVariables['YEMAN_POWER_CONTROL_DIR']=Join-Path $root 'PowerControl'}
  $process=[Diagnostics.Process]::Start($start)
  try {
    $stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync()
    if(-not $process.WaitForExit(10000)){$process.Kill();throw 'Identity-only command timed out'}
    if($process.ExitCode -ne 0){throw ('Identity-only command failed: '+$stderr.Result)}
    return ($stdout.Result | ConvertFrom-Json)
  } finally {$process.Dispose()}
}
$native=Read-Identity (Join-Path $root 'YeManCC\YeManCC.exe') '--battery-runtime-identity'
$hostInfo=Read-Identity (Join-Path $root 'PowerControl\battery-host\YeManBatteryHost.exe') '--identity'
foreach($identity in @($native,$hostInfo)){
  if($identity.protocol -ne $expected.protocol -or $identity.sourceId -cne $expected.sourceId){throw 'Loaded component contract differs from this delivery'}
}
$wantedPower=[IO.Path]::GetFullPath((Join-Path $root 'PowerControl')).TrimEnd('\')
if([IO.Path]::GetFullPath($native.powerRoot).TrimEnd('\') -ine $wantedPower){throw ("程序实际加载的 PowerControl 目录不同："+$native.powerRoot+"。请检查 YEMAN_POWER_CONTROL_DIR 或使用正式安装目录 C:\SOFT\YeMan。")}
$running=@(Get-Process -Name YeManCC -ErrorAction SilentlyContinue)
$restartNeeded=@($running | ForEach-Object {
  # A disk hash cannot prove a previously launched image has been replaced in memory.
  [ordered]@{pid=$_.Id;path=$_.Path;started=$_.StartTime;action='Exit and relaunch after verifying this delivery'}
})
[ordered]@{ok=$true;buildId=$expected.buildId;sourceId=$expected.sourceId;implementation=$native.implementation;
  nativePath=$native.nativePath;powerRoot=$native.powerRoot;helperVersion=$hostInfo.version;
  checkedFiles=@($expected.files).Count;hardwareAccessed=$false;runningInstances=$restartNeeded} | ConvertTo-Json -Depth 6
if($running.Count){Write-Warning '文件检查通过；已有 YMCC 进程仍需退出后重开，磁盘文件正确不代表旧进程已更新。'}
