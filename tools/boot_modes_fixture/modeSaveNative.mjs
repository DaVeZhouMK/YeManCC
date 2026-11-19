import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import {pathToFileURL} from 'node:url';import {createHash} from 'node:crypto';
export async function runModeSaveNativeOwner(out,args=[],options={}){
 const kinds=['revision','hash','mode','result','observed','applied','active','ack','unrelated','order','current'];
 const all=['global','xbox','steam','xbox-steam'].flatMap(mode=>kinds.map(kind=>({mode,kind,key:mode+'/'+kind})));
 assert(args.filter(v=>v.startsWith('--native-mode-save-')).every(v=>v.startsWith('--native-mode-save-only=')||v.startsWith('--native-mode-save-skip=')),'Unknown native mode-save option');
 const onlyArgs=args.filter(v=>v.startsWith('--native-mode-save-only='));assert(onlyArgs.length<=1,'Duplicate native mode-save selector');
 const only=onlyArgs.length?onlyArgs[0].slice('--native-mode-save-only='.length):undefined;
 const skipArgs=args.filter(v=>v.startsWith('--native-mode-save-skip='));assert(skipArgs.length<=1,'Duplicate native mode-save skip');
 const skip=skipArgs.length?skipArgs[0].slice('--native-mode-save-skip='.length):undefined;
 assert(!(only!==undefined&&skip!==undefined),'Choose only one native mode-save selector');
 if(skip!==undefined)assert(all.some(s=>s.key===skip),'Unknown exact native mode-save skip');
 const selected=(only===undefined?all:all.filter(s=>s.key===only)).filter(s=>s.key!==skip);assert(selected.length,'Unknown exact native mode-save selector');
 const native=options.native;assert(native&&typeof native.invoke==='function','Owned original native consumer required');
 const {createDiskRuntime}=await import(pathToFileURL(path.join(process.cwd(),'tools/boot_modes_fixture/diskSettings.mjs')));
 const disk=await createDiskRuntime(path.join(out,'disk'),native),a=disk.a,r=a.repository;
 const originalDiskHooks={...a.ports.hooks};
 const digest=file=>createHash('sha256').update(fs.readFileSync(file)).digest('hex');
 const sourceBefore=a.inputs.map(file=>path.resolve(process.cwd(),file)).filter(file=>fs.existsSync(file)).map(file=>({path:file,bytes:fs.statSync(file).size,sha256:digest(file)}));
 const compiledBundle=path.join(out,'disk/initial/boot-runtime.mjs'),bundleBefore=digest(compiledBundle);
 await r.loadSettings();const seed=structuredClone(disk.read()),rows=[],resultFile=path.join(out,'mode-save-native-results.json');
 try{
  for(const s of selected){
   a.boot.cancelBootModeDeferredMouse();a.boot.cancelBootModeLaunches();a.ports.reset();Object.assign(a.ports.hooks,originalDiskHooks);
   const data=structuredClone(seed);data.input.startupObservedBoot='fixture-windows-boot-1';disk.writeRaw(data);r.clearSettingsCache();Object.assign(a.ports.settings,await r.loadSettings());
   const doc=a.model.createBootModeDocument(await a.boot.captureBootModeSnapshot());
   const wanted={...structuredClone(doc.global.input),persona:'dualsense-edge',mouse:'off',outputTarget:{persona:'dualsense-edge'}};
   if(s.mode==='global')doc.global.input=wanted;else doc.scenes[s.mode].overrides.input=wanted;
   const ready=disk.read();ready.extensions.bootModes=structuredClone(doc);disk.writeRaw(ready);await r.loadSettings();
   const port=globalThis.__bootDiskPort,original=port.write;let boundary,error,result;
   port.write=async(file,content,transaction={})=>{
    const target=JSON.parse(content);
    if(!boundary&&target.extensions?.bootModes?.revision===doc.revision+1&&target.extensions?.bootModes?.current===s.mode){
     boundary={entered:true,attempt:target,traceIndex:disk.trace.length,callsIndex:a.ports.calls.length};
     const before=disk.read(),next=structuredClone(before);let expectedInputRevision;
     if(s.kind==='revision'){next.input.revision++;expectedInputRevision=before.input.revision;}
     if(s.kind==='hash'){next.input.revision++;next.input.gyroMotion.gyroWeight=Number(next.input.gyroMotion.gyroWeight)+.125;expectedInputRevision=before.input.revision;}
     if(s.kind==='mode'){next.extensions.bootModes.revision+=5;next.extensions.bootModes.boot=s.mode==='steam'?'xbox':'steam';}
     if(s.kind==='result')next.extensions.bootModesStartupResult={schema:1,boot:'new-result',mode:s.mode,revision:99,sourceRevision:98,status:'partial',applied:[],skipped:[],pending:[]};
     if(s.kind==='observed'){next.input.startupObservedBoot='new-observed-boot';expectedInputRevision=before.input.revision;}
     if(s.kind==='applied')next.extensions.bootModesAppliedBoot='new-applied-boot';
     if(s.kind==='active')next.extensions.bootModesActiveBoot='new-active-boot';
     if(s.kind==='ack'){
      const request=a.inputReceipts.inputApplicationRequest(next.input);
      Object.assign(next.input,{hostAcknowledgedRevision:request.revision,hostAcknowledgedConfigHash:request.hash,pendingConfigHash:request.hash,appliedRevision:request.revision,appliedConfigHash:request.hash,applyStatus:'active'});
     }
     if(s.kind==='unrelated')next.extensions.peer={keep:true};
     if(s.kind==='order')next.extensions.bootModes=Object.fromEntries(Object.entries(next.extensions.bootModes).reverse());
     if(s.kind!=='current'){
      const reply=await native.invoke(file,{operation:'write',content:JSON.stringify(next),baseline:before,...(expectedInputRevision===undefined?{}:{expectedInputRevision})});
      boundary.peerReply=reply;assert.equal(reply.ok,true,'Owned peer input must be admitted before old request '+s.key);
     }
     boundary.afterPeer=disk.read();boundary.raw=fs.readFileSync(disk.file,'utf8');boundary.backup=fs.existsSync(disk.file+'.bak')?fs.readFileSync(disk.file+'.bak','utf8'):null;
    }
    return original(file,content,transaction);
   };
   try{result=await a.boot.applyBootMode(s.mode,doc,{startup:true});}catch(e){error={message:e.message,failedStep:e.failedStep,rollback:e.rollback};}finally{port.write=original;}
   if(!boundary)fs.writeFileSync(path.join(out,'PRE-BOUNDARY-FAILURE.json'),JSON.stringify({key:s.key,error,result,trace:disk.trace,calls:a.ports.calls,final:disk.read()},null,2),{flag:'wx'});
   assert(boundary,'Actual repository apply never reached selected native mode-save '+s.key);
   const rejected=!['ack','unrelated','order','current'].includes(s.kind),after=disk.read();
   const row={key:s.key,rejected,actualError:error??null,boundary,after,postBoundaryTrace:disk.trace.slice(boundary.traceIndex),postBoundaryCalls:a.ports.calls.slice(boundary.callsIndex)};rows.push(row);
   if(rejected){
    assert(error,'Stale native mode-save must reject '+s.key);assert.equal(error.failedStep,'save-current-mode');
    assert.equal(fs.readFileSync(disk.file,'utf8'),boundary.raw,'Old rejected owner changed native file '+s.key);
    assert.equal(fs.existsSync(disk.file+'.bak')?fs.readFileSync(disk.file+'.bak','utf8'):null,boundary.backup,'Rejected old write rotated backup '+s.key);
    assert.equal(row.postBoundaryTrace.filter(v=>v.op==='write').length,1,'Old owner issued rollback writes '+s.key);
   }else{
    assert.equal(error,undefined,error?.message);assert(result);assert.equal(after.extensions.bootModes.current,s.mode);assert.equal(after.extensions.bootModes.revision,doc.revision+1);
    if(s.kind==='ack')for(const field of ['hostAcknowledgedRevision','hostAcknowledgedConfigHash','pendingConfigHash','appliedRevision','appliedConfigHash'])assert.equal(after.input[field],boundary.afterPeer.input[field],'Native ACK not preserved '+field);
    if(s.kind==='unrelated')assert.deepEqual(after.extensions.peer,{keep:true});
   }
   a.boot.cancelBootModeDeferredMouse();a.boot.cancelBootModeLaunches();
  }
  assert.equal(rows.length,selected.length);return rows;
 }finally{
  a.boot.cancelBootModeDeferredMouse();a.boot.cancelBootModeLaunches();
  const sourceAfter=sourceBefore.map(row=>({...row,currentSha256:digest(row.path)}));
  assert(sourceAfter.every(row=>row.sha256===row.currentSha256),'Mode-save source changed during actual native scope');assert.equal(digest(compiledBundle),bundleBefore,'Mode-save runtime bytes changed');
  fs.writeFileSync(resultFile,JSON.stringify({sourceBefore,sourceAfter,compiledBundle:{path:compiledBundle,bytes:fs.statSync(compiledBundle).size,sha256:bundleBefore},mockedModules:a.mockedModules,rows,actualRows:rows.length,complete:rows.length===selected.length,selectedNames:selected.map(s=>s.key),selectionOnly:only??null,selectionSkip:skip??null,
   scope:'Actual repository -> original final native lock and file writer. Feature/input/Steam endpoints remain original isolated ports; supplied peer facts are fixture boundary inputs, not observed OS actions.',hashCaseAlsoAdvancesRevision:true},null,2),{flag:'wx'});
 }
}
