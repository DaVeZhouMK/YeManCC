import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {fileURLToPath} from 'node:url';
import {validateAdapters} from '../runner.mjs';
import {extractNativeFollowup,balancedEnd} from './source-extract.mjs';
import {runInputFollowupRegressions,followupSourceInputs,getInputFollowupAdapter} from './input-followup.mjs';
const repoRoot=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../../..');
const workDir=process.env.YMCC_INPUT_FOLLOWUP_WORK_DIR??path.join(process.cwd(),'work/input-followup');
let cached;
const current=()=>cached??=(runInputFollowupRegressions({repoRoot,workDir}));
test('current formal native source and rule selftest pass',async()=>{
 const result=await current();
 assert.equal(result.status,'PASS',JSON.stringify({diagnostic:result.native.diagnostic?.failures,keyboard:result.native.keyboard?.failures,contract:result.keyboardContract,legacy:result.legacySelftest}));
 assert.equal(result.native.exitCode,0);assert.equal(result.summary.keyboardOptions,137);assert.equal(result.legacySelftest.status,'PASS');assert.deepEqual(result.sourceDrift,[]);
 assert.equal(result.native.importAudit.status,'PASS');assert.deepEqual(result.native.importAudit.forbiddenImports,[]);
});
test('all four OEM log sites use the dispatch identity for each action source',async()=>{
 const result=await current(),evidence=result.native.diagnostic.inputConsumerStateFeedback;
 for(const source of ['ownAction','inputAction','uiAction','mixed-priority']){
  const rows=evidence.filter(e=>e.input.sourceKind===source);
  for(const event of ['oem-shortcut-click','oem-shortcut-rules','oem-shortcut-edge','oem-shortcut-emit'])assert(rows.some(r=>r.feedback.event===event),source+':'+event);
  assert(rows.every(r=>r.feedback.physicalSuppressionConfirmed===false));
  for(const row of rows)if(row.state.actionId!==undefined)assert(row.state.actionId.length>0,source);
 }
 assert.equal(result.native.diagnostic.failures.length,0);
});
test('click accepted/failed/waiting/unconsumed branches remain distinct',async()=>{
 const result=await current(),rows=result.native.diagnostic.inputConsumerStateFeedback.filter(r=>r.input.sourceKind==='uiAction'&&r.feedback.event==='oem-shortcut-click');
 assert.deepEqual(rows.map(r=>r.state.status),['accepted','failed','waiting-double','unconsumed']);
 assert(rows.every(r=>!Object.hasOwn(r.state,'defaultActionSuppressionVerified')));
});
test('all keyboard options and admitted defaults are source bound',async()=>{
 const result=await current();assert.equal(result.keyboardContract.status,'PASS');
 assert.deepEqual(Array.from(result.keyboardContract.inspection,i=>i.count),[51,21,26,21,18]);
 assert.equal(result.keyboardContract.inspection.find(i=>i.actionId==='keyboard.symbol').defaultKey,'Backquote');
 assert(result.keyboardContract.inspection.every(i=>i.defaultInOptions&&i.defaultMatches&&i.defaultProblems.length===0));
 const mappings=result.native.keyboard.inputConsumerStateFeedback;
 assert.equal(mappings.length,137);assert(mappings.every(r=>r.feedback.identityMatched&&r.feedback.osInputPerformed===false));
 const get=(actionId,code)=>mappings.find(r=>r.input.actionId===actionId&&r.input.code===code).state;
 assert.equal(get('keyboard.numeric','F24').virtualKey,0x87);
 assert.equal(get('keyboard.numeric','NumpadEnter').extended,true);
 assert.equal(get('keyboard.leftFunction','Enter').extended,false);
 assert.equal(get('keyboard.symbol','Backquote').requiresShift,true);
 assert.equal(get('keyboard.leftFunction','Backquote').requiresShift,false);
});
test('extracted snippets are byte identical to the current product',()=>{
 const source=fs.readFileSync(path.join(repoRoot,'native/main.cpp'),'utf8'),header=fs.readFileSync(path.join(repoRoot,'native/input_shortcut_toggle.h'),'utf8');
 const spans=extractNativeFollowup(source,{inputShortcutHeader:header});
 assert.equal(spans.length,18);
 for(const span of spans){
  const owner=span.path?header:source;
  assert(owner.includes(span.text),span.name+' extraction rewrote product text');
  assert.equal(crypto.createHash('sha256').update(span.text).digest('hex'),span.sha256);
 }
 assert(spans.find(s=>s.name==='dispatch').text.includes('nativeShortcutUiDispatch'));
 assert(spans.find(s=>s.name==='summaryLog').text.includes('rule.trigger + rule.uiAction'));
});
test('source extractor skips comments, strings and raw literals',()=>{
 for(const input of ['{ /* } */ if(x){"}";} }','{ R"tag( } )tag"; }',"{ '{'; // }\n }",'{ std::string s="\\\"}"; }'])assert.equal(balancedEnd(input,0),input.length);
 assert.throws(()=>balancedEnd('{ /* no end',0),/UNTERMINATED_CPP_COMMENT/);
 assert.throws(()=>balancedEnd('{ "no end',0),/UNTERMINATED_CPP_LITERAL/);
});
test('unknown or incomplete source shape fails closed',()=>{
 assert.throws(()=>extractNativeFollowup('int main(){}'),/CPP_ANCHOR_NOT_UNIQUE/);
 assert.throws(()=>balancedEnd('{ if(x){}',0),/CPP_END_DELIMITER_MISSING/);
});
test('global fingerprint inputs include both ends and exact C++ consumer headers',()=>{
 const inputs=followupSourceInputs();
 for(const file of ['native/main.cpp','native/input_shortcut_toggle.h','native/shortcut_frontend_button.h','deps/json/json.hpp','src/bridge/controllerShortcutRules.ts','tools/controller_shortcut_rules_selftest.ts','tools/ymcc_sim/global/input-signals/native-diagnostic-fixture.cpp','tools/ymcc_sim/global/input-signals/keyboard-contract.ts'])assert(inputs.includes(file),file);
 assert.equal(new Set(inputs).size,inputs.length);
});


test('descriptor is accepted by the existing global runner without registry edits',()=>{
 const descriptor=getInputFollowupAdapter();assert.doesNotThrow(()=>validateAdapters([descriptor],repoRoot));
 assert.equal(descriptor.id,'controller.input-diagnostics');assert.deepEqual(descriptor.resultFiles,['input-followup-result.json']);
 assert.deepEqual(descriptor.evidenceInputs,followupSourceInputs());assert.equal(descriptor.kind,'node');
 assert(descriptor.args.includes('--work-dir'));assert(descriptor.args.includes('--output'));
});
