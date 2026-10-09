// Actual TdpView configuration handlers with Vue refs and memory-only services.
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
const root=process.cwd(),require=createRequire(path.join(root,'package.json'));
const view=fs.readFileSync(path.join(root,'src/views/TdpView.vue'),'utf8');
const a=view.indexOf('const bootApplyTdp = ref(true);'),b=view.indexOf('// ── 统一刷新入口',a);
assert(a>=0&&b>a);
assert.match(view,/v-model="bootApplyTdp"\s+:disabled="!autoApplyLoaded \|\| autoApplySaving"/);
assert.match(view,/v-model="wakeApplyTdp"\s+:disabled="!autoApplyLoaded \|\| autoApplySaving"/);
const {transform}=require('esbuild');
const compiled=(await transform(`const {ref}=require('vue');const errMsg=ref('');const readTdpAutoApply=()=>fx.read();const writeTdpAutoApply=p=>fx.write(p);\n${view.slice(a,b)}\nmodule.exports={bootApplyTdp,wakeApplyTdp,autoApplyLoaded,autoApplySaving,durableAutoApply,errMsg,loadAutoApply,onBootApplyTdp,onWakeApplyTdp};`,{loader:'ts',format:'cjs'})).code;
const cases=[];
function fixture(){const fx={read:async()=>({boot:true,wake:false}),write:async()=>{}};const module={exports:{}};vm.runInNewContext(compiled,{require,fx,module});return {...module.exports,fx};}
async function check(name,fn){try{await fn();cases.push({name,ok:true});}catch(e){cases.push({name,ok:false,error:e.message});}}
await check('reading failure disables edits and shows error',async()=>{const f=fixture();f.fx.read=async()=>{throw Error('read denied')};await f.loadAutoApply();assert.equal(f.autoApplyLoaded.value,false);assert.match(f.errMsg.value,/read denied/);let saves=0;f.fx.write=async()=>{saves++};await f.onBootApplyTdp(false);assert.equal(saves,0);});
await check('boot toggle owns only boot',async()=>{const f=fixture();await f.loadAutoApply();const patches=[];f.fx.write=async p=>patches.push(p);await f.onBootApplyTdp(false);assert.equal(JSON.stringify(patches),JSON.stringify([{boot:false}]));assert.equal(f.wakeApplyTdp.value,false);});
await check('wake toggle owns only wake',async()=>{const f=fixture();await f.loadAutoApply();const patches=[];f.fx.write=async p=>patches.push(p);await f.onWakeApplyTdp(true);assert.equal(JSON.stringify(patches),JSON.stringify([{wake:true}]));assert.equal(f.bootApplyTdp.value,true);});
await check('failed save restores durable view and reports failure',async()=>{const f=fixture();await f.loadAutoApply();f.fx.write=async()=>{throw Error('save denied')};f.bootApplyTdp.value=false;await f.onBootApplyTdp(false);assert.equal(f.bootApplyTdp.value,true);assert.equal(f.autoApplySaving.value,false);assert.match(f.errMsg.value,/save denied/);});
await check('failed save plus failed read does not permit default-enabled edits',async()=>{const f=fixture();await f.loadAutoApply();f.fx.write=async()=>{throw Error('save denied')};f.fx.read=async()=>{throw Error('read denied')};await f.onWakeApplyTdp(true);assert.equal(f.wakeApplyTdp.value,false);assert.equal(f.autoApplyLoaded.value,false);});
await check('late old load cannot undo a newer successful save',async()=>{const f=fixture();await f.loadAutoApply();let release;f.fx.read=()=>new Promise(r=>release=r);const old=f.loadAutoApply();await f.onBootApplyTdp(false);release({boot:true,wake:true});await old;assert.equal(f.bootApplyTdp.value,false);assert.equal(f.wakeApplyTdp.value,false);});
await check('reload during save cannot display an uncommitted old value',async()=>{const f=fixture();await f.loadAutoApply();let release;f.fx.write=()=>new Promise(r=>release=r);const pending=f.onBootApplyTdp(false);await f.loadAutoApply();assert.equal(f.bootApplyTdp.value,false);release();await pending;assert.equal(f.bootApplyTdp.value,false);});
await check('failed save can be retried without poisoning the control',async()=>{const f=fixture();await f.loadAutoApply();f.fx.write=async()=>{throw Error('save denied')};await f.onBootApplyTdp(false);f.fx.write=async()=>{};await f.onBootApplyTdp(false);assert.equal(f.bootApplyTdp.value,false);assert.equal(f.durableAutoApply.boot,false);});
const report={passed:cases.filter(c=>c.ok).length,failed:cases.filter(c=>!c.ok).length,cases,realHardwareOperations:false};console.log(JSON.stringify(report,null,2));const out=process.env.YMCC_GLOBAL_CONFIG_ARTIFACT_ROOT?path.join(process.env.YMCC_GLOBAL_CONFIG_ARTIFACT_ROOT,'tdp-auto-ui-results.json'):path.resolve(root,'../../../_scratch/global-cache-config-audit-20261006/tdp-auto-ui-results.json');fs.writeFileSync(out,JSON.stringify(report,null,2));if(report.failed)process.exitCode=1;
