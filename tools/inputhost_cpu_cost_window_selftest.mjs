#!/usr/bin/env node
// Current-source diagnostic fixture: all external boundaries are value/throw fakes.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';
const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const argv = process.argv.slice(2);
if (argv.length !== 0 && (argv.length !== 2 || argv[0] !== '--out')) throw Error('Usage: --out evidence-root');
const root = path.resolve(argv[1] ?? 'G:/YeManCC-Work/Build/Validation/VirtualInputPerf-R7-20261015/cpu-window');
const out = path.join(root, 'run-' + new Date().toISOString().replace(/[-:.]/g, ''));
fs.mkdirSync(out, { recursive: true });
const sha = b => crypto.createHash('sha256').update(b).digest('hex');
const programPath = path.join(repo, 'InputHost/Program.cs');
const programBytes = fs.readFileSync(programPath);
const source = programBytes.toString('utf8');
function extract(start, end) {
    const a=source.indexOf(start), b=source.indexOf(end,a);
    if (a < 0 || b <= a || source.indexOf(start,a+start.length) >= 0) throw Error('Non-unique source anchors '+start);
    return source.slice(a,b).trimEnd();
}
const methods = [
    extract('        private void MaybeEmitCostWindow()', '        // Only bounded in-memory copies happen here.'),
    extract('        private void ResetCostWindow()', '        private bool TrySubmitNeutral('),
    extract('        private string RoleOfCostThread(int osTid)', '        private long _costWindowStartTicks')
];
const transformations = [
    ['System.Diagnostics.ProcessThread','FakeThread'],
    ['System.Diagnostics.Process.GetCurrentProcess()','FakeProcessFactory.GetCurrentProcess()'],
    ['Environment.TickCount64','FakeClock.Tick']
];
const joined = methods.join('\n');
let transformed = joined;
const seams = transformations.map(([before,after]) => {
    const count=transformed.split(before).length-1;
    if (!count) throw Error('Missing seam '+before);
    transformed=transformed.split(before).join(after);
    return { before, after, count };
});
const fixturePath=path.join(repo,'tools/inputhost_cpu_cost_window_selftest.cs');
const fixtureBytes=fs.readFileSync(fixturePath);
const fixture=fixtureBytes.toString('utf8');
const marker='    // ACTUAL_METHODS_INSERTED_BY_RUNNER';
if (fixture.split(marker).length !== 2) throw Error('Fixture insertion marker');
const expanded=fixture.replace(marker, transformed);
const helperPath=path.join(repo,'InputHost/HostCpuCostWindow.cs');
const helper=fs.readFileSync(helperPath);
fs.writeFileSync(path.join(out,'Fixture.cs'),expanded);
fs.writeFileSync(path.join(out,'HostCpuCostWindow.cs'),helper);
fs.writeFileSync(path.join(out,'actual-diagnostic-methods.txt'),joined);
fs.writeFileSync(path.join(out,'CpuCostWindowSelftest.csproj'),`<Project Sdk="Microsoft.NET.Sdk">
<PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net10.0</TargetFramework><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable><EnableDefaultCompileItems>false</EnableDefaultCompileItems><NuGetAudit>false</NuGetAudit><RuntimeFrameworkVersion>10.0.11</RuntimeFrameworkVersion><RollForward>Disable</RollForward><NoWarn>CS0414;CS0649</NoWarn></PropertyGroup>
<ItemGroup><Compile Include="Fixture.cs"/><Compile Include="HostCpuCostWindow.cs"/></ItemGroup>
</Project>\n`);
const empty=path.join(out,'empty-nuget-source');fs.mkdirSync(empty);
function run(args,log){const result=spawnSync('dotnet',args,{cwd:out,encoding:'utf8',timeout:180000,maxBuffer:5*1024*1024,env:{...process.env,DOTNET_CLI_TELEMETRY_OPTOUT:'1'}});fs.writeFileSync(path.join(out,log),(result.stdout??'')+(result.stderr??''));if(result.error||result.status!==0)throw Error(log+': '+(result.error??result.status));}
run(['restore','CpuCostWindowSelftest.csproj','--source',empty,'-p:NuGetAudit=false'],'restore.log');
const profiles=[];
for(const profile of ['Release','Debug']){
    run(['build','CpuCostWindowSelftest.csproj','--no-restore','-c',profile,'--nologo'],'build-'+profile+'.log');
    const resultFile=path.join(out,profile.toLowerCase()+'-result.json');
    run([path.join(out,'bin',profile,'net10.0','CpuCostWindowSelftest.dll'),resultFile],'run-'+profile+'.log');
    const result=JSON.parse(fs.readFileSync(resultFile,'utf8'));
    if(!result.pass)throw Error('Fixture result failed');
    profiles.push({profile,...result});
    console.log(profile+': '+result.groups+' groups, '+result.checks+' assertion executions');
}
const manifest={sourceProgramSha256:sha(programBytes),actualMethodsSha256:sha(joined),helperSha256:sha(helper),fixtureTemplateSha256:sha(fixtureBytes),expandedFixtureSha256:sha(expanded),runnerSha256:sha(fs.readFileSync(fileURLToPath(import.meta.url))),seams,externalBoundaries:'Fake process/thread cumulative values, monotonic clock, flag and output sink; no live program, OS counter reads, SDK or device calls',helperProductionBytesCopiedExactly:true,sourceStableDuringRun:sha(fs.readFileSync(programPath))===sha(programBytes)&&sha(fs.readFileSync(helperPath))===sha(helper)};
if(!manifest.sourceStableDuringRun)throw Error('Source drift');
fs.writeFileSync(path.join(out,'source-manifest.json'),JSON.stringify(manifest,null,2)+'\n');
const result={pass:true,runDirectory:out,manifest,profiles};
fs.writeFileSync(path.join(out,'result.json'),JSON.stringify(result,null,2)+'\n');
fs.writeFileSync(path.join(root,'final-result.json'),JSON.stringify(result,null,2)+'\n');
console.log(out);
