import {auditLifecycleAcceptance,applyLifecycleVerdict,readLifecycleExecutionReceipts} from './capability-lifecycle/index.mjs';
import {parseCapabilities, assertCapabilityMode, selectCapabilities} from './capability-lifecycle/select-capabilities.mjs';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

import { configureCandidateAdapters } from './candidate-options.mjs';
import { sha256File, runAdapters, validateAdapters, verdictFor, compareRuns, makeRunDirectory, defaultEvidenceRoot, makeCoverageWorkItems } from './runner.mjs';

export function parseArgs(args) {
  const options = { mode: 'inventory', profile: 'desktop', domains: [], tests: [], receipts: [], requireAll: false };
  const allowed = new Set(['mode','profile','domain','test','capability','out-dir','payload-root','powershell','log-path','baseline','candidate','log-root','archive','python','receipt','complete-host-contract','runtime-root','shared-runtime-root','lighting-payload-root','expected-helper-sha256','hc-profile','hc-version','legacy-host-root','fan-tool-load-plan','fan-tool-load-plan-sha256','monitor-field-log-input']);
  for (let i = 0; i < args.length; i++) {
    const arg = args[i];
    if (arg === '--require-all') { options.requireAll = true; continue; }
    if (arg === '--audit-registered') { if(options.auditRegistered)throw Error('DUPLICATE_ARGUMENT:audit-registered');options.auditRegistered=true;continue; }
    if (arg === '--help') { options.help = true; continue; }
    if (!arg.startsWith('--') || !allowed.has(arg.slice(2)) || !args[i + 1] || args[i + 1].startsWith('--')) throw new Error(`INVALID_ARGUMENT:${arg}`);
    const value = args[++i], name = arg.slice(2);
    if (name === 'capability') options.capabilities = parseCapabilities([...(options.capabilities ?? []), value]);
    else if (name === 'domain') options.domains.push(...value.split(',').filter(Boolean));
    else if (name === 'receipt') options.receipts.push(value);
    else if (name === 'test') options.tests.push(...value.split(',').filter(Boolean));
    else {
      const key = name.replace(/-([a-z])/g, (_, c) => c.toUpperCase());
      if (options._seen?.has(key)) throw new Error(`DUPLICATE_ARGUMENT:${name}`);
      options._seen ??= new Set(); options._seen.add(key); options[key] = value;
    }
  }
  if (!['inventory','plan','run','replay','compare','corpus','replay-plan','log-check','signal-check','audit'].includes(options.mode)) throw new Error('INVALID_MODE');
  if (!['desktop','full','sleep'].includes(options.profile)) throw new Error('INVALID_PROFILE');
  if(options.auditRegistered&&options.mode!=='audit')throw Error('AUDIT_REGISTERED_REQUIRES_AUDIT_MODE');
  if (options.monitorFieldLogInput && !['log-check','replay-plan'].includes(options.mode)) throw Error('MONITOR_FIELD_INPUT_REQUIRES_LOG_MODE');
  if (options.capabilities) assertCapabilityMode(options.mode);
  delete options._seen;
  return options;
}
const readJson = file => JSON.parse(fs.readFileSync(file, 'utf8').replace(/^\uFEFF/, ''));
const tableCell = text => String(text ?? '').replaceAll('|', '\\|').replace(/[\r\n]/g, ' ');
export function writeReport(run, outDir) {
  const results = run.results ?? [];
  const surfaces = run.catalog?.surfaces ?? [];
  const missing = surfaces.filter(item => item.coverage === 'NOT_COVERED');
  const lines = [
    '# YMCC 全局模拟测试', '',
    `运行标识：${run.runId} · ${run.mode} / ${run.profile}`, '',
    `本轮结论：**${run.verdict.status}**。已执行 ${results.length} 项，${results.filter(item => item.status === 'PASS').length} 项通过。`, '',
    '本报告分别记录执行结果、源码入口关联和实机验收；注册了测试不表示该文件的每个分支或原生接口已经验证。', '',
    `自动发现 ${surfaces.length} 个功能入口，其中 ${missing.length} 个尚未关联测试。当前通过仅适用于报告内实际执行的测试范围。`, '',
    '## 各功能结果', '',
    '| 功能 | 测试数 | 通过 | 失败/超时 | 阻塞 | 入口未关联测试 |',
    '| --- | ---: | ---: | ---: | ---: | ---: |',
  ];
  const domains = new Set([...surfaces.map(item => item.domain), ...results.map(item => item.domain)]);
  for (const domain of [...domains].sort()) {
    const list = results.filter(item => item.domain === domain);
    lines.push(`| ${tableCell(domain)} | ${list.length} | ${list.filter(item => item.status === 'PASS').length} | ${list.filter(item => ['FAIL','TIMEOUT'].includes(item.status)).length} | ${list.filter(item => item.status === 'BLOCKED').length} | ${missing.filter(item => item.domain === domain).length} |`);
  }
  lines.push('', '## 已执行的测试', '', '| 测试 | 类型 | 结果 | 证据 |', '| --- | --- | --- | --- |');
  for (const result of results) lines.push(`| ${tableCell(result.id)} | ${tableCell(result.scope)} | ${tableCell(result.status)} | ${tableCell(result.evidenceDir)} |`);
  if(run.lifecycleAcceptance)lines.push('', '## 退出销毁与持续类恢复', '',
    '生命周期验收：'+run.lifecycleAcceptance.status+'；已声明功能 '+run.lifecycleAcceptance.summary.declaredFeatures+'，通过 '+run.lifecycleAcceptance.summary.passed+'，未判定的测试入口 '+run.lifecycleAcceptance.summary.unclassifiedAdapters+'。',
    '退出须证明自有资源清零、挂起关闭可有界销毁、晚到回调不能复活；持续类须证明睡眠和暂时故障后当前代次健康，显式关闭及重试上限优先。缺失分类或运行证据不代表通过，详见 lifecycle-acceptance.json。');
  lines.push('', '## 范围与限制', '',
    '- behavioral-mock：执行真实产品逻辑，外部依赖由已有测试夹具替代。',
    '- source-contract：检查源码或接口约定；不算运行行为验收。',
    '- host-mock：仅自建风扇 Host 模拟；不代表 EC/OEM、风扇转速或真实睡眠。',
    '- 本入口不执行真实系统睡眠、不写硬件、不操控安装版或其他用户程序。已有专用应用测试需单独绑定身份运行。',
    '- 未关联测试的入口完整列在 catalog.json；新增入口也会进入清单。未知归类和动态调用保留为待核对。',
    '- coverage-work-items.json 按功能与源码文件生成后续执行队列，保留失败、动态入口、静态检查和行为审查缺口。',
    '- 本轮不证明整机所有功能已通过，不证明 CPU 或功耗改善。',
    '- full 或 require-all 模式下覆盖缺口会使总结果为 INCOMPLETE_COVERAGE，不能作为全功能验收放行。',
    '', '## 后续执行', '',
    '优先处理 result.json 中 FAIL / TIMEOUT / BLOCKED，再处理 catalog.json 的 NOT_COVERED。修改后重跑相关测试，并用 compare 模式保留修复前后的差异。',
  );
  fs.writeFileSync(path.join(outDir, 'report.md'), lines.join('\n'));
}

export async function main(args = process.argv.slice(2), { repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..') } = {}) {
  const options = parseArgs(args);
  if (options.help) {
    console.log('YMCC global simulator: --mode inventory|plan|run|replay|compare|corpus|replay-plan|log-check|signal-check|audit --profile desktop|full|sleep [--domain fan,input] [--test id] [--capability fan,motion (repeatable; inventory/plan/run only)] [--out-dir PATH] [--payload-root CURRENT_HOST_DIRECTORY] [--runtime-root DIR --shared-runtime-root DIR] [--lighting-payload-root DIR --expected-helper-sha256 REVIEWED_SHA256] [--hc-profile PROFILE_NAME --hc-version LABEL] [--legacy-host-root PINNED_OLD_HOST_DIRECTORY] [--powershell PWSH] [--log-path FILE] [--baseline result.json --candidate result.json] [--log-root DIR --archive ZIP --python PYTHON] [--monitor-field-log-input BOUNDED_INLINE_JSON (log-check/replay-plan only)] [--receipt RUN_RESULT_JSON (repeatable)] [--audit-registered] [--require-all]');
    return 0;
  }
  const { runId, runDir } = makeRunDirectory(options.outDir ? path.resolve(options.outDir) : defaultEvidenceRoot());
  console.log(`YMCC_SIM_RUN ${runId}\nEVIDENCE ${runDir}`);
  if (options.mode === 'audit') {
    if(!options.receipts.length)throw new Error('AUDIT_REQUIRES_RECEIPTS');
    const {auditReceipts,auditRegisteredScope}=await import('./receipt-audit.mjs');
    let report=auditReceipts(options.receipts,{repoRoot});
    if(options.auditRegistered||options.requireAll||options.profile==='full'){
      const [normal,sleep,extra]=await Promise.all([import('./adapters.mjs'),import('./sleep.mjs'),import('./extra-adapters.mjs')]);
      // Missing cases need registration identities, not unrelated tool scans.
      // A supplied/selected case and every declared lifecycle capability still
      // use their actual descriptor factory and the original validation.
      configureCandidateAdapters([],options);
      const rows=[...normal.getAdapters(repoRoot),...sleep.getSleepAdapters(repoRoot),...extra.getExtraAdapters(repoRoot,{selectedIds:options.tests.length?options.tests:undefined,auditReceiptIds:report.cases.map(row=>row.id)})];
      const registered=rows.map(row=>row.registrationIdentityOnly?row:validateAdapters(configureCandidateAdapters([row],options),repoRoot)[0]);
      report=auditRegisteredScope(report,registered,{profile:options.profile,domains:options.domains,tests:options.tests});
      const lifecycleSelected=registered.filter(item=>item.profiles.includes(options.profile)&&(!options.domains.length||options.domains.includes(item.domain))&&(!options.tests.length||options.tests.includes(item.id)));
      const preciseSourceAudit=options.tests.length>0&&lifecycleSelected.length>0&&lifecycleSelected.every(item=>item.scope==='source-contract'&&item.lifecycleContract===undefined);
      report.lifecycleAcceptance=auditLifecycleAcceptance({repoRoot,adapters:lifecycleSelected,results:report.status==='CURRENT_RECEIPTS_VALID'?readLifecycleExecutionReceipts(options.receipts):[],requireAll:options.requireAll||!options.tests.length||!preciseSourceAudit});
      if(preciseSourceAudit){
        report.lifecycleAcceptance.meaning+=' Exact source-contract receipt scope only; lifecycle behavior is unverified.';
        report.diagnostics.push({code:'EXPLICIT_SOURCE_CONTRACT_LIFECYCLE_UNVERIFIED'});
      }
      if(options.requireAll&&report.status==='CURRENT_RECEIPTS_VALID'){
        report.status='INCOMPLETE_ACCEPTANCE';report.diagnostics.push({code:'AUDIT_DOES_NOT_EXECUTE_MISSING_TESTS'});
      }else if(report.status==='CURRENT_RECEIPTS_VALID'&&!preciseSourceAudit&&report.lifecycleAcceptance.status!=='PASS')report.status=report.lifecycleAcceptance.status==='FAIL'?'FAIL':'INCOMPLETE_LIFECYCLE_ACCEPTANCE';
    }else{
      // Receipt-only compatibility is limited to unclassified legacy scopes;
      // declared capabilities may never bypass their lifecycle contract.
      const {getExtraAdapters}=await import('./extra-adapters.mjs');
      const ids=report.cases.map(row=>row.id),declared=getExtraAdapters(repoRoot,{selectedIds:ids,auditReceiptIds:ids}).filter(row=>row.lifecycleContract!==undefined);
      if(declared.length){report.lifecycleAcceptance=auditLifecycleAcceptance({repoRoot,adapters:declared,results:report.status==='CURRENT_RECEIPTS_VALID'?readLifecycleExecutionReceipts(options.receipts):[],requireAll:true});if(report.status==='CURRENT_RECEIPTS_VALID'&&report.lifecycleAcceptance.status!=='PASS')report.status=report.lifecycleAcceptance.status==='FAIL'?'FAIL':'INCOMPLETE_LIFECYCLE_ACCEPTANCE';}
    }
    fs.writeFileSync(path.join(runDir,'receipt-audit.json'),JSON.stringify(report,null,2));
    console.log(`RECEIPT_AUDIT ${report.status}; current=${report.summary?.currentPass??0}; rerun=${report.needsRerun?.length??0}\nRESULT ${path.join(runDir,'receipt-audit.json')}`);
    return report.status==='CURRENT_RECEIPTS_VALID'?0:report.status==='FAIL'?2:10;
  }
  if (options.mode === 'compare') {
    if (!options.baseline || !options.candidate) throw new Error('COMPARE_REQUIRES_BASELINE_AND_CANDIDATE');
    const difference = compareRuns(readJson(options.baseline), readJson(options.candidate));
    difference.inputs = [options.baseline, options.candidate].map(file => ({ path: path.resolve(file), sha256: sha256File(file) }));
    fs.writeFileSync(path.join(runDir, 'diff.json'), JSON.stringify(difference, null, 2));
    console.log(`DIFF regressions=${difference.regressions} notRerun=${difference.scopeGaps} complete=${difference.comparisonComplete}`);
    return difference.regressions ? 2 : !difference.comparisonComplete ? 10 : 0;
  }
  if (options.mode === 'corpus' || options.mode === 'replay-plan') {
    const { indexLogCorpus, buildLogReplayPlan } = await import('./log-corpus.mjs');
    if (options.mode === 'corpus' && !options.logRoot) throw new Error('CORPUS_REQUIRES_LOG_ROOT');
    if (options.mode === 'replay-plan' && !options.archive) throw new Error('REPLAY_PLAN_REQUIRES_ARCHIVE');
    const evidence = options.mode === 'corpus'
      ? await indexLogCorpus(path.resolve(options.logRoot), { pythonPath: options.python })
      : await buildLogReplayPlan(path.resolve(options.archive), { pythonPath: options.python, domains: options.domains });
    if (options.monitorFieldLogInput) {
      const {readMonitorFieldLogFeedback} = await import('./monitor-notice-lifecycle/field-log-feedback.mjs');
      evidence.monitorFieldLogFeedback = readMonitorFieldLogFeedback({inputPath: options.monitorFieldLogInput, sourceArchive: evidence.sourceArchive});
      fs.writeFileSync(path.join(runDir, 'monitor-field-feedback.json'), JSON.stringify(evidence.monitorFieldLogFeedback, null, 2));
      if (['REJECTED','DIFFERENCE'].includes(evidence.monitorFieldLogFeedback.status)) evidence.status = 'FAIL';
    }
    fs.writeFileSync(path.join(runDir, options.mode === 'corpus' ? 'log-corpus.json' : 'log-replay-plan.json'), JSON.stringify(evidence, null, 2));
    console.log(`LOG_EVIDENCE ${evidence.status ?? 'RECORDED'} (no commands or hardware actions executed)`);
    return evidence.status === 'FAIL' ? 2 : options.mode === 'replay-plan' ? 12 : evidence.status === 'INCOMPLETE' || evidence.status === 'BLOCKED' ? 10 : 0;
  }
  if (options.mode === 'signal-check') {
    if (!options.archive) throw new Error('SIGNAL_CHECK_REQUIRES_ARCHIVE');
    const [{ buildLogReplayPlan }, { replaySignals }] = await Promise.all([import('./log-corpus.mjs'), import('./signal-replay.mjs')]);
    const domains = options.domains.length ? options.domains : ['fan', 'input'];
    if (domains.some(domain => !['fan','input','performance'].includes(domain))) throw new Error('INVALID_SIGNAL_DOMAIN');
    const plan = await buildLogReplayPlan(path.resolve(options.archive), {pythonPath:options.python, domains});
    fs.writeFileSync(path.join(runDir,'log-replay-plan.json'), JSON.stringify(plan,null,2));
    const evidence = await replaySignals({repoRoot, plan, domains, workDir:path.join(runDir,'consumer-work')});
    evidence.runId=runId;
    fs.writeFileSync(path.join(runDir,'signal-result.json'), JSON.stringify(evidence,null,2));
    console.log(`SIGNAL_CONSUMPTION ${evidence.status}\nRESULT ${path.join(runDir,'signal-result.json')}`);
    return evidence.status==='PASS' ? 0 : evidence.status==='FAIL' || plan.status==='FAIL' ? 2 : 10;
  }
  const [{ buildCatalog }, { getAdapters }, sleep, ownership] = await Promise.all([import('./catalog.mjs'), import('./adapters.mjs'), import('./sleep.mjs'), import('./capability-lifecycle/ownership-catalog.mjs')]);
  if (options.mode === 'replay') {
    if (!options.logPath) throw new Error('REPLAY_REQUIRES_LOG_PATH');
    const replay = await sleep.replaySleepLog(path.resolve(options.logPath));
    fs.writeFileSync(path.join(runDir, 'sleep-replay.json'), JSON.stringify(replay, null, 2));
    console.log(`SLEEP_REPLAY ${replay.status ?? replay.verdict ?? 'RECORDED'} (observation only; no OS sleep)`);
    return replay.status === 'FAIL' || replay.ok === false ? 2 : replay.status === 'INCOMPLETE' || replay.status === 'NOT_COVERED' ? 10 : 0;
  }
  const {getExtraAdapters}=await import('./extra-adapters.mjs');
  const adapters = validateAdapters(configureCandidateAdapters([...getAdapters(repoRoot), ...sleep.getSleepAdapters(repoRoot), ...getExtraAdapters(repoRoot,{selectedIds:!options.capabilities&&options.tests.length&&['run','plan','audit'].includes(options.mode)?options.tests:undefined})], options), repoRoot);
  let logPlan = null;
  if (options.mode === 'log-check') {
    if (!options.archive) throw new Error('LOG_CHECK_REQUIRES_ARCHIVE');
    const { buildLogReplayPlan } = await import('./log-corpus.mjs');
    logPlan = await buildLogReplayPlan(path.resolve(options.archive), { pythonPath: options.python, domains: options.domains });
    if (options.monitorFieldLogInput) {
      const {readMonitorFieldLogFeedback} = await import('./monitor-notice-lifecycle/field-log-feedback.mjs');
      logPlan.monitorFieldLogFeedback = readMonitorFieldLogFeedback({inputPath: options.monitorFieldLogInput, sourceArchive: logPlan.sourceArchive});
      fs.writeFileSync(path.join(runDir, 'monitor-field-feedback.json'), JSON.stringify(logPlan.monitorFieldLogFeedback, null, 2));
      if (logPlan.monitorFieldLogFeedback.status === 'REJECTED') logPlan.status = 'FAIL';
    }
    fs.writeFileSync(path.join(runDir, 'log-replay-plan.json'), JSON.stringify(logPlan, null, 2));
    if (logPlan.status === 'FAIL' || logPlan.status === 'BLOCKED') {
      console.log(`LOG_CHECK ${logPlan.status}; no recipes executed`);
      return logPlan.status === 'FAIL' ? 2 : 10;
    }
    // A plan chooses only registered recipes; it cannot inject commands or arbitrary paths.
    options.tests = [...new Set(logPlan.recommendedAdapterIds ?? [])];
    options.profile = 'full';
    options.domains = [];
    if (options.tests.length === 0) {
      fs.writeFileSync(path.join(runDir, 'log-check.json'), JSON.stringify({ status: 'BLOCKED', reason: 'NO_SUPPORTED_REPLAY_RECIPE', eventsObserved: logPlan.events?.length ?? 0, executed: false }, null, 2));
      console.log('LOG_CHECK BLOCKED: no supported replay recipe');
      return 10;
    }
  }
  const unknownDomains = options.domains.filter(domain => !adapters.some(item => item.domain === domain));
  const unknownTests = options.tests.filter(id => !adapters.some(item => item.id === id));
  if (unknownDomains.length || unknownTests.length) throw new Error(`UNKNOWN_SELECTION:${[...unknownDomains,...unknownTests].join(',')}`);
  let capabilitySelection = null;
  if (options.capabilities) {
    try {
      capabilitySelection = selectCapabilities({repoRoot, adapters, capabilities: options.capabilities, mode: options.mode, profile: options.profile, domains: options.domains, tests: options.tests});
    } catch (error) {
      if (error.selection) fs.writeFileSync(path.join(runDir, 'capability-selection.json'), JSON.stringify(error.selection, null, 2));
      throw error;
    }
    fs.writeFileSync(path.join(runDir, 'capability-selection.json'), JSON.stringify(capabilitySelection, null, 2));
  }
  const selected = adapters.filter(item => (!capabilitySelection || capabilitySelection.selectedIds.includes(item.id)) && item.profiles.includes(options.profile) && (!options.domains.length || options.domains.includes(item.domain)) && (!options.tests.length || options.tests.includes(item.id)));
  const sourceCatalog = await buildCatalog({ repoRoot, adapters });
  const ownershipCatalog = ownership.getOwnershipInventory({ repoRoot, adapters });
  const catalog = { ...sourceCatalog, ownershipCatalog };
  fs.writeFileSync(path.join(runDir, 'catalog.json'), JSON.stringify(catalog, null, 2));
  const plan = selected.map(item => ({ ...item, runnableHere: item.kind !== 'powershell' || item.scope !== 'host-mock' || Boolean(options.payloadRoot) }));
  fs.writeFileSync(path.join(runDir, 'plan.json'), JSON.stringify({ schemaVersion: 1, profile: options.profile, adapters: plan, ownershipCatalog, ...(capabilitySelection ? {capabilitySelection} : {}), noTestsExecuted: true }, null, 2));
  const run = {
    schemaVersion: 1, tool: 'ymcc_sim-global', runId, mode: options.mode, profile: options.profile,
    startedAt: new Date().toISOString(), repoRoot, nodeVersion: process.version,
    plannedTests: selected.length, results: [], catalog, ownershipCatalog,
    ...(capabilitySelection ? {capabilitySelection} : {}),
    candidateInputs: Object.fromEntries(['payloadRoot','completeHostContract','runtimeRoot','sharedRuntimeRoot','lightingPayloadRoot','expectedHelperSha256','hcProfile','hcVersion','legacyHostRoot','fanToolLoadPlan','fanToolLoadPlanSha256'].filter(key => options[key] !== undefined).map(key => [key, options[key]])),
    hostPayload: options.payloadRoot ? { root: path.resolve(options.payloadRoot), binaries: ['YeManFanHost.exe','YeManFanHost.dll','YeManFanBackend.dll','Microsoft.Windows.SDK.NET.dll','WinRT.Runtime.dll'].map(name => { const file = path.join(options.payloadRoot, name); return { name, sha256: fs.existsSync(file) ? sha256File(file) : null }; }) } : null,
    safetyScope: { realSleepPerformed: false, hardwareWritesPerformed: false, installedApplicationMutated: false },
    coverageMeaning: 'source-entry-association-only-not-complete-behavioral-validation',
    fullProductAcceptance: false,
  };
  if (options.mode === 'run' || options.mode === 'log-check') {
    run.results = await runAdapters(selected, { repoRoot, runDir, powershell: options.powershell ?? 'pwsh.exe', payloadRoot: options.payloadRoot,
      onProgress: item => console.log(item.phase === 'start' ? `RUN ${item.id}` : `${item.status} ${item.id} (${item.durationMs} ms)`),
    });
    const selectedSurfaces = catalog.surfaces.filter(item => !options.domains.length || options.domains.includes(item.domain));
    const uncoveredCount = selectedSurfaces.filter(item => item.coverage === 'NOT_COVERED' || item.domain === 'unclassified').length;
    run.verdict = verdictFor({ results: run.results, selectedCount: selected.length, uncoveredCount, requireAll: options.mode !== 'log-check' && (options.requireAll || options.profile === 'full') });
    run.coverageGaps = uncoveredCount;
    run.lifecycleAcceptance = auditLifecycleAcceptance({repoRoot,adapters:selected,results:run.results,requireAll:options.requireAll||options.profile==='full'});
    fs.writeFileSync(path.join(runDir,'lifecycle-acceptance.json'),JSON.stringify(run.lifecycleAcceptance,null,2));
    run.verdict = applyLifecycleVerdict(run.verdict,run.lifecycleAcceptance,{requireAll:options.requireAll||options.profile==='full'});
    if (logPlan) {
      run.logDriven = { sourceArchive: logPlan.sourceArchive, planStatus: logPlan.status, observedEvents: logPlan.events?.length ?? 0, executedRecipeIds: selected.map(item => item.id), exactEventTimelineReplayed: false };
      if (logPlan.monitorFieldLogFeedback) {
        run.logDriven.monitorFieldLogFeedback = logPlan.monitorFieldLogFeedback;
        const {applyMonitorFieldFeedbackVerdict} = await import('./monitor-notice-lifecycle/field-log-feedback.mjs');
        run.verdict = applyMonitorFieldFeedbackVerdict(run.verdict, logPlan.monitorFieldLogFeedback);
      }
      if (run.verdict.exitCode === 0 && logPlan.status !== 'READY') run.verdict = { status: 'RECIPES_PASS_LOG_EVIDENCE_INCOMPLETE', exitCode: 10 };
    }
  } else run.verdict = { status: options.mode === 'plan' ? 'PLAN_ONLY_NOT_EXECUTED' : 'INVENTORY_ONLY_NOT_EXECUTED', exitCode: options.mode === 'plan' ? 12 : 0 };
  fs.writeFileSync(path.join(runDir, 'coverage-work-items.json'), JSON.stringify(makeCoverageWorkItems(catalog, run.results), null, 2));
  run.finishedAt = new Date().toISOString();
  fs.writeFileSync(path.join(runDir, 'result.json'), JSON.stringify(run, null, 2));
  writeReport(run, runDir);
  console.log(`VERDICT ${run.verdict.status}\nRESULT ${path.join(runDir, 'result.json')}`);
  return run.verdict.exitCode;
}
if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  main().then(code => { process.exitCode = code; }).catch(error => { console.error(`YMCC_SIM_ERROR ${error.message}`); process.exitCode = 3; });
}