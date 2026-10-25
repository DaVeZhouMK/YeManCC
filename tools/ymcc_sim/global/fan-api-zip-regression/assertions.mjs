/** Assertions about original CLI artifacts, never replacements for its consumer. */
import assert from 'node:assert/strict';
import path from 'node:path';
const physicalFlags=['hardwareValidated','operatingSystemSleepPerformed','installedApplicationMutated','fullProductAcceptance','exactEventTimelineReplayed'];
export function assertCliCompletion(execution,stdout,expectedExit,expectedStatus,signalFile) {
 assert.equal(execution.exitCode,expectedExit,'ORIGINAL_CLI_EXIT');
 assert.equal(execution.signal,null,'CLI_SIGNAL');
 assert.equal(execution.timedOut,false,'CLI_TIMEOUT');
 assert.equal(execution.outputTruncated,false,'CLI_OUTPUT_TRUNCATED');
 assert.equal(execution.launchError,null,'CLI_LAUNCH_ERROR');
 const markers=stdout.split(/\r?\n/).filter(line=>line.startsWith('SIGNAL_CONSUMPTION '));
 assert.deepEqual(markers,['SIGNAL_CONSUMPTION '+expectedStatus],'UNIQUE_SIGNAL_TERMINAL');
 const outputs=stdout.split(/\r?\n/).filter(line=>line.startsWith('RESULT '));
 assert.equal(outputs.length,1,'UNIQUE_ACTUAL_RESULT');
 assert.equal(path.resolve(outputs[0].slice(7)),path.resolve(signalFile),'ACTUAL_RESULT_PATH');
}
export function assertScope(plan,signal,archiveSha) {
 assert.equal(plan.sourceArchive.sha256,archiveSha,'PLAN_ARCHIVE_IDENTITY');
 assert.equal(plan.sourceArchive.stable,true,'STABLE_ARCHIVE');
 assert.equal(signal.sourceArchive.sha256,archiveSha,'CONSUMER_ARCHIVE_IDENTITY');
 assert.equal(plan.fanApiIngestion.ordinaryZipProjectionConnected,true,'ZIP_REALLY_CONNECTED');
 assert.equal(plan.fanApiIngestion.normalizerReadsOriginalZip,false);
 for(const key of ['originalMemberByteHashesAvailable','modelIdentityEstablished','originalUserIntentEstablished','originalSessionCorrelationEstablished','exactOriginalTimelineReplayed'])assert.equal(plan.fanApiIngestion[key],false,key);
 assert.equal(plan.fanApiIngestion.physicalReceipts,0);
 for(const key of physicalFlags)assert.equal(signal[key],false,key);
 assert(plan.events.every(row=>row.provenance==='source-derived-fixture'&&row.target==='owned-mock-only'),'NO_OBSERVED_FIELD_ACTIONS');
 assert(!plan.events.some(row=>['fan.intent','fan.power','fan.clock'].includes(row.event)),'NO_INFERRED_CONTROL_OR_POWER');
 for(const batch of plan.fanApiIngestion.batches) {
  assert.match(batch.projectedArrayIdentity.sha256,/^[a-f0-9]{64}$/);
  assert.notEqual(batch.projectedArrayIdentity.sha256,archiveSha,'ARRAY_SHA_NOT_ARCHIVE_SHA');
  assert.deepEqual(batch.normalizedScope.sourceIdentity,batch.projectedArrayIdentity);
 }
 for(const row of signal.results)if(row.result){const summary=row.result.summary;assert.equal(summary.curveWrites,0);assert.equal(summary.explicitRequests,0);assert.equal(summary.hasControlIntent,false);assert.equal(summary.physicalReceipts,0);}
}
export function assertPositiveReceipt(plan,signal) {
 assert.equal(plan.status,'READY');assert.equal(signal.status,'PASS');
 assert.equal(plan.fanApiIngestion.batches.length,1);assert.equal(plan.fanApiIngestion.batches[0].normalizedScope.status,'NORMALIZED');
 assert.equal(plan.members.length,1);assert.equal(plan.members[0].integrity,'CRC_VERIFIED');
 assert.deepEqual(plan.events.map(row=>[row.event,row.id,row.line,row.atMs]),[['startup.handshake','member-0001:1',1,0],['api.transport-failure','member-0001:2',2,2048]]);
 assert.equal(signal.results.length,1);assert.equal(signal.results[0].status,'PASS');
 assert.equal(signal.results[0].consumer,'tools/ymcc_sim/global/consumer/fan-signal-consumer.mjs');
 const consumer=signal.results[0].result;
 assert.deepEqual(consumer.evidence.map(row=>[row.input.eventId,row.input.event,row.feedback.status]),[['member-0001:1','startup.handshake','accepted'],['member-0001:2','api.transport-failure','rejected']]);
 assert.equal(consumer.evidence[0].consumer.entry,'FanHostLifecycle.start');
 assert.equal(consumer.evidence[1].consumer.entry,'FanHostLifecycle.getState -> HttpFanApiAdapter.getState');
 assert.equal(consumer.evidence[1].feedback.code,'WINHTTP_12029_SEND');
 assert.equal(consumer.evidence[1].feedback.actualProductRejection,true);
 assert.equal(consumer.summary.primaryFailure,'WINHTTP_12029_SEND');
 assert.equal(consumer.summary.consumedCount,2);assert.equal(consumer.summary.handshakes,1);
 for(const row of consumer.evidence)assert(['input','consumer','state','feedback'].every(key=>Object.hasOwn(row,key)));
 return {observedDiagnostic:{code:'WINHTTP_12029_SEND',consumerOutcome:'rejected',actualProductRejection:true},assertionMeaning:'the expected transport rejection was preserved; software pipeline PASS is not absence of logged errors'};
}
export function assertConflictReceipt(plan,signal) {
 assert.equal(plan.status,'FAIL');assert.deepEqual(plan.events,[]);
 assert(plan.diagnostics.some(row=>row.status==='FAIL'&&row.code==='FAN_API_ROOT_RECEIPT_RESPONSE_CONTRADICTION'));
 assert.equal(plan.fanApiIngestion.batches[0].normalizedScope.status,'REJECTED');
 assert.equal(signal.status,'BLOCKED');assert.deepEqual(signal.results,[]);
 assert(signal.diagnostics.some(row=>row.code==='SOURCE_PLAN_NOT_USABLE'));
}
export function assertMissingSchemaReceipt(plan,signal) {
 assert.equal(plan.status,'INCOMPLETE');assert.equal(signal.status,'INCOMPLETE');
 assert(plan.diagnostics.some(row=>row.status==='INCOMPLETE'&&row.code==='FAN_API_OBSERVATION_VERSION_MISSING'));
 assert(!plan.diagnostics.some(row=>row.status==='FAIL'));
 const rejected=plan.fanApiIngestion.batches.filter(row=>row.normalizedScope.status==='REJECTED'),accepted=plan.fanApiIngestion.batches.filter(row=>row.normalizedScope.status==='NORMALIZED');
 assert.equal(rejected.length,1);assert.equal(accepted.length,1);
 assert(!plan.events.some(row=>rejected[0].sourceEventIds.includes(row.id)));
 assert.deepEqual(plan.events.map(row=>row.event),['startup.handshake','api.transport-failure']);
 assert(plan.events.every(row=>accepted[0].sourceEventIds.includes(row.id)));
 assert.equal(signal.results.length,1);assert.equal(signal.results[0].status,'PASS');
 assert.equal(signal.results[0].result.summary.consumedCount,2);
 assert.equal(signal.results[0].result.evidence[1].feedback.code,'WINHTTP_12029_SEND');
}
