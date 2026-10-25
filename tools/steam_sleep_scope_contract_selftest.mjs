import assert from 'node:assert/strict';
import fs from 'node:fs';
import crypto from 'node:crypto';
import vm from 'node:vm';
// Offline test: only the pinned Valve method is evaluated. No Steam process,
// controller, network, sleep, or game is touched. Pass the verified source index.
const profiles = [
  {
    "channel": "stable",
    "version": "1788652215",
    "bundleSha256": "f9606b111203c9140dcdd6fc016a0ee00e4c8406aeb786187c42873f08d071fb",
    "methodLength": 998,
    "methodFnv1a32": 2265414143,
    "packageSha256": "791e7069a239ffb07366193b17ed93fe9c3b6e131fd766c505e3a7029e9c5f57"
  },
  {
    "channel": "publicbeta",
    "version": "1791856936",
    "bundleSha256": "e2123c10531bed1855dbaa4366e688d3acb32319c9a9695a70c64d62e24e7db7",
    "methodLength": 1038,
    "methodFnv1a32": 4095097876,
    "packageSha256": "012f3d0a5799278e0e404e568f20df1f0d37d50556f4562f46fc4f8dcea3bf8a"
  }
];
const arg = name => process.argv[process.argv.indexOf(name) + 1];
assert(process.argv.includes('--source-index'), 'pass --source-index pointing to the manifest-verified Valve source index');
const index = JSON.parse(fs.readFileSync(arg('--source-index'), 'utf8'));
const expectBetaRejected = process.argv.includes('--expect-beta-rejected');
const source = fs.readFileSync('native/steam_live.js', 'utf8');
const sha256 = value => crypto.createHash('sha256').update(value).digest('hex');
const fnv = text => { let value = 2166136261; for (let i=0;i<text.length;i++) value=Math.imul(value^text.charCodeAt(i),16777619)>>>0;return value; };
function extractMethod(text) {
  const marker='ScopeRunningApps(){',start=text.indexOf(marker);
  assert(start>=0 && text.indexOf(marker,start+1)<0,'unique Valve scope method required');
  let depth=0,quote=null,escape=false;
  for(let i=start+marker.length-1;i<text.length;i++) {
    const c=text[i];
    if(quote){if(escape)escape=false;else if(c==='\\')escape=true;else if(c===quote)quote=null;continue;}
    if(c==='"'||c==="'"||c==='`'){quote=c;continue;}
    if(c==='{')depth++;else if(c==='}'&&--depth===0)return text.slice(start,i+1);
  }
  throw Error('unclosed Valve method');
}
const rows=[];
function fixture(profile,method,options={}) {
  const {rawRunning=[],drift=false,tamper=false}=options,local=('local' in options)?options.local:4;
  let account=42;const stats={calls:0,retired:[]};
  const target=Object.freeze({appid:-1895035161,display_name:'original shortcut',local_per_client_data:Object.freeze({display_status:local}),selected_per_client_data:Object.freeze({display_status:4})});
  const stopped=Object.freeze({appid:945360,local_per_client_data:Object.freeze({display_status:11})});
  const apps=[target,stopped];
  const enumRoot={Ul:{Fi:4,Vv:1}},desktop={VE:413080},client={s5U:769},appStore={tw:{allApps:apps}};
  const platform={rf:()=>false},log={Debug(){},IsDebugEnabled:()=>false};
  const mobx={h5(fn){stats.calls++;if(drift)account=43;fn();},HO:value=>value};
  const globals=profile.channel==='stable'?{ze:enumRoot,Ve:desktop,m:client,J:appStore,y:platform,wt:log,n:mobx}:{ce:enumRoot,F:desktop,l:client,ke:appStore,I:platform,Di:log,e:mobx};
  const actual=tamper?method.replace('ScopeRunningApps - App ','ScopeRunningApps ? App '):method;
  assert(!tamper||actual!==method);
  const scope=vm.runInNewContext('({'+actual+'})',globals,{timeout:1000});
  Object.assign(scope,{m_runningAppIDs:[...rawRunning],m_remoteClientStreams:[],m_WindowStore:{BHasAppWindow:()=>true,OnAppNoLongerRunning:id=>stats.retired.push(id)},WindowStore:{GetAppWindowIDs:()=>[]}});
  assert.equal(Function.prototype.toString.call(scope.ScopeRunningApps),actual);
  const window={settingsStore:{m_CMInterface:{steamid:{GetAccountID:()=>account}}},appStore:{m_mapApps:new Map(apps.map(o=>[o.appid,o]))},SteamUIStore:scope};
  const fn=vm.runInNewContext(source,{window,SteamClient:{},performance,setTimeout,clearTimeout,console},{timeout:1000});
  return {fn,scope,stats,target,request:{operation:'sleep.reconcile',account:42,appids:[2399932135]}};
}
for(const profile of profiles) {
  const item=index.find(row=>row.channel===profile.channel&&row.version===profile.version);
  assert(item?.verifiedAgainstOfficialManifest&&item.sha256===profile.packageSha256,'official package provenance mismatch');
  const file=item.sources.find(row=>row.symbols?.ScopeRunningApps);
  const raw=fs.readFileSync(file.path);assert.equal(sha256(raw),profile.bundleSha256,'pinned Valve source changed');
  const method=extractMethod(raw.toString('utf8'));
  assert.equal(method.length,profile.methodLength);assert.equal(fnv(method),profile.methodFnv1a32);
  for(const local of [4,1]) {
    const f=fixture(profile,method,{local,rawRunning:[945360]});const r=await f.fn(f.request);
    if(expectBetaRejected&&profile.channel==='publicbeta') {
      assert(!r.ok&&!r.mutated);assert.equal(r.reason,'steam-rescope-schema-changed');assert.equal(f.stats.calls,0);
      rows.push({channel:profile.channel,case:'real-running-'+local,result:'baseline-beta-rejected',reason:r.reason});continue;
    }
    assert(r.ok&&r.mutated&&r.runtimeAccepted&&!r.healthVerified);assert.equal(f.stats.calls,1);
    assert.deepEqual([...f.scope.m_runningAppIDs],[-1895035161]);assert.equal(f.target.local_per_client_data.display_status,local);
    assert.equal(r.gameInput,'unknown');if(!expectBetaRejected)assert.equal(r.rescopeContract.profile,'valve-win64-'+profile.version);
    const again=await f.fn(f.request);assert(again.ok&&!again.mutated);assert.equal(f.stats.calls,1);
    rows.push({channel:profile.channel,case:'real-running-'+local,result:'one-native-rescope-then-noop',contract:r.rescopeContract});
  }
  for(const local of [11,0,null,undefined]) {
    const f=fixture(profile,method,{local});const r=await f.fn(f.request);
    assert(!r.ok&&!r.mutated);assert.equal(r.reason,'steam-backend-not-running');assert.equal(f.stats.calls,0);assert.equal(f.scope.m_runningAppIDs.length,0);
    rows.push({channel:profile.channel,case:'non-running-'+String(local),result:'rejected-with-zero-writes'});
  }
  {
    const f=fixture(profile,method,{tamper:true});const r=await f.fn(f.request);
    assert(!r.ok&&!r.mutated);assert.equal(r.reason,'steam-rescope-schema-changed');assert.equal(f.stats.calls,0);
    rows.push({channel:profile.channel,case:'same-length-unapproved-body',result:'rejected-with-zero-writes'});
  }
  if(!expectBetaRejected) {
    const f=fixture(profile,method,{drift:true});const r=await f.fn(f.request);
    assert(!r.ok&&r.mutated);assert.equal(r.reason,'steam-account-changed');assert.equal(f.stats.calls,1);
    rows.push({channel:profile.channel,case:'account-changed-during-native-call',result:'not-accepted'});
  }
}
const receipt={mode:expectBetaRejected?'before':'after',cases:rows.length,actualValveMethods:true,productionScript:true,osAndSteamTransportMocked:true,gameInputVerified:false,continueButtonVerified:false,rows};
if(process.argv.includes('--output'))fs.writeFileSync(arg('--output'),JSON.stringify(receipt,null,2)+'\n');
console.log(JSON.stringify(receipt,null,2));
