// Virtual backend diagnostics ONLY. LB+RB reservation/summon proposals are
// WITHDRAWN/out of scope; this fixture performs no shortcut/OEM/service tests.
import fs from 'node:fs'; import path from 'node:path'; import crypto from 'node:crypto';
import { fileURLToPath } from 'node:url';
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const job=path.resolve('G:/YeManCC-Work/_scratch/msi-unified-reboot-20261012-1322/persona-runtime-1410');
const out=path.join(job,'runtime-fake');fs.mkdirSync(out,{recursive:true});
const program=path.join(repo,'InputHost','Program.cs'),touch=path.join(repo,'InputHost','ScreenTouchpadReport.cs'),buttons=path.join(repo,'InputHost','ScreenButtonReport.cs');
const current=fs.readFileSync(program,'utf8'),before=fs.readFileSync(path.join(job,'Program.before.cs'),'utf8'),tp=fs.readFileSync(touch,'utf8'),bp=fs.readFileSync(buttons,'utf8');
const receipt=[];
function clean(s){return s.replace(/\/\/[^\n]*|\/\*[\s\S]*?\*\//g,m=>m.replace(/[^\n]/g,' ')).replace(/"(?:\\.|[^"\\])*"/g,m=>' '.repeat(m.length));}
function extract(text,name,file,field=false){
 const rx=new RegExp(`^[ \\t]*private (?:static |sealed |readonly )*[^\\n]*\\b${name}${field?'\\s*=':'\\('}`,'m');const m=rx.exec(text);if(!m)throw Error('Missing actual member '+name);let start=m.index,s=clean(text.slice(start)),end;
 if(field){let depth=0;for(let i=0;i<s.length;i++){if(s[i]==='{')depth++;if(s[i]==='}')depth--;if(s[i]===';'&&depth===0){end=i+1;break;}}}
 else {let o=s.indexOf('('),d=0,k;for(let i=o;i<s.length;i++){if(s[i]==='(')d++;if(s[i]===')'&&--d===0){k=i+1;break;}}while(/\s/.test(s[k]))k++;if(s.slice(k,k+2)==='=>'){let dep=0;for(let i=k;i<s.length;i++){if(s[i]==='{')dep++;if(s[i]==='}')dep--;if(s[i]===';'&&dep===0){end=i+1;break;}}}else{if(s[k]!=='{')throw Error('Bad body '+name);let dep=0;for(let i=k;i<s.length;i++){if(s[i]==='{')dep++;if(s[i]==='}'&&--dep===0){end=i+1;break;}}}}
 if(!end)throw Error('Unclosed '+name);const code=text.slice(start,start+end);receipt.push({name,file,line:text.slice(0,start).split('\n').length,sha256:crypto.createHash('sha256').update(code).digest('hex')});return code;
}
const fields=['BaseFields','FrameFields','HidHideFields'].map(n=>extract(current,n,program,true));
const methods=['TryGetVirtualOutputProfile','TryParseEnvelope','AttachReportChainMetadata1410Safely','TryReadString','TryReadUInt64','TryReadInt32','TryReadShort','TryReadByte','TryReadAxis','TryReadTrigger','TryReadImuAxis','TryReadImuQuaternion','IsSha256','CanonicalFrameHash','AppendFrameHashBits','BuildDualSenseDataOnlyReport','WriteSonyImuLe16','QuantizeMotionAxis','AxisToByte','TriggerToByte','MapHatOctant','MapFaceButtons','MapDigitalButtons','MapDsEdgePaddles','BuildEliteState','UnitStickAxis'].map(n=>extract(current,n,program));
methods.push(extract(before,'TryParseEnvelope',job+'/Program.before.cs').replace('TryParseEnvelope(', 'TryParseEnvelopeBeforeRaw1410('));
for(const n of ['TryReadScreenPads','TryReadScreenPad','WriteDualSenseScreenPads','WriteDualSenseContact','WriteSonyContact'])methods.push(extract(tp,n,touch));
methods.push(extract(bp,'TryReadScreenButtons',buttons));
// Historical parser stays verbatim; only its inactive all-zero quaternion is
// projected to today's identity default for ordinary-control comparisons.
// Current base-vs-optional controls and every encoded report remain exact.
methods.push(` private static bool TryParseEnvelopeBefore1410(string message,out CommandEnvelope? envelope,out string error){
 var ok=TryParseEnvelopeBeforeRaw1410(message,out envelope,out error);
 if(ok && envelope is {Command:"SUBMIT_FRAME",ImuAdmitted:false,ImuQuatW:0,ImuQuatX:0,ImuQuatY:0,ImuQuatZ:0}) envelope=envelope with{ImuQuatW=1};
 return ok;
}`);
const record=extract(current,'CommandEnvelope',program);
const sessionMethods=['ArmReportChain1410Safely','CloseReportChain1410Safely','PrepareReportChainInvocation1410','SubmitSonyReportChain1410','SubmitEliteReportChain1410','TrySubmitFrame','TrySubmitNeutral','SerializeResponse','SameTuple'].map(n=>{const code=extract(current,n,program);return n==='ArmReportChain1410Safely'?code.replace('new MsiReportChainRuntime1410()','FakeRuntimeFactory1410()'):code;});
for(const n of ['TrySubmitFrame','TrySubmitNeutral','SerializeResponse'])sessionMethods.push(extract(before,n,job+'/Program.before.cs').replace(n+'(',n+'Before1410(').replaceAll('BuildXbox360State(', 'BuildEliteState('));
const fake=`using System.Diagnostics; using System.Security.Cryptography; using System.Text; using System.Text.Json; using YeManInputHost.Diagnostics;
[Flags] internal enum HMButton {None=0,A=1,B=2,X=4,Y=8,LeftBumper=16,RightBumper=32,Back=64,Start=128,LeftStick=256,RightStick=512,Guide=1024}
internal enum HMHat {None=0,North=1,NorthEast=2,East=3,SouthEast=4,South=5,SouthWest=6,West=7,NorthWest=8}
internal enum HMAxis {X,Y,Z,Rx,Ry,Rz,Vx,Vy}
internal sealed class HMProfile {public string Id {get;init;}="dualsense-edge";public int InputReportSize {get;init;}=64;}
internal struct HMGamepadState {public HMButton Buttons;public HMHat Hat;public Dictionary<HMAxis,float>? Axes;}
internal sealed class HMContext {public HMProfile? GetProfile(string n)=>new(){Id=n,InputReportSize=n=="xbox-360-wired"?18:n=="xbox-elite-v2"?0:64};}
internal sealed class HMController {public int RawCalls,StateCalls;public byte[]? LastRaw;public HMGamepadState LastState;public Exception? Failure;public Action? DuringSubmit;public void SubmitRawReport(ReadOnlySpan<byte> p){++RawCalls;LastRaw=p.ToArray();DuringSubmit?.Invoke();if(Failure!=null)throw Failure;}public void SubmitState(in HMGamepadState s){++StateCalls;LastState=s;DuringSubmit?.Invoke();if(Failure!=null)throw Failure;}}
internal static partial class Program {
 private const int ProtocolVersion=1,MaxMessageBytes=16384,DualSenseTouchWidth=1920,DualSenseTouchHeight=1080,DsEdgePaddleLeft=0x40,DsEdgePaddleRight=0x80;${extract(current,'SonyWireBatteryFull',program,true)}private static long _dualSenseFrameCounter,_ds4FrameCounter;
 private const string LockedCoreSha256="fake-metadata-only",LockedCoreMvid="fake-metadata-only";
 [ThreadStatic] private static Dictionary<string,JsonElement>? t_fieldCache;
 private readonly record struct ScreenPad(bool Active,bool Click,short X,short Y,ushort Pressure);
 ${fields.join('\n')}\n${record}\n${methods.join('\n')}
 // These unrelated backend branches are inert stand-ins, never real SDK code.
 private static void SubmitSteamDeckWithScreenPads(HMController c,HMProfile p,CommandEnvelope e,bool observePadData=false,bool isNeutralCommand=false)=>c.SubmitState(default);
 private static byte[] BuildDs4V2DataOnlyReport(HMProfile p,CommandEnvelope e,long n)=>new byte[63];
 private sealed class FakeObservation {public bool IsAbsent=true,AllStarted=true;public int PrimaryRootCount=1,XboxCompanionRootCount=0,HidChildCount=1;public string Detail="pnp-hid-ids=HID\\\\HIDCLASS\\\\FAKE[HID\\\\VID_054C&PID_0DF2];";}
 // Optional release/lifetime evidence stays absent in this report-chain-only fixture.
 // These inert adapters never replace tested parser/build/report-chain methods.
 private class InertOptionalEvidence {public object? Evidence=>null;public bool Matches(CommandEnvelope e)=>false;public string TryAppend(string value,int limit)=>value;public string TryAppend(string value,string command,int limit)=>value;}
 private sealed class MsiSteamDeckReleaseFence1726:InertOptionalEvidence {public const string PrepareField="msiSteamDeckReleaseFence";}
 private sealed class HostSession {
 private readonly MsiHostStageEvidence? _msiHostStageEvidence=null;
 private readonly MsiSteamDeckReleaseFence1726? _msiSteamDeckReleaseFence1726=null;
 private readonly InertOptionalEvidence? _msiNonSdReleaseDiagnostic=null,_targetCreationLifetime=null,_releaseTimingEvidence=null;
 private readonly CommandEnvelope? _msiNonSdReleaseDiagnosticBinding=null,_releaseTimingBinding=null;
 private readonly bool _msiNonSdReleaseDiagnosticRequested=false;
 private readonly HostRuntimeBinaryIdentity.Snapshot? _runtimeBinaryIdentity=null;
 private HMContext? _context=new();public HMController? _controller=new();private CommandEnvelope? _tuple;private readonly string _startupNonce="fake-nonce",_hostInstanceId="fake-host";private readonly int _reportChainParentPid1410=9844;public MsiReportChainRuntime1410? _reportChain1410;private bool _reportChainCapabilityAdvertised1410;private ulong _lastSequence=1;
 private bool _targetPnpAdmitted=true,_assetVerified=true,_virtualEndpointReconciled=false,_costFlagEnabled=false;private readonly FakeObservation _virtualEndpoint=new();
 private long _lastSubmitElapsedUs,_backendSubmitOk,_backendSubmitFail,_backendNeutralOk,_backendNeutralFail;private string _lastBackendDetail="";private ulong _lastBackendSequence;private long _costHidSubmitUsSum,_costHidSubmitCount;
 public Func<MsiReportChainRuntime1410>? TestRuntimeFactory; private MsiReportChainRuntime1410 FakeRuntimeFactory1410()=>TestRuntimeFactory?.Invoke()??throw new Exception("fake diagnostic factory unavailable");
 private static void RecordCostRole(string n){} private static void RecordCostFrame(long n,bool rejected){}
 ${sessionMethods.join('\n')}
 public HostSession(CommandEnvelope e){_tuple=e;_lastSequence=e.Sequence;}
 public bool Frame(CommandEnvelope e,out string detail)=>TrySubmitFrame(e,out detail);
 public bool FrameBefore(CommandEnvelope e,out string detail)=>TrySubmitFrameBefore1410(e,out detail);
 public bool Neutral(CommandEnvelope? e,out string detail,string reason="submit-neutral-command"){if(e!=null)_lastSequence=e.Sequence;return TrySubmitNeutral(out detail,e,reason);}
 public bool NeutralBefore(out string detail)=>TrySubmitNeutralBefore1410(out detail);
 public string Response(CommandEnvelope e,bool ok,string status)=>SerializeResponse(e,ok,status,"detail",null,null);
 public string ResponseBefore(CommandEnvelope e,bool ok,string status)=>SerializeResponseBefore1410(e,ok,status,"detail",null,null);
 public void Close(string reason)=>CloseReportChain1410Safely(reason);
 public void ArmProduction(CommandEnvelope e)=>ArmReportChain1410Safely(e);
 public void SetAdmitted(bool value)=>_targetPnpAdmitted=value;
 public (long Ok,long Fail,long NeutralOk,long NeutralFail,string Detail,ulong Sequence) Counts=>(_backendSubmitOk,_backendSubmitFail,_backendNeutralOk,_backendNeutralFail,_lastBackendDetail,_lastBackendSequence);
 }
}
`;
fs.writeFileSync(path.join(out,'ExtractedRuntime.cs'),fake);
const esc=s=>s.replaceAll('&','&amp;').replaceAll('"','&quot;');
fs.writeFileSync(path.join(out,'RuntimeFake1410.csproj'),`<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net10.0</TargetFramework><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable><EnableDefaultCompileItems>false</EnableDefaultCompileItems><NuGetAudit>false</NuGetAudit><UseSharedCompilation>false</UseSharedCompilation></PropertyGroup><ItemGroup><Compile Include="ExtractedRuntime.cs"/><Compile Include="${esc(path.join(repo,'InputHost','MsiVirtualReportChain1410.cs'))}"/>${['CanonicalFrameFingerprintMemo.cs','HostResponseFields.cs','HostRuntimeBinaryIdentity.cs','MsiHostStageEvidence.cs'].map(n=>'<Compile Include="'+esc(path.join(repo,'InputHost',n))+'"/>').join('')}<Compile Include="${esc(path.join(repo,'tools','msi_report_chain_runtime_1410_selftest.cs'))}"/></ItemGroup></Project>`);
fs.writeFileSync(path.join(out,'NuGet.Config'),'<configuration><packageSources><clear /></packageSources></configuration>');
fs.writeFileSync(path.join(out,'extracted-source-receipt.json'),JSON.stringify({scope:'actual current parser/Sony+Elite builders/submit wrappers/neutral/serializer; baseline before parser/submit/serializer; inert HM types, no SDK',members:receipt,dependencySubstitutions:['HMProfile/HMContext/HMController replaced by inert fake type definitions','Historical before-method Elite builder symbol adapted to current BuildEliteState; legacy output branches are not executed','Legacy raw parser is retained unchanged; explicit comparison adapter projects only inactive zero quaternion to current identity; current base/optional controls and actual encoded bytes remain exact','Current hash memo/response/identity/stage data-only helpers compiled unchanged; stage/runtime identity instances null; release/lifetime optional evidence explicitly absent with inert null adapters','ArmReportChain1410Safely constructor-only seam: new MsiReportChainRuntime1410() -> injected FakeRuntimeFactory1410(); no product appdata I/O'],SDKLoaded:false,productExecuted:false},null,2));
console.log('GENERATED_RUNTIME_ACTUAL_MEMBER_FIXTURE '+out);




