import crypto from 'node:crypto';
export const sha = data => crypto.createHash('sha256').update(data).digest('hex');
export const need = (value, code) => { if (!value) throw Error(code); };
// Preserve the old byte contract for archived evidence; it does not certify the
// lifecycle-aware source below. The current private fault removes ordering only.
export const HISTORICAL_CONTRACT = Object.freeze({epoch:'2026-10-19-four-line-ordering',
  currentSha:'f1946ffc02c6cc6930378b8f0f1b8051b366a100254c12d0c0fc41370428707f',
  beforeSha:'b3c5c29961115196dee570d86dd05077aaaca95943df4e051ad65d03bfbdaa9c',removedLines:4});
export const PRIOR_LIFECYCLE_CONTRACT = Object.freeze({epoch:'2026-10-20-lifecycle-aware-two-guard-ordering',currentSha:'173b40da1a0fc3c1301af26c1c1b8174c6963526ba5722583bb47e75c49b30b8',beforeSha:'d2cf176e8110cb3dd1b3035913d8285c1c23ab11bf261f47232a6a737c5a0acd',removedLines:2});
export const ORDERING_CONTRACT_EPOCH = '2026-10-20-lifecycle-two-guard-ordering-with-user-comments';
export const BEFORE_SHA = '03f8b64e5d943cf0589971ffb03c9ce94d9ed78b9ee159e5e13d63c18305fad1';
export const CURRENT_SHA = '6bb7b043776460a6720ae1b2f1ec8388a768d366d627e2734665c74fba1c5085';
export const HELPER_SHA = '879a1a0feb9a3660b9803d83048b198fa16e2112f2bbd6e792d68a4c5e88bed7';
export const BASELINE_SHA = 'd74af5ef651cc3a611ad6294d8fc1b803081b7c29b0c011ff7ce54d0c2180721';
export const ORDER_ASSERTION = 'latest issued lighting request must retain its own receipt after older replies';
export const RECEIPTS = [
  ['backend-refusal', {ok:false, reason:'light-device-core-missing'}, '灯光应用失败：light-device-core-missing'],
  ['bridge-exception', {throwMessage:'fixture bridge timeout'}, '灯光应用失败：fixture bridge timeout'],
  ['missing-acknowledgement', {}, '灯光应用失败：未知原因'],
  ['accepted-operation', {ok:true}, '掌机灯光已应用。'],
];
export const ORDERINGS = [
  ['older-success-after-newer-refusal', {ok:false,reason:'new-request-refused'}, {ok:true}, '灯光应用失败：new-request-refused', '掌机灯光已应用。'],
  ['older-refusal-after-newer-success', {ok:true}, {ok:false,reason:'old-request-refused'}, '掌机灯光已应用。', '灯光应用失败：old-request-refused'],
  ['older-exception-after-newer-success', {ok:true}, {throwMessage:'old bridge timeout'}, '掌机灯光已应用。', '灯光应用失败：old bridge timeout'],
  ['older-success-after-newer-exception', {throwMessage:'new bridge timeout'}, {ok:true}, '灯光应用失败：new bridge timeout', '掌机灯光已应用。'],
];
export const ORIGINAL_BROWSER_CASES = [
  'persisted stable preset ID reaches real card/API args, not enum128 or array position',
  'real preset selection persists and applies the same stable ID',
  'normal color mode resumes its existing route without preset ID',
  'Loki keeps normal color and on/off controls without a no-op brightness slider',
  'Mini retains HID light/brightness with explicit unsupported preset rather than serial fallback',
];
const equal = (a,b) => JSON.stringify(a) === JSON.stringify(b);
export function privateOrderingSource(current) {
  need(sha(current) === CURRENT_SHA, 'LIGHTING_CURRENT_SOURCE_IDENTITY');
  let text = current.toString('utf8'), removed = 0;
  for (const [line, count] of [['    if (request !== ledApplyRequest) return;', 2]]) {
    const matches = text.split(/(?<=\n)/).filter(row => row.replace(/\r?\n$/, '') === line);
    need(matches.length === count, 'LIGHTING_TWO_GUARD_FAULT_ANCHOR');
    text = text.split(/(?<=\n)/).filter(row => row.replace(/\r?\n$/, '') !== line).join('');
    removed += matches.length;
  }
  const result = Buffer.from(text, 'utf8');
  need(removed === 2 && sha(result) === BEFORE_SHA, 'LIGHTING_CURRENT_PRIVATE_PREIMAGE_MISMATCH');
  return result;
}
export function natural(row, label, expectedExit = 0) {
  need(row && Number.isInteger(row.pid) && row.pid > 0 && row.exitCode === expectedExit &&
    !row.signal && !row.timedOut && !row.launchError && !row.outputTruncated && !row.killRequested &&
    typeof row.startedUtc === 'string' && typeof row.finishedUtc === 'string' &&
    Number.isFinite(Date.parse(row.startedUtc)) && Date.parse(row.finishedUtc) >= Date.parse(row.startedUtc),
    `LIGHTING_PROCESS_INCOMPLETE:${label}`);
}
export function verifyFeedback({cases, ordering, feedback, browser, stdout = '', stderr = '', exitCode,
  component, sourceSha256, negative = false}) {
  const expectedExit = negative ? 1 : 0;
  need(exitCode === expectedExit && feedback?.exitCode === expectedExit && feedback.status === (negative ? 'FAIL' : 'PASS') &&
    feedback.snapshotOverride === negative && feedback.sourceSha256 === sourceSha256 && feedback.component === component &&
    feedback.hardwareIo === false && feedback.bridge === 'memory fixture', 'LIGHTING_FEEDBACK_IDENTITY_OR_EXIT');
  need(Array.isArray(cases) && cases.length === 12 && new Set(cases.map(row => row.id)).size === 12 &&
    equal(ordering?.cases, cases) && ordering.status === (negative ? 'FAIL' : 'PASS') &&
    ordering.hardwareIo === false && ordering.physicalEffectVerified === false, 'LIGHTING_CASE_SET');
  for (let i = 0; i < RECEIPTS.length; i++) {
    const [id, input, expected] = RECEIPTS[i], row = cases[i];
    need(row.id === id && equal(row.input, input) && row.expected === expected && row.feedback === expected &&
      row.status === 'PASS' && row.hardwareIo === false && row.state?.applies?.length === 1 && row.state.savedCount === 0,
      `LIGHTING_ORIGINAL_RECEIPT:${id}`);
    const call = row.state.applies[0];
    need(equal(call, {enabled:true, mode:'solid', color:'#112233', color2:'#445566', speed:65, brightness:42}), `LIGHTING_ORIGINAL_API_ARGS:${id}`);
  }
  let cursor = 4;
  for (const height of [720, 800]) for (const [id, newReply, oldReply, expected, oldFeedback] of ORDERINGS) {
    const row = cases[cursor++], key = `${id}-${height}`, state = row.state;
    need(row.id === key && equal(row.input?.viewport, {width:1280, height}) && row.input.id === id &&
      equal(row.input.newReply, newReply) && equal(row.input.oldReply, oldReply) && row.input.expected === expected &&
      row.expected === expected && row.hardwareIo === false && row.physicalEffectVerified === false &&
      row.sourceKind === 'source-derived-fixture', `LIGHTING_ORDERING_INPUT:${key}`);
    need(state?.callCount === 2 && Array.isArray(state.args) && state.args.length === 2 &&
      state.args.every((call,i) => equal(call, {enabled:true,mode:'solid',color:'#112233',color2:'#445566',speed:65,brightness:i ? 74 : 53})) &&
      state.afterNew === expected && state.afterOld === (negative ? oldFeedback : expected) &&
      row.feedback === state.afterOld && row.status === (negative ? 'FAIL' : 'PASS'), `LIGHTING_ORDERING_SEMANTICS:${key}`);
  }
  if (negative) {
    need(/AssertionError \[ERR_ASSERTION\]/.test(stderr) && stderr.includes(ORDER_ASSERTION) &&
      /real-component-feedback-probe\.mjs:\d+:\d+/.test(stderr) && /8 !== 0/.test(stderr) &&
      !browser && !stdout.includes('LIGHTING_OPERATIONS_BROWSER_OK'), 'LIGHTING_ORIGINAL_ORDERING_ASSERTION_REQUIRED');
  } else {
    const expectedCases = [...ORIGINAL_BROWSER_CASES, ...RECEIPTS.map(row => row[0]), ...cases.slice(4).map(row => row.id)];
    need(browser?.ok === true && equal(browser.cases, expectedCases) && equal(browser.errors, []) &&
      browser.hardwareIo === false && browser.HCAssemblyLoaded === false && Number.isInteger(browser.browserPort) &&
      browser.browserPort >= 49152 && browser.browserPort <= 65535 &&
      stdout.includes('LIGHTING_OPERATIONS_BROWSER_OK groups=17 deviceOperations=0'), 'LIGHTING_ORIGINAL_BROWSER_ASSERTIONS_REQUIRED');
  }
  return {status: negative ? 'EXPECTED_REJECTION' : 'PASS', originalReceiptsPassed:4,
    orderingPassed:negative ? 0 : 8, orderingOriginalFailures:negative ? 8 : 0,
    resolutions:[{width:1280,height:720},{width:1280,height:800}],
    originalBrowserGroups:negative ? 'passed before original ordering assertion' : 5};
}
