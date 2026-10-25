import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';import{spawnSync}from'node:child_process';import{pathToFileURL,fileURLToPath}from'node:url';
const args=process.argv.slice(2);const opt=(k,d)=>{const i=args.indexOf(k);return i<0?d:args[i+1];};
const repo=path.resolve(opt('--repo',path.join(path.dirname(fileURLToPath(import.meta.url)),'..')));
const source=path.resolve(opt('--source-root',path.join(repo,'InputHost'))),out=path.resolve(opt('--out','./game-output-fixture'));
if(fs.existsSync(out))throw Error('Fresh evidence directory required');fs.mkdirSync(out,{recursive:true});
const {createOfflineHostFixture}=await import(pathToFileURL(opt('--generator',repo+'/tools/msi_steamdeck_release_fence_1726_selftest.mjs')));
const inputNames=fs.readdirSync(source).filter(n=>n.endsWith('.cs'));
const linkedCatalog=path.resolve(opt('--game-catalog',path.join(repo,'capabilities/HcControllerGameFeedbackCapabilityCatalog.generated.cs')));
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
const sources=[...(fs.existsSync(linkedCatalog)?[linkedCatalog]:[]),...inputNames.map(n=>path.join(source,n)),opt('--generator',repo+'/tools/msi_steamdeck_release_fence_1726_selftest.mjs'),repo+'/tools/msi_steamdeck_release_fence_1726_selftest.cs',repo+'/tools/inputhost_lifecycle_selftest.cs',fileURLToPath(new URL('./inputhost_game_output_observation_selftest.cs',import.meta.url)),repo+'/capabilities/CONTROLLER-GAME-OUTPUT-OBSERVATION.json',repo+'/deps/hidmaestro/HIDMaestro.Core.dll',new URL(import.meta.url).pathname.replace(/^\/([A-Za-z]:)/,'$1')].map(file=>({path:file,sha256:sha(fs.readFileSync(file))}));
const contract=JSON.parse(fs.readFileSync(path.join(repo,'capabilities/CONTROLLER-GAME-OUTPUT-OBSERVATION.json'),'utf8'));
if(contract.schemaVersion!==1||contract.maximumPayloadBytes!==256||contract.completePacketQueue!==false||contract.protocolDecoded!==false||contract.gameFeedbackForwardingProven!==false||contract.physicalAcceptance!==false||contract.cpuGpuConsumptionChanged!==false)throw Error('GAME_OUTPUT_OBSERVATION_CONTRACT_SCOPE');
if(sha(fs.readFileSync(path.join(repo,contract.lockedSdk.file)))!==contract.lockedSdk.sha256)throw Error('GAME_OUTPUT_OBSERVATION_SDK_STALE');
let {fake,receipt}=createOfflineHostFixture(out,source,true,{gameFeedbackCatalogPath:linkedCatalog,includePsOwnershipSupport:true});
fake=fake.replace(/^internal enum HMOutputSource[^\n]*\n/m,'').replace(/^internal readonly struct HMOutputPacket[^\n]*\n/m,'').replace('public event Action<HMController,HMOutputPacket>? OutputReceived;','');
const fakeAnchor='internal sealed class HMController : IDisposable {';if(fake.split(fakeAnchor).length!==2)throw Error('Fake controller injection anchor');
fake=fake.replace(fakeAnchor,`enum HMOutputSource : byte { Output=0, FeatureWrite=1, FeatureRead=3 }
static class OutputEventFaults { internal static bool Add, Remove; }
readonly struct HMOutputPacket(HMOutputSource source,byte reportId,ReadOnlyMemory<byte> data,uint seqNo){public readonly HMOutputSource Source=source;public readonly byte ReportId=reportId;public readonly ReadOnlyMemory<byte> Data=data;public readonly uint SeqNo=seqNo;}
${fakeAnchor}
 private Action<HMController,HMOutputPacket>? _outputReceived;
 public event Action<HMController,HMOutputPacket>? OutputReceived { add { if(OutputEventFaults.Add)throw new IOException("fake-add");_outputReceived+=value; } remove { if(OutputEventFaults.Remove)throw new IOException("fake-remove");_outputReceived-=value; } }
 public int SubscriberCount=>_outputReceived?.GetInvocationList().Length??0;
 public Action<HMController,HMOutputPacket>? SnapshotCallback()=>_outputReceived;
 public void Emit(byte source,byte reportId,byte[] data,uint sequence)=>_outputReceived?.Invoke(this,new((HMOutputSource)source,reportId,data,sequence));
`);
fs.writeFileSync(path.join(out,'Extracted.cs'),fake);
const program=fs.readFileSync(path.join(source,'Program.cs'),'utf8');
let extra='using YeManInputHost.Diagnostics; internal static partial class Program { private sealed partial class HostSession { public HMController? ObservedController=>_controller;\n';
function member(name){const rx=new RegExp('^        private [^\\n]*\\b'+name+'\\(', 'm');const m=rx.exec(program);if(!m)throw Error('Missing actual observation method '+name);const start=m.index,mask=program.slice(start).replace(/"(?:\\.|[^"\\])*"|\/\/[^\n]*|\/\*[\s\S]*?\*\//g,m=>m.replace(/[^\n]/g,' '));let depth=0,end;for(let i=mask.indexOf('{');i<mask.length;i++){if(mask[i]==='{')depth++;if(mask[i]==='}'&&--depth===0){end=i+1;break;}}if(!end)throw Error('Unclosed method');const code=program.slice(start,start+end);receipt.push({name,file:path.join(source,'Program.cs'),sha256:sha(code)});return code;}
const candidate=program.includes('private void BindGameOutputObservation(');
if(candidate&&!fake.includes('private void BindGameOutputObservation(')){for(const n of ['_gameOutputObservation','_gameOutputController','_gameOutputCallback','_gameOutputTuple']){const m=new RegExp('^        private [^\\n]*\\b'+n+'[^\\n]*;', 'm').exec(program);if(!m)throw Error('Missing observation field '+n);extra+=m[0]+'\n';}for(const n of ['BindGameOutputObservation','DetachGameOutputObservation','AppendGameOutputObservation'])extra+=member(n)+'\n';fs.copyFileSync(path.join(source,'HostGameOutputObservation.cs'),path.join(out,'HostGameOutputObservation.cs'));}
if(candidate)extra+=' public string AppendDirect(string response,string wire,bool ok){TryParseEnvelope(wire,out var envelope,out _);return AppendGameOutputObservation(response,envelope,ok);}';
extra+='}}';fs.writeFileSync(path.join(out,'Bindings.cs'),extra);
let legacy=fs.readFileSync(repo+'/tools/msi_steamdeck_release_fence_1726_selftest.cs','utf8').replace('internal static class Fixture','internal static partial class Fixture').replace('private static int Main()','private static int RunLegacyFenceTests()').replace('Fixture.Reset(); body(); ++_tests;','Fixture.Reset(); if (ContinueLegacy) LegacyRecord(name, body); else body(); ++_tests;');
fs.writeFileSync(path.join(out,'Legacy.cs'),legacy);fs.copyFileSync(repo+'/tools/inputhost_lifecycle_selftest.cs',path.join(out,'Test.cs'));
for(const n of ['MsiSteamDeckReleaseFence1726.cs','MsiVirtualReportChain1410.cs','CanonicalFrameFingerprintMemo.cs','HostResponseFields.cs'])fs.copyFileSync(path.join(source,n),path.join(out,n));
let gameTests=fs.readFileSync(new URL('./inputhost_game_output_observation_selftest.cs',import.meta.url),'utf8');
if(!candidate)gameTests=gameTests.slice(0,gameTests.indexOf('    // CANDIDATE_ONLY'))+'    private static int GameOutputMain()=>RunGameGroups(false);\n}\n';
fs.writeFileSync(path.join(out,'Game.cs'),gameTests);
fs.writeFileSync(path.join(out,'Entry.cs'),'internal static partial class Program { private static int Main(string[] args)=>args.Length==1?LifecycleMain(RunLegacyFenceTests):GameOutputMain(); }');
fs.writeFileSync(path.join(out,'Fixture.csproj'),'<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net10.0</TargetFramework><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable><NuGetAudit>false</NuGetAudit><EnableNETAnalyzers>false</EnableNETAnalyzers><UseAppHost>false</UseAppHost></PropertyGroup></Project>');
fs.writeFileSync(path.join(out,'NuGet.Config'),'<configuration><packageSources><clear /></packageSources></configuration>');
const profiles=[];
for(const config of (opt('--configs','Release,Debug').split(','))){
 const profile={config};
 for(const [name,a]of [['build',['build','Fixture.csproj','-c',config,'--nologo','-v:q']],...(candidate?[['original-lifecycle',[path.join(out,'bin',config,'net10.0/Fixture.dll'),'--lifecycle']]]:[]),['game-observation',[path.join(out,'bin',config,'net10.0/Fixture.dll')]]]){
  const r=spawnSync('C:/Program Files/dotnet/dotnet.exe',a,{cwd:out,encoding:'utf8',timeout:60000,windowsHide:true});const log=(r.stdout??'')+(r.stderr??'');fs.writeFileSync(path.join(out,config+'-'+name+'.log'),log);profile[name]={exit:r.status};if(r.error)profile[name].error=String(r.error);
  if(name==='build'&&r.status!==0)throw Error('Build failed: '+out+'/'+config+'-'+name+'.log');
  if(name!=='build'){try{profile[name].result=JSON.parse(r.stdout.trim().split(/\r?\n/).at(-1));}catch{}}
 }
 profiles.push(profile);
}
const stable=sources.every(r=>sha(fs.readFileSync(r.path))===r.sha256);
const passed=stable&&profiles.every(p=>p['game-observation'].exit===0&&(!candidate||p['original-lifecycle'].exit===0));
const result={status:passed?'PASS':'FAIL',candidate,stable,profiles,sources,extracted:receipt,sourceRoot:source,targetAssemblyLoaded:false,realHostStarted:false,deviceIo:false,actualMethods:['Host parser/dispatch/prepare/release/serializer',...(candidate?['Host output bind/detach/append','Bounded coalesced observation helper']:[])],injection:['SDK HMController inert event surface matching locked metadata; physical construction excluded','Existing fixture SDK/PnP/neutral/frame/stop delegates retained','Existing optional diagnostics inert; original three legacy parser expectation failures retained']};
fs.writeFileSync(path.join(out,'result.json'),JSON.stringify(result,null,2)+'\n');
console.log(JSON.stringify({status:result.status,candidate,stable,profiles:profiles.map(p=>({config:p.config,build:p.build.exit,lifecycle:p["original-lifecycle"]?.exit,game:p["game-observation"].exit,cases:p["game-observation"].result?.cases.length,failures:p["game-observation"].result?.cases.filter(c=>!c.pass)}))}));if(!passed)process.exitCode=1;
