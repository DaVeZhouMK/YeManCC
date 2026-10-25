import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {extract} from './fixtures/legion_motion_consumer/extract_main.mjs';
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..'),args=process.argv.slice(2);
assert(args.length===4&&args[0]==='--out'&&args[2]==='--toolchain'&&path.isAbsolute(args[1])&&path.isAbsolute(args[3]),'Use --out ABS_FRESH_PRIVATE_DIRECTORY --toolchain ABS_CAPTURED_TOOLCHAIN_DIRECTORY');
const out=path.resolve(args[1]),toolchain=fs.realpathSync(args[3]);
const hash=p=>crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex'),digest=v=>crypto.createHash('sha256').update(v).digest('hex');
const read=p=>JSON.parse(fs.readFileSync(p,'utf8').replace(/^\uFEFF/,'')),save=(p,v)=>fs.writeFileSync(p,JSON.stringify(v,null,2)+'\n');
const id=p=>({path:p,sha256:hash(p),bytes:fs.statSync(p).size});
const capturePath=toolchain+'/build-environment.json',manifestPath=toolchain+'/toolchain-manifest.json',capture=read(capturePath),tm=read(manifestPath);
const inside=(p,r)=>{const rel=path.relative(r,p);return rel===''||(!rel.startsWith('..'+path.sep)&&rel!=='..'&&!path.isAbsolute(rel));};
let existing=path.dirname(out);while(!fs.existsSync(existing))existing=path.dirname(existing);const resolvedOut=path.resolve(fs.realpathSync(existing),path.relative(existing,out));
const protectedRoots=[fs.realpathSync(repo),toolchain,...tm.roots.map(r=>fs.realpathSync(r.frozen)),...tm.tools.filter(t=>['cl.exe','link.exe','dumpbin.exe'].includes(t.name)).map(t=>path.dirname(fs.realpathSync(t.original)))];
assert(!fs.existsSync(out)&&out!==path.parse(out).root&&!protectedRoots.some(r=>inside(resolvedOut,r)),'Output must be fresh, private and outside source/toolchain roots');
const env={...capture.environment};for(const key of Object.keys(env))if(/^(CL|_CL_|LINK|_LINK_)$/i.test(key))delete env[key];
env.INCLUDE=tm.roots.filter(r=>r.category==='INCLUDE').map(r=>r.frozen).join(';');env.LIB=tm.roots.filter(r=>r.category==='LIB').map(r=>r.frozen).join(';');env.LIBPATH=env.LIB;
const cl=capture.tools['cl.exe'].paths[0],dump=capture.tools['dumpbin.exe'].paths[0],tools=[id(capturePath),id(manifestPath)];
for(const tool of[cl,dump,capture.tools['link.exe'].paths[0]]){const binding=tm.tools.find(t=>path.resolve(t.original).toLowerCase()===path.resolve(tool).toLowerCase());assert(binding&&hash(tool)===binding.sha256,'captured tool identity');tools.push(id(tool));}
const byTool=new Map(tm.files.map(f=>[path.resolve(f.frozen).toLowerCase(),f]));
const sourceNames=['native/main.cpp','native/hidhide_cli_writer_runtime.h','native/hidhide_cli_writer_record.h','native/hidhide_cli_writer_store.h','native/hidhide_cli_job_guard.h','native/virtual_target_return_runtime.h','native/virtual_target_return_session.h','native/virtual_target_return_store.h','native/virtual_target_return_record.h','native/virtual_target_return_process.h','native/virtual_target_return_physical_path.h','native/input_host_transaction_mutex.h','native/virtual_start_protection.h','native/ps_endpoint_ownership.h','deps/json/json.hpp','tools/fixtures/legion_motion_consumer/extract_main.mjs','tools/virtual_physical_return_runtime_selftest.mjs','tools/fixtures/virtual_physical_return_runtime/fixture.cpp','tools/fixtures/virtual_physical_return_runtime/README.md'];
const sources=sourceNames.map(relative=>({relative,...id(path.join(repo,relative))}));
fs.mkdirSync(out,{recursive:true});fs.mkdirSync(path.join(out,'sources'));env.TEMP=path.join(out,'temp');env.TMP=env.TEMP;fs.mkdirSync(env.TEMP);
for(const s of sources){s.snapshot=path.join(out,'sources',path.basename(s.path));fs.copyFileSync(s.path,s.snapshot);}
const main=fs.readFileSync(path.join(out,'sources/main.cpp'),'utf8'),runtimeRaw=fs.readFileSync(path.join(out,'sources/virtual_target_return_runtime.h'),'utf8');
const writerRaw=fs.readFileSync(path.join(out,'sources/hidhide_cli_writer_runtime.h'),'utf8');
const spans=[];
function bindSpan(symbol,code){const start=main.indexOf(code);assert(start>=0&&main.indexOf(code,start+1)===-1,'unique actual source span '+symbol);const offset=Buffer.byteLength(main.slice(0,start));assert.equal(digest(fs.readFileSync(path.join(out,'sources/main.cpp')).subarray(offset,offset+Buffer.byteLength(code))),digest(code));spans.push({symbol,startByte:offset,bytes:Buffer.byteLength(code),sha256:digest(code),verbatim:true});return code;}
const types=['HidHidePhysicalIdentity','HidHideVisibilitySnapshot','HidHideVisibilityJournal'].map(name=>bindSpan(name,extract(main,name,'struct').code)).join('\n');
const first=main.indexOf('static std::string W2U(const wchar_t*'),last=main.indexOf('static std::wstring U2W(',first);assert(first>=0&&last>first,'actual W2U overload span');
const paths=bindSpan('W2U-overloads',main.slice(first,last))+'\n'+['ascii_lower','trim_ascii','U2W','hidHideContainsPath'].map(name=>bindSpan(name,extract(main,name).code)).join('\n');
const refresh=bindSpan('hidHideRefreshPhysicalHide',extract(main,'hidHideRefreshPhysicalHide').code);
assert(refresh.includes('inputHostTargetReturnBeforeHideLocked'),'Finalized Refresh must contain actual physical reservation hook');
const reaper=bindSpan('hidHideCliReapPendingLocked',extract(main,'hidHideCliReapPendingLocked').code);
const consumers=['inputHostTargetReturnBeforeHideLocked','inputHostTargetReturnAfterPhysicalRestoreLocked'].map(symbol=>({symbol,code:extract(runtimeRaw,symbol).code}));
let adapted=runtimeRaw;const substitutions=[];
for(const[symbol,body]of[['inputHostTargetReturnRecordPath','return fixturePath();'],['inputHostTargetReturnMachineBinding','return "fixture-machine";'],['inputHostTargetReturnCaptureOwner','return fixtureCapture(process);'],['inputHostTargetReturnVerifyPhysicalPath','return fixtureProof(root,target);'],['inputHostTargetReturnProbePhysicalWriter','return fixtureProbeOwner(owner);']]){
 const original=extract(runtimeRaw,symbol).code,brace=original.indexOf('{'),replacement=original.slice(0,brace+1)+'\n'+body+'\n}';assert.equal(adapted.split(original).length,2,'one exact environment leaf');adapted=adapted.replace(original,()=>replacement);substitutions.push({symbol,beforeSha256:digest(original),afterSha256:digest(replacement),scope:'Environment/CM leaf only'});
}
for(const c of consumers)assert.equal(extract(adapted,c.symbol).code,c.code,'actual physical consumers must remain unchanged');

// Adapt only the five explicit writer environment/OS leaves; keep all data,
// open/clearSettled/settleOwned/quiescent and the main reaper verbatim.
let writerAdapted=writerRaw;const writerSubstitutions=[];
for(const[symbol,body]of [['hidHideCliWriterRecordPath','return fixtureCliWriterPath;'],['hidHideCliWriterMachineBinding','return "fixture-cli-machine";'],['hidHideCliWriterCapture','return fixtureCapture(process);'],['hidHideCliWriterProbe','return fixtureCliProbe(identity);'],['hidHideCliWriterProbeJob','return fixtureCliProbeJob(name);']]){
 const original=extract(writerRaw,symbol).code,brace=original.indexOf('{'),replacement=original.slice(0,brace+1)+'\n'+body+'\n}';assert.equal(writerAdapted.split(original).length,2,'one exact CLI environment leaf');writerAdapted=writerAdapted.replace(original,()=>replacement);writerSubstitutions.push({symbol,beforeSha256:digest(original),afterSha256:digest(replacement),scope:'Explicit private writer file/machine and controlled own/native/old-child/job observation only'});
}
const writerAlgorithms=['open','clearSettled','settleOwned','quiescent'].map(symbol=>{const code=extract(writerRaw,symbol).code;assert.equal(extract(writerAdapted,symbol).code,code,'real writer algorithm must remain verbatim');return{symbol,sha256:digest(code),unchanged:true};});
assert.equal(extract(adapted,'physicalWritersQuiescent').code,extract(runtimeRaw,'physicalWritersQuiescent').code,'physical writer gate is not substituted');

save(path.join(out,'SOURCES.json'),{sources,tools,spans,substitutions,writerSubstitutions,writerAlgorithms,reaperSha256:digest(reaper),consumers:consumers.map(c=>({symbol:c.symbol,sha256:digest(c.code)})),wholeMainCompiled:false});
const variants=[{name:'Release',flags:['/O2']},{name:'Checked',flags:['/Od','/RTC1']},
 {name:'negative-skip-before',symbol:'inputHostTargetReturnBeforeHideLocked',prefix:'return true;',required:'Refresh/reserve-before-driver'},
 {name:'negative-ignore-root',before:'if(!proof.proven())continue;',after:'(void)proof; /* explicit semantic negative: proof ignored */',required:'Before/root-unknown-zero-hide'},
 {name:'negative-consume-without-readback',before:'if(!readback||!after.valid||hidHideContainsPath(after.hidden,path.instanceId))',after:'if(ownedTargets.empty() && (!readback||!after.valid||hidHideContainsPath(after.hidden,path.instanceId)))',required:'After/unknown-or-still-hidden-0'}];
const results=[],boundInputs=new Map();
try{for(const v of variants){
 const dir=path.join(out,v.name);fs.mkdirSync(dir);fs.mkdirSync(path.join(dir,'private-files'));
 const generated=[];for(const s of sources.filter(s=>!s.relative.endsWith('.mjs')&&!s.relative.endsWith('.md')&&s.relative!=='native/main.cpp')){const dest=path.join(dir,path.basename(s.path));fs.copyFileSync(s.snapshot,dest);generated.push({...id(dest),original:s.relative,originalSha256:s.sha256});}
 let runtime=adapted;if(v.prefix){const original=extract(runtime,v.symbol).code,brace=original.indexOf('{'),changed=original.slice(0,brace+1)+'\n'+v.prefix+' // explicit compiled semantic negative\n'+original.slice(brace+1);assert.equal(runtime.split(original).length,2);runtime=runtime.replace(original,()=>changed);}
 if(v.before){assert.equal(runtime.split(v.before).length,2,'one semantic fault anchor');runtime=runtime.replace(v.before,()=>v.after);}
 for(const[name,text]of Object.entries({'types.inc':types,'paths.inc':paths,'refresh.inc':refresh,'runtime.fixture.h':runtime,'writer-runtime.fixture.h':writerAdapted,'reaper.inc':reaper})){const file=path.join(dir,name);fs.writeFileSync(file,text+'\n');generated.push(id(file));}
 save(path.join(dir,'GENERATED.json'),{inputs:generated,semanticFault:v.required?v:null,actualRefreshSha256:digest(refresh),actualReaperSha256:digest(reaper),writerRawSha256:digest(writerRaw),writerAdaptedSha256:digest(writerAdapted),writerAlgorithmBindings:writerAlgorithms,runtimeRawSha256:digest(runtimeRaw),runtimeAdaptedSha256:digest(runtime),onlyEnvironmentAdaptationsAndSelectedFault:true});
 const exe=path.join(dir,'PhysicalReturnRuntimeFixture.exe'),command=['/nologo','/std:c++20','/utf-8','/EHsc','/MT','/W4','/WX','/wd4505','/DUNICODE','/D_UNICODE',...(v.flags??['/O2']),'/sourceDependencies',path.join(dir,'dependencies.json'),path.join(dir,'fixture.cpp'),'/I'+dir,'/Fo'+path.join(dir,'fixture.obj'),'/Fe'+exe,'/link','/VERBOSE:LIB'];
 const built=spawnSync(cl,command,{cwd:dir,env,encoding:'utf8',windowsHide:true,timeout:60000,maxBuffer:16*1024*1024});const buildLog=(built.stdout??'')+(built.stderr??'');fs.writeFileSync(path.join(dir,'build.log'),buildLog);save(path.join(dir,'COMMAND.json'),{executable:cl,arguments:command,exit:built.status,error:built.error?.message??null});assert.equal(built.status,0,'actual compilation failed: '+path.join(dir,'build.log'));
 const dependency=read(path.join(dir,'dependencies.json')),byGenerated=new Map(generated.map(g=>[path.resolve(g.path).toLowerCase(),g]));
 const actualInputs=[dependency.Data.Source,...dependency.Data.Includes].map(file=>{const key=path.resolve(file).toLowerCase(),binding=byGenerated.get(key)??byTool.get(key);assert(binding&&hash(file)===binding.sha256,'unbound or changed compiler input '+file);if(byTool.has(key))boundInputs.set(key,id(file));return{path:file,sha256:binding.sha256,scope:byGenerated.has(key)?'frozen actual source/generated consumer':'captured toolchain'};});
 for(const name of['runtime.fixture.h','writer-runtime.fixture.h','reaper.inc','refresh.inc','virtual_target_return_session.h','virtual_target_return_store.h','input_host_transaction_mutex.h','hidhide_cli_writer_store.h','hidhide_cli_writer_record.h','hidhide_cli_job_guard.h'])assert(actualInputs.some(r=>r.path.endsWith(name)),'required actual compiler input missing '+name);
 const libPaths=[...new Set([...buildLog.matchAll(/([A-Za-z]:[\\/][^\r\n]*?\.lib)(?=[:\s(]|$)/gi)].map(m=>m[1].trim()))];assert(libPaths.length>=3,'link library input observations');const libraries=libPaths.map(file=>{const binding=byTool.get(path.resolve(file).toLowerCase());assert(binding&&hash(file)===binding.sha256,'unbound or changed actual link input '+file);boundInputs.set(path.resolve(file).toLowerCase(),id(file));return id(file);});
 const imports=spawnSync(dump,['/imports',exe],{cwd:dir,env,encoding:'utf8',windowsHide:true,timeout:10000});fs.writeFileSync(path.join(dir,'imports.log'),(imports.stdout??'')+(imports.stderr??''));assert.equal(imports.status,0);const dlls=[...new Set([...imports.stdout.matchAll(/^\s+([A-Za-z0-9_.-]+\.dll)\s*$/gm)].map(m=>m[1].toLowerCase()))];assert.deepEqual(dlls,['kernel32.dll'],'fixture must not import CM, SetupAPI, controller SDK or service APIs');
 const binary=id(exe);save(path.join(dir,'COMPILE-CLOSURE.json'),{compiler:id(cl),binary,inputs:actualInputs,libraries,staticImportedDlls:dlls,runtimeModuleLoadTraced:false});
 const ran=spawnSync(exe,[path.join(dir,'private-files'),path.join(dir,'RESULT.json')],{cwd:dir,env,encoding:'utf8',windowsHide:true,timeout:30000,maxBuffer:8*1024*1024});fs.writeFileSync(path.join(dir,'run.log'),(ran.stdout??'')+(ran.stderr??''));assert.equal(hash(exe),binary.sha256);assert(fs.existsSync(path.join(dir,'RESULT.json')),'fixture terminal report missing');const r=read(path.join(dir,'RESULT.json'));
 if(v.required){assert.equal(ran.status,1,'compiled semantic fault must fail assertions without crash');assert(r.failures.some(f=>f.case===v.required),'semantic fault must hit intended original assertions');}else{assert.equal(ran.status,0,'actual fixture assertions failed: '+path.join(dir,'RESULT.json'));assert(r.pass);}
 const receipt={variant:v.name,positive:!v.required,pass:v.required?ran.status===1:r.pass,cases:r.caseCount,checks:r.assertions,failures:r.failures.length,requiredCase:v.required??null,compilerInputs:actualInputs.length,linkerLibraries:libraries.length,binary,result:id(path.join(dir,'RESULT.json'))};results.push(receipt);console.log(JSON.stringify(receipt));
 }
 const changed=[...sources,...tools,...boundInputs.values()].filter(r=>!fs.existsSync(r.path)||hash(r.path)!==r.sha256);assert.equal(changed.length,0,'actual selected source or toolchain changed during execution');
 const result={pass:true,status:'ACTUAL_PHYSICAL_AND_CROSS_NATIVE_WRITER_GATES_VERIFIED_NOT_FULL_LIFECYCLE',createdAt:new Date().toISOString(),sources,tools,spans,substitutions,writerSubstitutions,writerAlgorithms,reaperSha256:digest(reaper),consumers:consumers.map(c=>({symbol:c.symbol,sha256:digest(c.code)})),variants:results,boundToolchainInputs:[...boundInputs.values()],sourceCurrent:true,
 scope:{wholeMainCompiled:false,actualFullRuntimeHeaderCompiled:true,actualRefreshConsumerCompiled:true,actualSessionStoreAndTransactionMutex:true,onlyFivePhysicalAndFiveCliEnvironmentLeavesReplaced:true,actualWriterSessionStoreBridgeReaper:true,actualJobGuardClassCompiled:true,jobGuardPreparedOrRealJobQueried:false,controlledHostOwner:'synthetic native Host PID/birth; actual current parent capture',deviceApisProductHostOrInstalledAppdataAccess:false,ApplyReconnectAndFullReturnCallerIntegration:'owned by coordinator; not claimed',coldRecoveryOrHardwareAccepted:false,wholeGoalComplete:false}};
 save(path.join(out,'RESULT.json'),result);console.log(JSON.stringify({pass:true,variants:results.length,out}));
}catch(error){save(path.join(out,'FAILURE.json'),{pass:false,error:error.message,stack:error.stack,completedVariants:results,wholeGoalComplete:false});throw error;}
