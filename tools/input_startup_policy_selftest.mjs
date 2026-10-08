import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import { createRequire } from 'node:module';
const require = createRequire(path.join(process.cwd(), 'package.json'));
const vue = require('vue'), { parse, compileScript } = require('vue/compiler-sfc'), { transformSync } = require('esbuild');
const root = process.cwd(), cases = [];
async function check(name, fn) { await fn(); cases.push(name); }
function evaluate(source, imports = {}, globals = {}) {
  const module = { exports: {} };
  vm.runInNewContext(transformSync(source, {loader:'ts',format:'cjs'}).code, {module,exports:module.exports,console,TextEncoder,structuredClone,
    require(name) { if(name==='vue')return vue; if(name in imports)return imports[name];throw Error('Unexpected import '+name); },...globals});
  return module.exports;
}
function load(file, imports, globals) { return evaluate(fs.readFileSync(path.join(root,file),'utf8'),imports,globals); }
const policy=load('src/bridge/inputStartupPolicy.ts'), hash=load('src/bridge/inputConfigHash.ts'), hc=load('src/bridge/hcInputUtils.ts');
const repoImports={'./settingsSnapshot':load('src/bridge/settingsSnapshot.ts'),'./quickAppDefaults':load('src/bridge/quickAppDefaults.ts'),'./api':{},'./inputStartupPolicy':policy,'./inputConfigHash':hash,'./hcInputUtils':hc};
const repo=load('src/bridge/settingsRepository.ts',repoImports);
const plain=value=>JSON.parse(JSON.stringify(value));
function element(type,text=''){return {type,text,props:{},children:[],parent:null,get textContent(){return this.text+this.children.map(n=>n.textContent).join('');}};}
const renderer=vue.createRenderer({createElement:element,createText:text=>element('#text',text),createComment:text=>element('#comment',text),
  setText(n,t){n.text=t;},setElementText(n,t){n.text=t;n.children=[];},parentNode:n=>n.parent,nextSibling:n=>n.parent?.children[n.parent.children.indexOf(n)+1]||null,
  insert(n,p,a=null){if(n.parent)n.parent.children.splice(n.parent.children.indexOf(n),1);n.parent=p;const i=a?p.children.indexOf(a):-1;if(i<0)p.children.push(n);else p.children.splice(i,0,n);},
  remove(n){if(n.parent)n.parent.children.splice(n.parent.children.indexOf(n),1);n.parent=null;},patchProp(n,k,p,v){n.props[k]=v;}});
const descendants=n=>[n,...n.children.flatMap(descendants)];
async function flush(){for(let i=0;i<40;i++){await Promise.resolve();await vue.nextTick();}}
async function fixture(raw={}) {
  let settings=plain(repo.normalizeSettings({startupDesired:{fanControl:true,unknownBoot:{keep:1},...raw},input:{outputTarget:{persona:'elite',buttonMappingEnabled:true},gyroMotion:{enabled:true,preset:'custom',gyroMultiplier:2.3}}}));
  let fail=false,block=null, writes=0;const actions=[],events=new Map();
  const window={addEventListener(n,cb){events.set(n,cb);},removeEventListener(n){events.delete(n);},setTimeout,clearTimeout};
  const yeman=new Proxy({BOOT_CONTROL_CENTER_TASK:'boot',BOOT_MIRROR_CHANGED_EVENT:'boot-changed'}, {get(o,k){return k in o?o[k]:async(...args)=>{actions.push([k,...args]);return false;};}});
  const repository={async readSettingsSection(section){return structuredClone(settings[section]||{});},async saveSettingsSection(section,value){writes++;if(block)await block;
    if(fail)throw Error('simulated write failed');settings[section]=plain(repo.mergeSettings(settings[section],value));}};
  const dropdown={props:['modelValue','options','disabled'],emits:['update:modelValue'],setup:(p,{emit})=>()=>vue.h('button',{'data-dd':true,'data-model':p.modelValue,'data-options':p.options,disabled:p.disabled,onSelect:v=>emit('update:modelValue',v)},p.options.find(o=>o.value===p.modelValue)?.label||'')};
  const toggle={props:['modelValue','label','disabled'],emits:['update:modelValue'],setup:p=>()=>vue.h('button',{'data-toggle':p.label,disabled:p.disabled},p.label)};
  const stub={setup:()=>()=>vue.h('span')};
  const source=fs.readFileSync(path.join(root,'src/views/PowerView.vue'),'utf8'),{descriptor,errors}=parse(source,{filename:'PowerView.vue'});assert.equal(errors.length,0);
  const compiled=compileScript(descriptor,{id:'startup-test',inlineTemplate:true,templateOptions:{compilerOptions:{hoistStatic:false}}}).content;
  const imports={'@/bridge/inputStartupPolicy':policy,'@/bridge/settingsRepository':repository,'@/bridge/yeman':yeman,
    '@/bridge/api':{fs:{readTextFile:async()=>'{"configured":true}'},shell:{open:async()=>{}}},
    '@/bridge/trayResident':{readTrayResident:async()=>false,setTrayResident:async()=>{}},
    '@/components/Dropdown.vue':{__esModule:true,default:dropdown},'@/components/Toggle.vue':{__esModule:true,default:toggle},
    '@/components/WarnBar.vue':{__esModule:true,default:stub},'@/components/InlineIcon.vue':{__esModule:true,default:stub}};
  const component=evaluate(compiled,imports,{window,document:{},setTimeout,clearTimeout}).default;
  const container=element('root'),app=renderer.createApp(component);app.provide('globalRefreshKey',vue.ref(0));app.mount(container);await flush();
  const dd=kind=>descendants(container).find(n=>n.props['data-input-startup']===kind).children.find(n=>n.props['data-dd']);
  return {container,dd,actions,get settings(){return settings;},get writes(){return writes;},set fail(v){fail=v;},
    select(kind,value){dd(kind).props.onSelect(value);},async hold(){let resolve;block=new Promise(r=>resolve=r);return async()=>{block=null;resolve();await flush();};},
    async refresh(){events.get('boot-changed')?.();await flush();},unmount(){app.unmount();}};
}
await check('4个开机手柄、5个陀螺仪选项，默认与损坏配置均关闭',()=>{
  assert.deepEqual(plain(policy.STARTUP_PAD_OPTIONS).map(o=>o.label),['关闭虚拟手柄','SteamDeck','PS5','Xbox']);
  assert.deepEqual(plain(policy.STARTUP_GYRO_OPTIONS).map(o=>o.label),['关闭陀螺仪','FPS射击','赛车','自定义','Steam']);
  for(const value of [null,[],{},true,{virtualGamepadPersona:'xbox360',gyroPreset:'fps'}])assert.deepEqual(plain(policy.normalizeInputStartupPreferences(value)),{virtualGamepadPersona:'disabled',gyroPreset:'off'});
  for(const pad of policy.STARTUP_PAD_OPTIONS)for(const gyro of policy.STARTUP_GYRO_OPTIONS){const n=policy.normalizeInputStartupPreferences({virtualGamepadPersona:pad.value,gyroPreset:gyro.value});assert.equal(n.gyroPreset,pad.value==='disabled'?'off':gyro.value);}
});
await check('真实Power页面按手柄/陀螺仪/控制中心顺序，禁用下拉旁路不写入',async()=>{
  const f=await fixture({virtualGamepadPersona:'disabled',gyroPreset:'fps'}), nodes=descendants(f.container);
  const pad=nodes.findIndex(n=>n.props['data-input-startup']==='pad'),gyro=nodes.findIndex(n=>n.props['data-input-startup']==='gyro'),boot=nodes.findIndex(n=>n.props['data-toggle']==='开机启动野蛮控制中心');
  assert(pad<gyro&&gyro<boot);assert.equal(f.dd('pad').props.disabled,false);assert.equal(f.dd('gyro').props.disabled,true);assert.equal(f.dd('gyro').props['data-model'],'off');
  f.select('gyro','fps');await flush();assert.equal(f.writes,0);f.unmount();
});
await check('选择只保存开机意图，不操作当前人格/gyro/原生任务，其他开机字段保留',async()=>{
  const f=await fixture(),input=JSON.stringify(f.settings.input),actions=f.actions.length;
  f.select('pad','steamdeck');await flush();assert.equal(f.dd('gyro').props.disabled,false);f.select('gyro','racing');await flush();
  assert.equal(f.settings.startupDesired.virtualGamepadPersona,'steamdeck');assert.equal(f.settings.startupDesired.gyroPreset,'racing');assert.equal(f.settings.startupDesired.fanControl,true);assert.equal(f.settings.startupDesired.unknownBoot.keep,1);
  assert.equal(JSON.stringify(f.settings.input),input);assert.equal(f.actions.length,actions);assert.equal(f.writes,2);f.unmount();
});
await check('关闭开机手柄强制落盘gyro off，重新开启不复活旧预设',async()=>{
  const f=await fixture({virtualGamepadPersona:'elite',gyroPreset:'fps'});f.select('pad','disabled');await flush();
  assert.equal(f.settings.startupDesired.gyroPreset,'off');assert.equal(f.dd('gyro').props.disabled,true);assert.equal(f.dd('gyro').props['data-model'],'off');
  f.select('pad','dualsense-edge');await flush();assert.equal(f.dd('gyro').props['data-model'],'off');assert.equal(f.dd('gyro').props.disabled,false);f.unmount();
});
await check('非法选项旁路不会静默改成关闭',async()=>{
  const f=await fixture({virtualGamepadPersona:'elite',gyroPreset:'fps'});f.select('pad','xbox360');f.select('gyro','invalid');await flush();
  assert.equal(f.writes,0);assert.equal(f.dd('pad').props['data-model'],'elite');assert.equal(f.dd('gyro').props['data-model'],'fps');f.unmount();
});
await check('保存中禁用两项、阻止重复事件；失败保持旧显示和旧磁盘值',async()=>{
  const f=await fixture({virtualGamepadPersona:'steamdeck',gyroPreset:'fps'}), release=await f.hold();f.fail=true;f.select('pad','elite');await flush();
  assert(f.dd('pad').props.disabled&&f.dd('gyro').props.disabled);f.select('gyro','racing');f.select('pad','disabled');await flush();assert.equal(f.writes,1);
  await release();assert.equal(f.dd('pad').props['data-model'],'steamdeck');assert.equal(f.dd('gyro').props['data-model'],'fps');assert.equal(f.settings.startupDesired.virtualGamepadPersona,'steamdeck');
  assert(f.container.textContent.includes('输入开机启动设置失败'));assert.equal(f.dd('pad').props.disabled,false);f.unmount();
});
await check('页面刷新不重放开机意图，也不以运行状态反推下拉',async()=>{
  const f=await fixture();await f.refresh();assert.equal(f.writes,0);assert.equal(f.dd('pad').props['data-model'],'disabled');assert.equal(f.settings.input.outputTarget.persona,'elite');f.unmount();
});
await check('真实repository事务分离：运行CAS不改开机项，开机保存不改input',async()=>{
  let disk=JSON.stringify(repo.normalizeSettings({startupDesired:{virtualGamepadPersona:'steamdeck',gyroPreset:'racing',unknown:'keep'},input:{gyroMotion:{presets:{fps:{gyroMultiplier:1.8}}}}})),failed=false;
  const durable=load('src/bridge/settingsRepository.ts',{...repoImports,'./api':{fs:{exists:async()=>true,readTextFile:async p=>p.endsWith('yeman-settings.json')?disk:'{}'},settingsStore:{write:async(p,s)=>{if(failed)return false;disk=s;return true;}}}});
  let doc=await durable.loadSettings();const prefs=JSON.stringify(doc.startupDesired);
  const result=await durable.compareAndSwapInputSettings(doc.input.revision,{outputTarget:{persona:'elite',buttonMappingEnabled:true},gyroMotion:{enabled:true,preset:'fps'}});assert(result.ok);
  assert.equal(JSON.stringify(JSON.parse(disk).startupDesired),prefs);const input=JSON.stringify(JSON.parse(disk).input);
  await durable.saveSettingsSection('startupDesired',{virtualGamepadPersona:'disabled',gyroPreset:'off'});assert.equal(JSON.stringify(JSON.parse(disk).input),input);assert.equal(JSON.parse(disk).startupDesired.unknown,'keep');
  const before=disk;failed=true;await assert.rejects(()=>durable.saveSettingsSection('startupDesired',{virtualGamepadPersona:'elite',gyroPreset:'steam'}));assert.equal(disk,before);
});
await check('损坏主配置保留损坏证据并从备份恢复，新开机项不丢失',async()=>{
  const backup=JSON.stringify(repo.normalizeSettings({startupDesired:{virtualGamepadPersona:'dualsense-edge',gyroPreset:'steam',unknown:'keep'},input:{gyroMotion:{presets:{custom:{gyroMultiplier:2.1}}}}}));
  let disk='{not-json',renamed=0;const durable=load('src/bridge/settingsRepository.ts',{...repoImports,'./api':{
    fs:{exists:async()=>true,readTextFile:async p=>p.endsWith('yeman-settings.json')?disk:p.endsWith('.bak')?backup:'{}',rename:async(a,b)=>{assert(b.includes('.corrupt-'));renamed++;return true;}},
    settingsStore:{write:async(p,s)=>{disk=s;return true;}}}});
  const settings=await durable.loadSettings();assert.equal(renamed,1);assert.equal(settings.startupDesired.virtualGamepadPersona,'dualsense-edge');assert.equal(settings.startupDesired.gyroPreset,'steam');
  assert.equal(settings.startupDesired.unknown,'keep');assert.equal(settings.input.gyroMotion.presets.custom.gyroMultiplier,2.1);assert.doesNotThrow(()=>JSON.parse(disk));
});
await check('native在worker之前同锁写入、失败保持关闭，进程once防重放与旧ACK清理',()=>{
  const source=fs.readFileSync(path.join(root,'native/main.cpp'),'utf8'),head=fs.readFileSync(path.join(root,'native/input_startup_policy.h'),'utf8');
  const transaction=source.slice(source.indexOf('static bool applyInputStartupOnce()'),source.indexOf('// MOCK 伪装握手门',source.indexOf('static bool applyInputStartupOnce()')));
  assert(transaction.includes('std::call_once'));assert(transaction.indexOf('SettingsFileGuard guard')<transaction.indexOf('ymSettingsReadUnlocked()'));assert(transaction.includes('ymSettingsWriteDocumentUnlocked(all.dump(2))'));
  assert(transaction.indexOf('if (!applyInputStartupOnce())')<transaction.indexOf('CreateThread(nullptr, 0, inputCaptureThreadProc'));
  assert(transaction.includes('"startup-input-settings-write-failed"'));assert(/startup-input-settings-write-failed[\s\S]*?return;/.test(transaction));
  assert(head.includes('"startupAppliedSession"'));assert(head.includes('input["applyStatus"] = "unknown"'));assert(head.includes('"hostAcknowledgedConfigHash"'));
});
console.log(JSON.stringify({suite:'Input startup frontend policy',passed:cases.length,cases,nativeOperations:'mocked',hardwareOperations:0},null,2));
