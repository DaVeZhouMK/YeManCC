#!/usr/bin/env node
// Current-source diagnostic fixture: all external boundaries are value/throw fakes.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';
const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const argv = process.argv.slice(2);
if (argv[0] === '--cost-retention') {
    runCostRetention();
} else if (argv[0] === '--flag-reader-storm') {
    runFlagReaderStorm();
} else {
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

}

// Explicit selection of the existing file reader; default cost-window tests above are unchanged.
function runFlagReaderStorm() {
    const chosen = argv.includes('--case') ? argv[argv.indexOf('--case') + 1] : null;
    const expectedLength = chosen ? 5 : 3;
    if (argv.length !== expectedLength || argv[0] !== '--flag-reader-storm' || argv[1] !== '--out' ||
        !argv[2] || !path.isAbsolute(argv[2]) || (chosen && (argv[3] !== '--case' || !/^[a-z0-9-]+$/.test(chosen))))
        throw Error('Usage: --flag-reader-storm --out absolute-owned-evidence-root [--case name]');
    const root = fs.realpathSync(argv[2]);
    if (fs.lstatSync(argv[2]).isSymbolicLink()) throw Error('Owned root cannot be a link');
    const owner = JSON.parse(fs.readFileSync(path.join(root, 'OWNED.json'), 'utf8'));
    if (owner.consumer !== 'tools/inputhost_cpu_cost_window_selftest.mjs' || owner.member !== 'input-host-cost-enabled.flag')
        throw Error('Owned marker mismatch');
    const out = path.join(root, 'run-' + new Date().toISOString().replace(/[-:.]/g, ''));
    fs.mkdirSync(out);
    fs.writeFileSync(path.join(out, 'OWNED.json'), JSON.stringify(owner, null, 2) + '\n');
    const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
    const pin = file => { const bytes = fs.readFileSync(file); return { path: file, bytes: bytes.length, sha256: sha(bytes) }; };
    const programPath = path.join(repo, 'InputHost/Program.cs');
    const fixturePath = path.join(repo, 'tools/inputhost_cpu_cost_window_selftest.cs');
    const runnerPath = fileURLToPath(import.meta.url);
    const programBytes = fs.readFileSync(programPath), fixtureBytes = fs.readFileSync(fixturePath);
    const begin = Buffer.from('        private static bool CostLoggingRequestedByFlag()');
    const finish = Buffer.from('        internal bool CostEnabledForRead');
    const a = programBytes.indexOf(begin), b = programBytes.indexOf(finish, a);
    if (a < 0 || b <= a || programBytes.indexOf(begin, a + 1) >= 0) throw Error('Non-unique actual reader span');
    const actual = programBytes.subarray(a, b);
    const sectionBegin = Buffer.from('// FLAG_READER_FIXTURE_BEGIN\r\n');
    const sectionEnd = Buffer.from('// FLAG_READER_FIXTURE_END');
    const sa = fixtureBytes.indexOf(sectionBegin), sb = fixtureBytes.indexOf(sectionEnd, sa);
    if (sa < 0 || sb <= sa || fixtureBytes.indexOf(sectionBegin, sa + 1) >= 0) throw Error('Fixture section anchors');
    const selectedFixture = fixtureBytes.subarray(sa + sectionBegin.length, sb);
    const marker = Buffer.from('    // ACTUAL_FLAG_READER_INSERTED_BY_RUNNER');
    const mi = selectedFixture.indexOf(marker);
    if (mi < 0 || selectedFixture.indexOf(marker, mi + 1) >= 0) throw Error('Fixture reader insertion marker');
    const expanded = Buffer.concat([selectedFixture.subarray(0, mi), actual, selectedFixture.subarray(mi + marker.length)]);
    if (!expanded.includes(actual)) throw Error('Verbatim extraction failed');
    const sourceFile = path.join(out, 'FlagReaderFixture.cs');
    fs.writeFileSync(sourceFile, expanded);
    fs.writeFileSync(path.join(out, 'actual-reader.txt'), actual);
    const project = path.join(out, 'FlagReaderSelection.csproj');
    fs.writeFileSync(project, `<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net10.0</TargetFramework><ImplicitUsings>disable</ImplicitUsings><Nullable>enable</Nullable><EnableDefaultCompileItems>false</EnableDefaultCompileItems><NuGetAudit>false</NuGetAudit><RuntimeFrameworkVersion>10.0.11</RuntimeFrameworkVersion><RollForward>Disable</RollForward><UseSharedCompilation>false</UseSharedCompilation><StartupObject>InputHostFlagReaderStorm</StartupObject></PropertyGroup><ItemGroup><Compile Include="FlagReaderFixture.cs" /></ItemGroup></Project>\n`);
    const dotnet = path.join(process.env.ProgramFiles || 'C:/Program Files', 'dotnet/dotnet.exe');
    if (!fs.existsSync(dotnet)) throw Error('Existing offline dotnet tool missing');
    const originalInputs = [programPath, fixturePath, runnerPath].map(pin);
    const empty = path.join(out, 'empty-nuget-source'); fs.mkdirSync(empty);
    const budgetPath = path.join(root, 'BUDGET.json');
    const budget = fs.existsSync(budgetPath) ? JSON.parse(fs.readFileSync(budgetPath, 'utf8')) : { limitMs: 60000, spentMs: 0, records: [] };
    const records = [];
    function run(label, args) {
        const remaining = budget.limitMs - budget.spentMs;
        if (remaining <= 0) throw Error('Selected cumulative budget exhausted');
        const start = new Date().toISOString(), stamp = performance.now();
        const result = spawnSync(dotnet, args, { cwd: out, encoding: 'utf8', timeout: Math.max(1, Math.floor(remaining)), maxBuffer: 5 * 1024 * 1024,
            env: { ...process.env, DOTNET_CLI_TELEMETRY_OPTOUT: '1', DOTNET_SKIP_FIRST_TIME_EXPERIENCE: '1' } });
        const elapsedMs = Math.round((performance.now() - stamp) * 1000) / 1000;
        const stdout = path.join(out, label + '.stdout.txt'), stderr = path.join(out, label + '.stderr.txt');
        fs.writeFileSync(stdout, result.stdout || ''); fs.writeFileSync(stderr, result.stderr || '');
        const record = { label, pid: result.pid, command: [dotnet, ...args], cwd: out, startedUtc: start, endedUtc: new Date().toISOString(), elapsedMs,
            exitCode: result.status, signal: result.signal, error: result.error ? String(result.error) : null,
            naturalTerminal: !result.error && result.signal === null, stdout: pin(stdout), stderr: pin(stderr) };
        budget.spentMs += elapsedMs; budget.records.push(record); records.push(record);
        fs.writeFileSync(budgetPath, JSON.stringify(budget, null, 2) + '\n');
        fs.writeFileSync(path.join(out, label + '-PROCESS.json'), JSON.stringify(record, null, 2) + '\n');
        return result;
    }
    const restore = run('RESTORE', ['restore', project, '--source', empty, '-p:NuGetAudit=false', '--disable-build-servers']);
    let build = null, execute = null;
    if (restore.status === 0 && !restore.error) {
        build = run('BUILD', ['build', project, '--no-restore', '-c', 'Release', '--nologo', '--disable-build-servers', '-v:normal', '-bl:' + path.join(out, 'build.binlog')]);
        if (build.status === 0 && !build.error) execute = run('RUN', [path.join(out, 'bin/Release/net10.0/FlagReaderSelection.dll'), out, ...(chosen ? [chosen] : [])]);
    }
    const resultFile = path.join(out, 'RESULT.json');
    const result = fs.existsSync(resultFile) ? JSON.parse(fs.readFileSync(resultFile, 'utf8')) : null;
    const currentInputs = [programPath, fixturePath, runnerPath].map(pin);
    const sourceDrift = originalInputs.filter((entry, index) => entry.sha256 !== currentInputs[index].sha256);
    const generated = [sourceFile, project, path.join(out, 'obj/project.assets.json')];
    if (fs.existsSync(path.join(out, 'obj/Release/net10.0'))) {
        for (const name of fs.readdirSync(path.join(out, 'obj/Release/net10.0')))
            if (name.endsWith('.cs')) generated.push(path.join(out, 'obj/Release/net10.0', name));
    }
    const manifest = { status: result ? 'EXECUTED_CURRENT_READER' : 'PREPARATION_FAILED', program: originalInputs[0], runner: originalInputs[2], fixture: originalInputs[1],
        actualReader: { startByte: a, endByte: b, bytes: actual.length, sha256: sha(actual), verbatim: expanded.includes(actual) },
        actualReaderFile: pin(path.join(out, 'actual-reader.txt')), expandedFixture: pin(sourceFile), generatedInputs: generated.filter(fs.existsSync).map(pin),
        productCompiledAsWhole: false, hostProfileOrSdkConstructed: false, sourceDrift, tools: [pin(dotnet)], compiledProfile: 'Release only',
        selected: chosen || 'flag-reader-only', defaultCostLifecycleOrWriterGroupsRun: false,
        budget: { limitMs: budget.limitMs, spentMs: budget.spentMs, mutableLedger: budgetPath } };
    fs.writeFileSync(path.join(out, 'SOURCE-MANIFEST.json'), JSON.stringify(manifest, null, 2) + '\n');
    fs.writeFileSync(path.join(out, 'BUDGET-SNAPSHOT.json'), fs.readFileSync(budgetPath));
    const summary = { status: result?.status || 'PREPARATION_FAILED', out, result: result ? pin(resultFile) : null,
        manifest: pin(path.join(out, 'SOURCE-MANIFEST.json')), records, sourceDrift,
        counts: result ? { cases: result.cases, passed: result.passed, failed: result.failed, assertions: result.assertions, fixtureErrors: result.fixtureErrors } : null };
    fs.writeFileSync(path.join(out, 'PROCESS.json'), JSON.stringify(summary, null, 2) + '\n');
    fs.writeFileSync(path.join(root, 'final-result.json'), JSON.stringify(summary, null, 2) + '\n');
    console.log(JSON.stringify({ status: summary.status, out, ...summary.counts, budgetSpentMs: budget.spentMs, sourceDrift }));
    if (sourceDrift.length || !execute || execute.error || execute.status !== 0 || result?.fixtureErrors) process.exitCode = 1;
}


// Bounded file mode of the existing inputhost.cpu-cost diagnostics fixture.
function runCostRetention() {
    if(argv.length!==5||argv[1]!=='--out'||argv[3]!=='--baseline-root')throw Error('Usage: --cost-retention --out evidence-root --baseline-root old-InputHost-directory');
    const out=path.resolve(argv[2],'run-'+new Date().toISOString().replace(/[-:.]/g,''));fs.mkdirSync(out,{recursive:true});
    const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
    const paths=['InputHost/HostCostLog.cs','InputHost/HostCostArchive.cs','tools/inputhost_cpu_cost_retention_selftest.cs'];
    const bindings=paths.map(relative=>{const bytes=fs.readFileSync(path.join(repo,relative));fs.writeFileSync(path.join(out,path.basename(relative)),bytes);return {relative,sha256:sha(bytes)};});
    const baselineRoot=path.resolve(argv[4]);
    for(const name of ['HostCostLog.cs','HostCostArchive.cs']) {
        const bytes=fs.readFileSync(path.join(baselineRoot,name));
        const text=bytes.toString('utf8').replace('namespace YeManInputHost.Diagnostics;','namespace YeManInputHost.Diagnostics.Baseline;');
        if(text===bytes.toString('utf8'))throw Error('baseline namespace anchor missing');
        fs.writeFileSync(path.join(out,'Baseline'+name),text);
        bindings.push({baseline:path.join(baselineRoot,name),sha256:sha(bytes),seam:'namespace-only;actual-original-writer-body'});
    }
    fs.writeFileSync(path.join(out,'CostRetention.csproj'),`<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net10.0</TargetFramework><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable><NuGetAudit>false</NuGetAudit></PropertyGroup></Project>\n`);
    const empty=path.join(out,'empty-nuget-source');fs.mkdirSync(empty);
    function run(args,log){const r=spawnSync('dotnet',args,{cwd:out,encoding:'utf8',timeout:90000,maxBuffer:3*1024*1024,env:{...process.env,DOTNET_CLI_TELEMETRY_OPTOUT:'1'}});fs.writeFileSync(path.join(out,log),(r.stdout??'')+(r.stderr??''));if(r.error||r.status!==0)throw Error(log+': '+(r.error??r.status));}
    run(['restore','CostRetention.csproj','--source',empty,'-p:NuGetAudit=false'],'restore.log');
    run(['build','CostRetention.csproj','--no-restore','-c','Release','--nologo'],'build.log');
    const files=path.join(out,'files');run([path.join(out,'bin/Release/net10.0/CostRetention.dll'),files],'run.log');
    const result=JSON.parse(fs.readFileSync(path.join(files,'RESULT.json'),'utf8'));
    const stable=bindings.filter(x=>x.relative).every(x=>sha(fs.readFileSync(path.join(repo,x.relative)))===x.sha256);
    const report={...result,bindings,sourceStable:stable,hostStarted:false,hardwareAccess:false,runDirectory:out};
    fs.writeFileSync(path.join(argv[2],'report.json'),JSON.stringify(report,null,2)+'\n');
    if(!stable||!result.pass)throw Error('cost-retention source/result rejected');
    console.log(JSON.stringify({pass:result.pass,checks:result.checks.length,production20MBCompactionMs:result.production20MBCompactionMs,report:path.join(argv[2],'report.json')}));
}
