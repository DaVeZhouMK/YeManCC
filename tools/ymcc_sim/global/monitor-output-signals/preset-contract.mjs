import fs from'node:fs';import path from'node:path';import assert from'node:assert/strict';import vm from'node:vm';import{createRequire}from'node:module';
import{root,hash}from'./common.mjs';
export const PRESET_ORDER=[{value:'minimal',label:'精简'},{value:'simple',label:'简约'},{value:'standard',label:'标准'},{value:'detailed',label:'详细'}];
export const OLD_LABEL_ASSERT="assert.deepEqual(m.HUD_DATA_PRESETS.map(p=>p.label),['简约','精简','标准','详细'])";
export const NEW_LABEL_ASSERT="assert.deepEqual(m.HUD_DATA_PRESETS.map(p=>p.label),['精简','简约','标准','详细'])";
export async function verifyPresetContract(m,out){
 assert.deepEqual(m.HUD_DATA_PRESETS.map(p=>({value:p.value,label:p.label})),PRESET_ORDER,'reviewed stable ID/label/visual order changed');
 assert.equal(m.HUD_DEFAULT_DATA_PRESET,'simple');
 const counts=Object.fromEntries(PRESET_ORDER.map(p=>[p.value,Object.values(m.hudDataPresetFields(p.value)).filter(Boolean).length]));
 assert.deepEqual({minimal:counts.minimal,simple:counts.simple,standard:counts.standard},{minimal:3,simple:21,standard:24});
 assert.equal(counts.detailed,m.HUD_METRICS.filter(metric=>metric.ready&&metric.id!=='clock').length,'detailed remains the ready-metric consumer, not an invented historic count');
 const req=createRequire(path.join(root,'package.json')),{parse}=req('vue/compiler-sfc'),ts=req('typescript'),{transform}=req('esbuild'),{reactive,computed,ref}=req('vue');
 const file=path.join(root,'src/components/MonitorOutputSettings.vue'),text=fs.readFileSync(file,'utf8'),script=parse(text).descriptor.scriptSetup.content;
 const tree=ts.createSourceFile(file,script,ts.ScriptTarget.Latest,true,ts.ScriptKind.TS);
 const names=new Set(['dataPresetOptions','dataPreset','dataPresetStep','dataPresetLabel','resetPresetTitle']);
 const code=[];for(const node of tree.statements){if(ts.isVariableStatement(node)&&node.declarationList.declarations.some(d=>names.has(d.name.getText(tree))))code.push(node.getText(tree));
  if(ts.isFunctionDeclaration(node)&&['chooseDataPresetStep','chooseDataPreset'].includes(node.name?.getText(tree)))code.push(node.getText(tree));}
 assert.equal(code.length,7,'actual consumer fragments changed; re-review required');
 const prefs=reactive(m.normalizeMonitorOutput({ymccTemplate:'A'})),saved=[];
 const js=(await transform(code.join('\n')+'\nglobalThis.__consumer={options:dataPresetOptions,step:()=>dataPresetStep.value,label:()=>dataPresetLabel.value,reset:()=>resetPresetTitle.value,chooseDataPresetStep};',{loader:'ts',format:'iife'})).code;
 const ctx={HUD_DATA_PRESETS:m.HUD_DATA_PRESETS,prefs,computed,editingTemplate:ref('A'),persist:(...args)=>saved.push(args)};vm.createContext(ctx);vm.runInContext(js,ctx,{timeout:1000});
 const consumer=ctx.__consumer,steps=[];
 assert.equal(consumer.step(),1);assert.equal(consumer.label(),'简约');
 assert.equal(consumer.reset(),'A模板 · 重置为简约（只恢复数据项）','actual A reset label consumes the injected editingTemplate ref');
 for(let i=0;i<4;i++){consumer.chooseDataPresetStep(i);steps.push({index:i,id:prefs.ymcc.dataPreset,label:consumer.label(),fieldCount:Object.values(prefs.ymcc.fields).filter(Boolean).length});assert.equal(prefs.ymcc.dataPreset,PRESET_ORDER[i].value);assert.equal(consumer.label(),PRESET_ORDER[i].label);}
 assert.equal(saved.length,4);assert(text.includes('v-for="(option,index) in dataPresetOptions"'),'visual tick consumer changed');

 assert.equal(consumer.reset(),'A模板 · 重置为详细（只恢复数据项）','actual A reset label follows the fourth original choice');
 // Replay the same seven source fragments; only persistence and editingTemplate are memory ports.
 function mount(normalized,template){const prefs=reactive(normalized),saved=[],editingTemplate=ref(template);
  assert.equal(prefs.ymccTemplate,template,'fixture editingTemplate ref must identify the actual selected bank');
  const ctx={HUD_DATA_PRESETS:m.HUD_DATA_PRESETS,prefs,computed,editingTemplate,persist:(...args)=>saved.push(args)};vm.createContext(ctx);vm.runInContext(js,ctx,{timeout:1000});
  return {prefs,saved,editingTemplate,consumer:ctx.__consumer};
 }
 const snapshot=value=>JSON.parse(JSON.stringify(value));
 const bankData=bank=>snapshot({dataPreset:bank.dataPreset,fields:bank.fields,dataPresetFields:bank.dataPresetFields});
 const freshB=mount(m.normalizeMonitorOutput(),'B');
 assert.equal(freshB.prefs.ymcc.dataPreset,'minimal','fresh actual model defaults B to minimal');
 assert.equal(freshB.consumer.step(),0,'fresh B uses the real minimal selector position');assert.equal(freshB.consumer.label(),'精简');
 assert.equal(freshB.consumer.reset(),'B模板 · 重置为精简（只恢复数据项）','actual B reset label consumes a B ref');
 assert.equal(freshB.saved.length,0,'reading fresh B defaults does not save settings');
 const explicit=[];
 for(const [template,preset]of [['A','standard'],['B','detailed']]){
  const saved=m.normalizeMonitorOutput({ymccTemplate:template,ymcc:{dataPreset:preset,fields:m.hudDataPresetFields(preset)}});
  const loaded=mount(m.normalizeMonitorOutput(snapshot(saved)),template),index=PRESET_ORDER.findIndex(item=>item.value===preset);
  assert.equal(loaded.prefs.ymccTemplate,template,'saved explicit A/B selection survives default normalization');
  assert.equal(loaded.prefs.ymcc.dataPreset,preset,'saved explicit A/B preset survives the fresh B default');
  assert.equal(loaded.consumer.step(),index);assert.equal(loaded.consumer.label(),PRESET_ORDER[index].label);
  assert.equal(loaded.consumer.reset(),template+'模板 · 重置为'+PRESET_ORDER[index].label+'（只恢复数据项）');
  explicit.push({template,preset,index:loaded.consumer.step(),label:loaded.consumer.label(),reset:loaded.consumer.reset()});
 }
 const bankA=mount(m.normalizeMonitorOutput({ymccTemplate:'A'}),'A'),initialB=bankData(bankA.prefs.ymccTemplates.B);
 bankA.prefs.ymcc.fields.fps=false;bankA.consumer.chooseDataPresetStep(2);bankA.consumer.chooseDataPresetStep(1);
 assert.equal(bankA.prefs.ymcc.fields.fps,false,'actual A chooser restores its edited simple preset memory');
 const savedA=m.normalizeMonitorOutput(snapshot(bankA.prefs)),expectedA=bankData(savedA.ymcc);
 assert.deepEqual(bankData(savedA.ymccTemplates.B),initialB,'editing A through the real fragments cannot overwrite B');
 const bankB=mount(m.switchMonitorHudTemplate(savedA,'B',10001),'B');
 assert.deepEqual(bankData(bankB.prefs.ymcc),initialB,'switching A to B loads only the saved B preset bank');
 assert.equal(bankB.consumer.step(),0);assert.equal(bankB.consumer.label(),'精简');
 bankB.prefs.ymcc.fields.cpuPower=false;bankB.consumer.chooseDataPresetStep(1);bankB.consumer.chooseDataPresetStep(0);
 assert.equal(bankB.prefs.ymcc.fields.cpuPower,false,'actual B chooser restores its edited minimal preset memory');
 const savedB=m.normalizeMonitorOutput(snapshot(bankB.prefs)),expectedB=bankData(savedB.ymcc);
 assert.deepEqual(bankData(savedB.ymccTemplates.A),expectedA,'editing B through the real fragments cannot overwrite A');
 const restoredA=mount(m.switchMonitorHudTemplate(savedB,'A',10002),'A');
 assert.deepEqual(bankData(restoredA.prefs.ymcc),expectedA,'round-trip A retains its full preset memory');
 assert.equal(restoredA.consumer.step(),1);assert.equal(restoredA.consumer.label(),'简约');assert.equal(restoredA.consumer.reset(),'A模板 · 重置为简约（只恢复数据项）');
 const restoredB=mount(m.switchMonitorHudTemplate(m.normalizeMonitorOutput(snapshot(restoredA.prefs)),'B',10003),'B');
 assert.deepEqual(bankData(restoredB.prefs.ymcc),expectedB,'round-trip B retains its full preset memory');
 assert.equal(restoredB.consumer.step(),0);assert.equal(restoredB.consumer.label(),'精简');assert.equal(restoredB.consumer.reset(),'B模板 · 重置为精简（只恢复数据项）');
 assert.deepEqual(bankA.saved,[[false,true],[false,true]]);assert.deepEqual(bankB.saved,[[false,true],[false,true]]);
 const templateContract={explicitA:{template:'A',initialIndex:1,initialLabel:'简约',initialReset:'A模板 · 重置为简约（只恢复数据项）',originalChooseSaves:saved.length},freshB:{template:'B',preset:'minimal',index:freshB.consumer.step(),label:freshB.consumer.label(),reset:freshB.consumer.reset()},savedExplicit:explicit,bankIsolation:{A:expectedA,B:expectedB,roundTripA:bankData(restoredA.prefs.ymcc),roundTripB:bankData(restoredB.prefs.ymcc),onlyPresetMemoryCompared:true,sharedDragRevisionNotMisclassifiedAsBankData:true},sourceContract:'src/bridge/monitorHudModel.ts normalization/switch and the same seven actual component fragments; no replacement model',editingTemplate:'explicit process-local Vue ref A or B; production computed reset title unchanged'};
 const doc='docs/monitor-simple-default-preset-20261014.md';const docs=fs.readFileSync(path.join(root,doc),'utf8');assert(docs.includes('作为默认')&&docs.includes('21 个独立开关')&&docs.includes('独立数据开关记忆'));
 const report={schema:'ymcc.monitor-preset-metadata.v1',status:'PASS',reviewed:PRESET_ORDER,defaultPreset:'simple',defaultPresetTemplate:'A',counts,templateContract,
  actualConsumer:{path:'src/components/MonitorOutputSettings.vue',sourceSha256:hash(text),fragments:code,steps,initialIndex:1,initialLabel:'简约',visualOrderBasis:'Actual template iterates dataPresetOptions; same array is consumed by real step/label/choose functions executed here with memory prefs/persist',fullComponentMounted:false},
  docs:{path:doc,sha256:hash(docs),meaning:'Default simple/21 fields and independent memory are specified; default-first visual order is not required'},
  originalAssertion:OLD_LABEL_ASSERT,migratedAssertion:NEW_LABEL_ASSERT,changeMeaning:'Only label order expectation; stable ID mapping, default preset, fields, saved selection and all behavior expectations remain unchanged',
  scope:'Exact model metadata plus actual component selector functions; source/template visual mapping is not full browser/component behavior'};
 fs.writeFileSync(path.join(out,'preset-contract.json'),JSON.stringify(report,null,2));return report;
}
