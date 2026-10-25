import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import { fileURLToPath } from 'node:url';
import ts from 'typescript';

const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const args=process.argv.slice(2),specified=args[0]==='--out'?args[1]:args[0];
const out=path.resolve(specified||path.resolve(repo,'../../..','Build/Validation/CtrlAlt-TaskView-ActionTrace-20261015/frontend-tests'));
fs.mkdirSync(out,{recursive:true});
const sources={};
for(const name of ['src/components/GameQuickActions.vue','src/bridge/api.ts','src/gamepad/engine.ts','src/gamepad/activationEvidence.ts'])sources[name]=fs.readFileSync(path.join(repo,name),'utf8');
const sha=s=>crypto.createHash('sha256').update(s).digest('hex');
function script(text){return text.includes('<script')?text.match(/<script[^>]*>([\s\S]*?)<\/script>/)[1]:text;}
function parse(name,text){return ts.createSourceFile(name,script(text),ts.ScriptTarget.Latest,true,ts.ScriptKind.TS);}
const component=parse('component.ts',sources['src/components/GameQuickActions.vue']);
const fn=component.statements.find(n=>ts.isFunctionDeclaration(n)&&n.name?.text==='openTaskView');
assert(fn,'production openTaskView missing');
const body=fn.getText(component);
const engine=parse('engine.ts',sources['src/gamepad/engine.ts']);
const activateFn=engine.statements.find(n=>ts.isFunctionDeclaration(n)&&n.name?.text==='activate');
const rangeFn=engine.statements.find(n=>ts.isFunctionDeclaration(n)&&n.name?.text==='isRangeInput');
assert(activateFn&&rangeFn,'production activation functions missing');
const offerFn=engine.statements.find(n=>ts.isFunctionDeclaration(n)&&n.name?.text==='offerPageCommand');assert(offerFn,'production page-command boundary missing');
const cadenceFn=engine.statements.find(n=>ts.isFunctionDeclaration(n)&&n.name?.text==='resetSliderCadence');assert(cadenceFn,'production edit reset missing');
const cadenceBody=cadenceFn.getText(engine);
const activateBody=activateFn.getText(engine),rangeBody=rangeFn.getText(engine),offerBody=offerFn.getText(engine);
assert(component.statements.some(n=>ts.isImportDeclaration(n)&&n.moduleSpecifier.text==='@/gamepad/activationEvidence'&&n.importClause.namedBindings.elements.some(e=>e.name.text==='readActivationEvidence')),'component must use the tested module');
assert(engine.statements.some(n=>ts.isImportDeclaration(n)&&n.moduleSpecifier.text==='@/gamepad/activationEvidence'&&n.importClause.namedBindings.elements.some(e=>e.name.text==='withGamepadActivation')),'engine must use the tested module');
assert(/data-gp-game-control="switch-program"[^>]*@click="openTaskView"/.test(sources['src/components/GameQuickActions.vue']),'switch-program must receive the Vue click event');
const api=parse('api.ts',sources['src/bridge/api.ts']);
function objectMembers(name,keys){
  let value;
  for(const node of api.statements)if(ts.isVariableStatement(node))for(const d of node.declarationList.declarations)if(d.name.getText(api)===name)value=d.initializer;
  assert(value&&ts.isObjectLiteralExpression(value),`production ${name} missing`);
  return keys.map(key=>{const p=value.properties.find(p=>p.name?.getText(api)===key);assert(p,`production ${name}.${key} missing`);return p.getText(api);});
}
const shellText=objectMembers('shell',['taskView','taskViewWithEvidence']);
const windowText=objectMembers('windowApi',['minimize','minimizeWithEvidence']);
function transpile(text,module=ts.ModuleKind.None){const r=ts.transpileModule(text,{compilerOptions:{target:ts.ScriptTarget.ES2022,module},reportDiagnostics:true});assert(!(r.diagnostics||[]).some(d=>d.category===ts.DiagnosticCategory.Error));return r.outputText;}
let checks=0;function check(condition,message){checks++;assert(condition,message);}
function makeScenario({taskError=null,minimizeError=null,waitTask=false}={}){
  const trace=[],busy={value:false};let release;
  const held=new Promise(resolve=>{release=resolve;});
  class FixtureElement {
    constructor(){this.isConnected=true;this.visualFocused=true;this.classList={contains:()=>this.visualFocused};this.dataset={gpGameControl:'switch-program'};this.tagName='BUTTON';this.clickCount=0;}
    matches(selector){return selector==='[data-gp-game-control="switch-program"]'&&this.dataset.gpGameControl==='switch-program';}
    closest(){return null;}
    click(){this.clickCount++;this.pending=this.onClick?.({currentTarget:this,target:this,isTrusted:false,detail:0});}
    dispatchEvent(event){this.dispatched=event;return true;}
  }
  const document={body:{tag:'body'},documentElement:{tag:'root'},activeElement:null,visibilityState:'visible',windowFocused:true,hasFocus(){return this.windowFocused;},querySelector(){return null;}};
  const context={taskViewBusy:busy,Date:{now:()=>123456789},document,HTMLElement:FixtureElement,
    window:{dispatchEvent:()=>true},exports:{},Event:class {constructor(type){this.type=type;}},CustomEvent:class {constructor(type,options){this.type=type;this.detail=options?.detail;}},
    focusEligible:true,focusables:()=>context.focusEligible?[document.activeElement]:[],focusFirst:()=>trace.push({focusFirst:true}),recoverGamepadFocus:()=>false,
    commitSlider:()=>trace.push({sliderCommitted:true}),log:()=>{},
    nextTick:callback=>Promise.resolve().then(callback),
    focusGamepadElement:element=>{trace.push({focusRestored:true});document.activeElement=element;return true;},
    invoke:async(command,args,options)=>{trace.push({command,args,options});if(command==='shell.taskView'){if(waitTask)await held;if(taskError)throw new Error(taskError);}if(command==='window.minimize'&&minimizeError)throw new Error(minimizeError);return true;},
    status:(message,error)=>trace.push({status:message,error}),};
  vm.createContext(context);
  vm.runInContext(transpile(sources['src/gamepad/activationEvidence.ts'],ts.ModuleKind.CommonJS),context,{timeout:1000});
  context.readActivationEvidence=context.exports.readActivationEvidence;
  context.withGamepadActivation=context.exports.withGamepadActivation;
  const source=`const shell={${shellText.join(',')}};const windowApi={${windowText.join(',')}};let taskViewActionSequence=0;let sliderEditMode=false,sliderRepeatInput=null,sliderRepeatDirection=0,sliderAccelStart=0,sliderLastApply=0;${body};${rangeBody};${offerBody};${cadenceBody};${activateBody};globalThis.activateFixture=activate;globalThis.run=openTaskView;globalThis.shellApi=shell;globalThis.windowApiObject=windowApi;`;
  vm.runInContext(transpile(source),context,{timeout:1000});return {context,trace,busy,release};
}
function clickEvent(element,source){
  if(source==='missing')return undefined;
  return {currentTarget:element,target:element,isTrusted:source==='touch',...(source==='touch'?{pointerType:'touch'}:{}),detail:source==='touch'?1:0};
}
function runActivation(s,element,source){
  const event=clickEvent(element,source);
  if(source==='gamepad'){
    element.onClick=()=>s.context.run(event);
    s.context.activateFixture({});
    return element.pending;
  }
  return s.context.run(event);
}
{
  const s=makeScenario();await s.context.run();
  check(s.trace.map(x=>x.command||'status').join(',')==='shell.taskView,window.minimize,status','normal order changed');
  const [a,b]=s.trace;
  check(a.args===b.args,'one action must share a caller token across commands');
  check(a.args.diagnosticAction==='game-recognition-switch-program','wrong entry metadata');
  check(/^[A-Za-z0-9:._-]{1,80}$/.test(a.args.diagnosticActionId),'caller token exceeds native retention contract');
  check(s.busy.value===false,'normal action left busy');
  check(a.options.timeoutMs===6000&&b.options.timeoutMs===1500,'request and handover timeouts must be finite');
  check(a.args.requestedAtUnixMs===123456789,'native request needs admission timestamp');
  check(s.trace[2].status==='已请求切换程序','success must describe API request, not UI visibility');
  const firstToken=a.args.diagnosticActionId;await s.context.run();
  check(s.trace[3].args.diagnosticActionId!==firstToken,'same-clock repeated action lost distinction');
  check(s.trace[3].args===s.trace[4].args,'second operation tokens differ');
}
{
  const s=makeScenario({waitTask:true});const first=s.context.run();
  check(s.busy.value===true&&s.trace.length===1,'first action must be in flight');
  await s.context.run();check(s.trace.length===1,'busy reentry duplicated an action');
  s.release();await first;check(s.trace.filter(x=>x.command==='shell.taskView').length===1&&s.trace.filter(x=>x.command==='window.minimize').length===1,'in-flight action was lost');
  check(s.busy.value===false,'busy state not released');
}
{
  const s=makeScenario({taskError:'synthetic-send-failed'});await s.context.run();
  check(s.trace.length===2&&s.trace[0].command==='shell.taskView'&&s.trace[1].error==='切换 Windows 任务视图失败：synthetic-send-failed','send failure behavior/message changed');
  check(!s.trace.some(x=>x.command==='window.minimize'),'send failure must not minimize');
  check(s.busy.value===false,'send failure left busy');
}
{
  const s=makeScenario({minimizeError:'synthetic-post-failed'});await s.context.run();
  check(s.trace.map(x=>x.command||'status').join(',')==='shell.taskView,window.minimize,status','minimize failure control order changed');
  check(s.trace[2].error==='已请求切换程序，但 YMCC 未能收起：synthetic-post-failed','post failure must remain visible to the user');
  check(s.busy.value===false,'post failure left busy');
}
{
  const s=makeScenario();const event={isTrusted:true,type:'click'};
  await s.context.shellApi.taskView(event);await s.context.windowApiObject.minimize(event);
  check(s.trace[0].args.requestedAtUnixMs===123456789&&s.trace[1].args===undefined,'generic wrappers must not forward browser event objects');
  check(s.trace[0].options.timeoutMs===6000,'generic TaskView request must have finite frontend wait');
}
{
  const s=makeScenario({taskError:'IPC command timed out: shell.taskView'});await s.context.run();
  check(s.busy.value===false,'an IPC timeout must release the button');
  check(!s.trace.some(x=>x.command==='window.minimize'),'timeout must not minimize after unknown API outcome');
  await s.context.run();
  check(s.trace.filter(x=>x.command==='shell.taskView').length===2,'timeout must allow the next explicit request');
}
// Exercise the existing focus restoration boundaries for every relevant source.
for(const activation of ['missing','gamepad','touch','scripted'])
for(const boundary of ['eligible','other-control','page-hidden','window-unfocused','detached','highlight-changed','success']){
  const s=makeScenario({waitTask:true,taskError:boundary==='success'?null:'failure'});
  const element=new s.context.HTMLElement();s.context.document.activeElement=element;
  const pending=runActivation(s,element,activation);s.context.document.activeElement=s.context.document.body;
  if(boundary==='other-control')s.context.document.activeElement=new s.context.HTMLElement();
  if(boundary==='page-hidden')s.context.document.visibilityState='hidden';
  if(boundary==='window-unfocused')s.context.document.windowFocused=false;
  if(boundary==='detached')element.isConnected=false;
  if(boundary==='highlight-changed')element.visualFocused=false;
  s.release();await pending;await Promise.resolve();
  check(s.trace.filter(r=>r.focusRestored).length===(boundary==='eligible'?1:0),'local focus restoration escaped its boundary: '+boundary);
  check(s.busy.value===false,'focus boundary left busy: '+boundary);
  const expected={missing:'unknown',gamepad:'gamepad-engine',touch:'trusted-touch',scripted:'scripted'}[activation];
  check(s.trace[0].args.diagnosticActivation===expected,'focus behavior lost activation evidence: '+activation+'/'+boundary);
  check(s.trace[0].options.timeoutMs===6000,'activation changed TaskView timeout');
  if(boundary==='success')check(s.trace[1].options.timeoutMs===1500&&s.trace[0].args===s.trace[1].args,'activation changed bounded minimize or shared evidence');
}
// Use the production engine function, module, component function and wrappers together.
for(const activation of ['gamepad','touch','scripted','missing']){
  const s=makeScenario({waitTask:true});
  const element=new s.context.HTMLElement();s.context.document.activeElement=element;
  const event=clickEvent(element,activation);
  if(activation==='gamepad')element.onClick=()=>s.context.run(event);
  const pending=activation==='gamepad'?(s.context.activateFixture({}),element.pending):s.context.run(event);
  const evidence=s.trace[0].args;
  const expected={gamepad:'gamepad-engine',touch:'trusted-touch',scripted:'scripted',missing:'unknown'}[activation];
  check(evidence.diagnosticActivation===expected,'wrong production activation: '+activation);
  check(evidence.diagnosticEventTrusted===(activation==='missing'?undefined:activation==='touch'),'wrong event trust snapshot');
  check(evidence.diagnosticClickDetail===(activation==='missing'?undefined:activation==='touch'?1:0),'wrong click detail snapshot');
  check(s.context.readActivationEvidence({currentTarget:element,isTrusted:false}).diagnosticActivation==='scripted','engine marker escaped final click');
  if(event){event.currentTarget=null;event.isTrusted=false;event.detail=999;event.pointerType='private-device';}
  await s.context.run({currentTarget:element,isTrusted:true,pointerType:'pen',detail:3});
  check(s.trace.length===1,'busy reentry must not duplicate or overwrite evidence');
  s.release();await pending;
  check(s.trace[0].args===s.trace[1].args&&evidence.diagnosticActivation===expected,'evidence must be snapshotted before await');
  check(s.trace[0].options.timeoutMs===6000&&s.trace[1].options.timeoutMs===1500,'source must not change bounded timeouts');
  check(Object.keys(evidence).every(k=>['diagnosticAction','diagnosticActionId','requestedAtUnixMs','diagnosticActivation','diagnosticEventTrusted','diagnosticClickDetail'].includes(k)),'event or pointer data leaked into IPC');
  check(!s.trace.some(row=>row.focusRestored)&&s.busy.value===false,'success focus or busy behavior changed');
}
for(const [pointerType,expected] of [['pen','trusted-pen'],['mouse','trusted-mouse'],['','trusted-other'],[undefined,'trusted-other'],['unrecognized','trusted-other']]){
  const s=makeScenario();
  await s.context.run({currentTarget:new s.context.HTMLElement(),isTrusted:true,pointerType,detail:7});
  check(s.trace[0].args.diagnosticActivation===expected&&s.trace[0].args.diagnosticClickDetail===3,'trusted source or bounded detail changed');
}
{
  const s=makeScenario();const element=new s.context.HTMLElement(),other=new s.context.HTMLElement();
  await s.context.withGamepadActivation(other,()=>s.context.run({currentTarget:element,target:other,isTrusted:false,detail:NaN}));
  check(s.trace[0].args.diagnosticActivation==='scripted','different currentTarget must not inherit engine provenance');
  check(!('diagnosticClickDetail' in s.trace[0].args),'nonfinite detail must be omitted at IPC');
}
for(const branch of ['ordinary','custom-dropdown','native-dropdown','select','range','ineligible']){
  const s=makeScenario(),element=new s.context.HTMLElement();s.context.document.activeElement=element;
  if(branch==='ordinary')element.dataset.gpGameControl='other';
  if(branch==='custom-dropdown')element.dataset.gpDropdown='';
  if(branch==='native-dropdown')element.dataset.gpNativeDropdown='';
  if(branch==='select'){element.tagName='SELECT';element.options=[{},{}];element.selectedIndex=0;}
  if(branch==='range'){element.tagName='INPUT';element.type='range';}
  if(branch==='ineligible')s.context.focusEligible=false;
  let observed;
  element.onClick=event=>{observed=s.context.readActivationEvidence(event).diagnosticActivation;};
  s.context.activateFixture({});
  const clicked=branch==='ordinary'||branch==='native-dropdown';
  check(element.clickCount===(clicked?1:0),'engine branch click count changed: '+branch);
  if(clicked)check(observed==='scripted','non-final or other-control click acquired engine marker: '+branch);
  check(!s.trace.some(row=>row.command),'unrelated engine branch invoked task view: '+branch);
}
{
  const s=makeScenario(),element=new s.context.HTMLElement();s.context.document.activeElement=element;
  const failure=new Error('click-throw');element.onClick=()=>{throw failure;};
  let caught;try{s.context.activateFixture({});}catch(error){caught=error;}
  check(caught===failure,'engine click exception changed');
  check(s.context.readActivationEvidence({currentTarget:element,isTrusted:false}).diagnosticActivation==='scripted','throwing engine click leaked marker');
}
// Bind assertions to the actual tested function/properties, while reporting any
// unrelated concurrent whole-file change instead of relabelling it as tested.
const nowComponent=parse('current.ts',fs.readFileSync(path.join(repo,'src/components/GameQuickActions.vue'),'utf8'));
const nowFn=nowComponent.statements.find(n=>ts.isFunctionDeclaration(n)&&n.name?.text==='openTaskView');
check(nowFn.getText(nowComponent)===body,'production function changed while testing');
const nowEngine=parse('current-engine.ts',fs.readFileSync(path.join(repo,'src/gamepad/engine.ts'),'utf8'));
check(nowEngine.statements.find(n=>ts.isFunctionDeclaration(n)&&n.name?.text==='activate').getText(nowEngine)===activateBody,'production engine activate changed while testing');
check(fs.readFileSync(path.join(repo,'src/gamepad/activationEvidence.ts'),'utf8')===sources['src/gamepad/activationEvidence.ts'],'activation module changed while testing');
check(fs.readFileSync(path.join(repo,'src/bridge/api.ts'),'utf8')===sources['src/bridge/api.ts'],'bridge changed while testing');
const result={pass:true,checks,productionFunctionSha256:sha(body),engineActivateSha256:sha(activateBody),activationModuleSha256:sha(sources['src/gamepad/activationEvidence.ts']),bridgePropertiesSha256:sha([...shellText,...windowText].join('\n')),
  sourceHashes:Object.fromEntries(Object.entries(sources).map(([k,v])=>[k,sha(v)])),
  scope:'Source-extracted JavaScript memory fixture; no product, browser, OS input or Steam execution',
  actualUiAcceptance:false};
fs.writeFileSync(path.join(out,'result.json'),JSON.stringify(result,null,2)+'\n');
fs.writeFileSync(path.join(out,'task-view-action-result.json'),JSON.stringify(result,null,2)+'\n');
console.log(JSON.stringify(result));
