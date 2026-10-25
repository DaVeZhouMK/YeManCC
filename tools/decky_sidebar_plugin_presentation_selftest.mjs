// Exact production TSX bindings with persistent inert hooks; real host snapshots.
import assert from 'node:assert/strict';import fs from 'node:fs';import path from 'node:path';import vm from 'node:vm';import {createRequire} from 'node:module';
import {root,task} from './fixtures/decky_task_paths.mjs';import {load} from './fixtures/decky_game_memory_store.mjs';import {createMirrorHostFixture} from './fixtures/decky_mirror_host_fixture.mjs';
const require=createRequire(path.join(root,'package.json')),{transformSync}=require('esbuild');const source=fs.readFileSync(process.env.YMCC_FAN_UI_SOURCE_OVERRIDE||path.join(root,'decky-plugin/src/index.tsx'),'utf8');
const timers=new Map();let id=0,now=0;const schedule=(action,delay)=>{timers.set(++id,{action,delay,due:now+delay});return id;},cancel=id=>timers.delete(id);
const fanModule=load(fs.readFileSync(path.join(root,'decky-plugin/src/fanChoiceDebounce.ts'),'utf8'));const performanceModel=load(fs.readFileSync(path.join(root,'decky-plugin/src/performanceModel.ts'),'utf8'));
const powerModule=load(fs.readFileSync(path.join(root,'decky-plugin/src/powerChoiceDebounce.ts'),'utf8'));const performanceModule=load(fs.readFileSync(path.join(root,'decky-plugin/src/performanceIntentQueue.ts'),'utf8'));
const state={connected:true,snapshot:null,busy:false,notice:''},mutations=[],visibleCalls=[],events=new Map(),hooks=[],refs=[],cleanups=[],subscriptions=new Set();let visible=true,hi=0,ri=0,ei=0,disposed=0;
class InertClient{snapshot(){return state;}subscribe(fn){subscriptions.add(fn);fn(state);return()=>subscriptions.delete(fn);}setVisible(value){visibleCalls.push(value);}bootstrapChanged(){}dispose(){disposed++;}mutate(command,args,expected){if(expected&&(expected.runId!==state.snapshot?.runId||expected.generation!==state.snapshot?.generation||expected.revision!==state.snapshot?.revision||expected.game?.identity!==state.snapshot?.game?.identity))return Promise.reject(Error('stale'));mutations.push({command,args:structuredClone(args)});return Promise.resolve();}}
const react={createElement:(type,props,...children)=>({type,props:props??{},children}),Fragment:'Fragment',useState:initial=>{const index=hi++;if(!(index in hooks))hooks[index]=initial;return [hooks[index],value=>{hooks[index]=value;}];},useRef:initial=>{const index=ri++;return refs[index]??= {current:initial};},useEffect:(action,deps)=>{const index=ei++;const old=cleanups[index];if(!old||JSON.stringify(old.deps)!==JSON.stringify(deps)){old?.stop?.();cleanups[index]={deps,stop:action()};}}};
const DFL={ButtonItem:'ButtonItem',PanelSection:'PanelSection',PanelSectionRow:'PanelSectionRow',SliderField:'SliderField',DropdownItem:'DropdownItem',ToggleField:'ToggleField',Router:{}};
const module={exports:{}};vm.runInNewContext(transformSync(source,{loader:'tsx',format:'cjs',jsxFactory:'SP_REACT.createElement'}).code,{module,exports:module.exports,SP_REACT:react,DFL,Event,__YMCC_DECKY_BUILD_ID__:'presentation-fixture',crypto:{randomUUID:()=> 'fixture'},window:{addEventListener:(k,v)=>events.set(k,v),removeEventListener:k=>events.delete(k),dispatchEvent:event=>{events.get(event.type)?.(event);return true;}},structuredClone,setTimeout:schedule,clearTimeout:cancel,require:name=>{if(name==='../../src/bridge/frameRateModel')return load(fs.readFileSync(path.join(root,'src/bridge/frameRateModel.ts'),'utf8'));if(name==='../../src/bridge/deckyInputOptions')return load(fs.readFileSync(path.join(root,'src/bridge/deckyInputOptions.ts'),'utf8'),{'./controllerPersona':load(fs.readFileSync(path.join(root,'src/bridge/controllerPersona.ts'),'utf8'))});if(name==='./steamWallpaper')return {installSteamWallpaper:()=>null};if(name==='./deckyLoaderUpdate')return {installLoaderUpdateRow:()=>null};if(name==='./steamHudProbe')return {installSteamHudProbe:()=>{throw Error('Default build must not install HUD probe');}};if(name==='./steamMonitorHud')return {installSteamMonitorHud:()=>null};if(name==='./AnchoredDropdown')return {AnchoredDropdown:'AnchoredDropdown'};if(name==='./fanChoiceDebounce')return fanModule;if(name==='./powerChoiceDebounce')return powerModule;if(name==='./performanceIntentQueue')return performanceModule;if(name==='./performanceModel')return performanceModel;if(name==='./PerformanceControls')return {PerformanceControls:'PerformanceControls'};if(name==='./GameProfileControls')return {GameProfileControls:'GameProfileControls'};if(name==='@decky/api')return {useQuickAccessVisible:()=>visible};if(name==='./qamPlacement')return {installConsoleTab:()=>({active:false,dispose(){}})};if(name==='./mirrorClient')return {MirrorClient:InertClient};if(name==='../../src/bridge/losslessProgramState')return load(fs.readFileSync(path.join(root,'src/bridge/losslessProgramState.ts'),'utf8'));if(name==='./steamRunningContext')return {observeSteamRunning:()=>({availability:'unavailable'})};throw Error('unmocked '+name);}});
const plugin=module.exports.default();const render=()=>{hi=ri=ei=0;return plugin.content.type();};
function flatten(v,out=[]){if(Array.isArray(v))v.forEach(c=>flatten(c,out));else if(v&&typeof v==='object'){out.push(v);flatten(v.children,out);}return out;}
function text(v){if(Array.isArray(v))return v.map(text).join('');if(v&&typeof v==='object')return text(v.children);return v==null||v===false?'':String(v);}
const controls=()=>flatten(render()).filter(n=>['SliderField','ToggleField','AnchoredDropdown'].includes(n.type)||n.type==='ButtonItem'&&text(n.props.label).startsWith('小黄鸭'));
const expectedProgramLabel=()=>!state.snapshot?.losslessProgram?.known?'小黄鸭状态待确认':state.snapshot.losslessProgram.running?'小黄鸭已运行':'小黄鸭未运行';const find=label=>controls().find(n=>text(n.props.label).startsWith(label));
const flush=async()=>{for(let i=0;i<100;i++)await Promise.resolve();};const advance=async ms=>{now+=ms;for(const [id,t] of [...timers])if(t.due<=now){timers.delete(id);t.action();}await flush();};const emit=()=>{for(const fn of subscriptions)fn(state);};
const host=await createMirrorHostFixture();host.ready();host.emit('deckySidebar.mirrorRequest',{connection:'1',runId:'a'.repeat(32),request:{command:'snapshot',runId:'a'.repeat(32)}});await flush();state.snapshot=host.replies.at(-1).message.snapshot;host.stop();const baseline=structuredClone(state.snapshot);
const selected=process.env.YMCC_PRESENTATION_CASES??'';assert.ok(['','lossless'].includes(selected),'Unknown presentation scenario filter');
const cases=[],skipped=[];async function check(name,fn){if(selected==='lossless'&&!name.startsWith('Lossless ')){skipped.push(name);return;}await fn();cases.push(name);}
const choose=async(label,value)=>{const widget=find(label);widget.props.onMenuWillOpen(()=>{});widget.props.onChange({data:value});await flush();};
await check('console Y uses the native QAM SVG layout without changing tab height or font baseline',()=>{const icon=plugin.icon;assert.equal(icon.type,'svg');assert.equal(icon.props['aria-label'],'野蛮系统控制台');assert.equal(icon.props.role,'img');assert.equal(icon.props.viewBox,'0 0 24 24');assert.equal(icon.props.width,'1em');assert.equal(icon.props.height,'1em');assert.equal(icon.props.fill,'currentColor');assert.equal(icon.props.style,undefined);assert.equal(icon.children.filter(c=>c&&typeof c==='object')[0].type,'path');assert.ok(!icon.children.some(c=>typeof c==='string'&&c.trim()));});
await check('browser timer APIs keep native Window binding and all dropdowns share the same alignment grid',()=>{assert.ok(source.includes('schedule:(action,delay) => setTimeout(action,delay)'));assert.ok(source.includes('cancel:handle => clearTimeout(handle)'));assert.ok(source.includes('grid-template-columns: 76px minmax(0,1fr)'));assert.ok(source.includes('width: calc(100% - 84px)'));assert.ok(!source.includes('width: 64%'));});
await check('power is first, no pure headings or duplicate console title, frame ceiling precedes core and SMT then the fan divider',()=>{const tree=render(),c=controls();assert.ok(text(c[0].props.label).startsWith('交流电方案'));const labels=c.map(n=>text(n.props.label)),ceiling=labels.indexOf('锁帧上限');assert.ok(ceiling>0);assert.deepEqual(labels.slice(ceiling+1,ceiling+4),['核心调度','超线程','风扇控制']);const flat=flatten(tree),divider=flat.findIndex(n=>n.type==='hr'),smt=flat.findIndex(n=>n.type==='ToggleField'&&text(n.props.label)==='超线程'),fan=flat.findIndex(n=>n.type==='SliderField'&&text(n.props.label)==='风扇控制');assert.ok(smt>=0&&divider>smt&&divider<fan);assert.equal(flatten(tree).filter(n=>n.props.className==='ymcc-console-title'||n.props.className==='ymcc-module-title').length,0);assert.equal(text(plugin.titleView),'');assert.equal(flatten(tree).filter(n=>n.type==='hr').length,5);});
await check('original sliders and dropdowns plus independent monitor switch',()=>{const c=controls();assert.equal(c.filter(n=>n.type==='SliderField').length,3);assert.equal(c.filter(n=>n.type==='AnchoredDropdown').length,11);assert.equal(c.filter(n=>n.type==='ToggleField').length,3);assert.equal(plugin.name,'野蛮系统控制台');assert.equal(mutations.length,0);});
await check('core order big first, small last; gyro still exactly four original presets; speed eight original factors',()=>{assert.equal(find('核心调度').props.rgOptions[0].data,'big-small');assert.equal(find('核心调度').props.rgOptions.at(-1).data,'only-small');assert.deepEqual(Array.from(find('陀螺仪预设').props.rgOptions,c=>c.data),['fps','racing','custom','steam']);assert.deepEqual(Array.from(find('游戏变速').props.rgOptions,c=>c.data),['0.5','0.8','1','1.2','1.5','2','4','8']);});
await check('Lossless program lamp and one-click button replace the working-state toggle',()=>{
 const button=find('小黄鸭');assert.equal(button.type,'ButtonItem');assert.equal(text(button.children),'一键缩放');
 assert.equal(button.props.disabled,true);assert.equal(find('LosslessScaling 插帧'),undefined);
 const before=mutations.length;button.props.onClick();assert.equal(mutations.length,before);
});
await check('Lossless running/stopped/unknown labels are independent of EXE and working-state fields',async()=>{
 for(const [program,label,color] of [[{known:true,running:true},'小黄鸭已运行','#4cc38a'],[{known:true,running:false},'小黄鸭未运行','#c4c4c4'],[{known:false,running:false},'小黄鸭状态待确认','#d6a34c'],[undefined,'小黄鸭状态待确认','#d6a34c']]){
  for(const configured of ['on','off','unknown']){
   state.snapshot=structuredClone(baseline);state.snapshot.losslessProgram=program;state.snapshot.game.fields.losslessScaling={value:configured,supported:true,choices:[]};emit();
   const button=find('小黄鸭');assert.equal(text(button.props.label),label);assert.equal(button.props.disabled,false);
   assert.equal(flatten(button.props.label).find(n=>n.props['aria-hidden']==='true').props.style.background,color);
   const count=mutations.length,before=structuredClone(state.snapshot);button.props.onClick();await flush();
   assert.equal(mutations.length,count+1);assert.deepEqual(mutations.at(-1),{command:'game.setField',args:{field:'losslessScaling',value:'on'}});
   assert.deepEqual(state.snapshot,before);render();render();assert.equal(mutations.length,count+1);
  }
 }
 state.snapshot=structuredClone(baseline);emit();
});
await check('Lossless lamp is visible without a game but one-click honors all existing action gates',async()=>{
 for(const patch of [{game:null},{ready:false},{actions:{...baseline.actions,game:false}}]){
  state.snapshot={...structuredClone(baseline),losslessProgram:{known:true,running:true},...patch};emit();const button=find('小黄鸭'),n=mutations.length;
  assert.equal(button.props.disabled,true);if(patch.ready===false)assert.equal(text(button.props.label),'小黄鸭状态待确认');button.props.onClick();await flush();assert.equal(mutations.length,n);
 }
 for(const patch of [{connected:false},{busy:true}]){
  state.snapshot=structuredClone(baseline);state.snapshot.losslessProgram={known:true,running:true};state.snapshot.game.fields.losslessScaling.supported=true;Object.assign(state,patch);emit();
  const button=find('小黄鸭'),n=mutations.length;assert.equal(button.props.disabled,true);if(patch.connected===false)assert.equal(text(button.props.label),'小黄鸭状态待确认');button.props.onClick();await flush();assert.equal(mutations.length,n);
  state.connected=true;state.busy=false;
 }
 state.snapshot=structuredClone(baseline);emit();
});
await check('current source only and unknown source hides both',()=>{state.snapshot.powerSource='dc';assert.ok(find('电池方案'));assert.equal(find('交流电方案'),undefined);state.snapshot.powerSource=null;assert.equal(find('电池方案'),undefined);assert.equal(find('交流电方案'),undefined);state.snapshot.powerSource='ac';});
await check('power selection visibly pending, no eager request, last choice after 3 seconds',async()=>{const w=find('交流电方案'),choices=state.snapshot.game.fields.acMode.choices;w.props.onChange(choices.findIndex(c=>c.data==='performance'));assert.equal(mutations.length,0);assert.match(text(render()),/停止切换 3 秒/);await advance(2500);find('交流电方案').props.onChange(choices.findIndex(c=>c.data==='extreme'));await advance(2999);assert.equal(mutations.length,0);await advance(1);assert.deepEqual(mutations.at(-1),{command:'game.setField',args:{field:'acMode',value:'extreme'}});assert.equal(timers.size,0);});
await check('global sentinel cannot be selected from slider or hand menu',async()=>{const n=mutations.length;const ac=state.snapshot.game.fields.acMode;find('交流电方案').props.onChange(ac.choices.findIndex(c=>c.data==='follow'));const hand=find('虚拟手柄');assert.equal(hand.props.rgOptions.find(c=>c.data==='follow'),undefined);hand.props.onMenuWillOpen(()=>{});hand.props.onChange({data:'follow'});hand.props.onCancel();await flush();assert.equal(mutations.length,n);assert.equal(timers.size,0);});
await check('core and speed dispatch exact original values; opening and cancelling has zero writes',async()=>{const n=mutations.length;find('核心调度').props.onMenuWillOpen(()=>{});find('核心调度').props.onCancel();assert.equal(mutations.length,n);await choose('核心调度','only-small');assert.deepEqual(mutations.at(-1).args,{field:'corePolicyMode',value:'only-small'});await choose('游戏变速','1.5');assert.deepEqual(mutations.at(-1).args,{field:'speedFactor',value:'1.5'});});
await check('native hand, gyro controls and fan original callback mappings',async()=>{await choose('虚拟手柄','elite');assert.deepEqual(mutations.at(-1).args,{field:'padPersona',value:'elite'});find('陀螺仪').props.onChange(true);await flush();assert.deepEqual(mutations.at(-1).args,{field:'gyroEnabled',value:'on'});state.snapshot.game.fields.gyroEnabled.value='on';await choose('陀螺仪预设','racing');assert.deepEqual(mutations.at(-1).args,{field:'gyroPreset',value:'racing'});find('风扇控制').props.onChange(1);await advance(2999);assert.notEqual(mutations.at(-1).command,'fan.setChoice');await advance(1);assert.deepEqual(mutations.at(-1),{command:'fan.setChoice',args:{choice:'soft'}});state.snapshot.fan.enabled=true;emit();find('风扇控制').props.onChange(0);await advance(3000);assert.deepEqual(mutations.at(-1),{command:'fan.setChoice',args:{choice:'off'}});});
await check('disabled gyro cannot open preset, and unknown control values fail closed',()=>{state.snapshot.game.fields.gyroEnabled.value='off';assert.equal(find('陀螺仪预设').props.disabled,true);assert.equal(find('陀螺仪预设').props.onMenuWillOpen(()=>{throw Error('not allowed');}),false);});
await check('late owned menu cannot cross game identity',async()=>{const w=find('核心调度'),old=state.snapshot,n=mutations.length;w.props.onMenuWillOpen(()=>{});state.snapshot=structuredClone(old);state.snapshot.game.identity='different-game';emit();w.props.onChange({data:'only-big'});await flush();assert.equal(mutations.length,n);assert.equal(timers.size,0);state.snapshot=old;emit();});
await check('busy/disconnected/pending/hidden block callbacks and hiding cancels power intent',async()=>{const n=mutations.length;for(const mode of ['busy','disconnected','pending','hidden']){state.busy=mode==='busy';state.connected=mode!=='disconnected';state.snapshot.ready=mode!=='pending';visible=mode!=='hidden';emit();assert.ok(controls().every(w=>mode==='pending'&&text(w.props.label)==='风扇控制'?!w.props.disabled:w.props.disabled));find('核心调度').props.onMenuWillOpen(()=>{throw Error('unexpected');});find('超线程').props.onChange(true);}assert.equal(mutations.length,n);state.busy=false;state.connected=true;state.snapshot.ready=true;visible=true;emit();const ac=state.snapshot.game.fields.acMode;find('交流电方案').props.onChange(ac.choices.findIndex(c=>c.data==='performance'));visible=false;render();await advance(3000);assert.equal(mutations.length,n);visible=true;render();});
await check('selecting the current default core explicitly sends the first dedicated intent; remembered same value remains a no-op',async()=>{const old=state.snapshot,n=mutations.length;state.snapshot=structuredClone(baseline);emit();assert.equal(state.snapshot.game.fields.corePolicyMode.inherited,true);await choose('核心调度','big-small');assert.equal(mutations.length,n+1);assert.deepEqual(mutations.at(-1).args,{field:'corePolicyMode',value:'big-small'});state.snapshot.game.fields.corePolicyMode.inherited=false;await choose('核心调度','big-small');assert.equal(mutations.length,n+1);state.snapshot=old;emit();});
await check('independent FPS shows current AC/DC side only, unlimited disables slider and hides neither globals nor touchpads',async()=>{state.snapshot=structuredClone(baseline);emit();assert.ok(find('插电锁帧 AC'));assert.equal(find('电池锁帧 DC'),undefined);state.snapshot.powerSource='dc';emit();assert.equal(find('插电锁帧 AC'),undefined);assert.ok(find('电池锁帧 DC'));state.snapshot.frames.pair.dc={fps:0,ceiling:0,lastFps:60};emit();assert.equal(find('电池锁帧 DC').props.disabled,true);assert.equal(find('锁帧上限').props.selectedOption,'0');state.snapshot.powerSource=null;emit();assert.equal(find('锁帧上限'),undefined);assert.equal(find('电池锁帧 DC'),undefined);state.snapshot=structuredClone(baseline);emit();});
await check('FPS slider only commits on native completion, ceiling uses original explicit frame command',async()=>{state.snapshot=structuredClone(baseline);emit();const n=mutations.length,slider=find('插电锁帧 AC');slider.props.onChange(90);await flush();assert.equal(mutations.length,n);assert.equal(find('插电锁帧 AC').props.value,90);slider.props.onChangeComplete(90);await flush();assert.deepEqual(mutations.at(-1),{command:'frame.setField',args:{field:'acFps',value:'90'}});await choose('锁帧上限','0');assert.deepEqual(mutations.at(-1),{command:'frame.setField',args:{field:'acCeiling',value:'0'}});state.snapshot=structuredClone(baseline);emit();});
await check('four primary touchpad dropdowns always render, mappings follow original off/single/dual layout',()=>{for(const layout of ['off','single','dual']){state.snapshot=structuredClone(baseline);state.snapshot.touchpads.fields.layout.value=layout;emit();for(const label of ['触摸板','YMCC呼出','专用按键','背部按键'])assert.ok(find(label));assert.equal(!!find('触摸板映射'),layout==='single');assert.equal(!!find('左侧映射'),layout==='dual');assert.equal(!!find('右侧映射'),layout==='dual');}state.snapshot=structuredClone(baseline);emit();});
await check('touchpad dropdown calls only global touchpad command; global controls usable with no game',async()=>{state.snapshot=structuredClone(baseline);state.snapshot.game=null;state.snapshot.actions.game=false;emit();assert.ok(find('触摸板'));assert.ok(find('插电锁帧 AC'));assert.equal(find('核心调度').props.disabled,true);await choose('YMCC呼出','right');assert.deepEqual(mutations.at(-1),{command:'touchpad.setField',args:{field:'summonPosition',value:'right'}});await choose('背部按键','10');assert.deepEqual(mutations.at(-1),{command:'touchpad.setField',args:{field:'rearMask',value:'10'}});state.snapshot=structuredClone(baseline);emit();});
await check('all four global touchpad menus stay unlocked when dedicated and global input actions are unavailable',async()=>{
  for(const withGame of [true,false]){
    state.snapshot=structuredClone(baseline);if(!withGame)state.snapshot.game=null;
    state.snapshot.actions.global=false;state.snapshot.actions.game=false;emit();
    for(const [label,field] of [['触摸板','layout'],['YMCC呼出','summonPosition'],['专用按键','specialMask'],['背部按键','rearMask']]){
      const widget=find(label);assert.equal(widget.props.disabled,false);
      const value=widget.props.rgOptions.find(o=>!o.disabled&&o.data!==widget.props.selectedOption).data;
      const n=mutations.length;await choose(label,value);
      assert.equal(mutations.length,n+1);assert.deepEqual(mutations.at(-1),{command:'touchpad.setField',args:{field,value}});
    }
  }
  assert.equal(timers.size,0);state.snapshot=structuredClone(baseline);emit();
});
await check('unknown/disabled global control permissions prevent frame and touchpad callbacks',async()=>{state.snapshot=structuredClone(baseline);state.snapshot.actions.frames=false;state.snapshot.actions.touchpads=false;emit();const n=mutations.length;assert.equal(find('插电锁帧 AC').props.disabled,true);assert.equal(find('触摸板').props.disabled,true);find('插电锁帧 AC').props.onChangeComplete(90);find('触摸板').props.onMenuWillOpen(()=>{throw Error('unsupported');});await flush();assert.equal(mutations.length,n);state.snapshot=structuredClone(baseline);emit();});
await check('all states retain the compact order and trainer directly below LS without diagnostic footer',async()=>{const saved=state.snapshot;for(const snapshot of [null,structuredClone(baseline),{...structuredClone(baseline),game:null},{...structuredClone(baseline),powerSource:'dc'}]){state.snapshot=snapshot;emit();const c=controls(),labels=c.map(n=>text(n.props.label));assert.deepEqual(labels.slice(-3),['监控总开关','游戏变速',expectedProgramLabel()]);assert.ok(find('风扇控制'));const buttons=flatten(render()).filter(n=>n.type==='ButtonItem');assert.equal(buttons.length,2);assert.equal(text(buttons[0].children),'一键缩放');assert.equal(text(buttons[1].props.label),'修改器');assert.equal(text(buttons[1].children),'未找到修改器');assert.equal(buttons[1].props.disabled,true);assert.equal(flatten(render()).some(n=>n.type==='details'),false);}state.snapshot=saved;emit();});
await check('no-game TDP and virtual controller use original global intents; power retains trailing 3-second delay',async()=>{const f=await createMirrorHostFixture();f.state.game=null;f.ready();f.emit('deckySidebar.mirrorRequest',{connection:'1',runId:'a'.repeat(32),request:{command:'snapshot',runId:'a'.repeat(32)}});await flush();state.snapshot=f.replies.at(-1).message.snapshot;f.stop();emit();assert.ok(find('交流电方案'));assert.ok(find('虚拟手柄'));assert.equal(find('虚拟手柄').props.disabled,false);const n=mutations.length;find('交流电方案').props.onChange(state.snapshot.global.fields.acMode.choices.findIndex(o=>o.data==='performance'));await advance(2999);assert.equal(mutations.length,n);await advance(1);assert.equal(mutations.at(-1).command,'global.setField');await choose('虚拟手柄','elite');assert.deepEqual(mutations.at(-1),{command:'global.setField',args:{field:'padPersona',value:'elite'}});state.snapshot=structuredClone(baseline);emit();});

await check('only a confirmed current-game trainer enables the button directly below LosslessScaling',async()=>{
  state.snapshot=structuredClone(baseline);state.snapshot.trainer={available:true,pending:false,notice:''};emit();
  const all=flatten(render()),button=all.find(n=>n.type==='ButtonItem'&&text(n.props.label)==='修改器'),ls=all.find(n=>n.type==='ButtonItem'&&text(n.props.label).startsWith('小黄鸭'));
  assert(all.indexOf(button)>all.indexOf(ls));assert.equal(button.props.disabled,false);assert.equal(text(button.children),'打开修改器');
  const count=mutations.length;button.props.onClick();await flush();assert.equal(mutations.length,count+1);assert.deepEqual(mutations.at(-1),{command:'game.setField',args:{field:'trainer',value:'open'}});
  state.snapshot=structuredClone(baseline);emit();
});
await check('trainer button does not send intents when missing, disconnected, busy, hidden or without a confirmed game',async()=>{
  const cases=[{trainer:{available:true,pending:true,notice:''}},{trainer:{available:false,pending:false,notice:''}},{trainer:undefined},{game:null,trainer:{available:true,pending:false,notice:''}},{actions:{...baseline.actions,game:false},trainer:{available:true,pending:false,notice:''}},{ready:false,trainer:{available:true,pending:false,notice:''}}];
  for(const patch of cases){state.snapshot={...structuredClone(baseline),...patch};emit();const button=flatten(render()).find(n=>n.type==='ButtonItem'&&text(n.props.label)==='修改器');const count=mutations.length;assert.equal(button.props.disabled,true);button.props.onClick();await flush();assert.equal(mutations.length,count);}
  for(const patch of [{connected:false},{busy:true}]){state.snapshot={...structuredClone(baseline),trainer:{available:true,pending:false,notice:''}};Object.assign(state,patch);emit();const button=flatten(render()).find(n=>n.type==='ButtonItem'&&text(n.props.label)==='修改器'),count=mutations.length;assert.equal(button.props.disabled,true);button.props.onClick();await flush();assert.equal(mutations.length,count);state.connected=true;state.busy=false;}
  state.snapshot={...structuredClone(baseline),trainer:{available:true,pending:false,notice:''}};visible=false;emit();const button=flatten(render()).find(n=>n.type==='ButtonItem'&&text(n.props.label)==='修改器'),count=mutations.length;assert.equal(button.props.disabled,true);button.props.onClick();await flush();assert.equal(mutations.length,count);visible=true;state.snapshot=structuredClone(baseline);emit();
});

await check('only the trainer button waits for its launcher; the monitor control remains interactive',async()=>{
 state.snapshot={...structuredClone(baseline),trainer:{available:true,pending:true,notice:''}};emit();const button=flatten(render()).find(n=>n.type==='ButtonItem'&&text(n.props.label)==='修改器');assert.equal(button.props.disabled,true);assert.equal(text(button.children),'启动中…');assert.equal(find('监控总开关').props.disabled,false);state.snapshot=structuredClone(baseline);emit();
});

// Use production host data for the new disabled-slot capability; no external input/device IO.
const originalScreen=load(fs.readFileSync(path.join(root,'src/bridge/screenTouchpads.ts'),'utf8'),{'./ipc':{isNativeRuntime:false}});
async function independentSnapshot(mode='off',layout='off') {
  const f=await createMirrorHostFixture();f.state.game=null;
  f.state.input.outputTarget={persona:'disabled',buttonMappingEnabled:false};
  f.state.touchpads={...originalScreen.screenTouchpadDefaults('disabled'),ok:true,available:true,visible:false,persona:'disabled',layout,enabled:layout!=='off',steamDeckAvailable:false,ps5Available:false};
  if(mode===null)delete f.state.touchpads.standaloneSpecialMode;else f.state.touchpads.standaloneSpecialMode=mode;
  f.ready();f.emit('deckySidebar.mirrorRequest',{connection:'1',runId:'a'.repeat(32),request:{command:'snapshot',runId:'a'.repeat(32)}});
  await flush();const snapshot=f.replies.at(-1).message.snapshot;f.stop();return snapshot;
}
const specialControls=()=>controls().filter(n=>text(n.props.label)==='专用按键');
await check('disabled persona reuses exactly one of four primary dropdowns and leaves touch mappings unchanged',async()=>{
  for(const layout of ['off','single','dual']) {
    state.snapshot=await independentSnapshot('off',layout);emit();
    for(const label of ['触摸板','YMCC呼出','专用按键','背部按键'])assert.ok(find(label));
    assert.equal(specialControls().length,1);
    assert.deepEqual(Array.from(find('专用按键').props.rgOptions,c=>({data:c.data,label:c.label})),Array.from(originalScreen.STANDALONE_SPECIAL_MODES,c=>({data:c.value,label:c.label})));
    assert.equal(find('专用按键').props.disabled,false);assert.equal(find('背部按键').props.disabled,true);
    assert.equal(!!find('触摸板映射'),layout==='single');assert.equal(!!find('左侧映射'),layout==='dual');assert.equal(!!find('右侧映射'),layout==='dual');
    for(const label of ['触摸板映射','左侧映射','右侧映射'])if(find(label))assert.ok(!find(label).props.rgOptions.some(c=>c.data==='steamdeck'||c.data==='dualsense'));
  }
  state.snapshot=structuredClone(baseline);emit();
});
await check('independent dropdown sends only original global standaloneSpecialMode; selecting same mode is a no-op',async()=>{
  state.snapshot=await independentSnapshot();emit();const start=mutations.length;
  for(const value of ['steamdeck','ps5','off']) {
    await choose('专用按键',value);
    assert.deepEqual(mutations.at(-1),{command:'touchpad.setField',args:{field:'standaloneSpecialMode',value}});
    state.snapshot.touchpads.fields.standaloneSpecialMode.value=value;emit();
  }
  assert.equal(mutations.length,start+3);await choose('专用按键','off');assert.equal(mutations.length,start+3);
  state.snapshot=structuredClone(baseline);emit();
});
await check('independent mode descriptions show the four functional actions without advertising virtual touch reports',async()=>{
  state.snapshot=await independentSnapshot('steamdeck');emit();let output=text(render());
  assert.doesNotMatch(output,/Steam 键：Steam 主菜单；三点键：Steam 快捷菜单/);assert.ok(!output.includes('PS 静音键'));
  state.snapshot=await independentSnapshot('ps5');emit();output=text(render());
  assert.doesNotMatch(output,/PS 主页键：Steam 主菜单；PS 静音键：默认麦克风静音/);assert.ok(!output.includes('三点键：'));
  state.snapshot=await independentSnapshot('off');emit();assert.ok(!text(render()).includes('PS 静音键'));
  state.snapshot=structuredClone(baseline);emit();
});
await check('virtual persona keeps its original specialMask options and callback, with no duplicate independent entry',async()=>{
  state.snapshot=structuredClone(baseline);emit();assert.equal(specialControls().length,1);
  assert.deepEqual(Array.from(find('专用按键').props.rgOptions,c=>c.data),['0','1','2','3']);
  await choose('专用按键','1');assert.deepEqual(mutations.at(-1),{command:'touchpad.setField',args:{field:'specialMask',value:'1'}});
  assert.ok(!text(render()).includes('PS 静音键'));state.snapshot=structuredClone(baseline);emit();
});
await check('unavailable independent backend or permission disables the existing entry without a virtual-controller fallback',async()=>{
  // Missing backend field is tested before the first published snapshot, not by mutating a cached snapshot.
  for(const scenario of ['legacy','invalid','permission']) {
    state.snapshot=await independentSnapshot(scenario==='legacy'?null:scenario==='invalid'?'unknown':'off');
    if(scenario==='permission')state.snapshot.actions.touchpads=false;emit();const n=mutations.length;
    assert.equal(specialControls().length,1);const dropdown=find('专用按键');assert.equal(dropdown.props.disabled,true);
    dropdown.props.onMenuWillOpen(()=>{throw Error('unsupported independent entry opened');});dropdown.props.onChange({data:'steamdeck'});await flush();
    assert.equal(mutations.length,n);assert.equal(state.snapshot.touchpads.fields.specialMask.supported,false);assert.equal(timers.size,0);
  }
  state.snapshot=structuredClone(baseline);emit();
});

await check('virtual target dropdown matches mainline four choices in both no-game and dedicated contexts',async()=>{for(const noGame of [false,true]){state.snapshot=structuredClone(baseline);if(noGame){const f=await createMirrorHostFixture();f.state.game=null;f.ready();f.emit('deckySidebar.mirrorRequest',{connection:'1',runId:'a'.repeat(32),request:{command:'snapshot',runId:'a'.repeat(32)}});await flush();state.snapshot=f.replies.at(-1).message.snapshot;f.stop();}emit();assert.deepEqual(Array.from(find('虚拟手柄').props.rgOptions,o=>o.data),['disabled','steamdeck','dualsense-edge','elite']);}state.snapshot=structuredClone(baseline);emit();});
await check('global gyro switch and four-preset dropdown remain visible without a confirmed game and use global commands',async()=>{const f=await createMirrorHostFixture();f.state.game=null;f.state.input.outputTarget.gyroEnabled=true;f.state.input.gyroMotion={enabled:true,preset:'fps'};f.ready();f.emit('deckySidebar.mirrorRequest',{connection:'1',runId:'a'.repeat(32),request:{command:'snapshot',runId:'a'.repeat(32)}});await flush();state.snapshot=f.replies.at(-1).message.snapshot;f.stop();emit();assert.ok(find('陀螺仪'));assert.ok(find('陀螺仪预设'));assert.equal(controls().filter(n=>text(n.props.label)==='陀螺仪').length,1);assert.deepEqual(Array.from(find('陀螺仪预设').props.rgOptions,c=>c.data),['fps','racing','custom','steam']);await choose('陀螺仪预设','racing');assert.deepEqual(mutations.at(-1),{command:'global.setField',args:{field:'gyroPreset',value:'racing'}});find('陀螺仪').props.onChange(false);await flush();assert.deepEqual(mutations.at(-1),{command:'global.setField',args:{field:'gyroEnabled',value:'off'}});state.snapshot=structuredClone(baseline);emit();});
await check('pending fan stays selected and interactive without timeout rollback',async()=>{state.snapshot=structuredClone(baseline);Object.assign(state.snapshot.fan,{enabled:true,supported:false,pending:true,notice:'等待 YMCC 风扇真实信号'});emit();assert.equal(find('风扇控制').props.value,2);assert.equal(find('风扇控制').props.disabled,false);assert.equal(find('虚拟手柄').props.disabled,false);assert.equal(find('交流电方案').props.disabled,false);const before=mutations.length;assert.match(text(render()),/等待 YMCC 风扇真实信号/);await advance(60000);assert.equal(mutations.length,before);assert.equal(find('风扇控制').props.value,2);state.snapshot=structuredClone(baseline);emit();});

await check('one independent monitor switch precedes the adjacent speed and LS controls with thin dividers',async()=>{
  state.snapshot=structuredClone(baseline);emit();const nodes=flatten(render());
  const switches=nodes.filter(n=>n.type==='ToggleField'&&text(n.props.label)==='监控总开关');assert.equal(switches.length,1);
  const widget=switches[0];assert.equal(widget.props.checked,baseline.monitor.enabled);assert.equal(widget.props.disabled,false);assert.equal(widget.props.description,undefined);
  const ordered=controls().map(n=>text(n.props.label));assert.deepEqual(ordered.slice(-3),['监控总开关','游戏变速',expectedProgramLabel()]);assert.equal(nodes.filter(n=>n.type==='hr').length,5);const separators=nodes.map((n,i)=>n.type==='hr'?i:-1).filter(i=>i>=0),at=label=>nodes.findIndex(n=>['SliderField','ToggleField','AnchoredDropdown','ButtonItem'].includes(n.type)&&text(n.props.label)===label);assert.ok(at('风扇控制')<separators[1]&&separators[1]<at('虚拟手柄'));assert.ok(at('陀螺仪预设')<separators[2]&&separators[2]<at('触摸板'));assert.ok(separators[3]<at('监控总开关')&&at('监控总开关')<separators[4]&&separators[4]<at('游戏变速'));assert.equal(nodes.slice(at('游戏变速'),at(expectedProgramLabel())).some(n=>n.type==='hr'),false);
  const before=structuredClone(state.snapshot),count=mutations.length;widget.props.onChange(false);await flush();
  assert.equal(mutations.length,count+1);assert.deepEqual(mutations.at(-1),{command:'monitor.setEnabled',args:{enabled:false}});assert.deepEqual(state.snapshot,before);
});
await check('monitor is independent of game/input permissions and ignores unchanged or malformed values',async()=>{
  for(const hasGame of [true,false]){state.snapshot=structuredClone(baseline);if(!hasGame)state.snapshot.game=null;state.snapshot.actions.game=false;state.snapshot.actions.global=false;state.snapshot.actions.fan=false;emit();
    const widget=find('监控总开关');assert.equal(widget.props.disabled,false);const count=mutations.length;
    widget.props.onChange(widget.props.checked);widget.props.onChange('off');assert.equal(mutations.length,count);
    widget.props.onChange(!widget.props.checked);await flush();assert.deepEqual(mutations.at(-1),{command:'monitor.setEnabled',args:{enabled:!widget.props.checked}});
  }state.snapshot=structuredClone(baseline);emit();
});
await check('unknown monitor state or permission disables only the monitor switch',async()=>{
  for(const mode of ['state','permission','denied']){state.snapshot=structuredClone(baseline);if(mode==='state')delete state.snapshot.monitor;else if(mode==='permission')delete state.snapshot.actions.monitor;else state.snapshot.actions.monitor=false;emit();
    const count=mutations.length,widget=find('监控总开关');assert.equal(widget.props.disabled,true);widget.props.onChange(false);await flush();assert.equal(mutations.length,count);
  }state.snapshot=structuredClone(baseline);emit();
});
await check('four fan positions restart the 3s deadline and collapse to one final intent',async()=>{state.snapshot=structuredClone(baseline);emit();const n=mutations.length,w=find('风扇控制');assert.equal(w.props.min,0);assert.equal(w.props.max,3);assert.equal(w.props.notchCount,4);w.props.onChange(1);assert.doesNotMatch(text(render()),/风扇挡位待应用|停止切换 3 秒/);await advance(2000);find('风扇控制').props.onChange(3);await advance(2999);assert.equal(mutations.length,n);await advance(1);assert.deepEqual(mutations.at(-1),{command:'fan.setChoice',args:{choice:'aggressive'}});assert.equal(mutations.length,n+1);assert.equal(timers.size,0);});
await check('six power positions remain stable and selecting an inherited current mode records a dedicated intent',async()=>{state.snapshot=structuredClone(baseline);emit();const modes=state.snapshot.game.fields.acMode.choices;assert.deepEqual(Array.from(modes,o=>o.data),['eco','balanced','medium','performance','elite','extreme']);assert.equal(find('交流电方案').props.notchCount,6);const n=mutations.length;find('交流电方案').props.onChange(modes.findIndex(o=>o.data===state.snapshot.game.fields.acMode.value));await advance(3000);assert.equal(mutations.length,n+1);assert.equal(mutations.at(-1).command,'game.setField');});
await check('shared-not-ready leaves only the explicitly admitted Fan intent interactive',async()=>{
  state.snapshot=structuredClone(baseline);state.snapshot.ready=false;state.snapshot.fan.enabled=false;emit();
  const n=mutations.length;assert.equal(find('风扇控制').props.disabled,false);
  for(const label of ['交流电方案','核心调度','监控总开关'])assert.equal(find(label).props.disabled,true);
  find('交流电方案').props.onChange(5);find('监控总开关').props.onChange(true);await advance(3000);assert.equal(mutations.length,n);
  find('风扇控制').props.onChange(3);await advance(2999);assert.equal(mutations.length,n);await advance(1);
  assert.deepEqual(mutations.at(-1),{command:'fan.setChoice',args:{choice:'aggressive'}});assert.equal(mutations.length,n+1);
  state.snapshot=structuredClone(baseline);emit();
});
await check('Fan delay survives shared readiness loss and still submits one explicit choice',async()=>{
  state.snapshot=structuredClone(baseline);state.snapshot.fan.enabled=false;emit();const n=mutations.length;
  find('风扇控制').props.onChange(1);state.snapshot.ready=false;emit();await advance(3000);
  assert.equal(mutations.length,n+1);assert.deepEqual(mutations.at(-1),{command:'fan.setChoice',args:{choice:'soft'}});
  state.snapshot=structuredClone(baseline);emit();
});
await check('independent Fan UI still rejects busy disconnected hidden or revoked controls',async()=>{
  for(const reason of ['busy','disconnected','hidden','action','toggle']){
    state.snapshot=structuredClone(baseline);state.snapshot.ready=false;state.busy=reason==='busy';state.connected=reason!=='disconnected';visible=reason!=='hidden';
    if(reason==='action')state.snapshot.actions.fan=false;if(reason==='toggle')state.snapshot.fan.canToggle=false;emit();
    const n=mutations.length,w=find('风扇控制');assert.equal(w.props.disabled,true);w.props.onChange(3);await advance(3000);assert.equal(mutations.length,n);
  }
  state.busy=false;state.connected=true;visible=true;state.snapshot=structuredClone(baseline);emit();
});
await check('menu bounded lease expires and cleanup removes all owned timers/listeners',async()=>{state.snapshot=structuredClone(baseline);emit();find('核心调度').props.onMenuWillOpen(()=>{});assert.equal(timers.size,1);await advance(45000);assert.equal(timers.size,0);find('交流电方案').props.onChange(state.snapshot.game.fields.acMode.choices.findIndex(c=>c.data==='performance'));for(const c of cleanups)c?.stop?.();plugin.onDismount();assert.equal(timers.size,0);assert.equal(subscriptions.size,0);assert.equal(events.size,0);assert.equal(disposed,1);});
if(selected){for(const c of cleanups)c?.stop?.();plugin.onDismount();assert.equal(timers.size,0);assert.equal(subscriptions.size,0);assert.equal(events.size,0);}
fs.writeFileSync(path.join(task,'validation/MAINLINE22-PLUGIN-PRESENTATION.json'),JSON.stringify({selected:selected||'all',fullPresentationSuite:!selected,skipped, cases:cases.length,results:cases,exactProductionTsx:true,actualHostSnapshotData:true,inertReact:true,actualSteam:false,realConfigWrites:0,hardwareWrites:0},null,2));console.log('Plugin presentation PASS '+cases.length);
