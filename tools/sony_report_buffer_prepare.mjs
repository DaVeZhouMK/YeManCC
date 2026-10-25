// Generate an unapplied, byte-bound Sony buffer candidate and isolated real-SDK projects.
// This preparation command NEVER invokes dotnet, compilers, test binaries or perf.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {fileURLToPath} from 'node:url';
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const workspace=path.resolve(repo,'../../..');
const out=path.join(workspace,'Build/Validation/VirtualInput-Cpu-Exhaustion-20261018/HostSony');
if(process.argv.length!==2)throw Error('Preparation takes no execution options. Use the separate gated execute tool.');
if(fs.existsSync(path.join(out,'PREPARED.json')))throw Error('Frozen preparation exists; do not silently regenerate after results.');
fs.mkdirSync(out,{recursive:true});
const sha=x=>crypto.createHash('sha256').update(x).digest('hex');
const write=(name,value)=>{const f=path.join(out,name);fs.mkdirSync(path.dirname(f),{recursive:true});fs.writeFileSync(f,value);};
const save=(name,value)=>write(name,JSON.stringify(value,null,2)+'\n');
const sourceBytes=fs.readFileSync(path.join(repo,'InputHost/Program.cs')),source=sourceBytes.toString('utf8');
const eol=source.includes('\r\n')?'\r\n':'\n';
function block(text,start){let depth=0,state='code';for(let i=start;i<text.length;i++){const c=text[i],two=text.slice(i,i+2);if(state==='line'){if(c==='\n')state='code';}else if(state==='comment'){if(two==='*/'){state='code';i++;}}else if(state==='string'||state==='char'){if(c==='\\')i++;else if(c===(state==='string'?'"':"'"))state='code';}else if(two==='//'){state='line';i++;}else if(two==='/*'){state='comment';i++;}else if(c==='"')state='string';else if(c==="'")state='char';else if(c==='{')depth++;else if(c==='}'&&--depth===0)return i+1;}throw Error('Unclosed source block');}
function method(text,name){const re=new RegExp('^([ \\t]*)(?:private|public|internal) [^\\r\\n]*\\b'+name+'\\(','gm'),m=[...text.matchAll(re)];if(m.length!==1)throw Error('Method marker nonunique '+name);const start=m[0].index,end=block(text,text.indexOf('{',start));return{start,end,text:text.slice(start,end)};}
const edits=[];
function changeMethod(name,transform){const old=method(source,name),replacement=transform(old.text);if(replacement===old.text)throw Error('No candidate change '+name);edits.push({label:name,start:old.start,end:old.end,before:old.text,after:replacement});}
for(const [name,fill,isDs4]of [['BuildDs4V2DataOnlyReport','FillDs4V2DataOnlyReport',true],['BuildDualSenseDataOnlyReport','FillDualSenseDataOnlyReport',false]])changeMethod(name,old=>{
 const open=old.indexOf('{'),body=old.slice(open),signature=old.slice(0,open),alloc='        var data = new byte[63];';
 if(body.split(alloc).length!==2||body.split('        return data;').length!==2)throw Error('Changed builder allocation/return '+name);
 const guard=isDs4?'ds4-v2':'dualsense';
 const wrapper=signature+'{'+eol+'        if (profile.InputReportSize != 64)'+eol+'            throw new InvalidOperationException("'+guard+'-wire-size-mismatch:" + profile.InputReportSize);'+eol+'        var data = new byte[63];'+eol+'        '+fill+'(profile, envelope, '+(isDs4?'counter':'frameIndex')+', data'+(isDs4?'':', edgePaddles')+');'+eol+'        return data;'+eol+'    }';
 const fillSignature=isDs4?'    private static void '+fill+'(HMProfile profile, CommandEnvelope envelope, long counter, byte[] data)'+eol+'    ':'    private static void '+fill+'(HMProfile profile, CommandEnvelope envelope, long frameIndex,'+eol+'        byte[] data, int edgePaddles = 0)'+eol+'    ';
 const fillBody=body.replace(alloc,'        ArgumentNullException.ThrowIfNull(data);'+eol+'        if (data.Length != 63) throw new ArgumentException("sony-report-buffer-size-mismatch", nameof(data));'+eol+'        Array.Clear(data);').replace('        return data;','');
 return wrapper+eol+eol+fillSignature+fillBody;
});
function connect(old){
 const before=old;
 for(const expression of ['BuildDualSenseDataOnlyReport(profile, envelope, frameIndex)','BuildDualSenseDataOnlyReport(profile, envelope, frameIndex, MapDsEdgePaddles(envelope.BackButtons))','BuildDualSenseDataOnlyReport(profile, neutral, frameIndex)','BuildDualSenseDataOnlyReport(profile, neutral, frameIndex, MapDsEdgePaddles(neutral.BackButtons))']){
  const needle='var report = '+expression+';';if(!old.includes(needle))continue;
  const inside=expression.slice(expression.indexOf('(')+1,-1),parts=inside.split(', '),args=parts.slice(0,3).join(', ')+', _sonyReportBuffer'+(parts.length>3?', '+parts.slice(3).join(', '):'');
  old=old.replaceAll(needle,'FillDualSenseDataOnlyReport('+args+');'+eol+'                            var report = _sonyReportBuffer;');
 }
 for(const kind of ['envelope','neutral']){
  const needle='default: _controller.SubmitRawReport(BuildDs4V2DataOnlyReport(profile, '+kind+', _ds4FrameCounter++)); break;';
  if(old.includes(needle))old=old.replaceAll(needle,'default:'+eol+'                            FillDs4V2DataOnlyReport(profile, '+kind+', _ds4FrameCounter++, _sonyReportBuffer);'+eol+'                            _controller.SubmitRawReport(_sonyReportBuffer);'+eol+'                            break;');
 }
 if(old===before)throw Error('No production connection');return old;
}
changeMethod('TrySubmitFrame',connect);changeMethod('TrySubmitNeutral',connect);
const marker='        private readonly object _lifecycleGate = new();';const at=source.indexOf(marker);if(at<0||source.indexOf(marker,at+1)>=0)throw Error('Lifecycle field anchor');
edits.push({label:'HostSession._sonyReportBuffer',start:at+marker.length,end:at+marker.length,before:'',after:eol+'        // Owned by this serialized HostSession; the SDK synchronously copies each submission.'+eol+'        private readonly byte[] _sonyReportBuffer = new byte[63];'});
let candidate=source;for(const e of [...edits].sort((a,b)=>b.start-a.start))candidate=candidate.slice(0,e.start)+e.after+candidate.slice(e.end);
let reverse=candidate;for(const e of edits){if(e.after===''||reverse.split(e.after).length!==2)throw Error('Ambiguous reversal '+e.label);reverse=reverse.replace(e.after,e.before);}if(reverse!==source)throw Error('Candidate is not exactly reversible');
write('Program.before.cs',sourceBytes);write('Program.candidate.cs',Buffer.from(candidate));
save('candidate-edit-plan.json',{applied:false,productionBeforeSha256:sha(sourceBytes),candidateSha256:sha(Buffer.from(candidate)),exactReversalPass:true,edits:edits.map(e=>({...e,beforeSha256:sha(e.before),afterSha256:sha(e.after)})),allowedProductMembers:['BuildDs4V2DataOnlyReport','BuildDualSenseDataOnlyReport','FillDs4V2DataOnlyReport','FillDualSenseDataOnlyReport','HostSession._sonyReportBuffer','HostSession.TrySubmitFrame','HostSession.TrySubmitNeutral'],otherProductMembersChanged:false});
let diff='--- a/InputHost/Program.cs\n+++ b/InputHost/Program.cs\n';for(const e of [...edits].sort((a,b)=>a.start-b.start)){const line=source.slice(0,e.start).split('\n').length;const a=e.before.replaceAll('\r\n','\n').split('\n'),b=e.after.replaceAll('\r\n','\n').split('\n');diff+='@@ -'+line+','+a.length+' +'+line+','+b.length+' @@ '+e.label+'\n'+a.map(x=>'-'+x).join('\n')+'\n'+b.map(x=>'+'+x).join('\n')+'\n';}write('candidate-review.diff',diff);
const sourceFiles=[];function walk(d){return fs.readdirSync(d,{withFileTypes:true}).flatMap(e=>e.isDirectory()&&!['bin','obj'].includes(e.name)?walk(path.join(d,e.name)):e.isFile()?[path.join(d,e.name)]:[]);}
const files=walk(path.join(repo,'InputHost')).filter(f=>/\.cs$/.test(f));files.push(path.join(repo,'native/ProcessEfficiency.cs'));
for(const f of files){const rel=path.relative(repo,f).replaceAll('\\','/'),bytes=fs.readFileSync(f);sourceFiles.push({path:f,relativePath:rel,sha256:sha(bytes),bytes:bytes.length});write('SourceSnapshot/'+rel,bytes);}
const sdk=path.join(repo,'deps/hidmaestro/HIDMaestro.Core.dll'),sdkBytes=fs.readFileSync(sdk);if(sha(sdkBytes)!=='8fe7bdb21ec497591f527b7b87c4bc7b41b45c35a06cc211a7a0dce48838ed2c')throw Error('Unapproved SDK');
const ownership=path.join(workspace,'Build/Validation/VirtualInputPerf-R6-20261015/sdk-ownership');
const prior=JSON.parse(fs.readFileSync(path.join(ownership,'ownership-evidence.json'),'utf8'));
const sdkSources=[];for(const f of ['HMController.cs','SharedMemoryIO.cs','HMProfile.cs','HMController.il']){const file=path.join(ownership,f),b=fs.readFileSync(file);const expected=f.endsWith('.il')?prior.ilEvidence.sha256:prior.decompiledTypes.find(e=>path.basename(e.path)===f)?.sha256;if(expected&&expected!==sha(b))throw Error('Prior pinned SDK source drift '+f);sdkSources.push({path:file,sha256:sha(b),bytes:b.length,expectedSha256:expected??null});write('SdkProof/'+f,b);}
const plan={schemaVersion:1,frozenBeforeCompilation:new Date().toISOString(),candidate:'HOST-STEADY-01-only',scope:'Actual copied HostSession.Handle: parse + tuple/sequence validation + builder + unmodified pinned HMController.SubmitRawReport + real SDK shared-memory copy/SetEvent into exclusively owned memory + actual response serialization. No device or controller/context constructors, no pipe wait, no external consumer.',runtimeFrameworkVersion:'10.0.11',configurations:['Release-no-tier','Release-tieredPGO'],correctnessConfigurations:['Release','Debug'],scenarios: ['dualshock4/stable-zero','dualshock4/motion-touch','dualsense/stable-zero','dualsense/motion-touch','dualsense-edge/stable-zero','dualsense-edge/motion-touch'],rounds:11,minimumFasterPairs:9,minimumCpuReduction:0.03,maximumElapsedRegression:0.05,callsPerSample:65536,warmupPassesPerVariant:4,requiredMetrics:['process CPU cumulative delta; median and paired-median reduction >=3%, >=9/11 faster','raw current-thread cycles; same thresholds','candidate allocated bytes <= baseline in EVERY pair and strict lower per row median','complete-chain elapsed paired and ratio regression <=5%'],allRowsAndBothConfigurationsMustPass:true,officialCampaignsPerConfiguration:1,formalProcessTimeoutSeconds:420,noRetimingRejectedCandidates:true,noPostResultPlanChanges:true,noOutlierRemoval:true,noForcedGC:true,noPriorityOrAffinityChanges:true,adoption:'Only correctness + exact source/binary identity + all full-chain gates passing in both runtime modes permits a narrow product patch. No fake SDK fallback. If fixture cannot faithfully initialize true pinned types/methods, leave unapplied.',limits:['Pre-admitted private-memory fixture; not actual controller construction/driver/pipe/consumer validation.','Optional stage/cost/report-chain tracing is unarmed in formal runs; their payload borrowing contracts require separate source/correctness protection.','Separate session concurrency tests use explicit builder counters; do not claim production process-global counters support multiple live sessions.'],formalExecutionAllowedNow:false};save('acceptance-plan.json',plan);
const xml=s=>s.replaceAll('&','&amp;').replaceAll('<','&lt;');const runtime=path.join(repo,'deps/hc-runtime-upstream/HC-CANDIDATE-0.32.4.0-06c0b954-20260902');
const references=[sdk,path.join(runtime,'Nefarius.Utilities.DeviceManagement.dll'),path.join(runtime,'Nefarius.Drivers.HidHide.dll'),path.join(runtime,'Newtonsoft.Json.dll')].map(f=>({path:f,sha256:sha(fs.readFileSync(f)),bytes:fs.statSync(f).size}));
const props='<TargetFramework>net10.0-windows</TargetFramework><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable><PlatformTarget>x64</PlatformTarget><EnableDefaultCompileItems>false</EnableDefaultCompileItems><NuGetAudit>false</NuGetAudit><EnableNETAnalyzers>false</EnableNETAnalyzers><RuntimeFrameworkVersion>10.0.11</RuntimeFrameworkVersion><RollForward>Disable</RollForward>';
write('Projects/Contract/Contract.csproj','<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup>'+props+'<OutputType>Library</OutputType></PropertyGroup><ItemGroup><Compile Include="'+xml(path.join(repo,'tools/sony_report_buffer_contract.cs'))+'"/></ItemGroup></Project>');
const guards=['ReleaseLocal','ReleaseLocalLocked','TryPrepare','HandleHidHideCommand','HandleCloseRefreshBegin','TryEnsureDriverInstalled','TryEnsureUsbipBackend','RunPipeClientAsync','Error'];
for(const variant of ['Baseline','Candidate']){
 const original=variant==='Baseline'?source:candidate;let staged=original.replace('private sealed class HostSession','private sealed partial class HostSession');
 const safety=[];for(const name of guards){const m=method(staged,name),pos=staged.indexOf('{',m.start)+1,insert=eol+'            SonyReportBufferFixtureSafety.Forbidden("'+name+'");';staged=staged.slice(0,pos)+insert+staged.slice(pos);safety.push(name);}
 write('Projects/'+variant+'/Program.cs',Buffer.from(staged));
 const compile=files.filter(f=>path.basename(f)!=='Program.cs').map(f=>'<Compile Include="'+xml(path.join(out,'SourceSnapshot',path.relative(repo,f)))+'"/>').join('');
 const refs=references.map(r=>'<Reference Include="'+path.basename(r.path,'.dll')+'"><HintPath>'+xml(r.path)+'</HintPath><Private>true</Private></Reference>').join('');
 write('Projects/'+variant+'/'+variant+'.csproj','<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup>'+props+'<OutputType>Library</OutputType><AssemblyName>SonyReport'+variant+'</AssemblyName>'+(variant==='Candidate'?'<DefineConstants>SONY_REPORT_BUFFER_CANDIDATE</DefineConstants>':'')+'</PropertyGroup><ItemGroup><Compile Include="Program.cs"/>'+compile+'<Compile Include="'+xml(path.join(repo,'tools/sony_report_buffer_bridge.cs'))+'"/><ProjectReference Include="../Contract/Contract.csproj"/>'+refs+'</ItemGroup></Project>');
 save('Projects/'+variant+'/fixture-instrumentation.json',{variant,productionProgramSha256:sha(Buffer.from(original)),stagedProgramSha256:sha(Buffer.from(staged)),hostSessionDeclarationMadePartial:true,forbiddenProductMethods:safety,normalHotMethodsReplaced:false,sdkMethodsReplaced:false});
}
write('Projects/Runner/Runner.csproj','<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup>'+props+'<OutputType>Exe</OutputType><AssemblyName>SonyReportBufferFixture</AssemblyName></PropertyGroup><ItemGroup><Compile Include="'+xml(path.join(repo,'tools/sony_report_buffer_fixture.cs'))+'"/><ProjectReference Include="../Contract/Contract.csproj"/><ProjectReference Include="../Baseline/Baseline.csproj" Aliases="Baseline"/><ProjectReference Include="../Candidate/Candidate.csproj" Aliases="Candidate"/></ItemGroup></Project>');
write('Projects/NuGet.Config','<configuration><packageSources><clear/></packageSources></configuration>');
const tools=fs.readdirSync(path.join(repo,'tools')).filter(f=>/^sony_report_buffer_/.test(f)).map(f=>({path:path.join(repo,'tools',f),sha256:sha(fs.readFileSync(path.join(repo,'tools',f)))}));
save('PREPARED.json',{status:'UNAPPLIED_PREPARED_NOT_COMPILED_NOT_TIMED',preparedAt:new Date().toISOString(),productProgramSha256:sha(sourceBytes),candidateSha256:sha(Buffer.from(candidate)),sourceFiles,references,sdkSources,tools,planSha256:sha(fs.readFileSync(path.join(out,'acceptance-plan.json'))),sourceReversalPassed:true,compilerRuns:0,testRuns:0,formalPerformanceRuns:0,productionChanged:false});
console.log('Prepared unapplied Sony candidate and actual-source projects at '+out);
