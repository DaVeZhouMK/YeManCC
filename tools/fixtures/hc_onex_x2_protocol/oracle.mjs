// Independent expectations are parsed from pinned C#; this module never reads
// the production header or constructs HC/device objects.
import fs from 'node:fs';
import path from 'node:path';
import { createHash } from 'node:crypto';
export const sha = (data, algorithm = 'sha256') => createHash(algorithm).update(data).digest('hex');
const clean = s => s.replace(/"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'|\/\/[^\n]*|\/\*[\s\S]*?\*\//g,
  s => s.startsWith('/') ? s.replace(/[^\n]/g, ' ') : s);
function unique(text, regex, description) {
  const found = [...text.matchAll(new RegExp(regex.source, regex.flags.replace('g', '') + 'g'))];
  if (found.length !== 1) throw Error(`${description}: expected one source match, got ${found.length}`);
  return found[0];
}
function balanced(text, start) {
  let depth = 0;
  for (let i = start; i < text.length; ++i) {
    if (text[i] === '"' || text[i] === "'") {
      const quote = text[i];
      while (++i < text.length) { if (text[i] === '\\') ++i; else if (text[i] === quote) break; }
    } else if (text[i] === '{') ++depth;
    else if (text[i] === '}' && --depth === 0) return i;
  }
  throw Error('Unbalanced C# source block');
}
function method(text, name) {
  const regex = new RegExp(`(?:protected|private|public)\\s+(?:(?:override|virtual|async|static)\\s+)*[\\w\\[\\]<>?]+\\s+${name}\\s*\\(([^)]*)\\)\\s*(=>|\\{)`, 'g');
  const all = [...text.matchAll(regex)];
  if (all.length > 1) throw Error(`Ambiguous method ${name}`);
  if (!all.length) return null;
  const m = all[0], start = m.index + m[0].length;
  if (m[2] === '{') return { params: m[1], body: text.slice(start, balanced(text, start - 1)) };
  return { params: m[1], body: text.slice(start, text.indexOf(';', start)) };
}
const integer = token => {
  const s = token.trim(); if (!/^(?:0x[\da-f]+|\d+)$/i.test(s)) throw Error(`Unreviewed integer ${s}`);
  return Number(s);
};
const arrayValue = (body, params = {}) => {
  const matches = [...body.matchAll(/\[([^\[\]]*)\]/g)];
  if (matches.length !== 1) throw Error('Expected one C# array expression');
  return matches[0][1].split(',').map(s => s.trim()).filter(Boolean).map(token => {
    const value = Object.hasOwn(params, token) ? params[token] : integer(token);
    if (!Number.isInteger(value) || value < 0 || value > 255) throw Error('Expected byte');
    return value;
  });
};
const roleTags = { None: 0, Turbo: 1, Keyboard: 2, Home: 3, L4: 4, R4: 5 };
export function deriveOracle(workspace, fixtureDirectory) {
  const upstream = path.join(workspace, 'Docs/Research/PcCapabilities-HC-CTW-20261014/upstream');
  const pinsBytes = fs.readFileSync(path.join(fixtureDirectory, 'source-pins.json'));
  const pins = JSON.parse(pinsBytes);
  const manifestBytes = fs.readFileSync(path.join(upstream, 'source-manifest.json'));
  const manifest = JSON.parse(manifestBytes);
  const treeBytes = fs.readFileSync(path.join(upstream, 'hc-tree.json'));
  const tree = JSON.parse(treeBytes);
  if (tree.sha !== pins.commit || tree.truncated !== false) throw Error('Fixed HC tree is incomplete or belongs to a different commit');
  if (pins.commit !== 'a250b19a4b7e29dac3b383f2d1ac517d1af43e4e' || sha(manifestBytes) !== pins.manifestSha256)
    throw Error('Fixed HC manifest/commit mismatch');
  const sources = new Map(), provenance = [];
  for (const pin of pins.files) {
    const bytes = fs.readFileSync(path.join(upstream, 'sources/hc', pin.path));
    const blob = sha(Buffer.concat([Buffer.from(`blob ${bytes.length}\0`), bytes]), 'sha1');
    const rows = manifest.filter(r => r.project === 'hc' && r.path === pin.path);
    const entries = tree.tree.filter(r => r.path === pin.path && r.type === 'blob');
    if (sha(bytes) !== pin.sha256 || blob !== pin.gitBlob || rows.length !== 1 ||
        rows[0].commit !== pins.commit || rows[0].sha256.toLowerCase() !== pin.sha256 ||
        rows[0].gitBlob !== blob || entries.length !== 1 || entries[0].sha !== blob)
      throw Error(`Fixed source SHA256/Git blob mismatch: ${pin.path}`);
    const code = clean(bytes.toString('utf8'));
    const name = path.basename(pin.path, '.cs');
    const cls = unique(code, new RegExp(`public\\s+class\\s+${name}\\s*:\\s*(\\w+)`), pin.path);
    const open = code.indexOf('{', cls.index + cls[0].length);
    const namespace = unique(code, /namespace\s+([\w.]+)\s*[;{]/, `${name} namespace`)[1];
    sources.set(name, { parent: cls[1], namespace, code:code.slice(open + 1, balanced(code, open)) });
    provenance.push({ ...pin, bytes: bytes.length });
  }
  if (sources.size !== 4) throw Error('Expected exactly four pinned protocol sources');
  function resolve(cls, name) {
    const source = sources.get(cls); if (!source) throw Error(`No declaration of ${name} in fixed inheritance`);
    const own = method(source.code, name);
    return own ? { owner: cls, ...own } : resolve(source.parent, name);
  }
  function lineage(cls) {
    const source = sources.get(cls); return source ? [...lineage(source.parent), cls] : [];
  }
  const x1 = sources.get('OneXPlayerX1').code, x2 = sources.get('OneXPlayerX2').code;
  const flagRole = { None: 'None', L4: 'L4', R4: 'R4' };
  for (const label of ['Turbo', 'Keyboard', 'Home']) {
    const chords = [...x2.matchAll(new RegExp(`new KeyboardChord\\("${label}"[^;]*?ButtonFlags\\.(OEM\\d+)`, 'g'))];
    const flags = [...new Set(chords.map(m => m[1]))];
    if (flags.length !== 1 || chords.length !== (label === 'Turbo' ? 3 : 1)) throw Error(`Ambiguous chord role ${label}`);
    flagRole[flags[0]] = label;
  }
  function mapping(cls) {
    const source = sources.get(cls); if (!source) return new Map();
    const result = mapping(source.parent), own = method(source.code, 'MapVendorButton');
    if (own) {
      for (const m of own.body.matchAll(/(0x[\da-f]+)\s*=>\s*ButtonFlags\.(\w+)/gi)) result.set(integer(m[1]), m[2]);
    }
    return result;
  }
  const clickBody = resolve('OneXPlayerX2', 'HandleEvent').body;
  const clickFlags = [...clickBody.matchAll(/case\s+ButtonFlags\.(\w+)\s*:/g)].map(m => m[1]);
  const clickMs = integer(unique(clickBody, /KeyPressAndRelease\(button,\s*(\d+)\)/, 'click duration')[1]);
  if (!/if\s*\(pressed\)/.test(clickBody)) throw Error('Unexpected click release semantics');
  const constant = name => integer(unique(x1, new RegExp(`const (?:byte|int) ${name}\\s*=\\s*(0x[\\da-f]+|\\d+)\\s*;`, 'i'), name)[1]);
  const read = resolve('OneXPlayerX1', 'ReadLoopAsync').body;
  const writer = resolve('OneXPlayerX1', 'WriteVendorHidCommand').body;
  const get = (text, re, name, group = 1) => integer(unique(text, re, name)[group]);
  const wire = {
    marker: constant('FrameMarker'), buttonCommand: constant('ButtonCommandId'), statusCommand: constant('StatusCommandId'),
    minimumLogicalLength: get(read, /data\.Length\s*<\s*(\d+)/, 'min logical length'),
    markerIndex: get(read, /data\[(\d+)\]\s*!=\s*FrameMarker/, 'logical marker index'),
    markerFromEnd: get(read, /data\[\^(\d+)\]\s*!=\s*FrameMarker/, 'logical tail marker'),
    commandIndex: get(read, /data\[(\d+)\]\s*==\s*ButtonCommandId/, 'logical command index'),
    buttonIndex: get(read, /byte buttonId\s*=\s*data\[(\d+)\]/, 'logical button index'),
    pressedIndex: get(read, /bool pressed\s*=\s*data\[(\d+)\]/, 'logical press index'),
    pressedValue: get(read, /bool pressed\s*=\s*data\[\d+\]\s*==\s*(0x[\da-f]+)/i, 'logical pressed value'),
    statusIndex: get(resolve('OneXPlayerX1', 'HandleStatusReport').body, /switch\s*\(data\[(\d+)\]\)/, 'status byte'),
    payloadIndex: get(writer, /Array\.Copy\(payload,\s*0,\s*frame,\s*(\d+),/, 'payload index'),
    indicatorIndex: get(writer, /frame\[(\d+)\]\s*=\s*0x01/, 'indicator index'),
    indicator: get(writer, /frame\[\d+\]\s*=\s*(0x01)/, 'indicator'),
    frameLength: get(writer, /reportLength\s*==\s*(\d+)\s*\?/, 'documented 64-byte frame'),
    reportId: get(writer, /WithReportID\(frame,\s*(0x[\da-f]+)/i, 'report ID'),
    commandTail: get(writer, /frame\[\^(\d+)\]\s*=\s*commandId/, 'command tail'),
    markerTail: get(writer, /frame\[\^(\d+)\]\s*=\s*FrameMarker/, 'output marker tail'),
    payloadOverhead: get(writer, /payload\.Length\s*>\s*frameLength\s*-\s*(\d+)/, 'payload overhead'),
  };
  // HC also has a generic length branch; this library deliberately admits only
  // 64/65 documented by these comments, rejecting other lengths (not guessing).
  const originalX1 = fs.readFileSync(path.join(upstream, 'sources/hc/HandheldCompanion/Devices/OneXPlayer/OneXPlayerX1.cs'), 'utf8');
  if (!/65-byte HID/.test(originalX1) || !/report ID 0x00 followed by the 64-byte protocol frame/.test(originalX1))
    throw Error('Documented 65-byte output convention missing');
  function payloadFor(cls, name, values) {
    const resolved = resolve(cls, name);
    if (name === 'BuildIntercept') {
      const arms = unique(resolved.body, /enabled\s*\?\s*\[([^\]]*)\]\s*:\s*\[([^\]]*)\]/, 'intercept bool arms');
      return arrayValue(`[${values[0] ? arms[1] : arms[2]}]`);
    }
    const params = resolved.params.split(',').map(p => p.trim().split(/\s+/).at(-1));
    if (params.length !== values.length) throw Error(`Argument arity ${name}`);
    return arrayValue(resolved.body, Object.fromEntries(params.map((p,i) => [p, values[i]])));
  }
  const models = ['OneXPlayerX2', 'OneXPlayerX2MiniPro', 'OneXPlayer3'].map((cls, i) => {
    const chain = lineage(cls);
    const assignment = name => {
      let value;
      for (const c of chain) { const m = sources.get(c).code.match(new RegExp(`\\b${name}\\s*=\\s*([^;]+);`)); if (m) value = m[1].trim(); }
      if (!value) throw Error(`Missing inherited assignment ${cls}.${name}`); return value;
    };
    const pids = assignment('productIds').replace(/[\[\]]/g, '').split(',').map(t => t.trim() === 'PID_VENDOR' ? constant('PID_VENDOR') : integer(t));
    const ids = mapping(cls), roles = Array.from({length:256}, (_,id) => {
      const flag = ids.get(id) || 'None';
      // Special exists only in the old base and is fully overridden by X2.
      if (!Object.hasOwn(flagRole, flag)) throw Error(`Unmapped inherited role ${flag}`);
      return roleTags[flagRole[flag]];
    });
    const config = resolve(cls, 'ConfigureController');
    const calls = [...config.body.matchAll(/WriteVendorHidCommand\((0x[\da-f]+),\s*(Build\w+)\(([^)]*)\)\);/gi)];
    if (!calls.length) throw Error(`No initialization calls for ${cls}`);
    const init = calls.map((call, index) => {
      const values = call[3].split(',').map(s => s.trim()).map(s => s === 'false' ? false : s === 'true' ? true : integer(s));
      const after = config.body.slice(call.index + call[0].length, calls[index+1]?.index ?? config.body.length);
      const waits = [...after.matchAll(/await\s+Task\.Delay\((\d+)\)/g)];
      if (waits.length > 1) throw Error('Unexpected multiple initialization waits');
      return { command:integer(call[1]), payload:payloadFor(cls,call[2],values), delayAfterMs:waits.length ? integer(waits[0][1]) : 0 };
    });
    const filters = [...x1.matchAll(/new HidFilter\(unchecked\(\(short\)(0x[\da-f]+)\),\s*unchecked\((0x[\da-f]+)\)\)/gi)];
    if (filters.length !== 1) throw Error('Unexpected base collection filter');
    const extra = [...x2.matchAll(/hidFilters\[(0x[\da-f]+)\]\s*=\s*new HidFilter\(unchecked\(\(short\)(0x[\da-f]+)\),\s*unchecked\((0x[\da-f]+)\)\)/gi)];
    if (extra.length !== 1 || extra[0][2] !== filters[0][1] || extra[0][3] !== filters[0][2]) throw Error('Secondary PID filter mismatch');
    return { cls, canonicalClass:`${sources.get(cls).namespace}.${cls}`, model:i+1, lineage:chain, configOwner:config.owner, vid:integer(assignment('vendorId')), pids,
      usagePage:integer(filters[0][1]), usage:integer(filters[0][2]), roles, init };
  });
  return { commit:pins.commit, manifestSha256:sha(manifestBytes), treeSha256:sha(treeBytes), sourcePinsSha256:sha(pinsBytes), provenance, wire,
    clickRoles:clickFlags.map(flag => roleTags[flagRole[flag]]), clickMs, models };
}
export function cppOracle(o) {
  const structModels = o.models.map(m => `  {${JSON.stringify(m.cls)},${JSON.stringify(m.canonicalClass)},${m.model},${m.vid},{${m.pids}},${m.usagePage},${m.usage},
    {${m.roles}},
    {${m.init.map(s => `{${s.command},{${s.payload}},${s.delayAfterMs}}`).join(',')}}}`).join(',\n');
  return `#pragma once
#include <array>
#include <cstdint>
#include <vector>
namespace oracle {
struct Init { unsigned command; std::vector<std::uint8_t> payload; unsigned delay; };
struct Model { const char* cls; const char* canonicalClass; unsigned tag,vid; std::vector<unsigned> pids; unsigned page,usage; std::array<unsigned,256> roles; std::vector<Init> init; };
inline const std::vector<Model> models = {\n${structModels}\n};
${Object.entries(o.wire).map(([k,v])=>`inline constexpr std::size_t ${k} = ${v};`).join('\n')}
inline constexpr unsigned clickMs = ${o.clickMs};
inline const std::vector<unsigned> clickRoles = {${o.clickRoles}};
} // namespace oracle
`;
}