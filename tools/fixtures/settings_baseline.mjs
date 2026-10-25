import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import { createRequire } from 'node:module';

/** Real repository + API + IPC. The only native surface is this memory store. */
export async function compileSettingsConsumer(root) {
  const require = createRequire(path.join(root, 'package.json'));
  const { build } = require('esbuild');
  const compiled = await build({
    stdin: { contents: `export * as repo from './src/bridge/settingsRepository'; export * as baseline from './src/bridge/settingsBaseline';`, resolveDir: root, loader: 'ts' },
    bundle: true, write: false, format: 'cjs', platform: 'browser',
    define: { 'process.env.NODE_ENV': '"production"' },
  });
  return compiled.outputFiles[0].text;
}
export function createSettingsFixture(compiled, initial, initialBackup) {
  const directory = 'Q:\\ymcc-memory-fixture\\PowerControl';
  const file = directory + '\\yeman-settings.json';
  const key = value => String(value).replaceAll('/', '\\').toLowerCase();
  const state = { directory, file, files: new Map(), calls: [], writes: [], renames: [], readFailure: false, writeFailure: false, beforeWrite: null };
  const seed = (target, value) => state.files.set(key(target), typeof value === 'string' ? value : JSON.stringify(value));
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
            if (key(args.path) !== key(file)) throw Error('settings read escaped fixture');
            if (state.readFailure) throw Error('fixture sharing/read error');
            const raw = state.files.get(key(file));
            result = { stamp: raw === undefined ? 'missing' : raw, unchanged: raw !== undefined && args.stamp === raw, content: raw ?? '' };
          } else if (cmd === 'settings.write') {
            if (key(args.path) !== key(file)) throw Error('settings write escaped fixture');
            if (state.beforeWrite) await state.beforeWrite(args);
            if (state.writeFailure) throw Error('fixture atomic write interrupted');
            const raw = state.files.get(key(file));
            if (raw !== undefined) state.files.set(key(file + '.bak'), raw);
            state.files.set(key(file), args.content); state.writes.push(args);
            result = { ok: true, content: args.content };
          } else if (cmd === 'fs.exists') result = state.files.has(key(args.path));
          else if (cmd === 'fs.readTextFile') {
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
    window: { chrome: { webview }, dispatchEvent() {}, addEventListener() {}, removeEventListener() {} }, document: {},
    CustomEvent: class { constructor(type, init) { this.type = type; this.detail = init?.detail; } },
  }, { filename: 'production-settings-baseline-consumer.cjs' });
  return { ...module.exports, state, seed, key, text: target => state.files.get(key(target || file)), document: () => JSON.parse(state.files.get(key(file))) };
}
