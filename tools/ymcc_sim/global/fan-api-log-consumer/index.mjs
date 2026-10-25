/** Connect schema-2 projections to the existing real product consumer in its NoIO fixture. */
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { createHash } from 'node:crypto';
import { replayFanSignals } from '../consumer/fan-signal-consumer.mjs';
import { normalizeFanApiRecords, FAN_API_LOG_LIMITS } from './normalize.mjs';
export { normalizeFanApiRecords, FAN_API_LOG_LIMITS };
const ownRoot=path.dirname(fileURLToPath(import.meta.url));
const defaultRepo=path.resolve(ownRoot,'../../../..');
const CODES=new Set(['FAN_DEVICE_GATE_DENIED','WINHTTP_12029_SEND','FAN_PROTOCOL_INCOMPATIBLE','FAN_API_RESPONSE_INVALID','FAN_HTTP_RESULT_UNKNOWN','CAPABILITY_RECEIPT_CONTRADICTORY','INPUT_ENTRY_MISSING','SOURCE_FIELDS_NOT_EXTRACTED','CAPABILITY_RECEIPT_MISSING','FEEDBACK_MISSING_OR_INVALID','PRODUCT_SOURCE_CHANGED_DURING_REPLAY','OFFLINE_BOUNDARY_VIOLATION','SOURCE_GENERATION_STALE','INPUT_GENERATION_INVALID','PROTOCOL_CONTRACT_INVALID']);
const code=value=>typeof value==='string'?(CODES.has(value)?value:'UNCLASSIFIED_PRODUCT_DIAGNOSTIC'):null;
const safeEnum=(value,values)=>values.includes(value)?value:'unknown';
const count=value=>Number.isSafeInteger(value)&&value>=0?value:null;
const endpoints=new Set(['/api/handshake','/api/state','/api/open','/api/open-events','/api/acquire-control','/api/heartbeat','/api/enable','/api/preset','/api/disable','/api/restore','/api/release-control','/api/close','/api/shutdown','/api/suspend','/api/resume']);
function sourceRows(rows) {
 return (Array.isArray(rows)?rows:[]).filter(r=>typeof r?.path==='string'&&!path.isAbsolute(r.path)&&!r.path.split(/[\\/]/).includes('..')&&/^[a-zA-Z0-9_./-]+$/.test(r.path)&&/^[a-f0-9]{64}$/.test(r.sha256??''))
  .map(r=>({path:r.path,sha256:r.sha256}));
}
function projectConsumer(replay,events) {
 const byId=new Map(events.map(e=>[e.id,e]));
 const evidence=(replay.evidence??[]).filter(row=>byId.has(row.input?.eventId)).map(row=>{
  const input=byId.get(row.input.eventId);
  return {eventId:input.id,event:input.event,provenance:'source-derived-fixture',
   sourceObservation:input.observation.event,sourceRootOk:input.receipt?.ok??null,
   sourceTransportResponseReceived:input.event==='api.transport-failure'?false:null,
   sourceHardwareWritesFlag:input.hardwareWritesObserved??null,
   consumer:{status:safeEnum(row.consumer?.status,['dispatched','blocked','not-dispatched','duplicate-suppressed']),
    entry:safeEnum(row.consumer?.entry,['FanHostLifecycle.start','FanHostLifecycle.getState -> HttpFanApiAdapter.getState']),
    requests:(row.consumer?.requests??[]).map(r=>({endpoint:endpoints.has(r.endpoint)?r.endpoint:'unknown',method:safeEnum(r.method,['POST','GET']),eventId:input.id})),},
   feedback:{status:safeEnum(row.feedback?.status,['accepted','rejected','unknown','missing','duplicate']),code:code(row.feedback?.code),stage:safeEnum(row.feedback?.stage,['input','consumer','state','feedback']),secondary:row.feedback?.secondary===true,
    actualProductRejection:row.feedback?.actualProductRejection===true,reconstructionOnly:true,hardwareReceipt:false},
   state:{controlReady:row.state?.after?.controlReady===true,hasControlIntent:row.state?.after?.hasControlIntent===true,
    curveWrites:count(row.state?.after?.curveWrites),starts:count(row.state?.after?.starts),stops:count(row.state?.after?.stops)}};
 });
 const summary=replay.summary??{};
 return {status:safeEnum(replay.status,['PASS','FAIL','INCOMPLETE']),invoked:true,
  summary:{eventCount:count(summary.eventCount),consumedCount:count(summary.consumedCount),primaryFailure:code(summary.primaryFailure),
   curveWrites:count(summary.curveWrites),explicitRequests:count(summary.explicitRequests),starts:count(summary.starts),stops:count(summary.stops),requestCount:count(summary.requestCount),pendingTimers:count(summary.pendingTimers),physicalReceipts:0},
  diagnostics:(replay.diagnostics??[]).map(r=>({code:code(r.code),stage:safeEnum(r.stage,['input','consumer','state','feedback']),status:safeEnum(r.status,['FAIL','INCOMPLETE']),eventId:byId.has(r.eventId)?r.eventId:null})),evidence,
  compatibility:{status:safeEnum(replay.compatibility?.status,['COMPATIBLE_PROTOCOL_ONLY','INCOMPATIBLE','UNKNOWN_PROTOCOL']),protocolExpected:Number.isSafeInteger(replay.compatibility?.protocolExpected)?replay.compatibility.protocolExpected:null,
   protocolObserved:(replay.compatibility?.protocolObserved??[]).filter(v=>Number.isSafeInteger(v)&&v>0),runtimeAssemblyGate:'not-executed-by-signal-consumer'},
  sourceHashes:sourceRows(replay.sourceHashes),
  boundary:{externalNetwork:false,realHostProcess:false,hardwareWrites:false,realOperatingSystemSleep:false,originalUserIntentReplayed:false}};
}
export async function consumeFanApiRecords(records,{repoRoot=defaultRepo,sourceIdentity,limits}={}) {
 const normalized=normalizeFanApiRecords(records,{sourceIdentity,limits});
 const scope={...normalized.scope,softwareConsumer:'existing replayFanSignals → unmodified FanHostLifecycle / HttpFanApiAdapter',originalLogActionsReexecuted:false,physicalHardwareValidated:false,fullGoalComplete:false};
 if(normalized.scope.status==='REJECTED'||!normalized.events.length)
  return {status:normalized.scope.status==='REJECTED'?'REJECTED':'INCOMPLETE',normalization:normalized,consumer:{invoked:false},scope};
 const raw=await replayFanSignals({repoRoot,events:normalized.events});
 const consumer=projectConsumer(raw,normalized.events);
 for(const name of ['index.mjs','normalize.mjs'])consumer.sourceHashes.push({path:'tools/ymcc_sim/global/fan-api-log-consumer/'+name,sha256:createHash('sha256').update(fs.readFileSync(path.join(ownRoot,name))).digest('hex')});
 return {status:consumer.status==='FAIL'?'FAIL':normalized.scope.status==='INCOMPLETE'||consumer.status!=='PASS'?'INCOMPLETE':'PASS',normalization:normalized,consumer,scope};
}