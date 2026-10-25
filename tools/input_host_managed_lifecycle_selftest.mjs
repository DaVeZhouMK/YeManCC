// Offline bounded integration of current HostSession with existing SDK 2.0 BCL fixtures.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {createOfflineHostFixture} from './msi_steamdeck_release_fence_1726_selftest.mjs';
import {extract} from './ymcc_sim/global/inputhost-protocol/extract-parser.mjs';
const self=fileURLToPath(import.meta.url),repo=path.resolve(path.dirname(self),'..');
const args=process.argv.slice(2),option=(key,fallback)=>{const i=args.indexOf(key);return i<0?fallback:args[i+1];};
const out=path.resolve(option('--out','G:/YeManCC-Work/Testing/YMCC/Evidence/VC-HostManaged23/run-001'));
const fault=option('--fault','none');
if(!['none','accept-late-output','skip-output-stop','skip-idle-stop','ignore-parent-cancellation'].includes(fault))throw Error('Unsupported semantic negative');
if(fs.existsSync(out))throw Error('Fresh evidence required: '+out);
fs.mkdirSync(out,{recursive:true});
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
const fixtureRel='tools/fixtures/input_host_managed_lifecycle_20261023';
const sdkRel='tools/fixtures/hidmaestro/2.0.0';
const sdkSpec=JSON.parse(fs.readFileSync(path.join(repo,sdkRel,'binary-binding.json')));
const sourceNames=['Program.cs','ScreenTouchpadReport.cs','ScreenButtonReport.cs','PsEndpointOwnership.cs','HostGameOutputObservation.cs','HostControllerOutputStop.cs','HostControllerIdleCtsRelease.cs','ParentProcessLifetime.cs','MsiSteamDeckReleaseFence1726.cs','MsiVirtualReportChain1410.cs','CanonicalFrameFingerprintMemo.cs','HostResponseFields.cs'];
const relativeInputs=[...sourceNames.map(n=>'InputHost/'+n),path.relative(repo,self),'tools/msi_steamdeck_release_fence_1726_selftest.mjs','tools/msi_steamdeck_release_fence_1726_selftest.cs','tools/inputhost_lifecycle_selftest.mjs','tools/inputhost_lifecycle_selftest.cs','tools/inputhost_parent_lifecycle_selftest.cs','tools/ymcc_sim/global/inputhost-protocol/extract-parser.mjs','tools/ymcc_sim/global/inputhost-protocol/provenance.mjs','tools/ymcc_sim/global/inputhost-protocol/toolchain-inputs.json','tools/ymcc_sim/global/host-sdk-stop/BclStopFixture.cs.in','capabilities/HcControllerGameFeedbackCapabilityCatalog.generated.cs',fixtureRel+'/Workers.cs.in',fixtureRel+'/ManagedCases.cs',sdkRel+'/binary-binding.json',...Object.keys(sdkSpec.members).map(n=>sdkRel+'/'+n)];
const inputs=[...new Set(relativeInputs)].map(relative=>{const file=path.join(repo,relative),bytes=fs.readFileSync(file),snapshot=path.join(out,'sources',relative);fs.mkdirSync(path.dirname(snapshot),{recursive:true});fs.writeFileSync(snapshot,bytes,{flag:'wx'});return {relative,path:file,snapshot,sha256:sha(bytes),bytes:bytes.length};});
const read=relative=>fs.readFileSync(inputs.find(r=>r.relative===relative).snapshot,'utf8').replace(/^\uFEFF/,'');
const write=(file,data)=>fs.writeFileSync(file,typeof data==='string'?data:JSON.stringify(data,null,2)+'\n');
write(path.join(out,'inputs-before.json'),inputs);
const dotnetSpec=JSON.parse(read('tools/ymcc_sim/global/inputhost-protocol/toolchain-inputs.json'));
const dotnet=path.join(dotnetSpec.dotnetRoot,'dotnet.exe');
const selectedToolchain=dotnetSpec.pinned.map(p=>({path:p.path,expected:p.sha256,actual:sha(fs.readFileSync(p.path))}));
if(selectedToolchain.some(p=>p.actual!==p.expected))throw Error('Pinned toolchain changed');
write(path.join(out,'toolchain.json'),selectedToolchain);
const sdkFile=path.join(repo,'deps/hidmaestro/HIDMaestro.Core.dll');
if(sha(fs.readFileSync(sdkFile))!==sdkSpec.sdkSha256||fs.statSync(sdkFile).size!==sdkSpec.sdkBytes)throw Error('Pinned SDK binary changed');
for(const [name,entry]of Object.entries(sdkSpec.members))if(sha(fs.readFileSync(path.join(repo,sdkRel,name)))!==entry.sha256)throw Error('Pinned method fixture changed: '+name);
const project=path.join(out,'integrated');fs.mkdirSync(project);
const hostSnapshot=path.join(out,'sources/InputHost');
const generated=createOfflineHostFixture(project,hostSnapshot,true,{includePsOwnershipSupport:true,gameFeedbackCatalogPath:path.join(out,'sources/capabilities/HcControllerGameFeedbackCapabilityCatalog.generated.cs')});
let fake=generated.fake;
function replaceOnce(text,from,to){if(text.split(from).length!==2)throw Error('Nonunique anchor '+from.slice(0,100));return text.replace(from,()=>to);}
function removeBlock(text,marker){const p=extract(text,marker);return replaceOnce(text,p.text,'');}
fake=removeBlock(fake,'internal sealed class HMController : IDisposable');
fake=removeBlock(fake,'internal sealed class HMContext : IDisposable');
fake='using HIDMaestro; using HIDMaestro.Internal;\n'+fake;
// Helper sources remain byte-identical. Only metadata entry calls use reviewed IRuntime seams.
fake=replaceOnce(fake,'Fixture.Events.Add("neutral");','Fixture.Events.Add("neutral");if(OwnedWorkers.All.Any(w=>w.Clock.Thread.IsAlive))World.UnsafeNeutralWrites++;');
fake=replaceOnce(fake,'HostControllerOutputStop.TryStop(_controller, out detail)','ManagedAdmission.StopOutput(_controller, out detail)');
fake=replaceOnce(fake,'HostControllerIdleCtsRelease.TryCapture(_controller, out var idleCtsRelease, out var captureDetail)','ManagedAdmission.Capture(_controller, out var idleCtsRelease, out var captureDetail)');
let compatibility=read('tools/inputhost_lifecycle_selftest.cs').split('    private static Dictionary<string,object> Group(')[0]+'}\n';
compatibility=removeBlock(compatibility,'private static void StopSteamDeckIdleForRelease(');
compatibility=removeBlock(compatibility,'private static class HostControllerOutputStop');
compatibility=removeBlock(compatibility,'private static class HostControllerIdleCtsRelease');
compatibility=replaceOnce(compatibility,'public void Decode(string a,ulong b,string c,ulong d,ulong e,string f){}','public void Decode(string a,ulong b,string c,ulong d,ulong e,string f){} public void BeginRead(){} public void EndRead(int n){} public void BeginHandle(){} public void EndHandle(){} public void BeginWrite(){} public void EndWrite(){}');
const original=read('InputHost/Program.cs'),screen=read('InputHost/ScreenTouchpadReport.cs');
const selected=[];
function actual(source,marker,file){const part=extract(source,marker);selected.push({...part,text:undefined,file,sha256:sha(part.text),marker});return part.text;}
let pipeLoop=actual(original,'private static async Task<int> RunPipeClientAsync(','InputHost/Program.cs');
const codec=actual(original,'private sealed class MessageCodec','InputHost/Program.cs');
const finalCall='session.ReleaseLocal(out _);';
const main=extract(original,'private static async Task<int> Main(').text;
if(!main.includes('finally\n')&&!main.includes('finally\r\n'))throw Error('Main finally drift');
const finallyPart=main.slice(main.lastIndexOf('finally'));
if(!finallyPart.includes(finalCall))throw Error('Main finally release drift');
selected.push({file:'InputHost/Program.cs',marker:'Main finally ReleaseLocal call',sha256:sha(finalCall),source:finallyPart});
const idleStop=actual(screen,'private static void StopSteamDeckIdleForRelease(','InputHost/ScreenTouchpadReport.cs');
let takeover=actual(screen,'private static uint TakeScreenPadReportOwnership(','InputHost/ScreenTouchpadReport.cs');
takeover=replaceOnce(takeover,'YeManInputHost.Diagnostics.HostControllerIdleCtsRelease.TryStopInputClock(controller,out var detail)','ManagedAdmission.StopClock(controller,out var detail)');
const reflectionField=screen.match(/^    private static readonly FieldInfo\? ScreenPadLastEncoded[^\r\n]+/m)?.[0];if(!reflectionField)throw Error('Raw handoff field drift');
if(fault==='accept-late-output')fake=replaceOnce(fake,'_gameOutputObservation.Revoke();','/* private negative: revoke removed */');
if(fault==='skip-output-stop')fake=replaceOnce(fake,'ManagedAdmission.StopOutput(_controller, out detail)','BypassOutput(_controller, out detail)');
if(fault==='skip-idle-stop')takeover=replaceOnce(takeover,'ManagedAdmission.StopClock(controller,out var detail)','BypassClock(controller,out var detail)');
if(fault==='ignore-parent-cancellation')pipeLoop=pipeLoop.replaceAll('parentLifetime.ExitToken','CancellationToken.None');
write(path.join(project,'ExtractedHost.cs'),fake);
write(path.join(project,'Compatibility.cs'),'using HIDMaestro; using YeManInputHost.Diagnostics;\n'+compatibility);
write(path.join(project,'Boundary.cs'),'namespace YeManInputHost { internal sealed class UsbipMaintenanceGate:System.IDisposable { internal static UsbipMaintenanceGate? TryEnter(bool required)=>new(); public void Dispose(){} } }\ninternal static partial class Program { private sealed partial class HostSession { private void ResetVirtualSignalEvidenceSafely(){} } }\n');
write(path.join(project,'ExtraProduction.cs'),'using System.Reflection; using System.Buffers.Binary; using System.Diagnostics; using System.IO.Pipes; using System.Text; using HIDMaestro;\ninternal static partial class Program {\n'+reflectionField+'\n'+idleStop+'\n'+takeover+'\n'+pipeLoop+'\n'+codec+'\n private static bool ExecuteMainFinallyRelease(HostSession session,out string detail)=>session.ReleaseLocal(out detail);\n private static bool BypassOutput(HMController c,out string d){d="negative-output-skip";return true;}\n private static bool BypassClock(HMController c,out string d){d="negative-clock-skip";return true;}\n}\n');
let legacy=read('tools/msi_steamdeck_release_fence_1726_selftest.cs').replace('internal static class Fixture','internal static partial class Fixture').replace('private static int Main()','private static int LegacyOriginalMain()');
write(path.join(project,'LegacyDefinitions.cs'),legacy);
for(const name of ['HostControllerOutputStop.cs','HostControllerIdleCtsRelease.cs','ParentProcessLifetime.cs','MsiSteamDeckReleaseFence1726.cs','MsiVirtualReportChain1410.cs','CanonicalFrameFingerprintMemo.cs','HostResponseFields.cs'])fs.copyFileSync(path.join(hostSnapshot,name),path.join(project,name));
fs.copyFileSync(path.join(out,'sources',fixtureRel,'ManagedCases.cs'),path.join(project,'ManagedCases.cs'));
const bcl=read('tools/ymcc_sim/global/host-sdk-stop/BclStopFixture.cs.in');
let clock=extract(bcl,'namespace HIDMaestro.Internal').text;
clock=replaceOnce(clock,'@@EXACT_CLOCK_STOP@@',['DeviceClock.Stop.cs','DeviceClock.Wake.cs','DeviceClock.Close.cs'].map(n=>read(sdkRel+'/'+n)).join('\n'));
clock=replaceOnce(clock,'internal bool Closed => _closed;','internal bool Closed => _closed; internal Microsoft.Win32.SafeHandles.SafeWaitHandle ProofHandle=>_wake.SafeWaitHandle;');
let controller=extract(bcl,'namespace HIDMaestro\n').text;
controller=replaceOnce(controller,'@@EXACT_CONTROLLER_STOP@@',Object.keys(sdkSpec.members).filter(n=>n.startsWith('HMController.')).map(n=>read(sdkRel+'/'+n)).join('\n'));
controller=replaceOnce(controller,'private void SendReleaseFrames() => World.Trace.Add("release.fake");','private void SendReleaseFrames(){World.Trace.Add("release.fake");Program.Fixture.Events.Add("controller-dispose");}');
controller=replaceOnce(controller,'internal sealed partial class HMController\n','internal sealed partial class HMController : IDisposable\n');
controller=replaceOnce(controller,'World.ContextCalls++;','World.ContextCalls++; Program.Fixture.Events.Add("sdk-context-callback");');
const events='\nnamespace HIDMaestro { internal sealed partial class HMController { public event Action<HMController,HMOutputPacket>? OutputReceived; internal Action<HMController,HMOutputPacket>? CapturedOutput=>OutputReceived; internal int SubscriberCount=>OutputReceived?.GetInvocationList().Length??0; } }\n';
let workers=read(fixtureRel+'/Workers.cs.in').replace('@@CLOCK_SCAFFOLD@@',()=>clock).replace('@@CONTROLLER_SCAFFOLD@@',()=>controller);
write(path.join(project,'Workers.cs'),workers+events);
write(path.join(project,'Fixture.csproj'),'<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net10.0</TargetFramework><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable><NuGetAudit>false</NuGetAudit><EnableNETAnalyzers>false</EnableNETAnalyzers><UseAppHost>false</UseAppHost></PropertyGroup></Project>');
write(path.join(project,'NuGet.Config'),'<configuration><packageSources><clear /></packageSources></configuration>');
write(path.join(project,'global.json'),{sdk:{version:dotnetSpec.sdkVersion,rollForward:'disable'}});
write(path.join(out,'consumer-extractions.json'),{generated:generated.receipt,extra:selected,sdk:sdkSpec,mainFinally:'Exact release call in Main finally executed after loop; product Main never invoked',substitutions:['Existing generator fake PnP/clock/neutral/frame/HidHide/report-chain and diagnostic terminals retained.','Production output/clock helper files unchanged; three call sites route fake metadata through IRuntime seams.','Existing BCL SDK-stop scaffold clock Loop uses owned BCL Thread/Event; native timer pointer zero; native call forbidden.','All five SDK 2.0 HMController stop/dispose methods and three DeviceClock stop/close methods hash-bound; fake context callback only.']});
const commands=[];
function command(label,exe,argv,cwd,timeout=60000){const r=spawnSync(exe,argv,{cwd,encoding:'utf8',windowsHide:true,timeout,maxBuffer:16*1024*1024});write(path.join(out,label+'.log'),(r.stdout??'')+(r.stderr??''));commands.push({label,exe,argv,cwd,status:r.status,signal:r.signal,error:r.error?.message??null});write(path.join(out,'commands.json'),commands);return r;}
const profiles=[];
for(const config of ['Release','Debug']){
 const build=command(config+'-build',dotnet,['build','Fixture.csproj','-c',config,'--nologo','-v:q'],project);
 if(build.status!==0)throw Error('Build failed: '+path.join(out,config+'-build.log'));
 const r=command(config+'-tests',dotnet,[path.join(project,'bin',config,'net10.0/Fixture.dll')],project);
 let result;try{result=JSON.parse(r.stdout.trim().split(/\r?\n/).at(-1));}catch{}
 profiles.push({config,exit:r.status,result});
}
const sourceStable=inputs.every(p=>sha(fs.readFileSync(p.path))===p.sha256);
const failures=profiles.flatMap(p=>(p.result?.cases??[]).filter(r=>!r.pass).map(r=>({config:p.config,...r})));
const result={status:sourceStable&&profiles.every(p=>p.exit===0)?'PASS':'FAIL',resourceAcceptance:'INCOMPLETE_PARENT_LOSS_STALLED_WORKER_REPRODUCED',fault,sourceStable,profiles,failures,inputs,toolchain:selectedToolchain,sdk:{path:sdkFile,sha256:sdkSpec.sdkSha256,loaded:false},productionMainExecuted:false,productOrInstalledProcessesTouched:false,ownedFixtureChildProcessesOnly:true,realDriverIo:false};
write(path.join(out,'result.json'),result);
console.log(JSON.stringify({status:result.status,fault,sourceStable,profiles:profiles.map(p=>({config:p.config,exit:p.exit,cases:p.result?.cases.map(r=>({name:r.name,pass:r.pass,error:r.error,contract:r.resourceContractPass,knownFailure:r.knownFailureReproduced}))})),result:path.join(out,'result.json')}));
if(result.status!=='PASS')process.exitCode=1;




