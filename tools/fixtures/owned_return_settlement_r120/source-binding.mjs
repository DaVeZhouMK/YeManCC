import assert from 'node:assert/strict';
import crypto from 'node:crypto';
export const sha = value => crypto.createHash('sha256').update(value).digest('hex');
function endBody(text, start) {
  let depth = 0, mode = 'code';
  for (let i = start; i < text.length; ++i) {
    const c = text[i], n = text[i + 1];
    if (mode === 'line') { if (c === '\n') mode = 'code'; continue; }
    if (mode === 'block') { if (c === '*' && n === '/') { mode = 'code'; ++i; } continue; }
    if (mode === 'string' || mode === 'char') {
      if (c === '\\') { ++i; continue; }
      if (c === (mode === 'string' ? '"' : "'")) mode = 'code';
      continue;
    }
    if (c === '/' && n === '/') { mode = 'line'; ++i; continue; }
    if (c === '/' && n === '*') { mode = 'block'; ++i; continue; }
    if (c === '"') { mode = 'string'; continue; }
    if (c === "'") { mode = 'char'; continue; }
    if (c === '{') ++depth;
    if (c === '}' && --depth === 0) return i + 1;
  }
  throw Error('Unclosed source body');
}
export function extract(text, kind, name, file, bindings) {
  const prefix = kind === 'function' ? '^static[^\\n;{]*\\b' + name + '\\s*\\(' :
    kind === 'enum' ? '^enum class\\s+' + name + '\\s*\\{' : '^struct\\s+' + name + '\\s*\\{';
  const pattern = new RegExp(prefix, 'mg');
  let match;
  while ((match = pattern.exec(text))) {
    const brace = text.indexOf('{', match.index), semi = text.indexOf(';', match.index);
    if (brace < 0 || (semi >= 0 && semi < brace)) continue;
    let end = endBody(text, brace);
    if (kind !== 'function') { assert.equal(text[end], ';'); ++end; }
    const code = text.slice(match.index, end);
    bindings.push({ file, kind, symbol: name, startLine: text.slice(0, match.index).split('\n').length,
      endLine: text.slice(0, end).split('\n').length, sha256: sha(code),
      startUtf8Byte: Buffer.byteLength(text.slice(0, match.index)), endUtf8ByteExclusive: Buffer.byteLength(text.slice(0, end)) });
    return code;
  }
  throw Error('Missing production definition: ' + file + '::' + name);
}
