import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {privateOrderingSource, sha, BEFORE_SHA, CURRENT_SHA, HISTORICAL_CONTRACT, RECEIPTS, ORDERINGS, ORIGINAL_BROWSER_CASES, ORDER_ASSERTION, verifyFeedback, natural} from './verify.mjs';
import {verifyProbeIdentity, verifyObservation} from './run.mjs';
import {REPO, COMPONENT, HELPER, MODULE, getLightingUiFeedbackOrderingConsumerAdapters, evidenceInputs, reviewedTools} from './adapter.mjs';
import {validateAdapters, fingerprintInputs, fingerprintArtifacts} from '../runner.mjs';

const clone = value => structuredClone(value);
function sample(negative=false) {
  const args=brightness=>({enabled:true,mode:'solid',color:'#112233',color2:'#445566',speed:65,brightness});
  const cases=RECEIPTS.map(([id,input,expected])=>({id,input:clone(input),state:{applies:[args(42)],savedCount:0},expected,feedback:expected,status:'PASS',hardwareIo:false}));
  for(const height of [720,800]) for(const [id,newReply,oldReply,expected,oldFeedback] of ORDERINGS) cases.push({
    id:`${id}-${height}`,input:{viewport:{width:1280,height},id,newReply,oldReply,expected},
    state:{callCount:2,args:[args(53),args(74)],afterNew:expected,afterOld:negative?oldFeedback:expected},
    expected,feedback:negative?oldFeedback:expected,status:negative?'FAIL':'PASS',sourceKind:'source-derived-fixture',hardwareIo:false,physicalEffectVerified:false});
  const component=negative?'private-fault.vue':'actual.vue',sourceSha256=negative?BEFORE_SHA:CURRENT_SHA;
  const stdout=negative?'':'LIGHTING_OPERATIONS_BROWSER_OK groups=17 deviceOperations=0\n';
  const stderr=negative?`AssertionError [ERR_ASSERTION]: ${ORDER_ASSERTION}\n8 !== 0\n at real-component-feedback-probe.mjs:85:9`:'';
  return {cases,ordering:{cases:clone(cases),status:negative?'FAIL':'PASS',hardwareIo:false,physicalEffectVerified:false},
    feedback:{exitCode:negative?1:0,status:negative?'FAIL':'PASS',snapshotOverride:negative,component,sourceSha256,hardwareIo:false,bridge:'memory fixture'},
    browser:negative?null:{ok:true,cases:[...ORIGINAL_BROWSER_CASES,...cases.map(row=>row.id)],errors:[],hardwareIo:false,HCAssemblyLoaded:false,browserPort:53000},
    stdout,stderr,exitCode:negative?1:0,component,sourceSha256,negative};
}
function mutateCase(row, action) {action(row.cases); row.ordering.cases=clone(row.cases); return row;}

test('adapter declares positive sources and a bounded, present source/tool closure',()=>{
  const adapters=getLightingUiFeedbackOrderingConsumerAdapters();
  assert.equal(validateAdapters(adapters,REPO).length,1);
  assert.ok(adapters[0].evidenceInputs.includes(COMPONENT)); assert.ok(adapters[0].evidenceInputs.includes(HELPER));
  assert.deepEqual(fingerprintInputs(REPO,evidenceInputs()).filter(row=>row.status!=='OK'),[]);
  assert.ok(reviewedTools().length<=640);
  assert.deepEqual(fingerprintArtifacts(reviewedTools()).filter(row=>row.status!=='OK'),[]);
});
test('current private source removes only ordering guards and preserves lifecycle',()=>{
  const actual=fs.readFileSync(path.join(REPO,COMPONENT));
  assert.equal(sha(actual),CURRENT_SHA); assert.equal(sha(privateOrderingSource(actual)),BEFORE_SHA);
  const fault=privateOrderingSource(actual).toString('utf8');
  for(const line of ['let ledApplyRequest = 0;','  const request = ++ledApplyRequest;','  ++ledApplyRequest;','onDeactivated(deactivateFeedback);','onBeforeUnmount(deactivateFeedback);'])assert.ok(fault.includes(line),line);
  assert.ok(!fault.includes('    if (request !== ledApplyRequest) return;'));
  assert.equal(HISTORICAL_CONTRACT.currentSha,'f1946ffc02c6cc6930378b8f0f1b8051b366a100254c12d0c0fc41370428707f');
  assert.equal(HISTORICAL_CONTRACT.beforeSha,'b3c5c29961115196dee570d86dd05077aaaca95943df4e051ad65d03bfbdaa9c');
  assert.throws(()=>privateOrderingSource(Buffer.concat([actual,Buffer.from('\n')])));
  assert.throws(()=>privateOrderingSource(Buffer.from(actual.toString().replace('  const request = ++ledApplyRequest;',''))));
});
test('exact 4 original receipts plus 8 ordering passes are accepted',()=>{
  assert.equal(verifyFeedback(sample()).orderingPassed,8);
  assert.equal(verifyFeedback(sample(true)).orderingOriginalFailures,8);
});
test('missing/duplicate resolution and changed API arguments or expected text are rejected',()=>{
  for(const change of [
    cases=>cases.pop(), cases=>{cases[11]=clone(cases[4]);},
    cases=>{cases[4].state.args[0].brightness=74;}, cases=>{cases[4].state.callCount=1;},
    cases=>{cases[4].input.viewport.height=900;}, cases=>{cases[4].expected='changed';},
    cases=>{cases[0].state.savedCount=1;},cases=>{cases[0].status='FAIL';},
    cases=>{cases[0].state.applies[0].mode='preset';},cases=>{cases[4].state.afterNew='older';}
  ]) assert.throws(()=>verifyFeedback(mutateCase(sample(true),change)));
});
test('fault requires all exact original stale texts, not only FAIL labels',()=>{
  for(const change of [cases=>{cases[4].state.afterOld='wrong failure';},cases=>{cases[4].status='PASS';},
    cases=>{cases[4].feedback=cases[4].expected;},cases=>{cases[11].state.afterOld=cases[11].expected;}]) {
    assert.throws(()=>verifyFeedback(mutateCase(sample(true),change)));
  }
});
test('compile/start/timeout/other assertions cannot replace original ordering assertion',()=>{
  for(const stderr of ['build failed','browser launch failed','TimeoutError','AssertionError [ERR_ASSERTION]: different assertion\n8 !== 0\n at real-component-feedback-probe.mjs:85:9',
    `AssertionError [ERR_ASSERTION]: ${ORDER_ASSERTION}\n8 !== 0\n at other.mjs:85:9`]) {
    assert.throws(()=>verifyFeedback({...sample(true),stderr}));
  }
  assert.throws(()=>verifyFeedback({...sample(true),exitCode:2}));
  assert.throws(()=>verifyFeedback({...sample(true),ordering:null}));
  assert.throws(()=>verifyFeedback({...sample(),browser:{ok:true,cases:[],errors:[]}}));
  assert.throws(()=>verifyFeedback({...sample(true),feedback:{...sample(true).feedback,snapshotOverride:false}}));
});
test('natural terminal requires actual PID, exit status and no forced termination',()=>{
  const row={pid:42,exitCode:0,startedUtc:'2026-10-19T00:00:00Z',finishedUtc:'2026-10-19T00:00:01Z'};
  natural(row,'fixture');
  for(const changed of [{pid:null},{exitCode:null},{signal:'SIGTERM'},{timedOut:true},{launchError:'ENOENT'},{outputTruncated:true},{killRequested:true},{finishedUtc:null}]) {
    assert.throws(()=>natural({...row,...changed},'fixture'));
  }
});
test('positive/fault probes may differ only in component selection',()=>{
  const a='fs.readFileSync("G:\\\\actual.vue")\nassert.equal(8,0);';
  const b='fs.readFileSync("D:\\\\private.vue")\nassert.equal(8,0);';
  assert.equal(verifyProbeIdentity(a,b,'G:\\actual.vue','D:\\private.vue').assertionsIdentical,true);
  assert.throws(()=>verifyProbeIdentity(a,b.replace('8,0','0,0'),'G:\\actual.vue','D:\\private.vue'));
});
test('observation refuses missing compiler/browser/node terminal evidence',()=>{
  assert.throws(()=>verifyObservation([],{repo:REPO}));
});

test('preloaded observer retains real ESM spawnSync child and graceful compiler close without UI execution',()=>{
  const scratchRoot=process.env.YMCC_LIGHTING_TEST_ROOT;
  assert.ok(scratchRoot && path.isAbsolute(scratchRoot),'Set YMCC_LIGHTING_TEST_ROOT to an owned test evidence directory');
  fs.mkdirSync(scratchRoot,{recursive:true}); const dir=fs.mkdtempSync(path.join(scratchRoot,'observer-'));
  const repo=path.join(dir,'repo'),trace=path.join(dir,'trace'); fs.mkdirSync(repo);
  const config=path.join(dir,'config.json'),entry=path.join(repo,'fixture.mjs'),child=path.join(repo,'child.mjs');
  fs.writeFileSync(child,"process.stdout.write('original-child-output'); process.stderr.write('original-child-error');\n");
  fs.writeFileSync(entry,`import {spawnSync,spawn} from 'node:child_process';\nimport fs from 'node:fs';\nconst r=spawnSync(process.execPath,[${JSON.stringify(child)}],{encoding:'utf8'}); if(r.status!==0)throw Error('child failed');\nconst compiler=spawn(process.execPath,['-e',\"process.stdin.resume();process.stdin.on('end',()=>process.exitCode=0)\"],{stdio:['pipe','pipe','pipe']}); compiler.unref();compiler.stdin.unref();compiler.stdout.unref();compiler.stderr.unref();\n`);
  const observer=path.join(REPO,MODULE,'observe.cjs');
  fs.writeFileSync(config,JSON.stringify({repo,trace,evidenceRoot:dir,esbuildBinary:process.execPath}));
  const env={...process.env,YMCC_LIGHTING_OBSERVATION:config,NODE_OPTIONS:`--require "${observer.replaceAll('\\','/')}"`}; delete env.NODE_PATH;
  // The compiler stand-in is an ordinary node stdin reader, not a second observed fixture.
  const source=fs.readFileSync(entry,'utf8').replace("{stdio:['pipe','pipe','pipe']}","{stdio:['pipe','pipe','pipe'],env:{...process.env,NODE_OPTIONS:''}}"); fs.writeFileSync(entry,source);
  const ran=spawnSync(process.execPath,[entry],{env,encoding:'utf8',timeout:15000,windowsHide:true});
  assert.equal(ran.status,0,ran.stderr); assert.equal(ran.signal,null);
  const reports=fs.readdirSync(trace).filter(file=>/^node-\d+\.json$/.test(file)).map(file=>JSON.parse(fs.readFileSync(path.join(trace,file),'utf8')));
  assert.equal(reports.length,2);
  const parent=reports.find(row=>row.entry===entry); assert.equal(parent.terminal.exitCode,0); assert.equal(parent.children.length,2);
  for(const row of parent.children) { assert.equal(row.terminal,true); natural(row,'observer-unit'); }
  assert.equal(parent.children[1].shutdownMethod,'stdin-eof-at-beforeExit');
  assert.equal(fs.readFileSync(parent.children[0].stdoutFile,'utf8'),'original-child-output');
  assert.equal(fs.readFileSync(parent.children[0].stderrFile,'utf8'),'original-child-error');
  assert.deepEqual(parent.observationErrors,[]); assert.deepEqual(parent.drift,[]);
});

test('archived actual historical failure is accepted without rerunning the browser', {skip:!process.env.YMCC_LIGHTING_HISTORY_ROOT}, ()=>{
  const dir=path.join(process.env.YMCC_LIGHTING_HISTORY_ROOT,'before01');
  const read=file=>JSON.parse(fs.readFileSync(path.join(dir,file),'utf8'));
  const feedback=read('feedback-result.json');
  const result=verifyFeedback({cases:read('ui-feedback-cases.json'),ordering:read('ordering-result.json'),feedback,
    browser:null,stdout:feedback.stdout,stderr:feedback.stderr,exitCode:feedback.exitCode,
    component:feedback.component,sourceSha256:HISTORICAL_CONTRACT.beforeSha,negative:true});
  assert.equal(result.orderingOriginalFailures,8); assert.equal(result.originalReceiptsPassed,4);
});


test('original top-level AssertionError waits for actual compiler EOF close in browser finally',()=>{
  const scratchRoot=process.env.YMCC_LIGHTING_TEST_ROOT;
  assert.ok(scratchRoot && path.isAbsolute(scratchRoot));
  fs.mkdirSync(scratchRoot,{recursive:true});const dir=fs.mkdtempSync(path.join(scratchRoot,'observer-assertion-'));
  const repo=path.join(dir,'repo'),trace=path.join(dir,'trace'),moduleDir=path.join(repo,'node_modules','playwright');
  fs.mkdirSync(moduleDir,{recursive:true});
  fs.writeFileSync(path.join(moduleDir,'index.js'),"module.exports={chromium:{async launch(){return{async close(){return 'original-browser-close'}}}}};\n");
  const entry=path.join(repo,'fixture.mjs'),config=path.join(dir,'config.json');
  fs.writeFileSync(entry,`import {createRequire} from 'node:module';import {spawn} from 'node:child_process';import assert from 'node:assert/strict';
const {chromium}=createRequire(import.meta.url)('playwright');
const compiler=spawn(process.execPath,['-e',"process.stdin.resume();process.stdin.on('end',()=>process.exitCode=0)"],{stdio:['pipe','pipe','pipe'],env:{...process.env,NODE_OPTIONS:''}});
compiler.unref();compiler.stdin.unref();compiler.stdout.unref();compiler.stderr.unref();
const browser=await chromium.launch();
try{assert.equal(8,0,${JSON.stringify(ORDER_ASSERTION)});}finally{await browser.close();}
`);
  const observer=path.join(REPO,MODULE,'observe.cjs');
  fs.writeFileSync(config,JSON.stringify({repo,trace,evidenceRoot:dir,esbuildBinary:process.execPath}));
  const env={...process.env,YMCC_LIGHTING_OBSERVATION:config,NODE_OPTIONS:`--require "${observer.replaceAll('\\','/')}"`};delete env.NODE_PATH;
  const ran=spawnSync(process.execPath,[entry],{env,encoding:'utf8',timeout:15000,windowsHide:true});
  fs.writeFileSync(path.join(dir,'stdout.log'),ran.stdout||'');fs.writeFileSync(path.join(dir,'stderr.log'),ran.stderr||'');
  assert.equal(ran.status,1,ran.stderr);assert.equal(ran.signal,null);assert.equal(ran.error,undefined);
  assert.match(ran.stderr,/AssertionError \[ERR_ASSERTION\]/);assert(ran.stderr.includes(ORDER_ASSERTION));assert.match(ran.stderr,/8 !== 0/);
  const reports=fs.readdirSync(trace).filter(file=>/^node-\d+\.json$/.test(file)).map(file=>JSON.parse(fs.readFileSync(path.join(trace,file),'utf8')));
  assert.equal(reports.length,1);const parent=reports[0];assert.equal(parent.terminal.exitCode,1);assert.equal(parent.children.length,1);
  const compilerRow=parent.children[0];assert.equal(compilerRow.terminal,true);natural(compilerRow,'assertion-path-compiler');
  assert.equal(compilerRow.shutdownMethod,'stdin-eof-at-browser-close');assert.equal(compilerRow.killRequested,false);
  assert(Date.parse(compilerRow.finishedUtc)<=Date.parse(parent.terminal.finishedUtc));
  assert.deepEqual(parent.observationErrors,[]);assert.deepEqual(parent.drift,[]);
});
