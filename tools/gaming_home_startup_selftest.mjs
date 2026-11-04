import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import crypto from 'node:crypto';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { createRequire } from 'node:module';
const root = process.cwd();
const require = createRequire(path.join(root, 'package.json'));
const vue = require('vue');
const { parse, compileScript } = require('vue/compiler-sfc');
const { transformSync } = require('esbuild');
const backupMode = process.argv.includes('--backup-storm');
const backupOut = backupMode ? path.resolve(process.env.YMCC_OS_BACKUP_OUT) : null;
const backupSourceRoot = process.env.YMCC_OS_BACKUP_SOURCE_ROOT || root;
const backupReceipts = [], backupPins = [], backupOwned = [];
const digest = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const product = rel => path.join(backupMode ? backupSourceRoot : root, rel);
const selectedOriginalBackups = new Set([
  'backup failure aborts before any mutation',
  'failed user-hive write rolls back the newly added OEM value, not the entire key',
  'write failure after mutation restores both original values',
  'concurrent external OEM changes are not overwritten during rollback',
  'invalid value types and failed readbacks never report successful enablement',
  'legacy task is never recreated, replacement XML is safe and Steam never kills Xbox',
  'rollback failure is reported explicitly with the backup retained',
  'actual bridge reads create no backups, scheduled task calls or registry writes',
  'actual bridge backs up and retires old task without altering an existing correct configuration',
  'failed legacy task export never deletes the task and reports partial completion',
  'actual bridge serializes explicit enable/disable, keeps home and backs up both changes',
]);
if (backupMode) fs.mkdirSync(backupOut, { recursive: false });
const backupInputs = backupMode ? ['src/bridge/yeman.ts', 'PowerControl/Repair-GamingHandheld.ps1'].map(rel => product(rel))
  .concat([fileURLToPath(import.meta.url), ...['src/bridge/gamingHomePolicy.ts','src/bridge/frameRateModel.ts','src/bridge/tdpRuntimeState.ts','PowerControl/Xbox大屏游戏模式.xml','PowerControl/YeManSteam2.bat'].map(p=>path.join(root,p)), process.execPath])
  .map(p => ({ path: p, sha256: digest(fs.readFileSync(p)) })) : [];
const cases = [];
async function check(name, fn) {
  if (backupMode && !selectedOriginalBackups.has(name) && !name.startsWith('backup storm:')) return;
  if (!backupMode) { await fn(); cases.push(name); console.log('PASS:', name); return; }
  if (process.env.YMCC_OS_BACKUP_CASE && process.env.YMCC_OS_BACKUP_CASE !== name) return;
  try { await fn(); cases.push(name); backupReceipts.push({ name, pass: true }); console.log('PASS:', name); }
  catch (error) { backupReceipts.push({ name, pass: false, error: String(error), stack: error.stack }); console.log('FAIL:', name, error.stack); }
}
function evaluate(source, imports = {}, globals = {}) {
  const module = { exports: {} };
  const js = transformSync(source, { loader: 'ts', format: 'cjs', target: 'es2022' }).code;
  vm.runInNewContext(js, { module, exports: module.exports, require: name => {
    if (name === 'vue') return vue;
    if (name in imports) return imports[name];
    throw Error('Unexpected import: ' + name);
  }, console, TextEncoder, setTimeout, clearTimeout, ...globals });
  return module.exports;
}
const policy = evaluate(fs.readFileSync(path.join(root, 'src/bridge/gamingHomePolicy.ts'), 'utf8'));
const frameRateModel = evaluate(fs.readFileSync(path.join(root, 'src/bridge/frameRateModel.ts'), 'utf8'));
const tdpRuntimeState = evaluate(fs.readFileSync(path.join(root, 'src/bridge/tdpRuntimeState.ts'), 'utf8'));
const BIOS = 'HARDWARE\\DESCRIPTION\\System\\BIOS';
const { GAMING_OEM_KEY: OEM, GAMING_CONFIG_KEY: CFG } = policy;
const key = (r, p, n) => [r, p, n].join('|');
function fixture({ device = null, startup = 0, home = 'Microsoft.GamingApp_8wekyb3d8bbwe!Microsoft.Xbox.App', manufacturer = 'ASUSTeK COMPUTER INC.', product = 'ROG Ally RC71L_RC71L' } = {}) {
  const values = new Map(), reads = [], writes = [], removed = [], backups = [], events = [];
  const set = (r, p, n, v) => v == null ? values.delete(key(r,p,n)) : values.set(key(r,p,n),v);
  set('HKLM',OEM,'DeviceForm',device); set('HKCU',CFG,'StartupToGamingHome',startup); set('HKCU',CFG,'GamingHomeApp',home);
  set('HKLM','HARDWARE\\DESCRIPTION\\System\\BIOS','SystemManufacturer',manufacturer);
  set('HKLM','HARDWARE\\DESCRIPTION\\System\\BIOS','SystemProductName',product);
  const f = { values, reads, writes, removed, backups, events, failBackup: false, writeHook: null };
  f.port = {
    async read(r,p,n) { reads.push([r,p,n]); if (p === BIOS) throw Error('BIOS identity must not be queried');
      return values.has(key(r,p,n)) ? values.get(key(r,p,n)) : null; },
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
});
await check('read-only snapshots preserve an enabled startup without a legacy task', async () => {
  const f=fixture({device:46,startup:1}); for(let i=0;i<3;i++)assert((await policy.readGamingHomeState(f.port)).startupEnabled);
  assert.equal(f.writes.length,0); assert.equal(f.backups.length,0);
});
await check('missing DeviceForm is configured to 46/1, backed up first, Xbox home unchanged', async () => {
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
for (const [label, manufacturer, product] of [
  ['ASUS desktop', 'ASUSTeK COMPUTER INC.', 'System Product Name'],
  ['Lenovo handheld', 'LENOVO', 'Legion Go'],
  ['MSI handheld', 'Micro-Star International Co., Ltd.', 'Claw'],
  ['Valve handheld', 'Valve', 'Steam Deck'],
  ['ordinary desktop', 'Dell Inc.', 'Desktop PC'],
  ['missing BIOS identity', null, null],
  ['invalid BIOS identity', 123, {}],
]) {
  await check(label + ' can enable startup with missing/existing DeviceForm and no BIOS access', async () => {
    for (const device of [null, 3]) {
      const f=fixture({device,manufacturer,product}), home=await f.get('HKCU',CFG,'GamingHomeApp');
      const state=await policy.setGamingHomeStartup(f.port,true);
      assert.equal(state.deviceForm,46);assert.equal(state.startupValue,1);assert.equal(state.homeApp,home);
      assert.equal(f.events[0],'backup');assert.equal(f.backups[0].deviceForm,device);
      assert.deepEqual(f.writes.map(w=>w[2]),['DeviceForm','StartupToGamingHome']);
      assert(!f.reads.some(([,p])=>p===BIOS));
    }
  });
}
await check('observing an unconfigured desktop never changes configuration', async () => {
  const f=fixture({device:3,manufacturer:'Dell Inc.',product:'Desktop PC'});
  const state=await policy.readGamingHomeState(f.port);assert.equal(state.deviceForm,3);assert(!state.startupEnabled);
  assert.equal(f.writes.length,0);assert.equal(f.backups.length,0);assert(!f.reads.some(([,p])=>p===BIOS));
});
await check('disabling on an unconfigured desktop leaves DeviceForm untouched', async () => {
  const f=fixture({device:3,startup:1,manufacturer:'Dell Inc.',product:'Desktop PC'});
  const state=await policy.setGamingHomeStartup(f.port,false);assert.equal(state.deviceForm,3);assert.equal(state.startupValue,0);
  assert.deepEqual(f.writes.map(w=>w[2]),['StartupToGamingHome']);assert(!f.reads.some(([,p])=>p===BIOS));
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
const inputPolicy=backupMode ? null : evaluate(fs.readFileSync(path.join(root,'src/bridge/inputStartupPolicy.ts'),'utf8'));
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
await check('real page enables an ordinary non-ROG desktop without Steam or tray coupling; disable retains DeviceForm',async()=>{
  const f=await pageFixture(fixture({device:3,manufacturer:'Dell Inc.',product:'Desktop PC'}));
  f.toggle('开机进入Xbox全屏游戏模式',true);await flush();
  const startup=descendants(f.container).find(n=>n.props['data-label']==='开机进入Xbox全屏游戏模式');
  assert.equal(startup.props['data-model'],true);assert(!f.container.textContent.includes('全屏游戏模式设置失败'));
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
  const bridge=fs.readFileSync(product('src/bridge/yeman.ts'),'utf8');
  const def=bridge.match(/\{ name: 'Xbox大屏游戏模式'[^\n]+/)[0];assert(!def.includes('xml:'));
  assert(bridge.includes('gamingHomeWriteTail = action.then'));assert(bridge.includes("saveGamingBackup('.task.xml'") || bridge.includes("gamingBackupPath('.task.xml')"));
  const xml=fs.readFileSync(path.join(root,'PowerControl/Xbox大屏游戏模式.xml'),'utf16le');assert(!xml.includes('physpanel.exe'));
  assert(xml.includes('Repair-GamingHandheld.ps1'));assert(xml.includes('S-1-5-18'));
  const ps=fs.readFileSync(product('PowerControl/Repair-GamingHandheld.ps1'),'utf8');
  assert(ps.includes('-Name DeviceForm -PropertyType DWord -Value 46'));assert(!ps.includes('HKCU:'));
  assert(!/\$isRog|SystemManufacturer|SystemProductName|recognized ROG|\\System\\BIOS/.test(ps));
  assert(ps.includes('$Inspect -or $current -eq 46'));assert(ps.includes('Administrator permission is required'));
  const save = ps.includes('$backup = Save-GamingDeviceFormBackup') ? '$backup = Save-GamingDeviceFormBackup' : 'Set-Content -LiteralPath $backup';
  assert(ps.includes(save));assert(ps.indexOf(save) < ps.indexOf('New-ItemProperty -LiteralPath $oemPath'));
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
    async mkdir(p){directories.add(p);return true;},async writeTextFileAtomic(p,c,_timeout,expected){if(expected?.expectedContent===null&&files.has(p))throw Error('backup already exists');files.set(p,c);},
    async readTextFile(p){if(!files.has(p))throw Error('backup missing');return files.get(p);},
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
  }},'./gamingHomePolicy':policy,'./frameRateModel':frameRateModel,'./tdpRuntimeState':tdpRuntimeState,'./rtssOverlay':{},'./settingsRepository':{},'./tdpCircuitBreaker':{},'./controllerShortcutRules':{OEM_EMPTY_SNAPSHOT:{}}};
  const bridge=backupMode ? backupBridge(api, (name,a)=>imports['./ipc'].invoke(name,a), 'C:\\owned-memory') : evaluate(fs.readFileSync(path.join(root,'src/bridge/yeman.ts'),'utf8'),imports,{window:{addEventListener(){},removeEventListener(){}}});
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
  const f=bridgeFixture(fixture({device:3,manufacturer:'LENOVO',product:'Legion Go'}));
  await Promise.all([f.bridge.writeGamingHomeStartup(true),f.bridge.writeGamingHomeStartup(false)]);
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

if (!backupMode) console.log('\n'+cases.length+' checks passed. All registry/task operations were mocks or source inspection; no real system changes.');



const backupPsConsumer = "param([string]$Source,[string]$OwnedRoot,[string]$Output)\n$ErrorActionPreference='Stop'\n$script:Owned=[IO.Path]::GetFullPath($OwnedRoot)\nif(Test-Path -LiteralPath $script:Owned){throw 'owned root must be fresh'}\n[IO.Directory]::CreateDirectory($script:Owned)|Out-Null\n$tokens=$null;$parseErrors=$null\n$ast=[Management.Automation.Language.Parser]::ParseFile($Source,[ref]$tokens,[ref]$parseErrors)\nif($parseErrors.Count){throw 'source parse failed'}\n$statements=@($ast.EndBlock.Statements)\n$start=@($statements|Where-Object {$_ -is [Management.Automation.Language.AssignmentStatementAst] -and $_.Left.Extent.Text -eq '$directory'})[0]\n$stop=@($statements|Where-Object {$_.Extent.StartOffset -gt $start.Extent.StartOffset -and $_.Extent.Text.StartsWith('if (-not (Test-Path -LiteralPath $oemPath))')})[0]\nif(!$start -or !$stop){throw 'exact backup block absent'}\n$raw=[IO.File]::ReadAllText($Source)\n$block=$raw.Substring($start.Extent.StartOffset,$stop.Extent.StartOffset-$start.Extent.StartOffset)\n$funcs=@($statements|Where-Object {$_ -is [Management.Automation.Language.FunctionDefinitionAst] -and $_.Name -like '*Gaming*Backup*'})\n$selection=(@($funcs|ForEach-Object {$_.Extent.Text}) -join \"`n\")+\"`n\"+$block\nif($selection -match 'New-ItemProperty|Get-ItemProperty|WindowsIdentity|powercfg|schtasks|Start-Process'){throw 'non-file capability in selection'}\n[IO.File]::WriteAllText((Join-Path $Output 'actual-ps-selection.ps1'),$selection,[Text.UTF8Encoding]::new($false))\nforeach($f in $funcs){Invoke-Expression $f.Extent.Text}\nif(Get-Command Read-GamingBackupText -ErrorAction SilentlyContinue){\n $script:ActualRead=(Get-Command Read-GamingBackupText).ScriptBlock\n function Read-GamingBackupText {\n  param([string]$Path)\n  $p=Assert-Owned $Path\n  if($p.EndsWith('.json')){\n   if($script:Fault -eq 'published-short'){[IO.File]::WriteAllText($p,'{bad')}\n   if($script:Fault -eq 'published-deny'){$script:Held=[IO.FileStream]::new($p,[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)}\n  }\n  &$script:ActualRead $p\n }\n}\n$script:Cases=@();$script:Events=@();$script:Fault='';$script:Held=$null\nfunction Assert-Owned([string]$Path){\n  $p=[IO.Path]::GetFullPath($Path)\n  if(!$p.StartsWith($script:CaseRoot+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase) -and $p -ne $script:CaseRoot){throw ('outside owned '+$p)}\n  if($Path -match '^HK|^Registry:'){throw 'registry capability forbidden'}\n  return $p\n}\nfunction Test-Path {param([string]$LiteralPath) [IO.File]::Exists((Assert-Owned $LiteralPath)) -or [IO.Directory]::Exists((Assert-Owned $LiteralPath))}\nfunction New-Item {param([string]$Path,[string]$ItemType) if($ItemType -ne 'Directory'){throw 'file port directory only'};[IO.Directory]::CreateDirectory((Assert-Owned $Path))}\nfunction Remove-Item {param([string]$LiteralPath,[switch]$Force) [IO.File]::Delete((Assert-Owned $LiteralPath))}\nfunction Get-Date {param([string]$Format) return '20261101-000000-000'}\nfunction Set-Content {\n param([string]$LiteralPath,[Parameter(ValueFromPipeline=$true)]$Value,[string]$Encoding,[switch]$NoNewline)\n $p=Assert-Owned $LiteralPath;$text=[string]$Value\n $script:Events+=@{op='Set-Content';path=$p;fault=$script:Fault}\n if($script:Fault -eq 'throw'){throw 'injected write refusal'}\n if($script:Fault -eq 'silent'){return}\n if($script:Fault -eq 'short'){$text=$text.Substring(0,[int]($text.Length/2))}\n if($script:Fault -eq 'wrong-root'){$text='[]'}\n if($script:Fault -eq 'future'){$text='{\"Schema\":999,\"Debt\":\"KEEP\"}'}\n if(!$NoNewline){$text+=\"`r`n\"}\n [IO.File]::WriteAllText($p,$text,[Text.UTF8Encoding]::new($true))\n if($script:Fault -eq 'missing'){[IO.File]::Delete($p)}\n if($script:Fault -eq 'locked'){$script:Held=[IO.FileStream]::new($p,[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)}\n if($script:Fault -eq 'publish-race'){\n   $at=$p.IndexOf('.json',[StringComparison]::OrdinalIgnoreCase)\n   if($at -ge 0){$dest=$p.Substring(0,$at+5);[IO.File]::WriteAllText($dest,'{\"Schema\":999,\"Debt\":\"EXTERNAL\"}')}\n }\n}\n# The only executable source is the AST-selected file backup region. The OS write is an inert continuation marker.\nfunction Run-Backup($Before=3){\n $current=$Before;$identity=[pscustomobject]@{User=[pscustomobject]@{Value='S-1-5-21-100-200-300-1001'}}\n Invoke-Expression $script:BackupBlock\n $script:Events+=@{op='FAKE-DeviceForm-write';value=46}\n return $backup\n}\n$script:BackupBlock=$block\nfunction Check([string]$Name,[scriptblock]$Body){\n if($env:YMCC_OS_BACKUP_CASE -and $env:YMCC_OS_BACKUP_CASE -ne $Name){return}\n $script:CaseRoot=Join-Path $script:Owned ('case-'+($script:Cases.Count+1));[IO.Directory]::CreateDirectory($script:CaseRoot)|Out-Null\n $env:ProgramData=$script:CaseRoot;$script:Events=@();$script:Fault='';$script:Held=$null\n $pass=$true;$errorText=''\n try{&$Body}catch{$pass=$false;$errorText=$_.Exception.ToString()}\n finally{if($script:Held){$script:Held.Dispose();$script:Held=$null}}\n $script:Cases+=@{name=$Name;pass=$pass;error=$errorText;ownedRoot=$script:CaseRoot;events=@($script:Events)}\n}\nfunction Must([bool]$Ok,[string]$Why){if(!$Ok){throw $Why}}\nfunction Assert-Blocked([scriptblock]$Action){$threw=$false;try{&$Action|Out-Null}catch{$threw=$true;$script:Events+=@{op='actual-or-injected-file-refusal';message=$_.Exception.Message;hresult=$_.Exception.HResult}};Must $threw 'backup failure was acknowledged as success';Must (!@($script:Events|Where-Object op -eq 'FAKE-DeviceForm-write').Count) 'backup failure authorized fake OS mutation'}\nCheck 'PS typed original and proven absence retained with verified file before fake OS write' {\n foreach($v in @(3,$null,4294967295,[int]-1)){\n  $expected=if($v -is [int] -and $v -lt 0){4294967295}else{$v}\n  $script:Events=@();$p=Run-Backup $v;$record=[IO.File]::ReadAllText($p)|ConvertFrom-Json\n  Must ($record.Schema -eq 1 -and $record.Value -eq 'DeviceForm' -and $record.Target -eq 46) 'record identity mismatch'\n  Must ($record.Before -eq $expected) 'original value guessed or lost'\n  Must ($script:Events[-1].op -eq 'FAKE-DeviceForm-write') 'continuation not reached'\n }\n}\nforeach($fault in @('throw','short','silent','wrong-root','future','missing','locked','publish-race','published-short','published-deny')){\n $capture=$fault\n Check ('PS '+$fault+' failure blocks fake OS write and explicit retry works') {\n  $script:Fault=$capture\n  Assert-Blocked {Run-Backup 7}\n  if($script:Held){$script:Held.Dispose();$script:Held=$null}\n  $saved=@{};Get-ChildItem -LiteralPath $script:CaseRoot -File -Recurse|ForEach-Object {$saved[$_.FullName]=[IO.File]::ReadAllBytes($_.FullName)}\n  $script:Fault='';$p=Run-Backup 7\n  Must (([IO.File]::ReadAllText($p)|ConvertFrom-Json).Before -eq 7) 'retry lost original'\n  foreach($key in $saved.Keys){Must ([Convert]::ToBase64String([IO.File]::ReadAllBytes($key)) -eq [Convert]::ToBase64String($saved[$key])) 'retry modified original failure artifact'}\n }\n}\nCheck 'PS collision keeps malformed and future original backup bytes' {\n $dir=Join-Path $env:ProgramData 'YeMan/gaming-home-backups';[IO.Directory]::CreateDirectory($dir)|Out-Null\n $existing=Join-Path $dir 'DeviceForm-20261101-000000-000.json';[IO.File]::WriteAllText($existing,'{\"Schema\":999,\"Debt\":\"KEEP\"}')\n $p=Run-Backup 9\n Must ([IO.File]::ReadAllText($existing) -eq '{\"Schema\":999,\"Debt\":\"KEEP\"}') 'old future original overwritten'\n Must ($p -ne $existing) 'reused original backup path'\n}\nCheck 'PS unknown original type cannot be replaced by target defaults' {\n foreach($bad in @('unknown',@{},[long]-1,4294967296,$true,3.5)){$script:Events=@();Assert-Blocked {Run-Backup $bad}}\n}\n$pins=@($funcs|ForEach-Object {@{name=$_.Name;start=$_.Extent.StartOffset;end=$_.Extent.EndOffset;sha256=([BitConverter]::ToString([Security.Cryptography.SHA256]::Create().ComputeHash([Text.Encoding]::UTF8.GetBytes($_.Extent.Text)))).Replace('-','').ToLowerInvariant()}})\n$assemblies=@([AppDomain]::CurrentDomain.GetAssemblies()|Where-Object {$_.Location -and [IO.File]::Exists($_.Location)}|ForEach-Object {@{path=$_.Location;sha256=([BitConverter]::ToString([Security.Cryptography.SHA256]::Create().ComputeHash([IO.File]::ReadAllBytes($_.Location)))).Replace('-','').ToLowerInvariant()}})\n$report=[pscustomobject]@{cases=@($script:Cases);pass=(!@($script:Cases|Where-Object {!$_.pass}).Count);source=$Source;functionPins=$pins;runtimeAssemblies=$assemblies;wholeScriptExecuted=$false;realOSActions=0;psVersion=$PSVersionTable.PSVersion.ToString()}|ConvertTo-Json -Depth 12\n[IO.File]::WriteAllText((Join-Path $Output 'ps-result.json'),$report,[Text.UTF8Encoding]::new($false))\nif(@($script:Cases|Where-Object {!$_.pass}).Count){exit 1}\n";
function backupBridge(api, invoke, ownedRoot, globals = {}) {
  const ts = require('typescript');
  const sourcePath = product('src/bridge/yeman.ts'), source = fs.readFileSync(sourcePath, 'utf8');
  const tree = ts.createSourceFile(sourcePath, source, ts.ScriptTarget.Latest, true, ts.ScriptKind.TS);
  const names = new Set(['PC_DIR','join','TASK_FOLDER','BOOT_CONTROL_CENTER_TASK','BOOT_RTSS_TASK','TASKS','getTaskDef','TASK_FILE_ROOT','taskPath','taskPathCandidates','taskFilePath','taskDefinedAtPath','deleteTask','gamingBackupSequence','gamingBackupPath','gamingHomeRegistry','readGamingHomeConfiguration','readGamingHomeStartup','retireLegacyGamingTask','gamingHomeWriteTail','writeGamingHomeStartup','restoreAllYemanTasks']);
  const actual = [];
  for (const node of tree.statements) {
    const declared = ts.isVariableStatement(node) ? node.declarationList.declarations.map(d => d.name.getText(tree)) : [node.name?.getText(tree)];
    if (declared.some(name => names.has(name) || /^gamingBackup|^saveGamingBackup/.test(name || ''))) {
      assert(declared.every(name => names.has(name) || /^gamingBackup|^saveGamingBackup/.test(name || '')));
      actual.push(node.getText(tree));
      if (!backupPins.some(pin => pin.start === node.getStart(tree))) backupPins.push({names:declared,start:node.getStart(tree),end:node.end,sha256:digest(node.getText(tree))});
    }
  }
  const emitted = actual.join('\n') + '\nPC_DIR=__ownedRoot;\nexport { gamingHomeRegistry, retireLegacyGamingTask };';
  fs.writeFileSync(path.join(backupOut, 'actual-bridge-selection.ts'), emitted);
  return evaluate(emitted, {}, {fs:api.fs, shell:api.shell, registry:api.registry, invoke,
    readGamingHomeState:policy.readGamingHomeState, setGamingHomeStartup:policy.setGamingHomeStartup,
    __ownedRoot:ownedRoot, TextEncoder, ...globals});
}
function ownedBackupFixture({legacy=false,device=3,startup=0,fault=null,xml=undefined}={}) {
  const ownedRoot = path.join(backupOut,'owned','case-'+(backupOwned.length+1));
  fs.mkdirSync(ownedRoot,{recursive:true});
  const guard = p => {const full=path.resolve(p);assert(full===ownedRoot||full.startsWith(ownedRoot+path.sep),'outside owned backup '+full);return full;};
  const canonical='野蛮优化整合系统\\Xbox大屏游戏模式', tasks=new Set(legacy?[canonical]:[]), events=[];
  const state={DeviceForm:device,StartupToGamingHome:startup,GamingHomeApp:'Original.Home!App'};
  const initial=JSON.stringify(state);
  let mode=fault, sequence=0, taskXml=xml===undefined?'<Task version="1.4"><RegistrationInfo><Description>unknown preserved</Description></RegistrationInfo></Task>':xml;
  const filePort={
    async exists(p){if(p.startsWith('C:\\Windows\\System32\\Tasks\\'))return tasks.has(p.slice('C:\\Windows\\System32\\Tasks\\'.length));return fs.existsSync(guard(p));},
    async mkdir(p){fs.mkdirSync(guard(p),{recursive:true});return true;},
    async readTextFile(p,max,options){p=guard(p);events.push({op:'readback',path:p,bounded:options?.bounded===true});if(mode==='deny')throw Error('injected owned read denial');if(mode==='missing'&&fs.existsSync(p))fs.unlinkSync(p);const bytes=fs.readFileSync(p);if(bytes.length>max)throw Error('bounded read limit');return bytes.toString('utf8');},
    async writeTextFileAtomic(p,text,_timeout,expected){
      p=guard(p);events.push({op:'backup',path:p,expected:expected??null,fault:mode});
      if(mode==='throw')throw Error('injected backup write failure');
      if(mode==='race')fs.writeFileSync(p,'{"schema":999,"Debt":"EXTERNAL"}');
      if(expected?.expectedContent===null&&fs.existsSync(p))throw Error('source appeared before publish');
      if(mode==='silent')return;
      let value=text;
      if(mode==='short')value=text.slice(0,Math.floor(text.length/2));
      if(mode==='empty')value='';
      if(mode==='wrong-root')value='[]';
      if(mode==='duplicate')value='{"schema":1,"schema":999}';
      if(mode==='future')value='{"schema":999,"Debt":"KEEP"}';
      if(mode==='oversize')value='x'.repeat((1<<20)+1);
      const temp=p+'.owned-'+(++sequence)+'.tmp';const fd=fs.openSync(temp,'wx');
      try {fs.writeFileSync(fd,value);fs.fsyncSync(fd);}finally{fs.closeSync(fd);}
      fs.renameSync(temp,p);
      if(mode==='task-change')taskXml='<Task version="1.4"><Actions><Exec><Command>external</Command></Exec></Actions></Task>';
    }
  };
  const registry={async read(_r,_k,n){assert(n in state);return state[n];},async write(_r,_k,n,v){assert(n in state);events.push({op:'fake-registry',name:n});state[n]=v;return true;}};
  const shell={async run(program,args){assert.equal(program,'schtasks');const task=args[args.indexOf('/TN')+1];events.push({op:'fake-task',args});
    if(args.includes('/Query'))return {exitCode:tasks.has(task)?0:1,stdout:args.includes('/XML')?taskXml:'task',stderr:''};
    if(args.includes('/Delete')){tasks.delete(task);return {exitCode:0,stdout:'',stderr:''};}throw Error('unapproved task port');
  }};
  class FixedDate extends Date {constructor(...args){super(...(args.length?args:['2026-11-01T00:00:00.000Z']));}}
  const bridge=backupBridge({fs:filePort,registry,shell},async(n,a)=>{assert.equal(n,'registry.delete');state[a.name]=null;return true;},ownedRoot,{Date:FixedDate});
  const entry={ownedRoot,events};backupOwned.push(entry);
  return {bridge,ownedRoot,guard,state,initial,tasks,canonical,events,set fault(value){mode=value;},get xml(){return taskXml;}};
}
async function runBackupStorm() {
  await check('backup storm: real owned JSON and XML roundtrip precedes fake OS mutation',async()=>{
    const f=ownedBackupFixture({legacy:true});await f.bridge.writeGamingHomeStartup(true);
    const writes=f.events.filter(e=>e.op==='backup');assert.equal(writes.length,2);
    const json=JSON.parse(fs.readFileSync(writes[0].path,'utf8'));assert.equal(json.schema,1);assert.equal(json.before.deviceForm,3);assert.equal(json.before.homeApp,'Original.Home!App');
    assert.equal(fs.readFileSync(writes[1].path,'utf8'),f.xml);assert.equal(f.tasks.size,0);
    assert(f.events.indexOf(writes[0])<f.events.findIndex(e=>e.op==='fake-registry'));
  });
  for (const fault of ['throw','short','silent','empty','wrong-root','duplicate','future','oversize','missing','deny']) {
    await check('backup storm: '+fault+' JSON failure blocks fake registry and retry preserves evidence',async()=>{
      const f=ownedBackupFixture({fault});await assert.rejects(f.bridge.writeGamingHomeStartup(true));
      assert.equal(JSON.stringify(f.state),f.initial);assert(!f.events.some(e=>e.op==='fake-registry'));
      const before=ownedBackupFiles(f.ownedRoot);f.fault=null;await f.bridge.writeGamingHomeStartup(true);
      for(const [p,bytes] of before)assert.deepEqual(fs.readFileSync(p),bytes);
      assert.equal(f.state.DeviceForm,46);assert.equal(f.state.StartupToGamingHome,1);
    });
  }
  await check('backup storm: colliding malformed future backups remain byte exact',async()=>{
    const f=ownedBackupFixture();const dir=path.join(f.ownedRoot,'gaming-home-backups');fs.mkdirSync(dir);
    for(const [i,raw] of ['{bad','{"schema":999,"Debt":"KEEP"}'].entries())fs.writeFileSync(path.join(dir,'2026-11-01T00-00-00-000Z-'+(i+1)+'.json'),raw);
    const before=ownedBackupFiles(dir);await f.bridge.writeGamingHomeStartup(true);
    for(const [p,bytes] of before)assert.deepEqual(fs.readFileSync(p),bytes);
  });
  await check('backup storm: source appearance before publish keeps external bytes and blocks fake registry',async()=>{
    const f=ownedBackupFixture({fault:'race'});await assert.rejects(f.bridge.writeGamingHomeStartup(true));assert.equal(JSON.stringify(f.state),f.initial);
    const files=ownedBackupFiles(f.ownedRoot);assert.equal(files.size,1);assert.equal([...files.values()][0].toString(),'{"schema":999,"Debt":"EXTERNAL"}');
  });
  for(const fault of ['short','silent','deny'])await check('backup storm: '+fault+' XML failure blocks task deletion then explicit retry works',async()=>{
    const f=ownedBackupFixture({legacy:true,device:46,startup:1,fault});await assert.rejects(f.bridge.writeGamingHomeStartup(true));
    assert(f.tasks.has(f.canonical));assert(!f.events.some(e=>e.op==='fake-task'&&e.args.includes('/Delete')));
    const before=ownedBackupFiles(f.ownedRoot);f.fault=null;await f.bridge.writeGamingHomeStartup(true);assert.equal(f.tasks.size,0);
    for(const [p,bytes]of before)assert.deepEqual(fs.readFileSync(p),bytes);
  });
  for(const xml of ['',null,42,'x'.repeat((1<<20)+1)])await check('backup storm: invalid XML export '+String(xml).slice(0,12)+' cannot authorize task deletion',async()=>{
    const f=ownedBackupFixture({legacy:true,device:46,startup:1,xml});await assert.rejects(f.bridge.writeGamingHomeStartup(true));assert(f.tasks.has(f.canonical));
  });
  await check('backup storm: late external task definition remains owned by external source',async()=>{
    const f=ownedBackupFixture({legacy:true,device:46,startup:1,fault:'task-change'});await assert.rejects(f.bridge.writeGamingHomeStartup(true));assert(f.tasks.has(f.canonical));
    assert(!f.events.some(e=>e.op==='fake-task'&&e.args.includes('/Delete')));
  });
  if (!process.env.YMCC_OS_BACKUP_CASE || process.env.YMCC_OS_BACKUP_CASE.startsWith('PS ')) {
    const ps=process.env.YMCC_OS_BACKUP_POWERSHELL;
    assert(ps && fs.existsSync(ps),'explicit local PS executable required');
    const runner=path.join(backupOut,'owned-ps-consumer.ps1');fs.writeFileSync(runner,backupPsConsumer);
    const command=['-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',runner,'-Source',product('PowerControl/Repair-GamingHandheld.ps1'),'-OwnedRoot',path.join(backupOut,'ps-owned'),'-Output',backupOut];
    const at=Date.now(),child=spawnSync(ps,command,{cwd:root,timeout:30000,encoding:'utf8',windowsHide:true});
    fs.writeFileSync(path.join(backupOut,'ps-console.log'),(child.stdout||'')+'\n'+(child.stderr||''));
    fs.writeFileSync(path.join(backupOut,'ps-PROCESS.json'),JSON.stringify({pid:child.pid,command:[ps,...command],elapsedMs:Date.now()-at,exitCode:child.status,signal:child.signal,timeoutMs:30000,error:child.error?.message,tool:{path:ps,sha256:digest(fs.readFileSync(ps))}},null,2));
    const resultPath=path.join(backupOut,'ps-result.json');
    if(!fs.existsSync(resultPath))backupReceipts.push({name:'PS actual AST consumer execution',pass:false,error:'missing PS receipt; '+child.stderr});
    else {const actual=JSON.parse(fs.readFileSync(resultPath,'utf8').replace(/^\uFEFF/,''));for(const item of actual.cases)if(!process.env.YMCC_OS_BACKUP_CASE||item.name===process.env.YMCC_OS_BACKUP_CASE)backupReceipts.push(item);}
  }
  const files=ownedBackupFiles(backupOut);
  const identities=[...new Set([...Object.values(require.cache).map(m=>m.filename),require.resolve('@esbuild/win32-x64/esbuild.exe')])].filter(p=>fs.existsSync(p)).map(p=>({path:p,sha256:digest(fs.readFileSync(p))}));
  const inputsAfter=backupInputs.map(pin=>({path:pin.path,sha256:digest(fs.readFileSync(pin.path))}));
  const receipt={selection:'original gaming_home_startup_selftest --backup-storm',cases:backupReceipts.length,passed:backupReceipts.filter(x=>x.pass).length,failed:backupReceipts.filter(x=>!x.pass).length,
    pass:backupReceipts.every(x=>x.pass)&&JSON.stringify(backupInputs)===JSON.stringify(inputsAfter),receipts:backupReceipts,inputsBefore:backupInputs,inputsAfter,sourceDrift:JSON.stringify(backupInputs)!==JSON.stringify(inputsAfter),functionPins:backupPins,ownedCases:backupOwned,
    files:[...files].map(([p,b])=>({path:p,bytes:b.length,sha256:digest(b)})),runtimeModules:identities,hardwareRegistryTaskProgramActions:0,wholePowerShellScriptExecuted:false,wholeYemanModuleLoaded:false};
  fs.writeFileSync(path.join(backupOut,'RESULT.json'),JSON.stringify(receipt,null,2));
  console.log(JSON.stringify({cases:receipt.cases,passed:receipt.passed,failed:receipt.failed,sourceDrift:receipt.sourceDrift}));
  process.exitCode=receipt.pass?0:1;
}
function ownedBackupFiles(directory) {
  const result=new Map();
  for(const item of fs.readdirSync(directory,{withFileTypes:true})) {
    const p=path.join(directory,item.name);assert(!item.isSymbolicLink(),'owned evidence link');
    if(item.isDirectory())for(const [name,b]of ownedBackupFiles(p))result.set(name,b);
    else if(item.isFile())result.set(p,fs.readFileSync(p));
  }
  return result;
}
if (backupMode) await runBackupStorm();
