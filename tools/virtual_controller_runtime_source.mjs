// Bounded source preparation, not a C++ preprocessor. Other production includes
// remain real compiler inputs. Never follow an arbitrary local/system include.
import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';

export const virtualControllerRuntimeHeaders = Object.freeze([
  'virtual_controller_capability_runtime.h',
  'virtual_controller_start_runtime.h',
  'virtual_controller_stop_runtime.h',
  'virtual_controller_ui_output_runtime.h',
  'virtual_target_return_runtime.h',
  'hidhide_cli_writer_runtime.h',
  'virtual_target_return_cold_runtime.h',
]);
const allowed = new Set(virtualControllerRuntimeHeaders);
const sha = data => createHash('sha256').update(data).digest('hex');

// Mask comments and literals only to decide whether a leading # is a real
// directive. Read its spelling from the original bytes, preserving all code.
function directives(source) {
  const literals = /\/\/[^\r\n]*|\/\*[\s\S]*?\*\/|(?:u8|u|U|L)?R"([^ ()\\\t\r\n]{0,16})\([\s\S]*?\)\1"|"(?:\\[\s\S]|[^"\\\r\n])*"|'(?:\\[^\r\n]|[^'\\\r\n])*'/g;
  const masked = source.replace(literals, s => s.replace(/[^\r\n]/g, ' '));
  return [...masked.matchAll(/^[\t \uFEFF]*#[^\r\n]*/gm)].map(m => ({
    start: m.index,
    end: m.index + m[0].length,
    text: source.slice(m.index, m.index + m[0].length),
    masked: m[0],
  }));
}

export function readVirtualControllerSource(entryFile, {readFile = file => fs.readFileSync(file)} = {}) {
  entryFile = path.resolve(entryFile);
  const directory = path.dirname(entryFile), inputs = [], includes = [], seen = new Set();
  function expand(file, stack = []) {
    if (stack.includes(file)) throw Error('RUNTIME_INCLUDE_CYCLE:' + file);
    // Do not silently deduplicate: doing so could conceal duplicate definitions
    // from the existing exact-name/unique-anchor production extractors.
    if (seen.has(file)) throw Error('RUNTIME_INCLUDE_REPEATED:' + file);
    seen.add(file);
    const data = Buffer.from(readFile(file)), text = data.toString('utf8');
    if (!Buffer.from(text).equals(data)) throw Error('RUNTIME_SOURCE_NOT_UTF8:' + file);
    inputs.push({file, data, sha256: sha(data)});
    let source = '', cursor = 0;
    for (const directive of directives(text)) {
      const include = /^[\t \uFEFF]*#[\t ]*include[\t ]*"([^"\r\n]+)"/.exec(directive.text);
      const specifier = include?.[1], name = specifier?.replaceAll('\\', '/').split('/').at(-1);
      if (allowed.has(name)) {
        if (specifier !== name) throw Error('RUNTIME_INCLUDE_NOT_ADJACENT:' + specifier);
        const tail = directive.masked.slice(directive.text.indexOf('"') + specifier.length + 2);
        if (tail.trim()) throw Error('RUNTIME_INCLUDE_UNSUPPORTED:' + directive.text);
        const target = path.join(directory, name);
        const line = text.slice(0, directive.start).split('\n').length;
        includes.push({from: file, file: target, line});
        const nested = expand(target, [...stack, file]);
        source += text.slice(cursor, directive.start) + '#line 1 "' + name + '"\n' + nested;
        if (!nested.endsWith('\n')) source += '\n';
        source += '#line ' + line + ' "' + path.basename(file) + '"';
      } else if (file !== entryFile && /^[\t \uFEFF]*#[\t ]*pragma[\t ]+once[\t ]*$/.test(directive.masked)) {
        // A header's pragma must not acquire the identity of main.inert.cpp.
        source += text.slice(cursor, directive.start) + '// #pragma once: expanded runtime header';
      } else {
        continue;
      }
      cursor = directive.end;
    }
    return source + text.slice(cursor);
  }
  const source = expand(entryFile);
  const manifest = {
    schemaVersion: 1,
    entry: entryFile,
    allowlist: [...virtualControllerRuntimeHeaders],
    inputs: inputs.map(({file, data, sha256}) => ({file, sha256, bytes: data.length})),
    includes,
    expandedSha256: sha(source),
    expandedBytes: Buffer.byteLength(source),
    meaning: 'Verbatim adjacent runtime bodies, only pragma-once and include directives expanded; other includes remain compiler-observed inputs. Function extraction and semantic negatives operate on this source in the existing TU.',
  };
  return {source, inputs, manifest};
}

// Archive every raw contributor, even when a consumer extracts only one function.
// Writes fresh files; never alters the source snapshot used for expansion.
export function snapshotVirtualControllerSource(entryFile, snapshotDirectory, options) {
  const result = readVirtualControllerSource(entryFile, options);
  snapshotDirectory = path.resolve(snapshotDirectory);
  fs.mkdirSync(snapshotDirectory, {recursive: true});
  for (const input of result.inputs) {
    const snapshot = path.join(snapshotDirectory, path.basename(input.file));
    fs.writeFileSync(snapshot, input.data, {flag: 'wx'});
    result.manifest.inputs.find(row => row.file === input.file).snapshot = snapshot;
  }
  const expanded = path.join(snapshotDirectory, 'runtime-expanded.cpp');
  fs.writeFileSync(expanded, result.source, {flag: 'wx'});
  result.manifest.expandedSnapshot = expanded;
  fs.writeFileSync(path.join(snapshotDirectory, 'runtime-expansion.json'), JSON.stringify(result.manifest, null, 2) + '\n', {flag: 'wx'});
  return result;
}

// Multiline anchors may live in LF headers beside a CRLF main.cpp. Require
// exactly one match across both spellings, and preserve that match's newline.
export function replaceUniqueSourceText(source, before, after, label) {
  before = before.replaceAll('\r\n', '\n');
  after = after.replaceAll('\r\n', '\n');
  if (!before) throw Error('EMPTY_SOURCE_ANCHOR:' + label);
  const candidates = [...new Set([before, before.replaceAll('\n', '\r\n')])];
  const hits = candidates.flatMap(text => Array.from({length: source.split(text).length - 1}, () => text));
  if (hits.length !== 1) throw Error('NONUNIQUE_SOURCE_ANCHOR:' + label + ':' + hits.length);
  const hit = hits[0], replacement = hit.includes('\r\n') ? after.replaceAll('\n', '\r\n') : after;
  return source.replace(hit, () => replacement);
}
