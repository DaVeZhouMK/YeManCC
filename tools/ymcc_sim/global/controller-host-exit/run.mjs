import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {createHash} from 'node:crypto';
import {REPO,MODULE,contract,sourceInputs,artifactRows} from './adapter.mjs';
import {validateNativeExit} from './receipts.mjs';
import {toolInputs} from '../controller-signal-evidence/adapter.mjs';
import {createOwnedWorkspace} from '../controller-signal-evidence/owned-workspace.mjs';
import {consumerEnvironment} from '../controller-signal-evidence/environment.mjs';
import {compositionContract,verifyNestedToolFiles} from '../controller-signal-evidence/owned-cleanup-composition-receipts.mjs';
import {runProcess} from '../runner.mjs';
const hash=p=>createHash('sha256').update(fs.readFileSync(p)).digest('hex'),read=p=>JSON.parse(fs.readFileSync(p,'utf8').replace(/^\uFEFF/,''));
const pin=p=>({path:p,sha256:hash(p)}),write=(p,v)=>fs.writeFileSync(p,JSON.stringify(v,null,2)+'\n');
export async function executeNativeExit(out){
 out=path.resolve(out);assert(!fs.existsSync(out),'NATIVE_EXIT_FRESH_OUTPUT_REQUIRED');fs.mkdirSync(out,{recursive:true});
 const tools=toolInputs(REPO),sources=Object.fromEntries(sourceInputs.map(n=>[n,hash(path.join(REPO,n))])),artifacts=artifactRows(),workspace=createOwnedWorkspace({repo:REPO,output:out,primary:tools.primary,sourceHashes:sources});
 const result={schemaVersion:1,id:contract.id,status:'RUNNING',pass:false,fullLifecycleAcceptance:'INCOMPLETE',physicalReturnVerified:false,driverExecuted:false,processExitUsedAsCleanup:false,startedAt:new Date().toISOString(),sourceHashes:sources,artifactPins:artifacts,toolsBefore:tools.files,children:{},workspace:{root:workspace.root,identity:workspace.identity}};
 const save=()=>write(path.join(out,'result.json'),result),ctx={repo:REPO,sources,tools};save();
 const verify=()=>{for(const [n,h] of Object.entries(sources))assert.equal(hash(path.join(REPO,n)),h,'NATIVE_EXIT_SOURCE_DRIFT:'+n);for(const p of artifacts)assert.equal(hash(p.path),p.sha256,'NATIVE_EXIT_ARTIFACT_DRIFT:'+p.path);workspace.verify();};
 const env=consumerEnvironment({ambient:process.env,tools,out,nodeExecutable:process.execPath});
 try{
  verify();const {manifest,contract:tc}=compositionContract(REPO);assert.equal(manifest.files.length,5772,'NATIVE_EXIT_COMPLETE_NESTED_TOOL_COUNT');
  for(const phase of ['before','after']){
   if(phase==='after')break;const list=verifyNestedToolFiles(manifest.files);const file=path.join(out,'nested-tools-before.json');write(file,{kind:'complete-frozen-toolchain-manifest',phase,files:list,manifestSha256:tc.toolchain.manifestSha256,sourceHashes:sources});result.nestedBefore=pin(file);
  }
  for(const [name,c] of Object.entries(contract.routes)){
   const owned=workspace.allocate('native-host-exit-'+name),args=['-B',path.join(REPO,c.consumer),c.arg,owned.buildOutput];if(name==='parent')args.push('--expected-main-sha256',sources['native/main.cpp']);
   const row={id:name,executable:tools.python,args,output:owned.buildOutput,ownedWorkspace:owned,startedAt:new Date().toISOString(),exitCode:null};result.children[name]=row;save();
   const observed=await runProcess(tools.python,args,{cwd:REPO,env:{...env,TEMP:owned.temp,TMP:owned.temp},timeoutMs:240000});Object.assign(row,observed,{finishedAt:new Date().toISOString()});fs.writeFileSync(path.join(out,name+'-stdout.log'),observed.stdout??'');fs.writeFileSync(path.join(out,name+'-stderr.log'),observed.stderr??'');delete row.stdout;delete row.stderr;
   assert(observed.exitCode===0&&!observed.signal&&!observed.launchError&&!observed.timedOut&&!observed.outputTruncated,'NATIVE_EXIT_CHILD_FAILED:'+name);
   row.receipt=pin(path.join(owned.buildOutput,'RESULT.json'));save();verify();
  }
  const list=verifyNestedToolFiles(manifest.files),file=path.join(out,'nested-tools-after.json');write(file,{kind:'complete-frozen-toolchain-manifest',phase:'after',files:list,manifestSha256:tc.toolchain.manifestSha256,sourceHashes:sources});result.nestedAfter=pin(file);assert.equal(list.length,5772);
  const old=read(contract.historicalFailure.path);assert.equal(old.status,'ACTUAL_NATIVE_EXIT_PARENT_CONTRACT_FAIL_REPRODUCED');assert.equal(old.productPass,false);result.historicalFailureRetained=contract.historicalFailure;
  verify();result.sourceStable=true;result.artifactsStable=true;result.toolsAfter=tools.files;result.status='PASS';result.pass=true;result.validation=validateNativeExit(result,ctx);
 }catch(error){result.status='FAIL';result.pass=false;result.error=error.message;result.stack=error.stack;}
 finally{result.finishedAt=new Date().toISOString();save();}
 return result;
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){
 try{assert.equal(process.argv.length,4);assert.equal(process.argv[2],'--out');const r=await executeNativeExit(process.argv[3]);console.log(JSON.stringify({status:r.status,validation:r.validation,error:r.error,fullLifecycleAcceptance:r.fullLifecycleAcceptance}));if(!r.pass)process.exitCode=1;}catch(e){console.error(e.stack);process.exitCode=1;}
}
