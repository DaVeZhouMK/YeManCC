import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';

const hash = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const key = file => path.resolve(file).replace(/^\\\\\?\\/, '').toLowerCase();
const inside = (parent, file) => {
  const relative = path.relative(parent, file);
  return relative !== '' && relative !== '..' && !relative.startsWith('..' + path.sep) && !path.isAbsolute(relative);
};
const pin = file => ({path:path.resolve(file), sha256:hash(fs.readFileSync(file))});
const save = (file, value) => fs.writeFileSync(file, JSON.stringify(value, null, 2) + '\n');
function canonical(file) {
  const stat = fs.lstatSync(file);
  assert(!stat.isSymbolicLink(), 'Unexpected reparse path: ' + file);
  assert.equal(key(fs.realpathSync.native(file)), key(file), 'Realpath identity changed: ' + file);
  return stat;
}
function mkdirPlain(directory) {
  let ancestor = path.resolve(directory);
  for (;;) {
    try {canonical(ancestor); break;} catch (error) {
      if (error.code !== 'ENOENT') throw error;
      const next = path.dirname(ancestor); assert.notEqual(next, ancestor); ancestor = next;
    }
  }
  fs.mkdirSync(directory, {recursive:true});
  assert(canonical(directory).isDirectory());
}
function resourceSession(root, out, config) {
  root = path.resolve(root); out = path.resolve(out); canonical(root);
  assert(!fs.existsSync(out), 'Fresh ' + config.name + ' evidence required');
  mkdirPlain(out);
  const workspace = path.resolve('G:/YeManCC-Work/Testing/YMCC/Workspace');
  assert(canonical(workspace).isDirectory());
  const parent = path.join(workspace, config.short);
  if (!fs.existsSync(parent)) fs.mkdirSync(parent);
  assert(canonical(parent).isDirectory());
  const work = fs.mkdtempSync(path.join(parent, 'r'));
  assert.equal(key(path.dirname(work)), key(parent)); canonical(work);
  const owned = file => {
    assert(inside(work, path.resolve(file)), 'Path escapes exact owned work: ' + file);
    let ancestor = path.resolve(file);
    for (;;) {
      try {canonical(ancestor); break;} catch (error) {
        if (error.code !== 'ENOENT') throw error;
        ancestor = path.dirname(ancestor); assert(ancestor === work || inside(work, ancestor));
      }
    }
    return file;
  };
  const directory = relative => {const file=owned(path.join(work, relative)); mkdirPlain(file); return file;};
  const frozen = directory('s'), pins = [], seen = new Set(), runs = [], observedTools = new Map();
  save(path.join(work, 'OWNER.json'), {schemaVersion:1, nonce:crypto.randomUUID(), processId:process.pid,
    module:'tools/' + config.module, work, parent, callerEvidenceDir:out, createdAt:new Date().toISOString()});
  save(path.join(out, 'WORKSPACE.json'), {schemaVersion:1, work, callerEvidenceDir:out,
    sourceRoot:frozen, workspaceOwner:pin(path.join(work,'OWNER.json')), ancestorChecks:'exact parent, canonical realpath, no unapproved reparse ancestry', node:pin(process.execPath)});
  function freeze(file) {
    file=path.resolve(file); if(seen.has(key(file)))return; seen.add(key(file));
    assert(inside(root,file), 'Source outside exact repository: ' + file); assert(canonical(file).isFile());
    const relative=path.relative(root,file), bytes=fs.readFileSync(file), target=owned(path.join(frozen,relative));
    mkdirPlain(path.dirname(target)); fs.writeFileSync(target,bytes,{flag:'wx'}); canonical(target);
    assert.equal(hash(fs.readFileSync(target)),hash(bytes));
    pins.push({path:relative.replaceAll('\\','/'), sha256:hash(bytes), captured:target});
    for(const match of bytes.toString('utf8').matchAll(/^\s*#include\s+"([^"\r\n]+)"/gm)) {
      const child=[path.resolve(path.dirname(file),match[1]),path.join(root,'native',match[1]),path.join(root,'deps/json',match[1])].find(f=>fs.existsSync(f));
      if(child)freeze(child);
    }
  }
  for(const relative of ['tools/'+config.cpp,'tools/'+config.module,'decky-plugin/vendor/decky-loader.lock.json',
    'tools/ymcc_sim/global/decky-update-recovery/native-launch-noio/native-tools.json',
    'tools/ymcc_sim/global/decky-persistent-lifecycle/owned-tools.json',
    'tools/ymcc_sim/global/decky-update-recovery/resource-fixture-tools.json','tools/ymcc_sim/global/decky-update-recovery/resource-fixture-tools.mjs']) freeze(path.join(root,relative));
  save(path.join(out,'SOURCE-FREEZE.json'),{schemaVersion:1,root,frozen,files:pins});
  const lock=JSON.parse(fs.readFileSync(path.join(frozen,'decky-plugin/vendor/decky-loader.lock.json'),'utf8'));
  const loader=path.join(root,'PowerControl/decky/PluginLoader_noconsole.exe'); canonical(loader);
  assert.equal(hash(fs.readFileSync(loader)),lock.asset.sha256,'Official data file changed');
  const manifest=JSON.parse(fs.readFileSync(path.join(frozen,'tools/ymcc_sim/global/decky-update-recovery/native-launch-noio/native-tools.json'),'utf8'));
  const tools=manifest.native, declared=new Map(),reviewed=new Map();
  const reviewedManifest=JSON.parse(fs.readFileSync(path.join(frozen,'tools/ymcc_sim/global/decky-persistent-lifecycle/owned-tools.json'),'utf8'));
  const resourceManifestFile=path.join(frozen,'tools/ymcc_sim/global/decky-update-recovery/resource-fixture-tools.json');
  assert.equal(hash(fs.readFileSync(resourceManifestFile)),'a4b691bbf95cdbdfcfa5ca593d5bff955c95b8b75bf0b2ca4596f9a7ebb7acda','Resource fixed declaration changed before compile');
  const resourceManifest=JSON.parse(fs.readFileSync(resourceManifestFile,'utf8'));
  assert.equal(resourceManifest.schemaVersion,1);assert.equal(resourceManifest.files.length,1);
  for(const row of [...reviewedManifest.files,...resourceManifest.files]){canonical(row.path);assert.equal(hash(fs.readFileSync(row.path)),row.sha256,'Reviewed native tool changed before compile: '+row.path);reviewed.set(key(row.path),row);}
  for(const row of manifest.files){canonical(row.path); assert.equal(hash(fs.readFileSync(row.path)),row.sha256,'Declared native tool changed: '+row.path);declared.set(key(row.path),row);}
  for(const tool of [tools.compiler,tools.linker,tools.dumpbin,tools.commandProcessor])assert(declared.has(key(tool)),'Selected tool outside original declaration');
  const env=Object.fromEntries(Object.entries(process.env).map(([k,v])=>[k.toUpperCase(),v]));
  Object.assign(env,{PATH:path.dirname(tools.compiler)+';'+process.env.SystemRoot+'/System32',INCLUDE:tools.include.join(';'),LIB:tools.lib.join(';'),CL:'',_CL_:'',LINK:'',_LINK_:'',VSLANG:'1033'});
  function sourceRows(sourceRoot){return pins.map(row=>({relative:row.path,...pin(owned(path.join(sourceRoot,row.path)))}));}
  function observeTool(file,role){
    file=path.resolve(file); canonical(file);
    const original=declared.get(key(file));
    const admitted=original||reviewed.get(key(file));assert(admitted,'Actual tool dependency absent from both fixed declarations: '+file);
    const row={...pin(file),role,originalDeclaration:!!original};
    assert.equal(row.sha256,admitted.sha256,'Fixed declared tool input changed: '+file);
    const old=observedTools.get(key(file)); if(old)assert.equal(row.sha256,old.sha256,'Observed tool input changed: '+file);
    observedTools.set(key(file),row); return row;
  }
  function command(kind,program,args,dir,evidenceDir,environment,budget){
    const startedAt=new Date().toISOString(),started=performance.now();
    const result=spawnSync(program,args,{cwd:dir,env:environment,windowsHide:true,timeout:budget,maxBuffer:kind==='build'?4000000:6000000});
    const stdout=path.join(evidenceDir,kind+'.stdout.log'),stderr=path.join(evidenceDir,kind+'.stderr.log');
    fs.writeFileSync(stdout,result.stdout||'');fs.writeFileSync(stderr,result.stderr||'');
    fs.writeFileSync(path.join(evidenceDir,kind==='build'?'build.log':'run.log'),Buffer.concat([result.stdout||Buffer.alloc(0),result.stderr||Buffer.alloc(0)]));
    const receipt={kind,program:pin(program),args,cwd:dir,pid:result.pid,startedAt,elapsedMs:performance.now()-started,budgetMs:budget,status:result.status,signal:result.signal,error:result.error?.message??null,natural:!result.error&&!result.signal&&Number.isInteger(result.status),environment:{TEMP:environment.TEMP,TMP:environment.TMP,INCLUDE:environment.INCLUDE,LIB:environment.LIB},stdout:pin(stdout),stderr:pin(stderr)};
    save(path.join(evidenceDir,kind+'.PROCESS.json'),receipt);return {result,receipt};
  }
  function variant(index,mutation){
    const target=directory('v'+index);
    for(const row of pins){const destination=owned(path.join(target,row.path));mkdirPlain(path.dirname(destination));fs.copyFileSync(path.join(frozen,row.path),destination,fs.constants.COPYFILE_EXCL);}
    const file=owned(path.join(target,mutation[1])),original=fs.readFileSync(file,'utf8');
    assert(original.includes(mutation[2]),'Native mutation anchor missing '+mutation[0]);
    fs.writeFileSync(file,original.replace(mutation[2],mutation[3]));
    save(path.join(out,'mutation-'+mutation[0]+'.json'),{name:mutation[0],sourceRoot:target,path:mutation[1],from:mutation[2],to:mutation[3],expectedFailure:mutation[4],beforeSha256:hash(Buffer.from(original)),after:pin(file),sources:sourceRows(target)});
    return target;
  }
  function execute(name,sourceRoot,index,selectedCase){
    assert(/^[a-z0-9-]+$/.test(name));
    const dir=directory(index===null?'p':'n'+index),evidenceDir=path.join(out,name);mkdirPlain(evidenceDir);
    const temp=owned(path.join(dir,'t'));mkdirPlain(temp);
    const environment={...env,TEMP:temp,TMP:temp},exe=owned(path.join(dir,'resource-cache.exe')),dependencies=owned(path.join(dir,'d.json'));
    const capturedSources=sourceRows(sourceRoot),sourceMap=new Map(capturedSources.map(row=>[key(row.path),row]));
    const args=['/nologo','/std:c++20','/utf-8','/EHsc','/MT','/O2','/DNOMINMAX','/DUNICODE','/D_UNICODE','/I'+path.join(sourceRoot,'native'),'/I'+path.join(sourceRoot,'deps/json'),path.join(sourceRoot,'tools',config.cpp),'/Fo'+path.join(dir,'resource-cache.obj'),'/Fe'+exe,'/sourceDependencies',dependencies,'/link','/verbose:lib','advapi32.lib','user32.lib','ws2_32.lib','bcrypt.lib'];
    save(path.join(evidenceDir,'PATHS.json'),{schemaVersion:1,name,callerEvidenceDir:out,work,sourceRoot,compileDir:dir,temp,executable:exe,nativeResult:path.join(dir,'native.json'),fixture:path.join(dir,config.fixture),sources:capturedSources});
    const build=command('build',tools.compiler,args,dir,evidenceDir,environment,60000);
    assert.equal(build.result.status,0,config.title+' fixture compile failed '+(build.result.stdout||'')+(build.result.stderr||''));
    const data=JSON.parse(fs.readFileSync(dependencies,'utf8')).Data;
    const actualInputs=[data.Source,...data.Includes].map(file=>{const source=sourceMap.get(key(file));if(source){assert.equal(hash(fs.readFileSync(file)),source.sha256);return {...source,role:'captured-source'};}return observeTool(file,'actual-compiler-input');});
    const linkText=Buffer.concat([build.result.stdout||Buffer.alloc(0),build.result.stderr||Buffer.alloc(0)]).toString('utf8');
    const libraries=[...new Set([...linkText.matchAll(/[A-Za-z]:\\[^\r\n]*?\.lib\b/g)].map(match=>path.resolve(match[0])))].map(file=>observeTool(file,'actual-link-library'));
    assert(libraries.length>0,'Actual linker dependency trace missing');
    fs.copyFileSync(dependencies,path.join(evidenceDir,'dependencies.json'),fs.constants.COPYFILE_EXCL);
    const fixture=owned(path.join(dir,config.fixture));let junction=null;
    if(config.kind==='cache'){
      const outside=owned(path.join(fixture,'outside')),link=owned(path.join(fixture,'reparse/PowerControl/decky/.ymcc-resource-cache/a'));
      mkdirPlain(outside);fs.writeFileSync(path.join(outside,'KEEP.txt'),'OUTSIDE');mkdirPlain(path.dirname(link));
      fs.symlinkSync(outside,link,'junction');
      assert(fs.lstatSync(link).isSymbolicLink());assert.equal(key(fs.realpathSync.native(link)),key(outside));assert(inside(work,fs.realpathSync.native(link)));
      junction={path:link,target:outside,realTarget:fs.realpathSync.native(link),insideExactOwnedWork:true};
    }
    const nativeFile=owned(path.join(dir,'native.json'));
    const run=command('run',exe,[fixture,nativeFile,loader,...(selectedCase?['--case',selectedCase]:[])],dir,evidenceDir,environment,30000);
    const native=fs.existsSync(nativeFile)?JSON.parse(fs.readFileSync(nativeFile,'utf8')):null;
    if(native){fs.copyFileSync(nativeFile,path.join(evidenceDir,'native.json'),fs.constants.COPYFILE_EXCL);assert.equal(hash(fs.readFileSync(nativeFile)),hash(fs.readFileSync(path.join(evidenceDir,'native.json'))));}
    const result={name,status:run.result.status,error:run.result.error?.message,signal:run.result.signal,native,exeSha256:hash(fs.readFileSync(exe)),executable:pin(exe),actualPaths:{work,sourceRoot,dir,temp,fixture,nativeResult:nativeFile,callerEvidenceDir:out},build:build.receipt,run:run.receipt,sourceInputs:capturedSources,actualCompilerInputs:actualInputs,actualLinkLibraries:libraries,junction,originalCompileBudgetMs:60000,originalNativeBudgetMs:30000,originalStopDeadlineMs:3200};
    save(path.join(evidenceDir,'EXECUTION.json'),result);runs.push(result);return result;
  }
  function audit(){
    const sourceAfter=pins.map(row=>{const current=pin(path.join(root,row.path));return {path:row.path,beforeSha256:row.sha256,afterSha256:current.sha256,captured:pin(row.captured),unchanged:row.sha256===current.sha256};});
    const toolsAfter=manifest.files.map(row=>({path:row.path,expectedSha256:row.sha256,actualSha256:hash(fs.readFileSync(row.path))}));
    const actualToolAfter=[...observedTools.values()].map(row=>({...row,afterSha256:hash(fs.readFileSync(row.path))}));
    save(path.join(out,'SOURCE-AUDIT.json'),{schemaVersion:1,sourceChanged:sourceAfter.some(row=>!row.unchanged),rows:sourceAfter});
    save(path.join(out,'TOOL-GRAPH.json'),{schemaVersion:1,originalManifest:pin(path.join(root,'tools/ymcc_sim/global/decky-update-recovery/native-launch-noio/native-tools.json')),declaredTools:manifest.files,additionalFixedDeclaration:reviewedManifest.files,resourceFixedDeclaration:resourceManifest.files,selected:tools,actualInputs:[...observedTools.values()],observedOutsideOriginalDeclaration:[...observedTools.values()].filter(row=>!row.originalDeclaration),toolsAfter,actualToolAfter,node:pin(process.execPath),officialData:pin(loader)});
    for(const row of toolsAfter)assert.equal(row.actualSha256,row.expectedSha256,'Original native tool changed '+row.path);
    for(const row of actualToolAfter)assert.equal(row.afterSha256,row.sha256,'Actual native tool changed '+row.path);
    assert.equal(hash(fs.readFileSync(loader)),lock.asset.sha256,'Official data file changed');
    return sourceAfter;
  }
  return {root,out,work,pins,frozen,lock,runs,variant,execute,audit};
}

const CONFIG = {
  "kind": "cache",
  "name": "resource-cache",
  "exportName": "resourceCacheTests",
  "short": "Rc",
  "cpp": "decky_resource_cache_selftest.cpp",
  "module": "decky_resource_cache_selftest.mjs",
  "fixture": "resource-fixture",
  "title": "Resource cache",
  "scope": "Actual setup/cache compiled and run with real owned files, SHA256, exclusive TCP leases and cancellation; controlled same-image Job hard-stop during copy/metadata/live-repair, inert Loader plus official bytes as data. Positive runs every case; each semantic negative executes its original named assertion.",
  "mutants": [
    [
      "no-automatic-repair",
      "native/decky_sidebar_setup.h",
      "maintenance=ymcc::deckyresources::Cache(home,contract,digest,safe,mayPrepare,resourceOptions).run();",
      "maintenance={{\"resourcesRepaired\",0u},{\"resourceCacheAvailable\",false}};",
      "missing Loader and plugin are automatically restored before ready"
    ],
    [
      "missing-owner-gate",
      "native/decky_sidebar_resource_cache.h",
      "return current()&&options_.allowRepair&&(!options_.mayWrite||options_.mayWrite());",
      "return current();",
      "Loader ownership gate prevents resource repair"
    ],
    [
      "untrusted-stage-owner",
      "native/decky_sidebar_resource_cache.h",
      "require(observed==expected,\"resource-cache-stage-owner-invalid\");",
      "/* deliberately trust forged staging metadata */",
      "invalid staging owner cannot retire arbitrary or owned-looking content"
    ],
    [
      "unbounded-copy-stage",
      "native/decky_sidebar_resource_cache.h",
      "const auto staged=temporary(false);",
      "const auto staged=destination.parent_path()/(L\".ymcc-unbounded-\"+std::to_wstring(GetCurrentProcessId())+L\"-\"+std::to_wstring(GetTickCount64())+L\".tmp\");",
      "repeated hard interruption retains only bounded owned staging and can resume preparation"
    ]
  ]
};

export async function resourceCacheTests(root,out,{mutations=true}={}) {
  const session=resourceSession(root,out,CONFIG),{frozen}=session;
  let positive=null,executionError=null;
  const negatives=[],checks=[];
  try {
    positive=session.execute('positive',frozen,null);
    save(path.join(session.out,'positive.json'),positive);
    checks.push(()=>assert.equal(positive.status,0,'Native cache regression failed '+JSON.stringify(positive.native?.cases.filter(c=>!c.pass))+' '+positive.error));
    if(mutations)for(const [index,mutation] of CONFIG.mutants.entries()){
      const [name,relative,from,to,failure]=mutation;
      const target=session.variant(index,mutation);
      const run=session.execute('negative-'+name,target,index,failure);
      save(path.join(session.out,'negative-'+name+'.json'),run);
      const detected=run.status===2&&!!run.native?.cases.some(c=>c.name===failure&&!c.pass);
      negatives.push({name,detected,failure,execution:path.join(session.out,'negative-'+name,'EXECUTION.json')});
      checks.push(()=>assert.equal(run.status,2,'Native semantic negative did not execute to a behavioral FAIL '+name));
      checks.push(()=>assert(run.native?.cases.some(c=>c.name===failure&&!c.pass),'Native mutation escaped its original assertion '+name));
    }
  }catch(error){executionError=error;}
  let sourceAfter;
  try {sourceAfter=session.audit();}catch(error){executionError??=error;}
  const sourceChanges=sourceAfter?.filter(row=>!row.unchanged)??[];
  save(path.join(session.out,'EXECUTION-SUMMARY.json'),{work:session.work,positive:positive?{status:positive.status,count:positive.native?.count}:null,negativeMutations:negatives,executionError:executionError?.stack??null,sourceChanges});
  if(executionError)throw executionError;
  for(const check of checks)check();
  for(const row of sourceChanges)assert.equal(row.afterSha256,row.beforeSha256,'Source changed during cache regression '+row.path);
  const result={pass:true,nativeCases:positive.native.count,negativeMutations:negatives,sourceHashes:session.pins,officialDataSha256:session.lock.asset.sha256,officialLoaderExecuted:false,realSteamActions:0,installedWrites:0,physicalSleep:false,scope:CONFIG.scope,workRoot:session.work,callerEvidenceDir:session.out,sourceChanged:false,originalCompileBudgetMs:60000,originalNativeBudgetMs:30000,originalStopDeadlineMs:3200};
  save(path.join(session.out,'result.json'),result);return result;
}
if(process.argv[1]&&path.resolve(process.argv[1])===import.meta.filename){const result=await resourceCacheTests(path.resolve(import.meta.dirname,'..'),process.argv[2],{mutations:!process.argv.includes('--positive-only')});console.log('NATIVE_RESOURCE_CACHE_PASS cases='+result.nativeCases+' mutations='+result.negativeMutations.length);}
