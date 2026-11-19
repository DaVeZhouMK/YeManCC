param([Parameter(Mandatory=$true)][string]$Node,[Parameter(Mandatory=$true)][string]$Worker,[Parameter(Mandatory=$true)][string]$Root)
$ErrorActionPreference='Stop'
$workerNodeInfo=[Diagnostics.ProcessStartInfo]::new()
$workerNodeInfo.FileName=$Node;$workerNodeInfo.UseShellExecute=$false;$workerNodeInfo.CreateNoWindow=$true
$workerNodeInfo.RedirectStandardOutput=$true;$workerNodeInfo.RedirectStandardError=$true
$workerNodeInfo.StandardOutputEncoding=[Text.Encoding]::UTF8;$workerNodeInfo.StandardErrorEncoding=[Text.Encoding]::UTF8
$workerNodeInfo.WorkingDirectory=(Get-Location).ProviderPath
$workerNodeInfo.ArgumentList.Add($Worker);$workerNodeInfo.ArgumentList.Add($Root)
$workerNodeProcess=[Diagnostics.Process]::Start($workerNodeInfo)
try {
 $workerNodeOutput=$workerNodeProcess.StandardOutput.ReadToEndAsync();$workerNodeError=$workerNodeProcess.StandardError.ReadToEndAsync()
 $workerNodeProcess.WaitForExit();$workerNodeExit=$workerNodeProcess.ExitCode
 [Console]::Out.Write($workerNodeOutput.GetAwaiter().GetResult());[Console]::Error.Write($workerNodeError.GetAwaiter().GetResult())
} finally {$workerNodeProcess.Dispose()}
exit $workerNodeExit
