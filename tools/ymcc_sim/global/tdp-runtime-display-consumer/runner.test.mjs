import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {createRequire} from 'node:module';
import {parseArgs,verifyOriginalReport,verifyNegative} from './run.mjs';
import {getTdpRuntimeDisplayAdapters,CASE_ID,REPO} from './adapter.mjs';
const {makePaths}=createRequire(import.meta.url)('./paths.cjs');
const spec=JSON.parse(fs.readFileSync(new URL('./spec.json',import.meta.url),'utf8'));
const report=()=>({passed:52,cases:[...spec.expectedNames],actualHardwareTest:false,physicalHardwareReadback:false,scope:spec.originalScope});
const fault=()=>({exitCode:1,signal:null,error:null,reportMissing:true,stderr:'AssertionError [ERR_ASSERTION]: Expected values to be strictly equal:\n    at file:///private/source-repo/tools/'+path.basename(spec.script)+':55:159\n    at check (file:///private/source-repo/tools/'+path.basename(spec.script)+':44:34)'});
function fixture(t){
 const parent=fs.realpathSync(os.tmpdir()),root=fs.mkdtempSync(path.join(parent,'ymcc-tdp-display-contract-'));
 t.after(()=>{const actual=fs.realpathSync(root);assert.equal(path.dirname(actual),parent);assert(path.basename(actual).startsWith('ymcc-tdp-display-contract-'));fs.rmSync(actual,{recursive:true,force:true});});
 return root;
}
test('descriptor executes the original runtime display consumers and declares their source evidence',()=>{
 const [adapter]=getTdpRuntimeDisplayAdapters();assert.equal(adapter.id,CASE_ID);assert.deepEqual(adapter.resultFiles,['tdp-display/result.json']);
 for(const file of ['tools/monitor_tdp_runtime_selftest.mjs','src/bridge/yeman.ts','src/bridge/tdpRuntimeState.ts','src/bridge/monitorAppliedTdp.ts','src/bridge/monitorHudModel.ts','src/bridge/monitorBarPacking.ts'])assert(adapter.evidenceInputs.includes(file));
 assert.equal(adapter.scope,'behavioral-mock');assert.equal(adapter.artifactInputs.length,5);
});
test('all original names complete with 49 behavioral checks and three explicit static checks',()=>{
 assert.equal(spec.expectedNames.length,52);assert.equal(spec.staticCaseNames.length,3);assert(spec.staticCaseNames.every(name=>spec.expectedNames.includes(name)));
 assert.deepEqual(verifyOriginalReport(report(),spec),{checks:52,behavioralChecks:49,staticSourceChecks:3});
});
test('missing, duplicate or substituted original cases cannot pass',()=>{
 for(const mode of ['missing','duplicate','unknown','wrong-count']){const raw=report();if(mode==='missing'){raw.cases.pop();raw.passed--;}else if(mode==='duplicate')raw.cases[0]=raw.cases[1];else if(mode==='unknown')raw.cases[0]='invented check';else raw.passed--;assert.throws(()=>verifyOriginalReport(raw,spec),/ORIGINAL_CASES_INCOMPLETE/);}
});
test('a complete report cannot upgrade inert software checks to physical acceptance',()=>{
 for(const key of ['actualHardwareTest','physicalHardwareReadback','scope']){const raw=report();raw[key]=key==='scope'?'full hardware':true;assert.throws(()=>verifyOriginalReport(raw,spec),/ORIGINAL_SCOPE/);}
});
test('fault detection requires the exact original first assertion frame',()=>{
 assert.deepEqual(verifyNegative(fault(),spec),{expectedCase:spec.fault.expectedCase,originalAssertionLine:55});
 const raw=fault();raw.stderr=raw.stderr.replace(':55:',':54:');assert.throws(()=>verifyNegative(raw,spec),/ORIGINAL_ASSERTION_NOT_CAUGHT/);
});
test('timeout, signal, zero exit and a borrowed completion report cannot substitute for the expected fault',()=>{
 for(const patch of [{exitCode:0},{signal:'SIGTERM'},{error:'ETIMEDOUT'},{reportMissing:false}])assert.throws(()=>verifyNegative({...fault(),...patch},spec),/ORIGINAL_ASSERTION_NOT_CAUGHT/);
});
test('compile errors and a later matching frame cannot masquerade as the targeted business assertion',()=>{
 const raw=fault();raw.stderr=raw.stderr.replace('AssertionError [ERR_ASSERTION]','SyntaxError');assert.throws(()=>verifyNegative(raw,spec),/ORIGINAL_ASSERTION_NOT_CAUGHT/);
 const later=fault();later.stderr=later.stderr.replace('    at file:', '    at file:///private/unrelated.mjs:1:1\n    at file:');assert.throws(()=>verifyNegative(later,spec),/ORIGINAL_ASSERTION_NOT_CAUGHT/);
});
test('legacy Build output is redirected exactly and prefix siblings remain distinct',t=>{
 const root=fixture(t),mapping=makePaths(path.join(root,'case'),path.join(root,'source-repo'));
 assert.deepEqual(mapping.redirect(mapping.legacy),{path:mapping.output,redirected:true});
 assert.deepEqual(mapping.redirect(path.join(mapping.legacy,'nested','report.json')),{path:path.join(mapping.output,'nested','report.json'),redirected:true});
 assert.equal(mapping.redirect(mapping.legacy+'-sibling/report.json').redirected,false);
 assert.equal(mapping.redirect(path.join(mapping.legacy,'..','outside.json')).redirected,false);
 assert.equal(mapping.inside(path.join(root,'case'),path.join(root,'case-sibling')),false);
});
test('captured resolver remains nonrecursive while the public Windows path.resolve is hooked',t=>{
 const root=fixture(t),mapping=makePaths(path.join(root,'case'),path.join(root,'source-repo')),original=path.resolve;let calls=0;
 try{path.resolve=function(...args){assert(++calls<8,'resolver recursion');return mapping.redirect(original(...args)).path;};
  assert.equal(path.resolve(mapping.legacy,'nested','artifact.mjs'),path.join(mapping.output,'nested','artifact.mjs'));assert.equal(calls,1);
  const fresh=makePaths(mapping.caseRoot,mapping.sourceRoot);assert.equal(fresh.output,mapping.output);assert.equal(calls,1);
 }finally{path.resolve=original;}
});
test('root mapping requires absolute roots and never reads the real installation namespace',t=>{
 const root=fixture(t);assert.throws(()=>makePaths('relative',root),/ABSOLUTE_ROOTS_REQUIRED/);assert.throws(()=>makePaths(root,'relative'),/ABSOLUTE_ROOTS_REQUIRED/);
 const mapping=makePaths(path.join(root,'case'),path.join(root,'source-repo'));assert.equal(mapping.redirect('C:/SOFT/YeMan/PowerControl/config.json').redirected,false);
});
test('CLI accepts only fresh absolute evidence outside the repository',t=>{
 const root=fixture(t);assert.equal(parseArgs(['--evidence-dir',path.join(root,'fresh')]).root,path.join(root,'fresh'));
 for(const args of [[],['--bad',root],['--evidence-dir','relative'],['--evidence-dir',root],['--evidence-dir',path.join(REPO,'fresh-display-evidence')],['--evidence-dir',root,'extra'],['--evidence-dir',root+'\n']])assert.throws(()=>parseArgs(args));
});