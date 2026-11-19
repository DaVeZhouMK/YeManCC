// Data repair/intent regressions against the actual normalization/storage/merge helpers.
import fs from 'node:fs';import path from 'node:path';import {createRequire}from'node:module';
const root=process.cwd(),out=path.resolve(process.argv[2]||'G:/YeManCC-Work/Build/Tasks/ControllerShortcutDataRound2-20261101/model');fs.mkdirSync(out,{recursive:true});
const req=createRequire(path.join(root,'package.json')),m={exports:{}};new Function('module','exports','require',req('esbuild').buildSync({stdin:{contents:"export * from './src/bridge/controllerShortcutRules';export * from './src/bridge/controllerShortcutStorage';export * from './src/bridge/controllerOemPresentation';",resolveDir:root,loader:'ts'},bundle:true,platform:'node',format:'cjs',write:false}).outputFiles[0].text)(m,m.exports,req);const p=m.exports,checks=[],failures=[];
const check=(ok,name)=>{checks.push({name,pass:!!ok});if(!ok)failures.push(name)};const same=(a,b)=>JSON.stringify(a)===JSON.stringify(b);

const selection=process.argv.includes('--case')?process.argv[process.argv.indexOf('--case')+1]:'';if(selection&&selection!=='definitions')throw Error('Unknown repair model selection');
if(selection==='definitions'){
 const report=JSON.parse(fs.readFileSync(process.argv[process.argv.indexOf('--native-catalog-report')+1],'utf8'));if(!report.pass||report.selection!=='definitions')throw Error('Verified native definitions required');
 const raw=report.modelCatalogs['HandheldCompanion.Devices.GPDWin5'],catalog=p.normalizeOemKeysSnapshot(raw),key=catalog.keys.find(k=>k.keyId==='gamepad'),recipe=key.keyboardContributions[0];
 check(p.controllerShortcutInputLabel({source:'keyboard',code:'F13'},catalog).includes('Gamepad'),'raw F13 gets explanatory profile label');
 check(key.keyboardContributions[0].outputMask==='17179869184','64-bit canonical identity remains exact text');
 check(p.controllerShortcutInputLabel({source:'keyboard',code:'F14'},catalog).includes('条件映射')&&p.controllerShortcutInputDefinition({source:'keyboard',code:'F14'},catalog).includes('接口'),'raw F14 exposes the existing interface condition');
 check(!p.controllerShortcutInputDefinition({source:'keyboard',code:'F13'},catalog).includes('需当前背键接口'),'F13 never borrows rear interface restriction');
 const mouseCatalog={...catalog,keys:[{...key,keyId:'guide',label:'Guide',keyboardContributions:[{...recipe,keyGroups:[],mouseCodes:['MouseLeft','MouseForward']}]}]};check(p.controllerShortcutInputDefinition({source:'oem',code:'guide'},mouseCatalog).includes('鼠标贡献')&&!p.controllerShortcutInputDefinition({source:'oem',code:'guide'},mouseCatalog).includes('键盘贡献'),'controlled mouse expression presentation never claims keyboard-only transport');
 for(const change of [{schemaVersion:2},{scope:'physical-alias'},{bindingEquivalenceProven:true},{profileId:-1},{outputMask:'18446744073709551616'},{keyGroups:[['NotAKey']]},{earlierGroupRules:null},{earlierGroupRules:[null]},{earlierGroupRules:[{...recipe,ruleIndex:recipe.ruleIndex+1}]}]){
  const bad=structuredClone(raw);bad.keys.find(k=>k.keyId==='gamepad').keyboardContributions=[{...recipe,...change}];const normalized=p.normalizeOemKeysSnapshot(bad);
  check(normalized.keys.length===catalog.keys.length&&normalized.keys.find(k=>k.keyId==='gamepad').keyboardContributions===undefined,'invalid optional definition rejected without losing actual key '+JSON.stringify(change));
 }
 const normalized=p.normalizeOemKeysSnapshot(raw);normalized.keys.find(k=>k.keyId==='gamepad').keyboardContributions[0].keyGroups[0][0]='F24';check(raw.keys.find(k=>k.keyId==='gamepad').keyboardContributions[0].keyGroups[0][0]==='F13','normalized metadata is deeply isolated');
 const ambiguous=structuredClone(catalog);ambiguous.keys.push({...structuredClone(key),keyId:'other',label:'Other'});check(p.controllerShortcutInputLabel({source:'keyboard',code:'F13'},ambiguous)==='F13','ambiguous profile mappings keep raw label');
 const old=structuredClone(raw);for(const k of old.keys)delete k.keyboardContributions;check(p.controllerShortcutInputLabel({source:'keyboard',code:'F13'},p.normalizeOemKeysSnapshot(old))==='F13','old catalog remains available without guessed mapping');
 const inputs=[{source:'oem',code:'gamepad'},{source:'keyboard',code:'F13'},{source:'keyboard',code:'MetaRight'}],rule={...p.newControllerShortcutRule('preserve','window.summon'),trigger:'press',inputs};check(same(p.readControllerShortcutRules({shortcutRules:[rule]})[0].inputs,inputs),'recorded OEM/raw/right modifier identities survive reload without aliasing');
 check(p.controllerShortcutInputDefinition({source:'oem',code:'home'},catalog).includes('完整专用状态'),'Home explanation retains exact configured-state distinction');
 fs.writeFileSync(path.join(out,'RESULT.json'),JSON.stringify({pass:!failures.length,checks,failures,selection,hardwareInput:false},null,2));console.log('DEFINITION MODEL '+checks.length+' CHECKS '+failures.length+' FAILURES');process.exit(failures.length?1:0);
}
const base={...p.newControllerShortcutRule('repair','monitor.toggle'),id:'repair',trigger:'press',inputs:[{source:'controller',code:'a'}],params:{operation:'toggle',future:{keep:7}},metadata:{keep:true}};
for(const opaque of [{id:'repair',inputs:[],params:{}},{id:'repair',actionId:null,metadata:{opaque:1}},{id:'repair',actionId:123,metadata:{opaque:2}}]){
 const container={shortcutRules:[base,opaque]},before=p.readControllerShortcutRules(container);
 check(same(p.preserveControllerShortcutRecords(container,before,[]),[opaque]),'delete valid rule preserves colliding opaque record '+JSON.stringify(opaque.actionId));
 const draft=before.map(r=>({...r,enabled:false})),saved=p.preserveControllerShortcutRecords(container,before,draft);
 check(same(saved[1],opaque),'edit valid rule does not rewrite colliding opaque record '+JSON.stringify(opaque.actionId));
}
for(const [field,value]of [['inputs',['a','b','x','y','lb'].map(code=>({source:'controller',code}))],['trigger','future-trigger'],['params',null]]){
 const container={shortcutRules:[{...base,[field]:value}]},before=p.readControllerShortcutRules(container),draft=structuredClone(before),intent=new Map([[base.id,new Set([field])]]);
 const untouched=p.preserveControllerShortcutRecords(container,before,draft);check(same(untouched,container.shortcutRules),'untouched normalized '+field+' remains original');
 const repaired=p.preserveControllerShortcutRecords(container,before,draft,intent);check(same(repaired[0][field],draft[0][field]),'explicit normalized '+field+' repairs original saved field');check(same(repaired[0].metadata,base.metadata),'explicit '+field+' repair preserves metadata');
}
const before=[structuredClone(base)],local=structuredClone(before),remote=structuredClone(before);remote[0].inputs=[{source:'controller',code:'b'}];
check(!p.mergeControllerShortcutDraft(before,local,remote,new Map([[base.id,new Set(['inputs'])]])).ok,'explicit same-value re-record conflicts with a concurrent different input');
remote[0].inputs=structuredClone(before[0].inputs);remote[0].params.operation='hide';const merged=p.mergeControllerShortcutDraft(before,local,remote,new Map([[base.id,new Set(['inputs'])]]));check(merged.ok&&merged.rules[0].params.operation==='hide','explicit same-value input intent permits an unrelated concurrent parameter change');
const record={...structuredClone(base),params:null,extra:{keep:[1,2]}},container={shortcutRules:[record]};const rules=p.readControllerShortcutRules(container);const repaired=p.preserveControllerShortcutRecords(container,rules,rules,new Map([[base.id,new Set(['params'])]]));check(repaired[0].params?.operation==='toggle'&&same(repaired[0].extra,record.extra),'missing params object can be repaired without losing outer extensions');
const changedType={...base,actionId:'keyboard.letter',params:{key:'KeyK'}};const typeSaved=p.preserveControllerShortcutRecords({shortcutRules:[base]},[base],[changedType],new Map([[base.id,new Set(['actionId','params'])]]))[0];check(same(typeSaved.params.future,base.params.future)&&typeSaved.params.operation===undefined,'changing output type preserves unknown params and removes previous known params');
const nativeBase={...p.nativeDefaultRule('returnDesktop'),trigger:'future-trigger',params:{future:{keep:91}},metadata:{keep:17}};const nativeBefore=p.readControllerShortcutRules({shortcutRules:[nativeBase]}),factory=p.nativeDefaultRule('returnDesktop');const nativeSaved=p.preserveControllerShortcutRecords({shortcutRules:[nativeBase]},nativeBefore,[factory],new Map([[factory.id,new Set(Object.keys(factory))]]))[0];check(nativeSaved.trigger==='press'&&same(nativeSaved.params.future,{keep:91})&&nativeSaved.metadata?.keep===17,'restoring a normalized default repairs its raw trigger and retains future params');
fs.writeFileSync(path.join(out,'ACCEPTANCE.json'),JSON.stringify({pass:!failures.length,checks,failures,productionSettingsWritten:false},null,2));console.log(JSON.stringify({checks:checks.length,failures},null,2));if(failures.length)process.exitCode=1;
