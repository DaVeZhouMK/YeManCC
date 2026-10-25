import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
export const hash = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
export const need = (value, code) => { if (!value) throw Error('LIBRARY_CONSUMER_' + code); };
export const inside = (root, file) => { const r = path.relative(path.resolve(root), path.resolve(file)); return r === '' || (!path.isAbsolute(r) && r !== '..' && !r.startsWith('..' + path.sep)); };
export function sourceFile(repo, relative) {
  need(typeof relative === 'string' && relative.length && !path.isAbsolute(relative) && !relative.includes(String.fromCharCode(92)) && !relative.split('/').some(p => p === '..' || p === '') && !/[\0\r\n]/.test(relative), 'RELATIVE_SOURCE_REQUIRED');
  const file = path.resolve(repo, relative);
  need(inside(fs.realpathSync(repo), fs.realpathSync(file)), 'SOURCE_REALPATH_ESCAPE');
  return file;
}
export function applyFault(text, fault) {
  const begin = text.indexOf(fault.functionBegin), end = text.indexOf(fault.functionEnd, begin + fault.functionBegin.length);
  need(begin >= 0 && end > begin, 'FAULT_FUNCTION_ANCHOR_CHANGED');
  const body = text.slice(begin, end);
  need(body.split(fault.before).length === 2 && fault.before !== fault.after, 'FAULT_EXACT_OPERAND_CHANGED');
  return text.slice(0, begin) + body.replace(fault.before, fault.after) + text.slice(end);
}
export function verifyOriginal(raw, spec, { negative = false, exitCode } = {}) {
  need(raw && typeof raw.allPassed === 'boolean' && Array.isArray(raw.cases) && raw.caseCount === raw.cases.length, 'ORIGINAL_REPORT_SCHEMA');
  need(JSON.stringify(raw.cases.map(r => r.name)) === JSON.stringify(spec.caseNames), 'ORIGINAL_ASSERTION_SEQUENCE_CHANGED');
  need(raw.cases.every(r => typeof r.passed === 'boolean') && raw.realSteamFilesModified === false, 'ORIGINAL_BOUNDARY_MISSING');
  if (spec.id.includes('account-discovery')) need(raw.steamStoppedOrLaunched === false && raw.networkUsed === false, 'ACCOUNT_EXTERNAL_IO_BOUNDARY');
  else need(raw.fuzzCombinationCount === 1000 && raw.artworkPriorityCombinationCount === 1000, 'ORIGINAL_FUZZ_NOT_EXECUTED');
  const failed = raw.cases.filter(r => !r.passed).map(r => r.name);
  if (negative) {
    need(exitCode === 1 && raw.allPassed === false && JSON.stringify(failed) === JSON.stringify(spec.fault.expectedFailedNames), 'ORIGINAL_NAMED_SEMANTIC_FAILURE_NOT_DETECTED');
    const proofs = spec.fault.expectedFailureErrors;
    need(Array.isArray(proofs) && proofs.length === failed.length && proofs.every((proof, i) => proof.name === failed[i] && typeof proof.message === 'string' && proof.message.length > 0), 'ORIGINAL_FAILURE_ERROR_CONTRACT');
    for (const [i, row] of raw.cases.filter(r => !r.passed).entries()) {
      const proof = proofs[i];
      need(typeof row.error === 'string' && row.error.length > 0, 'ORIGINAL_BUSINESS_ASSERTION_ERROR_MISSING');
      if (proof.producerFrame) {
        const split = row.error.indexOf('\n    at '), frame = row.error.slice(split + 1).split('\n')[0];
        need(proof.message.startsWith('AssertionError [ERR_ASSERTION]:') && split >= 0 && row.error.slice(0, split) === proof.message, 'ORIGINAL_BUSINESS_ASSERTION_ERROR_CHANGED');
        need(frame.startsWith('    at file:///') && decodeURIComponent(frame).endsWith('/' + spec.producer + ':' + proof.producerFrame.line + ':' + proof.producerFrame.column), 'ORIGINAL_BUSINESS_ASSERTION_FRAME_CHANGED');
      } else need(row.error === proof.message, 'ORIGINAL_BUSINESS_ASSERTION_ERROR_CHANGED');
    }
  }
  else need(exitCode === 0 && raw.allPassed === true && failed.length === 0, 'ORIGINAL_BEHAVIOR_FAILED');
  return { cases: raw.caseCount, failedNames: failed, originalAssertionOrderPreserved: true, businessAssertionErrorsVerified: negative };
}