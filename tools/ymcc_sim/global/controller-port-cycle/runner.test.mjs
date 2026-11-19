import test from 'node:test';import assert from 'node:assert/strict';import {verifyOriginal} from './run.mjs';
const original={cases:403,failures:0,failedCases:[],realHardwareAccess:false,noTargetCreation:true,maxLocationReads:2,maxPreparations:1,maxCycles:1};
const fault={...original,failures:2,failedCases:['unknown board result','unknown board exact counts']};
const originalScript='C:/fixture/tools/external_controller_port_cycle_selftest.ps1';
const faultOptions={negative:true,exitCode:1,originalScript,stderr:`Exception: ${originalScript}:14\r\n     | Injected self-test failed with exit code 3\r\n`};
test('original JSON case result is required; build success or generic summary cannot replace it',()=>{
 assert.equal(verifyOriginal(JSON.stringify(original)).status,'PASS');
 for(const value of ['', 'Build succeeded.', JSON.stringify(fault),JSON.stringify({...original,noTargetCreation:false}),JSON.stringify({...original,cases:1})])assert.throws(()=>verifyOriginal(value));
 assert.throws(()=>verifyOriginal(JSON.stringify(original)+'\nBuild failed.'));
 assert.throws(()=>verifyOriginal(JSON.stringify(original)+'\n'+JSON.stringify(original)));
 assert.throws(()=>verifyOriginal(JSON.stringify(original),{stderr:'A different error'}));
 assert.throws(()=>verifyOriginal(JSON.stringify(original),{exitCode:null}));
});
test('negative binds both exact assertions and the original managed failure terminal',()=>{
 assert.equal(verifyOriginal(JSON.stringify(fault),faultOptions).status,'EXPECTED_REJECTION');
 assert.equal(verifyOriginal(JSON.stringify({...fault,failedCases:[...fault.failedCases].reverse()}),faultOptions).status,'EXPECTED_REJECTION');
 assert.throws(()=>verifyOriginal(JSON.stringify({...fault,failedCases:['unknown board result','model result']}),faultOptions),/FAULT_MISMATCH/);
 assert.throws(()=>verifyOriginal(JSON.stringify({...fault,failedCases:['unknown board result','unknown board result']}),faultOptions),/FAULT_MISMATCH/);
 assert.throws(()=>verifyOriginal(JSON.stringify({...fault,failedCases:[]}),faultOptions),/COUNTS_INVALID/);
 for(const override of [{exitCode:0},{exitCode:2},{stderr:'Isolated build failed with exit code 3'},{originalScript:'C:/other.ps1'},{stderr:faultOptions.stderr.replace('exit code 3','exit code 1')}]){
  assert.throws(()=>verifyOriginal(JSON.stringify(fault),{...faultOptions,...override}),/FAULT_TERMINAL_MISMATCH/);
 }
 assert.throws(()=>verifyOriginal('compiler failed',faultOptions));
});
