import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import os from 'node:os';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { createOfflineHostFixture } from './msi_steamdeck_release_fence_1726_selftest.mjs';
import {readInputHostRuntimePool,runtimePoolProbeSource,readRuntimePoolReceipts} from './lib/inputhost_runtime_pool.mjs';
import {buildTargets,recordEvaluatedInputs} from './ymcc_sim/global/inputhost-protocol/provenance.mjs';
import {managedFixtureEnvironment} from './ymcc_sim/global/controller-signal-evidence/environment.mjs';
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
const args=process.argv.slice(2),options=new Map();
for(let i=0;i<args.length;i++){
 const name=args[i];
 if(!['--out','--source-root','--fault','--configs','--compact-frame-reply','--compact-frame-reply-case','--compact-frame-reply-cost','--compact-frame-request-cost','--runtime-pool','--close-refresh','--close-refresh-case','--warm-probe-reference'].includes(name)||options.has(name))throw Error('Unknown or duplicate lifecycle option: '+name);
 if(name==='--compact-frame-reply'||name==='--compact-frame-reply-cost'||name==='--compact-frame-request-cost'||name==='--close-refresh'){options.set(name,true);continue;}
 const value=args[++i];if(typeof value!=='string'||!value.trim()||value.startsWith('--'))throw Error('Missing or empty value: '+name);
 options.set(name,value);
}
const closeRefresh=options.has('--close-refresh');
const closeSelectedCase=options.get('--close-refresh-case')??null;
if(closeSelectedCase!==null&&(!closeRefresh||!['completion-pending-poll','warm-resource-trend','warm-resource-probe-only'].includes(closeSelectedCase)))throw Error('Unknown or invalid close-refresh case');
if(closeRefresh&&[...options.keys()].some(k=>!['--close-refresh','--close-refresh-case','--out','--source-root','--configs','--runtime-pool','--warm-probe-reference'].includes(k)))throw Error('Close refresh mode accepts no unrelated selections');
const closeCases=closeSelectedCase===null?['queued-shutdown','active-shutdown','release-target','terminal-release','no-target-release','normal-completion']:[closeSelectedCase];
const warmProbeOnly=closeSelectedCase==='warm-resource-probe-only';
const warmResources=closeSelectedCase==='warm-resource-trend'||warmProbeOnly;
if(options.has('--warm-probe-reference')!==warmProbeOnly)throw Error('--warm-probe-reference is required only for warm-resource-probe-only');
const requestCost=options.has('--compact-frame-request-cost');
if(requestCost&&options.has('--compact-frame-reply-cost'))throw Error('Request and reply cost variants are mutually exclusive');
const compact=options.has('--compact-frame-reply'),cost=options.has('--compact-frame-reply-cost')||requestCost,selectedCase=options.get('--compact-frame-reply-case')??null;
if(compact&&cost)throw Error('Compact semantic and cost modes are mutually exclusive');
const configs=(options.get('--configs')??(cost?'Release':'Release,Debug')).split(',').map(s=>s.trim());
if(!configs.length||configs.some(c=>!['Release','Debug'].includes(c))||new Set(configs).size!==configs.length)throw Error('Empty, unknown or duplicate configuration selection');
if(warmResources&&(configs.length!==1||configs[0]!=='Release'))throw Error('Warm resource measurement requires exactly --configs Release');
if(selectedCase!==null&&!compact)throw Error('--compact-frame-reply-case requires --compact-frame-reply');
if(cost&&(configs.length!==1||configs[0]!=='Release'))throw Error('Cost mode requires exactly --configs Release');
const fault=options.get('--fault')??null;
if(cost&&(fault!==null||selectedCase!==null))throw Error('Cost mode has a fixed scope and accepts no fault/case selection');
const badBindingCase='compact: incorrect binding rejects before SDK and resumes at next sequence';
const requestGrammarCase='compact: current grammar rejects unknown typed and duplicate bindings';
if(fault!==null&&(compact?!((fault==='ignore-compact-binding'&&selectedCase===badBindingCase)||(fault==='allow-compact-mixed-identity'&&selectedCase===requestGrammarCase)):fault!=='drop-dispose-retention'))throw Error('Unsupported fault/selection combination; compact faults require their exact original case');
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
const poolProject=path.join(repo,'InputHost/YeManInputHost.csproj');
const runtimePool=readInputHostRuntimePool(poolProject,options.get('--runtime-pool')??'product');
if(fs.existsSync(out))throw Error('Fresh output required');fs.mkdirSync(out,{recursive:true});
const inputs=[],copies=[],processes=[],profiles=[],generatedFiles=[];
for(const file of [poolProject,path.join(repo,'tools/lib/inputhost_runtime_pool.mjs')])inputs.push({file,bytes:fs.readFileSync(file)});
let receipt=[],generation=null,faultMutation=null,warmProbeReference=null,compilerInputs=null,managedEnvironment=null;
const group=closeRefresh?'close-refresh-lifecycle':cost?(requestCost?'compact-frame-request-cost':'compact-frame-reply-cost'):compact?'compact-frame-reply':'default-lifecycle';
const recordCompilerInputs=!closeRefresh&&!compact&&!cost;
function capture(file){if(!inputs.some(i=>i.file===file))inputs.push({file,bytes:fs.readFileSync(file)});}
function write(name,bytes){const file=path.join(out,name);fs.writeFileSync(file,bytes,{flag:'wx'});generatedFiles.push({path:file,sha256:sha(fs.readFileSync(file))});}
function copy(name){const source=path.join(host,name);capture(source);write(name,fs.readFileSync(source));copies.push({source,destination:path.join(out,name),sha256:sha(fs.readFileSync(path.join(out,name)))});}
function stable(){return inputs.every(i=>fs.existsSync(i.file)&&sha(fs.readFileSync(i.file))===sha(i.bytes));}
const managedSpecPath=path.join(repo,'tools/ymcc_sim/global/inputhost-protocol/toolchain-inputs.json');
const managedSpecBytes=recordCompilerInputs?fs.readFileSync(managedSpecPath):null;
const managedSpec=managedSpecBytes?JSON.parse(managedSpecBytes.toString('utf8')):null;
const closeDotnet=managedSpec?path.join(managedSpec.dotnetRoot,'dotnet.exe'):path.resolve(repo,'../../../Environment/Toolchains/dotnet-sdk/10.0.400/dotnet.exe');
const closeEnv=recordCompilerInputs?managedFixtureEnvironment({ambient:process.env,dotnetRoot:managedSpec.dotnetRoot,out}):closeRefresh?{...process.env,DOTNET_ROOT:path.dirname(closeDotnet),PATH:path.dirname(closeDotnet)+';C:\\Windows\\System32;C:\\Windows',NUGET_PACKAGES:path.resolve(repo,'../../../Environment/Dependency-Caches/nuget-hc-candidate-global-packages'),DOTNET_SKIP_FIRST_TIME_EXPERIENCE:'1',DOTNET_NOLOGO:'1',MSBUILDDISABLENODEREUSE:'1',DOTNET_CLI_USE_MSBUILD_SERVER:'0'}:process.env;
function run(config,name,argv){
 const startedAt=new Date().toISOString(),start=performance.now();
 const maxBufferBytes=cost?16*1024*1024:undefined;
 const requestedDotnetSha256=sha(fs.readFileSync(closeDotnet));
 const r=spawnSync(closeDotnet,argv,{cwd:out,timeout:60000,windowsHide:true,maxBuffer:maxBufferBytes,env:closeEnv});
 const stdout=r.stdout??Buffer.alloc(0),stderr=r.stderr??Buffer.alloc(0);
 const stdoutPath=path.join(out,config+'-'+name+'.stdout.log'),stderrPath=path.join(out,config+'-'+name+'.stderr.log');
 fs.writeFileSync(stdoutPath,stdout,{flag:'wx'});fs.writeFileSync(stderrPath,stderr,{flag:'wx'});fs.writeFileSync(path.join(out,config+'-'+name+'.log'),Buffer.concat([stdout,stderr]),{flag:'wx'});
 const record={config,name,command:closeDotnet,argv,cwd:out,toolchain:closeRefresh?{dotnetSha256:requestedDotnetSha256,sdk:'10.0.400',env:{DOTNET_ROOT:closeEnv.DOTNET_ROOT,PATH:closeEnv.PATH,NUGET_PACKAGES:closeEnv.NUGET_PACKAGES,MSBUILDDISABLENODEREUSE:closeEnv.MSBUILDDISABLENODEREUSE}}:{requestedExecutable:closeDotnet,requestedExecutableSha256:requestedDotnetSha256,executableIdentityScope:'Requested absolute file before launch; loaded-module closure is not observed',declaredSdk:managedSpec?.sdkVersion??'10.0.400',environmentMode:recordCompilerInputs?'fixed-owned-managed-v1':'inherited',environmentQualified:recordCompilerInputs,environmentQualificationScope:recordCompilerInputs?'Constructed child variables and owned profile paths only; full dynamic loader is not observed':null},maxBufferBytes:maxBufferBytes??null,pid:r.pid??null,startedAt,endedAt:new Date().toISOString(),elapsedMs:performance.now()-start,status:r.status,signal:r.signal,error:r.error?{name:r.error.name,code:r.error.code??null,message:r.error.message}:null,
  stdout:{path:stdoutPath,bytes:stdout.length,sha256:sha(stdout)},stderr:{path:stderrPath,bytes:stderr.length,sha256:sha(stderr)}};
 processes.push(record);return{record,stdout:stdout.toString('utf8')};
}
function finish(status,extra={}){
 const sourceStable=stable(),generatedStable=generatedFiles.every(f=>fs.existsSync(f.path)&&sha(fs.readFileSync(f.path))===f.sha256);
 const actualRuntimePool=profiles.some(p=>p.result)?readRuntimePoolReceipts(out,runtimePool):[];
 const result={runtimePool,actualRuntimePool,status:status==='PASS'&&(!sourceStable||!generatedStable)?'FAIL':status,group,selectedCase,selectedCases,preflightSelectedCases,configs,fault,faultMutation,sourceStable,generatedStable,profiles,processes,
  sources:inputs.map(i=>({path:i.file,bytes:i.bytes.length,sha256:sha(i.bytes)})),copies,generatedFiles,ranges:receipt,generation,warmProbeReference,compilerInputs,managedEnvironment,
  legacyWholeFixtureStatus:warmResources?'NOT_RUN_EXPLICIT_RESOURCE_SELECTION':(compact||cost)?'NOT_RUN_EXPLICIT_COMPACT_SELECTION':'FAIL_RETAINED_PROTOCOL_REJECTION_DETAIL_DRIFT',
  currentLifecycleStatus:warmResources?'MEASUREMENT_INVARIANTS_ONLY_RESOURCE_STABILITY_NOT_ASSERTED':(compact||cost)?'NOT_RUN_EXPLICIT_COMPACT_SELECTION':'PASS_IF_ALL_CURRENT_GROUPS_PASS',
  productEntryExecuted:false,hardwareIo:false,sdkInputAssembliesLoaded:false,motorOutputCallbackStatus:(compact||cost)?'NOT_RUN':'UNCHANGED_EXISTING_SCOPE',
  bindingMismatchContract:(compact||cost)?'After tuple and next-sequence admission: state-rejected/frame-reply-binding-mismatch consumes sequence, before SDK. Recovery uses next sequence. Nonce/tuple/grammar rejection does not consume sequence.':null,
  budgetScope:warmProbeOnly?'Observer-only control with explicit original result reference; cumulative owner plan includes preparation and cleanup. Prior 900s trend account remains closed.':warmResources?'First explicit P2 warm OS-resource boundary; original 900s plan includes preparation; old P0/cost budgets remain closed.':closeRefresh?'Explicit managed P0 task, cumulative 600s including audit/preparation/build/run; no full suite':cost?'Consumes only the remaining time supplied by the original cumulative 20000ms OwnedJob account, including every prior semantic/boundary run. One Release build, one boundary preflight, fixed cost once; no retry, extension or new budget.':'No independent retry/budget allocation. Main owner must execute selected normal and one selected fault inside the same fixed 20s OwnedJob scope.',
  costDesign:cost?{personas:costPersonas,workloads:12,warmupPerArm:32,pairedRounds:11,framesPerRoundPerArm:128,expectedMeasuredHandleCalls:33792,expectedWarmupHandleCalls:768,fullArm:requestCost?'Current full request plus compact replies; same reply format in both arms':'Current-source newly prepared HostSession in legacy-compatible full reply mode; not a historical product baseline',requestCompaction:requestCost,speedThreshold:null,sdkWorkerCost:'NOT_MEASURED',motorCallbacks:'NOT_RUN'}:null,
  substitutions:['Existing offline generator and production parser/Handle/HandleLocked/prepare/release/serializer are reused.','SDK/PnP/neutral/frame/stop terminals and optional diagnostic writers are explicit inert injections; no real SDK frame encoder or product entry executes.','Default branch retains original legacy raw run and all lifecycle assertions. Compact branch never invokes either.','Actual HostResponseFields and CompactFrameReplyBinding/CompactFrameReplyFields converters are separate source copies.','AppendGameOutputObservation remains the extracted production method; no motor callback execution claimed.'],...extra};
 fs.writeFileSync(path.join(out,'result.json'),JSON.stringify(result,null,2)+'\n',{flag:'wx'});
 console.log(JSON.stringify(result));if(result.status!=='PASS')process.exitCode=1;
}
try{
 const names=['Program.cs','HostTerminalCleanup.cs','PsEndpointOwnership.cs','HostGameOutputObservation.cs','HostControllerOutputStop.cs','HostControllerIdleCtsRelease.cs','MsiSteamDeckReleaseFence1726.cs','MsiVirtualReportChain1410.cs','ScreenTouchpadReport.cs','ScreenButtonReport.cs','CanonicalFrameFingerprintMemo.cs','HostResponseFields.cs'];
 for(const n of names)capture(path.join(host,n));
 for(const n of ['msi_steamdeck_release_fence_1726_selftest.mjs','msi_steamdeck_release_fence_1726_selftest.cs','inputhost_lifecycle_selftest.cs','inputhost_lifecycle_selftest.mjs'])capture(path.join(repo,'tools',n));
 for(const n of ['provenance.mjs','adapter.mjs','toolchain-inputs.json'])capture(path.join(repo,'tools/ymcc_sim/global/inputhost-protocol',n));
 capture(path.join(repo,'tools/ymcc_sim/global/controller-signal-evidence/environment.mjs'));
 if(recordCompilerInputs){
  if(sha(inputs.find(i=>i.file===managedSpecPath).bytes)!==sha(managedSpecBytes))throw Error('Managed declaration changed before environment setup');
  for(const name of ['USERPROFILE','DOTNET_CLI_HOME','APPDATA','LOCALAPPDATA','TEMP','NUGET_PACKAGES','MSBuildUserExtensionsPath']){
   const folder=closeEnv[name],relative=path.relative(out,folder);if(path.isAbsolute(relative)||relative==='..'||relative.startsWith('..'+path.sep))throw Error('Managed private directory escaped output');fs.mkdirSync(folder,{recursive:true});
  }
  write('global.json',JSON.stringify({sdk:{version:managedSpec.sdkVersion,rollForward:'disable',allowPrerelease:false}}));
  write('Directory.Build.props','<Project><PropertyGroup><UseSharedCompilation>false</UseSharedCompilation><NuGetAudit>false</NuGetAudit></PropertyGroup></Project>');
  write('Directory.Build.targets','<Project />');
  const versions=relative=>fs.readdirSync(path.join(managedSpec.dotnetRoot,relative),{withFileTypes:true}).filter(e=>e.isDirectory()).map(e=>e.name).sort();
  const pin=file=>{const bytes=fs.readFileSync(file);return{path:file,bytes:bytes.length,sha256:sha(bytes)};};
  const variables=Object.fromEntries(Object.entries(closeEnv).sort(([a],[b])=>a.localeCompare(b)));
  const platform={platform:process.platform,arch:process.arch,release:os.release(),version:os.version()};
  const normalized=Object.fromEntries(Object.entries(variables).map(([name,value])=>[name,value.replaceAll(path.resolve(out),'{fixtureRoot}')]));
  managedEnvironment={schemaVersion:1,mode:'fixed-owned-managed-v1',variables,platform,normalizedIdentity:sha(JSON.stringify({variables:normalized,platform})),selectors:['global.json','Directory.Build.props','Directory.Build.targets'].map(name=>pin(path.join(out,name))),declaredSdk:managedSpec.sdkVersion,declaredRuntime:managedSpec.runtimeVersion,resolverMembership:{sdk:versions('sdk'),framework:versions('shared/Microsoft.NETCore.App'),fxr:versions('host/fxr')},hostfxr:pin(path.join(managedSpec.dotnetRoot,'host/fxr',managedSpec.runtimeVersion,'hostfxr.dll')),qualificationScope:'Fixed child environment/selectors and declared component membership; no dynamic loaded-module claim'};
 }
 const program=inputs.find(i=>i.file===path.join(host,'Program.cs')).bytes.toString('utf8');
  if(warmProbeOnly){
   const referencePath=path.resolve(options.get('--warm-probe-reference'));capture(referencePath);
   const referenceBytes=inputs.find(i=>i.file===referencePath).bytes;
   const reference=JSON.parse(referenceBytes.toString('utf8'));
   const profile=reference.profiles?.[0],measurement=profile?.result,row=measurement?.cases?.[0];
   if(reference.status!=='PASS'||reference.group!=='close-refresh-lifecycle'||reference.profiles?.length!==1||profile.config!=='Release'||profile.exit!==0||profile.scopeMatches!==true||measurement?.passed!==true||measurement.cases?.length!==1||row?.name!=='warm-resource-trend'||row.pass!==true||row.cycles!==3072||JSON.stringify(row.modeCounts)!=='[1024,1024,1024]'||row.parentsBound!==3072||row.parentsDisposed!==3072||row.parentHandlesClosed!==3072||row.retainedAtCancel!==2048||measurement.forcedGc!==false)throw Error('Warm reference scope/invariants mismatch');
   const samples=row.samples,phases=['cold','warm',...Array(6).fill('measured'),'idle-after'];
   const windows=[-1,0,1,2,3,4,5,6,7],cycleCounts=[0,768,1152,1536,1920,2304,2688,3072,3072];
   if(!Array.isArray(samples)||samples.length!==9||samples.some((s,i)=>s.phase!==phases[i]||s.window!==windows[i]||s.cycles!==cycleCounts[i]||!Number.isFinite(s.elapsedMs)||i>0&&s.elapsedMs<=samples[i-1].elapsedMs))throw Error('Warm reference sample schedule mismatch');
   for(const name of ['Program.cs','ParentProcessLifetime.cs']){
    const file=path.join(host,name);capture(file);const old=reference.sources?.find(i=>path.resolve(i.path)===file);
    if(!old||old.sha256!==sha(inputs.find(i=>i.file===file).bytes))throw Error('Warm reference product source differs: '+name);
   }
   const oldProject=reference.sources?.find(i=>path.resolve(i.path)===poolProject);
   if(!oldProject||oldProject.sha256!==sha(inputs.find(i=>i.file===poolProject).bytes)||reference.runtimePool?.requested!==runtimePool.requested||reference.runtimePool.expectedWindowsThreadPool!==runtimePool.expectedWindowsThreadPool)throw Error('Warm reference runtime pool differs');
   if(reference.actualRuntimePool?.length!==1)throw Error('Warm reference runtime receipt missing');
   const pool=reference.actualRuntimePool[0];capture(pool.path);
   const actualPool=JSON.parse(inputs.find(i=>i.file===pool.path).bytes.toString('utf8'));
   if(JSON.stringify(actualPool)!==JSON.stringify(pool.result)||actualPool.actualWindowsThreadPool!==runtimePool.expectedWindowsThreadPool||actualPool.expectedWindowsThreadPool!==runtimePool.expectedWindowsThreadPool)throw Error('Warm reference actual runtime receipt differs');
   const intervalMs=samples.slice(2,8).map((s,i)=>s.elapsedMs-samples[i+1].elapsedMs);
   if(intervalMs.some(ms=>!Number.isFinite(ms)||ms<=0||ms>10000))throw Error('Warm reference intervals out of bounds');
   warmProbeReference={path:referencePath,sha256:sha(referenceBytes),intervalMs,requestedDelayMs:intervalMs.map(Math.ceil),referenceCycles:row.cycles,referenceMeasurementIncludesWarmup:true,noAdditionalSettleDelay:true,productSourceAndPoolMatched:true};
  }
 const hasCompact=/\bCompactFrameReplyBinding\s+_compactFrameReplies\s*=/.test(program);
 if((compact||cost)&&(!hasCompact||!program.includes('BindCompactFrameRepliesSafely(')||!program.includes('private string AppendCompactFrameReplyBinding(')||!/string\?\s+FrameReplyBinding\s*\{/.test(program))){finish('RUNTIME_PENDING',{error:'Program compact binding field, Bind/final-Append methods or nullable envelope property not yet wired'});}
 else{
  if(compact||cost||hasCompact)capture(path.join(host,'CompactFrameReplyBinding.cs'));
  const observation=inputs.find(i=>i.file===path.join(host,'HostGameOutputObservation.cs')).bytes.toString('utf8');
  if(observation.includes('HcControllerGameFeedbackCapabilityCatalog.ResolveToken'))capture(path.join(repo,'capabilities/HcControllerGameFeedbackCapabilityCatalog.generated.cs'));
  const generated=createOfflineHostFixture(out,host,true,{includePsOwnershipSupport:true,compactFrameReply:compact,compactFrameReplyCost:cost,closeRefresh});
  let fake=generated.fake;receipt=generated.receipt;
  // The extracted SHUTDOWN dispatcher uses this process-entry-owned field.
  // Keep its actual declaration and helper; product Main is not run here, so
  // it remains null and these protocol cases do not arm a process deadline.
  const terminalExitFields=[...program.matchAll(/^[ \t]*private static HostTerminalCleanup\.ProcessExit\?[ \t]+_terminalProcessExit[ \t]*;[ \t]*\r?$/gm)];
  if(terminalExitFields.length!==1)throw Error('Actual process exit field declaration missing or ambiguous');
  const terminalExitField=terminalExitFields[0],programAnchor='internal static partial class Program {';
  if(fake.split(programAnchor).length!==2)throw Error('Offline Program process exit field anchor drifted');
  fake=fake.replace(programAnchor,programAnchor+'\n'+terminalExitField[0]+'\n');
  receipt.push({name:'_terminalProcessExit',file:path.join(host,'Program.cs'),line:program.slice(0,terminalExitField.index).split('\n').length,sha256:sha(terminalExitField[0])});
  copy('HostTerminalCleanup.cs');
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
  if(fault==='allow-compact-mixed-identity'){
   const start=fake.indexOf('                if (properties.ContainsKey("runId") || properties.ContainsKey("epoch") ||');
   const end=fake.indexOf('                { error = "compact-frame-request-mixed-identity"; return false; }',start);
   if(start<0||end<0)throw Error('Actual compact mixed-identity gate missing');
   const anchor=fake.slice(start,end),replacement='                if (false) /* private negative: permit mixed compact request identities */\n';
   if(fake.split(anchor).length!==2)throw Error('Compact mixed-identity gate not unique');
   fake=fake.replace(anchor,replacement);faultMutation={file:path.join(out,'Extracted.cs'),anchor,replacement,count:1,privateGeneratedOnly:true,case:requestGrammarCase};
  }
  if(warmResources)copy('ParentProcessLifetime.cs');
  if(warmProbeOnly)write('WarmResourceProbeSchedule.cs','internal static class WarmResourceProbeSchedule { internal static readonly int[] DelaysMs = new int[] {'+warmProbeReference.requestedDelayMs.join(',')+'}; }\n');
  write('Extracted.cs',fake);
  const legacy=inputs.find(i=>i.file===path.join(repo,'tools/msi_steamdeck_release_fence_1726_selftest.cs')).bytes.toString('utf8')
   .replace('internal static class Fixture','internal static partial class Fixture').replace('private static int Main()','private static int RunLegacyFenceTests()')
   .replace('Fixture.Reset(); body(); ++_tests;','Fixture.Reset(); if (ContinueLegacy) LegacyRecord(name, body); else body(); ++_tests;');
  write('Legacy.cs',legacy);write('Test.cs',inputs.find(i=>i.file===testSource).bytes);
  for(const name of ['MsiSteamDeckReleaseFence1726.cs','MsiVirtualReportChain1410.cs','CanonicalFrameFingerprintMemo.cs','HostResponseFields.cs',...(compact||cost||hasCompact?['CompactFrameReplyBinding.cs']:[])])copy(name);
  write('Entry.cs',`internal static partial class Program {
 private static int Main(string[] args){
#if CLOSE_REFRESH_FIXTURE
#if CLOSE_REFRESH_WARM_RESOURCE_FIXTURE
  if(args.Length==2 && args[0]=="--close-refresh" && args[1]=="${closeSelectedCase}")return CloseRefreshWarmResourcesMain(${warmProbeOnly?'true':'false'});
#endif
  if(args.Length==2 && args[0]=="--close-refresh" && args[1]=="completion-pending-poll")return CloseRefreshPollWindowMain();
  if(args.Length==1 && args[0]=="--close-refresh")return CloseRefreshMain();
#elif COMPACT_FRAME_REPLY_COST
  if(args.Length==1 && args[0]=="--compact-frame-reply-cost")return CompactFrameReplyCostMain();
  if(args.Length==1 && args[0]=="--compact-frame-request-cost")return CompactFrameReplyCostMain(true);
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
  write('RuntimePool.cs',runtimePoolProbeSource(runtimePool));
  if(recordCompilerInputs){
   const originalReferenceItem='@(ReferencePath-&gt;';
   if(buildTargets.split(originalReferenceItem).length!==2)throw Error('Original compiler recording reference item anchor drifted');
   const propertyNames=['NETCoreSdkVersion','RuntimeFrameworkVersion','RollForward','MSBuildSDKsPath','MSBuildToolsPath','RoslynTargetsPath','CscToolPath','CscToolExe','ProvideCommandLineArgs','SkipCompilerExecution','MSBuildEnableWorkloadResolver','CompilerResponseFile','Win32Manifest','Win32Resource','ApplicationIcon','SourceLink','AppConfigForCompiler','ResolvedCodeAnalysisRuleSet','DirectoryBuildPropsPath','DirectoryBuildTargetsPath'];
   const extra='<WriteLinesToFile File="$(MSBuildProjectDirectory)/compiler-commandline-$(Configuration).txt" Lines="@(CscCommandLineArgs)" Overwrite="true" Encoding="UTF-8" /><WriteLinesToFile File="$(MSBuildProjectDirectory)/compiler-properties-$(Configuration).txt" Lines="'+propertyNames.map(name=>name+'&#x9;$('+name+')').join(';')+'" Overwrite="true" Encoding="UTF-8" />';
   write('FixtureInputs.targets',buildTargets.replace(originalReferenceItem,'@(ReferencePathWithRefAssemblies-&gt;').replace('</Target>',extra+'</Target>'));
  }
  write('Fixture.csproj','<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net10.0</TargetFramework><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable><NuGetAudit>false</NuGetAudit><EnableNETAnalyzers>false</EnableNETAnalyzers><UseAppHost>false</UseAppHost>'+runtimePool.projectProperty+(recordCompilerInputs?'<RuntimeFrameworkVersion>'+managedSpec.runtimeVersion+'</RuntimeFrameworkVersion><RollForward>Disable</RollForward><ProvideCommandLineArgs>true</ProvideCommandLineArgs>':'')+(warmResources?'<UseSharedCompilation>false</UseSharedCompilation>':'')+(closeRefresh?'<DefineConstants>CLOSE_REFRESH_FIXTURE'+(warmResources?';CLOSE_REFRESH_WARM_RESOURCE_FIXTURE':'')+(warmProbeOnly?';CLOSE_REFRESH_WARM_PROBE_ONLY':'')+'</DefineConstants>':cost?'<DefineConstants>COMPACT_FRAME_REPLY_FIXTURE;COMPACT_FRAME_REPLY_COST</DefineConstants>':compact?'<DefineConstants>COMPACT_FRAME_REPLY_FIXTURE</DefineConstants>':'')+'</PropertyGroup>'+(recordCompilerInputs?'<Import Project="FixtureInputs.targets" />':'')+'</Project>');
  write('NuGet.Config','<configuration><packageSources><clear /></packageSources></configuration>');
  if(!stable())throw Error('Source drift before subprocess execution');
  for(const config of configs){
   const built=run(config,'build',['build','Fixture.csproj','-c',config,'--nologo','-v:q','-p:UseSharedCompilation=false','-nodeReuse:false','-p:RestoreConfigFile='+path.join(out,'NuGet.Config')]);
   if(built.record.status!==0||built.record.error){profiles.push({config,exit:built.record.status,result:null,error:'Build failed; selected cases NOT_RUN'});break;}
   if(cost){
    const boundary=run(config,'compact-boundary-tests',[path.join(out,'bin',config,'net10.0/Fixture.dll'),'--compact-frame-reply',boundaryCase]);
    let boundaryResult=null;try{boundaryResult=JSON.parse(boundary.stdout.trim().split(/\r?\n/).at(-1));}catch{}
    const boundaryScopeMatches=boundaryResult?.group==='compact-frame-reply'&&boundaryResult?.legacyExecuted===false&&boundaryResult?.selectedCase===boundaryCase&&boundaryResult?.cases?.length===1&&boundaryResult.cases[0].name===boundaryCase;
    if(boundary.record.status!==0||boundary.record.error||!boundaryScopeMatches||boundaryResult?.passed!==true){profiles.push({config,exit:boundary.record.status,result:null,boundaryResult,boundaryScopeMatches,scopeMatches:false,error:'New boundary failed/incomplete; cost NOT_RUN'});break;}
    const measured=run(config,'compact-cost',[path.join(out,'bin',config,'net10.0/Fixture.dll'),requestCost?'--compact-frame-request-cost':'--compact-frame-reply-cost']);
    let result=null;try{result=JSON.parse(measured.stdout.trim().split(/\r?\n/).at(-1));}catch{}
    const names=result?.cases?.map(c=>c.name);
    const scopeMatches=result?.group===group&&result?.requestCompaction===requestCost&&result?.legacyExecuted===false&&result?.semanticSuiteExecuted===false&&Array.isArray(names)&&names.length===12&&names.every((n,i)=>n===costCases[i])&&result.measuredHandleCalls===33792&&result.warmupHandleCalls===768&&result.setupHandleCalls===72&&result.cleanupHandleCalls===24&&result.cases.every(c=>c.pass===true&&c.rounds?.length===11&&c.rounds.every((r,i)=>r.round===i+1&&r.order===(i%2===0?'AB':'BA')&&r.framesPerArm===128&&r.full?.payload?.validatedResponses===128&&r.compact?.payload?.validatedResponses===128));
    const rawPath=path.join(out,group+'.raw.jsonl');let costRaw=null;
    if(fs.existsSync(rawPath)){const bytes=fs.readFileSync(rawPath);costRaw={path:rawPath,bytes:bytes.length,sha256:sha(bytes),declaredPathMatches:typeof result?.rawPath==='string'&&path.relative(out,path.resolve(result.rawPath))===group+'.raw.jsonl'};}
    profiles.push({config,exit:measured.record.status,result,boundaryResult,boundaryScopeMatches,scopeMatches:scopeMatches&&!!costRaw?.bytes&&costRaw.declaredPathMatches,costRaw,processError:measured.record.error});
    break;
   }
   if(!compact&&!closeRefresh){const legacyRun=run(config,'original-legacy-tests',[path.join(out,'bin',config,'net10.0/Fixture.dll'),'--legacy-original']);if(legacyRun.record.status!==1||legacyRun.record.error)throw Error('Original legacy contract drift was not reproduced');}
   const tests=run(config,'tests',[path.join(out,'bin',config,'net10.0/Fixture.dll'),...(closeRefresh?['--close-refresh',...(closeSelectedCase===null?[]:[closeSelectedCase])]:compact?['--compact-frame-reply',...(selectedCase===null?[]:[selectedCase])]:[])]);
   let result=null;try{result=JSON.parse(tests.stdout.trim().split(/\r?\n/).at(-1));}catch{}
   const actualNames=result?.cases?.map(c=>c.name);
   const scopeMatches=closeRefresh?result?.group==='close-refresh-lifecycle'&&JSON.stringify(actualNames)===JSON.stringify(closeCases):!compact||result?.group==='compact-frame-reply'&&result?.legacyExecuted===false&&result?.selectedCase===selectedCase&&Array.isArray(actualNames)&&actualNames.length===selectedCases.length&&actualNames.every((n,i)=>n===selectedCases[i])&&!('legacyCases' in result);
   profiles.push({config,exit:tests.record.status,result,scopeMatches,processError:tests.record.error});
   if(tests.record.error||!scopeMatches||!result)break;
  }
  const buildProcesses=processes.filter(p=>p.name==='build');
  if(recordCompilerInputs&&buildProcesses.length===configs.length&&buildProcesses.every(p=>p.status===0&&!p.error)){
   const declaration=JSON.parse(inputs.find(i=>i.file===path.join(repo,'tools/ymcc_sim/global/inputhost-protocol/toolchain-inputs.json')).bytes.toString('utf8'));
   const summary=recordEvaluatedInputs(out,{artifacts:declaration.pinned},{minimumLedgers:configs.length});
   const manifest=path.join(out,'evaluated-compiler-inputs.json');
   const pin=file=>{const bytes=fs.readFileSync(file);return{path:file,bytes:bytes.length,sha256:sha(bytes)};};
   compilerInputs={status:'RECORDED',...summary,manifest:pin(manifest),ledgerFiles:configs.map(config=>pin(path.join(out,'evaluated-inputs-'+config+'.txt'))),commandLines:configs.map(config=>pin(path.join(out,'compiler-commandline-'+config+'.txt'))),properties:configs.map(config=>pin(path.join(out,'compiler-properties-'+config+'.txt'))),referenceItem:'ReferencePathWithRefAssemblies',scope:'Original AfterCoreCompile recording target; actual Csc source/reference items and Analyzer members plus compiler arguments/properties, no product execution or loaded-module proof'};
  }
  const passed=profiles.length===configs.length&&profiles.every(p=>p.exit===0&&p.result?.passed===true&&p.scopeMatches&&!p.processError);
  finish(passed?'PASS':'FAIL');
 }
}catch(error){finish('FAIL',{error:error.stack??String(error)});}
