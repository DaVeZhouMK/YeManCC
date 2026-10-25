import fs from 'node:fs';
import path from 'node:path';
import {runProcess} from '../runner.mjs';
import {ADAPTER_TIMEOUT_MS} from './contracts.mjs';
import {hashFile,readJson,ownedFile,ownedTree} from './evidence.mjs';
import {MATRIX_PROBE,MATRIX_BUDGET_MS,MATRIX_CHILD_TIMEOUT_MS,MATRIX_OUTPUT_LIMIT,sha,assertMatrixProcess,matrixCheckerArguments,createMatrixFaults,verifyMatrixEvidence} from './matrix-evidence.mjs';

/** Called only after the unchanged complete producer/report/evidence guards pass. */
export async function runMatrixSupplement({repo,out,generated,source,sourceBytes,sourceInputs,externalInputs,tools,generatedClosure,env,adapterStartedAt}){
 if(!path.isAbsolute(out)||path.parse(out).root.toLowerCase()!=='d:\\'||path.resolve(out)!==path.join(path.dirname(path.resolve(generated)),'q')||!Number.isFinite(adapterStartedAt))throw Error('MATRIX_D_OWNED_CASE_REQUIRED');
 if(fs.existsSync(out))throw Error('MATRIX_FRESH_OUTPUT_REQUIRED');fs.mkdirSync(out);
 const start=Date.now(),deadline=Math.min(start+MATRIX_BUDGET_MS,adapterStartedAt+ADAPTER_TIMEOUT_MS-10000);
 const aggregate={schemaVersion:1,kind:'motion-matrix-binding-supplement',status:'FAIL',startedAt:new Date(start).toISOString(),budget:{supplementMs:MATRIX_BUDGET_MS,childMaxMs:MATRIX_CHILD_TIMEOUT_MS,originalProducerMs:840000,unchangedAdapterMs:ADAPTER_TIMEOUT_MS,reservedFinishMs:10000,availableMs:Math.max(0,deadline-start)},positive:null,faults:[],declaredToolInputs:tools.files,toolAllowlistMeaning:'All inherited members retained; only per-configuration sourceDependencies rows are observed includes.',sourceHashes:[],fullBusExecuted:false,hardwareIo:false,physicalAcceptance:false,wholeProcessCpuReduction:null};
 const beforeFile=path.join(out,'main.before.cpp');fs.writeFileSync(beforeFile,sourceBytes,{flag:'wx'});
 try{
  if(hashFile(source)!==sha(sourceBytes))throw Error('MATRIX_CURRENT_SOURCE_CHANGED');
  const pins=new Map(externalInputs.map(row=>[path.resolve(row.path).toLowerCase(),row.sha256]));
  for(const row of tools.files)if(pins.get(path.resolve(row.path).toLowerCase())!==row.sha256||hashFile(row.path)!==row.sha256)throw Error('MATRIX_TOOL_PIN_MISMATCH');
  if(path.resolve(process.execPath).toLowerCase()!==path.resolve(tools.nodeExecutable).toLowerCase())throw Error('MATRIX_NODE_NOT_PINNED');
  const oracleSha256=hashFile(ownedFile(generated,'expectations.json'));
  const controls=createMatrixFaults(sourceBytes,fs.readFileSync(path.join(repo,MATRIX_PROBE)));
  fs.writeFileSync(path.join(out,'source-controls.json'),JSON.stringify({schemaVersion:1,source,sourceSha256:sha(sourceBytes),controls:controls.map(({bytes,...control})=>control),generationOnly:true},null,2));
  const execute=async(folder,sourceFile,raw,fault)=>{
   const timeoutMs=Math.min(MATRIX_CHILD_TIMEOUT_MS,deadline-Date.now());if(timeoutMs<1000)throw Error('MATRIX_SUPPLEMENT_BUDGET_EXHAUSTED');
   const caseOut=path.join(folder,'e'),args=matrixCheckerArguments({repo,out:caseOut,source:sourceFile,generated:raw,fault});
   const call=await runProcess(process.execPath,args,{cwd:repo,env,timeoutMs,outputLimit:MATRIX_OUTPUT_LIMIT});
   fs.writeFileSync(path.join(folder,'checker.stdout.log'),call.stdout);fs.writeFileSync(path.join(folder,'checker.stderr.log'),call.stderr);
   const {stdout,stderr,...processProof}=call;
   fs.writeFileSync(path.join(folder,'checker-process.json'),JSON.stringify({executable:process.execPath,args,timeoutMs,outputLimit:MATRIX_OUTPUT_LIMIT,...processProof},null,2));
   try{assertMatrixProcess(call);}catch(error){
    const resultFile=path.join(caseOut,'result.json');let checkerReport;
    try{const partial=readJson(resultFile);checkerReport={path:resultFile,sha256:hashFile(resultFile),status:partial.status,failure:partial.failure,completedConfigurations:partial.configurations?.map(c=>c.profile)??[],processes:partial.processes??[]};}catch(readError){checkerReport={path:resultFile,readError:readError.code??readError.message};}
    throw Object.assign(Error(call.timedOut?'MATRIX_CHECKER_TIMEOUT':'MATRIX_CHECKER_PROCESS_FAILED'),{evidence:{processFile:path.join(folder,'checker-process.json'),process:processProof,checkerReport}});
   }
   const verified=verifyMatrixEvidence({repo,out:caseOut,generated:raw??path.join(caseOut,'generated'),source:sourceFile,sourceSha256:hashFile(sourceFile),sourceInputs,externalInputs,toolInputs:tools.files,fault,oracleSha256,generatedClosure:raw?generatedClosure:undefined,checkerPid:call.pid});
   const summaries=stdout.trim().split(/\r?\n/).map(line=>JSON.parse(line));
   if(summaries.length!==verified.report.configurations.length)throw Error('MATRIX_CHECKER_OUTPUT_DIFFERS');
   for(let i=0;i<summaries.length;i++){
    const observed=summaries[i],config=verified.report.configurations[i],expected={profile:config.profile,accepted:config.accepted,status:config.result.status,checks:config.result.checks,transforms:config.result.matrixTransformPairs,oldFind:config.result.oldMatrixReviewedFindCalls,newFind:config.result.newMatrixReviewedFindCalls,error:config.result.error};
    if(JSON.stringify(observed)!==JSON.stringify(expected))throw Error('MATRIX_CHECKER_OUTPUT_DIFFERS');
   }
   return {status:'PASS',resultFile:verified.resultFile,resultSha256:hashFile(verified.resultFile),processFile:path.join(folder,'checker-process.json'),process:processProof,timeoutMs,configurations:verified.report.configurations.map(c=>({profile:c.profile,buildExit:c.buildExit,executionExit:c.executionExit,result:c.result,executableSha256:c.executableSha256})),observedIncludes:verified.observedIncludes};
  };
  const positiveFolder=path.join(out,'p');fs.mkdirSync(positiveFolder);
  aggregate.positive=await execute(positiveFolder,source,generated);
  for(let i=0;i<controls.length;i++){
   const control=controls[i],folder=path.join(out,'f'+i);fs.mkdirSync(folder);const mutant=path.join(folder,'main.cpp');fs.writeFileSync(mutant,control.bytes,{flag:'wx'});
   const definition={id:control.id,kind:control.kind,expected:control.expected};
   // No --generated here: each checker invokes the original generate-only path
   // for its own exact mutant source before compiling its Release probe.
   const execution=await execute(folder,mutant,undefined,definition);
   if(hashFile(mutant)!==control.proof.sourceAfterSha256||hashFile(source)!==control.proof.sourceBeforeSha256)throw Error('MATRIX_CONTROL_SOURCE_CHANGED');
   aggregate.faults.push({...definition,proof:control.proof,source:mutant,sourceBefore:{path:beforeFile,sha256:control.proof.sourceBeforeSha256},sourceAfter:{path:mutant,sha256:hashFile(mutant)},mutantUnchangedAtFinish:true,...execution});
  }
  if(aggregate.faults.length!==4)throw Error('MATRIX_ALL_FOUR_FAULTS_REQUIRED');
  if(Date.now()>deadline)throw Error('MATRIX_SUPPLEMENT_BUDGET_EXHAUSTED');
  aggregate.semanticSourceFaultsDetected=3;aggregate.logicalLookupRegressionDetected=1;aggregate.status='PASS';
 }catch(error){aggregate.status='FAIL';aggregate.failure={code:/^[A-Z_]+$/.test(error.message)?error.message:'MATRIX_EVIDENCE_REJECTED',detail:String(error.message).slice(0,400),...(error.evidence?{evidence:error.evidence}:{})};}
 return finalizeMatrixSupplement({aggregate,out,source,sourceBytes,beforeFile,start});
}

/** Preserve the first execution failure before touching potentially locked PDBs.
 * Incomplete closure remains a failure and cannot replace the original timeout. */
export function collectMatrixClosure(out){
 const rows=[],errors=[];let files=[];
 try{files=ownedTree(out);}catch(error){errors.push({phase:'enumerate',path:out,code:error.code??'MATRIX_TREE_READ_FAILED',detail:String(error.message).slice(0,400)});}
 for(const file of files){if(path.resolve(file)===path.join(path.resolve(out),'result.json'))continue;try{rows.push({path:file,sha256:hashFile(file)});}catch(error){errors.push({phase:'hash',path:file,code:error.code??'MATRIX_FILE_READ_FAILED',detail:String(error.message).slice(0,400)});}}
 return {rows,errors};
}
export function finalizeMatrixSupplement({aggregate,out,source,sourceBytes,beforeFile,start}){
 aggregate.secondaryFailures??=[];
 const fail=(code,error,phase)=>{const detail={code,phase,...(error?{systemCode:error.code??null,detail:String(error.message).slice(0,400)}:{})};aggregate.status='FAIL';if(aggregate.failure)aggregate.secondaryFailures.push(detail);else aggregate.failure=detail;};
 const persistFailure=()=>{if(!aggregate.failure||fs.existsSync(path.join(out,'failure.json')))return;try{fs.writeFileSync(path.join(out,'failure.json'),JSON.stringify({schemaVersion:1,status:'FAIL',failure:aggregate.failure,recordedAt:new Date().toISOString()},null,2)+'\n',{flag:'wx'});}catch(error){aggregate.secondaryFailures.push({code:'MATRIX_FAILURE_RECEIPT_WRITE_FAILED',systemCode:error.code??null,detail:String(error.message).slice(0,400)});}};
 persistFailure();
 const afterFile=path.join(out,'main.after.cpp');
 try{
  fs.copyFileSync(source,afterFile,fs.constants.COPYFILE_EXCL);aggregate.productionCandidateUnchanged=hashFile(beforeFile)===hashFile(afterFile)&&hashFile(source)===sha(sourceBytes);
  aggregate.sourceBefore={path:beforeFile,sha256:hashFile(beforeFile)};aggregate.sourceAfter={path:afterFile,sha256:hashFile(afterFile)};
  if(!aggregate.productionCandidateUnchanged)fail('MATRIX_CURRENT_SOURCE_CHANGED',null,'source-after');
 }catch(error){aggregate.productionCandidateUnchanged=false;fail('MATRIX_SOURCE_FINALIZATION_FAILED',error,'source-after');}
 persistFailure();
 const collected=collectMatrixClosure(out);aggregate.sourceHashes=collected.rows;aggregate.closureCollectionComplete=collected.errors.length===0;
 if(collected.errors.length){fail('MATRIX_EVIDENCE_CLOSURE_INCOMPLETE',null,'closure');aggregate.secondaryFailures.push(...collected.errors);}
 persistFailure();
 const failureFile=path.join(out,'failure.json');if(fs.existsSync(failureFile)&&!aggregate.sourceHashes.some(row=>row.path===failureFile))try{aggregate.sourceHashes.push({path:failureFile,sha256:hashFile(failureFile)});}catch(error){aggregate.secondaryFailures.push({code:'MATRIX_FAILURE_RECEIPT_HASH_FAILED',systemCode:error.code??null});}
 aggregate.finishedAt=new Date().toISOString();aggregate.durationMs=Date.now()-start;aggregate.executionClosureVerified=aggregate.status==='PASS'&&aggregate.closureCollectionComplete;
 const resultFile=path.join(out,'result.json');fs.writeFileSync(resultFile,JSON.stringify(aggregate,null,2)+'\n',{flag:'wx'});
 return {...aggregate,closure:[...aggregate.sourceHashes,{path:resultFile,sha256:hashFile(resultFile)}]};
}
