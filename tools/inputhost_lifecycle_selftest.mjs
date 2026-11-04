import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { createOfflineHostFixture } from './msi_steamdeck_release_fence_1726_selftest.mjs';
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
const args=process.argv.slice(2),options=new Map();
for(let i=0;i<args.length;i++){
 const name=args[i];
 if(!['--out','--source-root','--fault','--configs','--compact-frame-reply','--compact-frame-reply-case','--compact-frame-reply-cost'].includes(name)||options.has(name))throw Error('Unknown or duplicate lifecycle option: '+name);
 if(name==='--compact-frame-reply'||name==='--compact-frame-reply-cost'){options.set(name,true);continue;}
 const value=args[++i];if(typeof value!=='string'||!value.trim()||value.startsWith('--'))throw Error('Missing or empty value: '+name);
 options.set(name,value);
}
const compact=options.has('--compact-frame-reply'),cost=options.has('--compact-frame-reply-cost'),selectedCase=options.get('--compact-frame-reply-case')??null;
if(compact&&cost)throw Error('Compact semantic and cost modes are mutually exclusive');
const configs=(options.get('--configs')??(cost?'Release':'Release,Debug')).split(',').map(s=>s.trim());
if(!configs.length||configs.some(c=>!['Release','Debug'].includes(c))||new Set(configs).size!==configs.length)throw Error('Empty, unknown or duplicate configuration selection');
if(selectedCase!==null&&!compact)throw Error('--compact-frame-reply-case requires --compact-frame-reply');
if(cost&&(configs.length!==1||configs[0]!=='Release'))throw Error('Cost mode requires exactly --configs Release');
const fault=options.get('--fault')??null;
if(cost&&(fault!==null||selectedCase!==null))throw Error('Cost mode has a fixed scope and accepts no fault/case selection');
const badBindingCase='compact: incorrect binding rejects before SDK and resumes at next sequence';
if(fault!==null&&(compact?fault!=='ignore-compact-binding'||selectedCase!==badBindingCase:fault!=='drop-dispose-retention'))throw Error('Unsupported fault/selection combination; compact fault requires the exact incorrect-binding case');
const testSource=path.join(repo,'tools/inputhost_lifecycle_selftest.cs');
const testText=fs.readFileSync(testSource,'utf8');
const declaredCompactCases=[...testText.matchAll(/tests\.Add\(\("([^"\r\n]+)",/g)].map(m=>m[1]);
if((compact||cost)&&(declaredCompactCases.length!==9||new Set(declaredCompactCases).size!==9||selectedCase!==null&&!declaredCompactCases.includes(selectedCase)))throw Error('Unknown, empty or drifted compact case selection');
const boundaryCase='compact: final UTF8 advertisement stays within message budget';
const costPersonas=[...testText.matchAll(/\/\* compact-cost-persona \*\/\s*\("([^"\r\n]+)",/g)].map(m=>m[1]);
if(cost&&(costPersonas.length!==3||new Set(costPersonas).size!==3||!declaredCompactCases.includes(boundaryCase)))throw Error('Cost persona or boundary declaration drift');
const costCases=costPersonas.flatMap(persona=>['off','on'].flatMap(imu=>['static','changing'].map(canonical=>`${persona}/imu-${imu}/${canonical}`)));
const selectedCases=cost?costCases:compact?(selectedCase===null?declaredCompactCases:[selectedCase]):null;
const preflightSelectedCases=cost?[boundaryCase]:null;
const out=path.resolve(options.get('--out')??'G:/YeManCC-Work/Build/Validation/VirtualController-Lifecycle-20261019/host/lifecycle-current');
const host=path.resolve(options.get('--source-root')??path.join(repo,'InputHost'));
if(fs.existsSync(out))throw Error('Fresh output required');fs.mkdirSync(out,{recursive:true});
const inputs=[],copies=[],processes=[],profiles=[],generatedFiles=[];
let receipt=[],generation=null,faultMutation=null;
const group=cost?'compact-frame-reply-cost':compact?'compact-frame-reply':'default-lifecycle';
function capture(file){if(!inputs.some(i=>i.file===file))inputs.push({file,bytes:fs.readFileSync(file)});}
function write(name,bytes){const file=path.join(out,name);fs.writeFileSync(file,bytes);generatedFiles.push({path:file,sha256:sha(fs.readFileSync(file))});}
function copy(name){const source=path.join(host,name);capture(source);write(name,fs.readFileSync(source));copies.push({source,destination:path.join(out,name),sha256:sha(fs.readFileSync(path.join(out,name)))});}
function stable(){return inputs.every(i=>fs.existsSync(i.file)&&sha(fs.readFileSync(i.file))===sha(i.bytes));}
function run(config,name,argv){
 const startedAt=new Date().toISOString(),start=performance.now();
 const maxBufferBytes=cost?16*1024*1024:undefined;
 const r=spawnSync('dotnet',argv,{cwd:out,timeout:60000,windowsHide:true,maxBuffer:maxBufferBytes});
 const stdout=r.stdout??Buffer.alloc(0),stderr=r.stderr??Buffer.alloc(0);
 const stdoutPath=path.join(out,config+'-'+name+'.stdout.log'),stderrPath=path.join(out,config+'-'+name+'.stderr.log');
 fs.writeFileSync(stdoutPath,stdout);fs.writeFileSync(stderrPath,stderr);fs.writeFileSync(path.join(out,config+'-'+name+'.log'),Buffer.concat([stdout,stderr]));
 const record={config,name,command:'dotnet',argv,cwd:out,maxBufferBytes:maxBufferBytes??null,pid:r.pid??null,startedAt,endedAt:new Date().toISOString(),elapsedMs:performance.now()-start,status:r.status,signal:r.signal,error:r.error?{name:r.error.name,code:r.error.code??null,message:r.error.message}:null,
  stdout:{path:stdoutPath,bytes:stdout.length,sha256:sha(stdout)},stderr:{path:stderrPath,bytes:stderr.length,sha256:sha(stderr)}};
 processes.push(record);return{record,stdout:stdout.toString('utf8')};
}
function finish(status,extra={}){
 const sourceStable=stable(),generatedStable=generatedFiles.every(f=>fs.existsSync(f.path)&&sha(fs.readFileSync(f.path))===f.sha256);
 const result={status:status==='PASS'&&(!sourceStable||!generatedStable)?'FAIL':status,group,selectedCase,selectedCases,preflightSelectedCases,configs,fault,faultMutation,sourceStable,generatedStable,profiles,processes,
  sources:inputs.map(i=>({path:i.file,bytes:i.bytes.length,sha256:sha(i.bytes)})),copies,generatedFiles,ranges:receipt,generation,
  legacyWholeFixtureStatus:(compact||cost)?'NOT_RUN_EXPLICIT_COMPACT_SELECTION':'FAIL_RETAINED_PROTOCOL_REJECTION_DETAIL_DRIFT',
  currentLifecycleStatus:(compact||cost)?'NOT_RUN_EXPLICIT_COMPACT_SELECTION':'PASS_IF_ALL_CURRENT_GROUPS_PASS',
  productEntryExecuted:false,hardwareIo:false,sdkInputAssembliesLoaded:false,motorOutputCallbackStatus:(compact||cost)?'NOT_RUN':'UNCHANGED_EXISTING_SCOPE',
  bindingMismatchContract:(compact||cost)?'After tuple and next-sequence admission: state-rejected/frame-reply-binding-mismatch consumes sequence, before SDK. Recovery uses next sequence. Nonce/tuple/grammar rejection does not consume sequence.':null,
  budgetScope:cost?'Consumes only the remaining time supplied by the original cumulative 20000ms OwnedJob account, including every prior semantic/boundary run. One Release build, one boundary preflight, fixed cost once; no retry, extension or new budget.':'No independent retry/budget allocation. Main owner must execute selected normal and one selected fault inside the same fixed 20s OwnedJob scope.',
  costDesign:cost?{personas:costPersonas,workloads:12,warmupPerArm:32,pairedRounds:11,framesPerRoundPerArm:128,expectedMeasuredHandleCalls:33792,expectedWarmupHandleCalls:768,fullArm:'Current-source newly prepared HostSession in legacy-compatible full reply mode; not a historical product baseline',speedThreshold:null,sdkWorkerCost:'NOT_MEASURED',motorCallbacks:'NOT_RUN'}:null,
  substitutions:['Existing offline generator and production parser/Handle/HandleLocked/prepare/release/serializer are reused.','SDK/PnP/neutral/frame/stop terminals and optional diagnostic writers are explicit inert injections; no real SDK frame encoder or product entry executes.','Default branch retains original legacy raw run and all lifecycle assertions. Compact branch never invokes either.','Actual HostResponseFields and CompactFrameReplyBinding/CompactFrameReplyFields converters are separate source copies.','AppendGameOutputObservation remains the extracted production method; no motor callback execution claimed.'],...extra};
 fs.writeFileSync(path.join(out,'result.json'),JSON.stringify(result,null,2)+'\n');
 console.log(JSON.stringify(result));if(result.status!=='PASS')process.exitCode=1;
}
try{
 const names=['Program.cs','PsEndpointOwnership.cs','HostGameOutputObservation.cs','HostControllerOutputStop.cs','HostControllerIdleCtsRelease.cs','MsiSteamDeckReleaseFence1726.cs','MsiVirtualReportChain1410.cs','ScreenTouchpadReport.cs','ScreenButtonReport.cs','CanonicalFrameFingerprintMemo.cs','HostResponseFields.cs'];
 for(const n of names)capture(path.join(host,n));
 for(const n of ['msi_steamdeck_release_fence_1726_selftest.mjs','msi_steamdeck_release_fence_1726_selftest.cs','inputhost_lifecycle_selftest.cs','inputhost_lifecycle_selftest.mjs'])capture(path.join(repo,'tools',n));
 const program=inputs.find(i=>i.file===path.join(host,'Program.cs')).bytes.toString('utf8');
 const hasCompact=/\bCompactFrameReplyBinding\s+_compactFrameReplies\s*=/.test(program);
 if((compact||cost)&&(!hasCompact||!program.includes('BindCompactFrameRepliesSafely(')||!program.includes('private string AppendCompactFrameReplyBinding(')||!/string\?\s+FrameReplyBinding\s*\{/.test(program))){finish('RUNTIME_PENDING',{error:'Program compact binding field, Bind/final-Append methods or nullable envelope property not yet wired'});}
 else{
  if(compact||cost||hasCompact)capture(path.join(host,'CompactFrameReplyBinding.cs'));
  const observation=inputs.find(i=>i.file===path.join(host,'HostGameOutputObservation.cs')).bytes.toString('utf8');
  if(observation.includes('HcControllerGameFeedbackCapabilityCatalog.ResolveToken'))capture(path.join(repo,'capabilities/HcControllerGameFeedbackCapabilityCatalog.generated.cs'));
  const generated=createOfflineHostFixture(out,host,true,{includePsOwnershipSupport:true,compactFrameReply:compact,compactFrameReplyCost:cost});
  let fake=generated.fake;receipt=generated.receipt;
  generation={source:path.join(host,'Program.cs'),sourceSha256:generated.sourceSha256,substitutions:generated.substitutions,scope:'Whole production members identified in ranges; only declared offline terminal/clock substitutions and the explicitly requested private mutation.'};
  if(generated.sourceSha256!==sha(inputs.find(i=>i.file===path.join(host,'Program.cs')).bytes))throw Error('Program changed during fixture generation');
  if(fault==='drop-dispose-retention'){
   const anchor='return RetainSdkDisposeFailure(errors, ownedBefore, out detail);';if(fake.split(anchor).length!==3)throw Error('SDK retention fault sites drifted');
   const replacement='_idleCtsReleasePending = false; _idleCtsRelease = null; /* private negative: forget failed Dispose and continue clearing owners */';
   fake=fake.replaceAll(anchor,replacement);faultMutation={file:path.join(out,'Extracted.cs'),anchor,replacement,count:2,privateGeneratedOnly:true};
  }
  if(fault==='ignore-compact-binding'){
   // A single negated Matches call is the actual rejection gate. The positive
   // Matches in SerializeResponse is intentionally left in place.
   const checks=[...fake.matchAll(/!_compactFrameReplies\.Matches\(\s*_controller\s*,\s*envelope\.FrameReplyBinding\s*\)/g)];
   if(checks.length!==1)throw Error('Actual compact rejection gate anchor drifted; refuse mutation');
   const anchor=checks[0][0],replacement='false /* private negative: ignore actual compact binding rejection */';
   fake=fake.replace(anchor,replacement);faultMutation={file:path.join(out,'Extracted.cs'),anchor,replacement,count:1,privateGeneratedOnly:true,case:badBindingCase};
  }
  write('Extracted.cs',fake);
  const legacy=inputs.find(i=>i.file===path.join(repo,'tools/msi_steamdeck_release_fence_1726_selftest.cs')).bytes.toString('utf8')
   .replace('internal static class Fixture','internal static partial class Fixture').replace('private static int Main()','private static int RunLegacyFenceTests()')
   .replace('Fixture.Reset(); body(); ++_tests;','Fixture.Reset(); if (ContinueLegacy) LegacyRecord(name, body); else body(); ++_tests;');
  write('Legacy.cs',legacy);write('Test.cs',inputs.find(i=>i.file===testSource).bytes);
  for(const name of ['MsiSteamDeckReleaseFence1726.cs','MsiVirtualReportChain1410.cs','CanonicalFrameFingerprintMemo.cs','HostResponseFields.cs',...(compact||cost||hasCompact?['CompactFrameReplyBinding.cs']:[])])copy(name);
  write('Entry.cs',`internal static partial class Program {
 private static int Main(string[] args){
#if COMPACT_FRAME_REPLY_COST
  if(args.Length==1 && args[0]=="--compact-frame-reply-cost")return CompactFrameReplyCostMain();
  if(args.Length==2 && args[0]=="--compact-frame-reply" && args[1]=="${boundaryCase}")return CompactFrameReplyMain(args[1]);
#elif COMPACT_FRAME_REPLY_FIXTURE
  if(args.Length is 1 or 2 && args[0]=="--compact-frame-reply")return CompactFrameReplyMain(args.Length==2?args[1]:null);
#else
  if(args.Length==1 && args[0]=="--legacy-original")return RunLegacyFenceTests();
  if(args.Length==0)return LifecycleMain(RunLegacyFenceTests);
#endif
  System.Console.Error.WriteLine("Unknown lifecycle fixture entry or empty selection");return 2;
 }
}`);
  write('Fixture.csproj','<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net10.0</TargetFramework><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable><NuGetAudit>false</NuGetAudit><EnableNETAnalyzers>false</EnableNETAnalyzers><UseAppHost>false</UseAppHost>'+(cost?'<DefineConstants>COMPACT_FRAME_REPLY_FIXTURE;COMPACT_FRAME_REPLY_COST</DefineConstants>':compact?'<DefineConstants>COMPACT_FRAME_REPLY_FIXTURE</DefineConstants>':'')+'</PropertyGroup></Project>');
  write('NuGet.Config','<configuration><packageSources><clear /></packageSources></configuration>');
  if(!stable())throw Error('Source drift before subprocess execution');
  for(const config of configs){
   const built=run(config,'build',['build','Fixture.csproj','-c',config,'--nologo','-v:q']);
   if(built.record.status!==0||built.record.error){profiles.push({config,exit:built.record.status,result:null,error:'Build failed; selected cases NOT_RUN'});break;}
   if(cost){
    const boundary=run(config,'compact-boundary-tests',[path.join(out,'bin',config,'net10.0/Fixture.dll'),'--compact-frame-reply',boundaryCase]);
    let boundaryResult=null;try{boundaryResult=JSON.parse(boundary.stdout.trim().split(/\r?\n/).at(-1));}catch{}
    const boundaryScopeMatches=boundaryResult?.group==='compact-frame-reply'&&boundaryResult?.legacyExecuted===false&&boundaryResult?.selectedCase===boundaryCase&&boundaryResult?.cases?.length===1&&boundaryResult.cases[0].name===boundaryCase;
    if(boundary.record.status!==0||boundary.record.error||!boundaryScopeMatches||boundaryResult?.passed!==true){profiles.push({config,exit:boundary.record.status,result:null,boundaryResult,boundaryScopeMatches,scopeMatches:false,error:'New boundary failed/incomplete; cost NOT_RUN'});break;}
    const measured=run(config,'compact-cost',[path.join(out,'bin',config,'net10.0/Fixture.dll'),'--compact-frame-reply-cost']);
    let result=null;try{result=JSON.parse(measured.stdout.trim().split(/\r?\n/).at(-1));}catch{}
    const names=result?.cases?.map(c=>c.name);
    const scopeMatches=result?.group==='compact-frame-reply-cost'&&result?.legacyExecuted===false&&result?.semanticSuiteExecuted===false&&Array.isArray(names)&&names.length===12&&names.every((n,i)=>n===costCases[i])&&result.measuredHandleCalls===33792&&result.warmupHandleCalls===768&&result.setupHandleCalls===72&&result.cleanupHandleCalls===24&&result.cases.every(c=>c.pass===true&&c.rounds?.length===11&&c.rounds.every((r,i)=>r.round===i+1&&r.order===(i%2===0?'AB':'BA')&&r.framesPerArm===128&&r.full?.payload?.validatedResponses===128&&r.compact?.payload?.validatedResponses===128));
    const rawPath=path.join(out,'compact-frame-reply-cost.raw.jsonl');let costRaw=null;
    if(fs.existsSync(rawPath)){const bytes=fs.readFileSync(rawPath);costRaw={path:rawPath,bytes:bytes.length,sha256:sha(bytes),declaredPathMatches:typeof result?.rawPath==='string'&&path.relative(out,path.resolve(result.rawPath))==='compact-frame-reply-cost.raw.jsonl'};}
    profiles.push({config,exit:measured.record.status,result,boundaryResult,boundaryScopeMatches,scopeMatches:scopeMatches&&!!costRaw?.bytes&&costRaw.declaredPathMatches,costRaw,processError:measured.record.error});
    break;
   }
   if(!compact){const legacyRun=run(config,'original-legacy-tests',[path.join(out,'bin',config,'net10.0/Fixture.dll'),'--legacy-original']);if(legacyRun.record.status!==1||legacyRun.record.error)throw Error('Original legacy contract drift was not reproduced');}
   const tests=run(config,'tests',[path.join(out,'bin',config,'net10.0/Fixture.dll'),...(compact?['--compact-frame-reply',...(selectedCase===null?[]:[selectedCase])]:[])]);
   let result=null;try{result=JSON.parse(tests.stdout.trim().split(/\r?\n/).at(-1));}catch{}
   const actualNames=result?.cases?.map(c=>c.name);
   const scopeMatches=!compact||result?.group==='compact-frame-reply'&&result?.legacyExecuted===false&&result?.selectedCase===selectedCase&&Array.isArray(actualNames)&&actualNames.length===selectedCases.length&&actualNames.every((n,i)=>n===selectedCases[i])&&!('legacyCases' in result);
   profiles.push({config,exit:tests.record.status,result,scopeMatches,processError:tests.record.error});
   if(tests.record.error||!scopeMatches||!result)break;
  }
  const passed=profiles.length===configs.length&&profiles.every(p=>p.exit===0&&p.result?.passed===true&&p.scopeMatches&&!p.processError);
  finish(passed?'PASS':'FAIL');
 }
}catch(error){finish('FAIL',{error:error.stack??String(error)});}
