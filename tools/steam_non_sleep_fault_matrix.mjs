// Offline fault injection into VM fixtures only; never connects to live Steam.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import crypto from 'node:crypto';
const source=fs.readFileSync('native/steam_live.js','utf8');
const scopeSource=fs.readFileSync('tools/fixtures/steam_scope_running_apps_valve_20261011.txt','utf8');
const appid=2399932135,gameid='10307630032478011392';
const original=Object.freeze({pid:45256,created:'134363500000000000',path:String.raw`E:\GAME\TMNT.exe`,session:1});
function fixture(){
 const overview={appid,gameid,app_type:0x40000000,local_per_client_data:{display_status:4}};
 let scopeCalls=0;
 const scope=vm.runInNewContext('({'+scopeSource+'})',{
  ze:{Ul:{Fi:4,Vv:1}},Ve:{VE:413080},m:{s5U:769},J:{tw:{allApps:[overview]}},
  y:{rf:()=>false},wt:{Debug(){},IsDebugEnabled:()=>false},n:{h5(fn){scopeCalls++;fn()},HO:v=>v}
 });
 Object.assign(scope,{m_runningAppIDs:[appid],m_remoteClientStreams:[],
  m_WindowStore:{BHasAppWindow:()=>false,OnAppNoLongerRunning(){},GetAppWindowIDs:()=>[]}});
 scope.WindowStore=scope.m_WindowStore;
 const window={SteamUIStore:scope,appStore:{m_mapApps:new Map([[appid,overview]])},
  settingsStore:{m_CMInterface:{steamid:{GetAccountID:()=>42}},clientSettings:{enable_overlay:true}},
  ControllerStore:{GetControllers:()=>[]}};
 const fn=vm.runInNewContext(source,{window,SteamClient:{},performance,setTimeout,clearTimeout,console});
 const request={operation:'sleep.reconcile',account:42,appids:[appid],contactRecovery:true,
  contactProof:{connected:true,appid,pid:original.pid,created:original.created},
  sleepSessionProcess:{pid:30460,created:'134363400000000000',root:String.raw`S:\Steam`}};
 return {overview,scope,fn,request,get calls(){return scopeCalls;}};
}
function state(f){return {localDisplayStatus:f.overview.local_per_client_data.display_status,
 runningList:[...f.scope.m_runningAppIDs],mockOriginal:{...original},scopeCalls:f.calls};}
const cases=[];
for(const fault of ['membership-only','local-display-not-running','contradictory-membership','record-proof-lost','wrong-shortcut-gameid']){
 const f=fixture(),trace=[{stage:'healthy',state:state(f)}];
 if(fault==='contradictory-membership')f.overview.local_per_client_data.display_status=11;
 else f.scope.m_runningAppIDs=[];
 if(fault==='local-display-not-running'||fault==='record-proof-lost')f.overview.local_per_client_data.display_status=11;
 if(fault==='record-proof-lost')f.request.contactProof.connected=false;
 if(fault==='wrong-shortcut-gameid')f.overview.gameid='10307630032478011393';
 trace.push({stage:'fault-injected-by-fixture',state:state(f)});
 const result=await f.fn(f.request);
 trace.push({stage:'production-operation-return',state:state(f),ok:result.ok,mutated:result.mutated,
 reason:result.reason,accepted:result.contactRecovery?.accepted??false});
 if(fault==='membership-only'){
  assert(result.ok&&result.mutated&&result.contactRecovery.accepted);assert.equal(f.calls,1);
  assert.deepEqual([...f.scope.m_runningAppIDs],[appid]);
 }else{
  assert(!result.ok&&!result.contactRecovery?.accepted);
  const attempted=fault==='local-display-not-running';assert.equal(f.calls,attempted?1:0);
  assert.equal(result.mutated,attempted);
  assert.equal(result.reason,attempted?'steam-contact-rescope-not-confirmed':fault==='contradictory-membership'?'steam-contact-rescope-not-confirmed':'steam-contact-proof-rejected');
 }
 // Undo only this fixture's injected data. This is NOT a production recovery.
 f.overview.local_per_client_data.display_status=4;f.overview.gameid=gameid;f.request.contactProof.connected=true;f.scope.m_runningAppIDs=[appid];
 const restored=await f.fn(f.request);
 assert(restored.ok&&!restored.mutated&&restored.contactRecovery.accepted);
 trace.push({stage:'fixture-reset-control-not-a-repair',state:state(f),ok:restored.ok,mutated:restored.mutated});
 assert(trace.every(row=>JSON.stringify(row.state.mockOriginal)===JSON.stringify(original)));
 cases.push({fault,trace});
}
const output={schema:'steam-non-sleep-fault-matrix.v1',syntheticEvidenceOnly:true,
 hypothesis:'separate membership-only loss from local-display state loss, proof loss and shortcut identity mismatch',
 caveat:'No real Steam/process/backend was changed. UI store display_status and record proof are independent fixture inputs; contactProof is not a live native record observation. Fixture reset does not prove same-instance recovery.',
 sourceSha256:crypto.createHash('sha256').update(source).digest('hex'),scopeFixtureSha256:crypto.createHash('sha256').update(scopeSource).digest('hex'),
 cases,passed:cases.length,powerOperations:0,applicationOperations:0,liveDebugOperations:0,deviceOperations:0};
const outputIndex=process.argv.indexOf('--output');
if(outputIndex>=0){assert(process.argv[outputIndex+1]);const file=path.resolve(process.argv[outputIndex+1]);fs.mkdirSync(path.dirname(file),{recursive:true});fs.writeFileSync(file,JSON.stringify(output,null,2)+'\n');}
console.log(JSON.stringify(output,null,2));
