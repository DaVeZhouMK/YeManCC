import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {extract} from './fixtures/legion_motion_consumer/extract_main.mjs';
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const opts={},args=process.argv.slice(2),allowed=new Set(['--out','--toolchain','--main']);
for(let i=0;i<args.length;i+=2){assert(allowed.has(args[i])&&args[i+1]&&!opts[args[i]],'Unknown/duplicate/incomplete argument');assert(path.isAbsolute(args[i+1]),'Use explicit absolute paths');opts[args[i]]=path.resolve(args[i+1]);}
assert(opts['--out']&&opts['--toolchain'],'Use --out ABS_FRESH_EVIDENCE_DIR --toolchain ABS_CAPTURED_TOOLCHAIN [--main ABS_MAIN]');
const out=opts['--out'],toolchain=opts['--toolchain'],main=opts['--main']??path.join(repo,'native/main.cpp');
const inside=(p,r)=>{const rel=path.relative(r,p);return rel===''||(!rel.startsWith('..'+path.sep)&&rel!=='..'&&!path.isAbsolute(rel));};
assert(!fs.existsSync(out)&&out!==path.parse(out).root&&!inside(out,repo)&&!inside(out,toolchain),'Output must be fresh and outside product/toolchain');
assert(fs.existsSync(path.join(toolchain,'build-environment.json'))&&fs.existsSync(path.join(toolchain,'toolchain-manifest.json')),'Explicit captured toolchain required');
fs.mkdirSync(out,{recursive:true});
const sha=b=>crypto.createHash('sha256').update(b).digest('hex'),hash=p=>sha(fs.readFileSync(p));
const read=p=>JSON.parse(fs.readFileSync(p,'utf8').replace(/^\uFEFF/,''));
const save=(p,v)=>fs.writeFileSync(p,JSON.stringify(v,null,2)+'\n');
const sourceInputs=[],mutable=[];
function freeze(p,label,current=true){const bytes=fs.readFileSync(p),snapshot=path.join(out,'inputs',label);fs.mkdirSync(path.dirname(snapshot),{recursive:true});fs.writeFileSync(snapshot,bytes);const entry={path:p,snapshot,sha256:sha(bytes),bytes:bytes.length};sourceInputs.push(entry);if(current)mutable.push(entry);return bytes;}
function select(text){
 const pieces=[extract(text,'hidHideRun'),extract(text,'HidHideCliCallHandles','struct')];
 for(const name of ['g_hidHideMx','g_hidHidePendingChildProcess','g_hidHidePendingReadPipe','g_inputHostSwitchRecoveryStartDeadline']){
  const re=new RegExp('^static[^\\r\\n;]*\\b'+name+'\\s*(?:=\\s*[^;\\r\\n]+)?;\\r?$','gm'),found=[...text.matchAll(re)];assert.equal(found.length,1,'One actual declaration '+name);
  const code=found[0][0].replace(/\r$/,'');pieces.push({symbol:name,kind:'actual declaration',code,sha256:sha(code)});
 }
 return {pieces,cli:extract(text,'hidHideCliPath')};
}
const mainBytes=freeze(main,'main.source.cpp',false),selected=select(mainBytes.toString('utf8'));
const fixturePath=path.join(repo,'tools/fixtures/hidhide_cli_cross_native/fixture.cpp');
const fixture=freeze(fixturePath,'fixture.cpp').toString('utf8');
const json=freeze(path.join(repo,'deps/json/json.hpp'),'json.hpp');
const efficiency=freeze(path.join(repo,'native/process_efficiency.h'),'process_efficiency.h');
for(const file of ['tools/hidhide_cli_cross_native_selftest.mjs','tools/fixtures/hidhide_cli_cross_native/README.md','tools/fixtures/legion_motion_consumer/extract_main.mjs','tools/lib/hc_controller_motion_fixture.mjs'])freeze(path.join(repo,file),file);
const globalOrder=['g_inputHostSwitchRecoveryStartDeadline','g_hidHideMx','g_hidHidePendingChildProcess','g_hidHidePendingReadPipe','HidHideCliCallHandles','hidHideRun'];
const body=globalOrder.map(n=>selected.pieces.find(x=>x.symbol===n).code).join('\n\n')+'\n';
save(out+'/SOURCE-MANIFEST.json',{inputs:sourceInputs,selected,wholeMainSha256:sha(mainBytes),wholeMainApproval:false,transformations:[
 {leaf:'hidHideCliPath',actual:selected.cli,substitution:'ownImage() of compiled private fixture; --fake-cli role supplied as arguments'},
 {leaf:'TerminateProcess',substitution:'macro to fixtureTerminate for extracted consumer only; exact owned fake image, explicit controlled denial; all other Win32 functions real'},
 {leaf:'process launcher',substitution:'none; actual frozen process_efficiency.h, real CreateProcessW'},
 {harness:'observer private cleanup job',behavior:'KILL_ON_JOB_CLOSE owned by observer, no parent/native kill boundary; keeps children alive when caller dies; prevents orphan leak if observer fails'}
]});
const capture=read(path.join(toolchain,'build-environment.json')),tm=read(path.join(toolchain,'toolchain-manifest.json')),env={...capture.environment};
for(const k of Object.keys(env))if(/^(CL|_CL_|LINK|_LINK_)$/i.test(k))delete env[k];
env.INCLUDE=tm.roots.filter(x=>x.category==='INCLUDE').map(x=>x.frozen).join(';');env.LIB=tm.roots.filter(x=>x.category==='LIB').map(x=>x.frozen).join(';');env.LIBPATH=env.LIB;
const cl=capture.tools['cl.exe'].paths[0],dump=capture.tools['dumpbin.exe'].paths[0],link=capture.tools['link.exe'].paths[0];
const tools=[cl,link,dump].map(p=>{const bound=tm.tools.find(t=>path.resolve(t.original).toLowerCase()===path.resolve(p).toLowerCase());assert(bound&&hash(p)===bound.sha256,'Captured compiler/link/import tool identity '+p);return{path:p,sha256:bound.sha256};});
const byTool=new Map(tm.files.map(x=>[path.resolve(x.frozen).toLowerCase(),x]));
const variants=[{name:'Release',flags:['/O2'],cases:['cross-native','same-native']},{name:'Checked',flags:['/Od','/RTC1'],cases:['cross-native','same-native']},
 {name:'negative-bypass-pending',flags:['/O2'],cases:['cross-native'],negative:true}];
const results=[];
try {
 for(const v of variants){
  const dir=path.join(out,v.name);fs.mkdirSync(dir);const exe=path.join(dir,'private-cli117.exe');
  let consumer=body;
  if(v.negative){const anchor='if (g_hidHidePendingChildProcess) {';assert.equal(consumer.split(anchor).length-1,1);consumer='static volatile bool fixtureBypassPendingGate = true;\n'+consumer.replace(anchor,'if (!fixtureBypassPendingGate && g_hidHidePendingChildProcess) {');}
  const generated=[];
  for(const [name,bytes] of [['fixture.cpp',fixture],['actual_consumer.inc',consumer],['json.hpp',json],['process_efficiency.h',efficiency]]){const p=path.join(dir,name);fs.writeFileSync(p,bytes);generated.push({path:p,sha256:hash(p)});}
  save(dir+'/GENERATED-MANIFEST.json',{files:generated,semanticMutation:v.negative?{anchor:'if (g_hidHidePendingChildProcess) {',replacement:'if (!fixtureBypassPendingGate && g_hidHidePendingChildProcess) {',declaration:'static volatile bool fixtureBypassPendingGate = true;'}:null});
  const command=['/nologo','/std:c++17','/EHsc','/W4','/WX','/utf-8','/MT',...v.flags,'/sourceDependencies','dependencies.json','fixture.cpp','/Fe:private-cli117.exe','/Fo:fixture.obj','/link','/VERBOSE:LIB'];
  const built=spawnSync(cl,command,{cwd:dir,env,encoding:'utf8',windowsHide:true,timeout:60000,maxBuffer:16*1024*1024});
  fs.writeFileSync(dir+'/build.log',(built.stdout??'')+(built.stderr??''));save(dir+'/COMMAND.json',{executable:cl,arguments:command,exit:built.status,error:built.error?.message});assert.equal(built.status,0,'Compile failed '+dir+'/build.log');
  const generatedMap=new Map(generated.map(x=>[path.resolve(x.path).toLowerCase(),x]));
  const dependency=read(dir+'/dependencies.json');
  const actualInputs=[dependency.Data.Source,...dependency.Data.Includes].map(p=>{const key=path.resolve(p).toLowerCase(),bound=generatedMap.get(key)??byTool.get(key);assert(bound&&hash(p)===bound.sha256,'Unbound/changed compiler input '+p);return{path:p,sha256:bound.sha256,scope:generatedMap.has(key)?'frozen source':'frozen toolchain'};});
  const libraryPaths=[...new Set([...((built.stdout??'')+'\n'+(built.stderr??'')).matchAll(/([A-Za-z]:[\\/][^\r\n]*?\.lib)(?=[:\s(]|$)/gi)].map(m=>m[1].trim()))];
  assert(libraryPaths.length>0,'Actual linker library paths required');
  const libraries=libraryPaths.map(p=>{const bound=byTool.get(path.resolve(p).toLowerCase());assert(bound&&hash(p)===bound.sha256,'Unbound/changed actual linker library '+p);return{path:p,sha256:bound.sha256};});
  const imports=spawnSync(dump,['/imports',exe],{cwd:dir,env,encoding:'utf8',windowsHide:true,timeout:10000});fs.writeFileSync(dir+'/imports.log',(imports.stdout??'')+(imports.stderr??''));assert.equal(imports.status,0,'dumpbin imports');
  const dlls=[...new Set([...imports.stdout.matchAll(/^\s+([A-Za-z0-9_.-]+\.dll)\s*$/gm)].map(m=>m[1].toLowerCase()))];assert.deepEqual(dlls,['kernel32.dll']);
  const closure={inputs:actualInputs,libraries,tools,binarySha256:hash(exe),staticImportedDlls:dlls,unbound:[],dynamicModuleTrace:false};save(dir+'/COMPILE-CLOSURE.json',closure);
  const cases=[];
  for(const name of v.cases){
   const run=path.join(dir,name);fs.mkdirSync(run);
   const ran=spawnSync(exe,['--observe',name,run],{cwd:dir,env,encoding:'utf8',windowsHide:true,timeout:25000,maxBuffer:4*1024*1024});
   fs.writeFileSync(run+'/stdout.log',ran.stdout??'');fs.writeFileSync(run+'/stderr.log',ran.stderr??'');
   assert(ran.status===0||ran.status===1,'Crash/timeout is not semantic proof '+run+' '+ran.error?.message);
   const row=read(run+'/observer-result.json');assert.equal(row.pass,ran.status===0);assert(!row.infrastructureException,'Unexpected infrastructure error '+row.infrastructureException);
   assert(row.noOwnedProcessesRemain,'Private children leaked');
   if(v.negative){assert(!row.pass&&row.failures.includes('same-native second call blocked by pending gate')&&row.failures.includes('same-native second call created no fake child'),'Pending bypass must be caught by the original observer assertions');}
   else assert(row.pass,'Actual baseline assertion failures '+JSON.stringify(row.failures));
   cases.push({name,pass:row.pass,checks:row.checks,failures:row.failures,observations:row.observations.length,result:run+'/observer-result.json',resultSha256:hash(run+'/observer-result.json'),noOwnedProcessesRemain:row.noOwnedProcessesRemain});
  }
  const row={variant:v.name,negative:!!v.negative,expectedOutcomeObserved:true,cases,checks:cases.reduce((s,c)=>s+c.checks,0),compilerInputs:actualInputs.length,libraries:libraries.length,binary:exe,binarySha256:hash(exe),closure:dir+'/COMPILE-CLOSURE.json'};
  save(dir+'/RESULT.json',row);results.push(row);console.log(JSON.stringify({variant:v.name,checks:row.checks,compilerInputs:row.compilerInputs,libraries:row.libraries,caseFailures:cases.map(c=>({name:c.name,failures:c.failures}))}));
 }
 const now=select(fs.readFileSync(main,'utf8')),current={wholeMainSha256:hash(main),wholeMainChanged:hash(main)!==sha(mainBytes),selectedCurrent:now.pieces.every(p=>selected.pieces.find(x=>x.symbol===p.symbol)?.sha256===p.sha256),cliPathCurrent:now.cli.sha256===selected.cli.sha256,toolsCurrent:mutable.every(x=>hash(x.path)===x.sha256)};
 assert(current.selectedCurrent&&current.cliPathCurrent&&current.toolsCurrent,'Selected source/tool changed during test');
 save(out+'/SOURCE-CURRENT.json',current);
 save(out+'/RESULT.json',{task:117,status:'PASS_PRIVATE_REAL_PROCESS_EVIDENCE_CROSS_NATIVE_GAP_CONFIRMED',pass:true,results,productionConsumerSha256:selected.pieces.find(x=>x.symbol==='hidHideRun').sha256,sourceInputs,current,
  conclusion:'A timed-out exact CLI handle is retained and blocks a second dispatch inside one native; after that native ExitProcess, the old real private CLI remains Alive and a naturally created second native with empty pending state dispatches another CLI. Both children write the same private JSON world. Same-native exact old-child exit permits next original-entry dispatch.',
  controlledLeaves:['CLI executable path maps to own private --fake-cli role','one explicit termination refusal on exact owned private child per baseline scenario'],
  actualOperations:['production hidHideRun/mutex/pending/RAII/deadline','production process_efficiency.h/CreateProcessW','pipes/Peek/Read/Wait(0)/process identities','old native ExitProcess','new independent native process','private same-world JSON writes'],
  actualHidHideCliRuns:0,deviceApiCalls:0,productHostOrSteamRuns:0,productionChanges:0,staticImportScopeOnly:true,
  limitations:['Controlled termination refusal is not a real Windows refusal or actual HidHideCLI reproduction','Fake world is not driver state; no product/device integration proof','Observer owns a cleanup-only kill-on-close job through both native lifetimes; no production mitigation assessed','No cold recovery implementation or whole-main acceptance'],toolchain:{path:toolchain,manifestSha256:hash(path.join(toolchain,'toolchain-manifest.json')),tools},wholeGoalComplete:false});
} catch(e){save(out+'/FAILED.json',{task:117,pass:false,error:e.stack,results,productOrDeviceActions:0});throw e;}
