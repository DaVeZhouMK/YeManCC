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
const fault=option('--fault','none'); const eligibility=args.includes('--eligibility-only');
if(!['none','terminal-suppress-failure','terminal-no-retry','terminal-retry-sdk','terminal-repeat-shutdown','terminal-log-fatal'].includes(fault))throw Error('Unsupported semantic negative');
if(fs.existsSync(out))throw Error('Fresh evidence required: '+out);
fs.mkdirSync(out,{recursive:true});
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
const fixtureRel='tools/fixtures/input_host_managed_lifecycle_20261023';
const sdkRel='tools/fixtures/hidmaestro/2.0.0';
const sdkSpec=JSON.parse(fs.readFileSync(path.join(repo,sdkRel,'binary-binding.json')));
const sourceNames=['Program.cs','ScreenTouchpadReport.cs','ScreenButtonReport.cs','PsEndpointOwnership.cs','HostGameOutputObservation.cs','HostControllerOutputStop.cs','HostControllerIdleCtsRelease.cs','ParentProcessLifetime.cs','MsiSteamDeckReleaseFence1726.cs','MsiVirtualReportChain1410.cs','CanonicalFrameFingerprintMemo.cs','HostResponseFields.cs','HostTerminalCleanup.cs'];
const relativeInputs=[...sourceNames.map(n=>'InputHost/'+n),path.relative(repo,self),'tools/msi_steamdeck_release_fence_1726_selftest.mjs','tools/msi_steamdeck_release_fence_1726_selftest.cs','tools/inputhost_lifecycle_selftest.mjs','tools/inputhost_lifecycle_selftest.cs','tools/inputhost_parent_lifecycle_selftest.cs','tools/ymcc_sim/global/inputhost-protocol/extract-parser.mjs','tools/ymcc_sim/global/inputhost-protocol/provenance.mjs','tools/ymcc_sim/global/inputhost-protocol/toolchain-inputs.json','tools/ymcc_sim/global/host-sdk-stop/BclStopFixture.cs.in','capabilities/HcControllerGameFeedbackCapabilityCatalog.generated.cs',fixtureRel+'/Workers.cs.in',fixtureRel+'/ManagedCases.cs',fixtureRel+'/TerminalCases.cs',fixtureRel+'/TerminalPorts.cs',sdkRel+'/binary-binding.json',...Object.keys(sdkSpec.members).map(n=>sdkRel+'/'+n)];
const inputs=[...new Set(relativeInputs)].map(relative=>{const file=path.join(repo,relative),bytes=fs.readFileSync(file),snapshot=path.join(out,'sources',relative);fs.mkdirSync(path.dirname(snapshot),{recursive:true});fs.writeFileSync(snapshot,bytes,{flag:'wx'});return {relative,path:file,snapshot,sha256:sha(bytes),bytes:bytes.length};});
const closureFile=option('--runtime-source-closure',null),identityFile=option('--source-identity-manifest',null),identitySha=option('--source-identity-sha256',null);
if([closureFile,identityFile,identitySha].some(Boolean)&&![closureFile,identityFile,identitySha].every(Boolean))throw Error('All runtime closure and explicit identity arguments are required together');
const pin=file=>({path:file,sha256:sha(fs.readFileSync(file))});
let registrationBinding=null;
if(identityFile){
 if(!path.isAbsolute(closureFile)||!path.isAbsolute(identityFile)||sha(fs.readFileSync(identityFile))!==identitySha)throw Error('Approved managed source identity changed');
 const approval=JSON.parse(fs.readFileSync(identityFile,'utf8')),closure=JSON.parse(fs.readFileSync(closureFile,'utf8'));
 for(const row of inputs){const relative=row.relative.replaceAll('\\','/');if(approval.sourceBindings?.[relative]!==row.sha256||closure[relative]!==row.sha256)throw Error('Managed actual input differs approved source: '+relative);}
 for(const [relative,expected] of Object.entries(approval.sourceBindings??{}))if(closure[relative]!==expected||sha(fs.readFileSync(path.join(repo,relative)))!==expected)throw Error('Managed approved source binding differs: '+relative);
 for(const [relative,expected] of Object.entries(closure))if(sha(fs.readFileSync(path.join(repo,relative)))!==expected)throw Error('Managed parent runtime closure changed: '+relative);
 registrationBinding={identityMode:'explicit-managed-source-identity-and-runtime-closure',approvedIdentityManifest:pin(identityFile),runtimeSourceClosureIdentity:pin(closureFile),runtimeSourceClosure:closure};
 writeBinding(path.join(out,'registration-binding-before.json'),registrationBinding);
}
function writeBinding(file,value){fs.writeFileSync(file,JSON.stringify(value,null,2)+'\n',{flag:'wx'});}
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
fake=replaceOnce(fake,'Fixture.Events.Add("neutral");','Fixture.Events.Add("neutral");if(_tuple?.Persona=="steamdeck"&&OwnedWorkers.All.Any(w=>w.Clock.Thread.IsAlive))World.UnsafeNeutralWrites++;');
fake=replaceOnce(fake,'HostControllerOutputStop.TryStop(_controller, out detail)','ManagedAdmission.StopOutput(_controller, out detail)');
fake=replaceOnce(fake,'HostControllerIdleCtsRelease.TryCapture(_controller, out var idleCtsRelease, out var captureDetail)','ManagedAdmission.Capture(_controller, out var idleCtsRelease, out var captureDetail)');
let compatibility=read('tools/inputhost_lifecycle_selftest.cs').split('    private static Dictionary<string,object> Group(')[0]+'}\n';
compatibility=removeBlock(compatibility,'private static void StopSteamDeckIdleForRelease(');
compatibility=removeBlock(compatibility,'private static class HostControllerOutputStop');
compatibility=removeBlock(compatibility,'private static class HostControllerIdleCtsRelease');
compatibility=replaceOnce(compatibility,'public void Decode(string a,ulong b,string c,ulong d,ulong e,string f){}','public void Decode(string a,ulong b,string c,ulong d,ulong e,string f){if(TerminalTestPorts.ThrowHandle)throw new InvalidOperationException("injected-handle-fault");} public object FatalSnapshot()=>new { fixtureOnly=true }; public void BeginRead(){} public void EndRead(int n){} public void BeginHandle(){} public void EndHandle(){} public void BeginWrite(){TerminalTestPorts.BeforeWrite?.Invoke();} public void EndWrite(){}');
compatibility=replaceOnce(compatibility,'private sealed class Stage {','private sealed class Stage : IDisposable { public static Stage? TryCreate(int parentPid,string hostInstance){TerminalTestPorts.StageCreates++;if(TerminalTestPorts.ThrowInitialization)throw new IOException("injected-host-initialization");return new Stage();} public void Dispose(){TerminalTestPorts.StageDisposes++;TerminalTestPorts.BeforeStageDispose?.Invoke();} ');
const original=read('InputHost/Program.cs'),screen=read('InputHost/ScreenTouchpadReport.cs');
const selected=[];
function actual(source,marker,file){const part=extract(source,marker);selected.push({...part,text:undefined,file,sha256:sha(part.text),marker});return part.text;}
let pipeLoop='',codec='',mainBlock='throw new NotSupportedException("eligibility-only before Main patch");';
if(!eligibility){
 pipeLoop=actual(original,'private static async Task<HostTerminalCleanup.LoopResult> RunPipeClientAsync(','InputHost/Program.cs');
 codec=actual(original,'private sealed class MessageCodec','InputHost/Program.cs');
 const main=extract(original,'private static async Task<int> Main(').text.replace(/\r\n/g,'\n');
 const ownerLine='        var session = new HostSession(startupNonce!, parentPid, msiStageRequested);';
 if(main.split(ownerLine).length!==2)throw Error('Main session ownership anchor changed');
 const tail=main.slice(main.indexOf(ownerLine)+ownerLine.length).replace(/MsiHostStageEvidence/g,'Stage');
 mainBlock=tail.slice(0,tail.lastIndexOf('}')).trimEnd();
 if(!mainBlock.includes('HostTerminalCleanup.RunAsync(')||mainBlock.includes('session.ReleaseLocal(out _)'))throw Error('Main terminal ownership drift');
 selected.push({file:'InputHost/Program.cs',marker:'Exact Main owned-session initialization and terminal try/finally',sha256:sha(mainBlock),source:mainBlock});
}
let fatalLogger=actual(original,'private static void LogFatalExit(','InputHost/Program.cs');
fatalLogger=replaceOnce(fatalLogger,'private static void LogFatalExit(','private static void ActualLogFatalExit(');
if(fault==='terminal-log-fatal')fatalLogger=replaceOnce(fatalLogger,'\"resource-settlement\"','\"fatal\"');
fatalLogger=replaceOnce(fatalLogger,'var dir = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "YeManCC");','var dir = Path.Combine(AppContext.BaseDirectory, "fixture-owned-logs");');
const fatalFields=['ProcessStartedUtc','_fatalContextLastCommand','_fatalContextHostInstance'].map(name=>{const match=new RegExp('^        private static [^\\r\\n]*\\b'+name+'[^\\r\\n]*;','m').exec(original);if(!match)throw Error('Fatal context field missing '+name);return match[0];}).join('\n');
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
write(path.join(project,'Boundary.cs'),'// Explicit memory leaf; USB maintenance/reboot behavior is not exercised by the managed-release cases.\nnamespace YeManCC { internal static class UsbipMaintenanceState { internal static bool Pending => false; } }\nnamespace YeManInputHost { internal sealed class UsbipMaintenanceGate:System.IDisposable { internal static UsbipMaintenanceGate? TryEnter(bool required)=>new(); public void Dispose(){} } }\ninternal static partial class Program { private sealed partial class HostSession { private void ResetVirtualSignalEvidenceSafely(){} } }\n');
write(path.join(project,'ExtraProduction.cs'),'using System.Reflection; using System.Buffers.Binary; using System.Diagnostics; using System.IO.Pipes; using System.Text; using System.Text.Json; using HIDMaestro; using YeManInputHost.Diagnostics;\ninternal static partial class Program {\n'+fatalFields+'\n'+fatalLogger+'\n'+reflectionField+'\n'+idleStop+'\n'+takeover+'\n'+pipeLoop+'\n'+codec+'\n private static async Task<int> ExecuteMainTerminalAsync(string pipeName,int parentPid,HostSession session,bool msiStageRequested=false){\n'+mainBlock+'\n}\n private static bool BypassOutput(HMController c,out string d){d="negative-output-skip";return true;}\n private static bool BypassClock(HMController c,out string d){d="negative-clock-skip";return true;}\n}\n');
let legacy=read('tools/msi_steamdeck_release_fence_1726_selftest.cs').replace('internal static class Fixture','internal static partial class Fixture').replace('private static int Main()','private static int LegacyOriginalMain()');
write(path.join(project,'LegacyDefinitions.cs'),legacy);
for(const name of ['HostControllerOutputStop.cs','HostControllerIdleCtsRelease.cs','ParentProcessLifetime.cs','MsiSteamDeckReleaseFence1726.cs','MsiVirtualReportChain1410.cs','CanonicalFrameFingerprintMemo.cs','HostResponseFields.cs','HostTerminalCleanup.cs'])fs.copyFileSync(path.join(hostSnapshot,name),path.join(project,name));
let baseCases=read(fixtureRel+'/ManagedCases.cs');
baseCases=removeBlock(baseCases,'private static async Task<Dictionary<string,object?>> PipeCase(');
baseCases=removeBlock(baseCases,'private static async Task<int> Main(');
baseCases=replaceOnce(baseCases,'private static void LogFatalExit(string kind,string detail){lock(Fatal)Fatal.Add(kind+":"+detail);}','private static void LogFatalExit(string kind,string detail){ActualLogFatalExit(kind,detail);lock(Fatal)Fatal.Add(kind+":"+detail);if(kind.StartsWith("terminal-cleanup-"))TerminalReceipts.Add(System.Text.Json.JsonSerializer.Deserialize<YeManInputHost.Diagnostics.HostTerminalCleanup.Receipt>(detail)!);}');
baseCases=replaceOnce(baseCases,'Fixture.Reset();OwnedWorkers.Reset();Fatal.Clear();','Fixture.Reset();OwnedWorkers.Reset();Fatal.Clear();TerminalReceipts.Clear();TerminalTestPorts.Reset();_msiHostStageEvidence=null;ActualLogStart=ReadAllActualLogRows().Length;');
baseCases=replaceOnce(baseCases,'["beforeFixtureCleanup"]=resources,["trace"]=trace','["beforeFixtureCleanup"]=resources,["terminalReceipts"]=TerminalReceipts.ToArray(),["trace"]=trace');
write(path.join(project,'BaseCases.cs'),baseCases);
for(const name of ['TerminalCases.cs','TerminalPorts.cs'])write(path.join(project,name),read(fixtureRel+'/'+name));
let terminal=read('InputHost/HostTerminalCleanup.cs');
if(fault==='terminal-suppress-failure')terminal=replaceOnce(terminal,': ReleaseFailedExitCode;',': 0;');
if(fault==='terminal-no-retry')terminal=replaceOnce(terminal,'if (retry) final = Invoke();','if (false) final = Invoke();');
if(fault==='terminal-retry-sdk')terminal=replaceOnce(terminal,'"controller-idle-cts-capture:sdk-idle-clock-timeout";','"controller-idle-cts-capture:sdk-idle-clock-timeout" || detail.StartsWith("controller-dispose:", StringComparison.Ordinal);');
if(fault==='terminal-repeat-shutdown')terminal=replaceOnce(terminal,'var first = outcome.ShutdownRelease ?? Invoke();','var first = Invoke();');
write(path.join(project,'HostTerminalCleanup.cs'),terminal);

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
workers=replaceOnce(workers,'return Monitor.TryEnter(g,ms);','return TerminalTestPorts.TryEnter(g,ms);');
workers=replaceOnce(workers,'return ((Thread)t).Join(ms);','return TerminalTestPorts.Join(t,ms);');
workers=replaceOnce(workers,':((Thread)t).Join(0);',':TerminalTestPorts.HasTerminated(t);');
workers=replaceOnce(workers,'public bool IsPinnedClock(Type t)=>t==typeof(DeviceClock<StreamTick>);','public bool IsPinnedClock(Type t)=>!TerminalTestPorts.CaptureIdentityFailure&&t==typeof(DeviceClock<StreamTick>);');
write(path.join(project,'Workers.cs'),workers+events);
write(path.join(project,'Fixture.csproj'),'<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net10.0</TargetFramework><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable><NuGetAudit>false</NuGetAudit><EnableNETAnalyzers>false</EnableNETAnalyzers><UseAppHost>false</UseAppHost></PropertyGroup></Project>');
write(path.join(project,'NuGet.Config'),'<configuration><packageSources><clear /></packageSources></configuration>');
write(path.join(project,'global.json'),{sdk:{version:dotnetSpec.sdkVersion,rollForward:'disable'}});
write(path.join(out,'consumer-extractions.json'),{generated:generated.receipt,extra:selected,sdk:sdkSpec,mainTerminalBlock:'Exact current Main owned-session initialization and terminal try/finally executed; only diagnostic type name mapped to existing Stage port; product startup and Main never invoked',eligibilityOnly:eligibility,substitutions:['Existing generator fake PnP/clock/neutral/frame/HidHide/report-chain and diagnostic terminals retained.','Production output/clock helper files unchanged; three call sites route fake metadata through IRuntime seams.','Existing BCL SDK-stop scaffold clock Loop uses owned BCL Thread/Event; native timer pointer zero; native call forbidden.','All five SDK 2.0 HMController stop/dispose methods and three DeviceClock stop/close methods hash-bound; fake context callback only.']});
const commands=[];
function command(label,exe,argv,cwd,timeout=60000){const r=spawnSync(exe,argv,{cwd,encoding:'utf8',windowsHide:true,timeout,maxBuffer:16*1024*1024});write(path.join(out,label+'.log'),(r.stdout??'')+(r.stderr??''));commands.push({label,exe,argv,cwd,status:r.status,signal:r.signal,error:r.error?.message??null});write(path.join(out,'commands.json'),commands);return r;}
const profiles=[];
for(const config of ['Release','Debug']){
 const build=command(config+'-build',dotnet,['build','Fixture.csproj','-c',config,'--nologo','-v:q'],project);
 if(build.status!==0)throw Error('Build failed: '+path.join(out,config+'-build.log'));
 const r=command(config+'-tests',dotnet,[path.join(project,'bin',config,'net10.0/Fixture.dll'),...(eligibility?['--eligibility-only']:[])],project);
 let result;try{result=JSON.parse(r.stdout.trim().split(/\r?\n/).at(-1));}catch{}
 profiles.push({config,exit:r.status,result});
}
const sourceStable=inputs.every(p=>sha(fs.readFileSync(p.path))===p.sha256);
const failures=profiles.flatMap(p=>(p.result?.cases??[]).filter(r=>!r.pass).map(r=>({config:p.config,...r})));
const runtimeSourceClosureStable=!registrationBinding||sha(fs.readFileSync(identityFile))===identitySha&&sha(fs.readFileSync(closureFile))===registrationBinding.runtimeSourceClosureIdentity.sha256&&Object.entries(registrationBinding.runtimeSourceClosure).every(([relative,expected])=>sha(fs.readFileSync(path.join(repo,relative)))===expected);
const result={registrationBinding,runtimeSourceClosureStable,status:sourceStable&&runtimeSourceClosureStable&&profiles.every(p=>p.exit===0)?'PASS':'FAIL',resourceAcceptance:'BOUNDED_TERMINAL_RESULT_ONLY_OS_FALLBACK_UNKNOWN',eligibilityOnly:eligibility,fault,sourceStable,profiles,failures,inputs,toolchain:selectedToolchain,sdk:{path:sdkFile,sha256:sdkSpec.sdkSha256,loaded:false},productionMainExecuted:false,productOrInstalledProcessesTouched:false,ownedFixtureChildProcessesOnly:true,realDriverIo:false};
write(path.join(out,'result.json'),result);
console.log(JSON.stringify({status:result.status,fault,sourceStable,profiles:profiles.map(p=>({config:p.config,exit:p.exit,cases:p.result?.cases.map(r=>({name:r.name,pass:r.pass,error:r.error}))})),result:path.join(out,'result.json')}));
if(result.status!=='PASS')process.exitCode=1;




