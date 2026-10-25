import test from 'node:test';
import assert from 'node:assert/strict';
import {requireNaturalB54ActualTerminal} from './r3-b54-actual-worker.mjs';
const valid=()=>({status:'OWNED_TARGET_EXIT_ZERO_REQUIRES_BUSINESS_RECEIPT_REVIEW',code:0,signal:null,targetExecutions:1,forcedOwnNativeTermination:false,ownedPid:12345});
test('natural zero exit still requires later business evidence',()=>assert.equal(requireNaturalB54ActualTerminal(valid()),true));
test('native60 timeout retains exact failure before missing aggregate',()=>assert.throws(()=>requireNaturalB54ActualTerminal({status:'NATIVE_60S_TIMEOUT',code:null,signal:'SIGTERM',ownedPid:3448,targetExecutions:1,forcedOwnNativeTermination:true,businessPass:null}),{message:'B54_ACTUAL_WORKER_NATIVE_60S_TIMEOUT'}));
for(const [name,patch]of [['nonzero exit',{code:2}],['signal',{signal:'SIGTERM'}],['forced termination',{forcedOwnNativeTermination:true}],['no target',{targetExecutions:0}],['repeated target',{targetExecutions:2}],['invalid owned pid',{ownedPid:0}],['wrong status',{status:'PASS'}]])test(name+' remains rejected',()=>assert.throws(()=>requireNaturalB54ActualTerminal({...valid(),...patch}),{message:'B54_ACTUAL_WORKER_ACTUAL_NATURAL_NATIVE_EXIT'}));
test('missing terminal rejected',()=>assert.throws(()=>requireNaturalB54ActualTerminal(null),{message:'B54_ACTUAL_WORKER_ACTUAL_NATURAL_NATIVE_EXIT'}));
