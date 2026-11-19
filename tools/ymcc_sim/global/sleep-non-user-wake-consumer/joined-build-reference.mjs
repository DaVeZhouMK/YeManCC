import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';

const need=(ok,code)=>{if(!ok)throw Error('NONUSER_BUILD_REFERENCE_'+code);};
const digest=bytes=>createHash('sha256').update(bytes).digest('hex');
const inside=(base,file)=>{const rel=path.relative(path.resolve(base),path.resolve(file));return !path.isAbsolute(rel)&&rel!=='..'&&!rel.startsWith('..'+path.sep);};
const stable=value=>JSON.stringify(value&&typeof value==='object'?Array.isArray(value)?value.map(x=>JSON.parse(stable(x))):Object.fromEntries(Object.keys(value).sort().map(key=>[key,JSON.parse(stable(value[key]))])):value);
const key=file=>path.resolve(file).toLowerCase();
const nonCompilationPaths=Object.freeze([
 'tools/ymcc_sim/global/sleep-non-user-wake-consumer/README.md',
 'tools/ymcc_sim/global/sleep-non-user-wake-consumer/runner.test.mjs'
]);
const nonCompilationPath=filename=>nonCompilationPaths.some(relative=>key(filename).endsWith(path.sep+relative.split('/').join(path.sep).toLowerCase()));

// These two exact captured roles are still evidence. They cannot grant test
// acceptance, substitute for a missing source, or exclude any executable input.
export function joinedCompilationSourceIdentity(rows){
 need(Array.isArray(rows),'SOURCE_MEMBERSHIP');const seen=new Set();
 const all=rows.map(row=>{
  need(row&&typeof row.path==='string'&&!path.isAbsolute(row.path)&&!row.path.includes('\\')&&row.path.split('/').every(part=>part&&part!=='.'&&part!=='..')&&/^[a-f0-9]{64}$/.test(row.sha256??'')&&!seen.has(row.path.toLowerCase()),'SOURCE_MEMBERSHIP');
  seen.add(row.path.toLowerCase());return {path:row.path,sha256:row.sha256};
 }).sort((a,b)=>a.path.localeCompare(b.path));
 need(nonCompilationPaths.every(relative=>all.some(row=>row.path===relative)),'NONCOMPILATION_ROLE_MISSING');
 return {all,compilation:all.filter(row=>!nonCompilationPaths.includes(row.path)),nonCompilation:all.filter(row=>nonCompilationPaths.includes(row.path))};
}

// Tool routing metadata changes per shell call. Do not pass this exact variable
// to the owned Python/compiler/native child; retain all other inherited inputs.
export function joinedBuildEnvironment(inherited){
 const environment={...inherited};
 for(const name of Object.keys(environment))if(/^CODEX_TOOL_CALL_ID$/i.test(name))delete environment[name];
 return environment;
}

function noReparse(file){
 for(let current=path.resolve(file);;){let stat;try{stat=fs.lstatSync(current);}catch(error){throw Error('NONUSER_BUILD_REFERENCE_MEMBER_UNAVAILABLE:'+error.code);}need(!stat.isSymbolicLink(),'REPARSE');const parent=path.dirname(current);if(parent===current)break;current=parent;}
}
export function pinJoinedBuildReference(file){
 need(typeof file==='string'&&file.length>0&&path.isAbsolute(file)&&path.basename(file)==='result.json','PATH');
 noReparse(file);need(fs.lstatSync(file).isFile(),'TYPE');const bytes=fs.readFileSync(file);
 return {path:path.resolve(file),bytes:bytes.length,sha256:digest(bytes)};
}

// Every effective environment variable participates. Only values of these exact
// owned path roles may relocate; unrelated paths/values keep their actual bytes.
export function joinedBuildContext({root,environment,batchLines}){
 need(path.isAbsolute(root)&&Array.isArray(batchLines),'CONTEXT');
 const owned=path.resolve(root),token='<YMCC_OWNED_SLEEP_ROOT>',relocatable=new Set(['CL','TEMP','TMP','PYTHONPATH','YMCC_SLEEP_PY_CONFIG','YMCC_SLEEP_NATIVE_ROOT']);
 const normalize=value=>{need(!value.includes(token),'CONTEXT_TOKEN');return value.replaceAll(owned,(match,offset)=>{
  const before=offset===0||/[\s"'=;]/.test(value[offset-1]),end=offset+owned.length;
  const after=end===value.length||/[\\/\s"';]/.test(value[end]);return before&&after?token:match;
 });};
 const entries=[],names=new Set();
 for(const [name,value] of Object.entries(environment)){
  const upper=name.toUpperCase();need(!names.has(upper)&&typeof value==='string','ENVIRONMENT');names.add(upper);
  entries.push([upper,digest(relocatable.has(upper)?normalize(value):value)]);
 }
 entries.sort(([a],[b])=>a.localeCompare(b));
 return {schemaVersion:1,role:'joined-original-effective-build-context',environment:entries,
  recipeSha256:digest(JSON.stringify(batchLines.map(normalize))),
  normalization:'Only current owned scratch paths in the original rendered recipe and CL/TEMP/TMP/PYTHONPATH/YMCC_SLEEP_PY_CONFIG/YMCC_SLEEP_NATIVE_ROOT; all other effective environment bytes participate.'};
}

// A qualified old result is build evidence only. This function runs no compiler,
// native scenario, or child process, and cannot produce a scenario PASS.
export function verifyJoinedBuildReference(authority,{snapshot,context=null}={}){
 need(authority&&snapshot&&Array.isArray(snapshot.sources),'REQUEST');
 const current=pinJoinedBuildReference(authority.path);
 need(current.sha256===authority.sha256&&current.bytes===authority.bytes,'AUTHORITY_CHANGED');
 const evidence=path.dirname(current.path),memo=new Map();
 const version=file=>{
  const stat=fs.lstatSync(file,{bigint:true}),fields=['dev','ino','size','mtimeNs','ctimeNs'];
  need(stat.isFile()&&!stat.isSymbolicLink(),'MEMBER_TYPE');
  need(fields.every(name=>typeof stat[name]==='bigint'),'MEMBER_VERSION_UNAVAILABLE');
  return fields.map(name=>String(stat[name])).join('|');
 };
 function bytes(file,expected=null,size=null){
  file=path.resolve(file);noReparse(file);
  // Each input role reobserves file identity before sharing bytes. Metadata-only
  // changes cause a fresh hash, not a failure by themselves. A concurrent change
  // during the read gets one bounded fresh observation; persistent churn refuses.
  for(let attempt=0;attempt<2;attempt++){
   const before=version(file);let observed=memo.get(file);
   if(!observed||observed.version!==before){const data=fs.readFileSync(file);observed={data,sha256:digest(data),version:before};}
   if(version(file)!==before){memo.delete(file);continue;}
   memo.set(file,observed);
   need(expected===null||observed.sha256===expected,'MEMBER_CHANGED:'+file);
   need(size===null||observed.data.length===size,'MEMBER_SIZE:'+file);return observed.data;
  }
  need(false,'MEMBER_CHANGED_DURING_REFERENCE:'+file);
 }
 const json=(file,sha=null)=>JSON.parse(bytes(file,sha).toString('utf8').replace(/^\uFEFF/,''));
 const result=json(current.path,current.sha256);
 need(result.schemaVersion===1&&result.id==='sleep.non-user-wake-consumer'&&result.status==='PASS'&&result.passed===true&&result.executionSelection==='resume-classification-sender-only'&&result.prototypeOnly===false,'ORIGINAL_RESULT');
 need(result.legacyParentExecuted===false&&result.sourceChanges?.length===0&&result.toolChanges?.length===0&&result.validationError===null,'ORIGINAL_SOURCE_STATUS');
 const archive=result.executionArchive;
 need(archive?.archivePerformed===true&&archive.cleaned===true&&archive.originalContentsRewritten===false&&path.resolve(archive.mapping)===path.join(evidence,'execution-archive-mapping.json')&&path.resolve(archive.rawWorkerResult)===path.join(evidence,'execution/worker-result.json'),'ARCHIVE_BINDING');
 const mapping=json(archive.mapping,archive.mappingSha256);
 need(mapping.schemaVersion===1&&mapping.kind==='sleep-native-execution-byte-archive'&&mapping.originalContentsRewritten===false&&path.resolve(mapping.evidenceRoot)===evidence&&path.resolve(mapping.archiveRoot)===path.join(evidence,'execution')&&mapping.originalRoot===archive.originalRoot&&Array.isArray(mapping.files)&&mapping.files.length===archive.archiveFiles,'ARCHIVE_SCHEMA');
 const mapped=new Map();
 for(const row of mapping.files){
  need(typeof row.relative==='string'&&row.relative.length>0&&!path.isAbsolute(row.relative)&&inside(mapping.originalRoot,row.originalPath)&&inside(mapping.archiveRoot,row.archivedPath)&&path.resolve(row.originalPath)===path.resolve(mapping.originalRoot,row.relative)&&path.resolve(row.archivedPath)===path.resolve(mapping.archiveRoot,row.relative)&&!mapped.has(key(row.originalPath)),'ARCHIVE_MEMBERSHIP');
  bytes(row.archivedPath,row.sha256,row.bytes);mapped.set(key(row.originalPath),row);
 }
 const locate=(file,sha=null)=>{const row=mapped.get(key(file));need(row&&(!sha||row.sha256===sha),'UNARCHIVED_INPUT:'+file);return row.archivedPath;};
 const originalSnapshot=json(path.join(evidence,'input-snapshot.json'));
 const originalIdentity=joinedCompilationSourceIdentity(originalSnapshot.sources),currentIdentity=joinedCompilationSourceIdentity(snapshot.sources);
 need(stable(originalIdentity.all.map(row=>row.path))===stable(currentIdentity.all.map(row=>row.path)),'SOURCE_MEMBERSHIP');
 need(stable(originalIdentity.compilation)===stable(currentIdentity.compilation),'SOURCE_IDENTITY');
 need(stable(originalSnapshot.tool)===stable(snapshot.tool),'TOOL_IDENTITY');
 for(const row of originalSnapshot.sources){need(path.resolve(row.saved)===path.resolve(evidence,'input-sources',row.path)&&inside(evidence,row.saved),'SOURCE_MEMBER');bytes(row.saved,row.sha256);}
 const sourceEntries=result.sourceHashes.filter(row=>!path.isAbsolute(row.path));
 need(stable(joinedCompilationSourceIdentity(sourceEntries).all)===stable(originalIdentity.all),'RESULT_SOURCE_MEMBERSHIP');
 const nonCompilationInputs=[];
 for(const relative of nonCompilationPaths){
  const prior=originalSnapshot.sources.find(row=>row.path===relative),present=snapshot.sources.find(row=>row.path===relative);
  need(typeof present.saved==='string'&&path.isAbsolute(present.saved)&&key(present.saved).endsWith(path.sep+'input-sources'+path.sep+relative.split('/').join(path.sep).toLowerCase()),'CURRENT_NONCOMPILATION_MEMBER');
  bytes(present.saved,present.sha256);
  nonCompilationInputs.push({path:relative,originalSha256:prior.sha256,currentSha256:present.sha256,changed:prior.sha256!==present.sha256,completeEvidenceVerified:true,grantsTestAcceptance:false});
 }
 const specRow=originalSnapshot.sources.find(row=>row.path==='tools/ymcc_sim/global/sleep-non-user-wake-consumer/spec.json');
 need(specRow&&!json(specRow.saved,specRow.sha256).productSources.some(relative=>nonCompilationPaths.includes(relative)),'NONCOMPILATION_ROLE_IS_EXECUTABLE_INPUT');
 const observedPaths=new Set();
 for(const row of result.sourceHashes.filter(row=>path.isAbsolute(row.path))){need(inside(evidence,row.path)&&!observedPaths.has(key(row.path)),'RESULT_MEMBER');observedPaths.add(key(row.path));bytes(row.path,row.sha256);}
 for(const file of [archive.mapping,archive.rawWorkerResult,path.join(evidence,'input-snapshot.json'),path.join(evidence,'job-receipt.json')])need(observedPaths.has(key(file)),'RESULT_PROOF_MISSING');
 const job=json(path.join(evidence,'job-receipt.json')),observed=result.ownedJob;
 need(observed?.status==='PASS'&&observed.naturalExit===true&&observed.resourcesReturned===true&&observed.forcedCleanup===false&&observed.reasons.length===0,'ORIGINAL_JOB_OBSERVATION');
 need(job.jobAssigned===true&&Number.isSafeInteger(job.childPid)&&job.childPid>0&&job.childPid===observed.childPid&&job.childExitedBeforeReturn===true&&job.resourceObservationVersion===1&&job.jobQueryCount>0&&job.activeProcessesFinal===0&&job.zeroObservedBeforeDispose===true&&job.jobDisposed===true&&!job.cleanupError&&job.status==='EXITED'&&job.exitCode===0&&job.rootExitCode===0&&job.deadlineExpired===false&&job.terminationRequested===false&&job.terminationSucceeded===false&&job.finishedEpochMs<=job.deadlineEpochMs,'ORIGINAL_JOB_TERMINAL');
 const worker=json(archive.rawWorkerResult),fact=worker.facts?.['resume-sender'];
 need(worker.status==='PASS'&&fact?.exitCode===0&&fact.compiledThisRun===true&&fact.compileReused===false&&fact.compiled===true&&fact.started===true&&fact.resumeSenderJoin?.pass===true,'ORIGINAL_COMPILE_REQUIRED');
 const produced=fact.produced,out=path.join(mapping.originalRoot,'resume-sender/original-output');
 const artifacts={};
 for(const [role,name] of [['cpp','joined.cpp'],['exe','joined.exe'],['object','joined.obj'],['binding','JOIN-TASK.json'],['helperResult','JOIN-RESULT.json']]){
  const row=produced?.[role];need(row&&path.resolve(row.path)===path.join(out,name),'ARTIFACT_ROLE');const file=locate(row.path,row.sha256);bytes(file,row.sha256);artifacts[role]={path:file,sha256:row.sha256,bytes:fs.statSync(file).size};
 }
 const helper=json(artifacts.helperResult.path),compile=helper.processes?.[0];
 need(helper.pass===true&&helper.compiledThisRun===true&&helper.compileReference===null&&helper.processes.length===2&&compile.name==='compile'&&compile.exitCode===0&&compile.timedOut===false&&compile.timeoutMs===60000&&helper.processes[1].name==='behavior'&&helper.processes[1].exitCode===0&&!helper.processes[1].timedOut,'ORIGINAL_COMPILE_TERMINAL');
 const batch=bytes(locate(path.join(out,'build.cmd'))),contextRow=fact.buildContext;
 need(contextRow&&path.resolve(contextRow.path)===path.join(out,'build-context.json'),'BUILD_CONTEXT_MISSING');
 const priorContext=json(locate(contextRow.path,contextRow.sha256));
 need(priorContext.schemaVersion===1&&priorContext.role==='joined-original-effective-build-context','BUILD_CONTEXT_SCHEMA');
 const originalRecipe=joinedBuildContext({root:mapping.originalRoot,environment:{},batchLines:batch.toString('utf8').split(/\r?\n/).filter(line=>line.trim())});
 need(originalRecipe.recipeSha256===priorContext.recipeSha256,'ORIGINAL_RECIPE');
 if(context!==null)need(stable(priorContext)===stable(context),'EFFECTIVE_ENVIRONMENT_OR_RECIPE');
 const auditRow=fact.pythonAudits?.[0];need(fact.pythonAudits?.length===1&&auditRow,'ORIGINAL_AUDIT_MISSING');
 const audit=json(locate(auditRow.path,auditRow.sha256));
 need(audit.denied?.length===0&&audit.naturalPythonExit===true&&!audit.uncaughtException&&audit.commands?.length===2&&stable(audit.commands[0].argv)===stable(compile.command)&&audit.commands[0].scriptSha256===digest(batch)&&audit.commands[1].executableSha256===artifacts.exe.sha256,'ORIGINAL_AUDIT');
 need(!Object.keys({...audit.reads,...audit.bootstrapImportedPythonModules}).some(nonCompilationPath)&&!audit.commands.some(row=>row.argv.some(value=>typeof value==='string'&&nonCompilationPath(value))),'NONCOMPILATION_ROLE_WAS_READ_OR_EXECUTED');
 const compilerInputs=fact.actualCompilerInputs;need(compilerInputs&&path.resolve(compilerInputs.path)===path.join(out,'evaluated-inputs.json'),'COMPILER_GRAPH_MISSING');
 const compilerGraph=json(locate(compilerInputs.path,compilerInputs.sha256));
 need(compilerGraph.schemaVersion===1&&compilerGraph.rows?.length===compilerInputs.rows&&compilerGraph.rows.length>0,'COMPILER_GRAPH_SCHEMA');
 const allowed=new Map(snapshot.tool.files.map(row=>[key(row.path),row.sha256])),roles=new Set();
 for(const row of compilerGraph.rows){
  need(!nonCompilationPath(row.path),'NONCOMPILATION_ROLE_WAS_COMPILED');
  need(['actual-compile-source','actual-header','actual-MSVC-source-dependencies','actual-library-search'].includes(row.role),'COMPILER_GRAPH_ROLE');roles.add(row.role);
  if(inside(mapping.originalRoot,row.path))bytes(locate(row.path,row.sha256),row.sha256);
  else{need(allowed.get(key(row.path))===row.sha256,'UNPINNED_COMPILER_INPUT');bytes(row.path,row.sha256);}
 }
 need(['actual-compile-source','actual-header','actual-MSVC-source-dependencies','actual-library-search'].every(role=>roles.has(role)),'COMPILER_GRAPH_INCOMPLETE');
 return {schemaVersion:1,status:'BUILD_REFERENCE',executedThisRun:false,actualPassedThisRun:false,compilerInvocationsThisRun:0,
  authority:current,artifacts,context:priorContext,compilerGraph:{path:locate(compilerInputs.path,compilerInputs.sha256),sha256:compilerInputs.sha256,rows:compilerInputs.rows},
  originalJob:{path:path.join(evidence,'job-receipt.json'),sha256:digest(bytes(path.join(evidence,'job-receipt.json')))},
  verifiedArchiveFiles:mapping.files.length,verifiedOriginalResultMembers:observedPaths.size,nonCompilationInputs,
  originalCompilationOnly:true,requestedNativeScenariosMustExecuteFresh:true};
}
