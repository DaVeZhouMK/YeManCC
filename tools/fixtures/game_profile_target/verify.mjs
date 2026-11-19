import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';
export async function verifyTargetIdentity(p){
 const {schedule,capture,clone}=p,report={pass:false,scope:'Actual current performanceSchedule with memory terminal ports, actual target producer. No installed app or hardware.',cases:[],realHardwareWrites:0,errors:[]};
 fs.mkdirSync(p.output,{recursive:true});
 const tracked=['src/bridge/gameCustomProfileTarget.ts','src/bridge/gamePolicyHysteresis.ts','src/bridge/performanceSchedule.ts','src/bridge/deckyProfileActions.ts','tools/game_profile_editor_selftest.mjs','tools/fixtures/game_profile_target/verify.mjs'];
 const hashes=()=>Object.fromEntries(tracked.map(file=>[file,createHash('sha256').update(fs.readFileSync(file)).digest('hex')]));report.sourceHashes=hashes();
 const game={name:'sample.exe',title:'Sample',path:'G:\\Games\\Sample\\sample.exe',pid:4132,processCreated:'134380657807671463',identityGeneration:27};
 const original=clone(game),generation=1,target=capture(game,generation);
 const check=async(name,run)=>{try{await run();report.cases.push({name,ok:true});}catch(error){report.cases.push({name,ok:false,error:String(error.stack||error)});}};
 const stable=()=>{p.setGame(clone(original));p.setGeneration(1);p.setCurrent(true);};stable();
 const master=await schedule.loadPerformanceSchedule(),snapshot=await schedule.getContinuousPerformanceSnapshot('global');
 const seed={displayName:'Sample',enabled:true,controls:clone(snapshot.controls),ac:clone(master.profiles.ac[master.active.ac]),dc:clone(master.profiles.dc[master.active.dc]),padPersona:'follow',gyroOverride:'follow'};
 const globalBefore=JSON.stringify(p.state().settings.performanceSchedule);
 await check('full producer carries exact path/native identity generation and lossless birth',async()=>{
  assert.deepEqual(JSON.parse(JSON.stringify(target)),{exe:'sample.exe',pid:4132,processCreated:original.processCreated,path:original.path,identityGeneration:27,generation:1});
  game.path='G:\\Other\\replacement.exe';game.identityGeneration=99;assert.equal(target.path,original.path);assert.equal(target.identityGeneration,27);
 });
 await check('installed old four-field producer is rejected before any config or hardware write',async()=>{
  const before=JSON.stringify(p.state()),old={exe:target.exe,pid:target.pid,processCreated:target.processCreated,generation:target.generation};
  await assert.rejects(schedule.patchGameCustomProfile(old,{enabled:true},seed,{expectedAbsent:true}),/outdated/);assert.equal(JSON.stringify(p.state()),before);
 });
 await check('complete identity creates enabled dedicated config and preserves global parameters',async()=>{
  await schedule.patchGameCustomProfile(target,{enabled:true},seed,{expectedAbsent:true});
  const saved=await schedule.loadGameCustomConfig();assert.equal(saved.entries['sample.exe'].enabled,true);assert.deepEqual(clone(saved.entries['sample.exe'].controls),seed.controls);assert.equal(JSON.stringify(p.state().settings.performanceSchedule),globalBefore);assert.equal(p.state().writes.length,0);
 });
 await check('same complete target handles input and enabled toggles without performance overwrite',async()=>{
  await schedule.patchGameCustomProfile(target,{padPersona:'elite'});
  await schedule.patchGameCustomProfile(target,{enabled:false},undefined,{expectedEnabled:true});await schedule.patchGameCustomProfile(target,{enabled:true},undefined,{expectedEnabled:false});
  const saved=await schedule.loadGameCustomConfig();assert.equal(saved.entries['sample.exe'].padPersona,'elite');assert.equal(saved.entries['sample.exe'].enabled,true);assert.deepEqual(clone(saved.entries['sample.exe'].controls),seed.controls);
 });
 for(const [name,patch]of [['same PID and birth but new path',{path:'G:\\Other\\sample.exe'}],['same PID/path/birth but new native identity generation',{identityGeneration:28}],['same PID/path but restarted process',{processCreated:'134380657807671464'}],['different selected PID',{pid:4133}]])await check(name+' refuses an old action with no write',async()=>{
  stable();const before=JSON.stringify(p.state());p.setGame({...original,...patch});await assert.rejects(schedule.patchGameCustomProfile(target,{padPersona:'steamdeck'}),/outdated/);assert.equal(JSON.stringify(p.state()),before);stable();
 });
 await check('pending/unavailable authority refuses an otherwise matching captured target',async()=>{
  const before=JSON.stringify(p.state());p.setCurrent(false);await assert.rejects(schedule.patchGameCustomProfile(target,{enabled:false}),/outdated/);assert.equal(JSON.stringify(p.state()),before);stable();
 });
 await check('settings reset generation cannot replay a previous edit',async()=>{
  const before=JSON.stringify(p.state());p.setGeneration(2);await assert.rejects(schedule.patchGameCustomProfile(target,{enabled:false}),/generation/);assert.equal(JSON.stringify(p.state()),before);stable();
 });
 await check('complete target removes one confirmed dedicated config on first confirmation',async()=>{
  const saved=await schedule.loadGameCustomConfig(),revision=JSON.stringify([saved.entries['sample.exe'],saved.rtss['sample.exe']]);await schedule.removeGameCustomProfile(target,revision);
  const after=await schedule.loadGameCustomConfig();assert.equal(after.entries['sample.exe'],undefined);assert.equal(after.rtss['sample.exe'],undefined);assert.equal(p.state().writes.length,0);
 });
 await check('Decky actual action producer submits the same full identity',async()=>{
  const dependencies={'./gamePolicyHysteresis':p.keyModule,'./gameCustomProfileTarget':{captureGameCustomProfileTarget:capture},'./virtualTargetPolicy':{requireVirtualTargetAllowed:()=>{}},'./deckyGameActions':{gameMirrorIdentity:g=>JSON.stringify(g),makeGameMirrorEntry:()=>clone(seed),rememberGameMirrorInput:()=>{}}};
  const module=p.evaluate('src/bridge/deckyProfileActions.ts',dependencies),config={entries:{'sample.exe':clone(seed)},rtss:{}},revision=module.gameProfileRevision(config,'sample.exe');let observed;
  const actions=module.createProfileMirrorActions({allowed:()=>true,game:()=>original,label:()=> 'Sample',settingsGeneration:()=>1,power:async()=>({phase:'ready',hardwareWritesAllowed:true,generation:1}),acquire:()=>()=>{},load:async()=>clone(config),remove:async t=>{observed=clone(t)},restore:async()=> 'auto'});
  await actions.execute({field:'profile',value:'global'},{generation:1,gameIdentity:JSON.stringify(original),gameAdmission:{profile:revision},checkpoint(){}});assert.deepEqual(observed,JSON.parse(JSON.stringify(target)));
 });
 await check('Decky resumed power generation is independent of the settings generation',async()=>{
  stable();await schedule.patchGameCustomProfile(target,{enabled:true},seed,{expectedAbsent:true});
  const deps={'./gamePolicyHysteresis':p.keyModule,'./gameCustomProfileTarget':{captureGameCustomProfileTarget:capture},'./virtualTargetPolicy':{requireVirtualTargetAllowed:()=>{}},'./deckyGameActions':{gameMirrorIdentity:g=>JSON.stringify(g),makeGameMirrorEntry:()=>clone(seed),rememberGameMirrorInput:()=>{}}};
  const module=p.evaluate('src/bridge/deckyProfileActions.ts',deps),config=await schedule.loadGameCustomConfig(),revision=module.gameProfileRevision(config,'sample.exe');let observed;
  const actions=module.createProfileMirrorActions({allowed:()=>true,game:()=>original,label:()=> 'Sample',settingsGeneration:()=>1,power:async()=>({phase:'ready',hardwareWritesAllowed:true,generation:9}),acquire:()=>()=>{},load:schedule.loadGameCustomConfig,remove:async(t,r)=>{observed=clone(t);await schedule.removeGameCustomProfile(t,r)},restore:async()=> 'auto'});
  await actions.execute({field:'profile',value:'global'},{generation:9,gameIdentity:JSON.stringify(original),gameAdmission:{profile:revision},checkpoint(){}});
  assert.equal(observed.generation,1);assert.equal((await schedule.loadGameCustomConfig()).entries['sample.exe'],undefined);
 });
 await check('Decky settings reset during a power read cancels before store mutation',async()=>{
  stable();let settingsGen=1,mutated=false;const config={entries:{'sample.exe':clone(seed)},rtss:{}};
  const deps={'./gamePolicyHysteresis':p.keyModule,'./gameCustomProfileTarget':{captureGameCustomProfileTarget:capture},'./virtualTargetPolicy':{requireVirtualTargetAllowed:()=>{}},'./deckyGameActions':{gameMirrorIdentity:g=>JSON.stringify(g),makeGameMirrorEntry:()=>clone(seed),rememberGameMirrorInput:()=>{}}};
  const module=p.evaluate('src/bridge/deckyProfileActions.ts',deps),revision=module.gameProfileRevision(config,'sample.exe');
  const actions=module.createProfileMirrorActions({allowed:()=>true,game:()=>original,label:()=> 'Sample',settingsGeneration:()=>settingsGen,power:async()=>{settingsGen=2;return {phase:'ready',hardwareWritesAllowed:true,generation:9}},acquire:()=>()=>{},load:async()=>config,remove:async()=>{mutated=true},restore:async()=> 'auto'});
  await assert.rejects(actions.execute({field:'profile',value:'global'},{generation:9,gameIdentity:JSON.stringify(original),gameAdmission:{profile:revision},checkpoint(){}}),/GAME_PROFILE_TARGET_CHANGED/);assert.equal(mutated,false);stable();
 });
 await check('Decky stale power generation remains rejected after separating settings epoch',async()=>{
  let mutated=false;const config={entries:{'sample.exe':clone(seed)},rtss:{}};
  const deps={'./gamePolicyHysteresis':p.keyModule,'./gameCustomProfileTarget':{captureGameCustomProfileTarget:capture},'./virtualTargetPolicy':{requireVirtualTargetAllowed:()=>{}},'./deckyGameActions':{gameMirrorIdentity:g=>JSON.stringify(g),makeGameMirrorEntry:()=>clone(seed),rememberGameMirrorInput:()=>{}}};
  const module=p.evaluate('src/bridge/deckyProfileActions.ts',deps),revision=module.gameProfileRevision(config,'sample.exe');
  const actions=module.createProfileMirrorActions({allowed:()=>true,game:()=>original,label:()=> 'Sample',settingsGeneration:()=>1,power:async()=>({phase:'ready',hardwareWritesAllowed:true,generation:9}),acquire:()=>()=>{},load:async()=>config,remove:async()=>{mutated=true},restore:async()=> 'auto'});
  await assert.rejects(actions.execute({field:'profile',value:'global'},{generation:8,gameIdentity:JSON.stringify(original),gameAdmission:{profile:revision},checkpoint(){}}),/GAME_PROFILE_POWER_NOT_READY/);assert.equal(mutated,false);
 });
 assert.deepEqual(hashes(),report.sourceHashes,'Source changed during target regression');
 report.passed=report.cases.filter(c=>c.ok).length;report.failed=report.cases.filter(c=>!c.ok).length;report.pass=report.failed===0;report.hardwareWritesObserved=p.state().writes.length;
 fs.writeFileSync(path.join(p.output,'result.json'),JSON.stringify(report,null,2));console.log(JSON.stringify({pass:report.pass,passed:report.passed,failed:report.failed,output:p.output},null,2));if(!report.pass)process.exitCode=1;return report;
}


// One optional boundary of the existing profile-target consumer. UI lifecycle
// hooks are inert; the real setup functions, target capture and store execute.
export async function verifyInputPreflight(p){
 const tracked=['src/components/GameCustomProfilePanel.vue','src/bridge/performanceSchedule.ts','src/bridge/gameCustomProfileTarget.ts','src/bridge/gamePolicyHysteresis.ts','src/bridge/performanceControls.ts','src/bridge/frameRateModel.ts','src/bridge/controllerPersona.ts','src/bridge/settingsSnapshot.ts','tools/game_profile_editor_selftest.mjs','tools/fixtures/game_profile_target/verify.mjs'];
 const hashes=()=>Object.fromEntries(tracked.map(file=>[file,createHash('sha256').update(fs.readFileSync(file)).digest('hex')]));
 const report={pass:false,selection:p.selection,scope:'Actual Vue setup producer plus performanceSchedule transaction; inert UI hooks/utility IPC and in-memory filesystem. No app, rendered UI, Steam or hardware.',sourceHashes:hashes(),cases:[],realHardwareWrites:0};
 const vue=p.vue;
 const a={name:'a.exe',title:'A',path:'G:\\Games\\A\\a.exe',pid:4132,processCreated:'134380657807671463',identityGeneration:27};
 const b={name:'b.exe',title:'B',path:'G:\\Games\\B\\b.exe',pid:4133,processCreated:'134380657807671465',identityGeneration:29};
 const definitions=[
  ['stable', 'same original game saves SteamDeck after the utility preflight'],
  ['switch', 'A to empty to B during JoyXoff wait cannot apply the old click to B'],
  ['birth', 'same PID with a new birth during preflight rejects the old click'],
  ['path', 'same PID and birth with a new path during preflight rejects the old click'],
  ['identity', 'a changed native selection generation rejects the old click'],
  ['settings', 'settings reset during preflight cannot reuse the old click'],
  ['unknown', 'unknown game readiness during preflight cannot save a profile'],
  ['busy', 'a repeated click during preflight owns only one utility request and write'],
  ['utility-failure', 'a failed JoyXoff close retains the original game selection behavior'],
 ];
 const selected=p.selection==='input-preflight-switch'?definitions.filter(([key])=>key==='switch'):
  p.selection==='input-preflight-controls'?definitions.filter(([key])=>['stable','busy','utility-failure'].includes(key)):definitions;
 assert.equal(selected.length,p.selection==='input-preflight-switch'?1:p.selection==='input-preflight-controls'?3:9);
 for(const [kind,name]of selected){
  const row={name};
  try{
   p.setGeneration(1);p.setCurrent(true);p.setGame(p.clone(a));let generation=1;
   const master=await p.schedule.loadPerformanceSchedule(),snapshot=await p.schedule.getContinuousPerformanceSnapshot('global');
   const seed=()=>({displayName:'fixture',enabled:true,controls:p.clone(snapshot.controls),ac:p.clone(master.profiles.ac[master.active.ac]),dc:p.clone(master.profiles.dc[master.active.dc]),padPersona:'follow',gyroOverride:'follow'});
   await p.schedule.saveGameCustomConfig({version:1,entries:{'a.exe':seed(),'b.exe':seed()},rtss:{}});
   const props=vue.reactive({game:p.clone(a),open:true}),events=[],targets=[];
   let finishClose,failClose,closes=0;
   const pendingClose=new Promise((resolve,reject)=>{finishClose=resolve;failClose=reject});
   const panel=p.evaluate('src/components/GameCustomProfilePanel.vue',{
    '@/bridge/virtualTargetPolicyState':{useVirtualTargetPolicy:()=>vue.ref({}),currentVirtualTargetPolicy:()=>({})},
    '@/bridge/virtualTargetPolicy':{virtualTargetOptions:x=>x,virtualTargetSelectionError:()=>null,requireVirtualTargetAllowed(){},STEAM_DECK_SELF_OUTPUT_MESSAGE:''},
    '@/bridge/gameCustomProfileTarget':{captureGameCustomProfileTarget:p.capture},
    vue:{...vue,onMounted(){},onUnmounted(){},watch(){},nextTick:async fn=>{fn?.()}},
    '@/gamepad/focus':{focusGamepadElement(){}},
    '@/bridge/performanceSchedule':{...p.schedule,patchGameCustomProfile:async(...args)=>{targets.push(p.clone(args[0]));return p.schedule.patchGameCustomProfile(...args)}},
    '@/bridge/gamedetect':{detectedGameName:g=>g?.title||g?.name||''},
    '@/bridge/quickActionLock':{tryAcquireQuickAction:()=>()=>{}},
    '@/bridge/gameproc':{closeJoyxoffIfRunning:()=>{closes++;return pendingClose}},
    '@/bridge/settingsRepository':{getSettingsGeneration:()=>generation},
   },{defineProps:()=>props,defineEmits:()=>((...args)=>events.push(args)),document:{querySelector:()=>null}},['selectSteamDeckPad','load']);
   await panel.load();const before=p.state(),operation=panel.selectSteamDeckPad('SteamDeck');
   assert.equal(closes,1);
   const change=next=>{p.setGame(next?p.clone(next):null);props.game=next?p.clone(next):null};
   if(kind==='switch'){change(null);change(b)}
   if(kind==='birth')change({...a,processCreated:'134380657807671464'});
   if(kind==='path')change({...a,path:'G:\\Other\\a.exe'});
   if(kind==='identity')change({...a,identityGeneration:28});
   if(kind==='settings'){generation=2;p.setGeneration(2)}
   if(kind==='unknown')p.setCurrent(false);
   if(kind==='busy')await panel.selectSteamDeckPad('SteamDeck');
   if(kind==='utility-failure')failClose(Error('fixture utility refusal'));else finishClose(true);
   await operation;
   const after=p.state(),saved=await p.schedule.loadGameCustomConfig();
   row.observedTargets=targets;row.utilityRequests=closes;
   assert.equal(closes,1,'the in-flight click must retain its single utility request');
   assert.deepEqual(after.writes,before.writes,'configuration editing must not issue hardware actions');
   assert.deepEqual(after.settings,before.settings,'no global configuration mutation');
   if(['stable','busy','utility-failure'].includes(kind)){
    assert.equal(saved.entries['a.exe'].padPersona,'steamdeck');assert.equal(saved.entries['b.exe'].padPersona,'follow');
    assert.equal(targets.length,1);assert.deepEqual(targets[0],p.clone(p.capture(a,1)));
    assert.ok(events.some(([event,value])=>event==='status'&&value.message?.includes('SteamDeck')));
   }else{
    assert.equal(after.custom,before.custom,'an old click cannot change A or B after admission changes');
    assert.ok(targets.every(target=>target.pid===a.pid&&target.processCreated===a.processCreated&&target.path===a.path&&target.identityGeneration===a.identityGeneration&&target.generation===1));
   }
   row.ok=true;
  }catch(error){row.ok=false;row.error=String(error.stack||error)}
  report.cases.push(row);
 }
 assert.deepEqual(hashes(),report.sourceHashes,'Source drift during the preflight consumer');
 report.passed=report.cases.filter(row=>row.ok).length;report.failed=report.cases.filter(row=>!row.ok).length;report.pass=report.failed===0;
 fs.mkdirSync(p.output,{recursive:true});fs.writeFileSync(path.join(p.output,'result.json'),JSON.stringify(report,null,2),{flag:'wx'});
 console.log(JSON.stringify({pass:report.pass,passed:report.passed,failed:report.failed,selection:report.selection}));
 if(!report.pass)process.exitCode=1;return report;
}
