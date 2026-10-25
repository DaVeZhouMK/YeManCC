import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';
import { createHash } from 'node:crypto';
import { spawnSync } from 'node:child_process';
const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const require = createRequire(path.join(repo, 'package.json'));
const esbuild = require('esbuild');
const args = process.argv.slice(2), options = { browser: true };
let positional;
for (let i = 0; i < args.length; i++) {
  if (args[i].startsWith('--')) {
    const key = args[i].slice(2);
    options[key] = ['browser'].includes(key) ? true : args[++i];
  } else positional ??= args[i];
}
const out = path.resolve(options.out || options.output || positional || process.env.YMCC_UI_BROWSER_OUT || fs.mkdtempSync(path.join(os.tmpdir(), 'ymcc-status-')));
fs.mkdirSync(out, { recursive: true });
const sourceFile = options['status-source'] ? path.resolve(options['status-source']) : path.join(repo, 'src/bridge/virtualOutputStatus.ts');
const rows = [], replay = { status: 'SKIPPED', reason: 'Pass --logs for field-log replay.', rows: [] };
const ui = { status: 'PENDING' };
const report = { schemaVersion: 1, testName: 'controller.virtual-output-status', status: 'RUNNING', rows, replay, ui, deviceIo: false, productAppExecuted: false };
const hash = file => createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const inputs = ['src/bridge/controllerFeatureStatus.ts', 'src/bridge/virtualOutputStatus.ts', 'src/components/ControllerChannelStatus.vue', 'src/views/ButtonMappingView.vue', 'tools/virtual_output_status_selftest.ts', 'tools/virtual_output_status_selftest.mjs'];
report.inputs = inputs.map(file => ({ file, sha256: hash(path.join(repo, file)) }));
function test(name, fn) { try { fn(); rows.push({ name, status: 'PASS' }); } catch (error) { rows.push({ name, status: 'FAIL', error: String(error) }); } }
const statusBundle = path.join(out, 'status-consumer.cjs');
esbuild.buildSync({ stdin: { contents: fs.readFileSync(sourceFile, 'utf8'), resolveDir: path.join(repo, 'src/bridge'), loader: 'ts' }, bundle: true, platform: 'node', format: 'cjs', outfile: statusBundle, logLevel: 'silent' });
const { virtualOutputPresentation: present, acceptVirtualOutputRuntime: accept } = require(statusBundle);
const active = { state: 'active', requestedPersona: 'steamdeck', boundPersona: 'steamdeck', hostEpoch: 2, hostAlive: true, prepared: true, neutralized: true, frameAccepted: true, sourceLive: true, sourceCurrent: true, suppressionState: 'verified' };
const protection = { requestId: 4, persona: 'steamdeck', locked: true, phase: 'verifying', elapsedMs: 1500, remainingMs: 28500, maxMs: 30000, settleMs: 10000, verificationPending: true, candidateOutcome: 'success' };
const view = (patch, p = protection) => present({ ...active, ...patch, startProtection: p }, true, 'steamdeck');
test('saved-intent-is-not-success', () => assert.equal(present(null, true, 'steamdeck').tone, 'pending'));
test('host-creation-stage', () => assert.equal(view({ state: 'starting', hostAlive: false, prepared: false, frameAccepted: false }).text, '虚拟手柄-设备创建中'));
for (const patch of [{ sourceLive: false }, { sourceCurrent: false }, { frameAccepted: false }, { state: 'suspected-failed', reason: 'physical-source-unavailable' }, { state: 'unknown', reason: 'physical-source-unconfirmed' }]) {
  test('incomplete-current-source:' + JSON.stringify(patch), () => { const result = view(patch); assert.equal(result.text, '虚拟手柄-等待输入'); assert.equal(result.tone, 'pending'); });
}
test('inspection-is-not-final', () => assert.equal(view({}).text, '虚拟手柄-检查中'));
test('candidate-failure-is-not-final', () => assert.equal(view({}, { ...protection, candidateOutcome: 'failure' }).tone, 'pending'));
test('native-final-success-with-current-receipt', () => assert.equal(view({}, { ...protection, locked: false, phase: 'success', verificationPending: false }).tone, 'on'));
for (const key of ['sourceLive', 'sourceCurrent', 'frameAccepted', 'hostAlive', 'prepared', 'neutralized']) test('old-success-cannot-supply-current:' + key, () => assert.notEqual(view({ [key]: false }, { ...protection, locked: false, phase: 'success' }).tone, 'on'));
for (const phase of ['expired', 'cancelled', 'failure']) test('non-success-final:' + phase, () => assert.notEqual(view({}, { ...protection, locked: false, phase }).tone, 'on'));
for (const reason of ['backend-fault-hold', 'physical-fallback', 'transport-fault', 'host-not-alive', 'persona-mismatch', 'target-not-admitted']) test('real-fault:' + reason, () => { const result = view({ state: 'suspected-failed', reason }); assert.equal(result.tone, 'warning'); assert.ok(result.detail.length); });
for (const switchPending of [false, true]) test('hold-outranks-pending:' + switchPending, () => assert.equal(view({ state: 'suspected-failed', reason: 'backend-fault-hold', switchPending }).text, '虚拟手柄-失败'));
test('Steam-stopped-retains-actionable-warning', () => assert.match(view({ steamRunning: false }).detail, /Steam.*运行/));
test('native-final-failure-cannot-be-green', () => assert.equal(view({}, { ...protection, phase: 'failure', locked: false }).tone, 'warning'));
test('isolation-warning-preserves-local-output', () => { const result = view({ state: 'suspected-failed', reason: 'suppression-degraded', suppressionState: 'degraded' }, { ...protection, phase: 'success', locked: false }); assert.equal(result.text, '虚拟手柄-已开启，隔离异常'); assert.equal(result.tone, 'warning'); });
test('isolation-warning-during-inspection', () => assert.equal(view({ suppressionState: 'degraded' }).text, '虚拟手柄-检查中，隔离异常'));
test('isolation-warning-does-not-invent-output', () => { const result = view({ suppressionState: 'degraded', sourceLive: false }); assert.equal(result.text, '虚拟手柄-隔离异常'); assert.equal(result.tone, 'warning'); });
test('mismatched-protection-cannot-complete-current-mode', () => assert.equal(view({}, { ...protection, persona: 'elite', phase: 'success', locked: false }).tone, 'pending'));
test('same-persona-old-success-cannot-replace-new-request', () => { const next = { ...active, state: 'starting', startProtection: { ...protection, requestId: 5, phase: 'pending' } }; const old = { ...active, startProtection: { ...protection, locked: false, phase: 'success' } }; assert.equal(accept(next, old), next); assert.equal(accept(old, old, 5), null); });
test('new-request-with-missing-identity-cannot-use-old-success', () => assert.equal(accept(active, { ...active }, 5), null));
test('native-off-return-failure-persists', () => assert.equal(present({ state: 'suspected-failed', physicalReturnFailed: true }, false, 'disabled').tone, 'warning'));
test('native-off-return-work-is-pending', () => assert.equal(present({ physicalReturnPending: true, physicalReturnFailed: true }, false, 'disabled').tone, 'pending'));
const legacyBundle = path.join(out, 'legacy-status.cjs');
esbuild.buildSync({ entryPoints: [path.join(repo, 'tools/virtual_output_status_selftest.ts')], bundle: true, platform: 'node', format: 'cjs', outfile: legacyBundle, logLevel: 'silent' });
const legacy = spawnSync(process.execPath, [legacyBundle], { encoding: 'utf8', windowsHide: true, timeout: 15000 });
fs.writeFileSync(path.join(out, 'legacy-status.log'), (legacy.stdout || '') + (legacy.stderr || ''));
test('existing-TS-presentation-regression', () => assert.equal(legacy.status, 0, legacy.stderr));

if (options.logs) {
  try {
    const logRoot = path.resolve(options.logs), records = [], manifests = [];
    for (const name of ['virtual-gamepad.log.1', 'virtual-gamepad.log']) {
      const file = path.join(logRoot, name); manifests.push({ file, sha256: hash(file) });
      for (const [i, line] of fs.readFileSync(file, 'utf8').split('\n').entries()) if (line.trim()) records.push({ file: name, line: i + 1, value: JSON.parse(line) });
    }
    const select = (event, predicate = () => true) => records.filter(r => r.value.event === event && predicate(r.value));
    const open = select('input-host-open-result', r => r.epoch === 2)[0], started = select('input-host-started', r => r.epoch === 2)[0];
    assert(open && started, 'Replay requires the exported host epoch 2 receipts.');
    const receiptBase = { requestedPersona: started.value.persona, boundPersona: started.value.boundPersona, hostEpoch: open.value.epoch, hostAlive: open.value.hello, prepared: open.value.prepared, neutralized: open.value.neutralized };
    // Only attach producer fields; no native verdict or admission policy is reimplemented here.
    // These log records are not full gamepad.state snapshots. Missing state/sourceCurrent/Steam
    // remain unknown; replay proves presentation of partial receipts and native protection phases.
    for (const row of select('virtual-ui-output', r => r.hostEpoch === 2)) {
      const r = row.value;
      const receipt = { ...receiptBase, frameAccepted: r.frameAccepted, sourceLive: r.sourceLive };
      const result = present(receipt, true, r.persona);
      assert.notEqual(result.tone, 'on', 'A partial log receipt must not invent native runtime success.');
      replay.rows.push({ source: { file: row.file, line: row.line, time: r.time }, native: r, suppliedReceipt: receipt, result });
    }
    for (const row of select('virtual-start-protection-state')) {
      const r = row.value;
      const receipt = { ...(r.persona === 'steamdeck' ? receiptBase : { requestedPersona: r.persona }), startProtection: r };
      const result = present(receipt, r.persona !== 'disabled', r.persona);
      assert.notEqual(result.tone, 'warning', 'Candidate failure cannot be mistaken for a terminal failure.');
      assert.notEqual(result.tone, 'on', 'Protection final success alone is not a current output receipt.');
      replay.rows.push({ source: { file: row.file, line: row.line, time: r.time }, native: r, suppliedReceipt: receipt, result });
    }
    replay.finals = select('input-host-switch-verification', r => r.stage === 'final');
    replay.inspectionChanges = select('input-host-switch-verification', r => r.stage === 'inspection');
    assert(replay.finals.some(row => row.value.requestId === 4 && row.value.outcome === 'success' && row.value.knownFaultChecks === 0));
    assert(replay.finals.some(row => row.value.persona === 'disabled' && row.value.candidateOutcome === 'failure' && row.value.outcome === 'success'));
    replay.suppression = select('hidhide.visibility-snapshot').map(row => ({ source: { file: row.file, line: row.line }, time: row.value.time, suppressionDegraded: row.value.suppressionDegraded, configurationVerified: row.value.configurationVerified, filterAttached: row.value.filterAttached, externalVisibility: row.value.externalVisibility }));
    replay.inputs = manifests;
    replay.observationLimit = 'Export does not include full gamepad.state.virtualOutput snapshots or the screenshot time. Exact UI warning reason cannot be established; source intermittency is observed, suppression-degraded as the screenshot cause is unproven. Final local success does not prove Steam/game consumption.';
    replay.status = 'PASS';
  } catch (error) { replay.status = 'FAIL'; replay.error = String(error); }
}

if (options.browser) {
  let browser;
  try {
    const { parse, compileScript, compileStyle } = require('vue/compiler-sfc');
    let playwright;
    try { playwright = require('playwright'); } catch {
      const root = options['playwright-root'] || process.env.YMCC_TEST_PLAYWRIGHT_ROOT || path.join(os.homedir(), '.cache/codex-runtimes/codex-primary-runtime/dependencies/node');
      playwright = createRequire(path.join(path.resolve(root), 'status-runtime.cjs'))('playwright');
    }
    const channelFile = path.join(repo, 'src/components/ControllerChannelStatus.vue'), pageFile = path.join(repo, 'src/views/ButtonMappingView.vue');
    const imports = new Map();
    const ipcStub = `export const calls=[];const handlers=new Map();export const getLastGamepadState=()=>null;export function on(name,fn){if(!handlers.has(name))handlers.set(name,new Set());handlers.get(name).add(fn);return()=>handlers.get(name).delete(fn)}export function emit(name,value){for(const fn of handlers.get(name)||[])fn(value)}export async function invoke(name,args){calls.push({name,args});if(name==='gyro.windowsSensors.get')return {contextStableAtCheck:true,selectedAtQuery:{gyro:{present:true}}};if(name==='oem.keys.status')return {schemaVersion:1,familyId:'msi-claw',deviceClass:'HandheldCompanion.Devices.ClawCG3EM',supported:true,keys:[{keyId:'m1',label:'M1',backIndex:1,captureImplemented:true},{keyId:'m2',label:'M2',backIndex:2,captureImplemented:true},{keyId:'claw',label:'CLAW',backIndex:0,captureImplemented:true,triggerCapability:'click-only'},{keyId:'qs',label:'QS',backIndex:0,captureImplemented:true,triggerCapability:'click-only'}],runtime:{msiWmiState:2,msiWmiCurrent:true,keyboardActive:true,nativeRearAutomaticMapping:'held'}};if(name==='oem.keys.get')return {familyId:'msi-claw',supported:true,keys:[{keyId:'m1',label:'M1',backIndex:1,captureImplemented:true},{keyId:'m2',label:'M2',backIndex:2,captureImplemented:true}]};if(name==='input.shortcutRuntime.get')return {active:false};return {}}`;
    const actual = new Set(['virtualOutputStatus', 'virtualStartProtectionView', 'virtualTargetPolicy', 'virtualTargetPolicyState', 'controllerPersona', 'controllerShortcutRules', 'controllerFeatureStatus', 'inputContracts']);
    const sfcPlugin = { name: 'actual-status-sfc', setup(build) {
      build.onResolve({ filter: /^fixture-ipc$/ }, () => ({ path: 'ipc', namespace: 'fixture' }));
      build.onResolve({ filter: /^@\// }, args => {
        const base = path.basename(args.path);
        if (args.path.endsWith('/ipc')) return { path: 'ipc', namespace: 'fixture' };
        if (args.path.endsWith('.vue')) return { path: args.path, namespace: 'stub-component' };
        if (actual.has(base)) return { path: path.join(repo, 'src', args.path.slice(2) + '.ts') };
        return { path: args.path, namespace: 'stub-import' };
      });
      build.onResolve({ filter: /^(\.\/|\.\.\/).*ipc$/ }, () => ({ path: 'ipc', namespace: 'fixture' }));
      build.onLoad({ filter: /.*/, namespace: 'fixture' }, () => ({ contents: ipcStub, loader: 'js' }));
      build.onLoad({ filter: /.*/, namespace: 'stub-component' }, () => ({ contents: `export default {render(){return null}}`, loader: 'js' }));
      build.onLoad({ filter: /.*/, namespace: 'stub-import' }, args => {
        const names = imports.get(args.path) || [];
        const defaults = {
          mouseModeState: `{value:{enabled:false}}`,
          readGameInputEditor: `async()=>({input:{revision:1,outputTarget:{buttonMappingEnabled:false,persona:'disabled'}},target:{exe:'',pid:0,processCreated:''},profile:null})`,
          VirtualStartProtectionActive: `class extends Error {}`,
          onInputSettingsChanged: `()=>()=>{}`, onGameInputEditorChanged: `()=>()=>{}`,
        };
        return { contents: [...names].map(name => `export const ${name}=${defaults[name] || '()=>undefined'};`).join('\n'), loader: 'js' };
      });
      build.onLoad({ filter: /\.vue$/ }, args => {
        let source = fs.readFileSync(args.path, 'utf8');
        if (args.path === pageFile) {
          const channelTag = source.match(/<ControllerChannelStatus\s[^>]+\/>/)?.[0];
          assert(channelTag, 'Actual template must wire ControllerChannelStatus.');
          source = source.replace('</script>', 'defineExpose({physicalGyroSupported,physicalBackButtons,physicalBackButtonsCaptured,oemUsage,observedOemKeys,virtualOutputStatus,virtualOutputRuntime,applyInputSnapshot,acceptStartProtection});\n</script>');
          source = source.replace(/<template>[\s\S]*?<\/template>(?=\s*<style)/, '<template>' + channelTag + '</template>');
        }
        const { descriptor } = parse(source, { filename: args.path });
        const script = compileScript(descriptor, { id: 'status-test', inlineTemplate: true, genDefaultAs: '__component' });
        const js = esbuild.transformSync(script.content, { loader: 'ts', format: 'esm' }).code;
        for (const match of js.matchAll(/import\s*\{([^}]+)\}\s*from\s*["']([^"']+)["']/g)) {
          const names = match[1].split(',').map(s => s.trim().split(/\s+as\s+/)[0]).filter(Boolean);
          imports.set(match[2], new Set([...(imports.get(match[2]) || []), ...names]));
        }
        return { contents: js + '\nexport default __component;', loader: 'js', resolveDir: path.dirname(args.path) };
      });
    }};
    // Keep the actual status component; other page children are irrelevant to the wiring test.
    sfcPlugin.setup = ((setup) => build => { setup(build); build.onResolve({ filter: /^actual-channel$/ }, () => ({ path: channelFile })); })(sfcPlugin.setup);
    const entry = `import {createApp,ref,KeepAlive,h,nextTick} from 'vue';import Channel from 'actual-channel';import Page from ${JSON.stringify(pageFile.replaceAll('\\','/'))};import {emit,calls} from 'fixture-ipc';const visible=ref(true),instance=ref();const Wrapped={setup(){return()=>h(Page,{ref:instance})}};createApp({setup(){return()=>h(KeepAlive,null,{default:()=>visible.value?h(Wrapped):null})}}).mount('#page');const channel=ref({status:{text:'虚拟手柄-未开启',detail:'未请求',tone:'off'},gyroSupported:false,gyroEnabled:false,backButtons:0,physicalGyroSupported:true,physicalBackButtons:2,physicalBackButtonsCaptured:2});createApp({setup(){return()=>h(Channel,channel.value)}}).mount('#channel');window.test={emit,calls,visible,instance,channel,nextTick};`;
    // Redirect this one real child import while leaving the page's other children stubbed.
    const oldSetup = sfcPlugin.setup;
    sfcPlugin.setup = build => { build.onResolve({ filter: /^@\/components\/ControllerChannelStatus.vue$/ }, () => ({ path: channelFile })); oldSetup(build); };
    const built = await esbuild.build({ stdin: { contents: entry, loader: 'js', resolveDir: repo }, bundle: true, platform: 'browser', format: 'iife', write: false, plugins: [sfcPlugin], define: { __VUE_OPTIONS_API__: 'true', __VUE_PROD_DEVTOOLS__: 'false', __VUE_PROD_HYDRATION_MISMATCH_DETAILS__: 'false' }, logLevel: 'silent' });
    fs.writeFileSync(path.join(out, 'status-ui-bundle.js'), built.outputFiles[0].contents);
    const css = compileStyle({ source: parse(fs.readFileSync(channelFile, 'utf8')).descriptor.styles[0].content, id: 'status-test', scoped: false }).code;
    browser = await playwright.chromium.launch({ channel: 'msedge', headless: true });
    ui.rows = []; ui.errors = [];
    for (const height of [720, 800]) {
      const page = await browser.newPage({ viewport: { width: 1280, height } });
      page.on('pageerror', e => ui.errors.push(String(e)));
      await page.setContent('<html><meta charset="utf-8"><style>body{background:#111b28;color:#ddd;font-family:Arial;--text-dim:#aeb9c8;--accent:#63d7ad}#page,#channel{width:620px;margin:40px}</style><style>' + css + '</style><div id="page"></div><div id="channel"></div></html>');
      await page.addScriptTag({ content: built.outputFiles[0].text });
      await page.waitForFunction(() => window.test?.instance.value?.physicalBackButtons === 2);
      const initial = await page.locator('#page').innerText();
      assert.match(initial, /未开启/); assert.match(initial, /需虚拟手柄/); assert.match(initial, /待按键确认/); assert.match(initial, /专用按键/);
      await page.evaluate(async()=>{window.test.emit('gamepad.state',{oemPressedKeys:['m1','claw','unknown']});await window.test.nextTick()});
      assert.equal(await page.locator('#page [data-feature=back] strong').innerText(),'已收到 1/2');
      assert.equal(await page.locator('#page [data-feature=dedicated] strong').innerText(),'已收到 1/2');
      const checks = await page.evaluate(async ({ active, protection }) => {
        const t=window.test;const p=t.instance.value;
        p.applyInputSnapshot({revision:2,outputTarget:{persona:'steamdeck',buttonMappingEnabled:true}});
        t.emit('gamepad.state',{virtualOutput:{...active,startProtection:protection}});await t.nextTick();
        const inspection=document.querySelector('#page').innerText;
        const newer={...active,state:'starting',frameAccepted:false,startProtection:{...protection,requestId:5,phase:'pending'}};
        t.emit('gamepad.state',{virtualOutput:newer});t.emit('gamepad.state',{virtualOutput:{...active,startProtection:{...protection,phase:'success',locked:false}}});await t.nextTick();
        const requestId=p.virtualOutputRuntime?.startProtection?.requestId;
        p.physicalGyroSupported=null;t.visible.value=false;await t.nextTick();
        t.emit('gyro.telemetry',{sensorPresent:true});const hiddenIgnored=p.physicalGyroSupported===null;
        t.visible.value=true;await t.nextTick();t.emit('gyro.telemetry',{sensorPresent:true});await t.nextTick();
        const visibleApplied=p.physicalGyroSupported===true;
        return {inspection,requestId,hiddenIgnored,visibleApplied,calls:t.calls};
      }, { active, protection });
      assert.match(checks.inspection,/检查中/);assert.equal(checks.requestId,5);assert(checks.hiddenIgnored);assert(checks.visibleApplied);
      assert.equal(checks.calls.filter(c=>c.name==='gyro.windowsSensors.get').length,0);
      assert.equal(checks.calls.filter(c=>c.name==='oem.keys.status').length,2);
      assert.equal(checks.calls.filter(c=>/set|write|calibrat/i.test(c.name)).length,0);
      await page.evaluate(async()=>{const t=window.test;t.channel.value={...t.channel.value,physicalGyroSupported:null,physicalBackButtons:null};await t.nextTick()});
      const unknown=await page.locator('#channel').innerText();assert(!unknown.includes('不支持'));assert.match(unknown,/待确认/);
      const bounds=await page.locator('#channel .controller-channel').evaluateAll(els=>els.map(el=>({width:el.clientWidth,scrollWidth:el.scrollWidth})));
      assert(bounds.length===3&&bounds.every(b=>b.scrollWidth<=b.width+1));
      assert(Math.max(...bounds.map(b=>b.width))-Math.min(...bounds.map(b=>b.width))<=1);
      await page.locator('#page [data-feature=back]').click();
      assert(await page.locator('#page .controller-channel-detail').isVisible());
      await page.locator('#page [data-feature=back]').press('Enter');
      assert.equal(await page.locator('#page .controller-channel-detail').count(),0);
      await page.screenshot({ path: path.join(out, 'status-' + height + '.png') });
      ui.rows.push({viewport:{width:1280,height},initial,unknown,checks,bounds});await page.close();
    }
    assert.deepEqual(ui.errors,[]);ui.status='PASS';
    ui.scope='Actual ControllerChannelStatus SFC and actual ButtonMapping setup, lifecycle/event callbacks and original status tag; unrelated page children/API calls stubbed. No full-page navigation or hardware acceptance claim.';
  } catch (error) { ui.status='FAIL';ui.error=String(error); } finally { await browser?.close(); }
}
report.status = rows.some(row => row.status === 'FAIL') || replay.status === 'FAIL' || ui.status === 'FAIL' ? 'FAIL' : 'PASS';
report.summary = { tests: rows.length, passed: rows.filter(r => r.status === 'PASS').length, failed: rows.filter(r => r.status === 'FAIL').length, replay: replay.status, browser: ui.status };
for (const [name,value] of [['virtual-output-status-report.json',report],['virtual-output-status-log-replay.json',replay],['virtual-output-status-ui.json',ui]]) fs.writeFileSync(path.join(out,name),JSON.stringify(value,null,2)+'\n');
console.log(JSON.stringify({status:report.status,...report.summary,report:path.join(out,'virtual-output-status-report.json')}));
process.exitCode = report.status === 'PASS' ? 0 : 1;
