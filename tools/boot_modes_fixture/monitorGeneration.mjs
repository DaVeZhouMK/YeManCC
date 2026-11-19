import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import {pathToFileURL} from 'node:url';import {createHash} from 'node:crypto';
export async function runMonitorGeneration(out,args=[],options={}){
const M=process.cwd();
const lockRaces=args.includes('--monitor-generation-lock-races');
const kinds=lockRaces?['prior-claim','locked-claim','locked-output','locked-boot','locked-unrelated']:['generation','current','dispose'];
const all=['global','xbox','steam','xbox-steam'].flatMap(mode=>[true,false].flatMap(enabled=>kinds.map(kind=>({mode,enabled,kind,key:mode+'/'+(enabled?'on':'off')+'/'+kind}))));
const choices=args.filter(v=>v.startsWith('--monitor-generation-only='));assert(choices.length<=1,'Duplicate monitor-generation selector');
const selected=choices.length?all.filter(s=>s.key===choices[0].slice('--monitor-generation-only='.length)):all;assert(selected.length,'Unknown monitor-generation selector');
const source=options.bootModesSource;
const {createDiskRuntime}=await import(pathToFileURL(path.join(M,'tools/boot_modes_fixture/diskSettings.mjs')));
const native=options.native;assert(native&&typeof native.invoke==='function','Owned original native required');
const digest=p=>createHash('sha256').update(fs.readFileSync(p)).digest('hex');
const rows=[];
const filePair=file=>({main:digest(file),backup:fs.existsSync(file+'.bak')?digest(file+'.bak'):null});
async function verifyWriteRace(s,disk,actualTarget){
 const {ports:q}=disk.a,file=disk.file,claim={boot:'fixture-windows-boot-1',writeId:'fixture-monitor-claim-a'};
 const row={key:s.key,expectedState:s.enabled?'active':'disabled',initial:filePair(file)};rows.push(row);
 assert(row.initial.backup,'Fixture needs an existing backup');
 const emit=()=>q.emitMonitorOutputApplied(actualTarget,{ok:true,applied:s.enabled?'ymcc':'off'},{startupReceiptOwner:claim});
 const locked=s.kind.startsWith('locked-');
 let pending;
 const traceAt=disk.trace.length;
 try{
  if(locked){
   disk.faults.holdWrite=true;
   pending=emit();
   const deadline=Date.now()+1500;
   while(!disk.faults.releaseWrite&&Date.now()<deadline)await new Promise(resolve=>setTimeout(resolve,1));
   assert.equal(typeof disk.faults.releaseWrite,'function','Original consumer did not reach native write boundary');
   assert.equal(disk.trace.slice(traceAt).filter(v=>v.op==='write').length,1);
   assert.deepEqual(filePair(file),row.initial,'Held write touched the file pair');
  }
  const baseline=disk.read(),next=structuredClone(baseline);
  if(s.kind==='locked-output'){
   next.rtss.monitorOutput={...actualTarget,enabled:!s.enabled,ymccEnabled:!s.enabled};
  }else if(s.kind==='locked-boot')next.input.startupObservedBoot='fixture-windows-boot-2';
  else if(s.kind==='locked-unrelated')next.extensions.fixtureIndependentPreference='kept';
  else next.extensions.bootModesClaimWriteId='fixture-monitor-claim-b';
  const competing=await native.invoke(file,{operation:'write',content:JSON.stringify(next),baseline});
  assert.equal(competing.ok,true,'Independent native write did not land before old ACK');
  row.competing=filePair(file);
  assert.notEqual(row.competing.main,row.initial.main);
  assert.equal(disk.read().extensions.bootModesStartupResult.modules.monitor.state,'waiting');
  if(locked){disk.faults.releaseWrite();await pending;}
  else await emit();
  row.final=filePair(file);
  row.attempts=disk.trace.slice(traceAt).filter(v=>v.op==='write').length;
  const after=disk.read();row.state=after.extensions.bootModesStartupResult.modules.monitor.state;
  assert.equal(row.attempts,locked?1:0,'Expected an exact renderer write count');
  if(s.kind==='locked-unrelated'){
   assert.equal(row.state,row.expectedState);
   assert.equal(after.extensions.fixtureIndependentPreference,'kept');
   assert.equal(row.final.backup,row.competing.main,'Successful merge must back up the latest preference');
  }else{
   assert.equal(row.state,'waiting','Retired monitor receipt advanced a replacement configuration');
   assert.deepEqual(row.final,row.competing,'Retired monitor receipt touched main or backup bytes');
  }
  assert.equal(q.calls.filter(v=>v.op==='monitor.apply'||v.op==='input.cas'||v.op==='app.launch').length,0);
  row.pass=true;
 }finally{
  disk.faults.holdWrite=false;
  disk.faults.releaseWrite?.();
  if(pending)await pending;
 }
}
try{
 for(const s of selected){
  const disk=await createDiskRuntime(path.join(out,s.key.replaceAll('/','-')),native,{bootModesSource:source}),a=disk.a,r=a.repository,q=a.ports;
  let stop,stopFresh;
  try{
   await r.loadSettings();Object.assign(q.settings,await r.loadSettings());
   const doc=a.model.createBootModeDocument(await a.boot.captureBootModeSnapshot());doc.boot=s.mode;doc.current=s.mode;doc.bootEnabled=true;doc.revision=8;
   const target={...doc.global.monitor.preferences,enabled:s.enabled,output:'ymcc',ymccEnabled:s.enabled,rtssEnabled:false};
   doc.global.monitor.preferences=structuredClone(target);
   const seed=disk.read();seed.input.startupObservedBoot='fixture-windows-boot-1';seed.rtss.monitorOutput=structuredClone(target);
   seed.extensions={...seed.extensions,bootModes:doc,bootModesAppliedBoot:'fixture-windows-boot-1',bootModesActiveBoot:'fixture-windows-boot-1',
    bootModesStartupResult:{schema:1,boot:'fixture-windows-boot-1',mode:s.mode,sourceRevision:7,revision:8,status:'partial',applied:[],skipped:[],pending:['监控：等待原监控 owner 确认'],modules:{monitor:{state:'waiting',requested:structuredClone(target)}}}};
   if(lockRaces)seed.extensions.bootModesClaimWriteId='fixture-monitor-claim-a';
   const beforeSeed=disk.read();const seeded=await native.invoke(disk.file,{operation:'write',content:JSON.stringify(seed),baseline:beforeSeed,expectedInputRevision:beforeSeed.input.revision});assert.equal(seeded.ok,true);
   await r.loadSettings();const actualTarget=structuredClone(disk.read().rtss.monitorOutput);
   stop=a.boot.observeBootModeRuntimeReceipts();await new Promise(resolve=>setImmediate(resolve));
   if(lockRaces){await verifyWriteRace(s,disk,actualTarget);continue;}
   const originalGeneration=r.getSettingsGeneration();
   if(s.kind==='generation'){r.clearSettingsCache();await r.loadSettings();assert.notEqual(r.getSettingsGeneration(),originalGeneration);}
   if(s.kind==='dispose'){stop();stop=undefined;}
   const before=fs.readFileSync(disk.file,'utf8'),backup=fs.existsSync(disk.file+'.bak')?fs.readFileSync(disk.file+'.bak','utf8'):null,traceAt=disk.trace.length;
   await q.emitMonitorOutputApplied(actualTarget,{ok:true,applied:s.enabled?'ymcc':'off'});
   const after=disk.read(),row={key:s.key,originalGeneration,currentGeneration:r.getSettingsGeneration(),after,
    trace:disk.trace.slice(traceAt),beforeRawSha256:createHash('sha256').update(before).digest('hex'),afterRawSha256:digest(disk.file)};rows.push(row);
   if(s.kind==='current'){
    assert.equal(after.extensions.bootModesStartupResult.modules.monitor.state,s.enabled?'active':'disabled');
    assert(row.trace.some(v=>v.op==='write'));
   }else{
    assert.equal(after.extensions.bootModesStartupResult.modules.monitor.state,'waiting','Retired observer must not acknowledge a new configuration generation '+s.key);
    assert.equal(fs.readFileSync(disk.file,'utf8'),before,'Retired observer wrote file '+s.key);
    assert.equal(fs.existsSync(disk.file+'.bak')?fs.readFileSync(disk.file+'.bak','utf8'):null,backup,'Retired observer rotated backup '+s.key);
    assert.equal(row.trace.filter(v=>v.op==='write').length,0);
   }
   if(s.kind==='generation'){
    stop();stop=undefined;stopFresh=a.boot.observeBootModeRuntimeReceipts();await new Promise(resolve=>setImmediate(resolve));
    await q.emitMonitorOutputApplied(actualTarget,{ok:true,applied:s.enabled?'ymcc':'off'});
    row.fresh=disk.read();assert.equal(row.fresh.extensions.bootModesStartupResult.modules.monitor.state,s.enabled?'active':'disabled','Fresh observer must accept original acknowledgement '+s.key);
   }
   assert.equal(q.calls.filter(v=>v.op==='monitor.apply'||v.op==='input.cas'||v.op==='app.launch').length,0);
  }finally{stop?.();stopFresh?.();a.boot.cancelBootModeLaunches();a.boot.cancelBootModeDeferredMouse();disk.flush();}
 }
}finally{
 fs.writeFileSync(path.join(out,'MONITOR-GENERATION.json'),JSON.stringify({rows,actualRows:rows.length,selectedNames:selected.map(v=>v.key),complete:rows.length===selected.length&&(!lockRaces||rows.every(r=>r.pass)),source:source??path.join(M,'src/bridge/bootModes.ts'),sourceSha256:digest(source??path.join(M,'src/bridge/bootModes.ts')),scope:lockRaces?'Real repository and production native transaction with private files; deterministic interleaving before native submit, monitor event and cross-process mutex isolated':'Original repository/native transaction and actual files; monitor event is an isolated original port'},null,2),{flag:'wx'});
}

}
