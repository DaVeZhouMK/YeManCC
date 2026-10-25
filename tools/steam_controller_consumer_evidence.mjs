#!/usr/bin/env node
/** Explicit-directory offline analysis. No processes, devices, network, archive
 * extraction or collectors. ACK/config/device presence never proves game input. */
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { fileURLToPath } from 'node:url';
export const SCHEMA = 'ymcc-steam-controller-consumer-evidence-v2';
// A numeric FILETIME/u64 above 2^53 must retain its original token. The metadata
// distinguishes a JSON numeric token from a (wrongly typed) quoted ACK scalar.
const exactNumericFields = new WeakMap();
export function parseEvidenceJson(text) {
  return JSON.parse(text,function(key,value,context) {
    if(typeof value==='number'&&Number.isInteger(value)&&!Number.isSafeInteger(value)) {
      if(!/^-?\d+$/.test(context?.source??''))throw Error('Unsafe integer lacks an exact decimal JSON token.');
      let fields=exactNumericFields.get(this);if(!fields)exactNumericFields.set(this,fields=new Set());fields.add(key);
      return context.source;
    }
    return value;
  });
}
function positiveU64(object,key) {
  const value=object[key];
  if(typeof value==='number')return Number.isSafeInteger(value)&&value>0?value:null;
  if(typeof value==='string'&&exactNumericFields.get(object)?.has(key)&&/^\d+$/.test(value)) {
    const n=BigInt(value);return n>0n&&n<=18446744073709551615n?value:null;
  }
  return null;
}

function nativeOwnerProjection(row,source,time) {
  const n=row.nativeOwner,result={status:'unknown',identity:null,reasons:[],
    source:{...source,jsonPointer:'/nativeOwner'},scope:'native-boot-self-OS-identity-report;not-endpoint-or-game-consumption'};
  if(!n||typeof n!=='object'||Array.isArray(n)){result.reasons.push('owner-not-retained');return result;}
  if(n.schema!==1||n.authority!=='native-self-process-identity'||n.creationSource!=='GetProcessTimes-self'||n.readOnly!==true||n.gameInputUnverified!==true||n.steamConsumptionProven!==false){result.reasons.push('unsupported-owner-contract');return result;}
  if(n.querySucceeded!==true||n.status!=='observed'){result.reasons.push('self-query-not-observed');return result;}
  if(!Number.isSafeInteger(n.pid)||n.pid<=0||n.pid>0xffffffff){result.reasons.push('invalid-owner-PID');return result;}
  if(n.pid!==row.pid){result.status='contradicted';result.reasons.push('owner-PID-differs-from-native-common-PID');return result;}
  if(typeof n.creationFiletime!=='string'||exactNumericFields.get(n)?.has('creationFiletime')||! /^[1-9]\d*$/.test(n.creationFiletime)||BigInt(n.creationFiletime)<=0n||BigInt(n.creationFiletime)>18446744073709551615n){result.reasons.push('missing-or-wrong-exact-birth-string');return result;}
  const birthMs=filetimeMs(n.creationFiletime);
  if(time.status!=='normalized'||!Number.isSafeInteger(birthMs)||!Number.isSafeInteger(time.utcIntervalEndExclusiveMs)){result.reasons.push('owner-clock-unknown');return result;}
  if(birthMs>=time.utcIntervalEndExclusiveMs){result.status='contradicted';result.reasons.push('owner-birth-after-boot-time-interval');return result;}
  result.status='observed';result.identity={pid:n.pid,creationFiletime:n.creationFiletime};return result;
}
function nativeParentRelationship(life,boots,eventTime) {
  const result={status:'unknown',nativeLifetime:null,nativeOwnerSource:null,reasons:[],
    scope:'exact-native-PID-and-birth-to-reader-parent-only;not-shared-wake-endpoint-Steam-or-game',sharedWakeOwnershipProven:false,gameInputUnverified:true};
  if(life?.status!=='bound-observer-start'||eventTime?.status!=='normalized'){result.reasons.push('observer-start-or-event-clock-unknown');return result;}
  const eligible=boots.filter(b=>b.pid===life.parentPid&&b.time.status==='normalized'&&b.time.utcEpochMs<=life.start.time.utcEpochMs);
  if(!eligible.length){result.reasons.push('no-earlier-native-owner-boot');return result;}
  const latest=Math.max(...eligible.map(b=>b.time.utcEpochMs)),candidates=eligible.filter(b=>b.time.utcEpochMs===latest);
  if(candidates.length!==1){result.reasons.push('ambiguous-native-boot-lifetime');return result;}
  const boot=candidates[0];result.nativeOwnerSource=boot.nativeOwner.source;
  if(boot.nativeOwner.status!=='observed'){result.status=boot.nativeOwner.status;result.reasons.push('native-owner-'+boot.nativeOwner.status);return result;}
  if(boot.nativeOwner.identity.creationFiletime!==life.parentCreated){result.status='contradicted';result.reasons.push('exact-parent-birth-differs-from-native-owner');return result;}
  const later=boots.filter(b=>b.pid===life.parentPid&&b.time.status==='normalized'&&b.time.utcEpochMs>boot.time.utcEpochMs&&b.time.utcEpochMs<=eventTime.utcEpochMs);
  if(later.length){result.status=later.some(b=>b.nativeOwner.status==='observed'&&b.nativeOwner.identity.creationFiletime!==life.parentCreated)?'contradicted':'unknown';result.reasons.push('native-PID-boot-lifetime-changed-after-observer-start');return result;}
  result.status='matched';result.nativeLifetime=boot.token;result.matchingAxes=['native PID=reader parent PID','native exact FILETIME=reader parent exact FILETIME'];return result;
}
const ACK_ORIGINS=['strict-fast-validator-equality-and-parsed-host','existing-validator-and-parsed-dom-reply'];
function ackProjection(row,source,identity,token) {
  const a=row.ackIdentity,result={status:'unknown',authority:'native-local-ack-diagnostic',
    evidenceScope:'L1-local-native-validation-self-report;not-independent-OS-Steam-or-game-input',
    source:{...source,jsonPointer:'/ackIdentity'},timeScope:'enclosing-sparse-event-time;not-ACK-arrival-or-action-time',
    protocolTuple:null,currentSamplesAreProtocolIdentity:false,gameInputUnverified:true,steamConsumptionProven:false,reasons:[]};
  if(!a||typeof a!=='object'||Array.isArray(a)){result.reasons.push(a===null?'projection-null':'projection-missing-or-not-object');return result;}
  if(row.inputChainEvidenceSchema!==3||!/^native-(?:lifecycle|detail)\.log(?:\.[1-9][0-9]?)?$/.test(source.file))result.reasons.push('not-native-schema-3-producer');
  if(a.schema!==1||a.authority!=='native-local-ack-diagnostic')result.reasons.push('unsupported-schema-or-authority');
  if(Object.hasOwn(a,'producerScope')&&(a.producerScope!=='native-lifecycle-command-receipt'||row.event!=='input-host-command-result'||!['PREPARE_TARGET','RECREATE_TARGET','RELEASE_TARGET','SHUTDOWN'].includes(row.command)||(a.tupleVerified===true&&a.command!==row.command)))result.reasons.push('unsupported-or-mismatched-lifecycle-command-projection');
  for(const k of ['nativeAccepted','tupleVerified','gameInputUnverified','steamConsumptionProven'])if(typeof a[k]!=='boolean')result.reasons.push('wrong-boolean-type:'+k);
  if(a.gameInputUnverified!==true||a.steamConsumptionProven!==false)result.reasons.push('invalid-consumption-boundary');
  if(typeof a.nativeAccepted==='boolean')result.nativeAccepted=a.nativeAccepted;
  if(typeof a.tupleVerified==='boolean')result.tupleVerifiedReported=a.tupleVerified;
  if(result.reasons.length)return result;
  if(!a.nativeAccepted||!a.tupleVerified) {
    const expectedStatus=a.nativeAccepted?'unknown':'not-accepted';
    if(a.tupleVerified||a.status!==expectedStatus||a.identityOrigin!=='none') {result.status='contradicted';result.reasons.push('inconsistent-acceptance-status-origin');}
    else {result.status=expectedStatus;result.reasons.push(a.nativeAccepted?'accepted-without-verified-projection':'native-frame-not-accepted');}
    return result;
  }
  if(a.status!=='verified-local-ack'||!ACK_ORIGINS.includes(a.identityOrigin)||a.requestBindingCapturedBeforeRequest!==true||a.protocolGenerationScope!=='input-host-bound-protocol;not-current-shared-power')result.reasons.push('missing-or-invalid-request-binding-proof');
  for(const k of ['runId','hostInstanceId','requestId'])if(typeof a[k]!=='string'||!a[k].length||a[k].length>4096||/[\x00-\x1f]/.test(a[k]))result.reasons.push('missing-or-wrong-endpoint-id:'+k);
  const values={};for(const k of ['protocolPowerGeneration','hostEpoch','wireSequence']){values[k]=positiveU64(a,k);if(values[k]===null)result.reasons.push('missing-or-wrong-u64:'+k);}
  if(result.reasons.length)return result;
  result.status='verified-local-ack';result.identityOrigin=a.identityOrigin;result.requestBindingCapturedBeforeRequest=true;
  result.protocolTuple={...values,runToken:token('run',a.runId),hostToken:token('host',a.hostInstanceId),requestToken:token('request',a.requestId),
    namespace:identity.ymccLifetime?`input-host-bound-protocol:${identity.ymccLifetime}`:null,
    namespaceStatus:identity.ymccLifetime?'bound-native-lifetime':'unknown',protocolGenerationScope:a.protocolGenerationScope};
  result.currentSharedPowerRelation='separate-namespace;equal-or-different-numbers-do-not-prove-or-refute-shared-wake';
  return result;
}

// Names are opaque, exact, bundle-salted strings. No case folding, USB ancestor
// substitution, VID/PID, GUID or WGI heuristic is allowed in a PnP name join.
const ENDPOINT_LIMITS=Object.freeze({detailChars:32768,roleItems:64,totalItems:256,hardwareIdsPerItem:32,instanceChars:511,hardwareIdChars:1024,bridgeDelayMs:5000,snapshots:1024,readers:4096,boundaries:4096,comparisonsPerReader:256});
const PNP_ROLES=[
  ['primary-root','pnp-primary-root-ids','pnp-primary-roots'],
  ['xbox-companion-root','pnp-xbox-companion-root-ids','pnp-xbox-companion-roots'],
  ['hid-child','pnp-hid-ids','pnp-hid-children'],
  ['steamdeck-root','pnp-steamdeck-root-ids','pnp-steamdeck-roots'],
  ['steamdeck-hid-child','pnp-steamdeck-hid-ids','pnp-steamdeck-hid-children']
];
const opaqueEndpointText=(value,max)=>typeof value==='string'&&value.length>0&&value.length<=max&&!/[\x00-\x1f\x7f\ufffd]/.test(value);
const endpointText=(value,max)=>typeof value==='string'&&value.length>0&&value.length<=max&&!/[\x00-\x20\x7f\ufffd]|\.\.\.|…|truncat(?:ed|ion)/i.test(value);
const instanceName=value=>endpointText(value,ENDPOINT_LIMITS.instanceChars)&&!/[\[\],;|]/.test(value)&&value.split('\\').length===3&&value.split('\\').every(Boolean);
const endpointSource=(source,key)=>({...source,jsonPointer:'/'+key});
function targetPnpProjection(o,source,token) {
  const result={status:'unknown',complete:false,source:endpointSource(source,'targetPnpDetail'),
    encoding:'InputHost.DescribeVirtualEndpoint-five-role-lists',
    sourceScope:'native-forwarded-host-SetupDi-present-topology-and-CM-ancestry-self-report',
    ownershipProven:false,endpointIncarnationProven:false,candidates:[],roles:[],reasons:[],
    cache:{status:'cached-prepare-observation;not-fresh-per-reply',apiSampleTime:'not-retained',frameReceiptRefreshesSnapshot:false},limits:ENDPOINT_LIMITS};
  const detail=o.targetPnpDetail;
  if(typeof detail!=='string'){result.reasons.push('detail-missing-or-wrong-type');return result;}
  if(!detail.length||detail.length>ENDPOINT_LIMITS.detailChars){result.reasons.push('detail-empty-or-over-bound');return result;}
  const fields=new Map(),allowed=new Set([...PNP_ROLES.flatMap(([,list,count])=>[list,count]),'pnp-started']);
  for(const [index,part] of detail.split(';').entries()) {
    const at=part.indexOf('='),key=part.slice(0,at),value=part.slice(at+1);
    if(at<1||!allowed.has(key)||fields.has(key)){result.reasons.push('malformed-duplicate-or-unrecognized-detail-field');continue;}
    fields.set(key,{value,index});
  }
  if(!['true','false'].includes(fields.get('pnp-started')?.value))result.reasons.push('missing-or-invalid-started-field');
  else result.allStartedAtCachedSnapshot=fields.get('pnp-started').value==='true';
  for(const [role,listKey,countKey] of PNP_ROLES) {
    const field=fields.get(listKey),countField=fields.get(countKey),count=countField&&/^(?:0|[1-9]\d*)$/.test(countField.value)?Number(countField.value):null;
    const rr={role,source:{...result.source,detailField:listKey,segmentIndex:field?.index??null},complete:false,declaredCount:Number.isSafeInteger(count)?count:null,parsedCount:0,reasons:[]};result.roles.push(rr);
    if(!field||count===null||count>ENDPOINT_LIMITS.roleItems){rr.reasons.push('missing-or-invalid-role-list-or-count');continue;}
    const items=field.value===''?[]:field.value.split(',');
    if(items.length>ENDPOINT_LIMITS.roleItems){rr.reasons.push('role-over-bound');continue;}
    const names=new Set();
    for(const [itemIndex,item] of items.entries()) {
      const match=/^([^\[\]]+)\[([^\[\]]*)\]$/.exec(item),name=match?.[1],hardware=match?.[2]===''?[]:match?.[2]?.split('|');
      if(!match||!instanceName(name)||!hardware||hardware.length>ENDPOINT_LIMITS.hardwareIdsPerItem||hardware.some(h=>!endpointText(h,ENDPOINT_LIMITS.hardwareIdChars)||/[\[\],;|]/.test(h))||names.has(name)){rr.reasons.push('malformed-truncated-duplicate-or-over-bound-role-item');continue;}
      names.add(name);rr.parsedCount++;
      result.candidates.push({role,pnpInstanceToken:token('pnp-instance',name),hardwareIdTokens:hardware.map(h=>token('hardware-id',h)),stringComplete:true,source:{...rr.source,itemIndex},ownershipProven:false,endpointIncarnationProven:false});
    }
    if(count!==items.length||rr.parsedCount!==count)rr.reasons.push('role-count-or-string-completeness-mismatch');
    rr.complete=rr.reasons.length===0;
  }
  if(result.candidates.length>ENDPOINT_LIMITS.totalItems){result.candidates=[];result.reasons.push('total-candidates-over-bound');}
  if(result.roles.some(r=>!r.complete))result.reasons.push('one-or-more-role-lists-incomplete');
  result.complete=result.reasons.length===0;result.status=result.complete?'complete-role-name-candidates':'unknown';
  for(const candidate of result.candidates)candidate.snapshotComplete=result.complete;
  return result;
}
function probeEndpointProjection(o,event,source,token,knownProducer) {
  const stringToken=(role,key,max=4096)=>opaqueEndpointText(o[key],max)?token(role,o[key]):null;
  const interfacePathToken=stringToken('device-interface','interfacePath'),reportedPnpInstanceToken=instanceName(o.pnpInstanceId)?token('pnp-instance',o.pnpInstanceId):null;
  const result={status:'unknown',sourceScope:'existing-controller-visibility-probe-format6',
    interfacePathToken,reportedPnpInstanceToken,pnpInstanceToken:null,reportedUsbAncestorToken:instanceName(o.usbRootInstance)?token('usb-ancestor',o.usbRootInstance):null,usbAncestorToken:null,
    diInstanceGuidToken:stringToken('di-instance-guid','instanceGuid'),wgiNonRoamableIdToken:stringToken('wgi-id','nonRoamableId'),
    containerToken:null,containerStatus:'not-retained-by-reviewed-producer',reasons:[],
    sources:Object.fromEntries(['interfacePath','pnpInstanceId','usbRootInstance','instanceGuid','nonRoamableId','pathHresult','pnpInstanceConfigRet','pnpLocateConfigRet'].filter(k=>Object.hasOwn(o,k)).map(k=>[k,endpointSource(source,k)])),
    identitySpacesAreDistinct:true,ownershipProven:false,endpointIncarnationProven:false};
  if(integer(o.pathHresult))result.pathHresult=o.pathHresult;
  if(integer(o.pnpInstanceConfigRet))result.pnpInstanceConfigRet=o.pnpInstanceConfigRet;
  if(integer(o.pnpLocateConfigRet))result.pnpLocateConfigRet=o.pnpLocateConfigRet;
  if(event!=='directinput-device'){result.reasons.push('not-DirectInput-interface-mapping;WGI-unmapped');return result;}
  if(knownProducer!==true)result.reasons.push('format6-producer-start-contract-not-retained');
  if(!interfacePathToken||o.interfaceMapping!=='directinput-guidandpath'||!integer(o.pathHresult)||o.pathHresult<0||o.pathHresult>0x7fffffff)result.reasons.push('missing-or-failed-DIPROP_GUIDANDPATH-source');
  if(!reportedPnpInstanceToken)result.reasons.push('missing-incomplete-or-wrong-type-PnP-name');
  if(o.pnpInstanceConfigRet!==0)result.reasons.push('missing-wrong-type-or-failed-CM-instance-property-read');
  if(result.reasons.length)return result;
  result.status='successful-CM-instance-name-readback';result.pnpInstanceToken=reportedPnpInstanceToken;
  result.apiSource='DIPROP_GUIDANDPATH + CM_Get_Device_Interface_PropertyW(DEVPKEY_Device_InstanceId)';
  result.propertyTypeBasis='reviewed-producer-only-emits-name-after-CR_SUCCESS-STRING-nonempty';
  if(o.pnpLocateConfigRet===0&&o.usbRootInstanceKnown!==false&&instanceName(o.usbRootInstance)&&o.usbRootInstance.startsWith('USB\\')&&!o.usbRootInstance.includes('&MI_')){result.usbAncestorToken=token('usb-ancestor',o.usbRootInstance);result.usbAncestorSource='CM_Locate_DevNodeW + source-gated-CM_Get_Device_IDW-parent-walk';}
  else result.usbAncestorStatus='unknown-or-not-USB-root';
  return result;
}
function targetCreationLifetimeProjection(o,source,ack,token) {
  const raw=o.targetCreationLifetimeEvidence,result={status:'unknown',source:{...source,jsonPointer:'/targetCreationLifetimeEvidence'},scope:'host-local-SDK-object-creation-lifetime-diagnostic-only',protocolBindingVerified:ack?.status==='verified-local-ack',endpointIncarnationProven:false,sdkOwnershipProven:false,steamConsumptionProven:false,gameInputUnverified:true,reasons:[]};
  if(!raw||typeof raw!=='object'||Array.isArray(raw)){result.reasons.push('missing-or-invalid-lifetime-object');return result;}
  if(raw.schema!==1||raw.version!==1||raw.source!=='caller-confirmed-sdk-target-object-lifecycle'||raw.scope!==result.scope||raw.callerLockScope!=='existing-host-lifecycle-lock-required;not-independently-observed'){result.reasons.push('unsupported-lifetime-contract');return result;}
  for(const key of ['closed','disabled','protocolTupleIncluded','osIncarnationProven','sdkOwnershipProven','steamConsumptionProven','gameInputUnverified'])if(typeof raw[key]!=='boolean')result.reasons.push('wrong-boolean:'+key);
  if(raw.protocolTupleIncluded!==false||raw.osIncarnationProven!==false||raw.sdkOwnershipProven!==false||raw.steamConsumptionProven!==false||raw.gameInputUnverified!==true)result.reasons.push('invalid-lifetime-consumption-boundary');
  const exact=(v,key)=>typeof v==='string'&&!exactNumericFields.get(raw)?.has(key)&&/^(?:0|[1-9]\d*)$/.test(v)&&BigInt(v)<=18446744073709551615n;
  if(!exact(raw.attemptOrdinal,'attemptOrdinal')||!exact(raw.creationOrdinal,'creationOrdinal'))result.reasons.push('missing-or-wrong-exact-ordinal-string');
  const match=typeof raw.namespace==='string'&&/^target-creation-local:v1:([a-f0-9]{32})$/.exec(raw.namespace);
  if(!match)result.reasons.push('invalid-local-namespace');
  if(!['live','closed','attempting','unknown'].includes(raw.state))result.reasons.push('unknown-lifetime-state');
  if(result.reasons.length)return result;
  const expected=n=>'target-creation:v1:'+match[1]+':'+n;
  if(raw.state==='live') {
    if(raw.closed!==false||raw.disabled!==false||raw.creationOrdinal==='0'||raw.attemptOrdinal==='0'||raw.lifetimeToken!==expected(raw.creationOrdinal))result.reasons.push('inconsistent-live-token-ordinal-or-state');
  } else if(raw.closed!==true||raw.lifetimeToken!==null)result.reasons.push('closed-or-unknown-state-cannot-retain-live-token');
  if(raw.closedLifetimeToken!==null||raw.closedCreationOrdinal!==null){if(!exact(raw.closedCreationOrdinal,'closedCreationOrdinal')||raw.closedCreationOrdinal==='0'||raw.closedLifetimeToken!==expected(raw.closedCreationOrdinal)||BigInt(raw.closedCreationOrdinal)>BigInt(raw.creationOrdinal))result.reasons.push('inconsistent-closed-token-and-ordinal');}
  if(result.reasons.length)return result;
  result.hostLocalNamespaceToken=token('target-creation-namespace',raw.namespace);result.state=raw.state;result.closed=raw.closed;result.disabled=raw.disabled;result.attemptOrdinal=raw.attemptOrdinal;result.creationOrdinal=raw.creationOrdinal;
  result.lifetimeToken=raw.lifetimeToken?token('target-creation-lifetime',raw.lifetimeToken):null;result.closedLifetimeToken=raw.closedLifetimeToken?token('target-creation-lifetime',raw.closedLifetimeToken):null;result.closedCreationOrdinal=raw.closedCreationOrdinal;result.reason=safe(raw.reason);
  result.status=raw.disabled||raw.state==='unknown'?'unknown':raw.state==='live'?'reported-live-sdk-object':raw.state==='closed'?'reported-closed-sdk-object':'reported-creation-attempt';
  if(!result.protocolBindingVerified)result.reasons.push('local-lifetime-has-no-verified-same-row-request-binding');
  return result;
}

function endpointReceiptProjection(o,identity,source,ack,token) {
  const result={source,command:safe(o.command),phase:safe(o.phase),accepted:o.ok===true&&o.phase==='accepted',
    localTuple:{status:'unknown',scope:'native-retained-command-envelope;not-full-request-bound-protocol',nativeLifetime:identity.ymccLifetime,runToken:null,hostToken:null,hostEpoch:null,wireSequence:null},
    protocolBinding:{status:'unknown',tuple:null,reasons:['complete-request-bound-protocol-not-retained-in-ordinary-command-row']}};
  const local=result.localTuple;
  for(const [key,role,out] of [['runId','run','runToken'],['hostInstanceId','host','hostToken']])if(endpointText(o[key],4096))local[out]=token(role,o[key]);
  local.hostEpoch=positiveU64(o,'epoch');local.wireSequence=positiveU64(o,'sequence');
  if(local.nativeLifetime&&local.runToken&&local.hostToken&&local.hostEpoch!==null&&local.wireSequence!==null)local.status='complete-retained-command-envelope';
  // Only an explicit verified projection on this very row can supply missing
  // protocol fields. A later sparse FRAME ACK never fills a creation receipt.
  if(local.status==='complete-retained-command-envelope'&&ack?.status==='verified-local-ack'&&ack.protocolTuple?.namespace&&compareIdentity(local,ack.protocolTuple,['runToken','hostToken','hostEpoch','wireSequence']).status==='matched')
    result.protocolBinding={status:'verified-local-ack-same-row',tuple:ack.protocolTuple,source:ack.source,scope:'L1-only;does-not-prove-owned-endpoint-or-consumption',reasons:[]};
  return result;
}


function endpointNameBridge(evidence) {
  const snapshots=evidence.filter(e=>e.facts.targetEndpointCandidates),readers=evidence.filter(e=>e.layer==='L2'&&['directinput-device','wgi-device'].includes(e.facts.event));
  const boundaries=evidence.filter(e=>e.layer==='L1'&&(e.facts.endpointReceipt&&['PREPARE_TARGET','RECREATE_TARGET','RELEASE_TARGET','SHUTDOWN'].includes(e.facts.endpointReceipt.command)||['input-host-started','input-host-open-result','input-host-stop-result'].includes(e.kind)));
  const result={schema:1,status:'unknown',scope:'same-bundle-complete-instance-name-candidate-only',
    matchingPolicy:'exact-case-sensitive-complete-PnP-string-token;no-VID-PID-name-GUID-WGI-or-ancestor-substitution',
    protocolPolicy:'same-row-verified-local-ACK-required;nearby-sparse-ACK-and-current-shared-power-never-fill-command-fields',
    lifetimePolicy:'exact-nativeOwner-reader-parent + retained-create-reply + no-intervening-target-boundary + bounded-earlier-source-time',
    limits:ENDPOINT_LIMITS,snapshotCount:snapshots.length,readerCount:readers.length,boundaryCount:boundaries.length,
    lifecycleEvents:boundaries.slice(0,ENDPOINT_LIMITS.boundaries).map(e=>({evidenceId:e.id,layer:e.layer,source:e.sources[0],time:e.time,nativeLifetime:e.identity.ymccLifetime,command:e.facts.endpointReceipt?.command??e.kind,phase:e.facts.endpointReceipt?.phase??null,accepted:e.facts.endpointReceipt?.accepted??null,localTuple:e.facts.endpointReceipt?.localTuple??null,protocolBinding:e.facts.endpointReceipt?.protocolBinding??null})),observations:[],
    ownershipProven:false,endpointIncarnationProven:false,ackConsumptionProven:false,steamControllerAssociation:'unknown',wgiToPnP:'unknown',originalGameInput:'unknown',continueAcceptance:'unknown'};
  if(snapshots.length>ENDPOINT_LIMITS.snapshots||readers.length>ENDPOINT_LIMITS.readers||boundaries.length>ENDPOINT_LIMITS.boundaries){result.reason='endpoint-analysis-over-bound;no-partial-matches-authorized';return result;}
  for(const reader of readers) {
    const endpoint=reader.facts.endpointMetadata,parent=reader.facts.nativeParentRelationship,life=reader.facts.observerLifecycle;
    const entry={readerEvidenceId:reader.id,status:'unknown',source:reader.sources[0],time:reader.time,
      interfacePathToken:endpoint.interfacePathToken,pnpInstanceToken:endpoint.pnpInstanceToken,reportedPnpInstanceToken:endpoint.reportedPnpInstanceToken,
      observerLifetime:reader.identity.observerLifetime,nativeParentRelationship:parent,apiMetadataStatus:endpoint.status,
      candidates:[],reasons:[],ownershipProven:false,endpointIncarnationProven:false,ackConsumptionProven:false,gameInputUnverified:true};result.observations.push(entry);
    if(reader.facts.event==='wgi-device'){entry.reasons.push('WGI-ID-has-no-reviewed-PnP-bridge');continue;}
    const lookup=endpoint.reportedPnpInstanceToken;
    if(!lookup){entry.reasons.push('complete-reader-PnP-name-not-retained');continue;}
    const relevant=snapshots.filter(s=>s.facts.targetEndpointCandidates.candidates.some(c=>c.pnpInstanceToken===lookup));
    if(relevant.length>ENDPOINT_LIMITS.comparisonsPerReader){entry.reasons.push('matching-snapshots-over-bound;none-selected');continue;}
    if(!relevant.length){entry.reasons.push('no-exact-complete-name-match;near-names-and-USB-ancestors-not-substituted');continue;}
    for(const snapshot of relevant) {
      const projection=snapshot.facts.targetEndpointCandidates,receipt=snapshot.facts.endpointReceipt;
      const reasons=[];
      if(endpoint.status!=='successful-CM-instance-name-readback')reasons.push('reader-name-has-no-successful-actual-CM-API-source');
      if(life?.status!=='matched'||parent?.status!=='matched')reasons.push('exact-nativeOwner-observer-parent-lifetime-not-matched');
      if(!parent?.nativeLifetime||parent.nativeLifetime!==snapshot.identity.ymccLifetime)reasons.push('different-or-missing-native-owner-lifetime');
      if(!projection.complete)reasons.push('host-five-role-snapshot-incomplete');
      if(!receipt||!receipt.accepted||!['PREPARE_TARGET','RECREATE_TARGET'].includes(receipt.command))reasons.push('not-an-accepted-create-or-recreate-reply;cached-or-failed-receipt');
      if(snapshot.facts.targetCreationLifetime&&(snapshot.facts.targetCreationLifetime.status!=='reported-live-sdk-object'||!snapshot.facts.targetCreationLifetime.protocolBindingVerified))reasons.push('same-row-sdk-object-lifetime-not-live-or-request-unbound');
      if(receipt?.localTuple.status!=='complete-retained-command-envelope')reasons.push('complete-local-command-identity-tuple-not-retained');
      if(receipt?.protocolBinding.status!=='verified-local-ack-same-row')reasons.push('complete-request-bound-protocol-tuple-not-retained-on-create-row');
      if(reader.observation==='contradicted'||snapshot.observation==='contradicted')reasons.push('contradicted-source-observation');
      let delta=null;
      if(reader.time.status!=='normalized'||snapshot.time.status!=='normalized')reasons.push('source-time-unknown-or-contradicted');
      else {
        delta=reader.time.utcEpochMs-snapshot.time.utcEpochMs;
        if(snapshot.time.utcIntervalEndExclusiveMs>reader.time.utcEpochMs)reasons.push('create-reply-future-or-timestamp-interval-overlaps-reader');
        if(delta>ENDPOINT_LIMITS.bridgeDelayMs)reasons.push('cached-snapshot-outside-bounded-source-time-window');
      }
      const afterSource=b=>b.sources.some(bs=>snapshot.sources.some(ss=>bs.file===ss.file&&bs.line>ss.line));
      const sharesSource=b=>b.sources.some(bs=>snapshot.sources.some(ss=>bs.file===ss.file));
      // Across files, a missing clock or overlapping precision cannot establish
      // that a release/recreate preceded the creation. Do not silently skip it.
      const afterCreation=b=>b.time.status!=='normalized'?(afterSource(b)||!sharesSource(b)):snapshot.time.status==='normalized'&&(afterSource(b)||b.time.utcEpochMs>snapshot.time.utcEpochMs||!sharesSource(b)&&b.time.utcIntervalEndExclusiveMs>snapshot.time.utcEpochMs);
      const sameOwner=b=>b.identity.ymccLifetime===snapshot.identity.ymccLifetime&&!!snapshot.identity.ymccLifetime||b.identity.ymccPid===snapshot.identity.ymccPid&&b.time.status!=='normalized';
      const later=boundaries.filter(b=>b.id!==snapshot.id&&sameOwner(b)&&afterCreation(b));
      const intervening=later.filter(b=>b.time.status!=='normalized'||reader.time.status!=='normalized'||b.time.utcEpochMs<reader.time.utcIntervalEndExclusiveMs);
      if(intervening.length)reasons.push('intervening-release-recreate-prepare-host-boundary-or-unknown-boundary-time');
      // Equal-name roles are retained separately. Neither a unique role nor an
      // arbitrarily selected root is an SDK ownership or incarnation assertion.
      for(const candidate of projection.candidates.filter(c=>c.pnpInstanceToken===lookup))entry.candidates.push({
        status:reasons.length?'unknown':'same-device-instance-name-candidate',nameEquality:'exact-complete-string-token-equality',role:candidate.role,pnpInstanceToken:lookup,
        hostEvidenceId:snapshot.id,hostLayer:'L1',hostSource:candidate.source,hostTime:snapshot.time,readerLayer:'L2',readerSource:endpoint.sources.pnpInstanceId,readerTime:reader.time,
        protocolBinding:receipt?.protocolBinding??{status:'unknown',tuple:null},
        lifecycle:{createReply:receipt&&['PREPARE_TARGET','RECREATE_TARGET'].includes(receipt.command)?{evidenceId:snapshot.id,source:receipt.source,command:receipt.command,accepted:receipt.accepted,time:snapshot.time}:null,
          observerStart:life?.startSource??null,observerStop:life?.stopSource??null,
          interveningBoundaries:intervening.slice(0,16).map(b=>({evidenceId:b.id,source:b.sources[0],command:b.facts.endpointReceipt?.command??b.kind,time:b.time})),
          laterBoundaryCount:later.length,endpointIncarnation:'unknown;controller-object-recreate-can-reuse-name-and-host-tuple'},
        sourceTimeRelation:{status:delta!==null&&!reasons.some(r=>/time|future|boundary/.test(r))?'bounded-earlier-reply-time':'unknown',replyToReaderMs:delta,maxMs:ENDPOINT_LIMITS.bridgeDelayMs,hostApiSampleTime:'not-retained',hostCache:projection.cache,readerTick:reader.facts.tick},
        reasons,ownershipProven:false,endpointIncarnationProven:false,ackConsumptionProven:false,gameInputUnverified:true});
    }
    if(entry.candidates.some(c=>c.status==='same-device-instance-name-candidate'))entry.status='same-device-instance-name-candidate';
    else entry.reasons.push('exact-name-equality-retained-with-missing-or-invalid-contract;no-endpoint-join-authorized');
  }
  result.nameCandidateCount=result.observations.reduce((n,r)=>n+r.candidates.filter(c=>c.status==='same-device-instance-name-candidate').length,0);
  result.exactNameEqualityCount=result.observations.reduce((n,r)=>n+r.candidates.length,0);
  if(result.nameCandidateCount)result.status='same-device-instance-name-candidate';
  return result;
}

const sha = v => crypto.createHash('sha256').update(v).digest('hex');
const integer = Number.isSafeInteger;
const present = v => v !== undefined && v !== null && v !== '';
const inRoot = (root, file) => { const r=path.relative(root,file); return r!=='..'&&!r.startsWith(`..${path.sep}`)&&!path.isAbsolute(r); };
const KNOWN = ['YeManCC-export-manifest.txt','steam-input-export-manifest.json','wake-recovery-last.json','wake-recovery-current.json','native-lifecycle.log','native-detail.log','app.log','sleep-facts.log','steam-controller.log','steam-controller-ui.log','steam-gameprocess.log','steam/logs/controller.txt','steam/logs/controller_ui.txt','steam/logs/gameprocess_log.txt'];
const EXTRA = /^(?:native-lifecycle|native-detail|app|virtual-gamepad|input-host-(?:cost|rejects|fatal)|msi-controller-visibility(?:-wake-[12])?|game-consumer-receipts)\.log(?:\.[1-9][0-9]?)?$/;
export function normalizeAppId(v) {
  if(typeof v==='number'&&!integer(v))return null;
  if(!/^-?\d+$/.test(String(v??'')))return null;
  const n=BigInt(v);if(n<-(1n<<31n)||n>(1n<<64n)-1n)return null;
  return String(n<0n?BigInt.asUintN(32,n):n>0xffffffffn?(n>>32n)&0xffffffffn:n);
}
export function filetimeMs(v) {
  if(typeof v==='number'&&!integer(v)||!/^\d+$/.test(String(v??'')))return null;
  const n=(BigInt(v)-116444736000000000n)/10000n;
  return n>=0n&&n<=8640000000000000n?Number(n):null;
}
export function compareIdentity(expected,actual,axes) {
  const matchingAxes=[],missingAxes=[],conflictingAxes=[];
  for(const k of axes) {
    if(!present(expected?.[k])||!present(actual?.[k]))missingAxes.push(k);
    else if(String(expected[k])!==String(actual[k]))conflictingAxes.push(k);
    else matchingAxes.push(k);
  }
  return {status:conflictingAxes.length?'contradicted':missingAxes.length?'unknown':'matched',matchingAxes,missingAxes,conflictingAxes};
}
export function generationEvidence(row,sourceFile,ownerLifetime=null,allowShared=false) {
  const result=[];
  for(const field of ['powerGeneration','generation','wakeGeneration']) {
    if(!present(row[field]))continue;
    const valid=integer(row[field])&&row[field]>=0;
    const shared=field==='powerGeneration'&&valid&&row[field]>0&&allowShared&&present(ownerLifetime);
    result.push({field,value:valid?row[field]:null,
      namespace:shared?`ymcc-shared-power:${ownerLifetime}`:`source:${sourceFile}:${safe(row.event)}:${field}`,
      ownerLifetime,status:shared?'bound-shared-wake':'unknown',
      relation:shared?'native-common-schema-explicit-power-epoch':'source-local-only;numeric-equality-does-not-establish-shared-wake'});
  }
  return result;
}
export function normalizeTime(row,offset) {
  const local=typeof row.time==='string'?row.time:typeof row.at==='string'?row.at:null,candidates=[];
  if(integer(row.utcEpochMs))candidates.push(['utcEpochMs',row.utcEpochMs]);
  if(typeof row.recordEpoch==='number'&&Number.isFinite(row.recordEpoch))candidates.push(['recordEpoch',Math.round(row.recordEpoch*1000)]);
  if(local){const explicit=/(?:Z|[+-]\d\d:\d\d)$/.test(local);if(explicit||integer(offset)){const s=local.replace(' ','T'),ms=Date.parse(explicit?s:`${s}Z`);if(Number.isFinite(ms))candidates.push([explicit?'explicit-offset':'manifest-local-offset',ms-(explicit?0:offset*60000)]);}}
  if(!candidates.length)return {local,utcEpochMs:null,utc:null,status:'unknown',basis:local?'local-without-verified-offset':'timestamp-missing'};
  if(candidates.some(([,ms])=>Math.abs(ms-candidates[0][1])>1500))return {local,utcEpochMs:null,utc:null,status:'contradicted',basis:'timestamp-fields-disagree',candidateEpochMs:candidates.map(([,ms])=>ms)};
  const ms=candidates[0][1];const fraction=local?.match(/\.(\d+)(?:Z|[+-]\d\d:\d\d)?$/)?.[1];const precisionMs=candidates[0][0]==='recordEpoch'||candidates[0][0]==='utcEpochMs'?1:fraction?10**Math.max(0,3-fraction.length):1000;if(!Number.isFinite(ms)||Math.abs(ms)>8640000000000000)return {local,utcEpochMs:null,utc:null,status:'unknown',basis:'timestamp-out-of-range'};
  return {local,utcEpochMs:ms,precisionMs,utcIntervalEndExclusiveMs:ms+precisionMs,utc:new Date(ms).toISOString(),status:'normalized',basis:candidates.map(([k])=>k).join('+')};
}
// JSON pointer -> exact first source line, including repeated keys in arrays.
function jsonLocations(text) {
  const map=new Map();let at=0,line=1;
  function white(){while(at<text.length&&/\s/.test(text[at]))if(text[at++]==='\n')line++;}
  function string(){const start=at++;while(at<text.length){const c=text[at++];if(c==='\\')at++;else if(c==='"')break;}return JSON.parse(text.slice(start,at));}
  function value(ptr){white();map.set(ptr,line);
    if(text[at]==='{'){at++;white();if(text[at]==='}'){at++;return;}while(at<text.length){white();const k=string();white();if(text[at++]!==':')throw Error('json-location-colon');value(`${ptr}/${k.replace(/~/g,'~0').replace(/\//g,'~1')}`);white();const c=text[at++];if(c==='}')return;if(c!==',')throw Error('json-location-comma');}}
    else if(text[at]==='['){at++;white();if(text[at]===']'){at++;return;}for(let i=0;at<text.length;i++){value(`${ptr}/${i}`);white();const c=text[at++];if(c===']')return;if(c!==',')throw Error('json-location-array');}}
    else if(text[at]==='"')string();else while(at<text.length&&!/[\s,\]}]/.test(text[at]))at++;
  }value('');return map;
}
const safe = v => /^[a-zA-Z0-9_.:-]{1,100}$/.test(v||'')?v:'unclassified';
const exe = v => typeof v==='string'?v.match(/[a-zA-Z]:[\\/][^"\r\n]*?\.exe/i)?.[0]??null:null;
const cleanPath = v => typeof v==='string'?v.replace(/\//g,'\\').toLowerCase():null;

export function analyzeDirectory(input,options={}) {
  if(!input)throw Error('An explicit --input directory is required.');
  const root=fs.realpathSync(path.resolve(input));if(!fs.statSync(root).isDirectory())throw Error('Input must be a directory.');
  const files=[],diagnostics=[],rows=[],docs=new Map();
  for(const name of [...new Set([...KNOWN,...fs.readdirSync(root).filter(n=>EXTRA.test(n))])].sort()) {
    const f=path.join(root,name);if(!fs.existsSync(f))continue;
    if(!inRoot(root,fs.realpathSync(f)))throw Error(`Input member escapes the explicit directory: ${name}`);
    const stat=fs.statSync(f);if(!stat.isFile()||stat.size>64*1024*1024){diagnostics.push({kind:'file-not-read',file:name,status:'unknown',reason:'not-regular-or-over-size-limit'});continue;}
    const bytes=fs.readFileSync(f),text=bytes.toString('utf8').replace(/^\uFEFF/,''),source={file:name.replace(/\\/g,'/'),sha256:sha(bytes),bytes:bytes.length};files.push(source);
    if(name.endsWith('.json')){try{docs.set(name,{object:parseEvidenceJson(text),source,locations:jsonLocations(text)});}catch{diagnostics.push({kind:'malformed-json-document',file:name,status:'unknown'});}continue;}
    let malformed=0;for(const [i,s]of text.split(/\r?\n/).entries()){if(!s.trim())continue;let object=null;const j=s.indexOf('{');
      if(j>=0&&(s.startsWith('{')||/^\[.*\]\s*\[\w+\]/.test(s))){try{object=parseEvidenceJson(s.slice(j));}catch{malformed++;continue;}if(!object||typeof object!=='object'||Array.isArray(object)){malformed++;continue;}const t=s.match(/^\[([^\]]+)\]/)?.[1];if(!object.time&&t)object={...object,time:t};}
      rows.push({object,text:s,source:{...source,line:i+1}});
    }if(malformed)diagnostics.push({kind:'malformed-json-lines',file:name,rows:malformed,status:'unknown'});
  }
  const manifest=rows.filter(r=>r.source.file==='YeManCC-export-manifest.txt'),bias=manifest.find(r=>/^timeZoneBiasMinutes=-?\d+$/.test(r.text));
  const manifestOffset=bias?-Number(bias.text.split('=')[1]):null;let offset=options.utcOffsetMinutes??manifestOffset;
  if(present(offset)&&(!integer(offset)||offset<-840||offset>840))throw Error('UTC offset must be integer minutes in [-840,840].');
  if(present(options.utcOffsetMinutes)&&present(manifestOffset)&&options.utcOffsetMinutes!==manifestOffset){diagnostics.push({kind:'utc-offset-conflict',status:'contradicted',manifestOffsetMinutes:manifestOffset,suppliedOffsetMinutes:options.utcOffsetMinutes,source:bias.source});offset=null;}
  const salt=sha(files.map(f=>`${f.file}:${f.sha256}`).join('\n'));
  const token=(role,v)=>present(v)?`${role}:${crypto.createHmac('sha256',salt).update(String(v)).digest('hex').slice(0,24)}`:null;
  const pathToken=v=>token('path',cleanPath(v));
  const exported=manifest.find(r=>r.text.startsWith('exportedAtLocal=')),exportTime=normalizeTime({time:exported?.text.split('=')[1]},offset);
  const jsonRows=rows.filter(r=>r.object),boots=jsonRows.filter(r=>r.object.event==='boot-single-instance-acquired'&&r.source.file.startsWith('native-lifecycle.log')).map(r=>{const time=normalizeTime(r.object,offset);return {pid:r.object.pid,time,source:r.source,nativeOwner:nativeOwnerProjection(r.object,r.source,time),token:token('ymcc-lifetime',`${r.source.file}:${r.object.pid}:${r.object.time}:${r.source.line}`)};});
  const bootTimes=boots.map(b=>b.time.utcEpochMs).filter(integer),startMs=bootTimes.length?Math.min(...bootTimes):null;
  const evidence=[],seen=new Map();let outside=0;
  const origin=(d,ptr)=>({...d.source,line:d.locations.get(ptr)??1,jsonPointer:ptr});
  function emit(layer,kind,source,time,facts,identity={}) {
    if(integer(time.utcEpochMs)&&(integer(startMs)&&time.utcEpochMs<startMs||integer(exportTime.utcEpochMs)&&time.utcEpochMs>exportTime.utcEpochMs+2000)){outside++;return null;}
    const key=sha(JSON.stringify({layer,kind,time,facts,identity}));if(seen.has(key)){const old=seen.get(key);if(!old.sources.some(s=>s.file===source.file&&s.line===source.line&&s.jsonPointer===source.jsonPointer))old.sources.push(source);return old;}
    const e={id:`e${evidence.length+1}`,layer,kind,observation:time.status==='contradicted'?'contradicted':'observed',sources:[source],time,identity,facts,originalInstance:{status:'unknown',basis:[],missingAxes:['original-instance-anchor']}};evidence.push(e);seen.set(key,e);return e;
  }
  const lifetime=(pid,ms,source)=>boots.filter(b=>b.pid===pid&&integer(ms)&&integer(b.time.utcEpochMs)&&b.time.utcEpochMs<=ms&&(b.source.file!==source.file||b.source.line<=source.line)).sort((a,b)=>b.time.utcEpochMs-a.time.utcEpochMs)[0]?.token??null;
  const nativeIdentity=(o,t,source)=>{
    const owner=lifetime(o.pid,t.utcEpochMs,source);
    // Only appendNativeLifecycleLog's documented common field uses the shared
    // power epoch. A bare generation is always source/event namespaced.
    const knownSchema=/^native-(?:lifecycle|detail)\.log(?:\.[1-9][0-9]?)?$/.test(source.file);
    const shared=knownSchema&&owner&&integer(o.powerGeneration)&&o.powerGeneration>0;
    return {ymccPid:integer(o.pid)?o.pid:null,ymccLifetime:owner,
      powerGeneration:shared?o.powerGeneration:null,
      sharedWakeNamespace:shared?`ymcc-shared-power:${owner}`:null,
      sharedWakeGeneration:shared?o.powerGeneration:null,
      generationEvidence:generationEvidence(o,source.file,owner,!!shared),
      hostEpoch:integer(o.epoch)?o.epoch:integer(o.hostEpoch)?o.hostEpoch:null,
      runToken:token('run',o.runId),hostToken:token('host',o.hostInstanceId),persona:safe(o.persona||o.boundPersona||o.requestedPersona)};
  };
  const anchors=[];
  for(const [name,d] of docs) {
    if(!name.startsWith('wake-recovery-'))continue;const w=d.object;
    emit('context','wake-record',origin(d,'/implementation'),normalizeTime(w,offset),{implementation:safe(w.implementation),generation:w.generation??null,phase:safe(w.phase),gameInput:safe(w.gameInput),healthVerified:w.healthVerified===true},{reportedRecoveryGeneration:w.generation??null,generationEvidence:generationEvidence({event:'wake-recovery-record',generation:w.generation},name,null,false)});
    const selected=w.before?.suspendCapture?.targetSelection;
    if(selected&&integer(selected.selectedPid)&&present(selected.selectedCreated)) {
      const gamePath=exe(selected.nativePrimary?.label||w.before?.nativePrimary?.label),client=w.before.steamClient||{};
      const identity={gamePid:selected.selectedPid,gameCreated:String(selected.selectedCreated),gamePathToken:pathToken(gamePath),appId:normalizeAppId(selected.selectedAppid??selected.ymccSteamJoin?.appid),reportedRecoveryGeneration:integer(w.generation)?w.generation:null,sharedWakeNamespace:null,sharedWakeGeneration:null,generationEvidence:generationEvidence({event:'wake-recovery-record',generation:w.generation},name,null,false),steamPid:client.pid??null,steamCreated:client.created?String(client.created):null,steamIdentityToken:token('steam-session',client.identityToken)};
      const row=emit('context','captured-original-instance',origin(d,'/before/suspendCapture/targetSelection'),normalizeTime(w.before,offset),{jointBindingVerified:selected.jointBindingVerified===true,windowsSessionMatchedReported:selected.ymccSteamJoin?.windowsSessionMatched===true,nativePidValidationReported:selected.ymccPidValidation?.exactProcessIdentity===true,evidenceScope:'native-query-and-capture-claims;not-controller-or-game-input-readback',creationUtc:filetimeMs(identity.gameCreated)!==null?new Date(filetimeMs(identity.gameCreated)).toISOString():null},identity);
      if(row)anchors.push(row);
    }
    for(const [ptr,o]of [['/before',w.before],['/after',w.after],...(Array.isArray(w.observations)?w.observations.map((v,i)=>[`/observations/${i}`,v]):[])]) {
      if(!o||typeof o!=='object'||!o.game)continue;const g=o.game,c=o.steamClient||{};
      emit('L4','original-game-inspection',origin(d,`${ptr}/game`),normalizeTime(o,offset),{gameInputVerified:o.gameApiProbe?.gameInputVerified===true,hookHealthVerified:o.gameApiProbe?.hookHealthVerified===true,gameInput:safe(typeof o.gameInput==='string'?o.gameInput:o.association?.gameInput||w.gameInput),elapsedMs:integer(o.elapsedMs)?o.elapsedMs:null,stage:safe(o.stage),inputStatus:safe(o.input?.status),inputReason:safe(o.input?.reason),inputContextMatched:o.input?.contextMatched===true,continueButtonVerified:o.continueRecovery?.continueButtonVerified===true||o.continue?.continueButtonVerified===true,modulesObserved:(o.gameApiProbe?.inputLibrariesObserved||[]).filter(s=>/^(?:dinput\d*|sdl\d*|windows\.gaming\.input|xinput[\d_]*)\.dll$/i.test(s)),controllerCountReported:integer(o.controllerCount)?o.controllerCount:null,evidenceScope:'native-query-only;modules-counts-and-running-state-are-not-game-consumption'}, {gamePid:g.pid??null,gameCreated:g.created?String(g.created):null,appId:normalizeAppId(o.tracker?.appid??o.association?.appid??o.steam?.appid),reportedRecoveryGeneration:o.generation??w.generation,sharedWakeNamespace:null,sharedWakeGeneration:null,generationEvidence:generationEvidence({event:'wake-recovery-inspection',generation:o.generation??w.generation},name,null,false),steamPid:c.pid??null,steamCreated:c.created?String(c.created):null});
    }
  }
  const receipts=[],osStarts=new Map(),observerLifetimes=[];
  for(const row of jsonRows) {
    const o=row.object,event=safe(o.event),t=normalizeTime(o,offset),source=row.source,identity=nativeIdentity(o,t,source);
    if(/^(?:input-host-(?:started|target-neutralized|open-result|command-result|submit-stats|submit-flow|frame-reject|msi-transport-boundary|stop-result)|wake-input-chain-(?:first-frame|sustained|snapshot)|msi-virtual-wake-(?:policy|rearm|cancelled))$/.test(event)) {
      let detail;if(event==='input-host-submit-stats'&&typeof o.detail==='string'){try{detail=parseEvidenceJson(o.detail);}catch{diagnostics.push({kind:'malformed-host-submit-stats',status:'unknown',source});}}
      const facts={event,evidenceScope:'native-and-host-receipt-only;downstream-consumption-unknown'};
      for(const k of ['command','phase','status','reason','policy','failureBoundary','countScope','hostEpochScope','evidenceScope'])if(typeof o[k]==='string')facts[k]=safe(o[k]);
      for(const k of ['ok','active','hostActive','hostAlive','hostPrepared','hostNeutralized','firstFrame','semanticNeutral','targetPnpAdmitted','targetPnpAllStarted','requestWritten','responseReadSucceeded','transportUsable','physicalSourcePending','admitted','neutral','gameInputUnverified'])if(typeof o[k]==='boolean')facts[k]=o[k];
      for(const k of ['wireSequence','sequence','acceptedSinceBoot','nonNeutralAccepted','nonNeutralAcceptedSinceBoot','connectedMask','physicalSourceMask','inputChainEvidenceSchema','countPowerGeneration','sampledHostEpoch','acceptedInCurrentGeneration','nonNeutralAcceptedInCurrentGeneration'])if(integer(o[k]))facts[k]=o[k];
      if(detail){facts.aggregate={};for(const k of ['frameOk','frameFail','neutralOk','neutralFail','lastSequence'])if(integer(detail[k]))facts.aggregate[k]=detail[k];facts.aggregateTuple=compareIdentity({runId:o.runId,epoch:o.epoch,persona:o.persona},detail,['runId','epoch','persona']);}
      if(integer(o.countPowerGeneration)){facts.countGenerationComparison=compareIdentity({value:identity.sharedWakeGeneration},{value:o.countPowerGeneration},['value']);facts.countGenerationComparison.scope='schema-2-count-sample;not-frame-tuple-proof';}if(o.inputChainEvidenceSchema===3||Object.hasOwn(o,'ackIdentity'))facts.ackIdentity=ackProjection(o,source,identity,token);
      if(Object.hasOwn(o,'targetPnpDetail'))facts.targetEndpointCandidates=targetPnpProjection(o,source,token);
      if(Object.hasOwn(o,'targetCreationLifetimeEvidence'))facts.targetCreationLifetime=targetCreationLifetimeProjection(o,source,facts.ackIdentity,token);
      if(event==='input-host-command-result')facts.endpointReceipt=endpointReceiptProjection(o,identity,source,facts.ackIdentity,token);
      const e=emit('L1',event,source,t,facts,identity);if(e&&facts.ackIdentity?.status==='contradicted')e.observation='contradicted';if(e&&facts.countGenerationComparison?.status==='contradicted')e.observation='contradicted';if(e&&facts.aggregateTuple?.status==='contradicted')e.observation='contradicted';if(e&&identity.runToken&&identity.hostToken)receipts.push(e);
    }
    // Probe process creation is explicitly not an independent reader receipt.
    if(event==='msi-controller-transition'&&JSON.stringify(o.facts||{}).includes('creationReceiptOnly'))emit('context','observer-launch-metadata',source,t,{creationReceiptOnly:true,evidenceScope:'native-self-report;not-independent-reading'},identity);
    if(/^msi-controller-visibility/.test(source.file)) {
      const key=`${source.file}:${o.observerPid}`;let state=osStarts.get(key);
      if(event==='start') {
        const birth=present(o.parentCreationTime)?String(o.parentCreationTime):null,birthMs=filetimeMs(birth);
        const valid=o.formatVersion===6&&integer(o.observerPid)&&o.observerPid>0&&integer(o.parentPid)&&o.parentPid>0&&integer(birthMs)&&t.status==='normalized'&&birthMs<=t.utcEpochMs&&integer(o.tick)&&o.tick>=0&&o.readOnly===true&&o.writesHardware===false;
        const life={observerLifetime:token('observer-lifetime',`${source.file}:${source.line}:${o.observerPid}:${birth}:${t.utcEpochMs}:${o.tick}`),observerPid:o.observerPid,parentPid:integer(o.parentPid)?o.parentPid:null,parentCreated:birth,
          parentCreationUtc:integer(birthMs)?new Date(birthMs).toISOString():null,status:valid?'bound-observer-start':'unknown',
          identityScope:'logged-parent-PID-and-exact-birth;observer-start-lifetime-not-observer-OS-birth',start:{source,time:t,tick:o.tick??null},lastRetained:{source,time:t,tick:o.tick??null},stop:null,termination:'not-retained;not-failure-proof',
          probeWakeNamespace:token('probe-wake-namespace',`${source.file}:${source.line}:${o.observerPid}`),reportedWakeGeneration:o.wakeGeneration??null,reportedWakeEpisode:o.wakeEpisode??null,sharedWakeNamespace:null,validApiReadObservations:0};
        state={start:o,life,stopped:false,lastTick:o.tick,lastMs:t.utcEpochMs};osStarts.set(key,state);observerLifetimes.push(life);
      }
      const start=state?.start||{},life=state?.life;
      let validity=life?.status==='bound-observer-start'?'matched':'unknown',reasons=[];
      if(!life)reasons.push('start-not-retained-before-this-row');
      if(state?.stopped){validity='contradicted';reasons.push('row-after-observer-stop');}
      if(life&&present(o.parentPid)&&o.parentPid!==life.parentPid||life&&present(o.parentCreationTime)&&String(o.parentCreationTime)!==life.parentCreated){validity='contradicted';reasons.push('observer-parent-identity-conflict');}
      if(t.status!=='normalized'||!integer(o.tick)||o.tick<0){validity='unknown';reasons.push('UTC-or-monotonic-tick-unknown');}
      else if(life&&(t.utcEpochMs<life.start.time.utcEpochMs||o.tick<life.start.tick||integer(state.lastTick)&&o.tick<state.lastTick||integer(state.lastMs)&&t.utcEpochMs<state.lastMs)){validity='contradicted';reasons.push('observer-lifetime-time-regression');}
      if(life&&integer(o.tick)&&integer(t.utcEpochMs)&&integer(life.start.tick)&&integer(life.start.time.utcEpochMs)) {
        const delta=(t.utcEpochMs-life.start.time.utcEpochMs)-(o.tick-life.start.tick);
        if(Math.abs(delta)>1500){validity='contradicted';reasons.push('UTC-tick-elapsed-disagree');}
      }
      const oi={observerToken:life?.observerLifetime??null,observerPid:integer(o.observerPid)?o.observerPid:null,observerLifetime:life?.observerLifetime??null,parentPid:life?.parentPid??null,parentCreated:life?.parentCreated??null,
        reportedProbeWakeGeneration:o.wakeGeneration??start.wakeGeneration??null,probeWakeNamespace:life?.probeWakeNamespace??null,sharedWakeNamespace:null,sharedWakeGeneration:null,
        generationEvidence:generationEvidence({...o,wakeGeneration:o.wakeGeneration??start.wakeGeneration},source.file,life?.observerLifetime??null,false),
        devicePathToken:token('device-path',o.path||o.devicePath||o.interfacePath||o.pnpInstanceId),deviceGuidToken:token('device-guid',o.guid||o.instanceGuid),wgiIdToken:token('wgi-id',o.nonRoamableId),inventoryId:integer(o.inventoryId)?o.inventoryId:null};
      const endpoint=probeEndpointProjection(o,event,source,token,start.formatVersion===6);
      Object.assign(oi,{interfacePathToken:endpoint.interfacePathToken,pnpInstanceToken:endpoint.pnpInstanceToken,reportedPnpInstanceToken:endpoint.reportedPnpInstanceToken,usbAncestorToken:endpoint.usbAncestorToken,reportedUsbAncestorToken:endpoint.reportedUsbAncestorToken,diInstanceGuidToken:endpoint.diInstanceGuidToken,wgiNonRoamableIdToken:endpoint.wgiNonRoamableIdToken});
      const facts={event,endpointMetadata:endpoint,evidenceScope:'independent-reader-api-observation;not-Steam-or-game-consumption',freshPhysicalActionVerified:false,
        observerLifecycle:{status:validity,reasons,startSource:life?.start.source??null,stopSource:life?.stop?.source??null},tick:integer(o.tick)?o.tick:null,
        apiIdentitySpace:event==='directinput-device'?'DirectInput-exact-interface-and-instance-GUID':event==='wgi-device'?'WGI-nonRoamableId;not-DirectInput-path':null};
      for(const k of ['stateApiSucceeded','decodedContentChanged','havePriorReading','freshMetadataHandleOpen','freshHidReadHandleOpen','readAccessOpen','readOnly','writesHardware','steamConsumptionProven','rawInputReceiptsRegistered'])if(typeof o[k]==='boolean')facts[k]=o[k];
      for(const k of ['comparisonGapMs','comparisonSeries','previousReadingInventoryId','directInputHresult','pollHresult','stateHresult','readingTimestamp','connectedGamepads','count','vid','pid','hidVid','hidPid','hidUsage','hidUsagePage','buttonsDownCount','acceptedMessages','hidMessagesExamined','readErrors','malformedMessages','unqualifiedMessages','deviceLimitDrops','windowStartTick','windowEndTick'])if(integer(o[k]))facts[k]=o[k];
      if(Array.isArray(o.collections))facts.collections=o.collections.slice(0,64).map(c=>({interfaceToken:token('device-path',c.interfacePath),handleToken:token('raw-handle',c.rawDeviceHandle),deviceGeneration:integer(c.deviceGeneration)?c.deviceGeneration:null,
        generationNamespace:life?`observer-device:${life.observerLifetime}:${token('device-path',c.interfacePath)}`:null,collectionRole:safe(c.collectionRole),
        ...Object.fromEntries(['vid','pid','usagePage','usage','messageReceipts','reportReceipts','changedPayloadReports','firstReceiptTick','lastReceiptTick','firstQueueTick','lastQueueTick','maxQueueDelayMs'].filter(k=>integer(c[k])).map(k=>[k,c[k]])),semanticInputDecoded:c.semanticInputDecoded===true,reportBytesLogged:c.reportBytesLogged===true}));
      facts.apiReadValidity=['directinput-device','wgi-device'].includes(event)&&o.stateApiSucceeded===true&&validity==='matched'?'observed':validity==='contradicted'?'contradicted':'unknown';
      const e=emit('L2','independent-os-observation',source,t,facts,oi);if(e){if(validity==='contradicted')e.observation='contradicted';else if(validity==='unknown')e.observation='unknown';}
      if(life){life.lastRetained={source,time:t,tick:o.tick??null};if(facts.apiReadValidity==='observed')life.validApiReadObservations++;state.lastTick=o.tick;state.lastMs=t.utcEpochMs;
        if(event==='stop'){life.stop={source,time:t,tick:o.tick??null,reason:safe(o.reason)};life.termination='logged-stop';state.stopped=true;}}

    }
    if(source.file.startsWith('game-consumer-receipts.log'))emit('L4','unverified-consumer-claim',source,t,{event,evidenceScope:'untrusted-producer;requires-original-process-and-action-verification'},{gamePid:o.gamePid??null,gameCreated:present(o.gameCreated)?String(o.gameCreated):null,sharedWakeNamespace:null,sharedWakeGeneration:null,generationEvidence:generationEvidence(o,source.file,null,false)});
  }
  let pending=null;const attributes=new Map();
  for(const row of rows.filter(r=>/(?:steam-controller(?:-ui)?\.log|steam-gameprocess\.log|steam\/logs\/(?:controller(?:_ui)?|gameprocess_log)\.txt)$/.test(r.source.file))) {
    const text=row.text,local=text.match(/^\[([^\]]+)\]/)?.[1];
    if(!local){if(pending){const serial=text.match(/^\s*Serial:\s*(.+)$/)?.[1];if(serial){pending.identity.steamSerialToken=token('serial',serial.trim());if(!pending.sources.some(s=>s.file===row.source.file&&s.line===row.source.line))pending.sources.push({...row.source,role:'controller-serial-attribute'});}const product=text.match(/^\s*ProductID:\s*(\d+)/)?.[1];if(product){pending.facts.productId=Number(product);if(!pending.sources.some(s=>s.file===row.source.file&&s.line===row.source.line))pending.sources.push({...row.source,role:'controller-product-attribute'});}}continue;}
    pending=null;const t=normalizeTime({time:local},offset);let m;
    if((m=text.match(/AppID (\d+) adding PID (\d+) as a tracked process (.*)/)))emit('context','steam-gameprocess-track',row.source,t,{evidenceScope:'Steam-tracked-process;no-input-or-OS-birth-readback',nativeGameId:m[1]},{appId:normalizeAppId(m[1]),gamePid:Number(m[2]),gamePathToken:pathToken(exe(m[3]))});
    else if((m=text.match(/AppID (\d+) no longer tracking PID (\d+)/)))emit('context','steam-gameprocess-untrack',row.source,t,{evidenceScope:'Steam-tracking-ended;not-controller-consumption'},{appId:normalizeAppId(m[1]),gamePid:Number(m[2])});
    else if((m=text.match(/Controller (\d+) (connected|disconnected)/))){const e=emit('L3','steam-controller-connection',row.source,t,{connected:m[2]==='connected',evidenceScope:'Steam-controller-list;not-game-input'},{controllerIndex:Number(m[1])});if(e)attributes.set(`${row.source.file}:${m[1]}`,e);}
    else if((m=text.match(/!! Controller (\d+) attributes:/)))pending=attributes.get(`${row.source.file}:${m[1]}`)||null;
    else if((m=text.match(/Loaded Config for (.*?) Path for App ID (\d+), Controller (\d+):\s*(.*)/))){const config=m[4].replace(/\\/g,'/').split('/').pop();emit('L3','steam-config-selection',row.source,t,{selectionKind:/^[A-Za-z ]{1,80}$/.test(m[1])?m[1]:'unclassified',configPathToken:pathToken(m[4]),emptyConfig:config==='empty.vdf',evidenceScope:'config-loaded;no-action-dispatch-or-game-consumption'},{appId:normalizeAppId(m[2]),controllerIndex:Number(m[3])});}
    else if((m=text.match(/Queueing activation for controller:\s*(\d+) app:\s*(-?\d+)/)))emit('L3','steam-config-activation-queued',row.source,t,{evidenceScope:'activation-queued;not-execution-or-game-input'},{controllerIndex:Number(m[1]),appId:normalizeAppId(m[2])});
    else if((m=text.match(/(?:Add to Config Cache Request|full cache hit) (\d+) (\d+)/)))emit('L3','steam-config-cache',row.source,t,{evidenceScope:'config-cache;not-game-input'},{controllerIndex:Number(m[1]),appId:normalizeAppId(m[2])});
    else if(/OnFocusWindowChanged|Lizard Mode|hid_read failure|PollState Changed/.test(text))emit('L3',/hid_read failure/.test(text)?'steam-hid-read-failure':/Lizard Mode/.test(text)?'steam-lizard-mode':'steam-focus-or-poll-state',row.source,t,{evidenceScope:'Steam-focus-or-read-state;not-original-game-consumption'},{appId:normalizeAppId(text.match(/AppID (-?\d+)/)?.[1])});
  }
  // Attribute each JSON claim to its containing snapshot and relevant fields.
  for(const e of evidence) {
    const source=e.sources[0],d=docs.get(source.file);
    if(!d||!source.jsonPointer)continue;
    const pointers=e.kind==='captured-original-instance'?['/generation','/before/recordEpoch','/before/steamClient']:
      e.kind==='original-game-inspection'?[source.jsonPointer.replace(/\/game$/,''),source.jsonPointer.replace(/\/game$/,'')+'/recordEpoch',source.jsonPointer.replace(/\/game$/,'')+'/input',source.jsonPointer.replace(/\/game$/,'')+'/gameApiProbe']:[];
    for(const ptr of pointers)if(d.locations.has(ptr)&&!e.sources.some(s=>s.jsonPointer===ptr))e.sources.push({...origin(d,ptr),role:'related-json-claim'});
  }
  for(const e of evidence) {
    if(!anchors.length)continue;const anchor=anchors.find(a=>a.identity.gamePid===e.identity.gamePid&&a.identity.gameCreated===e.identity.gameCreated)||(anchors.length===1?anchors[0]:null);if(!anchor){e.originalInstance={status:'unknown',basis:[],missingAxes:['unambiguous-original-instance-anchor']};continue;}
    const birth=filetimeMs(anchor.identity.gameCreated),time=e.time.utcEpochMs;
    const axes=e.layer==='L1'?['sharedWakeNamespace','sharedWakeGeneration','gamePid','gameCreated','gamePathToken','steamPid','steamCreated']:e.layer==='L2'?['sharedWakeNamespace','sharedWakeGeneration','gamePid','gameCreated','gamePathToken']:['appId','gamePid','gameCreated','gamePathToken'];
    const join=compareIdentity(anchor.identity,e.identity,axes),tooEarly=integer(time)&&integer(birth)&&time+(e.time.precisionMs??1)<=birth,overlapsBirth=integer(time)&&integer(birth)&&time<birth&&!tooEarly,clockUnknown=e.time.status!=='normalized';
    e.originalInstance={...join,status:tooEarly||e.time.status==='contradicted'?'contradicted':clockUnknown?'unknown':e.id===anchor.id?'matched':join.status,anchorEvidenceId:anchor.id,basis:e.id===anchor.id?['native-captured-identity-anchor']:join.matchingAxes.map(k=>`equal-${k}`),temporalStatus:tooEarly?'before-original-process-creation':clockUnknown?'unknown':overlapsBirth?'timestamp-precision-overlaps-original-creation':'not-before-original-creation',evidenceScope:'identity-relationship-only;never-controller-consumption'};
    if(e.layer==='L4'&&e.kind==='original-game-inspection'){const j=compareIdentity(anchor.identity,e.identity,['gamePid','gameCreated']);e.originalInstance.nativePidBirthComparison=j;e.originalInstance.basis=j.matchingAxes.map(k=>`native-reported-equal-${k}`);}
    if(e.layer==='L1') {const same=receipts.filter(r=>r.identity.runToken===e.identity.runToken&&r.identity.hostEpoch===e.identity.hostEpoch&&r.identity.hostToken===e.identity.hostToken&&r.identity.ymccLifetime===e.identity.ymccLifetime&&r.identity.sharedWakeNamespace===e.identity.sharedWakeNamespace&&r.identity.sharedWakeGeneration===e.identity.sharedWakeGeneration&&r.observation!=='contradicted'&&r.time.status==='normalized'&&e.time.status==='normalized'&&r.time.utcEpochMs<=e.time.utcEpochMs&&(r.sources[0].file!==e.sources[0].file||r.sources[0].line<=e.sources[0].line));e.hostTupleAssociation={status:e.identity.runToken&&e.identity.hostToken&&e.identity.ymccLifetime&&e.identity.sharedWakeNamespace&&same.length?'matched':'unknown',basis:'explicit-run-epoch-host-and-same-shared-wake-namespace-lifetime-only',evidenceIds:same.slice(0,12).map(r=>r.id)};}
  }
  for(const e of evidence.filter(e=>e.layer==='L2')) {const life=observerLifetimes.find(l=>l.observerLifetime===e.identity.observerLifetime);if(life)e.facts.observerLifecycle.stopSource=life.stop?.source??null;e.facts.nativeParentRelationship=nativeParentRelationship(life,boots,e.time);if(e.facts.observerLifecycle.status!=='matched'&&e.facts.nativeParentRelationship.status==='matched'){e.facts.nativeParentRelationship.status=e.facts.observerLifecycle.status;e.facts.nativeParentRelationship.nativeLifetime=null;e.facts.nativeParentRelationship.reasons.push('observer-row-lifecycle-not-valid');}}
  // The request-bound tuple is associated only within the retained native owner
  // lifetime. Current shared-power samples never supply its identity. Future
  // receipts and a reused PID cannot be borrowed by an earlier sparse event.
  for(const e of evidence.filter(e=>e.layer==='L1')) {
    const ack=e.facts.ackIdentity,tuple=ack?.protocolTuple;
    const valid=ack?.status==='verified-local-ack'&&tuple?.namespace&&e.time.status==='normalized'&&e.observation!=='contradicted';
    const same=valid?evidence.filter(r=>r.layer==='L1'&&r.facts.ackIdentity?.status==='verified-local-ack'&&r.time.status==='normalized'&&r.observation!=='contradicted'&&r.time.utcEpochMs<=e.time.utcEpochMs&&
      (r.sources[0].file!==e.sources[0].file||r.sources[0].line<=e.sources[0].line)&&compareIdentity(tuple,r.facts.ackIdentity.protocolTuple,['namespace','runToken','hostToken','hostEpoch','protocolPowerGeneration']).status==='matched'):[];
    e.protocolHostAssociation={status:valid?'matched':'unknown',basis:'native-local-verified-request-tuple-and-retained-owner-lifetime;no-current-power-or-game-join',evidenceIds:same.slice(-12).map(r=>r.id)};
  }
  const last=new Map();
  for(const e of evidence) {const scope=e.identity.ymccLifetime||e.identity.observerToken;if(!scope||!integer(e.time.utcEpochMs))continue;const key=`${e.sources[0].file}:${scope}`,prior=last.get(key);
    if(prior&&e.sources[0].line>prior.line&&e.time.utcEpochMs<prior.ms-1500){e.time.status='contradicted';e.time.basis='source-lifetime-clock-regression';e.originalInstance.status='contradicted';if(e.hostTupleAssociation)e.hostTupleAssociation.status='unknown';if(e.protocolHostAssociation)e.protocolHostAssociation.status='unknown';diagnostics.push({kind:'source-lifetime-clock-regression',status:'contradicted',evidenceId:e.id,priorEvidenceId:prior.id});}
    last.set(key,{ms:e.time.utcEpochMs,line:e.sources[0].line,id:e.id});
  }
  for(const e of evidence.filter(e=>e.kind==='wake-input-chain-sustained'&&![2,3].includes(e.facts.inputChainEvidenceSchema))) {const first=evidence.find(f=>f.kind==='wake-input-chain-first-frame'&&e.identity.sharedWakeNamespace&&f.identity.sharedWakeNamespace===e.identity.sharedWakeNamespace&&f.identity.sharedWakeGeneration===e.identity.sharedWakeGeneration);
    if(first&&integer(first.time.utcEpochMs)&&first.time.utcEpochMs===e.time.utcEpochMs)diagnostics.push({kind:'sustained-on-first-generation-frame',status:'contradicted',claim:'50-current-generation-sustained-frames',evidenceIds:[first.id,e.id],reason:'process-lifetime-cumulative-threshold;no-current-generation-50-frame-proof'});
  }
  const endpointNameCandidates=endpointNameBridge(evidence);
  const coverage={};for(const layer of ['L1','L2','L3','L4']){const ev=evidence.filter(e=>e.layer===layer);coverage[layer]={observations:ev.length,sourceFiles:[...new Set(ev.flatMap(e=>e.sources.map(s=>s.file)))],apiReadObservations:layer==='L2'?ev.filter(e=>e.facts.apiReadValidity==='observed'&&e.observation!=='contradicted').length:null,scope:layer==='L1'?'local-backend-ACK':layer==='L2'?'independent-OS-reader':layer==='L3'?'Steam-controller-and-config':'original-game-input',originalGameConsumption:'unknown'};}
  const declared=manifest.filter(r=>/^msi-controller-visibility.*\.log(?:\.\d+)?\s+\d+/.test(r.text)).map(r=>({file:r.text.split(/\s+/)[0],source:r.source}));
  return {schema:SCHEMA,inputDirectory:root,readOnly:true,privacy:{rawLinesIncluded:false,pathsSerialsRunsHostsAndSessionTokens:'bundle-scoped-HMAC-SHA256-pseudonyms',accountIdentifiersIncluded:false},clock:{utcOffsetMinutes:offset,offsetSource:present(options.utcOffsetMinutes)?'explicit-option':bias?'export-manifest':'missing',exportTime},analysisWindow:{startUtc:integer(startMs)?new Date(startMs).toISOString():null,endUtc:exportTime.utc,scope:'retained-native-lifetimes-through-export;not-complete-history',outsideWindowRows:outside},baseline:{packageImplementations:[...new Set([...docs.values()].map(d=>d.object.implementation).filter(v=>typeof v==='string').map(safe))],mainlineImplementation:options.mainlineImplementation?safe(options.mainlineImplementation):null,mainlineEvidenceScope:'caller-declared-source-marker;package-cannot-accept-later-builds'},endpointNameCandidates,generationNamespacePolicy:{sharedSchema:'native-lifecycle-common-fields:appendNativeLifecycleLog',sharedField:'powerGeneration',ownerRequired:'same-retained-YMCC-boot-lifetime',genericGeneration:'source-file+event+field;never-shared-by-number',recoveryAndProbeGeneration:'unbound-without-explicit-parent-lifetime-join',gameConsumptionPermissionGranted:false},files,nativeOwnerLifetimes:boots.map(b=>({nativeLifetime:b.token,pid:b.pid,time:b.time,nativeOwner:b.nativeOwner})),observerLifetimes,originalInstances:anchors.map(a=>({evidenceId:a.id,identity:a.identity})),coverage,
    layerClaims:[{layer:'L1',observation:coverage.L1.observations?'observed':'unknown',claim:'host-local-receipts',downstream:'unknown'},{layer:'L2',observation:coverage.L2.apiReadObservations?'observed':'unknown',claim:'independent-OS-reader',downstream:'unknown'},{layer:'L3',observation:coverage.L3.observations?'observed':'unknown',claim:'Steam-list-config-and-read-state',downstream:'unknown'},{layer:'L4',observation:'unknown',claim:'same-original-game-actual-input-consumption',reason:'no-trusted-action-correlated-original-process-input-receipt'}],conclusions:{originalGameInput:'unknown',continueAcceptance:'unknown',fourLayerEndToEnd:'unknown',hostOnlyEvidenceNeverPromoted:true,steamIndexToVirtualEndpoint:'unknown',generationNumbersAreProcessLocal:true,genericGenerationNeverUsedAsSharedWake:true},
    missingContracts:[{kind:'independent-reader-log-coverage',status:coverage.L2.apiReadObservations?'observed':'unknown',declaredButAbsentFromExplicitInput:declared.filter(r=>!files.some(f=>f.file===r.file)),reason:'missing-in-extracted-scope-does-not-prove-collector-or-export-failure'},{kind:'wake-frame-tuple-identity',status:evidence.some(e=>e.protocolHostAssociation?.status==='matched')?'observed':'unknown',scope:'L1-native-local-ACK-projection-only;never-L2-or-game-Continue',required:['native schema3 + nested ACK schema1','retained native owner lifetime','protocol run/host/epoch/power/request/sequence'],reason:'schema3 may expose verified protocol binding; sampled current power/epoch and nearby stats cannot supply it'},{kind:'Steam-controller-endpoint-join',status:'unknown',required:['Steam process+birth+session','Steam controller lifetime/index','same exact OS endpoint identity','layout/action-set identity','action receipt'],reason:'VID/PID/name/index/config cannot bind a virtual device or prove delivery'},{kind:'original-game-input-and-Continue',status:'unknown',required:['original PID+birth+path+Windows session','action time and identity','actual game response','real Continue click returning to same instance'],reason:'API modules/Running/ACK/config are separate observations'}],diagnostics,evidence};
}
export function parseArguments(args) {
  const o={};for(let i=0;i<args.length;i++){const a=args[i];if(a==='--help'||a==='-h'){o.help=true;continue;}const keys={'--input':'input','--output':'output','--utc-offset-minutes':'utcOffsetMinutes','--mainline-implementation':'mainlineImplementation'};if(!keys[a])throw Error(`Unknown argument: ${a}`);if(present(o[keys[a]]))throw Error(`Duplicate argument: ${a}`);const v=args[++i];if(!v||v.startsWith('--'))throw Error(`Missing value: ${a}`);o[keys[a]]=a==='--utc-offset-minutes'?Number(v):v;}
  if(!o.help&&!o.input)throw Error('An explicit --input directory is required.');return o;
}
function cli() {
  const o=parseArguments(process.argv.slice(2));if(o.help){console.log('Usage: node tools/steam_controller_consumer_evidence.mjs --input <extracted-directory> [--output <new-report.json>] [--utc-offset-minutes 480] [--mainline-implementation <source-marker>]\nExplicit input only. No product/process/device/network operations. stdout is JSON without --output. Output files must be new and outside input.');return;}
  const report=analyzeDirectory(o.input,o),text=JSON.stringify(report,null,2)+'\n';if(o.output){const output=path.resolve(o.output),parent=fs.realpathSync(path.dirname(output)),actual=path.join(parent,path.basename(output));if(inRoot(report.inputDirectory,actual))throw Error('Output must be outside the input directory.');fs.writeFileSync(actual,text,{encoding:'utf8',flag:'wx'});console.log(JSON.stringify({schema:SCHEMA,output:actual,evidence:report.evidence.length,coverage:report.coverage,conclusions:report.conclusions},null,2));}else process.stdout.write(text);
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){try{cli();}catch(e){console.error(e.message);process.exitCode=1;}}
