import assert from 'node:assert/strict';
import crypto from 'node:crypto';
// Preparation only: compile actual App bodies in the original audited bundle.
// All modules outside the App scheduler remain explicit inert dependency ports.
export function appPowerResumeSource(app, sleepPlans = null) {
 const functions=['powerEventGeneration','notePowerTransitionGeneration','onPowerSuspending','onPowerResuming','onPowerResumeReady','onFanResumeReady','onPowerResumed','scheduleResumeTransaction'];
 const masked=app.replace(/\/\/[^\n]*|\/\*[\s\S]*?\*\/|'(?:\\.|[^'\\])*'|"(?:\\.|[^"\\])*"/g,value=>' '.repeat(value.length));
 const chunks=functions.map(name=>{const anchor='function '+name+'(';const start=app.indexOf(anchor);assert(start>=0&&app.indexOf(anchor,start+1)<0,'APP_RESUME_FUNCTION_UNIQUE '+name);const open=masked.indexOf('{',start);let depth=0,end=-1;for(let i=open;i<masked.length;i++){if(masked[i]==='{')depth++;else if(masked[i]==='}'&&--depth===0){end=i+1;break;}}assert(open>start&&end>open,'APP_RESUME_COMPLETE_FUNCTION '+name);const text=app.slice(start,end);return{name,text,sha256:crypto.createHash('sha256').update(text).digest('hex')};});
 const begin='  stopSharedPowerWatch = onSharedPowerLifecycle(state => {';
 const at=app.indexOf(begin);assert(at>=0&&app.indexOf(begin,at+1)<0,'APP_RESUME_SHARED_CALLBACK_UNIQUE');
 // Keep adjacent mount subscriptions outside the extracted callback.
 const callbackStart=at+begin.length,closing=/\r?\n  \}(?:, (\{ [^{}\r\n]+ \}))?\);(?=\r?\n)/.exec(app.slice(callbackStart));
 assert(closing,'APP_RESUME_SHARED_CALLBACK_END');
 const until=callbackStart+closing.index+closing[0].length;
 const body=app.slice(callbackStart,callbackStart+closing.index);
 const followupBegin=app.indexOf("// This consumer retains only the exact stages"),followupEnd=app.indexOf("let resumeLifecycleActive",followupBegin);assert(followupBegin>=0&&followupEnd>followupBegin,"APP_RESUME_FOLLOWUP_DECLARATION_UNIQUE");const followupDeclaration=app.slice(followupBegin,followupEnd);
 const observationDeclaration=app.match(/^  let sharedPowerKey = [^\r\n]+;/m)?.[0];assert(observationDeclaration&&app.split(observationDeclaration).length===2,'APP_RESUME_OBSERVATION_DECLARATION_UNIQUE');
 const scopeDeclaration=app.match(/^  let sharedSuspendScope: [^\r\n]+;/m)?.[0];assert(scopeDeclaration&&app.split(scopeDeclaration).length===2,'APP_RESUME_SUSPEND_SCOPE_DECLARATION_UNIQUE');
 const sleepBegin=app.search(/(?:let|const) sleepPlanOptimizationQueue/),sleepEnd=app.indexOf('let latestResumeGeneration',sleepBegin);
 const sleepDeclaration=app.slice(sleepBegin,sleepEnd);
 const readyBegin=app.indexOf('async function powerLifecycleReadyForWrites('),readyEnd=app.indexOf('\n}',readyBegin)+2;
 const readyBody=app.slice(readyBegin,readyEnd);
 const optimizerBegin=sleepPlans?.indexOf('export async function optimizeSleepPowerPlans(')??-1;
 const optimizerEnd=optimizerBegin>=0?sleepPlans.indexOf('\n/* ===',optimizerBegin):-1;
 const admissionBegin=sleepPlans?.indexOf('export interface StaticWakeAdmission {')??-1;
 const admissionEnd=admissionBegin>=0?sleepPlans.indexOf('\nasync function staticWakeAction(',admissionBegin):-1;
 if(sleepPlans)assert(sleepBegin>=0&&sleepEnd>sleepBegin&&readyBegin>=0&&readyEnd>readyBegin&&optimizerBegin>=0&&optimizerEnd>optimizerBegin,'APP_SLEEP_PLAN_COMPLETE_SOURCE');
 if(sleepPlans)assert(admissionBegin>=0&&admissionEnd>admissionBegin&&sleepPlans.indexOf('export interface StaticWakeAdmission {',admissionBegin+1)<0,'APP_SLEEP_PLAN_ADMISSION_SOURCE');
 const admission=sleepPlans?sleepPlans.slice(admissionBegin,admissionEnd).replace(/^export /gm,''):'';
 const optimizer=sleepPlans?sleepPlans.slice(optimizerBegin,optimizerEnd).replace('export async function','async function'):'';
 const unmountBegin=app.indexOf('  resumeLifecycleActive = false;'),unmountEnd=app.indexOf('  networkAccelerationWake.dispose();',unmountBegin);
 const stopBody=app.slice(unmountBegin,unmountEnd);
 const legacyReadyWatch=app.match(/stopResumeWatch = on<[^\n]+>\('power\.resume-ready', \(\{ generation \}\) => \{\r?\n[\s\S]*?\r?\n  \}\);/)?.[0]??'';
 const fanReadyWatch=app.match(/stopFanResumeWatch = on<[^\n]+>\('fan\.resume-ready', \(\{ generation \}\) => \{\r?\n[\s\S]*?\r?\n  \}\);/g);
 assert.equal(fanReadyWatch?.length,1,'APP_RESUME_FAN_READY_WATCH_UNIQUE');
 const source=`
const calls: any[] = [], retries: any[] = [];
let observedSharedPower: any = null;
const getSharedPowerLifecycle=()=>ports.sharedSnapshot?.()??observedSharedPower;
const fanWakeRequests=new Map();
const rememberFanWakeRequest=(generation:number)=>{if(!fanWakeRequests.has(generation))fanWakeRequests.set(generation,{generation,cancellationRevision:0});return fanWakeRequests.get(generation);};
const requestSharedPowerLifecycleReadyConfirmation=(listener:any)=>ports.confirmReady?.(listener)??(()=>{});
let latestResumeGeneration=0,committedResumeGeneration=0,resumeLifecycleActive=true,resumeWorkAllowed=false;
${followupDeclaration}
let resumeRetryTimer:any=null,resumeVideoTimer:any=null;
${observationDeclaration}
${scopeDeclaration}
const resumeRetryCounts=new Map<number,number>(),publicResumeQueue=createPowerResumeQueue();
const resumeQueue=ports.queueOwner?{invalidate(){publicResumeQueue.invalidate();},run(g:any,work:any,continueWork:any){return publicResumeQueue.run(g,(current:any)=>{ports.queueOwner(g,current);return work(current);},continueWork);}}:publicResumeQueue;
const onAcPower={value:true},documentVisible={value:true},nativeWindowVisible={value:true},backgroundVideoSuspended={value:false};
const CustomEvent=class {detail:any;constructor(_name:any,options:any){this.detail=options.detail;}};
const timers=new Map<number,any>();let nextTimer=0;
const window={setTimeout(fn:any,_ms:any){const id=++nextTimer;timers.set(id,fn);return id;},clearTimeout(id:any){timers.delete(id);}};
const clearTimeout=(id:any)=>window.clearTimeout(id);
const networkAccelerationWake={schedule(g:any){calls.push(['network',g]);},cancel(){}};
let dynamicBackgroundGeneration=0,sharedMediaPending=null,lastDynamicGameIdentity=null,dynamicBackgroundNoGameTimer=null;
const requestSharedBackgroundRefresh=()=>{};
const tdpRuntimeStore={invalidatePower(){}};
const fanHostLifecycle={coordinatorSnapshot:{powerGeneration:0,wakeReadyGeneration:0},controlCancellationRevision:0,hasControlIntent:true,setPowerGeneration(_g:any){},observePowerBoundary(_p:any,_g:any){},async resumeAfterNativeReady(g:number){return ports.fanReady?.(g)??false;}};
const ensureFanAutoStart=()=>{};
const fanDiagnosticLog=()=>{};
const setFanControlActive=(_on:any)=>{};
const suspendBackgroundVideo=()=>{},resumeBackgroundVideo=()=>{},reconcileBackgroundVideo=async()=>{},requestVideoPowerCheck=()=>{};
${sleepPlans?`${sleepDeclaration}
${readyBody}
${admission}
const getSleepPowerPlanOptimizationEnabled=()=>ports.sleepEnabled?.()??Promise.resolve(true);
const cachedSleepPowerPlanOptimizationEnabled=()=>ports.sleepEnabledCurrent?.()??true;
const getActiveScheme=()=>ports.activeScheme();
const readPowerPolicyIndex=(setting:any,ac:any)=>ports.policy(setting,ac);
const readPlanPowerIndex=(guid:any,sub:any,setting:any,ac:any)=>ports.plan(guid,sub,setting,ac);
const shell={run:(...args:any[])=>ports.shell(...args)};
const PW={WIN_BAL:'381b4222-f694-41f0-9685-ff5bb260df2e'};
${optimizer}`:'const sleepPlanOptimizationQueue={invalidate(){}}; let sleepPlanOptimizationRequest=null,sleepPlanOptimizationConfirmation=null; const continueSleepPowerPlanOptimization=()=>{}; const scheduleSleepPowerPlanOptimization=()=>{ports.sleepPlan?.();};'}
const requestFanCommittedResume=(_g:any)=>{ports.fanCommitted?.(_g);};
const detectPowerModeStable=()=>ports.mode?.()??Promise.resolve('ac');
const getFloatInfo=()=>({enabled:false});
const getPerformanceScheduleOwnership=()=>ports.ownership?.()??Promise.resolve('none');
const powerLifecycle={get(){return Promise.resolve(getSharedPowerLifecycle());},completeResume(g:any,_meta:any){calls.push(['commit',g]);return ports.commit?.(g)??Promise.resolve({ok:true});}};
const uiPowerReconciliation={reconcile(){}};
const resumeTdpDaemonAfterWake=async(_required:any)=>{calls.push(['daemon',latestResumeGeneration]);return true;};
const requestGamePolicyTdpReconciliation=(g:any)=>{calls.push(['policy',g]);};
const reconcileRememberedPowerScheme=()=>ports.scheme?.()??Promise.resolve('auto');
type WakePerformanceResult = 'settled' | 'retry-on-ready' | 'unconfirmed';
const restorePerformanceScheduleAfterWake=async(mode:any,dedicatedOnly:boolean,ownsWake:()=>boolean):Promise<WakePerformanceResult>=>{
  calls.push(['performance',latestResumeGeneration]);
  const result=await ports.performance?.(mode,{dedicatedOnly,automaticOnly:true,restoreRemembered:true,forceRemembered:true,ownsWake});
  return result==='retry-on-ready'||result==='unconfirmed'?result:'settled';
};
const LEGACY_CCD_UNDERVOLT_ACTIVE=false,applyCpuAutostart=async()=>{};
const scheduleResumeRetry=(generation:any,reason:any)=>{retries.push([generation,reason]);};
${chunks.map(c=>c.text).join('\n')}
let stopResumeWatch:any,stopFanResumeWatch:any;
const on=(name:any,listener:any)=>ports.listen?.(name,listener);
${legacyReadyWatch}
${fanReadyWatch[0]}
function observe(state:any){observedSharedPower=state;${body}}
return {calls,retries,observe,ready:onPowerResumeReady,sharedOptions:${closing[1]??"{}"},
stop(){stopResumeWatch?.();stopFanResumeWatch?.();${sleepPlans?stopBody:'resumeLifecycleActive=false;resumeQueue.invalidate();'}},
sleepSchedule:scheduleSleepPowerPlanOptimization,
sleepState(){return{pending:!!sleepPlanOptimizationRequest,confirming:typeof sleepPlanOptimizationConfirmation!=='undefined'&&!!sleepPlanOptimizationConfirmation};},
flushTimers(){const pending=[...timers.values()];timers.clear();for(const fn of pending)fn();},
state(){return{latest:latestResumeGeneration,committed:committedResumeGeneration,allowed:resumeWorkAllowed,followup:resumeFollowup?{generation:resumeFollowup.generation,completed:resumeFollowup.completed}:null};}};
`;
 return {source,bindings:{functions:chunks.map(({name,sha256})=>({name,sha256})),observationDeclaration:{sha256:crypto.createHash('sha256').update(observationDeclaration).digest('hex')},followupDeclaration:{sha256:crypto.createHash('sha256').update(followupDeclaration).digest('hex')},sharedCallback:{sha256:crypto.createHash('sha256').update(body).digest('hex'),begin:at,end:until},sleepPlan:sleepPlans?{declaration:sleepDeclaration,legacyReady:readyBody,optimizer,stopBody,scope:'Exact actual App queue and yeman optimizer; read/IPC dependency ports are inert; no Native or powercfg execution'}:null,scope:'Actual App source bodies; inert fan/network/media/settings/performance dependency ports; no mounted Vue or system operations'}};
}
