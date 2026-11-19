import assert from 'node:assert/strict';import fs from 'node:fs';import path from 'node:path';
import {root,load,fixture,profile,seed,gamePolicyKey,gameDisplayName,performanceControlsModule} from './fixtures/decky_game_memory_store.mjs';
const key=load(fs.readFileSync(path.join(root,'src/bridge/gamePolicyHysteresis.ts'),'utf8'));
const action=load(fs.readFileSync(path.join(root,'src/bridge/deckyGameActions.ts'),'utf8'),{'./gamePolicyHysteresis':key});
const {createGameMirrorActions,makeGameMirrorAdmission,gameMirrorIdentity}=action;
const baseSchedule={version:2,configured:true,enabled:true,active:{ac:'balanced',dc:'eco'},profiles:{ac:{},dc:{}}};
for(const side of ['ac','dc'])for(const mode of ['eco','balanced','medium','performance','elite','extreme'])baseSchedule.profiles[side][mode]={cpuPreset:'balanced',coreMode:'big',tdpMax:side==='ac'?30:10,fpsTarget:60,cpuTarget:'none',tdpStrategy:'none'};
const game={pid:101,name:'game.exe',title:'Fixture Game',path:'C:/Games/Game.exe',processCreated:'birth-A'};
function fixtureAction(initial=seed()){const memory=fixture(initial),state={enabled:true,game,power:{generation:1,phase:'ready',hardwareWritesAllowed:true},schedule:structuredClone(baseSchedule),caps:{heterogeneous:true,smtAvailable:true},calls:{save:0,power:0,load:0}};const deps={featureAllowed:()=>state.enabled,game:()=>state.game,label:gameDisplayName,power:async()=>{state.calls.power++;return structuredClone(state.power);},acquire:memory.lock.tryAcquireQuickAction,load:async()=>{state.calls.load++;return memory.store.loadGameCustomConfig();},current:memory.store.getGameCustomConfig,schedule:async()=>structuredClone(state.schedule),currentSchedule:()=>state.schedule,save:async config=>{state.calls.save++;await memory.store.saveGameCustomConfig(config);},closeJoyxoff:async()=>false,continuous:async()=>structuredClone(performanceControlsModule.normalizeContinuousControls(undefined)),gyroDefaults:async()=>({padPersona:'steamdeck',virtualPad:true,enabled:true,preset:'fps',source:'fixture-gyro'})};const actionInstance=createGameMirrorActions(deps);return {memory,state,action:actionInstance,run:async(field,value)=>{const loaded=await memory.store.loadGameCustomConfig(),entry=loaded.entries[gamePolicyKey(game)],ctx={generation:state.power.generation,revision:1,gameIdentity:gameMirrorIdentity(game),gameAdmission:makeGameMirrorAdmission(entry,state.schedule,undefined,'fixture-gyro'),checkpoint:()=>{if(!state.enabled)throw Error('MIRROR_CONTEXT_EXPIRED');}};return actionInstance.execute('game.setField',{field,value},ctx);}};}

const cases=[];async function check(name,fn){await fn();cases.push(name);}
await check('legacy input adapter cannot create, revive or mutate an EXE profile',async()=>{
 for(const status of ['absent','dormant','active']){const initial=seed();if(status==='absent')delete initial.entries['game.exe'];else initial.entries['game.exe'].enabled=status==='active';const f=fixtureAction(initial),before=JSON.stringify(f.memory.raw());await assert.rejects(f.run('padPersona','elite'),/INPUT_GLOBAL/);assert.equal(f.state.calls.save,0);assert.equal(JSON.stringify(f.memory.raw()),before);assert.equal(f.state.calls.power,0);assert.equal(f.state.calls.load,0);}
});
await check('rejected EXE controller edits preserve independent AC/DC performance and FPS',async()=>{
 const initial=seed();initial.entries['game.exe'].controls=performanceControlsModule.normalizeContinuousControls(undefined);initial.entries['game.exe'].controls.ac.tdp=27;initial.entries['game.exe'].controls.dc.tdp=13;const f=fixtureAction(initial),before=JSON.stringify(f.memory.raw());await assert.rejects(f.run('padPersona','elite'),/INPUT_GLOBAL/);assert.equal(JSON.stringify(f.memory.raw()),before);assert.equal(f.state.calls.save,0);
});
await check('all legacy gyro switches and four presets refuse EXE saves',async()=>{
 const f=fixtureAction(),before=JSON.stringify(f.memory.raw());for(const [field,values] of [['gyroPreset',['fps','racing','custom','steam']],['gyroEnabled',['on','off']],['gyroOverride',['follow','on','off','fps','racing','custom','steam']]])for(const value of values)await assert.rejects(f.run(field,value),/INPUT_GLOBAL/);assert.equal(JSON.stringify(f.memory.raw()),before);assert.equal(f.state.calls.save,0);
});
await check('legacy power/core commands remain rejected without IO',async()=>{
 const f=fixtureAction();for(const [field,value] of [['acMode','performance'],['dcMode','eco'],['corePolicyMode','only-small'],['hyperThreadPolicy','off']])await assert.rejects(f.run(field,value),/INPUT_GLOBAL|INVALID/);assert.equal(f.state.calls.save,0);assert.equal(f.state.calls.power,0);assert.equal(f.state.calls.load,0);
});
await check('disabled virtual pad cannot make legacy EXE gyro controls writable',async()=>{
 const initial=seed();initial.entries['game.exe'].padPersona='disabled';initial.entries['game.exe'].gyroOverride='off';const f=fixtureAction(initial);await assert.rejects(f.run('gyroPreset','fps'),/INPUT_GLOBAL/);assert.equal(f.state.calls.save,0);
});
await check('retired adapter acquires no quick-action lock or owner resources',async()=>{
 const f=fixtureAction(),release=f.memory.lock.tryAcquireQuickAction('other');await assert.rejects(f.run('padPersona','elite'),/INPUT_GLOBAL/);assert.equal(f.state.calls.save,0);release();await assert.rejects(f.run('padPersona','elite'),/INPUT_GLOBAL/);const available=f.memory.lock.tryAcquireQuickAction('after');assert(available);available();
});
await check('opaque identity preserves legacy tuple and appends only a supplied native identity lease',()=>{
 const tuple=['game.exe',game.path,game.pid,game.processCreated];assert.equal(gameMirrorIdentity(game),JSON.stringify(tuple));assert.equal(gameMirrorIdentity(null),'');
 assert.equal(gameMirrorIdentity({...game,generation:10,identityGeneration:4}),JSON.stringify([...tuple,4]));
 assert.equal(gameMirrorIdentity({...game,generation:11,identityGeneration:4}),gameMirrorIdentity({...game,generation:10,identityGeneration:4}));
 assert.notEqual(gameMirrorIdentity({...game,identityGeneration:5}),gameMirrorIdentity({...game,identityGeneration:4}));
});

function admittedContext(f,target){return {generation:f.state.power.generation,revision:1,gameIdentity:gameMirrorIdentity(target),gameAdmission:makeGameMirrorAdmission(f.memory.store.getGameCustomConfig().entries[gamePolicyKey(target)],f.state.schedule,undefined,'fixture-gyro'),checkpoint:()=>{}};}
await check('new native lease cannot revive retired input actions or perform target IO',async()=>{
 const f=fixtureAction();await f.memory.store.loadGameCustomConfig();const target={...game,generation:10,identityGeneration:4},context=admittedContext(f,target);f.state.game={...target,generation:11,identityGeneration:5};await assert.rejects(f.action.execute('game.setField',{field:'padPersona',value:'elite'},context),/INPUT_GLOBAL/);assert.equal(f.state.calls.save,0);assert.equal(f.state.calls.power,0);assert.equal(f.state.calls.load,0);
});
await check('A to B to A transitions cannot revive retired EXE input commands',async()=>{
 const f=fixtureAction();await f.memory.store.loadGameCustomConfig();const target={...game,generation:10,identityGeneration:4};f.state.game=target;const pending=f.action.execute('game.setField',{field:'padPersona',value:'elite'},admittedContext(f,target));f.state.game={...target,pid:202,identityGeneration:5};f.state.game={...target,generation:12,identityGeneration:6};await assert.rejects(pending,/INPUT_GLOBAL/);assert.equal(f.state.calls.save,0);
});
console.log(JSON.stringify({status:'passed',cases,physicalHardwareWrites:0},null,2));
