[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$Out,[string]$ProcessQuery="",[uint32]$ExpectedPid=0,[uint32]$BackendPid=0,[uint32]$EditorPid=0,[switch]$SharedChannel)
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $PSScriptRoot
New-Item -ItemType Directory -Path $Out -Force|Out-Null
. (Join-Path $repo 'PowerControl\steam-appearance\resident.ps1')
$cases=New-Object Collections.Generic.List[string]
function Assert($Value,$Name){if(!$Value){throw $Name};$cases.Add($Name)}
function Reject([scriptblock]$Work){try{&$Work|Out-Null;return $false}catch{return $true}}
if($SharedChannel){
 if(!$ProcessQuery -or !$ExpectedPid){throw 'Explicit shared channel fixture identity required'}
 . (Join-Path $repo 'PowerControl\steam-appearance\idle.ps1')
 function Get-Process {throw 'PRIVATE_STEAM_PROCESS_LOOKUP_DENIED'}
 function Get-NetTCPConnection {throw 'PRIVATE_STEAM_PORT_LOOKUP_DENIED'}
 $SessionPid=$ExpectedPid;$SessionBirth=''
 $identity=Get-AppearanceSteamSession
 Assert ($identity.pid -eq $ExpectedPid -and $identity.processEpoch -match '^[1-9]\d*$' -and $identity.accountEpoch -match '^[1-9]\d*$') 'Actual PS consumer receives the same public Observer process and account epochs without opening Steam'
 Assert ((Get-AppearanceSteamSession).birth -ceq $identity.birth) 'A repeated request reads the current public facts instead of a private cached PID'
 Assert (Reject {Invoke-AppearanceSharedSteam @{operation='identity';javascript='unsafe'}}) 'A caller cannot smuggle JavaScript into the typed public read'
 Assert (Reject {Invoke-AppearanceSharedSteam @{operation='evaluate'}}) 'Unknown shared Steam operations are refused without a private fallback'
 $stale=[pscustomobject]@{pid=$identity.pid;birth=$identity.birth;root=$identity.root;processEpoch='999999';accountEpoch=$identity.accountEpoch}
 Assert (Reject {Invoke-AppearanceSharedSteam @{operation='documents';identity=$stale;plans=@{}}}) 'An obsolete process epoch cannot dispatch a document observation'
 $map=[IO.MemoryMappedFiles.MemoryMappedFile]::OpenExisting($ProcessQuery);$view=$map.CreateViewAccessor()
 try {
  $birth=$view.ReadUInt32(60);$view.Write(60,[uint32]($birth -bxor 1))
  Assert (Reject {Invoke-AppearanceSharedSteam @{operation='identity'}}) 'The cross-process client rejects a reused or changed native owner birth'
  $view.Write(60,$birth)
 }finally{$view.Dispose();$map.Dispose()}
 Assert ((Get-AppearanceSteamSession).pid -eq $ExpectedPid) 'A correctly restored owned fixture channel remains usable after a rejected request'
 [pscustomobject]@{ok=$true;passed=$cases.Count;cases=@($cases);realSteamOperations=0;realCssOperations=0;privateSteamProcessLookups=0;privateSteamPortLookups=0}|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $Out 'result.json') -Encoding UTF8
 Write-Output "PASS: $($cases.Count) actual public Steam channel consumer checks";exit 0
}
if($ProcessQuery){
 Initialize-ResidentAppearanceInterop
 function Get-Process {param($Id,$Name) if($Name -or !$Id){throw 'PRIVATE_PROCESS_ENUMERATION_DENIED'};Microsoft.PowerShell.Management\Get-Process -Id $Id -ErrorAction Stop}
 function Get-CimInstance {throw 'PRIVATE_CIM_ENUMERATION_DENIED'}
 $first=Update-AppearanceProcessInventory
 Assert ($first.steamPid -eq $ExpectedPid) 'Cross-process receipt carries the exact upstream host identity'
 Assert (@(Get-AppearanceProcessCandidates 1|Where-Object Id -eq $BackendPid).Count -eq 1) 'Shared filtered candidates include the owned backend with a checked birth'
 Assert (@(Get-AppearanceProcessCandidates 2|Where-Object Id -eq $EditorPid).Count -eq 1) 'Shared filtered candidates include the owned minimized-independent editor identity'
 Assert (Reject {Update-AppearanceProcessInventory}) 'A fresh unavailable native owner observation remains Unknown'
 Assert ($null -eq $script:AppearanceProcessReceipt) 'A failed fresh observation revokes previous absence/candidates'
 $null=Update-AppearanceProcessInventory
 $row=@($script:AppearanceProcessReceipt.rows|Where-Object pid -eq $BackendPid)[0];$row.birth=([long]$row.birth+1).ToString()
 Assert (Reject {Get-AppearanceProcessCandidates 1}) 'A reused PID cannot inherit a shared candidate receipt'
 $null=Update-AppearanceProcessInventory
 $editor=Get-AppearanceProcessCandidates 2|Where-Object Id -eq $EditorPid
 $parent=Get-AppearanceParentId $editor
 Assert ($parent -eq $ExpectedPid) 'Parent ancestry uses the pinned process directly without CIM discovery'
 [pscustomobject]@{ok=$true;passed=$cases.Count;cases=@($cases);realCssOperations=0;realSteamOperations=0;privateScans=0;ownedFixtureProcessesOnly=$true}|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $Out 'result.json') -Encoding UTF8
 Write-Output "PASS: $($cases.Count) real native-to-PS shared process checks";exit 0
}
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

# Exercise the production native listener reader with only owned ephemeral sockets.
$tcp4=New-Object Net.Sockets.TcpListener([Net.IPAddress]::Loopback,0)
$tcp6=New-Object Net.Sockets.TcpListener([Net.IPAddress]::IPv6Loopback,0)
try {
 $tcp4.Start();$tcp6.Server.DualMode=$false;$tcp6.Start()
 $port4=$tcp4.LocalEndpoint.Port;$port6=$tcp6.LocalEndpoint.Port
 $listeners4=@([Ymcc.CssResidentProcess]::Listeners($port4));$listeners6=@([Ymcc.CssResidentProcess]::Listeners($port6))
 Assert (@($listeners4|Where-Object {$_.LocalAddress -eq '127.0.0.1' -and $_.OwningProcess -eq $own.Id -and $_.LocalPort -eq $port4}).Count -eq 1) 'IPv4 native listener lookup returns the exact owned ephemeral socket and PID'
 Assert (@($listeners6|Where-Object {$_.LocalAddress -eq '::1' -and $_.OwningProcess -eq $own.Id -and $_.LocalPort -eq $port6}).Count -eq 1) 'IPv6 native listener lookup preserves the address and exact PID'
 $tcp4.Stop()
 Assert (@([Ymcc.CssResidentProcess]::Listeners($port4)|Where-Object OwningProcess -eq $own.Id).Count -eq 0) 'A closed socket disappears on the next query without a cached absence or owner'
 Assert (Reject {[Ymcc.CssResidentProcess]::Listeners(0)}) 'Invalid listener ports fail instead of returning an empty successful observation'
}finally{$tcp4.Stop();$tcp6.Stop()}
$decode=[Ymcc.CssResidentProcess].GetMethod('DecodeTcpListeners',[Reflection.BindingFlags]'NonPublic,Static')
function Read-FixtureListenerTable([byte[]]$Bytes,[int]$Family,[int]$Port) {
 $memory=[Runtime.InteropServices.Marshal]::AllocHGlobal($Bytes.Length)
 try {
  [Runtime.InteropServices.Marshal]::Copy($Bytes,0,$memory,$Bytes.Length)
  $decode.Invoke($null,[object[]]@($memory,[int]$Bytes.Length,$Family,$Port))
 }finally{[Runtime.InteropServices.Marshal]::FreeHGlobal($memory)}
}
$table4=New-Object byte[] 52;$table4[0]=2
foreach($offset in @(4,28)){$table4[$offset]=2;$table4[$offset+8]=0x8b;$table4[$offset+9]=0xed;$table4[$offset+20]=123}
$table4[8]=127;$table4[11]=1
$decoded=@(Read-FixtureListenerTable $table4 2 35821)
Assert ($decoded.Count -eq 2 -and $decoded[0].LocalAddress -eq '127.0.0.1' -and $decoded[1].LocalAddress -eq '0.0.0.0' -and $decoded[0].OwningProcess -eq 123) 'Native parser preserves duplicate and wildcard listeners for the original ownership checks'
Assert (@(Read-FixtureListenerTable $table4 2 35822).Count -eq 0) 'Port matching decodes the network byte order before filtering'
$table4[0]=3
Assert (Reject {Read-FixtureListenerTable $table4 2 35821}) 'A truncated native table fails closed instead of implying no listener'
$table4[0]=2;$table4[4]=5
Assert (Reject {Read-FixtureListenerTable $table4 2 35821}) 'A non-listening matching row cannot authorize an absent or valid backend'
$table6=New-Object byte[] 60;$table6[0]=1;$table6[19]=1;$table6[24]=0x8b;$table6[25]=0xed;$table6[52]=2;$table6[56]=124
$decoded=@(Read-FixtureListenerTable $table6 23 35821)
Assert ($decoded.Count -eq 1 -and $decoded[0].LocalAddress -eq '::1' -and $decoded[0].OwningProcess -eq 124) 'IPv6 table offsets are decoded independently from the IPv4 layout'
$table6[19]=0
Assert ((Read-FixtureListenerTable $table6 23 35821).LocalAddress -eq '::') 'IPv6 wildcard listeners remain visible for rejection by the caller'
Assert (Reject {Read-FixtureListenerTable $table6 99 35821}) 'Unknown native address families are rejected'

# Exercise exact main-controller functions with the live own process behind a
# blank .Path view, while ports/process enumeration remain memory-only.
$tokens=$null;$errors=$null;$ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $repo 'PowerControl\steam-appearance.ps1'),[ref]$tokens,[ref]$errors)
if($errors.Count){throw $errors[0]}
foreach($name in @('Get-BackendProcesses','Test-BackendOwner')){
 $fn=$ast.Find({param($n)$n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq $name},$true)
 Invoke-Expression $fn.Extent.Text
}
$script:BackendExe=$expected
function Get-AppearanceBackendCandidates {$limited}
function Get-Process {param($Name,$Id) if($Name -or !$Id){throw 'PRIVATE_PROCESS_ENUMERATION_DENIED'};$limited}
function Test-CurrentUserProcess($Process){$Process.Id -eq $own.Id}
function Get-AppearanceTcpListeners {param($LocalPort) [pscustomobject]@{LocalAddress='127.0.0.1';OwningProcess=$own.Id}}
Assert (@(Get-BackendProcesses).Count -eq 1) 'Controller backend enumeration recognizes a verified existing process with empty .NET Path'
Assert (Test-BackendOwner) 'Controller listener ownership resolves the same limited-query image without rejecting empty Path'
[pscustomobject]@{ok=$true;passed=$cases.Count;cases=@($cases);host64Bit=[Environment]::Is64BitProcess;powerShell=$PSVersionTable.PSVersion.ToString();handleDelta=($after-$before);realCssOperations=0;realSteamOperations=0;testedProcess='own PowerShell process only'}|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $Out 'result.json') -Encoding UTF8
Write-Output "PASS: $($cases.Count) limited process identity checks"
