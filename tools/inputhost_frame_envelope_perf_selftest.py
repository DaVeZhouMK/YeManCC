#!/usr/bin/env python3
"""R33 complete current-source parser A/B; prepare verifies only, timing is opt-in/held.
Synthetic data only. No Host project, SDK, session, pipe, device or archive execution.
"""
from pathlib import Path
import argparse, datetime, difflib, hashlib, json, os, re, subprocess, sys
sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT.parents[2] / 'Build/Validation/VirtualInputPerf-R33-20261015/frame-envelope-worker'
HOST_SHA = '5c4de79ae9125ad12ef24409902451047de16bbc23dd5e7748d83e3b35ede3a6'
AFFECTED = ['frame-ascii-stable','frame-ascii-moving','frame-unicode','frame-physical-ids','frame-raw-domain','frame-imu','frame-pads-steamdeck','frame-pads-dualsense','frame-pads-edge','frame-pads-ds4','frame-source-metadata','frame-invalid-metadata','frame-legacy-minimal','frame-reordered-escaped']
CONTROLS = ['nonframe-all-commands','nonframe-hidhide-physical','nonframe-prepare-metadata-fence','invalid-early','invalid-late-frame']
PLAN = dict(schemaVersion=1, profiles=['Release','Debug'], campaigns=2, pairedRounds=11,
    minimumFasterRounds=10, minimumPairedReduction=.05, minimumRatioReduction=.05,
    affectedFullParserWorkloads=AFFECTED, controlFullParserWorkloads=CONTROLS,
    maximumControlRegression=.05, allocationBytesMustNotIncrease=True,
    callsPerMeasurement=4096, allocationCalls=2048, globalWarmupMs=1500, scenarioWarmupMs=150,
    tieredCompilation='0', seed=2026101533, inputKind='SYNTHETIC; no field capture or archive',
    decision='Every affected row must pass all gates in both profiles and campaigns; every control ratio <=1.05; allocations may not increase on any row. One fixed formal attempt, no retiming or tuning.',
    timingHeld='Parent must review fresh correctness and bindings before explicit --performance-approved.')
def sha(data):
    return hashlib.sha256(data.encode('utf-8') if isinstance(data,str) else data).hexdigest()
def write(path,data):
    path=Path(path).resolve()
    if not path.is_relative_to(OUTPUT.resolve()): raise ValueError('Evidence outside worker directory')
    path.parent.mkdir(parents=True,exist_ok=True)
    path.write_bytes(data.encode('utf-8') if isinstance(data,str) else data)
def save(path,data): write(path,json.dumps(data,ensure_ascii=True,indent=2)+'\n')
def close_block(text,start):
    depth=0; mode='code'; i=start
    while i<len(text):
        c=text[i]; pair=text[i:i+2]
        if mode=='line':
            if c=='\n': mode='code'
        elif mode=='block':
            if pair=='*/': mode='code'; i+=1
        elif mode in ('string','char'):
            if c=='\\': i+=1
            elif c==('"' if mode=='string' else "'"): mode='code'
        elif pair=='//': mode='line'; i+=1
        elif pair=='/*': mode='block'; i+=1
        elif c=='"': mode='string'
        elif c=="'": mode='char'
        elif c=='{': depth+=1
        elif c=='}':
            depth-=1
            if depth==0: return i+1
        i+=1
    raise ValueError('Unclosed source block')
def extract(text,marker):
    if text.count(marker)!=1: raise ValueError('Nonunique source marker: '+marker)
    a=text.index(marker); end=close_block(text,text.index('{',a))
    if text[end:end+1]==';': end+=1
    return text[a:end]
def transform(parser):
    nl='\r\n' if '\r\n' in parser else '\n'
    ctor='new CommandEnvelope(command, requestId, sequence, runId, epoch, powerGeneration, targetId, ownerIdentity, persona, profileIdentity, startupNonce, configRevision, configHash)'
    changes=[]
    def replace_once(old,new):
        nonlocal parser
        if parser.count(old)!=1: raise ValueError('Candidate source anchor mismatch: '+old)
        parser=parser.replace(old,new,1); changes.append((old,new))
    replace_once('var parsed = '+ctor+';', 'CommandEnvelope? parsed = command == "SUBMIT_FRAME" ? null : '+ctor+';')
    replace_once('parsed = parsed with { MsiSteamDeckReleaseFence = releaseFence.GetBoolean() };', 'parsed = parsed! with { MsiSteamDeckReleaseFence = releaseFence.GetBoolean() };')
    replace_once('if (physicalBaseContainerInstanceId.Length != 0 || physicalDeviceInstanceId.Length != 0 ||'+nl+'                command.StartsWith("HIDHIDE_", StringComparison.Ordinal))',
        'if (command != "SUBMIT_FRAME" && (physicalBaseContainerInstanceId.Length != 0 || physicalDeviceInstanceId.Length != 0 ||'+nl+'                command.StartsWith("HIDHIDE_", StringComparison.Ordinal)))')
    replace_once('parsed = parsed with'+nl+'                {'+nl+'                    PhysicalBaseContainerInstanceId', 'parsed = parsed! with'+nl+'                {'+nl+'                    PhysicalBaseContainerInstanceId')
    replace_once('parsed = parsed with'+nl+'                {'+nl+'                    ScreenButtons=',
        'parsed = '+ctor+nl+'                {'+nl+'                    PhysicalBaseContainerInstanceId = physicalBaseContainerInstanceId,'+nl+'                    PhysicalDeviceInstanceId = physicalDeviceInstanceId,'+nl+'                    ScreenButtons=')
    replace_once('AttachReportChainMetadata1410Safely(parsed, properties,', 'AttachReportChainMetadata1410Safely(parsed!, properties,')
    return parser,changes
METHODS = ['TryParseEnvelope','AttachReportChainMetadata1410Safely','TryReadString','TryReadUInt64','TryReadInt32','TryReadShort','TryReadByte','TryReadAxis','TryReadTrigger','TryReadImuAxis','IsSha256','CanonicalFrameHash','AppendFrameHashBits']
def method(text,name):
    matches=list(re.finditer(r'^    private static [^\r\n]*\b'+re.escape(name)+r'\(',text,re.M))
    if len(matches)!=1: raise ValueError('Method signature missing/nonunique: '+name)
    return extract(text,matches[0].group(0).strip())
def generate(sources,baseline,candidate):
    src=sources['InputHost/Program.cs'].decode('utf-8-sig')
    pads=sources['InputHost/ScreenTouchpadReport.cs'].decode('utf-8-sig')
    buttons=sources['InputHost/ScreenButtonReport.cs'].decode('utf-8-sig')
    memo=sources['InputHost/CanonicalFrameFingerprintMemo.cs'].decode('utf-8-sig')
    record=extract(src,'private sealed record CommandEnvelope(')
    screen=re.search(r'private readonly record struct ScreenPad\([^;]+;',pads).group(0)
    constants='\n'.join(re.search(r'private const int '+n+r'\s*=\s*[^;]+;',src).group(0) for n in ['ProtocolVersion','MaxMessageBytes'])
    fields='\n'.join(extract(src,'private static readonly HashSet<string> '+n+' =') for n in ['BaseFields','FrameFields','HidHideFields'])
    cache=re.search(r'\[ThreadStatic\] private static Dictionary<string, JsonElement>\? t_fieldCache;',src).group(0)
    pieces={n:method(src,n) for n in METHODS}
    pieces.update({n:method(pads,n) for n in ['TryReadScreenPads','TryReadScreenPad']})
    pieces['TryReadScreenButtons']=method(buttons,'TryReadScreenButtons')
    memo_class=extract(memo,'internal static class CanonicalFrameFingerprintMemo').replace('internal static class','private static class',1)
    text='''using System;
using System.Collections.Generic;
using System.Text;
using System.Text.Json;
using System.Security.Cryptography;
using System.Runtime.CompilerServices;
using YeManInputHost.Diagnostics;
internal static class GeneratedParsers
{
'''+record.replace('private sealed record','internal sealed record',1)+'\n'+screen.replace('private readonly','internal readonly',1)+'\n'
    for name,parser in [('Baseline',baseline),('Candidate',candidate)]:
        body='\n'.join(parser if n=='TryParseEnvelope' else p for n,p in pieces.items())
        text+='internal static class '+name+' {\n'+constants+'\n'+fields+'\n'+cache+'\n'+body+'\n'+memo_class+'''
internal static bool Parse(string? s,out CommandEnvelope? e,out string error)=>TryParseEnvelope(s!,out e,out error);
internal static string Hash(int b,float x,float y,float z,float w,float l,float r)=>CanonicalFrameHash(b,x,y,z,w,l,r);
internal static object? Cache=>t_fieldCache;
internal static int CacheCount=>t_fieldCache?.Count ?? 0;
}\n'''
    fence=sources['InputHost/MsiSteamDeckReleaseFence1726.cs'].decode('utf-8-sig')
    constant=re.search(r'public const string PrepareField\s*=\s*[^;]+;',fence).group(0)
    command_part=baseline[baseline.index('if (command is not ('):baseline.index('error = "unknown-command";')]
    commands=re.findall(r'"([A-Z_]+)"',command_part)
    if len(commands)!=19 or len(set(commands))!=19:raise ValueError('Actual command whitelist changed')
    text+='internal static readonly string[] Commands = new[] { '+', '.join(json.dumps(c) for c in commands)+' };\n'
    text+='}\ninternal static class MsiSteamDeckReleaseFence1726 { '+constant+' }\n'
    return text,pieces,record,constant

def dotnet(run,args,stem,timeout=240):
    env=os.environ.copy();env.update(DOTNET_CLI_TELEMETRY_OPTOUT='1',DOTNET_NOLOGO='1',DOTNET_TieredCompilation='0',DOTNET_CLI_WORKLOAD_UPDATE_NOTIFY_DISABLE='true',DOTNET_SKIP_FIRST_TIME_EXPERIENCE='1')
    p=subprocess.run(['dotnet',*map(str,args)],cwd=run,capture_output=True,encoding='utf-8',errors='replace',env=env,timeout=timeout)
    write(run/(stem+'.stdout.log'),p.stdout);write(run/(stem+'.stderr.log'),p.stderr)
    return p

def prepare(stage_only=False):
    run=OUTPUT/('prepare-'+datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')+'-'+str(os.getpid()))
    run.mkdir(parents=True)
    paths=['InputHost/Program.cs','InputHost/MsiVirtualReportChain1410.cs','InputHost/MsiSteamDeckReleaseFence1726.cs','InputHost/ScreenTouchpadReport.cs','InputHost/ScreenButtonReport.cs','InputHost/CanonicalFrameFingerprintMemo.cs','InputHost/HostResponseFields.cs','InputHost/YeManInputHost.csproj']
    sources={p:(ROOT/p).read_bytes() for p in paths}
    if sha(sources[paths[0]])!=HOST_SHA: raise ValueError('Fresh Host drift: re-review current parser before preparing')
    audit_root=OUTPUT.parent
    audits={n:sha((audit_root/n).read_bytes()) for n in ['host-candidate-audit.md','source-binding.json']}
    native_before=sha((ROOT/'native/main.cpp').read_bytes())
    src=sources[paths[0]].decode('utf-8-sig');baseline=method(src,'TryParseEnvelope');candidate,changes=transform(baseline)
    reversed_candidate=candidate
    for old,new in reversed(changes):
        if reversed_candidate.count(new)!=1: raise ValueError('Nonunique candidate reversal')
        reversed_candidate=reversed_candidate.replace(new,old,1)
    assert reversed_candidate==baseline
    full_candidate=src.replace(baseline,candidate,1)
    assert full_candidate.replace(candidate,baseline,1)==src
    save(run/'acceptance-plan.json',PLAN)
    save(run/'timing-status.json',{'status':'HELD','reason':'Fresh correctness and bindings require parent review; no timing executed.'})
    for p,raw in sources.items():write(run/'snapshot'/p,raw)
    write(run/'baseline-parser.cs.txt',baseline);write(run/'candidate-parser.cs.txt',candidate)
    write(run/'candidate-Program.cs',(b'\xef\xbb\xbf' if sources[paths[0]].startswith(b'\xef\xbb\xbf') else b'')+full_candidate.encode('utf-8'))
    write(run/'candidate-UNAPPLIED.patch',''.join(difflib.unified_diff(baseline.splitlines(True),candidate.splitlines(True),fromfile='actual/TryParseEnvelope',tofile='candidate/TryParseEnvelope')))
    generated,pieces,record,fence=generate(sources,baseline,candidate)
    write(run/'GeneratedParsers.cs',generated)
    write(run/'ActualMetadata.cs',sources['InputHost/MsiVirtualReportChain1410.cs'])
    write(run/'Fixture.cs',(ROOT/'tools/inputhost_frame_envelope_perf_selftest.cs').read_bytes())
    project='''<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net10.0-windows</TargetFramework><AssemblyName>FrameEnvelopeFixture</AssemblyName><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable><PlatformTarget>x64</PlatformTarget><EnableDefaultCompileItems>false</EnableDefaultCompileItems><TreatWarningsAsErrors>true</TreatWarningsAsErrors></PropertyGroup><ItemGroup><Compile Include="Fixture.cs"/><Compile Include="GeneratedParsers.cs"/><Compile Include="ActualMetadata.cs"/></ItemGroup></Project>'''
    write(run/'FrameEnvelopeFixture.csproj',project)
    bindings=dict(schemaVersion=1,inputKind=PLAN['inputKind'],baselineAuthority='Full current on-disk TryParseEnvelope; no old source or binary baseline',
        sources=[dict(path=p,sha256=sha(raw),bytes=len(raw)) for p,raw in sources.items()],
        baselineParserSha256=sha(baseline),candidateParserSha256=sha(candidate),completeCandidateProgramSha256=sha((run/'candidate-Program.cs').read_bytes()),
        outsideParserUnchanged=True,exactReversalPassed=True,onlyCandidateChanges=[dict(before=a,after=b) for a,b in changes],
        unchangedActualMethods={n:sha(p) for n,p in pieces.items() if n!='TryParseEnvelope'},
        actualRecordSha256=sha(record),actualReleaseFenceConstant=fence,actualMetadataFileSha256=sha(sources['InputHost/MsiVirtualReportChain1410.cs']),
        metadataDependency='Full current MsiVirtualReportChain1410.cs compiled unmodified; not a diagnostic stub',
        productionExecuted=False,productionEdited=False,toolFixtureSha256=sha((ROOT/'tools/inputhost_frame_envelope_perf_selftest.cs').read_bytes()),
        toolDriverSha256=sha(Path(__file__).read_bytes()),generatedSha256=sha(generated),projectSha256=sha(project),planSha256=sha((run/'acceptance-plan.json').read_bytes()),
        threadState='Each variant contains its own unchanged actual ThreadStatic dictionary and its own actual nested CanonicalFrameFingerprintMemo; no per-request reset.',
        copiedTypeVisibilityOnly='CommandEnvelope and ScreenPad private -> internal in wrapper; memo class internal -> private nested. Parser/helper code unchanged except declared candidate edits.',
        parentNativeBeforeSha256=native_before,immutableAuditSha256=audits,oomParityClaim=False,performanceExecuted=False)
    save(run/'source-binding.json',bindings)
    if stage_only:
        save(run/'stage-status.json',dict(status='SOURCE_STAGED_ONLY',buildsExecuted=False,correctnessExecuted=False,performance='HELD',reason='Parent R34 timing hold; source extraction and writing only. Fresh prepare/build must follow explicit release.'))
        print(json.dumps(dict(run=str(run),status='SOURCE_STAGED_ONLY',buildsExecuted=False,correctnessExecuted=False,performance='HELD')))
        return
    empty=run/'empty-nuget-source';empty.mkdir()
    p=dotnet(run,['restore','FrameEnvelopeFixture.csproj','--source',empty,'-p:NuGetAudit=false'],'restore')
    if p.returncode:raise RuntimeError('Offline restore failed; see '+str(run))
    results=[]
    for profile in PLAN['profiles']:
        p=dotnet(run,['build','FrameEnvelopeFixture.csproj','-c',profile,'--no-restore','--nologo'],'build-'+profile)
        if p.returncode:raise RuntimeError('Build '+profile+' failed; see '+str(run/('build-'+profile+'.stdout.log')))
        p=dotnet(run,[run/'bin'/profile/'net10.0-windows/FrameEnvelopeFixture.dll','verify',run,profile],profile+'-verify',600)
        if p.returncode:raise RuntimeError('Correctness '+profile+' failed; see '+str(run/(profile+'-verify.stderr.log')))
        result=json.loads((run/(profile+'-correctness.json')).read_text());results.append(result)
        print(profile+': '+str(result['checks'])+' assertions / '+str(result['cases'])+' cases',flush=True)
    for path,raw in sources.items():
        if (ROOT/path).read_bytes()!=raw:raise RuntimeError('Live source drift: '+path)
    for n,h in audits.items():
        if sha((audit_root/n).read_bytes())!=h:raise RuntimeError('Parent audit changed: '+n)
    bindings['liveSourcesRechecked']=True;bindings['parentNativeAfterSha256']=sha((ROOT/'native/main.cpp').read_bytes())
    bindings['compiledArtifacts']=[dict(path=str(p.relative_to(run)),sha256=sha(p.read_bytes()),bytes=p.stat().st_size) for profile in PLAN['profiles'] for p in (run/'bin'/profile/'net10.0-windows').glob('*') if p.is_file()]
    save(run/'source-binding.json',bindings)
    save(run/'correctness-result.json',dict(passAll=all(r['pass'] for r in results),profiles=results,performance='HELD; not run',scope='Full current-source parser; synthetic data; no SDK/session/pipe/device',oomParityClaim=False))
    save(OUTPUT/'latest-prepare.json',dict(run=str(run),readyForParentReview=True,performance='HELD'))
    print(json.dumps(dict(run=str(run),readyForParentReview=True,performance='HELD',productionEdited=False)))

def benchmark(run,approved):
    if not approved:raise ValueError('Performance HELD: parent review + --performance-approved required')
    run=run.resolve()
    if not run.is_relative_to(OUTPUT.resolve()):raise ValueError('Not an R33 worker run')
    if (run/'formal-attempt.json').exists() or (OUTPUT/'formal-attempt.json').exists():raise ValueError('R33 formal attempt already consumed; no retiming')
    binding=json.loads((run/'source-binding.json').read_text())
    if json.loads((run/'acceptance-plan.json').read_text())!=PLAN:raise ValueError('Frozen plan changed')
    if not json.loads((run/'correctness-result.json').read_text())['passAll']:raise ValueError('Fresh correctness not passed')
    for source in binding['sources']:
        if sha((ROOT/source['path']).read_bytes())!=source['sha256']:raise ValueError('Live source drift: '+source['path'])
    for name,key in [('Fixture.cs','toolFixtureSha256'),('GeneratedParsers.cs','generatedSha256'),('FrameEnvelopeFixture.csproj','projectSha256'),('acceptance-plan.json','planSha256')]:
        if sha((run/name).read_bytes())!=binding[key]:raise ValueError('Frozen artifact changed: '+name)
    if sha(Path(__file__).read_bytes())!=binding['toolDriverSha256']:raise ValueError('Current driver differs from reviewed build')
    if sha((ROOT/'tools/inputhost_frame_envelope_perf_selftest.cs').read_bytes())!=binding['toolFixtureSha256']:raise ValueError('Current C# tool differs from reviewed build')
    for name,key in [('ActualMetadata.cs','actualMetadataFileSha256'),('baseline-parser.cs.txt','baselineParserSha256'),('candidate-parser.cs.txt','candidateParserSha256'),('candidate-Program.cs','completeCandidateProgramSha256')]:
        if sha((run/name).read_bytes())!=binding[key]:raise ValueError('Bound source artifact changed: '+name)
    for row in binding['compiledArtifacts']:
        if sha((run/row['path']).read_bytes())!=row['sha256']:raise ValueError('Compiled artifact changed')
    with (OUTPUT/'formal-attempt.json').open('x',encoding='utf-8') as formal:
        json.dump(dict(run=str(run),started=datetime.datetime.now(datetime.timezone.utc).isoformat(),scope='Single R33 fixed formal attempt; never remove to retime'),formal,indent=2)
    save(run/'formal-attempt.json',dict(started=datetime.datetime.now(datetime.timezone.utc).isoformat(),authorization='Explicit --performance-approved after parent review',planSha256=binding['planSha256']))
    rows=[]
    for profile in PLAN['profiles']:
        for campaign in [1,2]:
            stem=f'{profile}-campaign-{campaign}'
            p=dotnet(run,[run/'bin'/profile/'net10.0-windows/FrameEnvelopeFixture.dll','benchmark',run,profile,str(campaign)],stem,600)
            if p.returncode not in [0,2]:raise RuntimeError('Formal invocation failed: '+stem)
            result=json.loads((run/(stem+'.json')).read_text());rows.append(result)
            print(stem+': '+str(sum(r['pass'] for r in result['rows']))+'/'+str(len(result['rows'])),flush=True)
    save(run/'benchmark-result.json',dict(accepted=all(r['pass'] for r in rows),campaigns=rows,plan=PLAN,wholeProcessClaim=False,productionEdited=False))
    print(json.dumps(dict(accepted=all(r['pass'] for r in rows),run=str(run))))
if __name__=='__main__':
    ap=argparse.ArgumentParser();ap.add_argument('--benchmark-run',type=Path);ap.add_argument('--performance-approved',action='store_true');ap.add_argument('--stage-only',action='store_true');args=ap.parse_args()
    if args.stage_only and args.benchmark_run:raise ValueError('Source staging cannot start performance')
    benchmark(args.benchmark_run,args.performance_approved) if args.benchmark_run else prepare(args.stage_only)
