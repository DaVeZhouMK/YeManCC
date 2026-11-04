# Compatibility entry for the retired timeout/kill policy. Exercise resident verification.
[CmdletBinding()]
param([Alias('Out')][string]$OutputDirectory='G:/YeManCC-Work/Testing/YMCC/Evidence/SteamAppearanceResidentCompatibility',
      [switch]$ConfigStorm,[string]$ComponentRoot='',[string]$Case='')
$ErrorActionPreference='Stop'
if($ConfigStorm){
 $OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
 if([IO.Directory]::Exists($OutputDirectory) -or [IO.File]::Exists($OutputDirectory)){throw 'FRESH_OWNED_EVIDENCE_REQUIRED'}
 [IO.Directory]::CreateDirectory($OutputDirectory)|Out-Null
 if(!$ComponentRoot){$ComponentRoot=Join-Path (Split-Path -Parent $PSScriptRoot) 'PowerControl'}
 $source=Join-Path ([IO.Path]::GetFullPath($ComponentRoot)) 'steam-appearance/idle.ps1'
 function Get-IdleHash([byte[]]$Bytes){$h=[Security.Cryptography.SHA256]::Create();try{([BitConverter]::ToString($h.ComputeHash($Bytes))).Replace('-','').ToLowerInvariant()}finally{$h.Dispose()}}
 $sourceBefore=[IO.File]::ReadAllBytes($source);$consumerBefore=[IO.File]::ReadAllBytes($PSCommandPath)
 $tokens=$null;$errors=$null;$ast=[Management.Automation.Language.Parser]::ParseFile($source,[ref]$tokens,[ref]$errors)
 if($errors.Count){throw 'IDLE_SOURCE_PARSE_FAILED'}
 $required=@('Assert-AppearanceIdlePath','Read-AppearanceIdle','Test-AppearanceIdleSession','Clear-AppearanceIdle')
 $selected=@($ast.EndBlock.Statements|Where-Object {$_ -is [Management.Automation.Language.FunctionDefinitionAst] -and ($_.Name -in $required -or $_.Name -like '*AppearanceIdleJson*' -or $_.Name -like '*AppearanceIdleSnapshot*' -or $_.Name -like '*AppearanceIdleRecord*' -or $_.Name -like '*AppearanceIdleBytes*')})
 foreach($name in $required){if(@($selected|Where-Object Name -eq $name).Count -ne 1){throw 'EXACT_IDLE_DECLARATION_MISSING'}}
 $functionPins=@($selected|ForEach-Object {@{name=$_.Name;start=$_.Extent.StartOffset;end=$_.Extent.EndOffset;sha256=(Get-IdleHash ([Text.Encoding]::UTF8.GetBytes($_.Extent.Text)))}})
 $compiled=($selected|ForEach-Object {$_.Extent.Text}) -join "`n"
 if($compiled -match 'Get-Process|Get-CimInstance|Invoke-RestMethod|Start-Process|Get-AppearanceStatus'){throw 'IDLE_DATA_SELECTION_ESCAPED'}
 [IO.File]::WriteAllText((Join-Path $OutputDirectory 'actual-idle-functions.ps1'),$compiled,[Text.UTF8Encoding]::new($false))
 foreach($definition in $selected){Invoke-Expression $definition.Extent.Text}
 $script:IdleResults=New-Object Collections.Generic.List[object]
 $script:IdleFault='';$script:CaseRoot='';$script:IdleCalls=@();$script:HeldIdle=$null
 function Assert-IdleOwned([string]$Path,[switch]$Metadata){
  $p=[IO.Path]::GetFullPath($Path);$root=$script:CaseRoot
  if(!$p.StartsWith($root+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase) -and $p -ne $root){
   if(!$Metadata -or !$root.StartsWith($p.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)){throw ('OUTSIDE_OWNED_IDLE_PATH:'+ $p)}
  }
  return $p
 }
 function Test-Path {param([string]$LiteralPath,[string]$PathType)
  $p=Assert-IdleOwned $LiteralPath -Metadata
  if($PathType -eq 'Leaf'){return [IO.File]::Exists($p)}
  [IO.File]::Exists($p) -or [IO.Directory]::Exists($p)
 }
 function Get-Item {param([string]$LiteralPath,[switch]$Force)
  $p=Assert-IdleOwned $LiteralPath -Metadata
  $item=if([IO.Directory]::Exists($p)){[IO.DirectoryInfo]::new($p)}else{[IO.FileInfo]::new($p)}
  if($script:IdleFault -eq 'size-lie' -and $p -eq $script:AppearanceIdleFile){return [pscustomobject]@{Length=0;Attributes=$item.Attributes;FullName=$p}}
  $item
 }
 function Remove-Item {param([string]$LiteralPath,[switch]$Force)
  $p=Assert-IdleOwned $LiteralPath
  $script:IdleCalls+=@{op='remove';path=$p;fault=$script:IdleFault}
  if($script:IdleFault -eq 'remove-denied'){throw [IO.IOException]::new('owned injected remove denial')}
  if($script:IdleFault -eq 'remove-silent'){return}
  [IO.File]::Delete($p)
 }
 function Get-Process {throw 'REAL_PROCESS_FORBIDDEN'}
 function Invoke-RestMethod {throw 'REAL_NETWORK_FORBIDDEN'}
 function Invoke-WebRequest {throw 'REAL_NETWORK_FORBIDDEN'}
 function Start-Process {throw 'REAL_PROGRAM_FORBIDDEN'}
 function Assert-Idle([bool]$Ok,[string]$Message){if(!$Ok){throw ('ASSERTION:'+ $Message)}}
 function Reject-Idle([scriptblock]$Body){try{&$Body|Out-Null;return $false}catch{return $true}}
 function New-IdleValue {
  [ordered]@{version=1;pid=123;birth='456';root=(Join-Path $script:CaseRoot 'fake-steam');revision=('a'*64);targetId=('b'*16);backendExe=$script:BackendExe;FutureNote=@{keep='user';values=@(1,$null,'unknown')}}
 }
 function Put-Idle($Value){[IO.File]::WriteAllText($script:AppearanceIdleFile,($Value|ConvertTo-Json -Depth 12 -Compress),[Text.UTF8Encoding]::new($false))}
 function Read-IdleBytes {,[IO.File]::ReadAllBytes($script:AppearanceIdleFile)}
 function Check-Idle([string]$Name,[scriptblock]$Body){
  if($Case -and $Case -cne $Name){return}
  $script:CaseRoot=Join-Path $OutputDirectory ('owned-'+($script:IdleResults.Count+1));[IO.Directory]::CreateDirectory($script:CaseRoot)|Out-Null
  $script:AppearanceIdleFile=Join-Path $script:CaseRoot 'idle.json';$script:BackendExe=Join-Path $script:CaseRoot 'fake-css.exe';$script:IdleFault='';$script:IdleCalls=@();$script:HeldIdle=$null
  $pass=$true;$problem=''
  try{&$Body}catch{$pass=$false;$problem=$_.Exception.ToString()}
  finally{if($script:HeldIdle){$script:HeldIdle.Dispose();$script:HeldIdle=$null}}
  $files=@([IO.Directory]::GetFiles($script:CaseRoot,'*',[IO.SearchOption]::AllDirectories)|ForEach-Object {@{path=$_;bytes=([IO.FileInfo]::new($_)).Length;sha256=(Get-IdleHash ([IO.File]::ReadAllBytes($_)))}})
  $script:IdleResults.Add([pscustomobject]@{name=$Name;pass=$pass;error=$problem;ownedRoot=$script:CaseRoot;calls=@($script:IdleCalls);files=$files})
  Write-Output ((@('FAIL','PASS')[[int]$pass])+': '+$Name)
 }
 Check-Idle 'missing marker remains inactive and explicit clear is idempotent' {
  Assert-Idle ($null -eq (Read-AppearanceIdle)) 'missing marker acquired authority';Clear-AppearanceIdle
  Assert-Idle (![IO.File]::Exists($script:AppearanceIdleFile)) 'clear created marker'
 }
 Check-Idle 'legal record unknown fields read session consume clear and reread' {
  Put-Idle (New-IdleValue);$m=Read-AppearanceIdle
  Assert-Idle ($m.FutureNote.keep -ceq 'user' -and $m.FutureNote.values.Count -eq 3) 'unknown fields lost'
  $session=[pscustomobject]@{pid=123;birth='456';root=(Join-Path $script:CaseRoot 'fake-steam')}
  Assert-Idle (Test-AppearanceIdleSession $m $session) 'real data consumer rejected legal identity'
  $session.birth='457';Assert-Idle (!(Test-AppearanceIdleSession $m $session)) 'late session authorized marker'
  Clear-AppearanceIdle;Assert-Idle ($null -eq (Read-AppearanceIdle)) 'clear did not remain inactive'
 }
 foreach($bad in @('', '{', '[]', 'null', '42', '{"version":1}')){
  $captured=$bad
  Check-Idle ('bad root or truncation '+$bad) {
   [IO.File]::WriteAllText($script:AppearanceIdleFile,$captured);$before=Read-IdleBytes
   Assert-Idle ($null -eq (Read-AppearanceIdle)) 'bad root authorized marker'
   Assert-Idle ((Get-IdleHash (Read-IdleBytes)) -eq (Get-IdleHash $before)) 'reader modified original'
  }
 }
 foreach($field in @('version-string','version-bool','version-float','pid-string','pid-bool','birth-number','revision-array','target-type','root-type','backend-relative','future-version')){
  $captured=$field
  Check-Idle ('wrong known type '+$field) {
   $v=New-IdleValue
   switch($captured){
    'version-string'{$v.version='1'} 'version-bool'{$v.version=$true} 'version-float'{$v.version=1}
    'pid-string'{$v.pid='123'} 'pid-bool'{$v.pid=$true} 'birth-number'{$v.birth=456} 'revision-array'{$v.revision=@('a'*64)}
    'target-type'{$v.targetId=@('b'*16)} 'root-type'{$v.root=123} 'backend-relative'{$v.backendExe='unknown.exe'} 'future-version'{$v.version=999}
   }
   Put-Idle $v
   if($captured -eq 'version-float'){$raw=[IO.File]::ReadAllText($script:AppearanceIdleFile).Replace('"version":1,','"version":1.0,');[IO.File]::WriteAllText($script:AppearanceIdleFile,$raw)}
   $before=Read-IdleBytes;Assert-Idle ($null -eq (Read-AppearanceIdle)) 'coercion authorized unknown marker'
   Assert-Idle ((Get-IdleHash (Read-IdleBytes)) -eq (Get-IdleHash $before)) 'bad original modified'
  }
 }
 Check-Idle 'duplicate escaped key cannot authorize identity' {
  Put-Idle (New-IdleValue);$raw=[IO.File]::ReadAllText($script:AppearanceIdleFile);$raw=$raw.Substring(0,$raw.Length-1)+',"\u0076ersion":1}'
  [IO.File]::WriteAllText($script:AppearanceIdleFile,$raw);Assert-Idle ($null -eq (Read-AppearanceIdle)) 'escaped duplicate authorized marker'
 }
 Check-Idle 'duplicate unknown nested key is preserved inactive' {
  Put-Idle (New-IdleValue);$raw=[IO.File]::ReadAllText($script:AppearanceIdleFile);$raw=$raw.Substring(0,$raw.Length-1)+',"extra":{"x":1,"x":2}}'
  [IO.File]::WriteAllText($script:AppearanceIdleFile,$raw);Assert-Idle ($null -eq (Read-AppearanceIdle)) 'duplicate unknown object authorized marker'
 }
 Check-Idle 'valid UTF8 UTF16 and UTF32 BOM records preserve values' {
  $text=(New-IdleValue)|ConvertTo-Json -Depth 12 -Compress
  foreach($encoding in @([Text.UTF8Encoding]::new($true,$true),[Text.UnicodeEncoding]::new($false,$true,$true),[Text.UnicodeEncoding]::new($true,$true,$true),[Text.UTF32Encoding]::new($false,$true,$true),[Text.UTF32Encoding]::new($true,$true,$true))){
   [IO.File]::WriteAllBytes($script:AppearanceIdleFile,([byte[]]($encoding.GetPreamble()+$encoding.GetBytes($text))))
   $m=Read-AppearanceIdle;Assert-Idle ($m -and $m.FutureNote.keep -ceq 'user') 'legal encoding lost value'
  }
 }
 Check-Idle 'invalid UTF8 inside unknown string remains inactive' {
  Put-Idle (New-IdleValue);$text=[IO.File]::ReadAllText($script:AppearanceIdleFile).Replace('"user"','"BAD-ENCODING"');$bytes=[Text.Encoding]::UTF8.GetBytes($text);$i=$text.IndexOf('BAD-ENCODING');$bytes[$i]=255
  [IO.File]::WriteAllBytes($script:AppearanceIdleFile,$bytes);Assert-Idle ($null -eq (Read-AppearanceIdle)) 'replacement decoding authorized unknown data'
 }
 Check-Idle 'handle bounded size refuses stat size lie and preserves file' {
  $v=New-IdleValue;$v.extra='x'*9000;Put-Idle $v;$script:IdleFault='size-lie';$before=Read-IdleBytes
  Assert-Idle ($null -eq (Read-AppearanceIdle)) 'stat-only limit accepted oversized payload'
  Assert-Idle ((Get-IdleHash (Read-IdleBytes)) -eq (Get-IdleHash $before)) 'oversized original modified'
 }
 Check-Idle 'clear cannot erase future unknown or corrupt original' {
  foreach($raw in @('{bad','{"version":999,"debt":"KEEP"}','[]')){
   [IO.File]::WriteAllText($script:AppearanceIdleFile,$raw)
   Assert-Idle (Reject-Idle {Clear-AppearanceIdle}) 'unknown marker clear acknowledged success'
   Assert-Idle ([IO.File]::ReadAllText($script:AppearanceIdleFile) -ceq $raw) 'unknown original erased'
  }
 }
 Check-Idle 'failed clear and silent clear retain marker then explicit retry' {
  foreach($fault in @('remove-denied','remove-silent')){
   Put-Idle (New-IdleValue);$before=Read-IdleBytes;$script:IdleFault=$fault
   Assert-Idle (Reject-Idle {Clear-AppearanceIdle}) 'failed clear falsely acknowledged'
   Assert-Idle ((Get-IdleHash (Read-IdleBytes)) -eq (Get-IdleHash $before)) 'failed clear removed original'
   $script:IdleFault='';Clear-AppearanceIdle;Assert-Idle (![IO.File]::Exists($script:AppearanceIdleFile)) 'explicit clear retry failed'
  }
 }
 Check-Idle 'owned sharing denial is inactive then read recovers after release' {
  Put-Idle (New-IdleValue);$script:HeldIdle=[IO.FileStream]::new($script:AppearanceIdleFile,[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
  Assert-Idle ($null -eq (Read-AppearanceIdle)) 'locked read produced marker'
  $script:HeldIdle.Dispose();$script:HeldIdle=$null;Assert-Idle ($null -ne (Read-AppearanceIdle)) 'read did not recover'
 }
 Check-Idle 'explicit valid repair preserves bad copy and enables only matching in-memory consume' {
  [IO.File]::WriteAllText($script:AppearanceIdleFile,'{bad');$retained=Join-Path $script:CaseRoot 'bad-original.json';[IO.File]::Copy($script:AppearanceIdleFile,$retained)
  Assert-Idle ($null -eq (Read-AppearanceIdle)) 'bad record authorized marker';Put-Idle (New-IdleValue);$m=Read-AppearanceIdle
  Assert-Idle ($m -and [IO.File]::ReadAllText($retained) -ceq '{bad') 'explicit fake producer repair lost retained original'
  Assert-Idle (!(Test-AppearanceIdleSession $m ([pscustomobject]@{pid=999;birth='456';root=$m.root}))) 'wrong fake session consumed marker'
 }
 $sourceAfter=[IO.File]::ReadAllBytes($source);$consumerAfter=[IO.File]::ReadAllBytes($PSCommandPath)
 $drift=(Get-IdleHash $sourceBefore) -cne (Get-IdleHash $sourceAfter) -or (Get-IdleHash $consumerBefore) -cne (Get-IdleHash $consumerAfter)
 $assemblies=@([AppDomain]::CurrentDomain.GetAssemblies()|Where-Object {$_.Location -and [IO.File]::Exists($_.Location)}|ForEach-Object {@{path=$_.Location;sha256=(Get-IdleHash ([IO.File]::ReadAllBytes($_.Location)))}})
 $failed=@($script:IdleResults|Where-Object {!$_.pass}).Count
 $result=[pscustomobject]@{pass=($failed -eq 0 -and !$drift);cases=$script:IdleResults.Count;failed=$failed;receipts=$script:IdleResults.ToArray();source=$source;sourceSha256=(Get-IdleHash $sourceBefore);consumer=$PSCommandPath;consumerSha256=(Get-IdleHash $consumerBefore);functionPins=$functionPins;runtimeAssemblies=$assemblies;sourceDrift=$drift;wholeScriptExecuted=$false;realSteamProcessNetworkRegistryServiceTaskActions=0;psVersion=$PSVersionTable.PSVersion.ToString()}
 [IO.File]::WriteAllText((Join-Path $OutputDirectory 'RESULT.json'),($result|ConvertTo-Json -Depth 16),[Text.UTF8Encoding]::new($false))
 if(!$result.pass){exit 1};exit 0
}
& (Join-Path $PSScriptRoot 'steam_appearance_manual_observation_selftest.ps1') -Out $OutputDirectory
if($LASTEXITCODE){exit $LASTEXITCODE}