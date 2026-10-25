/** Test producer: the existing fixture executes unmodified fanApi + fanDiagnostics. */
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { createHash } from 'node:crypto';
import { createFanSignalFixture, SIMULATED_HANDSHAKE } from '../consumer/fan-signal-fixture.mjs';
const defaultRepo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../../..');
export async function generateFanApiFixture({repoRoot=defaultRepo,handshake=SIMULATED_HANDSHAKE,transportFailure=true}={}) {
 const fixture=createFanSignalFixture({repoRoot,handshake,simulation:{diagnosticLoggingEnabled:true}});
 try {
  const start=await fixture.clock.outcome(fixture.life.start());
  if(transportFailure){fixture.setStateTransportFailure(true);await fixture.clock.outcome(fixture.life.getState());}
  const records=JSON.parse(JSON.stringify(fixture.diagnostics.filter(r=>['api.response','api.transport-failure','api.request-failure'].includes(r.event))));
  if(!records.some(r=>r.event==='api.response'&&r.details.observationSchemaVersion===2))throw Error('ACTUAL_SCHEMA2_PROJECTION_NOT_OBSERVED');
  if(fixture.violations.length||!fixture.sourcesUnchanged())throw Error('FIXTURE_BOUNDARY_OR_SOURCE_CHANGED');
  const jsonl=Buffer.from(records.map(r=>JSON.stringify(r)).join('\n')+'\n');
  return {records,jsonl,sourceIdentity:{kind:'source-derived-fixture',sha256:createHash('sha256').update(jsonl).digest('hex')},
   producer:{scope:'source-derived-fixture',startRejected:start.error!==undefined,unmodifiedProductionModules:['src/bridge/fanApi.ts','src/bridge/fanDiagnostics.ts'],
    sourceHashes:fixture.sourceInputs(),sourceUnchanged:true,boundaryViolations:0,requests:fixture.requests.map(r=>({method:r.method,endpoint:r.endpoint})),actualHostStarted:false,hardwareIo:false}};
 } finally {fixture.dispose();}
}