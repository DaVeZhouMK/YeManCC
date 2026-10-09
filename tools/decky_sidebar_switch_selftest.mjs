import { task } from './fixtures/decky_task_paths.mjs';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import { createRequire } from 'node:module';
const root=path.resolve(import.meta.dirname,'..');
const require=createRequire(path.join(root,'package.json'));const {transformSync}=require('esbuild');
const source=fs.readFileSync(path.join(root,'src/views/SteamView.vue'),'utf8');
const start=source.indexOf('function acceptDeckyState(');const end=source.indexOf('onMounted(() => {',start);
const fragment=source.slice(start,end)+'\nexport {acceptDeckyState,setDeckyEnabled};';
function fixture(){let resolve,reject,calls=0;const pending=new Promise((yes,no)=>{resolve=yes;reject=no;});const notices=[];
const module={exports:{}};const context=vm.createContext({module,exports:module.exports,deckyState:{value:{enabled:false,revision:10}},deckyBusy:{value:false},deckyViewEpoch:1,deckySidebar:{setEnabled:()=>{calls++;return pending;}},showNotice:m=>notices.push(m)});
vm.runInContext(transformSync(fragment,{loader:'ts',format:'cjs'}).code,context);return {api:module.exports,context,resolve,reject,notices,count:()=>calls};}
const cases=[];
async function check(name,fn){await fn();cases.push({name,status:'passed'});}
await check('older native state ignored',()=>{const f=fixture();f.api.acceptDeckyState({enabled:true,revision:9});assert.equal(f.context.deckyState.value.enabled,false);});
await check('newer native state accepted',()=>{const f=fixture();f.api.acceptDeckyState({enabled:true,revision:11});assert.equal(f.context.deckyState.value.enabled,true);});
await check('pending setting blocks duplicate invokes',async()=>{const f=fixture();const p=f.api.setDeckyEnabled(true);await f.api.setDeckyEnabled(false);assert.equal(f.count(),1);assert.equal(f.context.deckyState.value.enabled,false);f.resolve({enabled:true,revision:11});await p;assert.equal(f.context.deckyState.value.enabled,true);assert.equal(f.context.deckyBusy.value,false);});
await check('save failure keeps old preference and gives error',async()=>{const f=fixture();const p=f.api.setDeckyEnabled(true);f.reject(new Error('mock save failed'));await p;assert.equal(f.context.deckyState.value.enabled,false);assert.match(f.notices[0],/设置失败/);assert.equal(f.context.deckyBusy.value,false);});
await check('late reply after unmount does not update new view',async()=>{const f=fixture();const p=f.api.setDeckyEnabled(true);f.context.deckyViewEpoch++;f.resolve({enabled:true,revision:11});await p;assert.equal(f.context.deckyState.value.enabled,false);});
await check('late failure after unmount does not create notification timer',async()=>{const f=fixture();const p=f.api.setDeckyEnabled(true);f.context.deckyViewEpoch++;f.reject(new Error('late'));await p;assert.equal(f.notices.length,0);});
fs.writeFileSync(path.resolve(task,'validation/STEAM-SIDEBAR-SWITCH-SELFTEST.json'),JSON.stringify({project:'YMCC Decky 侧边栏',cases:cases.length,results:cases,realPreferenceWrites:false},null,2));console.log(`Sidebar switch tests passed: ${cases.length}`);
