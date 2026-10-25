import assert from 'node:assert/strict';
import path from 'node:path';
import vm from 'node:vm';
import {createRequire} from 'node:module';
export async function verifyMouseStatusHint(root) {
 const {build}=createRequire(path.join(root,'package.json'))('esbuild');
 const compiled=await build({stdin:{contents:"export {admitDesktopHudFrame} from './src/bridge/desktopHudProtocol';export {acceptMouseModeState,mouseModeState} from './src/bridge/mouseModeState';export {createMouseStatusHint,MOUSE_STATUS_HINT_MS} from './src/bridge/mouseStatusHint';export {mouseModeText} from './src/bridge/mouseModePresentation';export {normalizeMonitorOutput} from './src/bridge/monitorHudModel';export {shouldPresentShortcutFeedback} from './src/bridge/shortcutActionFeedback';",resolveDir:root,loader:'ts'},bundle:true,write:false,platform:'browser',format:'iife',globalName:'consumer',plugins:[{name:'isolated-ipc',setup(b){b.onResolve({filter:/^\.\/ipc$/},()=>({path:'ipc',namespace:'memory'}));b.onLoad({filter:/.*/,namespace:'memory'},()=>({contents:'export const on=()=>()=>{};',loader:'js'}));}}]});
 const context=vm.createContext({structuredClone,setTimeout,clearTimeout});vm.runInContext(compiled.outputFiles[0].text,context);
 const {createMouseStatusHint,mouseModeText,normalizeMonitorOutput,shouldPresentShortcutFeedback}=context.consumer;
 const cases=[];const need=(v,name)=>{assert.ok(v,name);cases.push(name);};
 const base={mode:'auto',on:true,stateKnown:true,provider:'ymcc',controlAllowed:true,mappingVerified:false,verification:'process-liveness',scene:'desktop',pending:false,ok:true,sequence:1,policyReason:'desktop_foreground'};
 let now=0,id=0,shown=null;const tasks=new Map();
 const hint=createMouseStatusHint({emit:s=>shown=s,setTimer:(fn,ms)=>{tasks.set(++id,{at:now+ms,fn});return id;},clearTimer:i=>tasks.delete(i)});
 const accept=(delta={},enabled=true)=>hint.accept({...base,...delta},enabled);
 const advance=ms=>{now+=ms;for(const [i,t] of [...tasks])if(t.at<=now){tasks.delete(i);t.fn();}};
 accept();need(shown===null&&tasks.size===0,'initial-hud-mount-is-quiet');
 accept({on:false,scene:'external',sequence:2});need(mouseModeText(shown).title==='手柄模式','foreground-game-shows-controller-mode');
 advance(1500);accept({on:false,scene:'external',sequence:3});advance(1001);need(shown===null&&tasks.size===0,'duplicate-packets-do-not-extend-2500ms');
 accept({on:false,scene:'external',sequence:4});need(shown===null,'expired-state-does-not-reappear');
 accept({sequence:5});need(mouseModeText(shown).title==='鼠标模式'&&mouseModeText(shown).detail.includes('映射待确认'),'desktop-title-retains-provider-verification');
 accept({on:false,scene:'ymcc',sequence:4});need(shown?.on===true,'stale-sequence-cannot-replace-new-state');
 accept({on:false,scene:'ymcc',sequence:6});need(shown===null&&tasks.size===0,'ymcc-entry-hides-and-stays-silent');
 accept({sequence:7,pending:true});need(shown===null,'ymcc-exit-pending-is-silent');
 accept({sequence:8});need(shown===null,'ymcc-exit-desktop-settlement-is-silent');
 accept({sequence:9});need(shown===null,'ymcc-exit-repeated-frame-remains-silent');
 accept({mode:'on',sequence:10});need(shown===null&&tasks.size===0,'manual-on-hides-immediately');
 accept({mode:'off',on:false,sequence:11});need(shown===null,'manual-off-never-shows-hint');
 accept({sequence:12,pending:true});need(shown===null,'auto-mode-entry-waits-for-settled-result');
 accept({sequence:13});need(shown?.on===true,'entering-auto-emits-settled-mode');
 accept({sequence:14},false);need(shown===null&&tasks.size===0,'disable-setting-cancels-current-hint');
 accept({on:false,scene:'external',sequence:15},false);need(shown===null,'disabled-setting-observes-without-rendering');
 accept({on:false,scene:'external',sequence:16},true);need(shown===null,'reenable-setting-does-not-replay');
 accept({sequence:17});need(shown?.on===true,'new-change-after-reenable-renders');
 accept({on:false,scene:'external',sequence:18,pending:true});need(shown===null,'pending-hides-old-confirmed-state');
 accept({on:false,scene:'external',sequence:19});need(shown?.on===false,'pending-transition-emits-only-settled-state');
 accept({provider:'steam',controlAllowed:false,stateKnown:false,sequence:20});need(shown===null,'steam-managed-persona-hides');
 accept({sequence:21});need(shown?.on===true,'return-to-auto-ymcc-is-a-mode-change');
 accept({on:false,ok:false,scene:'external',sequence:22});need(shown===null,'failed-policy-does-not-claim-controller-success');
 hint.dispose();need(tasks.size===0&&shown===null,'dispose-releases-timer');
 accept({on:false,scene:'external',sequence:23});need(tasks.size===0&&shown===null,'disposed-consumer-cannot-resurrect');
 const initialPending=createMouseStatusHint({emit:s=>shown=s,setTimer:()=>0,clearTimer:()=>{}});
 initialPending.accept({...base,pending:true});initialPending.accept({...base,sequence:2});need(shown===null,'initial-pending-settlement-is-quiet');initialPending.dispose();
 const prefs=normalizeMonitorOutput({mouseStatusEnabled:false});need(prefs.mouseStatusEnabled===false,'mouse-status-preference-normalizes-false');need(normalizeMonitorOutput({}).mouseStatusEnabled===true,'mouse-status-preference-defaults-enabled');
 for(const state of ['applied','failed','pending','saved'])need(!shouldPresentShortcutFeedback('mouse.toggle',state),'legacy-large-mouse-notice-rejected-'+state);
 need(shouldPresentShortcutFeedback('display.brightnessAdjust','applied')&&shouldPresentShortcutFeedback('app.updateReady','saved'),'unrelated-notifications-stay-visible');
 // Exhaust all supported foreground pairs in all three modes/personas/settings,
 // with both a direct receipt and a pending transition. YMCC is silent both ways.
 const scenes=['desktop','explorer','game-window','game-full','ymcc','other-app','unknown'];
 const sample=(mode,provider,scene,sequence)=>({...base,mode,provider,scene:scene==='explorer'?'desktop':scene.startsWith('game-')||scene==='other-app'?'external':scene,on:mode==='on'||(mode==='auto'&&['desktop','explorer'].includes(scene)),controlAllowed:provider==='ymcc',stateKnown:provider==='ymcc',sequence});
 let matrixCases=0;
 for(const mode of ['auto','on','off'])for(const provider of ['ymcc','steam'])for(const enabled of [true,false])for(const pending of [true,false])for(const from of scenes)for(const to of scenes){
  let visible=null;const matrix=createMouseStatusHint({emit:v=>visible=v,setTimer:()=>1,clearTimer:()=>{}});
  const a=sample(mode,provider,from,1),b=sample(mode,provider,to,3);matrix.accept(a,enabled);
  if(pending)matrix.accept({...b,pending:true,sequence:2},enabled);matrix.accept(b,enabled);
  const expected=mode==='auto'&&provider==='ymcc'&&enabled&&from!=='ymcc'&&to!=='ymcc'&&a.on!==b.on;
  assert.equal(!!visible,expected,`foreground-matrix ${mode}/${provider}/${enabled}/${pending}: ${from}->${to}`);
  matrix.accept({...b,sequence:4},enabled);assert.equal(!!visible,expected,'duplicate-matrix-frame');matrix.dispose();matrixCases++;
 }
 need(matrixCases===1176,'1176-foreground-mode-persona-setting-pending-combinations');
 let modeMatrixCases=0;
 for(const fromMode of ['auto','on','off'])for(const toMode of ['auto','on','off'])for(const provider of ['ymcc','steam'])for(const enabled of [true,false])for(const pending of [true,false])for(const scene of scenes){
  let visible=null;const matrix=createMouseStatusHint({emit:v=>visible=v,setTimer:()=>1,clearTimer:()=>{}});
  matrix.accept(sample(fromMode,provider,scene,1),enabled);
  const next=sample(toMode,provider,scene,3);if(pending)matrix.accept({...next,pending:true,sequence:2},enabled);matrix.accept(next,enabled);
  const expected=fromMode!=='auto'&&toMode==='auto'&&provider==='ymcc'&&enabled&&scene!=='ymcc';
  assert.equal(!!visible,expected,`mode-matrix ${provider}/${enabled}/${pending}/${scene}: ${fromMode}->${toMode}`);matrix.dispose();modeMatrixCases++;
 }
 need(modeMatrixCases===504,'504-mode-entry-persona-setting-pending-combinations');

 for(const target of scenes){
  let visible=null;const visit=createMouseStatusHint({emit:v=>visible=v,setTimer:()=>1,clearTimer:()=>{}});
  visit.accept(sample('auto','ymcc','desktop',1));visit.accept({...sample('auto','ymcc','ymcc',2),ok:false});visit.accept({...sample('auto','ymcc',target,3),pending:true});visit.accept(sample('auto','ymcc',target,4));
  need(visible===null,'ymcc-failure-and-late-return-silent-'+target);visit.dispose();
 }
 // Enabling auto inside YMCC is also silent through its later minimize result.
 let visible=null;const visit=createMouseStatusHint({emit:v=>visible=v,setTimer:()=>1,clearTimer:()=>{}});
 visit.accept({...base,mode:'off',on:false,scene:'ymcc'});visit.accept({...base,on:false,scene:'ymcc',sequence:2});visit.accept({...base,sequence:3});need(visible===null,'select-auto-inside-ymcc-and-minimize-is-silent');
 visit.accept({...base,on:false,scene:'external',sequence:4});need(visible?.on===false,'first-real-change-after-ymcc-return-still-shows');visit.dispose();
 // Observe a real producer visit but deliberately drop its middle frame before
 // the HUD consumer: its next wire-admitted return still has to be silent.
 const {acceptMouseModeState,mouseModeState}=context.consumer;
 let skippedVisible=null;const skipped=createMouseStatusHint({emit:v=>skippedVisible=v,setTimer:()=>1,clearTimer:()=>{}});
 acceptMouseModeState({...base,sequence:101});skipped.accept(mouseModeState.value);
 acceptMouseModeState({...base,on:false,scene:'ymcc',sequence:102});const revision=mouseModeState.value.ymccVisitRevision;
 acceptMouseModeState({...base,on:false,scene:'ymcc',sequence:103});need(mouseModeState.value.ymccVisitRevision===revision,'duplicate-ymcc-state-does-not-renew-visit-marker');
 acceptMouseModeState({...base,on:false,scene:'external',sequence:104});
 skipped.accept({...mouseModeState.value,pending:true});need(skippedVisible===null,'skipped-ymcc-frame-return-pending-silent');
 const packet={type:'desktop-hud',protocol:1,hudId:1,nonce:'fixture',seq:1,expiresAt:6000,show:true,preferences:normalizeMonitorOutput({output:'ymcc'}),mouseMode:mouseModeState.value,data:{source:'ymcc-uniform',stale:false,values:{},coreUsage:[],coreClocks:[],frameTimes:[]}};
 const wire=context.consumer.admitDesktopHudFrame(JSON.parse(JSON.stringify(packet)),1,'fixture',0,0);
 need(wire?.mouseMode?.ymccVisitRevision===revision,'ymcc-visit-marker-survives-real-hud-wire-admission');
 skipped.accept(wire.mouseMode);need(skippedVisible===null,'skipped-ymcc-frame-return-settlement-silent');
 acceptMouseModeState({...base,sequence:105});skipped.accept(mouseModeState.value);need(skippedVisible?.on===true,'next-real-desktop-transition-after-skipped-ymcc-shows');
 acceptMouseModeState({...base,on:false,scene:'ymcc',sequence:103});need(mouseModeState.value.sequence===105,'stale-ymcc-event-cannot-create-visit');skipped.dispose();
 return {status:'PASS',cases:cases.length,matrixCases,modeMatrixCases,names:cases,consumer:'compiled-production-hint-controller-presentation-preferences-feedback;injected-clock'};
}
