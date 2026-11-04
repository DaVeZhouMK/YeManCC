// Actual Vue commit, shipped Decky controls/actions and the original scheduler share one memory hardware boundary.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {createRequire} from 'node:module';
import {createPerformanceMemoryFixture,evaluatePerformanceModule as evaluate,models} from './fixtures/performance_memory_fixture.mjs';
if(process.argv.includes('--power-schemes-only')) {
 const {verifyPowerSchemes}=await import('./fixtures/performance_power_schemes/verify.mjs');
 const index=process.argv.indexOf('--out-dir');
 const report=await verifyPowerSchemes({output:index>=0?process.argv[index+1]:undefined});
 console.log(JSON.stringify(report,null,2));
 process.exit(report.pass?0:1);
}
const require=createRequire(path.resolve('package.json')),ts=require('typescript');
const clone=x=>structuredClone(x),read=file=>fs.readFileSync(file,'utf8');
const mirrorModel=evaluate(read('decky-plugin/src/performanceModel.ts'));
const actions=evaluate(read('src/bridge/deckyPerformanceActions.ts'),{'../../decky-plugin/src/performanceModel':mirrorModel,'./performanceControls':models});
const lockModule=()=>evaluate(read('src/bridge/quickActionLock.ts'));
const React={Fragment:'fragment',createElement:(type,props,...children)=>({type,props:{...props,children}}),useState:v=>[v,()=>{}],useEffect(){}};
const DFL={Focusable:'focusable',PanelSectionRow:'row',SliderField:'slider',ToggleField:'toggle'};
const controls=evaluate(read('decky-plugin/src/PerformanceControls.tsx'),{'./performanceModel':mirrorModel,'./AnchoredDropdown':{AnchoredDropdown:'dropdown'}},{SP_REACT:React,DFL},'tsx');
function descendants(node){if(!node)return[];if(Array.isArray(node))return node.flatMap(descendants);if(typeof node!=='object')return[];return[node,...descendants(node.props?.children)]}
function vueEditor(f,scope) {
 const script=/<script setup[^>]*>([\s\S]*?)<\/script>/.exec(read('src/components/ContinuousPerformancePanel.vue'))[1];
 const ast=ts.createSourceFile('panel.ts',script,ts.ScriptTarget.Latest,true);
 const methods=ast.statements.filter(n=>ts.isFunctionDeclaration(n)&&['merge','change','commit','flushRefresh','sameEditContext'].includes(n.name?.text)).map(n=>n.getText(ast)).join('\n');
 return {async commit(patch){
  const snapshot={value:await f.schedule.getContinuousPerformanceSnapshot(scope)},note={value:''};
  const module=evaluate('let pending=null,draftRevision=0;const alive=true,epoch=0,contextInvalidated=false,editingInput=null,refreshPending=false;\n'+methods+'\nexport {commit};',{}, {
   props:{readonlyPreview:false},snapshot,powerScheme:{get value(){return snapshot.value?.powerScheme??'yeman'}},cpuManaged:{get value(){return snapshot.value.bank.mode!=='manual'||snapshot.value.powerScheme==='yeman'}},draft:{value:clone(snapshot.value.bank)},saving:{value:false},removalBusy:{value:false},note,
   frames:{value:clone(snapshot.value.frameRates)},frame:{value:clone(snapshot.value.frameRates[snapshot.value.side])},
   patchContinuousBank:models.patchContinuousBank,updateContinuousPerformanceControls:f.schedule.updateContinuousPerformanceControls,
   emit(){},load:async()=>{},
  });
  await module.commit(patch);assert.equal(note.value,'设置已保存');return snapshot.value;
 }};
}
function deckyEditor(f,beforeUpdate,sharedLock) {
 let live=true,phase='ready';const lock=sharedLock??lockModule();
 const gateway=actions.createPerformanceMirrorActions({allowed:()=>live,read:()=>f.schedule.getContinuousPerformanceSnapshot(),update:(...args)=>{beforeUpdate?.();return f.schedule.updateContinuousPerformanceControls(...args)},
  power:async()=>({phase,hardwareWritesAllowed:phase==='ready',generation:1}),acquire:lock.tryAcquireQuickAction});
 const context=s=>({generation:1,controlAdmission:{performance:actions.performanceMirrorSource(s)},checkpoint:()=>{if(!live)throw Error('stale-session')}});
 async function send(patch,snapshot){const s=snapshot??await f.schedule.getContinuousPerformanceSnapshot();return gateway.execute({patch},context(s))}
 return {send,disconnect:()=>{live=false},resuming:()=>{phase='resuming'},async activate(key,value){
  const s=await f.schedule.getContinuousPerformanceSnapshot(),view=actions.performanceMirrorView(s,'parity');let dispatched;
  const tree=controls.PerformanceControls({view,disabled:false,draft:{},change:(field,next)=>{dispatched=send({[field]:next},s)}});
  const nodes=descendants(tree);
  if(key==='mode'){
   const button=nodes.find(n=>n.props?.role==='button'&&n.props.children.flat(Infinity).includes(value==='auto'?'自动模式':'手动模式'));
   assert(button,'actual Decky mode button');button.props.onActivate();
  } else {
   const control=view.controls.find(c=>c.key===key);assert(control);const slider=nodes.find(n=>n.type==='slider'&&n.props.min===control.min&&n.props.max===control.max&&n.props.value===control.value&&descendants(n.props.label).some(v=>v.props?.children?.includes(control.label)));
   assert(slider,'actual Decky slider '+key);slider.props.onChange(value);
  }
  assert(dispatched,'control dispatched to original owner');return dispatched;
 }};
}
const cases=[];
async function check(name,run){if(process.argv.includes('--profile-actions-only')&&!name.startsWith('Decky全局')&&!name.startsWith('主线入口约定'))return;try{await run();cases.push({name,pass:true})}catch(e){cases.push({name,pass:false,error:e.stack})}}
for(const route of ['global','global-game','dedicated'])for(const side of ['ac','dc'])for(const mode of ['auto','manual'])await check(route+'/'+side+'/'+mode+' Vue与Decky实际按钮→同配置、同CPU/TDP调用',async()=>{
 const options={side,mode:mode==='auto'?'manual':'auto',dedicated:route==='dedicated',gameActive:route==='global-game'};
 const ui=createPerformanceMemoryFixture(options),decky=createPerformanceMemoryFixture(options),editor=vueEditor(ui,route==='dedicated'?'dedicated':'effective'),mirror=deckyEditor(decky);
 await editor.commit({mode});await mirror.activate('mode',mode);
 const patch=mode==='auto'?{auto:{bias:60}}:{manual:{frequencyMHz:3300}};
 await editor.commit(patch);await mirror.activate(mode==='auto'?'bias':'frequencyMHz',mode==='auto'?60:3300);
 const a=await ui.schedule.getContinuousPerformanceSnapshot(),b=await decky.schedule.getContinuousPerformanceSnapshot();
 assert.deepEqual(clone(a.controls),clone(b.controls));assert.equal(a.bank.mode,mode);assert.equal(b.identity.scope,route==='dedicated'?'dedicated':'global');
 assert.deepEqual(ui.hardwareWrites,decky.hardwareWrites);assert.deepEqual(clone(ui.settings),clone(decky.settings));
 assert.deepEqual(clone(await ui.schedule.loadGameCustomConfig()),clone(await decky.schedule.loadGameCustomConfig()));
 assert.equal(ui.floating,mode==='auto');assert.equal(decky.floating,mode==='auto');
 const before=decky.hardwareWrites.length;await decky.schedule.restorePerformanceScheduleIfConfigured(side,{automaticOnly:true});
 if(mode==='manual')assert.equal(decky.hardwareWrites.length,before);else assert(decky.hardwareWrites.length>before);
});
await check('Decky不能把旧全局快照写入切换后的专属',async()=>{
 const f=createPerformanceMemoryFixture({mode:'auto',gameActive:true}),editor=deckyEditor(f),stale=await f.schedule.getContinuousPerformanceSnapshot();
 await f.setEntry({enabled:true,displayName:'Game',controls:clone(stale.controls)});f.hardwareWrites.length=0;
 await assert.rejects(editor.send({mode:'manual'},stale),/SOURCE_CHANGED/);assert.deepEqual(f.hardwareWrites,[]);
});
for(const edge of ['disconnect','resuming'])await check('Decky '+edge+'不写入自动/手动',async()=>{const f=createPerformanceMemoryFixture({mode:'manual'}),editor=deckyEditor(f);editor[edge]();await assert.rejects(editor.send({mode:'auto'}));assert.deepEqual(f.hardwareWrites,[])});
await check('Decky无效模式被同一字段模型拒绝，实际参数不变',async()=>{const f=createPerformanceMemoryFixture(),editor=deckyEditor(f);await assert.rejects(editor.send({mode:'invented-mode'}));assert.deepEqual(f.hardwareWrites,[])});
await check('Decky在排队提交前失去会话时不能继续保存或写入',async()=>{
 const f=createPerformanceMemoryFixture({mode:'manual'});let editor;editor=deckyEditor(f,()=>editor.disconnect());
 await assert.rejects(editor.send({mode:'auto'}),/stale-session/);assert.deepEqual(f.hardwareWrites,[]);assert.equal(f.settings.performanceSchedule.controls.ac.mode,'manual');
});
const {PerformanceIntentQueue}=evaluate(read('decky-plugin/src/performanceIntentQueue.ts'));
for(const mode of ['auto','manual'])await check('Decky三秒合并最终进入原有'+mode+'配置，只提交最后一份参数',async()=>{
 const f=createPerformanceMemoryFixture({mode:mode==='auto'?'manual':'auto'}),editor=deckyEditor(f),snapshot=await f.schedule.getContinuousPerformanceSnapshot();
 let now=0,id=0;const timers=new Map();let sends=0;
 const queue=new PerformanceIntentQueue({current:()=>snapshot,sameSource:(a,b)=>actions.performanceMirrorSource(a)===actions.performanceMirrorSource(b),
  allowed:(patch,view)=>mirrorModel.performancePatchAllowed(patch,actions.performanceMirrorView(view,'queue')),
  value:(key,view)=>actions.performanceMirrorView(view,'queue').controls.find(c=>c.key===key)?.value,
  send:intent=>{sends++;return editor.send(intent.patch,intent.view)},schedule:(run,delay)=>{const key=++id;timers.set(key,{at:now+delay,run});return key},cancel:key=>timers.delete(key)});
 queue.choose('mode',mode,snapshot);queue.choose('tdp',34,snapshot);queue.choose('tdp',33,snapshot);queue.choose('tdp',32,snapshot);assert.equal(sends,0);
 now=3000;for(const [key,t]of [...timers])if(t.at<=now){timers.delete(key);t.run()}
 for(let i=0;i<1000;i++)await Promise.resolve();assert.equal(sends,1);
 const final=await f.schedule.getContinuousPerformanceSnapshot();assert.equal(final.bank.mode,mode);assert.equal(final.bank.tdp,32);assert.equal(f.hardwareWrites.filter(c=>c.kind==='tdp').length,1);assert.equal(f.floating,mode==='auto');
});
for(const route of ['global','dedicated','decky'])await check(route+'共享TDP保护：错误不钳最低、损坏配置不写默认、有效操作恢复',async()=>{
 const f=createPerformanceMemoryFixture({mode:'auto',dedicated:route!=='global'}),editor=route==='decky'?deckyEditor(f):vueEditor(f,route);
 const commit=patch=>route==='decky'?editor.send(patch):editor.commit(patch);
 await commit({tdp:15});const initial=f.hardwareWrites.length;
 await assert.rejects(commit({tdp:0}));assert.equal(f.hardwareWrites.length,initial);
 const current=await f.schedule.getContinuousPerformanceSnapshot('effective');
 if(route==='global')f.settings.performanceSchedule.controls.ac.tdp=null;
 else {const config=await f.schedule.loadGameCustomConfig();config.entries['game.exe'].controls.ac.tdp=null;await f.schedule.saveGameCustomConfig(config);}
 const begin=f.hardwareWrites.length;await f.schedule.restorePerformanceScheduleIfConfigured('ac',{automaticOnly:true});
 assert.deepEqual(f.hardwareWrites.slice(begin).filter(w=>w.kind==='tdp'),[]);assert.equal(f.floating,false);
 await commit({tdp:14});assert.equal(f.hardwareWrites.filter(w=>w.kind==='tdp').at(-1).watts,14);assert.equal(f.floating,true);
});
const policy=evaluate(read('src/bridge/virtualTargetPolicy.ts'));
const hysteresis=evaluate(read('src/bridge/gamePolicyHysteresis.ts'));
const gameActions=evaluate(read('src/bridge/deckyGameActions.ts'),{'./virtualTargetPolicy':policy,'./virtualTargetPolicyState':{currentVirtualTargetPolicy:()=>({steamDeckSelfOutputBlocked:false,reason:''})},'./gamePolicyHysteresis':hysteresis});
const targetCapture=evaluate(read('src/bridge/gameCustomProfileTarget.ts'),{'./gamePolicyHysteresis':hysteresis});
const profileActions=evaluate(read('src/bridge/deckyProfileActions.ts'),{'./virtualTargetPolicy':policy,'./gamePolicyHysteresis':hysteresis,'./gameCustomProfileTarget':targetCapture,'./deckyGameActions':gameActions});
function deferred(){let resolve;const promise=new Promise(r=>{resolve=r});return {promise,resolve}}
function profileEditor(f,{lock=lockModule(),beforeCommit=async()=>{},afterCommit=async()=>{},beforeRestore=async()=>{}}={}) {
 let live=true,phase='ready',powerGeneration=1,scheduleView;
 const gyro={source:'memory-defaults',padPersona:'disabled',enabled:false,preset:'fps'};
 const deps={allowed:()=>live,game:()=>f.game,label:()=> 'Game',settingsGeneration:()=>1,
  power:async()=>({phase,hardwareWritesAllowed:phase==='ready',generation:powerGeneration}),acquire:lock.tryAcquireQuickAction,
  load:f.schedule.loadGameCustomConfig,schedule:async()=>{scheduleView=await f.schedule.loadPerformanceSchedule();return scheduleView},currentSchedule:()=>scheduleView,
  snapshot:()=>f.schedule.getContinuousPerformanceSnapshot(),gyro:async()=>gyro,targetPolicy:()=>({steamDeckSelfOutputBlocked:false,reason:''}),
  import:async(...args)=>{await beforeCommit();await f.schedule.importGameCustomProfile(...args);await afterCommit('dedicated')},
  remove:async(...args)=>{await beforeCommit();await f.schedule.removeGameCustomProfile(...args);await afterCommit('global')},
  restore:async()=>{await beforeRestore();return f.schedule.restorePerformanceScheduleIfConfigured()}};
 const gateway=profileActions.createProfileMirrorActions(deps);
 async function admission(){const config=await f.schedule.loadGameCustomConfig(),snapshot=await f.schedule.getContinuousPerformanceSnapshot(),schedule=await f.schedule.loadPerformanceSchedule();
  return {generation:powerGeneration,gameIdentity:gameActions.gameMirrorIdentity(f.game),gameAdmission:{profile:profileActions.gameProfileRevision(config,'game.exe'),schedule:JSON.stringify(schedule),profileSource:profileActions.gameProfileImportSource(snapshot),gyro:gyro.source},checkpoint(){if(!live)throw Error('stale-session')}}}
 return {admission,select:async(value,context)=>gateway.execute({field:'profile',value},context??await admission()),
  disconnect:()=>{live=false},resuming:()=>{phase='resuming'},newPowerGeneration:()=>{powerGeneration++}};
}
const tdpWrites=f=>f.hardwareWrites.filter(w=>w.kind==='tdp');
const cpuWrites=f=>f.hardwareWrites.filter(w=>w.kind==='cpu');
for(const mode of ['auto','manual'])for(const side of ['ac','dc'])await check('Decky复杂切换 '+mode+'/'+side+' 普通→专属→全局→专属，每次只由本体消费一次',async()=>{
 const f=createPerformanceMemoryFixture({mode,side,gameActive:true}),lock=lockModule(),editor=deckyEditor(f,undefined,lock),profile=profileEditor(f,{lock});
 await editor.send({tdp:18});const global=await f.schedule.getContinuousPerformanceSnapshot();
 for(const [scope,watts]of [['dedicated',16],['global',17],['dedicated',15],['global',19]]){
  const savedGlobal=f.settings.performanceSchedule.controls[side].tdp,old=await f.schedule.getContinuousPerformanceSnapshot(),begin=tdpWrites(f).length,cpuBegin=cpuWrites(f).length,start=f.effects.length;
  const result=await profile.select(scope);assert.equal(result.saved,true);assert.equal(result.applied,true);
  const current=await f.schedule.getContinuousPerformanceSnapshot();assert.equal(current.identity.scope,scope);assert.equal(current.side,side);
  assert.equal(tdpWrites(f).length,begin+1,'one shared TDP apply per switch');assert.equal(cpuWrites(f).length,cpuBegin+1,'one shared CPU apply per switch');
  assert.equal(f.effects.slice(start).filter(x=>x==='start-float').length,mode==='auto'?1:0);assert.equal(f.floating,mode==='auto');
  const after=f.hardwareWrites.length;await assert.rejects(editor.send({tdp:40},old),/SOURCE_CHANGED/);assert.equal(f.hardwareWrites.length,after,'old owner has no hardware writes');
  const beforeEdit=tdpWrites(f).length;await editor.send({tdp:watts});assert.equal(tdpWrites(f).length,beforeEdit+1);assert.equal(tdpWrites(f).at(-1).watts,watts);
  if(scope==='dedicated')assert.equal(f.settings.performanceSchedule.controls[side].tdp,savedGlobal);
 }
});
for(const mode of ['auto','manual'])for(const dedicated of [false,true])for(const delayed of [false,true])await check('Decky复杂切换 '+mode+'/'+(dedicated?'专属→全局':'全局→专属')+'/'+(delayed?'迟到回执':'排队旧TDP')+' 提交归属后旧请求失效',async()=>{
 const hit=deferred(),releaseOld=deferred(),committed=deferred(),releaseProfile=deferred(),accepted=[];let hold=true;
 const ports={onTdpAccepted:(_side,w)=>accepted.push(w)};
 ports[delayed?'afterTdpWrite':'beforeTdpWrite']=async()=>{if(hold){hold=false;hit.resolve();await releaseOld.promise}};
 const f=createPerformanceMemoryFixture({mode,dedicated,gameActive:true,ports}),profile=profileEditor(f,{afterCommit:async()=>{committed.resolve();await releaseProfile.promise}});
 const old=f.schedule.restorePerformanceScheduleIfConfigured().then(value=>({value}),error=>({error:error.message}));await hit.promise;
 const change=profile.select(dedicated?'global':'dedicated');await committed.promise;
 releaseOld.resolve();await old;
 const oldAccepted=accepted.length,oldCpu=cpuWrites(f).length,oldFloating=f.floating,oldWrites=tdpWrites(f).length;
 releaseProfile.resolve();const result=await change;
 assert.equal(oldAccepted,0,'old request must not acknowledge new owner');assert.equal(oldCpu,0,'old owner must not continue CPU');assert.equal(oldFloating,false);
 assert.equal(oldWrites,delayed?1:0,'only already issued hardware can remain in trace');
 assert.equal(result.applied,true);assert.equal(accepted.length,1);assert.equal(cpuWrites(f).length,1);assert.equal(tdpWrites(f).length,delayed?2:1);
});
for(const dedicated of [false,true])for(const delayed of [false,true])await check('Decky复杂切换 '+(dedicated?'专属→全局':'全局→专属')+'/'+(delayed?'TDP编辑迟到回执':'TDP编辑排队')+' 撤销旧增量写入且不阻止新归属',async()=>{
 const hit=deferred(),releaseOld=deferred(),committed=deferred(),releaseProfile=deferred(),accepted=[];let armed=false;
 const ports={onTdpAccepted:(_side,w)=>accepted.push(w)};
 ports[delayed?'afterTdpWrite':'beforeTdpWrite']=async()=>{if(armed){armed=false;hit.resolve();await releaseOld.promise}};
 const f=createPerformanceMemoryFixture({mode:'auto',dedicated,gameActive:true,ports});await f.schedule.restorePerformanceScheduleIfConfigured();
 f.hardwareWrites.length=0;accepted.length=0;armed=true;const current=await f.schedule.getContinuousPerformanceSnapshot();
 const old=f.schedule.updateContinuousPerformanceControls(current.identity,{tdp:16}).then(value=>({value}),error=>({error:error.message}));await hit.promise;
 const profile=profileEditor(f,{afterCommit:async()=>{committed.resolve();await releaseProfile.promise}}),change=profile.select(dedicated?'global':'dedicated');await committed.promise;
 releaseOld.resolve();await old;const oldAccepted=accepted.length,oldWrites=tdpWrites(f).length;
 releaseProfile.resolve();assert.equal((await change).applied,true);assert.equal(oldAccepted,0);assert.equal(oldWrites,delayed?1:0);assert.equal(accepted.length,1);assert.equal(f.floating,true);
 assert.equal((await f.schedule.getContinuousPerformanceSnapshot()).identity.scope,dedicated?'global':'dedicated');
});
for(const mode of ['auto','manual'])await check('Decky复杂切换 '+mode+' 同一互斥器拒绝重入，普通编辑与专属切换共用本体队列',async()=>{
 const hit=deferred(),release=deferred();let hold=true;
 const f=createPerformanceMemoryFixture({mode,gameActive:true,ports:{beforeTdpWrite:async()=>{if(hold){hold=false;hit.resolve();await release.promise}}}}),lock=lockModule(),editor=deckyEditor(f,undefined,lock),profile=profileEditor(f,{lock});
 const edit=editor.send({tdp:17});await hit.promise;await assert.rejects(profile.select('dedicated'),/BUSY/);assert.equal(tdpWrites(f).length,0);
 release.resolve();await edit;assert.equal(tdpWrites(f).length,1);
 const context=await profile.admission();const first=await profile.select('dedicated',context);assert.equal(first.applied,true);const count=f.hardwareWrites.length;
 await assert.rejects(profile.select('dedicated',context),/SOURCE_CHANGED/);assert.equal(f.hardwareWrites.length,count);assert.equal(tdpWrites(f).length,2);
});
for(const boundary of ['pid','birth','supply','disconnect','resuming','power-generation'])await check('Decky复杂切换 '+boundary+' 拒绝旧入口，当前配置由本体继续接管',async()=>{
 const f=createPerformanceMemoryFixture({mode:'auto',gameActive:true}),profile=profileEditor(f),context=await profile.admission();
 if(boundary==='pid')f.setGame({...f.game,pid:23,processCreated:'created-23'});
 if(boundary==='birth')f.setGame({...f.game,processCreated:'created-again'});
 if(boundary==='supply')f.sourceMode.value='dc';
 if(boundary==='disconnect')profile.disconnect();if(boundary==='resuming')profile.resuming();if(boundary==='power-generation')profile.newPowerGeneration();
 const result=await profile.select('dedicated',context).then(value=>({value}),error=>({error:error.message}));
 if(boundary==='supply'){assert(result.error,'old supply snapshot rejected');assert.equal(tdpWrites(f).length,0);assert.equal((await profile.select('dedicated')).applied,true);assert.deepEqual(tdpWrites(f).map(w=>w.side),['dc']);assert.equal(f.ownerSide,'dc')}
 else {assert(result.error,'old context rejected');assert.equal(f.hardwareWrites.length,0);assert.equal((await f.schedule.getContinuousPerformanceSnapshot()).identity.scope,'global')}
});
await check('Decky复杂切换 已发CPU完成后才交新归属，不再延续旧核心/浮动/成功回执',async()=>{
 const hit=deferred(),release=deferred(),committed=deferred(),resumeProfile=deferred();let hold=true;const applied=[];
 const f=createPerformanceMemoryFixture({mode:'auto',gameActive:true,ports:{beforeCpuWrite:async()=>{if(hold){hold=false;hit.resolve();await release.promise}}}});
 f.schedule.onContinuousPerformanceApplied(value=>applied.push(value));
 const old=f.schedule.restorePerformanceScheduleIfConfigured().catch(()=>{});await hit.promise;
 const profile=profileEditor(f,{afterCommit:async()=>{committed.resolve();await resumeProfile.promise}}),change=profile.select('dedicated');await committed.promise;
 release.resolve();await old;assert.equal(f.hardwareWrites.filter(w=>w.kind==='core').length,0);assert.equal(applied.length,0);assert.equal(f.floating,false);
 resumeProfile.resolve();assert.equal((await change).applied,true);assert.equal(applied.length,1);assert.equal(cpuWrites(f).length,2,'already issued old batch drains before new batch');assert.equal(f.floating,true);
});
async function settleRuntime(f){for(let i=0;i<1000;i++)await Promise.resolve();await f.tasks.get('game-policy-runtime-retry')?.();for(let i=0;i<1000;i++)await Promise.resolve()}
for(const mode of ['auto','manual'])await check('Decky复杂切换 '+mode+' 常驻本体开启时往返四次与普通调节仍各消费一次',async()=>{
 const lock=lockModule(),f=createPerformanceMemoryFixture({mode,gameActive:true,ports:{isQuickActionBusy:lock.isQuickActionBusy}}),profile=profileEditor(f,{lock}),editor=deckyEditor(f,undefined,lock);
 await f.schedule.loadPerformanceSchedule();f.runtime.startGamePolicyRuntime();await settleRuntime(f);f.hardwareWrites.length=0;
 let editWatts=14;try {for(const scope of ['dedicated','global','dedicated','global']){
  const begin=tdpWrites(f).length;assert.equal((await profile.select(scope)).applied,true);await settleRuntime(f);assert.equal(tdpWrites(f).length,begin+1,'profile command and resident share acknowledgement');
  const editBegin=tdpWrites(f).length;await editor.send({tdp:editWatts++});await settleRuntime(f);assert.equal(tdpWrites(f).length,editBegin+1,'resident must not replay explicit edit');
 }}finally{f.runtime.stopGamePolicyRuntime()}
});
await check('Decky复杂切换 常驻读取中普通入口获得互斥，归属提交前后仍只由本体消费一次',async()=>{
 const hit=deferred(),releaseRuntime=deferred(),beforeCommit=deferred(),releaseCommit=deferred(),lock=lockModule();let hold=true;
 const f=createPerformanceMemoryFixture({mode:'auto',gameActive:true,ports:{isQuickActionBusy:lock.isQuickActionBusy,beforeRuntimeFrames:async()=>{if(hold){hold=false;hit.resolve();await releaseRuntime.promise}}}});
 await f.schedule.loadPerformanceSchedule();f.runtime.startGamePolicyRuntime();const starting=settleRuntime(f);await hit.promise;
 const profile=profileEditor(f,{lock,beforeCommit:async()=>{beforeCommit.resolve();await releaseCommit.promise}}),change=profile.select('dedicated');await beforeCommit.promise;
 releaseRuntime.resolve();await starting;for(let i=0;i<1200;i++)await Promise.resolve();const premature=tdpWrites(f).length;
 releaseCommit.resolve();const result=await change;await settleRuntime(f);f.runtime.stopGamePolicyRuntime();
 assert.equal(premature,0,'resident must honor the already acquired shared quick-action mutex after awaits');assert.equal(result.applied,true);assert.equal(tdpWrites(f).length,1);
});
for(const mode of ['auto','manual'])await check('Decky全局→专属→全局保留实际'+mode+'及AC/DC参数',async()=>{
 const f=createPerformanceMemoryFixture({mode,gameActive:true}),lock=lockModule(),schedule=await f.schedule.loadPerformanceSchedule();
 const gyro={source:'memory-defaults',padPersona:'disabled',enabled:false,preset:'fps'};
 const deps={allowed:()=>true,game:()=>f.game,label:()=> 'Game',settingsGeneration:()=>1,power:async()=>({phase:'ready',hardwareWritesAllowed:true,generation:1}),acquire:lock.tryAcquireQuickAction,
  load:f.schedule.loadGameCustomConfig,schedule:()=>f.schedule.loadPerformanceSchedule(),currentSchedule:()=>schedule,snapshot:()=>f.schedule.getContinuousPerformanceSnapshot(),gyro:async()=>gyro,targetPolicy:()=>({steamDeckSelfOutputBlocked:false,reason:''}),
  import:f.schedule.importGameCustomProfile,remove:f.schedule.removeGameCustomProfile,restore:f.schedule.restorePerformanceScheduleIfConfigured};
 const gateway=profileActions.createProfileMirrorActions(deps),global=await f.schedule.getContinuousPerformanceSnapshot();
 async function select(value){const config=await f.schedule.loadGameCustomConfig(),snapshot=await f.schedule.getContinuousPerformanceSnapshot();return gateway.execute({field:'profile',value},{generation:1,gameIdentity:gameActions.gameMirrorIdentity(f.game),gameAdmission:{profile:profileActions.gameProfileRevision(config,'game.exe'),schedule:JSON.stringify(schedule),profileSource:profileActions.gameProfileImportSource(snapshot),gyro:gyro.source},checkpoint(){}})}
 assert.equal((await select('dedicated')).saved,true);let s=await f.schedule.getContinuousPerformanceSnapshot();assert.equal(s.identity.scope,'dedicated');assert.deepEqual(clone(s.controls),clone(global.controls));assert.equal(s.bank.mode,mode);
 assert.equal((await select('global')).saved,true);s=await f.schedule.getContinuousPerformanceSnapshot();assert.equal(s.identity.scope,'global');assert.equal(s.bank.mode,mode);assert.deepEqual(clone(s.controls),clone(global.controls));
});
await check('主线入口约定：性能页跟随生效配置、顶部专属、Decky绑定同一写入函数',()=>{
 assert.match(read('src/views/PerformanceScheduleView.vue'),/ContinuousPerformancePanel scope="effective"/);
 assert.match(read('src/components/GameCustomProfilePanel.vue'),/ContinuousPerformancePanel v-if="hasConfiguration" scope="dedicated"/);
 assert.match(read('src/bridge/deckyMirrorHost.ts'),/createPerformanceMirrorActions\(\{[^\n]*read:\(\)=>getContinuousPerformanceSnapshot\(\),update:updateContinuousPerformanceControls/);
 assert.match(read('decky-plugin/src/index.tsx'),/mutate\('performance\.setControls',\{patch:intent\.patch\}/);
 assert.doesNotMatch(read('src/bridge/deckyPerformanceActions.ts'),/from ['"].*(?:yeman|autofloat)['"]/);
});
const report={pass:cases.every(c=>c.pass),count:cases.length,failed:cases.filter(c=>!c.pass).length,cases,hardwareCalls:0,scope:process.argv.includes('--profile-actions-only')?'Selected cases only: actual Decky profile adapter -> shared performance store global/dedicated round trips in auto/manual, plus static entry wiring assertions. Vue commit and TSX event cases NOT_RUN; no installed app or device operations.':'Actual Vue commit methods and shipped Decky TSX controls -> mirror action -> shared performanceSchedule and memory hardware boundary; actual profile import/remove. No installed app or device operations.'};
const outIndex=process.argv.indexOf('--out-dir');const out=outIndex>=0?process.argv[outIndex+1]:process.argv[2]?.startsWith('--')?undefined:process.argv[2];if(out){fs.mkdirSync(out,{recursive:true});fs.writeFileSync(path.join(out,'performance-entry-parity.json'),JSON.stringify(report,null,2)+'\n')}
console.log(JSON.stringify(report,null,2));if(!report.pass)process.exitCode=1;
