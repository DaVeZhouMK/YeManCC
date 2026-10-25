import test from 'node:test';
import assert from 'node:assert/strict';
import { ORIGINAL_NATIVE_BUDGET_MS, readCompletedNativeReport } from './r3-terminal22-observation-terminal.mjs';
const complete = (change = {}) => ({timedOut:false,launchError:null,signal:null,
  outputTruncated:false,durationMs:59000,exitCode:0,...change});
function rejectedBeforeReport(terminal, code, expectedExit=0) {
  let reads=0;
  assert.throws(()=>readCompletedNativeReport(terminal,expectedExit,()=>{
    ++reads;throw new SyntaxError('Unexpected end of JSON input');
  }),error=>error.code===code);
  assert.equal(reads,0,'unadmitted native execution must not parse partial evidence');
}
test('recorded timeout wins over empty-report parser failure',()=>
  rejectedBeforeReport(complete({timedOut:true,signal:'SIGTERM',exitCode:null,durationMs:60038}),
    'NATIVE22_ORIGINAL_BUDGET_TIMEOUT'));
test('timeout cannot be concealed by a forged complete PASS report',()=>{
  let reads=0;assert.throws(()=>readCompletedNativeReport(complete({timedOut:true}),0,
    ()=>{++reads;return {status:'PASS',checks:6457};}),
    error=>error.code==='NATIVE22_ORIGINAL_BUDGET_TIMEOUT');assert.equal(reads,0);
});
for(const [name,change,code] of [
 ['missing timeout fact',{timedOut:undefined},'NATIVE22_TERMINAL_METADATA_INCOMPLETE'],
 ['string timeout fact',{timedOut:'false'},'NATIVE22_TERMINAL_METADATA_INCOMPLETE'],
 ['launch failure',{launchError:'ENOENT'},'NATIVE22_PROCESS_LAUNCH_UNPROVEN'],
 ['missing launch fact',{launchError:undefined},'NATIVE22_PROCESS_LAUNCH_UNPROVEN'],
 ['abnormal process signal',{signal:'SIGTERM'},'NATIVE22_NATURAL_EXIT_UNPROVEN'],
 ['missing signal fact',{signal:undefined},'NATIVE22_NATURAL_EXIT_UNPROVEN'],
 ['truncated output',{outputTruncated:true},'NATIVE22_OUTPUT_INCOMPLETE'],
 ['missing output fact',{outputTruncated:undefined},'NATIVE22_OUTPUT_INCOMPLETE'],
 ['negative duration',{durationMs:-1},'NATIVE22_DURATION_UNPROVEN'],
 ['missing duration',{durationMs:undefined},'NATIVE22_DURATION_UNPROVEN'],
 ['nonfinite duration',{durationMs:Infinity},'NATIVE22_DURATION_UNPROVEN'],
 ['unreported deadline overrun',{durationMs:60001},'NATIVE22_ORIGINAL_BUDGET_EXCEEDED'],
 ['unexpected successful exit',{exitCode:1},'NATIVE22_ORIGINAL_EXIT_MISMATCH'],
 ['missing exit code',{exitCode:null},'NATIVE22_ORIGINAL_EXIT_MISMATCH']
])test(name+' cannot parse a report',()=>rejectedBeforeReport(complete(change),code));
test('original positive admits one complete report read',()=>{
  let reads=0;const raw={status:'PASS',checks:6457};
  assert.equal(readCompletedNativeReport(complete(),0,()=>{++reads;return raw;}),raw);
  assert.equal(reads,1);
});
test('specified semantic-negative exit is admitted without relabelling FAIL',()=>{
  const raw={status:'FAIL',failures:[{assertion:'original negative'}]};
  assert.equal(readCompletedNativeReport(complete({exitCode:1}),1,()=>raw),raw);
});
test('exact original budget boundary remains 60000ms',()=>{
  assert.equal(ORIGINAL_NATIVE_BUDGET_MS,60000);
  const raw={};assert.equal(readCompletedNativeReport(complete({durationMs:60000}),0,()=>raw),raw);
});
test('natural exit does not convert malformed report to pass',()=>{
  assert.throws(()=>readCompletedNativeReport(complete(),0,()=>JSON.parse('')),SyntaxError);
});
test('natural exit does not conceal a missing final report',()=>{
  const missing=new Error('complete native raw report missing');
  assert.throws(()=>readCompletedNativeReport(complete(),0,()=>{throw missing;}),error=>error===missing);
});
test('report content and original semantic validation are left to caller',()=>{
  const raw={status:'FAIL',checks:0};
  assert.equal(readCompletedNativeReport(complete(),0,()=>raw),raw);
});
test('unsupported terminal contract is rejected',()=>{
  for(const code of [-1,2,null,undefined])assert.throws(
    ()=>readCompletedNativeReport(complete(),code,()=>({})),
    error=>error.code==='NATIVE22_TERMINAL_INPUT_INVALID');
});
