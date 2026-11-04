// Exercise the actual yeman.ts reset and missing-scheme recovery through in-memory WebView IPC.
// Native, powercfg, registry and filesystem commands are never executed.
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {readFileSync,writeFileSync,mkdirSync} from 'node:fs';
import {dirname,resolve} from 'node:path';
import {fileURLToPath} from 'node:url';
import {spawnSync} from 'node:child_process';
import {createHash} from 'node:crypto';
const repo=resolve(dirname(fileURLToPath(import.meta.url)),'..');
const out=resolve(process.argv[2]||resolve(repo,'../../Build/Validation/PowerSchemeRebuild'));
mkdirSync(out,{recursive:true});
const {build,stop}=createRequire(resolve(repo,'package.json'))('esbuild');
const entry=String.raw`
import assert from 'node:assert/strict';
import {writeFileSync} from 'node:fs';
const YM='1cb8b882-a900-4b9f-9bac-99d151e64441',BAL='381b4222-f694-41f0-9685-ff5bb260df2e';
const A='9e3d8a66-1513-451a-b489-f99dbdd37198',B='dfa153a8-6515-46c4-bb03-54a97e0fb8c0';
const NONE='fea3413e-7e05-4911-9a71-700331f1c294',PERSONALITY='245d8541-3943-4422-b025-13a784f679b7';
const listeners=[];let fixture=null;const tests=[];const NO_REPLY=Symbol('no IPC response');
const realSetTimeout=globalThis.setTimeout,realClearTimeout=globalThis.clearTimeout,ownedTimers=new Set();
globalThis.setTimeout=(fn,ms,...args)=>{
 fixture?.timerMs?.push(ms);
 const timer=realSetTimeout((...values)=>{ownedTimers.delete(timer);fn(...values)},fixture?.fastTimeouts?Math.min(ms,10):ms,...args);
 ownedTimers.add(timer);return timer;
};
globalThis.clearTimeout=timer=>{ownedTimers.delete(timer);realClearTimeout(timer)};
const ok=(stdout='')=>({exitCode:0,stdout,stderr:''});
const bad=(stderr='fixture failure')=>({exitCode:1,stdout:'',stderr});
function dispatch(cmd,args){
 if(!fixture)throw Error('IPC before fixture was ready: '+cmd);
 fixture.ipc.push({cmd,args});
 if(cmd==='power.activeScheme')return fixture.active;
 if(cmd==='power.schemeExists'){
  assert.deepEqual(Object.keys(args),['scheme']);assert.equal(typeof args.scheme,'string');
  if(fixture.schemeExistsError!==undefined)throw Error(fixture.schemeExistsError);
  if(fixture.schemeExistsHang)return NO_REPLY;
  if(Object.hasOwn(fixture,'schemeExistsReceipt'))return fixture.schemeExistsReceipt;
  if(!fixture.nativeSchemeExists)throw Error('unknown command: power.schemeExists');
  const guid=args.scheme.toLowerCase()==='scheme_current'?fixture.active:args.scheme.replace(/^\{|\}$/g,'').toLowerCase();
  const receipt={ok:true,exists:fixture.plans.has(guid)&&fixture.queryMissing!==guid,code:0};
  fixture.schemeChecks.push({scheme:args.scheme,...receipt});return receipt;
 }
 if(cmd==='power.setActiveScheme'){
  if(!fixture.nativeSetActiveScheme)throw Error('unknown command: power.setActiveScheme');
  assert.deepEqual(Object.keys(args),['scheme']);const guid=args.scheme;
  if(fixture.activationFailure===guid||fixture.activationFailure?.includes?.(guid))return {ok:false,code:5};
  if(!fixture.plans.has(guid))return {ok:false,code:87};
  if(fixture.activationIgnored!==guid&&!fixture.activationIgnored?.includes?.(guid))fixture.active=guid;
  if(fixture.personalityAfterActivation)fixture.personality=[...fixture.personalityAfterActivation];
  return {ok:true,code:0};
 }
 if(cmd==='log.write'){
  assert.equal(args.level,'info');assert(args.message.startsWith('[power.personality] '));
  fixture.logs.push(JSON.parse(args.message.slice('[power.personality] '.length)));
  if(fixture.logFailure)throw Error('log unavailable');
  return fixture.logHang?NO_REPLY:true;
 }
 if(cmd==='registry.read'){
  assert.equal(args.root,'HKLM');
  assert.equal(args.path,['SYSTEM','CurrentControlSet','Control','Power','User','PowerSchemes',YM,PERSONALITY].join('\\'));
  assert(fixture.plans.has(YM),'Personality is checked only after YMCC exists');
  assert(['ACSettingIndex','DCSettingIndex'].includes(args.name));
  const side=args.name==='ACSettingIndex'?'AC':'DC';
  if(fixture.personalityReadFailure===true||fixture.personalityReadFailure===side)throw Error('registry read denied');
  if(fixture.personalityReadHang===side)return NO_REPLY;
  return fixture.personality[args.name==='ACSettingIndex'?0:1];
 }
 if(cmd==='fs.exists')return /YM\.pow$/i.test(args.path)?fixture.hasPow:/yeman-settings\.json$/i.test(args.path);
 if(cmd==='settings.location')return {directory:fixture.directory,file:fixture.directory+'\\yeman-settings.json',backup:fixture.directory+'\\yeman-settings.json.bak'};
 if(cmd==='settings.read')return {stamp:String(fixture.revision),unchanged:false,content:JSON.stringify(fixture.settings)};
 if(cmd==='settings.write'){fixture.settings=JSON.parse(args.content);fixture.revision++;return true;}
 if(cmd==='fs.readTextFile')return /yeman-settings\.json$/i.test(args.path)?JSON.stringify(fixture.settings):'';
 if(cmd==='shell.run'){
  assert.equal(args.program,'powercfg','Only in-memory powercfg is allowed');
  const [verb,guid]=args.args;fixture.calls.push(args.args);
  if(verb==='/query')return fixture.plans.has(guid) && fixture.queryMissing!==guid?ok():bad('specified power scheme does not exist');
  if(verb==='/setacvalueindex'||verb==='/setdcvalueindex'){
   const side=verb==='/setacvalueindex'?'AC':'DC';
   assert.deepEqual(args.args,[verb,YM,NONE,PERSONALITY,'1'],'Only the requested personality may be changed');
   assert(fixture.plans.has(YM));
   assert.equal(args.timeoutMs,3000,'Personality write uses a bounded native timeout');
   if(fixture.personalityWriteThrow===side)throw Error('personality IPC unavailable');
   if(fixture.personalityWriteHang===side)return NO_REPLY;
   if(fixture.personalityWriteFailure===side)return bad('personality write denied');
   if(fixture.personalityWriteIgnored!==side)fixture.personality[side==='AC'?0:1]=1;
   return ok();
  }
  if(verb==='/list')return fixture.listFailure?bad('list denied'):ok(fixture.listText??[...fixture.plans].map(g=>'Power Scheme GUID: '+g+' (Balanced)').join('\n'));
  if(verb==='/setactive'){
   if(fixture.activationFailure===guid || fixture.activationFailure?.includes?.(guid))return bad('access denied');
   if(!fixture.plans.has(guid))return bad('invalid parameters');
   if(fixture.activationIgnored!==guid && !fixture.activationIgnored?.includes?.(guid))fixture.active=guid;
   if(fixture.personalityAfterActivation)fixture.personality=[...fixture.personalityAfterActivation];
   return ok();
  }
  if(verb==='/delete'){
   assert.equal(guid,YM,'External schemes must never be deleted');assert.notEqual(fixture.active,YM,'Do not delete active YMCC');
   if(fixture.deleteFailure)return bad('delete denied');if(!fixture.deleteIgnored)fixture.plans.delete(YM);return ok();
  }
  if(verb==='/import'){
   assert.equal(args.args[2],YM);assert.notEqual(fixture.active,YM);
   if(fixture.importFailure)return bad('import denied');if(!fixture.importIgnored)fixture.plans.add(YM);return ok();
  }
  throw Error('Unexpected powercfg mutation: '+verb);
 }
 throw Error('Unhandled memory-only IPC: '+cmd);
}
globalThis.window={chrome:{webview:{addEventListener:(_,fn)=>listeners.push(fn),postMessage:r=>queueMicrotask(()=>{
 try{const result=dispatch(r.cmd,r.args||{});if(result===NO_REPLY)return;for(const fn of listeners)fn({data:{id:r.id,result}})}
 catch(error){for(const fn of listeners)fn({data:{id:r.id,error:String(error.message)}})}
})}},addEventListener(){},removeEventListener(){},dispatchEvent(){return true},setTimeout,clearTimeout};
globalThis.document={visibilityState:'visible',addEventListener(){},removeEventListener(){},querySelector(){return null},createElement(){return {content:{}}}};
globalThis.CustomEvent=class{constructor(type,opts){this.type=type;this.detail=opts?.detail}};
const yeman=await import('./src/bridge/yeman.ts');
function resetFixture(options={}){
 fixture={logs:[],personality:[1,1],active:YM,plans:new Set([A,B,YM]),hasPow:true,settings:{schemaVersion:1},revision:1,calls:[],ipc:[],timerMs:[],schemeChecks:[],directory:'C:\\YMCC-MEMORY-'+tests.length,...options};
 return fixture;
}
async function run(name,options={},expectedError=null,action='rebuildYemanScheme'){
 resetFixture(options);
 const beforeExternal=[...fixture.plans].filter(g=>g!==YM).sort();
 const beforeYeman=fixture.plans.has(YM),beforeActive=fixture.active;
 const beforePersonality=[...fixture.personality];
 yeman.setPowerControlDir(fixture.directory);
 let error=null;
 try{await yeman[action](action==='switchScheme'?'yeman':action==='rebuildYemanScheme'?(options.mode??'yeman'):undefined)}catch(e){error=String(e.message)}
 if(expectedError)assert.match(error||'',expectedError,name);else assert.equal(error,null,name);
 assert.deepEqual([...fixture.plans].filter(g=>g!==YM).sort(),beforeExternal,name+' external plans preserved');
 assert.equal(fixture.calls.some(c=>c[0]==='/restoredefaultschemes'||c[0]==='/duplicatescheme'),false,name+' no global restore/create');
 const deletes=fixture.calls.filter(c=>c[0]==='/delete');
 const imports=fixture.calls.filter(c=>c[0]==='/import');
 assert.ok(imports.every(c=>c[2]===YM),name+' fixed YMCC GUID only');
 if(action!=='rebuildYemanScheme')assert.equal(deletes.length,0,name+' recovery never deletes');
 if(!expectedError){
  assert(fixture.plans.has(YM),name+' YMCC exists');
  const manual=action==='reconcileRememberedPowerScheme' && options.settings?.power?.scheme?.mode==='manual';
  if(!manual)assert.deepEqual(fixture.personality,options.expectedPersonality??[1,1],name+' expected best-effort outcome');
  if(action==='ensureYemanScheme'){
   assert.equal(fixture.active,beforeActive,name+' ensuring existence preserves active plan');
   assert.equal(imports.length,beforeYeman?0:1,name+' import only when missing');
   assert.equal(fixture.calls.some(c=>c[0]==='/list'),false,name+' recovery does not need a transition');
   assert.equal(fixture.calls.some(c=>c[0]==='/setactive'),false,name+' personality repair never adds activation');
  }else if(manual){
   assert.equal(fixture.active,beforeActive);assert.deepEqual(fixture.personality,beforePersonality);assert.equal(fixture.calls.length,0);
  }else{
   assert.equal(fixture.active,YM);assert.equal(fixture.settings.power.scheme.guid,YM);
   if(action==='rebuildYemanScheme')assert.equal(deletes.length,beforeYeman?1:0);
  }
 }
 tests.push({name,action,pass:true,error,calls:fixture.calls,logs:fixture.logs,ipc:fixture.ipc,schemeChecks:fixture.schemeChecks});return fixture;
}
const personalityWrites=f=>f.calls.filter(c=>c[0]==='/setacvalueindex'||c[0]==='/setdcvalueindex');
const personalityReads=f=>f.ipc.filter(i=>i.cmd==='registry.read');
let p=await run('Personality already high-performance is read-only',{},null,'ensureYemanScheme');
assert.deepEqual(p.calls,[['/query',YM]]);assert.equal(personalityReads(p).length,2);assert.equal(p.logs.length,0);
p=await run('Repair AC once with no readback or activation',{personality:[2,1]},null,'ensureYemanScheme');
assert.deepEqual(personalityWrites(p),[['/setacvalueindex',YM,NONE,PERSONALITY,'1']]);assert.equal(personalityReads(p).length,2);
assert.equal(p.logs.length,1);assert.equal(p.logs[0].commandSucceeded,true);assert.equal(p.logs[0].side,'AC');
const priorCalls=p.calls.length;await yeman.ensureYemanScheme();assert.equal(p.calls.length,priorCalls+1,'Repeated check does not write matching values');
p.personality[1]=2;await yeman.ensureYemanScheme();assert.equal(personalityWrites(p).at(-1)[0],'/setdcvalueindex');
p=await run('Repair DC only',{personality:[1,0]},null,'ensureYemanScheme');assert.deepEqual(personalityWrites(p),[['/setdcvalueindex',YM,NONE,PERSONALITY,'1']]);
p=await run('Repair both sides with one initial read per side',{personality:[2,2]},null,'ensureYemanScheme');
assert.equal(personalityWrites(p).length,2);assert.equal(personalityReads(p).length,2);assert.equal(p.calls[0][0],'/query');
assert(p.logs.every(l=>l.commandSucceeded&&l.exitCode===0));
p=await run('Missing values get one write each',{personality:[null,null]},null,'ensureYemanScheme');assert.equal(personalityWrites(p).length,2);
p=await run('Inactive YMCC repair preserves the active plan',{active:B,personality:[2,0]},null,'ensureYemanScheme');assert.equal(p.calls.some(c=>c[0]==='/setactive'),false);
p=await run('Imported plan is repaired after it exists',{plans:new Set([A,B]),active:A,personality:[2,2]},null,'ensureYemanScheme');
assert(p.calls.findIndex(c=>c[0]==='/import')<p.calls.findIndex(c=>c[0]==='/setacvalueindex'));
p=await run('AC failure is logged while DC still proceeds',{personality:[2,2],personalityWriteFailure:'AC',expectedPersonality:[2,1]},null,'ensureYemanScheme');
assert.equal(personalityWrites(p).length,2);assert(p.logs.some(l=>l.side==='AC'&&!l.commandSucceeded&&l.exitCode===1));assert(p.logs.some(l=>l.side==='DC'&&l.commandSucceeded));
p=await run('DC failure does not escape the repair',{personality:[1,2],personalityWriteFailure:'DC',expectedPersonality:[1,2]},null,'ensureYemanScheme');assert.equal(p.logs[0].commandSucceeded,false);
p=await run('Exit-zero receipt is logged without readback or retry',{personality:[2,1],personalityWriteIgnored:'AC',expectedPersonality:[2,1]},null,'ensureYemanScheme');
assert.equal(personalityWrites(p).length,1);assert.equal(personalityReads(p).length,2);assert.equal(p.logs[0].commandSucceeded,true);
p=await run('One read failure is logged and does not suppress the other side',{personality:[2,2],personalityReadFailure:'AC',expectedPersonality:[2,1]},null,'ensureYemanScheme');
assert.equal(personalityWrites(p).length,1);assert(p.logs.some(l=>l.stage==='read'&&l.side==='AC'&&!l.commandSucceeded));
p=await run('Both read failures return normally',{personality:[2,2],personalityReadFailure:true,expectedPersonality:[2,2]},null,'ensureYemanScheme');assert.equal(personalityWrites(p).length,0);assert.equal(p.logs.length,2);
p=await run('IPC write exception is logged while the other side continues',{personality:[2,2],personalityWriteThrow:'AC',expectedPersonality:[2,1]},null,'ensureYemanScheme');assert.equal(personalityWrites(p).length,2);
p=await run('Unanswered registry IPC times out without blocking recovery',{personality:[2,2],personalityReadHang:'AC',fastTimeouts:true,expectedPersonality:[2,1]},null,'ensureYemanScheme');
assert(p.logs.some(l=>l.side==='AC'&&l.stage==='read'&&/timed out/.test(l.detail)));assert.equal(personalityWrites(p).length,1);
p=await run('Unanswered write IPC times out without retries',{personality:[2,2],personalityWriteHang:'AC',fastTimeouts:true,expectedPersonality:[2,1]},null,'ensureYemanScheme');
assert(p.logs.some(l=>l.side==='AC'&&l.stage==='write'&&/timed out/.test(l.detail)));assert.equal(personalityWrites(p).length,2);
p=await run('Logging failure cannot fail recovery',{personality:[2,2],logFailure:true},null,'ensureYemanScheme');assert.equal(personalityReads(p).length,2);
p=await run('Unanswered log delivery does not delay repair',{personality:[2,2],logHang:true,fastTimeouts:true},null,'ensureYemanScheme');assert.equal(p.logs.length,2);
p=await run('Personality repair does not attempt an extra activation',{personality:[2,2],activationFailure:YM},null,'ensureYemanScheme');assert.equal(p.calls.some(c=>c[0]==='/setactive'),false);
p=await run('Automatic recovery continues after a personality failure',{personality:[2,1],personalityWriteFailure:'AC',expectedPersonality:[2,1]},null,'ensureRememberedYemanSchemeActive');assert.equal(personalityWrites(p).length,1);
p=await run('Existing automatic guard repairs personality',{personality:[2,2]},null,'ensureRememberedYemanSchemeActive');assert.equal(personalityWrites(p).length,2);
p=await run('Startup and power-event reconciliation repairs personality',{personality:[2,2]},null,'reconcileRememberedPowerScheme');assert.equal(personalityWrites(p).length,2);
p=await run('Switching to YMCC only uses the original activation',{active:B,personality:[2,2]},null,'switchScheme');assert.deepEqual(p.calls.filter(c=>c[0]==='/setactive'),[['/setactive',YM]]);
p=await run('Manual Windows scheme remains owned by the user',{active:B,personality:[2,2],settings:{schemaVersion:1,power:{scheme:{version:2,guid:YM,name:'YeMan',mode:'manual',selectedGuid:B,remembered:true}}}},null,'reconcileRememberedPowerScheme');assert.equal(personalityWrites(p).length,0);
let f=await run('Reported machine: two Balanced names with custom GUIDs');
assert.deepEqual(f.calls.filter(c=>c[0]==='/setactive'),[['/setactive',A],['/setactive',YM]]);
assert.equal(f.calls.some(c=>c[0]==='/setactive'&&c[1]===BAL),false);
f=await run('Localized list and uppercase GUIDs', {listText:'电源方案 GUID: '+A.toUpperCase()+' (平衡)\n电源方案 GUID: '+B+' (平衡)\n电源方案 GUID: '+YM+' (野蛮系统优化电源V20) *'});
assert.equal(f.calls.find(c=>c[0]==='/setactive')[1],A);
f=await run('Existing standard Balanced may serve as transition', {plans:new Set([YM,BAL,A])});assert.equal(f.calls.find(c=>c[0]==='/setactive')[1],BAL);
f=await run('Already active custom plan avoids an extra switch', {active:B});assert.deepEqual(f.calls.filter(c=>c[0]==='/setactive'),[['/setactive',YM]]);
f=await run('Stale list candidate is rechecked', {plans:new Set([B,YM]),listText:'GUID: '+A+'\nGUID: '+B+'\nGUID: '+YM});assert.equal(f.calls.find(c=>c[0]==='/setactive')[1],B);
f=await run('Overlay GUIDs cannot be a rebuild fallback', {plans:new Set([YM,'961cc777-2547-4f9d-8174-7d86181b8a7a',B])});assert.equal(f.calls.find(c=>c[0]==='/setactive')[1],B);
const ANY='01234567-89ab-cdef-0123-456789abcdef';
f=await run('Any existing scheme works without standard or reported GUIDs',{plans:new Set([YM,ANY])});assert.equal(f.calls.find(c=>c[0]==='/setactive')[1],ANY);
f=await run('First activation denied retries the next existing scheme',{activationFailure:A});assert.deepEqual(f.calls.filter(c=>c[0]==='/setactive'),[['/setactive',A],['/setactive',B],['/setactive',YM]]);
f=await run('Wrong first activation readback retries the next scheme',{activationIgnored:A});assert.equal(f.calls.filter(c=>c[0]==='/setactive')[1][1],B);
f=await run('Missing YM.pow preserves plans before any switch', {hasPow:false},/YM\.pow/);assert.equal(f.calls.length,0);assert(f.plans.has(YM));
f=await run('Only YMCC exists: stop before deletion', {plans:new Set([YM])},/临时电源方案/);assert.equal(f.calls.some(c=>c[0]==='/delete'),false);assert(f.plans.has(YM));
f=await run('List failure stops before deletion', {listFailure:true},/list denied/);assert(f.plans.has(YM));
f=await run('All candidates denied stop before deletion', {activationFailure:[A,B]},/access denied/);assert(f.plans.has(YM));assert.equal(f.calls.some(c=>c[0]==='/delete'),false);
f=await run('All candidates have wrong activation readback', {activationIgnored:[A,B]},/切换未生效/);assert(f.plans.has(YM));assert.equal(f.calls.some(c=>c[0]==='/delete'),false);
f=await run('Deletion failure is visible and keeps original YMCC', {deleteFailure:true},/delete denied/);assert(f.plans.has(YM));
f=await run('Successful deletion command without deletion is rejected',{deleteIgnored:true},/旧方案仍然存在/);assert(f.plans.has(YM));assert.equal(f.calls.some(c=>c[0]==='/import'),false);
f=await run('Import failure keeps the existing fallback usable', {importFailure:true},/import denied/);assert.equal(f.active,A);assert(f.plans.has(A));
f=await run('Import success without scheme readback is rejected',{importIgnored:true},/导入后仍未/);assert.equal(f.active,A);assert(!f.plans.has(YM));
f=await run('Activation of rebuilt YMCC failure is visible', {activationFailure:YM},/access denied/);assert.equal(f.active,A);assert(f.plans.has(YM));
f=await run('Rebuilt YMCC final readback prevents false success',{activationIgnored:YM},/激活未生效/);assert.equal(f.active,A);assert.equal(f.settings.power,undefined);
f=await run('Reset with already missing YMCC skips deletion and transition',{plans:new Set([A,B]),active:A});assert.equal(f.calls.some(c=>c[0]==='/delete'||c[0]==='/list'),false);assert.deepEqual(f.calls.filter(c=>c[0]==='/setactive'),[['/setactive',YM]]);
f=await run('Missing recovery imports only fixed YMCC',{plans:new Set([A,B]),active:A},null,'ensureYemanScheme');
f=await run('Existing YMCC recovery is a no-op even without YM.pow',{hasPow:false,active:B},null,'ensureYemanScheme');assert.deepEqual(f.calls,[['/query',YM]]);
f=await run('Missing recovery without YM.pow preserves other schemes',{plans:new Set([A,B]),active:A,hasPow:false},/YM\.pow/,'ensureYemanScheme');assert.equal(f.calls.some(c=>c[0]==='/import'),false);
f=await run('Missing recovery import denied is visible',{plans:new Set([A,B]),active:A,importFailure:true},/import denied/,'ensureYemanScheme');assert.equal(f.active,A);
f=await run('Missing recovery rejects silent failed import',{plans:new Set([A,B]),active:A,importIgnored:true},/导入后仍未/,'ensureYemanScheme');assert.equal(f.active,A);
f=await run('Selecting missing YMCC restores and activates fixed GUID',{plans:new Set([A,B]),active:A},null,'switchScheme');assert.deepEqual(f.calls.filter(c=>c[0]==='/setactive'),[['/setactive',YM]]);
f=await run('Selecting restored YMCC reports activation denied',{plans:new Set([A,B]),active:A,activationFailure:YM},/access denied/,'switchScheme');assert.equal(f.active,A);assert.equal(f.settings.power,undefined);
f=await run('Selecting restored YMCC checks final activation before remembering',{plans:new Set([A,B]),active:A,activationIgnored:YM},/仍未生效/,'switchScheme');assert.equal(f.settings.power,undefined);
f=await run('Automatic missing recovery uses the same fixed GUID',{plans:new Set([A,B]),active:A},null,'ensureRememberedYemanSchemeActive');assert.equal(f.settings.power.scheme.mode,'auto');
f=await run('Automatic existing YMCC does not import extra schemes',{},null,'ensureRememberedYemanSchemeActive');assert.equal(f.calls.some(c=>c[0]==='/import'||c[0]==='/setactive'),false);
await run('A failed reset does not poison subsequent queue entries');
f=await run('Automatic reset retains automatic mode',{mode:'auto'});assert.equal(f.settings.power.scheme.mode,'auto');
// Extend this same consumer; the original cases/assertions above remain unchanged.
const originalCaseCount=tests.length;
const schemeRequests=f=>f.ipc.filter(i=>i.cmd==='power.schemeExists');
async function checkExists(name,options,check){
 const f=resetFixture({nativeSchemeExists:true,nativeSetActiveScheme:true,...options});
 yeman.setPowerControlDir(f.directory);
 const beforeSettings=JSON.stringify(f.settings),beforePersonality=[...f.personality];
 const details=await check(f);
 assert.deepEqual(f.calls.filter(c=>c[0]!=='/query'),[],name+' no CLI writes');
 assert(f.ipc.every(i=>i.cmd==='power.schemeExists'||i.cmd==='shell.run'||i.cmd==='fs.exists'),name+' no activation/registry/settings writes');
 assert.equal(JSON.stringify(f.settings),beforeSettings);assert.deepEqual(f.personality,beforePersonality);
 const expectedTimers=f.ipc.filter(i=>i.cmd==='power.schemeExists'||i.cmd==='shell.run').map(i=>i.cmd==='power.schemeExists'?5000:i.args.timeoutMs+5000);
 assert.deepEqual(f.timerMs,expectedTimers,name+' existence IPC is bounded to 5000ms; old CLI keeps its original timeout');
 tests.push({name,action:'schemeExists',pass:true,calls:f.calls,ipc:f.ipc,timerMs:f.timerMs,details});return f;
}
for(const present of [true,false])await checkExists('Native '+(present?'present':'absent')+' is a fresh boolean on repeated reads',{plans:new Set(present?[YM]:[])},async f=>{
 for(let i=0;i<2;i++)assert.equal(await yeman.schemeExists(YM),present);
 assert.equal(schemeRequests(f).length,2);assert.deepEqual(f.calls,[]);return {present,requests:2};
});
await checkExists('Same GUID delete-create ABA never reuses presence',{},async f=>{
 const values=[await yeman.schemeExists(YM)];f.plans.delete(YM);values.push(await yeman.schemeExists(YM));f.plans.add(YM);values.push(await yeman.schemeExists(YM));
 assert.deepEqual(values,[true,false,true]);assert.equal(schemeRequests(f).length,3);assert.deepEqual(f.calls,[]);return {values};
});
await checkExists('Alternate GUIDs do not share a global presence cache',{plans:new Set([A])},async f=>{
 const guids=[A,B,A,B],values=[];for(const guid of guids)values.push(await yeman.schemeExists(guid));
 assert.deepEqual(values,[true,false,true,false]);assert.deepEqual(schemeRequests(f).map(i=>i.args.scheme),guids);assert.deepEqual(f.calls,[]);return {guids,values};
});
await checkExists('SCHEME_CURRENT is forwarded and freshly queried after current changes',{active:A,plans:new Set([A])},async f=>{
 const values=[await yeman.schemeExists('sChEmE_CuRrEnT')];f.active=B;values.push(await yeman.schemeExists('sChEmE_CuRrEnT'));
 assert.deepEqual(values,[true,false]);assert.deepEqual(schemeRequests(f).map(i=>i.args.scheme),['sChEmE_CuRrEnT','sChEmE_CuRrEnT']);assert.deepEqual(f.calls,[]);return {values,scope:'frontend forwarding only, not native alias implementation'};
});
const forwarded=[
 [YM,true],[YM.toUpperCase(),true],['{'+YM+'}',true],['{'+YM.toUpperCase()+'}',true],
 ['SCHEME_CURRENT',true],['sChEmE_bAlAnCeD',true],['SCHEME_MIN',true],['scheme_max',true],
 ['',false],['not-a-guid',false],['SCHEME_UNKNOWN',false],[YM.slice(0,-1),false],['{'+YM,false],[' '+YM,false],
];
await checkExists('GUID braces case aliases and invalid strings are forwarded unchanged',{},async f=>{
 for(const [scheme,exists] of forwarded){f.schemeExistsReceipt={ok:true,exists,code:0};assert.equal(await yeman.schemeExists(scheme),exists)}
 assert.deepEqual(schemeRequests(f).map(i=>i.args),forwarded.map(([scheme])=>({scheme})));assert.deepEqual(f.calls,[]);
 return {forwarded,scope:'frontend accepts successful native booleans; native parsing is tested separately'};
});
for(const message of ['unknown: power.schemeExists','unknown command: power.schemeExists','UnKnOwN:POWER.SCHEMEEXISTS','unknown command:\tpower.schemeExists']){
 await checkExists('Only explicit unknown permits CLI: '+JSON.stringify(message),{schemeExistsError:message,plans:new Set([YM])},async f=>{
  assert.equal(await yeman.schemeExists(YM),true);f.plans.delete(YM);assert.equal(await yeman.schemeExists(YM),false);
  assert.deepEqual(f.calls,[['/query',YM],['/query',YM]]);assert.equal(schemeRequests(f).length,2);return {message,present:true,absent:false,legacyNonzeroRemainsFalse:true};
 });
}
await checkExists('An old-shell fallback is not cached across later native errors',{schemeExistsError:'unknown: power.schemeExists'},async f=>{
 assert.equal(await yeman.schemeExists(YM),true);f.schemeExistsError='access denied';await assert.rejects(yeman.schemeExists(YM),/access denied/);
 assert.deepEqual(f.calls,[['/query',YM]]);delete f.schemeExistsError;assert.equal(await yeman.schemeExists(YM),true);
 assert.equal(schemeRequests(f).length,3);assert.equal(f.calls.length,1);return {nativeRequests:3,cliRequests:1};
});
const failures=[
 ['operation denied',{schemeExistsReceipt:{ok:false,exists:false,code:5}},/读取电源方案失败：5/],
 ['maximum unsigned DWORD error',{schemeExistsReceipt:{ok:false,exists:false,code:4294967295}},/读取电源方案失败：4294967295/],
 ['IPC denied',{schemeExistsError:'access denied'},/access denied/],
 ['native schema error',{schemeExistsError:'invalid power.schemeExists args'},/invalid power\.schemeExists args/],
 ['unanswered native IPC',{schemeExistsHang:true,fastTimeouts:true},/timed out/],
];
for(const message of ['unknown: power.schemeExists extra','prefix unknown: power.schemeExists','unknown command: power.setActiveScheme','unknown: power.schemeExists.timeout','unknown power.schemeExists',' unknown: power.schemeExists','unknown: power.schemeExists '])failures.push(['non-exact unknown '+JSON.stringify(message),{schemeExistsError:message},null]);
for(const [label,receipt] of [
 ['null',null],['undefined',undefined],['boolean',true],['number',0],['string pretending unknown','unknown: power.schemeExists'],['array',[]],['missing fields',{}],
 ['missing exists',{ok:true,code:0}],['missing ok',{exists:true,code:0}],['missing code',{ok:true,exists:false}],
 ['nonboolean ok',{ok:1,exists:false,code:0}],['nonboolean exists',{ok:true,exists:0,code:0}],['string code',{ok:true,exists:false,code:'0'}],
 ['fractional code',{ok:true,exists:false,code:0.5}],['negative code',{ok:false,exists:false,code:-1}],['overflow code',{ok:false,exists:false,code:4294967296}],
 ['NaN code',{ok:false,exists:false,code:NaN}],['infinite code',{ok:false,exists:false,code:Infinity}],
])failures.push(['malformed '+label,{schemeExistsReceipt:receipt},/回执无效/]);
for(const receipt of [
 {ok:true,exists:true,code:5},{ok:true,exists:false,code:5},{ok:false,exists:true,code:5},{ok:false,exists:false,code:0},{ok:false,exists:true,code:0},
])failures.push(['contradiction '+JSON.stringify(receipt),{schemeExistsReceipt:receipt},/回执矛盾/]);
for(const [label,options,expected] of failures)await checkExists('Fail closed before writes: '+label,options,async f=>{
 const errors=[];
 for(const action of ['schemeExists','ensureYemanScheme','rebuildYemanScheme']){
  const active=f.active,plans=[...f.plans];let error;
  try{await yeman[action](action==='schemeExists'?YM:undefined)}catch(e){error=e}
  assert(error instanceof Error,label+' '+action+' must throw');if(expected)assert.match(error.message,expected);else assert.equal(error.message,options.schemeExistsError);
  assert.deepEqual(f.calls,[],label+' no CLI fallback/import/delete/write');assert.deepEqual([...f.plans],plans);assert.equal(f.active,active);
  errors.push({action,error:error.message});
 }
 assert.equal(schemeRequests(f).length,3);return {errors};
});
f=await run('Native present recovery does not spawn CLI query or activate',{nativeSchemeExists:true,nativeSetActiveScheme:true},null,'ensureYemanScheme');
assert.deepEqual(f.calls,[]);assert.equal(schemeRequests(f).length,1);assert.equal(personalityReads(f).length,2);
f=await run('Native absent recovery confirms imported fixed GUID freshly',{nativeSchemeExists:true,nativeSetActiveScheme:true,plans:new Set([A,B]),active:A},null,'ensureYemanScheme');
assert.deepEqual(f.calls,[['/import',f.directory+'\\YM.pow',YM]]);assert.deepEqual(f.schemeChecks.map(i=>i.exists),[false,true]);assert.equal(f.active,A);
f=await run('Native rebuild freshly observes fixed GUID present absent and present',{nativeSchemeExists:true,nativeSetActiveScheme:true});
assert.equal(f.calls.some(c=>c[0]==='/query'||c[0]==='/setactive'),false);
assert.deepEqual(f.schemeChecks.filter(i=>i.scheme===YM).map(i=>i.exists),[true,false,false,true]);
assert.deepEqual(f.ipc.filter(i=>i.cmd==='power.setActiveScheme').map(i=>i.args.scheme),[A,YM]);
await new Promise(resolve=>realSetTimeout(resolve,25));
assert.equal(ownedTimers.size,0,'Every fixture-owned IPC timeout is cleared or expires, including no-reply cases');
const coverage={originalCases:originalCaseCount,addedCases:tests.length-originalCaseCount,forwardedArgumentSubcases:forwarded.length,failClosedReceiptSubcases:failures.length,failClosedConsumerInvocations:failures.length*3};
const runtimeClosure={remainingOwnedTimers:ownedTimers.size,memoryWebViewOnly:true,realPowercfgExecuted:false,nativeApiExecuted:false,ipcTimeoutMs:5000,timeoutClock:'No-reply cases use the original bounded fast clock (10ms); requested IPC timeout is asserted as 5000ms'};
writeFileSync(process.argv[2],JSON.stringify({passed:tests.length,total:tests.length,tests,coverage,runtimeClosure,actualBridgeModule:true,memoryIpcOnly:true,realPowercfgExecuted:false},null,2));
console.log(tests.length+'/'+tests.length+' power-scheme rebuild tests passed');
`;
const sha256=file=>createHash('sha256').update(readFileSync(file)).digest('hex');
const sourceFile=resolve(repo,'src/bridge/yeman.ts'),sourceBefore=sha256(sourceFile);
const built=await build({stdin:{contents:entry,resolveDir:repo,loader:'js'},bundle:true,platform:'node',format:'esm',target:'node22',outfile:resolve(out,'fixture.mjs'),alias:{'@':resolve(repo,'src')},metafile:true});
await stop();
assert.equal(sha256(sourceFile),sourceBefore,'Shared yeman.ts must not drift during fixture bundling');
const sources=Object.keys(built.metafile.inputs).filter(file=>file!=='<stdin>').map(file=>({file:resolve(repo,file),sha256:sha256(resolve(repo,file))}));
writeFileSync(resolve(out,'bundle-metafile.json'),JSON.stringify(built.metafile,null,2));
const witness={file:sourceFile,sha256:sourceBefore,sources,consumer:{file:fileURLToPath(import.meta.url),sha256:sha256(fileURLToPath(import.meta.url))},memoryIpcOnly:true};
writeFileSync(resolve(out,'source-witness.json'),JSON.stringify(witness,null,2));
const startedAt=new Date().toISOString(),started=performance.now();
const run=spawnSync(process.execPath,[resolve(out,'fixture.mjs'),resolve(out,'results.json')],{encoding:'utf8',windowsHide:true,timeout:60000});
writeFileSync(resolve(out,'process-receipt.json'),JSON.stringify({startedAt,finishedAt:new Date().toISOString(),elapsedMs:performance.now()-started,cwd:process.cwd(),node:process.execPath,nodeVersion:process.version,hostPid:process.pid,childPid:run.pid,command:[process.execPath,resolve(out,'fixture.mjs'),resolve(out,'results.json')],timeoutMs:60000,status:run.status,signal:run.signal,error:run.error?String(run.error):null,esbuildStopped:true},null,2));
writeFileSync(resolve(out,'run.log'),(run.stdout||'')+(run.stderr||''));
assert.equal(run.status,0,run.stdout+'\n'+run.stderr);console.log(run.stdout.trim());
