import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import { createRequire } from 'node:module';
const root = process.cwd();
const require = createRequire(path.join(root, 'package.json'));
const { transformSync } = require('esbuild');
const source = fs.readFileSync(path.join(root, 'src/bridge/trayResident.ts'), 'utf8');
const cases = [];
async function check(name, fn) { await fn(); cases.push(name); console.log('PASS:', name); }
function fixture(initial = false) {
  const f = { persisted: initial, replies: [], requests: [], saves: [], generation: 1, replyHook: null, saveHook: null };
  const repository = {
    getSettingsGeneration: () => f.generation,
    assertSettingsGeneration: generation => { if(generation !== f.generation) throw Error('settings generation changed'); },
    readSettingsSection: async section => { assert.equal(section,'tray'); return { resident:f.persisted, preserved:'extra field' }; },
    saveSettingsSection: async (section, value, generation) => {
      assert.equal(section,'tray'); assert.equal(generation === undefined || generation === f.generation,true);
      if(f.saveHook) await f.saveHook(value.resident);
      f.saves.push(value.resident); f.persisted = value.resident;
    },
  };
  const api = { tray: { setResident: async value => {
    f.requests.push(value);
    if(f.replyHook) return f.replyHook(value);
    return f.replies.length ? f.replies.shift() : value;
  } } };
  const module = { exports: {} };
  const js = transformSync(source,{loader:'ts',format:'cjs',target:'es2022'}).code;
  vm.runInNewContext(js,{module,exports:module.exports,require:name=>{
    if(name==='./api') return api;
    if(name==='./settingsRepository') return repository;
    throw Error('Unexpected dependency: '+name);
  },console});
  f.bridge = module.exports;
  return f;
}
await check('disable accepts the native false state and persists off without rollback',async()=>{
  const f=fixture(true); await f.bridge.setTrayResident(false);
  assert.equal(f.persisted,false); assert.deepEqual(f.requests,[false]); assert.deepEqual(f.saves,[false]);
  assert.equal(await f.bridge.readTrayResident(),false);
});
await check('enable accepts the native true state and persists on',async()=>{
  const f=fixture(); await f.bridge.setTrayResident(true);
  assert.equal(f.persisted,true); assert.deepEqual(f.saves,[true]);
});
await check('opposite returned states are genuine failures and restore the saved preference',async()=>{
  for(const requested of [true,false]){
    const f=fixture(!requested);f.replies.push(!requested);
    await assert.rejects(f.bridge.setTrayResident(requested),/任务栏设置未被系统接受/);
    assert.equal(f.persisted,!requested);assert.deepEqual(f.saves,[requested,!requested]);
  }
});
await check('undefined, null and non-boolean IPC replies are not accepted as success',async()=>{
  for(const reply of [undefined,null,0,1,'false','true',{}]){
    const f=fixture(true);f.replies.push(reply);
    await assert.rejects(f.bridge.setTrayResident(false),/任务栏设置未被系统接受/);
    assert.equal(f.persisted,true);
  }
});
await check('IPC rejection rolls back preference, and later disable can still succeed',async()=>{
  const f=fixture(true);f.replyHook=async()=>{throw Error('IPC unavailable');};
  await assert.rejects(f.bridge.setTrayResident(false),/IPC unavailable/);assert.equal(f.persisted,true);
  f.replyHook=null;await f.bridge.setTrayResident(false);assert.equal(f.persisted,false);
});
await check('preference save failure never calls the native interface',async()=>{
  const f=fixture(true);f.saveHook=async()=>{throw Error('save unavailable');};
  await assert.rejects(f.bridge.setTrayResident(false),/save unavailable/);
  assert.equal(f.persisted,true);assert.equal(f.requests.length,0);
});
await check('rollback save failure is reported instead of claiming the previous state persisted',async()=>{
  const f=fixture(true);f.replies.push(true);f.saveHook=async value=>{if(value)throw Error('rollback blocked');};
  await assert.rejects(f.bridge.setTrayResident(false),/偏好回滚失败：rollback blocked/);assert.equal(f.persisted,false);
});
await check('queued enable then disable preserves native false and finishes off',async()=>{
  const f=fixture();await Promise.all([f.bridge.setTrayResident(true),f.bridge.setTrayResident(false)]);
  assert.equal(f.persisted,false);assert.deepEqual(f.requests,[true,false]);assert.deepEqual(f.saves,[true,false]);
});
await check('stale settings generation aborts before save or native mutation',async()=>{
  const f=fixture(true);const pending=f.bridge.setTrayResident(false);f.generation++;
  await assert.rejects(pending,/settings generation changed/);assert.equal(f.persisted,true);
  assert.equal(f.requests.length,0);assert.equal(f.saves.length,0);
});
await check('startup applies saved off as native false without rewriting preference',async()=>{
  const f=fixture();await f.bridge.applyTrayResident();assert.deepEqual(f.requests,[false]);assert.equal(f.saves.length,0);
});
await check('contract check: native tray.setResident returns requested resident state, not a success flag',()=>{
  const native=fs.readFileSync(path.join(root,'native/main.cpp'),'utf8');
  const handler=native.match(/ipc_on\("tray\.setResident",[\s\S]*?\n    \}\);/)?.[0];
  assert(handler);assert(handler.includes('return g_taskbarResident;'));
  assert(!handler.includes('return true;'));
});
console.log('\n'+cases.length+' checks passed. Preferences and IPC were mocked; no host state was queried or changed.');
