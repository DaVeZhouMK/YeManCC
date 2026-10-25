/** Connect already filtered ordinary-ZIP observations to the frozen normalizer.
 * No original command/session is reconstructed; identities describe the new
 * projected array and retain the separate original archive identity.
 */
import {createHash} from 'node:crypto';
import {normalizeFanApiRecords} from './fan-api-log-consumer/normalize.mjs';
export function connectFanApiProjections(plan) {
 const original=plan.events??[],groups=new Map(),kept=original.filter(row=>row.event!=='fan-api.observation');
 const order=new Map(original.map((row,index)=>[row.id,index]));
 for(const event of original.filter(row=>row.event==='fan-api.observation')) {
  if(!groups.has(event.memberId))groups.set(event.memberId,[]);
  groups.get(event.memberId).push(event);
 }
 const batches=[];
 for(const [memberId,rows] of groups) {
  const records=rows.map(row=>({atMs:row.atMs,event:row.fanApiRecord?.event,details:row.fanApiRecord?.details}));
  const sha256=createHash('sha256').update(JSON.stringify(records)).digest('hex');
  const normalized=normalizeFanApiRecords(records,{sourceIdentity:{kind:'source-derived-fixture',sha256}});
  batches.push({memberId,sourceEventIds:rows.map(row=>row.id),projectedArrayIdentity:{kind:'source-derived-fixture',sha256},
   identityMeaning:'SHA of JSON.stringify(filtered reconstructed records); never the original ZIP or member-byte SHA',
   normalizedScope:normalized.scope,diagnostics:normalized.diagnostics});
  for(const diagnostic of normalized.diagnostics)plan.diagnostics.push({status:diagnostic.severity==='reject'&&!['OBSERVATION_VERSION_MISSING','OBSERVATION_VERSION_UNSUPPORTED'].includes(diagnostic.code)?'FAIL':'INCOMPLETE',
   code:'FAN_API_'+diagnostic.code,memberId,...(diagnostic.recordIndex?{firstLine:rows[diagnostic.recordIndex-1]?.line}:{}),count:1});
  for(const event of normalized.events) {
   const source=rows[event.line-1];
   kept.push({...event,id:source.id,memberId:source.memberId,line:source.line,
    sourceProjection:'ordinary-log-allowlisted-record',originalObservationTime:source.time??null,
    originalTimeBasis:source.timeBasis??null,originalAtMs:source.atMs,
    target:'owned-mock-only',timingStatus:source.timingStatus});
  }
 }
 if(groups.size) {
  plan.fanApiIngestion={ordinaryZipProjectionConnected:true,normalizerReadsOriginalZip:false,
   originalArchiveIdentity:plan.sourceArchive,originalMemberByteHashesAvailable:false,
   modelIdentityEstablished:false,originalUserIntentEstablished:false,originalSessionCorrelationEstablished:false,
   exactOriginalTimelineReplayed:false,physicalReceipts:0,batches};
  plan.status=plan.diagnostics.some(row=>row.status==='FAIL')?'FAIL':plan.status!=='READY'||plan.diagnostics.length?'INCOMPLETE':'READY';
 }
 plan.events=plan.status==='FAIL'?[]:kept.sort((a,b)=>order.get(a.id)-order.get(b.id));
 return plan;
}
