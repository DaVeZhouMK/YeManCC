import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {fileURLToPath} from 'node:url';
export const MODULE = 'tools/ymcc_sim/global/library-workspace-datachain-consumer';
export const HERE = path.dirname(fileURLToPath(import.meta.url));
export const FILES = ['adapter.mjs','run.mjs','contract.mjs','observe.cjs','spec.json','toolchain-inputs.json','README.md','contract.test.mjs'];
export const spec = JSON.parse(fs.readFileSync(path.join(HERE, 'spec.json'), 'utf8'));
export const sha = value => crypto.createHash('sha256').update(value).digest('hex');
export const shaFile = file => sha(fs.readFileSync(file));
export const need = (value, message) => { if (!value) throw Error(message); };
export const json = file => JSON.parse(fs.readFileSync(file, 'utf8'));
export const save = (file, value) => fs.writeFileSync(file, JSON.stringify(value, null, 2) + '\n', {flag:'wx'});
export const inside = (root, file) => { const r = path.relative(root, file); return r === '' || (!path.isAbsolute(r) && r !== '..' && !r.startsWith('..' + path.sep)); };
export function checkedFile(root, relative) {
  need(!path.isAbsolute(relative) && !relative.split(/[\\/]/).includes('..'), 'RELATIVE_SOURCE_REQUIRED');
  const base = fs.realpathSync(root), file = fs.realpathSync(path.join(base, relative));
  need(inside(base, file) && fs.statSync(file).isFile(), 'SOURCE_OUTSIDE_ROOT');
  return file;
}
export function fingerprint(file) { return {path:path.resolve(file), size:fs.statSync(file).size, sha256:shaFile(file)}; }
export function unchanged(rows) { return rows.every(row => {try {return fs.statSync(row.path).size === row.size && shaFile(row.path) === row.sha256;} catch {return false;}}); }
export function extractVm(source) {
  const spans = [];
  const add = (name, start, end) => { need(start >= 0 && end > start, 'VM_SOURCE_SPAN_MISSING:' + name); const code = source.slice(start, end); spans.push({name,start,end,startLine:source.slice(0,start).split('\n').length,endLine:source.slice(0,end).split('\n').length,sha256:sha(code),code}); };
  let start = source.indexOf('function isWorkspaceRecord(');
  add('snapshot',start,source.indexOf('function invoke(',start));
  for (const name of ['normalizedUiPath','escapeHtml','initials','artworkMarkup']) {
    const line = source.split(/\r?\n/).find(line => line.startsWith('function ' + name + '('));
    need(line, 'VM_HELPER_MISSING:' + name); start = source.indexOf(line); add(name,start,start+line.length);
  }
  start = source.indexOf('function artworkOverride(');
  add('artwork',start,source.indexOf('function artworkDropMarkup(',start));
  return spans;
}
export function mutate(source, faultId) {
  const fault = spec.faults.find(f => f.id === faultId); need(fault, 'UNKNOWN_FAULT');
  need(source.split(fault.before).length === 2, 'FAULT_ANCHOR_NOT_UNIQUE');
  const spans = extractVm(source), offset = source.indexOf(fault.before);
  need(spans.some(s => s.name === fault.vm && offset >= s.start && offset + fault.before.length <= s.end), 'FAULT_OUTSIDE_VM');
  return source.replace(fault.before,fault.after);
}
export function semanticVerdict({report,trace,exitCode,faultId,controlsOk}) {
  const fault = spec.faults.find(f => f.id === faultId);
  if (!controlsOk || !report || !trace || trace.forbidden?.length || !trace.finished || trace.vm.length !== 6 || trace.vm.some(v => !v.completed)) return false;
  if (JSON.stringify(report.cases?.map(c => c.name)) !== JSON.stringify(spec.caseNames) || report.caseCount !== spec.caseNames.length || report.fuzzCombinationCount !== 1000 || report.artworkPriorityCombinationCount !== 1000 || report.realSteamFilesModified !== false) return false;
  if (!(trace.calls.normalizeWorkspaceSnapshot?.calls >= 1000 && trace.calls.artworkEntry?.calls >= 3000)) return false;
  const failed = report.cases.filter(c => !c.passed);
  if (!faultId) return exitCode === 0 && report.allPassed === true && failed.length === 0 && trace.assertions.length === 0;
  if (!fault || exitCode !== 1 || report.allPassed !== false || JSON.stringify(failed.map(c=>c.name)) !== JSON.stringify(fault.expectedFailedNames)) return false;
  return failed.length > 0 && failed.every(c => trace.assertions.some(a => a.code === 'ERR_ASSERTION' && a.message === c.error && a.stack.includes('custom_steam_library_ui_datachain_selftest.mjs:')));
}
export function verifyReceipt(receipt) {
  return receipt.accepted===true && unchanged([...receipt.sourceBefore,...receipt.vmInputBefore,...receipt.toolBefore,...receipt.outputAfter]);
}
export function assertPlainDestination(destination) {
  let ancestor=path.resolve(destination);
  while(!fs.existsSync(ancestor)){const parent=path.dirname(ancestor);need(parent!==ancestor,'OUTPUT_ANCESTOR_REQUIRED');ancestor=parent;}
  const normalize=value=>process.platform==='win32'?path.resolve(value).toLowerCase():path.resolve(value);
  need(!fs.lstatSync(ancestor).isSymbolicLink()&&normalize(fs.realpathSync(ancestor))===normalize(ancestor),'OUTPUT_FORWARDING_FORBIDDEN');
}
