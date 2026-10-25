import test from 'node:test';
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import {
  OBSERVED_FILE_REQUEST, FILE_PROVIDER_IDENTITY, FILE_PROVIDER_SHA256,
  projectJoinedConfiguredFileSinkMODEL_ONLY, inspectJoinedConfiguredFileSinkInputs,
  prepareJoinedConfiguredFileSink,
} from './prepare-joined-configured-file-sink.mjs';

const sha = x => crypto.createHash('sha256').update(x).digest('hex');
const root = 'D:\\MODEL_ONLY\\joined';
const pin = (path, sha256 = 'a'.repeat(64)) => ({path, sha256});
const out = {outDir: root + '\\file-binding', resultPath: root + '\\Build\\file-binding\\case\\result.json'};
function fixture() {
  const producer = {...pin(root + '\\tools\\JoinedFactoryProbe.dll'), identity: 'JoinedFactoryProbe, Version=1.0.0.0, Culture=neutral, PublicKeyToken=null'};
  const authority = pin(root + '\\tools\\STA\\ScopedLoadAuthority.dll');
  const launcher = pin(root + '\\tools\\STA\\STA.dll');
  const provider = {...pin(root + '\\runtime\\Serilog.Sinks.File.dll', FILE_PROVIDER_SHA256), identity: FILE_PROVIDER_IDENTITY, role: 'target'};
  const cp = pin(root + '\\inputs\\YeManFanHost.json');
  const requests = [
    'System.Runtime.InteropServices, Version=8.0.0.0, Culture=neutral, PublicKeyToken=b03f5f7f11d50a3a',
    'System.Runtime, Version=8.0.0.0, Culture=neutral, PublicKeyToken=b03f5f7f11d50a3a',
    'System.Collections.Concurrent, Version=8.0.0.0, Culture=neutral, PublicKeyToken=b03f5f7f11d50a3a',
    'System.Runtime, Version=6.0.0.0, Culture=neutral, PublicKeyToken=b03f5f7f11d50a3a',
    'netstandard, Version=2.0.0.0, Culture=neutral, PublicKeyToken=cc7b13ffcd2ddd51',
  ];
  const framework = [...new Set(requests.map(x => x.split(',')[0]))].map(name => ({...pin('C:\\MODEL_ONLY\\core10\\' + name + '.dll'), identity: name + ', Version=10.0.0.0, Culture=neutral, PublicKeyToken=b03f5f7f11d50a3a', role: 'framework'}));
  const managed = [{...producer, role: 'tool'}, provider, {...authority, identity: 'ScopedLoadAuthority, Version=0.0.0.0, Culture=neutral, PublicKeyToken=null', role: 'infrastructure'}, ...framework];
  while (managed.length < 74) managed.push({...pin(root + '\\runtime\\MODEL_ONLY_' + managed.length + '.dll'), identity: 'MODEL_ONLY_' + managed.length + ', Version=1.0.0.0, Culture=neutral, PublicKeyToken=null', role: 'target'});
  const aliases = requests.map(requestedIdentity => ({requestedIdentity, targetContext: 'Default', provider: framework.find(x => x.identity.startsWith(requestedIdentity.split(',')[0] + ',')), requestingContexts: ['Default']}));
  const policyPin = pin(root + '\\POLICY.json');
  const policy = {schemaVersion: 6, ownedRoot: root, entry: producer.path, contexts: [{id: 'Default', kind: 'default', registrationType: null, registrationAssembly: null, managed, requests: [{requestedIdentity: provider.identity, targetContext: 'Default', targetPath: provider.path}]}], metadata: [cp], exactAliases: aliases};
  const ids = Array.from({length: 123}, (_, i) => 'MODEL_ONLY:case:' + i);
  const base = {kind: 'joined-original123-source-prepared', profile: 'joined-original123-schema6-single-default', role: 'positive', ownedRoot: root, artifacts: {producer, authority, producerAuthority: {...authority, path: root + '\\tools\\producer\\ScopedLoadAuthority.dll'}, launcher}, policy: policyPin, host: pin(root + '\\host\\Host.dll'), hc: pin(root + '\\runtime\\HC.dll'), runtimePins: pin(root + '\\runtime-pins.json'), resultPath: root + '\\Build\\old\\result.json', budgets: {business: 60000, parent: 360000, generation: 30000, compile: 120000}, expectedCaseIds: ids, failureMasks: {'first-start': ids.slice(0, 3), readiness: ids.slice(3, 9), continuation: ids.slice(9, 11), native: ids.slice(11, 15), 'native-path': ids.slice(15, 16)}, initializeRole: 'UNKNOWN', unknownReferences: [{status: 'UNKNOWN_NOT_ADMITTED'}], nativeBoundaryLength: 259};
  base.args = [base.host.path, base.hc.path, root + '\\runtime', root + '\\shared', base.runtimePins.path, root + '\\inputs\\inventory.json', cp.path, base.resultPath];
  base.metadataArguments = [launcher.path, 'validate', policyPin.path, policyPin.sha256, root + '\\metadata-receipt.json'];
  base.businessArguments = [launcher.path, 'run', policyPin.path, policyPin.sha256, root + '\\business-launch.json', ...base.args];
  policy.contexts[0].requests = [...managed.map(p => ({requestedIdentity: p.identity, targetContext: 'Default', targetPath: p.path})), ...aliases.map(a => ({requestedIdentity: a.requestedIdentity, targetContext: 'Default', targetPath: a.provider.path}))];
  const basePreparedPin = pin(root + '\\PREPARED-PLAN.json');
  const review = {schemaVersion: 1, kind: 'joined-configured-file-sink-request-review', evidenceKind: 'ACTUAL_PINNED_PARTIAL_REQUEST_REVIEW', basePrepared: basePreparedPin, requestedIdentity: OBSERVED_FILE_REQUEST, provider: structuredClone(provider), configuration: cp};
  return {evidenceKind: 'MODEL_ONLY', managedCount: 74, managedInput: {managedCount: 74, managed: structuredClone(managed), frameworkExtensions: [], entries: []}, base, policy, review, config: {Serilog: {Using: ['Serilog.Sinks.File'], WriteTo: [{Name: 'File', Args: {path: 'MODEL_ONLY.log'}}]}}, observation: {exitCode: 2, expired: false, launchFailure: null, unknownLoaded: [], originalResult: {path: base.resultPath}}, observedBase: structuredClone(base), observedPolicy: structuredClone(policy), rawRow: {...provider, path: 'D:\\MODEL_ONLY\\prior\\Serilog.Sinks.File.dll'}, argv: {kind: 'JoinedFactoryProbe', producer: producer.path, args: [...base.args], argvSha256: sha(JSON.stringify(base.args))}, basePreparedPin, observedError: "Could not load '" + OBSERVED_FILE_REQUEST + "' AUTHORITY_REQUEST_NOT_APPROVED"};
}
const project = (v, options = out) => projectJoinedConfiguredFileSinkMODEL_ONLY(v, options);
function reject(name, mutate, code, changeOut) {
  test('MODEL_ONLY rejects ' + name, () => {
    const v = fixture(); mutate(v); assert.throws(() => project(v, changeOut ?? out), error => error.message === code, code);
  });
}

test('MODEL_ONLY exact append preserves every original policy byte projection and all arguments except output', () => {
  const v = fixture(), before = JSON.stringify(v), p = project(v);
  assert.equal(JSON.stringify(v), before);
  assert.equal(p.evidenceKind, 'MODEL_ONLY'); assert.equal(p.executionApprovalGranted, false); assert.equal(p.targetExecutions, 0);
  assert.deepEqual(p.policy.contexts[0].managed, v.policy.contexts[0].managed);
  assert.deepEqual(p.policy.metadata, v.policy.metadata);
  assert.deepEqual(p.policy.exactAliases.slice(0, -1), v.policy.exactAliases);
  assert.deepEqual(p.policy.contexts[0].requests.slice(0, -1), v.policy.contexts[0].requests);
  const reversed = structuredClone(p.policy); reversed.exactAliases.pop(); reversed.contexts[0].requests.pop(); assert.deepEqual(reversed, v.policy);
  assert.deepEqual(p.args.slice(0, 7), v.base.args.slice(0, 7)); assert.equal(p.args.length, 8); assert.equal(p.args[7], out.resultPath);
  assert.deepEqual(p.alias.requestingContexts, ['Default']); assert.equal(p.alias.provider.role, 'target');
  assert.equal(p.request.requestedIdentity, OBSERVED_FILE_REQUEST); assert.equal(p.nativeRoot.endsWith('\\case\\n'), true);
});
test('MODEL_ONLY explicitly reviewed same-byte relocation retains original observation', () => {
  const v = fixture(); v.observedBase.resultPath = 'D:\\MODEL_ONLY\\oldstage\\Build\\case\\result.json'; v.observation.originalResult.path = v.observedBase.resultPath;
  v.observedPolicy.contexts[0].managed.find(x => x.identity === FILE_PROVIDER_IDENTITY).path = 'D:\\MODEL_ONLY\\oldstage\\runtime\\Serilog.Sinks.File.dll';
  assert.equal(project(v).alias.provider.path, v.review.provider.path);
});
reject('missing model marker', v => {delete v.evidenceKind;}, 'FILE_SINK_MODEL_MARKER_REQUIRED');
reject('schema5', v => {v.policy.schemaVersion = 5;}, 'FILE_SINK_SCHEMA6_OWNED_ROOT');
reject('fault base profile', v => {v.base.role = 'native';}, 'FILE_SINK_VARIANT_EXPECTATION');
reject('custom context', v => {v.policy.contexts.push(structuredClone(v.policy.contexts[0]));}, 'FILE_SINK_SINGLE_DEFAULT');
reject('nonnull registration', v => {v.policy.contexts[0].registrationType = 'Any';}, 'FILE_SINK_DEFAULT_REGISTRATION');
reject('managed pin removal', v => {v.policy.contexts[0].managed.pop();}, 'FILE_SINK_MANAGED_COUNT');
reject('framework alias removal', v => {v.policy.exactAliases.pop();}, 'FILE_SINK_KEEP_FIVE_FRAMEWORK_ALIASES');
reject('framework alias cross context', v => {v.policy.exactAliases[0].requestingContexts = ['Other'];}, 'FILE_SINK_FRAMEWORK_ALIAS_BINDING');
reject('changed entry', v => {v.policy.entry = 'D:\\Else.dll';}, 'FILE_SINK_ENTRY');
reject('wrong authority copy', v => {v.base.artifacts.producerAuthority.sha256 = 'b'.repeat(64);}, 'FILE_SINK_SHARED_AUTHORITY');
reject('wrong Default authority pin', v => {v.policy.contexts[0].managed[2].sha256 = 'b'.repeat(64);}, 'FILE_SINK_AUTHORITY_DEFAULT_PIN');
reject('wrong full provider identity', v => {v.policy.contexts[0].managed[1].identity = FILE_PROVIDER_IDENTITY.replace('7.0.0.0', '8.0.0.0');}, 'FILE_SINK_EXISTING_PROVIDER');
reject('provider framework role', v => {v.policy.contexts[0].managed[1].role = 'framework';}, 'FILE_SINK_EXISTING_PROVIDER');
reject('wrong provider SHA', v => {v.policy.contexts[0].managed[1].sha256 = 'b'.repeat(64);}, 'FILE_SINK_EXISTING_PROVIDER');
reject('provider outside runtime', v => {v.policy.contexts[0].managed[1].path = 'D:\\Else.dll';}, 'FILE_SINK_EXISTING_PROVIDER');
reject('model claim as actual review', v => {v.review.evidenceKind = 'MODEL_ONLY';}, 'FILE_SINK_ACTUAL_REQUEST_REVIEW');
reject('wrong base pin', v => {v.review.basePrepared = {...v.review.basePrepared, sha256: 'b'.repeat(64)};}, 'FILE_SINK_REVIEW_BASE_PIN');
reject('plain simple-name request', v => {v.review.requestedIdentity = 'Serilog.Sinks.File';}, 'FILE_SINK_EXACT_OBSERVED_REQUEST');
reject('version-bearing request', v => {v.review.requestedIdentity = FILE_PROVIDER_IDENTITY;}, 'FILE_SINK_EXACT_OBSERVED_REQUEST');
reject('wildcard request', v => {v.review.requestedIdentity = 'Serilog.Sinks.*';}, 'FILE_SINK_EXACT_OBSERVED_REQUEST');
reject('review provider token change', v => {v.review.provider.identity = FILE_PROVIDER_IDENTITY.replace('24c2f752a8e58a10', 'null');}, 'FILE_SINK_REVIEW_PROVIDER');
reject('config not metadata pinned', v => {v.policy.metadata = [];}, 'FILE_SINK_CONFIG_METADATA_EXACT_PIN');
reject('ambiguous config metadata', v => {v.policy.metadata.push({...v.policy.metadata[0]});}, 'FILE_SINK_CONFIG_METADATA_EXACT_PIN');
reject('wrong config review SHA', v => {v.review.configuration = {...v.review.configuration, sha256: 'b'.repeat(64)};}, 'FILE_SINK_CONFIG_REVIEW_PIN');
reject('extra Using', v => {v.config.Serilog.Using.push('Other');}, 'FILE_SINK_FILE_ONLY_CONFIGURATION');
reject('duplicate Using', v => {v.config.Serilog.Using.push('Serilog.Sinks.File');}, 'FILE_SINK_FILE_ONLY_CONFIGURATION');
reject('additional sink', v => {v.config.Serilog.WriteTo.push({Name: 'Console'});}, 'FILE_SINK_FILE_ONLY_CONFIGURATION');
reject('non-File sink', v => {v.config.Serilog.WriteTo[0].Name = 'Async';}, 'FILE_SINK_FILE_ONLY_CONFIGURATION');
reject('observation non-string', v => {v.observedError = {};}, 'FILE_SINK_OBSERVATION_EXACT_REFUSAL');
reject('wrong observed token', v => {v.observedError = "'Serilog.Sinks.File, Culture=neutral, PublicKeyToken=24c2f752a8e58a10' AUTHORITY_REQUEST_NOT_APPROVED";}, 'FILE_SINK_OBSERVATION_EXACT_REFUSAL');
reject('unrelated refusal', v => {v.observedError = "'" + OBSERVED_FILE_REQUEST + "' PIN_MISMATCH";}, 'FILE_SINK_OBSERVATION_EXACT_REFUSAL');
reject('invented observation success', v => {v.observation.exitCode = 0;}, 'FILE_SINK_ACTUAL_OBSERVATION');
reject('unknown loaded graph', v => {v.observation.unknownLoaded.push({});}, 'FILE_SINK_ACTUAL_OBSERVATION');
reject('observed config differs', v => {v.observedPolicy.metadata[0].sha256 = 'b'.repeat(64);}, 'FILE_SINK_OBSERVATION_BYTE_LINEAGE');
reject('raw byte mismatch', v => {v.rawRow.sha256 = 'b'.repeat(64);}, 'FILE_SINK_RAW_PROVIDER_IDENTITY');
reject('already existing request', v => {v.policy.contexts[0].requests.push({requestedIdentity: OBSERVED_FILE_REQUEST});}, 'FILE_SINK_ALREADY_DECLARED');
reject('9 consumer args', v => {v.base.args.push('public');}, 'FILE_SINK_EIGHT_ARGUMENTS');
reject('wrong inventory argument', v => {v.base.args[5] = 'D:\\Else.json';}, 'FILE_SINK_EIGHT_ARGUMENTS');
reject('bad old argv hash', v => {v.argv.argvSha256 = 'b'.repeat(64);}, 'FILE_SINK_BASE_ARGV_HASH');
reject('count only case duplicates', v => {v.base.expectedCaseIds[1] = v.base.expectedCaseIds[0];}, 'FILE_SINK_ORIGINAL_IDS_MASKS');
reject('mask wrong size', v => {v.base.failureMasks.native.pop();}, 'FILE_SINK_ORIGINAL_IDS_MASKS');
reject('mask unknown ID', v => {v.base.failureMasks.native[0] = 'unlisted';}, 'FILE_SINK_ORIGINAL_IDS_MASKS');
reject('budget changed', v => {v.base.budgets.business = 61000;}, 'FILE_SINK_ORIGINAL_BUDGET_UNKNOWN_NATIVE259');
reject('initialize assumed', v => {v.base.initializeRole = 'PASS';}, 'FILE_SINK_ORIGINAL_BUDGET_UNKNOWN_NATIVE259');
reject('native259 changed', v => {v.base.nativeBoundaryLength = 239;}, 'FILE_SINK_ORIGINAL_BUDGET_UNKNOWN_NATIVE259');
reject('base launch changed', v => {v.base.businessArguments[1] = 'validate';}, 'FILE_SINK_BASE_LAUNCH_ARGUMENTS');
reject('outDir escapes', () => {}, 'FILE_SINK_OUTPUT_OWNERSHIP', {...out, outDir: 'D:\\outside'});
reject('result outside Build', () => {}, 'FILE_SINK_OUTPUT_OWNERSHIP', {...out, resultPath: root + '\\Else\\result.json'});
reject('reused result name', () => {}, 'FILE_SINK_FRESH_CASE_LAYOUT', {...out, resultPath: root + '\\Build\\old\\result.json'});
reject('wrong result filename', () => {}, 'FILE_SINK_FRESH_CASE_LAYOUT', {...out, resultPath: root + '\\Build\\new\\other.json'});
reject('overlapping outputs', () => {}, 'FILE_SINK_OUTPUT_DIRECT_CHILD', {outDir: root + '\\Build\\new', resultPath: root + '\\Build\\new\\case\\result.json'});
reject('native root length >=239', () => {}, 'FILE_SINK_NATIVE_ROOT_LT239', {...out, resultPath: root + '\\Build\\' + 'x'.repeat(239) + '\\result.json'});
test('actual inspection rejects MODEL_ONLY path before reading or writing', () => {
  assert.throws(() => inspectJoinedConfiguredFileSinkInputs({basePreparedPin: pin(root + '\\PREPARED-PLAN.json')}), /FILE_SINK_ACTUAL_REJECTS_MODEL/);
});
test('actual prepare rejects MODEL_ONLY path before reading or writing', () => {
  assert.throws(() => prepareJoinedConfiguredFileSink({basePreparedPin: pin(root + '\\PREPARED-PLAN.json'), ...out}), /FILE_SINK_ACTUAL_REJECTS_MODEL/);
});


// Additional controls keep the original 56 cases above; these are not CLR evidence.
function extensionFixture(count = 3) {
  const v = fixture(), d = v.policy.contexts[0];
  const ext = Array.from({length: count}, (_, i) => ({...pin('C:\\MODEL_ONLY\\core10\\MODEL_EXTENSION_' + i + '.dll'), identity: 'MODEL_EXTENSION_' + i + ', Version=10.0.0.0, Culture=neutral, PublicKeyToken=b03f5f7f11d50a3a', role: 'framework'}));
  d.managed.push(...ext); d.requests = [...d.managed.map(p => ({requestedIdentity: p.identity, targetContext: 'Default', targetPath: p.path})), ...v.policy.exactAliases.map(a => ({requestedIdentity: a.requestedIdentity, targetContext: 'Default', targetPath: a.provider.path}))];
  v.managedCount = d.managed.length;
  v.managedInput = {managedCount: v.managedCount, managed: structuredClone(d.managed), frameworkExtensions: structuredClone(ext), entries: ext.map(p => ({provider: structuredClone(p), classificationRole: 'framework', observed: structuredClone(p), classified: structuredClone(p)}))};
  return v;
}
function rejectExtension(name, mutate, code) {
  test('MODEL_ONLY Default77 rejects ' + name, () => {
    const v = extensionFixture(); mutate(v); assert.throws(() => project(v), e => e.message === code, code);
  });
}
test('MODEL_ONLY Default77 preserves all exact descriptors, input metadata and five-to-six aliases', () => {
  const v = extensionFixture(), before = JSON.stringify(v), p = project(v);
  assert.equal(p.policy.contexts[0].managed.length, 77); assert.deepEqual(p.policy.contexts[0].managed, v.managedInput.managed);
  assert.deepEqual(p.policy.metadata, v.policy.metadata); assert.equal(p.policy.exactAliases.length, 6);
  assert.equal(p.policy.exactAliases.filter(x => x.requestedIdentity === OBSERVED_FILE_REQUEST).length, 1);
  assert.deepEqual(p.policy.exactAliases.slice(0, 5), v.policy.exactAliases);
  assert.deepEqual(p.policy.contexts[0].requests.slice(0, -1), v.policy.contexts[0].requests);
  assert.equal(JSON.stringify(v), before); assert.equal(p.metadataApprovalGranted, false); assert.equal(p.executionApprovalGranted, false);
});
test('MODEL_ONLY count is exact input and not an assembly-name allowlist', () => {
  const v = extensionFixture(5); assert.equal(project(v).policy.contexts[0].managed.length, 79);
});
rejectExtension('one pin missing with declared77', v => {v.policy.contexts[0].managed.pop();}, 'FILE_SINK_MANAGED_COUNT');
rejectExtension('count silently lowered with missing qualified pin', v => {v.policy.contexts[0].managed.pop(); v.managedCount--; v.managedInput.managedCount--;}, 'FILE_SINK_MANAGED_EXACT_QUALIFIED_SET');
rejectExtension('same-count unknown identity replacement', v => {v.policy.contexts[0].managed.at(-1).identity = 'Unknown, Version=10.0.0.0, Culture=neutral, PublicKeyToken=null';}, 'FILE_SINK_MANAGED_EXACT_QUALIFIED_SET');
rejectExtension('same-count path replacement', v => {v.policy.contexts[0].managed.at(-1).path = 'D:\\MODEL_ONLY\\else.dll';}, 'FILE_SINK_MANAGED_EXACT_QUALIFIED_SET');
rejectExtension('same-count SHA replacement', v => {v.policy.contexts[0].managed.at(-1).sha256 = 'b'.repeat(64);}, 'FILE_SINK_MANAGED_EXACT_QUALIFIED_SET');
rejectExtension('same-count role replacement', v => {v.policy.contexts[0].managed.at(-1).role = 'target';}, 'FILE_SINK_MANAGED_EXACT_QUALIFIED_SET');
rejectExtension('unqualified appended nonframework', v => {const row = {...v.policy.contexts[0].managed.at(-1), path: 'D:\\MODEL_ONLY\\extra.dll', identity: 'Extra, Version=1.0.0.0, Culture=neutral, PublicKeyToken=null', role: 'target'}; v.policy.contexts[0].managed.push(row); v.managedCount++; v.managedInput.managedCount++;}, 'FILE_SINK_MANAGED_EXACT_QUALIFIED_SET');
rejectExtension('framework extension relabelled target even if declared in full set', v => {v.policy.contexts[0].managed.at(-1).role = 'target'; v.managedInput.managed.at(-1).role = 'target'; v.managedInput.frameworkExtensions.at(-1).role = 'target';}, 'FILE_SINK_EXTENSION_EXACT_FRAMEWORK');
rejectExtension('missing extension observation', v => {v.managedInput.entries.pop();}, 'FILE_SINK_FRAMEWORK_EXTENSION_SET');
rejectExtension('wrong observed identity', v => {v.managedInput.entries[0].observed.identity = 'Wrong, Version=10.0.0.0, Culture=neutral, PublicKeyToken=null';}, 'FILE_SINK_EXTENSION_OBSERVATION_CLASSIFICATION');
rejectExtension('wrong observed path', v => {v.managedInput.entries[0].observed.path = 'D:\\MODEL_ONLY\\else.dll';}, 'FILE_SINK_EXTENSION_OBSERVATION_CLASSIFICATION');
rejectExtension('wrong observed SHA', v => {v.managedInput.entries[0].observed.sha256 = 'b'.repeat(64);}, 'FILE_SINK_EXTENSION_OBSERVATION_CLASSIFICATION');
rejectExtension('wrong raw identity', v => {v.managedInput.entries[0].classified.identity = 'Wrong, Version=10.0.0.0, Culture=neutral, PublicKeyToken=null';}, 'FILE_SINK_EXTENSION_OBSERVATION_CLASSIFICATION');
rejectExtension('raw classification is target', v => {v.managedInput.entries[0].classificationRole = 'target';}, 'FILE_SINK_EXTENSION_OBSERVATION_CLASSIFICATION');
rejectExtension('missing basis', v => {delete v.managedInput;}, 'FILE_SINK_MANAGED_EXACT_QUALIFIED_SET');
rejectExtension('nonnumeric count', v => {v.managedCount = '77';}, 'FILE_SINK_MANAGED_COUNT');
rejectExtension('noninteger count', v => {v.managedCount = 77.5;}, 'FILE_SINK_MANAGED_COUNT');
rejectExtension('zero count', v => {v.managedCount = 0;}, 'FILE_SINK_MANAGED_COUNT');
rejectExtension('count above3072', v => {v.managedCount = 3073;}, 'FILE_SINK_MANAGED_COUNT');
rejectExtension('route removed', v => {v.policy.contexts[0].requests.splice(-6, 1);}, 'FILE_SINK_EXACT_DEFAULT_REQUEST_ROUTES');
rejectExtension('extra unknown request', v => {v.policy.contexts[0].requests.push({requestedIdentity: 'Unknown', targetContext: 'Default', targetPath: 'D:\\unknown.dll'});}, 'FILE_SINK_EXACT_DEFAULT_REQUEST_ROUTES');
rejectExtension('route wrong target context', v => {v.policy.contexts[0].requests[0].targetContext = 'Product';}, 'FILE_SINK_EXACT_DEFAULT_REQUEST_ROUTES');
rejectExtension('route wrong provider path', v => {v.policy.contexts[0].requests[0].targetPath = 'D:\\MODEL_ONLY\\wrong.dll';}, 'FILE_SINK_EXACT_DEFAULT_REQUEST_ROUTES');
rejectExtension('duplicate requested identity', v => {v.policy.contexts[0].requests[0] = {...v.policy.contexts[0].requests[1]};}, 'FILE_SINK_EXACT_DEFAULT_REQUEST_ROUTES');
rejectExtension('noncanonical full identity even when basis repeats it', v => {v.policy.contexts[0].managed.at(-1).identity = 'Partial'; v.managedInput.managed.at(-1).identity = 'Partial';}, 'FILE_SINK_MANAGED_FULL_DESCRIPTOR');
rejectExtension('malformed SHA even when basis repeats it', v => {v.policy.contexts[0].managed.at(-1).sha256 = '*'; v.managedInput.managed.at(-1).sha256 = '*';}, 'FILE_SINK_MANAGED_FULL_DESCRIPTOR');
rejectExtension('relative path even when basis repeats it', v => {v.policy.contexts[0].managed.at(-1).path = 'relative.dll'; v.managedInput.managed.at(-1).path = 'relative.dll';}, 'FILE_SINK_MANAGED_FULL_DESCRIPTOR');
rejectExtension('duplicate exact identity', v => {v.policy.contexts[0].managed.at(-1).identity = v.policy.contexts[0].managed.at(-2).identity; v.managedInput.managed.at(-1).identity = v.policy.contexts[0].managed.at(-1).identity;}, 'FILE_SINK_MANAGED_UNIQUE');
rejectExtension('duplicate normalized path', v => {v.policy.contexts[0].managed.at(-1).path = v.policy.contexts[0].managed.at(-2).path; v.managedInput.managed.at(-1).path = v.policy.contexts[0].managed.at(-1).path;}, 'FILE_SINK_MANAGED_UNIQUE');

for(const role of ['first-start','readiness','continuation','native','native-path'])test('MODEL_ONLY original qualified role '+role+' retains exact mask and exit',()=>{const v=fixture();v.base.role=role;v.base.expectedExitCode=2;v.base.expectedFailureMask=structuredClone(v.base.failureMasks[role]);v.base.variantQualification=pin(root+'\\variant-'+role+'.json');const r=project(v);assert.deepEqual(r.policy.contexts[0].managed,v.policy.contexts[0].managed);assert.equal(r.alias.requestedIdentity,OBSERVED_FILE_REQUEST);});
reject('unknown original role',v=>v.base.role='initialize','FILE_SINK_BASE_PROFILE');
reject('variant missing qualification',v=>{v.base.role='native';v.base.expectedExitCode=2;v.base.expectedFailureMask=v.base.failureMasks.native;},'FILE_SINK_VARIANT_EXPECTATION');
reject('variant wrong expectation',v=>{v.base.role='native';v.base.expectedExitCode=0;v.base.expectedFailureMask=v.base.failureMasks.native;v.base.variantQualification=pin(root+'\\variant.json');},'FILE_SINK_VARIANT_EXPECTATION');
reject('variant wrong exact mask',v=>{v.base.role='native';v.base.expectedExitCode=2;v.base.expectedFailureMask=[];v.base.variantQualification=pin(root+'\\variant.json');},'FILE_SINK_VARIANT_EXPECTATION');
