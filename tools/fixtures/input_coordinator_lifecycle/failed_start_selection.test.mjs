import assert from 'node:assert/strict';
import fs from 'node:fs';
import test from 'node:test';
import {getSupplementalAdapter} from '../../ymcc_sim/global/controller-coordinator-lifecycle/supplemental.mjs';
import {failedStartCase,inspectFailedStartSelection,selectFailedStartFixture} from './failed_start_selection.mjs';

const fixture=fs.readFileSync(new URL('./fixture.cpp',import.meta.url),'utf8');
const start=` cases.push_back({{"name","${failedStartCase}"}});`;
const end=' for(const bool naturalOrder:{false,true}){';
const reviewed=JSON.parse(fs.readFileSync(new URL('../../ymcc_sim/global/controller-coordinator-pipe/r3-terminal26-switch-expectations.json',import.meta.url),'utf8'));
const originalGrammar=reviewed.cases.find(row=>row.name===failedStartCase).grammar;

test('the private fixture routes one case and retains the exact original case code',()=>{
 const projected=selectFailedStartFixture(fixture), from=fixture.indexOf(start), to=fixture.indexOf(end,from);
 assert(projected.text.includes(fixture.slice(from,to)));
 assert.equal(projected.text.split(start).length,2);
 assert(projected.text.includes('if(cliBoundaryOnly==5){runSelectedFailedStart();return;}'));
 assert(projected.text.includes('static void switchRecoveryCases(int cliBoundaryOnly=5)'));
 assert.equal(projected.receipt.assertionsModified,false);
 assert.equal(projected.receipt.sourceCaptureModified,false);
});

test('missing or ambiguous original case or dispatch is not projected',()=>{
 for(const bad of [fixture.replace(start,''),fixture+start,fixture.replace('if(cliBoundaryOnly && cliBoundaryOnly!=4){','')])
  assert.throws(()=>selectFailedStartFixture(bad),/FAILED_START_.*ANCHOR/);
});

function example(){
 const timeline=['failed-start-candidate','failed-start-at9999','failed-start-at10000-before-reserve','failed-start-fallback-spent','failed-start-healthy-final']
  .map(stage=>({stage,phase:'success',fallbackAttempts:1}));
 return [{mode:'switch-recovery',exitCode:0,result:{status:'PASS',forbiddenIo:0,failures:[],checks:originalGrammar.length,
  cases:[{name:failedStartCase,timeline,cleanup:{hostHandle:false,prepared:false,quarantined:false}}],
  assertionTrace:originalGrammar.map((label,i)=>({case:failedStartCase,assertion:reviewed.labels[label],checkIndex:i+1,passed:true}))}}];
}

test('only the original selected case with the actual assertion trace and release qualifies',()=>{
 assert.equal(inspectFailedStartSelection(example()).status,'PASS');
 for(const mutate of [r=>r[0].result.cases.push(r[0].result.cases[0]),r=>r[0].result.assertionTrace.pop(),
  r=>r[0].result.assertionTrace[1].passed=false,r=>r[0].result.cases[0].timeline.pop(),
  r=>r[0].result.cases[0].cleanup.hostHandle=true,r=>r[0].exitCode=3,r=>r[0].result.forbiddenIo=1]){
  const rows=example();mutate(rows);assert.equal(inspectFailedStartSelection(rows).status,'FAIL');
 }
});

test('the canonical adapter only selects the native case and rejects managed or unknown input',()=>{
 const plan=getSupplementalAdapter({caseName:failedStartCase,planning:true});
 assert.equal(plan.id,'controller.coordinator-lifecycle');
 assert.deepEqual(plan.args.slice(-6),['--suite','switch-recovery','--case',failedStartCase,'--native-optimization','Od']);
 assert.deepEqual(plan.selectionDetails.expectedCases,[failedStartCase]);
 assert.equal(plan.selectionDetails.nativeOptimization,'Od');
 assert.equal(plan.cacheAcceptance,false);
 assert.throws(()=>getSupplementalAdapter({caseName:failedStartCase,planning:true,managedDll:'wrong.dll'}),/FAILED_START_REQUIRES_NATIVE_ONLY/);
 assert.throws(()=>getSupplementalAdapter({caseName:failedStartCase}),/INHERITED_FUTURE_DEADLINE_REQUIRED/);
 assert.throws(()=>getSupplementalAdapter({caseName:failedStartCase+':unknown',planning:true}),/UNKNOWN_COORDINATOR_CASE/);
});
