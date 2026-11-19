// Execute the real frontend bridge, with only IPC replaced. Test notification dedup and teardown.
import fs from 'node:fs';import path from 'node:path';import vm from 'node:vm';import assert from 'node:assert/strict';import {createRequire} from 'node:module';
const root=path.resolve(import.meta.dirname,'..'),req=createRequire(path.join(root,'package.json')),{transformSync,buildSync}=req('esbuild'),listeners=new Set(),calls=[],cases=[];
const option=name=>{
 const indices=process.argv.flatMap((value,index)=>value===name?[index]:[]);
 assert(indices.length<=1,'Duplicate '+name);
 if(!indices.length)return null;
 const value=process.argv[indices[0]+1];
 assert(typeof value==='string'&&value.trim()&&!value.startsWith('--'),'Missing or empty '+name);
 return value;
};
const bridgeOption=option('--bridge-source'),selected=option('--case');
const bridgeSource=bridgeOption===null?path.join(root,'src/bridge/deckySidebar.ts'):path.resolve(bridgeOption);
const module={exports:{}};vm.runInNewContext(transformSync(fs.readFileSync(bridgeSource,'utf8'),{loader:'ts',format:'cjs'}).code,{module,exports:module.exports,Error,structuredClone,require:name=>{assert.equal(name,'./ipc');return {invoke:(...v)=>{calls.push(v);return Promise.resolve(true);},on:(event,fn)=>{assert.equal(event,'deckySidebar.updated');listeners.add(fn);return()=>listeners.delete(fn);}};}});
const preferences={exports:{}};vm.runInNewContext(buildSync({entryPoints:[path.join(root,'src/bridge/bootModeGlobalPreferences.ts')],bundle:true,write:false,platform:'browser',format:'cjs'}).outputFiles[0].text,{module:preferences,exports:preferences.exports,structuredClone,TextEncoder,TextDecoder});
const registered=[];
const api=module.exports,emit=s=>{for(const f of listeners)f(s);},check=(name,fn)=>registered.push({name,fn});let stop;
check('notification observer starts without checking versions, launching or changing settings',()=>{stop=api.startDeckySafetyNotifications();assert.equal(calls.length,0);assert.equal(listeners.size,1);});
check('network/plugin/unknown reasons cannot show global safety closure',()=>{for(const reason of ['HTTP-429','plugin-error','live-debug-unavailable',undefined])emit({enabled:false,reason});assert.equal(calls.length,0);});
check('actual fatal disabled state sends existing notification with reason and YMCC upgrade action',()=>{emit({enabled:false,reason:'decky-render-failed'});assert.equal(calls.length,1);assert.equal(calls[0][0],'notification.show');assert.match(calls[0][1].body,/致命渲染错误.*已自动关闭.*更新 YMCC/);});
check('same disabled fault repeated by backend is displayed once',()=>{emit({enabled:false,reason:'decky-render-failed'});assert.equal(calls.length,1);});
check('disabled state description retains reason when opening settings later',()=>assert.match(api.deckySidebarDescription({enabled:false,reason:'decky-render-failed'}),/已自动关闭/));
check('explicit enable renews notification ownership for a later new fault',()=>{emit({enabled:true});emit({enabled:false,reason:'decky-render-failed'});assert.equal(calls.length,2);});
check('startup error reports possible Steam incompatibility without claiming proof',()=>assert.match(api.deckySidebarSafetyReason('decky-frontend-incompatible'),/可能与当前 Steam 不兼容/));
check('repeated process crashes advise inspecting plugins as well as updating YMCC',()=>assert.match(api.deckySidebarSafetyReason('loader-crash-loop'),/最近安装的插件.*更新 YMCC/));
check('explicit off without fault keeps normal opt-in description',()=>assert.match(api.deckySidebarDescription({enabled:false}),/^开启后/));
check('App shutdown removes observer and no later notification can be emitted',()=>{stop();assert.equal(listeners.size,0);const n=calls.length;emit({enabled:false,reason:'loader-crash-loop'});assert.equal(calls.length,n);});
check('ordinary and startup bridge writes retain provenance and legacy ordinary arguments',()=>{
 const start=calls.length;
 api.deckySidebar.setEnabled(false);api.deckySidebar.setEnabled(true,{origin:'boot-mode'});
 const wake={exports:{}};
 vm.runInNewContext(transformSync(fs.readFileSync(path.join(root,'src/bridge/api.ts'),'utf8'),{loader:'ts',format:'cjs'}).code,{module:wake,exports:wake.exports,Error,require:name=>{if(name==='./bootModeGlobalPreferences')return preferences.exports;assert.equal(name,'./ipc');return {invoke:(...v)=>{calls.push(v);return Promise.resolve(true);}};}});
 wake.exports.wakePassword.set(true);wake.exports.wakePassword.set(false,{origin:'boot-mode'});
 const recovery={exports:{}};
 vm.runInNewContext(transformSync(fs.readFileSync(path.join(root,'src/bridge/sleepRecovery.ts'),'utf8'),{loader:'ts',format:'cjs'}).code,{module:recovery,exports:recovery.exports,Error,require:name=>{if(name==='./bootModeGlobalPreferences')return preferences.exports;assert.equal(name,'./ipc');return {isNativeRuntime:true,invoke:(...v)=>{calls.push(v);return Promise.resolve(true);}};}});
 recovery.exports.setAutomaticSleepRecovery(true);recovery.exports.setAutomaticSleepRecovery(false,{origin:'boot-mode'});
 assert.deepEqual(JSON.parse(JSON.stringify(calls.slice(start).map(([method,args])=>({method,args})))),[
  {method:'deckySidebar.setEnabled',args:{enabled:false}},
  {method:'deckySidebar.setEnabled',args:{enabled:true,origin:'boot-mode'}},
  {method:'power.wakePassword.set',args:{enabled:true}},
  {method:'power.wakePassword.set',args:{enabled:false,origin:'boot-mode'}},
  {method:'sleepRecovery.autoSet',args:{enabled:true}},
  {method:'sleepRecovery.autoSet',args:{enabled:false,origin:'boot-mode'}},
 ]);
 const context={input:{revision:8,outputTarget:{persona:'elite'}},extensions:{bootModes:{schema:1,revision:3,current:'steam'}}};
 const original=structuredClone(context),options={origin:'boot-mode',bootContext:context};
 api.deckySidebar.setEnabled(true,options);context.input.revision=99;context.extensions.bootModes.current='global';options.origin='ordinary';
 assert.deepEqual(JSON.parse(JSON.stringify(calls.at(-1)[1])),{enabled:true,origin:'boot-mode',bootContext:original},'actual Decky bridge must carry an immutable boot owner to native');
});
const chosen=selected===null?registered:registered.filter(row=>row.name===selected);
assert(chosen.length>0,'Unknown or empty exact Decky safety selection');
assert(new Set(registered.map(row=>row.name)).size===registered.length,'Duplicate Decky safety case');
for(const {name,fn} of chosen){fn();cases.push(name);}
const out=path.resolve(process.env.YMCC_DECKY_ARTIFACT_ROOT||'work/decky-health');fs.mkdirSync(out,{recursive:true});fs.writeFileSync(path.join(out,'SAFETY-NOTIFICATION.json'),JSON.stringify({passed:true,cases:cases.length,results:cases,nativeSettingsWrites:0,realSteam:false},null,2));console.log('Decky safety notification tests passed: '+cases.length);
