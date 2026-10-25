import assert from 'node:assert/strict';
import crypto from 'node:crypto';
export const sha256 = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
// Comparison only: retain byte-exact extracted code and hashes for compilation/evidence.
export const normalizeLineEndings = text => text.replace(/\r\n/g, '\n');
function blockEnd(text, open) {
  let depth = 0;
  for (let i = open; i < text.length; ++i) {
    const c = text[i], n = text[i + 1];
    if (c === '/' && n === '/') { const end = text.indexOf('\n', i + 2); i = end < 0 ? text.length : end; }
    else if (c === '/' && n === '*') { const end = text.indexOf('*/', i + 2); assert(end >= 0); i = end + 1; }
    else if (c === 'R' && n === '"') { const paren = text.indexOf('(', i + 2), stop = ')' + text.slice(i + 2, paren) + '"'; const end = text.indexOf(stop, paren + 1); assert(paren >= 0 && end >= 0); i = end + stop.length - 1; }
    else if (c === '"' || c === "'") { const quote = c; for (++i; i < text.length; ++i) { if (text[i] === '\\') ++i; else if (text[i] === quote) break; } assert(i < text.length); }
    else if (c === '{') ++depth;
    else if (c === '}' && --depth === 0) return i + 1;
  }
  throw Error('Unclosed C++ block');
}
export function extract(text) {
  const ranges = [];
  const record = (name, start, end) => {
    assert(start >= 0 && end > start, name);
    const code = text.slice(start, end);
    ranges.push({name, start, end, startLine: text.slice(0, start).split('\n').length,
      endLine: text.slice(0, end).split('\n').length, sha256: sha256(code), code});
    return code;
  };
  const bounds = name => {
    const matches = [...text.matchAll(new RegExp('^static[^\\r\\n;{]*\\b' + name + '\\s*\\(', 'gm'))]
      .filter(match => { const brace = text.indexOf('{', match.index), semi = text.indexOf(';', match.index); return brace >= 0 && (semi < 0 || brace < semi); });
    assert.equal(matches.length, 1, 'Unique actual function: ' + name);
    const start = matches[0].index;
    return [start, blockEnd(text, text.indexOf('{', start))];
  };
  const fn = name => record(name, ...bounds(name));
  const declaration = name => {
    const matches = [...text.matchAll(new RegExp('^(?:using[ \\t]+' + name + '\\b|static[^;{}\\r\\n]*\\b' + name + '\\b)[^;]*;', 'gm'))];
    assert.equal(matches.length, 1, 'Unique actual declaration: ' + name);
    return record(name, matches[0].index, matches[0].index + matches[0][0].length);
  };
  const type = name => { const start = text.indexOf('struct ' + name + ' {'); assert(start >= 0); const end = blockEnd(text, text.indexOf('{', start)); assert.equal(text[end], ';'); return record(name, start, end + 1); };
  const region = (name, startMarker, endFunction) => record(name, text.indexOf(startMarker), bounds(endFunction)[1]);
  const parts = {};
  parts.TYPES = '#define R118_HAS_ISSUE_WAIT ' + Number(text.includes('ULONGLONG issueAdmissionDeadline = 0;')) + '\n' + type('HidHidePhysicalIdentity');
  parts.WRAPPER = ['kOpenXInput_ERROR_SUCCESS', 'kOpenXInput_ERROR_UNAVAILABLE', 'kOpenXInput_ERROR_DEVICE_NOT_CONNECTED', 'OpenXInputAssignGuard'].map(declaration).join('\n') + '\n' +
    ['ymccOpenXInputGetUserIndex', 'ymccOpenXInputGetDevicePath', 'ymccOpenXInputAssignSlot'].map(fn).join('\n\n');
  parts.HELPERS = ['hidHideUpper', 'hidHideIsVirtualIdentity', 'xinputNonVirtualConnectedMask', 'xinputPhysicalConnectedMask', 'ymccOpenXInputVisiblePersona'].map(fn).join('\n\n');
  parts.BUDGET = region('entire actual intent budget implementation', 'struct YmccSlotIntentBudget {', 'ymccSlotIntentOperations');
  parts.PLAN_TYPES = type('YmccSlotHandoffPlan') + '\n' + ['g_ymccSlotHandoffPlan', 'g_ymccSlotHandoffPlanActive', 'kYmccSlotProbeSpacingMs'].map(declaration).join('\n');
  parts.PLAN_FUNCTIONS = ['ymccSlotHandoffQueueLog', 'ymccSlotHandoffPlanClear', 'ymccSlotHandoffGateReason', 'ymccSlotHandoffOperationBlocker'].map(fn).join('\n\n');
  parts.REFRESH = region('actual physical refresh outcome and helper', 'struct YmccSlotPhysicalRefreshOutcome {', 'ymccOpenXInputCycleDisplacedPhysical');
  parts.GUARD = ['g_openXInputVirtualSlotAttempts', 'kOpenXInputVirtualSlotMaxAttempts'].map(declaration).join('\n') + '\n' + fn('ymccOpenXInputVirtualSlotGuard');
  const [stepStart, stepEnd] = bounds('ymccOpenXInputSlotHandoffStep');
  const start = text.indexOf('    case 4: {', stepStart), brace = text.indexOf('{', start), end = blockEnd(text, brace);
  assert(start > stepStart && end < stepEnd, 'Actual handoff case4');
  parts.CASE4 = record('entire actual handoff case4 body', brace + 1, end - 1);
  return {parts, ranges};
}
