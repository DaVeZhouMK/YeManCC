// Offline only: compile Host, then exercise exact extracted lifecycle/parser/
// serializer members with inert SDK/PnP/clock/HidHide stand-ins. Never run Host.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const out = path.resolve(process.argv[2] ?? 'G:/YeManCC-Work/Build/Validation/MSI-Controller-R12-20261013/non-sd-release-evidence-implementation-20261014/actual-host-functions');
const host = path.join(repo, 'InputHost'), source = path.join(host, 'Program.cs');
const text = fs.readFileSync(source, 'utf8');
fs.mkdirSync(out, { recursive: true });
const receipt = [];
const sha = s => crypto.createHash('sha256').update(s).digest('hex');
function mask(s) {
  return s.replace(/"(?:\\.|[^"\\])*"|\/\/[^\n]*|\/\*[\s\S]*?\*\//g, m => m.replace(/[^\n]/g, ' '));
}
function extract(name, file = source, kind = 'method', record = true) {
  const src = fs.readFileSync(file, 'utf8');
  const rx = new RegExp(`^[ \\t]*(?:private|public) (?:static |sealed |readonly )*[^\\n]*\\b${name}${kind === 'field' ? '\\s*=' : kind === 'enum' ? '\\s*\\{' : '\\('}`, 'm');
  const m = rx.exec(src); if (!m) throw Error('Missing current member ' + name);
  const start = m.index, s = mask(src.slice(start)); let end;
  if (kind === 'field') {
    let depth = 0;
    for (let i = 0; i < s.length; i++) { if (s[i] === '{') depth++; if (s[i] === '}') depth--; if (s[i] === ';' && depth === 0) { end = i + 1; break; } }
  } else {
    let k;
    if (kind === 'enum') k = s.indexOf('{');
    else { let depth = 0; for (let i = s.indexOf('('); i < s.length; i++) { if (s[i] === '(') depth++; if (s[i] === ')' && --depth === 0) { k = i + 1; break; } } while (/\s/.test(s[k])) k++; }
    if (s.slice(k, k + 2) === '=>') { end = s.indexOf(';', k) + 1; }
    else if (s[k] === ';') end = k + 1;
    else { if (s[k] !== '{') throw Error('Bad member body ' + name); let depth = 0; for (let i = k; i < s.length; i++) { if (s[i] === '{') depth++; if (s[i] === '}' && --depth === 0) { end = i + 1; break; } } }
  }
  if (!end) throw Error('Unclosed member ' + name);
  const code = src.slice(start, start + end);
  if (record) receipt.push({ name, file, line: src.slice(0, start).split('\n').length, sha256: sha(code) });
  return code;
}
const clock = code => code.replaceAll('Environment.TickCount64', 'Fixture.Now').replaceAll('Thread.Sleep', 'Fixture.Sleep');
const fields = ['BaseFields', 'FrameFields', 'HidHideFields'].map(n => extract(n, source, 'field'));
const methods = ['TryParseEnvelope', 'AttachReportChainMetadata1410Safely', 'TryReadString', 'TryReadUInt64', 'TryReadInt32', 'TryReadShort', 'TryReadByte', 'TryReadAxis', 'TryReadTrigger', 'TryReadImuAxis', 'IsSha256', 'CanonicalFrameHash', 'AppendFrameHashBits', 'SameTuple', 'DescribeEndpointCounts', 'DescribePrepareException', 'SanitizeReceiptField'].map(n => extract(n));
for (const n of ['WaitForEndpointFamilyRemoval', 'WaitForVirtualEndpointArrival', 'WaitForVirtualEndpointRemoval']) methods.push(clock(extract(n)));
for (const n of ['TryReadScreenPads', 'TryReadScreenPad']) methods.push(extract(n, path.join(host, 'ScreenTouchpadReport.cs')));
methods.push(extract('TryReadScreenButtons', path.join(host, 'ScreenButtonReport.cs')));
const session = ['HostSession', 'Handle', 'HandleLocked', 'ReleaseLocal', 'ReleaseLocalLocked', 'MsiNonSdSnapshot', 'CaptureMsiSdkDeclaredIdentityDiagnostic', 'BeginMsiNonSdReleaseDiagnostic', 'ObserveMsiNonSdReleaseDiagnostic', 'CompleteMsiNonSdReleaseDiagnostic', 'RememberIdempotent', 'TryPrepare', 'SerializeResponse', 'Ack'].map(n => clock(extract(n)));
const costLifetime = /^ {8}internal IDisposable\? CostDiagnosticLifetime => _msiCostDiagnostics;$/m.exec(text)?.[0];
if (!costLifetime) throw Error('missing current cost diagnostic lifetime');
receipt.push({ name:'CostDiagnosticLifetime', file:source, sha256:sha(costLifetime) });
const fake = `using System.Diagnostics; using System.Security.Cryptography; using System.Text; using System.Text.Json; using YeManInputHost.Diagnostics;
internal sealed class HMProfile { public string Id=""; public bool RequiresUsbipBackend; public ushort VendorId=0x28de,Pid=0x1205; public ushort ProductId=>Pid; }
internal sealed class HMController : IDisposable { public void Dispose(){Program.Fixture.Events.Add("controller-dispose");Program.Fixture.OnControllerDispose?.Invoke();if(Program.Fixture.ControllerThrows)throw new IOException("fake-controller");} }
internal sealed class HMContext : IDisposable {
 public HMContext(){Program.Fixture.Events.Add("context-create");} public void LoadDefaultProfiles(){} public HMProfile GetProfile(string id)=>new(){Id=id,RequiresUsbipBackend=id=="steam-deck-composite"};
 public HMController CreateController(HMProfile p){Program.Fixture.Creates++;Program.Fixture.Events.Add("sdk-create");if(Program.Fixture.CreateThrows)throw new IOException("fake-create");Program.Fixture.Current=Program.Fixture.GenericPersona is { } gp ? new(true,1,gp=="xbox360"?1:0,1,0,0,true,"fake-generic-prepare-cache") : Program.Arrival(Program.Fixture.NextRoot);return new();}
 public static void RemoveAllVirtualControllers(bool keep){Program.Fixture.Sweeps++;Program.Fixture.Events.Add("existing-sweep");Program.Fixture.OnSweep?.Invoke();if(Program.Fixture.SweepThrows)throw new IOException("fake-sweep");}
 public void Dispose(){Program.Fixture.Events.Add("context-dispose");if(Program.Fixture.ContextThrows)throw new IOException("fake-context");}
}
namespace YeManInputHost.Diagnostics { internal sealed class XboxSdkDiagnostics : IDisposable { public static XboxSdkDiagnostics? TryBegin(string a,string b,string c,ulong d,string e,string f,string g)=>null; public void SetEndpoint(string d){} public void Dispose(){} } }
namespace YeManInputHost.Diagnostics { internal sealed class MsiCostWindowDiagnostics : IDisposable {
 internal MsiCostWindowDiagnostics(bool enabled,string dir){Program.Fixture.Events.Add("cost-diagnostic-create");if(Program.Fixture.CostInitThrows)throw new IOException("fake-diagnostic-init");}
 internal void InvalidateController(){Program.Fixture.Events.Add("cost-generation-invalidated");}
 public void Dispose(){Program.Fixture.Events.Add("cost-diagnostic-dispose");}
} }
namespace YeManInputHost.Diagnostics {internal static class HostRuntimeBinaryIdentity {internal static object? Capture(params object[] inputs)=>null;internal static string AppendToHello(string response,object? identity,int maxBytes)=>response;}}
internal static partial class Program {
 private const int ProtocolVersion=1,MaxMessageBytes=16384,VirtualEndpointRemovalTimeoutMs=3500,VirtualEndpointArrivalTimeoutMs=3500,VirtualEndpointPollIntervalMs=50,SwitchReconcileFamilyWaitMs=2500;
 private const string LockedCoreSha256="fake-no-sdk",LockedCoreMvid="fake-no-sdk";
 private const long LockedCoreBytes=0;
 [ThreadStatic] private static Dictionary<string,JsonElement>? t_fieldCache;
 private readonly record struct ScreenPad(bool Active,bool Click,short X,short Y,ushort Pressure);
 private static bool _screenPadOwnsReport; private static uint _screenPadPacketCounter;
 ${fields.join('\n')}
 ${extract('CommandEnvelope')}
 ${extract('VirtualEndpointObservation').replace('private readonly record struct','internal readonly record struct')}
 ${extract('Phase', source, 'enum')}
 ${extract('IdempotentReply')}
 ${methods.join('\n')}
 private static VirtualEndpointObservation ObserveVirtualEndpoint(string? persona=null,bool requireInstanceIds=false){Fixture.Probes++;Fixture.Events.Add("observe");if(Fixture.ProbeThrows)throw new IOException("fake-pnp");return Fixture.OnObserve?.Invoke()??Fixture.Current;}
 private static void LogUsbipTransportLifecycleSafely(string s){}
 private static void NoteHostInstance(string s){} private static void NoteCommand(string s){} private static void RecordCostRole(string s){}
 private static bool TryVerifyLockedAsset(out string d){d="fake-asset";return true;}
 private static HMProfile? ApplySonyAxisDeclarationSwap(HMProfile? p)=>p;
 private static bool TryEnsureDriverInstalled(HMContext c,out string d,bool requireUsbip){d="fake-backend";return true;}
 private static T CreateTargetWithBackendWarning<T>(Func<(bool Ready,string Detail)> prepare,Func<T> create,out string receipt){receipt=prepare().Detail;return create();}
 private sealed class HostSession {
 private readonly object _lifecycleGate=new(); private HMContext? _context;private HMController? _controller;private CommandEnvelope? _tuple;
 private readonly string _startupNonce,_hostInstanceId="fake-host";private readonly int _reportChainParentPid1410;private readonly Dictionary<string,IdempotentReply> _idempotentReplies=new(StringComparer.Ordinal);
 private Phase _phase=Phase.AwaitHello;private ulong _lastSequence;private long _lastActivityAt,_lastSubmitElapsedUs,_costJsonParseUsSum,_costJsonParseCount;private bool _costFlagEnabled,_assetVerified,_targetPnpAdmitted,_virtualEndpointReconciled;
 private VirtualEndpointObservation _virtualEndpoint=new(false,0,0,0,0,0,false,"pnp-not-observed");
 private const string CostStateDir="fake-unused-path";
 private readonly bool _msiCostDeferredRequested;private readonly MsiCostWindowDiagnostics? _msiCostDiagnostics;
 ${costLifetime}
 private readonly bool _msiNonSdReleaseDiagnosticRequested;private MsiNonSdReleaseDiagnostic? _msiNonSdReleaseDiagnostic;private CommandEnvelope? _msiNonSdReleaseDiagnosticBinding;
 private MsiSteamDeckReleaseFence1726? _msiSteamDeckReleaseFence1726; private MsiReportChainRuntime1410? _reportChain1410;private bool _reportChainCapabilityAdvertised1410;
 ${session.join('\n')}
 // Inert stand-ins for unrelated IO. The actual dispatcher and lifetime methods
 // above are unmodified apart from clock/sleep injection. No SDK/PnP DLL loaded.
 private object? _runtimeBinaryIdentity;
 private readonly FakeHostStage? _msiHostStageEvidence=null;
 private sealed class FakeHostStage { public void Decode(params object[] fields){} }
 private void LogUsbipTransportLifecycleSafely(string s){Fixture.Events.Add("transport:"+s);}
 private void CloseReportChain1410Safely(string r){Fixture.Events.Add("report-chain-close");}
 private void ArmReportChain1410Safely(CommandEnvelope e){} private void ResetBackendSubmitStats(){} private string ComposeBackendSubmitStatsDetail()=>"fake-stats";
 private bool TrySubmitNeutral(out string d,CommandEnvelope? e=null,string reason="release-local"){Fixture.Events.Add("neutral");d="fake-neutral";if(Fixture.NeutralThrows)throw new IOException(d);return !Fixture.NeutralFails;}
 private bool TrySubmitFrame(CommandEnvelope e,out string d){d="fake-frame";return true;}
 private string Error(CommandEnvelope? e,string s,string d,string? releaseDisposition=null)=>SerializeResponse(e,false,s,d,null,releaseDisposition);
 private string HandleHidHideCommand(CommandEnvelope e,out bool shutdown){shutdown=false;Fixture.Events.Add(e.Command);return Ack(e,"fake-hidhide");}
 private string HandleCloseRefreshBegin(CommandEnvelope e){Fixture.Events.Add(e.Command);return Ack(e,"fake-cycle-begin");}
 private string HandleCloseRefreshPoll(CommandEnvelope e){Fixture.Events.Add(e.Command);return Ack(e,"fake-cycle-poll");}
 public bool PrepareDirect(CommandEnvelope e,out string d){_tuple=e;return TryPrepare(e,out d);} public MsiSteamDeckReleaseEvidence1726? Receipt=>_msiSteamDeckReleaseFence1726?.Evidence;
 }
}
`;
const fakeDir = path.join(out, 'fake'); fs.mkdirSync(fakeDir, { recursive: true });
fs.writeFileSync(path.join(fakeDir, 'ExtractedHost.cs'), fake);
const esc = s => s.replaceAll('&', '&amp;').replaceAll('"', '&quot;');
const includes = ['MsiSteamDeckReleaseFence1726.cs', 'MsiVirtualReportChain1410.cs', 'MsiNonSdReleaseDiagnostic.cs', 'MsiPnpCensusDiagnostic.cs', 'MsiSdkDeclaredIdentityDiagnostic.cs'].map(n => path.join(host, n)).concat(path.join(repo,'tools','msi_non_sd_release_evidence_actual_selftest.cs'));
fs.writeFileSync(path.join(fakeDir, 'Fake.csproj'), `<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net10.0</TargetFramework><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable><EnableDefaultCompileItems>false</EnableDefaultCompileItems><NuGetAudit>false</NuGetAudit></PropertyGroup><ItemGroup><Compile Include="ExtractedHost.cs"/>${includes.map(p => `<Compile Include="${esc(p)}"/>`).join('')}</ItemGroup></Project>`);
const hostDir = path.join(out, 'build'); fs.mkdirSync(hostDir, { recursive: true });
function csFiles(dir) { return fs.readdirSync(dir,{withFileTypes:true}).flatMap(e => e.isDirectory() ? ['obj','bin'].includes(e.name) ? [] : csFiles(path.join(dir,e.name)) : e.name.endsWith('.cs') ? [path.join(dir,e.name)] : []); }
const frozenFiles = csFiles(host).map(file => {
 const code = fs.readFileSync(file), frozen = path.join(hostDir,'frozen',path.relative(host,file));
 fs.mkdirSync(path.dirname(frozen),{recursive:true}); fs.writeFileSync(frozen,code);
 return {source:file,frozen,sha256:sha(code)};
});
fs.writeFileSync(path.join(out,'frozen-host-source-manifest.json'),JSON.stringify(frozenFiles,null,2));
const originalProject = fs.readFileSync(path.join(host, 'YeManInputHost.csproj'), 'utf8');
const hostProject = originalProject.replace('</PropertyGroup>', '<EnableDefaultCompileItems>false</EnableDefaultCompileItems><NuGetAudit>false</NuGetAudit></PropertyGroup>')
 .replace(/<HintPath>([^<]+)<\/HintPath>/g, (_, p) => `<HintPath>${esc(path.resolve(host,p.replace('$(MSBuildThisFileDirectory)','')))}</HintPath>`)
 .replace('</Project>', `<ItemGroup>${frozenFiles.map(f=>`<Compile Include="${esc(f.frozen)}"/>`).join('')}</ItemGroup></Project>`);
fs.writeFileSync(path.join(hostDir, 'HostCompileOnly.csproj'), hostProject);
for (const dir of [fakeDir,hostDir]) fs.writeFileSync(path.join(dir,'NuGet.Config'), '<configuration><packageSources><clear /></packageSources></configuration>');
const baseline = 'G:/YeManCC-Work/Build/Validation/MSI-Controller-R12-20261013/non-sd-release-evidence-implementation-20261014/before/InputHost__Program.cs';
const preserved = ['ArmReportChain1410Safely','CloseReportChain1410Safely','PrepareReportChainInvocation1410','SubmitSonyReportChain1410','SubmitEliteReportChain1410','TrySubmitFrame','TrySubmitNeutral','AttachReportChainMetadata1410Safely','BuildSteamDeckState','BuildDs4V2DataOnlyReport','BuildXbox360State','HandleHidHideCommand','HandleCloseRefreshBegin','HandleCloseRefreshPoll'];
const preservation = preserved.map(n => ({name:n,unchanged:extract(n, source,'method',false)===extract(n,baseline,'method',false)}));
if (preservation.some(p=>!p.unchanged)) throw Error('Concurrent/preexisting code overwritten: '+JSON.stringify(preservation));
fs.writeFileSync(path.join(out,'extracted-source-receipt.json'),JSON.stringify({sourceSha256:sha(text),members:receipt,preservation,substitutions:['Existing clock/sleep delegates replaced with fake clock in extracted lifetime/wait members','ObserveVirtualEndpoint -> fake complete enumeration snapshots','SDK/asset/backend/neutral/HidHide/error-log and cost-worker boundary -> inert fakes; production fence/parser/dispatch/prepare/release/serializer and diagnostic lifetime used'],realHostExecuted:false,realPnPRead:false,deviceServiceRegistrySteamOperations:false},null,2));
function run(label,args,cwd) {
 const result=spawnSync('dotnet',args,{cwd,encoding:'utf8',timeout:180000});
 const log=(result.stdout??'')+(result.stderr??'');fs.writeFileSync(path.join(out,label+'.log'),log);console.log(label+': exit='+result.status);
 if(result.error||result.status!==0)throw Error(label+' failed: '+(result.error??result.status));
}
run('host-build',['build','HostCompileOnly.csproj','-c','Release','--nologo'],hostDir);
run('fake-build',['build','Fake.csproj','-c','Release','--nologo'],fakeDir);
run('fake-tests',[path.join(fakeDir,'bin','Release','net10.0','Fake.dll')],fakeDir);
if (fs.readFileSync(source,'utf8')!==text) throw Error('Program.cs changed concurrently during validation; rerun required');
const nativeHeader = path.join(repo,'native','msi_steamdeck_release_fence_1726.h');
const nativeCode = fs.readFileSync(nativeHeader), pairDir = path.join(out,'paired-wire');fs.mkdirSync(pairDir,{recursive:true});
fs.writeFileSync(path.join(pairDir,'msi_steamdeck_release_fence_1726.h'),nativeCode);
const wireSource = path.join(repo,'tools','msi_steamdeck_release_fence_1726_selftest.cpp');
const obj = path.join(pairDir,'wire.obj'), exe = path.join(pairDir,'wire.exe');
const batch = path.join(pairDir,'compile-wire.cmd');
fs.writeFileSync(batch, '@echo off\r\ncall "C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul\r\nif errorlevel 1 exit /b 1\r\ncl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 "'+wireSource+'" /I"'+pairDir+'" /I"'+path.join(repo,'deps','json')+'" /Fo"'+obj+'" /Fe"'+exe+'"\r\nexit /b %errorlevel%\r\n');
function runProcess(label,command,args,cwd) {
 const result=spawnSync(command,args,{cwd,encoding:'utf8',timeout:180000});
 const log=(result.stdout??'')+(result.stderr??'');fs.writeFileSync(path.join(out,label+'.log'),log);console.log(log);
 if(result.error||result.status!==0)throw Error(label+' failed: '+(result.error??result.status));
}
runProcess('paired-wire-build','cmd.exe',['/d','/c',batch],pairDir);
runProcess('paired-wire-tests',exe,[path.join(fakeDir,'bin','Release','net10.0','wire-success.json'),path.join(fakeDir,'bin','Release','net10.0','wire-failure.json')],pairDir);
for(const file of frozenFiles) if(sha(fs.readFileSync(file.source))!==file.sha256)throw Error('Host source changed during validation '+file.source);
if(sha(fs.readFileSync(nativeHeader))!==sha(nativeCode))throw Error('Native header changed during paired tests; rerun');
fs.writeFileSync(path.join(out,'validated-deliverable.json'),JSON.stringify({hostExecuted:false,SDKExecuted:false,realPnP:false,frozenHostBinary:path.join(hostDir,'bin','Release','net10.0-windows','YeManInputHost.exe'),hostSha256:sha(fs.readFileSync(path.join(hostDir,'bin','Release','net10.0-windows','YeManInputHost.exe'))),hostDllSha256:sha(fs.readFileSync(path.join(hostDir,'bin','Release','net10.0-windows','YeManInputHost.dll'))),nativeHeaderSha256:sha(nativeCode),sourceManifest:path.join(out,'frozen-host-source-manifest.json'),tests:['fake-tests.log','paired-wire-tests.log']},null,2));
console.log('PASS Host frozen/compiled only; extracted fake session and paired native wire tests executed; no real Host/product/PnP/SDK operations.');
