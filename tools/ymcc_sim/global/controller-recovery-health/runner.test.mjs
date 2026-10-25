import test from 'node:test';
import assert from 'node:assert/strict';
import {verifyOriginalReport,readJson} from './provenance.mjs';
const spec=readJson(new URL('./spec.json',import.meta.url));
function report(){let extra=spec.checkCount-spec.caseCount;return {caseCount:spec.caseCount,checks:spec.checkCount,deviceIO:false,newThreads:false,failedCases:0,cases:spec.caseNames.map(name=>({name,passed:true,checks:Array.from({length:1+(extra-->0?1:0)},()=>({name:'original semantic check',actual:2,expected:2,passed:true}))}))};}
test('compile errors and nonexecuted negative results cannot count as semantic rejection',()=>{
 for(const buildExitCode of [1,null,undefined])assert.throws(()=>verifyOriginalReport(report(),spec,{negative:true,buildExitCode,executeExitCode:1}),/COMPILE_FAILURE/);
 assert.throws(()=>verifyOriginalReport(report(),spec,{negative:true,buildExitCode:0,executeExitCode:2}),/EXECUTED_ASSERTION/);
});
test('positive requires every original assertion and complete original case list',()=>{
 assert.equal(verifyOriginalReport(report(),spec,{buildExitCode:0,executeExitCode:0}).status,'PASS');
 const truncated=report();truncated.cases.pop();assert.throws(()=>verifyOriginalReport(truncated,spec,{buildExitCode:0,executeExitCode:0}),/CASE_COUNT/);
 const forged=report();forged.cases[0].checks[0].actual=0;assert.throws(()=>verifyOriginalReport(forged,spec,{buildExitCode:0,executeExitCode:0}),/ASSERTION_TAMPERED/);
});
test('generic nonzero or unrelated assertions cannot replace the pinned original stale-source failures',()=>{
 assert.throws(()=>verifyOriginalReport(report(),spec,{negative:true,buildExitCode:0,executeExitCode:1}),/UNEXPECTED_NEGATIVE/);
 const wrong=report();wrong.cases[0].checks[0]={name:'other failure',actual:0,expected:2,passed:false};wrong.cases[0].passed=false;wrong.failedCases=1;
 assert.throws(()=>verifyOriginalReport(wrong,spec,{negative:true,buildExitCode:0,executeExitCode:1}),/UNEXPECTED_NEGATIVE/);
});
