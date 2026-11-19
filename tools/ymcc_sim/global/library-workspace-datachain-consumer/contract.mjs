import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {fileURLToPath} from 'node:url';
import {isDeepStrictEqual} from 'node:util';
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
  if (controlsOk !== true || !report || trace?.finished !== true || !Array.isArray(trace.forbidden) || trace.forbidden.length !== 0 || !Array.isArray(trace.assertions)) return false;
  if (!Array.isArray(trace.vm) || trace.vm.length !== 6 || trace.vm.some(v => v?.completed !== true)) return false;
  if (!Array.isArray(report.cases) || report.cases.some(c => typeof c?.passed !== 'boolean')) return false;
  if (JSON.stringify(report.cases.map(c => c.name)) !== JSON.stringify(spec.caseNames) || report.caseCount !== spec.caseNames.length || report.fuzzCombinationCount !== 1000 || report.artworkPriorityCombinationCount !== 1000 || report.realSteamFilesModified !== false) return false;
  const normalizeCalls = trace.calls?.normalizeWorkspaceSnapshot?.calls, artworkCalls = trace.calls?.artworkEntry?.calls;
  if (!Number.isInteger(normalizeCalls) || normalizeCalls < 1000 || !Number.isInteger(artworkCalls) || artworkCalls < 3000) return false;
  const failed = report.cases.filter(c => c.passed === false);
  if (!faultId) return exitCode === 0 && report.allPassed === true && failed.length === 0 && trace.assertions.length === 0;
  if (!fault || exitCode !== 1 || report.allPassed !== false || JSON.stringify(failed.map(c=>c.name)) !== JSON.stringify(fault.expectedFailedNames)) return false;
  return failed.length > 0 && failed.every(c => typeof c.error === 'string' && trace.assertions.some(a => a?.code === 'ERR_ASSERTION' && a.message === c.error && typeof a.stack === 'string' && a.stack.includes('custom_steam_library_ui_datachain_selftest.mjs:')));
}
const samePin = (a,b) => b?.path===a.path && b.size===a.size && b.sha256===a.sha256;
function receiptPins(rows, requiredPaths) {
  need(Array.isArray(rows) && rows.length===requiredPaths.length, 'RECEIPT_PIN_COUNT');
  const pins=new Map(rows.map(row=>{
    need(typeof row?.path==='string' && path.isAbsolute(row.path) && path.resolve(row.path)===row.path &&
      Number.isSafeInteger(row.size) && row.size>=0 && typeof row.sha256==='string' && /^[\da-f]{64}$/.test(row.sha256), 'RECEIPT_PIN_INVALID');
    return [row.path,row];
  }));
  need(pins.size===rows.length && new Set(requiredPaths).size===rows.length && requiredPaths.every(file=>pins.has(file)), 'RECEIPT_PIN_MEMBERS');
  return pins;
}
export function verifyReceipt(receipt) {
  try {
    if (receipt?.schemaVersion!==1 || receipt.accepted!==true || receipt.controlStatus!=='PASS' ||
        !Array.isArray(receipt.controlFailures) || receipt.controlFailures.length!==0) return false;
    const {caseDir,child,faultId,outputBefore}=receipt, negative=faultId!==null;
    const fresh=value=>value?.reportExists===false && value?.traceExists===false;
    if (typeof caseDir!=='string' || !path.isAbsolute(caseDir) || path.resolve(caseDir)!==caseDir ||
        receipt.kind!==(negative?'semantic-negative':'original-positive') || receipt.semanticDetected!==negative ||
        (negative && !spec.faults.some(f=>f.id===faultId)) || !fresh(outputBefore) ||
        child?.exitCode!==(negative?1:0) || child.error!==null || child.signal!==null) return false;
    const reportName='Build/CustomSteamLibrary/ui-datachain-summary.json';
    const files=['stdout.txt','stderr.txt','child.json','plan.json','observer-config.json','vm-inputs.json',reportName,'trace.json'];
    const file=name=>path.join(caseDir,name);
    const outputs=receiptPins(receipt.outputAfter,files.map(file));
    const read=name=>{
      const target=file(name), pin=outputs.get(target);
      assertPlainDestination(target);
      const bytes=fs.readFileSync(target);
      need(bytes.length===pin.size && sha(bytes)===pin.sha256,'RECEIPT_OUTPUT_CHANGED');
      return bytes;
    };
    const plan=JSON.parse(read('plan.json'));
    if (plan.schemaVersion!==1 || plan.caseDir!==caseDir || plan.faultId!==faultId || !fresh(plan.outputBefore) ||
        !isDeepStrictEqual(receipt.fault,plan.fault) || !isDeepStrictEqual(child,JSON.parse(read('child.json')))) return false;
    const verifyInputs=(before,after,planned,paths)=>{
      const pins=receiptPins(before,paths), closing=receiptPins(after,paths), opening=receiptPins(planned,paths);
      return [...pins.values()].every(pin=>samePin(pin,opening.get(pin.path)) && samePin(pin,closing.get(pin.path))) && unchanged([...pins.values()]);
    };
    const toolRoot=path.dirname(plan.tools[1].path);
    if (!samePin(child.node,plan.tools[0]) ||
        !verifyInputs(receipt.sourceBefore,receipt.sourceAfter,plan.sourceInputs,spec.sourceInputs.map(rel=>checkedFile(plan.repoRoot,rel))) ||
        !verifyInputs(receipt.vmInputBefore,receipt.vmInputAfter,plan.viewInputs,spec.sourceInputs.map(rel=>file(path.join('sourceview','repo',rel)))) ||
        !verifyInputs(receipt.toolBefore,receipt.toolAfter,plan.tools,[child.node.path,...FILES.map(name=>path.join(toolRoot,name))])) return false;
    const reportBytes=read(reportName), report=JSON.parse(reportBytes), trace=JSON.parse(read('trace.json'));
    if (!semanticVerdict({report,trace,exitCode:child.exitCode,faultId,controlsOk:true})) return false;
    if (read('stdout.txt').toString('utf8')!==reportBytes.toString('utf8')+'\n') return false;
    for (const name of ['stderr.txt','observer-config.json','vm-inputs.json']) read(name);
    return isDeepStrictEqual(receipt.originalResult,{allPassed:report.allPassed,caseCount:report.caseCount,failed:report.cases.filter(c=>c.passed===false)});
  } catch {return false;}
}
export function assertPlainDestination(destination) {
  let ancestor=path.resolve(destination);
  while(!fs.existsSync(ancestor)){const parent=path.dirname(ancestor);need(parent!==ancestor,'OUTPUT_ANCESTOR_REQUIRED');ancestor=parent;}
  const normalize=value=>process.platform==='win32'?path.resolve(value).toLowerCase():path.resolve(value);
  need(!fs.lstatSync(ancestor).isSymbolicLink()&&normalize(fs.realpathSync(ancestor))===normalize(ancestor),'OUTPUT_FORWARDING_FORBIDDEN');
}
