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
for(const name of ['src/components/GameQuickActions.vue','src/bridge/api.ts','src/gamepad/engine.ts','src/gamepad/activationEvidence.ts','src/gamepad/taskViewTouchActivation.ts'])sources[name]=fs.readFileSync(path.join(repo,name),'utf8');
const sha=s=>crypto.createHash('sha256').update(s).digest('hex');
function script(text){return text.includes('<script')?text.match(/<script[^>]*>([\s\S]*?)<\/script>/)[1]:text;}
function parse(name,text){return ts.createSourceFile(name,script(text),ts.ScriptTarget.Latest,true,ts.ScriptKind.TS);}
const component=parse('component.ts',sources['src/components/GameQuickActions.vue']);
const fn=component.statements.find(n=>ts.isFunctionDeclaration(n)&&n.name?.text==='openTaskView');
assert(fn,'production openTaskView missing');
const body=fn.getText(component);
function extractLifecycle(tree){
  const stateNames=new Set(['disposed','menuStateEpoch','taskViewSessionEpoch','controlStateEpoch','losslessReadEpoch']);
  const state=tree.statements.filter(ts.isVariableStatement).flatMap(n=>[...n.declarationList.declarations])
    .filter(d=>stateNames.has(d.name.getText(tree))).map(d=>'let '+d.getText(tree)+';').join('\n');
  const calls=tree.statements.filter(ts.isExpressionStatement).map(n=>n.expression).filter(ts.isCallExpression);
  const openWatch=calls.find(n=>n.expression.getText(tree)==='watch'&&ts.isArrowFunction(n.arguments[0])&&n.arguments[0].body.getText(tree)==='props.open');
  const unmount=calls.find(n=>n.expression.getText(tree)==='onBeforeUnmount');
  assert(openWatch&&unmount,'production menu lifecycle missing');
  return [state,openWatch.getText(tree)+';',unmount.getText(tree)+';'].join('\n');
}
const lifecycleBody=extractLifecycle(component);
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
function makeScenario({taskError=null,minimizeError=null,waitTask=false,waitMinimize=false,holdFocusTick=false}={}){
  const trace=[],busy={value:false},focusTicks=[],touchEvents=new WeakMap();let release,releaseMinimize;
  const held=new Promise(resolve=>{release=resolve;});
  const minimizeHeld=new Promise(resolve=>{releaseMinimize=resolve;});
  class FixtureElement {
    constructor(){this.isConnected=true;this.visualFocused=true;this.classList={contains:()=>this.visualFocused};this.dataset={gpGameControl:'switch-program'};this.tagName='BUTTON';this.clickCount=0;}
    matches(selector){return selector==='[data-gp-game-control="switch-program"]'&&this.dataset.gpGameControl==='switch-program';}
    closest(){return null;}
    click(){this.clickCount++;this.pending=this.onClick?.({currentTarget:this,target:this,isTrusted:false,detail:0});}
    dispatchEvent(event){this.dispatched=event;return true;}
  }
  const document={body:{tag:'body'},documentElement:{tag:'root'},activeElement:null,visibilityState:'visible',windowFocused:true,hasFocus(){return this.windowFocused;},querySelector(){return null;}};
  const context={taskViewBusy:busy,taskViewTouch:{cancel(){},request(target,evidence,isCurrent){
    // The source-extracted Shell/lifecycle regression uses an explicit touch port.
    // The real capture/IPC coordinator is executed separately below.
    if(!isCurrent())return Promise.resolve();const event={target,isTrusted:true,pointerType:'touch',detail:1};
    touchEvents.set(event,evidence);return context.run(event);}},readTaskViewSystemTouchEvidence:event=>event?touchEvents.get(event):undefined,props:{open:true,game:{pid:1}},Date:{now:()=>123456789},document,HTMLElement:FixtureElement,
    watch:(_get,callback)=>{context.menuOpenChanged=callback;},onBeforeUnmount:callback=>{context.unmountFixture=callback;},
    refreshMenuState:async()=>{},trainerMenuOpen:{value:false},closeLosslessMenu:()=>{},
    stopLosslessState:null,stopLosslessObservation:null,trainerCancelRequested:{value:false},stopTrainerState:null,stopPolicyGame:null,
    onGamepadBack:()=>{},resizeOptiDialog:()=>{},QUICKAPP_SUSPENDED_EVENT:'fixture:suspended',onSuspendedStateSync:()=>{},
    onWakeGameRecovery:()=>{},onTargetLost:()=>{},refreshGame:()=>{},fsrResolve:null,closeDialog:()=>{},
    window:{dispatchEvent:()=>true,removeEventListener:()=>{}},exports:{},Event:class {constructor(type){this.type=type;}},CustomEvent:class {constructor(type,options){this.type=type;this.detail=options?.detail;}},
    focusEligible:true,focusables:()=>context.focusEligible?[document.activeElement]:[],focusFirst:()=>trace.push({focusFirst:true}),recoverGamepadFocus:()=>false,
    commitSlider:()=>trace.push({sliderCommitted:true}),log:()=>{},
    nextTick:callback=>holdFocusTick?(focusTicks.push(callback),Promise.resolve()):Promise.resolve().then(callback),
    focusGamepadElement:element=>{trace.push({focusRestored:true});document.activeElement=element;return true;},
    invoke:async(command,args,options)=>{trace.push({command,args,options});if(command==='shell.taskView'){if(waitTask)await held;if(taskError)throw new Error(taskError);}if(command==='window.minimize'){if(waitMinimize)await minimizeHeld;if(minimizeError)throw new Error(minimizeError);}return true;},
    status:(message,error)=>trace.push({status:message,error}),};
  vm.createContext(context);
  vm.runInContext(transpile(sources['src/gamepad/activationEvidence.ts'],ts.ModuleKind.CommonJS),context,{timeout:1000});
  context.readActivationEvidence=context.exports.readActivationEvidence;
  context.withGamepadActivation=context.exports.withGamepadActivation;
  const source=`const shell={${shellText.join(',')}};const windowApi={${windowText.join(',')}};let taskViewActionSequence=0;let sliderEditMode=false,sliderRepeatInput=null,sliderRepeatDirection=0,sliderAccelStart=0,sliderLastApply=0;${lifecycleBody};${body};${rangeBody};${offerBody};${cadenceBody};${activateBody};globalThis.activateFixture=activate;globalThis.run=openTaskView;globalThis.shellApi=shell;globalThis.windowApiObject=windowApi;globalThis.setMenuOpenFixture=(open)=>{props.open=open;globalThis.menuOpenChanged(open);};globalThis.advanceGameTargetFixture=()=>{props.game={pid:2};++menuStateEpoch;};`;
  vm.runInContext(transpile(source),context,{timeout:1000});return {context,trace,busy,release,releaseMinimize,flushFocusTicks:()=>{for(const callback of focusTicks.splice(0))callback();}};
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
  check(evidence.diagnosticEventTrusted===(activation==='missing'?undefined:activation==='touch'||activation==='gamepad'),'wrong event trust snapshot');
  check(evidence.diagnosticClickDetail===(activation==='missing'?undefined:activation==='touch'||activation==='gamepad'?1:0),'wrong click detail snapshot');
  check(s.context.readActivationEvidence({currentTarget:element,isTrusted:false}).diagnosticActivation==='scripted','engine marker escaped final click');
  if(event){event.currentTarget=null;event.isTrusted=false;event.detail=999;event.pointerType='private-device';}
  await s.context.run({currentTarget:element,isTrusted:true,pointerType:'pen',detail:3});
  check(s.trace.length===1,'busy reentry must not duplicate or overwrite evidence');
  s.release();await pending;
  check(s.trace[0].args===s.trace[1].args&&evidence.diagnosticActivation===expected,'evidence must be snapshotted before await');
  check(s.trace[0].options.timeoutMs===6000&&s.trace[1].options.timeoutMs===1500,'source must not change bounded timeouts');
  check(Object.keys(evidence).every(k=>['diagnosticAction','diagnosticActionId','requestedAtUnixMs','diagnosticActivation','diagnosticEventTrusted','diagnosticClickDetail','diagnosticInputRoute'].includes(k)),'event or pointer data leaked into IPC');
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
// Run the extracted action with the production open watcher and unmount callback.
// The fake IPC and nextTick queues expose late replies without browser or OS input.
const sessionCases=[];
function invalidateMenu(s,boundary){
  if(boundary==='unmounted')s.context.unmountFixture();
  else {s.context.setMenuOpenFixture(false);if(boundary==='reopened')s.context.setMenuOpenFixture(true);}
}
async function waitForMinimize(s){
  for(let turn=0;turn<12&&!s.trace.some(r=>r.command==='window.minimize');turn++)await Promise.resolve();
  check(s.trace.some(r=>r.command==='window.minimize'),'fixture must reach the held minimize await');
}
for(const stage of ['task','minimize'])
for(const outcome of ['success','failure'])
for(const boundary of ['closed','reopened','unmounted']){
  const name=stage+'/'+outcome+'/'+boundary;
  const s=makeScenario({waitTask:stage==='task',waitMinimize:stage==='minimize',
    taskError:stage==='task'&&outcome==='failure'?'late-task-failure':null,
    minimizeError:stage==='minimize'&&outcome==='failure'?'late-minimize-failure':null});
  const element=new s.context.HTMLElement();s.context.document.activeElement=element;
  const pending=runActivation(s,element,'gamepad');s.context.document.activeElement=s.context.document.body;
  if(stage==='minimize')await waitForMinimize(s);
  invalidateMenu(s,boundary);
  s.release();s.releaseMinimize();await pending;await Promise.resolve();
  check(s.trace.filter(r=>r.command==='window.minimize').length===(stage==='minimize'?1:0),'stale TaskView reply minimized a later menu: '+name);
  check(!s.trace.some(r=>'status' in r),'stale action reported into a later menu: '+name);
  check(!s.trace.some(r=>r.focusRestored),'stale action restored its previous button: '+name);
  check(s.busy.value===false,'stale action failed to release busy: '+name);
  check(s.trace[0].args.diagnosticActivation==='gamepad-engine','stale action changed activation evidence: '+name);
  sessionCases.push(name);
  if(name==='task/success/reopened'){
    await s.context.run();
    check(s.trace.filter(r=>r.command==='shell.taskView').length===2&&s.trace.filter(r=>r.command==='window.minimize').length===1,'reopened menu cannot start a fresh action');
    check(s.trace[0].args.diagnosticActionId!==s.trace[1].args.diagnosticActionId,'reopened action reused the old evidence token');
    check(s.busy.value===false,'fresh reopened action left busy');
  }
}
for(const stage of ['task','minimize'])
for(const boundary of ['current','closed','reopened','unmounted']){
  const s=makeScenario({waitTask:true,holdFocusTick:true,taskError:stage==='task'?'task-failure':null,minimizeError:stage==='minimize'?'minimize-failure':null});
  const element=new s.context.HTMLElement();s.context.document.activeElement=element;
  const pending=runActivation(s,element,'gamepad');s.context.document.activeElement=s.context.document.body;
  s.release();await pending;
  check(s.trace.filter(r=>'status' in r).length===1,'current failure must report once before the focus tick');
  check(!s.trace.some(r=>r.focusRestored)&&s.busy.value===false,'focus tick must be held after busy release');
  if(boundary!=='current')invalidateMenu(s,boundary);
  s.flushFocusTicks();
  check(s.trace.filter(r=>r.focusRestored).length===(boundary==='current'?1:0),'queued failure focus crossed a menu session: '+stage+'/'+boundary);
  sessionCases.push('focus-tick/'+stage+'/'+boundary);
}
for(const boundary of ['closed','unmounted']){
  const s=makeScenario();invalidateMenu(s,boundary);await s.context.run();
  check(s.trace.length===0&&s.busy.value===false,'inactive menu admitted a TaskView request: '+boundary);
  sessionCases.push('admission/'+boundary);
}
{
  const s=makeScenario({waitTask:true});const pending=s.context.run();
  s.context.advanceGameTargetFixture();s.release();await pending;
  check(s.trace.map(r=>r.command||'status').join(',')==='shell.taskView,window.minimize,status','game target refresh cancelled the menu action');
  check(s.trace[0].args===s.trace[1].args&&s.busy.value===false,'game target refresh changed evidence or busy release');
  sessionCases.push('game-target-change-keeps-menu-session');
}

// Execute the actual capture coordinator with a bounded in-memory OS delivery
// port. Trusted here is an explicit fixture value, never actual Windows input.
const touchCases=[];
function touchFixture({receipt={downAccepted:true,upAccepted:true,reason:'accepted'},held=false,timeoutMs=25,prepareHeld=false}={}){
  const listeners=new Map(),trace=[],timers=new Map();let sequence=0,resolveNative,rejectNative,resolvePrepare,current=true,clock=0;
  const prepared={touchTicket:17,requestedAtUnixMs:3629};
  const preparation=new Promise(resolve=>{resolvePrepare=resolve});
  const nativePromise=new Promise((resolve,reject)=>{resolveNative=resolve;rejectNative=reject});
  const doc={visibilityState:'visible',defaultView:{innerWidth:1280,innerHeight:720},elementFromPoint:()=>target,
    addEventListener(type,fn){if(!listeners.has(type))listeners.set(type,new Set());listeners.get(type).add(fn)},
    removeEventListener(type,fn){listeners.get(type)?.delete(fn)}};
  const target={ownerDocument:doc,isConnected:true,disabled:false,rect:{left:10,right:110,top:20,bottom:60,width:100,height:40},
    matches(){return this.disabled},contains(node){return node===this},getBoundingClientRect(){return this.rect}};
  const context={exports:{},performance:{now:()=>clock},windowApi:{prepareTaskViewTouch:()=>prepareHeld?preparation:Promise.resolve(prepared),
    touchTaskViewButton:request=>{trace.push(request);return held?nativePromise:Promise.resolve(receipt)}},
    setTimeout(fn){const id=++sequence;timers.set(id,fn);return id},clearTimeout(id){timers.delete(id)},queueMicrotask};
  vm.createContext(context);
  vm.runInContext(transpile(sources['src/gamepad/taskViewTouchActivation.ts'].replace(/^import .*;\r?\n/,''),ts.ModuleKind.CommonJS),context,{timeout:1000});
  const coordinator=context.exports.createTaskViewTouchActivation({timeoutMs});
  let clicks=0,provenance;
  function emit(type,extra={}){
    const event={type,isTrusted:true,pointerType:'touch',pointerId:42,target,currentTarget:target,clientX:60,clientY:40,
      preventDefault(){this.prevented=true},stopImmediatePropagation(){this.stopped=true},...extra};
    for(const fn of [...(listeners.get(type)||[])]){fn(event);if(event.stopped)break}
    if(type==='click'&&!event.stopped){clicks++;provenance=context.exports.readTaskViewSystemTouchEvidence(event)}return event;
  }
  const evidence={diagnosticAction:'game-recognition-switch-program',diagnosticActionId:'touch:1',requestedAtUnixMs:123,diagnosticActivation:'gamepad-engine'};
  const start=()=>coordinator.request(target,evidence,()=>current);
  return {start,emit,target,doc,trace,coordinator,newCoordinator:()=>context.exports.createTaskViewTouchActivation({timeoutMs}),evidence,resolveNative,rejectNative,resolvePrepare:value=>resolvePrepare(value??prepared),advance:ms=>{clock+=ms},useImmediate:()=>{held=false},setCurrent:value=>{current=value},getClicks:()=>clicks,getProvenance:()=>provenance,
    expire:()=>{for(const fn of [...timers.values()])fn()},listenerCount:()=>[...listeners.values()].reduce((n,s)=>n+s.size,0),timerCount:()=>timers.size};
}
async function tick(){for(let turn=0;turn<8;turn++)await Promise.resolve()}
async function exerciseTouch(name,run){await run();touchCases.push(name)}
await exerciseTouch('native-receipt-alone-does-not-execute',async()=>{
 const s=touchFixture(),pending=s.start();await tick();check(s.trace.length===1&&s.getClicks()===0,'receipt replayed an action');
 check(s.trace[0].x===60&&s.trace[0].y===40&&s.trace[0].viewportWidth===1280,'DOM center/viewport not forwarded');
 s.emit('pointerdown');s.emit('pointerup');s.emit('click');await pending;
 check(s.getClicks()===1&&s.getProvenance().diagnosticInputRoute==='system-touch','OS click provenance missing');
 check(s.getProvenance().diagnosticActionId==='touch:1','touch and Shell action tokens differ');
 check(s.trace[0].touchTicket===17&&s.trace[0].touchBudgetMs===1000&&s.trace[0].requestedAtUnixMs===3629&&s.getProvenance().requestedAtUnixMs===3629,'skewed wall time reached native injection or the Shell click');
 check(s.listenerCount()===0&&s.timerCount()===0,'successful touch left resources');
});
for(const mode of ['expired','menu-changed','late-reply','moved','bad-ticket','physical-touch'])
await exerciseTouch('preparation-'+mode,async()=>{
 const s=touchFixture({prepareHeld:true}),pending=s.start(),result=pending.catch(error=>error);await tick();
 if(mode==='expired')s.advance(1001);
 if(mode==='menu-changed')s.setCurrent(false);
 if(mode==='late-reply'){s.expire();await result;}
 if(mode==='moved')s.target.rect={...s.target.rect,left:20,right:120};
 if(mode==='physical-touch'){s.emit('pointerdown');await pending;s.emit('pointerup');s.emit('click');}
 s.resolvePrepare(mode==='bad-ticket'?{touchTicket:0,requestedAtUnixMs:3629}:undefined);await tick();
 check((typeof (await result)?.message==='string')===(mode!=='physical-touch'),'wrong preparation terminal result: '+mode);
 check(s.trace.length===0&&s.listenerCount()===0&&s.timerCount()===0,'preparation sent a stale touch or left a fence: '+mode);
 check(s.getClicks()===(mode==='physical-touch'?1:0)&&!s.getProvenance(),'preparation swallowed or relabelled physical click: '+mode);
});
await exerciseTouch('event-before-native-reply-single-action',async()=>{
 const s=touchFixture({held:true}),pending=s.start();await tick();s.emit('pointerdown');s.emit('pointerup');s.emit('click');
 s.emit('click');await s.start();check(s.trace.length===1&&s.getClicks()===1,'overlap or duplicate click replayed touch');
 s.resolveNative({downAccepted:true,upAccepted:true,reason:'accepted'});await pending;check(s.listenerCount()===0,'late receipt left listeners');
});
await exerciseTouch('delivered-click-wins-lost-receipt',async()=>{
 const s=touchFixture({held:true}),pending=s.start();await tick();s.emit('pointerdown');s.emit('pointerup');s.emit('click');await pending;
 check(s.getClicks()===1&&s.listenerCount()===0&&s.timerCount()===0,'delivered click incorrectly waits for native receipt');
 s.rejectNative(Error('late lost reply'));await tick();check(s.getClicks()===1,'lost reply repeated an executed click');
});
await exerciseTouch('lost-receipt-before-delivered-click',async()=>{
 const s=touchFixture({held:true}),pending=s.start();await tick();s.rejectNative(Error('transport lost'));await tick();
 s.emit('pointerdown');s.emit('pointerup');s.emit('click');await pending;
 check(s.getClicks()===1&&s.listenerCount()===0&&s.timerCount()===0,'unknown receipt suppressed actual delivery');
});
await exerciseTouch('timeout-guards-reopened-document-until-late-click',async()=>{
 const s=touchFixture({held:true}),pending=s.start(),rejection=pending.catch(e=>e);await tick();s.coordinator.cancel();s.expire();await rejection;
 check(s.listenerCount()===4&&s.timerCount()===0,'cancelled unresolved contact lost passive fence');
 await assert.rejects(s.start());s.emit('pointerdown');s.emit('pointerup');s.emit('click');
 check(s.getClicks()===0&&s.listenerCount()===0,'late old click escaped into reopened controls');
 s.resolveNative({downAccepted:false,upAccepted:false,reason:'late known refusal'});await tick();
 s.useImmediate();const fresh=s.start();await tick();s.emit('pointerdown');s.emit('pointerup');s.emit('click');await fresh;
 check(s.getClicks()===1&&s.trace.length===2,'contact retirement prevented next explicit request');
});
await exerciseTouch('new-component-cannot-replace-old-document-owner',async()=>{
 const s=touchFixture({held:true}),pending=s.start(),rejection=pending.catch(e=>e);await tick();s.coordinator.cancel();
 const next=s.newCoordinator();await assert.rejects(next.request(s.target,s.evidence,()=>true));
 check(s.trace.length===1,'new component admitted a second native contact before old retirement');
 s.expire();await rejection;await assert.rejects(next.request(s.target,s.evidence,()=>true));
 check(s.listenerCount()===4&&s.timerCount()===0,'new instance replaced older retired guard');
 s.emit('pointerdown');s.emit('pointerup');s.emit('click');check(s.getClicks()===0&&s.listenerCount()===0,'old late touch reached new component');
 s.useImmediate();const fresh=next.request(s.target,s.evidence,()=>true);await tick();s.emit('pointerdown');s.emit('pointerup');s.emit('click');await fresh;
 check(s.getClicks()===1&&s.trace.length===2,'new document owner could not start after old contact retired');
 s.resolveNative({downAccepted:false,upAccepted:false,reason:'late old no delivery'});await tick();
 check(s.listenerCount()===0,'old native receipt resurrected/replaced document owner');
});
await exerciseTouch('two-explicit-activations-remain-usable',async()=>{
 const s=touchFixture();for(let i=0;i<2;i++){const pending=s.start();await tick();s.emit('pointerdown');s.emit('pointerup');s.emit('click');await pending}
 check(s.getClicks()===2&&s.trace.length===2&&s.listenerCount()===0,'second explicit activation lost');
});
for(const kind of ['untrusted','mouse','up-without-down','wrong-pointer','wrong-point','wrong-target','moved','disabled','detached','hidden','cancelled','old-session'])
await exerciseTouch('reject-'+kind,async()=>{
 const s=touchFixture({held:true}),pending=s.start();const rejection=pending.catch(e=>e);await tick();
 if(kind==='moved')s.target.rect={...s.target.rect,left:20,right:120};
 if(kind==='disabled')s.target.disabled=true;if(kind==='detached')s.target.isConnected=false;
 if(kind==='hidden')s.doc.visibilityState='hidden';if(kind==='cancelled')s.coordinator.cancel();if(kind==='old-session')s.setCurrent(false);
 const extra=kind==='untrusted'?{isTrusted:false}:kind==='mouse'?{pointerType:'mouse'}:kind==='wrong-point'?{clientX:80}:kind==='wrong-target'?{target:{}}:{};
 if(kind!=='up-without-down')s.emit('pointerdown',extra);s.emit('pointerup',{...extra,...(kind==='wrong-pointer'?{pointerId:99}:{})});
 s.emit('click',extra);check(s.getClicks()===0,'invalid event reached Shell: '+kind);s.expire();await rejection;
 if(s.listenerCount()){s.emit('pointerdown');s.emit('pointerup');s.emit('click')}
 check(s.trace.length===1&&s.listenerCount()===0&&s.timerCount()===0&&s.getClicks()===0,'invalid sequence replayed input or leaked resources: '+kind);
 s.resolveNative({downAccepted:true,upAccepted:true,reason:'late'});await tick();check(s.getClicks()===0,'late native reply resurrected action');
});
for(const kind of ['partial','unknown','no-delivery'])await exerciseTouch(kind+'-never-replays',async()=>{
 const s=touchFixture({held:true}),pending=s.start();const rejection=pending.catch(e=>e);await tick();
 if(kind==='unknown')s.rejectNative(Error('IPC timed out'));
 else s.resolveNative({downAccepted:kind==='partial',upAccepted:false,reason:'test refusal'});
 await tick();if(kind!=='no-delivery')check(s.listenerCount()===4,'uncertain/partial contact lost cancellation fence');
 if(kind!=='no-delivery')s.expire();await rejection;if(kind!=='no-delivery'){check(s.listenerCount()===4&&s.timerCount()===0,'timeout lost passive contact fence');await assert.rejects(s.start());s.emit('pointerdown');s.emit('pointerup');s.emit('click')}check(s.trace.length===1&&s.getClicks()===0&&s.listenerCount()===0,'failure replay/leak: '+kind);
});
for(const kind of ['disabled','hidden','covered','out-of-viewport'])await exerciseTouch('preflight-'+kind,async()=>{
 const s=touchFixture();if(kind==='disabled')s.target.disabled=true;if(kind==='hidden')s.doc.visibilityState='hidden';
 if(kind==='covered')s.doc.elementFromPoint=()=>({});if(kind==='out-of-viewport')s.target.rect={...s.target.rect,left:1400,right:1500};
 await assert.rejects(s.start());check(s.trace.length===0&&s.listenerCount()===0,'preflight issued native input: '+kind);
});
await exerciseTouch('pointercancel-rejects-click',async()=>{
 const s=touchFixture(),pending=s.start();const rejection=pending.catch(e=>e);await tick();s.emit('pointerdown');s.emit('pointercancel');s.emit('pointerup');s.emit('click');
 s.expire();await rejection;check(s.getClicks()===0&&s.listenerCount()===0,'cancelled contact executed');
});

// Bind assertions to the actual tested function/properties, while reporting any
// unrelated concurrent whole-file change instead of relabelling it as tested.
const nowComponent=parse('current.ts',fs.readFileSync(path.join(repo,'src/components/GameQuickActions.vue'),'utf8'));
const nowFn=nowComponent.statements.find(n=>ts.isFunctionDeclaration(n)&&n.name?.text==='openTaskView');
check(nowFn.getText(nowComponent)===body,'production function changed while testing');
check(extractLifecycle(nowComponent)===lifecycleBody,'production menu lifecycle changed while testing');
const nowEngine=parse('current-engine.ts',fs.readFileSync(path.join(repo,'src/gamepad/engine.ts'),'utf8'));
check(nowEngine.statements.find(n=>ts.isFunctionDeclaration(n)&&n.name?.text==='activate').getText(nowEngine)===activateBody,'production engine activate changed while testing');
check(fs.readFileSync(path.join(repo,'src/gamepad/activationEvidence.ts'),'utf8')===sources['src/gamepad/activationEvidence.ts'],'activation module changed while testing');
check(fs.readFileSync(path.join(repo,'src/bridge/api.ts'),'utf8')===sources['src/bridge/api.ts'],'bridge changed while testing');
const result={pass:true,checks,sessionCases,touchCases,menuLifecycleSha256:sha(lifecycleBody),productionFunctionSha256:sha(body),engineActivateSha256:sha(activateBody),activationModuleSha256:sha(sources['src/gamepad/activationEvidence.ts']),bridgePropertiesSha256:sha([...shellText,...windowText].join('\n')),
  sourceHashes:Object.fromEntries(Object.entries(sources).map(([k,v])=>[k,sha(v)])),
  scope:'Source-extracted JavaScript memory fixture; no product, browser, OS input or Steam execution',
  actualUiAcceptance:false};
fs.writeFileSync(path.join(out,'result.json'),JSON.stringify(result,null,2)+'\n',{flag:'wx'});
fs.writeFileSync(path.join(out,'task-view-action-result.json'),JSON.stringify(result,null,2)+'\n',{flag:'wx'});
console.log(JSON.stringify(result));
