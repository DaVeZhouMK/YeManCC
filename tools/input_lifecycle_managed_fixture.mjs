// Build-only SDK-free managed consumer for the real native pipe integration.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {createOfflineHostFixture} from './msi_steamdeck_release_fence_1726_selftest.mjs';
import {toolchainFiles} from './ymcc_sim/global/inputhost-protocol/adapter.mjs';

const self=fileURLToPath(import.meta.url),repo=path.resolve(path.dirname(self),'..');
const evidenceRoot=path.resolve(repo,'../../../Build/Validation/VirtualController-Lifecycle-20261019/integration/managed');
const ownershipEvidenceRoot=path.resolve(repo,'../../../Build/Validation/VirtualController-Lifecycle-20261019/ps-instance-ownership-audit01');
const hash=b=>crypto.createHash('sha256').update(b).digest('hex');
const fileHash=p=>hash(fs.readFileSync(p));
const need=(ok,message)=>{if(!ok)throw Error(message);};
const json=(p,v)=>fs.writeFileSync(p,JSON.stringify(v,null,2)+'\n',{flag:'wx'});
const replaceOnce=(s,a,b)=>{need(s.split(a).length===2,'Source anchor drift: '+a);return s.replace(a,b);};
function member(text,marker){
 const start=text.indexOf(marker);need(start>=0&&text.indexOf(marker,start+marker.length)<0,'Nonunique production member: '+marker);
 const open=text.indexOf('{',start);let depth=0,mode='code';
 for(let i=open;i<text.length;i++){
  const c=text[i],pair=text.slice(i,i+2);
  if(mode==='line'){if(c==='\n')mode='code';}
  else if(mode==='comment'){if(pair==='*/'){mode='code';i++;}}
  else if(mode==='string'||mode==='char'){if(c==='\\')i++;else if(c===(mode==='string'?'"':"'"))mode='code';}
  else if(pair==='//'){mode='line';i++;}else if(pair==='/*'){mode='comment';i++;}
  else if(c==='"')mode='string';else if(c==="'")mode='char';else if(c==='{')depth++;
  else if(c==='}'&&--depth===0)return{text:text.slice(start,i+1),start,end:i+1,line:text.slice(0,start).split('\n').length};
 }
 throw Error('Unclosed member: '+marker);
}
const xml=s=>s.replaceAll('&','&amp;').replaceAll('"','&quot;');
export function buildManagedFixture(out,options={}){
 const semanticMutation=options.semanticMutation??null;
 need(semanticMutation===null||semanticMutation==='omit-recreate-owned-pin','Unknown managed semantic mutation');
 need(path.isAbsolute(out),'--out must be absolute');out=path.resolve(out);
 const within=base=>{const rel=path.relative(base,out);return rel&&rel!=='..'&&!rel.startsWith('..'+path.sep)&&!path.isAbsolute(rel);};
 const currentEvidenceRoot=options.evidenceRoot===undefined?null:path.resolve(options.evidenceRoot);
 if(currentEvidenceRoot){const roots=['../../../Build/Validation','../../../Testing/YMCC/Evidence'].map(n=>path.resolve(repo,n)),admitted=roots.some(root=>{const rel=path.relative(root,currentEvidenceRoot);return rel&&rel!=='..'&&!rel.startsWith('..'+path.sep)&&!path.isAbsolute(rel);});need(path.isAbsolute(options.evidenceRoot)&&admitted&&fs.realpathSync(currentEvidenceRoot).toLowerCase()===currentEvidenceRoot.toLowerCase(),'Explicit current evidence root must be an existing owned Validation/Testing child');}
 need(within(evidenceRoot)||within(ownershipEvidenceRoot)||(currentEvidenceRoot&&within(currentEvidenceRoot)),'--out must be a new child of an authorized lifecycle evidence root');
 need(!fs.existsSync(out),'Fresh output required: '+out);fs.mkdirSync(out,{recursive:true});
 const commands=[];
 function run(name,args){const r=spawnSync('dotnet',args,{cwd:out,encoding:'utf8',timeout:60000,windowsHide:true,env:{...process.env,DOTNET_CLI_TELEMETRY_OPTOUT:'1',DOTNET_NOLOGO:'1',DOTNET_CLI_WORKLOAD_UPDATE_NOTIFY_DISABLE:'true'}});fs.writeFileSync(path.join(out,name+'.log'),(r.stdout??'')+(r.stderr??''));commands.push({name,args,exit:r.status,error:r.error?.message??null});need(!r.error&&r.status===0,name+' failed: '+path.join(out,name+'.log'));return r.stdout;}
 try{
 const names=['InputHost/Program.cs','InputHost/PsEndpointOwnership.cs','InputHost/ParentProcessLifetime.cs','InputHost/HostTerminalCleanup.cs','InputHost/HostControllerOutputStop.cs','InputHost/HostControllerIdleCtsRelease.cs','InputHost/HostGameOutputObservation.cs','InputHost/HostVirtualSignalEvidence.cs','InputHost/HostSdkFirstWitness.cs','InputHost/TargetCreationLifetimeEvidence.cs','InputHost/SteamDeckPadReportObservation.cs','InputHost/MsiSteamDeckReleaseFence1726.cs','InputHost/MsiVirtualReportChain1410.cs','InputHost/ScreenTouchpadReport.cs','InputHost/ScreenButtonReport.cs','InputHost/CanonicalFrameFingerprintMemo.cs','InputHost/HostResponseFields.cs','InputHost/CompactFrameReplyBinding.cs','tools/msi_steamdeck_release_fence_1726_selftest.mjs','tools/msi_steamdeck_release_fence_1726_selftest.cs','tools/inputhost_lifecycle_selftest.cs','tools/input_lifecycle_managed_fixture.mjs','tools/fixtures/input_lifecycle_integration/ManagedEntry.cs','tools/ymcc_sim/global/inputhost-protocol/adapter.mjs','tools/ymcc_sim/global/inputhost-protocol/toolchain-inputs.json','capabilities/HcControllerGameFeedbackCapabilityCatalog.generated.cs'];
 if(options.psOwnershipSelfTest)names.push('tools/ps_endpoint_ownership_selftest.cs','tools/ps_endpoint_ownership_selftest.mjs');
 const inputs=names.map(relative=>{const p=path.join(repo,relative),bytes=fs.readFileSync(p),snapshot=path.join(out,'input-sources',relative);fs.mkdirSync(path.dirname(snapshot),{recursive:true});fs.writeFileSync(snapshot,bytes,{flag:'wx'});return{path:p,relative,snapshot,sha256:hash(bytes),bytes:bytes.length};});
 const read=relative=>fs.readFileSync(inputs.find(r=>r.relative===relative).snapshot,'utf8').replace(/^\uFEFF/,'');
 const tools=toolchainFiles(repo).map(p=>{need(fs.existsSync(p)&&fs.statSync(p).isFile(),'Missing compiler/framework input: '+p);return{path:p,sha256:fileHash(p),bytes:fs.statSync(p).size};});
 const source=read('InputHost/Program.cs'),host=path.join(repo,'InputHost');
 const generated=createOfflineHostFixture(out,host,true,{externalGameOutputHelper:true,externalGameFeedbackCatalog:true});let fake=generated.fake;
 need(!/\bclass\s+HcControllerGameFeedbackCapabilityCatalog\b/.test(fake),'Game feedback catalog must be compiled separately, not embedded');
 // Compile the real passive signal helper and its current session methods.
 // The state type remains an inert SDK terminal; no diagnostic method is stubbed.
 const signalMembers=['private void ResetVirtualSignalEvidenceSafely(','internal HostVirtualSignalEvidence.Attempt BeginVirtualSignalEvidenceSafely(','internal void CompleteVirtualSignalEvidenceSafely('].map(marker=>({...member(source,marker),marker}));
 // The base extractor supplies an inert reset. Replace only that exact boundary
 // before injecting the real signal methods; keep one actual HostSession member.
 const inheritedSignalReset=member(fake,'private void ResetVirtualSignalEvidenceSafely(');
 need(inheritedSignalReset.text==='private void ResetVirtualSignalEvidenceSafely(){}','Base virtual-signal reset is not the declared inert boundary');
 fake=replaceOnce(fake,inheritedSignalReset.text,'');
 generated.receipt=generated.receipt.filter(row=>row.name!=='ResetVirtualSignalEvidenceSafely');
 const signalField=/^        private HostVirtualSignalEvidence\? _virtualSignalEvidence;/m.exec(source);
 need(signalField,'Missing actual virtual signal observer field');
 const sdkWitnessField=/^        private HostSdkFirstWitness\? _sdkFirstWitness;/m.exec(source);
 need(sdkWitnessField,'Missing actual SDK witness observer field');
 fake='using HIDMaestro;\n'+replaceOnce(fake,'private sealed partial class HostSession {','private sealed partial class HostSession {\n'+signalField[0]+'\n'+sdkWitnessField[0]+'\n'+signalMembers.map(r=>r.text).join('\n'));
 // Preserve actual production protocol/core constants, while the old asset
 // verification endpoint remains a clearly declared fake.
 const constants=['LockedCoreSha256','LockedCoreMvid'].map(name=>{const matches=[...source.matchAll(new RegExp('private const string '+name+'\\s*=\\s*[^;]+;','g'))];need(matches.length===1,'Constant missing: '+name);return matches[0][0];});
 fake=replaceOnce(fake,'private const string LockedCoreSha256="fake-no-sdk",LockedCoreMvid="fake-no-sdk";',constants.join('\n'));
 // Extend only this managed fixture's fake Elite profile and PnP terminal.
 // Keep the real HostSession/PREPARE path and the existing absent-target gate.
 fake=replaceOnce(fake,
  'public HMProfile GetProfile(string id)=>new(){Id=id,RequiresUsbipBackend=id=="steam-deck-composite"};',
  'public HMProfile GetProfile(string id)=>new(){Id=id,RequiresUsbipBackend=id=="steam-deck-composite",VendorId=(ushort)(id=="xbox-elite-v2"?0x045e:id=="dualsense-edge"?0x054c:0x28de),Pid=(ushort)(id=="xbox-elite-v2"?0x0b00:id=="dualsense-edge"?0x0df2:0x1205)};');
 // Exact production ownership helper/session methods. Only the SDK metadata
 // identity and the five CM leaves below are private-fixture boundaries.
 const ownershipField=source.match(/^        private readonly PsEndpointOwnership _psEndpointOwnership = new\(\);/m);
 need(ownershipField,'Missing production PS ownership owner');
 const ownershipMethods=['private PsEndpointOwnership.Binding PsOwnershipBinding(','private void CapturePsEndpointOwnership(','private string AppendPsEndpointOwnership('].map(marker=>member(source,marker).text);
 fake=replaceOnce(fake,'private sealed partial class HostSession {','private sealed partial class HostSession {\n'+
  ownershipField[0].replace('= new();','= new(new PsEndpointOwnership.SdkContract(typeof(HMController).FullName!, typeof(HMController).Module.ModuleVersionId));')+'\n'+ownershipMethods.join('\n'));
 fake=replaceOnce(fake,'internal sealed class HMController : IDisposable { ',
  'internal sealed class HMController : IDisposable { internal string? InstanceId { get; } internal HMController(string? instanceId=null){InstanceId=instanceId;} ');
 fake=replaceOnce(fake,'Program.Fixture.Current=Program.Arrival(Program.Fixture.NextRoot);return new();',
  'Program.Fixture.Current=Program.Arrival(Program.Fixture.NextRoot);return new(p.Id=="dualsense-edge"&&!Program.PsFixtureMissingSdkRoot?Program.ManagedPsRoot(Program.Fixture.Creates):null);');
 let managedEntry='using YeManInputHost.Diagnostics;\n'+read('tools/fixtures/input_lifecycle_integration/ManagedEntry.cs');
 managedEntry=replaceOnce(managedEntry,'"dualsense-edge" => new(true, 1, 0, 1, 0, 0, true, "fixture-edge-arrival"),','"dualsense-edge" => ManagedPsPnpObservation(),');
 managedEntry=replaceOnce(managedEntry,
  '"elite" => new(true, 1, 0, 1, 0, 0, true, "fixture-elite-arrival"),',
  '"elite" => ManagedElitePnpObservation(),');
 const pnpNodes=source.match(/private readonly record struct PnpNode\([^\r\n]+\);/g);
 need(pnpNodes?.length===1,'Nonunique production PnpNode definition');
 // Use the product node shape and formatter: each ID has one [hardware-id]
 // suffix, with the same field order and separators as a real observation.
 managedEntry+='\ninternal static partial class Program {\n'+pnpNodes[0]+'\n'+member(source,'private static string DescribeVirtualEndpoint(').text+'\n'+String.raw`
 private const string ManagedEliteRootInstanceId = @"SWD\HIDMAESTRO_VID_045E_PID_0B00&IG_00\OFFLINE_ELITE";
 private const string ManagedEliteHidInstanceId = @"HID\HIDMAESTRO_VID_045E_PID_0B00&IG_00\OFFLINE_ELITE";
 internal static bool PsFixtureMissingSdkRoot;
 private static bool PsFixtureLocateFailedOnce;
 internal static string ManagedPsRoot(int creation) => @"SWD\HIDMAESTRO_VID_054C_PID_0DF2&IG_00\OFFLINE_PS_" + (_managedFault == "ps-same-instance-ids" ? 1 : creation);
 internal static string ManagedPsHid(int creation) => @"HID\HIDMAESTRO_VID_054C_PID_0DF2&IG_00\OFFLINE_PS_" + (_managedFault == "ps-same-instance-ids" ? 1 : creation);
 private static VirtualEndpointObservation ManagedPsPnpObservation()
 {
  var roots = new[] { new PnpNode(ManagedPsRoot(Fixture.Creates), 1, true, new[] { @"ROOT\HIDMAESTRO" }) };
  var hid = new[] { new PnpNode(ManagedPsHid(Fixture.Creates), 2, true, new[] { @"HID\VID_054C&PID_0DF2" }) };
  var empty = Array.Empty<PnpNode>();
  return new(true, 1, 0, 1, 0, 0, true, DescribeVirtualEndpoint(roots, empty, hid, empty, empty, true));
 }
 // Private node table only. No ownership verdict or subtree traversal lives here.
 private sealed class PsOwnershipCmReader : PsEndpointOwnership.ICmReader
 {
  internal static readonly PsOwnershipCmReader Instance = new();
  internal static PsEndpointOwnership.ICmReader? Override;
  internal static int Reads;
  private static bool Present => !Fixture.Current.IsFullyAbsent;
  public int Locate(string id,out uint node){++Reads;if(Override is {} r)return r.Locate(id,out node);if(_managedFault=="ps-late-ownership-read"&&!PsFixtureLocateFailedOnce){PsFixtureLocateFailedOnce=true;node=0;return 51;}node=1;return Present&&StringComparer.OrdinalIgnoreCase.Equals(id,ManagedPsRoot(Fixture.Creates))?0:13;}
  public int GetChild(uint node,out uint child){++Reads;if(Override is {} r)return r.GetChild(node,out child);child=2;return Present&&node==1?0:13;}
  public int GetSibling(uint node,out uint sibling){++Reads;if(Override is {} r)return r.GetSibling(node,out sibling);sibling=0;return 13;}
  public int GetDeviceId(uint node,out string id){++Reads;if(Override is {} r)return r.GetDeviceId(node,out id);id=node==1?ManagedPsRoot(Fixture.Creates):node==2?ManagedPsHid(Fixture.Creates):"";return Present&&id.Length!=0?0:13;}
  public int GetStatus(uint node,out uint status,out uint problem){++Reads;if(Override is {} r)return r.GetStatus(node,out status,out problem);status=8;problem=0;return Present&&(node==1||node==2)?0:13;}
 }
 private static VirtualEndpointObservation ManagedElitePnpObservation()
 {
  var roots = new[] { new PnpNode(ManagedEliteRootInstanceId, 0, true, new[] { @"ROOT\HIDMAESTRO" }) };
  var hid = new[] { new PnpNode(ManagedEliteHidInstanceId, 0, true, new[] { @"HID\VID_045E&PID_0B00&IG_00" }) };
  var empty = Array.Empty<PnpNode>();
  return new(true, roots.Length, 0, hid.Length, 0, 0, true,
   DescribeVirtualEndpoint(roots, empty, hid, empty, empty, true))
  {
   SteamDeckRootInstanceIds = Array.AsReadOnly(Array.Empty<string>()),
   SteamDeckHidChildInstanceIds = Array.AsReadOnly(Array.Empty<string>()),
   PresentInstanceIds = Array.AsReadOnly(new[] { ManagedEliteRootInstanceId, ManagedEliteHidInstanceId })
  };
 }
`+'}\n';
 if(semanticMutation==='omit-recreate-owned-pin')fake=replaceOnce(fake,
  'envelope.Command is ("PREPARE_TARGET" or "RECREATE_TARGET" or "RELEASE_TARGET" or "SHUTDOWN") &&',
  'envelope.Command is ("PREPARE_TARGET" or "RELEASE_TARGET" or "SHUTDOWN") &&');
 let legacy=read('tools/msi_steamdeck_release_fence_1726_selftest.cs');
 legacy=replaceOnce(legacy,'internal static class Fixture','internal static partial class Fixture');
 legacy=replaceOnce(legacy,'private static int Main()','private static int UnusedLegacySelfTestEntry()');
 let compatibility='using YeManInputHost.Diagnostics;\n'+read('tools/inputhost_lifecycle_selftest.cs');
 compatibility=replaceOnce(compatibility,'private sealed class Stage','private sealed partial class Stage');
 // The standalone protocol test has a deliberately inert SDK witness terminal.
 // This composed fixture compiles the real helper and injects its actual session
 // field above, so the standalone nested type/field must not shadow or duplicate it.
 if(compatibility.includes('private sealed class HostSdkFirstWitness')){
  const inherited=member(compatibility,'private sealed class HostSdkFirstWitness');
  const expected='private sealed class HostSdkFirstWitness { public void BindLifetime(object? controller,object? lifetime){} public string TryAppend(string message,int maxBytes)=>message; }';
  need(inherited.text.replace(/\s+/g,'')===expected.replace(/\s+/g,''),'Standalone SDK witness is not the reviewed inert boundary');
  compatibility=replaceOnce(compatibility,inherited.text,'');
 }
 const inheritedSdkFields=[...compatibility.matchAll(/^[ \t]*private HostSdkFirstWitness\? _sdkFirstWitness;\r?$/gm)];
 need(inheritedSdkFields.length<=1,'Standalone SDK witness field is duplicated');
 if(inheritedSdkFields.length)compatibility=replaceOnce(compatibility,inheritedSdkFields[0][0],'');

 // Preserve the old compact-reply fault seam while forwarding real target-lifetime
 // methods. The new SDK witness captures actual fixture-object lifetime evidence.
 compatibility=replaceOnce(compatibility,member(compatibility,'private sealed class TargetEvidence').text,String.raw`private sealed class TargetEvidence {
  private readonly TargetCreationLifetimeEvidence? _actual=TargetCreationLifetimeEvidence.TryCreate();
  public TargetCreationLifetimeEvidence.Attempt BeginAttempt()=>_actual?.BeginAttempt()??default;
  public void Failure(TargetCreationLifetimeEvidence.Attempt attempt)=>_actual?.Failure(attempt);
  public void Created(TargetCreationLifetimeEvidence.Attempt attempt,HMController controller)=>_actual?.Created(attempt,controller);
  public void ReleaseAttempt()=>_actual?.ReleaseAttempt();
  public TargetCreationLifetimeEvidence.Snapshot? Capture()=>_actual?.Capture();
  public string TryAppend(string message,string command,int max)=>Fixture.CompactAppend&&command=="SUBMIT_FRAME"?message[..^1]+",\"fixtureAppend\":true}":_actual?.TryAppend(message,command,max)??message;
 }`);

 compatibility=replaceOnce(compatibility,'public void Decode(string a,ulong b,string c,ulong d,ulong e,string f){}','public void Decode(string a,ulong b,string c,ulong d,ulong e,string f){ManagedDecode(a,b,c,d,e,f);}');
 compatibility=replaceOnce(compatibility,'return Fixture.OutputStops;','return ManagedOutputStopResult(Fixture.OutputStops);');
 compatibility=replaceOnce(compatibility,'\"injected-reader-stall\"','\"sdk-output-stop-timeout\"');
 const pieces=['private static async Task<HostTerminalCleanup.LoopResult> RunPipeClientAsync(','private sealed class MessageCodec'].map(marker=>({...member(source,marker),marker}));
 const files={
 'SignalSdkTerminal.cs':`// Inert SDK data shapes for the actual HostVirtualSignalEvidence observer.
// The enum/state shape matches the existing report-chain SDK stand-ins.
namespace HIDMaestro {
 [System.Flags] internal enum HMButton { None=0,A=1,B=2,X=4,Y=8,LeftBumper=16,RightBumper=32,Back=64,Start=128,LeftStick=256,RightStick=512,Guide=1024 }
 internal enum HMHat { None=0,North=1,NorthEast=2,East=3,SouthEast=4,South=5,SouthWest=6,West=7,NorthWest=8 }
 internal enum HMAxis { X,Y,Z,Rx,Ry,Rz,Vx,Vy }
 internal struct HMGamepadState { public HMButton Buttons; public HMHat Hat; public System.Collections.Generic.Dictionary<HMAxis,float>? Axes; }
}
// USB maintenance memory leaves come solely from createOfflineHostFixture.
`,
 'ExtractedHost.cs':fake,'FixtureTerminals.cs':compatibility,'LegacyFixture.cs':legacy,
 'ManagedEntry.cs':managedEntry,
 'ProductionPipe.cs':'using System.Diagnostics; using System.IO.Pipes; using System.Text; using YeManInputHost.Diagnostics;\ninternal static partial class Program {\n'+pieces.map(r=>r.text).join('\n')+'\n}\n',
 'ParentProcessLifetime.cs':read('InputHost/ParentProcessLifetime.cs'),
  'PsEndpointOwnership.cs':read('InputHost/PsEndpointOwnership.cs')
 };
 for(const name of ['MsiSteamDeckReleaseFence1726.cs','MsiVirtualReportChain1410.cs','CanonicalFrameFingerprintMemo.cs','HostResponseFields.cs','CompactFrameReplyBinding.cs','HostGameOutputObservation.cs','HostVirtualSignalEvidence.cs','HostSdkFirstWitness.cs','TargetCreationLifetimeEvidence.cs','SteamDeckPadReportObservation.cs'])files[name]=read('InputHost/'+name);
 files['HcControllerGameFeedbackCapabilityCatalog.generated.cs']=read('capabilities/HcControllerGameFeedbackCapabilityCatalog.generated.cs');
 if(options.psOwnershipSelfTest)files['PsEndpointOwnershipTests.cs']=read('tools/ps_endpoint_ownership_selftest.cs');
 for(const [name,text]of Object.entries(files))fs.writeFileSync(path.join(out,name),text,{flag:'wx'});
 const project=path.join(out,'Fixture.csproj'),dll=path.join(out,'bin/Release/net10.0/Fixture.dll');
 const terminalSource=inputs.find(row=>row.relative==='InputHost/HostTerminalCleanup.cs');
 need(terminalSource&&fileHash(terminalSource.snapshot)===terminalSource.sha256,'Actual terminal policy snapshot required');
 const compileNames=[...Object.keys(files),path.relative(out,terminalSource.snapshot),'BuildIdentity.g.cs'];
 const projectText=`<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net10.0</TargetFramework><AssemblyName>Fixture</AssemblyName><StartupObject>${options.psOwnershipSelfTest?'PsEndpointOwnershipTestEntry':'Program'}</StartupObject><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable><UseAppHost>false</UseAppHost><NuGetAudit>false</NuGetAudit><EnableNETAnalyzers>false</EnableNETAnalyzers><EnableDefaultCompileItems>false</EnableDefaultCompileItems><ImportDirectoryBuildProps>false</ImportDirectoryBuildProps><ImportDirectoryBuildTargets>false</ImportDirectoryBuildTargets><UseSharedCompilation>false</UseSharedCompilation><Deterministic>true</Deterministic></PropertyGroup><ItemGroup>${compileNames.map(n=>'<Compile Include="'+xml(n)+'"/>').join('')}</ItemGroup><Import Project="FixtureInputs.targets"/></Project>\n`;
 fs.writeFileSync(project,projectText,{flag:'wx'});
 fs.writeFileSync(path.join(out,'NuGet.Config'),'<configuration><packageSources><clear /></packageSources></configuration>\n',{flag:'wx'});
 fs.writeFileSync(path.join(out,'FixtureInputs.targets'),`<Project><Target Name="RecordManagedFixtureInputs" AfterTargets="CoreCompile"><ItemGroup><_ManagedImports Include="$(MSBuildAllProjects)"/></ItemGroup><WriteLinesToFile File="$(MSBuildProjectDirectory)/evaluated-inputs.txt" Lines="@(Compile-&gt;'Compile&#x9;%(FullPath)');@(ReferencePath-&gt;'Reference&#x9;%(FullPath)');@(Analyzer-&gt;'Analyzer&#x9;%(FullPath)');@(_ManagedImports-&gt;'Import&#x9;%(FullPath)')" Overwrite="true" Encoding="UTF-8"/></Target></Project>\n`,{flag:'wx'});
 const generatedInputs=[...Object.keys(files),'Fixture.csproj','NuGet.Config','FixtureInputs.targets'].map(name=>{const p=path.join(out,name);return{path:p,sha256:fileHash(p),bytes:fs.statSync(p).size};});
 const bindings={schemaVersion:1,semanticMutation,semanticMutationScope:semanticMutation?"Explicit negative generated Host response condition only; source and pipe/codec bodies otherwise preserved":null,sources:inputs,generatedInputs,toolchainAllowlist:tools,ranges:[...generated.receipt,...signalMembers.map(({text,...r})=>({...r,file:path.join(host,'Program.cs'),sha256:hash(text),copiedVerbatim:true})),...pieces.map(({text,...r})=>({...r,file:path.join(host,'Program.cs'),sha256:hash(text),copiedVerbatim:true}))],constants:constants.join('\n'),scope:{productionPipeLoop:'verbatim',productionMessageCodec:'verbatim',productionParentLifetime:'verbatim',hostSession:'Existing actual-consumer extraction; existing fake clock replacements retained. Handle, HandleLocked, ReleaseLocal and ReleaseLocalLocked are not stubbed.',terminals:'Existing SDK/PnP/neutral/frame/diagnostic fakes reused. Stage diagnostic observation and one-shot fake output-stop result extended only in generated copies.',normalHostMainExecuted:false,realSdkLoaded:false,hardwareIo:false,productFilesEdited:false},toolchainMeaning:'Declared compiler/framework allowlist, not all files are claimed loaded; evaluated-inputs.txt identifies actual Compile/Reference/Analyzer/Import rows.'};
 const bindingText=JSON.stringify(bindings),bindingHash=hash(bindingText);
 fs.writeFileSync(path.join(out,'BuildIdentity.g.cs'),'internal static class ManagedFixtureIdentity { internal const string InputManifestSha256="'+bindingHash+'"; }\n',{flag:'wx'});
 const sdkVersion=run('sdk-version',['--version']).trim();
 run('build',['build',project,'-c','Release','--nologo','-v:minimal']);
 const ledger=fs.readFileSync(path.join(out,'evaluated-inputs.txt'),'utf8').replace(/^\uFEFF/,'');
 const actual=ledger.split(/\r?\n/).filter(Boolean).map(line=>{const [kind,...parts]=line.split('\t'),p=path.resolve(parts.join('\t'));need(['Compile','Reference','Analyzer','Import'].includes(kind)&&fs.existsSync(p),'Unbound actual compile input: '+line);return{kind,path:p,sha256:fileHash(p)};});
 need(actual.some(r=>r.kind==='Compile')&&actual.some(r=>r.kind==='Reference'),'Actual compiler inputs missing');
 const terminalCompile=actual.filter(row=>row.kind==='Compile'&&path.resolve(row.path)===path.resolve(terminalSource.snapshot));
 need(terminalCompile.length===1&&terminalCompile[0].sha256===terminalSource.sha256,'Actual terminal policy snapshot must be compiled exactly once');
 const ownershipInput=inputs.find(r=>r.relative==='InputHost/PsEndpointOwnership.cs');
 const ownershipCompile=actual.filter(r=>r.kind==='Compile'&&path.resolve(r.path)===path.resolve(out,'PsEndpointOwnership.cs'));
 need(ownershipCompile.length===1&&ownershipCompile[0].sha256===ownershipInput.sha256,'PS ownership helper missing or changed in actual compiler inputs');
 const observationInput=inputs.find(r=>r.relative==='InputHost/HostGameOutputObservation.cs');
 const observationCompile=actual.filter(r=>r.kind==='Compile'&&path.resolve(r.path)===path.resolve(out,'HostGameOutputObservation.cs'));
 need(observationCompile.length===1&&observationCompile[0].sha256===observationInput.sha256,'Observation helper missing or changed in actual compiler inputs');
 const signalInput=inputs.find(r=>r.relative==='InputHost/HostVirtualSignalEvidence.cs');
 const signalCompile=actual.filter(r=>r.kind==='Compile'&&path.resolve(r.path)===path.resolve(out,'HostVirtualSignalEvidence.cs'));
 need(signalCompile.length===1&&signalCompile[0].sha256===signalInput.sha256,'Virtual signal helper missing or changed in actual compiler inputs');
 const signalDecoderInput=inputs.find(r=>r.relative==='InputHost/SteamDeckPadReportObservation.cs');
 const signalDecoderCompile=actual.filter(r=>r.kind==='Compile'&&path.resolve(r.path)===path.resolve(out,'SteamDeckPadReportObservation.cs'));
 need(signalDecoderCompile.length===1&&signalDecoderCompile[0].sha256===signalDecoderInput.sha256,'Virtual signal decoder missing or changed in actual compiler inputs');
 const gameFeedbackCatalogInput=inputs.find(r=>r.relative==='capabilities/HcControllerGameFeedbackCapabilityCatalog.generated.cs');
 const gameFeedbackCatalogCompile=actual.filter(r=>r.kind==='Compile'&&path.resolve(r.path)===path.resolve(out,'HcControllerGameFeedbackCapabilityCatalog.generated.cs'));
 need(gameFeedbackCatalogCompile.length===1&&gameFeedbackCatalogCompile[0].sha256===gameFeedbackCatalogInput.sha256,'Game feedback catalog missing or changed in actual compiler inputs');
 const declared=new Map(tools.map(r=>[r.path.toLowerCase(),r]));
 for(const row of actual){const r=path.relative(out,row.path);if(r==='..'||r.startsWith('..'+path.sep)||path.isAbsolute(r)){need(declared.has(row.path.toLowerCase()),'Compiler input outside declared allowlist: '+row.path);need(declared.get(row.path.toLowerCase()).sha256===row.sha256,'Compiler input changed: '+row.path);}}
 const sourceStable=inputs.every(r=>fileHash(r.path)===r.sha256&&fileHash(r.snapshot)===r.sha256),toolchainStable=tools.every(r=>fileHash(r.path)===r.sha256);need(sourceStable&&toolchainStable,'Input changed during build');
 need(generatedInputs.every(r=>fileHash(r.path)===r.sha256),'Generated compilation input changed during build');
 const runtimeArtifacts=['Fixture.dll','Fixture.deps.json','Fixture.runtimeconfig.json'].map(name=>{const p=path.join(path.dirname(dll),name);return{path:p,sha256:fileHash(p),bytes:fs.statSync(p).size};});
 const result={schemaVersion:1,status:'BUILT',semanticMutation,behaviorPass:null,fixtureExecuted:false,fixtureProject:project,fixtureDll:dll,manifestPath:path.join(path.dirname(dll),'fixture-manifest.json'),inputManifestSha256:bindingHash,sourceStable,toolchainStable,sdkVersion,commands,runtimeArtifacts,evaluatedCompilerInputs:actual,generatedIdentity:{path:path.join(out,'BuildIdentity.g.cs'),sha256:fileHash(path.join(out,'BuildIdentity.g.cs'))},normalHostMainExecuted:false,realSdkLoaded:false,hardwareIo:false,sourceFilesEdited:false,cli:['dotnet',dll,'--pipe','NAME','--parent-pid','PID','--receipt','ABS_NEW_JSON','[--fault output-stop-once]'],receiptContract:['hasController','hasContext','phase','events','faultTriggered','pipeExitCode','outputStopResults','stateAfterLoop','stateAfterFinalCleanup','inputBindings'],boundaries:['Compiled fixture only. Behavioral/natural-exit conclusions require the native parent to run scenarios and observe process exit.','SDK SHA/MVID in replies are current product constants; the admitted asset verifier fake does not load/verify the actual SDK.','Only BCL runtime and the generated fixture assembly are loaded; no device/driver/Steam calls.']};
 const manifestText=JSON.stringify(result).slice(0,-1)+',"inputBindings":'+bindingText+'}\n';
 fs.writeFileSync(result.manifestPath,manifestText,{flag:'wx'});fs.writeFileSync(path.join(out,'fixture-manifest.json'),manifestText,{flag:'wx'});
 const output={status:result.status,semanticMutation,fixtureProject:project,fixtureDll:dll,manifestPath:result.manifestPath,manifestSha256:fileHash(result.manifestPath),runtimeArtifacts,sourceStable,toolchainStable,sdkVersion};
 json(path.join(out,'build-result.json'),output);return output;
 }catch(error){json(path.join(out,'failure.json'),{status:'INCOMPLETE',error:error.message,commands,normalHostMainExecuted:false,realSdkLoaded:false,hardwareIo:false});throw error;}
}
if(process.argv[1]&&path.resolve(process.argv[1])===self){try{
 const args=process.argv.slice(2);
 need([2,4,6].includes(args.length)&&args[0]==='--out','Use --out ABS_NEW_DIRECTORY [--mutation omit-recreate-owned-pin] [--evidence-root ABS_EXISTING_OWNED_ROOT]');
 const selected=new Map();
 for(let i=2;i<args.length;i+=2){
  const key=args[i],value=args[i+1];
  need(['--mutation','--evidence-root'].includes(key)&&!selected.has(key)&&typeof value==='string'&&value.trim()!==''&&!value.startsWith('--'),'Invalid or repeated managed build option');
  selected.set(key,value);
 }
 console.log(JSON.stringify(buildManagedFixture(args[1],{semanticMutation:selected.get('--mutation')??null,...(selected.has('--evidence-root')?{evidenceRoot:selected.get('--evidence-root')}:{})}),null,2));
}catch(error){console.error(error.stack??error.message);process.exitCode=1;}}
