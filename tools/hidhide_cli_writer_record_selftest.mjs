import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {spawn} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import assert from 'node:assert/strict';
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..'),args=process.argv.slice(2);
assert(args.length===4&&args[0]==='--out'&&args[2]==='--toolchain'&&path.isAbsolute(args[1])&&path.isAbsolute(args[3]),'Use --out ABS_FRESH_DIRECTORY --toolchain ABS_CAPTURED_TOOLCHAIN_DIRECTORY');
const out=path.resolve(args[1]),toolchain=path.resolve(args[3]),key=p=>path.resolve(p).toLowerCase();
const within=(a,b)=>{const r=path.relative(a,b);return r===''||(!r.startsWith('..'+path.sep)&&r!=='..'&&!path.isAbsolute(r));};
assert(!within(repo,out)&&!within(out,repo)&&!within(toolchain,out)&&!within(out,toolchain),'Output cannot overlap product source or captured toolchain');
assert(!fs.existsSync(out),'Existing evidence must never be overwritten');
const sha=p=>crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex');
const json=p=>JSON.parse(fs.readFileSync(p,'utf8').replace(/^\uFEFF/,''));
const id=p=>({path:path.resolve(p),sha256:sha(p),bytes:fs.statSync(p).size});
// Verify the explicitly supplied sealed evidence before creating any output.
const priorResultPath=path.join(toolchain,'RESULT.json'),prior=json(priorResultPath);
assert.equal(sha(priorResultPath),fs.readFileSync(path.join(toolchain,'RESULT.sha256'),'utf8').trim().split(/\s+/)[0],'captured evidence RESULT seal');
assert.equal(sha(prior.supportingEvidence.manifest.path),prior.supportingEvidence.manifest.sha256,'captured evidence inventory');
const priorInventory=json(prior.supportingEvidence.manifest.path);
for(const name of ['build-environment.json','toolchain-manifest.json']){const p=path.join(toolchain,name),row=priorInventory.files.find(x=>key(x.path)===key(p));assert(row&&sha(p)===row.sha256,'sealed captured artifact: '+name);}
const capture=json(path.join(toolchain,'build-environment.json')),manifest=json(path.join(toolchain,'toolchain-manifest.json'));
fs.mkdirSync(out,{recursive:true});
const save=(name,value)=>{const p=path.resolve(out,name);assert(within(out,p)&&p!==out,'evidence path boundary');fs.mkdirSync(path.dirname(p),{recursive:true});fs.writeFileSync(p,JSON.stringify(value,null,2)+'\n',{flag:'wx'});return id(p);};
const sources=['native/hidhide_cli_writer_record.h','native/hidhide_cli_writer_store.h','native/virtual_target_return_record.h','native/virtual_target_return_store.h','tools/fixtures/hidhide_cli_writer_record/fixture.cpp','tools/fixtures/hidhide_cli_writer_record/README.md','tools/hidhide_cli_writer_record_selftest.mjs','deps/json/json.hpp'];
const frozen=sources.map(relative=>{const p=path.join(repo,relative),before=sha(p),dest=path.join(out,'source',relative);fs.mkdirSync(path.dirname(dest),{recursive:true});fs.copyFileSync(p,dest,fs.constants.COPYFILE_EXCL);assert.equal(sha(dest),before,'source copy identity');assert.equal(sha(p),before,'source stable at copy');return{relative,path:p,sha256:before,snapshot:dest,bytes:fs.statSync(dest).size};});
save('SOURCES.json',{schemaVersion:1,sources:frozen,toolchain:id(path.join(toolchain,'toolchain-manifest.json')),capturedEnvironment:id(path.join(toolchain,'build-environment.json')),priorResult:id(priorResultPath)});
const env={...capture.environment};for(const k of Object.keys(env))if(/^(CL|_CL_|LINK|_LINK_|YEMAN_SCREEN_NATIVE_INPUT_CANDIDATE)$/i.test(k))delete env[k];
env.INCLUDE=manifest.roots.filter(x=>x.category==='INCLUDE').map(x=>x.frozen).join(';');env.LIB=manifest.roots.filter(x=>x.category==='LIB').map(x=>x.frozen).join(';');env.LIBPATH=env.LIB;env.TEMP=path.join(out,'temp');env.TMP=env.TEMP;fs.mkdirSync(env.TEMP,{recursive:true});
const cl=capture.tools['cl.exe'].paths[0],dump=capture.tools['dumpbin.exe'].paths[0];
for(const row of manifest.tools)assert.equal(sha(row.original),row.sha256,'captured tool identity: '+row.name);
const toolRows=new Map(manifest.files.map(x=>[key(x.frozen),x]));
const results=[],allArtifacts=[];
async function run(executable,argv,cwd,log,timeout=0){
    const fd=fs.openSync(log,'wx');let finished=false;
    return await new Promise((resolve,reject)=>{const child=spawn(executable,argv,{cwd,env,windowsHide:true,stdio:['ignore',fd,fd]});
        const timer=timeout?setTimeout(()=>{if(!finished)child.kill();},timeout):null;
        child.once('error',error=>{finished=true;if(timer)clearTimeout(timer);fs.closeSync(fd);reject(error);});
        child.once('close',(code,signal)=>{if(finished)return;finished=true;if(timer)clearTimeout(timer);fs.closeSync(fd);resolve({exitCode:code,signal,log:id(log)});});
    });
}
const variants=[{name:'Release',flags:['/O2','/DNDEBUG']},{name:'Checked',flags:['/Od','/RTC1','/Z7']},
{name:'negative-duplicate-keys',flags:['/O2'],file:'hidhide_cli_writer_record.h',before:'if(duplicate||!stack.empty()||',after:'if(!stack.empty()||',requiredCase:'decode/duplicate-root'},
{name:'negative-owner-shared',flags:['/O2'],file:'hidhide_cli_writer_store.h',before:'ownerShareMode=0;',after:'ownerShareMode=FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE;',requiredCase:'lock/other-process-writer-blocked'},
{name:'negative-publish-before-commit',flags:['/O2'],file:'hidhide_cli_writer_store.h',before:'if(!saved.committed())return setError',after:'if(!saved.committed())record_=candidate;\n        if(!saved.committed())return setError',requiredCase:'store/flush-preserves'}];
try {
    for(const variant of variants){
        const dir=path.join(out,variant.name);fs.mkdirSync(path.join(dir,'files'),{recursive:true});const generated=[];
        for(const source of frozen.filter(x=>/\.(h|hpp|cpp)$/.test(x.relative))){const dest=path.join(dir,path.basename(source.relative));fs.copyFileSync(source.snapshot,dest,fs.constants.COPYFILE_EXCL);generated.push({...id(dest),original:source.relative});}
        if(variant.file){const p=path.join(dir,variant.file),text=fs.readFileSync(p,'utf8');assert.equal(text.split(variant.before).length,2,'single semantic mutation anchor');fs.writeFileSync(p,text.replace(variant.before,variant.after));generated.find(x=>key(x.path)===key(p)).sha256=sha(p);generated.find(x=>key(x.path)===key(p)).bytes=fs.statSync(p).size;}
        save(variant.name+'/GENERATED.json',{files:generated,mutation:variant.file?variant:null});
        const exe=path.join(dir,'CliWriterRecordFixture.exe'),dependencyPath=path.join(dir,'dependencies.json');
        const command=['/nologo','/std:c++20','/utf-8','/EHsc','/MT','/W4','/DUNICODE','/D_UNICODE',...variant.flags,'/showIncludes','/sourceDependencies',dependencyPath,path.join(dir,'fixture.cpp'),'/I'+dir,'/Fo'+path.join(dir,'fixture.obj'),'/Fe'+exe,'/link','/SUBSYSTEM:CONSOLE','/MACHINE:x64','kernel32.lib','/VERBOSE:LIB','/MAP:'+path.join(dir,'fixture.map')];
        console.log('BUILD '+variant.name);
        const built=await run(cl,command,dir,path.join(dir,'build.log'));save(variant.name+'/COMMAND.json',{executable:id(cl),arguments:command,...built});assert.equal(built.exitCode,0,'actual native compilation/link failed: '+variant.name+'/build.log');
        const generatedBy=new Map(generated.map(x=>[key(x.path),x])),dep=json(dependencyPath);
        const inputPaths=[...new Set([dep.Data.Source,...dep.Data.Includes])];
        const actualInputs=inputPaths.map(p=>{const row=generatedBy.get(key(p))??toolRows.get(key(p));assert(row&&sha(p)===row.sha256,'unbound/changed actual compiler input: '+p);return{path:p,sha256:row.sha256,scope:generatedBy.has(key(p))?'frozen-source-or-explicit-mutant':'captured-toolchain'};});
        const text=fs.readFileSync(path.join(dir,'build.log'),'utf8'),libs=[...new Set((text.match(/[A-Za-z]:[\\/][^\r\n]*?\.lib/gi)??[]).map(p=>path.resolve(p)))];
        assert(libs.length>0,'actual linker must report searched library paths');
        const libraries=libs.map(p=>{const row=toolRows.get(key(p));assert(row&&sha(p)===row.sha256,'unbound/changed actual library: '+p);return{path:p,sha256:row.sha256};});
        const mapText=fs.readFileSync(path.join(dir,'fixture.map'),'utf8');
        const contributors=[...new Set([...mapText.matchAll(/^[ \t]*[a-f0-9]{4}:[a-f0-9]{8}[ \t]+\S+[ \t]+[a-f0-9]{16}(?:[ \t]+[fi])*[ \t]+([^:\s]+):[^\s]+[ \t]*$/gmi)].map(m=>m[1].toLowerCase()))];
        for(const name of contributors)assert.equal(libraries.filter(x=>path.basename(x.path).replace(/\.lib$/i,'').toLowerCase()===name).length,1,'map contributor resolves to one bound library: '+name);
        const inspected=await run(dump,['/imports',exe],dir,path.join(dir,'imports.log'));assert.equal(inspected.exitCode,0);
        const imports=[...new Set([...fs.readFileSync(path.join(dir,'imports.log'),'utf8').matchAll(/^\s+([A-Za-z0-9_.-]+\.dll)\s*$/gm)].map(m=>m[1].toLowerCase()))];assert.deepEqual(imports,['kernel32.dll'],'static imports exclude all device/CLI/Host DLLs');
        const closure=save(variant.name+'/COMPILE-LINK-CLOSURE.json',{sourceDependencyReport:id(dependencyPath),actualCompilerInputs:actualInputs,unboundInputs:[],actualLinkerSearchedLibraries:libraries,mapContributors:contributors,map:id(path.join(dir,'fixture.map')),binary:id(exe),staticImportedDlls:imports,compiler:id(cl),linker:id(capture.tools['link.exe'].paths[0]),dumpbin:id(dump),scope:'Static imports and exact compile/link identities only; no runtime module trace.'});
        console.log('RUN '+variant.name);
        const ran=await run(exe,[path.join(dir,'files'),path.join(dir,'RESULT.json')],dir,path.join(dir,'run.log'),60000);assert(fs.existsSync(path.join(dir,'RESULT.json')),'fixture produced terminal receipt');
        const result=json(path.join(dir,'RESULT.json'));
        assert(!result.failures.some(x=>x.assertion.startsWith('scenario exception:')),'setup/runtime exceptions cannot count as semantic mutant detection');
        if(variant.file){assert.equal(ran.exitCode,1,'mutant must fail assertions, not compile/crash');assert(result.failures.some(x=>x.case===variant.requiredCase),'mutant trips original intended assertion: '+variant.requiredCase);}
        else{assert.equal(ran.exitCode,0,'private fixture assertions failed: '+variant.name+'/RESULT.json');assert(result.pass);assert(result.realIndependentNativeProcesses>=7&&result.ownerCreationCrashProcesses===2,'real independent native exclusion and exact owner-creation crash release');assert(result.childRuns.every(x=>x.pass),'independent process probes passed');}
        for(const row of generated)assert.equal(sha(row.path),row.sha256,'compiled generated input remains unchanged');
        const receipt={variant:variant.name,status:variant.file?'PASS_EXPECTED_SEMANTIC_FAILURE':'PASS',expectedFailure:!!variant.file,requiredCase:variant.requiredCase??null,caseCount:result.caseCount,assertions:result.assertions,observedFailures:result.failures,realIndependentNativeProcesses:result.realIndependentNativeProcesses,ownerCreationCrashProcesses:result.ownerCreationCrashProcesses,actualCompilerInputs:actualInputs.length,actualLinkerLibraries:libraries.length,staticImportedDlls:imports,closure,result:id(path.join(dir,'RESULT.json')),binary:id(exe),execution:ran};
        results.push(receipt);allArtifacts.push(closure,receipt.result,receipt.binary);console.log(JSON.stringify({variant:variant.name,status:receipt.status,cases:receipt.caseCount,assertions:receipt.assertions,observedFailures:result.failures.length,compilerInputs:actualInputs.length,libraries:libraries.length}));
    }
    const sourceDrift=frozen.filter(x=>sha(x.path)!==x.sha256||sha(x.snapshot)!==x.sha256);assert.equal(sourceDrift.length,0,'claimed data/store/test inputs drifted');
    const productionHeaders=frozen.filter(x=>x.relative.startsWith('native/hidhide_cli_writer_')).map(x=>({relative:x.relative,text:fs.readFileSync(x.snapshot,'utf8')}));
    const forbidden=/\b(?:CreateProcessW|OpenProcess|GetProcessTimes|GetExitCodeProcess|TerminateProcess|CreateJobObjectW|AssignProcessToJobObject|SetInformationJobObject|WaitForSingleObject|CreateThread|ShellExecuteW)\s*\(/;
    assert(productionHeaders.every(x=>!forbidden.test(x.text)),'production headers have no process/liveness/Job/thread/command calls');
    const inventory=[];function collect(dir){for(const entry of fs.readdirSync(dir,{withFileTypes:true})){if(dir===out&&['temp'].includes(entry.name))continue;const p=path.join(dir,entry.name);if(entry.isDirectory())collect(p);else if(!['RESULT.json','RESULT.sha256','evidence-manifest.json'].includes(entry.name)||dir!==out)inventory.push(id(p));}}
    collect(out);const inventoryId=save('evidence-manifest.json',{schemaVersion:1,sealedAtUtc:new Date().toISOString(),files:inventory,toolchainManifest:id(path.join(toolchain,'toolchain-manifest.json'))});
    save('RESULT.json',{schemaVersion:1,task:119,status:'PASS_DATA_AND_SYNCHRONOUS_WRITER_SESSION_NOT_LIFECYCLE',pass:true,recordedAtUtc:new Date().toISOString(),results,sources:frozen,sourceDrift,toolchain:{explicitDirectory:toolchain,manifest:id(path.join(toolchain,'toolchain-manifest.json')),environment:id(path.join(toolchain,'build-environment.json')),rediscovered:false},evidenceManifest:inventoryId,realPrivateFileIo:true,ordinaryProductHostRealCliDevicesServicesJobsOrAppDataAccessed:false,onlyOwnedFixtureProcessesLaunched:true,productionProcessOrJobCalls:false,runtimeIntegrated:false,targetOrColdCleanupAuthorityGranted:false,scope:'Strict writer record and bounded synchronous file Session with independently compiled semantic negatives; not CLI lifecycle/Job wiring, main integration or hardware acceptance.'});
    fs.writeFileSync(path.join(out,'RESULT.sha256'),sha(path.join(out,'RESULT.json'))+'  RESULT.json\n',{flag:'wx'});
    console.log(JSON.stringify({status:'PASS',result:id(path.join(out,'RESULT.json')),variants:results.length}));
} catch(error){save('FAILED.json',{status:'FAIL',error:error.stack,completedVariants:results,sources:frozen});console.error(error.stack);process.exitCode=1;}