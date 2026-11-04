import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {selectCases,parseOptions} from './cli.mjs';
import {missingCatalogTools} from '../global/cli.mjs';
const repo=path.resolve(import.meta.dirname,'../../..');
function noExternalReads(action,allowed=[]){
 const original=fs.readFileSync,external=[];
 fs.readFileSync=function(file,...args){const full=path.resolve(String(file)),rel=path.relative(repo,full);if((path.isAbsolute(rel)||rel==='..'||rel.startsWith('..'+path.sep))&&!allowed.includes(full)){external.push(full);throw Error('UNRELATED_EXTERNAL_READ:'+full);}return original.call(this,file,...args);};
 return Promise.resolve().then(action).finally(()=>{fs.readFileSync=original;assert.deepEqual(external,[]);});
}
test('fan plan discovers the current catalog without loading unrelated tool or historical evidence closures',async()=>noExternalReads(async()=>{
 const selected=await selectCases(parseOptions(['--module','fan','--mode','plan']));
 assert.deepEqual(selected.selectedIds,['fan.curve']);assert.equal(selected.adapters.length,1);assert.equal(selected.adapters[0].kind,'typescript');
}));
test('explicit source replay materializes its own scope without unrelated music/signal/sleep tool preparation',async()=>noExternalReads(async()=>{
 const selected=await selectCases(parseOptions(['--test','steam.shared-mode-source','--mode','run','--rerun']));
 assert.deepEqual(selected.selectedIds,['steam.shared-mode-source']);assert.equal(selected.adapters[0].blockedReason,undefined);
}));
test('music planning stays identity-only; actual tool verification is not counted as already executed',async()=>noExternalReads(async()=>{
 const selected=await selectCases(parseOptions(['--test','audio.music-player','--mode','plan']));
 assert.equal(selected.adapters[0].registrationIdentityOnly,true);assert.equal(selected.adapters[0].blockedReason,'MUSIC_EXECUTION_IDENTITY_DEFERRED');
}));
test('Decky minimum is the original owned lifecycle and cannot silently choose wallpaper',async()=>noExternalReads(async()=>{
 const selected=await selectCases(parseOptions(['--module','decky','--mode','plan']));
 assert.deepEqual(selected.selectedIds,['plugins.decky-owned-runtime-lifecycle']);assert.equal(selected.deferred.length,1);assert.equal(selected.adapters.length,0);
}));
test('shared lifecycle requests deduplicate original IDs and keep partial coverage',async()=>noExternalReads(async()=>{
 const selected=await selectCases(parseOptions(['--module','sleep,lifecycle','--mode','plan']));
 assert.equal(selected.selectedIds.length,2);assert.equal(selected.duplicateReferences.length,2);
}));
test('unknown IDs and excessive/reset budgets reject rather than silently passing',async()=>{
 await assert.rejects(()=>selectCases(parseOptions(['--test','missing.actual-test'])),/UNKNOWN_TEST/);
 for(const value of ['0','999','1800001','NaN'])assert.throws(()=>parseOptions(['--budget-ms',value]),/WALL_BUDGET/);
 assert.equal(parseOptions([]).budgetMs,1800000);
});

const bugRoot='G:/YeManCC-Work/Testing/YMCC/Evidence/ReleaseFixturePolicy-20261030/Implementation/bug-plan-controls';fs.mkdirSync(bugRoot,{recursive:true});
function bugFile(name,issues,extra={}){const file=path.resolve(bugRoot,name+'.json');fs.writeFileSync(file,JSON.stringify({schemaVersion:1,kind:'ymcc-release-bug-plan',issues,...extra}));return file;}
test('explicit bug plan keeps functional CPU and HC regressions while excluding optimization/upgrades',async()=>{
 const file=bugFile('selected-functional',[{id:'cpu-functional',reason:'Current state publication regression',purpose:'bug-regression',testIds:['performance.cpu-state']},{id:'fan-functional',reason:'Current mock runtime connection regression',purpose:'bug-regression',testIds:['fan.runtime-connection']},{id:'future-upgrade',reason:'Separate future upgrade',purpose:'hc-upgrade',testIds:['future.upgrade-case']}]);
 await noExternalReads(async()=>{const selected=await selectCases(parseOptions(['--bug-plan',file,'--mode','plan']));assert.deepEqual(selected.selectedIds,['performance.cpu-state','fan.runtime-connection']);assert.equal(selected.bugSelection.excludedIssues.length,1);assert.equal(selected.deferred.length,1);},[file]);
});
test('specialized-only and empty bug plans do not expand to the module or full suite',async()=>{
 const file=bugFile('specialized-only',[{id:'power',reason:'Separate CPU optimization',purpose:'cpu-optimization',testIds:['future.power-case']}]);
 await noExternalReads(async()=>{const selected=await selectCases(parseOptions(['--bug-plan',file,'--mode','run']));assert.equal(selected.bugSelection.emptySelection,true);assert.equal(selected.adapters.length,0);assert.equal(selected.deferred.length,0);assert.deepEqual(selected.selectedIds,[]);},[file]);
});
test('release bug selection rejects broadening flags and unknown bug IDs',async()=>{
 assert.throws(()=>parseOptions(['--bug-plan','x.json','--module','fan']),/BUG_PLAN_REQUIRES_EXACT/);
 assert.throws(()=>parseOptions(['--bug-plan','x.json','--mode','full','--full-acceptance']),/BUG_PLAN_REQUIRES_EXACT/);
 const file=bugFile('unknown-bug',[{id:'unknown',reason:'Must not silently skip',purpose:'bug-regression',testIds:['unknown.actual-bug']}]);
 await assert.rejects(()=>selectCases(parseOptions(['--bug-plan',file])),/UNKNOWN_RELEASE_BUG_TEST_ID/);
});

test('catalog package absence is distinct from source syntax/ABI errors and cannot become PASS',()=>{
 const seen=[];assert.deepEqual(missingCatalogTools(name=>{seen.push(name);return 'resolved:'+name;}),[]);assert.deepEqual(seen,['typescript','vue/compiler-sfc']);
 assert.deepEqual(missingCatalogTools(name=>{const error=Error('missing');error.code='ERR_MODULE_NOT_FOUND';throw error;}),['typescript','vue/compiler-sfc']);
 assert.throws(()=>missingCatalogTools(()=>{throw new SyntaxError('broken module');}),/broken module/);
});

test('required release bug profile cannot silently omit one requested original case',async()=>{
 const file=bugFile('profile-mismatch-required',[{id:'mixed-bug',reason:'Both original regressions are required',purpose:'bug-regression',testIds:['steam.shared-mode-source','sleep.host-forms']}]);
 await assert.rejects(()=>selectCases(parseOptions(['--bug-plan',file,'--mode','plan'])),/BUG_PLAN_PROFILE_EXCLUDES_REQUIRED_TEST/);
});
test('bug-plan candidate payload and explicit profile reach the original Host consumer options',async()=>{
 const payload=path.resolve(bugRoot,'candidate-host'),file=bugFile('candidate-host-required',[{id:'host-bug',reason:'Validate the declared current Host payload',purpose:'bug-regression',testIds:['sleep.engine-memory']}],{profile:'full',candidateInputs:{payloadRoot:payload}});
 const o=parseOptions(['--bug-plan',file,'--mode','plan']);const selected=await selectCases(o);
 assert.equal(o.payloadRoot,payload);assert.equal(o.profile,'full');assert.deepEqual(selected.selectedIds,['sleep.engine-memory']);assert.equal(selected.adapters[0].requiresPayloadRoot,true);assert.deepEqual(selected.bugSelection.candidateInputs,{payloadRoot:payload});
});


test('full profile includes all explicitly required desktop and sleep cases without expanding the suite',async()=>{
 const file=bugFile('profile-full-required',[{id:'mixed-bug',reason:'Two required scopes',purpose:'bug-regression',testIds:['steam.shared-mode-source','sleep.host-forms']}],{profile:'full'});
 const selected=await selectCases(parseOptions(['--bug-plan',file,'--mode','plan']));
 assert.deepEqual(selected.selectedIds,['steam.shared-mode-source','sleep.host-forms']);assert.equal(selected.bugSelection.profile,'full');
});
test('explicit contradictory profile and candidate CLI values reject while equal inputs remain usable',async()=>{
 const payload=path.resolve(bugRoot,'candidate-host'),file=bugFile('candidate-conflicts',[{id:'host-bug',reason:'Use exactly this candidate',purpose:'bug-regression',testIds:['sleep.engine-memory']}],{profile:'full',candidateInputs:{payloadRoot:payload}});
 await assert.rejects(()=>selectCases(parseOptions(['--bug-plan',file,'--profile','desktop'])),/BUG_PLAN_PROFILE_CONFLICT/);
 await assert.rejects(()=>selectCases(parseOptions(['--bug-plan',file,'--payload-root',path.resolve(bugRoot,'other-candidate')])),/BUG_PLAN_CANDIDATE_CONFLICT:payloadRoot/);
 const same=await selectCases(parseOptions(['--bug-plan',file,'--profile','full','--payload-root',payload]));assert.deepEqual(same.selectedIds,['sleep.engine-memory']);
});
test('existing candidate pair validation is reused before any selected factory is loaded',async()=>{
 const file=bugFile('candidate-pin-required',[{id:'host-bug',reason:'Missing reviewed helper pin is not usable',purpose:'bug-regression',testIds:['sleep.engine-memory']}],{profile:'full',candidateInputs:{lightingPayloadRoot:path.resolve(bugRoot,'candidate-lighting')}});
 await assert.rejects(()=>selectCases(parseOptions(['--bug-plan',file])),/LIGHTING_CANDIDATE_REQUIRES_ROOT_AND_REVIEWED_PIN/);
});
test('specialized-only plan ignores malformed specialty candidate configuration and cannot execute it',async()=>{
 const file=bugFile('specialized-candidate-no-load',[{id:'future',reason:'Optional upgrade',purpose:'hc-upgrade',testIds:['future.upgrade-case']}],{profile:'future-profile',candidateInputs:{completeHostContract:'missing',args:['not-a-consumer-input']}});
 await noExternalReads(async()=>{const selected=await selectCases(parseOptions(['--bug-plan',file,'--mode','run','--rerun']));assert.equal(selected.bugSelection.emptySelection,true);assert.deepEqual(selected.adapters,[]);assert.deepEqual(selected.bugSelection.candidateInputs,{});},[file]);
});
test('complete Host contract option is forwarded through the existing option vocabulary',()=>{
 const contract=path.resolve(bugRoot,'candidate-contract.json');assert.equal(parseOptions(['--complete-host-contract',contract]).completeHostContract,contract);
});
