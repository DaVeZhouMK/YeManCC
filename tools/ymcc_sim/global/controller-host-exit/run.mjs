import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {createHash} from 'node:crypto';
import {REPO,contract,sourceInputs,historicalArtifactRows} from './adapter.mjs';
import {compositionHistoricalReference,validateNativeExit} from './receipts.mjs';
import {fixedNativeExitMode} from './fixed-build-receipts.mjs';
import {readNativeExitTools,nativeExitEnvironment,nativeExitPythonFlags,nativeExitChildArgs} from './tool-profile.mjs';
import {createOwnedWorkspace} from '../controller-signal-evidence/owned-workspace.mjs';
import {runProcess} from '../runner.mjs';
const hash=p=>createHash('sha256').update(fs.readFileSync(p)).digest('hex'),read=p=>JSON.parse(fs.readFileSync(p,'utf8').replace(/^\uFEFF/,''));
const pin=p=>({path:p,sha256:hash(p)}),write=(p,v)=>fs.writeFileSync(p,JSON.stringify(v,null,2)+'\n');
export function completionDependencyMismatches(scope,sources){
 const pins=scope?.dependencyHashes;
 assert(scope?.schemaVersion===1&&pins&&typeof pins==='object'&&!Array.isArray(pins)&&Object.keys(pins).length,'NATIVE_EXIT_GATE_DEPENDENCY_CONTRACT');
 return Object.entries(pins).flatMap(([name,expected])=>{
  assert(Object.hasOwn(sources,name)&&/^[a-f0-9]{64}$/.test(expected)&&/^[a-f0-9]{64}$/.test(sources[name]),'NATIVE_EXIT_GATE_DEPENDENCY_UNDECLARED:'+name);
  const actual=sources[name];
  return actual===expected?[]:[{path:name,expectedSha256:expected,actualSha256:actual}];
 });
}
export async function executeNativeExit(out){
 out=path.resolve(out);assert(!fs.existsSync(out),'NATIVE_EXIT_FRESH_OUTPUT_REQUIRED');fs.mkdirSync(out,{recursive:true});
 const startedAt=new Date().toISOString(),sources=Object.fromEntries(sourceInputs.map(n=>[n,hash(path.join(REPO,n))]));
 // The last child checks these raw pins before compiling. Reject a known
 // mismatch before preparing any child, while retaining its full source gate.
 const scope=fs.readFileSync(path.join(REPO,contract.completionContract));
 assert.equal(createHash('sha256').update(scope).digest('hex'),sources[contract.completionContract],'NATIVE_EXIT_GATE_CONTRACT_DRIFT');
 const mismatches=completionDependencyMismatches(JSON.parse(scope.toString('utf8').replace(/^\uFEFF/,'')),sources);
 if(mismatches.length){
  const premature={schemaVersion:2,toolEvidenceMode:fixedNativeExitMode,id:contract.id,status:'FAIL',pass:false,error:'NATIVE_EXIT_GATE_DEPENDENCY_PRECHECK',
   fullLifecycleAcceptance:'INCOMPLETE',physicalReturnVerified:false,driverExecuted:false,processExitUsedAsCleanup:false,
   startedAt,finishedAt:new Date().toISOString(),sourceHashes:sources,children:{},
   admission:{stage:'completion-gates-raw-dependencies',mismatches,toolAndRuntimeAcceptance:'NOT_RUN',producerExecution:'NOT_RUN'}};
  fs.writeFileSync(path.join(out,'result.json'),JSON.stringify(premature,null,2)+'\n',{flag:'wx'});
  return premature;
 }
 const result={schemaVersion:2,toolEvidenceMode:fixedNativeExitMode,id:contract.id,status:'RUNNING',pass:false,fullLifecycleAcceptance:'INCOMPLETE',physicalReturnVerified:false,driverExecuted:false,processExitUsedAsCleanup:false,startedAt,sourceHashes:sources,children:{},admission:{status:'RUNNING',stage:null,producerExecution:'NOT_RUN'}};
 const save=()=>write(path.join(out,'result.json'),result);save();
 const stage=name=>{result.admission.stage=name;save();};
 try{
  stage('primary-tool-profile');
  const tools=readNativeExitTools(REPO);
  result.toolProfile={sha256:tools.profileSha256,files:tools.files.length,pythonFlags:nativeExitPythonFlags};
  result.toolsBefore=tools.files;
  stage('historical-artifact-pins');
  const artifacts=historicalArtifactRows(REPO);
  result.historicalArtifactPins=artifacts;
  result.compositionArchive=compositionHistoricalReference(REPO);
  stage('workspace-and-environment');
  const workspace=createOwnedWorkspace({repo:REPO,output:out,primary:tools.primary,sourceHashes:sources});
  result.workspace={root:workspace.root,identity:workspace.identity};
  const ctx={repo:REPO,sources,tools,requireFixedBuild:true};
  const verify=()=>{for(const [n,h] of Object.entries(sources))assert.equal(hash(path.join(REPO,n)),h,'NATIVE_EXIT_SOURCE_DRIFT:'+n);for(const p of artifacts)assert.equal(hash(p.path),p.sha256,'NATIVE_EXIT_ARTIFACT_DRIFT:'+p.path);workspace.verify();};
  const env=nativeExitEnvironment(process.env,tools,process.execPath);
  verify();
  result.admission={status:'PASS',stage:'complete',producerExecution:'NOT_RUN'};save();
  for(const [name,c] of Object.entries(contract.routes)){
   const owned=workspace.allocate('native-host-exit-'+name),args=nativeExitChildArgs(REPO,c,owned.buildOutput,name==='parent'?sources['native/main.cpp']:undefined,{fixedBuild:true});
   const row={id:name,executable:tools.python,args,output:owned.buildOutput,ownedWorkspace:owned,startedAt:new Date().toISOString(),exitCode:null};result.children[name]=row;result.admission.producerExecution='ATTEMPTED';save();
   const observed=await runProcess(tools.python,args,{cwd:REPO,env:{...env,TEMP:owned.temp,TMP:owned.temp},timeoutMs:240000});Object.assign(row,observed,{finishedAt:new Date().toISOString()});fs.writeFileSync(path.join(out,name+'-stdout.log'),observed.stdout??'');fs.writeFileSync(path.join(out,name+'-stderr.log'),observed.stderr??'');delete row.stdout;delete row.stderr;
   assert(observed.exitCode===0&&!observed.signal&&!observed.launchError&&!observed.timedOut&&!observed.outputTruncated,'NATIVE_EXIT_CHILD_FAILED:'+name);
   row.receipt=pin(path.join(owned.buildOutput,'RESULT.json'));save();workspace.verify(owned);verify();
  }
  const after=readNativeExitTools(REPO);
  assert.equal(after.profileSha256,tools.profileSha256,'NATIVE_EXIT_TOOL_PROFILE_DRIFT');
  result.toolsAfter=after.files;
  const old=read(contract.historicalFailure.path);assert.equal(old.status,'ACTUAL_NATIVE_EXIT_PARENT_CONTRACT_FAIL_REPRODUCED');assert.equal(old.productPass,false);result.historicalFailureRetained=contract.historicalFailure;
  verify();result.sourceStable=true;result.artifactsStable=true;result.status='PASS';result.pass=true;result.validation=validateNativeExit(result,ctx);
 }catch(error){result.status='FAIL';result.pass=false;result.error=error.message;result.stack=error.stack;if(result.admission.status==='RUNNING')result.admission.status='FAIL';}
 finally{result.finishedAt=new Date().toISOString();save();}
 return result;
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){
 try{assert.equal(process.argv.length,4);assert.equal(process.argv[2],'--out');const r=await executeNativeExit(process.argv[3]);console.log(JSON.stringify({status:r.status,validation:r.validation,error:r.error,fullLifecycleAcceptance:r.fullLifecycleAcceptance}));if(!r.pass)process.exitCode=1;}catch(e){console.error(e.stack);process.exitCode=1;}
}
