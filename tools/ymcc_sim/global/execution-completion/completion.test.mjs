import test from 'node:test';import assert from 'node:assert/strict';import fs from 'node:fs';import os from 'node:os';import path from 'node:path';
import {normalizeCompletionContract,completionContractFingerprint,verifyExecutionCompletion,completionProofMatchesContract,verifySavedExecutionCompletion,knownSettingsCompletionContract} from './index.mjs';
const contract={schemaVersion:1,rules:[{prefix:'suite: ',suffix:' cases passed',minimum:26}]};
const complete=(stdout='suite: 26 cases passed\n')=>({exitCode:0,stdout,stderr:'',signal:null,timedOut:false});
test('actual terminal summary is required even when a partial process returns zero',()=>{
 assert.equal(verifyExecutionCompletion(complete(),contract).status,'PASS');assert.equal(verifyExecutionCompletion(complete('PASS early scenario\n'),contract).status,'FAIL');
 for(const text of ['suite: 25 cases passed\n','suite: NaN cases passed\n','suite: 026 cases passed\n','suite: 26 cases passed\nsuite: 26 cases passed\n'])assert.equal(verifyExecutionCompletion(complete(text),contract).status,'FAIL');
 assert.equal(verifyExecutionCompletion(complete('suite: 30 cases passed\r\n'),contract).status,'PASS');
});
test('a terminal line cannot override process failures, source drift or truncation',()=>{
 for(const patch of [{exitCode:1},{exitCode:null},{signal:'SIGTERM'},{timedOut:true},{launchError:'missing'},{outputTruncated:true},{sourceChanged:true},{candidateArtifactChanged:true},{missingInputs:true},{buildError:true},{requiredEvidenceMissing:true},{requiredEvidenceFailed:true}])assert.equal(verifyExecutionCompletion({...complete(),...patch},contract).status,'FAIL');
});
test('literal contracts require all exact unique terminal statements',()=>{
 const c=knownSettingsCompletionContract('settings.cas');assert.equal(verifyExecutionCompletion(complete(c.rules.map(rule=>rule.line).join('\n')),c).status,'PASS');assert.equal(verifyExecutionCompletion(complete(c.rules[0].line),c).status,'FAIL');assert.equal(knownSettingsCompletionContract('unknown'),undefined);
});
test('contracts are strict bounded data, never regex or executable messages',()=>{
 for(const value of [{}, {schemaVersion:2,rules:[]},{schemaVersion:1,rules:[]},{schemaVersion:1,rules:[{line:'x',regex:'.*'}]},{schemaVersion:1,rules:[{line:'x\ny'}]},{schemaVersion:1,rules:[{line:'x',stream:null}]},{schemaVersion:1,rules:[{prefix:'x',suffix:'y',minimum:0}]},{schemaVersion:1,rules:[{line:'x'},{line:'x'}]}])assert.throws(()=>normalizeCompletionContract(value));
 assert.equal(completionContractFingerprint(contract),completionContractFingerprint({rules:[{minimum:26,suffix:' cases passed',prefix:'suite: ',stream:'stdout'}],schemaVersion:1}));
});
test('same declared contract must have actual recorded proof, never a renamed old PASS',()=>{
 const proof=verifyExecutionCompletion(complete(),contract);assert(completionProofMatchesContract(proof,contract));assert(!completionProofMatchesContract(null,contract));assert(!completionProofMatchesContract(proof,{schemaVersion:1,rules:[{line:'other marker'}]}));
});
test('raw streams are rechecked and changed, missing or forged-positive logs cannot certify execution',t=>{
 const base=fs.realpathSync(os.tmpdir()),dir=fs.mkdtempSync(path.join(base,'ymcc-completion-'));t.after(()=>{assert.equal(path.dirname(fs.realpathSync(dir)),base);assert(path.basename(dir).startsWith('ymcc-completion-'));fs.rmSync(dir,{recursive:true,force:true});});
 const data=complete();const row={...data,completionProof:verifyExecutionCompletion(data,contract)};fs.writeFileSync(path.join(dir,'stdout.log'),data.stdout);fs.writeFileSync(path.join(dir,'stderr.log'),'');
 assert.equal(verifySavedExecutionCompletion(row,dir).status,'PASS');fs.writeFileSync(path.join(dir,'stdout.log'),'suite: 27 cases passed\n');assert.equal(verifySavedExecutionCompletion(row,dir).code,'COMPLETION_STREAM_CHANGED');fs.writeFileSync(path.join(dir,'stdout.log'),data.stdout);assert.equal(verifySavedExecutionCompletion(row,dir).status,'PASS');fs.unlinkSync(path.join(dir,'stderr.log'));assert.equal(verifySavedExecutionCompletion(row,dir).status,'FAIL');
});
test('output budgets remain bounded and recorded proof cannot approve physical behavior',()=>{
 const r=verifyExecutionCompletion(complete('x'.repeat(2*1024*1024+1)),contract);assert.equal(r.status,'FAIL');assert.equal(r.outputWithinBudget,false);assert.equal(r.executedTests,0);
});
