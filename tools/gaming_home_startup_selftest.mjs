import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import { createRequire } from 'node:module';
const root = process.cwd();
const require = createRequire(path.join(root, 'package.json'));
const vue = require('vue');
const { parse, compileScript } = require('vue/compiler-sfc');
const { transformSync } = require('esbuild');
const cases = [];
async function check(name, fn) { await fn(); cases.push(name); console.log('PASS:', name); }
function evaluate(source, imports = {}, globals = {}) {
  const module = { exports: {} };
  const js = transformSync(source, { loader: 'ts', format: 'cjs', target: 'es2022' }).code;
  vm.runInNewContext(js, { module, exports: module.exports, require: name => {
    if (name === 'vue') return vue;
    if (name in imports) return imports[name];
    throw Error('Unexpected import: ' + name);
  }, console, setTimeout, clearTimeout, ...globals });
  return module.exports;
}
const policy = evaluate(fs.readFileSync(path.join(root, 'src/bridge/gamingHomePolicy.ts'), 'utf8'));
const { GAMING_OEM_KEY: OEM, GAMING_CONFIG_KEY: CFG } = policy;
const key = (r, p, n) => [r, p, n].join('|');
function fixture({ device = null, startup = 0, home = 'Microsoft.GamingApp_8wekyb3d8bbwe!Microsoft.Xbox.App', manufacturer = 'ASUSTeK COMPUTER INC.', product = 'ROG Ally RC71L_RC71L' } = {}) {
  const values = new Map(), writes = [], removed = [], backups = [], events = [];
  const set = (r, p, n, v) => v == null ? values.delete(key(r,p,n)) : values.set(key(r,p,n),v);
  set('HKLM',OEM,'DeviceForm',device); set('HKCU',CFG,'StartupToGamingHome',startup); set('HKCU',CFG,'GamingHomeApp',home);
  set('HKLM','HARDWARE\\DESCRIPTION\\System\\BIOS','SystemManufacturer',manufacturer);
  set('HKLM','HARDWARE\\DESCRIPTION\\System\\BIOS','SystemProductName',product);
  const f = { values, writes, removed, backups, events, failBackup: false, writeHook: null };
  f.port = {
    async read(r,p,n) { return values.has(key(r,p,n)) ? values.get(key(r,p,n)) : null; },
    async write(r,p,n,v) { writes.push([r,p,n,v]); events.push('write:' + n);
      if (f.writeHook) { const result = f.writeHook(r,p,n,v,set); if (result !== undefined) return result; }
      set(r,p,n,v); return true; },
    async remove(r,p,n) { removed.push([r,p,n]); set(r,p,n,null); return true; },
    async saveBackup(state) { events.push('backup'); if(f.failBackup) throw Error('backup failed'); backups.push(JSON.parse(JSON.stringify(state))); },
  };
  f.get = (r,p,n) => f.port.read(r,p,n);
  f.set = set;
  return f;
}
await check('verified defaults are DeviceForm=46 and StartupToGamingHome=1', () => {
  assert.equal(policy.GAMING_HOME_DEFAULTS.deviceForm,46); assert.equal(policy.GAMING_HOME_DEFAULTS.startup,1);
  assert(policy.isRogGamingHandheld('ASUSTeK COMPUTER INC.','ROG Xbox ALLY RC73YA_RC73YA'));
  assert(!policy.isRogGamingHandheld('ASUS','System Product Name'));
});
await check('read-only snapshots preserve an enabled startup without a legacy task', async () => {
  const f=fixture({device:46,startup:1}); for(let i=0;i<3;i++)assert((await policy.readGamingHomeState(f.port)).startupEnabled);
  assert.equal(f.writes.length,0); assert.equal(f.backups.length,0);
});
await check('missing ROG identity is repaired to 46/1, backed up first, Xbox home unchanged', async () => {
  const f=fixture(), home=await f.get('HKCU',CFG,'GamingHomeApp'), state=await policy.setGamingHomeStartup(f.port,true);
  assert.equal(state.deviceForm,46); assert.equal(state.startupValue,1); assert.equal(state.homeApp,home);
  assert.equal(f.events[0],'backup'); assert.equal(f.backups[0].deviceForm,null); assert.equal(f.backups[0].startupValue,0);
  assert.deepEqual(f.writes.map(w=>w[2]),['DeviceForm','StartupToGamingHome']);
});
await check('disabling changes only startup and retains handheld identity / AnyFSE home', async () => {
  const f=fixture({device:46,startup:1,home:'ArtemShpynov.AnyFSE_by4wjhxmygwn4!App'});
  const state=await policy.setGamingHomeStartup(f.port,false);assert.equal(state.deviceForm,46);assert.equal(state.startupValue,0);
  assert.equal(state.homeApp,'ArtemShpynov.AnyFSE_by4wjhxmygwn4!App');assert.deepEqual(f.writes.map(w=>w[2]),['StartupToGamingHome']);
});
await check('correct configuration is idempotent and other OEM-classified handhelds are accepted', async () => {
  const f=fixture({device:46,startup:1,manufacturer:'Other OEM',product:'Other handheld'});
  await policy.setGamingHomeStartup(f.port,true);assert.equal(f.writes.length,0);assert.equal(f.backups.length,0);
  f.set('HKCU',CFG,'StartupToGamingHome',0);await policy.setGamingHomeStartup(f.port,true);
  assert.equal(f.writes.length,1);assert.equal(f.writes[0][2],'StartupToGamingHome');
});
await check('desktop identity is never converted into handheld identity', async () => {
  const f=fixture({product:'System Product Name'});await assert.rejects(policy.setGamingHomeStartup(f.port,true),/拒绝/);
  assert.equal(f.writes.length,0);assert.equal(f.backups.length,0);
});
await check('backup failure aborts before any mutation', async () => {
  const f=fixture();f.failBackup=true;await assert.rejects(policy.setGamingHomeStartup(f.port,true),/backup failed/);assert.equal(f.writes.length,0);
});
await check('failed HKLM write does not enable startup', async () => {
  const f=fixture();f.writeHook=(r,p,n)=>n==='DeviceForm'?false:undefined;
  await assert.rejects(policy.setGamingHomeStartup(f.port,true),/DeviceForm/);assert.equal(await f.get('HKCU',CFG,'StartupToGamingHome'),0);
  assert.equal(f.writes.length,1);
});
await check('failed user-hive write rolls back the newly added OEM value, not the entire key', async () => {
  const f=fixture();f.writeHook=(r,p,n)=>n==='StartupToGamingHome'?false:undefined;
  await assert.rejects(policy.setGamingHomeStartup(f.port,true),/StartupToGamingHome/);
  assert.equal(await f.get('HKLM',OEM,'DeviceForm'),null);assert.equal(await f.get('HKCU',CFG,'StartupToGamingHome'),0);
  assert.deepEqual(f.removed,[['HKLM',OEM,'DeviceForm']]);
});
await check('write failure after mutation restores both original values', async () => {
  const f=fixture({device:3});let failed=false;
  f.writeHook=(r,p,n,v,set)=>{if(n==='StartupToGamingHome' && v===1 && !failed){failed=true;set(r,p,n,v);return false;}};
  await assert.rejects(policy.setGamingHomeStartup(f.port,true));
  assert.equal(await f.get('HKLM',OEM,'DeviceForm'),3);assert.equal(await f.get('HKCU',CFG,'StartupToGamingHome'),0);
});
await check('concurrent external OEM changes are not overwritten during rollback', async () => {
  const f=fixture();f.writeHook=(r,p,n,v,set)=>{if(n==='StartupToGamingHome'){set('HKLM',OEM,'DeviceForm',99);return false;}};
  await assert.rejects(policy.setGamingHomeStartup(f.port,true));assert.equal(await f.get('HKLM',OEM,'DeviceForm'),99);
});
await check('invalid value types and failed readbacks never report successful enablement', async () => {
  const invalid=fixture({startup:'1'});await assert.rejects(policy.setGamingHomeStartup(invalid.port,true),/DWORD/);assert.equal(invalid.writes.length,0);
  const dropped=fixture();dropped.writeHook=()=>true;await assert.rejects(policy.setGamingHomeStartup(dropped.port,true),/回读/);
  assert.equal(await dropped.get('HKCU',CFG,'StartupToGamingHome'),0);
});
function element(type,text=''){return{type,text,props:{},children:[],parent:null,get textContent(){return this.text+this.children.map(n=>n.textContent).join('');}};}
const renderer=vue.createRenderer({createElement:element,createText:text=>element('#text',text),createComment:text=>element('#comment',text),
  setText(n,t){n.text=t;},setElementText(n,t){n.text=t;n.children=[];},parentNode:n=>n.parent,nextSibling:n=>n.parent?.children[n.parent.children.indexOf(n)+1]||null,
  insert(n,p,a=null){if(n.parent)n.parent.children.splice(n.parent.children.indexOf(n),1);n.parent=p;const i=a?p.children.indexOf(a):-1;if(i<0)p.children.push(n);else p.children.splice(i,0,n);},
  remove(n){if(n.parent)n.parent.children.splice(n.parent.children.indexOf(n),1);n.parent=null;},patchProp(n,k,p,v){n.props[k]=v;}});
const descendants=n=>[n,...n.children.flatMap(descendants)];
async function flush(){for(let i=0;i<60;i++){await Promise.resolve();await vue.nextTick();}}
const inputPolicy=evaluate(fs.readFileSync(path.join(root,'src/bridge/inputStartupPolicy.ts'),'utf8'));
async function pageFixture(registryFixture=fixture({device:46,startup:1}), trayOptions={}){
  const actions=[], refreshKey=vue.ref(0), visible=vue.ref(true);let readFailure=false;
  const yeman=new Proxy({BOOT_CONTROL_CENTER_TASK:'boot',BOOT_MIRROR_CHANGED_EVENT:'boot-changed',
    async readGamingHomeConfiguration(){actions.push(['readGamingHomeConfiguration']);if(readFailure)throw Error('read unavailable');return policy.readGamingHomeState(registryFixture.port);},
    async writeGamingHomeStartup(enabled){actions.push(['writeGamingHomeStartup',enabled]);await policy.setGamingHomeStartup(registryFixture.port,enabled);return true;},
  },{get(o,k){return k in o?o[k]:async(...args)=>{actions.push([k,...args]);return false;};}});
  const toggle={props:['modelValue','label','description','disabled'],emits:['update:modelValue'],setup:(p,{emit})=>()=>vue.h('button',{'data-label':p.label,'data-model':p.modelValue,'data-description':p.description,disabled:p.disabled,onToggle:v=>emit('update:modelValue',v)},p.label)};
  const stub={setup:()=>()=>vue.h('span')};
  const {descriptor,errors}=parse(fs.readFileSync(path.join(root,'src/views/PowerView.vue'),'utf8'),{filename:'PowerView.vue'});assert.equal(errors.length,0);
  const compiled=compileScript(descriptor,{id:'gaming-test',inlineTemplate:true,templateOptions:{compilerOptions:{hoistStatic:false}}}).content;
  let trayPreference=trayOptions.initial===true;
  const trayRequests=[];
  const realTray=evaluate(fs.readFileSync(path.join(root,'src/bridge/trayResident.ts'),'utf8'),{
    './api':{tray:{setResident:async value=>{
      trayRequests.push(value);
      if(trayOptions.error)throw Error(trayOptions.error);
      return trayOptions.mismatch?!value:value;
    }}},
    './settingsRepository':{
      getSettingsGeneration:()=>1,assertSettingsGeneration:()=>{},
      readSettingsSection:async()=>({resident:trayPreference}),
      saveSettingsSection:async(section,value)=>{assert.equal(section,'tray');trayPreference=value.resident;},
    },
  });
  const imports={'@/bridge/yeman':yeman,'@/bridge/inputStartupPolicy':inputPolicy,
    '@/bridge/settingsRepository':{readSettingsSection:async()=>({}),saveSettingsSection:async()=>{}},
    '@/bridge/api':{fs:{readTextFile:async()=>'{}'},shell:{open:async()=>{}}},
    '@/bridge/trayResident':{readTrayResident:()=>realTray.readTrayResident(),setTrayResident:async(v)=>{actions.push(['setTrayResident',v]);await realTray.setTrayResident(v);}},
    '@/components/Toggle.vue':{__esModule:true,default:toggle},'@/components/Dropdown.vue':{__esModule:true,default:stub},
    '@/components/WarnBar.vue':{__esModule:true,default:{props:['text'],setup:p=>()=>vue.h('span',p.text)}},'@/components/InlineIcon.vue':{__esModule:true,default:stub}};
  const window={addEventListener(){},removeEventListener(){},dispatchEvent(){},setTimeout,clearTimeout};
  const component=evaluate(compiled,imports,{window,document:{}}).default;
  const app=renderer.createApp({setup:()=>()=>vue.h(vue.KeepAlive,null,{default:()=>visible.value?vue.h(component):vue.h(stub)})});
  const container=element('root');app.provide('globalRefreshKey',refreshKey);app.mount(container);await flush();
  return {actions,container,registryFixture,trayRequests,get trayPreference(){return trayPreference;},
    toggle(label,v){const button=descendants(container).find(n=>n.props['data-label']===label);assert(button,'Missing toggle '+label);button.props.onToggle(v);},
    async refresh(){refreshKey.value++;await flush();},async activate(){visible.value=false;await flush();visible.value=true;await flush();},
    set readFailure(v){readFailure=v;},unmount(){app.unmount();}};
}
await check('real page mount, refresh and activation retain enabled startup without a legacy task',async()=>{
  const f=await pageFixture();await f.refresh();await f.activate();
  assert.equal(await f.registryFixture.get('HKCU',CFG,'StartupToGamingHome'),1);assert.equal(f.registryFixture.writes.length,0);
  assert(!f.actions.some(([name])=>['writeGamingHomeStartup','steamMasterSet','setTrayResident'].includes(name)));
  assert(!f.actions.some(a=>a.includes('Xbox大屏游戏模式')));f.unmount();
});
await check('page read failure does not disable startup or change tray / Steam settings',async()=>{
  const f=await pageFixture();f.readFailure=true;await f.refresh();assert.equal(f.registryFixture.writes.length,0);
  assert(f.container.textContent.includes('读取全屏启动配置失败'));f.unmount();
});
await check('real page enable applies 46/1 without Steam or tray coupling; disable retains identity',async()=>{
  const f=await pageFixture(fixture());f.toggle('开机进入Xbox全屏游戏模式',true);await flush();
  assert.equal(await f.registryFixture.get('HKLM',OEM,'DeviceForm'),46);assert.equal(await f.registryFixture.get('HKCU',CFG,'StartupToGamingHome'),1);
  assert(!f.actions.some(([name])=>['steamMasterSet','setTrayResident'].includes(name)));
  f.toggle('开机进入Xbox全屏游戏模式',false);await flush();assert.equal(await f.registryFixture.get('HKLM',OEM,'DeviceForm'),46);
  assert.equal(await f.registryFixture.get('HKCU',CFG,'StartupToGamingHome'),0);f.unmount();
});
await check('Steam toggle is independent of system startup',async()=>{
  const f=await pageFixture();f.toggle('Steam高级开机启动(earlystart)',false);await flush();
  assert(f.actions.some(([name])=>name==='steamMasterSet'));assert.equal(f.registryFixture.writes.length,0);f.unmount();
});
await check('legacy task is never recreated, replacement XML is safe and Steam never kills Xbox',()=>{
  const bridge=fs.readFileSync(path.join(root,'src/bridge/yeman.ts'),'utf8');
  const def=bridge.match(/\{ name: 'Xbox大屏游戏模式'[^\n]+/)[0];assert(!def.includes('xml:'));
  assert(bridge.includes('gamingHomeWriteTail = action.then'));assert(bridge.includes("gamingBackupPath('.task.xml')"));
  const xml=fs.readFileSync(path.join(root,'PowerControl/Xbox大屏游戏模式.xml'),'utf16le');assert(!xml.includes('physpanel.exe'));
  assert(xml.includes('Repair-GamingHandheld.ps1'));assert(xml.includes('S-1-5-18'));
  const ps=fs.readFileSync(path.join(root,'PowerControl/Repair-GamingHandheld.ps1'),'utf8');
  assert(ps.includes('-Name DeviceForm -PropertyType DWord -Value 46'));assert(!ps.includes('HKCU:'));
  const steam=fs.readFileSync(path.join(root,'PowerControl/YeManSteam2.bat'),'utf8');assert(!/taskkill[^\r\n]*Xbox/i.test(steam));
});

await check('rollback failure is reported explicitly with the backup retained',async()=>{
  const f=fixture();f.writeHook=(r,p,n)=>n==='StartupToGamingHome'?false:undefined;f.port.remove=async()=>false;
  await assert.rejects(policy.setGamingHomeStartup(f.port,true),/回退未完成/);assert.equal(f.backups.length,1);
});
function bridgeFixture(registryFixture=fixture(),legacy=false){
  const canonical='野蛮优化整合系统\\Xbox大屏游戏模式',tasks=new Set(legacy?[canonical]:[]),directories=new Set(),files=new Map(),calls=[];
  let exportFails=false;
  const api={registry:registryFixture.port,fs:{
    async exists(p){const prefix='C:\\Windows\\System32\\Tasks\\';return p.startsWith(prefix)?tasks.has(p.slice(prefix.length)):directories.has(p)||files.has(p);},
    async mkdir(p){directories.add(p);return true;},async writeTextFileAtomic(p,c){files.set(p,c);},
    async readDir(){return[{isFile:true,name:'Xbox大屏游戏模式.xml'}];},
  },shell:{async run(program,args){
    calls.push([program,...args]);const task=args[args.indexOf('/TN')+1];
    if(program!=='schtasks')throw Error('Unexpected program '+program);
    if(args.includes('/Query'))return{exitCode:tasks.has(task)&&!(args.includes('/XML')&&exportFails)?0:1,stdout:args.includes('/XML')?'<Task>original physpanel definition</Task>':'task',stderr:''};
    if(args.includes('/Delete')){tasks.delete(task);return{exitCode:0,stdout:'',stderr:''};}
    throw Error('Unexpected scheduled task mutation '+args.join(' '));
  }}};
  const imports={'./api':api,'./ipc':{invoke:async(name,a)=>{
    assert.equal(name,'registry.delete');assert(a.name);return registryFixture.port.remove(a.root,a.path,a.name);
  }},'./gamingHomePolicy':policy,'./rtssOverlay':{},'./settingsRepository':{},'./tdpCircuitBreaker':{},'./controllerShortcutRules':{OEM_EMPTY_SNAPSHOT:{}}};
  const bridge=evaluate(fs.readFileSync(path.join(root,'src/bridge/yeman.ts'),'utf8'),imports,{window:{addEventListener(){},removeEventListener(){}}});
  return{bridge,tasks,files,calls,registryFixture,set exportFails(v){exportFails=v;}};
}
await check('actual bridge reads create no backups, scheduled task calls or registry writes',async()=>{
  const f=bridgeFixture(fixture({device:46,startup:1}));assert((await f.bridge.readGamingHomeConfiguration()).startupEnabled);
  assert.equal(f.files.size,0);assert.equal(f.calls.length,0);assert.equal(f.registryFixture.writes.length,0);
});
await check('actual bridge backs up and retires old task without altering an existing correct configuration',async()=>{
  const f=bridgeFixture(fixture({device:46,startup:1}),true);await f.bridge.writeGamingHomeStartup(true);
  assert.equal(f.tasks.size,0);assert.equal(f.registryFixture.writes.length,0);
  assert([...f.files.keys()].some(p=>p.endsWith('.task.xml')));assert(!f.calls.some(a=>a.includes('/Create')));
  const restored=await f.bridge.restoreAllYemanTasks();assert.equal(restored.imported,0);
});
await check('failed legacy task export never deletes the task and reports partial completion',async()=>{
  const f=bridgeFixture(fixture({device:46,startup:1}),true);f.exportFails=true;
  await assert.rejects(f.bridge.writeGamingHomeStartup(true),/未删除/);assert.equal(f.tasks.size,1);assert(!f.calls.some(a=>a.includes('/Delete')));
});
await check('actual bridge serializes explicit enable/disable, keeps home and backs up both changes',async()=>{
  const f=bridgeFixture();await Promise.all([f.bridge.writeGamingHomeStartup(true),f.bridge.writeGamingHomeStartup(false)]);
  assert.equal(await f.registryFixture.get('HKLM',OEM,'DeviceForm'),46);assert.equal(await f.registryFixture.get('HKCU',CFG,'StartupToGamingHome'),0);
  assert.equal([...f.files.keys()].filter(p=>p.endsWith('.json')).length,2);assert(!f.calls.some(a=>a.includes('/Create')));
});


await check('requested Xbox/Steam labels and description render, technical startup description is absent',async()=>{
  const f=await pageFixture(),text=f.container.textContent;
  assert(text.includes('Xbox全屏游戏模式'));assert(!text.includes('Windows 全屏游戏模式'));
  assert(text.includes('Steam大屏开机启动'));assert(!text.includes('Steam 独立启动'));
  const startup=descendants(f.container).find(n=>n.props['data-label']==='开机进入Xbox全屏游戏模式');
  assert(startup);assert.equal(startup.props['data-description'],undefined);
  const steam=descendants(f.container).find(n=>n.props['data-label']==='Steam高级开机启动(earlystart)');
  assert.equal(steam.props['data-description'],'开启的是Steam大屏页面的联动模式');f.unmount();
});
await check('real page plus real residency bridge accepts successful disable without an error banner',async()=>{
  const f=await pageFixture(fixture({device:46,startup:1}),{initial:true});
  f.toggle('任务栏常驻',false);await flush();
  assert.equal(f.trayPreference,false);assert.deepEqual(f.trayRequests,[false]);
  const toggle=descendants(f.container).find(n=>n.props['data-label']==='任务栏常驻');
  assert.equal(toggle.props['data-model'],false);assert.equal(toggle.props.disabled,false);
  assert(!descendants(f.container).some(n=>n.props.class==='err-bar'));
  assert.equal(f.registryFixture.writes.length,0);f.unmount();
});
await check('real page still shows genuine residency mismatch and restores the prior preference',async()=>{
  const f=await pageFixture(fixture({device:46,startup:1}),{initial:true,mismatch:true});
  f.toggle('任务栏常驻',false);await flush();assert.equal(f.trayPreference,true);
  const toggle=descendants(f.container).find(n=>n.props['data-label']==='任务栏常驻');
  assert.equal(toggle.props['data-model'],true);assert.equal(toggle.props.disabled,false);
  assert(f.container.textContent.includes('任务栏常驻设置失败：任务栏设置未被系统接受'));
  assert.equal(f.registryFixture.writes.length,0);f.unmount();
});

console.log('\n'+cases.length+' checks passed. All registry/task operations were mocks or source inspection; no real system changes.');
