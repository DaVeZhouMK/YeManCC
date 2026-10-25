[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$Out)
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $PSScriptRoot
New-Item -ItemType Directory -Path $Out -Force|Out-Null
. (Join-Path $repo 'PowerControl\steam-appearance\resident.ps1')
$cases=New-Object Collections.Generic.List[string]
function Assert($Value,$Name){if(!$Value){throw $Name};$cases.Add($Name)}
function Reject([scriptblock]$Work){try{&$Work|Out-Null;return $false}catch{return $true}}
$own=[Diagnostics.Process]::GetCurrentProcess()
$expected=$own.MainModule.FileName;$born=$own.StartTime.ToUniversalTime().ToFileTimeUtc()
$limited=[pscustomobject]@{Id=$own.Id;StartTime=$own.StartTime;Path=$null}
$actual=Get-AppearanceProcessPath $limited
Assert ([IO.Path]::GetFullPath($actual) -ieq [IO.Path]::GetFullPath($expected)) 'Empty .NET Path resolves through the exact production limited-query function'
$denied=New-Object PSObject -Property @{Id=$own.Id;StartTime=$own.StartTime}
$denied|Add-Member -MemberType ScriptProperty -Name Path -Value {throw 'fixture MainModule access denied'}
Assert ((Get-AppearanceProcessPath $denied) -ieq $actual) 'A throwing MainModule/Path getter uses the same limited-query fallback'
$fast=[pscustomobject]@{Path=$expected;Id=0;StartTime=$null}
Assert ((Get-AppearanceProcessPath $fast) -ieq $expected) 'Readable paths need no additional process query or identity discovery'
Assert (Reject {[Ymcc.CssResidentProcess]::ReadPath([uint32]$own.Id,([long]$born+1))}) 'Reused PID or changed process birth is refused on the pinned query handle'
Assert (Reject {[Ymcc.CssResidentProcess]::ReadPath([uint32]0,[long]$born)}) 'Inaccessible or absent process is refused without a fallback launch'
Assert (Reject {Get-AppearanceProcessPath $null}) 'Missing process cannot yield a fabricated path'
$before=$own.HandleCount
for($iteration=0;$iteration -lt 100;$iteration++){$null=Get-AppearanceProcessPath $limited}
$own.Refresh();$after=$own.HandleCount
Assert ($after -le $before+2) 'One hundred path fallbacks close every native observation handle'
$source=[IO.File]::ReadAllText((Join-Path $repo 'PowerControl\steam-appearance\resident.ps1'))
Assert ($source -match 'OpenProcess\(0x1000,false,pid\)' -and $source -match 'birth!=expectedBirth') 'Query path requires limited read rights and compares creation time before reading the image'

# Exercise exact main-controller functions with the live own process behind a
# blank .Path view, while ports/process enumeration remain memory-only.
$tokens=$null;$errors=$null;$ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $repo 'PowerControl\steam-appearance.ps1'),[ref]$tokens,[ref]$errors)
if($errors.Count){throw $errors[0]}
foreach($name in @('Get-BackendProcesses','Test-BackendOwner')){
 $fn=$ast.Find({param($n)$n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq $name},$true)
 Invoke-Expression $fn.Extent.Text
}
$script:BackendExe=$expected
function Get-Process {param($Name,$Id) $limited}
function Test-CurrentUserProcess($Process){$Process.Id -eq $own.Id}
function Get-NetTCPConnection {param($State,$LocalPort) [pscustomobject]@{LocalAddress='127.0.0.1';OwningProcess=$own.Id}}
Assert (@(Get-BackendProcesses).Count -eq 1) 'Controller backend enumeration recognizes a verified existing process with empty .NET Path'
Assert (Test-BackendOwner) 'Controller listener ownership resolves the same limited-query image without rejecting empty Path'
[pscustomobject]@{ok=$true;passed=$cases.Count;cases=@($cases);host64Bit=[Environment]::Is64BitProcess;powerShell=$PSVersionTable.PSVersion.ToString();handleDelta=($after-$before);realCssOperations=0;realSteamOperations=0;testedProcess='own PowerShell process only'}|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $Out 'result.json') -Encoding UTF8
Write-Output "PASS: $($cases.Count) limited process identity checks"
