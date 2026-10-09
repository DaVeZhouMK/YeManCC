import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
const source = fs.readFileSync('native/steam_live.js', 'utf8');
const results = [];
function fixture(options = {}) {
  const stats = { edits: 0, saves: 0, overlayWrites: 0, loads: 0 };
  const account = options.account ?? 42, index = options.controllerIndex ?? 4, type = options.controllerNumericType ?? 2;
  const layoutPath = options.path ?? 'S:\\Steam\\42\\controller_neptune.vdf';
  const group = {mode:7,modeid:options.modeid ?? 22,mode_shift:false,settings:[{key:30,int_value:145},{key:99,int_value:77}]};
  const cfg = {controller_type:type,url:'autosave://'+layoutPath,sets:[{key:'Default',source_bindings:[{key:12,active_group:group}]}]};
  const s = {m_nEditNumber:0,m_nLastSavedEditNumber:0,IsUpdatingEditingConfiguration:false,PreviewedConfiguration:null,EditedConfiguration:cfg,
    EnsureEditingConfiguration(app,index) { stats.loads++;this.EditingConfigurationAppId=app;this.StableAppId=app;this.EditingConfigurationControllerIndex=index;this.m_updatingEditingConfigurationPromise=Promise.resolve(); },
    SetControllerSourceMode(app,msg) { stats.edits++; assert.equal(app,413080);assert.equal(msg.source_binding_key,12);assert.equal(msg.new_setting.key,30);assert.equal(msg.modeid,group.modeid);group.settings[0].int_value=options.clamp?10:msg.new_setting.int_value;this.m_nEditNumber++;if(options.concurrent)this.m_nEditNumber++;this.m_updatingEditingConfigurationPromise=options.timeout?new Promise(()=>{}):Promise.resolve(); },
    SaveEditingConfiguration(app,publish,callback) { stats.saves++;assert.equal(app,413080);assert.equal(publish,false);this.m_nLastSavedEditNumber=this.m_nEditNumber;this.EditingConfigurationAppId=-1;callback(); }
  };
  const settings = { clientSettings:{enable_overlay:false,overlay_fps_counter_corner:0,overlay_fps_counter_detail_level:1,overlay_fps_counter_scale_factor:1,overlay_fps_counter_saturation_factor:1,overlay_fps_counter_bgopacity:1},m_CMInterface:{steamid:{GetAccountID:()=>account}},GetClientSetting(key) {assert(key in this.clientSettings);return [this.clientSettings[key],async value=>{stats.overlayWrites++;this.clientSettings[key]=value;}];} };
  const window = {controllerConfiguratorStore:s,ControllerStore:{GetControllers:()=>('controllers' in options)?options.controllers:[{eControllerType:type,nControllerIndex:index}],GetControllerTypeString:value=>value===type?'controller_neptune':'controller_generic'},settingsStore:settings};
  const fn=vm.runInNewContext(source,{window,SteamClient:{Settings:{SetSetting(){}}},performance,setTimeout:(cb,n)=>setTimeout(cb,Math.min(n,20)),clearTimeout,console});
  const request = {operation:'mouse.set',controllerType:'controller_neptune',path:layoutPath,account,baseline:145,percent:137};
  return {fn,request,stats,s,cfg,group,window,settings};
}
async function test(name,body) {await body();results.push(name);}
await test('actual embedded script is in sync',async()=>{const header=fs.readFileSync('native/steam_live_script.h','utf8');assert(header.includes(source.trimEnd()));});
await test('native edits/save use exact Steam desktop source/setting and preserve unrelated values',async()=>{const f=fixture(),r=await f.fn(f.request);assert(r.ok);assert(r.saved);assert.equal(r.percent,137);assert.equal(r.controllerIndex,4);assert.equal(f.stats.edits,1);assert.equal(f.stats.saves,1);assert.equal(f.group.settings[1].int_value,77);assert(!f.window.__ymccSteamLiveBusy);});
await test('read and same-value write do not modify or save',async()=>{const f=fixture();assert((await f.fn({...f.request,operation:'mouse.get'})).ok);assert((await f.fn({...f.request,percent:145})).ok);assert.equal(f.stats.edits,0);assert.equal(f.stats.saves,0);});
await test('current 323 is readable, never automatically clamped',async()=>{const f=fixture();f.group.settings[0].int_value=323;const r=await f.fn({...f.request,operation:'mouse.get'});assert.equal(r.percent,323);assert.equal(f.stats.edits,0);});
for (const percent of [1,100,300]) await test(`UI write boundary ${percent} goes through native and readback`,async()=>{const f=fixture();assert((await f.fn({...f.request,percent})).ok);assert.equal(f.stats.saves,1);});
for (const percent of [0,301,1.5,4294967297]) await test(`invalid request ${percent} never mutates`,async()=>{const f=fixture();const r=await f.fn({...f.request,percent});assert.equal(r.reason,'invalid-sensitivity');assert(!r.mutated);assert.equal(f.stats.edits,0);});
await test('dirty/preview/game editor conflicts never load or mutate',async()=>{for(const kind of ['dirty','preview','game']){const f=fixture();if(kind==='dirty')f.s.m_nEditNumber=1;if(kind==='preview')f.s.PreviewedConfiguration={};if(kind==='game')f.s.EditingConfigurationAppId=123;const r=await f.fn(f.request);assert.equal(r.reason,'live-editor-busy');assert.equal(f.stats.loads,0);assert.equal(f.stats.saves,0);}});
await test('account drift never mutates another account',async()=>{const f=fixture(),r=await f.fn({...f.request,account:43});assert.equal(r.reason,'steam-account-changed');assert.equal(f.stats.edits,0);});
await test('ambiguous/wrong devices never guessed',async()=>{for(const controllers of [[],[{eControllerType:30,nControllerIndex:0}],[{eControllerType:2,nControllerIndex:0},{eControllerType:2,nControllerIndex:1}]]){const f=fixture({controllers}),r=await f.fn(f.request);assert(!r.ok);assert.equal(f.stats.edits,0);}});
await test('wrong layout, mode, mode shift, duplicate source/setting refused',async()=>{for(const kind of ['url','mode','shift','source','setting']){const f=fixture();if(kind==='url')f.cfg.url+='wrong';if(kind==='mode')f.group.mode=4;if(kind==='shift')f.group.mode_shift=true;if(kind==='source')f.cfg.sets[0].source_bindings.push({...f.cfg.sets[0].source_bindings[0]});if(kind==='setting')f.group.settings.push({...f.group.settings[0]});assert(!(await f.fn(f.request)).ok);assert.equal(f.stats.edits,0);}});
await test('changed runtime baseline wins over stale caller',async()=>{const f=fixture(),r=await f.fn({...f.request,baseline:119});assert.equal(r.reason,'desktop-sensitivity-changed');assert.equal(f.stats.edits,0);});
await test('Steam clamping/rejecting a value is never saved/reported as success',async()=>{const f=fixture({clamp:true}),r=await f.fn({...f.request,percent:1});assert.equal(r.reason,'live-value-not-accepted');assert(r.mutated);assert.equal(f.stats.saves,0);});
await test('concurrent edit is not saved on behalf of the other UI',async()=>{const f=fixture({concurrent:true}),r=await f.fn(f.request);assert.equal(r.reason,'live-editor-changed');assert.equal(f.stats.saves,0);});
await test('timeout is bounded, marked uncertain, lock released',async()=>{const f=fixture({timeout:true}),r=await f.fn(f.request);assert.equal(r.reason,'live-timeout');assert(r.mutated);assert(!f.window.__ymccSteamLiveBusy);});
await test('same-context concurrency is rejected',async()=>{const f=fixture();f.window.__ymccSteamLiveBusy='other';assert.equal((await f.fn(f.request)).reason,'live-busy');assert.equal(f.stats.edits,0);});
await test('overlay read/set/noop uses Steam settings wrapper and native callback value',async()=>{const f=fixture();assert.equal((await f.fn({operation:'overlay.get',account:42})).value,0);assert((await f.fn({operation:'overlay.set',account:42,enabled:true})).ok);assert.equal(f.stats.overlayWrites,1);assert((await f.fn({operation:'overlay.set',account:42,enabled:true})).ok);assert.equal(f.stats.overlayWrites,1);assert.equal(f.stats.edits,0);});

await test('uninitialized Steam controller list never falls back to a generic device',async()=>{const f=fixture({controllers:null});const r=await f.fn(f.request);assert.equal(r.reason,'live-controller-not-connected');assert.equal(f.stats.edits,0);});
await test('machine-specific paths/account/controller index/type/modeid never hardcoded',async()=>{
  for(const [index,type,account,path,modeid] of [
    [0,2,17,'C:\\Program Files (x86)\\Steam\\steamapps\\common\\Steam Controller Configs\\17\\config\\413080\\controller_neptune.vdf',25],
    [3,44,1234567890,'E:\\SteamLibrary\\steamapps\\common\\Steam Controller Configs\\1234567890\\config\\413080\\controller_neptune.vdf',901],
    [8,69,7654321,'F:\\便携Steam\\steamapps\\common\\Steam Controller Configs\\7654321\\config\\413080\\controller_neptune.vdf',6]]) {
    const f=fixture({controllerIndex:index,controllerNumericType:type,account,path,modeid});const r=await f.fn(f.request);assert(r.ok);assert.equal(r.controllerIndex,index);assert.equal(f.stats.edits,1);assert.equal(f.stats.saves,1);
  }
});
await test('SteamDeck request never changes generic controller even when its percentage matches',async()=>{
  const f=fixture({controllers:[{eControllerType:30,nControllerIndex:0}]});const r=await f.fn(f.request);assert.equal(r.reason,'live-controller-not-connected');assert.equal(f.stats.edits,0);assert.equal(f.stats.saves,0);
});
await test('unified client batch applies monitor and overlay together through Steam wrappers',async()=>{
  const f=fixture();const values={enable_overlay:true,overlay_fps_counter_corner:5,overlay_fps_counter_detail_level:4,overlay_fps_counter_scale_factor:1.4,overlay_fps_counter_saturation_factor:0,overlay_fps_counter_bgopacity:.7};
  const result=await f.fn({operation:'settings.set',account:42,values});assert(result.ok);assert.deepEqual(JSON.parse(JSON.stringify(result.values)),values);assert.equal(f.stats.overlayWrites,6);assert.equal(f.stats.edits,0);
  const read=await f.fn({operation:'settings.get',account:42,keys:Object.keys(values)});assert(read.ok);assert.deepEqual(JSON.parse(JSON.stringify(read.values)),values);
});
await test('unified settings preflights every field before mutating any field',async()=>{
  for(const invalid of [{bad_key:1},{overlay_fps_counter_corner:7},{overlay_fps_counter_scale_factor:1.5},{overlay_fps_counter_bgopacity:-.1},{overlay_fps_counter_saturation_factor:.35},{enable_overlay:1},{overlay_fps_counter_detail_level:0}]){
    const f=fixture();const result=await f.fn({operation:'settings.set',account:42,values:{enable_overlay:true,...invalid}});assert(!result.ok);assert.equal(f.stats.overlayWrites,0);assert(!result.mutated);
  }
});
await test('unified settings read/noop/account drift never mutate Steam',async()=>{
  const f=fixture();assert((await f.fn({operation:'settings.get',account:42,keys:['overlay_fps_counter_corner']})).ok);
  assert((await f.fn({operation:'settings.set',account:42,values:{enable_overlay:false}})).ok);assert.equal(f.stats.overlayWrites,0);
  assert.equal((await f.fn({operation:'settings.set',account:43,values:{enable_overlay:true}})).reason,'steam-account-changed');assert.equal(f.stats.overlayWrites,0);
});
console.log(JSON.stringify({suite:'Steam live production JS mocked',passed:results.length,cases:results,hardwareOperations:0},null,2));
