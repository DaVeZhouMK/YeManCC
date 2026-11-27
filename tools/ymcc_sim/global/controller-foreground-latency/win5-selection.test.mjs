import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import test from 'node:test';
import {fileURLToPath} from 'node:url';
import {parseOptions,selectCases} from '../../optimization/cli.mjs';
import {getControllerForegroundLatencyAdapters} from './adapter.mjs';
import {PREHOST_ID,verifyPreHostCacheReference} from './prehost-cache-contract.mjs';
import {assertPreHostBatchSelection} from './prehost-output-provenance.mjs';

const win5='win5-return-exit2', flag='--'+win5, envKey='YMCC_FOREGROUND_SHIELD_MODE';
const expectedArgs=['--output','{outDir}/foreground-field',flag];
function withMode(mode,body){
    const old=process.env[envKey];process.env[envKey]=mode;
    return Promise.resolve().then(body).finally(()=>{if(old===undefined)delete process.env[envKey];else process.env[envKey]=old;});
}

test('Win5 plan and run materialize one original ID and identical exact arguments',()=>withMode(win5,async()=>{
    for(const args of [['--mode','plan'],['--mode','run','--rerun']]){
        const selected=await selectCases(parseOptions([...args,'--test',PREHOST_ID]));
        assert.deepEqual(selected.selectedIds,[PREHOST_ID]);
        assert.deepEqual(selected.deferred,[]);
        assert.deepEqual(selected.adapters[0].args,expectedArgs);
    }
}));
test('Win5 refuses mixed, module, release-bug and Full selection',()=>withMode(win5,async()=>{
    for(const args of [[],['--test',PREHOST_ID+',fan.curve'],['--test',PREHOST_ID,'--module','controller'],
        ['--mode','full','--full-acceptance','--test',PREHOST_ID],['--bug-plan','not-read.json']]){
        await assert.rejects(selectCases(parseOptions(args)),/WIN5_RETURN_EXIT2_REQUIRES_ONE_EXPLICIT_ORIGINAL_TEST/);
    }
}));
test('existing nine-case descriptor does not change its arguments',()=>withMode('prehost-writer',()=>{
    assert.deepEqual(getControllerForegroundLatencyAdapters().find(a=>a.id===PREHOST_ID).args,['--output','{outDir}/foreground-field','--prehost-writer']);
}));
test('Win5 rejects missing output, duplicate flag and old-batch flag before preparation',()=>{
    const worker=fileURLToPath(new URL('../../../shield_foreground_field_selftest.mjs',import.meta.url));
    for(const args of [[flag],['--output','unused',flag,flag],['--output','unused',flag,'--prehost-writer']]){
        const r=spawnSync(process.execPath,[worker,...args],{encoding:'utf8',timeout:4000,windowsHide:true});
        assert.ifError(r.error);
        assert.equal(r.status,1);
        assert.match(r.stderr,/Win5 selection requires its own explicit output and no other scope/);
    }
});
test('nested receipt requires exact selection and refuses missing or extra execution',()=>{
    const names=[win5], batch={passed:true,error:null,notRun:[],incompleteStartedCases:[],selectedCases:names,startedCases:names,cases:[{selection:win5}]};
    assert.doesNotThrow(()=>assertPreHostBatchSelection(batch,names,true));
    for(const altered of [{...batch,notRun:names},{...batch,startedCases:[]},{...batch,selectedCases:[win5,'off']},{...batch,cases:[...batch.cases,{selection:'off'}]}]){
        assert.throws(()=>assertPreHostBatchSelection(altered,names,true),/Incomplete pre-Host batch|Pre-Host .* differ/);
    }
    assert.throws(()=>assertPreHostBatchSelection(batch,names,false),/Exact original nine selections required/);
});
test('cache receipt cannot borrow the identity of the other selection',()=>{
    const descriptors=getControllerForegroundLatencyAdapters({win5ReturnExit2:true});
    const a=descriptors.find(a=>a.id===PREHOST_ID);
    for(const [args,selection] of [[expectedArgs,'prehost-writer'],[['--output','{outDir}/foreground-field','--prehost-writer'],win5]]){
        assert.throws(()=>verifyPreHostCacheReference({manifest:{adapter:{...a,args},options:{preHostFixedContract:{kind:'ymcc-prehost-native-reference-v1',selection}}},raw:{id:PREHOST_ID}}),/PREHOST_CACHE_FIXED_CONTRACT_MISSING/);
    }
});
