param([string]$HostDll,[Parameter(Mandatory=$true)][string]$OutputRoot,[switch]$FileConfigOnly,[string]$HostSourcePath,[string]$CaseFilter='')
$ErrorActionPreference='Stop'
if ($FileConfigOnly) {
  if ([string]::IsNullOrWhiteSpace($HostSourcePath)) { $HostSourcePath = Join-Path $PSScriptRoot '..\FanLab\real-host\Program.cs' }
  $fileSource = [IO.Path]::GetFullPath($HostSourcePath)
  $fileOut = [IO.Path]::GetFullPath($OutputRoot)
  $repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
  $workspaceRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot '..\..\..'))
  $evidenceRoot = [IO.Path]::GetFullPath((Join-Path $workspaceRoot 'Testing\YMCC\Evidence')).TrimEnd('\') + '\'
  if (-not $fileOut.StartsWith($evidenceRoot,[StringComparison]::OrdinalIgnoreCase)) { throw 'File-config requires an owned YMCC Evidence output root.' }
  if (Test-Path -LiteralPath $fileOut) { throw 'Use a new evidence directory.' }
  [IO.Directory]::CreateDirectory($fileOut) | Out-Null
  Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'fan_write_confirmation_selftest.cs') -Destination (Join-Path $fileOut 'Consumer.cs')
  $extractor = @'
import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';
const [input,out,consumer,runner]=process.argv.slice(2),hash=b=>crypto.createHash('sha256').update(b).digest('hex');
const pin=p=>({path:path.resolve(p),bytes:fs.statSync(p).size,sha256:hash(fs.readFileSync(p))});
const bytes=fs.readFileSync(input),text=bytes.toString('utf8').replace(/^\uFEFF/,'');
function block(anchor){const start=text.indexOf(anchor);if(start<0||text.indexOf(anchor,start+anchor.length)>=0)throw Error('Exact production anchor not unique');const open=text.indexOf('{',start);let depth=0,state='code';for(let i=open;i<text.length;i++){const c=text[i],n=text[i+1];if(state==='line'){if(c==='\n')state='code';continue;}if(state==='block'){if(c==='*'&&n==='/'){state='code';i++;}continue;}if(state==='verbatim'){if(c==='"'&&n==='"'){i++;continue;}if(c==='"')state='code';continue;}if(state==='string'||state==='char'){if(c==='\\'){i++;continue;}if(c===(state==='string'?'"':"'"))state='code';continue;}if(c==='/'&&n==='/'){state='line';i++;continue;}if(c==='/'&&n==='*'){state='block';i++;continue;}if(c==='@'&&n==='"'){state='verbatim';i++;continue;}if(c==='"'){state='string';continue;}if(c==="'"){state='char';continue;}if(c==='{')depth++;else if(c==='}'&&--depth===0){const code=text.slice(start,i+1);return {code,startLine:text.slice(0,start).split('\n').length,endLine:text.slice(0,i+1).split('\n').length,sha256:hash(code)};}}throw Error('Unclosed production block');}
const members=[['HostOptions','internal sealed record HostOptions('],['SessionTokenMatches','    private static bool SessionTokenMatches(string supplied, ReadOnlySpan<byte> expected)'],['IsAuthorized','    private bool IsAuthorized()'],['HasValidWriteConfirmation','    private bool HasValidWriteConfirmation()']].map(([name,anchor])=>({name,...block(anchor)}));
const member=n=>members.find(x=>x.name===n).code;
let source=`#nullable enable\nusing System;using System.IO;using System.Text;using System.Security.Cryptography;\nusing File = EvidenceFanAuth.FileConfigIoPort;\nnamespace EvidenceFanAuth;\n`+member('HostOptions')+`\ninternal static class TokenComparisonSlice {\n`+member('SessionTokenMatches')+`\ninternal static bool Match(string supplied,ReadOnlySpan<byte> expected)=>SessionTokenMatches(supplied,expected);\n}\ninternal sealed record BoundRoute(bool HcCallbackReady);\ninternal sealed class AuthorizationSlice {\nprivate readonly HostOptions options;private readonly byte[]? writeConfirmationUtf8,writeSessionTokenUtf8;private readonly BoundRoute? Route;private readonly string? FactoryType;\ninternal AuthorizationSlice(HostOptions boundOptions,bool routeReady,string? factoryType){options=boundOptions;Route=routeReady?new BoundRoute(true):null;FactoryType=factoryType;writeConfirmationUtf8=string.IsNullOrWhiteSpace(options.Confirmation)?null:Encoding.UTF8.GetBytes(options.Confirmation);writeSessionTokenUtf8=string.IsNullOrWhiteSpace(options.SessionToken)?null:Encoding.UTF8.GetBytes(options.SessionToken);}\n`+member('IsAuthorized')+'\n'+member('HasValidWriteConfirmation')+`\ninternal bool Evaluate()=>IsAuthorized();internal bool ConfirmationValid()=>HasValidWriteConfirmation();\n}\n`;
fs.mkdirSync(path.join(out,'inputs'),{recursive:true});fs.writeFileSync(path.join(out,'inputs/Program.cs.txt'),bytes);fs.writeFileSync(path.join(out,'ExactProduction.cs'),source);
const artifacts=members.map(m=>{const p=path.join(out,'inputs',m.name+'.cs.txt');fs.writeFileSync(p,m.code);return {name:m.name,startLine:m.startLine,endLine:m.endLine,sha256:m.sha256,artifact:pin(p)};});
const manifest={atUtc:new Date().toISOString(),source:pin(input),snapshot:pin(path.join(out,'inputs/Program.cs.txt')),members:artifacts,hostOptionsOwnParseAndNextIncluded:true,consumer:pin(consumer),runner:pin(runner),generated:pin(path.join(out,'ExactProduction.cs')),memberBodiesVerbatim:true,bindings:'Fake options/route/factory/credential bytes bound readonly before first Evaluate; no backend/server construction',filePort:'Exact source File aliases to owned-root guard that delegates System.IO.File; delayed-result timing only',forbiddenProductionMembersAbsent:['Main','LocalFanHostServer','LoadAssemblyAndFactory','EnsureRealWriteAuthorization','ValidateFormalSessionCapabilityAcl']};fs.writeFileSync(path.join(out,'source-binding.json'),JSON.stringify(manifest,null,2)+'\n');
'@
  $utf8 = New-Object Text.UTF8Encoding($false)
  [IO.File]::WriteAllText((Join-Path $fileOut 'prepare-exact.mjs'),$extractor,$utf8)
  & node (Join-Path $fileOut 'prepare-exact.mjs') $fileSource $fileOut (Join-Path $PSScriptRoot 'fan_write_confirmation_selftest.cs') $PSCommandPath
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
  [IO.File]::WriteAllText((Join-Path $fileOut 'NuGet.Config'),'<configuration><packageSources><clear /></packageSources></configuration>',$utf8)
  $project = @'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net10.0</TargetFramework><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable><TreatWarningsAsErrors>true</TreatWarningsAsErrors><EnableDefaultCompileItems>false</EnableDefaultCompileItems><DefineConstants>FAN_FILE_CONFIG_SELFTEST</DefineConstants><UseSharedCompilation>false</UseSharedCompilation><ImportDirectoryBuildProps>false</ImportDirectoryBuildProps><ImportDirectoryBuildTargets>false</ImportDirectoryBuildTargets><NuGetAudit>false</NuGetAudit></PropertyGroup><ItemGroup><Compile Include="Consumer.cs" /><Compile Include="ExactProduction.cs" /></ItemGroup><Target Name="FileConfigCompileAuthority" BeforeTargets="CoreCompile"><WriteLinesToFile File="$(MSBuildProjectDirectory)/compile-items.txt" Lines="@(Compile->'%(FullPath)')" Overwrite="true" /><WriteLinesToFile File="$(MSBuildProjectDirectory)/reference-items.txt" Lines="@(ReferencePath->'%(FullPath)')" Overwrite="true" /><WriteLinesToFile File="$(MSBuildProjectDirectory)/msbuild-projects.txt" Lines="$(MSBuildAllProjects)" Overwrite="true" /></Target></Project>
'@
  [IO.File]::WriteAllText((Join-Path $fileOut 'FileConfig.csproj'),$project,$utf8)
  function Invoke-FileConfigProcess([string]$Label,[string[]]$Arguments,[int]$BudgetMs) {
    $dotnetPath = (Get-Command dotnet -ErrorAction Stop).Source
    $info = New-Object Diagnostics.ProcessStartInfo
    $info.FileName=$dotnetPath; $info.WorkingDirectory=$fileOut; $info.UseShellExecute=$false; $info.CreateNoWindow=$true
    $info.RedirectStandardOutput=$true; $info.RedirectStandardError=$true
    if ($info.PSObject.Properties.Name -contains 'ArgumentList') { foreach($argument in $Arguments){$info.ArgumentList.Add($argument)} }
    else { $info.Arguments = ($Arguments | ForEach-Object { if($_ -match '["\r\n]'){throw 'Unsafe fixture process argument'}; '"'+$_+'"' }) -join ' ' }
    foreach($entry in @{DOTNET_CLI_TELEMETRY_OPTOUT='1';DOTNET_SKIP_FIRST_TIME_EXPERIENCE='1';DOTNET_NOLOGO='1';MSBUILDDISABLENODEREUSE='1';DOTNET_CLI_HOME=(Join-Path $fileOut 'dotnet-home');NUGET_PACKAGES=(Join-Path $fileOut 'packages')}.GetEnumerator()){$info.EnvironmentVariables[$entry.Key]=$entry.Value}
    $process = New-Object Diagnostics.Process
    $process.StartInfo=$info
    $clock=[Diagnostics.Stopwatch]::StartNew();$started=[DateTime]::UtcNow.ToString('o');[void]$process.Start()
    $ownedPid=$process.Id;$stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync()
    $exited=$process.WaitForExit($BudgetMs)
    if(-not $exited){try{$process.Kill($true)}catch{$process.Kill()};[void]$process.WaitForExit(5000)}
    [IO.File]::WriteAllText((Join-Path $fileOut ($Label+'.stdout')),$stdout.GetAwaiter().GetResult(),$utf8)
    [IO.File]::WriteAllText((Join-Path $fileOut ($Label+'.stderr')),$stderr.GetAwaiter().GetResult(),$utf8)
    $receipt=[ordered]@{label=$Label;pid=$ownedPid;exe=$dotnetPath;args=$Arguments;cwd=$fileOut;startedAtUtc=$started;finishedAtUtc=[DateTime]::UtcNow.ToString('o');elapsedMs=$clock.Elapsed.TotalMilliseconds;budgetMs=$BudgetMs;timedOut=(-not $exited);exitCode=$process.ExitCode}
    [IO.File]::WriteAllText((Join-Path $fileOut ($Label+'-PROCESS.json')),($receipt|ConvertTo-Json -Depth 8),$utf8)
    $exitCode=$process.ExitCode;$process.Dispose();if(-not $exited){return 124};return $exitCode
  }
  $compileArgs=@('build',(Join-Path $fileOut 'FileConfig.csproj'),'-c','Release','-o',(Join-Path $fileOut 'bin'),'--disable-build-servers',('-p:RestoreConfigFile='+(Join-Path $fileOut 'NuGet.Config')),'-p:NuGetAudit=false','-p:UseSharedCompilation=false',('-bl:'+(Join-Path $fileOut 'compile.binlog')))
  $compileExit=Invoke-FileConfigProcess 'compile' $compileArgs 60000
  if($compileExit-ne0){Get-Content -LiteralPath (Join-Path $fileOut 'compile.stdout') -Tail 20;Get-Content -LiteralPath (Join-Path $fileOut 'compile.stderr') -Tail 10;exit $compileExit}
  $runArgs=@((Join-Path $fileOut 'bin/FileConfig.dll'),'--file-config',(Join-Path $fileOut 'fixtures'),$CaseFilter)
  $runExit=Invoke-FileConfigProcess 'native' $runArgs 12000
  Get-Content -LiteralPath (Join-Path $fileOut 'native.stdout') -Tail 16
  exit $runExit
}
if ([string]::IsNullOrWhiteSpace($HostDll)) { throw 'HostDll is required without FileConfigOnly.' }
$fanOut=[IO.Path]::GetFullPath($OutputRoot)
if(Test-Path -LiteralPath $fanOut){throw 'Use a new evidence directory.'}
[IO.Directory]::CreateDirectory($fanOut)|Out-Null
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'fan_write_confirmation_selftest.cs') -Destination (Join-Path $fanOut 'Program.cs')
'<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net10.0-windows10.0.19041.0</TargetFramework><UseWPF>true</UseWPF><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable><TreatWarningsAsErrors>true</TreatWarningsAsErrors></PropertyGroup></Project>'|Set-Content -LiteralPath (Join-Path $fanOut 'Checks.csproj') -Encoding utf8
& dotnet build (Join-Path $fanOut 'Checks.csproj') -c Release -o (Join-Path $fanOut 'bin') --disable-build-servers *> (Join-Path $fanOut 'build.out')
if($LASTEXITCODE-ne0){Get-Content (Join-Path $fanOut 'build.out') -Tail 14;exit $LASTEXITCODE}
& dotnet (Join-Path $fanOut 'bin/Checks.dll') ([IO.Path]::GetFullPath($HostDll)) (Join-Path $fanOut 'result.json')
exit $LASTEXITCODE
