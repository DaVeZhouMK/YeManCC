import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {MODULE,FILES,HERE,spec,sha,shaFile,need,json,save,inside,checkedFile,fingerprint,unchanged,extractVm,mutate,semanticVerdict,assertPlainDestination} from './contract.mjs';
export function prepareCase({repoRoot,caseDir,faultId=null}) {
  repoRoot=fs.realpathSync(repoRoot);caseDir=path.resolve(caseDir);
  need(!inside(repoRoot,caseDir),'OUTPUT_MUST_BE_OUTSIDE_SOURCE');assertPlainDestination(caseDir);
  fs.mkdirSync(caseDir,{recursive:false});
  const tool=json(path.join(HERE,'toolchain-inputs.json'));
  need(tool.node.sha256===shaFile(process.execPath)&&tool.node.version===process.version,'NODE_IDENTITY_MISMATCH');
  const sourceInputs=spec.sourceInputs.map(relative=>({relative,...fingerprint(checkedFile(repoRoot,relative))}));
  need(sourceInputs[0].sha256===spec.producerSha256,'PRODUCER_IDENTITY_MISMATCH');
  const tools=[fingerprint(process.execPath),...FILES.map(f=>fingerprint(path.join(HERE,f)))];
  // Exactly two levels above sourceview/repo is this owned case directory.
  const view=path.join(caseDir,'sourceview','repo');fs.mkdirSync(view,{recursive:true});
  for(const row of sourceInputs){const dest=path.join(view,row.relative);fs.mkdirSync(path.dirname(dest),{recursive:true});fs.copyFileSync(row.path,dest);}
  let fault=null;
  if(faultId){const file=path.join(view,spec.sourceInputs[1]),before=fs.readFileSync(file,'utf8'),after=mutate(before,faultId);fs.writeFileSync(file,after);fault={...spec.faults.find(f=>f.id===faultId),source:spec.sourceInputs[1],beforeSha256:sha(before),afterSha256:sha(after),changedFiles:1};}
  const viewInputs=sourceInputs.map(row=>({relative:row.relative,...fingerprint(path.join(view,row.relative))}));
  need(viewInputs.every((row,i)=>row.sha256===sourceInputs[i].sha256 || (faultId&&i===1)),'SOURCEVIEW_COPY_NOT_EXACT');
  const vm=extractVm(fs.readFileSync(path.join(view,spec.sourceInputs[1]),'utf8'));
  save(path.join(caseDir,'vm-inputs.json'),vm);
  const report=path.join(caseDir,'Build','CustomSteamLibrary','ui-datachain-summary.json'),trace=path.join(caseDir,'trace.json');
  need(path.resolve(view,'../../Build')===path.join(caseDir,'Build'),'ORIGINAL_BUILD_GUARD_LAYOUT_INVALID');
  const configFile=path.join(caseDir,'observer-config.json');
  save(configFile,{reads:viewInputs,vm,report,trace});
  const plan={schemaVersion:1,repoRoot,caseDir,view,report,trace,configFile,faultId,fault,sourceInputs,viewInputs,tools,vm,configIdentity:fingerprint(configFile),vmIdentity:fingerprint(path.join(caseDir,'vm-inputs.json')),outputBefore:{reportExists:fs.existsSync(report),traceExists:fs.existsSync(trace)},startedAt:new Date().toISOString()};
  save(path.join(caseDir,'plan.json'),plan);return plan;
}
export function executeCase(plan) {
  need(unchanged([...plan.sourceInputs,...plan.viewInputs,...plan.tools,plan.configIdentity,plan.vmIdentity]),'PRE_EXECUTION_INPUT_CHANGED');
  need(!fs.existsSync(plan.report)&&!fs.existsSync(plan.trace),'FRESH_OUTPUT_REQUIRED');
  const started=Date.now();
  const env={SystemRoot:process.env.SystemRoot,WINDIR:process.env.WINDIR,TEMP:plan.caseDir,TMP:plan.caseDir,YMCC_WORKSPACE_CONFIG:plan.configFile};
  const args=['--experimental-permission','--allow-fs-read='+plan.caseDir,'--allow-fs-read='+HERE,'--allow-fs-write='+plan.caseDir,'--require',path.join(HERE,'observe.cjs'),path.join(plan.view,spec.sourceInputs[0]),plan.report];
  const child=spawnSync(process.execPath,args,{cwd:plan.view,env,shell:false,windowsHide:true,encoding:'utf8',timeout:spec.timeoutMs,maxBuffer:2*1024*1024});
  const childResult={exitCode:child.status,signal:child.signal,error:child.error?{code:child.error.code,message:child.error.message}:null,durationMs:Date.now()-started,args,node:fingerprint(process.execPath)};
  fs.writeFileSync(path.join(plan.caseDir,'stdout.txt'),child.stdout??'',{flag:'wx'});
  fs.writeFileSync(path.join(plan.caseDir,'stderr.txt'),child.stderr??'',{flag:'wx'});
  save(path.join(plan.caseDir,'child.json'),childResult);
  return evaluateCase(plan,childResult);
}
export function evaluateCase(plan,child) {
  const reasons=[];
  const check=(value,reason)=>{if(!value)reasons.push(reason);return value;};
  check(unchanged(plan.sourceInputs),'SOURCE_CHANGED');check(unchanged(plan.viewInputs),'VM_SOURCE_CHANGED');check(unchanged(plan.tools),'TOOL_CHANGED');
  check(unchanged([plan.configIdentity,plan.vmIdentity]),'VM_CONFIG_CHANGED');
  check(!plan.outputBefore.reportExists&&!plan.outputBefore.traceExists,'OUTPUT_NOT_FRESH');
  check(!child.error&&!child.signal&&Number.isInteger(child.exitCode),'CHILD_EXECUTION_INCOMPLETE');
  let report=null,trace=null;
  try{report=json(plan.report);trace=json(plan.trace);}catch{reasons.push('FRESH_REPORT_OR_TRACE_MISSING');}
  if(trace){
    check(trace.finished===true&&!trace.forbidden.length,'OBSERVER_CONTROL_FAILURE');
    check(trace.node.path===process.execPath&&trace.node.version===process.version,'ACTUAL_NODE_MISMATCH');
    check(JSON.stringify(trace.vm.map(v=>({sha256:v.sha256,code:v.code,completed:v.completed})))===JSON.stringify(plan.vm.map(v=>({sha256:v.sha256,code:v.code,completed:true}))),'ACTUAL_VM_INPUT_MISMATCH');
    check(plan.viewInputs.every(row=>trace.reads.some(r=>path.resolve(r.path)===row.path&&r.sha256===row.sha256)),'SOURCE_READ_NOT_OBSERVED');
    check(trace.reads.every(r=>plan.viewInputs.some(row=>row.path===path.resolve(r.path)&&row.sha256===r.sha256)),'UNREVIEWED_READ');
    check(trace.writes.length===1&&trace.writes[0]===plan.report,'ORIGINAL_OUTPUT_WRITE_NOT_OBSERVED');
    check(trace.nonBuiltinModulesUsed.length===0,'NON_BUILTIN_TOOL_USED');
  }
  if(report){
    try{check(fs.readFileSync(path.join(plan.caseDir,'stdout.txt'),'utf8')===fs.readFileSync(plan.report,'utf8')+'\n','RAW_REPORT_STDOUT_DIFFER');}catch{reasons.push('RAW_STDOUT_MISSING');}
  }
  const accepted=semanticVerdict({report,trace,exitCode:child.exitCode,faultId:plan.faultId,controlsOk:reasons.length===0});
  if(!accepted&&reasons.length===0)reasons.push('ORIGINAL_CASE_OR_EXPECTED_ASSERTION_CONTRACT_FAILED');
  const files=['stdout.txt','stderr.txt','child.json','plan.json','observer-config.json','vm-inputs.json',...(fs.existsSync(plan.report)?['Build/CustomSteamLibrary/ui-datachain-summary.json']:[]),...(fs.existsSync(plan.trace)?['trace.json']:[])];
  return {schemaVersion:1,kind:plan.faultId?'semantic-negative':'original-positive',faultId:plan.faultId,accepted,semanticDetected:!!plan.faultId&&accepted,controlStatus:reasons.length?'FAIL':'PASS',controlFailures:reasons,caseDir:plan.caseDir,originalResult:report?{allPassed:report.allPassed,caseCount:report.caseCount,failed:report.cases.filter(c=>!c.passed)}:null,child,outputBefore:plan.outputBefore,outputAfter:files.map(f=>fingerprint(path.join(plan.caseDir,f))),sourceBefore:plan.sourceInputs,sourceAfter:plan.sourceInputs.map(r=>fingerprint(r.path)),vmInputBefore:plan.viewInputs,vmInputAfter:plan.viewInputs.map(r=>fingerprint(r.path)),toolBefore:plan.tools,toolAfter:plan.tools.map(r=>fingerprint(r.path)),fault:plan.fault};
}
export function runSuite({repoRoot,outDir,currentRoot=null}) {
  repoRoot=fs.realpathSync(repoRoot);outDir=path.resolve(outDir);
  need(!inside(repoRoot,outDir),'OUTPUT_MUST_BE_OUTSIDE_SOURCE');assertPlainDestination(outDir);
  fs.mkdirSync(path.dirname(outDir),{recursive:true});fs.mkdirSync(outDir);
  const start=Date.now(),topInputs=[...spec.sourceInputs.map(f=>fingerprint(checkedFile(repoRoot,f))),...FILES.map(f=>fingerprint(path.join(HERE,f))),fingerprint(process.execPath)];
  const manifestFile=path.join(repoRoot,'.ymcc-source-snapshot','manifest.json');let snapshot=null;
  if(fs.existsSync(manifestFile)){
    const manifest=json(manifestFile);
    const sameRoot=typeof manifest.snapshotRoot==='string'&&path.resolve(manifest.snapshotRoot).toLowerCase()===repoRoot.toLowerCase();
    need(['PASS','FAIL'].includes(manifest.status)&&manifest.kind==='ymcc-fixed-source-snapshot'&&sameRoot&&Array.isArray(manifest.files)&&manifest.files.length>0,'COMPLETED_SNAPSHOT_REQUIRED');
    const rows=spec.sourceInputs.map(relative=>{const matches=manifest.files.filter(r=>r.path===relative),row=matches[0];need(matches.length===1&&row&&shaFile(checkedFile(repoRoot,relative))===row.sha256,'SNAPSHOT_SOURCE_IDENTITY_MISMATCH');return row;});
    snapshot={...fingerprint(manifestFile),status:manifest.status,sourceRoot:manifest.sourceRoot,snapshotRoot:manifest.snapshotRoot,completeFileCount:manifest.files.length,consumedRows:rows,wholeSnapshotReverifiedByThisModule:false};topInputs.push(fingerprint(manifestFile));
  }
  const currentBefore=currentRoot?spec.sourceInputs.map(f=>({relative:f,...fingerprint(checkedFile(currentRoot,f))})):null;
  const receipts=[];
  for(const faultId of [null,...spec.faults.map(f=>f.id)]){
    const plan=prepareCase({repoRoot,caseDir:path.join(outDir,faultId||'positive'),faultId});
    const receipt=executeCase(plan);save(path.join(plan.caseDir,'receipt.json'),receipt);receipts.push(receipt);
  }
  const currentAfter=currentRoot?spec.sourceInputs.map(f=>({relative:f,...fingerprint(checkedFile(currentRoot,f))})):null;
  const ok=receipts.every(r=>r.accepted)&&unchanged(topInputs)&&(currentBefore===null||unchanged(currentBefore));
  const result={schemaVersion:1,status:ok?'PASS':'FAIL',ok,scope:'library.workspace-ui normalization and artwork original Node VM plus original HTML checks',repoRoot,outDir,snapshot,currentIdentity:currentBefore?{root:currentRoot,before:currentBefore,after:currentAfter,unchanged:unchanged(currentBefore),matchesConsumedSource:currentBefore.every((r,i)=>r.sha256===topInputs[i].sha256)}:null,sourceHashes:spec.sourceInputs.map(f=>({path:f,sha256:shaFile(checkedFile(repoRoot,f))})),artifactInputs:[fingerprint(process.execPath)],sourceInputs:spec.sourceInputs,ownedOutputs:receipts.map(r=>r.caseDir),inputBefore:topInputs,inputAfter:topInputs.map(r=>fingerprint(r.path)),inputUnchanged:unchanged(topInputs),receipts,timeoutMs:spec.timeoutMs,durationMs:Date.now()-start,boundary:{hcFixed24Reopened:false,nativeWorkspace80OperationsCovered:false,browserUsed:false,esbuildUsed:false,vueUsed:false,deviceOrSteamStarted:false,realLibraryFilesRead:false,wholeGlobalAccepted:false,currentSourceApprovalGranted:false}};
  save(path.join(outDir,'result.json'),result);return result;
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){
  const args=process.argv.slice(2),options={};
  for(let i=0;i<args.length;i+=2){need(['--repo','--evidence-dir','--current-root'].includes(args[i])&&args[i+1],'INVALID_ARGUMENT');options[args[i]]=args[i+1];}
  try{const result=runSuite({repoRoot:options['--repo']||path.resolve(HERE,'../../../..'),outDir:options['--evidence-dir']||process.env.YMCC_SIM_EVIDENCE_DIR,currentRoot:options['--current-root']||null});console.log(JSON.stringify({status:result.status,receipts:result.receipts.map(r=>({kind:r.kind,fault:r.faultId,accepted:r.accepted,failures:r.controlFailures})),durationMs:result.durationMs}));process.exitCode=result.ok?0:1;}catch(error){console.error(JSON.stringify({status:'TOOL_CONTROL_FAILURE',semanticDetected:false,error:error.stack}));process.exitCode=2;}
}
