import fs from 'node:fs';
import path from 'node:path';
import {runProcess} from '../runner.mjs';
import {runOwnerFaults} from './owner-fault-scheduler.mjs';
import {ADAPTER_TIMEOUT_MS} from './contracts.mjs';
import {hashFile,readJson,ownedFile,ownedTree} from './evidence.mjs';
import {sha} from './matrix-evidence.mjs';
import {OWNER_CONTRACT,OWNER_BUDGET_MS,OWNER_CHILD_TIMEOUT_MS,OWNER_OUTPUT_LIMIT,assertOwnerProcess,ownerCheckerArguments,createOwnerFaults,verifyOwnerEvidence,validateOwnerStage} from './owner-evidence.mjs';
const need=(value,code)=>{if(!value)throw Error(code);};
const json=(file,value)=>fs.writeFileSync(file,JSON.stringify(value,null,2)+'\n',{flag:'wx'});
const pin=file=>({path:file,sha256:hashFile(file)});
/** An unreadable artifact still fails the stage, while preserving its first failure. */
export function captureOwnerClosure(out,result){
 const closure=[],failures=[];let files=[];
 const reject=(file,error)=>failures.push({path:file,code:error.code??'OWNER_ARTIFACT_READ_FAILED',detail:String(error.message).slice(0,400)});
 try{files=ownedTree(out);}catch(error){reject(out,error);}
 for(const file of files){try{closure.push(pin(file));}catch(error){reject(file,error);}}
 result.sourceInventory=files;result.sourceHashes=closure;result.closureFailures=failures;
 if(failures.length){result.status='FAIL';result.failure??={code:'OWNER_ARTIFACT_IDENTITY_UNAVAILABLE',detail:'Complete owned artifact identity could not be verified.'};}
 return closure;
}

/** Finish failures remain failures and never erase the original checker error. */
export function finalizeOwnerEvidence({out,source,sourceBytes,beforeFile,result,start}){
 const afterFile=path.join(out,'main.after.cpp');result.finalizationFailures=[];
 const reject=(stage,file,error)=>{result.status='FAIL';result.failure??={code:'OWNER_SOURCE_FINISH_IDENTITY_UNAVAILABLE',detail:'Final source identity could not be verified.'};result.finalizationFailures.push({stage,path:file,code:error.code??'OWNER_FINISH_FAILED',detail:String(error.message).slice(0,400)});};
 result.sourceBefore=null;result.sourceAfter=null;result.productionCandidateUnchanged=null;
 try{fs.copyFileSync(source,afterFile,fs.constants.COPYFILE_EXCL);}catch(error){reject('source-copy-after',afterFile,error);}
 for(const [name,file]of [['sourceBefore',beforeFile],['sourceAfter',afterFile]]){try{result[name]=pin(file);}catch(error){reject(name,file,error);}}
 if(result.sourceBefore&&result.sourceAfter){try{
  result.productionCandidateUnchanged=result.sourceBefore.sha256===result.sourceAfter.sha256&&hashFile(source)===sha(sourceBytes);
  if(!result.productionCandidateUnchanged){result.status='FAIL';result.failure??={code:'OWNER_CURRENT_SOURCE_CHANGED'};result.finalizationFailures.push({stage:'source-identity',path:source,code:'OWNER_CURRENT_SOURCE_CHANGED'});}
 }catch(error){reject('current-source-identity',source,error);}}
 result.finishedAt=new Date().toISOString();result.durationMs=Date.now()-start;
 captureOwnerClosure(out,result);
 const resultFile=path.join(out,'result.json');
 const escape=(code,error)=>Object.assign(Error(code),{code,evidence:{stage:'primary-owner',resultFile,failure:result.failure??null,finalizationFailures:result.finalizationFailures,closureFailures:result.closureFailures,reportError:{code:error.code??code,detail:String(error.message).slice(0,400)}}});
 try{json(resultFile,result);}catch(error){throw escape('OWNER_FINAL_REPORT_WRITE_FAILED',error);}
 // A readable report identity is still mandatory for acceptance.
 try{return [...result.sourceHashes,pin(resultFile)];}catch(error){throw escape('OWNER_FINAL_REPORT_IDENTITY_UNAVAILABLE',error);}
}

export async function executeOwnerChecker({repo,folder,source,generated,sourceInputs,externalInputs,tools,fault,env,deadline,oracleSha256}){
 const timeoutMs=Math.min(OWNER_CHILD_TIMEOUT_MS,deadline-Date.now());need(timeoutMs>=1000,'OWNER_SUPPLEMENT_BUDGET_EXHAUSTED');
 const out=path.join(folder,'e'),args=ownerCheckerArguments({repo,out,source,generated,fault});
 const call=await runProcess(process.execPath,args,{cwd:repo,env,timeoutMs,outputLimit:OWNER_OUTPUT_LIMIT});
 fs.writeFileSync(path.join(folder,'checker.stdout.log'),call.stdout,{flag:'wx'});fs.writeFileSync(path.join(folder,'checker.stderr.log'),call.stderr,{flag:'wx'});
 const {stdout,stderr,...processProof}=call,processFile=path.join(folder,'checker-process.json');json(processFile,{executable:process.execPath,args,timeoutMs,outputLimit:OWNER_OUTPUT_LIMIT,...processProof});
 assertOwnerProcess(call);
 const verified=verifyOwnerEvidence({repo,out,generated,source,sourceSha256:hashFile(source),sourceInputs,externalInputs,toolInputs:tools.files,fault,oracleSha256,checkerPid:call.pid});
 const lines=stdout.trim().split(/\r?\n/).map(line=>JSON.parse(line));need(lines.length===2,'OWNER_CHECKER_OUTPUT_DIFFERS');
 for(let i=0;i<lines.length;i++)need(JSON.stringify(lines[i])===JSON.stringify({mode:verified.report.configurations[i].mode,...verified.report.configurations[i].report}),'OWNER_CHECKER_OUTPUT_DIFFERS');
 return {status:'PASS',resultFile:verified.resultFile,resultSha256:hashFile(verified.resultFile),processFile,process:processProof,timeoutMs,configurations:verified.report.configurations,observedIncludes:verified.observedIncludes,children:verified.children};
}
/** Every fault uses the unchanged original generator, not a copied report or a static anchor. */
export async function prepareOwnerFaultInputs({repo,folder,control,env,deadline}){
 fs.mkdirSync(folder);const names={main:'main.cpp',header:'HcMotionCapabilityCatalog.h',catalog:'HcMotionCapabilityCatalog.generated.h'};
 const files={};for(const [part,name]of Object.entries(names)){files[part]=path.join(folder,name);fs.writeFileSync(files[part],control.bytes[part],{flag:'wx'});}
 const generated=path.join(folder,'g'),args=[path.join(repo,'tools/hc_model_motion_consumer_selftest.mjs'),'--source',files.main,'--binding-header',files.header,'--binding-catalog',files.catalog,'--out',generated,'--generate-only'];
 const timeoutMs=Math.min(OWNER_CHILD_TIMEOUT_MS,deadline-Date.now());need(timeoutMs>=1000,'OWNER_SUPPLEMENT_BUDGET_EXHAUSTED');
 const call=await runProcess(process.execPath,args,{cwd:repo,env,timeoutMs,outputLimit:OWNER_OUTPUT_LIMIT});
 fs.writeFileSync(path.join(folder,'generate.stdout.log'),call.stdout,{flag:'wx'});fs.writeFileSync(path.join(folder,'generate.stderr.log'),call.stderr,{flag:'wx'});
 const {stdout,stderr,...proof}=call;json(path.join(folder,'generate-process.json'),{executable:process.execPath,args,timeoutMs,outputLimit:OWNER_OUTPUT_LIMIT,...proof});assertOwnerProcess(call);
 const bound=readJson(ownedFile(generated,'source-bindings.json'));need(bound.status==='generated-only'&&bound.configurations.length===0&&bound.mutations.length===0,'OWNER_FAULT_GENERATION_ONLY_REQUIRED');
 need(hashFile(files[control.part])===control.proof.partAfterSha256,'OWNER_FAULT_INPUT_CHANGED');
 return {source:files.main,generated,ownedPins:Object.values(files).map(pin),files};
}
/** Mandatory after all unchanged original producer/trace and matrix gates. */
export async function runOwnerSupplement({repo,out,generated,source,sourceBytes,sourceInputs,externalInputs,tools,generatedClosure,env,adapterStartedAt}){
 need(path.isAbsolute(out)&&path.parse(out).root.toLowerCase()==='d:\\'&&path.resolve(out)===path.join(path.dirname(path.resolve(generated)),'o')&&Number.isFinite(adapterStartedAt),'OWNER_D_OWNED_CASE_REQUIRED');
 need(!fs.existsSync(out),'OWNER_FRESH_OUTPUT_REQUIRED');fs.mkdirSync(out);
 const start=Date.now(),deadline=Math.min(start+OWNER_BUDGET_MS,adapterStartedAt+ADAPTER_TIMEOUT_MS-10000);
 const result={schemaVersion:1,kind:'motion-primary-owner-supplement',status:'FAIL',startedAt:new Date(start).toISOString(),budget:{supplementMs:OWNER_BUDGET_MS,childMaxMs:OWNER_CHILD_TIMEOUT_MS,originalProducerMs:840000,unchangedAdapterMs:ADAPTER_TIMEOUT_MS,reservedFinishMs:10000,availableMs:Math.max(0,deadline-start)},positive:null,faults:[],fullBusExecuted:false,hardwareIo:false,physicalAcceptance:false,wholeProcessCpuReduction:null};
 const beforeFile=path.join(out,'main.before.cpp');fs.writeFileSync(beforeFile,sourceBytes,{flag:'wx'});let closure=[];
 try{
  need(hashFile(source)===sha(sourceBytes),'OWNER_CURRENT_SOURCE_CHANGED');
  const original=readJson(ownedFile(generated,'source-bindings.json'));need(original.status==='pass'&&original.mode==='candidate'&&original.consumerOnlyControl===false,'OWNER_FULL_ORIGINAL_PRODUCER_REQUIRED');
  for(const row of generatedClosure){need(hashFile(row.path)===row.sha256,'OWNER_ORIGINAL_GENERATED_CHANGED');}
  const oracleSha256=hashFile(ownedFile(generated,'expectations.json'));
  for(const row of [...sourceInputs,...externalInputs,...tools.files])need(hashFile(row.path)===row.sha256,'OWNER_DECLARED_INPUT_CHANGED');
  need(path.resolve(process.execPath).toLowerCase()===path.resolve(tools.nodeExecutable).toLowerCase(),'OWNER_NODE_NOT_PINNED');
  const controls=createOwnerFaults({sourceBytes,headerBytes:fs.readFileSync(path.join(repo,'capabilities/HcMotionCapabilityCatalog.h')),catalogBytes:fs.readFileSync(path.join(repo,'capabilities/HcMotionCapabilityCatalog.generated.h')),contract:readJson(path.join(repo,OWNER_CONTRACT))});
  json(path.join(out,'source-controls.json'),{schemaVersion:1,source,sourceSha256:sha(sourceBytes),controls:controls.map(({bytes,...control})=>control),generationOnly:true});
  const positiveFolder=path.join(out,'p');fs.mkdirSync(positiveFolder);
  result.positive=await executeOwnerChecker({repo,folder:positiveFolder,source,generated,sourceInputs,externalInputs,tools,env,deadline,oracleSha256});
  await runOwnerFaults({controls,result,execute:async(control,i)=>{
   const folder=path.join(out,'f'+i),prepared=await prepareOwnerFaultInputs({repo,folder,control,env,deadline});
   const fault={id:control.id,part:control.part,expected:control.expected};
   const executed=await executeOwnerChecker({repo,folder,source:prepared.source,generated:prepared.generated,sourceInputs:[...sourceInputs,...prepared.ownedPins],externalInputs,tools,fault,env,deadline,oracleSha256});
   need(hashFile(prepared.files[control.part])===control.proof.partAfterSha256,'OWNER_FAULT_INPUT_CHANGED');
   return {...fault,proof:control.proof,inputs:prepared.ownedPins,...executed};
  }});
  need(Date.now()<=deadline,'OWNER_SUPPLEMENT_BUDGET_EXHAUSTED');
  for(const row of generatedClosure)need(hashFile(row.path)===row.sha256,'OWNER_ORIGINAL_GENERATED_CHANGED');
  for(const row of [...sourceInputs,...externalInputs,...tools.files])need(hashFile(row.path)===row.sha256,'OWNER_DECLARED_INPUT_CHANGED');
  result.semanticSourceFaultsDetected=4;result.logicalLookupRegressionDetected=1;result.status='PASS';result.validation=validateOwnerStage(result);
 }catch(error){result.status='FAIL';result.failure={code:/^[A-Z_]+$/.test(error.message)?error.message:'OWNER_EVIDENCE_REJECTED',detail:String(error.message).slice(0,400)};}
 finally{closure=finalizeOwnerEvidence({out,source,sourceBytes,beforeFile,result,start});}
 return {...result,closure};
}
