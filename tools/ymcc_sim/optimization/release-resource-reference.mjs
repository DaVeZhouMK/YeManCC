import {verifyRawProducerCompletion} from '../global/decky-persistent-lifecycle/raw-owned-producer.mjs';
// Read-only inner verifier. The caller must first validate the complete parent/global proof.
// No producer, executable, compiler or product is launched by this module.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import * as cacheProducer from '../../decky_resource_cache_selftest.mjs';
import * as workerProducer from '../../decky_resource_worker_selftest.mjs';
import * as environmentHelpers from '../global/decky-persistent-lifecycle/baseline-environment.mjs';
import {selectedNativeTools, simulationCorePath} from '../global/release-host-environment.mjs';

const ENVIRONMENT_KEYS = environmentHelpers.RESOURCE_FIXTURE_ENVIRONMENT_KEYS;
const FILES = Object.freeze({result:'result.json',summary:'EXECUTION-SUMMARY.json',
  workspace:'WORKSPACE.json',freeze:'SOURCE-FREEZE.json',audit:'SOURCE-AUDIT.json',tools:'TOOL-GRAPH.json'});
const RUN_FILES = Object.freeze({execution:'EXECUTION.json',native:'native.json',paths:'PATHS.json',
  buildProcess:'build.PROCESS.json',runProcess:'run.PROCESS.json',dependencies:'dependencies.json'});
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const key = file => path.resolve(file).replace(/^\\\\\?\\/, '').toLowerCase();
const fail = (condition, message) => assert(condition, 'RESOURCE_REFERENCE_' + message);
const eq = (actual, expected, message) => assert.deepEqual(actual, expected, 'RESOURCE_REFERENCE_' + message);
const digest = value => typeof value === 'string' && /^[a-f0-9]{64}$/.test(value);
const pid = value => Number.isSafeInteger(value) && value > 0;
function relative(value) {
  fail(typeof value === 'string' && value.length > 0 && !path.isAbsolute(value) &&
    !value.includes(':') && !value.split(/[\\/]/).some(part => !part || part === '.' || part === '..'), 'SOURCE_PATH_INVALID');
  return value.replaceAll('\\', '/');
}
function within(parent, file) {
  const r = path.relative(parent, file);
  return r !== '' && r !== '..' && !r.startsWith('..' + path.sep) && !path.isAbsolute(r);
}
function samePath(actual, expected, message) {
  fail(typeof actual === 'string' && path.isAbsolute(actual), message);
  eq(key(actual), key(expected), message);
}
function rows(value, message) { fail(Array.isArray(value), message); return value; }
function unique(value, id, message) {
  const map = new Map();
  for (const row of rows(value, message)) { const name = id(row); fail(!map.has(name), message + '_DUPLICATE'); map.set(name, row); }
  return map;
}
function environment(value) {
  fail(value && typeof value === 'object' && !Array.isArray(value), 'ENVIRONMENT_MISSING');
  const found = new Map();
  for (const [name, item] of Object.entries(value)) {
    const upper = name.toUpperCase();
    fail(!found.has(upper) && ENVIRONMENT_KEYS.some(k => k.toUpperCase() === upper), 'ENVIRONMENT_SCOPE');
    fail(item === null || typeof item === 'string', 'ENVIRONMENT_VALUE'); found.set(upper, item);
  }
  fail(found.size === ENVIRONMENT_KEYS.length, 'ENVIRONMENT_INCOMPLETE');
  return Object.fromEntries(ENVIRONMENT_KEYS.map(name => [name, found.get(name.toUpperCase())]));
}
function finiteWindow(value, message) {
  const start = Date.parse(value?.startedAt), finish = Date.parse(value?.finishedAt);
  fail(Number.isFinite(start) && Number.isFinite(finish) && finish >= start &&
    Number.isFinite(value.elapsedMs) && value.elapsedMs >= 0, message);
  // Elapsed is monotonic accounting, while these are wall timestamps. Do not compare them.
  return {start, finish};
}
function budget(value) {
  eq(value.originalCompileBudgetMs, 60000, 'COMPILE_BUDGET');
  eq(value.originalNativeBudgetMs, 30000, 'NATIVE_BUDGET');
  eq(value.originalStopDeadlineMs, 3200, 'STOP_DEADLINE');
}
function resolveContract(family, supplied) {
  const title = family === 'cache' ? 'Cache' : 'Worker';
  const producer = family === 'cache' ? cacheProducer : workerProducer;
  const factory = producer['resource' + title + 'ReferenceContract'];
  fail(supplied || typeof factory === 'function', 'CONTRACT_EXPORT_MISSING');
  const contract = supplied ?? factory();
  eq(contract.family, family, 'CONTRACT_FAMILY');
  eq(contract.module, 'tools/decky_resource_' + family + '_selftest.mjs', 'CONTRACT_MODULE');
  eq(contract.exportName, 'resource' + title + 'Tests', 'CONTRACT_EXPORT');
  eq(contract.short, family === 'cache' ? 'Rc' : 'Rw', 'CONTRACT_WORKSPACE');
  fail(Number.isSafeInteger(contract.positiveCaseCount) && contract.positiveCaseCount > 0, 'CONTRACT_POSITIVE_COUNT');
  fail(typeof contract.step === 'string' && contract.step.length > 0, 'CONTRACT_STEP');
  const mutants = unique(contract.mutants, row => row.name, 'CONTRACT_MUTANTS');
  fail(mutants.size > 0, 'CONTRACT_MUTATION_COUNT');
  for (const mutant of mutants.values()) {
    fail(typeof mutant.name === 'string' && /^[a-z0-9-]+$/.test(mutant.name), 'CONTRACT_MUTATION_NAME');
    relative(mutant.path);
    fail(typeof mutant.from === 'string' && mutant.from.length > 0 && typeof mutant.to === 'string' &&
      mutant.from !== mutant.to && typeof mutant.failure === 'string' && mutant.failure.length > 0, 'CONTRACT_MUTATION');
    fail(Number.isSafeInteger(mutant.nativeCaseCount) && mutant.nativeCaseCount > 0, 'CONTRACT_NEGATIVE_COUNT');
    eq(mutant.selectedCase, family === 'cache' ? mutant.failure : null, 'CONTRACT_NEGATIVE_SCOPE');
  }
  return contract;
}

// Expand only the five source-bound cache loop forms. Unknown expressions fail closed;
// these labels describe the original native rows, never additional executions.
function cacheParameterizedNames(nativeSource) {
  const sites = [...nativeSource.matchAll(/\bcheck\s*\(\s*\(/g)];
  if (!sites.length) return [];
  const families = [
    ['ownerStorm','storm staging owner ',' preserves unknown provenance','pairs'],
    ['slotStorm','storm slot ',' stays unowned and cannot repair','strings'],
    ['receiptStorm','storm managed receipt ',' cannot borrow cache authority','strings'],
    [null,'storm IO ',' explicit retry publishes verified cache','inline'],
    [null,'storm occupied read ',' preserves bytes and explicit retry heals','inline'],
  ];
  eq(sites.length, families.length, 'SOURCE_PARAMETER_FAMILY_COUNT');
  const found = new Set(), names = [];
  for (const site of sites) {
    const call = nativeSource.slice(site.index).match(/^check\s*\(\s*\(\s*("(?:[^"\\]|\\.)*")\s*\+\s*kind\s*\+\s*("(?:[^"\\]|\\.)*")\s*\)\s*\.c_str\s*\(\s*\)\s*,/);
    fail(call, 'SOURCE_PARAMETER_EXPRESSION');
    const prefix = JSON.parse(call[1]), suffix = JSON.parse(call[2]);
    const index = families.findIndex(row => row[1] === prefix && row[2] === suffix);
    fail(index >= 0 && !found.has(index), 'SOURCE_PARAMETER_FAMILY'); found.add(index);
    const [variable,,,kind] = families[index], before = nativeSource.slice(0, site.index);
    let values;
    if (kind === 'inline') {
      const loop = before.match(/for\s*\(\s*const\s+std::string\s+kind\s*:\s*\{([^{}]*)\}\s*\)\s*$/);
      fail(loop, 'SOURCE_PARAMETER_LOOP'); values = stringList(loop[1]);
    } else {
      const loop = kind === 'pairs' ? /for\s*\(\s*const\s+auto\s*&\s*\[\s*kind\s*,\s*bytes\s*\]\s*:\s*ownerStorm\s*\)\s*$/ :
        new RegExp('for\\s*\\(\\s*const\\s+auto\\s*&\\s*kind\\s*:\\s*' + variable + '\\s*\\)\\s*$');
      fail(loop.test(before), 'SOURCE_PARAMETER_LOOP');
      const type = kind === 'pairs' ? 'std::pair\\s*<\\s*std::string\\s*,\\s*std::string\\s*>' : 'std::string';
      const declarations = [...before.matchAll(new RegExp('const\\s+std::vector\\s*<\\s*' + type + '\\s*>\\s+' + variable + '\\s*=\\s*\\{([\\s\\S]*?)\\n?\\s*\\};', 'g'))];
      eq(declarations.length, 1, 'SOURCE_PARAMETER_DECLARATION');
      const body = declarations[0][1];
      values = kind === 'pairs' ? [...body.matchAll(/(?:^|\}\s*,)\s*\{\s*("(?:[^"\\]|\\.)*")\s*,/g)].map(m => JSON.parse(m[1])) : stringList(body);
    }
    fail(values.length > 0 && new Set(values).size === values.length, 'SOURCE_PARAMETER_VALUES');
    names.push(...values.map(value => prefix + value + suffix));
  }
  eq(found.size, families.length, 'SOURCE_PARAMETER_FAMILY_COUNT');
  return names;
}
function stringList(body) {
  fail(/^\s*"(?:[^"\\]|\\.)*"(?:\s*,\s*"(?:[^"\\]|\\.)*")*\s*$/.test(body), 'SOURCE_PARAMETER_VALUES');
  return [...body.matchAll(/"(?:[^"\\]|\\.)*"/g)].map(m => JSON.parse(m[0]));
}


function createResourceProofReader(){
  const fileCache = new Map(), plainCache = new Set();
  function plain(file, kind) {
    fail(typeof file === 'string' && path.isAbsolute(file) && !file.split(/[\\/]/).some(p => p === '.' || p === '..'), 'ABSOLUTE_PLAIN_PATH_REQUIRED');
    const resolved = path.resolve(file), cacheKey = key(resolved);
    if (!plainCache.has(cacheKey)) {
      let part = resolved;
      for (;;) {
        const stat = fs.lstatSync(part);
        fail(!stat.isSymbolicLink(), 'REPARSE_PATH:' + part);
        eq(key(fs.realpathSync.native(part)), key(part), 'REALPATH_CHANGED:' + part);
        const parent = path.dirname(part); if (parent === part) break; part = parent;
      }
      plainCache.add(cacheKey);
    }
    const stat = fs.lstatSync(resolved);
    fail(kind === 'directory' ? stat.isDirectory() : stat.isFile(), 'PATH_KIND:' + file);
    return resolved;
  }
  function bytes(file) {
    plain(file, 'file');
    if (!fileCache.has(key(file))) fileCache.set(key(file), fs.readFileSync(file));
    return fileCache.get(key(file));
  }
  const read = file => JSON.parse(bytes(file).toString('utf8').replace(/^\uFEFF/, ''));
  const pin = file => ({path:path.resolve(file), sha256:sha(bytes(file))});
  function verifyPin(value, expectedFile) {
    fail(value && typeof value === 'object' && digest(value.sha256), 'PIN_REQUIRED');
    if (expectedFile !== undefined) samePath(value.path, expectedFile, 'PIN_PATH');
    eq(sha(bytes(value.path)), value.sha256, 'PIN_HASH:' + value.path);
    if (value.bytes !== undefined) eq(bytes(value.path).length, value.bytes, 'PIN_SIZE');
    return value;
  }
  return {plain,bytes,read,pin,verifyPin};
}

// A child producer is accepted only with the same owned RawProgram completion proof.
export function verifyOwnedResourceProducer({reference,family,repoRoot,out,caller,window,sourceInputs,toolArtifacts,mutations=true}){
 const {read,verifyPin}=createResourceProofReader();
 fail(reference?.schemaVersion===1&&reference.mode===(mutations?'complete-original-cli':'positive-only-cost-measurement'),'OWNED_MODE');
 fail(['cache','worker'].includes(family),'OWNED_FAMILY');
 verifyPin(reference.process,path.join(path.dirname(out),'resource-'+family+'-process.json'));
 const proof=read(reference.process.path),raw=proof.rawExecution,ownedRoot=path.join(path.dirname(out),'owned-resource-'+family);
 eq(proof.id,'resource-'+family,'OWNED_ID');eq(proof.exitCode,0,'OWNED_EXIT');eq(proof.error,null,'OWNED_ERROR');eq(proof.signal,null,'OWNED_SIGNAL');eq(proof.timedOut,false,'OWNED_TIMEOUT');eq(proof.outputTruncated,false,'OWNED_OUTPUT_LIMIT');eq(proof.originalBudgetMs,240000,'OWNED_ORIGINAL_BUDGET');
 fail(Number.isFinite(proof.timeoutMs)&&proof.timeoutMs>0&&proof.timeoutMs<=240000,'OWNED_CLIPPED_BUDGET');
 const childWindow=finiteWindow(proof,'OWNED_WINDOW');fail(childWindow.start>=window.start&&childWindow.finish<=window.finish,'OWNED_OUTSIDE_CALLER');
 verifyPin(raw?.terminal,path.join(ownedRoot,'terminal.json'));verifyPin(raw.request,path.join(ownedRoot,'request.json'));verifyPin(raw.arguments,path.join(ownedRoot,'arguments.json'));
 const terminal=read(raw.terminal.path),request=read(raw.request.path),argv=read(raw.arguments.path);
 eq(terminal.kind,'ymcc-raw-program-terminal-v1','OWNED_TERMINAL_KIND');eq(raw.collectionComplete,true,'OWNED_COLLECTION');eq(terminal.collectionComplete,true,'OWNED_TERMINAL_COLLECTION');eq(terminal.declaredPinsUnchanged,true,'OWNED_INPUT_DRIFT');eq(terminal.logsClosedAndFlushed,true,'OWNED_LOGS');
 samePath(terminal.request.path,raw.request.path,'OWNED_REQUEST_PIN');eq(terminal.request.sha256,raw.request.sha256,'OWNED_REQUEST_HASH');samePath(terminal.argumentsPin.path,raw.arguments.path,'OWNED_ARGUMENT_PIN');eq(terminal.argumentsPin.sha256,raw.arguments.sha256,'OWNED_ARGUMENT_HASH');eq(request.arguments,raw.arguments,'OWNED_REQUEST_ARGUMENTS');
 eq(terminal.businessExitCode,0,'OWNED_TERMINAL_EXIT');eq(terminal.timedOut,false,'OWNED_TERMINAL_TIMEOUT');eq(terminal.outputLimitExceeded,false,'OWNED_TERMINAL_OUTPUT');
 eq(terminal.business.atomicJobAdmission,true,'OWNED_ATOMIC_JOB');eq(terminal.owner.pid,caller.pid,'OWNED_CALLER_PID');fail(/^\d{17,20}$/.test(terminal.owner.expectedStartTicks),'OWNED_CALLER_BIRTH');
 const expectedArgv=[path.join(repoRoot,'tools/decky_resource_'+family+'_selftest.mjs'),out,...(mutations?[]:['--positive-only'])];eq(argv,expectedArgv,'OWNED_EXACT_ARGUMENTS');eq(proof.arguments,expectedArgv,'OWNED_PROOF_ARGUMENTS');eq(terminal.business.arguments,expectedArgv,'OWNED_TERMINAL_ARGUMENTS');samePath(proof.cwd,repoRoot,'OWNED_CWD');samePath(terminal.business.cwd,repoRoot,'OWNED_TERMINAL_CWD');samePath(request.cwd,repoRoot,'OWNED_REQUEST_CWD');
 fail(pid(proof.pid)&&proof.pid===terminal.business.pid,'OWNED_BUSINESS_PID');eq(raw.actualBusiness,terminal.business,'OWNED_BUSINESS_OBJECT');eq(terminal.business.program,proof.program,'OWNED_PROGRAM_OBJECT');verifyPin(proof.program,caller.node.path);eq(proof.program.sha256,caller.node.sha256,'OWNED_NODE');
 verifyPin(raw.helperSource,path.join(repoRoot,'tools/ymcc_sim/global/host-process.ps1'));samePath(terminal.launcher.helper.path,raw.helperSource.path,'OWNED_HELPER');eq(terminal.launcher.helper.sha256,raw.helperSource.sha256,'OWNED_HELPER_HASH');verifyPin(terminal.launcher.program);eq(terminal.launcher.pid,raw.helperPid,'OWNED_LAUNCHER_PID');
 eq(terminal.job,raw.job,'OWNED_JOB_OBJECT');eq(terminal.job.disposed,true,'OWNED_JOB_DISPOSED');eq(terminal.job.cleanupError,null,'OWNED_JOB_CLEANUP');eq(terminal.job.activeProcessesFinal,0,'OWNED_JOB_ZERO');eq(terminal.job.zeroObservedBeforeDispose,true,'OWNED_ZERO_OBSERVED');
 fail(terminal.finishedEpochMs<=terminal.deadlineEpochMs,'OWNED_DEADLINE');
 const tools=new Map(toolArtifacts.map(row=>[key(row.path),row.sha256]));
 for(const row of request.tools){eq(tools.get(key(row.path)),row.sha256,'OWNED_TOOL_AUTHORITY:'+row.path);}
 fail(request.tools.some(row=>key(row.path)===key(terminal.launcher.program.path)&&row.sha256===terminal.launcher.program.sha256),'OWNED_LAUNCHER_AUTHORITY');
 const expectedSources=[],sourceNames=new Map();for(const row of sourceInputs){const file=path.resolve(repoRoot,row.path),name=key(file);if(sourceNames.has(name)){eq(sourceNames.get(name),row.sha256,'OWNED_CONFLICTING_SOURCE_DECLARATION');continue;}sourceNames.set(name,row.sha256);expectedSources.push({path:file,sha256:row.sha256});}eq(request.sources,expectedSources,'OWNED_SOURCE_MEMBERSHIP');
 const declared=[...request.sources.map(row=>['sources',key(row.path),row.sha256]),...request.tools.map(row=>['tools',key(row.path),row.sha256])];
 eq(terminal.declaredInputsBefore.map(row=>[row.role,key(row.path),row.sha256]),declared,'OWNED_DECLARED_BEFORE');eq(terminal.declaredInputsAfter.map(row=>[key(row.path),row.sha256]),declared.map(([,file,hash])=>[file,hash]),'OWNED_DECLARED_AFTER');
 const completion=verifyRawProducerCompletion(terminal,request.toolCleanupPolicy??null,request.tools);eq(raw.completion,completion,'OWNED_COMPLETION');
 for(const stream of ['stdout','stderr']){verifyPin(terminal.logs[stream],path.join(ownedRoot,stream+'.raw'));verifyPin(proof[stream],path.join(path.dirname(out),'resource-'+family+'.'+stream+'.bin'));eq(proof[stream].sha256,terminal.logs[stream].sha256,'OWNED_STREAM_IDENTITY');}
 fail(/^\d{17,20}$/.test(terminal.business.startFileTimeUtc)&&/^\d{17,20}$/.test(terminal.businessExitFileTimeUtc),'OWNED_NATIVE_CLOCK');
 const nativeStart=Number((BigInt(terminal.business.startFileTimeUtc)-116444736000000000n)/10000n),nativeFinish=Number((BigInt(terminal.businessExitFileTimeUtc)-116444736000000000n)/10000n);
 eq(nativeStart,childWindow.start,'OWNED_NATIVE_BIRTH');fail(Number.isSafeInteger(nativeFinish)&&nativeFinish>=nativeStart&&nativeFinish<=childWindow.finish,'OWNED_NATIVE_EXIT');
 return {pid:proof.pid,process:reference.process,completion,window:{start:nativeStart,finish:nativeFinish},collectionWindow:childWindow};
}

function verifyResourceReference({family, repoRoot, coreRoot = simulationCorePath(),
  baselineResult, baselineValue, originalProcess, baselineProcessFile, sourceInputs, toolArtifacts,
  expectedEnvironment, nativeTools, contract: suppliedContract} = {}) {
  fail(family === 'cache' || family === 'worker', 'UNKNOWN_FAMILY');
  const contract = resolveContract(family, suppliedContract);
  if (expectedEnvironment === undefined) {
    fail(typeof environmentHelpers.resourceFixtureEnvironment === 'function', 'ENVIRONMENT_EXPORT_MISSING');
    expectedEnvironment = environmentHelpers.resourceFixtureEnvironment();
  }
  fail(Array.isArray(ENVIRONMENT_KEYS) && ENVIRONMENT_KEYS.length > 0, 'ENVIRONMENT_KEYS_MISSING');
  const expected = environment(expectedEnvironment);
  nativeTools ??= selectedNativeTools().native;
  const repo = path.resolve(repoRoot), core = path.resolve(coreRoot);
  const {plain,bytes,read,pin,verifyPin}=createResourceProofReader();
  plain(repo, 'directory'); plain(core, 'directory');
  fail(!within(repo, core) && !within(core, repo) && key(repo) !== key(core), 'CORE_SCOPE');
  const baselineFile = plain(baselineResult, 'file'), base = path.dirname(baselineFile);
  fail(within(path.join(core, 'Evidence'), baselineFile), 'BASELINE_OUTSIDE_CORE');
  eq(read(baselineFile), baselineValue, 'BASELINE_OBJECT_CHANGED');
  const entry = baselineValue?.resourceExecutions?.[family];
  fail(entry && entry.schemaVersion === 1, 'ENVELOPE_MISSING');
  eq(entry.family, family, 'ENVELOPE_FAMILY'); eq(entry.exportName, contract.exportName, 'ENVELOPE_EXPORT');
  eq(entry.options, {mutations:true}, 'OPTIONS');
  const out = path.join(base, 'resource-' + family);
  const originals = {};
  eq(Object.keys(entry.files ?? {}).sort(), Object.keys(FILES).sort(), 'FILES_SCOPE');
  for (const [name, file] of Object.entries(FILES)) {
    verifyPin(entry.files[name], path.join(out, file)); originals[name] = read(entry.files[name].path);
  }
  const result = originals.result, summary = originals.summary, workspace = originals.workspace;
  eq(result, baselineValue[family === 'cache' ? 'resourceCache' : 'resourceWorker'], 'RESULT_NOT_ORIGINAL');
  eq(result.pass, true, 'RESULT_NOT_PASS'); eq(result.sourceChanged, false, 'SOURCE_CHANGED'); budget(result);
  for (const [name, value] of Object.entries({officialLoaderExecuted:false,realSteamActions:0,installedWrites:0,physicalSleep:false}))
    eq(result[name], value, 'RESULT_SCOPE:' + name);
  const parentFile = plain(baselineProcessFile, 'file');
  fail(within(path.join(core, 'Evidence'), parentFile), 'PARENT_PROCESS_OUTSIDE_CORE');
  eq(read(parentFile), originalProcess, 'PARENT_PROCESS_OBJECT_CHANGED');
  fail(pid(originalProcess?.pid) && pid(entry.caller?.pid), 'PARENT_PID_MISSING');
  eq(entry.caller.pid, originalProcess.pid, 'PARENT_PID_CHANGED');
  eq(originalProcess.exitCode, 0, 'PARENT_EXIT'); eq(originalProcess.error, null, 'PARENT_ERROR');
  eq(originalProcess.signal, null, 'PARENT_SIGNAL'); eq(originalProcess.timedOut, false, 'PARENT_TIMEOUT');
  eq(originalProcess.outputTruncated, false, 'PARENT_OUTPUT_TRUNCATED');
  eq(originalProcess.originalBudgetMs, 240000, 'PARENT_BUDGET'); eq(originalProcess.timeoutMs, 240000, 'PARENT_TIMEOUT_BUDGET');
  if (originalProcess.natural !== undefined) eq(originalProcess.natural, true, 'PARENT_NOT_NATURAL');
  const parentWindow = finiteWindow(originalProcess, 'PARENT_WINDOW'), window = finiteWindow(entry, 'PRODUCER_WINDOW');
  fail(window.start >= parentWindow.start && window.finish <= parentWindow.finish, 'PRODUCER_OUTSIDE_PARENT');
  fail(Array.isArray(originalProcess.arguments) && originalProcess.arguments.length > 0 &&
    originalProcess.arguments.every(a => typeof a === 'string') && !originalProcess.arguments.includes('--positive-only'), 'PARENT_ARGUMENTS');
  eq(entry.caller.arguments, originalProcess.arguments, 'CALLER_ARGUMENTS');
  samePath(entry.caller.cwd, originalProcess.cwd, 'CALLER_CWD'); samePath(entry.caller.cwd, repo, 'CALLER_REPOSITORY');
  verifyPin(originalProcess.program); verifyPin(entry.caller.node, originalProcess.program.path);
  eq(entry.caller.node.sha256, originalProcess.program.sha256, 'CALLER_NODE');
  eq(environment(entry.environment), expected, 'ENVIRONMENT_CHANGED');
  const processStem = path.basename(parentFile, '.json');
  verifyPin(originalProcess.stdout, path.join(path.dirname(parentFile), processStem + '.stdout.bin'));
  verifyPin(originalProcess.stderr, path.join(path.dirname(parentFile), processStem + '.stderr.bin'));

  const sources = new Map();
  const inputRows = sourceInputs instanceof Map ? [...sourceInputs].map(([p, v]) => ({path:p,sha256:typeof v === 'string' ? v : v?.sha256})) : sourceInputs;
  for (const row of rows(inputRows, 'SOURCE_SCOPE_MISSING')) {
    const name = relative(row.path); fail(digest(row.sha256) && !sources.has(name.toLowerCase()), 'SOURCE_SCOPE_INVALID');
    sources.set(name.toLowerCase(), row.sha256);
  }
  function source(name, expectedHash) {
    name = relative(name);
    fail(sources.has(name.toLowerCase()), 'SOURCE_UNDECLARED:' + name);
    eq(sources.get(name.toLowerCase()), expectedHash, 'SOURCE_NOT_PARENT_BOUND:' + name);
    eq(sha(bytes(path.join(repo, name))), expectedHash, 'SOURCE_HASH:' + name);
  }
  fail(within(repo, originalProcess.arguments[0]), 'PARENT_ENTRY_SCOPE');
  source(path.relative(repo, originalProcess.arguments[0]), sha(bytes(originalProcess.arguments[0])));
  verifyPin(entry.module, path.join(repo, contract.module)); source(contract.module, entry.module.sha256);
  eq(entry.moduleSha256After, entry.module.sha256, 'MODULE_CHANGED');
  const moduleText = bytes(entry.module.path).toString('utf8');
  const scopeLiteral = moduleText.match(/"scope"\s*:\s*("(?:[^"\\]|\\.)*")/);
  fail(scopeLiteral, 'PRODUCER_SCOPE_MISSING'); eq(result.scope, JSON.parse(scopeLiteral[1]), 'PRODUCER_SCOPE_CHANGED');

  const toolScope = unique(toolArtifacts, row => key(row.path), 'TOOL_SCOPE_MISSING');
  for (const row of toolScope.values()) fail(digest(row.sha256), 'TOOL_SCOPE_DIGEST');
  function tool(row, actual = true) {
    fail(row && digest(row.sha256) && typeof row.path === 'string' && path.isAbsolute(row.path), 'TOOL_PIN_REQUIRED');
    eq(toolScope.get(key(row.path))?.sha256, row.sha256, 'TOOL_UNDECLARED:' + row.path);
    if (actual) verifyPin(row);
    return row;
  }
  tool(entry.caller.node);
  const ownedMode=baselineValue.resourceScheduling?.mode==='owned-parallel'||baselineValue.resourceScheduling?.mode==='owned-sequential';
  eq(Boolean(entry.ownedProducer),ownedMode,'OWNED_PRODUCER_REQUIRED_BY_SCHEDULE');
  const actualProducer=ownedMode?verifyOwnedResourceProducer({reference:entry.ownedProducer,family,repoRoot:repo,out,caller:entry.caller,window,sourceInputs:baselineValue.sourceInputs,toolArtifacts:[...toolScope.values()]}):{pid:originalProcess.pid};
  const producerWindow=ownedMode?actualProducer.window:window;
  const workParent = path.join(core, 'Workspace', contract.short), work = workspace.work;
  samePath(path.dirname(work), workParent, 'WORK_PARENT');
  fail(/^r[A-Za-z0-9]{6}$/.test(path.basename(work)), 'WORK_ROOT'); plain(work, 'directory');
  samePath(result.workRoot, work, 'RESULT_WORK'); samePath(result.callerEvidenceDir, out, 'RESULT_EVIDENCE');
  eq(workspace.schemaVersion, 1, 'WORKSPACE_SCHEMA'); samePath(workspace.callerEvidenceDir, out, 'WORKSPACE_EVIDENCE');
  samePath(workspace.sourceRoot, path.join(work, 's'), 'WORKSPACE_SOURCE');
  verifyPin(workspace.workspaceOwner, path.join(work, 'OWNER.json'));
  const owner = read(workspace.workspaceOwner.path);
  eq(owner.schemaVersion, 1, 'OWNER_SCHEMA');
  fail(typeof owner.nonce === 'string' && /^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/i.test(owner.nonce), 'OWNER_NONCE');
  eq(owner.processId, actualProducer.pid, 'OWNER_PID'); eq(owner.module, contract.module, 'OWNER_MODULE');
  samePath(owner.work, work, 'OWNER_WORK'); samePath(owner.parent, workParent, 'OWNER_PARENT');
  samePath(owner.callerEvidenceDir, out, 'OWNER_EVIDENCE');
  fail(Date.parse(owner.createdAt) >= producerWindow.start && Date.parse(owner.createdAt) <= producerWindow.finish, 'OWNER_WINDOW');
  verifyPin(workspace.node, entry.caller.node.path); eq(workspace.node.sha256, entry.caller.node.sha256, 'WORKSPACE_NODE');
  const freeze = originals.freeze, audit = originals.audit, graph = originals.tools;
  eq(freeze.schemaVersion, 1, 'FREEZE_SCHEMA'); samePath(freeze.root, repo, 'FREEZE_REPOSITORY');
  samePath(freeze.frozen, workspace.sourceRoot, 'FREEZE_ROOT');
  const frozen = unique(freeze.files, row => relative(row.path), 'FROZEN_SOURCES'); fail(frozen.size > 0, 'FROZEN_SOURCES_EMPTY');
  eq(result.sourceHashes, freeze.files, 'RESULT_SOURCES');
  eq(audit.schemaVersion, 1, 'AUDIT_SCHEMA'); eq(audit.sourceChanged, false, 'AUDIT_CHANGED');
  const audits = unique(audit.rows, row => relative(row.path), 'AUDIT_SOURCES'); eq([...audits.keys()].sort(), [...frozen.keys()].sort(), 'AUDIT_SCOPE');
  for (const [name, row] of frozen) {
    source(name, row.sha256); samePath(row.captured, path.join(work, 's', name), 'FROZEN_PATH');
    eq(sha(bytes(row.captured)), row.sha256, 'FROZEN_HASH');
    const after = audits.get(name); eq(after.unchanged, true, 'AUDIT_UNCHANGED');
    eq(after.beforeSha256, row.sha256, 'AUDIT_BEFORE'); eq(after.afterSha256, row.sha256, 'AUDIT_AFTER');
    verifyPin(after.captured, row.captured); eq(after.captured.sha256, row.sha256, 'AUDIT_CAPTURE');
  }
  fail(frozen.has(contract.module), 'MODULE_NOT_FROZEN');
  const cpp = 'tools/decky_resource_' + family + '_selftest.cpp'; fail(frozen.has(cpp), 'CPP_NOT_FROZEN');
  const nativeSource = bytes(frozen.get(cpp).captured).toString('utf8');
  // Literal, parameterized and auxiliary names all come from the bound C++ input.
  const casePattern = /\bcheck\s*\(\s*("(?:[^"\\]|\\.)*")|cases\.push_back\s*\(\s*\{\s*\{\s*"name"\s*,\s*("(?:[^"\\]|\\.)*")/g;
  const caseDeclarations = [...nativeSource.matchAll(casePattern)];
  const caseNames = caseDeclarations.map(m => JSON.parse(m[1] ?? m[2]));
  const primaryNames = caseDeclarations.filter(m => m[1] !== undefined).map(m => JSON.parse(m[1]));
  const parameterizedNames = family === 'cache' ? cacheParameterizedNames(nativeSource) : [];
  caseNames.push(...parameterizedNames); primaryNames.push(...parameterizedNames);
  eq(new Set(caseNames).size, contract.positiveCaseCount, 'SOURCE_CASE_SCOPE'); eq(caseNames.length, contract.positiveCaseCount, 'SOURCE_CASE_COUNT');
  eq(graph.schemaVersion, 1, 'TOOL_GRAPH_SCHEMA'); eq(graph.selected, nativeTools, 'SELECTED_TOOLS');
  verifyPin(graph.node, entry.caller.node.path); eq(graph.node.sha256, entry.caller.node.sha256, 'TOOL_NODE');
  const manifestName = 'tools/ymcc_sim/global/decky-update-recovery/native-launch-noio/native-tools.json';
  verifyPin(graph.originalManifest, path.join(repo, manifestName)); source(manifestName, graph.originalManifest.sha256);
  const declarations = new Map();
  for (const list of ['declaredTools','additionalFixedDeclaration','resourceFixedDeclaration']) {
    const declared = unique(graph[list], row => key(row.path), 'TOOL_DECLARATION');
    fail(declared.size > 0, 'TOOL_DECLARATION_EMPTY');
    for (const [name, row] of declared) {
      // These finite declarations are already hashed by the enclosing parent. Only actual inputs are read again.
      tool(row, false); if (declarations.has(name)) eq(declarations.get(name).sha256, row.sha256, 'TOOL_DECLARATION_CONFLICT');
      declarations.set(name, row);
    }
  }
  const originalTools = new Map(graph.declaredTools.map(row => [key(row.path), row]));
  for (const field of ['compiler','linker','dumpbin','commandProcessor']) {
    const row = originalTools.get(key(nativeTools[field])); fail(row, 'SELECTED_TOOL_UNDECLARED:' + field); tool(row);
  }
  const toolsAfter = unique(graph.toolsAfter, row => key(row.path), 'TOOLS_AFTER');
  eq([...toolsAfter.keys()].sort(), [...originalTools.keys()].sort(), 'TOOLS_AFTER_SCOPE');
  for (const [name, row] of toolsAfter) {
    eq(row.expectedSha256, originalTools.get(name).sha256, 'TOOL_BEFORE'); eq(row.actualSha256, row.expectedSha256, 'TOOL_AFTER');
  }
  const observed = unique(graph.actualInputs, row => key(row.path), 'ACTUAL_TOOLS');
  const observedAfter = unique(graph.actualToolAfter, row => key(row.path), 'ACTUAL_TOOLS_AFTER');
  eq([...observedAfter.keys()].sort(), [...observed.keys()].sort(), 'ACTUAL_TOOL_AFTER_SCOPE');
  for (const [name, row] of observed) {
    tool(row); eq(row.sha256, declarations.get(name)?.sha256, 'ACTUAL_TOOL_UNDECLARED');
    eq(row.originalDeclaration, originalTools.has(name), 'TOOL_DECLARATION_ORIGIN');
    eq(observedAfter.get(name), {...row,afterSha256:row.sha256}, 'ACTUAL_TOOL_CHANGED');
  }
  eq(graph.observedOutsideOriginalDeclaration, [...observed.values()].filter(row => !row.originalDeclaration), 'ADDITIONAL_TOOL_SCOPE');
  const officialName = 'PowerControl/decky/PluginLoader_noconsole.exe';
  verifyPin(graph.officialData, path.join(repo, officialName));
  source(officialName, graph.officialData.sha256); // Read-only payload authority, never compiler/tool authority.
  const lockName = 'decky-plugin/vendor/decky-loader.lock.json'; fail(frozen.has(lockName), 'OFFICIAL_LOCK_MISSING');
  eq(read(frozen.get(lockName).captured).asset?.sha256, graph.officialData.sha256, 'OFFICIAL_LOCK');
  eq(result.officialDataSha256, graph.officialData.sha256, 'OFFICIAL_DATA');

  const runs = unique(entry.runs, row => row.name, 'RUNS');
  const expectedRuns = ['positive', ...contract.mutants.map(m => 'negative-' + m.name)];
  eq([...runs.keys()].sort(), [...expectedRuns].sort(), 'RUN_SCOPE');
  const mutations = unique(entry.mutations, row => row.name, 'MUTATIONS');
  eq([...mutations.keys()].sort(), contract.mutants.map(m => m.name).sort(), 'MUTATION_SCOPE');
  const negatives = rows(result.negativeMutations, 'NEGATIVES');
  eq(negatives.map(row => row.name), contract.mutants.map(row => row.name), 'NEGATIVE_SCOPE');
  eq(summary.negativeMutations, negatives, 'SUMMARY_NEGATIVES');
  samePath(summary.work, work, 'SUMMARY_WORK'); eq(summary.executionError, null, 'SUMMARY_EXECUTION_ERROR'); eq(summary.sourceChanges, [], 'SUMMARY_SOURCE_CHANGED');
  const usedTools = new Set(), verifiedRuns = [], failurePins = [];
  function validateSources(value, sourceRoot, mutant) {
    const sourceRows = unique(value, row => relative(row.relative), 'RUN_SOURCE_ROWS');
    eq([...sourceRows.keys()].sort(), [...frozen.keys()].sort(), 'RUN_SOURCE_SCOPE');
    for (const [name, row] of sourceRows) {
      samePath(row.path, path.join(sourceRoot, name), 'RUN_SOURCE_PATH'); verifyPin(row);
      const original = bytes(frozen.get(name).captured);
      const changed = mutant && name === mutant.path ? Buffer.from(original.toString('utf8').replace(mutant.from, mutant.to)) : original;
      if (mutant && name === mutant.path) fail(original.toString('utf8').includes(mutant.from), 'MUTATION_ANCHOR');
      eq(row.sha256, sha(changed), 'MUTATION_SOURCE_HASH');
    }
    return sourceRows;
  }
  function processReceipt(value, kind, expectedProgram, args, dir, temp, evidenceDir, exitCode) {
    eq(value.kind, kind, 'PROCESS_KIND'); verifyPin(value.program, expectedProgram);
    eq(value.args, args, 'PROCESS_ARGUMENTS'); samePath(value.cwd, dir, 'PROCESS_CWD');
    fail(pid(value.pid), 'PROCESS_PID_MISSING'); eq(value.budgetMs, kind === 'build' ? 60000 : 30000, 'PROCESS_BUDGET');
    eq(value.status, exitCode, 'PROCESS_EXIT'); eq(value.natural, true, 'PROCESS_NOT_NATURAL');
    eq(value.signal, null, 'PROCESS_SIGNAL'); eq(value.error, null, 'PROCESS_ERROR');
    const env = {...expected, PATH:path.dirname(nativeTools.compiler) + ';' + expected.SystemRoot + '/System32',
      INCLUDE:nativeTools.include.join(';'),LIB:nativeTools.lib.join(';'),CL:'',_CL_:'',LINK:'',_LINK_:'',VSLANG:'1033',TEMP:temp,TMP:temp};
    eq(environment(value.environment), env, 'PROCESS_ENVIRONMENT');
    verifyPin(value.stdout, path.join(evidenceDir, kind + '.stdout.log')); verifyPin(value.stderr, path.join(evidenceDir, kind + '.stderr.log'));
    eq(bytes(path.join(evidenceDir, kind + '.log')), Buffer.concat([bytes(value.stdout.path),bytes(value.stderr.path)]), 'COMBINED_LOG_CHANGED');
    const start = Date.parse(value.startedAt);
    fail(Number.isFinite(start) && Number.isFinite(value.elapsedMs) && value.elapsedMs >= 0 &&
      start >= producerWindow.start && start <= producerWindow.finish, 'PROCESS_WINDOW');
    // Original native receipts have no finishedAt. Never invent a wall finish from monotonic elapsed.
    if (value.finishedAt !== undefined) {
      const finish = Date.parse(value.finishedAt);
      fail(Number.isFinite(finish) && finish >= start && finish <= producerWindow.finish, 'PROCESS_FINISHED_AT');
    }
  }
  for (const [index, name] of expectedRuns.entries()) {
    const mutant = index === 0 ? null : contract.mutants[index - 1], record = runs.get(name), evidenceDir = path.join(out, name);
    eq(Object.keys(record).sort(), ['name',...Object.keys(RUN_FILES)].sort(), 'RUN_FILES_SCOPE');
    for (const [field, file] of Object.entries(RUN_FILES)) verifyPin(record[field], path.join(evidenceDir, file));
    const execution = read(record.execution.path), native = read(record.native.path), paths = read(record.paths.path);
    eq(execution.name, name, 'EXECUTION_NAME'); budget(execution);
    eq(bytes(path.join(out, name + '.json')), bytes(record.execution.path), 'EXECUTION_COPY_CHANGED');
    const dir = path.join(work, index === 0 ? 'p' : 'n' + (index - 1)), temp = path.join(dir, 't');
    const sourceRoot = path.join(work, index === 0 ? 's' : 'v' + (index - 1));
    const executable = path.join(dir, 'resource-cache.exe'), nativeFile = path.join(dir, 'native.json');
    const fixture = path.join(dir, family === 'cache' ? 'resource-fixture' : 'worker-fixture');
    for (const directory of [dir,temp,sourceRoot,fixture]) plain(directory, 'directory');
    eq(execution.actualPaths, {work,sourceRoot,dir,temp,fixture,nativeResult:nativeFile,callerEvidenceDir:out}, 'EXECUTION_PATHS');
    eq(paths, {schemaVersion:1,name,callerEvidenceDir:out,work,sourceRoot,compileDir:dir,temp,executable,nativeResult:nativeFile,fixture,sources:execution.sourceInputs}, 'PATHS_SCOPE');
    const captured = validateSources(execution.sourceInputs, sourceRoot, mutant);
    if (mutant) {
      const proofPin = mutations.get(mutant.name).proof; verifyPin(proofPin, path.join(out, 'mutation-' + mutant.name + '.json'));
      const proof = read(proofPin.path);
      eq(proof.name, mutant.name, 'MUTATION_NAME'); samePath(proof.sourceRoot, sourceRoot, 'MUTATION_ROOT');
      eq(proof.path, mutant.path, 'MUTATION_PATH'); eq(proof.from, mutant.from, 'MUTATION_FROM'); eq(proof.to, mutant.to, 'MUTATION_TO');
      eq(proof.expectedFailure, mutant.failure, 'MUTATION_FAILURE'); eq(proof.beforeSha256, frozen.get(mutant.path)?.sha256, 'MUTATION_BEFORE');
      verifyPin(proof.after, path.join(sourceRoot, mutant.path)); eq(proof.after.sha256, captured.get(mutant.path)?.sha256, 'MUTATION_AFTER');
      eq(proof.sources, execution.sourceInputs, 'MUTATION_SOURCE_SCOPE');
    }
    verifyPin(execution.executable, executable); eq(execution.exeSha256, execution.executable.sha256, 'EXE_HASH');
    eq(bytes(nativeFile), bytes(record.native.path), 'NATIVE_COPY_CHANGED'); eq(execution.native, native, 'NATIVE_OBJECT_CHANGED');
    const expectedCount = mutant ? mutant.nativeCaseCount : contract.positiveCaseCount;
    const expectedNames = mutant ? (mutant.selectedCase ? [mutant.selectedCase] : primaryNames) : caseNames;
    eq(native.count, expectedCount, 'NATIVE_COUNT'); eq(native.cases?.length, expectedCount, 'NATIVE_CASE_COUNT');
    // Callback-owned auxiliary rows can precede their enclosing check's receipt.
    // Match the complete multiset; counts, duplicates, results and semantic failures remain checked.
    eq(native.cases.map(row => row.name).sort(), [...expectedNames].sort(), 'NATIVE_CASE_SCOPE');
    for (const row of native.cases) fail(typeof row.pass === 'boolean' && (row.pass || (typeof row.error === 'string' && row.error.length > 0)), 'NATIVE_CASE_RESULT');
    const passed = native.cases.filter(row => row.pass).length;
    eq(native.passed, passed, 'NATIVE_PASSED_COUNT'); eq(native.pass, !mutant, 'NATIVE_VERDICT');
    if (native.status !== undefined) eq(native.status, mutant ? 'FAIL' : 'PASS', 'NATIVE_STATUS');
    eq(native.officialLoaderExecuted, false, 'NATIVE_LOADER_SCOPE'); eq(native.realSteamActions, 0, 'NATIVE_STEAM_SCOPE');
    if (family === 'worker') { eq(native.childIsOwnTestImage, true, 'NATIVE_WORKER_SCOPE'); eq(native.originalStopDeadlineMs, 3200, 'NATIVE_STOP'); }
    else { eq(native.loaderFixtureAliasOnly, true, 'NATIVE_ALIAS_SCOPE'); eq(native.productProcessesLaunched, 0, 'NATIVE_PRODUCT_SCOPE'); eq(native.physicalSleep, false, 'NATIVE_SLEEP_SCOPE'); }
    if (mutant) {
      fail(passed < expectedCount, 'SEMANTIC_FAILURE_COUNT');
      fail(native.cases.some(row => row.name === mutant.failure && row.pass === false), 'SEMANTIC_FAILURE_MISSING');
      const negative = negatives[index - 1];
      eq(negative, {name:mutant.name,detected:true,failure:mutant.failure,execution:record.execution.path}, 'NEGATIVE_BINDING');
      failurePins.push({name:mutant.name,native:record.native,execution:record.execution,mutation:mutations.get(mutant.name).proof});
    } else eq(passed, expectedCount, 'POSITIVE_FAILED');
    eq(execution.status, mutant ? 2 : 0, 'EXECUTION_STATUS'); fail(execution.error == null && execution.signal == null, 'EXECUTION_NOT_NATURAL');
    eq(execution.build, read(record.buildProcess.path), 'BUILD_OBJECT_CHANGED'); eq(execution.run, read(record.runProcess.path), 'RUN_OBJECT_CHANGED');
    const dependencies = path.join(dir, 'd.json'); eq(bytes(dependencies), bytes(record.dependencies.path), 'DEPENDENCIES_COPY');
    const buildArgs = ['/nologo','/std:c++20','/utf-8','/EHsc','/MT','/O2','/DNOMINMAX','/DUNICODE','/D_UNICODE',
      '/I' + path.join(sourceRoot,'native'),'/I' + path.join(sourceRoot,'deps/json'),path.join(sourceRoot,cpp),
      '/Fo' + path.join(dir,'resource-cache.obj'),'/Fe' + executable,'/sourceDependencies',dependencies,
      '/link','/verbose:lib','advapi32.lib','user32.lib','ws2_32.lib','bcrypt.lib'];
    processReceipt(execution.build, 'build', nativeTools.compiler, buildArgs, dir, temp, evidenceDir, 0); tool(execution.build.program);
    const runArgs = [fixture,nativeFile,graph.officialData.path,...(mutant?.selectedCase ? ['--case',mutant.selectedCase] : [])];
    processReceipt(execution.run, 'run', executable, runArgs, dir, temp, evidenceDir, mutant ? 2 : 0);
    eq(execution.run.program.sha256, execution.executable.sha256, 'RUN_EXE_HASH');
    const dep = read(record.dependencies.path); eq(dep.Version, '1.2', 'DEPENDENCIES_VERSION');
    fail(dep.Data && Array.isArray(dep.Data.Includes), 'DEPENDENCIES_MISSING');
    samePath(dep.Data.Source, path.join(sourceRoot, cpp), 'DEPENDENCIES_SOURCE');
    const actualInputs = [dep.Data.Source,...dep.Data.Includes].map(file => {
      const sourceRow = [...captured.values()].find(row => key(row.path) === key(file));
      if (sourceRow) return {...sourceRow,role:'captured-source'};
      const observedRow = observed.get(key(file)); fail(observedRow, 'COMPILER_INPUT_UNDECLARED:' + file);
      usedTools.add(key(file)); eq(observedRow.role, 'actual-compiler-input', 'COMPILER_INPUT_ROLE'); tool(observedRow); return observedRow;
    });
    eq(execution.actualCompilerInputs, actualInputs, 'COMPILER_INPUT_GRAPH');
    const linkText = Buffer.concat([bytes(execution.build.stdout.path),bytes(execution.build.stderr.path)]).toString('utf8');
    const libraries = [...new Set([...linkText.matchAll(/[A-Za-z]:\\[^\r\n]*?\.lib\b/g)].map(m => path.resolve(m[0])))];
    fail(libraries.length > 0, 'LINK_TRACE_MISSING');
    eq(execution.actualLinkLibraries.map(row => key(row.path)), libraries.map(key), 'LINK_TRACE_SCOPE');
    for (const row of execution.actualLinkLibraries) {
      tool(row); eq(row.role, 'actual-link-library', 'LINK_LIBRARY_ROLE'); usedTools.add(key(row.path)); eq(row, observed.get(key(row.path)), 'LINK_LIBRARY_GRAPH');
    }
    if (family === 'worker') eq(execution.junction, null, 'WORKER_JUNCTION_SCOPE');
    else {
      // The sole intentional fixture junction is metadata only. No referenced artifact may traverse it.
      const link = path.join(fixture, 'reparse/PowerControl/decky/.ymcc-resource-cache/a'), outside = path.join(fixture, 'outside');
      eq(execution.junction, {path:link,target:outside,realTarget:outside,insideExactOwnedWork:true}, 'FIXTURE_JUNCTION_SCOPE');
      plain(outside, 'directory'); plain(path.dirname(link), 'directory');
      fail(fs.lstatSync(link).isSymbolicLink(), 'FIXTURE_JUNCTION_MISSING');
      eq(key(fs.realpathSync.native(link)), key(outside), 'FIXTURE_JUNCTION_TARGET');
    }
    verifiedRuns.push({name,execution:record.execution,native:record.native,buildProcess:record.buildProcess,runProcess:record.runProcess,
      buildLog:pin(path.join(evidenceDir,'build.log')),runLog:pin(path.join(evidenceDir,'run.log'))});
  }
  eq([...usedTools].sort(), [...observed.keys()].sort(), 'ACTUAL_TOOL_SCOPE');
  eq(summary.positive, {status:0,count:contract.positiveCaseCount}, 'SUMMARY_POSITIVE');
  eq(result.nativeCases, contract.positiveCaseCount, 'RESULT_NATIVE_COUNT');
  return {schemaVersion:1,status:'SHARED_REFERENCE',requiredReleaseStep:contract.step,family,
    originalProcess:pin(parentFile),originalResult:entry.files.result,originalBaseline:pin(baselineFile),
    originalElapsedMs:entry.elapsedMs,originalModule:entry.module,originalModuleSha256After:entry.moduleSha256After,
    originalCaller:entry.caller,originalEnvironment:entry.environment,originalOptions:entry.options,
    originalWorkspace:entry.files.workspace,originalWorkspaceOwner:workspace.workspaceOwner,originalOwnerNonce:owner.nonce,
    originalFiles:entry.files,ordinaryCases:contract.positiveCaseCount,semanticNegativeMutations:contract.mutants.length,
    originalRuns:verifiedRuns,originalNegativeFailuresPreserved:failurePins,
    originalProducerExecutionsWithinParent:1,originalProducerExecutionsByThisReference:0,
    executedThisRun:false,cached:false,fullProductAcceptance:false};
}

export function verifyNativeResourceReference(options = {}) {
  try {
    return verifyResourceReference(options);
  } catch (error) {
    if (String(error?.message).startsWith('RESOURCE_REFERENCE_')) throw error;
    throw new Error('RESOURCE_REFERENCE_' + (error?.message ?? String(error)), {cause:error});
  }
}


// The full parent/source/tool/current-window verifier MUST run before this inner verifier.
// These exports only describe or verify existing executions; they never launch a producer.
const NATIVE_REFERENCE_ENVIRONMENT_KEYS = Object.freeze([
  'SystemRoot','ComSpec','PATH','INCLUDE','LIB','LIBPATH','CL','_CL_','LINK','_LINK_','TEMP','TMP'
]);
const NATIVE_REFERENCE_FAMILIES = Object.freeze({
  health:Object.freeze({name:'health',label:'HEALTH',prefix:'NATIVE_HEALTH_REFERENCE_',
    source:'tools/decky_sidebar_health_selftest.cpp',toolchain:'nativeHealthToolchain',inputs:'nativeHealthInputs',
    extraLibraries:Object.freeze([]),sourceArgument:/(?:^|[\\/])decky_sidebar_health_selftest\.cpp$/i,
    stdout:/(?:^|\r?\n)DECKY_HEALTH_POLICY_OK(?:[ \t][^\r\n]*)?(?:\r?\n|$)/}),
  runtime:Object.freeze({name:'runtime',label:'RUNTIME',prefix:'NATIVE_RUNTIME_REFERENCE_',
    source:'tools/decky_sidebar_runtime_selftest.cpp',toolchain:'nativeRuntimeToolchain',inputs:'nativeRuntimeInputs',
    extraLibraries:Object.freeze(['shell32.lib']),sourceArgument:/(?:^|[\\/])decky_sidebar_runtime_selftest\.cpp$/i,
    stdout:/(?:^|\r?\n)YMCC Decky sidebar native no-launch tests passed: [0-9]+(?:\r?\n)?(?![\s\S])/})
});
const nativeAssert = (family,ok,reason) => assert(ok, family.prefix + reason);
const nativeEqual = (family,actual,expected,reason) => assert.deepEqual(actual, expected, family.prefix + reason);

export function nativeHealthCompileRequest(options) {
  return nativeCompileRequest(NATIVE_REFERENCE_FAMILIES.health,options);
}
export function nativeRuntimeCompileRequest(options) {
  return nativeCompileRequest(NATIVE_REFERENCE_FAMILIES.runtime,options);
}
function nativeCompileRequest(family,{sourceRoot,outputRoot,compiler}) {
  for (const value of [sourceRoot,outputRoot,compiler]) nativeAssert(family,typeof value === 'string' && path.isAbsolute(value), 'RECIPE_ABSOLUTE_PATH');
  const source=path.join(sourceRoot,family.source);
  const image=path.join(outputRoot,'test.exe'), graph=path.join(outputRoot,'deps.json');
  return {source,image,graph,compiler,cwd:outputRoot,arguments:[
    '/nologo','/std:c++20','/utf-8','/EHsc','/MT','/O2','/DNOMINMAX','/DUNICODE','/D_UNICODE',
    '/I'+path.join(sourceRoot,'native'),'/I'+path.join(sourceRoot,'deps/json'),source,
    '/sourceDependencies',graph,'/Fo'+image+'.obj','/Fe'+image,'/link','/SUBSYSTEM:CONSOLE','/MACHINE:x64',
    'advapi32.lib','user32.lib','userenv.lib','winhttp.lib','ws2_32.lib','bcrypt.lib',...family.extraLibraries
  ],runArguments:[]};
}

export function nativeHealthEnvironment(env) {
  return nativeEnvironment(NATIVE_REFERENCE_FAMILIES.health,env);
}
function nativeEnvironment(family,env) {
  const check=(ok,reason)=>nativeAssert(family,ok,reason);
  check(env && typeof env === 'object' && !Array.isArray(env), 'ENVIRONMENT_REQUIRED');
  const wanted=new Set(NATIVE_REFERENCE_ENVIRONMENT_KEYS.map(name=>name.toUpperCase())), folded=new Map();
  for (const [name,value] of Object.entries(env)) {
    const upper=name.toUpperCase(); if (!wanted.has(upper)) continue;
    check(value === null || typeof value === 'string', 'ENVIRONMENT_VALUE:'+name);
    check(!folded.has(upper) || folded.get(upper) === value, 'ENVIRONMENT_CASE_COLLISION:'+name);
    folded.set(upper,value);
  }
  return Object.fromEntries(NATIVE_REFERENCE_ENVIRONMENT_KEYS.map(name=>[name,folded.get(name.toUpperCase()) ?? null]));
}

export function verifyNativeHealthReference(options = {}) {
  return verifyNativeReference(NATIVE_REFERENCE_FAMILIES.health,options);
}
export function verifyNativeRuntimeReference(options = {}) {
  return verifyNativeReference(NATIVE_REFERENCE_FAMILIES.runtime,options);
}
function verifyNativeReference(family,{repoRoot,coreRoot,parentResult,parent,sourceInputs,toolArtifacts,
  nativeAuthority=selectedNativeTools().native}={}) {
  try {
    const {plain,bytes,read,pin,verifyPin}=createResourceProofReader();
    const check=(ok,reason)=>nativeAssert(family,ok,reason), equal=(actual,expected,reason)=>nativeEqual(family,actual,expected,reason);
    const repo=plain(repoRoot,'directory'),core=plain(coreRoot,'directory');
    check(key(repo)!==key(core) && !within(repo,core) && !within(core,repo),'CORE_SCOPE');
    const parentFile=plain(parentResult,'file'),base=path.dirname(parentFile);
    check(within(path.join(core,'Evidence'),parentFile),'PARENT_OUTSIDE_CORE');
    equal(read(parentFile),parent,'PARENT_ORIGINAL_CHANGED');
    const work=plain(parent?.work,'directory');
    check(within(path.join(core,'Workspace'),work),'WORK_OUTSIDE_CORE');
    const sourceRoot=plain(path.join(work,'src'),'directory'),outputRoot=plain(path.join(work,'n',family.name),'directory');
    const authority=nativeAuthority;
    check(authority && typeof authority.compiler==='string' && typeof authority.linker==='string','AUTHORITY_REQUIRED');
    for (const name of ['include','lib']) {
      check(Array.isArray(authority[name]) && authority[name].length>0 && authority[name].every(p=>typeof p==='string' && path.isAbsolute(p)),'AUTHORITY_'+name);
      check(new Set(authority[name].map(key)).size===authority[name].length,'AUTHORITY_DUPLICATE_'+name);
    }
    const toolchain=parent[family.toolchain];
    check(toolchain && Object.keys(toolchain).sort().join('|')==='compiler|include|lib|linker','TOOLCHAIN_REQUIRED');
    equal(toolchain.include,authority.include,'TOOLCHAIN_INCLUDE'); equal(toolchain.lib,authority.lib,'TOOLCHAIN_LIB');
    const toolMap=new Map();
    for (const row of rows(toolArtifacts,family.label+'_TOOLS_REQUIRED')) {
      // The caller already verified the complete parent tool set; this leaf re-reads only its actual dependencies.
      check(row && typeof row.path==='string' && path.isAbsolute(row.path) && digest(row.sha256),'TOOL_PIN_REQUIRED');
      if(row.bytes!==undefined)check(Number.isSafeInteger(row.bytes) && row.bytes>=0,'TOOL_PIN_SIZE');
      check(!toolMap.has(key(row.path)),'TOOL_DUPLICATE');toolMap.set(key(row.path),row);
    }
    const boundTool=(row,expected)=>{
      verifyPin(row,expected); check(toolMap.has(key(row.path)),'TOOL_UNDECLARED');
      equal(row.sha256,toolMap.get(key(row.path)).sha256,'TOOL_HASH');return row;
    };
    boundTool(toolchain.compiler,authority.compiler);boundTool(toolchain.linker,authority.linker);
    // Bind every explicit library and the static CRT inputs for this fixed /MT recipe.
    const requiredLibraries=["advapi32.lib", "user32.lib", "userenv.lib", "winhttp.lib", "ws2_32.lib", "bcrypt.lib", ...family.extraLibraries, "libcmt.lib", "libcpmt.lib", "libvcruntime.lib", "libucrt.lib", "oldnames.lib", "kernel32.lib"];
    for(const name of requiredLibraries){
      check(!fs.existsSync(path.join(outputRoot,name)),'LIBRARY_CWD_SHADOW:'+name);
      const file=authority.lib.map(dir=>path.join(dir,name)).find(file=>fs.existsSync(file));
      check(file,'LIBRARY_MISSING:'+name);check(toolMap.has(key(file)),'LIBRARY_TOOL_UNDECLARED:'+name);
      boundTool(toolMap.get(key(file)),file);
    }
    const sourceMap=new Map();
    for (const [file,digestValue] of sourceInputs instanceof Map ? sourceInputs : rows(sourceInputs,family.label+'_SOURCES_REQUIRED').map(row=>[row.path,row.sha256])) {
      const name=relative(file);check(digest(digestValue),'SOURCE_HASH_REQUIRED');check(!sourceMap.has(name.toLowerCase()),'SOURCE_DUPLICATE');sourceMap.set(name.toLowerCase(),digestValue);
    }
    const recipe=nativeCompileRequest(family,{sourceRoot,outputRoot,compiler:authority.compiler});
    const caseFor=id=>{
      const matches=rows(parent.cases,family.label+'_CASES_REQUIRED').filter(row=>row.id===id);
      equal(matches.length,1,'CASE_NOT_UNIQUE:'+id);equal(matches[0].passed,true,'CASE_NOT_PASSED:'+id);return matches[0];
    };
    const buildId='build.original-native-'+family.name,runId='original.native-'+family.name;
    const buildCase=caseFor(buildId),runCase=caseFor(runId);
    if (family.name==='runtime') {
      equal(buildCase.legacyMacro,true,'LEGACY_BUILD_ROLE');
      equal(runCase.legacyRegressionOnly,true,'LEGACY_RUN_ROLE');
      equal(runCase.currentTypedAcceptance,false,'LEGACY_TYPED_SCOPE');
    }
    const graph=verifyPin(buildCase.compilerGraph,recipe.graph),image=verifyPin(buildCase.exe,recipe.image);
    verifyPin(runCase.exe,recipe.image);equal(runCase.exe.sha256,image.sha256,'IMAGE_CASE_BINDING');
    const system=nativeEnvironment(family,process.env);
    check(typeof system.SystemRoot==='string' && system.SystemRoot.length>0 && typeof system.ComSpec==='string' && system.ComSpec.length>0,'SYSTEM_ENVIRONMENT_REQUIRED');
    const expected=nativeEnvironment(family,{SystemRoot:system.SystemRoot,ComSpec:system.ComSpec,
      INCLUDE:authority.include.join(';'),LIB:authority.lib.join(';'),
      PATH:path.dirname(authority.compiler)+';'+path.dirname(authority.linker)+';'+system.SystemRoot+'/System32',
      LIBPATH:'',CL:'',_CL_:'',LINK:'',_LINK_:'',TEMP:work,TMP:work});
    const begin=Date.parse(parent.startedAt),end=Date.parse(parent.finishedAt);
    check(Number.isFinite(begin) && Number.isFinite(end) && begin<=end,'PARENT_WINDOW');
    const all=rows(parent.processes,family.label+'_PROCESSES_REQUIRED');
    const nativeTokens=[recipe.source,recipe.image,recipe.graph].map(value=>value.replaceAll('\\','/').toLowerCase());
    const related=all.filter(row=>{
      if ([buildId,runId].includes(row.id)) return true;
      if (typeof row.program?.path==='string' && key(row.program.path)===key(recipe.image)) return true;
      return Array.isArray(row.arguments) && row.arguments.some(arg=>typeof arg==='string' &&
        (nativeTokens.some(token=>arg.replaceAll('\\','/').toLowerCase().includes(token)) || family.sourceArgument.test(arg)));
    });
    equal(related.length,2,'PRODUCER_EXECUTIONS_NOT_UNIQUE');
    const processFor=(id,program,args,maxBudget)=>{
      const matches=all.filter(row=>row.id===id);equal(matches.length,1,'PROCESS_NOT_UNIQUE:'+id);
      const proof=matches[0];check(related.includes(proof),'PROCESS_SCOPE');
      const file=path.join(base,'processes',id+'.json');plain(file,'file');equal(read(file),proof,'PROCESS_ORIGINAL_CHANGED:'+id);
      check(pid(proof.pid),'PROCESS_PID:'+id);
      for (const [name,value] of Object.entries({exitCode:0,signal:null,error:null,timedOut:false,outputTruncated:false})) equal(proof[name],value,'PROCESS_'+name+':'+id);
      if (proof.natural!==undefined) equal(proof.natural,true,'PROCESS_NOT_NATURAL');
      if (proof.completion!==undefined) equal(proof.completion.kind,'NATURAL','PROCESS_NOT_NATURAL');
      const start=Date.parse(proof.startedAt),finish=Date.parse(proof.finishedAt);
      check(Number.isFinite(start) && Number.isFinite(finish) && begin<=start && start<=finish && finish<=end,'PROCESS_WINDOW:'+id);
      check(Number.isFinite(proof.elapsedMs) && proof.elapsedMs>=0 && Number.isFinite(proof.timeoutMs) && proof.timeoutMs>0 && proof.timeoutMs<=maxBudget,'PROCESS_TIMING:'+id);
      equal(proof.originalBudgetMs,maxBudget,'PROCESS_BUDGET:'+id);
      equal(proof.arguments,args,'PROCESS_ARGUMENTS:'+id);samePath(proof.cwd,recipe.cwd,family.label+'_PROCESS_CWD');
      verifyPin(proof.program,program.path);equal(proof.program.sha256,program.sha256,'PROCESS_PROGRAM:'+id);
      for (const stream of ['stdout','stderr']) verifyPin(proof[stream],path.join(base,'processes',id+'.'+stream+'.bin'));
      const observation=proof[family.inputs];
      equal(observation?.schemaVersion,1,'ENVIRONMENT_OBSERVATION_REQUIRED');
      for (const state of ['before','after']) {
        equal(observation[state],nativeEnvironment(family,observation[state]),'ENVIRONMENT_OBSERVATION_SCOPE');
        equal(observation[state],expected,'ENVIRONMENT_FIXED:'+id+':'+state);
      }
      equal(observation.before,observation.after,'ENVIRONMENT_CHANGED');
      return {proof,original:pin(file),start,finish};
    };
    const compile=processFor(buildId,toolchain.compiler,recipe.arguments,60000);
    const run=processFor(runId,image,recipe.runArguments,15000);
    // Non-overlapping invocations can reuse an OS PID; program pins and original receipts bind each phase.
    check(compile.finish<=run.start,'PROCESS_ORDER');
    verifyPin(runCase.process,run.original.path);equal(runCase.process.sha256,run.original.sha256,'RUN_PROCESS_PIN');
    if (buildCase.process!==undefined) {verifyPin(buildCase.process,compile.original.path);equal(buildCase.process.sha256,compile.original.sha256,'COMPILE_PROCESS_PIN');}
    check(family.stdout.test(bytes(run.proof.stdout.path).toString('utf8')),'STDOUT_MARKER');
    const dependencies=read(graph.path);equal(dependencies.Version,'1.2','GRAPH_VERSION');
    check(dependencies.Data && Array.isArray(dependencies.Data.Includes),'GRAPH_REQUIRED');
    samePath(dependencies.Data.Source,recipe.source,family.label+'_GRAPH_SOURCE');
    const seen=new Set();
    for (const file of [dependencies.Data.Source,...dependencies.Data.Includes]) {
      plain(file,'file');check(!seen.has(key(file)),'GRAPH_DUPLICATE');seen.add(key(file));
      if (within(sourceRoot,file)) {
        const rel=relative(path.relative(sourceRoot,file)).toLowerCase(),expectedSha=sourceMap.get(rel);
        check(digest(expectedSha),'GRAPH_SOURCE_UNDECLARED:'+rel);equal(sha(bytes(file)),expectedSha,'FROZEN_SOURCE_HASH:'+rel);
        const live=path.join(repo,path.relative(sourceRoot,file));equal(sha(bytes(live)),expectedSha,'CURRENT_SOURCE_HASH:'+rel);
      } else {
        check(!within(work,file) && !within(repo,file),'GRAPH_INPUT_OUTSIDE_FREEZE');
        check(toolMap.has(key(file)),'GRAPH_TOOL_UNDECLARED');boundTool(toolMap.get(key(file)),file);
      }
    }
    // The exact recipe and cleared CL/_CL_ retain assertion evaluation: no NDEBUG or injected flags.
    return {schemaVersion:1,status:'SHARED_REFERENCE',requiredReleaseBuildStep:'official-native-'+family.name+'-build',
      requiredReleaseRunStep:'official-native-'+family.name,originalParent:pin(parentFile),
      originalCompileProcess:compile.original,originalRunProcess:run.original,originalProgram:image,compilerGraph:graph,
      originalCompileElapsedMs:compile.proof.elapsedMs,originalRunElapsedMs:run.proof.elapsedMs,
      originalProducerExecutionsWithinParent:1,originalProducerExecutionsByThisReference:0,
      compilerInvocationsWithinParent:1,compilerInvocationsByThisReference:0,
      cached:false,executedThisRun:false,actualPassedThisRun:false,fullProductAcceptance:false};
  } catch (error) {
    if (String(error?.message).startsWith(family.prefix)) throw error;
    throw new Error(family.prefix+(error?.message ?? String(error)),{cause:error});
  }
}
