// Hardware visibility through the shipped owner projection and actual Decky component.
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {root,validation} from './fixtures/decky_task_paths.mjs';
const {transformSync}=createRequire(path.join(root,'package.json'))('esbuild');
function load(file,modules={},globals={}) {
  const module={exports:{}};
  const code=transformSync(fs.readFileSync(path.join(root,file),'utf8'),{loader:file.endsWith('.tsx')?'tsx':'ts',format:'cjs',jsxFactory:'SP_REACT.createElement',jsxFragment:'SP_REACT.Fragment'}).code;
  vm.runInNewContext(code,{module,exports:module.exports,structuredClone,...globals,require:n=>{assert(Object.hasOwn(modules,n),'Unexpected import '+n);return modules[n];}});
  return module.exports;
}
const bankModel=load('src/bridge/performanceControls.ts');
const model=load('decky-plugin/src/performanceModel.ts');
const adapter=load('src/bridge/deckyPerformanceActions.ts',{'./performanceControls':bankModel,'../../decky-plugin/src/performanceModel':model});
const react={Fragment:'Fragment',createElement:(type,props,...children)=>({type,props:props??{},children}),useState:()=>[null,()=>{}],useEffect:()=>{}};
const widgets=Object.fromEntries(['PanelSectionRow','SliderField','ToggleField','Focusable'].map(k=>[k,k]));
const {PerformanceControls}=load('decky-plugin/src/PerformanceControls.tsx',{'./performanceModel':model,'./AnchoredDropdown':{AnchoredDropdown:'AnchoredDropdown'}},{SP_REACT:react,DFL:widgets});
function view({hybrid=false,mode='auto',side='ac',scope='global',unlimited=false,smt=true}={}) {
  const banks=bankModel.normalizeContinuousControls({ac:{tdp:20,tdpCeiling:35,mode,manual:{unlimited}},dc:{tdp:10,tdpCeiling:20,mode,manual:{unlimited}}});
  return adapter.performanceMirrorView({identity:{scope,side,exe:scope==='dedicated'?'game.exe':'',pid:0,processCreated:'',generation:1,revision:'saved-1'},controls:banks,bank:banks[side],side,hardwareMinimum:5,hardwareMaximum:35,hybrid,smtAvailable:smt,hyperThreadMode:'default',configured:true},'visibility-1');
}
function flatten(n,out=[]) {if(Array.isArray(n))n.forEach(x=>flatten(x,out));else if(n&&typeof n==='object'){out.push(n);flatten(n.children,out);}return out;}
function render(v,disabled=false) {return flatten(PerformanceControls({view:v,disabled,draft:{},change:()=>{throw Error('Rendering must not change settings');}}));}
const keys=v=>Array.from(model.visiblePerformanceControls(v),c=>c.key);
const hasLabel=(nodes,label)=>nodes.some(n=>n.props.label===label);
const coreAuto=['coreHigh','coreLow','coreSplit'];
const cases=[];
function check(name,fn) {fn();cases.push(name);}
check('uniform CPUs omit all three automatic core controls from the rendered product component',()=>{
  const v=view(),nodes=render(v);assert(coreAuto.every(k=>!keys(v).includes(k)));
  assert(!nodes.some(n=>n.props['data-ymcc-performance-row']==='autoCore'));
  assert(!hasLabel(nodes,'高功耗核心'));assert(!hasLabel(nodes,'低功耗核心'));
  assert.deepEqual(keys(v),['tdp','mode','bias','saving','hyperThreadMode']);
});
check('hybrid CPUs retain the paired choices, threshold slider and native horizontal focus group',()=>{
  const v=view({hybrid:true}),nodes=render(v);assert(coreAuto.every(k=>keys(v).includes(k)));
  const pair=nodes.find(n=>n.props['data-ymcc-performance-row']==='autoCore');assert(pair);
  assert.equal(pair.type,'Focusable');assert.equal(pair.props.focusable,false);assert.equal(pair.props['flow-children'],'row');
  assert(hasLabel(nodes,'高功耗核心'));assert(hasLabel(nodes,'低功耗核心'));
});
check('uniform CPUs also omit the manual core selector and leave SMT in a full row',()=>{
  const nodes=render(view({mode:'manual'}));assert(!hasLabel(nodes,'大小核心'));assert(hasLabel(nodes,'超线程'));
  assert(!nodes.some(n=>n.props['data-ymcc-performance-row']==='manualCore'));
});
check('hybrid manual mode retains both core and SMT controls in the original native row',()=>{
  const nodes=render(view({mode:'manual',hybrid:true}));assert(hasLabel(nodes,'大小核心'));assert(hasLabel(nodes,'超线程'));
  assert(nodes.some(n=>n.props['data-ymcc-performance-row']==='manualCore'&&n.props['flow-children']==='row'));
});
check('busy and read-only pages preserve controls on supported hardware',()=>{
  const v=view({hybrid:true});v.enabled=false;const nodes=render(v,true);
  assert(coreAuto.every(k=>keys(v).includes(k)));assert(hasLabel(nodes,'高功耗核心'));assert(hasLabel(nodes,'低功耗核心'));
  assert(nodes.filter(n=>n.type==='AnchoredDropdown').every(n=>n.props.disabled));
});
check('disabled frequency sliders remain visible when unlimited frequency is selected',()=>{
  const v=view({mode:'manual',unlimited:true});const frequency=v.controls.find(c=>c.key==='frequencyMHz');assert.equal(frequency.enabled,false);
  assert(keys(v).includes('frequencyMHz'));assert(render(v).some(n=>n.type==='SliderField'&&n.props.disabled&&n.props.max===7200));
});
check('unavailable SMT is kept visible rather than hiding unrelated disabled controls',()=>{
  const v=view({smt:false});assert(keys(v).includes('hyperThreadMode'));assert(hasLabel(render(v),'超线程'));
});
check('rendering and filtering preserve owner values, ordering and hardware flags',()=>{
  const v=view({hybrid:true}),before=JSON.stringify(v);model.visiblePerformanceControls(v);render(v);assert.equal(JSON.stringify(v),before);
});
check('capability changes re-evaluate visibility without leaving an empty core group',()=>{
  const hybrid=render(view({hybrid:true})),uniform=render(view());assert(hybrid.some(n=>n.props['data-ymcc-performance-row']==='autoCore'));
  assert(!uniform.some(n=>n.props['data-ymcc-performance-row']==='autoCore'));assert(hasLabel(uniform,'节能浮动'));
});
check('hidden core controls remain rejected by the existing authenticated patch validator',()=>{
  const v=view();for(const patch of [{coreHigh:'littleOnly'},{coreLow:'bigOnly'},{coreSplit:10}])assert.equal(model.performancePatchAllowed(patch,v),false);
  assert.equal(model.performancePatchAllowed({tdp:21},v),true);
});
check('hardware visibility follows the same rule across AC/DC, global/game and both modes',()=>{
  for(const side of ['ac','dc'])for(const scope of ['global','dedicated'])for(const mode of ['auto','manual'])for(const hybrid of [false,true]) {
    const v=view({side,scope,mode,hybrid}),coreKeys=mode==='auto'?coreAuto:['coreMode'];
    assert(coreKeys.every(k=>keys(v).includes(k)===hybrid));assert.equal(model.validPerformanceMirror(v),true);
  }
});
check('saving still immediately precedes SMT after unsupported core rows are removed',()=>{
  const rows=render(view()).filter(n=>n.type==='PanelSectionRow');const saving=rows.findIndex(n=>hasLabel(flatten(n.children),'节能浮动'));
  const smt=rows.findIndex(n=>hasLabel(flatten(n.children),'超线程'));assert(saving>=0);assert.equal(smt,saving+1);
});
const result={status:'passed',cases:cases.length,results:cases,productionOwnerProjection:true,productionDeckyComponent:true,hardwareWrites:0,physicalControllerTested:false};
fs.writeFileSync(path.join(validation,'HYBRID-CORE-VISIBILITY.json'),JSON.stringify(result,null,2)+'\n');
console.log('Hybrid core visibility PASS '+cases.length);
