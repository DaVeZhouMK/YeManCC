import assert from 'node:assert/strict';
import { normalizeSettings } from '../src/bridge/settingsRepository';
const defaults=normalizeSettings({});let checks=0;
for(const [key,template] of Object.entries(defaults)){
  if(template===null||typeof template!=='object')continue;
  for(const malformed of [null,17,true,'old-schema-value',Array.isArray(template)?{}:[]]){
    const raw={schemaVersion:1,[key]:malformed,future:{keep:'user-data'}};
    const before=JSON.stringify(raw);
    if(key==='configurationBaseline'){
      assert.throws(()=>normalizeSettings(raw), /配置基准/);
      assert.equal(JSON.stringify(raw),before,'unsupported receipt mutated');checks++;continue;
    }
    const normalized=normalizeSettings(raw);
    assert.equal(JSON.stringify(raw),before,'caller snapshot mutated');
    assert.equal(Array.isArray((normalized as any)[key]),Array.isArray(template),key+' shape');
    assert.ok((normalized as any)[key]!==null&&typeof (normalized as any)[key]==='object',key+' must be a usable section');
    assert.equal((normalized as any).future.keep,'user-data');checks++;
  }
}
for(const broken of [null,[],false,'obsolete']){
 const result=normalizeSettings({rtss:broken,ui:broken,input:{outputTarget:broken,gyroMotion:broken,buttonMapping:broken,ownership:broken},performanceSchedule:broken});
 assert.ok(result.input.outputTarget);assert.ok(result.input.gyroMotion);assert.ok(result.rtss);checks++;
}
const legacy=normalizeSettings({schemaVersion:0,ui:{theme:'dark',futureLayout:{keep:true}},input:{outputTarget:{persona:'xbox360'}},extensions:{manualModule:{version:'3'}}});
assert.equal(legacy.ui.theme,'dark');assert.equal((legacy.ui as any).futureLayout.keep,true);assert.equal((legacy.extensions as any).manualModule.version,'3');checks++;
const pollution=normalizeSettings(JSON.parse('{"__proto__":{"polluted":true},"rtss":null,"ui":null}'));
assert.equal(({} as any).polluted,undefined);assert.ok(pollution.rtss);checks++;
console.log(JSON.stringify({status:'PASS',checks,consumer:'production normalizeSettings',realSettingsWritten:false}));
