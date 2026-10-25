import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {pathToFileURL,fileURLToPath} from 'node:url';
import {installToolPolicyPlan,toolSourceInputs,fileHash,argvHash,TOOL_KINDS} from './tool-policy-runtime.mjs';
const rel='tools/ymcc_sim/global/onex-fan-coexistence';
const names={Harness:'OneXFanCoexistence',JoinedFactoryProbe:'JoinedFactoryProbe',BootstrapFixture:'YmccBootstrapFixture',STA:'STA',MTA:'MTA'};
const write=(p,v)=>{fs.mkdirSync(path.dirname(p),{recursive:true});fs.writeFileSync(p,typeof v==='string'?v:JSON.stringify(v,null,2)+'\n');return p;};
const pin=p=>({path:p,sha256:fileHash(p)});
const clone=p=>structuredClone(p);
export async function runSchema6RouteModelTests({outDir,sourceBefore}){
 assert.ok(path.isAbsolute(outDir)&&outDir.split(/[\\/]/).includes('MODEL_ONLY'),'EXPLICIT_MODEL_ONLY_OUTPUT_REQUIRED');assert.equal(fs.existsSync(outDir),false,'FRESH_MODEL_OUTPUT_REQUIRED');fs.mkdirSync(outDir,{recursive:true});
 const moduleFile=fileURLToPath(new URL('./tool-policy-runtime.mjs',import.meta.url)),moduleBody=fs.readFileSync(moduleFile,'utf8'),before=sourceBefore?await import(pathToFileURL(sourceBefore).href):null,rows=[];
 let sequence=0;
 function fixture(schema=6,kind='JoinedFactoryProbe'){
  const dir=path.join(outDir,String(sequence++).padStart(3,'0')),repo=path.join(dir,'repo'),owned=path.join(dir,'owned'),base=path.join(repo,rel);
  write(path.join(base,'tool-policy-runtime.mjs'),moduleBody);write(path.join(base,'MODEL_ONLY.cs'),'// MODEL_ONLY; never compiled.\n');
  for(const k of TOOL_KINDS)write(path.join(base,k+'.csproj'),'<Project><ItemGroup><Compile Include="MODEL_ONLY.cs" /></ItemGroup></Project>\n');
  for(const f of ['tools/ymcc_sim/global/candidate-options.mjs','tools/ymcc_sim/global/cli.mjs','tools/ymcc_sim/domains/global/global_domain.ps1'])write(path.join(repo,f),'// MODEL_ONLY source sentinel\n');
  const canonical=pin(write(path.join(owned,'canonical','ScopedLoadAuthority.dll'),'MODEL_ONLY authority bytes; not a PE; never executed.\n'));
  const tools=TOOL_KINDS.map(kind=>{const root=path.join(owned,kind),entry=pin(write(path.join(root,names[kind]+'.dll'),'MODEL_ONLY '+kind+' bytes; not a PE; never executed.\n')),t={kind,entry};
   if(['MTA','STA'].includes(kind))t.depsAbsent=path.join(root,names[kind]+'.deps.json');else t.deps=pin(write(path.join(root,names[kind]+'.deps.json'),{targets:{}}));
   if(kind!=='BootstrapFixture'){t.runtimeconfig=pin(write(path.join(root,names[kind]+'.runtimeconfig.json'),{runtimeOptions:{rollForward:'Disable',frameworks:[{name:'Microsoft.NETCore.App',version:'10.0.11'},{name:'Microsoft.WindowsDesktop.App',version:'10.0.11'}]}}));t.helper=pin(write(path.join(root,'ScopedLoadAuthority.dll'),fs.readFileSync(canonical.path,'utf8')));}return t;});
  const tool=tools.find(t=>t.kind===kind),mta=tools.find(t=>t.kind==='STA'),args=[tool.entry.path,'MODEL_ONLY_CONSUMER_ARG','owned result'];
  const program=pin(write(path.join(owned,'program','dotnet.exe'),'MODEL_ONLY non-executable program bytes.\n'));
  const policyPath=path.join(dir,'policy.json'),processPath=path.join(dir,'metadata-process.json'),receiptPath=path.join(dir,'metadata-receipt.json'),proofPath=path.join(dir,'metadata-admission.json'),planPath=path.join(dir,'tool-plan.json'),launchOut=path.join(dir,'launch-out');fs.mkdirSync(launchOut);
  const policy={schemaVersion:schema,entry:tool.entry.path,ownedRoot:owned,metadata:[tool.deps],contexts:[{id:'Default',kind:'default',registrationType:null,registrationAssembly:null,managed:[{...tool.entry,identity:names[kind]+', Version=1.0.0.0, Culture=neutral, PublicKeyToken=null',role:'tool'},{...canonical,identity:'ScopedLoadAuthority, Version=0.0.0.0, Culture=neutral, PublicKeyToken=null',role:'infrastructure'}],requests:[]}]};write(policyPath,policy);
  const call={kind,argvSha256:argvHash(args.slice(1)),policy:pin(policyPath)};
  const receipt={exitCode:0,failure:null,productLoaded:false,events:['prepare-metadata-only'],actualLoadedManaged:[{...mta.entry,identity:'STA, Version=1.0.0.0, Culture=neutral, PublicKeyToken=null'},{...mta.helper,identity:'ScopedLoadAuthority, Version=0.0.0.0, Culture=neutral, PublicKeyToken=null'}],MODEL_ONLY:true};write(receiptPath,receipt);
  const proc={code:0,signal:null,error:null,expired:false,naturalExit:true,ownedPid:12345,budgetMs:60000,durationMs:100,shell:false,cwd:owned,program, args:[mta.entry.path,'validate',policyPath,call.policy.sha256,receiptPath],MODEL_ONLY:true};write(processPath,proc);
  const proof={schemaVersion:1,kind:'onex-schema6-joined-metadata-admission',policySchemaVersion:6,policy:call.policy,sharedAuthority:canonical,entry:tool.entry,launcher:mta.entry,helper:mta.helper,program,cwd:owned,process:pin(processPath),receipt:pin(receiptPath),MODEL_ONLY:true};write(proofPath,proof);call.schema6MetadataAdmission=pin(proofPath);
  const plan={schemaVersion:1,repoRoot:repo,sources:toolSourceInputs(repo),buildInputs:[],tools,sharedAuthority:canonical,dotnet:program,calls:[call]};
  const f={dir,repo,owned,base,tool,mta,args,program,policy,call,receipt,proc,proof,plan,policyPath,processPath,receiptPath,proofPath,planPath,launchOut,
   savePlan(){write(planPath,plan);f.planPin=pin(planPath);},
   saveProof(){write(proofPath,proof);call.schema6MetadataAdmission=pin(proofPath);f.savePlan();},
   saveProcess(){write(processPath,proc);proof.process=pin(processPath);f.saveProof();},
   saveReceipt(){write(receiptPath,receipt);proof.receipt=pin(receiptPath);f.saveProof();},
   savePolicy(){write(policyPath,policy);call.policy=pin(policyPath);f.savePlan();},
   install(api={installToolPolicyPlan}){return api.installToolPolicyPlan({repoRoot:repo,toolLoadPlan:f.planPin.path,toolLoadPlanSha256:f.planPin.sha256});},
   route(controller=f.install(),executable=program.path){return controller.route(executable,args,launchOut);},
   extra(name,body='MODEL_ONLY alternate bytes\n'){return pin(write(path.join(dir,'alternate',name),body));}
  };f.savePlan();return f;
 }
 function gate(name,fn){try{fn();rows.push({name,status:'PASS'});}catch(e){rows.push({name,status:'FAIL',message:e.message,stack:e.stack});}}
 function refused(name,code,edit){gate(name,()=>{const f=fixture();edit(f);assert.throws(()=>f.route(),e=>e.code===code,code);});}
 gate('MODEL_ONLY schema6 exact proof routes unchanged business argv without spawning',()=>{const f=fixture(),c=f.install(),r=f.route(c);assert.deepEqual(r.args,[f.mta.entry.path,'run',f.call.policy.path,f.call.policy.sha256,r.policyReceipt,...f.args.slice(1)]);write(r.policyReceipt,{failure:null});c.verifyAfter(r.policyReceipt);c.verifyFinished();assert.equal(c.receipts.length,1);assert.equal(fs.existsSync(path.join(f.owned,'Build')),false);});
 gate('schema2 retains exact legacy route and ignores schema6-only fields',()=>{const f=fixture(2);delete f.plan.dotnet;f.call.schema6MetadataAdmission={path:'ignored',sha256:'ignored'};f.savePlan();const c=f.install(),a=f.route(c);if(before){const old=f.install(before),b=f.route(old);assert.deepEqual(a,b);old.verifyAfter();old.verifyFinished();}c.verifyAfter();c.verifyFinished();});
 for(const version of [3,4,7])gate('schema'+version+' stays rejected',()=>{const f=fixture(version);assert.throws(()=>f.route(),e=>e.code==='ONEX_TOOL_POLICY_ENTRY_MISMATCH');});
 refused('missing call-pinned proof','ONEX_TOOL_SCHEMA6_METADATA_ADMISSION_REQUIRED',f=>{delete f.call.schema6MetadataAdmission;f.savePlan();});
 refused('supports6 boolean is not an admission','ONEX_TOOL_SCHEMA6_METADATA_ADMISSION_REQUIRED',f=>{delete f.call.schema6MetadataAdmission;f.call.supports6=true;f.savePlan();});
 refused('self manifest without execution evidence is not admission','ONEX_TOOL_PIN_SHAPE',f=>{delete f.proof.process;f.saveProof();});
 refused('schema6 requires explicit plan program pin','ONEX_TOOL_SCHEMA6_DOTNET_PIN_REQUIRED',f=>{delete f.plan.dotnet;f.savePlan();});
 refused('old authority witness cannot bind new helper','ONEX_TOOL_SCHEMA6_METADATA_AUTHORITY_MISMATCH',f=>{f.proof.sharedAuthority=f.extra('ScopedLoadAuthority.dll','MODEL_ONLY old b91 bytes');f.saveProof();});
 refused('wrong exact policy path even with same bytes','ONEX_TOOL_SCHEMA6_METADATA_POLICY_MISMATCH',f=>{f.proof.policy=f.extra('policy.json',fs.readFileSync(f.policyPath,'utf8'));f.saveProof();});
 refused('wrong exact producer entry','ONEX_TOOL_SCHEMA6_METADATA_ENTRY_MISMATCH',f=>{f.proof.entry=f.extra('OneXFanCoexistence.dll');f.saveProof();});
 refused('wrong exact MTA launcher','ONEX_TOOL_SCHEMA6_METADATA_LAUNCHER_MISMATCH',f=>{f.proof.launcher=f.extra('MTA.dll');f.saveProof();});
 refused('wrong exact MTA helper','ONEX_TOOL_SCHEMA6_METADATA_HELPER_MISMATCH',f=>{f.proof.helper=f.extra('ScopedLoadAuthority.dll',fs.readFileSync(f.mta.helper.path,'utf8'));f.saveProof();});
 refused('wrong exact proof program','ONEX_TOOL_SCHEMA6_METADATA_PROGRAM_MISMATCH',f=>{f.proof.program=f.extra('dotnet.exe');f.saveProof();});
 refused('wrong process program','ONEX_TOOL_SCHEMA6_METADATA_PROGRAM_MISMATCH',f=>{f.proc.program=f.extra('dotnet.exe');f.saveProcess();});
 gate('route executable must match exact plan program',()=>{const f=fixture(),other=f.extra('dotnet.exe');assert.throws(()=>f.route(f.install(),other.path),e=>e.code==='ONEX_TOOL_SCHEMA6_METADATA_PROGRAM_MISMATCH');});
 for(const [label,edit]of [['nonzero code',p=>p.code=250],['signal',p=>p.signal='SIGTERM'],['error',p=>p.error='failed'],['expired',p=>p.expired=true],['not natural',p=>p.naturalExit=false],['budget45000',p=>p.budgetMs=45000],['budget60001',p=>p.budgetMs=60001],['duration over budget',p=>p.durationMs=60001],['shell true',p=>p.shell=true],['missing pid',p=>delete p.ownedPid]])refused('process '+label,'ONEX_TOOL_SCHEMA6_METADATA_PROCESS_NOT_SUCCESS',f=>{edit(f.proc);f.saveProcess();});
 refused('missing recorded cwd','ONEX_TOOL_SCHEMA6_METADATA_CWD_REQUIRED',f=>{delete f.proc.cwd;f.saveProcess();});
 refused('wrong recorded cwd','ONEX_TOOL_SCHEMA6_METADATA_CWD_REQUIRED',f=>{f.proc.cwd=f.dir;f.saveProcess();});
 refused('run mode cannot masquerade as validate','ONEX_TOOL_SCHEMA6_METADATA_ARGV_MISMATCH',f=>{f.proc.args[1]='run';f.saveProcess();});
 refused('extra business argv cannot masquerade as metadata','ONEX_TOOL_SCHEMA6_METADATA_ARGV_MISMATCH',f=>{f.proc.args.push('MODEL_ONLY_BUSINESS');f.saveProcess();});
 refused('wrong argv policy SHA','ONEX_TOOL_SCHEMA6_METADATA_ARGV_MISMATCH',f=>{f.proc.args[3]='0'.repeat(64);f.saveProcess();});
 for(const [label,edit]of [['exit code',r=>r.exitCode=250],['failure',r=>r.failure='no'],['product loaded',r=>r.productLoaded=true],['attach event',r=>r.events.push('attach:Default')],['missing prepare event',r=>r.events=[]]])refused('receipt '+label,'ONEX_TOOL_SCHEMA6_METADATA_RECEIPT_NOT_SUCCESS',f=>{edit(f.receipt);f.saveReceipt();});
 refused('empty loaded graph','ONEX_TOOL_SCHEMA6_METADATA_LOADED_GRAPH_REQUIRED',f=>{f.receipt.actualLoadedManaged=[];f.saveReceipt();});
 refused('missing actual MTA','ONEX_TOOL_SCHEMA6_METADATA_REQUIRED_TOOL_NOT_LOADED',f=>{f.receipt.actualLoadedManaged.shift();f.saveReceipt();});
 refused('missing actual shared helper','ONEX_TOOL_SCHEMA6_METADATA_REQUIRED_TOOL_NOT_LOADED',f=>{f.receipt.actualLoadedManaged.pop();f.saveReceipt();});
 refused('false productLoaded cannot hide producer in graph','ONEX_TOOL_SCHEMA6_METADATA_TARGET_LOADED',f=>{f.receipt.actualLoadedManaged.push({...f.tool.entry,identity:'OneXFanCoexistence, Version=1.0.0.0, Culture=neutral, PublicKeyToken=null'});f.saveReceipt();});
 refused('proof must stay outside policy.metadata','ONEX_TOOL_SCHEMA6_METADATA_PROOF_MUST_BE_EXTERNAL',f=>{f.policy.metadata.push(f.call.schema6MetadataAdmission);f.savePolicy();});
 refused('proof byte tamper','ONEX_TOOL_ARTIFACT_CHANGED',f=>fs.appendFileSync(f.proofPath,' '));
 refused('process byte tamper','ONEX_TOOL_ARTIFACT_CHANGED',f=>fs.appendFileSync(f.processPath,' '));
 refused('receipt byte tamper','ONEX_TOOL_ARTIFACT_CHANGED',f=>fs.appendFileSync(f.receiptPath,' '));
 gate('schema6 refuses Harness producer on its Joined route',()=>{const f=fixture(6,'Harness');assert.throws(()=>f.route(),e=>e.code==='ONEX_TOOL_SCHEMA6_STA_JOINED_ROUTE_REQUIRED');});
 gate('schema5 Joined remains refused by unchanged old MTA guard',()=>{const f=fixture(5,'JoinedFactoryProbe');assert.throws(()=>f.route(),e=>e.code==='ONEX_TOOL_SCHEMA5_MTA_ROUTE_REQUIRED');});
 gate('model source drift after install rejects at original source guard',()=>{const f=fixture(),c=f.install();fs.appendFileSync(path.join(f.base,'MODEL_ONLY.cs'),'// changed\n');assert.throws(()=>f.route(c),e=>e.code==='ONEX_TOOL_SOURCE_CHANGED');});
 gate('new source-set member rejects at original source guard',()=>{const f=fixture(),c=f.install();write(path.join(f.base,'added.mjs'),'// new\n');assert.throws(()=>f.route(c),e=>e.code==='ONEX_TOOL_SOURCE_SET_CHANGED');});
 gate('five-project binding guard retained',()=>{const f=fixture();f.plan.tools.pop();f.savePlan();assert.throws(()=>f.install(),e=>e.code==='ONEX_TOOL_FIVE_PROJECT_BINDINGS_REQUIRED');});
 gate('SDK deps guard retained',()=>{const f=fixture();write(f.tool.deps.path,{targets:{x:{'WinRT.Runtime/2':{runtime:{'WinRT.Runtime.dll':{}}}}}});f.tool.deps=pin(f.tool.deps.path);f.savePlan();assert.throws(()=>f.route(),e=>e.code==='ONEX_TOOL_STATIC_SDK_DEPENDENCY');});
 gate('tracer absent-deps guard retained',()=>{const f=fixture();write(f.mta.depsAbsent,{});assert.throws(()=>f.route(),e=>e.code==='ONEX_TOOL_TRACER_DEPS_MUST_BE_ABSENT');});
 gate('shared copy hash guard retained',()=>{const f=fixture();write(f.mta.helper.path,'MODEL_ONLY wrong helper');f.mta.helper=pin(f.mta.helper.path);f.savePlan();assert.throws(()=>f.route(),e=>e.code==='ONEX_TOOL_SHARED_AUTHORITY_COPY_MISMATCH');});
 gate('runtime framework contract guard retained',()=>{const f=fixture();write(f.mta.runtimeconfig.path,{runtimeOptions:{rollForward:'LatestMajor',frameworks:[]}});f.mta.runtimeconfig=pin(f.mta.runtimeconfig.path);f.savePlan();assert.throws(()=>f.route(),e=>e.code==='ONEX_TOOL_RUNTIME_CONTRACT');});
 gate('verifyFinished still rejects unused policies',()=>{const f=fixture(),c=f.install();assert.throws(()=>c.verifyFinished(),e=>e.code==='ONEX_TOOL_UNUSED_INVOCATION_POLICIES');});
 gate('verifyAfter still rejects actual launch authority failure',()=>{const f=fixture(),c=f.install(),r=f.route(c);write(r.policyReceipt,{failure:'MODEL_ONLY failure'});assert.throws(()=>c.verifyAfter(r.policyReceipt),e=>e.code==='ONEX_TOOL_LAUNCH_AUTHORITY_FAILURE');});
 for(const member of ['proofPath','processPath','receiptPath'])gate('verifyAfter and verifyFinished rehash '+member,()=>{const f=fixture(),c=f.install(),r=f.route(c);write(r.policyReceipt,{failure:null});fs.appendFileSync(f[member],' ');for(const fn of [()=>c.verifyAfter(r.policyReceipt),()=>c.verifyFinished()])assert.throws(fn,e=>e.code==='ONEX_TOOL_SCHEMA6_METADATA_EVIDENCE_CHANGED');});
 gate('verifyFinished rehashes exact program',()=>{const f=fixture(),c=f.install();f.route(c);fs.appendFileSync(f.program.path,' ');assert.throws(()=>c.verifyFinished(),e=>e.code==='ONEX_TOOL_SCHEMA6_METADATA_EVIDENCE_CHANGED');});
 refused('extra custom context is refused','ONEX_TOOL_SCHEMA6_JOINED_DEFAULT_CONTEXT_REQUIRED',f=>{f.policy.contexts.push({id:'Custom',kind:'custom',managed:[],requests:[]});f.savePolicy();});
 refused('non-null default registration type refused','ONEX_TOOL_SCHEMA6_JOINED_DEFAULT_CONTEXT_REQUIRED',f=>{f.policy.contexts[0].registrationType='Harness';f.savePolicy();});
 refused('non-null default registration assembly refused','ONEX_TOOL_SCHEMA6_JOINED_DEFAULT_CONTEXT_REQUIRED',f=>{f.policy.contexts[0].registrationAssembly=f.tool.entry;f.savePolicy();});
 refused('wrong exact Joined identity refused','ONEX_TOOL_SCHEMA6_JOINED_ENTRY_REQUIRED',f=>{f.policy.contexts[0].managed[0].identity='Other, Version=1.0.0.0, Culture=neutral, PublicKeyToken=null';f.savePolicy();});
 refused('non-tool Joined entry role refused','ONEX_TOOL_SCHEMA6_JOINED_ENTRY_REQUIRED',f=>{f.policy.contexts[0].managed[0].role='target';f.savePolicy();});
 // R2: require explicit boolean evidence; keep all original65 gates above intact.
 refused('process missing naturalExit is rejected','ONEX_TOOL_SCHEMA6_METADATA_PROCESS_NOT_SUCCESS',f=>{delete f.proc.naturalExit;f.saveProcess();});
 refused('process string false naturalExit is rejected','ONEX_TOOL_SCHEMA6_METADATA_PROCESS_NOT_SUCCESS',f=>{f.proc.naturalExit='false';f.saveProcess();});
 refused('process missing shell is rejected','ONEX_TOOL_SCHEMA6_METADATA_PROCESS_NOT_SUCCESS',f=>{delete f.proc.shell;f.saveProcess();});
 refused('process string true shell is rejected','ONEX_TOOL_SCHEMA6_METADATA_PROCESS_NOT_SUCCESS',f=>{f.proc.shell='true';f.saveProcess();});
 // Carry forward the independent review's six exact post-graph refusals.
 for(const [id,select,code]of [
  ['POST_PROGRAM',f=>f.program,'ONEX_TOOL_SCHEMA6_METADATA_EVIDENCE_CHANGED'],
  ['POST_CANONICAL_AUTHORITY',f=>f.plan.sharedAuthority,'ONEX_TOOL_SCHEMA6_METADATA_EVIDENCE_CHANGED'],
  ['POST_LAUNCHER_HELPER',f=>f.mta.helper,'ONEX_TOOL_SCHEMA6_METADATA_EVIDENCE_CHANGED'],
  ['POST_LAUNCHER',f=>f.mta.entry,'ONEX_TOOL_ARTIFACT_CHANGED'],
  ['POST_PRODUCER',f=>f.tool.entry,'ONEX_TOOL_ARTIFACT_CHANGED']
 ])gate('review postgraph '+id,()=>{const f=fixture(),c=f.install(),r=f.route(c);write(r.policyReceipt,{failure:null});fs.appendFileSync(select(f).path,' MODEL_ONLY changed');for(const fn of [()=>c.verifyAfter(r.policyReceipt),()=>c.verifyFinished()])assert.throws(fn,e=>e.code===code);});
 gate('review postgraph POST_ADDITIONAL_GRAPH_ROW',()=>{const f=fixture(),p=f.extra('ModelFramework.dll','MODEL_ONLY framework bytes');f.receipt.actualLoadedManaged.push({...p,identity:'System.Model, Version=10.0.0.0, Culture=neutral, PublicKeyToken=null'});f.saveReceipt();const c=f.install(),r=f.route(c);write(r.policyReceipt,{failure:null});fs.appendFileSync(p.path,' MODEL_ONLY changed');for(const fn of [()=>c.verifyAfter(r.policyReceipt),()=>c.verifyFinished()])assert.throws(fn,e=>e.code==='ONEX_TOOL_SCHEMA6_METADATA_EVIDENCE_CHANGED');});
 const result={schemaVersion:1,status:rows.every(r=>r.status==='PASS')?'PASS_MODEL_ONLY_SCHEMA6_ROUTE_SOURCE_GATES':'FAIL_MODEL_ONLY',passed:rows.filter(r=>r.status==='PASS').length,total:rows.length,tests:rows,source:pin(moduleFile),sourceBefore:sourceBefore?pin(sourceBefore):null,MODEL_ONLY:true,actualMetadataProcesses:0,compilerProcesses:0,productProcesses:0,primaryFullChainPassed:false,fixtureMeaning:'All exe/dll bytes and process/receipt JSON are explicitly synthetic model files; never execute them.'};write(path.join(outDir,'FINITE-GATES.json'),result);assert.equal(result.passed,result.total,JSON.stringify(rows.filter(r=>r.status==='FAIL')));return result;
}
if(process.argv[1]&&pathToFileURL(path.resolve(process.argv[1])).href===import.meta.url){const args=process.argv.slice(2);assert.equal(args.length,4);assert.equal(args[0],'--out-dir');assert.equal(args[2],'--source-before');const r=await runSchema6RouteModelTests({outDir:args[1],sourceBefore:args[3]});console.log(JSON.stringify({status:r.status,passed:r.passed,total:r.total,MODEL_ONLY:true,actualMetadataProcesses:0,productProcesses:0}));}
