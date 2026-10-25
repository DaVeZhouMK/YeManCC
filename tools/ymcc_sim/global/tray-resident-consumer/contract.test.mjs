import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {HERE,FILES,spec,need,json,save,sha,key,fingerprint,mutate,validateView,verifyReceipt,semanticVerdict,unchanged,outputFiles} from './contract.mjs';
import {prepareCase,executeCase,evaluateCase} from './run.mjs';
export async function maintenance({repoRoot,baseline,outDir,baselineImplementationRoot=null}){
 outDir=path.resolve(outDir);need(!fs.existsSync(outDir),'FRESH_MAINTENANCE_REQUIRED');fs.mkdirSync(outDir,{recursive:true});
 const base=json(path.join(baseline,'result.json')),results=[];
 const baselineReport=fingerprint(path.join(baseline,'result.json')),archiveInputs=[];
 if(baselineImplementationRoot){
  baselineImplementationRoot=fs.realpathSync(baselineImplementationRoot);
  assert.deepEqual(fs.readdirSync(baselineImplementationRoot).sort(),[...FILES].sort());
  for(const receipt of base.receipts)for(const row of receipt.plan.implementationInputs.filter(r=>r.role==='leaf-implementation')){
   assert(FILES.includes(path.basename(row.path)));
   const archived=fingerprint(path.join(baselineImplementationRoot,path.basename(row.path)));
   assert.equal(archived.sha256,row.sha256);assert.equal(archived.size,row.size);archiveInputs.push(archived);
  }
 }
 const verifyBaselineReceipt=receipt=>{
  if(!baselineImplementationRoot)return verifyReceipt(receipt);
  // Verification-only relocation of implementation identities; the original receipt is never changed or reissued.
  const historical=structuredClone(receipt);
  for(const row of historical.plan.implementationInputs)if(row.role==='leaf-implementation')row.path=path.join(baselineImplementationRoot,path.basename(row.path));
  return verifyReceipt(historical);
 };
 const test=async(name,fn)=>{await fn();results.push({name,passed:true});console.log('PASS:',name);};
 await test('fresh baseline receipts retain original positive and precise semantic failure',()=>{assert(base.ok);assert(base.receipts.every(verifyBaselineReceipt));assert.equal(base.receipts[0].originalResult.passedNames.length,11);assert.equal(base.receipts[1].originalResult.passedNames.length,2);});
 await test('single source fault is exact; producer, native and package bytes are unchanged',()=>{const [p,n]=base.receipts;for(const relative of spec.sourceInputs){const bytes=fs.readFileSync(path.join(p.plan.view,relative),'utf8'),negative=fs.readFileSync(path.join(n.plan.view,relative),'utf8');assert.equal(negative,relative===spec.fault.source?mutate(bytes):bytes);}assert.deepEqual(n.plan.fault.changedFiles,[spec.fault.source]);assert.equal(n.plan.fault.replacementCount,1);});
 await test('fault requires exactly one original source anchor',()=>{assert.throws(()=>mutate('no anchor'),/FAULT_ANCHOR_NOT_UNIQUE/);assert.throws(()=>mutate(spec.fault.before+' '+spec.fault.before),/FAULT_ANCHOR_NOT_UNIQUE/);});
 const plan=await prepareCase({repoRoot,caseDir:path.join(outDir,'control-case'),faultId:spec.fault.id});
 const original=fs.readFileSync(path.join(plan.view,spec.fault.source));
 await test('owned product source drift is rejected before launch',()=>{const f=path.join(plan.view,spec.fault.source);fs.appendFileSync(f,'\n// drift\n');assert.throws(()=>executeCase(plan),/OWNED_SOURCE_CHANGED/);fs.writeFileSync(f,original);validateView(plan);});
 await test('extra source member is rejected',()=>{const f=path.join(plan.view,'extra.ts');fs.writeFileSync(f,'export const stray=1;');assert.throws(()=>executeCase(plan),/ATOMIC_SOURCEVIEW_MEMBERS_CHANGED/);fs.unlinkSync(f);});
 await test('unapproved node_modules alias is rejected',()=>{const f=path.join(plan.view,'node_modules/extra');fs.mkdirSync(f);assert.throws(()=>executeCase(plan),/UNAUTHORIZED_ALIAS/);fs.rmdirSync(f);});
 await test('same-name same-byte compiler copy cannot replace the bound package alias',()=>{
  const alias=path.join(plan.view,'node_modules/esbuild'),owned=path.join(outDir,'same-name-copy/esbuild');fs.mkdirSync(path.join(owned,'lib'),{recursive:true});fs.copyFileSync(plan.authority.compilerEntry,path.join(owned,'lib/main.js'));
  fs.unlinkSync(alias);fs.symlinkSync(owned,alias,'junction');assert.throws(()=>executeCase(plan),/TOOL_ALIAS_TARGET_NOT_BOUND/);fs.unlinkSync(alias);fs.symlinkSync(path.dirname(path.dirname(plan.authority.compilerEntry)),alias,'junction');validateView(plan);
 });
 await test('tool identity drift in the declaration is rejected',()=>{const forged=structuredClone(plan);forged.tools[0].sha256='0'.repeat(64);assert.throws(()=>executeCase(forged),/PRE_EXECUTION_INPUT_CHANGED/);});
 await test('missing compiler authority is rejected before launch',()=>{const forged=structuredClone(plan);forged.authority.compilerBinary=path.join(outDir,'missing-esbuild.exe');assert.throws(()=>executeCase(forged),/CASE_AUTHORITY_CHANGED/);});
 const receipt=executeCase(plan);save(path.join(plan.caseDir,'receipt.json'),receipt);
 await test('fresh independent maintenance semantic control is accepted',()=>assert(verifyReceipt(receipt)));
 const tamper=[];
 await test('generated output drift makes the old receipt invalid',()=>{const f=receipt.actualLoaded.generated[0].path,b=fs.readFileSync(f);fs.appendFileSync(f,'\n// generated drift\n');tamper.push({kind:'generated',path:f,before:sha(b),after:sha(fs.readFileSync(f)),rejected:!verifyReceipt(receipt)});assert(!verifyReceipt(receipt));fs.writeFileSync(f,b);assert(verifyReceipt(receipt));});
 await test('extra generated file makes the output closure invalid',()=>{const f=path.join(plan.generated,'unrecorded.cjs');fs.writeFileSync(f,'module.exports=1;');assert(!verifyReceipt(receipt));fs.unlinkSync(f);assert(verifyReceipt(receipt));});
 await test('source drift after execution makes the old receipt invalid',()=>{const f=path.join(plan.view,spec.sourceInputs[0]),b=fs.readFileSync(f);fs.appendFileSync(f,'\n// producer drift\n');tamper.push({kind:'producer',path:f,before:sha(b),after:sha(fs.readFileSync(f)),rejected:!verifyReceipt(receipt)});assert(!verifyReceipt(receipt));fs.writeFileSync(f,b);assert(verifyReceipt(receipt));});
 await test('missing compiler, missing source, and startup failure cannot masquerade as a semantic negative',()=>{
  for(const error of [{code:'ENOENT',message:'missing compiler'},{code:'ENOENT',message:'missing source'},{code:'STARTUP',message:'startup failed'}]){
   const failed=evaluateCase(plan,{...receipt.child,exitCode:1,error});assert.equal(failed.accepted,false);assert.equal(failed.semanticDetected,false);assert(failed.controlFailures.includes('CHILD_INCOMPLETE'));
  }
  const trace=json(plan.trace),out=fs.readFileSync(path.join(plan.caseDir,'stdout.txt'),'utf8'),err=fs.readFileSync(path.join(plan.caseDir,'stderr.txt'),'utf8');
  assert(!semanticVerdict(plan,{...receipt.child,exitCode:0},trace,out,err,true));assert(!semanticVerdict(plan,receipt.child,{...trace,assertions:[{...trace.assertions[0],message:'some other failure'}]},out,err,true));
 });
 const probes=[
  ['external-read',"import fs from 'node:fs';fs.readFileSync(process.env.SystemRoot+'/win.ini')",'READ_IDENTITY'],
  ['raw-device-open',"import fs from 'node:fs';fs.openSync('\\\\\\\\.\\\\YMCC_SIM_DENIED_DEVICE','r')",'RAW_OPEN'],
  ['file-write',"import fs from 'node:fs';fs.writeFileSync(process.env.TEMP+'/must-not-exist','x')",'FS_MUTATION_OR_RAW_IO'],
  ['async-file-write',"import fs from 'node:fs/promises';await fs.writeFile(process.env.TEMP+'/must-not-exist','x')",'PROMISE_FS_MUTATION'],
  ['arbitrary-process',"import cp from 'node:child_process';cp.spawnSync(process.execPath,['-e','process.exit(0)'])",'PROCESS'],
  ['compiler-outside-transform',"import cp from 'node:child_process';cp.execFileSync(process.env.ESBUILD_BINARY_PATH,['--version'])",'COMPILER_EXEC_CONTRACT'],
  ['network',"fetch('https://example.invalid/')",'NETWORK'],
  ['worker-thread',"import {Worker} from 'node:worker_threads';new Worker('process.exit(0)',{eval:true})",'WORKER_THREAD'],
  ['native-import',"process.dlopen({},process.env.TEMP+'/denied.node')",'NATIVE_ADDON'],
  ['native-binding',"process.binding('fs')",'NATIVE_BINDING'],
  ['unapproved-cjs',"import {createRequire} from 'node:module';createRequire(process.env.YMCC_TRAY_CONFIG)(process.env.YMCC_COPY_COMPILER)",'NON_BOUND_CJS'],
 ];
 for(const [name,code,kind] of probes)await test('observer rejects '+name+' before the operation',()=>{
  const dir=path.join(outDir,'probe-'+name);fs.mkdirSync(dir);const cfg={...json(plan.configFile),trace:path.join(dir,'trace.json'),generated:dir},config=path.join(dir,'config.json');save(config,cfg);
  const child=spawnSync(process.execPath,['--no-addons','--require',path.join(HERE,'observe.cjs'),'--input-type=module','--eval',code],{cwd:plan.view,shell:false,windowsHide:true,encoding:'utf8',timeout:5000,maxBuffer:1024*1024,env:{SystemRoot:process.env.SystemRoot,WINDIR:process.env.WINDIR,TEMP:dir,TMP:dir,ESBUILD_WORKER_THREADS:'0',ESBUILD_BINARY_PATH:plan.authority.compilerBinary,YMCC_TRAY_CONFIG:config,YMCC_COPY_COMPILER:path.join(outDir,'same-name-copy/esbuild/lib/main.js')}});
  fs.writeFileSync(path.join(dir,'stdout.txt'),child.stdout??'');fs.writeFileSync(path.join(dir,'stderr.txt'),child.stderr??'');save(path.join(dir,'child.json'),{exitCode:child.status,error:child.error?child.error.message:null,signal:child.signal});
  const trace=json(cfg.trace);assert.equal(child.status,1);assert.equal(child.error,undefined);assert.equal(child.signal,null);assert(trace.finished&&trace.naturalExit);assert.deepEqual(trace.forbidden.map(r=>r.kind),[kind]);assert(!fs.existsSync(path.join(dir,'must-not-exist')));assert.equal(trace.compilers.length,0);
 });
 save(path.join(outDir,'tamper-checks.json'),tamper);
 const ok=results.every(r=>r.passed)&&base.receipts.every(verifyBaselineReceipt)&&verifyReceipt(receipt)&&unchanged([baselineReport,...archiveInputs]);
 const summary={schemaVersion:1,status:ok?'PASS':'FAIL',ok,checks:results.length,results,semanticControls:1,observerDenials:probes.length,baselineUnchanged:base.receipts.every(verifyBaselineReceipt)&&unchanged([baselineReport,...archiveInputs]),baselineVerification:{kind:baselineImplementationRoot?'historical-implementation-archive':'same-implementation',reportIdentity:baselineReport,implementationRoot:baselineImplementationRoot,archiveInputs,originalReportRewritten:false,positive11Rerun:false,historicalBehaviorApprovesCurrentImplementation:false},boundary:'Only owned copies, generated files, aliases, and declarations were changed. All denied operations stop in the observer before a system call. No physical tray, network, worker, arbitrary process or external file mutation was performed.',outputs:outputFiles(outDir)};
 save(path.join(outDir,'result.json'),summary);return summary;
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){
 const args=process.argv.slice(2),o={};for(let i=0;i<args.length;i+=2)o[args[i]]=args[i+1];
 try{const r=await maintenance({repoRoot:o['--repo'],baseline:o['--baseline'],outDir:o['--evidence-dir'],baselineImplementationRoot:o['--baseline-implementation-root']??null});console.log(JSON.stringify({status:r.status,checks:r.checks}));process.exitCode=r.ok?0:1;}catch(e){console.error(e.stack);process.exitCode=1;}
}
