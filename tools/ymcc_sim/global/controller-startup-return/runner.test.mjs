import test from 'node:test';import assert from 'node:assert/strict';import {verifyRuntime,verifyLedger} from './provenance.mjs';
test('failed compilation is never a startup-return semantic negative',()=>{
 for(const code of [1,null,undefined])assert.throws(()=>verifyRuntime(null,{}, {buildExitCode:code,executeExitCode:1,negative:true}),/COMPILE_FAILURE/);
});
test('missing original runtime reports cannot pass via generic nonzero exit',()=>{
 assert.throws(()=>verifyRuntime({tests:75,assertions:374,cases:[]},{tests:75,assertions:374,caseNames:[]},{buildExitCode:0,executeExitCode:1,negative:true}),/ORIGINAL_TEST/);
});
test('ledger requires both original completed private-filesystem profiles',()=>{
 const line='EXTERNALPAD_LEDGER_SUMMARY passed=13 total=13 assertions=8729 hardwareAccess=false productRun=false filesystem=private-temp-only\n';
 assert.throws(()=>verifyLedger('Build succeeded.',{exitCode:0}),/DID_NOT_COMPLETE/);
 assert.throws(()=>verifyLedger(line,{exitCode:0}),/DID_NOT_COMPLETE/);
 assert.equal(verifyLedger(line+line+'EXTERNALPAD_LEDGER_PASSED fixture\n',{exitCode:0}).status,'PASS');
});
