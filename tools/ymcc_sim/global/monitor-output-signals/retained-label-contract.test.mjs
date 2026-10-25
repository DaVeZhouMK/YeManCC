import test from 'node:test';
import assert from 'node:assert/strict';
import {OLD_LABEL_ASSERT,NEW_LABEL_ASSERT} from './preset-contract.mjs';
import {classifyTemplateLabelContract,verifyTemplateLabelOutcome} from './retained-label-contract.mjs';
const title='Default simple preset uses capsule/Tasha and the screenshot selection';
function pair(legacy=false){const proof=classifyTemplateLabelContract((legacy?OLD_LABEL_ASSERT:NEW_LABEL_ASSERT)+';'),row={id:'templates',sourceSha256:'a'.repeat(64),sourceLabelContract:proof,originalAssertionCalls:proof.originalAssertionCalls,afterAssertionCalls:proof.originalAssertionCalls,allBehaviorAssertionsPreserved:true,allOriginalAssertionsPreserved:true,metadataAssertionChanges:[],receipt:{fullCases:[{label:title,status:'PASS'},{label:'unrelated original assertion',status:'PASS'}]},status:'PASS',exitCode:0};const before=structuredClone(row),after=structuredClone(row);if(legacy){before.status='FAIL';before.exitCode=1;before.receipt.fullCases[0].status='FAIL';after.allOriginalAssertionsPreserved=false;after.metadataAssertionChanges=[{before:OLD_LABEL_ASSERT,after:NEW_LABEL_ASSERT,scope:'label metadata only'}];}return {before,after};}
test('only the two exact reviewed source assertions classify, including current producer without rewriting',()=>{assert.equal(classifyTemplateLabelContract(NEW_LABEL_ASSERT+';').mode,'already-current-label-expectation');assert.equal(classifyTemplateLabelContract(OLD_LABEL_ASSERT+';').mode,'legacy-label-expectation');for(const source of ['',NEW_LABEL_ASSERT+';'+OLD_LABEL_ASSERT+';',NEW_LABEL_ASSERT+';'+NEW_LABEL_ASSERT+';'])assert.throws(()=>classifyTemplateLabelContract(source),/TEMPLATE_LABEL_/);});
test('a matching string in a comment does not fabricate an executable metadata assertion',()=>{assert.throws(()=>classifyTemplateLabelContract('// '+NEW_LABEL_ASSERT),/TEMPLATE_LABEL_/);assert.equal(classifyTemplateLabelContract('// '+OLD_LABEL_ASSERT+'\n'+NEW_LABEL_ASSERT+';').mode,'already-current-label-expectation');});
test('current original producer passes with zero metadata assertion changes',()=>{const p=pair();const r=verifyTemplateLabelOutcome(p.before,p.after);assert.equal(r.status,'PASS');assert.equal(r.labelChangeApplied,false);});
test('the historical single-label correction remains narrow and distinct',()=>{const p=pair(true),r=verifyTemplateLabelOutcome(p.before,p.after);assert.equal(r.status,'PASS');assert.equal(r.labelChangeApplied,true);});
for(const [name,change]of[
 ['current source rewritten',p=>p.after.metadataAssertionChanges.push({before:OLD_LABEL_ASSERT,after:NEW_LABEL_ASSERT,scope:'label metadata only'})],
 ['original producer changed',p=>{p.after.sourceSha256='b'.repeat(64);}],
 ['original assertion omitted',p=>{p.after.afterAssertionCalls=0;}],
 ['original case reordered',p=>p.after.receipt.fullCases.reverse()],
 ['unrelated behavior failure',p=>{p.after.receipt.fullCases[1].status='FAIL';}],
 ['nonzero candidate exit',p=>{p.after.exitCode=1;}],
 ['behavior preservation missing',p=>{p.after.allBehaviorAssertionsPreserved=false;}],
])test('metadata gate rejects '+name,()=>{const p=pair();change(p);assert.throws(()=>verifyTemplateLabelOutcome(p.before,p.after),/TEMPLATE_LABEL_/);});
test('a second historical failure cannot hide behind the label correction',()=>{const p=pair(true);p.before.receipt.fullCases[1].status='FAIL';assert.throws(()=>verifyTemplateLabelOutcome(p.before,p.after),/TEMPLATE_LABEL_/);});
