import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {verifyBundledToolScope,bundledArtifacts} from '../bundled-node-tool-authority.mjs';
import {SPEC,MODULE,REPO,hash,key,inside,sourceFile,read,write,lockContext,applyFault,verifyPositive,verifyNegative,verifyOwnedScope,need} from './contract.mjs';
export function freshOutput(root){
 const absolute=path.resolve(root),parent=path.dirname(absolute);fs.mkdirSync(parent,{recursive:true});
 need(key(fs.realpathSync(parent))===key(parent),'OWNED_OUTPUT_PARENT_FORWARDING_REJECTED');
 need(!fs.existsSync(absolute),'FRESH_OWNED_EVIDENCE_REQUIRED');fs.mkdirSync(absolute);
 need(key(fs.realpathSync(absolute))===key(absolute),'OWNED_OUTPUT_FORWARDING_REJECTED');return absolute;
}
function ownedOutputs(root,toolPackages){
 const rows=[];
 function visit(dir){for(const entry of fs.readdirSync(dir,{withFileTypes:true})){
  const file=path.join(dir,entry.name);
  if(entry.name==='node_modules'){
   const names=fs.readdirSync(file).sort();need(entry.isDirectory()&&JSON.stringify(names)===JSON.stringify(Object.keys(toolPackages).sort()),'OWNED_TOOL_ALIAS_MEMBERS_CHANGED');
   for(const name of names){const alias=path.join(file,name);need(fs.lstatSync(alias).isSymbolicLink()&&key(fs.realpathSync(alias))===key(toolPackages[name]),'OWNED_TOOL_ALIAS_TARGET_CHANGED');}continue;
  }
  need(!entry.isSymbolicLink()&&inside(root,fs.realpathSync(file)),'OWNED_OUTPUT_REPARSE_REJECTED');
  if(entry.isDirectory())visit(file);else if(entry.isFile())rows.push({path:file,sha256:hash(file),status:'OK',role:'owned execution input or output; external tools excluded'});
 }}visit(root);return rows;
}
export async function execute(outDir,{repoRoot=REPO,env=process.env}={}){
 need(typeof outDir==='string'&&path.isAbsolute(outDir)&&!/[\0\r\n]/.test(outDir),'EXPLICIT_OWNED_OUTPUT_REQUIRED');need(!inside(repoRoot,path.resolve(outDir))&&!inside(path.resolve(outDir),repoRoot),'OUTPUT_MUST_BE_OUTSIDE_PRODUCT');
 const root=freshOutput(outDir),started=Date.now(),deadline=started+SPEC.timeoutMs;
 let context;try{context=lockContext(repoRoot,{env});}catch(error){const value={schemaVersion:1,id:SPEC.id,status:'BLOCKED',passed:false,reason:error.message,sourceApproval:false,preconditionIsNotSemanticDetection:true};write(path.join(root,'result.json'),value);return value;}
 const steps=[],ownedSourceRows=[],stepResults=[],artifactOutputs=[];
 write(path.join(root,'tool-scope-before.json'),context.authority);
 write(path.join(root,'source-before.json'),{selection:context.sourceSelection,sources:context.sources,helpers:context.helpers});
 try{
  for(const fault of [null,...SPEC.faults]){
   verifyBundledToolScope(context.authority,repoRoot);
   for(const row of context.sources)need(hash(sourceFile(repoRoot,row.path))===row.sha256,'SOURCE_CHANGED_BEFORE_EXECUTION');
   const label=fault?.id??'positive',stepRoot=path.join(root,label),view=path.join(stepRoot,'view');fs.mkdirSync(view,{recursive:true});
   const originals=[...SPEC.originalSources,'package.json'],viewSources=[];let mutation=null;
   for(const relative of originals){const original=sourceFile(repoRoot,relative),dest=path.join(view,relative);fs.mkdirSync(path.dirname(dest),{recursive:true});let bytes=fs.readFileSync(original);
    if(fault&&relative===SPEC.productSource){const before=bytes.toString('utf8'),after=applyFault(before,fault);bytes=Buffer.from(after);mutation={...fault,source:relative,beforeSha256:hash(original),afterSha256:null,onlyProductSourceChanged:true,originalProducerUnchanged:true};}
    fs.writeFileSync(dest,bytes);viewSources.push({path:dest,sha256:hash(dest),role:relative===SPEC.producer?'unchanged original producer':'original music player source or icon contract input'});if(mutation&&relative===SPEC.productSource)mutation.afterSha256=hash(dest);
   }
   // Two exact reviewed Vue and esbuild aliases. No compiler or dependency bytes are copied or rewritten.
   const modules=path.join(view,'node_modules');fs.mkdirSync(modules);const compilerPackage=path.dirname(path.dirname(context.authority.compilerEntry)),vuePackage=path.dirname(context.toolEntrypoints.vue);const toolPackages={esbuild:compilerPackage,vue:vuePackage};for(const [name,target]of Object.entries(toolPackages))fs.symlinkSync(target,path.join(modules,name),'junction');
   need(key(fs.realpathSync(path.join(modules,'esbuild')))==key(compilerPackage),'COMPILER_ALIAS_CHANGED');
   const rawFile=path.join(stepRoot,'original-result.json'),traceFile=path.join(stepRoot,'observation.json'),configFile=path.join(stepRoot,'observer-config.json');
   write(configFile,{stepRoot,raw:rawFile,trace:traceFile,producer:path.join(view,SPEC.producer),product:path.join(view,SPEC.productSource),compilerEntry:context.authority.compilerEntry,vueEntry:context.toolEntrypoints.vue,compilerBinary:context.authority.compilerBinary,compilerVersion:context.authority.compilerVersion,reads:[...viewSources,...context.sources.map(row=>({...row,path:sourceFile(repoRoot,row.path),role:'declared source helper'})),...context.authority.tools.map(row=>({...row,role:'complete predeclared Node tool scope'}))]});
   if(mutation)write(path.join(stepRoot,'single-source-fault.json'),mutation);
   const childEnv={SystemRoot:env.SystemRoot??env.SYSTEMROOT,TEMP:stepRoot,TMP:stepRoot,PATH:[path.dirname(context.authority.nodeExecutable),path.join(env.SystemRoot??env.SYSTEMROOT,'System32')].join(path.delimiter),ESBUILD_WORKER_THREADS:'0',ESBUILD_BINARY_PATH:context.authority.compilerBinary,YMCC_MUSIC_PLAYER_OBSERVER_CONFIG:configFile};
   const args=['--require',sourceFile(repoRoot,MODULE+'/observe.cjs'),path.join(view,SPEC.producer)];
   need(Date.now()<deadline,'ORIGINAL_TOTAL_BUDGET_EXHAUSTED');
   const began=new Date().toISOString(),proc=spawnSync(context.authority.nodeExecutable,args,{cwd:view,env:childEnv,encoding:'utf8',timeout:Math.max(1,deadline-Date.now()),maxBuffer:2*1024*1024,windowsHide:true});
   fs.writeFileSync(path.join(stepRoot,'stdout.log'),proc.stdout??'');fs.writeFileSync(path.join(stepRoot,'stderr.log'),proc.stderr??'');
   const processRow={label,executable:context.authority.nodeExecutable,args,cwd:view,environment:childEnv,pid:proc.pid,startedAt:began,finishedAt:new Date().toISOString(),exitCode:proc.status,signal:proc.signal,launchOrTimeoutError:proc.error?.message??null,originalTimeoutMs:SPEC.timeoutMs};steps.push(processRow);write(path.join(root,'process-ledger.json'),{steps});
   need(!proc.error&&!proc.signal&&Number.isInteger(proc.status),'PROCESS_NOT_NATURALLY_TERMINAL');need(fs.existsSync(traceFile),'OBSERVATION_MISSING');
   const trace=read(traceFile);
   // Hash and archive even a failing original run before evaluating its business result.
   for(const file of ['transform-input.ts','transform-output.cjs','transform-output.map','compiler-request.bin','compiler-response.bin'])if(fs.existsSync(path.join(stepRoot,file)))artifactOutputs.push({path:path.join(stepRoot,file),sha256:hash(path.join(stepRoot,file)),status:'OK'});
   for(const row of viewSources)need(hash(row.path)===row.sha256,'EXECUTED_VIEW_CHANGED');
   need(hash(path.join(view,SPEC.producer))===SPEC.producerSha256,'ORIGINAL_PRODUCER_MODIFIED');
   for(const relative of fault?[SPEC.productSource]:SPEC.originalSources.filter(file=>file!==SPEC.producer))need(trace.sourceReads.some(row=>key(row.realPath)===key(path.join(view,relative))&&row.sha256===hash(path.join(view,relative))),'SOURCE_NOT_ACTUALLY_READ:'+relative);
   need(trace.toolLoads.some(row=>key(row.realPath)===key(context.authority.compilerEntry)),'DECLARED_COMPILER_NOT_LOADED');
   for(const row of trace.toolLoads)need(context.authority.tools.some(pin=>key(pin.path)===key(row.realPath)&&pin.sha256===row.sha256)||context.sources.some(pin=>key(sourceFile(repoRoot,pin.path))===key(row.realPath)&&pin.sha256===row.sha256),'AMBIENT_TOOL_LOADED');
   if(!fault){try{const raw=JSON.parse(proc.stdout);write(rawFile,raw);}catch{throw Error('MUSIC_PLAYER_ORIGINAL_STDOUT_REPORT_MISSING_OR_INVALID');}}
   const verification=fault?verifyNegative(trace,fault,proc.status):verifyPositive(read(rawFile),trace,proc.status);
   ownedSourceRows.push(...viewSources);stepResults.push({label,mutation,verification,rawOriginalReport:fs.existsSync(rawFile)?rawFile:null,observation:traceFile,producerSha256:hash(path.join(view,SPEC.producer)),productSha256:hash(path.join(view,SPEC.productSource)),transformInput:path.join(stepRoot,'transform-input.ts'),transformOutput:path.join(stepRoot,'transform-output.cjs')});
   write(path.join(stepRoot,'verification.json'),verification);
   verifyBundledToolScope(context.authority,repoRoot);
  }
  const after=lockContext(repoRoot,{env});need(JSON.stringify(context)===JSON.stringify(after),'SOURCE_OR_TOOL_CHANGED_DURING_EXECUTION');
  write(path.join(root,'tool-scope-after.json'),after.authority);write(path.join(root,'source-after.json'),{selection:after.sourceSelection,sources:after.sources,helpers:after.helpers});
  const outputs=ownedOutputs(root,{esbuild:path.dirname(path.dirname(context.authority.compilerEntry)),vue:path.dirname(context.toolEntrypoints.vue)});
  verifyOwnedScope(outputs,root);
  const artifacts=bundledArtifacts(context.authority,[],outputs);
  const owned=new Map([...ownedSourceRows,...outputs].map(row=>[key(row.path),row]));
  for(const row of artifacts)need(hash(row.path)===row.sha256,'ARTIFACT_CHANGED');
  const toolLoads=[...new Map(stepResults.flatMap(step=>read(step.observation).toolLoads).map(row=>[key(row.realPath),row])).values()];
  const result={schemaVersion:1,id:SPEC.id,status:'PASS',passed:true,checks:SPEC.caseNames.length,caseNames:SPEC.caseNames,steps:stepResults,sourceHashes:[...context.sources,...[...owned.values()].map(({path,sha256})=>({path,sha256,status:'OK'}))],sourceSelection:context.sourceSelection,mandatoryAuthorityHelpers:context.helpers,declaredToolAuthority:context.authority,completeArtifactScope:artifacts,ownedOutputHashes:outputs,actuallyLoadedTools:toolLoads.filter(row=>context.authority.tools.some(pin=>key(pin.path)===key(row.realPath))),actuallyLoadedSourceHelpers:toolLoads.filter(row=>context.sources.some(pin=>key(sourceFile(repoRoot,pin.path))===key(row.realPath))),compilerMode:'Original esbuild.transformSync; explicit ESBUILD_WORKER_THREADS=0 synchronous native service, exact bound binary; input/output protocol and generated VM code archived',elapsedMs:Date.now()-started,originalBudgetMs:SPEC.timeoutMs,originalProducerAssertionsUnchanged:true,originalVmAndFixtureUnchanged:true,sourceApproval:false,boundary:{actualMusicSourceCompiledAndRunInOriginalVm:true,originalFakeAudioAndNativeMusicMemoryPorts:true,realAudioPlayback:false,realMusicFilesOrUserSettingsWritten:false,nativeMusicHostExecuted:false,uiBrowserRendered:false,physicalHardwareValidated:false,operatingSystemSleepPerformed:false},fullProductAcceptance:false};
  write(path.join(root,'result.json'),result);console.log(`MUSIC_PLAYER_TOOL_SCOPE_PASS cases=${SPEC.caseNames.length} faults=${SPEC.faults.length}`);return result;
 }catch(error){const result={schemaVersion:1,id:SPEC.id,status:'FAIL',passed:false,reason:error.message,steps,sourceApproval:false,preconditionIsNotSemanticDetection:true};write(path.join(root,'result.json'),result);throw error;}
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){
 try{need(process.argv.length===4&&process.argv[2]==='--evidence-dir','ARGUMENTS');const result=await execute(process.argv[3]);if(result.status==='BLOCKED')process.exitCode=10;}
 catch(error){console.error(error.stack??error.message);process.exitCode=2;}
}



