import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import { createRequire } from 'node:module';
const require = createRequire(path.join(process.cwd(), 'package.json'));
const vue = require('vue');
const { parse, compileScript } = require('vue/compiler-sfc');
const { transformSync } = require('esbuild');
const root = process.cwd(), cases = [];
async function check(name, run) { await run(); cases.push(name); }
function evaluate(source, imports = {}, globals = {}) {
  const module = { exports: {} };
  vm.runInNewContext(transformSync(source, { loader: 'ts', format: 'cjs' }).code, {
    module, exports: module.exports, console, structuredClone, TextEncoder, performance,
    require(name) { if (name === 'vue') return vue; if (name in imports) return imports[name]; throw Error('Unexpected import ' + name); },
    ...globals,
  });
  return module.exports;
}
function load(file, imports = {}, globals = {}) { return evaluate(fs.readFileSync(path.join(root, file), 'utf8'), imports, globals); }
function component(file, imports, globals) {
  const { descriptor, errors } = parse(fs.readFileSync(path.join(root, file), 'utf8'), { filename: file });
  assert.equal(errors.length, 0);
  return evaluate(compileScript(descriptor, { id: 'controller-shortcut-merge-selftest', inlineTemplate: true,
    templateOptions: { compilerOptions: { hoistStatic: false } } }).content, imports, globals).default;
}
const rawSchema = load('src/bridge/controllerShortcutRules.ts');
// Vue's reactive Array.map has a cross-realm fallback. Return schema results
// into the host realm, matching production's single JS realm (not fake saves).
const schema = Object.fromEntries(Object.entries(rawSchema).map(([key,value]) => [key,
  typeof value === 'function' ? (...args) => {
    const result = value(...args); return result && typeof result === 'object' ? structuredClone(result) : result;
  } : value]));
const hash = load('src/bridge/inputConfigHash.ts');
const hc = load('src/bridge/hcInputUtils.ts');
const repo = load('src/bridge/settingsRepository.ts', { './settingsSnapshot': load('src/bridge/settingsSnapshot.ts'), './api': {}, './quickAppDefaults': load('src/bridge/quickAppDefaults.ts'), './inputStartupPolicy': load('src/bridge/inputStartupPolicy.ts'), './inputConfigHash': hash, './hcInputUtils': hc });
const outputStatus = load('src/bridge/virtualOutputStatus.ts');
function element(type, text = '') {
  return { type, text, props: {}, children: [], parent: null,
    get textContent() { return this.text + this.children.map(child => child.textContent).join(''); },
    querySelector(selector) { return descendants(this).find(node => selector === '.shortcut-toggle' && hasClass(node, 'shortcut-toggle')) || null; },
  };
}
const memoryBody = element('body');
const renderer = vue.createRenderer({
  querySelector: selector => selector === 'body' ? memoryBody : null,
  createElement: type => element(type), createText: text => element('#text', text), createComment: text => element('#comment', text),
  setText: (node, text) => { node.text = text; }, setElementText: (node, text) => { node.text = text; node.children = []; },
  parentNode: node => node.parent, nextSibling: node => node.parent?.children[node.parent.children.indexOf(node) + 1] || null,
  insert(node, parent, anchor = null) { if (node.parent) node.parent.children.splice(node.parent.children.indexOf(node), 1); node.parent = parent;
    const index = anchor ? parent.children.indexOf(anchor) : -1; if (index < 0) parent.children.push(node); else parent.children.splice(index, 0, node); },
  remove(node) { if (node.parent) node.parent.children.splice(node.parent.children.indexOf(node), 1); node.parent = null; },
  patchProp(node, key, previous, next) { node.props[key] = next; },
});
const descendants = node => [node, ...node.children.flatMap(descendants)];
const hasClass = (node, cls) => String(node.props.class || '').split(/\s+/).includes(cls);
async function flush() { for (let i = 0; i < 35; i++) { await Promise.resolve(); await vue.nextTick(); } }
async function fixture(options = {}) {
  const custom = { ...schema.newControllerShortcutRule('custom-oem', 'keyboard.numeric'), origin: 'custom',
    inputs: [{ source: 'oem', code: 'm1' }], params: { key: 'F8' } };
  const native = schema.NATIVE_DEFAULT_SHORTCUTS.map(d => schema.nativeDefaultRule(d.defaultKey));
  let settings = structuredClone(repo.normalizeSettings({ input: {
    outputTarget: { persona: options.persona || 'steamdeck', buttonMappingEnabled: true,
      oemRearMap: { m1: 'left', m2: 'right', unknownOldKey: 'both' } },
    buttonMapping: { rules: { schemaVersion: 2, shortcutRules: [...native, custom, ...(options.hiddenRules || [])], customUnknownField: 'preserve-me' } },
  } }));
  let locked = { locked: !!options.locked, identity: options.locked ? 'game-A' : '' }, lockListener;
  if (options.locked) settings.input.gameOverride = { identity:'game-A',sessionId:'current',outputTarget:{persona:'elite'},gyroMotion:{enabled:true} };
  let failSave = false, saveCount = 0, nativeSaves = 0, joyCalls = 0, editorMounts = 0, editorUnmounts = 0, focused = null;
  let gamepad = { enabled:true, mouseBackend:'joyxoff', ...Object.fromEntries(schema.NATIVE_DEFAULT_SHORTCUTS.map(d => [d.defaultKey, true])) };
  const events = new Map(), nativeEvents = new Map();
  const window = {
    addEventListener(name, cb) { if (!events.has(name)) events.set(name, new Set()); events.get(name).add(cb); },
    removeEventListener(name, cb) { events.get(name)?.delete(cb); },
    dispatchEvent(event) { for (const cb of [...(events.get(event.type) || [])]) cb(event); },
    setTimeout, clearTimeout,
  };
  const globals = { window, document: { activeElement:null, addEventListener(){},removeEventListener(){} },
    CustomEvent: class { constructor(type, options) { this.type = type; this.detail = options?.detail; } } };
  const repository = { loadSettings: async () => structuredClone(settings),
    async compareAndSwapInputSettings(revision, patch) { saveCount++; const result = repo.evaluateInputCasWrite(settings.input,revision,patch,!failSave);
      if (result.ok) settings.input = structuredClone(result.value); return result; } };
  const stub = { setup: () => () => vue.h('div') }, icon = { setup: () => () => vue.h('span') };
  let mouseState = { ok:true, available:options.mouseAvailable !== false, percent:options.mousePercent || 100, pending:false, steamRunning:true, liveAvailable:!!options.mouseLive,
    ...(options.mouseAvailable === false ? {reason:'desktop-autosave-not-selected'} : {}) };
  let mouseGets = 0, mouseSets = [];
  const mouseBridge = load('src/bridge/steamDeckMouse.ts', { './ipc': { async invoke(command,args) {
    if(command==='steam.settings.get' && args.scope==='mouse'){mouseGets++;return structuredClone(mouseState);}
    args={...args,percent:args.mousePercent};
    mouseSets.push(args.percent);
    if(options.mouseDelay)await new Promise(resolve=>setTimeout(resolve,options.mouseDelay));
    if(options.mouseFail)return {ok:false,reason:'write-failed'};
    mouseState=options.mouseLive?{...mouseState,pending:false,percent:args.percent,appliedLive:true}:{...mouseState,pending:true,desiredPercent:args.percent};return structuredClone(mouseState);
  } } });
  const slider = component('src/components/Slider.vue', {'./InlineIcon.vue':{__esModule:true,default:icon}},globals);
  const sensitivity = component('src/components/SteamDeckMouseSensitivity.vue', {
    '@/components/Slider.vue':{__esModule:true,default:slider}, '@/bridge/steamDeckMouse':mouseBridge,
    '@/bridge/ipc':{on(name,cb){nativeEvents.set(name,cb);return()=>nativeEvents.delete(name);}},
  },globals);
  const card = component('src/components/ControllerShortcutsCard.vue', {
    '@/components/GamepadVisualizer.vue': { __esModule:true,default:stub }, '@/components/InlineIcon.vue': { __esModule:true,default:icon },
    '@/bridge/yeman': { summonGet:async()=>structuredClone(gamepad),summonSet:async patch=>{nativeSaves++;gamepad={...gamepad,...patch};return structuredClone(gamepad);} },
    '@/bridge/gameproc': { getGameInputRedistState:async()=>({mouseInterfaceAvailable:true}), installGameInputRedist:async()=>{},
      setMouseBackend:async backend=>({ok:true,backend,on:true}) },
    '@/gamepad/focus': { focusGamepadElement:node=>{focused=node;},getGamepadPopupPlacement:()=>({style:{},above:false}) },
    '@/bridge/settingsRepository': repository, '@/bridge/controllerShortcutRules': schema,
  }, globals);
  // The existing editor itself is unchanged; this stub tests the real page's
  // open/close/KeepAlive ownership wiring without native recording or Teleport.
  const editor = { emits:['close'], setup(_, { emit }) { editorMounts++;vue.onBeforeUnmount(()=>{editorUnmounts++;});
    return ()=>vue.h('div',{'data-editor-fixture':true},[vue.h('button',{onClick:()=>emit('close')},'关闭编辑器')]); } };
  const page = component('src/views/ButtonMappingView.vue', {
    '@/components/ControllerShortcutsCard.vue': { __esModule:true,default:card },
    '@/components/SteamDeckMouseSensitivity.vue': {__esModule:true,default:sensitivity},
    '@/components/ControllerFeedbackCapabilitiesCard.vue': { __esModule:true,default:stub },
    '@/components/ScreenTouchpadsSettings.vue': { __esModule:true,default:stub },
    '@/views/ControllerShortcutEditorView.vue': { __esModule:true,default:editor },
    '@/components/InlineIcon.vue': { __esModule:true,default:icon },
    '@/bridge/settingsRepository': repository,
    '@/bridge/ipc': { on(name, cb) { nativeEvents.set(name,cb);return()=>nativeEvents.delete(name); },async invoke(command){if(command==='input.shortcutRuntime.clear')nativeEvents.get('input.shortcutRuntime')?.({active:false});return {active:false};} }, '@/bridge/api':{shell:{open:async()=>{}}},
    '@/bridge/gameInputOverride': { getGameInputOverrideState:()=>locked,subscribeGameInputOverrideState(cb){lockListener=cb;cb(locked);return()=>{lockListener=null;};} },
    '@/bridge/gameproc': { closeJoyxoffIfRunning:async()=>{joyCalls++;} }, '@/bridge/virtualOutputStatus': outputStatus,
  }, globals);
  const shown=vue.ref(true), wrapper={setup:()=>()=>vue.h(vue.KeepAlive,null,()=>shown.value ? vue.h(page) : null)};
  const container=element('root'),app=renderer.createApp(wrapper);app.mount(container);await flush();
  return {container,events,nativeEvents,window, get settings(){return settings;},get saveCount(){return saveCount;},get nativeSaves(){return nativeSaves;},
    get editorMounts(){return editorMounts;},get editorUnmounts(){return editorUnmounts;},get focused(){return focused;},get joyCalls(){return joyCalls;},get mouseGets(){return mouseGets;},get mouseSets(){return mouseSets;},
    set failSave(value){failSave=value;},async setShown(value){shown.value=value;await flush();},
    async updateRule(key) {const rule=settings.input.buttonMapping.rules.shortcutRules.find(r=>r.id===custom.id);rule.inputs=[{source:'oem',code:key}];
      window.dispatchEvent({type:'ipc:controller-shortcuts.updated'});await flush();},
    async setLock(value){locked={locked:value,identity:value?'game-A':''};lockListener?.(locked);await flush();},
    unmount(){app.unmount();},
  };
}
await check('睡眠优化完全移除Steam状态小字，修复开关及保存逻辑保留',()=>{
  const source=fs.readFileSync(path.join(root,'src/views/SleepGuardView.vue'),'utf8');
  const {descriptor,errors}=parse(source,{filename:'SleepGuardView.vue'});assert.equal(errors.length,0);
  assert(!source.includes('SteamOverlayFixStatus'));
  assert(!source.includes('steam-overlay-fix-status'));
  assert(!source.includes('无需重启 Steam'));
  assert(source.includes(':model-value="cfg.steamOverlayOffFix"'));
  assert(source.includes('label="Steam大屏唤醒手柄卡死修复"'));
  assert(source.includes('@update:model-value="onSteamOverlayOffFix"'));
  assert(source.includes('steamOverlayOffFix: v'));
  assert(source.includes('steamOverlayOffFix: false'));
  assert(source.includes('description="Steam设置-游戏中-Steam叠加画面关闭"'));
  assert(source.includes('影响Steam监控和触摸板转盘显示'));
  assert(source.includes('学习版手柄唤醒容易卡界面'));
  assert(source.includes("? '确认开启' : '确认关闭'"));
  assert(source.includes('class="reset-confirm steam-overlay-confirm"'));
  assert(source.includes('data-gp-group="steam-overlay-confirm"'));
  const compiled=compileScript(descriptor,{id:'sleep-steam-status-removed'});
  assert(!compiled.content.includes('steamOverlayFixGet'));
  assert(!compiled.content.includes('steamOverlayFix.updated'));
});
const pageSource=fs.readFileSync(path.join(root,'src/views/ButtonMappingView.vue'),'utf8');
const cardSource=fs.readFileSync(path.join(root,'src/components/ControllerShortcutsCard.vue'),'utf8');
await check('SteamDeck虚拟手柄才显示灵敏度，范围1–300默认100，5%步进且挂载只读',async()=>{
  const f=await fixture();const range=descendants(f.container).find(n=>n.type==='input'&&n.props.type==='range');
  assert(range);assert.equal(range.props.min,1);assert.equal(range.props.max,300);assert.equal(range.props.step,'any');assert.equal(Number(range.props.value),100);
  assert(f.mouseGets>0);assert.equal(f.mouseSets.length,0);assert.equal(f.saveCount,0);
  assert(!descendants(f.container).some(n=>hasClass(n,'sensitivity-footer')));
  assert(!descendants(f.container).some(n=>n.type==='button'&&n.textContent==='恢复100%'));
  assert(!f.container.textContent.includes('默认100%'));f.unmount();
  const other=await fixture({persona:'dualsense-edge'});assert(!descendants(other.container).some(n=>hasClass(n,'steamdeck-mouse-bubble')));assert.equal(other.mouseGets,0);other.unmount();
});
await check('5%拖动网格保持1%与300%端点可达，不产生1/6/11的偏移',async()=>{
  const f=await fixture({mouseLive:true});const range=descendants(f.container).find(n=>n.type==='input'&&n.props.type==='range');
  for(const [raw,expected] of [[1,1],[5,5],[96,95],[100,100],[101,100],[105,105],[137,135],[296,295],[300,300]]){
    const event={target:{value:String(raw)}};range.props.onInput(event);await flush();assert.equal(Number(range.props.value),expected);assert.equal(Number(event.target.value),expected);
    range.props.onChange(event);await flush();assert.equal(f.mouseSets.at(-1),expected);
  }
  assert.deepEqual(f.mouseSets,[1,5,95,100,100,105,135,295,300]);f.unmount();
});
await check('SteamDeck键盘与手柄方向调节每步5%，不套用通用滑块的双倍加速',async()=>{
  const f=await fixture({mouseLive:true});const range=descendants(f.container).find(n=>n.type==='input'&&n.props.type==='range');
  for(const [key,expected] of [['ArrowRight',105],['ArrowRight',110],['ArrowLeft',105],['ArrowLeft',100],['ArrowDown',95],['ArrowUp',100]]){
    let prevented=false;range.props.onKeydown({key,preventDefault(){prevented=true;}});await flush();assert(prevented);assert.equal(Number(range.props.value),expected);
    range.props.onKeyup({key});await flush();assert.equal(f.mouseSets.at(-1),expected);
  }
  assert.deepEqual(f.mouseSets,[105,110,105,100,95,100]);f.unmount();
});
await check('真实137%读回保持原样，仅在用户操作时对齐5%网格',async()=>{
  for(const [key,expected] of [['ArrowRight',140],['ArrowLeft',135]]){
    const f=await fixture({mouseLive:true,mousePercent:137});const range=descendants(f.container).find(n=>n.type==='input'&&n.props.type==='range');
    assert.equal(Number(range.props.value),137);assert.equal(f.mouseSets.length,0);
    range.props.onKeydown({key,preventDefault(){}});await flush();assert.equal(Number(range.props.value),expected);
    range.props.onKeyup({key});await flush();assert.deepEqual(f.mouseSets,[expected]);f.unmount();
  }
});
await check('未指定步进原点的其他滑块保持原生step与原有键盘加速',async()=>{
  const icon={setup:()=>()=>vue.h('span')};const slider=component('src/components/Slider.vue',{'./InlineIcon.vue':{__esModule:true,default:icon}});
  const value=vue.ref(100),writes=[];const container=element('root');const app=renderer.createApp({setup:()=>()=>vue.h(slider,{modelValue:value.value,min:0,max:300,step:5,'onUpdate:modelValue':v=>{value.value=v;},onCommit:v=>writes.push(v)})});app.mount(container);await flush();
  const range=descendants(container).find(n=>n.type==='input');assert.equal(range.props.step,5);range.props.onKeydown({key:'ArrowRight',preventDefault(){}});await flush();assert.equal(Number(range.props.value),110);range.props.onKeyup({key:'ArrowRight'});await flush();assert.deepEqual(writes,[110]);app.unmount();
});
await check('后端仅微软鼠标/JoyXoff/SteamDeck鼠标三项，SteamDeck鼠标不可点击且只读高亮',async()=>{
  const f=await fixture();const row=descendants(f.container).find(n=>hasClass(n,'mouse-backend-buttons'));
  const buttons=row.children.filter(n=>n.type==='button');assert.deepEqual(buttons.map(n=>n.textContent),['微软鼠标','JoyXoff','SteamDeck鼠标']);
  assert.equal(buttons.length,3);assert.notEqual(buttons[2].props.disabled,undefined);assert(!buttons[2].props.onClick);assert(hasClass(buttons[2],'active'));
  assert(buttons[0].props.disabled);assert(buttons[1].props.disabled);assert(!hasClass(buttons[1],'active'));f.unmount();
});
await check('灵敏度仅由滑块提交，排队提示精简且不改input人格或快捷设置',async()=>{
  const f=await fixture({mousePercent:145});const before=JSON.stringify(f.settings.input);
  const range=descendants(f.container).find(n=>n.type==='input'&&n.props.type==='range');assert.equal(Number(range.props.value),145);
  range.props.onInput({target:{value:'300'}});range.props.onChange({target:{value:'300'}});await flush();
  assert.deepEqual(f.mouseSets,[300]);
  assert.equal(descendants(f.container).find(n=>hasClass(n,'sensitivity-hint')).textContent,'已排队300% 重新启动Steam后应用');
  range.props.onInput({target:{value:'100'}});range.props.onChange({target:{value:'100'}});await flush();assert.deepEqual(f.mouseSets,[300,100]);
  assert.equal(descendants(f.container).find(n=>hasClass(n,'sensitivity-hint')).textContent,'已排队100% 重新启动Steam后应用');
  assert.equal(JSON.stringify(f.settings.input),before);assert.equal(f.saveCount,0);assert.equal(f.nativeSaves,0);f.unmount();
});
await check('实时滑块拖动180ms去抖，只提交最后值并显示Steam实时回执',async()=>{
  const f=await fixture({mouseLive:true});const range=descendants(f.container).find(n=>n.type==='input'&&n.props.type==='range');
  for(const value of [119,130,137])range.props.onInput({target:{value:String(value)}});
  await new Promise(resolve=>setTimeout(resolve,240));await flush();assert.deepEqual(f.mouseSets,[135]);assert(f.container.textContent.includes('已通过 Steam 实时应用并保存'));f.unmount();
});
await check('实时请求未完成时滑块仍可拖动，串行保存只保留最后一个目标',async()=>{
  const f=await fixture({mouseLive:true,mouseDelay:250});const range=descendants(f.container).find(n=>n.type==='input'&&n.props.type==='range');
  range.props.onChange({target:{value:'137'}});await flush();assert(!range.props.disabled);
  range.props.onInput({target:{value:'200'}});range.props.onChange({target:{value:'200'}});
  range.props.onInput({target:{value:'250'}});range.props.onChange({target:{value:'250'}});
  await new Promise(resolve=>setTimeout(resolve,570));await flush();assert.deepEqual(f.mouseSets,[135,250]);assert.equal(Number(range.props.value),250);assert(f.container.textContent.includes('已通过 Steam 实时应用并保存'));f.unmount();
});
await check('退出页面或游戏接管取消尚未发送的拖动请求',async()=>{
  for(const locked of [false,true]){const f=await fixture({mouseLive:true});const range=descendants(f.container).find(n=>n.type==='input'&&n.props.type==='range');range.props.onInput({target:{value:'137'}});if(locked)await f.setLock(true);else f.unmount();await new Promise(resolve=>setTimeout(resolve,220));assert.equal(f.mouseSets.length,0);if(locked)f.unmount();}
});
await check('既有323%仅显示超界提示，不在挂载时改成300%或100%',async()=>{
  const f=await fixture({mouseLive:true,mousePercent:323});assert(f.container.textContent.includes('超出滑块范围'));assert.equal(f.mouseSets.length,0);f.unmount();
});
await check('异步状态事件更新真实文件值，不留下读取中禁用状态',async()=>{
  const f=await fixture({mouseLive:true});f.nativeEvents.get('steamDeckMouse.updated')({ok:true,available:true,percent:137,pending:false,steamRunning:true,liveAvailable:true,appliedLive:true});await flush();const range=descendants(f.container).find(n=>n.type==='input'&&n.props.type==='range');assert.equal(Number(range.props.value),137);assert(!range.props.disabled);f.unmount();
});
await check('未确认桌面布局或游戏接管时滑块不可达，失败恢复读回值',async()=>{
  const missing=await fixture({mouseAvailable:false});const a=descendants(missing.container).find(n=>n.type==='input'&&n.props.type==='range');assert(a.props.disabled);
  a.props.onChange({target:{value:'200'}});await flush();assert.equal(missing.mouseSets.length,0);assert(missing.container.textContent.includes('桌面'));missing.unmount();
  const locked=await fixture({locked:true});const b=descendants(locked.container).find(n=>n.type==='input'&&n.props.type==='range');assert(b.props.disabled);locked.unmount();
  const failed=await fixture({mouseFail:true,mousePercent:145});const c=descendants(failed.container).find(n=>n.type==='input'&&n.props.type==='range');c.props.onInput({target:{value:'200'}});c.props.onChange({target:{value:'200'}});await flush();
  assert.equal(Number(c.props.value),145);assert(failed.container.textContent.includes('保存失败'));failed.unmount();
});
await check('SteamDeck成功自愈事件清除上一次保存失败，不让本地错误遮挡成功状态',async()=>{
  const f=await fixture({mouseFail:true,mousePercent:145});const range=descendants(f.container).find(n=>n.type==='input'&&n.props.type==='range');
  range.props.onInput({target:{value:'200'}});range.props.onChange({target:{value:'200'}});await flush();
  assert(f.container.textContent.includes('保存失败'));
  f.nativeEvents.get('steamDeckMouse.updated')({ok:true,available:true,percent:200,pending:false,steamRunning:true,liveAvailable:true,appliedLive:true});await flush();
  assert(!f.container.textContent.includes('保存失败'));assert(f.container.textContent.includes('已通过 Steam 实时应用'));assert.equal(Number(range.props.value),200);f.unmount();
});
await check('另一个Steam账号只显示保留待办，不将原账号值写入新布局',async()=>{
  const f=await fixture({mouseLive:true});f.nativeEvents.get('steamDeckMouse.updated')({ok:true,available:false,percent:100,pending:false,waitingAccount:true,steamRunning:true});await flush();
  assert(f.container.textContent.includes('待办已保留'));assert(!f.container.textContent.includes('已取消'));assert.equal(f.mouseSets.length,0);f.unmount();
});
await check('自定义规则省略空defaultKey以通过严格JSON配置哈希，默认规则标识保留',()=>{
  const normalized=schema.normalizeControllerShortcutRule(schema.newControllerShortcutRule('custom','keyboard.numeric'),0);
  assert(!Object.hasOwn(normalized,'defaultKey'));
  const native=schema.normalizeControllerShortcutRule(schema.nativeDefaultRule('bDoubleMinimize'),0);
  assert.equal(native.defaultKey,'bDoubleMinimize');
  const current=structuredClone(repo.normalizeSettings({}).input);
  const result=repo.evaluateInputCas(current,current.revision,{buttonMapping:{rules:{shortcutRules:[normalized,native]}}});
  assert(result.ok);assert.equal(result.value.buttonMapping.rules.shortcutRules.length,2);
});
await check('独立OEM背键映射UI、数据读取和保存入口整体移除',()=>{
  for(const removed of ['rear-map-block','showRearMapEditor','rearMapUnavailable','saveRearMap','oemKeysGet','oemRearMap','OEM → 虚拟背键'])assert(!pageSource.includes(removed),removed);
});
await check('独立自定义快捷规则卡和两段说明文案不再存在',()=>{
  for(const removed of ['editor-entry-card','editorStatus','编辑器将在主窗口外层以居中气泡打开','在主窗口外层的全屏气泡中组合实体按键'])assert(!pageSource.includes(removed),removed);
  assert(!cardSource.includes('card-subtitle'));assert(!cardSource.includes('同源的真实规则'));assert(!cardSource.includes('点选卡片即开/关并保存'));
  assert(!cardSource.includes('在下方「自定义快捷规则」'));
});
await check('真实页面只有一个编辑器入口，位于快捷卡标题右侧同一头部',async()=>{
  const f=await fixture();const nodes=descendants(f.container),button=nodes.find(n=>hasClass(n,'shortcut-editor-open'));
  assert(button);assert.equal(nodes.filter(n=>n.type==='button'&&n.textContent==='打开编辑器').length,1);
  assert(hasClass(button.parent,'shortcut-header-actions'));assert(hasClass(button.parent.parent,'card-head'));
  assert(button.parent.parent.textContent.includes('手柄快捷操作'));assert.equal(button.props['aria-haspopup'],'dialog');
  assert.equal(nodes.filter(n=>hasClass(n,'shortcuts-card')).length,1);assert.equal(f.saveCount,0);
  assert(!f.container.textContent.includes('OEM → 虚拟背键'));assert(!f.container.textContent.includes('同源的真实规则'));f.unmount();
});
await check('新入口打开原页面编辑器，连续点击不重复创建，也不隐式保存配置',async()=>{
  const f=await fixture();const button=descendants(f.container).find(n=>hasClass(n,'shortcut-editor-open'));
  button.props.onClick();button.props.onClick();await flush();assert.equal(f.editorMounts,1);assert.equal(f.saveCount,0);
  const close=descendants(f.container).find(n=>n.type==='button'&&n.textContent==='关闭编辑器');assert(close);
  close.props.onClick();await flush();assert.equal(f.editorUnmounts,1);button.props.onClick();await flush();assert.equal(f.editorMounts,2);f.unmount();
});
await check('切离KeepAlive页面关闭编辑器，回来不会残留或重复入口',async()=>{
  const f=await fixture();descendants(f.container).find(n=>hasClass(n,'shortcut-editor-open')).props.onClick();await flush();
  await f.setShown(false);assert.equal(f.editorUnmounts,1);await f.setShown(true);
  assert.equal(descendants(f.container).filter(n=>n.props['data-editor-fixture']).length,0);
  assert.equal(descendants(f.container).filter(n=>hasClass(n,'shortcut-editor-open')).length,1);f.unmount();
});
await check('挂载、激活和打开编辑器不改已保存OEM映射、未知字段或原快捷规则',async()=>{
  const f=await fixture();const before=JSON.stringify(f.settings.input);
  await f.setShown(false);await f.setShown(true);descendants(f.container).find(n=>hasClass(n,'shortcut-editor-open')).props.onClick();await flush();
  assert.equal(f.saveCount,0);assert.equal(JSON.stringify(f.settings.input),before);f.unmount();
});
await check('共享规则卡保留8条分页，默认在前，自定义OEM规则在后',async()=>{
  const f=await fixture();assert.equal(descendants(f.container).filter(n=>hasClass(n,'shortcut-toggle')).length,8);
  const next=descendants(f.container).find(n=>n.type==='button'&&n.textContent==='下一页 ›');assert(next&&!next.props.disabled);
  next.props.onClick();await flush();const items=descendants(f.container).filter(n=>hasClass(n,'shortcut-toggle'));
  assert(items.some(n=>n.textContent.includes('自定义')));assert(items.some(n=>n.textContent.includes(schema.formatControllerShortcutInputs([{source:'oem',code:'m1'}]))));assert(f.focused);f.unmount();
});
await check('原卡片快捷开关仍保存同源规则及native开关，不触碰背键映射',async()=>{
  const f=await fixture();const before=structuredClone(f.settings.input.outputTarget.oemRearMap);
  descendants(f.container).find(n=>hasClass(n,'shortcut-toggle')).props.onClick();await flush();
  assert.equal(f.saveCount,1);assert.equal(f.nativeSaves,1);
  assert.equal(f.settings.input.buttonMapping.rules.shortcutRules[0].enabled,false);
  assert.deepEqual(f.settings.input.outputTarget.oemRearMap,before);assert.equal(f.settings.input.buttonMapping.rules.customUnknownField,'preserve-me');f.unmount();
});
await check('保存失败仍有可见失败回执，卡片原开关恢复',async()=>{
  const f=await fixture();f.failSave=true;const before=JSON.stringify(f.settings.input);
  descendants(f.container).find(n=>hasClass(n,'shortcut-toggle')).props.onClick();await flush();
  assert(f.container.textContent.includes('保存失败'));assert.equal(JSON.stringify(f.settings.input),before);assert.equal(f.nativeSaves,0);f.unmount();
});
await check('编辑器规则更新事件仍会刷新快捷卡的自定义规则',async()=>{
  const f=await fixture();descendants(f.container).find(n=>n.type==='button'&&n.textContent==='下一页 ›').props.onClick();await flush();
  await f.updateRule('m2');assert(descendants(f.container).some(n=>hasClass(n,'shortcut-toggle')&&n.textContent.includes(schema.formatControllerShortcutInputs([{source:'oem',code:'m2'}]))));f.unmount();
});
await check('更改输出人格仍保留已有OEM背键映射与未知键，不批量清零',async()=>{
  const f=await fixture();const before=structuredClone(f.settings.input.outputTarget.oemRearMap);
  descendants(f.container).find(n=>n.type==='button'&&hasClass(n,'persona-btn')&&n.textContent==='PS5').props.onClick();await flush();
  assert.equal(f.settings.input.outputTarget.persona,'dualsense-edge');assert.deepEqual(f.settings.input.outputTarget.oemRearMap,before);f.unmount();
});
await check('游戏专属接管锁定输出控制器，但合并后的快捷编辑入口仍可达',async()=>{
  const f=await fixture({locked:true});assert(f.container.textContent.includes('游戏专属配置生效中'));
  const nodes=descendants(f.container);assert(nodes.filter(n=>hasClass(n,'persona-btn')).every(n=>n.props.disabled));
  const open=nodes.find(n=>hasClass(n,'shortcut-editor-open'));assert(!open.props.disabled);open.props.onClick();await flush();assert.equal(f.editorMounts,1);
  nodes.find(n=>hasClass(n,'persona-btn')&&n.textContent==='SteamDeck').props.onClick();await flush();assert.equal(f.joyCalls,0);assert.equal(f.saveCount,0);f.unmount();
});
await check('卸载页面清理规则刷新、后退和native状态订阅',async()=>{
  const f=await fixture();f.unmount();assert.equal(f.events.get('ipc:controller-shortcuts.updated')?.size,0);
  assert.equal(f.events.get('ipc:gamepad-back')?.size,0);assert.equal(f.nativeEvents.size,0);
});
await check('快捷开关的临时人格只更新只读高亮，不写回本机配置；手动重选可退出临时覆盖',async()=>{
  const f=await fixture();const before=JSON.stringify(f.settings.input);const savedPersona=f.settings.input.outputTarget.persona;
  f.nativeEvents.get('input.shortcutRuntime')({active:true,persona:'disabled',virtualEnabled:false,gyroEnabled:false});await flush();
  assert(descendants(f.container).find(n=>hasClass(n,'persona-btn')&&n.textContent==='关闭虚拟手柄').props.class.includes('active'));
  assert.equal(f.saveCount,0);assert.equal(JSON.stringify(f.settings.input),before);
  const target=descendants(f.container).find(n=>hasClass(n,'persona-btn')&&n.textContent==='SteamDeck');target.props.onClick();await flush();
  assert.equal(f.settings.input.outputTarget.persona,savedPersona);assert(descendants(f.container).find(n=>hasClass(n,'persona-btn')&&n.textContent==='SteamDeck').props.class.includes('active'));f.unmount();
});
await check('专用配置生效时临时状态仍可读，只更新反馈而不解锁页面或保存专用配置',async()=>{
  const f=await fixture({locked:true});const before=JSON.stringify(f.settings.input.gameOverride);
  f.nativeEvents.get('input.shortcutRuntime')({active:true,persona:'dualsense-edge',virtualEnabled:true,gyroEnabled:true});await flush();
  const nodes=descendants(f.container);assert(nodes.find(n=>hasClass(n,'persona-btn')&&n.textContent==='PS5').props.class.includes('active'));
  assert(nodes.filter(n=>hasClass(n,'persona-btn')).every(n=>n.props.disabled));assert.equal(f.saveCount,0);
  assert.equal(JSON.stringify(f.settings.input.gameOverride),before);f.unmount();
});

// Execute the actual editor script, repository and API wrapper. Storage and
// platform actions remain isolated; optional native fixture executes real IPC/writer bodies.
async function editorSaveFixture(options={}) {
  let doc=structuredClone(repo.normalizeSettings({input:{revision:7,outputTarget:{persona:'steamdeck',buttonMappingEnabled:true,oemRearMap:{m1:'left'}},buttonMapping:{rules:{customUnknownField:'keep',shortcutRules:[...schema.NATIVE_DEFAULT_SHORTCUTS.map(d=>schema.nativeDefaultRule(d.defaultKey)),{...schema.newControllerShortcutRule('seed','keyboard.numeric'),inputs:[{source:'oem',code:'m1'}],params:{key:'F8'}}]}}},fan:{sentinel:'keep'},cpu:{sentinel:'keep'}}));
  let content=JSON.stringify(doc), casCalls=0, writes=0, nativeSaves=0, failWrite=false, failNative=false, legacyDrop=false, unprovenLegacyRead=false;
  const trace=[];let gamepad={enabled:true,...Object.fromEntries(schema.NATIVE_DEFAULT_SHORTCUTS.map(d=>[d.defaultKey,true]))};
  const disk=()=>JSON.parse(content);const mutate=fn=>{const value=disk();fn(value);content=JSON.stringify(value);};
  const base='Q:\\YMCC-OEM-SAVE-MEMORY-ONLY';
  const api=load('src/bridge/api.ts',{'./ipc':{async invoke(command,args){
    if(command==='settings.read')return {stamp:content,unchanged:false,content};
    if(command==='fs.readTextFile'){assert(args.path.startsWith(base));return unprovenLegacyRead?'invalid-json':content;}
    if(command==='fs.exists')return true;
    if(command==='settings.write'){
      writes++;trace.push(structuredClone(args));if(options.beforeWrite)options.beforeWrite(writes,mutate);
      if(failWrite)return false;
      if(options.unknownReceipt==='null-document')return {ok:true,content:'null'};
      if(options.unknownReceipt==='legacy-unreadable'){unprovenLegacyRead=true;return true;}
      if(legacyDrop)return {ok:true,content};
      if(process.env.YMCC_OEM_SAVE_NATIVE_FIXTURE){
        const {spawnSync}=require('node:child_process');const r=spawnSync(process.env.YMCC_OEM_SAVE_NATIVE_FIXTURE,[],{encoding:'utf8',input:JSON.stringify({current:disk(),args:{...args,path:'fixture-settings.json'}})+'\n'});
        assert.equal(r.status,0,r.stderr);const result=JSON.parse(r.stdout);assert(!result.exception,result.exception);
        if(result.reply.ok){content=JSON.stringify(result.document);assert(result.refreshes===1);}else assert.equal(result.writes,0);
        return result.reply;
      }
      if(args.expectedInputRevision!==undefined&&disk().input.revision!==args.expectedInputRevision)return {ok:false,reason:'revision-conflict',content};
      content=args.content;return {ok:true,content};
    }
    throw Error('Forbidden editor fixture IPC '+command);
  }}});
  const repository=load('src/bridge/settingsRepository.ts',{'./api':api,'./settingsSnapshot':load('src/bridge/settingsSnapshot.ts'),'./quickAppDefaults':load('src/bridge/quickAppDefaults.ts'),'./inputStartupPolicy':load('src/bridge/inputStartupPolicy.ts'),'./inputConfigHash':hash,'./hcInputUtils':hc});repository.setSettingsDirectory(base);
  const events=new Map();const window={innerWidth:1200,innerHeight:800,setTimeout,clearTimeout,addEventListener(n,cb){events.set(n,cb)},removeEventListener(n){events.delete(n)},dispatchEvent(e){events.get(e.type)?.(e)}};
  const globals={window,document:{querySelectorAll:()=>[],addEventListener(){},removeEventListener(){}},CustomEvent:class{constructor(type,o){this.type=type;this.detail=o?.detail}}};
  const stub={render:()=>vue.h('div')};const viewSource=process.env.YMCC_OEM_SAVE_EDITOR_SOURCE || path.join(root,'src/views/ControllerShortcutEditorView.vue');const {descriptor,errors}=parse(fs.readFileSync(viewSource,'utf8'),{filename:viewSource});assert.equal(errors.length,0);
  const Editor=evaluate(compileScript(descriptor,{id:'oem-save-real-editor'}).content,{
    '@/components/Dropdown.vue':{__esModule:true,default:stub},'@/components/GamepadVisualizer.vue':{__esModule:true,default:stub},'@/components/InlineIcon.vue':{__esModule:true,default:stub},'@/components/SegButton.vue':{__esModule:true,default:stub},
    '@/bridge/ipc':{on:()=>()=>{},isNativeRuntime:()=>false},'@/bridge/settingsRepository':{...repository,async compareAndSwapInputSettings(rev,patch){casCalls++;if(options.beforeCas)options.beforeCas(casCalls,mutate);return repository.compareAndSwapInputSettings(rev,patch)}},
    '@/bridge/yeman':{summonGet:async()=>structuredClone(gamepad),summonSet:async p=>{nativeSaves++;if(failNative)throw Error('native settings sync fixture failure');Object.assign(gamepad,p);return structuredClone(gamepad)},oemKeysGet:async()=>schema.OEM_EMPTY_SNAPSHOT,setShortcutRecording:async()=>({})},
    '@/bridge/controllerShortcutRules':schema,'@/gamepad/focus':{focusGamepadElement:()=>{}},
  },globals).default;
  Editor.render=()=>vue.h('div');const app=renderer.createApp(Editor);const proxy=app.mount(element('editor-root'));await flush();const state=proxy.$.setupState;
  return {state,repository,trace,get doc(){return disk()},get writes(){return writes},get casCalls(){return casCalls},get nativeSaves(){return nativeSaves},mutate,
    set failWrite(v){failWrite=v},set failNative(v){failNative=v},set legacyDrop(v){legacyDrop=v},
    edit(key='l4',id='draft'){state.rules=[...state.rules,{...schema.newControllerShortcutRule(id,'window.summon'),inputs:[{source:'oem',code:key}]}];state.dirty=true},
    async apply(){await state.apply();await flush()},async reload(){repository.clearSettingsCache();await state.load();await flush()},unmount(){app.unmount()}};
}
await check('真实编辑器OEM保存重读保留来源、参数、启用与未知同段字段',async()=>{
 const f=await editorSaveFixture();f.edit();await f.apply();assert.equal(f.state.dirty,false);assert(f.doc.input.buttonMapping.rules.shortcutRules.some(r=>r.id==='rule-draft'&&r.inputs[0].source==='oem'));
 assert.equal(f.doc.input.buttonMapping.rules.customUnknownField,'keep');assert.equal(f.doc.input.outputTarget.persona,'steamdeck');assert.equal(f.doc.fan.sentinel,'keep');assert.equal(f.doc.cpu.sentinel,'keep');assert.equal(f.trace[0].expectedInputRevision,7);
 const before=JSON.stringify(f.doc.input.buttonMapping.rules.shortcutRules);await f.reload();assert.deepEqual(JSON.parse(JSON.stringify(f.state.rules)),JSON.parse(before));f.unmount();
});
await check('保存冲突保留OEM草稿并合并不同规则及最新人格',async()=>{
 const f=await editorSaveFixture({beforeCas(n,mutate){if(n===1)mutate(d=>{d.input.revision++;d.input.outputTarget.persona='dualsense-edge';d.input.buttonMapping.rules.shortcutRules.push({...schema.newControllerShortcutRule('remote'),inputs:[{source:'oem',code:'m2'}]})})}});f.edit();await f.apply();
 const ids=f.doc.input.buttonMapping.rules.shortcutRules.map(r=>r.id);assert(ids.includes('rule-draft'));assert(ids.includes('rule-remote'));assert.equal(f.doc.input.outputTarget.persona,'dualsense-edge');assert.equal(f.casCalls,2);assert.equal(f.state.dirty,false);f.unmount();
});
await check('锁内相同新版本号竞态拒绝旧写后合并OEM规则重试',async()=>{
 const f=await editorSaveFixture({beforeWrite(n,mutate){if(n===1)mutate(d=>{d.input.revision++;d.input.outputTarget.persona='dualsense-edge';d.input.buttonMapping.rules.shortcutRules.push({...schema.newControllerShortcutRule('remote-late'),inputs:[{source:'oem',code:'m2'}]})})}});f.edit();await f.apply();
 const ids=f.doc.input.buttonMapping.rules.shortcutRules.map(r=>r.id);assert(ids.includes('rule-draft'));assert(ids.includes('rule-remote-late'));assert.equal(f.doc.input.revision,9);assert.equal(f.trace[1].expectedInputRevision,8);assert.equal(f.state.dirty,false);f.unmount();
});
await check('同条规则冲突不覆盖已保存规则且本地OEM草稿仍可编辑',async()=>{
 const f=await editorSaveFixture({beforeCas(n,mutate){if(n===1)mutate(d=>{d.input.revision++;d.input.buttonMapping.rules.shortcutRules.find(r=>r.id==='rule-seed').params.key='F10'})}});
 f.state.rules.find(r=>r.id==='rule-seed').params.key='F9';f.state.dirty=true;await f.apply();assert.equal(f.doc.input.buttonMapping.rules.shortcutRules.find(r=>r.id==='rule-seed').params.key,'F10');assert.equal(f.state.rules.find(r=>r.id==='rule-seed').params.key,'F9');assert(f.state.dirty);assert(f.state.status.includes('草稿已保留'));assert.equal(f.casCalls,1);assert.equal(f.nativeSaves,0);f.unmount();
});
await check('实际写入失败不加载覆盖草稿也不误报保存成功',async()=>{
 const f=await editorSaveFixture();f.edit();const before=JSON.stringify(f.doc);f.failWrite=true;await f.apply();assert.equal(JSON.stringify(f.doc),before);assert(f.state.rules.some(r=>r.id==='rule-draft'));assert(f.state.dirty);assert(f.state.status.includes('保存失败'));assert.equal(f.nativeSaves,0);f.failWrite=false;await f.apply();assert.equal(f.state.dirty,false);f.unmount();
});
await check('连续版本冲突最多一次自动重试且保留完整草稿',async()=>{
 const f=await editorSaveFixture({beforeCas(n,mutate){mutate(d=>{d.input.revision++;d.input.outputTarget.persona=n===1?'dualsense-edge':'steamdeck'})}});f.edit();await f.apply();assert.equal(f.casCalls,2);assert.equal(f.writes,0);assert(f.state.rules.some(r=>r.id==='rule-draft'));assert(f.state.dirty);assert(f.state.status.includes('配置仍在变更'));assert.equal(f.nativeSaves,0);f.unmount();
});
await check('旧后端成功回执丢掉input事务时不将OEM草稿签成已保存',async()=>{
 const f=await editorSaveFixture({beforeWrite(n,mutate){mutate(d=>{d.input.revision+=2})}});f.legacyDrop=true;f.edit();await f.apply();assert(f.state.dirty);assert(f.state.rules.some(r=>r.id==='rule-draft'));assert(!f.doc.input.buttonMapping.rules.shortcutRules.some(r=>r.id==='rule-draft'));assert.equal(f.nativeSaves,0);assert.equal(f.casCalls,2);f.unmount();
});
await check('默认快捷开关同步失败如实回执且再次应用可恢复',async()=>{
 const f=await editorSaveFixture();f.edit();f.failNative=true;await f.apply();assert(f.doc.input.buttonMapping.rules.shortcutRules.some(r=>r.id==='rule-draft'));assert(f.state.dirty);assert(f.state.status.includes('默认快捷开关未同步'));f.failNative=false;await f.apply();assert.equal(f.state.dirty,false);f.unmount();
});

for(const receipt of ['null-document','legacy-unreadable'])await check('成功回执缺少可验证文档时保留草稿：'+receipt,async()=>{
 const f=await editorSaveFixture({unknownReceipt:receipt});f.edit();const before=JSON.stringify(f.doc);await f.apply();assert.equal(JSON.stringify(f.doc),before);assert(f.state.dirty,'unproven-receipt-draft-retained');assert(f.state.rules.some(r=>r.id==='rule-draft'));assert(f.state.status.includes('保存失败'));assert.equal(f.nativeSaves,0);f.unmount();
});

await check('待接管规则在快捷卡不可达、不计入分页及计数，操作可见开关仍完整保留隐藏数据',async()=>{
  const hidden=schema.CONTROLLER_SHORTCUT_ACTIONS.filter(action=>!action.available).map(action=>({...schema.newControllerShortcutRule('hidden-'+action.id,action.id),inputs:[],params:{...schema.defaultParameters(action.id),legacyMarker:action.id}}));
  const f=await fixture({hiddenRules:hidden});
  const labels=hidden.map(rule=>schema.actionById(rule.actionId).label);
  assert.equal(f.saveCount,0);assert(f.container.textContent.includes('10/10 已启用'));
  for(const label of labels)assert(!f.container.textContent.includes(label));
  descendants(f.container).find(n=>n.type==='button'&&n.textContent==='下一页 ›').props.onClick();await flush();
  assert.equal(descendants(f.container).filter(n=>hasClass(n,'shortcut-toggle')).length,2);
  for(const label of labels)assert(!f.container.textContent.includes(label));
  descendants(f.container).find(n=>hasClass(n,'shortcut-toggle')).props.onClick();await flush();
  assert.equal(f.saveCount,1);
  assert.deepEqual(structuredClone(f.settings.input.buttonMapping.rules.shortcutRules.filter(rule=>rule.id.startsWith('rule-hidden-'))),structuredClone(hidden));
  f.unmount();
});

console.log(JSON.stringify({suite:'Controller shortcut UI merge',passed:cases.length,cases,nativeOperations:'mocked',hardwareOperations:0},null,2));

