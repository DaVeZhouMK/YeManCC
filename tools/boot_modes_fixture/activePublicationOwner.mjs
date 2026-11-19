import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {pathToFileURL} from 'node:url';
import {createHash} from 'node:crypto';
export async function runActivePublicationOwner(out,args=[],options={}){
const M=process.cwd();
const acknowledgement=args.includes('--active-publication-write-ack');
const claimOwner=args.includes('--active-publication-claim-owner');
assert(!(acknowledgement&&claimOwner),'Choose one active-publication scope');
const kinds=claimOwner?['claim-id','claim-id-removed','unrelated']:acknowledgement?['before-write','lost-ack','late-write','superseded']:['revision','hash','mode','result','observed','applied','active','ack','unrelated','order','current'];
const all=['global','xbox','steam','xbox-steam'].flatMap(mode=>kinds.map(kind=>({mode,kind,key:mode+'/'+kind})));
const onlyArgs=args.filter(s=>s.startsWith('--active-publication-only='));
assert(onlyArgs.length<=1,'Duplicate active-publication selector');
const selected=onlyArgs.length?all.filter(s=>s.key===onlyArgs[0].slice('--active-publication-only='.length)):all;
assert(selected.length,'Unknown active-publication selector');
const boundedClaim=args.includes('--active-publication-bounded-claim');
assert(!boundedClaim||(claimOwner&&selected.length===1&&selected[0].kind==='claim-id'),'Bounded recovery needs one exact claim-id case');
const source=options.bootModesSource;
const {createDiskRuntime}=await import(pathToFileURL(path.join(M,'tools/boot_modes_fixture/diskSettings.mjs')));
const native=options.native;
assert(native&&typeof native.invoke==='function','Original owned native required');
const digest=file=>createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const rows=[];
try{
 for(const s of selected){
  const disk=await createDiskRuntime(path.join(out,s.mode+'-'+s.kind),native,{bootModesSource:source,moduleDirectory:path.join(out,'active-publication-runtime')}),a=disk.a,r=a.repository;
  try{
   await r.loadSettings();const settings=disk.read();settings.input.startupObservedBoot='fixture-windows-boot-1';
   disk.writeRaw(settings);await r.loadSettings();Object.assign(a.ports.settings,await r.loadSettings());
   const doc=a.model.createBootModeDocument(await a.boot.captureBootModeSnapshot());doc.boot=s.mode;doc.bootEnabled=true;
   const ready=disk.read();ready.extensions.bootModes=structuredClone(doc);disk.writeRaw(ready);await r.loadSettings();
   const boot='fixture-windows-boot-1',steam=s.mode==='steam'||s.mode==='xbox-steam';
   if(acknowledgement){a.ports.state.steam=!steam;a.ports.state.earlyCommand=steam?null:'"C:\\SOFT\\YeMan\\PowerControl\\YeManSteam.bat"';}
   const port=globalThis.__bootDiskPort,original=port.write;let boundary,error,claimReceipt,deferred;
   const writes=[];
   port.write=async(file,content,transaction={})=>{
    const target=JSON.parse(content),before=disk.read();
    const isClaim=target.extensions?.bootModesAppliedBoot==='fixture-windows-boot-1'&&before.extensions?.bootModesAppliedBoot!=='fixture-windows-boot-1';
    const isMarker=target.extensions?.bootModesActiveBoot===boot&&before.extensions?.bootModesActiveBoot!==boot;
    const firstMarker=isMarker&&!boundary;
    if(firstMarker){
     boundary={entered:true,attempt:target,transaction,traceIndex:disk.trace.length};
     const next=structuredClone(before);let expectedInputRevision;
     if(s.kind==='revision'){next.input.revision++;expectedInputRevision=before.input.revision;}
     if(s.kind==='hash'){next.input.revision++;next.input.gyroMotion.gyroWeight=Number(next.input.gyroMotion.gyroWeight)+.125;expectedInputRevision=before.input.revision;}
     if(s.kind==='mode'){next.extensions.bootModes.revision+=5;next.extensions.bootModes.activeSnapshot.background.ui.backgroundOpacity=.38;}
     if(s.kind==='result')next.extensions.bootModesStartupResult={schema:1,boot:'fixture-windows-boot-1',mode:s.mode,revision:99,sourceRevision:98,status:'partial',applied:[],skipped:[],pending:['new-owner']};
     if(s.kind==='observed'){next.input.startupObservedBoot='new-observed-boot';expectedInputRevision=before.input.revision;}
     if(s.kind==='applied')next.extensions.bootModesAppliedBoot='new-applied-boot';
     if(s.kind==='active')next.extensions.bootModesActiveBoot='new-active-boot';
     if(claimOwner)assert.match(next.extensions.bootModesClaimWriteId,/^[0-9a-f-]{36}$/i);
     if(s.kind==='claim-id')next.extensions.bootModesClaimWriteId='d541cc21-a948-495e-99f4-5c31ef704c0e';
     if(s.kind==='claim-id-removed')delete next.extensions.bootModesClaimWriteId;
     if(s.kind==='ack'){
      const request=a.inputReceipts.inputApplicationRequest(next.input);
      Object.assign(next.input,{hostAcknowledgedRevision:request.revision,hostAcknowledgedConfigHash:request.hash,pendingConfigHash:request.hash,appliedRevision:request.revision,appliedConfigHash:request.hash,applyStatus:'active'});
     }
     if(s.kind==='unrelated')next.extensions.peer={keep:true};
     if(s.kind==='order')next.extensions.bootModes=Object.fromEntries(Object.entries(next.extensions.bootModes).reverse());
     if(s.kind!=='current'&&!acknowledgement){
      boundary.peerReply=await native.invoke(file,{operation:'write',content:JSON.stringify(next),baseline:before,...(expectedInputRevision===undefined?{}:{expectedInputRevision})});
      assert.equal(boundary.peerReply.ok,true,'Original peer write must pass');
     }
     boundary.raw=fs.readFileSync(disk.file,'utf8');boundary.afterPeer=disk.read();
     if(claimOwner){
      if(s.kind!=='unrelated'){
       // The settings-only receipt writer has no feature IPC context.
       const receiptFile=path.join(path.dirname(disk.file),'receipt-side','yeman-settings.json');
       fs.mkdirSync(path.dirname(receiptFile),{recursive:true});
       fs.copyFileSync(disk.file,receiptFile);
       fs.copyFileSync(disk.file+'.bak',receiptFile+'.bak');
       const backup=fs.readFileSync(receiptFile+'.bak','utf8');
       const staleReceipt=structuredClone(before);
       staleReceipt.extensions.bootModesStartupResult.pending.push('fixture stale receipt');
       boundary.staleReceiptReply=await native.invoke(receiptFile,{operation:'write',content:JSON.stringify(staleReceipt),baseline:before});
       boundary.receiptPreservedFiles=fs.readFileSync(receiptFile,'utf8')===boundary.raw&&fs.readFileSync(receiptFile+'.bak','utf8')===backup;
       if(boundedClaim){
        const recoveryFile=path.join(path.dirname(disk.file),'bounded-side','yeman-settings.json');
        fs.mkdirSync(path.dirname(recoveryFile),{recursive:true});
        const oversized=Buffer.alloc(4*1024*1024+1,0x78);
        fs.writeFileSync(recoveryFile,oversized);
        fs.copyFileSync(disk.file,recoveryFile+'.bak');
        const reply=await native.invoke(recoveryFile,{operation:'write',content:JSON.stringify(staleReceipt),baseline:before});
        boundary.boundedRecovery={staleReply:reply,mainPreserved:fs.readFileSync(recoveryFile).equals(oversized),
         backupPreserved:fs.readFileSync(recoveryFile+'.bak','utf8')===boundary.raw,files:fs.readdirSync(path.dirname(recoveryFile))};
        const recover=structuredClone(before);recover.ui.hideOverlayTime=!recover.ui.hideOverlayTime;
        const control=await native.invoke(recoveryFile,{operation:'write',content:JSON.stringify(recover),baseline:before});
        const restored=JSON.parse(fs.readFileSync(recoveryFile,'utf8'));
        Object.assign(boundary.boundedRecovery,{ordinaryAccepted:control.ok,newClaimPreserved:restored.extensions.bootModesClaimWriteId===next.extensions.bootModesClaimWriteId,
         ordinaryValuePreserved:restored.ui.hideOverlayTime===recover.ui.hideOverlayTime,
         receiptPreserved:JSON.stringify(restored.extensions.bootModesStartupResult)===JSON.stringify(next.extensions.bootModesStartupResult)});
       }
       // A different ordinary preference may still merge after claim turnover.
       const ordinary=structuredClone(before);ordinary.ui.hideOverlayTime=!ordinary.ui.hideOverlayTime;
       boundary.ordinaryReply=await native.invoke(file,{operation:'write',content:JSON.stringify(ordinary),baseline:before});
       assert.equal(boundary.ordinaryReply.ok,true);
       assert.equal(disk.read().extensions.bootModesClaimWriteId,next.extensions.bootModesClaimWriteId);
       boundary.rawBeforeMarker=fs.readFileSync(disk.file,'utf8');
       boundary.backupBeforeMarker=fs.readFileSync(disk.file+'.bak','utf8');
      }
     }
    }
    if(acknowledgement&&isMarker){
     writes.push(s.kind==='late-write'&&firstMarker?'deferred':'attempt');
     if(firstMarker&&s.kind==='before-write')disk.faults.failWrite=true;
     if(firstMarker&&s.kind==='lost-ack')disk.faults.dropWriteAck=true;
     if(firstMarker&&s.kind==='late-write'){
      deferred={file,content,transaction};throw Error('fixture marker acknowledgement timed out before native commit');
     }
     if(deferred){
      const late=await original(deferred.file,deferred.content,deferred.transaction);
      assert.equal(late.ok,true);writes.push('late-commit');deferred=undefined;
     }
    }
    const reply=await original(file,content,transaction);
    if(firstMarker&&s.kind==='superseded'){
     assert.equal(reply.ok,true);
     const committed=disk.read(),next=structuredClone(committed);
     next.input.revision++;next.input.startupObservedBoot='fixture-windows-boot-2';
     next.extensions.bootModesActiveBoot='fixture-windows-boot-2';
     const advance=await native.invoke(file,{operation:'write',content:JSON.stringify(next),baseline:committed,expectedInputRevision:committed.input.revision});
     assert.equal(advance.ok,true);boundary.afterPeer=disk.read();
     throw Error('fixture marker acknowledgement lost after a newer boot');
    }
    if(isClaim&&reply.ok){
     const committed=disk.read();claimReceipt=committed.extensions.bootModesStartupResult;
     assert.equal(committed.extensions.bootModesAppliedBoot,'fixture-windows-boot-1');
     assert.equal(claimReceipt?.boot,committed.extensions.bootModesAppliedBoot,'Claim marker and diagnostic receipt must commit together');
     assert.equal(claimReceipt.mode,s.mode);assert.equal(claimReceipt.revision,doc.revision);assert.equal(claimReceipt.status,'partial');
     assert.equal(claimReceipt.pending.length,1);assert.deepEqual(claimReceipt.applied,[]);
    }
    if(boundary&&!boundary.originalReply)boundary.originalReply=reply;
    return reply;
   };
   try{await a.boot.restoreSelectedBootMode();}catch(e){error={message:e.message,failedStep:e.failedStep};}finally{port.write=original;}
   const after=disk.read();
   const row={key:s.key,boundary,claimReceipt,error:error??null,after,writes,routeWrites:a.ports.calls.filter(c=>c.op==='startup.steam').map(c=>c.value),postBoundaryTrace:boundary?disk.trace.slice(boundary.traceIndex):disk.trace};rows.push(row);
   assert(boundary,'Final active-boot boundary not reached '+s.key);
   assert(claimReceipt,'Missing actual disk claim receipt '+s.key);
   if(claimOwner&&s.kind!=='unrelated'){
    row.markerPreservedMain=fs.readFileSync(disk.file,'utf8')===boundary.rawBeforeMarker;
    row.markerPreservedBackup=fs.readFileSync(disk.file+'.bak','utf8')===boundary.backupBeforeMarker;
    continue;
   }
   if(acknowledgement){
    const reopened=await disk.moduleAt('reopened');
    Object.assign(reopened.ports.settings,await reopened.repository.loadSettings());
    await reopened.boot.restoreSelectedBootMode();
    row.sameBootReopenWrites=reopened.ports.calls.filter(c=>c.op==='startup.steam').length;
    row.unchangedOnReopen=JSON.stringify(disk.read())===JSON.stringify(after);
    if(s.kind==='late-write')row.delayedCommitSettled=!deferred;
    continue;
   }
   const rejected=!['ack','unrelated','order','current'].includes(s.kind);
   if(rejected){
    assert.notEqual(after.extensions.bootModesActiveBoot,'fixture-windows-boot-1','Old startup must not mark newer input/configuration active '+s.key);
    assert.equal(after.input.revision,boundary.afterPeer.input.revision);
    if(s.kind==='result')assert.deepEqual(after.extensions.bootModesStartupResult,boundary.afterPeer.extensions.bootModesStartupResult);
   }else{
    assert.equal(error,undefined,error?.message);assert.equal(after.extensions.bootModesActiveBoot,'fixture-windows-boot-1');
    if(s.kind==='ack')assert.equal(after.input.hostAcknowledgedRevision,boundary.afterPeer.input.hostAcknowledgedRevision);
    if(s.kind==='unrelated')assert.deepEqual(after.extensions.peer,{keep:true});
   }
  }finally{a.boot.cancelBootModeDeferredMouse();a.boot.cancelBootModeLaunches();disk.flush();}
 }
 if(claimOwner){
  const failed=rows.filter(row=>!row.key.endsWith('/unrelated')&&(
   !row.error||row.after.extensions.bootModesActiveBoot==='fixture-windows-boot-1'||
   row.boundary.staleReceiptReply?.ok!==false||!row.boundary.receiptPreservedFiles||row.boundary.originalReply?.reason!=='revision-conflict'||
   !row.markerPreservedMain||!row.markerPreservedBackup||
   JSON.stringify(row.after.extensions.bootModesStartupResult)!==JSON.stringify(row.boundary.afterPeer.extensions.bootModesStartupResult)));
  assert.deepEqual(failed.map(row=>row.key),[],'Native settings writer must reject a superseded claim even when all older ownership fields match');
  if(boundedClaim){
   const recovery=rows[0].boundary.boundedRecovery;
   assert(recovery?.staleReply.ok===false&&recovery.mainPreserved&&recovery.backupPreserved&&recovery.files.length===2,
    'Rejected stale bounded recovery changed or archived source files');
   assert(recovery.ordinaryAccepted&&recovery.newClaimPreserved&&recovery.ordinaryValuePreserved&&recovery.receiptPreserved,
    'Safe bounded recovery did not keep the active claim and ordinary edit');
  }
 }
 if(acknowledgement){
  const failed=rows.filter(row=>{
   const [mode,kind]=row.key.split('/'),steam=mode==='steam'||mode==='xbox-steam';
   const {extensions}=row.after;
   if(row.sameBootReopenWrites||!row.unchangedOnReopen||row.routeWrites.length!==1||row.routeWrites[0]!==steam)return true;
   if(kind==='superseded')return !row.error||extensions.bootModesActiveBoot!=='fixture-windows-boot-2'||
    JSON.stringify(row.after)!==JSON.stringify(row.boundary.afterPeer);
   const attempts=kind==='lost-ack'?['attempt']:kind==='before-write'?['attempt','attempt']:['deferred','attempt','late-commit'];
   return !!row.error||extensions.bootModesActiveBoot!=='fixture-windows-boot-1'||extensions.bootModes.current!==mode||
    JSON.stringify(extensions.bootModesStartupResult)!==JSON.stringify(row.boundary.afterPeer.extensions.bootModesStartupResult)||
    JSON.stringify(row.writes)!==JSON.stringify(attempts)||(kind==='late-write'&&!row.delayedCommitSettled);
  });
  assert.deepEqual(failed.map(row=>row.key),[],'An uncertain final publication must not replay the scene or mislabel a committed result');
 }
}finally{
 fs.writeFileSync(path.join(out,'ACTIVE-PUBLICATION.json'),JSON.stringify({rows,actualRows:rows.length,selectedNames:selected.map(s=>s.key),complete:rows.length===selected.length,source:source??path.join(M,'src/bridge/bootModes.ts'),sourceSha256:digest(source??path.join(M,'src/bridge/bootModes.ts')),hashAlsoAdvancesRevision:true,featureEndpoints:'original isolated fixture ports; actual repository/native file writer only'},null,2),{flag:'wx'});
}

}
