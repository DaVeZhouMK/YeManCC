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
