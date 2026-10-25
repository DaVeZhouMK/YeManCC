import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';import {auditReceipts} from '../receipt-audit.mjs';
const sha=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const key=file=>process.platform==='win32'?path.resolve(file).toLowerCase():path.resolve(file);
const validHash=value=>typeof value==='string'&&/^[a-f0-9]{64}$/.test(value);
export function verifyUpstreamLocations(upstream,selectedCaseDir){
 if(!upstream||!['originalGlobalResult','caseDir','payload','sourceReceipt'].every(name=>typeof upstream[name]==='string'&&path.isAbsolute(upstream[name])&&!/[\0\r\n]/.test(upstream[name]))||!['originalGlobalResultSha256','payloadSha256','sourceReceiptSha256'].every(name=>validHash(upstream[name]))||typeof selectedCaseDir!=='string'||!path.isAbsolute(selectedCaseDir))throw Error('FAN_BROWSER_UPSTREAM_SCHEMA_REQUIRED');
 const expectedReceipt=path.join(selectedCaseDir,'fan-monitor/result.json'),expectedPayload=path.join(selectedCaseDir,'fan-monitor/positive/private-output/NATIVE-FAN-PAYLOADS.json');
 if(key(upstream.caseDir)!==key(selectedCaseDir)||key(upstream.sourceReceipt)!==key(expectedReceipt)||key(upstream.payload)!==key(expectedPayload))throw Error('FAN_BROWSER_UPSTREAM_CASE_BINDING');
 return{caseDir:path.resolve(selectedCaseDir),sourceReceipt:expectedReceipt,payload:expectedPayload};
}
export function inspectFanMonitorUpstream(upstream,{repoRoot}={}){
 if(!upstream||typeof repoRoot!=='string'||!path.isAbsolute(repoRoot)||!path.isAbsolute(upstream.originalGlobalResult??''))throw Error('FAN_BROWSER_UPSTREAM_PATHS_REQUIRED');
 const audit=auditReceipts([upstream.originalGlobalResult],{repoRoot}),selected=audit.cases.filter(row=>row.id==='sensor.passive-fan-monitor-consumer');
 if(selected.length!==1)throw Error('FAN_BROWSER_UPSTREAM_CASE_REQUIRED');
 const location=verifyUpstreamLocations(upstream,selected[0].caseDir),actualCase=fs.realpathSync(location.caseDir);
 for(const file of [location.sourceReceipt,location.payload]){const actual=fs.realpathSync(file),relative=path.relative(actualCase,actual);if(relative==='..'||relative.startsWith('..'+path.sep)||path.isAbsolute(relative))throw Error('FAN_BROWSER_UPSTREAM_REPARSE_ESCAPE');}
 const exactReceipt=sha(upstream.originalGlobalResult)===upstream.originalGlobalResultSha256,exactPayload=sha(location.payload)===upstream.payloadSha256,exactSourceReceipt=sha(location.sourceReceipt)===upstream.sourceReceiptSha256;
 const original=JSON.parse(fs.readFileSync(upstream.originalGlobalResult,'utf8').replace(/^\uFEFF/,'')),rawRows=original.results.filter(row=>row.id==='sensor.passive-fan-monitor-consumer'),source=JSON.parse(fs.readFileSync(location.sourceReceipt,'utf8').replace(/^\uFEFF/,''));
 const sourceReceiptPinned=rawRows.length===1&&(rawRows[0].structuredEvidence??[]).some(row=>typeof row.path==='string'&&key(row.path)===key(location.sourceReceipt)&&row.sha256===upstream.sourceReceiptSha256);
 const payloadRows=(Array.isArray(source.sourceHashes)?source.sourceHashes:[]).filter(row=>typeof row.path==='string'&&path.isAbsolute(row.path)&&key(row.path)===key(location.payload));
 const payloadPinned=payloadRows.length===1&&payloadRows[0].sha256===upstream.payloadSha256;
 const current=audit.status==='CURRENT_RECEIPTS_VALID'&&selected[0].status==='PASS'&&exactReceipt&&exactPayload&&exactSourceReceipt&&sourceReceiptPinned&&payloadPinned;
 return{status:current?'CURRENT_UPSTREAM':'UPSTREAM_NOT_CURRENT',audit,exactReceipt,exactPayload,exactSourceReceipt,sourceReceiptPinned,payloadPinned,caseBinding:true,browserExecuted:false,nativeExecuted:false,fullGoalComplete:false};
}
export function requireCurrentFanMonitorUpstream(upstream,options){const result=inspectFanMonitorUpstream(upstream,options);if(result.status!=='CURRENT_UPSTREAM')throw Object.assign(Error('FAN_BROWSER_UPSTREAM_NOT_CURRENT'),{upstreamInspection:result});return result;}