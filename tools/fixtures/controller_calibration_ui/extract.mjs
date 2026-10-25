import fs from 'node:fs';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import vm from 'node:vm';
import {createRequire} from 'node:module';
const require=createRequire(import.meta.url),ts=require('typescript'),vue=require('vue');
export const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
export function extract(bytes){
  const text=bytes.toString('utf8'),open='<script setup lang="ts">',start=text.indexOf(open)+open.length,end=text.indexOf('</script>',start);
  assert(start>=open.length&&end>start);const script=text.slice(start,end);
  const ast=ts.createSourceFile('GyroMotionView.ts',script,ts.ScriptTarget.Latest,true,ts.ScriptKind.TS);
  assert.equal(ast.parseDiagnostics.length,0);
  const first=script.indexOf('const calibrating = ref(false);'),last=script.indexOf('const stickOptions = [',first);assert(first>=0&&last>first);
  const entries=[{name:'actual-calibration-state-and-start',start:first,end:last}];
  for(const name of ['offCalibResult','offCalibStatus']){
    const matches=[];const walk=n=>{if(ts.isExpressionStatement(n)&&n.getText(ast).startsWith(name+' = on('))matches.push(n);ts.forEachChild(n,walk);};walk(ast);assert.equal(matches.length,1,name);
    const n=matches[0];entries.push({name:'actual-'+name,start:n.getStart(ast),end:n.end});
  }
  const queries=[];const walk=n=>{if(ts.isExpressionStatement(n)&&/^void invoke/.test(n.getText(ast))&&n.getText(ast).includes("('gyro.calibrate.status', {})"))queries.push(n);ts.forEachChild(n,walk);};walk(ast);assert.equal(queries.length,1,'initial status query');
  entries.push({name:'actual-initial-status-query',start:queries[0].getStart(ast),end:queries[0].end});
  const unmounts=[];const findUnmount=n=>{if(ts.isExpressionStatement(n)&&n.getText(ast).startsWith('onBeforeUnmount('))unmounts.push(n);ts.forEachChild(n,findUnmount);};findUnmount(ast);assert.equal(unmounts.length,1);entries.push({name:'actual-unmount',start:unmounts[0].getStart(ast),end:unmounts[0].end});
  const code=entries.map(e=>{e.code=script.slice(e.start,e.end);e.sha256=sha(e.code);e.startUtf8Byte=Buffer.byteLength(text.slice(0,start)+script.slice(0,e.start));e.endUtf8ByteExclusive=e.startUtf8Byte+Buffer.byteLength(e.code);return e.code;}).join('\n');
  const exported='\nglobalThis.__calibration = {startCalibration, state: () => ({calibrating: calibrating.value, result: calibResult.value, error: calibError.value, status: calibUiStatus.value}), dispose: () => globalThis.__beforeUnmount()};\n';
  const compiled=ts.transpileModule(code+exported,{fileName:'actual-calibration.ts',compilerOptions:{target:ts.ScriptTarget.ES2020,module:ts.ModuleKind.None,strict:true},reportDiagnostics:true});
  assert(!compiled.diagnostics?.some(d=>d.category===ts.DiagnosticCategory.Error));
  return {code,javascript:compiled.outputText,entries:entries.map(({code,...e})=>e),sourceSha256:sha(bytes),typescriptVersion:ts.version,vueVersion:vue.version};
}
export function runtime(binding){
  const handlers=new Map(),timers=new Map(),requests=[],statusQueries=[],diagnostics=[],logs=[];let nextTimer=1;
  const deferred=()=>{let resolve,reject;const promise=new Promise((a,b)=>{resolve=a;reject=b;});return {promise,resolve,reject};};
  const scope={onBeforeUnmount:callback=>{scope.__beforeUnmount=callback;},gyroPresentation:{dispose:()=>{}},stopGyroVisibility:null,stopOverride:null,saveTimer:null,gyroConnectedTimer:null,offShortcutRuntime:null,telemetry:()=>{},onGyroPreviewPointerDown:()=>{},onGyroPreviewPointerUp:()=>{},ref:vue.ref,computed:vue.computed,gameLocked:vue.ref(false),console:Object.fromEntries(['log','warn','error'].map(k=>[k,(...v)=>logs.push({level:k,values:v})])),
    window:{removeEventListener:()=>{},setTimeout:(callback,delay)=>{const id=nextTimer++;timers.set(id,{callback,delay});return id;},clearTimeout:id=>{timers.delete(id);}},
    on:(name,callback)=>{handlers.set(name,callback);return ()=>handlers.delete(name);},
    invoke:(name,payload)=>{if(name==='gyro.calibrate'){const d=deferred();requests.push(d);return d.promise;}if(name==='gyro.calibrate.status'){const d=deferred();statusQueries.push(d);return d.promise;}diagnostics.push({name,payload});return Promise.resolve({ok:true});}};
  vm.createContext(scope);vm.runInContext(binding.javascript,scope,{filename:'actual-calibration.generated.js',timeout:1000});
  const api=scope.__calibration;
  return {start:()=>api.startCalibration(),state:()=>JSON.parse(JSON.stringify(api.state())),emitResult:d=>handlers.get('gyro.calibrate.result')?.(d),emitStatus:d=>handlers.get('gyro.calibrate.status')?.(d),requests,statusQueries,timers,diagnostics,logs,lock:v=>{scope.gameLocked.value=v;},dispose:()=>api.dispose(),fireTimer:id=>{const t=timers.get(id);timers.delete(id);t?.callback();}};
}
export const controllerAck=(id=11)=>({ok:true,provider:'controller',requestId:id,pollWindowMs:5000});
export const controllerResult=(id=11,saved=true)=>({ok:saved,saved,provider:'controller',requestId:id,source:'manual',confidence:saved?0.995:0.5,weight:saved?9:0,offset:{x:id===11?1.25:2.5,y:-2,z:0}});
export async function reproduce(binding){
  const r=runtime(binding),pending=r.start();r.emitResult(controllerResult());const beforeAck=r.state();r.requests[0].resolve(controllerAck());await pending;const afterAck=r.state();
  assert.equal(beforeAck.calibrating,false);assert(beforeAck.result);assert.equal(afterAck.calibrating,true);assert(afterAck.result);assert.equal(r.timers.size,1);
  const old=runtime(binding),first=old.start();old.requests[0].resolve(controllerAck(11));await first;old.emitResult(controllerResult(11));const second=old.start();old.requests[1].resolve(controllerAck(12));await second;const active=old.state();old.emitResult(controllerResult(11));assert.equal(active.calibrating,true);assert.equal(old.state().calibrating,false);
  const status=runtime(binding),p=status.start();status.requests[0].resolve(controllerAck(12));await p;status.emitStatus({provider:'controller',status:'already-calibrated',trusted:true});assert.equal(status.state().calibrating,false);
  return {reproduced:true,earlyResultThenAck:{beforeAck,afterAck,timers:[...r.timers.values()].map(t=>t.delay)},oldResultDuringNewRequest:{before:active,after:old.state()},noIdTerminalDuringActive:{after:status.state()}};
}
