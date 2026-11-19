// Execution companion of the original command-pending Python entry. Import performs no work.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {spawnSync} from 'node:child_process';
import {fileURLToPath,pathToFileURL} from 'node:url';
const SELF=fileURLToPath(import.meta.url),HERE=path.dirname(SELF);
const read=file=>JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));
const key=file=>path.resolve(file).toLowerCase();
const pin=file=>{const bytes=fs.readFileSync(file);return{path:path.resolve(file),bytes:bytes.length,sha256:createHash('sha256').update(bytes).digest('hex')};};
const unique=rows=>[...new Map(rows.map(row=>[key(row.path),row])).values()];
const save=(file,value)=>{const fd=fs.openSync(file,'wx');try{fs.writeFileSync(fd,JSON.stringify(value,null,2)+'\n');fs.fsyncSync(fd);}finally{fs.closeSync(fd);}};
function plain(file){for(let p=path.resolve(file);;p=path.dirname(p)){if(fs.existsSync(p))assert(!fs.lstatSync(p).isSymbolicLink(),'PENDING_REPARSE_PATH');if(path.dirname(p)===p)break;}return path.resolve(file);}
function files(root){const rows=[];for(const entry of fs.readdirSync(root,{withFileTypes:true})){const file=path.join(root,entry.name);assert(!entry.isSymbolicLink(),'PENDING_OUTPUT_REPARSE');if(entry.isDirectory())rows.push(...files(file));else{assert(entry.isFile());rows.push(pin(file));}}return rows;}
function options(argv){const values={};for(let i=0;i<argv.length;i+=2){const name=argv[i];assert(['--repo','--out','--case','--reuse'].includes(name)&&!values[name]&&typeof argv[i+1]==='string','PENDING_ARGUMENTS');values[name]=argv[i+1];}assert(values['--repo']&&values['--out']&&values['--case'],'PENDING_EXACT_CASE_REQUIRED');assert(path.isAbsolute(values['--repo'])&&path.isAbsolute(values['--out']),'PENDING_ABSOLUTE_PATH_REQUIRED');assert(/^[a-z][a-z0-9-]{0,127}$/.test(values['--case']),'PENDING_CASE_NAME');return{repo:plain(values['--repo']),out:plain(values['--out']),selected:values['--case'],reuse:values['--reuse']?plain(values['--reuse']):null};}
async function worker(ctx,opt,config,toolConfig){
 const {repo,out,selected}=opt,records=[],started=Date.now();let failure=null,graph=null,native=null;
 const tools=unique(toolConfig.files.map(row=>{const observed=pin(row.path);assert.equal(observed.sha256,row.sha256,'PENDING_TOOL_IDENTITY_DRIFT');return observed;}));
 const rawModule=path.join(repo,'tools/ymcc_sim/global/decky-persistent-lifecycle/raw-owned-producer.mjs');
 const {startOwnedRawProducer,verifyRawProducerBudget,rawProgramLauncherDeclaration}=await import(pathToFileURL(rawModule));
 const host=path.join(repo,'tools/ymcc_sim/global/host-process.ps1');
 const ps=toolConfig.powershellExecutable,python=toolConfig.pythonExecutable,entry=path.resolve(HERE,'../steam_original_recovery_dual_route_selftest.py');
 const nativeTools=config.native,prepared=opt.reuse?path.join(opt.reuse,'prepared'):path.join(out,'prepared'),build=path.join(out,'build'),temp=path.join(out,'temp');
 fs.mkdirSync(build);fs.mkdirSync(temp);
 const env={};for(const name of ['SystemRoot','windir','ComSpec','PATHEXT','NUMBER_OF_PROCESSORS','PROCESSOR_ARCHITECTURE','USERPROFILE','APPDATA','LOCALAPPDATA','ProgramData'])if(process.env[name]!==undefined)env[name]=process.env[name];
 Object.assign(env,{PATH:[path.dirname(nativeTools.compiler),'C:/Windows/System32','C:/Windows'].map(p=>path.resolve(p)).join(';'),INCLUDE:nativeTools.include.join(';'),LIB:nativeTools.lib.join(';'),TEMP:temp,TMP:temp,VSLANG:'1033',CL:'',_CL_:'',LINK:'',_LINK_:'',PYTHONDONTWRITEBYTECODE:'1',PYTHONNOUSERSITE:'1'});
 const born=spawnSync(ps,['-NoLogo','-NoProfile','-NonInteractive','-Command','(Get-Process -Id '+process.pid+').StartTime.ToUniversalTime().Ticks'],{encoding:'utf8',windowsHide:true,timeout:15000});
 save(path.join(out,'owner-query.json'),{program:pin(ps),pid:born.pid,exitCode:born.status,signal:born.signal,error:born.error?.code??null,stdout:born.stdout,stderr:born.stderr});assert.equal(born.status,0);assert(/^\d{17,20}$/.test(born.stdout.trim()),'PENDING_OWNER_BIRTH');
 const launcher=rawProgramLauncherDeclaration({YMCC_SIM_RAW_PROGRAM_PWSH:ps});
 const fixed=unique([pin(SELF),pin(entry),pin(path.join(HERE,'recovery_command_pending.py')),pin(path.join(HERE,'recovery_command_group_member_wait.py')),pin(path.join(HERE,'recovery_command_pending.cpp.in')),pin(path.join(HERE,'recovery_command_pending_cases.inc')),pin(rawModule),pin(host),pin(path.join(repo,'tools/ymcc_sim/global/sleep-native-recovery-consumer/toolchain-inputs.json')),pin(path.join(repo,'tools/ymcc_sim/global/decky-update-recovery/native-launch-noio/native-tools.json'))]);
 const artifacts=unique([...tools,...launcher.files.map(pin),pin(process.execPath),pin(python),pin(ps),pin(nativeTools.compiler),pin(nativeTools.linker)]);
 const parentStarted=ctx.deadlineEpochMs-ctx.budgetMs;
 async function run(id,program,args,cap,source,compiler=false){
  const at=Date.now(),parentRemaining=ctx.deadlineEpochMs-at;assert(ctx.remainingMs()>cap+10000,'PENDING_PHASE_WINDOW_INSUFFICIENT');
  const vctip=artifacts.find(row=>path.basename(row.path).toLowerCase()==='vctip.exe');
  const owned=startOwnedRawProducer({id,program,args,cwd:build,env,host,powershell:ps,ownerStartTicks:born.stdout.trim(),directory:path.join(out,id),stdoutPath:path.join(out,id+'.out'),stderrPath:path.join(out,id+'.err'),allocatedAtEpochMs:at,deadlineEpochMs:at+Math.min(cap,parentRemaining),parentStartedAtEpochMs:parentStarted,parentBudgetMs:ctx.budgetMs,parentRemainingBeforeMs:parentRemaining,originalBudgetMs:cap,cleanupReserveMs:5000,sourceInputs:unique([...fixed,...source]),toolArtifacts:unique([...artifacts,pin(program)]),toolCleanupPolicy:compiler&&vctip?{kind:'MSVC_VCTIP_AFTER_ROOT_EXIT',maximumProcesses:1,program:{path:vctip.path,sha256:vctip.sha256}}:null});
  const result=await owned.wait();save(path.join(out,id+'.PROCESS.json'),result);records.push({id,path:path.join(out,id+'.PROCESS.json'),exitCode:result.exitCode});
  assert.equal(result.error,null,'PENDING_OWNED_PROCESS_INCOMPLETE');verifyRawProducerBudget(result,cap,ctx.budgetMs);assert.equal(result.exitCode,0,'PENDING_PHASE_FAILED:'+id);return result;
 }
 try{
  let sourceRows;
  if(opt.reuse){
   const prior=opt.reuse,oldPlan=read(path.join(prior,'COMPILE-PLAN.json')),oldGraph=read(path.join(prior,'ACTUAL-GRAPH.json')),oldProcess=read(path.join(prior,'compile.PROCESS.json')),oldSnapshots=read(path.join(prior,'SOURCE-SNAPSHOT.json'));
   assert.equal(oldProcess.error,null);assert.equal(oldProcess.exitCode,0);verifyRawProducerBudget(oldProcess,180000,600000);
   assert.equal(key(oldGraph.executable.path),key(path.join(prior,'build/Fixture.exe')));assert.equal(oldGraph.executable.sha256,pin(oldGraph.executable.path).sha256);
   const request=read(oldProcess.rawExecution.request.path),known=new Map([...request.sources,...request.tools].map(row=>[key(row.path),row]));
   for(const row of [...request.sources,...request.tools,...oldPlan.sourceRows,...oldPlan.artifacts])assert.equal(pin(row.path).sha256,row.sha256,'PENDING_REUSE_DECLARED_DRIFT');
   const oldDependencies=read(oldGraph.dependencies.path).Data,observed=[oldDependencies.Source,...oldDependencies.Includes];
   assert.equal(JSON.stringify(observed.map(key)),JSON.stringify(oldGraph.actualInputs.map(row=>key(row.path))),'PENDING_REUSE_GRAPH_MEMBERSHIP');
   for(const row of [...oldGraph.actualInputs,...oldGraph.actualLibraries])assert.equal(known.get(key(row.path))?.sha256,pin(row.path).sha256,'PENDING_REUSE_UNDECLARED_GRAPH');
   for(const row of oldGraph.actualInputs)assert.equal(pin(row.path).sha256,row.sha256);
   for(const row of oldGraph.actualLibraries)assert.equal(pin(row.path).sha256,row.sha256);
   assert.equal(oldPlan.compiler.sha256,pin(nativeTools.compiler).sha256);
   const manifest=read(path.join(prepared,'PREPARED.json'));assert(manifest.allowedExplicitCases.includes(selected),'PENDING_REUSE_UNKNOWN_CASE');
   for(const base of ['recovery_command_pending.cpp.in','recovery_command_pending_cases.inc','recovery_command_group_member_wait.py']){const old=request.sources.find(row=>path.basename(row.path)===base);assert(old);assert.equal(pin(path.join(HERE,base)).sha256,old.sha256);}
   const priorGenerator=request.sources.find(row=>path.basename(row.path)==='recovery_command_pending.py');assert(priorGenerator);
   const slice=text=>text.slice(0,text.indexOf('\n\ndef prepare('));
   assert.equal(createHash('sha256').update(slice(fs.readFileSync(priorGenerator.path,'utf8'))).digest('hex'),createHash('sha256').update(slice(fs.readFileSync(path.join(HERE,'recovery_command_pending.py'),'utf8'))).digest('hex'),'PENDING_REUSE_GENERATOR_BODY_DRIFT');
   const priorEntry=request.sources.find(row=>path.basename(row.path)==='steam_original_recovery_dual_route_selftest.py');assert(priorEntry);
   const entryPrefix=text=>text.slice(0,text.indexOf('\ndef main():'));
   assert.equal(entryPrefix(fs.readFileSync(priorEntry.path,'utf8')),entryPrefix(fs.readFileSync(entry,'utf8')),'PENDING_REUSE_EXTRACTOR_DRIFT');
   const changedOriginals=[];
   for(const row of oldSnapshots.originals){
    const current=['recovery_command_pending.cpp.in','recovery_command_pending_cases.inc'].includes(path.basename(row.original.path))?path.join(HERE,path.basename(row.original.path)):row.original.path;
    const actual=pin(current);if(actual.sha256!==row.original.sha256)changedOriginals.push({before:row.original,current:actual});
    assert.equal(pin(row.frozen.path).sha256,row.frozen.sha256);
   }
   sourceRows=oldPlan.sourceRows;graph=oldGraph;
   if(changedOriginals.length){
    const projection=path.join(out,'current-projection'),beforeMain=pin(path.join(repo,'native/main.cpp'));
    await run('prepare',python,[entry,'--repo',repo,'--out',projection,'--scope','startup-pair','--command-pending-only','--command-pending-case',selected,'--prepare-only'],60000,[]);
    const projected=read(path.join(projection,'PREPARED.json'));assert.equal(projected.freshMainObservation.sha256,beforeMain.sha256,'PENDING_PROJECTION_MAIN_CHANGED');assert.equal(pin(beforeMain.path).sha256,beforeMain.sha256,'PENDING_PROJECTION_MAIN_CHANGED');
    const compiledBase=path.join(prior,'prepared'),paired=[];
    for(const row of oldGraph.actualInputs){const rel=path.relative(compiledBase,row.path);if(!rel.startsWith('..'+path.sep)&&rel!=='..'&&!path.isAbsolute(rel)){const current=pin(path.join(projection,rel));assert.equal(current.sha256,row.sha256,'PENDING_REUSE_COMPILED_PROJECTION_DIFFERS');paired.push({compiled:row,current});}}
    assert(paired.some(row=>path.basename(row.current.path)==='runtime-composition.cpp')&&paired.some(row=>path.basename(row.current.path)==='command-pending-cases.inc'),'PENDING_REUSE_PROJECTION_GRAPH_INCOMPLETE');
    for(const row of projected.headers){assert.equal(pin(row.input.path).sha256,row.input.sha256,'PENDING_PROJECTED_HEADER_MOVED');assert.equal(pin(row.output.path).sha256,row.output.sha256);}
    const mainCopy=path.join(out,'current-projection-main.cpp');fs.copyFileSync(beforeMain.path,mainCopy,fs.constants.COPYFILE_EXCL);assert.equal(pin(mainCopy).sha256,beforeMain.sha256);
    save(path.join(out,'CURRENT-PROJECTION.json'),{changedOriginals,currentPreparation:pin(path.join(projection,'PREPARED.json')),mainObservation:beforeMain,frozenMain:pin(mainCopy),pairedCompilerInputs:paired,wholeCurrentMainAcceptance:false,onlyExactExistingCompilerInputProjectionReused:true});
    sourceRows=unique([...sourceRows,...files(projection),pin(mainCopy),pin(path.join(out,'CURRENT-PROJECTION.json'))]);
   }
   save(path.join(out,'COMPILED-REFERENCE.json'),{status:'CACHED_REFERENCE',executedThisRun:false,compilerInvocations:0,originalCompile:pin(path.join(prior,'compile.PROCESS.json')),originalGraph:pin(path.join(prior,'ACTUAL-GRAPH.json')),originalPlan:pin(path.join(prior,'COMPILE-PLAN.json')),originalSnapshots:pin(path.join(prior,'SOURCE-SNAPSHOT.json')),actualSourcesAndToolsRevalidated:true,originalGeneratorAndExtractorBodiesExact:true,executable:pin(oldGraph.executable.path)});
  }else{
  const mainBefore=pin(path.join(repo,'native/main.cpp'));
  await run('prepare',python,[entry,'--repo',repo,'--out',prepared,'--scope','startup-pair','--command-pending-only','--command-pending-case',selected,'--prepare-only'],60000,[]);
  const manifest=read(path.join(prepared,'PREPARED.json'));assert.equal(manifest.requestedCase,selected);assert(manifest.allowedExplicitCases.includes(selected),'PENDING_UNKNOWN_OR_UNSUPPORTED_CASE');assert.equal(manifest.compileAuthorized,false);assert.equal(manifest.businessAuthorized,false);assert.equal(manifest.freshMainObservation.sha256,mainBefore.sha256,'PENDING_MAIN_CHANGED_DURING_PREPARE');
  const originals=unique([manifest.template,manifest.cases,manifest.runtime,manifest.freshMainObservation,manifest.sleepContract,...manifest.headers.map(row=>row.input)]);
  fs.mkdirSync(path.join(out,'source-inputs'));const snapshots=[];
  for(const [index,row]of originals.entries()){const bytes=fs.readFileSync(row.path);assert.equal(createHash('sha256').update(bytes).digest('hex'),row.sha256,'PENDING_SOURCE_CHANGED_DURING_PREPARE');const target=path.join(out,'source-inputs',String(index).padStart(3,'0')+'-'+path.basename(row.path));fs.writeFileSync(target,bytes,{flag:'wx'});snapshots.push({original:row,frozen:pin(target)});}
  sourceRows=unique([...files(prepared),...snapshots.map(row=>row.frozen)]);save(path.join(out,'SOURCE-SNAPSHOT.json'),{originals:snapshots,generated:sourceRows,wholeMainCopiedForHistoricalIdentityOnly:true,currentMainAcceptance:false});
  const exe=path.join(build,'Fixture.exe'),object=path.join(build,'Fixture.obj'),dependencies=path.join(build,'dependencies.json');
  const args=['/nologo','/Bv','/std:c++20','/utf-8','/EHsc','/MT','/O2','/bigobj','/W4','/WX','/DUNICODE','/D_UNICODE','/sourceDependencies',dependencies,'/I'+path.join(prepared,'include'),path.join(prepared,'runtime-composition.cpp'),'/Fo'+object,'/Fe'+exe,'/link','/INCREMENTAL:NO','/VERBOSE:LIB','advapi32.lib','user32.lib','shell32.lib','version.lib','ole32.lib','bcrypt.lib'];
  save(path.join(out,'COMPILE-PLAN.json'),{compiler:pin(nativeTools.compiler),args,environment:env,sourceRows,artifacts,selectedCase:selected,maximumCompilerRuns:1,compileLimitMs:180000,nativeLimitMs:30000});
  const built=await run('compile',nativeTools.compiler,args,180000,sourceRows,true),known=new Map([...sourceRows,...artifacts].map(row=>[key(row.path),row]));
  const dependency=read(dependencies).Data,actualInputs=[dependency.Source,...dependency.Includes].map(file=>{const p=pin(file);assert.equal(known.get(key(file))?.sha256,p.sha256,'PENDING_UNDECLARED_COMPILER_INPUT:'+file);return p;});
  const libraries=[];for(const match of fs.readFileSync(built.stdout.path,'utf8').matchAll(/Searching\s+([A-Za-z]:[^\r\n]+\.lib):/g)){const p=pin(match[1]);assert.equal(known.get(key(p.path))?.sha256,p.sha256,'PENDING_UNDECLARED_LINK_INPUT');if(!libraries.some(row=>key(row.path)===key(p.path)))libraries.push(p);}assert(libraries.length>0,'PENDING_LINK_OBSERVATIONS_REQUIRED');
  graph={actualInputs,actualLibraries:libraries,executable:pin(exe),object:pin(object),dependencies:pin(dependencies),compile:pin(path.join(out,'compile.PROCESS.json'))};save(path.join(out,'ACTUAL-GRAPH.json'),graph);
  }
  const done=await run('native',graph.executable.path,[path.join(out,'runtime'),'--command-pending-only','--case',selected],30000,[...sourceRows,graph.executable,...graph.actualLibraries,pin(path.join(opt.reuse??out,'ACTUAL-GRAPH.json'))]);
  const text=fs.readFileSync(done.stdout.path,'utf8'),lines=text.split(/\r?\n/),jsonStart=lines.findIndex(line=>line.trim()==='{');assert(jsonStart>=0,'PENDING_NATIVE_JSON_MISSING');native=JSON.parse(lines.slice(jsonStart).join('\n'));
  assert.equal(native.selectedCase,selected);assert.equal(native.cases.length,1);assert.equal(native.cases[0].name,selected);assert.equal(native.cases[0].pass,true);assert.equal(native.error,'');assert.equal(native.nativeSteamAuthority,false);assert.equal(native.fieldAcceptance,false);assert.equal(native.ownedTreeNaturalJobZeroBeforeClose,true);save(path.join(out,'NATIVE-RAW.json'),native);
  for(const row of sourceRows)assert.equal(pin(row.path).sha256,row.sha256,'PENDING_FROZEN_SOURCE_DRIFT');for(const row of artifacts)assert.equal(pin(row.path).sha256,row.sha256,'PENDING_TOOL_CLOSING_DRIFT');
 }catch(error){failure={message:error.message,stack:error.stack};}
 const result={schemaVersion:1,status:failure?'FAIL':'PASS_SELECTED_SCOPE',runDir:ctx.runDir,summary:{selected:1,executedThisRun:records.some(row=>row.id==='native')?1:0,actualPassedThisRun:failure?0:1},selectedCase:selected,records,graph,native,failure,elapsedMs:Date.now()-started,compileRuns:records.filter(row=>row.id==='compile').length,scope:'Original command-pending single explicit parameter; controlled engine/IO/read ports. No full/default/real Steam/field/product acceptance.',productWrites:0,newDefaultIds:0,ordinaryExport:'NOT_RUN'};
 save(path.join(out,'RESULT.json'),result);save(path.join(ctx.runDir,'result.json'),result);console.log(JSON.stringify({status:result.status,out,selectedCase:selected,failure:failure?.message}));return failure?1:0;
}
async function main(){
 const argv=process.argv.slice(2),isWorker=argv[0]==='--budget-worker';
 const context=isWorker?read(argv[1]):null,opt=options(isWorker?context.args:argv),{repo,out}=opt;
 const workspace=path.resolve(repo,'../../..'),core=path.join(workspace,'Testing/YMCC');
 const rel=path.relative(core,out);assert(rel&&rel!=='..'&&!rel.startsWith('..'+path.sep)&&!path.isAbsolute(rel),'PENDING_OUTPUT_OUTSIDE_CORE');
 if(opt.reuse){const reused=path.relative(core,opt.reuse);assert(reused&&reused!=='..'&&!reused.startsWith('..'+path.sep)&&!path.isAbsolute(reused)&&key(opt.reuse)!==key(out),'PENDING_REUSE_OUTSIDE_CORE');}
 const configPath=path.join(repo,'tools/ymcc_sim/global/decky-update-recovery/native-launch-noio/native-tools.json'),toolPath=path.join(repo,'tools/ymcc_sim/global/sleep-native-recovery-consumer/toolchain-inputs.json');const config=read(configPath),toolConfig=read(toolPath);
 const {runWithWallBudget,readBudgetWorker}=await import(pathToFileURL(path.join(repo,'tools/ymcc_sim/optimization/execution-budget.mjs')));
 if(isWorker)return worker(readBudgetWorker(argv[1],argv[2],{repoRoot:repo,coreRoot:core}),opt,config,toolConfig);
 assert(!fs.existsSync(out),'PENDING_FRESH_OUTPUT_REQUIRED');fs.mkdirSync(out,{recursive:true});
 const result=await runWithWallBudget(argv,{repoRoot:repo,coreRoot:core,workerEntry:SELF,budgetMs:600000,powershell:toolConfig.powershellExecutable,outDir:path.join(out,'owned'),additionalInputs:[configPath,toolPath,path.resolve(HERE,'../steam_original_recovery_dual_route_selftest.py'),path.join(HERE,'recovery_command_pending.py'),path.join(HERE,'recovery_command_group_member_wait.py'),path.join(HERE,'recovery_command_pending.cpp.in'),path.join(HERE,'recovery_command_pending_cases.inc')]});return result.exitCode;
}
main().then(code=>{process.exitCode=code;},error=>{console.error(error.stack??String(error));process.exitCode=1;});