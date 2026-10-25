import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {MODULE,HERE,FILES,AUTHORITY_FILES,spec,need,sha,hash,key,inside,json,save,fingerprint,unchanged,plain,checkedFile,authority,authorityInputs,verifyTraySourceIdentity,verifyTrayCapturedManifest,mutate,validateView,semanticVerdict,verifyReceipt,outputFiles} from './contract.mjs';
export async function prepareCase({repoRoot,caseDir,faultId=null,toolAuthority=null}){
 repoRoot=fs.realpathSync(repoRoot);caseDir=path.resolve(caseDir);plain(caseDir);need(!inside(repoRoot,caseDir)&&!inside(HERE,caseDir),'OUTPUT_MUST_BE_OUTSIDE_INPUT_SOURCE');need(!fs.existsSync(caseDir),'FRESH_CASE_REQUIRED');
 need(faultId===null||faultId===spec.fault.id,'UNKNOWN_FAULT');
 const a=toolAuthority??await authority(repoRoot);
 const sourceInputs=spec.sourceInputs.map(relative=>({relative,...fingerprint(checkedFile(repoRoot,relative),'repository-input')}));
 const sourceIdentityChecks=sourceInputs.map(row=>verifyTraySourceIdentity(row,fs.readFileSync(row.path))); 
 const tools=a.tools.map(row=>fingerprint(row.path,'declared-tool'));
 need(tools.length+22<=3072,'ARTIFACT_BUDGET_EXCEEDED');
 const implementationInputs=[...FILES.map(f=>fingerprint(path.join(HERE,f),'leaf-implementation')),...authorityInputs(repoRoot)];
 fs.mkdirSync(caseDir);const view=path.join(caseDir,'sourceview','repo'),generated=path.join(caseDir,'generated');fs.mkdirSync(view,{recursive:true});fs.mkdirSync(generated);
 for(const row of sourceInputs){const target=path.join(view,row.relative);fs.mkdirSync(path.dirname(target),{recursive:true});fs.copyFileSync(row.path,target);}
 let fault=null;
 if(faultId){const target=path.join(view,spec.fault.source),before=fs.readFileSync(target,'utf8'),after=mutate(before);fs.writeFileSync(target,after);fault={...spec.fault,changedFiles:[spec.fault.source],replacementCount:1,beforeSha256:sha(before),afterSha256:sha(after)};}
 const viewInputs=sourceInputs.map(row=>({relative:row.relative,...fingerprint(path.join(view,row.relative),'owned-source')}));
 need(viewInputs.every((r,i)=>r.sha256===sourceInputs[i].sha256||(faultId&&r.relative===spec.fault.source)),'SOURCE_COPY_MISMATCH');
 fs.mkdirSync(path.join(view,'node_modules'));fs.symlinkSync(path.dirname(path.dirname(a.compilerEntry)),path.join(view,'node_modules','esbuild'),'junction');
 const source=fs.readFileSync(path.join(view,spec.fault.source),'utf8');
 // An independent CLI transform compares code bytes with original transformSync.
 const args=['--loader=ts','--format=cjs','--target=es2022'];
 const reference=spawnSync(a.compilerBinary,args,{cwd:view,input:source,encoding:'utf8',timeout:5000,maxBuffer:2*1024*1024,shell:false,windowsHide:true,env:{SystemRoot:process.env.SystemRoot,WINDIR:process.env.WINDIR,TEMP:caseDir,TMP:caseDir}});
 need(!reference.error&&!reference.signal&&reference.status===0&&reference.stdout.length>0,'REFERENCE_COMPILER_FAILED');
 const referenceFile=path.join(caseDir,'reference.cjs');fs.writeFileSync(referenceFile,reference.stdout,{flag:'wx'});
 const referenceRecord=path.join(caseDir,'reference-compiler.json');save(referenceRecord,{binary:fingerprint(a.compilerBinary,'compiler'),args,exitCode:reference.status,signal:reference.signal,sourceSha256:sha(source),codeSha256:sha(reference.stdout),stderr:reference.stderr,independentCliTransform:true});
 const configFile=path.join(caseDir,'observer-config.json'),trace=path.join(caseDir,'trace.json');
 save(configFile,{view,reads:[...viewInputs,...tools],compilerEntry:a.compilerEntry,compilerBinary:a.compilerBinary,compilerVersion:a.compilerVersion,sourceSha256:sha(source),referenceCode:reference.stdout,generated,trace});
 const plan={schemaVersion:1,repoRoot,caseDir,view,generated,trace,configFile,authority:a,faultId,fault,sourceInputs,viewInputs,sourceIdentityChecks,tools,implementationInputs,controlInputs:[fingerprint(configFile),fingerprint(referenceFile),fingerprint(referenceRecord)],startedAt:new Date().toISOString()};
 validateView(plan);need(unchanged([...sourceInputs,...tools,...implementationInputs]),'PREPARE_INPUT_CHANGED');save(path.join(caseDir,'plan.json'),plan);plan.controlInputs.push(fingerprint(path.join(caseDir,'plan.json')));return plan;
}
export function executeCase(plan){
 const config=json(plan.configFile);need(key(config.compilerBinary)===key(plan.authority.compilerBinary)&&key(config.compilerEntry)===key(plan.authority.compilerEntry),'CASE_AUTHORITY_CHANGED');
 validateView(plan);need(unchanged([...plan.sourceInputs,...plan.tools,...plan.implementationInputs,...plan.controlInputs]),'PRE_EXECUTION_INPUT_CHANGED');need(!fs.existsSync(plan.trace),'FRESH_TRACE_REQUIRED');
 const args=['--no-addons','--require',path.join(HERE,'observe.cjs'),path.join(plan.view,spec.sourceInputs[0])];
 const env={SystemRoot:process.env.SystemRoot,WINDIR:process.env.WINDIR,TEMP:plan.caseDir,TMP:plan.caseDir,ESBUILD_WORKER_THREADS:'0',ESBUILD_BINARY_PATH:plan.authority.compilerBinary,YMCC_TRAY_CONFIG:plan.configFile};
 const start=Date.now(),raw=spawnSync(process.execPath,args,{cwd:plan.view,env,encoding:'utf8',shell:false,windowsHide:true,timeout:spec.timeoutMs,maxBuffer:2*1024*1024});
 const child={exitCode:raw.status,signal:raw.signal,error:raw.error?{code:raw.error.code,message:raw.error.message}:null,durationMs:Date.now()-start,args,node:fingerprint(process.execPath,'node-executable')};
 fs.writeFileSync(path.join(plan.caseDir,'stdout.txt'),raw.stdout??'',{flag:'wx'});fs.writeFileSync(path.join(plan.caseDir,'stderr.txt'),raw.stderr??'',{flag:'wx'});save(path.join(plan.caseDir,'child.json'),child);
 return evaluateCase(plan,child);
}
export function evaluateCase(plan,child){
 const reasons=[],check=(ok,code)=>{if(!ok)reasons.push(code);return ok;};
 try{validateView(plan);}catch(e){reasons.push(e.message);}
 check(unchanged(plan.sourceInputs),'REPOSITORY_SOURCE_CHANGED');check(unchanged(plan.tools),'DECLARED_TOOL_CHANGED');check(unchanged(plan.implementationInputs),'IMPLEMENTATION_CHANGED');check(unchanged(plan.controlInputs),'CONTROL_INPUT_CHANGED');
 check(!child.error&&!child.signal&&Number.isInteger(child.exitCode),'CHILD_INCOMPLETE');
 let trace=null;try{trace=json(plan.trace);}catch{reasons.push('TRACE_MISSING');}
 const stdout=fs.readFileSync(path.join(plan.caseDir,'stdout.txt'),'utf8'),stderr=fs.readFileSync(path.join(plan.caseDir,'stderr.txt'),'utf8');
 if(trace){
  check(trace.finished&&trace.naturalExit&&trace.exitCode===child.exitCode&&!trace.forbidden.length,'OBSERVER_OR_NATURAL_EXIT_FAILURE');
  check(key(trace.node.path)===key(process.execPath)&&trace.node.version===process.version,'ACTUAL_NODE_MISMATCH');
  const count=plan.faultId?spec.negativeVmCount:spec.positiveVmCount;
  check(trace.transforms.length===count&&trace.vm.length===count&&trace.compilers.length===count,'ACTUAL_EXECUTION_COUNT');
  check(trace.transforms.every(r=>r.completed&&r.referenceEqual&&hash(r.path)===r.sha256),'GENERATED_CODE_CHANGED');
  check(trace.vm.every((r,i)=>r.completed&&r.sha256===trace.transforms[i]?.sha256&&JSON.stringify(r.memoryRequires)===JSON.stringify(['./api','./settingsRepository'])),'ORIGINAL_VM_MEMORY_REQUIRE_CONTRACT');
  check(trace.compilers.every(r=>r.completed&&key(r.path)===key(plan.authority.compilerBinary)&&r.sha256===plan.tools.find(t=>key(t.path)===key(r.path))?.sha256),'ACTUAL_COMPILER_EXEC_MISSING');
  check(trace.cjs.length===1&&trace.cjs[0].completed&&key(trace.cjs[0].path)===key(plan.authority.compilerEntry),'ACTUAL_CJS_LOAD_MISSING');
  const required=plan.viewInputs.filter(r=>r.relative!== 'package.json'&&(!plan.faultId||r.relative!=='native/main.cpp'));
  check(required.every(row=>trace.reads.some(r=>key(r.realpath)===key(row.path)&&r.sha256===row.sha256)),'ACTUAL_SOURCE_READ_MISSING');
  check(JSON.stringify(fs.readdirSync(plan.generated).sort())===JSON.stringify(trace.transforms.map(r=>path.basename(r.path)).sort()),'GENERATED_MEMBERS_CHANGED');
 }
 const accepted=semanticVerdict(plan,child,trace,stdout,stderr,reasons.length===0);if(!accepted&&!reasons.length)reasons.push('ORIGINAL_SEMANTIC_CONTRACT_FAILED');
 const outputAfter=outputFiles(plan.caseDir);
 return {schemaVersion:1,kind:plan.faultId?'semantic-negative':'original-positive',accepted,semanticDetected:!!plan.faultId&&accepted,controlFailures:reasons,plan,child,originalResult:{passedNames:stdout.split(/\r?\n/).filter(l=>l.startsWith('PASS: ')).map(l=>l.slice(6)),assertions:trace?.assertions??[],allOriginal11Passed:!plan.faultId&&accepted},actualLoaded:{sourceReads:trace?.reads.filter(r=>r.role==='owned-source')??[],toolReads:trace?.reads.filter(r=>r.role==='declared-tool')??[],cjs:trace?.cjs??[],compilerExec:trace?.compilers??[],vm:trace?.vm??[],generated:trace?.transforms??[],osDllGraph:false},outputAfter};
}
export function validateFixedRunBinding({fixedRunBinding,snapshot,toolAuthority:a}){
 if(!fixedRunBinding)return null;
 need(snapshot?.manifest?.path&&a?.proof?.bindingManifestFile,'FIXED_RUN_SNAPSHOT_AND_TOOL_BINDING_REQUIRED');
 const fixed=json(fixedRunBinding),manifest=json(snapshot.manifest.path),binding=json(a.proof.bindingManifestFile),graph=binding.graph;
 const manifestSha256=hash(snapshot.manifest.path),bindingManifestSha256=hash(a.proof.bindingManifestFile);
 need(snapshot.creationStatus==='PASS'&&manifest.status==='PASS'&&Array.isArray(manifest.files)&&manifest.files.length>0&&fixed.creationStatus==='PASS'&&fixed.captureComplete===true&&fixed.files===manifest.files.length&&snapshot.fileCount===manifest.files.length&&fixed.manifestSha256===manifestSha256&&snapshot.manifest.sha256===manifestSha256,'FIXED_RUN_SOURCE_PROVENANCE_MISMATCH');
 need(graph&&fixed.bindingManifestSha256===bindingManifestSha256&&a.proof.expectedBindingSha256===bindingManifestSha256&&sha(JSON.stringify(graph))===a.graphSha256&&binding.graphBeforeSha256===a.graphSha256&&binding.graphAfterSha256===a.graphSha256&&fixed.bindingGraphSha256===a.graphSha256&&fixed.bindingPackageCount===graph.packageCount&&graph.packageCount===a.packageCount&&fixed.bindingFiles===graph.fileCount&&graph.fileCount===a.fileCount,'FIXED_RUN_TOOL_PROVENANCE_MISMATCH');
 return {identity:fingerprint(fixedRunBinding),captureStatus:'PASS',manifestSha256,bindingManifestSha256,files:manifest.files.length,packages:graph.packageCount,tools:graph.fileCount};
}
export async function runSuite({repoRoot,outDir,currentRoot=null,fixedRunBinding=null}){
 repoRoot=fs.realpathSync(repoRoot);outDir=path.resolve(outDir);plain(outDir);need(!inside(repoRoot,outDir)&&!inside(HERE,outDir),'OUTPUT_MUST_BE_OUTSIDE_INPUT_SOURCE');need(!fs.existsSync(outDir),'FRESH_SUITE_REQUIRED');fs.mkdirSync(path.dirname(outDir),{recursive:true});
 const start=Date.now(),a=await authority(repoRoot),topInputs=[...spec.sourceInputs.map(f=>fingerprint(checkedFile(repoRoot,f))),...FILES.map(f=>fingerprint(path.join(HERE,f))),...authorityInputs(repoRoot),...a.tools.map(r=>fingerprint(r.path))];
 let snapshot=null,fixed=null;
 const mf=path.join(repoRoot,'.ymcc-source-snapshot/manifest.json');
 if(fs.existsSync(mf)){const m=json(mf),binding=json(a.proof.bindingManifestFile);need(hash(a.proof.bindingManifestFile)===a.proof.expectedBindingSha256,'CAPTURE_BINDING_IDENTITY_REQUIRED');const frozen=verifyTrayCapturedManifest(m,mf,a,binding);const consumed=spec.sourceInputs.map(f=>{const r=m.files.find(r=>r.path===f);need(r?.sha256===hash(checkedFile(repoRoot,f)),'SNAPSHOT_INPUT_MISMATCH');return r;});snapshot={manifest:fingerprint(mf),...frozen,fileCount:m.files.length,consumedRows:consumed};topInputs.push(fingerprint(mf));}
 if(fixedRunBinding){fixed=validateFixedRunBinding({fixedRunBinding,snapshot,toolAuthority:a});topInputs.push(fingerprint(fixedRunBinding));}
 const currentBefore=currentRoot?spec.sourceInputs.map(f=>({relative:f,...fingerprint(checkedFile(currentRoot,f))})):null;
 fs.mkdirSync(outDir);const receipts=[];
 for(const faultId of [null,spec.fault.id]){const plan=await prepareCase({repoRoot,caseDir:path.join(outDir,faultId||'positive'),faultId,toolAuthority:a});const receipt=executeCase(plan);save(path.join(plan.caseDir,'receipt.json'),receipt);receipts.push(receipt);}
 const authorityAfter=await authority(repoRoot);
 const toolScopeUnchanged=JSON.stringify(a)===JSON.stringify(authorityAfter);
 const ok=toolScopeUnchanged&&receipts.every(verifyReceipt)&&unchanged(topInputs)&&(!currentBefore||unchanged(currentBefore));
 const result={schemaVersion:1,id:spec.id,status:ok?'PASS':'FAIL',ok,active:true,joint:false,cpuPower:null,repoRoot,outDir,snapshot,fixed,sourceHashes:spec.sourceInputs.map(f=>({path:f,sha256:hash(checkedFile(repoRoot,f))})),artifactInputs:a.tools,toolAuthority:a,toolScopeUnchanged,artifactBudget:{declared:a.tools.length,generatedCode:22,total:a.tools.length+22,limit:3072},actualLoaded:receipts.map(r=>r.actualLoaded),receipts,inputBefore:topInputs,inputUnchanged:unchanged(topInputs),currentIdentity:currentBefore?{root:currentRoot,before:currentBefore,unchanged:unchanged(currentBefore),matchesConsumedSource:currentBefore.every(r=>hash(checkedFile(repoRoot,r.relative))===r.sha256)}:null,durationMs:Date.now()-start,scope:'Original tray preference software transaction and final native/main.cpp source contract',boundary:{physicalTrayValidated:false,hostStarted:false,installedAppStarted:false,systemSettingsModified:false,registryOrDeviceAccess:false,networkUsed:false,workerThreadsUsed:false,osLoadedDllGraph:false,allAppLifecycleValidated:false,wholeGlobalAccepted:false,sourceApprovalGranted:false,jointUserTestingReady:false,cpuPowerMeasurement:null,confinement:'Pinned trusted producer and compiler under exact observer contracts; --no-addons, no worker or arbitrary child-process access; no claim of an adversarial OS sandbox'}};
 save(path.join(outDir,'result.json'),result);return result;
}
export function parseOptions(args){const options={};for(let i=0;i<args.length;i+=2){need(['--repo','--evidence-dir','--current-root','--fixed-run-binding'].includes(args[i])&&args[i+1]&&!options[args[i]],'INVALID_ARGUMENT');options[args[i]]=args[i+1];}need(options['--repo']&&options['--evidence-dir'],'REPO_AND_EVIDENCE_REQUIRED');return{repoRoot:options['--repo'],outDir:options['--evidence-dir'],currentRoot:options['--current-root'],fixedRunBinding:options['--fixed-run-binding']};}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){
 try{const r=await runSuite(parseOptions(process.argv.slice(2)));console.log(JSON.stringify({status:r.status,receipts:r.receipts.map(r=>({kind:r.kind,accepted:r.accepted,failures:r.controlFailures,passes:r.originalResult.passedNames.length})),durationMs:r.durationMs}));process.exitCode=r.ok?0:1;}catch(e){console.error(JSON.stringify({status:'TOOL_CONTROL_FAILURE',semanticDetected:false,error:e.stack}));process.exitCode=2;}
}
