import test from 'node:test';import assert from 'node:assert/strict';
import {parseArgs,verifyOriginalOutput} from './run.mjs';
const summary=(kind,passed,total)=>`EXTERNALPAD_${kind.toUpperCase()}_SUMMARY passed=${passed} total=${total} assertions=999 hardwareAccess=false productRun=false productionHeader=true${kind==='runtime'?' operations=faked-external-effects ledgerReads=private-temp-fixtures':''}\n`;
for(const kind of ['policy','runtime']){
 test(`${kind}: requires both actual configurations and original completion marker`,()=>{
  const text=summary(kind,40,40)+summary(kind,40,40)+`EXTERNALPAD_${kind.toUpperCase()}_PASSED C:/fixture/verification-summary.txt\n`;
  assert.equal(verifyOriginalOutput(kind,text).checks,80);
  assert.throws(()=>verifyOriginalOutput(kind,text.replace(summary(kind,40,40),'')));
  assert.throws(()=>verifyOriginalOutput(kind,text.replace(/EXTERNALPAD_\w+_PASSED[^\n]+/,'')));
  assert.throws(()=>verifyOriginalOutput(kind,text,{exitCode:2}));
 });
 test(`${kind}: negative must execute original assertions and fail, compilation errors cannot substitute`,()=>{
  const text='FAIL original consumer rejects source\n'+summary(kind,39,40);
  assert.equal(verifyOriginalOutput(kind,text,{negative:true,exitCode:2}).status,'EXPECTED_REJECTION');
  assert.throws(()=>verifyOriginalOutput(kind,'compiler error',{negative:true,exitCode:2}));
  assert.throws(()=>verifyOriginalOutput(kind,text,{negative:true,exitCode:0}));
  assert.throws(()=>verifyOriginalOutput(kind,text.replace(/^FAIL[^\n]+\n/,''),{negative:true,exitCode:2}));
 });
}
test('strict selection refuses unknown, incomplete and duplicate arguments',()=>{
 assert.equal(parseArgs(['--test','controller.external-priority-policy','--evidence-dir','owned']).id,'controller.external-priority-policy');
 for(const args of [[],['--test','unknown','--evidence-dir','x'],['--test','controller.external-priority-policy'],['--test','controller.external-priority-policy','--evidence-dir','x','--evidence-dir','y']])assert.throws(()=>parseArgs(args));
});
