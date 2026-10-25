import path from 'node:path';
/** Fixed consumer orchestration for filtered log signals. No event can name a
 * module, command, device or path. Product operations stay inside each reviewed
 * consumer's memory boundary. Missing consumers/evidence cannot pass. */
import { pathToFileURL } from 'node:url';
import { sourcePath, sha256File } from './runner.mjs';

export const SIGNAL_CONSUMERS = Object.freeze({
  fan: Object.freeze({ file: 'tools/ymcc_sim/global/consumer/fan-signal-consumer.mjs', exportName: 'replayFanSignals' }),
  performance: Object.freeze({ file: 'tools/ymcc_sim/global/gpu-memory-signals/log-consumer.mjs', exportName: 'replayGpuMemorySignals' }),
  input: Object.freeze({ file: 'tools/ymcc_sim/global/input-signals/input-signal-consumer.mjs', exportName: 'replayInputSignals' }),
});
const issue = (code, domain, stage='consumer') => ({ code, domain, stage });
const snapshot = (root, file) => { try { return sha256File(sourcePath(root, file)); } catch { return null; } };

/** Dependency injection is for the orchestrator's own tests; the CLI always
 * loads the fixed, reviewed files above, never an event-supplied module. */
export async function replaySignals({ repoRoot, plan, domains = ['fan','input'], workDir, loadConsumer } = {}) {
  const result = { schemaVersion: 1, kind: 'ymcc-log-signal-consumption', status: 'BLOCKED',
    sourceArchive: plan?.sourceArchive ?? null, planStatus: plan?.status ?? null, results: [], diagnostics: [],
    inputEventCount: Array.isArray(plan?.events) ? plan.events.length : 0,
    evidenceMeaning: 'per-consumer-product-software-behavior;not-physical-hardware-or-exact-wallclock-timeline',
    exactEventTimelineReplayed: false, hardwareValidated: false, operatingSystemSleepPerformed: false,
    installedApplicationMutated: false, fullProductAcceptance: false };
  if (!Array.isArray(domains) || !domains.length || domains.some(domain => !Object.hasOwn(SIGNAL_CONSUMERS, domain))) {
    result.status='FAIL'; result.diagnostics.push(issue('INVALID_SIGNAL_DOMAIN',null,'input')); return result;
  }
  if (!plan || !Array.isArray(plan.events) || !['READY','INCOMPLETE'].includes(plan.status)) {
    result.diagnostics.push(issue('SOURCE_PLAN_NOT_USABLE',null,'input')); return result;
  }
  if (!plan.sourceArchive || plan.sourceArchive.stable !== true || !/^[a-f0-9]{64}$/.test(plan.sourceArchive.sha256 ?? '')) {
    result.diagnostics.push(issue('SOURCE_ARCHIVE_IDENTITY_UNPROVEN',null,'input')); return result;
  }
  if(plan.sourceHashes!==undefined) {
    if(!Array.isArray(plan.sourceHashes)||!plan.sourceHashes.length||plan.sourceHashes.some(row=>typeof row?.path!=='string'||!/^[a-f0-9]{64}$/.test(row.sha256??'')||snapshot(repoRoot,row.path)!==row.sha256)) {
      result.status='FAIL';result.diagnostics.push(issue('PLAN_SOURCE_CHANGED_OR_MISSING',null,'input'));return result;
    }
    result.planSourceHashes=plan.sourceHashes.map(row=>({...row}));
  }
  for (const domain of [...new Set(domains)]) {
    const spec=SIGNAL_CONSUMERS[domain], events=plan.events.filter(event => event.domain === domain);
    const row={domain, status:'BLOCKED', inputEventCount:events.length, consumer:spec.file, sourceHashBefore:snapshot(repoRoot,spec.file), sourceHashAfter:null, result:null};
    result.results.push(row);
    if (!events.length) { result.diagnostics.push(issue('NO_DOMAIN_SIGNALS',domain,'input')); continue; }
    if (!row.sourceHashBefore && !loadConsumer) { result.diagnostics.push(issue('CONSUMER_NOT_CONNECTED',domain)); continue; }
    try {
      const fn=loadConsumer ? await loadConsumer(domain, spec) : (await import(pathToFileURL(sourcePath(repoRoot,spec.file)).href))[spec.exportName];
      if (typeof fn !== 'function') { result.diagnostics.push(issue('CONSUMER_EXPORT_MISSING',domain)); continue; }
      row.result=await fn({repoRoot, events, sourceArchive:plan.sourceArchive, ...(workDir ? {workDir:path.join(workDir,domain)} : {})});
      row.sourceHashAfter=snapshot(repoRoot,spec.file);
      if (row.sourceHashBefore !== row.sourceHashAfter) { row.status='FAIL'; result.diagnostics.push(issue('CONSUMER_SOURCE_CHANGED',domain)); continue; }
      const normalized = {PASS:'PASS',INCOMPLETE:'INCOMPLETE',INCOMPLETE_COVERAGE:'INCOMPLETE',BLOCKED:'BLOCKED',BLOCKED_COMPATIBILITY_DIFFERENCE:'BLOCKED',FAIL:'FAIL'}[row.result?.status];
      if (!row.result || !normalized) {
        row.status='FAIL'; result.diagnostics.push(issue('CONSUMER_RECEIPT_INVALID',domain,'feedback')); continue;
      }
      const evidence=row.result.evidence ?? row.result.inputConsumerStateFeedback;
      if (!Array.isArray(evidence) || !evidence.length || evidence.some(row => !row || !['input','consumer','state','feedback'].every(key => Object.hasOwn(row,key)))) {
        row.status='BLOCKED'; result.diagnostics.push(issue('CONSUMER_FEEDBACK_MISSING',domain,'feedback')); continue;
      }
      const sources = Array.isArray(row.result.sourceHashes) ? row.result.sourceHashes
        : Object.entries(row.result.sourceHashes ?? {}).map(([path,sha256])=>({path,sha256}));
      if (!sources.length) { row.status='BLOCKED'; result.diagnostics.push(issue('CONSUMER_PRODUCT_HASHES_MISSING',domain,'feedback')); continue; }
      const stale=sources.some(item=>typeof item.path!=='string' || !/^[a-f0-9]{64}$/i.test(item.sha256??'') || snapshot(repoRoot,item.path)!==item.sha256.toLowerCase());
      if(stale) { row.status='FAIL'; result.diagnostics.push(issue('PRODUCT_SOURCE_CHANGED_OR_MISSING',domain,'feedback')); continue; }
      row.status=normalized;
    } catch {
      row.status='FAIL'; result.diagnostics.push(issue('CONSUMER_EXECUTION_FAILED',domain));
    }
  }
  result.status=result.results.some(row=>row.status==='FAIL') ? 'FAIL'
    : result.results.some(row=>row.status==='BLOCKED') ? 'BLOCKED'
    : plan.status!=='READY' || result.results.some(row=>row.status==='INCOMPLETE') ? 'INCOMPLETE'
    : 'PASS';
  if(result.planSourceHashes?.some(row=>snapshot(repoRoot,row.path)!==row.sha256)) {
    result.status='FAIL';result.diagnostics.push(issue('PLAN_SOURCE_CHANGED_DURING_CONSUMPTION',null,'feedback'));
  }
  result.summary={domains:result.results.length,executed:result.results.filter(row=>row.result).length,
    passed:result.results.filter(row=>row.status==='PASS').length, physicalReceipts:0};
  return result;
}
