import fs from 'node:fs';import assert from 'node:assert/strict';
export function verifyOriginalRealStoreCase({raw,parameter,processRow,journalPath,originalCase}){
 assert(['success','sharing-denial'].includes(parameter));assert.equal(processRow.exitCode,0);assert.equal(processRow.error,null);assert.equal(processRow.signal,null);
 assert.equal(raw.passed,true);assert.deepEqual(raw.failures,[]);assert.equal(raw.tests,1);assert.equal(raw.cases.length,1);assert.equal(raw.cases[0].name,originalCase);assert.equal(raw.cases[0].passed,true);
 assert.equal(raw.assertions,parameter==='success'?20:21);assert.equal(raw.cases[0].assertions,raw.assertions-3);
 assert.equal(raw.realFileTransaction,true);assert.equal(raw.noSdkDeviceIo,true);assert.equal(raw.logicalOnAccepted,false);assert.equal(raw.physicalReaderAccepted,false);assert.equal(raw.legacyOtherCases,'NOT_RUN');
 assert.equal(raw.storeEvidence.parameter,parameter);assert.equal(raw.storeEvidence.syntheticCanonicalCache,true);assert.equal(raw.storeEvidence.durableDischarges,parameter==='success'?1:0);assert.equal(raw.cases[0].owed,parameter!=='success');
 // These names come from the original counterJson producer, not the C++ member names.
 assert.equal(raw.cases[0].counters.commandOpens,0);assert.equal(raw.cases[0].counters.modewriterCalls,0);assert.equal(raw.cases[0].counters.liveSyntheticHandles,0);assert.equal(raw.cases[0].counters.badWrites,0);
 assert.equal(raw.finalIo.fileHandlesLive,0);assert.equal(raw.finalIo.directoryHandlesLive,0);assert.equal(raw.finalIo.leaseHeld,false);assert.equal(raw.finalIo.closeFailures,0);
 const text=fs.readFileSync(journalPath,'utf8');assert.equal(text,raw.storeEvidence.afterRecord);
 const before=JSON.parse(raw.storeEvidence.beforeRecord),after=JSON.parse(text);
 assert.equal(before.ownerPid,processRow.pid);assert.equal(typeof before.ownerCreationTime,'string');assert(/^[0-9]{16,20}$/.test(before.ownerCreationTime));assert.equal(before.modeReturnPending,true);
 assert.deepEqual(after,parameter==='success'?{...before,modeReturnPending:false,modeAttemptOwner:''}:before,'EXACT_ORIGINAL_RECORD_FIELDS_RETAINED');
 const a=raw.storeEvidence.afterIo,b=raw.storeEvidence.beforeIo;assert.equal(a.writes-b.writes,1);assert.equal(a.flushes-b.flushes,1);
 if(parameter==='success'){assert.notEqual(text,raw.storeEvidence.beforeRecord);assert.equal(a.moves-b.moves,1);assert.equal(a.replaces-b.replaces,0);}
 else{assert.equal(text,raw.storeEvidence.beforeRecord);assert.equal(a.moves-b.moves,6);assert.equal(a.replaces-b.replaces,1);assert(a.lastMoveError>0&&a.lastReplaceError>0);}
 return {accepted:true,assertions:raw.assertions,realLoadedOffOwnerFileTransaction:true,syntheticCanonicalCache:true,logicalOnAccepted:false,physicalReaderAccepted:false};
}
