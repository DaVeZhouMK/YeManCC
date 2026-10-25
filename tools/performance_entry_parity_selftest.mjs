// Actual Vue commit, shipped Decky controls/actions and the original scheduler share one memory hardware boundary.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {createRequire} from 'node:module';
import {createPerformanceMemoryFixture,evaluatePerformanceModule as evaluate,models} from './fixtures/performance_memory_fixture.mjs';
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
   props:{readonlyPreview:false},snapshot,draft:{value:clone(snapshot.value.bank)},saving:{value:false},removalBusy:{value:false},note,
   frames:{value:clone(snapshot.value.frameRates)},frame:{value:clone(snapshot.value.frameRates[snapshot.value.side])},
   patchContinuousBank:models.patchContinuousBank,updateContinuousPerformanceControls:f.schedule.updateContinuousPerformanceControls,
   emit(){},load:async()=>{},
  });
  await module.commit(patch);assert.equal(note.value,'设置已保存');return snapshot.value;
 }};
}
function deckyEditor(f,beforeUpdate) {
 let live=true,phase='ready';const lock=lockModule();
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
async function check(name,run){try{await run();cases.push({name,pass:true})}catch(e){cases.push({name,pass:false,error:e.stack})}}
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
const policy=evaluate(read('src/bridge/virtualTargetPolicy.ts'));
const hysteresis=evaluate(read('src/bridge/gamePolicyHysteresis.ts'));
const gameActions=evaluate(read('src/bridge/deckyGameActions.ts'),{'./virtualTargetPolicy':policy,'./virtualTargetPolicyState':{currentVirtualTargetPolicy:()=>({steamDeckSelfOutputBlocked:false,reason:''})},'./gamePolicyHysteresis':hysteresis});
const profileActions=evaluate(read('src/bridge/deckyProfileActions.ts'),{'./virtualTargetPolicy':policy,'./gamePolicyHysteresis':hysteresis,'./deckyGameActions':gameActions});
for(const mode of ['auto','manual'])await check('Decky全局→专属→全局保留实际'+mode+'及AC/DC参数',async()=>{
 const f=createPerformanceMemoryFixture({mode,gameActive:true}),lock=lockModule(),schedule=await f.schedule.loadPerformanceSchedule();
 const gyro={source:'memory-defaults',padPersona:'disabled',enabled:false,preset:'fps'};
 const deps={allowed:()=>true,game:()=>f.game,label:()=> 'Game',power:async()=>({phase:'ready',hardwareWritesAllowed:true,generation:1}),acquire:lock.tryAcquireQuickAction,
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
const report={pass:cases.every(c=>c.pass),count:cases.length,failed:cases.filter(c=>!c.pass).length,cases,hardwareCalls:0,scope:'Actual Vue commit methods and shipped Decky TSX controls -> mirror action -> shared performanceSchedule and memory hardware boundary; actual profile import/remove. No installed app or device operations.'};
const out=process.argv[2];if(out){fs.mkdirSync(out,{recursive:true});fs.writeFileSync(path.join(out,'performance-entry-parity.json'),JSON.stringify(report,null,2)+'\n')}
console.log(JSON.stringify(report,null,2));if(!report.pass)process.exitCode=1;
