import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
const arg=name=>{const n=process.argv.indexOf(name);return n<0?null:process.argv[n+1]};
const sourcePath=arg('--source')??'native/steam_live.js';
const source=fs.readFileSync(sourcePath,'utf8'),vulnerable=process.argv.includes('--expect-vulnerable');
const appid=2399932135,gameid='10307630032478011392';
function fixture(change=null,{afterSend=false}={}) {
  const state={lock:false,keyboard:false,menu:0,suppressed:false,overlay:false,configurator:false,focus:413080,overlayAppid:0,account:42,connected:true};
  const stats={raises:0,samples:0,changes:0,configReads:0};
  const editor={EditingConfigurationAppId:0,EditedConfiguration:null,PreviewedConfiguration:null,IsUpdatingEditingConfiguration:false,m_nEditNumber:0,m_nLastSavedEditNumber:0};
  const row={appid,app_type:0x40000000,gameid,local_per_client_data:{display_status:4}};
  const active={MenuStore:{GetOpenSideMenu:()=>state.menu,get m_cSuppressRequests(){return state.suppressed?1:0}},IsAnyVRWindow:()=>false,IsGamepadUIOverlayWindow:()=>state.overlay,IsDesktopOverlayWindow:()=>false,IsControllerConfiguratorWindow:()=>state.configurator};
  const ui={m_runningAppIDs:[appid],WindowStore:{GetAppWindowIDs:()=>[],BHasStandaloneKeyboard:()=>state.keyboard},GetFocusedWindowInstance:()=>active,GetShowingLockScreen:()=>state.lock};
  const apply=()=>{if(!change)return;stats.changes++;if(change in state)state[change]=typeof state[change]==='boolean'?true:1;
    if(change==='focus')state.focus=945360;if(change==='overlayAppid')state.overlayAppid=appid;if(change==='connected')state.connected=false;
    if(change==='editor')editor.m_nEditNumber=1;if(change==='local-status'){row.local_per_client_data.display_status=11;ui.m_runningAppIDs=[];}
    if(change==='gameid')row.gameid='2399932135';if(change==='account')state.account=43;
  };
  const window={SteamUIStore:ui,appStore:{m_mapApps:new Map([[appid,row]])},settingsStore:{m_CMInterface:{steamid:{GetAccountID:()=>state.account}},GetClientSetting:()=>[true]},
    FocusedAppWindowStore:{GetFocusedAppID:()=>state.focus,GetFocusedOverlayAppID:()=>state.overlayAppid,GetFocusedOverlayPID:()=>0,GetFocusedWindowID:()=>null},controllerConfiguratorStore:editor,
    ControllerStore:{GetControllers(){stats.samples++;if(stats.samples===2&&!afterSend)queueMicrotask(apply);return state.connected?[{nControllerIndex:15,eControllerType:2}]:[];}}};
  const SteamClient={Apps:{RaiseWindowForGame(id){assert.equal(id,gameid);stats.raises++;if(afterSend)apply();return Promise.resolve();}},Input:{GetConfigForAppAndController(){stats.configReads++;return Promise.resolve({URL:'empty.vdf'});}}};
  const fn=vm.runInNewContext(source,{window,SteamClient,performance,setTimeout,clearTimeout,console});
  const request={operation:'sleep.input-resume',account:42,appids:[appid],originalWindowReturn:true,originalTarget:{basis:'current-association',appid,pid:11960,created:'134364217869743774'}};
  return {fn,request,stats,state,apply};
}
const results=[];
for(const change of ['lock','keyboard','menu','suppressed','overlay','configurator','focus','overlayAppid','connected','editor','local-status']) {
  const f=fixture(change),result=await f.fn(f.request);assert.equal(f.stats.changes,1);
  assert.equal(f.stats.raises,vulnerable?1:0,change+' changed after async probe; final dispatch must reject');
  assert.equal(result.mutated,vulnerable);assert.equal(result.ok,false);
  results.push({case:change,raises:f.stats.raises,mutated:result.mutated,reason:result.reason,passed:true});
}
for(const change of ['account','gameid']) {
  const f=fixture(change),result=await f.fn(f.request);assert.equal(f.stats.raises,0);assert.equal(result.mutated,false);assert(!result.ok);
  results.push({case:change,raises:f.stats.raises,reason:result.reason,passed:true});
}
const benign=fixture();const clean=await benign.fn(benign.request);assert.equal(benign.stats.raises,1);assert(clean.mutated&&clean.actionAcknowledged&&clean.verificationPending);assert.equal(clean.gameInput,'not-tested');assert.equal(clean.healthVerified,false);
results.push({case:'unchanged-original-instance',raises:1,actualInputVerified:false,passed:true});
for(const change of ['lock','focus','connected']) {
  const f=fixture(change,{afterSend:true}),result=await f.fn(f.request);assert.equal(f.stats.raises,1);assert(result.mutated);assert(!result.ok);assert.equal(f.stats.changes,1);
  results.push({case:'after-send-'+change,raises:1,mutated:result.mutated,replayed:false,passed:true});
}
const report={sourcePath,expectVulnerable:vulnerable,cases:results,caseCount:results.length,usesProductionScript:true,fieldAcceptance:false,originalTargetRetargeted:false};
if(arg('--output'))fs.writeFileSync(arg('--output'),JSON.stringify(report,null,2)+'\n');
console.log(JSON.stringify(report,null,2));
