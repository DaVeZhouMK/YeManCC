/** Explicit original fixed-C++ software references; no executor/cache fallback. */
import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';
import {fileURLToPath} from 'node:url';
import {runWithWallBudget,readBudgetWorker,validateWallBudget} from './execution-budget.mjs';
const SELF=fileURLToPath(import.meta.url),REPO=path.resolve(import.meta.dirname,'../../..');
const CORE=path.resolve(REPO,'../../../Testing/YMCC');
const REFERENCE=path.join(REPO,'tools/ymcc_sim/global/controller-coordinator-pipe/r3-terminal26-artifact-reference.mjs');
const IDS=Object.freeze({
 'controller.coordinator-native-managed-pipe':'positive',
 'controller.coordinator-native-managed-pipe.cancellation-negative':'cancellation',
 'controller.coordinator-native-managed-pipe.ps-ownership-negative':'psOwnership',
 'controller.coordinator-native-managed-pipe.ps-binding-negative':'psBinding',
});
const SCOPE='FIXED_CPP_ARTIFACT_SOFTWARE_ONLY';
const inside=(root,file)=>{const r=path.relative(root.toLowerCase(),file.toLowerCase());return r===''||(!path.isAbsolute(r)&&r!=='..'&&!r.startsWith('..'+path.sep));};
const need=(v,msg)=>{if(!v)throw Error('FIXED_CPP_FRONTDOOR_'+msg);};
function ordinary(file){
 need(typeof file==='string'&&/^[A-Za-z]:/.test(file)&&(file[2]==='/'||file.charCodeAt(2)===92)&&![...file].some(c=>c.charCodeAt(0)<32)&&!file.slice(2).includes(':'),'ORDINARY_ABSOLUTE_PATH');
 need(file.slice(3).split(String.fromCharCode(92)).join('/').split('/').every(s=>s&&s!=='.'&&s!=='..'&&!/[. ]$/.test(s)),'PATH_ALIAS');return path.resolve(file);
}
function parentChain(file){
 let cursor=file;while(!fs.existsSync(cursor))cursor=path.dirname(cursor);
 for(;;){need(!fs.lstatSync(cursor).isSymbolicLink(),'REDIRECTED_PATH');if(path.dirname(cursor)===cursor)break;cursor=path.dirname(cursor);}
}
function parse(argv){
 need(!process.env.NODE_OPTIONS&&!process.env.NODE_PATH&&process.execArgv.length===0,'NODE_PRELOAD');
 need(argv.length===10,'EXACT_ARGUMENTS');const o={};
 for(let i=0;i<argv.length;i+=2){const k=argv[i],v=argv[i+1];need(['--test','--intake','--intake-sha256','--out-dir','--budget-ms'].includes(k)&&!Object.hasOwn(o,k)&&v&&!v.startsWith('--'),'ARGUMENTS');o[k]=v;}
 need(Object.keys(o).length===5,'ARGUMENT_SET');const tests=o['--test'].split(',');
 need(tests.length>0&&tests.length<=4&&new Set(tests).size===tests.length&&tests.every(id=>Object.hasOwn(IDS,id)),'EXPLICIT_ORIGINAL_IDS');
 need(/^[a-f0-9]{64}$/.test(o['--intake-sha256']),'INTAKE_SHA');
 const intake=ordinary(o['--intake']),outDir=ordinary(o['--out-dir']);
 need(inside(path.join(CORE,'Evidence'),intake)&&inside(path.join(CORE,'Evidence'),outDir),'CORE_EVIDENCE_ONLY');
 parentChain(intake);parentChain(outDir);
 need(/^\d+$/.test(o['--budget-ms']),'BUDGET');const budgetMs=validateWallBudget(Number(o['--budget-ms']));
 return {tests,intakePin:{path:intake,sha256:o['--intake-sha256']},outDir,budgetMs};
}
const pin=file=>{const b=fs.readFileSync(file);return {path:file,bytes:b.length,sha256:createHash('sha256').update(b).digest('hex')};};
const fresh=(file,value)=>{const fd=fs.openSync(file,'wx');try{fs.writeFileSync(fd,JSON.stringify(value,null,2)+'\n');fs.fsyncSync(fd);}finally{fs.closeSync(fd);}return pin(file);};
const limited=()=>({scope:SCOPE,cacheAcceptance:false,genericCacheQualified:false,cacheIndexWritten:false,historicalJavaScriptGeneratorClosureComplete:false,toolClosureComplete:false,currentPrimaryAccepted:false,fullSourceCurrent:false,fullAcceptance:false,fullProductAcceptance:false,nativeProcessesStartedThisRun:0,compilerInvocationsThisRun:0});
async function worker(context){
 const options=parse(context.args);need(options.budgetMs===context.budgetMs,'REQUESTED_BUDGET_BINDING');
 const rows=[],cases=path.join(context.runDir,'cases');fs.mkdirSync(cases);
 // The verifier is loaded only inside the original owned-budget worker.
 const {verifyTerminal26ArtifactReference}=await import('../global/controller-coordinator-pipe/r3-terminal26-artifact-reference.mjs');
 let stopped=false;
 for(const id of options.tests){
  const role=IDS[id];let row;
  if(stopped||context.remainingMs()<=0){row={id,role,status:'NOT_RUN_NO_VALID_REFERENCE',reason:stopped?'EARLIER_REFERENCE_REJECTED':'PARENT_BUDGET_EXHAUSTED',executedThisRun:false,actualPassedThisRun:false};}
  else{
   try{
    const result=verifyTerminal26ArtifactReference({role,intakePin:options.intakePin,repo:REPO});
    need(context.remainingMs()>0,'REFERENCE_EXCEEDED_PARENT_BUDGET');
    need(result.status==='CACHED_REFERENCE'&&result.id===id&&result.role===role&&result.scope===SCOPE&&result.executedThisRun===0&&result.actualPassedThisRun===0&&result.nativeProcessesStartedThisRun===0&&result.compilerInvocationsThisRun===0&&result.genericCacheQualified===false&&result.historicalJavaScriptGeneratorClosureComplete===false,'REFERENCE_ENVELOPE');
    const caseDir=path.join(cases,id);fs.mkdirSync(caseDir);
    row={id,role,status:'CACHED_REFERENCE',reference:fresh(path.join(caseDir,'reference.json'),result),originalIntake:result.originalIntake,originalParent:result.originalParent,originalLeaf:result.originalLeaf,executedThisRun:false,actualPassedThisRun:false};
   }catch(error){row={id,role,status:'NOT_RUN_NO_VALID_REFERENCE',reason:String(error.message??error),executedThisRun:false,actualPassedThisRun:false};stopped=true;}
  }
  rows.push(row);
 }
 const cachedReferences=rows.filter(r=>r.status==='CACHED_REFERENCE').length;
 const result={schemaVersion:1,status:cachedReferences===rows.length?'COMPLETE_WITH_CACHED_REFERENCES':'NOT_RUN_NO_VALID_REFERENCE',runDir:context.runDir,...limited(),requestedIntake:options.intakePin,summary:{selected:rows.length,executedThisRun:0,actualPassedThisRun:0,cachedReferences},rows,sourceInputs:[pin(SELF),pin(REFERENCE),pin(path.join(CORE,'Run.ps1'))],currentParentResourceProof:'execution-budget.json and owned-job.json produced after this worker exits; separate from rows[].originalParent'};
 fresh(path.join(context.runDir,'result.json'),result);console.log(JSON.stringify({status:result.status,runDir:context.runDir,...result.summary}));return result.status==='COMPLETE_WITH_CACHED_REFERENCES'?0:10;
}
async function main(argv){
 const options=parse(argv);
 const invocation=await runWithWallBudget(argv,{repoRoot:REPO,coreRoot:CORE,workerEntry:SELF,budgetMs:options.budgetMs,outDir:options.outDir,additionalInputs:[REFERENCE,path.join(CORE,'Run.ps1')]});
 const current=invocation.result,job=current.ownedJob;
 const resourceReady=current.resourceZeroVerified===true&&current.ownedJobVerified===true&&job?.terminationRequested===false&&current.sourceIdentity?.status==='MATCH'&&current.sourceReadErrors?.length===0&&current.sourceDrift?.length===0;
 const accepted=invocation.exitCode===0&&current.status==='COMPLETE_WITH_CACHED_REFERENCES'&&resourceReady;
 fresh(path.join(current.runDir,'reference-frontdoor.json'),{schemaVersion:1,status:accepted?'COMPLETE_WITH_CACHED_REFERENCES':'NOT_RUN_NO_VALID_REFERENCE',...limited(),runDir:current.runDir,executedThisRun:0,actualPassedThisRun:0,currentParent:{executionBudget:pin(path.join(current.runDir,'execution-budget.json')),workerResult:fs.existsSync(path.join(current.runDir,'result.json'))?pin(path.join(current.runDir,'result.json')):null,ownedJob:job,resourceZeroVerified:current.resourceZeroVerified===true,ownedJobVerified:current.ownedJobVerified===true},referencedOriginalRuns:{location:'cases/<original-id>/reference.json: originalIntake, originalParent, originalLeaf and their pinned raw/Job evidence',resourceProofScope:'Original parent/raw Job receipts remain inside each case reference; never this current parent.'}});
 return accepted?0:(invocation.exitCode||10);
}
if(process.argv[1]&&path.resolve(process.argv[1])===SELF){
 const args=process.argv.slice(2);
 Promise.resolve().then(()=>{
  if(args[0]==='--budget-worker'){
   need(args.length===3,'WORKER_ARGUMENTS');return worker(readBudgetWorker(args[1],args[2],{coreRoot:CORE,repoRoot:REPO}));
  }
  return main(args);
 }).then(code=>process.exitCode=code).catch(error=>{console.error('NOT_RUN_NO_VALID_REFERENCE '+String(error.message??error));process.exitCode=10;});
}
