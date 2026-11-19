param([Parameter(Mandatory=$true)][string]$Node,[Parameter(Mandatory=$true)][string]$Worker,[Parameter(Mandatory=$true)][string]$Root)
$ErrorActionPreference='Stop'
# Wait explicitly for the owned Node worker even when there is no console.
$info=[Diagnostics.ProcessStartInfo]::new($Node)
$info.UseShellExecute=$false
$info.CreateNoWindow=$true
$info.RedirectStandardOutput=$true
$info.RedirectStandardError=$true
$info.WorkingDirectory=(Get-Location).ProviderPath
foreach($arg in @($Worker,$Root)){$info.ArgumentList.Add($arg)}
$process=[Diagnostics.Process]::Start($info)
try {
 $stdout=$process.StandardOutput.BaseStream.CopyToAsync([Console]::OpenStandardOutput())
 $stderr=$process.StandardError.BaseStream.CopyToAsync([Console]::OpenStandardError())
 $process.WaitForExit()
 [void]$stdout.GetAwaiter().GetResult()
 [void]$stderr.GetAwaiter().GetResult()
 $code=$process.ExitCode
} finally {$process.Dispose()}
exit $code
