// This original semantic verifier is preserved byte for byte; no count/profile rewrite.
const need=(ok,code)=>{if(!ok)throw Error('CURRENT_ORIGINAL_'+code);};
const equal=(a,b)=>JSON.stringify(a)===JSON.stringify(b);
export function verifyOwnershipReturn(result,rollback){
 for(const connected of [0,1]){const name='PS-RETURN/ROG/owned-HID-foreign-peer/xinput-mask-'+connected,row=result.cases.find(r=>r.name===name),w=row?.ownedReturnWitness;need(w?.verdict==='foreign-peer'&&w.connectedMask===connected&&w.sourceMask===1&&w.queryComplete===true&&w.queryError===0&&w.oldGeneration===1&&w.oldRoot.length>0&&w.psQueriesDuringConsumer===1,'FOREIGN_PS_RETURN_ACTUAL_WITNESS');
  need(w.ownedMatches===(rollback?1:0)&&w.restoreCallsAfter-w.restoreCallsBefore===(rollback?0:1),'EXACT_PROFILE_MARKER_OWNERSHIP_FAULT');
  need(w.stateBefore.quarantined&&w.stateBefore.prepared&&w.stateBefore.epoch>0&&w.stateBefore.runId.length>0,'EXACT_OLD_TUPLE_REQUIRED');
  if(rollback)need(w.stateAfter.quarantined&&w.stateAfter.prepared&&w.stateAfter.epoch===w.stateBefore.epoch&&w.stateAfter.runId===w.stateBefore.runId,'FAULT_RETAINS_FOREIGN_MISCLASSIFIED_TUPLE');
  else need(!w.stateAfter.quarantined&&!w.stateAfter.prepared&&!w.stateAfter.hostHandle&&w.stateAfter.epoch===0&&w.stateAfter.runId==='','POSITIVE_FOREIGN_PEER_DOES_NOT_HOLD_OLD_TUPLE');
  const c=row.managedReceipt;need(c.nativeObservedProcessExit&&c.nativeObservedExitCode===5&&c.finalCleanupOk&&!c.hasController&&!c.hasContext,'REAL_OWNED_CHILD_CLEANUP_BEFORE_CONTROLLED_PNP');
  const verdicts=row.nativeEvents.filter(e=>e.event==='input-host-ps-return-proof').map(e=>e.detail.verdict);need(equal(verdicts,rollback?['unknown','present','absent']:['unknown','absent']),'ACTUAL_READER_THEN_CONSUMER_VERDICTS_REQUIRED');
  const end=result.assertionTrace.find(r=>r.case===name&&r.assertion==='owned-PS return witness leaves no new target or retained local resources');need(end?.passed===true,'LATER_COMPLETE_ABSENCE_MUST_CLEANUP');
 }
}
