[CmdletBinding()]
param(
 [Parameter(Mandatory=$true)][string]$DeploySource,
 [Parameter(Mandatory=$true)][string]$SourcePowerControl,
 [Parameter(Mandatory=$true)][string]$InstalledRoot,
 [Parameter(Mandatory=$true)][string]$BackupRoot,
 [Parameter(Mandatory=$true)][string]$EvidenceDir,
 [Parameter(Mandatory=$true)][string]$ExpectedHelperSha256,
 [Parameter(Mandatory=$true)][string]$ExpectedNativeSha256,
 [ValidateSet('Copy','LockedSource')][string]$Mode='Copy'
)
$ErrorActionPreference='Stop'
$env:PSModulePath=(Join-Path $PSHOME 'Modules')+';'+$env:PSModulePath
function Hash([string]$p){(Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLowerInvariant()}
function Save([string]$p,$v){[IO.File]::WriteAllText($p,($v|ConvertTo-Json -Depth 20),[Text.UTF8Encoding]::new($false))}
$evidence=[IO.Path]::GetFullPath($EvidenceDir).TrimEnd('\')
foreach($path in @($SourcePowerControl,$InstalledRoot,$BackupRoot)){
 $full=[IO.Path]::GetFullPath($path)
 if(-not $full.StartsWith($evidence+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'COPY_LANE_OUTSIDE_OWNED_CASE'}
 $cursor=$full
 while($cursor.Length -gt 3){if(Test-Path -LiteralPath $cursor){if((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'COPY_LANE_REPARSE'}};$cursor=Split-Path -Parent $cursor}
}
if(Test-Path -LiteralPath (Join-Path $evidence 'result.json')){throw 'Fresh copy-lane evidence required'}
$before=Hash $DeploySource
$tokens=$null;$parseErrors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile($DeploySource,[ref]$tokens,[ref]$parseErrors)
if($parseErrors.Count){throw 'DEPLOY_SOURCE_PARSE_FAILED'}
$statements=@($ast.EndBlock.Statements)
$matches=@(0..($statements.Count-1)|Where-Object {$statements[$_] -is [Management.Automation.Language.AssignmentStatementAst] -and $statements[$_].Left -is [Management.Automation.Language.VariableExpressionAst] -and $statements[$_].Left.VariablePath.UserPath -ceq 'sourceLightSetter'})
if($matches.Count -ne 1){throw 'DEPLOY_COPY_LANE_SHAPE_CHANGED'}
$position=$matches[0]
if($statements[$position+1] -isnot [Management.Automation.Language.IfStatementAst]){throw 'DEPLOY_COPY_LANE_SHAPE_CHANGED'}
$assignment=$statements[$position];$branch=$statements[$position+1]
$commands=@($branch.FindAll({param($n)$n -is [Management.Automation.Language.CommandAst]},$true)|ForEach-Object GetCommandName)
if(@($commands|Where-Object {$_ -ceq 'Move-Item'}).Count -ne 1 -or @($commands|Where-Object {$_ -ceq 'Copy-Item'}).Count -ne 1){throw 'DEPLOY_COPY_LANE_SHAPE_CHANGED'}
$text=[IO.File]::ReadAllText($DeploySource)
$snippet=$text.Substring($assignment.Extent.StartOffset,$branch.Extent.EndOffset-$assignment.Extent.StartOffset)
$snippetPath=Join-Path $evidence 'exact-deploy-lighting-lane.ps1'
[IO.File]::WriteAllText($snippetPath,$snippet,[Text.UTF8Encoding]::new($false))
$sourcePowerControl=$SourcePowerControl
$installedPowerControl=Join-Path $InstalledRoot 'PowerControl'
$native=Join-Path $InstalledRoot 'YeManCC/YeManCC.exe'
$oldHelper=Join-Path $installedPowerControl 'light-setter/YeManLightSetter.dll'
$pre=@{native=(Hash $native);helper=(Hash $oldHelper)}
$lock=$null;$copyError=$null
try {
 if($Mode -ceq 'LockedSource'){$lock=[IO.File]::Open((Join-Path $sourcePowerControl 'light-setter/YeManLightSetter.dll'),[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::None)}
 try{& ([scriptblock]::Create($snippet))}catch{$copyError=$_.Exception.Message}
}finally{if($null -ne $lock){$lock.Dispose()}}
$post=@{native=(Hash $native);helper=$(if(Test-Path -LiteralPath $oldHelper){Hash $oldHelper}else{$null})}
$backupHelper=Join-Path $BackupRoot 'PowerControl/light-setter/YeManLightSetter.dll'
$backupHash=if(Test-Path -LiteralPath $backupHelper){Hash $backupHelper}else{$null}
if($post.native -cne $ExpectedNativeSha256 -or $pre.native -cne $ExpectedNativeSha256){throw 'NATIVE_CHANGED_IN_COPY_ONLY_PROBE'}
if($Mode -ceq 'Copy'){
 if($copyError -or $post.helper -cne $ExpectedHelperSha256 -or $pre.helper -ceq $post.helper){throw 'EXPECTED_COPY_LANE_GAP_NOT_REPRODUCED'}
}else{
 if(-not $copyError -or $backupHash -cne $pre.helper -or $post.helper -ceq $pre.helper){throw 'EXPECTED_COPY_FAILURE_WITHOUT_LOCAL_ROLLBACK_NOT_REPRODUCED'}
}
if((Hash $DeploySource) -cne $before){throw 'DEPLOY_SOURCE_CHANGED_DURING_PROBE'}
$report=@{schemaVersion=1;status='GAP_REPRODUCED';passed=$true;productBugFixed=$false;processId=$PID;engineVersion=$PSVersionTable.PSVersion.ToString();source=@{path=$DeploySource;sha256=$before;startLine=$assignment.Extent.StartLineNumber;endLine=$branch.Extent.EndLineNumber};executedSnippet=@{path=$snippetPath;sha256=(Hash $snippetPath)};scope='Exact existing lighting copy sub-lane only. Full deploy preconditions/final guards not executed.';pre=$pre;post=$post;wrongTupleCopied=($Mode -ceq 'Copy');copyFailedWithoutLocalRollback=($Mode -ceq 'LockedSource');copyMode=$Mode;copyError=$copyError;backupHelperSha256=$backupHash;fullDeployBypassProved=$false;productApplicationExecuted=$false;hardwareIo=$false;actualCWritten=$false;actualGCanonicalWritten=$false}
Save (Join-Path $evidence 'result.json') $report
Write-Output ('DEPLOY_LIGHTING_COPY_GAP_REPRODUCED '+$PSVersionTable.PSVersion)
