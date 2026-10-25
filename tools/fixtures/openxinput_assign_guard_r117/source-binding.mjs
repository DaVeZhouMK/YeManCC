import assert from 'node:assert/strict';
import crypto from 'node:crypto';

export const sha256 = value => crypto.createHash('sha256').update(value).digest('hex');

// Extract declarations and entire bodies; never substitute statements inside them.
function blockEnd(text, open) {
  let depth = 0;
  for (let i = open; i < text.length; ++i) {
    const c = text[i], next = text[i + 1];
    if (c === '/' && next === '/') {
      const end = text.indexOf('\n', i + 2);
      i = end < 0 ? text.length : end;
    } else if (c === '/' && next === '*') {
      const end = text.indexOf('*/', i + 2);
      assert(end >= 0, 'Unclosed C++ comment');
      i = end + 1;
    } else if (c === 'R' && next === '"') {
      const paren = text.indexOf('(', i + 2);
      assert(paren >= 0 && paren - i <= 18, 'Invalid C++ raw string');
      const delimiter = ')' + text.slice(i + 2, paren) + '"';
      const end = text.indexOf(delimiter, paren + 1);
      assert(end >= 0, 'Unclosed C++ raw string');
      i = end + delimiter.length - 1;
    } else if (c === '"' || c === "'") {
      const quote = c;
      let closed = false;
      for (++i; i < text.length; ++i) {
        if (text[i] === '\\') ++i;
        else if (text[i] === quote) { closed = true; break; }
      }
      assert(closed, 'Unclosed C++ literal');
    } else if (c === '{') ++depth;
    else if (c === '}' && --depth === 0) return i + 1;
  }
  throw new Error('Unclosed C++ function');
}

function record(text, name, start, end) {
  const code = text.slice(start, end);
  return {name, start, end, startLine: text.slice(0, start).split('\n').length,
    endLine: text.slice(0, end).split('\n').length, sha256: sha256(code), code};
}

function fn(text, name) {
  const matches = [...text.matchAll(new RegExp('^static[ \\t]+(?:bool|BYTE)[ \\t]+' + name + '\\s*\\(', 'gm'))];
  assert.equal(matches.length, 1, 'Expected one actual definition: ' + name);
  const start = matches[0].index;
  const brace = text.indexOf('{', start), semi = text.indexOf(';', start);
  assert(brace > start && (semi < 0 || semi > brace), 'Function declaration without body: ' + name);
  return record(text, name, start, blockEnd(text, brace));
}

function declaration(text, name, required = true) {
  const matches = [...text.matchAll(new RegExp('^(?:using[ \\t]+|static[ \\t]+constexpr[ \\t]+DWORD[ \\t]+)' + name + '\\b[^;]*;', 'gm'))];
  if (!required && matches.length === 0) return null;
  assert.equal(matches.length, 1, 'Expected one actual declaration: ' + name);
  return record(text, name, matches[0].index, matches[0].index + matches[0][0].length);
}

export function extract(text) {
  const wrapper = fn(text, 'ymccOpenXInputAssignSlot');
  const alias = declaration(text, 'OpenXInputAssignGuard', false);
  const ready = /\bOpenXInputAssignGuard\b/.test(wrapper.code.slice(0, wrapper.code.indexOf('{')));
  assert(!ready || alias, 'Guarded wrapper lacks its callback alias');
  const ranges = ['kOpenXInput_ERROR_SUCCESS', 'kOpenXInput_ERROR_UNAVAILABLE',
    'kOpenXInput_ERROR_DEVICE_NOT_CONNECTED'].map(name => declaration(text, name));
  if (alias) ranges.push(alias);
  ranges.push(fn(text, 'ymccOpenXInputGetUserIndex'), wrapper);
  return {ready, ranges, code: ranges.map(range => range.code).join('\n\n')};
}
