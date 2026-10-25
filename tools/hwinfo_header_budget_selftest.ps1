[CmdletBinding()]
param([string]$RecoveryScript='',[string]$CasesPath='',[string]$ResultPath='')
$ErrorActionPreference='Stop'
# Keep the Windows PowerShell module lookup when launched from pwsh/Node.
$env:PSModulePath=(Join-Path $PSHOME 'Modules')+';'+$env:PSModulePath
if(!$RecoveryScript){$RecoveryScript=Join-Path $PSScriptRoot '../PowerControl/YeManHWiNFO.ps1'}
if(!$CasesPath){$CasesPath=Join-Path $PSScriptRoot 'fixtures/hwinfo_header_budget_cases.json'}
if(!$ResultPath){$ResultPath=Join-Path $PSScriptRoot '../../../../Build/Tasks/YMCC-Steam-HUD-Probe/hwinfo-health-budget-parity-20261016/AFTER-HELPER-RESULT.json'}
[void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($ResultPath)))
$tokens=$null;$errors=$null;$ast=[Management.Automation.Language.Parser]::ParseFile([IO.Path]::GetFullPath($RecoveryScript),[ref]$tokens,[ref]$errors)
if($errors.Count){throw 'Production helper parse failure'}
$function=$ast.Find({param($n)$n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq 'Test-HWiNFOSharedMemory'},$false)
if(!$function){throw 'Production probe missing'}
$body=$function.Body.Extent.Text;$body=$body.Substring(1,$body.Length-2)
function Get-HWiNFOClockMs{return 1700000000000L}
$cases=[IO.File]::ReadAllText([IO.Path]::GetFullPath($CasesPath))|ConvertFrom-Json
$results=@()
foreach($case in $cases){
 $mapping=$null;$view=$null
 try{
  $name='YMCC_HEADER_BUDGET_'+[Guid]::NewGuid().ToString('N')
  $probeText=$body
  foreach($original in @('Global\HWiNFO_SENS_SM2','HWiNFO_SENS_SM2','Global\HWiNFO_SENS_SM','HWiNFO_SENS_SM')){$probeText=$probeText.Replace($original,$name)}
  $probe=[scriptblock]::Create($probeText)
  $mapping=[IO.MemoryMappedFiles.MemoryMappedFile]::CreateNew($name,[long]$case.capacity)
  $view=$mapping.CreateViewAccessor()
  $view.Write(0,[uint32]0x53695748);$view.Write(4,[uint32]$case.version);$view.Write(8,[uint32]1);$view.Write(12,[int64]1700000000)
  $fields=@{sensorOffset=20;sensorSize=24;sensorCount=28;readingOffset=32;readingSize=36;readingCount=40}
  foreach($field in $fields.Keys){$view.Write([long]$fields[$field],[uint32]$case.$field)}
  if($case.sensorOffset -ge 48){$view.Write(44,[uint32]3000)}
  $proof=$null;$ok=& $probe -ProducerProof ([ref]$proof)
  $results+=@{name=$case.name;accepted=[bool]$ok;expected=[bool]$case.expected;producerProof=$proof;mapCapacity=$view.Capacity}
 }finally{if($view){$view.Dispose()};if($mapping){$mapping.Dispose()}}
}
$result=@{helperSha256=(Get-FileHash -LiteralPath $RecoveryScript -Algorithm SHA256).Hash;actualProductionProbe=$true;realNamedMappings=$true;realHWiNFOMap=$false;realProcessesOrRegistryChanged=$false;cases=$results;mismatches=@($results|Where-Object{$_.accepted -ne $_.expected})}
[IO.File]::WriteAllText([IO.Path]::GetFullPath($ResultPath),($result|ConvertTo-Json -Depth 15),[Text.UTF8Encoding]::new($false))
Write-Output ('Production helper probe: '+$results.Count+' cases; mismatches='+$result.mismatches.Count)

if($result.mismatches.Count -gt 0){exit 1}
