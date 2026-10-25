import fs from 'node:fs';
import path from 'node:path';
import {pathToFileURL} from 'node:url';
import {repoRoot,tests,readJson,sha256,assertCurrent,inside} from './common.mjs';
import {adaptTest} from './adapt.mjs';
import {checkProductReceipts} from './receipt-probe.mjs';
const config=readJson(process.argv[2]);
if(path.resolve(config.repoRoot)!==repoRoot)throw Error('WRONG_REPOSITORY');
const spec=tests.find(test=>test.id===config.id);
if(!spec||!inside(config.evidenceRoot,config.caseDir))throw Error('UNREVIEWED_CASE');
process.env[spec.env]=config.caseDir;
process.env.YMCC_MONITOR_AUDIT_CONFIG=process.argv[2];
// Product terminal calls are injected by the retained test. Any new network or
// real channel use is a regression, never an ambient fallback.
for(const name of ['fetch','WebSocket','BroadcastChannel'])Object.defineProperty(globalThis,name,{configurable:true,value:function(){throw Error('MONITOR_REAL_IO_DENIED:'+name)}});
let adaptation=null,audit=null,receipt=null,error=null;
try{
 assertCurrent(config.currentHashes);
 const source=fs.readFileSync(path.join(repoRoot,spec.source),'utf8');
 if(sha256(source)!==config.testSha256)throw Error('TEST_SOURCE_DRIFT');
 adaptation=adaptTest(spec,source,{auditRuntime:path.resolve('tools/ymcc_sim/global/monitor-signals/audit-runtime.mjs')});
 fs.writeFileSync(path.join(config.caseDir,'adaptations.json'),JSON.stringify({...adaptation,text:undefined},null,2));
 const script=path.join(config.caseDir,'retained-test.mjs');
 fs.writeFileSync(script,adaptation.text);
 await import(pathToFileURL(script).href);
 const runtime=await import('./audit-runtime.mjs');
 if(spec.id==='consumer-lifecycle')await checkProductReceipts(globalThis.__monitorRetainedExports,runtime.trace);
 const ports=spec.id==='consumer-lifecycle'?['IPC start/stop/snapshot/takeover','virtual timer/clock','snapshot/TDP publication','text-file access throws']:
  spec.id==='clock-demand'?['IPC unavailable','notification host inert','policy game access throws','preview window/channel injected','clock/carrier/router ports inert','output preferences memory only; placement save throws','monitorData subscriptions memory only; unowned snapshot reuse disabled','Intl formatting counter only']:
  ['pure HUD data input; no external product port'];
 for(const name of ports)runtime.recordPort(name,'reviewed fixture terminal; actual product consumer retained');
 audit=runtime.writeAudit();
 const receiptFile=path.join(config.caseDir,spec.receipt);
 if(!fs.existsSync(receiptFile))throw Error('MISSING_ORIGINAL_RECEIPT');
 receipt=readJson(receiptFile);
 if(receipt.ok!==true)throw Error('ORIGINAL_RECEIPT_NOT_OK');
 if(audit.loadedProductSources.length===0||audit.inputConsumerStateFeedback.length===0)throw Error('SOURCE_EXECUTION_EVIDENCE_MISSING');
 assertCurrent(config.currentHashes);
}catch(caught){error={code:caught.code??caught.message.split(':')[0],message:caught.message,stack:caught.stack};
 try{audit=(await import('./audit-runtime.mjs')).writeAudit();}catch(auditError){error.auditFailure=auditError.message;}}
const currentRows=audit?.loadedProductSources.filter(row=>row.kind==='current-product-source')??[];
const executedFunctions={
 'consumer-lifecycle':{
  'src/bridge/monitorData.ts':['createMonitorDataHub','hub.acquire/read/admitDisplayPublication/applied/pending/desired/attempts'],
  'src/bridge/monitorDisplayFeed.ts':['createLatestDisplayFeed','monitorSnapshotDisplayKey','feed.subscribe/push/advance/dispose'],
  'src/bridge/monitorHudSampleSource.ts':['createHudSampleSource','source.acquire/sample/users'],
  'src/bridge/monitorHudFreshness.ts':['monitorSnapshotHasFreshData','createHudAgeLease'],
  'src/bridge/monitorHudModel.ts':['hudVisibleFields','hudDataPresetFields','hudFromUniformSnapshot (sample-source consumer)'],
 },
 'clock-demand':{
  'src/bridge/monitorHudModel.ts':['hudFromUniformSnapshot','normalizeMonitorOutput'],
  'src/bridge/monitorHudSampleSource.ts':['createHudSampleSource','source.acquire/sample/setClockEnabled'],
  'src/bridge/monitorTopProjection.ts':['projectTopMonitorView'],
  'src/bridge/monitorHudPreview.ts':['createHudPreviewOwner','owner.open/update/close/dispose'],
  'src/bridge/monitorHudHost.ts':['startMonitorHudHost','owner.ready/stop','preference/applied callbacks'],
  'src/bridge/steamHudRelay.ts':['createSteamHudRelay','relay.connected/dispose (hidden frame)'],
 },
 'bounded-identity':{'src/bridge/monitorHudModel.ts':['makeHudFixture','boundedHudData','sameHudData']},
}[spec.id];
const executionBoundaries=Object.entries(executedFunctions).filter(([file])=>currentRows.some(row=>row.relative===file))
 .map(([file,functions])=>({path:file,functions,scope:'named calls exercised by retained assertions; remaining module branches unverified'}));
const result={schema:'ymcc.monitor-case.v1',id:spec.id,ok:error===null,status:error?'FAIL':'PASS',
 originalTest:{path:spec.source,sha256:config.testSha256,allOriginalAssertionsPreserved:adaptation?.assertionContract.allOriginalAssertionsPreserved??false,
 assertionCalls:adaptation?.assertionContract.originalAssertionCalls??null,receipt:spec.receipt},
 originalReceipt:receipt,error,adaptations:adaptation?.adaptations??[],sourceHashes:Object.fromEntries(currentRows.map(row=>[row.relative,row.sha256])),
 executedProductModules:executionBoundaries.map(row=>row.path),executionBoundaries,compiledProductSources:currentRows.map(row=>row.relative),inputConsumerStateFeedback:audit?.inputConsumerStateFeedback??[],
 sourceExecution:audit,terminalBoundary:{nativeApp:false,realHost:false,hardware:false,userSettings:false,realGameIdentity:false,
 onlyFilesystem:'reviewed source/baseline reads and fresh owned evidence writes',actualIO:'compiler loads and Node evidence files',time:'virtual clock and injected timer queues'},
 scope:'retained real-product regression; no physical monitoring or live game claim'};
fs.writeFileSync(path.join(config.caseDir,'case-result.json'),JSON.stringify(result,null,2));
console.log(JSON.stringify({id:result.id,status:result.status,steps:result.inputConsumerStateFeedback.length,error:result.error?.message}));
process.exitCode=error?1:0;
