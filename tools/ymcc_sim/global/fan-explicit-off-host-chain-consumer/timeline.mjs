import {need} from './contract.mjs';
const controlPaths=['/api/open','/api/open-events','/api/acquire-control','/api/enable'];
/** Client send/receive ordering only; server arrival and physical device timing are not inferred. */
export function verifyRequestTimeline(output,node){
 const rows=output.timeline;need(output.timelineSchemaVersion===1&&Array.isArray(rows)&&rows.length>0,'EXPLICIT_TIMELINE_MISSING');
 need(rows.every((r,i)=>r.sequence===i+1&&Number.isFinite(r.at)&&(i===0||r.at>=rows[i-1].at)),'EXPLICIT_TIMELINE_ORDER');
 const issued=rows.filter(r=>r.event==='request-issued'),received=rows.filter(r=>r.event==='response-received');need(issued.length===node.requests.length&&received.length===node.requests.length,'EXPLICIT_TIMELINE_HTTP_COUNT');
 for(let i=0;i<node.requests.length;i++){const actual=node.requests[i],send=issued[i],receive=received.find(r=>r.request===i+1);need(send.request===i+1&&send.path===actual.path&&receive?.path===actual.path&&receive.status===actual.status&&receive.sequence>send.sequence,'EXPLICIT_TIMELINE_HTTP_BINDING');}
 const sleep=rows.find(r=>r.event==='sleep-observed'),off=rows.find(r=>r.event==='disable-completed');need(sleep&&off&&sleep.sequence<off.sequence,'EXPLICIT_TIMELINE_SLEEP_OFF');
 const controls=issued.filter(r=>controlPaths.includes(r.path));for(const control of controls){const read=rows.filter(r=>r.sequence<control.sequence&&r.event==='native-read').at(-1);need(control.sequence>off.sequence&&control.native?.phase==='ready'&&control.native.hardwareWritesAllowed===true&&control.native.generation===control.coordinator?.powerGeneration,'EXPLICIT_NEW_CONTROL_WITHOUT_NATIVE_PERMISSION');need(read?.native?.phase==='ready'&&read.native.hardwareWritesAllowed===true&&read.native.generation===control.native.generation,'EXPLICIT_NEW_CONTROL_WITHOUT_OBSERVED_NATIVE_PERMISSION');}
 const refusal=rows.find(r=>r.event==='control-refused');if(refusal)need(!controls.some(r=>r.sequence>refusal.sequence),'EXPLICIT_NEW_CONTROL_AFTER_REFUSAL');
 if(output.mode==='sleep'||output.mode==='stale-native')need(output.ok===true&&output.error.includes('FAN_MANUAL_WAKE_NOT_READY')&&controls.length===0&&!!refusal,'EXPLICIT_REFUSAL_MODE_ACCEPTED_CONTROL');
 return{schemaVersion:1,status:'PASS',mode:output.mode,httpRequests:issued.length,newControlsAfterOff:controls.length,refused:!!refusal,clientTimeline:true,serverArrivalInferred:false,physicalAcceptance:false};
}
