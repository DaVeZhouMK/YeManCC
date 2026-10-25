import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
const moduleRelative='tools/ymcc_sim/global/onex-fan-coexistence';
const globalSources=['tools/ymcc_sim/global/candidate-options.mjs','tools/ymcc_sim/global/cli.mjs','tools/ymcc_sim/domains/global/global_domain.ps1'];
export const TOOL_KINDS=Object.freeze(['Harness','JoinedFactoryProbe','BootstrapFixture','STA','MTA']);
const entryNames={Harness:'OneXFanCoexistence.dll',JoinedFactoryProbe:'JoinedFactoryProbe.dll',BootstrapFixture:'YmccBootstrapFixture.dll',STA:'STA.dll',MTA:'MTA.dll'};
export const fileHash=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
export const argvHash=args=>crypto.createHash('sha256').update(JSON.stringify(args)).digest('hex');
function fail(code,detail){throw Object.assign(new Error(code),{code,detail,toolPolicyBlocked:true});}
function key(file){return path.resolve(file).toLowerCase();}
function same(a,b){return key(a)===key(b);}
function checkedFile(pin,code='ONEX_TOOL_ARTIFACT_CHANGED'){
 if(!pin||typeof pin.path!=='string'||!path.isAbsolute(pin.path)||!/^[a-f0-9]{64}$/.test(pin.sha256??''))fail('ONEX_TOOL_PIN_SHAPE',pin);
 if(!fs.existsSync(pin.path)||!fs.statSync(pin.path).isFile())fail('ONEX_TOOL_ARTIFACT_MISSING',{path:pin.path});
 if(fileHash(pin.path)!==pin.sha256)fail(code,{path:pin.path});return pin.path;
}
function read(pin){return JSON.parse(fs.readFileSync(checkedFile(pin),'utf8').replace(/^\uFEFF/,''));}
function unique(rows,label){const seen=new Set;for(const row of rows){const value=key(row.path);if(seen.has(value))fail('ONEX_TOOL_DUPLICATE_'+label,{path:row.path});seen.add(value);}}
// Producer sources only. Explicit project inputs provide generated source and
// compiler references. Dependency copies and outputs are never another source.
export function toolSourceInputs(repo){
 const base=path.join(repo,moduleRelative),files=[];
 const walk=dir=>{for(const item of fs.readdirSync(dir,{withFileTypes:true})){const f=path.join(dir,item.name);if(item.isSymbolicLink())fail('ONEX_TOOL_SOURCE_FORWARDING',{path:f});if(item.isDirectory()){if(!['bin','obj','Build','deps','fixtures','node_modules'].includes(item.name))walk(f);}else if(/\.(cs|csproj|props|mjs|ps1|json)$/.test(item.name))files.push(f);}};
 walk(base);
 const members=new Set(files.map(key));
 for(const project of files.filter(p=>/\.(csproj|props)$/.test(p))){
  const seen=new Set;for(const match of fs.readFileSync(project,'utf8').matchAll(/<(Compile|ProjectReference|Import)\s+(?:Include|Project)="([^"]+)"/g)){
   const [tag,name]=[match[1],match[2]];if(['$(FixtureSource)','$(BootstrapSource)'].includes(name))continue;
   if(/[$*?;]/.test(name))fail('ONEX_TOOL_PROJECT_DYNAMIC_SOURCE',{project,name});
   const file=path.resolve(path.dirname(project),name),relative=path.relative(base,file);
   if(relative.startsWith('..')||relative.split(path.sep).some(p=>['deps','obj','bin','Build'].includes(p))||!members.has(key(file)))fail('ONEX_TOOL_PROJECT_SOURCE_OUTSIDE_CLOSURE',{project,name});
   const id=tag+':'+key(file);if(seen.has(id))fail('ONEX_TOOL_PROJECT_DUPLICATE_SOURCE',{project,name});seen.add(id);
  }
 }
 files.push(...globalSources.map(p=>path.join(repo,p)));
 return files.sort((a,b)=>key(a).localeCompare(key(b))).map(p=>({path:path.resolve(p),sha256:fileHash(p)}));
}
export function installToolPolicyPlan({repoRoot,toolLoadPlan,toolLoadPlanSha256}){
 if(!toolLoadPlan||!toolLoadPlanSha256)fail('ONEX_TOOL_LOAD_PLAN_REQUIRED',{required:['toolLoadPlan','toolLoadPlanSha256']});
 const planPin={path:toolLoadPlan,sha256:toolLoadPlanSha256},plan=read(planPin);
 if(plan.schemaVersion!==1||!same(plan.repoRoot,repoRoot)||!Array.isArray(plan.sources)||!Array.isArray(plan.tools)||!Array.isArray(plan.calls))fail('ONEX_TOOL_PLAN_SHAPE');
 if(plan.calls.length===0)fail('ONEX_TOOL_INVOCATION_PLAN_EMPTY');
 for(const call of plan.calls)if(!['Harness','JoinedFactoryProbe'].includes(call.kind)||!/^[a-f0-9]{64}$/.test(call.argvSha256??'')||!call.policy||!path.isAbsolute(call.policy.path??'')||!/^[a-f0-9]{64}$/.test(call.policy.sha256??''))fail('ONEX_TOOL_INVOCATION_PLAN_SHAPE');
 unique(plan.sources,'SOURCE');if(!Array.isArray(plan.buildInputs))fail('ONEX_TOOL_BUILD_INPUT_PINS_REQUIRED');unique(plan.buildInputs,'BUILD_INPUT');unique(plan.calls.map(c=>({path:c.policy?.path??''})),'POLICY');
 const planned=new Map(plan.sources.map(p=>[key(p.path),p]));
 function verifySources(){checkedFile(planPin,'ONEX_TOOL_PLAN_CHANGED');const current=toolSourceInputs(repoRoot);if(current.length!==planned.size||current.some(p=>!planned.has(key(p.path))))fail('ONEX_TOOL_SOURCE_SET_CHANGED');for(const p of current)if(planned.get(key(p.path)).sha256!==p.sha256)fail('ONEX_TOOL_SOURCE_CHANGED',{path:p.path});}
 verifySources();
 if(plan.tools.length!==TOOL_KINDS.length||new Set(plan.tools.map(t=>t.kind)).size!==TOOL_KINDS.length||TOOL_KINDS.some(k=>!plan.tools.some(t=>t.kind===k)))fail('ONEX_TOOL_FIVE_PROJECT_BINDINGS_REQUIRED');
 const tools=new Map(plan.tools.map(t=>[t.kind,t]));
 for(const t of plan.tools)if(path.basename(t.entry.path)!==entryNames[t.kind])fail('ONEX_TOOL_ENTRY_KIND_MISMATCH',{kind:t.kind});
 // Require unique actual paths, not unique basenames: five projects can copy
 // the same helper, but each copy is explicitly hashed and must have one SHA.
 const used=new Set,receipts=[],schema5Admissions=new Map,schema6Admissions=new Map;
 function verifyArtifacts(kinds=TOOL_KINDS){
  const canonical=checkedFile(plan.sharedAuthority);
  const pins=[plan.sharedAuthority];
  for(const t of plan.tools.filter(t=>kinds.includes(t.kind))){checkedFile(t.entry);pins.push(t.entry);
   if(['STA','MTA'].includes(t.kind)){
    const absent=t.entry.path.replace(/\.dll$/i,'.deps.json');
    if(t.deps!==undefined||typeof t.depsAbsent!=='string'||!same(t.depsAbsent,absent)||fs.existsSync(absent))fail('ONEX_TOOL_TRACER_DEPS_MUST_BE_ABSENT',{kind:t.kind,path:absent});
   }else{checkedFile(t.deps);pins.push(t.deps);}
   if(t.kind!=='BootstrapFixture'){
    checkedFile(t.runtimeconfig);checkedFile(t.helper);pins.push(t.runtimeconfig,t.helper);
    if(t.helper.sha256!==plan.sharedAuthority.sha256||path.basename(t.helper.path)!=='ScopedLoadAuthority.dll')fail('ONEX_TOOL_SHARED_AUTHORITY_COPY_MISMATCH',{kind:t.kind});
    const cfg=read(t.runtimeconfig).runtimeOptions,frameworks=cfg?.frameworks??[cfg?.framework];
    if(cfg?.rollForward!=='Disable'||!['Microsoft.NETCore.App','Microsoft.WindowsDesktop.App'].every(name=>frameworks.some(f=>f?.name===name&&f.version==='10.0.11')))fail('ONEX_TOOL_RUNTIME_CONTRACT',{kind:t.kind});
   }
   const deps=t.deps?read(t.deps):{};for(const libs of Object.values(deps.targets??{}))for(const [library,body]of Object.entries(libs))for(const asset of Object.keys(body.runtime??{})){
    if(/Microsoft.Windows.SDK.NET|WinRT.Runtime/.test(library+' '+asset))fail('ONEX_TOOL_STATIC_SDK_DEPENDENCY',{kind:t.kind,library});
   }
  }
  unique(pins,'ARTIFACT');return canonical;
 }
 // Schema5 uses a separately reviewed, call-pinned metadata execution proof.
 // It is never written into policy.metadata and never launches another process.
 function schema5MetadataAdmission(call,policy,tool,launcher,executable){
  if(call.kind!=='Harness'||launcher.kind!=='MTA')fail('ONEX_TOOL_SCHEMA5_MTA_ROUTE_REQUIRED');
  if(!call.schema5MetadataAdmission)fail('ONEX_TOOL_SCHEMA5_METADATA_ADMISSION_REQUIRED');
  if(!plan.dotnet)fail('ONEX_TOOL_SCHEMA5_DOTNET_PIN_REQUIRED');
  const proof=read(call.schema5MetadataAdmission),pins=[call.schema5MetadataAdmission];
  if(proof.schemaVersion!==1||proof.kind!=='onex-schema5-metadata-admission'||proof.policySchemaVersion!==5)fail('ONEX_TOOL_SCHEMA5_METADATA_PROOF_SHAPE');
  for(const p of [call.schema5MetadataAdmission,proof.process,proof.receipt])if(policy.metadata.some(m=>p&&typeof p.path==='string'&&same(m.path,p.path)))fail('ONEX_TOOL_SCHEMA5_METADATA_PROOF_MUST_BE_EXTERNAL');
  const bind=(actual,expected,code)=>{checkedFile(actual,'ONEX_TOOL_SCHEMA5_METADATA_EVIDENCE_CHANGED');if(!expected||!same(actual.path,expected.path)||actual.sha256!==expected.sha256)fail(code);pins.push(actual);};
  bind(proof.policy,call.policy,'ONEX_TOOL_SCHEMA5_METADATA_POLICY_MISMATCH');
  bind(proof.sharedAuthority,plan.sharedAuthority,'ONEX_TOOL_SCHEMA5_METADATA_AUTHORITY_MISMATCH');
  bind(proof.entry,tool.entry,'ONEX_TOOL_SCHEMA5_METADATA_ENTRY_MISMATCH');
  bind(proof.launcher,launcher.entry,'ONEX_TOOL_SCHEMA5_METADATA_LAUNCHER_MISMATCH');
  bind(proof.helper,launcher.helper,'ONEX_TOOL_SCHEMA5_METADATA_HELPER_MISMATCH');
  if(proof.helper.sha256!==plan.sharedAuthority.sha256)fail('ONEX_TOOL_SCHEMA5_METADATA_AUTHORITY_MISMATCH');
  checkedFile(plan.dotnet);bind(proof.program,plan.dotnet,'ONEX_TOOL_SCHEMA5_METADATA_PROGRAM_MISMATCH');
  if(!same(executable,plan.dotnet.path))fail('ONEX_TOOL_SCHEMA5_METADATA_PROGRAM_MISMATCH');
  const process=read(proof.process),receipt=read(proof.receipt);pins.push(proof.process,proof.receipt);
  if(!process.program&&!process.dotnet)fail('ONEX_TOOL_SCHEMA5_METADATA_PROGRAM_MISMATCH');
  for(const p of [process.program,process.dotnet].filter(Boolean))bind(p,plan.dotnet,'ONEX_TOOL_SCHEMA5_METADATA_PROGRAM_MISMATCH');
  const duration=process.durationMs??process.elapsedMs;
  if(process.code!==0||process.signal!==null||process.error!==null||process.expired!==false||process.naturalExit===false||process.shell===true||!Number.isInteger(process.ownedPid)||process.ownedPid<=0||process.budgetMs!==60000||!Number.isFinite(duration)||duration<0||duration>60000||(process.durationMs!==undefined&&process.elapsedMs!==undefined&&process.durationMs!==process.elapsedMs))fail('ONEX_TOOL_SCHEMA5_METADATA_PROCESS_NOT_SUCCESS');
  if(typeof proof.cwd!=='string'||!path.isAbsolute(proof.cwd)||typeof process.cwd!=='string'||!path.isAbsolute(process.cwd)||!same(proof.cwd,process.cwd)||!fs.existsSync(proof.cwd)||!fs.statSync(proof.cwd).isDirectory())fail('ONEX_TOOL_SCHEMA5_METADATA_CWD_REQUIRED');
  const expected=[launcher.entry.path,'validate',call.policy.path,call.policy.sha256,proof.receipt.path];
  if(!Array.isArray(process.args)||JSON.stringify(process.args)!==JSON.stringify(expected))fail('ONEX_TOOL_SCHEMA5_METADATA_ARGV_MISMATCH');
  if(receipt.exitCode!==0||receipt.failure!==null||receipt.productLoaded!==false||JSON.stringify(receipt.events)!==JSON.stringify(['prepare-metadata-only']))fail('ONEX_TOOL_SCHEMA5_METADATA_RECEIPT_NOT_SUCCESS');
  if(!Array.isArray(receipt.actualLoadedManaged)||receipt.actualLoadedManaged.length===0)fail('ONEX_TOOL_SCHEMA5_METADATA_LOADED_GRAPH_REQUIRED');
  unique(receipt.actualLoadedManaged,'SCHEMA5_METADATA_LOADED_PATH');
  const loaded=receipt.actualLoadedManaged,exact=(row,p)=>same(row.path,p.path)&&row.sha256===p.sha256;
  for(const row of loaded){checkedFile(row,'ONEX_TOOL_SCHEMA5_METADATA_EVIDENCE_CHANGED');pins.push(row);if(typeof row.identity!=='string'||!row.identity.includes(', Version='))fail('ONEX_TOOL_SCHEMA5_METADATA_LOADED_GRAPH_REQUIRED');}
  const mta=loaded.find(r=>exact(r,launcher.entry)),authority=loaded.find(r=>exact(r,launcher.helper));
  if(mta?.identity!=='MTA, Version=1.0.0.0, Culture=neutral, PublicKeyToken=null'||authority?.identity!=='ScopedLoadAuthority, Version=0.0.0.0, Culture=neutral, PublicKeyToken=null')fail('ONEX_TOOL_SCHEMA5_METADATA_REQUIRED_TOOL_NOT_LOADED');
  const forbidden=new Set(['ONEXFANCOEXISTENCE','JOINEDFACTORYPROBE','YEMANFANHOST','YEMANFANBACKEND','HANDHELDCOMPANION','MICROSOFT.WINDOWS.SDK.NET','WINRT.RUNTIME']);
  const targets=policy.contexts.flatMap(c=>c.managed??[]).filter(p=>p.role!=='framework'&&p.sha256!==plan.sharedAuthority.sha256&&!exact(p,launcher.entry));
  if(loaded.some(r=>forbidden.has(r.identity.split(',')[0].toUpperCase())||exact(r,tool.entry)||targets.some(p=>exact(r,p)||r.sha256===p.sha256)))fail('ONEX_TOOL_SCHEMA5_METADATA_TARGET_LOADED');
  const uniquePins=new Map;for(const p of pins){const previous=uniquePins.get(key(p.path));if(previous&&previous.sha256!==p.sha256)fail('ONEX_TOOL_SCHEMA5_METADATA_PIN_CONFLICT');uniquePins.set(key(p.path),{path:p.path,sha256:p.sha256});}return [...uniquePins.values()];
 }
 function schema6MetadataAdmission(call,policy,tool,launcher,executable){
  if(call.kind!=='JoinedFactoryProbe'||launcher.kind!=='STA')fail('ONEX_TOOL_SCHEMA6_STA_JOINED_ROUTE_REQUIRED');
  if(policy.contexts?.length!==1||policy.contexts[0].id!=='Default'||policy.contexts[0].kind!=='default'||policy.contexts[0].registrationType!==null||policy.contexts[0].registrationAssembly!==null)fail('ONEX_TOOL_SCHEMA6_JOINED_DEFAULT_CONTEXT_REQUIRED');
  const entryPin=policy.contexts[0].managed?.filter(p=>same(p.path,tool.entry.path)&&p.sha256===tool.entry.sha256);if(entryPin?.length!==1||entryPin[0].identity!=='JoinedFactoryProbe, Version=1.0.0.0, Culture=neutral, PublicKeyToken=null'||entryPin[0].role!=='tool')fail('ONEX_TOOL_SCHEMA6_JOINED_ENTRY_REQUIRED');
  if(!call.schema6MetadataAdmission)fail('ONEX_TOOL_SCHEMA6_METADATA_ADMISSION_REQUIRED');
  if(!plan.dotnet)fail('ONEX_TOOL_SCHEMA6_DOTNET_PIN_REQUIRED');
  const proof=read(call.schema6MetadataAdmission),pins=[call.schema6MetadataAdmission];
  if(proof.schemaVersion!==1||proof.kind!=='onex-schema6-joined-metadata-admission'||proof.policySchemaVersion!==6)fail('ONEX_TOOL_SCHEMA6_METADATA_PROOF_SHAPE');
  for(const p of [call.schema6MetadataAdmission,proof.process,proof.receipt])if(policy.metadata.some(m=>p&&typeof p.path==='string'&&same(m.path,p.path)))fail('ONEX_TOOL_SCHEMA6_METADATA_PROOF_MUST_BE_EXTERNAL');
  const bind=(actual,expected,code)=>{checkedFile(actual,'ONEX_TOOL_SCHEMA6_METADATA_EVIDENCE_CHANGED');if(!expected||!same(actual.path,expected.path)||actual.sha256!==expected.sha256)fail(code);pins.push(actual);};
  bind(proof.policy,call.policy,'ONEX_TOOL_SCHEMA6_METADATA_POLICY_MISMATCH');
  bind(proof.sharedAuthority,plan.sharedAuthority,'ONEX_TOOL_SCHEMA6_METADATA_AUTHORITY_MISMATCH');
  bind(proof.entry,tool.entry,'ONEX_TOOL_SCHEMA6_METADATA_ENTRY_MISMATCH');
  bind(proof.launcher,launcher.entry,'ONEX_TOOL_SCHEMA6_METADATA_LAUNCHER_MISMATCH');
  bind(proof.helper,launcher.helper,'ONEX_TOOL_SCHEMA6_METADATA_HELPER_MISMATCH');
  if(proof.helper.sha256!==plan.sharedAuthority.sha256)fail('ONEX_TOOL_SCHEMA6_METADATA_AUTHORITY_MISMATCH');
  checkedFile(plan.dotnet);bind(proof.program,plan.dotnet,'ONEX_TOOL_SCHEMA6_METADATA_PROGRAM_MISMATCH');
  if(!same(executable,plan.dotnet.path))fail('ONEX_TOOL_SCHEMA6_METADATA_PROGRAM_MISMATCH');
  const process=read(proof.process),receipt=read(proof.receipt);pins.push(proof.process,proof.receipt);
  if(!process.program&&!process.dotnet)fail('ONEX_TOOL_SCHEMA6_METADATA_PROGRAM_MISMATCH');
  for(const p of [process.program,process.dotnet].filter(Boolean))bind(p,plan.dotnet,'ONEX_TOOL_SCHEMA6_METADATA_PROGRAM_MISMATCH');
  const duration=process.durationMs??process.elapsedMs;
  if(process.code!==0||process.signal!==null||process.error!==null||process.expired!==false||process.naturalExit!==true||process.shell!==false||!Number.isInteger(process.ownedPid)||process.ownedPid<=0||process.budgetMs!==60000||!Number.isFinite(duration)||duration<0||duration>60000||(process.durationMs!==undefined&&process.elapsedMs!==undefined&&process.durationMs!==process.elapsedMs))fail('ONEX_TOOL_SCHEMA6_METADATA_PROCESS_NOT_SUCCESS');
  if(typeof proof.cwd!=='string'||!path.isAbsolute(proof.cwd)||typeof process.cwd!=='string'||!path.isAbsolute(process.cwd)||!same(proof.cwd,process.cwd)||!fs.existsSync(proof.cwd)||!fs.statSync(proof.cwd).isDirectory())fail('ONEX_TOOL_SCHEMA6_METADATA_CWD_REQUIRED');
  const expected=[launcher.entry.path,'validate',call.policy.path,call.policy.sha256,proof.receipt.path];
  if(!Array.isArray(process.args)||JSON.stringify(process.args)!==JSON.stringify(expected))fail('ONEX_TOOL_SCHEMA6_METADATA_ARGV_MISMATCH');
  if(receipt.exitCode!==0||receipt.failure!==null||receipt.productLoaded!==false||JSON.stringify(receipt.events)!==JSON.stringify(['prepare-metadata-only']))fail('ONEX_TOOL_SCHEMA6_METADATA_RECEIPT_NOT_SUCCESS');
  if(!Array.isArray(receipt.actualLoadedManaged)||receipt.actualLoadedManaged.length===0)fail('ONEX_TOOL_SCHEMA6_METADATA_LOADED_GRAPH_REQUIRED');
  unique(receipt.actualLoadedManaged,'SCHEMA6_METADATA_LOADED_PATH');
  const loaded=receipt.actualLoadedManaged,exact=(row,p)=>same(row.path,p.path)&&row.sha256===p.sha256;
  for(const row of loaded){checkedFile(row,'ONEX_TOOL_SCHEMA6_METADATA_EVIDENCE_CHANGED');pins.push(row);if(typeof row.identity!=='string'||!row.identity.includes(', Version='))fail('ONEX_TOOL_SCHEMA6_METADATA_LOADED_GRAPH_REQUIRED');}
  const mta=loaded.find(r=>exact(r,launcher.entry)),authority=loaded.find(r=>exact(r,launcher.helper));
  if(mta?.identity!=='STA, Version=1.0.0.0, Culture=neutral, PublicKeyToken=null'||authority?.identity!=='ScopedLoadAuthority, Version=0.0.0.0, Culture=neutral, PublicKeyToken=null')fail('ONEX_TOOL_SCHEMA6_METADATA_REQUIRED_TOOL_NOT_LOADED');
  const forbidden=new Set(['ONEXFANCOEXISTENCE','JOINEDFACTORYPROBE','YEMANFANHOST','YEMANFANBACKEND','HANDHELDCOMPANION','MICROSOFT.WINDOWS.SDK.NET','WINRT.RUNTIME']);
  const targets=policy.contexts.flatMap(c=>c.managed??[]).filter(p=>p.role!=='framework'&&p.sha256!==plan.sharedAuthority.sha256&&!exact(p,launcher.entry));
  if(loaded.some(r=>forbidden.has(r.identity.split(',')[0].toUpperCase())||exact(r,tool.entry)||targets.some(p=>exact(r,p)||r.sha256===p.sha256)))fail('ONEX_TOOL_SCHEMA6_METADATA_TARGET_LOADED');
  const uniquePins=new Map;for(const p of pins){const previous=uniquePins.get(key(p.path));if(previous&&previous.sha256!==p.sha256)fail('ONEX_TOOL_SCHEMA6_METADATA_PIN_CONFLICT');uniquePins.set(key(p.path),{path:p.path,sha256:p.sha256});}return [...uniquePins.values()];
 }
 function route(executable,args,outDir){
  verifySources();
  if(!/^dotnet(?:\.exe)?$/i.test(path.basename(executable)))return {args,policyReceipt:null};
  if(args[0]==='build'){
   for(const arg of args.filter(a=>/^-p:(FixtureSource|BootstrapSource|CecilPath)=/.test(a))){
    const file=arg.slice(arg.indexOf('=')+1),matches=plan.buildInputs.filter(p=>same(p.path,file));
    if(matches.length!==1)fail('ONEX_TOOL_EXPLICIT_BUILD_INPUT_NOT_PINNED',{path:file});checkedFile(matches[0]);
   }
   const originalHost=typeof args[1]==='string'&&same(args[1],path.join(repoRoot,'FanLab/real-host/YeManFanHost.csproj'));
   return {args:originalHost?args:[...args,'-p:RuntimeFrameworkVersion=10.0.11','-p:RollForward=Disable'],policyReceipt:null};
  }
  const kind=Object.keys(entryNames).find(k=>path.basename(args[0]??'')===entryNames[k]);
  if(!kind)return {args,policyReceipt:null};
  if(!['Harness','JoinedFactoryProbe'].includes(kind))fail('ONEX_TOOL_DIRECT_LAUNCH_FORBIDDEN',{kind});
  verifyArtifacts([kind,kind==='JoinedFactoryProbe'?'STA':'MTA']);const tool=tools.get(kind);if(!same(tool.entry.path,args[0]))fail('ONEX_TOOL_UNBOUND_PRODUCER_PATH');
  const digest=argvHash(args.slice(1));const candidates=plan.calls.map((call,index)=>({call,index})).filter(({call,index})=>!used.has(index)&&call.kind===kind&&call.argvSha256===digest);
  if(candidates.length!==1)fail('ONEX_TOOL_EXACT_INVOCATION_POLICY_REQUIRED',{kind,argvSha256:digest});
  const {call,index}=candidates[0],policy=read(call.policy),metadata=policy.metadata??[];
  if((policy.schemaVersion!==2&&policy.schemaVersion!==5&&policy.schemaVersion!==6)||!same(policy.entry,tool.entry.path))fail('ONEX_TOOL_POLICY_ENTRY_MISMATCH');unique(metadata,'POLICY_METADATA');
  if(policy.contexts?.some(c=>c.managed?.some(p=>same(p.path,tools.get('BootstrapFixture').entry.path))))verifyArtifacts(['BootstrapFixture']);
  if(!metadata.some(p=>same(p.path,tool.deps.path)&&p.sha256===tool.deps.sha256))fail('ONEX_TOOL_PRODUCER_DEPS_POLICY_REQUIRED');
  for(const p of metadata)checkedFile(p);
  const d=policy.contexts?.filter(c=>c.id==='Default');
  if(d?.length!==1||!d[0].managed.some(p=>same(p.path,tool.entry.path)&&p.sha256===tool.entry.sha256))fail('ONEX_TOOL_ENTRY_PIN_POLICY_REQUIRED');
  const launcher=tools.get(kind==='JoinedFactoryProbe'?'STA':'MTA');
  const admission=policy.schemaVersion===5?schema5MetadataAdmission(call,policy,tool,launcher,executable):policy.schemaVersion===6?schema6MetadataAdmission(call,policy,tool,launcher,executable):null;used.add(index);
  const receipt=path.join(outDir,'tool-launch-'+String(receipts.length).padStart(3,'0')+'.json');
  if(admission)(policy.schemaVersion===6?schema6Admissions:schema5Admissions).set(receipt,admission);
  const routed=[launcher.entry.path,'run',call.policy.path,call.policy.sha256,receipt,...args.slice(1)];
  receipts.push({kind,argvSha256:digest,policy:call.policy,launcher:launcher.entry,entry:tool.entry,receipt});
  return {args:routed,policyReceipt:receipt};
 }
 function verifyConsumed(){for(const r of receipts){checkedFile(r.policy);checkedFile(r.entry);checkedFile(r.launcher);for(const p of schema5Admissions.get(r.receipt)??[])checkedFile(p,'ONEX_TOOL_SCHEMA5_METADATA_EVIDENCE_CHANGED');for(const p of schema6Admissions.get(r.receipt)??[])checkedFile(p,'ONEX_TOOL_SCHEMA6_METADATA_EVIDENCE_CHANGED');}}
 function verifyAfter(receipt){verifySources();verifyConsumed();if(!receipt)return;const result=JSON.parse(fs.readFileSync(receipt,'utf8'));if(result.failure!==null)fail('ONEX_TOOL_LAUNCH_AUTHORITY_FAILURE',{receipt,failure:result.failure});}
 function verifyFinished(){verifySources();verifyConsumed();if(used.size!==plan.calls.length)fail('ONEX_TOOL_UNUSED_INVOCATION_POLICIES',{declared:plan.calls.length,consumed:used.size});}
 return {route,verifyAfter,verifyFinished,verifySources,verifyArtifacts,receipts,planPin};
}
