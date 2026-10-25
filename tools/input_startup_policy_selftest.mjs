import os from 'node:os';
import {runtimeBundle} from './boot_modes_fixture/harness.mjs';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import { createRequire } from 'node:module';
const require = createRequire(path.join(process.cwd(), 'package.json'));
const vue=require('vue'), { transformSync } = require('esbuild');
const root = process.cwd(), cases = [];
async function check(name, fn) { await fn(); cases.push(name); }
function evaluate(source, imports = {}, globals = {}) {
  const module = { exports: {} };
  vm.runInNewContext(transformSync(source, {loader:'ts',format:'cjs'}).code, {module,exports:module.exports,console,TextEncoder,structuredClone,
    require(name) { if(name==='vue')return vue; if(name in imports)return imports[name];throw Error('Unexpected import '+name); },...globals});
  return module.exports;
}
function load(file, imports, globals) { return evaluate(fs.readFileSync(path.join(root,file),'utf8'),imports,globals); }
const personas=load('src/bridge/controllerPersona.ts');
const policy=load('src/bridge/inputStartupPolicy.ts',{'./controllerPersona':personas}), hash=load('src/bridge/inputConfigHash.ts'), hc=load('src/bridge/hcInputUtils.ts');
const baseline=load('src/bridge/settingsBaseline.ts',{'../../PowerControl/settings-baseline.json':JSON.parse(fs.readFileSync(path.join(root,'PowerControl/settings-baseline.json'),'utf8'))});
const repoImports={'./settingsBaseline':baseline,'./controllerPersona':personas,'./settingsSnapshot':load('src/bridge/settingsSnapshot.ts'),'./quickAppDefaults':load('src/bridge/quickAppDefaults.ts'),'./api':{},'./inputStartupPolicy':policy,'./inputConfigHash':hash,'./hcInputUtils':hc};
const repo=load('src/bridge/settingsRepository.ts',repoImports);
const plain=value=>JSON.parse(JSON.stringify(value));
await check('4个开机手柄、5个陀螺仪选项，默认与损坏配置均关闭',()=>{
  assert.deepEqual(plain(policy.STARTUP_PAD_OPTIONS).map(o=>o.label),['关闭虚拟手柄','SteamDeck','PS5','Xbox']);
  assert.deepEqual(plain(policy.STARTUP_GYRO_OPTIONS).map(o=>o.label),['关闭陀螺仪','FPS射击','赛车','自定义','Steam']);
  for(const value of [null,[],{},true,{virtualGamepadPersona:'invalid',gyroPreset:'fps'}])assert.deepEqual(plain(policy.normalizeInputStartupPreferences(value)),{virtualGamepadPersona:'disabled',gyroPreset:'off'});
  for(const [old,current] of [['xbox360','elite'],['dualshock4','dualsense-edge'],['dualsense','dualsense-edge']])assert.deepEqual(plain(policy.normalizeInputStartupPreferences({virtualGamepadPersona:old,gyroPreset:'fps'})),{virtualGamepadPersona:current,gyroPreset:'fps'});
  for(const pad of policy.STARTUP_PAD_OPTIONS)for(const gyro of policy.STARTUP_GYRO_OPTIONS){const n=policy.normalizeInputStartupPreferences({virtualGamepadPersona:pad.value,gyroPreset:gyro.value});assert.equal(n.gyroPreset,pad.value==='disabled'?'off':gyro.value);}
});
// The removed PowerView pad/gyro controls are replaced by actual scene-model/runtime behavior.
// Core inputStartupPolicy, repository and native assertions below remain intact.
const evidenceDir=process.argv[2]||fs.mkdtempSync(path.join(os.tmpdir(),'ymcc-input-startup-'));
fs.mkdirSync(evidenceDir,{recursive:true});
const {model:bootModel,boot,ports}=await runtimeBundle(evidenceDir);
if(!globalThis.window)globalThis.window=new EventTarget();
const scene=async()=>{ports.reset();return bootModel.createBootModeDocument(await boot.captureBootModeSnapshot());};
await check('模式草稿选择与启动保存均不操作当前人格、gyro或设备',async()=>{
 const document=await scene(),before=JSON.stringify(ports.settings.input);document.scenes.steam.overrides.input={...structuredClone(document.global.input),persona:'steamdeck',mouse:'steamdeck'};document.scenes.steam.overrides.gyro={motion:{enabled:true,preset:'racing'}};
 const saved=await boot.saveBootModes(document);assert.equal(JSON.stringify(ports.settings.input),before);const next=await boot.chooseBootMode('steam',saved,true);assert.equal(next.current,'global');assert.equal(next.boot,'steam');assert.equal(next.bootEnabled,true);assert.equal(JSON.stringify(ports.settings.input),before);assert(ports.calls.every(c=>c.op==='settings.update'||c.op==='ipc.bootMode.startup.identity'||c.op.startsWith('startup.')||c.op==='fx.set'&&c.value===false||c.op==='addon.set'&&c.value.enabled===false||c.op==='legacy.desktop.remove'&&c.value==='桌面模式-开机设置为桌面模式'||c.op==='registry.read'&&c.value.hive==='HKCU'&&c.value.key===String.raw`Software\Microsoft\Windows\CurrentVersion\Run`&&c.value.name==='Joyxoff'));
});
await check('关闭开机模式保留预设并同时关闭两个启动入口，不复活当前输入',async()=>{
 const document=await scene();document.boot='xbox-steam';document.bootEnabled=true;document.scenes['xbox-steam'].overrides.gyro={motion:{enabled:true,preset:'fps'}};ports.settings.extensions.bootModes=structuredClone(document);const before=JSON.stringify(ports.settings.input);
 const saved=await boot.chooseBootMode('xbox-steam',document,false);assert.equal(saved.bootEnabled,false);assert.deepEqual(saved.scenes,document.scenes);assert.equal(ports.state.xbox,false);assert.equal(ports.state.steam,false);assert.equal(JSON.stringify(ports.settings.input),before);
});
await check('失效修订拒绝保存且不覆盖现有选择；非法场景ID无法通过模型准入',async()=>{
 const document=await scene(),saved=await boot.saveBootModes(document);const before=JSON.stringify(ports.settings);await assert.rejects(boot.saveBootModes(document));assert.equal(JSON.stringify(ports.settings),before);assert.equal(saved.revision,1);for(const id of ['invalid','xbox360',null,3])assert.equal(bootModel.isBootModeId(id),false);
});
await check('启动平台写入失败保留选择与通知，刷新不会重放任何运行输入',async()=>{
 const document=await scene();ports.faults['startup.steam']=1;const saved=await boot.chooseBootMode('steam',document,true);assert.match(saved.bootNotice,/Steam/);const before=JSON.stringify(ports.settings.input);ports.calls.length=0;const refreshed=await boot.loadBootModes();assert.equal(refreshed.document.boot,'steam');assert.equal(JSON.stringify(ports.settings.input),before);assert.equal(ports.calls.length,0);
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
  assert(transaction.includes('std::call_once'));assert(transaction.indexOf('SettingsFileGuard guard')<transaction.indexOf('ymSettingsReadUnlocked()'));assert(transaction.includes('ymSettingsWriteDocumentUnlocked(prepared.dump(2))'));
  const compact=transaction.replace(/\s+/g,'');const guard=compact.indexOf('if(!applyInputStartupOnce())'),worker=compact.indexOf('CreateThread(nullptr,0,inputCaptureThreadProc');assert(guard>=0&&worker>=0&&guard<worker);
  assert(transaction.includes('"startup-input-settings-write-failed"'));assert(/startup-input-settings-write-failed[\s\S]*?return;/.test(transaction));
  assert(head.includes('"startupAppliedSession"'));assert(head.includes('input["applyStatus"] = "unknown"'));assert(head.includes('"hostAcknowledgedConfigHash"'));
});
console.log(JSON.stringify({suite:'Input startup frontend policy',passed:cases.length,cases,nativeOperations:'mocked',hardwareOperations:0},null,2));

