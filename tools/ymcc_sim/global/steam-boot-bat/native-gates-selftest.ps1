param([string]$Candidate,[string]$Evidence)
$ErrorActionPreference='Stop'
$env:PSModulePath=(Join-Path $PSHOME 'Modules')+';'+$env:PSModulePath
[IO.Directory]::CreateDirectory($Evidence)|Out-Null
$source=[IO.File]::ReadAllText($Candidate,[Text.Encoding]::UTF8);$marker='### YEMAN_SINGLE_BAT_POWERSHELL ###';$body=$source.Substring($source.LastIndexOf($marker)+$marker.Length)
$tokens=$null;$errors=$null;$ast=[Management.Automation.Language.Parser]::ParseInput($body,[ref]$tokens,[ref]$errors);if($errors.Count){throw ($errors.Message -join ';')}
foreach($name in @('Initialize-BootProcessApi','Initialize-WindowApi')){$fn=$ast.FindAll({param($n)$n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq $name},$false)[0];. ([scriptblock]::Create($fn.Extent.Text))}
Initialize-BootProcessApi;Initialize-WindowApi
$script:checks=0
function Assert($Ok,$Message){$script:checks++;if(-not $Ok){throw ('ASSERT: '+$Message)}}
function Good {
 $r=[YmBootProcess+Row]::new();$r.Id=321;$r.Birth=123456;$r.Session=42;$r.Readable=$true;$r.Name='XboxPcAppFT.exe';$r.Path='C:\Program Files\WindowsApps\Microsoft.GamingApp_1.0.0.0_x64__8wekyb3d8bbwe\XboxPcAppFT.exe';$r.Family='Microsoft.GamingApp_8wekyb3d8bbwe';$r.Package='Microsoft.GamingApp_1.0.0.0_x64__8wekyb3d8bbwe';$r.User='S-1-known';return $r
}
$r=Good;Assert ([YmBootProcess]::ExactXbox($r,42)) 'actual native exact-name current-session gate admits a valid Xbox identity'
$r.Session=99;Assert (-not [YmBootProcess]::ExactXbox($r,42)) 'native gate excludes another session'
$r=Good;$r.Readable=$false;Assert (-not [YmBootProcess]::ExactXbox($r,42)) 'native gate refuses unreadable identity'
foreach($name in @('explorer.exe','svchost.exe','GamingServices.exe','GameBar.exe','XboxPcAppFT.exe.evil','myXboxPcApp.exe','YeManCC.exe','Steam.exe')){$r=Good;$r.Name=$name;Assert (-not [YmBootProcess]::ExactXbox($r,42)) ('native gate excludes unrelated '+$name)}
foreach($name in @('XboxPcApp.exe','XboxPcAppCE.exe','xboxpcappft.EXE')){$r=Good;$r.Name=$name;Assert ([YmBootProcess]::ExactXbox($r,42)) ('native gate retains known image '+$name)}
$r=Good;Assert ([YmBootProcess]::TrustedPackage($r,42,'S-1-known')) 'actual GamingApp package is eligible only with the current caller SID'
$r=Good;$r.User='S-1-other';Assert (-not [YmBootProcess]::TrustedPackage($r,42,'S-1-known')) 'another user package is refused'
$r=Good;$r.Family='Microsoft.GamingApp_lookalike';Assert (-not [YmBootProcess]::TrustedPackage($r,42,'S-1-known')) 'lookalike package family is refused'
$r=Good;$r.Package='Microsoft.GamingApp_1.0.0.0_x64__different';Assert (-not [YmBootProcess]::TrustedPackage($r,42,'S-1-known')) 'another package publisher is refused'
$r=Good;$r.Package='AnotherPackage_1.0.0.0_x64__8wekyb3d8bbwe';Assert (-not [YmBootProcess]::TrustedPackage($r,42,'S-1-known')) 'another package under the same publisher is refused'
$r=Good;Assert (-not [YmBootProcess]::TrustedPackage($r,42,'')) 'unknown caller SID cannot authorize whole-package termination'
# Exact COM slot order matches the SDK header read in this task. No COM instance is created.
$flags=[Reflection.BindingFlags]::NonPublic -bor [Reflection.BindingFlags]::Public
$type=[YmBootProcess].GetNestedType('PackageDebug',$flags)
$slots=@($type.GetMethods()|Sort-Object MetadataToken|ForEach-Object Name)
$expected=@('EnableDebugging','DisableDebugging','Suspend','Resume','TerminateAllProcesses','SetTargetSessionId','EnumerateBackgroundTasks','ActivateBackgroundTask','StartServicing','StopServicing','StartSessionRedirection','StopSessionRedirection','GetPackageExecutionState','RegisterForPackageStateChanges','UnregisterForPackageStateChanges')
Assert (($slots -join ',') -ceq ($expected -join ',')) 'COM interface preserves all15 SDK slots in their actual order'
Assert ($type.GUID.ToString() -eq 'f27c3930-8029-4ad1-94e3-3dba417810c1') 'COM interface uses the SDK IID'
$command='function Initialize-BootProcessApi {'+${function:Initialize-BootProcessApi}.ToString()+"}`r`nInitialize-BootProcessApi`r`n"
$encoded=[Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($command));Assert ($encoded.Length -lt 29000) 'complete process API fits bounded Windows encoded-command helper'
# Execute the production package helper encoder with a memory-only launch terminal.
$fn=$ast.FindAll({param($n)$n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq 'Invoke-BootPackageStop'},$false)[0];. ([scriptblock]::Create($fn.Extent.Text))
$script:session=42;$script:bootUserSid='S-1-known';$script:encodedArguments=''
function Start-Quiet {param($Exe,$Arguments,[switch]$Capture)$script:encodedArguments=$Arguments;[pscustomobject]@{Code=0;Text='inert encoded helper'}}
$r=Good;$receipt=Invoke-BootPackageStop $r
Assert ($receipt.Code -eq 0 -and $script:encodedArguments.Length -lt 32000 -and $script:encodedArguments -like '*-WindowStyle Hidden -EncodedCommand *') 'actual package helper is hidden, captured and within Windows command line bound'
$encoded=$script:encodedArguments.Substring($script:encodedArguments.LastIndexOf(' ')+1);$decoded=[Text.Encoding]::Unicode.GetString([Convert]::FromBase64String($encoded));$childTokens=$null;$childErrors=$null
[Management.Automation.Language.Parser]::ParseInput($decoded,[ref]$childTokens,[ref]$childErrors)|Out-Null
Assert ($childErrors.Count -eq 0 -and $decoded.Contains('[YmBootProcess]::StopPackage([int]$t.id,[long]$t.birth,[int]$t.session,[string]$t.package,[string]$t.sid)')) 'encoded helper preserves exact PID, birth, session, package and SID operands without shell interpolation'
[ordered]@{status='PASS';checks=$script:checks;consumer='actual native Xbox identity/package gates and SDK COM ABI';nativeApisCompiled=$true;realNativeProcessOrComEffects=$false;sourceSha256=(Get-FileHash -LiteralPath $Candidate -Algorithm SHA256).Hash.ToLowerInvariant()}|ConvertTo-Json|Tee-Object -FilePath (Join-Path $Evidence 'native-gates-result.json')
