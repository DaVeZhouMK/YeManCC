import {createHash} from 'node:crypto';
import test from 'node:test';import assert from 'node:assert/strict';import {validateModelReport} from './run.mjs';
const groups={'capture-gate-exact-five-keys':1579,'identity-selector-catalog-profile':2927,'legacy-80-identities-profile-numbers-and-canonical':882,'matrix-remap-independent-helper':315,'oem.keys.get-lambda-return-and-log':1660,'onex-new-models-never-x1':51,'power-lifecycle-and-aoac':40,'profile-evaluator-press-release-combinations':3126,'steamdeck-unimplemented-capture-gate':336};
const fixture=()=>({status:'pass',configurations:['Release','Checked'].map(name=>({name,status:'pass',checks:10916,counts:{...groups},executableSha256:'a'.repeat(64)})),mutations:Array.from({length:10},(_,i)=>({name:String(i),status:'detected',failure:{status:'fail',section:'specific-boundary'},expectedSection:'specific-boundary'})),sourceChecks:[...['onexCapDescriptorFor','ymccDeviceClassSupportsLighting','nativeKeyboardShortcutMaskBit','oemBackHookVkBit','OneX BUS hardware admission'].map(name=>({name,unchanged:true,comparison:'CRLF-to-LF-only',beforeRawSha256:'a'.repeat(64),currentRawSha256:'b'.repeat(64),beforeNormalizedSha256:'c'.repeat(64),currentNormalizedSha256:'c'.repeat(64)})),{name:'ymccDeviceClassLightingSet',unchanged:true,comparison:'exact-identity-set; generated-order-independent',beforeRawSha256:'a'.repeat(64),currentRawSha256:'b'.repeat(64),beforeIdentities:['HandheldCompanion.Devices.ROGAlly'],currentIdentities:['HandheldCompanion.Devices.ROGAlly'],identitySetSha256:createHash('sha256').update(JSON.stringify(['HandheldCompanion.Devices.ROGAlly'])).digest('hex')}],sourceAtFinish:{wholeFileDrift:false,extractedBindings:[{unchanged:true}]},integrity:{inputs:Array.from({length:162},()=>({}))},hcCommit:'a250b19a4b7e29dac3b383f2d1ac517d1af43e4e'});
const expected=()=>({models:Array(10).fill({}),identities:Array(63).fill({}),legacyIdentities:Array(181).fill({})});
test('both complete original configurations and all semantic rejection boundaries are required',()=>assert.equal(validateModelReport(fixture(),expected()).status,'PASS'));
for(const[name,mutate]of [['generation is not execution',r=>r.status='generated-only-not-validated'],['one build is incomplete',r=>r.configurations.pop()],['missing behavior group',r=>delete r.configurations[0].counts['power-lifecycle-and-aoac']],['summary count cannot replace assertions',r=>r.configurations[0].checks++],['invalid compilation is not semantic rejection',r=>r.mutations[0].failure.section='compile-error'],['partial legacy set',r=>r.integrity.inputs.pop()],['changed legacy gate',r=>r.sourceChecks[0].unchanged=false],['current source drift',r=>r.sourceAtFinish.wholeFileDrift=true]])test(name,()=>{const r=fixture();mutate(r);assert.equal(validateModelReport(r,expected()).status,'FAIL');});
test('partial model/alias inventory cannot become full source matrix',()=>{const e=expected();e.legacyIdentities.pop();assert.equal(validateModelReport(fixture(),e).status,'FAIL');});

for(const [name,mutate]of [
 ['missing lighting entry',r=>r.sourceChecks[5].currentIdentities.pop()],
 ['extra lighting entry',r=>r.sourceChecks[5].currentIdentities.push('HandheldCompanion.Devices.SteamDeck')],
 ['duplicate lighting entry',r=>r.sourceChecks[5].currentIdentities.push('HandheldCompanion.Devices.ROGAlly')],
 ['changed raw normalized content despite unchanged boolean',r=>r.sourceChecks[0].currentNormalizedSha256='0'.repeat(64)],
 ['arbitrary sixth source item cannot impersonate lighting proof',r=>r.sourceChecks[5].name='otherModelSet'],
 ['changed lighting identity despite unchanged boolean',r=>r.sourceChecks[5].currentIdentities[0]='HandheldCompanion.Devices.SteamDeck'],
 ['missing lighting set proof',r=>delete r.sourceChecks[5].beforeIdentities],
 ['forged identity set digest',r=>r.sourceChecks[5].identitySetSha256='0'.repeat(64)],
 ['unreviewed order-independent raw gate',r=>r.sourceChecks[0].comparison='exact-identity-set; generated-order-independent'],
 ['duplicate source boundary cannot replace required gate',r=>r.sourceChecks[0].name=r.sourceChecks[1].name],
 ['missing raw source identity',r=>delete r.sourceChecks[5].beforeRawSha256],
])test(name,()=>{const r=fixture();mutate(r);assert.equal(validateModelReport(r,expected()).status,'FAIL');});
