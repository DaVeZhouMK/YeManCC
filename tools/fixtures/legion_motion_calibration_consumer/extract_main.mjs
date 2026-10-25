// Copy exact local production bytes only. Missing future methods are readiness, not a test pass.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
export const sha=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
export const requiredFunctions=[
  'inputCaptureControllerCalibrationRequest','inputCaptureControllerCalibrationStatus',
  'inputCaptureControllerCalibrationStore','inputCaptureControllerCalibrationAdvance',
  'inputCaptureControllerCalibrationAbort',
];
export const supportingFunctions=[
  'ascii_lower','legionBackIsPid','legionSBackIsPid','inputCaptureLegionControllerMotionRoute',
  'legionMotionSelectedSensorIndex','legionMotionSelectSensorIndex','legionMotionRevoke',
  'legionMotionHardwareAdmitted','legionMotionCollectionAdmitted','legionMotionIdentityAdmitted',
  'legionMotionStableIdentity','legionMotionSelectedIdentityChanging',
  'legionMotionSourceForSelectedLocked','legionMotionPublish','legionMotionSnapshot',
  'legionMotionSubmissionCurrent','inputCaptureControllerCalibration',
  'inputCaptureLegionMotionSessionRevoke','inputCaptureLegionMotion',
];
export function endBody(text,start){
  assert.equal(text[start],'{');let depth=0,mode='code',rawEnd='';
  for(let i=start;i<text.length;++i){
    const c=text[i],next=text[i+1];
    if(mode==='raw'){if(text.startsWith(rawEnd,i)){i+=rawEnd.length-1;mode='code';}continue;}
    if(mode==='line'){if(c==='\n')mode='code';continue;}
    if(mode==='block'){if(c==='*'&&next==='/'){++i;mode='code';}continue;}
    if(mode==='string'||mode==='char'){
      if(c==='\\'){++i;continue;}if(c===(mode==='string'?'"':"'"))mode='code';continue;
    }
    if(c==='/'&&next==='/'){++i;mode='line';continue;}
    if(c==='/'&&next==='*'){++i;mode='block';continue;}
    if(c==='R'&&next==='"'){const open=text.indexOf('(',i+2);assert(open>=0&&open-i<=18);
      rawEnd=')'+text.slice(i+2,open)+'"';mode='raw';i=open;continue;}
    if(c==='"'){mode='string';continue;}
    if(c==="'"){mode='char';continue;}
    if(c==='{')++depth;
    if(c==='}'&&--depth===0)return i+1;
  }throw Error('Unclosed exact source body');
}
function region(text,symbol,kind,start,end){const code=text.slice(start,end);return {symbol,kind,code,sha256:sha(code),
  startLine:text.slice(0,start).split('\n').length,endLine:text.slice(0,end).split('\n').length,
  startUtf8Byte:Buffer.byteLength(text.slice(0,start)),endUtf8ByteExclusive:Buffer.byteLength(text.slice(0,end))};}
export function extract(text,symbol,kind='function',required=true){
  const regex=kind==='struct'?new RegExp('^struct '+symbol+'\\s*\\{','gm'):
    new RegExp('^static\\s+(?:(?![;{}])[\\s\\S])*?\\b'+symbol+'\\s*\\(','gm');
  const found=[];
  for(const match of text.matchAll(regex)){
    const open=text.indexOf('{',match.index),semi=text.indexOf(';',match.index);
    if(open<0||(semi>=0&&semi<open))continue;
    let end=endBody(text,open);if(kind==='struct'){assert.equal(text[end],';');++end;}
    found.push(region(text,symbol,kind,match.index,end));
  }
  assert(found.length<=1,'Ambiguous actual source definition: '+symbol);
  if(required)assert.equal(found.length,1,'Missing actual source definition: '+symbol);
  return found[0]||null;
}
function uiRegion(text,symbol,head,arrow=false){
  const found=[...text.matchAll(head)];assert.equal(found.length,1,'UI anchor '+symbol);
  const start=found[0].index,open=arrow?text.indexOf('{',text.indexOf('=>',start)):text.indexOf('{',start);
  return region(text,symbol,'ui-source-only',start,endBody(text,open));
}
function ipcRegion(text,operation){
  const needle='ipc_on("'+operation+'",';const start=text.indexOf(needle);assert(start>=0&&text.indexOf(needle,start+1)<0);
  const open=text.indexOf('{',text.indexOf('-> json',start));
  return region(text,operation,'ipc-source-only',start,endBody(text,open));
}
export function freeze(repo,out){
  const mainBytes=fs.readFileSync(path.join(repo,'native/main.cpp')),text=mainBytes.toString('utf8');
  const entries=[...supportingFunctions,...requiredFunctions].map(name=>extract(text,name,'function',false)).filter(Boolean);
  const missingFunctions=requiredFunctions.filter(name=>!entries.some(e=>e.symbol===name));
  const identity=extract(text,'HidHidePhysicalIdentity','struct');entries.push(identity);
  const constants=[];
  for(const name of ['kLegionBackPids','kLegionSPids','kLegionGyroSide','g_legionMotionEpochStartedAt']){
    const matches=[...text.matchAll(new RegExp('^static[^\\r\\n]*\\b'+name+'(?:\\[\\])?\\s*(?:=|\\{)[^;]*;','gm'))];
    assert.equal(matches.length,1,'Constant declaration '+name);const m=matches[0];
    constants.push(region(text,name,'constant',m.index,m.index+m[0].length));
  }
  const anchors=['inputCaptureOnGamepad','inputCaptureShutdown','inputCaptureEmergencyCleanup']
    .map(name=>extract(text,name,'function',false)).filter(Boolean);
  anchors.push(ipcRegion(text,'gyro.calibrate'),ipcRegion(text,'gyro.calibrate.status'));
  const uiBytes=fs.readFileSync(path.join(repo,'src/views/GyroMotionView.vue')),ui=uiBytes.toString('utf8');
  const uiEntries=[uiRegion(ui,'startCalibration',/^async function startCalibration\(/gm),
    uiRegion(ui,'applyCalibStatus',/^function applyCalibStatus\(/gm),
    uiRegion(ui,'gyro.calibrate.result',/^\s*offCalibResult = on\('gyro\.calibrate\.result',/gm,true),
    uiRegion(ui,'gyro.calibrate.status',/^\s*offCalibStatus = on\('gyro\.calibrate\.status',/gm,true)];
  const result=uiEntries.find(e=>e.symbol==='gyro.calibrate.result');
  const expression=/const saved = ([^;\r\n]+);/.exec(result.code);
  const savedExpression=expression?.[1]||null; // UI ownership is external; capture only, do not gate native tests.
  fs.writeFileSync(path.join(out,'main.cpp.snapshot'),mainBytes);fs.writeFileSync(path.join(out,'GyroMotionView.vue.snapshot'),uiBytes);
  for(const [name,items] of [['actual-functions',entries],['source-anchors',anchors],['ui-anchors',uiEntries]]){
    fs.mkdirSync(path.join(out,name));for(const e of items)fs.writeFileSync(path.join(out,name,e.symbol+'.txt'),e.code);
  }
  const functions=entries.filter(e=>e.kind==='function');
  // Bodies are untouched. This is an extraction artifact, not a runnable stand-in.
  fs.writeFileSync(path.join(out,'production_functions.generated.h'),functions.map(e=>e.code).join('\n\n')+'\n');
  fs.writeFileSync(path.join(out,'production_types.generated.h'),identity.code+'\n');
  fs.writeFileSync(path.join(out,'production_constants.generated.h'),constants.map(e=>e.code).join('\n')+'\n');
  const discoveredCalibrationHeaders=[...text.matchAll(/^#include\s+"([^"]*calibration[^"]*\.h)"/gm)].map(m=>m[1]);
  const binding={source:'native/main.cpp',sourceSha256:sha(mainBytes),sourceBytes:mainBytes.length,
    requiredFunctions,missingFunctions,availableMethods:functions.length,
    functionsAndTypes:entries.map(({code,...e})=>e),constants:constants.map(({code,...e})=>e),
    anchors:anchors.map(({code,...e})=>e),uiSource:'src/views/GyroMotionView.vue',uiSourceSha256:sha(uiBytes),
    uiAnchors:uiEntries.map(({code,...e})=>e),discoveredCalibrationHeaders,
    executionBoundary:'Preparation only: extracted main bodies have not been compiled or executed. UI and complete IPC/lifecycle callers are source-only. No UI timing or helper execution is performed here.'};
  fs.writeFileSync(path.join(out,'main-source-bindings.json'),JSON.stringify(binding,null,2)+'\n');
  fs.writeFileSync(path.join(out,'ui-saved-expression.js.txt'),(savedExpression||'UI now delegates result admission to an independently tested helper; source-only snapshot.')+'\n');
  return {binding,entries,constants,anchors,uiEntries,savedExpression};
}
export function verify(repo,before){
  const main=fs.readFileSync(path.join(repo,'native/main.cpp')),text=main.toString('utf8');
  const ui=fs.readFileSync(path.join(repo,'src/views/GyroMotionView.vue'));
  return {fullSourceChanged:sha(main)!==before.binding.sourceSha256,sourceSha256After:sha(main),
    selectedMethodsStable:before.entries.every(e=>extract(text,e.symbol,e.kind).sha256===e.sha256),
    constantsStable:before.constants.every(e=>text.includes(e.code)),
    anchorsStable:before.anchors.every(e=>e.kind==='ipc-source-only'?ipcRegion(text,e.symbol).sha256===e.sha256:
      extract(text,e.symbol).sha256===e.sha256),uiSourceStable:sha(ui)===before.binding.uiSourceSha256};
}
