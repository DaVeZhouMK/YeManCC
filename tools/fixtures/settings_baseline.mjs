import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import { createRequire } from 'node:module';

/** Real repository + API + IPC. The only native surface is this memory store. */
export async function compileSettingsConsumer(root, options = {}) {
  const require = createRequire(path.join(root, 'package.json'));
  const { build } = require('esbuild');
  const compiled = await build({
    stdin: { contents: `export * as repo from './src/bridge/settingsRepository'; export * as baseline from './src/bridge/settingsBaseline';${options.uiSettings?"export * as ui from './src/bridge/uiSettings';":''}`, resolveDir: root, loader: 'ts' },
    bundle: true, write: false, metafile: true, format: 'cjs', platform: 'browser',
    define: { 'process.env.NODE_ENV': '"production"' },
  });
  options.onCompiled?.(compiled);
  return compiled.outputFiles[0].text;
}
export function createSettingsFixture(compiled, initial, initialBackup, options = {}) {
  const directory = options.storage?.directory || 'Q:\\ymcc-memory-fixture\\PowerControl';
  const file = directory + '\\yeman-settings.json';
  const key = value => String(value).replaceAll('/', '\\').toLowerCase();
  const state = { directory, file, files: new Map(), calls: [], writes: [], renames: [], readFailure: false, writeFailure: false, beforeWrite: null };
  const refreshStorage = () => {
    if (!options.storage) return;
    state.files.clear();
    for (const [target, raw] of options.storage.snapshot()) state.files.set(key(target), raw);
  };
  const seed = (target, value) => {
    const raw = typeof value === 'string' ? value : JSON.stringify(value);
    if (options.storage) { options.storage.seed(target, raw); refreshStorage(); }
    else state.files.set(key(target), raw);
  };
  if (initial !== undefined) seed(file, initial);
  if (initialBackup !== undefined) seed(file + '.bak', initialBackup);
  let receiver;
  const webview = {
    addEventListener(type, callback) { if (type !== 'message') throw Error('unexpected listener'); receiver = callback; },
    postMessage(message) {
      state.calls.push(message);
      queueMicrotask(async () => {
        try {
          const { cmd, args } = message;
          let result;
          if (cmd === 'settings.location') result = { directory, file, backup: file + '.bak' };
          else if (cmd === 'settings.read') {
            if (![key(file), key(file + '.bak')].includes(key(args.path))) throw Error('settings read escaped fixture');
            if (state.readFailure) throw Error('fixture sharing/read error');
            if (options.storage) result = await options.storage.read(args);
            else {
              const raw = state.files.get(key(args.path));
              result = { stamp: raw === undefined ? 'missing' : raw, unchanged: raw !== undefined && args.stamp === raw, content: raw ?? '' };
            }
          } else if (cmd === 'settings.write') {
            if (key(args.path) !== key(file)) throw Error('settings write escaped fixture');
            if (state.beforeWrite) await state.beforeWrite(args);
            if (state.writeFailure) throw Error('fixture atomic write interrupted');
            if (options.storage) {
              result = await options.storage.write(args);
              refreshStorage();
              if (result.ok) state.writes.push(args);
              receiver({ data: { id: message.id, result } });
              return;
            }
            let raw = state.files.get(key(file));
            // The memory terminal mirrors native recovery archival. This is
            // repository evidence only; the native file consumer remains separate.
            const documentValue = value => {
              try { const parsed = JSON.parse(value); return parsed && typeof parsed === 'object' && !Array.isArray(parsed) ? parsed : null; }
              catch { return null; }
            };
            const archive = target => {
              const original = state.files.get(key(target)); if (original === undefined) return;
              let sequence = 1; while (state.files.has(key(target + '.before-auto-recovery-' + sequence))) sequence++;
              state.files.set(key(target + '.before-auto-recovery-' + sequence), original);
              state.files.delete(key(target));
            };
            if (raw !== undefined && !documentValue(raw)) {
              const backup = state.files.get(key(file + '.bak')), recovered = documentValue(backup);
              if (recovered) {
                module.exports.baseline.assertSupportedSettingsBaseline(recovered);
                archive(file); raw = backup; state.files.set(key(file), backup);
              } else if (args.initialize === true) {
                archive(file); archive(file + '.bak'); raw = undefined;
              } else throw Error('fixture recovery has no compatible document');
            }
            const inputIntent=value=>{
              const input=value.input&&typeof value.input==='object'&&!Array.isArray(value.input)?structuredClone(value.input):null;
              if(input)for(const name of ['appliedRevision','appliedConfigHash','pendingConfigHash','hostAcknowledgedRevision','hostAcknowledgedConfigHash','applyStatus'])delete input[name];
              return input;
            };
            if(args.guardBootMode){
              const project=value=>Object.fromEntries(['bootModes','bootModesAppliedBoot','bootModesActiveBoot','bootModesStartupResult','bootModesClaimWriteId'].filter(name=>Object.hasOwn(value.extensions??{},name)).map(name=>[name,value.extensions[name]]));
              const current=JSON.parse(raw??'{}'),same=module.exports.baseline.equalSettingsDocuments;
              if(!same(project(current),project(args.baseline))||!same(current.input?.startupObservedBoot,args.baseline.input?.startupObservedBoot)){
                receiver({data:{id:message.id,result:{ok:false,reason:'revision-conflict',content:raw??'{}'}}});return;
              }
            }
            if (args.expectedInputRevision !== undefined && (JSON.parse(raw ?? '{}').input?.revision !== args.expectedInputRevision || !module.exports.baseline.equalSettingsDocuments(inputIntent(JSON.parse(raw ?? '{}')),inputIntent(args.baseline)))) {
              receiver({ data: { id: message.id, result: { ok: false, reason: 'revision-conflict', content: raw ?? '{}' } } });
              return;
            }
            if (raw !== undefined) state.files.set(key(file + '.bak'), raw);
            state.files.set(key(file), args.content); state.writes.push(args);
            result = { ok: true, content: args.content };
          } else if (cmd === 'fs.exists') result = state.files.has(key(args.path));
          else if (cmd === 'fs.readTextFile') {
            if (state.readFailure) throw Error('fixture sharing/read error');
            if (!state.files.has(key(args.path))) throw Error('missing fixture');
            result = state.files.get(key(args.path));
          } else if (cmd === 'fs.rename') {
            const from = key(args.from), to = key(args.to);
            if (!state.files.has(from)) throw Error('missing rename source');
            state.files.set(to, state.files.get(from)); state.files.delete(from); state.renames.push(args); result = true;
          } else throw Error('forbidden native operation: ' + cmd);
          receiver({ data: { id: message.id, result } });
        } catch (error) { receiver({ data: { id: message.id, error: String(error.message || error) } }); }
      });
    },
  };
  const module = { exports: {} };
  vm.runInNewContext(compiled, {
    module, exports: module.exports, console, setTimeout, clearTimeout, queueMicrotask,
    structuredClone, TextEncoder, TextDecoder,
    localStorage:{getItem:()=>null,removeItem(){}},
    window: { chrome: { webview }, dispatchEvent() {}, addEventListener() {}, removeEventListener() {} }, document: {},
    CustomEvent: class { constructor(type, init) { this.type = type; this.detail = init?.detail; } },
  }, { filename: 'production-settings-baseline-consumer.cjs' });
  return { ...module.exports, state, seed, key, text: target => state.files.get(key(target || file)), document: () => JSON.parse(state.files.get(key(file))) };
}
