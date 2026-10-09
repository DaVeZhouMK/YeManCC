/** Diagnostic log UI: no popup/extra toggle feedback; export result below card buttons.
 * Compiles real Vue SFC and production handlers with fixture IPC/dialog only.
 * No YMCC, hardware, user configuration, native dialogs, or release builds run.
 */
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import { fileURLToPath } from 'node:url';
import { createRequire } from 'node:module';
import { createHash } from 'node:crypto';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const require = createRequire(path.join(root, 'package.json'));
const { ref } = require('vue');
const { parse, compileScript, compileTemplate } = require('vue/compiler-sfc');
const { transformSync } = require('esbuild');
const sourcePath = path.join(root, 'src/views/SettingsView.vue');
const bound = fs.readFileSync(sourcePath);
const source = bound.toString('utf8').replace(/^\uFEFF/, '');
const { descriptor, errors } = parse(source, { filename: sourcePath });
const sectionStart = descriptor.scriptSetup.content.indexOf('const detailLoggingEnabled = ref(false);');
const sectionEnd = descriptor.scriptSetup.content.indexOf('// 版本号：', sectionStart);
assert(sectionStart >= 0 && sectionEnd > sectionStart, 'Missing production diagnostic log UI section');
const section = descriptor.scriptSetup.content.slice(sectionStart, sectionEnd);
const compiled = transformSync(`${section}\nexport { detailLoggingEnabled, detailLogBusy, detailLogStatus, loadDetailLogging, toggleDetailLogging, exportAllLogs };`, { loader: 'ts', format: 'cjs' }).code;
const passed = [];

async function check(name, fn) { await fn(); passed.push(name); }
function fixture(handler) {
  const calls = [], messages = [], errMsg = ref('');
  const module = { exports: {} };
  vm.runInNewContext(compiled, {
    module, exports: module.exports, ref, errMsg, Error, String,
    invoke: async (...args) => { calls.push(args); return handler(...args); },
    dialog: { message: async (...args) => { messages.push(args); throw new Error('Unexpected popup'); } },
  }, { filename: sourcePath });
  return { ...module.exports, calls, messages, errMsg };
}
const successfulArchive = { ok: true, path: 'X:\\fixture\\YeManCC-logs.zip', count: 7 };
const exportText = `已导出 7 个日志文件到桌面：${successfulArchive.path}`;
const deferred = () => { let resolve; const promise = new Promise(done => { resolve = done; }); return { promise, resolve }; };

try {
  await check('real-vue-script-and-template-compile', () => {
    assert.deepEqual(errors, []);
    const script = compileScript(descriptor, { id: 'diagnostic-log-ui-selftest' });
    transformSync(script.content, { loader: 'ts', format: 'esm' });
    const template = compileTemplate({ source: descriptor.template.content, filename: sourcePath, id: 'diagnostic-log-ui-selftest', compilerOptions: { bindingMetadata: script.bindings } });
    assert.deepEqual(template.errors, []);
  });
  await check('no-explanation-and-export-status-below-buttons-with-fixed-export-label', () => {
    const card = descriptor.template.content.match(/<section\b[^>]*aria-label="诊断日志"[^>]*>([\s\S]*?)<\/section>/)?.[1];
    assert(card);
    assert.equal((card.match(/<button\b/g) || []).length, 2);
    assert.equal((card.match(/<p\b/g) || []).length, 1);
    assert(card.includes('<p v-if="detailLogStatus" class="muted body fan-log-status" role="status">{{ detailLogStatus }}</p>'));
    assert(card.lastIndexOf('</button>') < card.indexOf('<p '));
    assert(card.includes('@click="exportAllLogs">导出全部日志到桌面</button>'));
    assert(!source.includes('默认只记录错误与关键事件（开关/连接/睡眠/故障）'));
    assert(!source.includes('detailLogExporting'));
    assert(!section.includes('dialog.message') && !section.includes('showDetailLogMessage'));
    assert(!section.includes('保存成功'));
  });
  await check('default-off-no-export-status-and-load-is-read-only', async () => {
    const f = fixture(async () => ({ gyro: false, virtual: false }));
    assert.equal(f.detailLoggingEnabled.value, false);
    assert.equal(f.detailLogStatus.value, '');
    await f.loadDetailLogging();
    assert.equal(f.calls[0][0], 'logs.domainGetEnabled');
    assert.equal(f.detailLoggingEnabled.value, false);
    assert.equal(f.detailLogStatus.value, '');
    assert.equal(f.messages.length, 0);
  });
  await check('saved-on-state-loads-without-saving-or-extra-feedback', async () => {
    const f = fixture(async () => ({ virtual: true }));
    await f.loadDetailLogging();
    assert.equal(f.detailLoggingEnabled.value, true);
    assert.equal(f.calls.length, 1);
    assert.equal(f.detailLogStatus.value, '');
    assert.equal(f.messages.length, 0);
  });
  await check('toggle-on-only-updates-button-state', async () => {
    const f = fixture(async () => ({ ok: true, virtual: true }));
    await f.toggleDetailLogging();
    assert.equal(f.calls[0][0], 'logs.domainSetEnabled');
    assert.equal(JSON.stringify(f.calls[0][1]), '{"domain":"virtual","enabled":true}');
    assert.equal(f.detailLoggingEnabled.value, true);
    assert.equal(f.detailLogBusy.value, false);
    assert.equal(f.detailLogStatus.value, '');
    assert.equal(f.errMsg.value, '');
    assert.equal(f.messages.length, 0);
  });
  await check('toggle-off-only-updates-button-state', async () => {
    const f = fixture(async () => ({ ok: true, virtual: false }));
    f.detailLoggingEnabled.value = true;
    await f.toggleDetailLogging();
    assert.equal(f.calls[0][1].enabled, false);
    assert.equal(f.detailLoggingEnabled.value, false);
    assert.equal(f.detailLogStatus.value, '');
    assert.equal(f.errMsg.value, '');
    assert.equal(f.messages.length, 0);
  });
  await check('toggle-preserves-previous-export-information', async () => {
    const f = fixture(async () => ({ ok: true, virtual: true }));
    f.detailLogStatus.value = exportText;
    await f.toggleDetailLogging();
    assert.equal(f.detailLogStatus.value, exportText);
    assert.equal(f.messages.length, 0);
  });
  await check('failed-save-retains-button-state-and-reports-only-in-page-error-area', async () => {
    const f = fixture(async () => ({ ok: false, reason: 'fixture-write-failed' }));
    await f.toggleDetailLogging();
    assert.equal(f.errMsg.value, '日志设置失败：fixture-write-failed');
    assert.equal(f.detailLoggingEnabled.value, false);
    assert.equal(f.detailLogBusy.value, false);
    assert.equal(f.detailLogStatus.value, '');
    assert.equal(f.messages.length, 0);
  });
  await check('unconfirmed-save-or-invalid-state-does-not-update-button', async () => {
    for (const response of [{ virtual: true }, { ok: true, virtual: 'true' }]) {
      const f = fixture(async () => response);
      await f.toggleDetailLogging();
      assert(f.errMsg.value.startsWith('日志设置失败：'));
      assert.equal(f.detailLoggingEnabled.value, false);
      assert.equal(f.detailLogStatus.value, '');
      assert.equal(f.messages.length, 0);
    }
  });
  await check('export-success-restores-original-inline-count-and-destination', async () => {
    const f = fixture(async () => successfulArchive);
    await f.exportAllLogs();
    assert.equal(f.calls[0][0], 'logs.exportAll');
    assert.equal(f.detailLogStatus.value, exportText);
    assert.equal(f.detailLogBusy.value, false);
    assert.equal(f.messages.length, 0);
  });
  await check('export-failure-is-inline-and-clears-busy-state', async () => {
    const f = fixture(async () => ({ ok: false, reason: 'archive-failed-after-2-attempts' }));
    await f.exportAllLogs();
    assert.equal(f.detailLogStatus.value, '导出失败：archive-failed-after-2-attempts');
    assert.equal(f.detailLogBusy.value, false);
    assert.equal(f.messages.length, 0);
  });
  await check('unconfirmed-export-or-missing-path-cannot-show-success', async () => {
    for (const response of [{ ok: true }, { path: successfulArchive.path }]) {
      const f = fixture(async () => response);
      await f.exportAllLogs();
      assert(f.detailLogStatus.value.startsWith('导出失败：'));
      assert.equal(f.messages.length, 0);
    }
  });
  await check('pending-export-uses-inline-progress-and-rejects-duplicate-actions', async () => {
    const pending = deferred();
    const f = fixture(() => pending.promise);
    f.detailLogStatus.value = 'old-export';
    const work = f.exportAllLogs();
    assert.equal(f.detailLogBusy.value, true);
    assert.equal(f.detailLogStatus.value, '正在打包全部日志…');
    await f.exportAllLogs();
    await f.toggleDetailLogging();
    assert.equal(f.calls.length, 1);
    pending.resolve(successfulArchive);
    await work;
    assert.equal(f.detailLogStatus.value, exportText);
    assert.equal(f.detailLogBusy.value, false);
    assert.equal(f.messages.length, 0);
  });
  await check('pending-save-has-no-extra-feedback-and-rejects-duplicate-actions', async () => {
    const pending = deferred();
    const f = fixture(() => pending.promise);
    const work = f.toggleDetailLogging();
    assert.equal(f.detailLogBusy.value, true);
    assert.equal(f.detailLogStatus.value, '');
    await f.toggleDetailLogging();
    await f.exportAllLogs();
    assert.equal(f.calls.length, 1);
    pending.resolve({ ok: true, virtual: true });
    await work;
    assert.equal(f.detailLogStatus.value, '');
    assert.equal(f.messages.length, 0);
  });
  await check('export-transport-error-is-inline-without-popup', async () => {
    const f = fixture(async () => { throw 'fixture-transport-failed'; });
    await f.exportAllLogs();
    assert.equal(f.detailLogStatus.value, '导出失败：fixture-transport-failed');
    assert.equal(f.detailLogBusy.value, false);
    assert.equal(f.messages.length, 0);
  });
  await check('successful-toggle-clears-previous-save-error-without-success-message', async () => {
    const f = fixture(async () => ({ ok: true, virtual: true }));
    f.errMsg.value = '日志设置失败：fixture';
    await f.toggleDetailLogging();
    assert.equal(f.errMsg.value, '');
    assert.equal(f.detailLogStatus.value, '');
    assert.equal(f.messages.length, 0);
  });
  await check('bound-view-unchanged-during-test', () => assert(bound.equals(fs.readFileSync(sourcePath))));
  const arg = process.argv.indexOf('--output-dir');
  const output = arg >= 0 ? path.resolve(process.argv[arg + 1]) : path.resolve(root, '../../Build/Validation/DiagnosticLogUI');
  fs.mkdirSync(output, { recursive: true });
  const report = {
    suite: 'Diagnostic log UI no-popup inline export feedback', passed: passed.length, failures: 0, cases: passed,
    sourcePath, sourceSha256: createHash('sha256').update(bound).digest('hex').toUpperCase(),
    scope: 'Compiled real Vue SFC and extracted production log handlers with fixture IPC/dialog',
    realProductOrHardwareExecuted: false, realUserSettingsRead: false, releasePackageRebuilt: false,
  };
  fs.writeFileSync(path.join(output, 'report.json'), JSON.stringify(report, null, 2) + '\n');
  console.log(JSON.stringify(report, null, 2));
} catch (error) {
  console.error('FAILED after', passed.length, 'checks:', error);
  process.exitCode = 1;
}
