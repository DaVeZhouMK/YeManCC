/** A bounded schema-2 projection. No filesystem, ZIP, network or product imports. */
import { createHash } from 'node:crypto';
import { types as nodeTypes } from 'node:util';
export const FAN_API_LOG_LIMITS = Object.freeze({ maxBytes:1048576, maxLineBytes:16384, maxLines:1024, maxEvents:128, maxSpanMs:60000 });
const RECEIPT_BOOLS = ['ok','supported','resumeAccepted','resumeNoop','controlIntentCancelled'];
const CAPABILITY_BOOLS = ['ok','supported','mockZeroHardwareEvidence','hardwareCapable','hardwareWritesEnabled','hardwareWritesObserved','fanRouteWriteReady'];
// Current public FanRouteKind labels plus the explicit EC Host route; no arbitrary identifiers.
const FAN_ROUTES = new Set(['GenericDuty','GenericRpmTarget','ProfileCurve','EC']);
const PROFILE_CLASSES = new Set(['HandheldCompanion.Devices.GPDWin5','HandheldCompanion.Devices.XboxROGAlly','HandheldCompanion.Devices.XboxROGAllyX']);
const HOST_MODES = new Set(['source-derived-fixture','safe-no-hardware','real-hc','real-hc-unavailable','real-hc-fault-locked']);
const object = x => x !== null && typeof x === 'object' && !Array.isArray(x);
const own = (o,k) => Object.hasOwn(o,k);
const sha = bytes => createHash('sha256').update(bytes).digest('hex');
function jsonData(value,limit,ancestors=new Set(),depth=0,counter={n:0}) {
 try {
 if (++counter.n > 1024 || depth > 10) return false;
 if (value === null || value === undefined || typeof value === 'boolean') return true;
 if (typeof value === 'number') return Number.isFinite(value);
 if (typeof value === 'string') return value.length <= limit;
 if (typeof value !== 'object' || ancestors.has(value) || nodeTypes.isProxy(value)) return false;
 if(Reflect.ownKeys(value).some(k=>typeof k==='symbol'))return false;
 let prototype=value;let prototypeDepth=0;
 while(prototype!==null){if(++prototypeDepth>16||Object.getOwnPropertyDescriptor(prototype,'toJSON')!==undefined)return false;prototype=Object.getPrototypeOf(prototype);}
 ancestors.add(value);
 const descriptors=Object.getOwnPropertyDescriptors(value);
 if (Object.keys(descriptors).length > 128) return false;
 for (const [key,entry] of Object.entries(descriptors)) {
  if (key.length > 256 || !own(entry,'value') || !jsonData(entry.value,limit,ancestors,depth+1,counter)) return false;
 }
 ancestors.delete(value); return true;
 } catch { return false; }
}
function limitsOf(options) {
 const limits={...FAN_API_LOG_LIMITS};
 if (options === undefined) return limits;
 if (!object(options)) throw Error('LIMIT_OPTIONS_INVALID');
 for (const [key,value] of Object.entries(options)) {
  if (!own(limits,key) || !Number.isSafeInteger(value) || value<1 || value>FAN_API_LOG_LIMITS[key]) throw Error('LIMIT_OPTIONS_INVALID');
  limits[key]=value;
 }
 return limits;
}
function duplicateJsonKey(text) {
 const stack=[];
 for(let at=0;at<text.length;at++) {
  const ch=text[at];
  if(ch==='"') {
   const start=at;let escape=false;
   for(at++;at<text.length;at++){if(escape){escape=false;continue;}if(text[at]==='\\'){escape=true;continue;}if(text[at]==='"')break;}
   const current=stack.at(-1);
   if(current?.object&&current.key){const key=JSON.parse(text.slice(start,at+1));if(current.keys.has(key))return true;current.keys.add(key);current.key=false;}
  } else if(ch==='{')stack.push({object:true,key:true,keys:new Set()});
  else if(ch==='[')stack.push({object:false});
  else if(ch==='}'||ch===']')stack.pop();
  else if(ch===','&&stack.at(-1)?.object)stack.at(-1).key=true;
 }
 return false;
}
function readRecords(input,limits,diagnostic) {
 let bytes=0,hash=null,lines=0;const rows=[];
 if(nodeTypes.isProxy(input)){diagnostic('RECORD_ARRAY_OR_JSONL_REQUIRED');return {rows,bytes,hash,lines};}
 if (Array.isArray(input)) {
  lines=input.length;
  if (lines>limits.maxLines) {diagnostic('LINE_BUDGET_EXCEEDED');return {rows,bytes,hash,lines};}
  const digest=createHash('sha256');digest.update('[');bytes=2;
  for(let i=0;i<input.length;i++) {
   const element=Object.getOwnPropertyDescriptor(input,String(i));
   if(!element||!own(element,'value')){diagnostic('RECORD_JSON_DATA_REQUIRED',i+1);return {rows:[],bytes,hash,lines};}
   if(!jsonData(element.value,limits.maxLineBytes)) {diagnostic('RECORD_JSON_DATA_REQUIRED',i+1);return {rows:[],bytes,hash,lines};}
   let text;try{text=JSON.stringify(element.value);}catch{diagnostic('RECORD_JSON_DATA_REQUIRED',i+1);return {rows:[],bytes,hash,lines};}
   if(typeof text!=='string') {diagnostic('RECORD_JSON_DATA_REQUIRED',i+1);return {rows:[],bytes,hash,lines};}
   const size=Buffer.byteLength(text);if(size>limits.maxLineBytes){diagnostic('LINE_BYTE_BUDGET_EXCEEDED',i+1);return {rows:[],bytes,hash,lines};}
   bytes+=size+(i?1:0);if(bytes>limits.maxBytes){diagnostic('BYTE_BUDGET_EXCEEDED',i+1);return {rows:[],bytes,hash,lines};}
   if(i)digest.update(',');digest.update(text);rows.push({value:JSON.parse(text),line:i+1});
  }
  digest.update(']');hash=digest.digest('hex');
 } else if (typeof input==='string'||Buffer.isBuffer(input)) {
  if(typeof input==='string'&&input.length>limits.maxBytes){diagnostic('BYTE_BUDGET_EXCEEDED');return {rows,bytes:Buffer.byteLength(input),hash,lines};}
  const raw=Buffer.isBuffer(input)?input:Buffer.from(input);bytes=raw.length;
  if(bytes>limits.maxBytes){diagnostic('BYTE_BUDGET_EXCEEDED');return {rows,bytes,hash,lines};}
  hash=sha(raw);let text;try{text=new TextDecoder('utf-8',{fatal:true}).decode(raw).replace(/^\uFEFF/,'');}catch{diagnostic('INPUT_UTF8_INVALID');return {rows,bytes,hash,lines};}
  const items=text.split('\n');if(items.at(-1)==='')items.pop();lines=items.length;
  if(lines>limits.maxLines){diagnostic('LINE_BUDGET_EXCEEDED');return {rows,bytes,hash,lines};}
  for(let i=0;i<items.length;i++) {
   const line=items[i].replace(/\r$/,'');if(Buffer.byteLength(line)>limits.maxLineBytes){diagnostic('LINE_BYTE_BUDGET_EXCEEDED',i+1);continue;}
   if(!line.trim())continue;
   try{const value=JSON.parse(line);if(duplicateJsonKey(line)){diagnostic('INPUT_JSON_DUPLICATE_KEY',i+1);continue;}if(!jsonData(value,limits.maxLineBytes))throw Error();rows.push({value,line:i+1});}catch{diagnostic('INPUT_JSON_LINE_INVALID',i+1);}
  }
 } else diagnostic('RECORD_ARRAY_OR_JSONL_REQUIRED');
 return {rows,bytes,hash,lines};
}
/** sourceIdentity.sha256 hashes the array's JSON.stringify bytes, or exact JSONL bytes. */
export function normalizeFanApiRecords(records,{sourceIdentity,limits:requestedLimits}={}) {
 const diagnostics=[];let rejected=false,incomplete=false;
 function diagnostic(code,recordIndex=null,severity='reject') {
  if(severity==='reject')rejected=true;else incomplete=true;
  if(diagnostics.length<63)diagnostics.push({code,recordIndex,severity});
  else if(diagnostics.length===63){diagnostics.push({code:'DIAGNOSTIC_BUDGET_EXCEEDED',recordIndex:null,severity:'reject'});rejected=true;}
 }
 let limits;try{limits=limitsOf(requestedLimits);}catch{limits={...FAN_API_LOG_LIMITS};diagnostic('LIMIT_OPTIONS_INVALID');}
 const input=readRecords(records,limits,diagnostic);
 const identityKind=sourceIdentity?.kind??'synthetic-json';
 const identityValid=object(sourceIdentity)&&typeof sourceIdentity.sha256==='string'&&/^[a-f0-9]{64}$/.test(sourceIdentity.sha256)
  &&['synthetic-json','source-derived-fixture'].includes(identityKind);
 if(!identityValid)diagnostic('SOURCE_IDENTITY_REQUIRED');
 else if(input.hash&&sourceIdentity.sha256!==input.hash)diagnostic('SOURCE_IDENTITY_MISMATCH');
 let origin=null,previousTime=null,previousGeneration=null;const events=[],seen=new Set();
 for(const {value:row,line} of input.rows) {
  const bad=code=>diagnostic(code,line);
  if(!object(row)||typeof row.event!=='string'||!object(row.details)){bad('RECORD_ENVELOPE_INVALID');continue;}
  if(row.event==='api.request-failure'){diagnostic('RESPONSE_REQUEST_FAILURE_NOT_TRANSPORT',line,'incomplete');continue;}
  if(!['api.response','api.transport-failure'].includes(row.event)){diagnostic('RECORD_EVENT_NOT_CONNECTED',line,'incomplete');continue;}
  const d=row.details;
  if(d.observationSchemaVersion!==2){bad(own(d,'observationSchemaVersion')?'OBSERVATION_VERSION_UNSUPPORTED':'OBSERVATION_VERSION_MISSING');continue;}
  if(!Number.isSafeInteger(row.atMs)||row.atMs<0){bad('OBSERVATION_TIME_REQUIRED');continue;}
  if(previousTime!==null&&row.atMs<previousTime){bad('OBSERVATION_ORDER_REVERSED');continue;}
  if(origin===null)origin=row.atMs;previousTime=row.atMs;
  if(row.atMs-origin>limits.maxSpanMs){bad('TIME_SPAN_BUDGET_EXCEEDED');continue;}
  if(!Number.isSafeInteger(d.generation)||d.generation<0){bad('GENERATION_TYPE_INVALID');continue;}
  if(previousGeneration!==null&&d.generation<previousGeneration){bad('GENERATION_ORDER_REVERSED');continue;}previousGeneration=d.generation;
  if(!Number.isFinite(d.durationMs)||d.durationMs<0||d.durationMs>limits.maxSpanMs){bad('DURATION_TYPE_OR_BUDGET_INVALID');continue;}
  if(typeof d.method!=='string'||typeof d.path!=='string'){bad('ROUTE_FIELD_TYPE_INVALID');continue;}
  let event;
  if(row.event==='api.response') {
   if(d.path!=='/api/handshake'){diagnostic('API_RESPONSE_ROUTE_NOT_CONNECTED',line,'incomplete');continue;}
   if(d.method!=='POST'){bad('HANDSHAKE_METHOD_INVALID');continue;}
   if(!Number.isInteger(d.status)||d.status<100||d.status>599||typeof d.ok!=='boolean'||typeof d.receiptValid!=='boolean'){bad('RESPONSE_ENVELOPE_TYPE_INVALID');continue;}
   if(!object(d.receipt)||!object(d.response)){bad('ROOT_RECEIPT_AND_RESPONSE_REQUIRED');continue;}
   if(RECEIPT_BOOLS.some(k=>own(d.receipt,k)&&typeof d.receipt[k]!=='boolean')
      ||CAPABILITY_BOOLS.some(k=>own(d.response,k)&&typeof d.response[k]!=='boolean')){bad('RECEIPT_BOOLEAN_TYPE_INVALID');continue;}
   if(typeof d.receipt.ok!=='boolean'||typeof d.receipt.supported!=='boolean'||typeof d.response.ok!=='boolean'||typeof d.response.supported!=='boolean'){bad('HANDSHAKE_PROJECTION_INCOMPLETE');continue;}
   if(RECEIPT_BOOLS.some(k=>own(d.receipt,k)&&own(d.response,k)&&d.receipt[k]!==d.response[k])){bad('ROOT_RECEIPT_RESPONSE_CONTRADICTION');continue;}
   const envelopeOk=d.status>=200&&d.status<300&&d.receipt.ok!==false&&d.receiptValid;
   if(d.ok!==envelopeOk){bad('RESPONSE_ACCEPTANCE_CONTRADICTION');continue;}
   if(!d.receiptValid){bad('RECEIPT_DECLARED_INVALID');continue;}
   if(typeof d.bodyCompleteObserved!=='boolean'||(own(d,'bodyComplete')&&typeof d.bodyComplete!=='boolean')){bad('BODY_COMPLETENESS_TYPE_INVALID');continue;}
   if(!d.bodyCompleteObserved){if(own(d,'bodyComplete'))bad('BODY_COMPLETENESS_CONTRADICTION');else diagnostic('BODY_COMPLETENESS_UNOBSERVED',line,'incomplete');continue;}
   if(d.bodyComplete!==true){bad('BODY_INCOMPLETE');continue;}
   if(d.status<200||d.status>=300){diagnostic('HANDSHAKE_HTTP_STATUS_NOT_REPLAYABLE',line,'incomplete');continue;}
   const r=d.response;
   if(own(r,'protocolVersion')&&(!Number.isSafeInteger(r.protocolVersion)||r.protocolVersion<1)){bad('PROTOCOL_VERSION_TYPE_INVALID');continue;}
   if(own(r,'hostMode')&&typeof r.hostMode!=='string'){bad('HOST_MODE_TYPE_INVALID');continue;}
   if(own(r,'hostMode')&&!HOST_MODES.has(r.hostMode)){diagnostic('HOST_MODE_NOT_REVIEWED',line,'incomplete');continue;}
   if((r.hostMode==='real-hc-unavailable'&&d.receipt.supported)
      ||(r.hardwareCapable===false&&(d.receipt.supported||r.fanRouteWriteReady===true))
      ||(d.receipt.supported===false&&r.fanRouteWriteReady===true)
      ||(r.mockZeroHardwareEvidence===true&&(r.hardwareWritesEnabled===true||r.hardwareWritesObserved===true))){bad('HANDSHAKE_CAPABILITY_CONTRADICTION');continue;}
   if(own(r,'fanRoute')&&r.fanRoute!==null&&typeof r.fanRoute!=='string'){bad('FAN_ROUTE_TYPE_INVALID');continue;}
   if(own(r,'fanRoute')&&r.fanRoute!==null&&!FAN_ROUTES.has(r.fanRoute)){diagnostic('FAN_ROUTE_NOT_REVIEWED',line,'incomplete');continue;}
   if(own(r,'deviceClass')&&r.deviceClass!==null&&typeof r.deviceClass!=='string'){bad('DEVICE_CLASS_TYPE_INVALID');continue;}
   const profileClass=PROFILE_CLASSES.has(r.deviceClass)?r.deviceClass:null;
   if(profileClass&&r.deviceIdentity!==undefined&&r.deviceIdentity!==null&&(!object(r.deviceIdentity)||Object.keys(r.deviceIdentity).length>0)){diagnostic('PRIVATE_DEVICE_IDENTITY_NOT_REPLAYABLE',line,'incomplete');continue;}
   const receipt=Object.fromEntries(RECEIPT_BOOLS.filter(k=>own(d.receipt,k)).map(k=>[k,d.receipt[k]]));
   const response=Object.fromEntries(CAPABILITY_BOOLS.filter(k=>own(r,k)).map(k=>[k,r[k]]));
   for(const k of ['hostMode','protocolVersion'])if(own(r,k))response[k]=r[k];
   if(FAN_ROUTES.has(r.fanRoute))response.fanRoute=r.fanRoute;
   if(profileClass)response.deviceClass=profileClass;
   event={event:'startup.handshake',ok:receipt.ok,supported:receipt.supported,
     ...Object.fromEntries(['mockZeroHardwareEvidence','hardwareCapable','hardwareWritesEnabled','hardwareWritesObserved','fanRouteWriteReady','hostMode','protocolVersion','fanRoute','deviceClass'].filter(k=>own(response,k)).map(k=>[k,response[k]])),
     receipt,response,observation:{event:'api.response',status:d.status,receiptValid:true,bodyCompleteObserved:true,bodyComplete:true,responseLocation:'root-or-state-projection-unproven',protocolObserved:own(r,'protocolVersion')}};
  } else {
   if(d.path!=='/api/state'||d.method!=='GET'){diagnostic('TRANSPORT_ROUTE_NOT_CONNECTED',line,'incomplete');continue;}
   if(d.responseReceived!==false||own(d,'status')||own(d,'receipt')||own(d,'response')){bad('TRANSPORT_RESPONSE_CONTRADICTION');continue;}
   if(typeof d.error!=='string'){bad('TRANSPORT_ERROR_TYPE_INVALID');continue;}
   if(!/^HTTP request failed \(WinHTTP 12029, phase=send\)$/.test(d.error)){diagnostic('TRANSPORT_FAILURE_NOT_REVIEWED',line,'incomplete');continue;}
   event={event:'api.transport-failure',path:'/api/state',winHttpCode:12029,transportPhase:'send',observation:{event:'api.transport-failure',responseReceived:false,result:'transport-failure'}};
  }
  const projected={...event,atMs:row.atMs-origin,generation:d.generation,durationMs:d.durationMs};
  const fingerprint=sha(JSON.stringify(projected));if(seen.has(fingerprint)){bad('DUPLICATE_OBSERVATION');continue;}seen.add(fingerprint);
  if(events.length>=limits.maxEvents){bad('EVENT_BUDGET_EXCEEDED');continue;}
  events.push({id:'fan-api-event-'+String(events.length+1).padStart(6,'0'),domain:'fan',provenance:'source-derived-fixture',memberId:'member-1',line,...projected});
 }
 if(!input.rows.length)diagnostic('NO_RECORDS');
 if(!events.length&&!rejected)diagnostic('NO_CONNECTED_OBSERVATIONS',null,'incomplete');
 return {events:rejected?[]:events,diagnostics,scope:{status:rejected?'REJECTED':incomplete?'INCOMPLETE':'NORMALIZED',sourceIdentity:identityValid&&input.hash===sourceIdentity.sha256?{sha256:input.hash,kind:identityKind}:null,
  inputBytes:input.bytes,inputLines:input.lines,connectedEvents:rejected?0:events.length,limits,
  schema:'fan-api observationSchemaVersion2',reconstruction:'source-derived-fixture',originalUserIntentEstablished:false,originalSessionCorrelationEstablished:false,originalCausalityEstablished:false,
  physicalWriteConfirmed:false,physicalRestoreConfirmed:false,ordinaryZipConnected:false,fieldMeaning:'receipt=root acceptance; response=unlocated state-or-root projection; types and conflicts checked; no root promotion',
  omitted:'unlisted fields, dynamic identities, original IDs, credentials, paths, games, sessions, tokens and error free text are not returned'}};
}