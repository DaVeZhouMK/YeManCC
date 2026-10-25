import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';

export const OBSERVED_FILE_REQUEST = 'Serilog.Sinks.File, Culture=neutral, PublicKeyToken=null';
export const FILE_PROVIDER_IDENTITY = 'Serilog.Sinks.File, Version=7.0.0.0, Culture=neutral, PublicKeyToken=24c2f752a8e58a10';
export const FILE_PROVIDER_SHA256 = '4f4282671c2ac68284b4dc99f7a76eac9cbd4e659978cef642cff9e6c01b676d';
const ENTRY = 'JoinedFactoryProbe, Version=1.0.0.0, Culture=neutral, PublicKeyToken=null';
const REVIEW_KIND = 'joined-configured-file-sink-request-review';
const FRAMEWORK_REQUESTS = [
  'System.Runtime.InteropServices, Version=8.0.0.0, Culture=neutral, PublicKeyToken=b03f5f7f11d50a3a',
  'System.Runtime, Version=8.0.0.0, Culture=neutral, PublicKeyToken=b03f5f7f11d50a3a',
  'System.Collections.Concurrent, Version=8.0.0.0, Culture=neutral, PublicKeyToken=b03f5f7f11d50a3a',
  'System.Runtime, Version=6.0.0.0, Culture=neutral, PublicKeyToken=b03f5f7f11d50a3a',
  'netstandard, Version=2.0.0.0, Culture=neutral, PublicKeyToken=cc7b13ffcd2ddd51',
];
const hash = data => crypto.createHash('sha256').update(data).digest('hex');
const eq = (a, b) => JSON.stringify(a) === JSON.stringify(b);
const clone = value => structuredClone(value);
const norm = value => path.win32.resolve(value).toLowerCase();
const samePath = (a, b) => typeof a === 'string' && typeof b === 'string' && norm(a) === norm(b);
const within = (root, file) => norm(file).startsWith(norm(root).replace(/\\$/, '') + '\\');
function need(ok, code) { if (!ok) throw new Error(code); }
function noModel(value) {
  if (typeof value === 'string') need(!/MODEL[_-]?ONLY/i.test(value), 'FILE_SINK_ACTUAL_REJECTS_MODEL');
  else if (value && typeof value === 'object') for (const [key, item] of Object.entries(value)) { noModel(key); noModel(item); }
}
function pointer(value, at) {
  need(typeof at === 'string' && at.startsWith('/') && !/~(?![01])/u.test(at), 'FILE_SINK_JSON_POINTER');
  for (const part of at.slice(1).split('/').map(x => x.replaceAll('~1', '/').replaceAll('~0', '~'))) {
    need(value !== null && typeof value === 'object' && Object.hasOwn(value, part), 'FILE_SINK_POINTER_MISSING');
    value = value[part];
  }
  return value;
}
function descriptorEqual(a, b) {
  return a && b && samePath(a.path, b.path) && a.sha256 === b.sha256 && a.identity === b.identity && a.role === b.role;
}
function pinEqual(a, b) { return a && b && samePath(a.path, b.path) && a.sha256 === b.sha256; }
function configPin(policy, ownedRoot) {
  const expected = path.win32.join(ownedRoot, 'inputs', 'YeManFanHost.json');
  const rows = policy.metadata.filter(x => samePath(x.path, expected));
  need(rows.length === 1, 'FILE_SINK_CONFIG_METADATA_EXACT_PIN');
  return rows[0];
}
const fullIdentity = value => typeof value === 'string' && /^[^,]+, Version=\d+\.\d+\.\d+\.\d+, Culture=[^,]+, PublicKeyToken=(?:null|[a-f0-9]{16})$/.test(value);
function descriptorsEqual(a, b) { return Array.isArray(a) && Array.isArray(b) && a.length === b.length && a.every((p, i) => descriptorEqual(p, b[i])); }
function validateManagedInput(v) {
  const d = v.policy.contexts[0], m = v.managedInput;
  need(m && m.managedCount === v.managedCount && descriptorsEqual(m.managed, d.managed), 'FILE_SINK_MANAGED_EXACT_QUALIFIED_SET');
  const paths = new Set(), identities = new Set();
  for (const row of d.managed) {
    need(fullIdentity(row.identity) && path.win32.isAbsolute(row.path ?? '') && /^[a-f0-9]{64}$/.test(row.sha256 ?? '') && ['tool', 'infrastructure', 'target', 'framework'].includes(row.role), 'FILE_SINK_MANAGED_FULL_DESCRIPTOR');
    need(!paths.has(norm(row.path)) && !identities.has(row.identity), 'FILE_SINK_MANAGED_UNIQUE'); paths.add(norm(row.path)); identities.add(row.identity);
  }
  need(Array.isArray(m.frameworkExtensions) && m.frameworkExtensions.length <= 3072 && Array.isArray(m.entries) && m.entries.length === m.frameworkExtensions.length, 'FILE_SINK_FRAMEWORK_EXTENSION_SET');
  const extensions = new Set();
  for (let i = 0; i < m.frameworkExtensions.length; i++) {
    const row = m.frameworkExtensions[i], e = m.entries[i];
    need(row.role === 'framework' && !extensions.has(norm(row.path)) && d.managed.filter(p => descriptorEqual(row, p)).length === 1, 'FILE_SINK_EXTENSION_EXACT_FRAMEWORK'); extensions.add(norm(row.path));
    need(e && descriptorEqual(e.provider, row) && e.classificationRole === 'framework' && pinEqual(e.observed, row) && e.observed.identity === row.identity && pinEqual(e.classified, row) && e.classified.identity === row.identity, 'FILE_SINK_EXTENSION_OBSERVATION_CLASSIFICATION');
  }
  const expectedRequests = [...d.managed.map(p => ({requestedIdentity: p.identity, targetContext: 'Default', targetPath: p.path})), ...v.policy.exactAliases.map(a => ({requestedIdentity: a.requestedIdentity, targetContext: 'Default', targetPath: a.provider.path}))];
  need(d.requests.length === expectedRequests.length && expectedRequests.every(p => d.requests.filter(r => r.requestedIdentity === p.requestedIdentity && r.targetContext === p.targetContext && samePath(r.targetPath, p.targetPath)).length === 1) && new Set(d.requests.map(r => r.requestedIdentity)).size === d.requests.length, 'FILE_SINK_EXACT_DEFAULT_REQUEST_ROUTES');
}
const originalRoleNames=['positive','first-start','readiness','continuation','native','native-path'];
function exactPreparedRole(base){
 need(originalRoleNames.includes(base.role),'FILE_SINK_BASE_PROFILE');
 if(base.role==='positive')return;
 need(base.expectedExitCode===2&&eq(base.expectedFailureMask,base.failureMasks[base.role])&&base.variantQualification,'FILE_SINK_VARIANT_EXPECTATION');
}
function qualifiedManagedInput(options, base, policy, read, verifyTree, modelOnly = false) {
  const inMetadata = pin => need(pin && policy.metadata.filter(x => pinEqual(x, pin)).length === 1, 'FILE_SINK_QUALIFICATION_METADATA_PIN');
  inMetadata(options.baseQualifiedInputsPin);
  const q = read(options.baseQualifiedInputsPin); if (!modelOnly) noModel(q);
  need(q.schemaVersion === 1 && q.evidenceKind === 'ACTUAL_PINNED_PARENT_REVIEW' && q.profile === base.profile && q.role === base.role && samePath(q.stageRoot, base.ownedRoot), 'FILE_SINK_QUALIFIED_BASE_PROFILE');
  need(eq(q.originalCaseIds, base.expectedCaseIds) && eq(q.failureMasks, base.failureMasks) && eq(q.oracleFailureMasks, base.failureMasks) && eq(q.budgets, base.budgets) && q.initializeRole === 'UNKNOWN' && q.nativeBoundaryLength === base.nativeBoundaryLength, 'FILE_SINK_QUALIFIED_ORIGINAL_CONTRACT');
  exactPreparedRole(base);if(base.role!=='positive'){inMetadata(q.evidence?.variantQualification);need(pinEqual(q.evidence.variantQualification,base.variantQualification),'FILE_SINK_VARIANT_QUALIFIED_PIN');const v=read(q.evidence.variantQualification);if(!modelOnly)noModel(v);need(v.schemaVersion===1&&v.kind==='joined-original123-variant-qualification'&&v.evidenceKind==='ACTUAL_PINNED_PARENT_REVIEW'&&v.role===base.role&&v.expectedExitCode===2&&eq(v.expectedMask,base.expectedFailureMask)&&v.faultHost.sha256===q.host.sha256&&v.faultHost.identity===q.host.identity,'FILE_SINK_VARIANT_QUALIFIED_BINDING');verifyTree(v);}
  for (const p of [q.reviewedSelectionPin, q.evidence?.dependencySelection]) inMetadata(p);
  const inventory = read(q.reviewedSelectionPin), selection = read(q.evidence.dependencySelection); if (!modelOnly) { noModel(inventory); noModel(selection); }
  const ext = q.frameworkExtensions ?? [];
  need(selection.kind === 'joined-default-exact-dependency-selection' && pinEqual(selection.inventory, q.reviewedSelectionPin) && pinEqual(selection.parentPolicy, q.parentPolicyPin) && descriptorsEqual(selection.selectedDefault, q.selectedDefault) && eq(selection.aliases, q.aliases) && eq(selection.observedRefs, q.observedRefs) && descriptorsEqual(selection.targetPins, q.reviewedTargetPins), 'FILE_SINK_QUALIFIED_SELECTION');
  need(descriptorsEqual(inventory.targetPins, q.reviewedTargetPins) && eq(inventory.frameworkBaseline45, q.reviewedFrameworks) && inventory.producerRuntimeExecuted === false && inventory.doesNotGrantWholeAssemblyRefGraph === true && descriptorsEqual(inventory.frameworkExtensions ?? [], ext) && descriptorsEqual(selection.frameworkExtensions ?? [], ext), 'FILE_SINK_QUALIFIED_INVENTORY');
  need(Array.isArray(ext) && ext.length <= 3072 && Array.isArray(q.selectedDefault) && Array.isArray(q.additionalTargets) && Array.isArray(q.reviewedTargetPins), 'FILE_SINK_QUALIFIED_COLLECTIONS');
  need(descriptorsEqual(q.additionalTargets.filter(p => p.role === 'framework'), ext), 'FILE_SINK_QUALIFIED_ADDITIONAL_FRAMEWORKS');
  const prior = q.parentPolicy?.contexts?.find(c => c.id === 'Default');
  need(prior && q.selectedDefault.every(p => ['framework', 'infrastructure'].includes(p.role) && prior.managed.some(x => descriptorEqual(x, p)) && q.reviewedFrameworks.some(x => pinEqual(x, p) && x.identity === p.identity)), 'FILE_SINK_QUALIFIED_PARENT_BASELINE');
  for (const p of q.additionalTargets.filter(p => p.role !== 'framework')) need(q.reviewedTargetPins.some(x => descriptorEqual(x, p)), 'FILE_SINK_UNQUALIFIED_NONFRAMEWORK');
  const mapping = new Map();
  const copied = (p, relative) => { need(p && path.win32.isAbsolute(p.path ?? '') && /^[a-f0-9]{64}$/.test(p.sha256 ?? ''), 'FILE_SINK_QUALIFIED_SOURCE_DESCRIPTOR'); const row = {...p, path: path.win32.join(base.ownedRoot, relative)}; mapping.set(norm(p.path), row); return row; };
  const producer = copied(q.artifacts?.producer, 'tools/JoinedFactoryProbe/JoinedFactoryProbe.dll');
  const authority = copied(q.artifacts?.authority, 'tools/STA/ScopedLoadAuthority.dll');
  const ports = copied(q.artifacts?.ports, 'tools/JoinedFactoryProbe/ConstructorIoPorts.dll');
  need(pinEqual(producer, base.artifacts.producer) && pinEqual(authority, base.artifacts.authority) && pinEqual(ports, base.artifacts.ports), 'FILE_SINK_QUALIFIED_TOOLS');
  for (const p of q.support) copied(p, 'host/' + path.win32.basename(p.path));
  const host = copied(q.host, 'host/YeManFanHost.dll');
  for (const p of q.runtimeMembers) copied(p, 'runtime/' + path.win32.basename(p.path));
  for (const p of q.sharedMembers) copied(p, 'shared/' + path.win32.basename(p.path));
  const mapped = p => { const copy = mapping.get(norm(p.path)); need(copy && copy.sha256 === p.sha256 && (copy.identity === undefined || copy.identity === p.identity), 'FILE_SINK_QUALIFIED_COPY_MAPPING'); return {...p, path: copy.path}; };
  const hc = mapped(q.hc), backend = mapped(q.backend);
  need(descriptorEqual({...host, role: 'target'}, base.host) && descriptorEqual({...hc, role: 'target'}, base.hc), 'FILE_SINK_QUALIFIED_TARGET_IMAGES');
  const managed = [{...producer, role: 'tool'}, {...authority, role: 'infrastructure'}, {...ports, role: 'infrastructure'}, {...host, role: 'target'}, {...hc, role: 'target'}, {...backend, role: 'target'}, ...q.additionalTargets.map(p => p.role === 'framework' ? clone(p) : mapped(p)), ...q.selectedDefault.map(p => mapping.has(norm(p.path)) ? mapped(p) : clone(p))];
  const mappedTargets = q.reviewedTargetPins.map(mapped);
  need(managed.filter(p => p.role !== 'framework' && !pinEqual(p, authority) && !q.selectedDefault.some(s => descriptorEqual(s, p))).length === mappedTargets.length && mappedTargets.every(p => managed.filter(x => descriptorEqual(x, p)).length === 1), 'FILE_SINK_QUALIFIED_TARGET_SET');
  need(eq(q.aliases, policy.exactAliases), 'FILE_SINK_QUALIFIED_FIVE_ALIASES');
  const entries = [];
  if (ext.length) {
    inMetadata(q.evidence.frameworkExtensionReview);
    const review = read(q.evidence.frameworkExtensionReview); if (!modelOnly) noModel(review);
    need(review.schemaVersion === 1 && review.kind === 'joined-framework-extension-review' && review.evidenceKind === 'ACTUAL_PINNED_MANAGED_GRAPH_REVIEW' && descriptorsEqual(review.frameworkExtensions, ext) && review.entries?.length === ext.length, 'FILE_SINK_QUALIFIED_FRAMEWORK_REVIEW');
    for (const entry of review.entries) {
      need(/^\/(?:managedGraph|actualLoadedManaged)\/(?:0|[1-9]\d*)$/.test(entry.observation?.pointer ?? ''), 'FILE_SINK_QUALIFIED_FRAMEWORK_OBSERVATION_POINTER');
      inMetadata(entry.observation.receipt); inMetadata(entry.classification?.raw);
      const graph = read(entry.observation.receipt), raw = read(entry.classification.raw); if (!modelOnly) { noModel(graph); noModel(raw); }
      entries.push({provider: entry.provider, classificationRole: entry.classification.role, observed: pointer(graph, entry.observation.pointer), classified: pointer(raw, entry.classification.pointer)});
      verifyTree(entry);
    }
    verifyTree(review);
  } else need(!q.evidence?.frameworkExtensionReview, 'FILE_SINK_UNEXPECTED_FRAMEWORK_REVIEW');
  verifyTree(q); verifyTree(inventory); verifyTree(selection);
  return {managedCount: options.managedCount, managed, frameworkExtensions: ext, entries};
}
function validateProjectionInput(v) {
  const {base: b, policy: p, review: r, config, observation, observedBase, observedPolicy, rawRow, argv} = v;
  need(b.kind === 'joined-original123-source-prepared' && b.profile === 'joined-original123-schema6-single-default', 'FILE_SINK_BASE_PROFILE');exactPreparedRole(b);
  need(p.schemaVersion === 6 && samePath(p.ownedRoot, b.ownedRoot), 'FILE_SINK_SCHEMA6_OWNED_ROOT');
  need(p.contexts?.length === 1, 'FILE_SINK_SINGLE_DEFAULT');
  const d = p.contexts[0];
  need(d.id === 'Default' && d.kind === 'default' && d.registrationType === null && d.registrationAssembly === null, 'FILE_SINK_DEFAULT_REGISTRATION');
  need(Number.isInteger(v.managedCount) && v.managedCount >= 1 && v.managedCount <= 3072 && d.managed?.length === v.managedCount && Array.isArray(d.requests) && Array.isArray(p.metadata), 'FILE_SINK_MANAGED_COUNT');
  need(p.exactAliases?.length === 5 && eq(p.exactAliases.map(x => x.requestedIdentity), FRAMEWORK_REQUESTS), 'FILE_SINK_KEEP_FIVE_FRAMEWORK_ALIASES');
  for (const a of p.exactAliases) need(a.targetContext === 'Default' && eq(a.requestingContexts, ['Default']) && a.provider.role === 'framework' && d.managed.some(x => descriptorEqual(x, a.provider)), 'FILE_SINK_FRAMEWORK_ALIAS_BINDING');
  need(b.artifacts.producer.identity === ENTRY && samePath(p.entry, b.artifacts.producer.path), 'FILE_SINK_ENTRY');
  need(d.managed.filter(x => samePath(x.path, p.entry) && x.identity === ENTRY && x.role === 'tool' && x.sha256 === b.artifacts.producer.sha256).length === 1, 'FILE_SINK_ENTRY_PIN');
  need(b.artifacts.authority.sha256 === b.artifacts.producerAuthority.sha256, 'FILE_SINK_SHARED_AUTHORITY');
  need(d.managed.filter(x => x.identity === 'ScopedLoadAuthority, Version=0.0.0.0, Culture=neutral, PublicKeyToken=null' && x.role === 'infrastructure' && pinEqual(x, b.artifacts.authority)).length === 1, 'FILE_SINK_AUTHORITY_DEFAULT_PIN');
  const providers = d.managed.filter(x => x.identity === FILE_PROVIDER_IDENTITY);
  need(providers.length === 1 && providers[0].role === 'target' && providers[0].sha256 === FILE_PROVIDER_SHA256 && samePath(providers[0].path, path.win32.join(b.ownedRoot, 'runtime', 'Serilog.Sinks.File.dll')), 'FILE_SINK_EXISTING_PROVIDER');
  const provider = providers[0];
  need(r.schemaVersion === 1 && r.kind === REVIEW_KIND && r.evidenceKind === 'ACTUAL_PINNED_PARTIAL_REQUEST_REVIEW', 'FILE_SINK_ACTUAL_REQUEST_REVIEW');
  need(pinEqual(r.basePrepared, v.basePreparedPin), 'FILE_SINK_REVIEW_BASE_PIN');
  need(r.requestedIdentity === OBSERVED_FILE_REQUEST, 'FILE_SINK_EXACT_OBSERVED_REQUEST');
  need(descriptorEqual(r.provider, provider), 'FILE_SINK_REVIEW_PROVIDER');
  const cp = configPin(p, b.ownedRoot);
  need(pinEqual(cp, r.configuration) && samePath(b.args?.[6], cp.path), 'FILE_SINK_CONFIG_REVIEW_PIN');
  need(eq(config.Serilog?.Using, ['Serilog.Sinks.File']) && config.Serilog?.WriteTo?.length === 1 && config.Serilog.WriteTo[0].Name === 'File', 'FILE_SINK_FILE_ONLY_CONFIGURATION');
  need(typeof v.observedError === 'string' && v.observedError.includes("'" + OBSERVED_FILE_REQUEST + "'") && v.observedError.includes('AUTHORITY_REQUEST_NOT_APPROVED'), 'FILE_SINK_OBSERVATION_EXACT_REFUSAL');
  need(observation.exitCode === 2 && observation.expired === false && observation.launchFailure === null && observation.unknownLoaded?.length === 0 && samePath(observation.originalResult?.path, observedBase.resultPath), 'FILE_SINK_ACTUAL_OBSERVATION');
  // A fresh qualified base can relocate reviewed bytes; the original failed result remains immutable.
  need(observedPolicy.schemaVersion === 6 && configPin(observedPolicy, observedBase.ownedRoot).sha256 === cp.sha256 && observedPolicy.contexts?.length === 1 && observedPolicy.contexts[0].managed.some(x => x.identity === provider.identity && x.sha256 === provider.sha256 && x.role === provider.role), 'FILE_SINK_OBSERVATION_BYTE_LINEAGE');
  need(rawRow.identity === provider.identity && rawRow.sha256 === provider.sha256 && typeof rawRow.path === 'string', 'FILE_SINK_RAW_PROVIDER_IDENTITY');
  need(!d.requests.some(x => x.requestedIdentity === OBSERVED_FILE_REQUEST) && !p.exactAliases.some(x => x.requestedIdentity === OBSERVED_FILE_REQUEST), 'FILE_SINK_ALREADY_DECLARED');
  validateManagedInput(v);
  need(b.args?.length === 8 && samePath(b.args[0], b.host.path) && samePath(b.args[1], b.hc.path) && samePath(b.args[2], path.win32.join(b.ownedRoot, 'runtime')) && samePath(b.args[3], path.win32.join(b.ownedRoot, 'shared')) && samePath(b.args[4], b.runtimePins.path) && samePath(b.args[5], path.win32.join(b.ownedRoot, 'inputs', 'inventory.json')) && samePath(b.args[7], b.resultPath), 'FILE_SINK_EIGHT_ARGUMENTS');
  need(eq(argv.args, b.args) && argv.producer === b.artifacts.producer.path && argv.kind === 'JoinedFactoryProbe' && argv.argvSha256 === hash(JSON.stringify(b.args)), 'FILE_SINK_BASE_ARGV_HASH');
  need(b.expectedCaseIds?.length === 123 && new Set(b.expectedCaseIds).size === 123 && eq(Object.keys(b.failureMasks), ['first-start', 'readiness', 'continuation', 'native', 'native-path']) && eq(Object.values(b.failureMasks).map(x => x.length), [3, 6, 2, 4, 1]) && Object.values(b.failureMasks).flat().every(x => b.expectedCaseIds.includes(x)), 'FILE_SINK_ORIGINAL_IDS_MASKS');
  need(b.budgets?.business === 60000 && b.budgets?.parent === 360000 && b.initializeRole === 'UNKNOWN' && Array.isArray(b.unknownReferences) && b.nativeBoundaryLength === 259, 'FILE_SINK_ORIGINAL_BUDGET_UNKNOWN_NATIVE259');
  need(eq(b.metadataArguments, [b.artifacts.launcher.path, 'validate', b.policy.path, b.policy.sha256, b.metadataArguments?.[4]]) && b.metadataArguments.length === 5 && eq(b.businessArguments, [b.artifacts.launcher.path, 'run', b.policy.path, b.policy.sha256, b.businessArguments?.[4], ...b.args]), 'FILE_SINK_BASE_LAUNCH_ARGUMENTS');
  return {provider, configuration: cp};
}
function project(v, outDir, resultPath) {
  const {provider, configuration} = validateProjectionInput(v), b = v.base;
  need(path.win32.isAbsolute(outDir) && path.win32.isAbsolute(resultPath) && within(b.ownedRoot, outDir) && within(path.win32.join(b.ownedRoot, 'Build'), resultPath), 'FILE_SINK_OUTPUT_OWNERSHIP');
  need(samePath(path.win32.dirname(outDir), b.ownedRoot), 'FILE_SINK_OUTPUT_DIRECT_CHILD');
  need(path.win32.basename(resultPath) === 'result.json' && !samePath(resultPath, b.resultPath) && !within(outDir, resultPath) && !within(path.win32.dirname(resultPath), outDir), 'FILE_SINK_FRESH_CASE_LAYOUT');
  const nativeRoot = path.win32.join(path.win32.dirname(resultPath), 'n');
  need(nativeRoot.length < 239, 'FILE_SINK_NATIVE_ROOT_LT239');
  const policy = clone(v.policy);
  const alias = {requestedIdentity: OBSERVED_FILE_REQUEST, targetContext: 'Default', provider: clone(provider), requestingContexts: ['Default']};
  const request = {requestedIdentity: OBSERVED_FILE_REQUEST, targetContext: 'Default', targetPath: provider.path};
  policy.exactAliases.push(alias); policy.contexts[0].requests.push(request);
  const reversed = clone(policy); reversed.exactAliases.pop(); reversed.contexts[0].requests.pop();
  need(eq(reversed, v.policy), 'FILE_SINK_APPEND_ONLY_POLICY');
  return {policy, args: [...b.args.slice(0, 7), resultPath], alias, request, configuration, nativeRoot, metadataApprovalGranted: false, executionApprovalGranted: false, targetExecutions: 0};
}

/** In-memory projection only; not an admission or a runnable plan. */
export function projectJoinedConfiguredFileSinkMODEL_ONLY(bundle, {outDir, resultPath}) {
  need(bundle.evidenceKind === 'MODEL_ONLY', 'FILE_SINK_MODEL_MARKER_REQUIRED');
  return {...project(bundle, outDir, resultPath), evidenceKind: 'MODEL_ONLY', status: 'MODEL_ONLY_NOT_RUNTIME_EVIDENCE'};
}

/** In-memory qualification-graph control. No pin I/O, hash verification or actual admission. */
export function projectJoinedConfiguredFileSinkQualifiedMODEL_ONLY(bundle, {outDir, resultPath}) {
  need(bundle.evidenceKind === 'MODEL_ONLY' && Array.isArray(bundle.MODEL_ONLY_documents), 'FILE_SINK_MODEL_MARKER_REQUIRED');
  const readModel = pin => {
    const rows = bundle.MODEL_ONLY_documents.filter(x => pinEqual(x.pin, pin));
    need(rows.length === 1 && rows[0].MODEL_ONLY === true, 'FILE_SINK_MODEL_DOCUMENT_PIN'); return clone(rows[0].document);
  };
  const managedInput = qualifiedManagedInput({managedCount: bundle.managedCount, baseQualifiedInputsPin: bundle.MODEL_ONLY_qualifiedInputsPin}, bundle.base, bundle.policy, readModel, () => {}, true);
  return projectJoinedConfiguredFileSinkMODEL_ONLY({...bundle, managedInput}, {outDir, resultPath});
}

function readActual(options) {
  noModel(options);
  const verified = new Map();
  const verify = pin => {
    need(pin && path.win32.isAbsolute(pin.path ?? '') && /^[a-f0-9]{64}$/.test(pin.sha256 ?? ''), 'FILE_SINK_PIN_SHAPE');
    const key = norm(pin.path), prior = verified.get(key);
    need(!prior || prior.sha256 === pin.sha256, 'FILE_SINK_PIN_CONFLICT');
    need(fs.statSync(pin.path).isFile() && hash(fs.readFileSync(pin.path)) === pin.sha256, 'FILE_SINK_PIN_DRIFT:' + pin.path);
    verified.set(key, {path: pin.path, sha256: pin.sha256}); return pin;
  };
  const read = pin => { verify(pin); return JSON.parse(fs.readFileSync(pin.path, 'utf8')); };
  const verifyTree = value => {
    if (!value || typeof value !== 'object') return;
    if (typeof value.path === 'string' && typeof value.sha256 === 'string') verify(value);
    for (const item of Object.values(value)) if (item && typeof item === 'object') verifyTree(item);
  };
  const base = read(options.basePreparedPin), policy = read(base.policy), review = read(options.requestReviewPin);
  noModel(base); noModel(review);
  const config = read(review.configuration), observation = read(review.observation?.receipt), observedBase = read(review.observation?.basePrepared);
  const observedPolicy = read(observedBase.policy), raw = read(review.raw?.receipt);
  need(raw.status === 'PASS_RAW_SELECTED_METHOD_READ' && raw.targetCodeExecuted === false && raw.targetAssembliesObserved === false, 'FILE_SINK_RAW_NO_TARGET_EXECUTION');
  const rawRow = pointer(raw, review.raw.pointer);
  verify({path: rawRow.path, sha256: rawRow.sha256});
  const argv = read(base.argv);
  const managedInput = qualifiedManagedInput(options, base, policy, read, verifyTree);
  const v = {base, policy, review, config, observation, observedBase, observedPolicy, rawRow, argv, managedCount: options.managedCount, managedInput, basePreparedPin: options.basePreparedPin, observedError: pointer(observation, review.observation.pointer)};
  validateProjectionInput(v);
  const observedResult = read(observation.originalResult);
  need(eq(observedResult.cases?.map(x => x.id), observedBase.expectedCaseIds) && observedResult.cases.some(x => x.status === 'FAIL' && x.error === v.observedError) && eq(observedBase.expectedCaseIds, base.expectedCaseIds) && eq(observedBase.failureMasks, base.failureMasks), 'FILE_SINK_OBSERVATION_ORIGINAL_CASE_ERROR');
  verifyTree(base); verifyTree(policy); verifyTree(review); verify(observation.originalResult);
  for (const pin of [base.compileReceipt, base.originalCompileReceipt, base.oracleInputs]) verifyTree(read(pin));
  const runtime = read(base.runtimePins);
  need(runtime && !Array.isArray(runtime) && Object.keys(runtime).length > 0, 'FILE_SINK_RUNTIME_PINS');
  for (const [file, sha256] of Object.entries(runtime)) verify({path: file, sha256});
  const admission = read(options.baseMetadataAdmissionPin);
  need(admission.kind === 'onex-schema6-joined-metadata-admission' && admission.policySchemaVersion === 6 && pinEqual(admission.policy, base.policy) && pinEqual(admission.entry, base.artifacts.producer) && pinEqual(admission.launcher, base.artifacts.launcher) && pinEqual(admission.helper, base.artifacts.authority) && admission.sharedAuthority.sha256 === base.artifacts.authority.sha256 && pinEqual(admission.program, base.program) && samePath(admission.cwd, base.ownedRoot), 'FILE_SINK_BASE_ACTUAL_METADATA_BINDING');
  verifyTree(admission);
  const proc = read(admission.process), receipt = read(admission.receipt);
  need(proc.code === 0 && proc.naturalExit === true && proc.shell === false && proc.expired === false && proc.signal === null && proc.error === null && proc.budgetMs === 60000 && Number.isFinite(proc.durationMs) && proc.durationMs >= 0 && proc.durationMs <= 60000 && pinEqual(proc.program, base.program) && samePath(proc.cwd, base.ownedRoot) && eq(proc.args, base.metadataArguments) && samePath(proc.args[4], admission.receipt.path), 'FILE_SINK_ACTUAL_METADATA_PROCESS');
  need(receipt.exitCode === 0 && receipt.failure === null && receipt.productLoaded === false && eq(receipt.events, ['prepare-metadata-only']) && Array.isArray(receipt.actualLoadedManaged), 'FILE_SINK_METADATA_ONLY_RECEIPT');
  for (const pin of [base.artifacts.launcher, base.artifacts.authority]) need(receipt.actualLoadedManaged.some(x => pinEqual(x, pin)), 'FILE_SINK_METADATA_LAUNCHER_HELPER');
  verifyTree(receipt); verifyTree(proc);
  const recheck = () => { for (const pin of verified.values()) verify(pin); };
  return {v, recheck, verified};
}
/** Read-only validation; never launches CLR or writes into the base stage. */
export function inspectJoinedConfiguredFileSinkInputs(options) {
  const actual = readActual(options); actual.recheck();
  return {status: 'INPUTS_PINNED_NO_NEW_METADATA_OR_BUSINESS_EXECUTION', verifiedPins: [...actual.verified.values()], defaultManagedCount: actual.v.policy.contexts[0].managed.length, alias: {requestedIdentity: OBSERVED_FILE_REQUEST, provider: actual.v.review.provider}, executionApprovalGranted: false, targetExecutions: 0};
}
function noLinkAncestors(file) {
  let cursor = path.resolve(file);
  while (true) {
    if (fs.existsSync(cursor)) need(!fs.lstatSync(cursor).isSymbolicLink() && samePath(fs.realpathSync.native(cursor), cursor), 'FILE_SINK_OUTPUT_REPARSE_POINT');
    const parent = path.dirname(cursor); if (parent === cursor) break; cursor = parent;
  }
}
/** Fresh files only. Parent must independently validate the NEW policy with its supported authority. */
export function prepareJoinedConfiguredFileSink(options) {
  const actual = readActual(options), {v} = actual;
  const {outDir, resultPath} = options;
  const projected = project(v, outDir, resultPath), caseDir = path.dirname(resultPath);
  noLinkAncestors(outDir); noLinkAncestors(caseDir);
  need(!fs.existsSync(outDir) && !fs.existsSync(caseDir), 'FILE_SINK_OUTPUT_ALREADY_EXISTS');
  actual.recheck();
  fs.mkdirSync(outDir);
  fs.mkdirSync(path.dirname(caseDir), {recursive: true}); noLinkAncestors(caseDir); fs.mkdirSync(caseDir);
  const write = (name, value) => {
    const file = path.join(outDir, name), data = JSON.stringify(value, null, 2) + '\n';
    fs.writeFileSync(file, data, {flag: 'wx'}); return {path: file, sha256: hash(data)};
  };
  const policy = write('POLICY.json', projected.policy);
  const argvSha256 = hash(JSON.stringify(projected.args));
  const argv = write('business-argv.json', {schemaVersion: 1, kind: 'JoinedFactoryProbe', producer: v.base.artifacts.producer.path, args: projected.args, argvSha256, executed: false});
  const metadataReceipt = path.join(outDir, 'metadata-receipt.json'), businessReceipt = path.join(outDir, 'business-launch.json');
  const plan = {...clone(v.base), kind: 'joined-configured-file-sink-prepared', status: 'PREPARED_NEW_POLICY_METADATA_ADMISSION_PENDING', policy, argv, args: projected.args, resultPath, metadataArguments: [v.base.artifacts.launcher.path, 'validate', policy.path, policy.sha256, metadataReceipt], businessArguments: [v.base.artifacts.launcher.path, 'run', policy.path, policy.sha256, businessReceipt, ...projected.args], argvSha256, basePrepared: options.basePreparedPin, baseMetadataAdmission: options.baseMetadataAdmissionPin, requestReview: options.requestReviewPin, baseQualifiedInputs: options.baseQualifiedInputsPin, inputManagedCount: options.managedCount, configuredFileSink: projected.alias, preparationOutputRoot: outDir, nativeProbeRoot: projected.nativeRoot, metadataApprovalGranted: false, executionApprovalGranted: false, targetExecutions: 0};
  const prepared = write('PREPARED-PLAN.json', plan);
  actual.recheck();
  const receipt = write('PREPARE-RECEIPT.json', {schemaVersion: 1, kind: 'joined-configured-file-sink-prepare-only', prepared, policy, argv, beforeAndAfterPins: [...actual.verified.values()], preserved: {managed: v.managedCount, frameworkAliases: 5, originalCaseIds: 123, businessArguments: 8, businessBudgetMs: 60000, parentBudgetMs: 360000, inputMetadata: v.policy.metadata.length}, newMetadataExecuted: false, productExecuted: false, executionApprovalGranted: false, unknownDynamicFinder: 'UNKNOWN_NOT_ADMITTED'});
  return {prepared, policy, argv, receipt, status: plan.status, metadataArguments: plan.metadataArguments, businessArguments: plan.businessArguments, executionApprovalGranted: false};
}
