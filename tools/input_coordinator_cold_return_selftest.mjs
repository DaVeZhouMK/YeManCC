import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';import assert from 'node:assert/strict';import {spawnSync} from 'node:child_process';import {fileURLToPath} from 'node:url';import {performance} from 'node:perf_hooks';
import {parseColdArguments,prepareColdSources} from './fixtures/input_coordinator_cold_return/prepare.mjs';
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..'),options=parseColdArguments(process.argv.slice(2));
const out=options.out,selection=options.selection;assert(!fs.existsSync(out),'Fresh evidence required');
const hash=p=>crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex'),sha=b=>crypto.createHash('sha256').update(b).digest('hex'),id=p=>({path:p,sha256:hash(p),bytes:fs.statSync(p).size});
const read=p=>JSON.parse(fs.readFileSync(p,'utf8').replace(/^\uFEFF/,'')),save=(p,v)=>fs.writeFileSync(p,JSON.stringify(v,null,2)+'\n',{flag:'wx'}),key=p=>path.resolve(p).toLowerCase();
const allVariants=[{name:'Release',flags:['/O2','/DNDEBUG']},{name:'Checked',flags:['/Od','/RTC1']}],variants=allVariants.filter(v=>options.configuration==='Both'||v.name===options.configuration);
const inside=(p,r)=>{const v=path.relative(r,p);return v===''||!v.startsWith('..')&&!path.isAbsolute(v);};
let toolchain,capture,tm,env,cl,link,dump;const tools=[],byTool=new Map();
if(!options.prepareOnly){
 toolchain=fs.realpathSync(options.toolchain);capture=read(path.join(toolchain,'build-environment.json'));tm=read(path.join(toolchain,'toolchain-manifest.json'));env={...capture.environment};
 assert(![repo,toolchain,...tm.roots.map(r=>r.frozen)].some(r=>inside(out,path.resolve(r))),'Private output outside source/toolchain roots required');
 for(const name of Object.keys(env))if(/^(CL|_CL_|LINK|_LINK_)$/i.test(name))delete env[name];
 env.INCLUDE=tm.roots.filter(r=>r.category==='INCLUDE').map(r=>r.frozen).join(';');env.LIB=tm.roots.filter(r=>r.category==='LIB').map(r=>r.frozen).join(';');env.LIBPATH=env.LIB;
 cl=capture.tools['cl.exe'].paths[0];link=capture.tools['link.exe'].paths[0];dump=capture.tools['dumpbin.exe'].paths[0];tools.push(id(path.join(toolchain,'build-environment.json')),id(path.join(toolchain,'toolchain-manifest.json')));
 for(const tool of [cl,link,dump]){const bound=tm.tools.find(t=>key(t.original)===key(tool));assert(bound&&hash(tool)===bound.sha256,'captured compiler/link identity');tools.push(id(tool));}
 for(const row of tm.files)byTool.set(key(row.frozen),row);
}
if(options.sharedRecord){
 const core=path.resolve(repo,'../../../Testing/YMCC'),rel=path.relative(core,options.sharedRecord);assert(rel&&!path.isAbsolute(rel)&&!rel.startsWith('..'+path.sep),'durable record outside core testing root');
 if(selection.startsWith('durable-write-'))assert(inside(options.sharedRecord,out)&&!fs.existsSync(options.sharedRecord),'durable writer requires a fresh owned output record');
 else assert(fs.existsSync(options.sharedRecord)&&fs.lstatSync(options.sharedRecord).isFile()&&!fs.lstatSync(options.sharedRecord).isSymbolicLink(),'durable reader requires explicit existing private record');
}
const started=performance.now(),preparation=prepareColdSources({repo,out,selection,configurations:variants.map(v=>v.name)});
const {snapshot,records,primary,prepared,localLeaves,generated,coldStruct}=preparation;tools.push(...preparation.toolInputs);
const sourceToolPolicy='CURRENT_CANONICAL_SOURCE_BOUND_PREPARATION_V2';
const historicalFreeze=path.resolve(repo,'../../../Build/Validation/VirtualController-Lifecycle-20261019/player-switch-lock12/cold-runtime-tools131/SOURCE-FREEZE131.json');
save(path.join(out,'SOURCE-TOOL-IDENTITY.json'),{sourceToolPolicy,actualCurrentInputs:preparation.toolInputs,historicalFreeze:fs.existsSync(historicalFreeze)?id(historicalFreeze):{path:historicalFreeze,present:false},historicalApprovalUsed:false,reason:'Current canonical preparation is versioned by actual source/tool inputs and fresh closing; old131 source receipts remain historical.'});
const preparationMs=performance.now()-started;
if(options.prepareOnly){
 for(const t of tools)assert.equal(hash(t.path),t.sha256,'preparation tool changed');for(const p of primary){assert.equal(hash(p.original),p.sha256);assert.equal(hash(p.snapshot),p.sha256);}for(const p of generated)assert.equal(hash(p.path),p.sha256);
 save(path.join(out,'PREPARE-ONLY-RESULT.json'),{schemaVersion:1,status:'PREPARED_NOT_COMPILED',selection,configurations:variants.map(v=>v.name),preparationMs,compilerInvocations:0,businessExecutions:0,toolInputs:tools,sourceToolPolicy,fullToolClosure:false,fullProductOrHardwareAccepted:false});
 console.log(JSON.stringify({status:'PREPARED_NOT_COMPILED',out,selection,preparationMs,compilerInvocations:0,businessExecutions:0}));
}else{
 const temp=path.join(out,'temp');fs.mkdirSync(temp);env.TEMP=env.TMP=temp;delete env.YMCC_COLD_SHARED_RECORD;if(options.sharedRecord)env.YMCC_COLD_SHARED_RECORD=options.sharedRecord;
const bySnapshot=new Map(records.map(r=>[key(r.snapshot),r])),boundTools=new Map(),results=[];
const requiredHeaders=prepared.compilerHeaderBindings.map(x=>x.source);
const windowsApp=tm.files.find(f=>/[/\\]windowsapp\.lib$/i.test(f.frozen));assert(windowsApp,'captured WindowsApp library required');
const native=path.join(snapshot,'native'),json=path.join(snapshot,'deps/json'),webview=path.join(snapshot,'deps/webview2/build/native/include');
function closure(file){const dep=read(file);return [dep.Data.Source,...dep.Data.Includes].map(file=>{const fileKey=key(file),g=generated.find(g=>key(g.path)===fileKey),s=bySnapshot.get(fileKey),t=byTool.get(fileKey),bound=g??s??t;assert(bound&&hash(file)===bound.sha256,'unbound/changed actual compiler input '+file);if(t)boundTools.set(fileKey,id(file));return{path:file,sha256:bound.sha256,scope:g?'generated full-TU explicit endpoint adaptation':s?'actual frozen source':'captured toolchain'};});}
try{for(const variant of variants){
 const dir=path.join(out,variant.name);fs.mkdirSync(dir);const obj=path.join(dir,'fixture.obj'),dsu=path.join(dir,'dsu.obj'),exe=path.join(dir,'ColdCoordinatorFixture.exe');
 const common=['/nologo','/std:c++20','/utf-8','/EHsc','/MT','/bigobj','/DUNICODE','/D_UNICODE',...variant.flags];
 const compile=[...common,'/c','/sourceDependencies',path.join(dir,'dependencies.json'),path.join(out,'fixture.cpp'),'/I'+out,'/I'+native,'/I'+json,'/I'+webview,'/Fo'+obj];
 const commands=[];function runTool(executable,args,log){const invokedAt=performance.now(),result=spawnSync(executable,args,{cwd:dir,env,encoding:'utf8',windowsHide:true,timeout:300000,maxBuffer:16*1024*1024});fs.writeFileSync(path.join(dir,log),(result.stdout??'')+(result.stderr??''));commands.push({executable,arguments:args,exit:result.status,error:result.error?.message??null,durationMs:performance.now()-invokedAt,timeoutMs:300000,signal:result.signal??null,timedOut:result.error?.code==='ETIMEDOUT',spawnErrorCode:result.error?.code??null});save(path.join(dir,log+'.command.json'),commands.at(-1));assert.equal(result.status,0,'actual full-TU build failed '+path.join(dir,log));return result;}
 runTool(cl,compile,'build.log');runTool(cl,[...common,'/c','/sourceDependencies',path.join(dir,'dsu-dependencies.json'),path.join(native,'dsu_server.cpp'),'/Fo'+dsu],'dsu-build.log');
 const linked=runTool(link,['/nologo',obj,dsu,'/OUT:'+exe,'/SUBSYSTEM:CONSOLE','/MACHINE:x64','/VERBOSE:LIB',path.join(snapshot,'deps/webview2/build/native/x64/WebView2LoaderStatic.lib'),windowsApp.frozen,...['user32','gdi32','shell32','shlwapi','ole32','oleaut32','uuid','dwmapi','winhttp','advapi32','shcore','version','psapi','PowrProf','ws2_32'].map(s=>s+'.lib')],'link.log');
 const actualInputs=[...closure(path.join(dir,'dependencies.json')),...closure(path.join(dir,'dsu-dependencies.json'))];
 for(const relative of requiredHeaders){const row=records.find(r=>r.relative===relative);assert(row&&actualInputs.some(i=>key(i.path)===key(relative==='native/msi_native_foreground_isolation_runtime.h'?preparation.plan.ownerAdapter.generated:row.snapshot)),'required real header absent from compiler closure '+relative);}
 const libraries=[...new Set([...((linked.stdout??'')+(linked.stderr??'')).matchAll(/([A-Za-z]:[\\/][^\r\n]*?\.lib)(?=[:\s(]|$)/gi)].map(m=>m[1].trim()))].map(file=>{const s=bySnapshot.get(key(file)),t=byTool.get(key(file));assert((s||t)&&hash(file)===(s??t).sha256,'unbound actual link library '+file);if(t)boundTools.set(key(file),id(file));return id(file);});assert(libraries.length>=7,'linker archive observations required');
 const imports=runTool(dump,['/imports',exe],'imports.log'),dlls=[...new Set([...imports.stdout.matchAll(/^\s+([A-Za-z0-9_.-]+\.dll)\s*$/gm)].map(m=>m[1].toLowerCase()))];
 save(path.join(dir,'COMMANDS.json'),commands);const binary=id(exe);save(path.join(dir,'COMPILE-CLOSURE.json'),{inputs:actualInputs,libraries,binary,compiler:id(cl),staticImportedDlls:dlls,runtimeModuleLoadTraced:false});
 const privateDir=path.join(dir,'private-files');fs.mkdirSync(privateDir);const resultPath=path.join(dir,'RESULT.json');const ran=spawnSync(exe,[privateDir,resultPath,...(selection==='original-cold-environment'?[]:[selection])],{cwd:dir,env:{...env,YEMAN_POWER_CONTROL_DIR:privateDir},windowsHide:true,encoding:'utf8',timeout:90000,maxBuffer:16*1024*1024});fs.writeFileSync(path.join(dir,'run.log'),(ran.stdout??'')+(ran.stderr??''));assert(fs.existsSync(resultPath),'actual cold fixture did not produce terminal RESULT; exit '+ran.status);const r=read(resultPath);assert(Array.isArray(r.cases)&&r.caseCount===r.cases.length&&new Set(r.cases.map(c=>c.name)).size===r.cases.length,'exact actual cold case identities required');if(selection!=='original-cold-environment')assert.equal(r.selection,selection,'selected cold report required');results.push({variant:variant.name,exit:ran.status,status:r.status,cases:r.caseCount,caseNames:r.cases.map(c=>c.name),selection,checks:r.checks,failures:r.failures?.length??null,binary,result:id(resultPath),compilerInputs:actualInputs.length,linkLibraries:libraries.length});save(path.join(out,'PROGRESS.json'),results);assert.equal(ran.status,0,'actual cold consumer assertions failed '+resultPath);assert.equal(r.status,'PASS');assert.equal(r.forbiddenIo,0);
 }
 const used=new Set(results.flatMap(v=>read(path.join(path.dirname(v.result.path),'COMPILE-CLOSURE.json')).inputs.map(i=>key(i.path))));const actualSource=records.filter(r=>used.has(key(r.snapshot))||primary.includes(r)||r.relative.startsWith('tools/'));
 const changed=actualSource.filter(r=>!fs.existsSync(r.original)||hash(r.original)!==r.sha256||hash(r.snapshot)!==r.sha256).map(r=>r.relative);for(const f of generated)assert.equal(hash(f.path),f.sha256);for(const t of boundTools.values())assert.equal(hash(t.path),t.sha256);
 save(path.join(out,'SOURCE-MANIFEST.json'),{records:actualSource,expanded:read(path.join(out,'EXPANSION.json')),core:prepared.core,structCore:prepared.structCore,requiredCompilerHeaders:prepared.compilerHeaderBindings,baselineSubstitutions:prepared.substitutions,coldOnlyLeaves:localLeaves,generated,tools,sourceToolPolicy,selection,preparationMs,boundToolchainInputs:[...boundTools.values()],changed,wholeProductMainExecuted:false});assert.deepEqual(changed,[],'actual source changed during independent cold run');
 save(path.join(out,'RESULT.json'),{task:131,pass:true,status:selection==='original-cold-environment'?'PASS_ACTUAL_FULL_TU_COLD_CONSUMERS_ONLY':'PASS_SELECTED_FULL_TU_COLD_SCOPE_ONLY',selection,sourceToolPolicy,preparationMs,results,mainSha256:primary.find(r=>r.relative==='native/main.cpp').sha256,sourceManifest:id(path.join(out,'SOURCE-MANIFEST.json')),coldEnvironmentSha256:sha(coldStruct),sourceChanged:changed,oldAllSuiteRun:false,fullProductOrHardwareAccepted:false});console.log(JSON.stringify({pass:true,out,results}));
}catch(error){save(path.join(out,'FAILED.json'),{task:131,pass:false,error:error.message,stack:error.stack,completed:results,localLeaves,primarySources:primary,tools});throw error;}
}
