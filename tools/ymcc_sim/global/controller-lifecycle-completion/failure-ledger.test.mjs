import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import vm from 'node:vm';
import {fileURLToPath,pathToFileURL} from 'node:url';
import {simulationCorePath} from '../release-host-environment.mjs';

const here=path.dirname(fileURLToPath(import.meta.url));
// Complete original modules run with inert dependency ports. These controls prove
// error-path capture, not the replaced producer/source/tool or device behavior.
for(const name of ['host-run.mjs','run.mjs'])for(const fault of ['stdout','stderr','nonzero','failure-receipt','failure-receipt-after-return','diagnostic-sink',...(name==='host-run.mjs'?['root-nonempty','root-empty','root-file','root-new-parent']:[])]){
 test(`${name} retains acquired process return on ${fault}`,async(t)=>{
  assert.equal(typeof vm.SourceTextModule,'function','Run with --experimental-vm-modules before creating test resources');
  const evidence=fs.realpathSync(simulationCorePath('Evidence'));
  const base=path.resolve(process.env.YMCC_SIM_FAILURE_LEDGER_TEST_ROOT??path.join(evidence,'ControllerFailureLedgerControls'));
  const relative=path.relative(evidence,base);
  assert(relative!==''&&!path.isAbsolute(relative)&&relative!=='..'&&!relative.startsWith('..'+path.sep));
  fs.mkdirSync(base,{recursive:true});
  const owner=fs.realpathSync(base);assert.equal(owner.toLowerCase(),base.toLowerCase());
  const root=fs.mkdtempSync(path.join(owner,'ymcc-lifecycle-ledger-'));
  try{
  const repo=path.join(root,'repo'),out=path.join(root,...(fault==='root-new-parent'?['nested','missing','output']:['output']));
  fs.mkdirSync(path.join(repo,'native'),{recursive:true});
  fs.mkdirSync(path.join(repo,'tools/ymcc_sim/global/inputhost-protocol'),{recursive:true});
  fs.writeFileSync(path.join(repo,'native/main.cpp'),'// inert source identity\n');
  fs.writeFileSync(path.join(repo,'input.txt'),'inert source');
  fs.writeFileSync(path.join(repo,'tool.txt'),'inert tool');
  fs.writeFileSync(path.join(repo,'tools/ymcc_sim/global/inputhost-protocol/toolchain-inputs.json'),JSON.stringify({dotnetRoot:repo}));
  const existingRoot=['root-nonempty','root-empty','root-file'].includes(fault);
  const snapshot=dir=>{
   if(!fs.existsSync(dir))return null;
   const stat=fs.lstatSync(dir);assert.equal(stat.isSymbolicLink(),false);
   if(stat.isFile())return {kind:'file',sha256:crypto.createHash('sha256').update(fs.readFileSync(dir)).digest('hex')};
   assert.equal(stat.isDirectory(),true);
   return {kind:'directory',entries:fs.readdirSync(dir).sort().map(name=>({name,content:snapshot(path.join(dir,name))}))};
  };
  if(existingRoot){
   if(fault==='root-file')fs.writeFileSync(out,'previous output path is a file');
   else{
    fs.mkdirSync(out);
    if(fault==='root-nonempty'){
     for(const name of ['inputs-before.json','positive.stdout.log','failure.json','result.json'])fs.writeFileSync(path.join(out,name),'previous evidence bytes: '+name);
     fs.mkdirSync(path.join(out,'positive'));fs.writeFileSync(path.join(out,'positive/result.json'),'previous producer receipt bytes');
    }
   }
  }
  const originalRoot=existingRoot?snapshot(out):null;
  const returned={pid:2468,exitCode:7,signal:null,timedOut:false,launchError:null,outputTruncated:false,durationMs:123.5,stdout:'completed stdout 中',stderr:'completed stderr 🙂'};
  let calls=0,hits=0,diagnosticAttempts=0;
  const diagnostics=[];
  const recordingFault=['failure-receipt','failure-receipt-after-return','diagnostic-sink'].includes(fault);
  const primary=Object.assign(new Error('controlled producer rejection before returning'),{code:'EPRODUCER'});
  const consolePort={error(...args){diagnosticAttempts++;if(fault==='diagnostic-sink')throw new Error('controlled diagnostic sink failure');diagnostics.push(args.join(' '));}};
  const code='ENOSPC';
  const message=`controlled ${code} at ${fault} writer`;
  const selected=p=>typeof p==='string'&&p===(recordingFault?path.join(out,'failure.json'):path.join(out,`positive.${fault}.log`));
  const controlledFs=new Proxy(fs,{get(target,key){
   if(key==='openSync')return function(p,...args){if(fault!=='nonzero'&&selected(p)){hits++;throw Object.assign(new Error(message),{code,path:p});}return target.openSync(p,...args);};
   if(key==='writeFileSync')return function(p,...args){if(fault!=='nonzero'&&selected(p)){hits++;throw Object.assign(new Error(message),{code,path:p});}return target.writeFileSync(p,...args);};
   return target[key];
  }});
  const id='controller.host-parent-lifetime';
  const processPort={execPath:process.execPath,env:{},argv:[]};
  const context=vm.createContext({process:processPort,console:consolePort,Buffer});
  const filename=path.join(here,name),source=fs.readFileSync(filename,'utf8');
  const module=new vm.SourceTextModule(source,{context,identifier:filename,initializeImportMeta(meta){meta.url=pathToFileURL(filename).href;}});
  const bindings={
   'node:fs':{default:controlledFs},'node:path':{default:path},'node:crypto':{default:crypto},'node:url':{fileURLToPath},
   './host-adapter.mjs':{CASES:{[id]:{mode:'parent',runner:'inert-parent.mjs',inputs:['input.txt']}},REPO:repo,INFRASTRUCTURE:[]},
   './adapter.mjs':{CASE_ID:'controller.lifecycle-completion',REPO:repo,INPUTS:['native/main.cpp','input.txt'],INFRASTRUCTURE:[],lifecycleCompletionTools:({capturePins})=>{assert.equal(capturePins,true);const tool=path.join(repo,'tool.txt');return {pins:[{path:tool,sha256:crypto.createHash('sha256').update(fs.readFileSync(tool)).digest('hex')}]};}},
   '../inputhost-protocol/adapter.mjs':{toolchainFiles:()=>[path.join(repo,'tool.txt')]},
   '../runner.mjs':{sourcePath:(base,relative)=>path.join(base,relative),runProcess:async()=>{calls++;if(recordingFault&&fault!=='failure-receipt-after-return')throw primary;return {...returned};}}
  };
   await module.link(specifier=>{
    assert(Object.hasOwn(bindings,specifier),'Unreviewed dependency: '+specifier);
    const values=bindings[specifier];return new vm.SyntheticModule(Object.keys(values),function(){for(const [key,value]of Object.entries(values))this.setExport(key,value);},{context,identifier:specifier});
   });
   await module.evaluate();
   const action=name==='host-run.mjs'?()=>module.namespace.execute(id,out):()=>module.namespace.execute(out,{configurations:['Release'],phase:'positive'});
   let error;try{await action();}catch(value){error=value;}
   assert(error,'Failure must remain a rejection');
   if(existingRoot){
    const after=snapshot(out);
    t.diagnostic(JSON.stringify({rootCase:fault,producerCalls:calls,oldRootBytesAndEntriesUnchanged:JSON.stringify(after)===JSON.stringify(originalRoot),actualErrorCode:error.code??null}));
    assert.equal(calls,0,'Existing output root must be rejected before any producer');
    assert.equal(error.code,'EEXIST');assert.equal(hits,0);assert.equal(diagnosticAttempts,0);
    assert.deepEqual(after,originalRoot,'Existing evidence bytes and directory membership must remain exact');
    return;
   }
   if(fault==='root-new-parent'){
    assert.equal(calls,1);assert.equal(error.code,'ENOENT');assert.equal(error.path,path.join(out,'positive/result.json'));
    assert.equal(hits,0);assert.equal(diagnosticAttempts,0);
    assert.equal(fs.statSync(out).isDirectory(),true);
    const failure=JSON.parse(fs.readFileSync(path.join(out,'failure.json'),'utf8'));
    assert.equal(failure.status,'FAIL');assert.equal(failure.steps.length,1);assert.equal(failure.steps[0].exitCode,returned.exitCode);
    assert.equal(fs.existsSync(path.join(out,'result.json')),false);
    return;
   }
   if(recordingFault){
    if(fault==='failure-receipt-after-return'){
     if(name==='host-run.mjs'){assert.equal(error.code,'ENOENT');assert.equal(error.path,path.join(out,'positive/result.json'));}
     else assert.equal(error.message,'ORIGINAL_PROCESS_EXIT_FAILED:positive:7');
    }else assert.equal(error,primary,'Failure-recording error must not replace the original exception');
    assert.equal(calls,1);assert.equal(hits,1);assert.equal(diagnosticAttempts,1);
    assert.equal(fs.existsSync(path.join(out,'failure.json')),false);
    assert.equal(fs.existsSync(path.join(out,'result.json')),false);
    if(fault==='diagnostic-sink')assert.equal(diagnostics.length,0);
    else{
     assert.equal(diagnostics.length,1);const prefix='YMCC_SIM_LIFECYCLE_FAILURE_RECORDING_FAILED ';
     assert(diagnostics[0].startsWith(prefix));const diagnostic=JSON.parse(diagnostics[0].slice(prefix.length));
     assert.equal(diagnostic.schemaVersion,1);assert.equal(diagnostic.status,'INCOMPLETE_RECORDING');
     assert.equal(diagnostic.file,path.join(out,'failure.json'));assert.equal(diagnostic.durableFallbackClaimed,false);
     assert.equal(diagnostic.originalError.message,error.message);assert.equal(diagnostic.originalError.code,error.code??null);
     assert.equal(diagnostic.recordingError.code,code);assert.equal(diagnostic.recordingError.message,message);
     assert.equal(diagnostic.steps.length,fault==='failure-receipt-after-return'?1:0);
     if(diagnostic.steps.length){
      const step=diagnostic.steps[0];assert.equal(step.name,'positive');
      for(const key of ['pid','exitCode','signal','timedOut','launchError','outputTruncated','durationMs'])assert.equal(step[key],returned[key],key);
      for(const stream of ['stdout','stderr']){
       assert.equal(step[stream].utf8Bytes,Buffer.byteLength(returned[stream]));
       assert.equal(step[stream].sha256,crypto.createHash('sha256').update(returned[stream]).digest('hex'));
       assert.equal(diagnostics[0].includes(returned[stream]),false,'Diagnostic must not duplicate full producer output');
      }
     }
    }
    return;
   }
   if(fault==='nonzero'){
    if(name==='host-run.mjs'){assert.equal(error.code,'ENOENT');assert.equal(error.path,path.join(out,'positive/result.json'));}
    else assert.equal(error.message,'ORIGINAL_PROCESS_EXIT_FAILED:positive:7');
   }
   else {assert.equal(error.code,code);assert.equal(error.message,message);}
   const failure=JSON.parse(fs.readFileSync(path.join(out,'failure.json'),'utf8'));
   assert.equal(failure.status,'FAIL');assert.equal(failure.originalFailurePreserved,true);
   assert.equal(calls,1,'No negative or retry producer may start after the first failed stage');
   assert.equal(hits,fault==='nonzero'?0:1);
   assert.equal(failure.steps.length,1,'Already returned process must be retained before log persistence');
   const step=failure.steps[0];
   for(const [key,value]of Object.entries(returned))assert.equal(step[key],value,key);
   assert.equal(step.name,'positive');assert(Array.isArray(step.args));
   assert.equal(fs.existsSync(path.join(out,'result.json')),false,'No success receipt after log failure or business failure');
   assert.equal(fs.existsSync(path.join(out,'inputs-before.json')),true);
  }finally{
   const actual=fs.realpathSync(root);
   assert.equal(path.dirname(actual),owner);assert(path.basename(actual).startsWith('ymcc-lifecycle-ledger-'));
   fs.rmSync(actual,{recursive:true,force:true});
  }
 });
}
