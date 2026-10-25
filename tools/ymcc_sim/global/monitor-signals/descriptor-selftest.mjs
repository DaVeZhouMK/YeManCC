import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {fileURLToPath,pathToFileURL}from'node:url';
import {repoRoot,sha256,readJson}from'./common.mjs';
import {getMonitorAdapters,CLOCK_BEFORE_ROOT,CLOCK_BEFORE_RELATIVE_FILES}from'./adapter.mjs';
import {executeAdapter,validateAdapters}from'../runner.mjs';

// Interface-only negative controls. All deletions/corruption are represented
// by omissions/replacement bytes in new copies; mainline inputs are untouched.
export async function checkStaticDescriptor(evidenceDir){
 const out=path.resolve(evidenceDir);fs.mkdirSync(out,{recursive:true});
 if(fs.existsSync(path.join(out,'result.json')))throw Error('FRESH_DESCRIPTOR_EVIDENCE_REQUIRED');
 const source=getMonitorAdapters()[0],fixture=CLOCK_BEFORE_ROOT+'/'+CLOCK_BEFORE_RELATIVE_FILES[0];
 const watched=[source.path,...source.evidenceInputs],pins=Object.fromEntries(watched.map(f=>[f,sha256(fs.readFileSync(path.join(repoRoot,f)))]));
 const originalManifest=readJson(path.join(repoRoot,'tools/ymcc_sim/global/monitor-signals/clock-baseline.json'));
 assert.deepEqual(CLOCK_BEFORE_RELATIVE_FILES,Object.keys(originalManifest.files),'reviewed static file list must remain exact');
 const descriptorSource=fs.readFileSync(path.join(repoRoot,'tools/ymcc_sim/global/monitor-signals/adapter.mjs'),'utf8');
 assert(!descriptorSource.includes('readFileSync')&&!descriptorSource.includes("'node:fs'"),'descriptor must not load fixture metadata');
 const checks=[];
 const runnerPath=path.join(repoRoot,'tools/ymcc_sim/global/runner.mjs');
 const runnerText=fs.readFileSync(runnerPath,'utf8');
 const oldLine="    result.status = adapter.kind === 'powershell' && result.exitCode === 10 && !result.timedOut ? 'BLOCKED' : evaluateResult(result);";
 const proposedLine="    const explicitBlockedExit = adapter.blockedExitCodes?.includes(result.exitCode) === true;\n    result.status = !result.timedOut && ((adapter.kind === 'powershell' && result.exitCode === 10) || explicitBlockedExit) ? 'BLOCKED' : evaluateResult(result);";
 const proposalPath=path.join(out,'runner-proposed.mjs');
 fs.writeFileSync(proposalPath,runnerText.includes(oldLine)?runnerText.replace(oldLine,proposedLine):runnerText);
 const proposed=await import(pathToFileURL(proposalPath).href);
 for(const variant of ['missing-manifest','invalid-manifest','missing-before-source']){
  const root=path.join(out,variant,'checkout'),runDir=path.join(out,variant,'runs');
  const absent=variant==='missing-before-source'?fixture:'tools/ymcc_sim/global/monitor-signals/clock-baseline.json';
  for(const relative of [...new Set([...watched,...source.referenceInputs])]){
   if(variant!=='invalid-manifest'&&relative===absent)continue;
   const dest=path.join(root,relative);fs.mkdirSync(path.dirname(dest),{recursive:true});
   fs.writeFileSync(dest,variant==='invalid-manifest'&&relative===absent?Buffer.from('{invalid fixture manifest'):fs.readFileSync(path.join(repoRoot,relative)));
  }
  const module=await import(pathToFileURL(path.join(root,'tools/ymcc_sim/global/monitor-signals/adapter.mjs')).href);
  const monitor=module.getMonitorAdapters()[0];assert.equal(monitor.id,source.id);
  assert.equal(monitor.evidenceInputs.filter(f=>f.startsWith(CLOCK_BEFORE_ROOT+'/')).length,15);
  // This child is a runner protocol control, not coverage of another product.
  const otherSource='tools/descriptor-other-control.mjs',otherFile=path.join(root,otherSource);
  fs.writeFileSync(otherFile,'console.log("unrelated capability selected successfully");');
  const other={id:'controller.descriptor-control',domain:'controller',title:'Unrelated selection protocol control',kind:'node',path:otherSource,args:[],timeoutMs:5000,
   profiles:['desktop'],scope:'behavioral-mock',evidenceInputs:[],resultFiles:[]};
  const inventory=[monitor,other];validateAdapters(inventory,root);
  const unrelated=inventory.filter(item=>item.domain==='controller');assert.equal(unrelated.length,1);
  const otherRun=await executeAdapter(unrelated[0],{repoRoot:root,runDir});assert.equal(otherRun.status,'PASS');
  const monitorRun=await executeAdapter(monitor,{repoRoot:root,runDir});
  let runtimeReceipt=null,proposedMonitorRun=null;
  if(variant==='invalid-manifest'){
   runtimeReceipt=readJson(path.join(monitorRun.evidenceDir,'monitor/result.json'));
   assert.equal(runtimeReceipt.status,'BLOCKED');assert.equal(monitorRun.exitCode,10);
   proposedMonitorRun=await proposed.executeAdapter(monitor,{repoRoot:root,runDir:path.join(out,variant,'proposed-runs')});
   assert.equal(proposedMonitorRun.status,'BLOCKED');
  }else{
   assert.equal(monitorRun.status,'BLOCKED');assert.equal(monitorRun.reason,'REQUIRED_INPUT_MISSING');
   assert(monitorRun.sourceInputs.some(item=>item.path===absent&&item.status!=='OK'));
  }
  checks.push({id:variant,status:'PASS',descriptorPresent:true,staticFixtureCount:15,unrelatedSelection:{id:otherRun.id,status:otherRun.status},
   currentGlobalMonitor:{status:monitorRun.status,reason:monitorRun.reason??null,exitCode:monitorRun.exitCode??null},
   runtimeMonitor:runtimeReceipt?{status:runtimeReceipt.status,reason:runtimeReceipt.reason,cases:runtimeReceipt.cases}:null,
   proposedGlobalMonitor:proposedMonitorRun?{status:proposedMonitorRun.status,exitCode:proposedMonitorRun.exitCode}:null,
   mainlineInputsModified:false});
 }
 for(const [relative,pin]of Object.entries(pins))assert.equal(sha256(fs.readFileSync(path.join(repoRoot,relative))),pin,'Mainline changed: '+relative);
 const result={schema:'ymcc.monitor-descriptor-interface.v1',ok:true,status:'PASS',checks,sourceHashes:pins,
  currentSharedRunnerRecognizesNodeBlockedExit:!runnerText.includes(oldLine),sharedRunnerSha256:sha256(runnerText),
  proposedRunnerSha256:sha256(fs.readFileSync(proposalPath)),mainlineInputsModified:false,
  scope:'Descriptor/input-isolation and unrelated runner selection controls only; no monitoring product suite executed',hardwareIo:false};
 fs.writeFileSync(path.join(out,'result.json'),JSON.stringify(result,null,2));return result;
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){
 try{const result=await checkStaticDescriptor(process.argv[2]);console.log(JSON.stringify({status:result.status,checks:result.checks,
  currentSharedRunnerRecognizesNodeBlockedExit:result.currentSharedRunnerRecognizesNodeBlockedExit,hardwareIo:false}));}
 catch(error){console.error(error.stack);process.exitCode=1;}
}
