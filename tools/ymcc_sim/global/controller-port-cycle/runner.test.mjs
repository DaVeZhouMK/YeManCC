import test from 'node:test';import assert from 'node:assert/strict';import {verifyOriginal} from './run.mjs';
const original={cases:200,failures:0,failedCases:[],realHardwareAccess:false,noTargetCreation:true,maxLocationReads:2,maxPreparations:1,maxCycles:1};
test('original JSON case result is required; build success or generic summary cannot replace it',()=>{
 assert.equal(verifyOriginal(JSON.stringify(original)).status,'PASS');
 for(const value of ['', 'Build succeeded.', JSON.stringify({...original,failures:1,failedCases:['model']}),JSON.stringify({...original,noTargetCreation:false}),JSON.stringify({...original,cases:0})])assert.throws(()=>verifyOriginal(value));
 assert.throws(()=>verifyOriginal(JSON.stringify(original),{exitCode:null}));
});
test('negative needs original failed names, actual nonzero exit and boundary facts',()=>{
 const row={...original,failures:1,failedCases:['unknown board is inert']};
 assert.equal(verifyOriginal(JSON.stringify(row),{negative:true,exitCode:2}).status,'EXPECTED_REJECTION');
 assert.throws(()=>verifyOriginal(JSON.stringify(row),{negative:true,exitCode:0}));
 assert.throws(()=>verifyOriginal('compiler failed',{negative:true,exitCode:2}));
 assert.throws(()=>verifyOriginal(JSON.stringify({...row,failedCases:[]}),{negative:true,exitCode:2}));
});
