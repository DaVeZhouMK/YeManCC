// Pure preparation for the existing baseline; no test execution, compiler or filesystem access.
import crypto from 'node:crypto';
// Exact current C++ blocks; neighboring config fields never define a body boundary.
export function uniqueAnchor(source,needle,label){const at=source.indexOf(needle);if(at<0||source.indexOf(needle,at+needle.length)>=0)throw Error('Review unique native '+label+' anchor');return at;}
export function cppBlock(source,open,label){
 if(source[open]!=='{')throw Error('Native '+label+' body missing');
 let depth=0,state='code',escaped=false;
 for(let i=open;i<source.length;i++){
  const c=source[i],next=source[i+1];
  if(state==='line'){if(c==='\n')state='code';continue;}
  if(state==='block'){if(c==='*'&&next==='/'){state='code';i++;}continue;}
  if(state==='string'||state==='character'){
   if(escaped){escaped=false;continue;}if(c==='\\'){escaped=true;continue;}
   if(c===(state==='string'?'"':"'"))state='code';continue;
  }
  if(c==='/'&&next==='/'){state='line';i++;continue;}
  if(c==='/'&&next==='*'){state='block';i++;continue;}
  if(c==='R'&&next==='"')throw Error('Review raw literal in native '+label);
  if(c==='"'){state='string';continue;}if(c==="'"){state='character';continue;}
  if(c==='{')depth++;else if(c==='}'&&--depth===0)return {open,end:i+1,body:source.slice(open+1,i)};
 }
 throw Error('Unterminated native '+label+' body');
}

export function extractDeckyReady(main){
 const at=uniqueAnchor(main,'static bool deckySidebarResumeAfterIntent(','shared Decky Ready consumer');
 const body=cppBlock(main,main.indexOf('{',at),'shared Decky Ready consumer');
 const code=main.slice(at,body.end),publicReady=code.startsWith('static bool deckySidebarResumeAfterIntent(const ymcc::sleeppower::SharedPowerV1& power,');
 if(!publicReady)return {index:at,code,publicReady};
 const projectionAt=uniqueAnchor(main,'static void projectSharedPowerLifecycle(HWND window) {','public Ready projection');
 const projection=cppBlock(main,main.indexOf('{',projectionAt),'public Ready projection').body;
 uniqueAnchor(projection,'!sgSleepRetryActive()&&!dispatch(deckyReconnectBit,"decky-ready",true,[&]{','public Decky delivery');
 uniqueAnchor(projection,'if(!deckySidebarRecheckPublicReady(power))','public Decky call');
 const deliveryAt=uniqueAnchor(main,'static bool deckySidebarRecheckPublicReady(const ymcc::sleeppower::SharedPowerV1& power) {','Decky Ready delivery');
 const delivery=cppBlock(main,main.indexOf('{',deliveryAt),'Decky Ready delivery');
 const call='deckySidebarResumeAfterIntent(power,"public-ready")';
 const callIndex=delivery.open+1+uniqueAnchor(delivery.body,call,'first Decky Ready call');
 return {index:at,code,publicReady,call,callIndex,delivery:main.slice(deliveryAt,delivery.end),deliveryAt,projectionAt};
}

export function deckyReadyFixtureDelivery(ready){
 if(!ready.publicReady)throw Error('Public Decky Ready source required');
 return '#define YMCC_FIXTURE_PUBLIC_DECKY_READY 1\n'+ready.delivery+'\n'+
  'inline void productionCommittedReady(){(void)deckySidebarRecheckPublicReady(fixtureDeckyPower(currentPowerGeneration()));}\n';
}

export function prepareNativeSource({main,template,repairHeader,automaticHeader,mainSha256}){
 const outputs=new Map();
 // Each required slot must be filled exactly once before any producer starts.
 for(const name of ['SESSION_DIAGNOSTIC','PREPARE_ENVIRONMENT','WAKE_READY','ADMISSION','MODE_CHANGED','STABLE_BIGPICTURE','WAKE_COMMIT','WAKE_MESSAGE','WAKE_DELIVERY'])uniqueAnchor(template,'/*__'+name+'__*/','fixture template '+name);
 const a=uniqueAnchor(main,'[]() -> ymcc::deckysidebar::LaunchAdmission {','launch admission'),m=uniqueAnchor(main,'modeObserver.changedWithTrigger = []','mode observer');
 const changedSignature='modeObserver.changedWithTrigger = [](const ymcc::steamsession::Session& session,const json& state,const char* publicationTrigger) {';
 if(!main.startsWith(changedSignature,m))throw Error('Review native mode observer callback signature');
 const admissionBlock=cppBlock(main,main.indexOf('{',a),'launch admission'),changedBlock=cppBlock(main,main.indexOf('{',m),'mode observer');
 const stableAt=uniqueAnchor(main,'modeObserver.stableBigPicture = []','stable Big Picture'),stableBlock=cppBlock(main,main.indexOf('{',stableAt),'stable Big Picture');
 const stable=stableBlock.body;
 const admission=admissionBlock.body,changed=changedBlock.body;
 const extraction={method:'unique anchor and lexical balanced C++ body',mainSha256:mainSha256,admission:{begin:admissionBlock.open,end:admissionBlock.end,sha256:crypto.createHash('sha256').update(admission).digest('hex')},modeChanged:{begin:changedBlock.open,end:changedBlock.end,sha256:crypto.createHash('sha256').update(changed).digest('hex')},stableBigPicture:{begin:stableBlock.open,end:stableBlock.end,sha256:crypto.createHash('sha256').update(stable).digest('hex')}};
 const wakeBegin=main.indexOf('    case WM_POWER_RESUME_COMMIT:'),wakeEnd=main.indexOf('    case WM_POWER_INPUT_RECOVER:',wakeBegin);
 if(wakeBegin<0||wakeEnd<0)throw Error('Review native committed wake source boundary');
 const wakeBody=main.slice(wakeBegin+'    case WM_POWER_RESUME_COMMIT:'.length,wakeEnd),wakeMessage=main.split('\n').find(l=>l.startsWith('#define WM_POWER_INPUT_RECOVER '));
 if(!wakeMessage||!wakeBody.includes('return 0;'))throw Error('Missing native wake completion contract');
 const commitMessage=main.split('\n').find(l=>l.startsWith('#define WM_POWER_RESUME_COMMIT '));
 const deliveryGlobal=main.split('\n').find(l=>l.startsWith('static std::atomic<unsigned long long> g_powerResumeCommitRedelivery'));
 if(!commitMessage||!deliveryGlobal)throw Error('Missing committed power delivery source');
 const deliveryBodies=[...(main.includes('static void sleepPowerArmUnknownRedelivery(')?['sleepPowerArmUnknownRedelivery']:[]),...(main.includes('static void sleepPowerRetainCommittedDelivery(')?['sleepPowerRetainCommittedDelivery']:[]),'sleepPowerPostResumeCommit','sleepPowerRedeliverCommitted'].map(name=>{const start=uniqueAnchor(main,'static void '+name+'(','power delivery '+name),block=cppBlock(main,main.indexOf('{',start),'power delivery '+name);return main.slice(start,block.end);});
 const delivery=deliveryGlobal+'\n'+deliveryBodies.join('\n');
 extraction.committedDelivery={sha256:crypto.createHash('sha256').update(delivery).digest('hex'),includesSenderAndTimerConsumer:true};
 const ready=extractDeckyReady(main);
 const prewarmStart=uniqueAnchor(main,'static std::string deckySidebarPrepareEnvironment(','native environment preparation'),prewarmBlock=cppBlock(main,main.indexOf('{',prewarmStart),'native environment preparation'),prewarmEnd=prewarmBlock.end;
 const diagnosticStart=uniqueAnchor(main,'static void deckySidebarSessionDiagnostic(','session diagnostic'),diagnosticBlock=cppBlock(main,main.indexOf('{',diagnosticStart),'session diagnostic');
 const readySource=ready.code+(ready.publicReady?'\n#define YMCC_FIXTURE_PUBLIC_DECKY_READY 1\n'+ready.delivery:'');
 const fixed=template.replace('/*__SESSION_DIAGNOSTIC__*/',main.slice(diagnosticStart,diagnosticBlock.end)).replace('/*__PREPARE_ENVIRONMENT__*/',main.slice(prewarmStart,prewarmEnd)).replace('/*__WAKE_READY__*/',readySource).replace('/*__ADMISSION__*/',admission).replace('/*__MODE_CHANGED__*/',changed).replace('/*__STABLE_BIGPICTURE__*/',stable).replace('/*__WAKE_COMMIT__*/',wakeBody).replace('/*__WAKE_MESSAGE__*/',wakeMessage+'\n'+commitMessage).replace('/*__WAKE_DELIVERY__*/',delivery);outputs.set('fixed.cpp',fixed);
 if(ready.publicReady)extraction.deckyReady={leafSha256:crypto.createHash('sha256').update(ready.code).digest('hex'),deliverySha256:crypto.createHash('sha256').update(ready.delivery).digest('hex'),entry:'public-ready-consumer'};
 const legacy=fixed.replace('if (mode == "bigpicture") {','if (mode == "bigpicture") { if(!g_steamLinkAdmission.allows(session)&&!g_deckyUpdateRecovery.allows(session))return {false,"ymcc-startup-observation-only",false};');if(legacy===fixed)throw Error('Legacy origin-gate mutation anchor changed');outputs.set('legacy.cpp',legacy);
 const retainedNegative=fixed.replace('return {true,"retained-frontend-rebind",false,{},true};','return {false,"decky-context-already-loaded",false};');if(retainedNegative===fixed)throw Error('Retained frontend admission mutation missing');outputs.set('retained-negative.cpp',retainedNegative);
 const uncertainDispatch=/^([ \t]*)dispatchEntered=true;(\r?\n[ \t]*Json result;)/gm;
 if([...repairHeader.matchAll(uncertainDispatch)].length!==1)throw Error('Uncertain manual-dispatch mutation anchor must be unique');
 const uncertainHeader=repairHeader.replace(uncertainDispatch,'$1dispatchEntered=false;$2');
 outputs.set('uncertain-manual.h',uncertainHeader);outputs.set('uncertain-auto.h',automaticHeader.replace('#include "decky_sidebar_manual_repair.h"','#include "uncertain-manual.h"'));
 outputs.set('repair-uncertain-negative.cpp',fixed.replace('#include "decky_sidebar_manual_repair.h"','#include "uncertain-manual.h"').replace('#include "decky_sidebar_automatic_repair.h"','#include "uncertain-auto.h"'));
 const retainedHelper='inline bool retainedRepairable(';
 const usesRetainedHelper=repairHeader.includes(retainedHelper);
 const retainedOwner=uniqueAnchor(repairHeader,usesRetainedHelper?retainedHelper:'inline bool repairable(','retained repair owner');
 const retainedBlock=cppBlock(repairHeader,repairHeader.indexOf('{',retainedOwner),'retained repair owner');
 const retainedArm=usesRetainedHelper?'(phase=="frontend-attached"&&reason=="retained-frontend-connected")':
  '(runtime.at("phase")=="frontend-attached"&&runtime.at("reason")=="retained-frontend-connected")';
 const retainedOffset=retainedBlock.body.indexOf(retainedArm);
 if(retainedOffset<0||retainedBlock.body.indexOf(retainedArm,retainedOffset+retainedArm.length)>=0)
  throw Error('Retained backend repair mutation anchor missing or ambiguous');
 const retainedAt=retainedBlock.open+1+retainedOffset;
 const retainedRepairHeader=repairHeader.slice(0,retainedAt)+'false'+repairHeader.slice(retainedAt+retainedArm.length);
 outputs.set('retained-manual.h',retainedRepairHeader);outputs.set('retained-auto.h',automaticHeader.replace('#include "decky_sidebar_manual_repair.h"','#include "retained-manual.h"'));
 outputs.set('repair-retained-negative.cpp',fixed.replace('#include "decky_sidebar_manual_repair.h"','#include "retained-manual.h"').replace('#include "decky_sidebar_automatic_repair.h"','#include "retained-auto.h"'));
 const staleHeader=repairHeader.replace('origin!=Origin::Automatic&&deps.freshMode?deps.freshMode(session):deps.mode(session)','deps.mode(session)');if(staleHeader===repairHeader)throw Error('Manual stale-cache mutation anchor missing');
 outputs.set('stale-manual.h',staleHeader);outputs.set('stale-auto.h',automaticHeader.replace('#include "decky_sidebar_manual_repair.h"','#include "stale-manual.h"'));
 outputs.set('repair-stale-mode-negative.cpp',fixed.replace('#include "decky_sidebar_manual_repair.h"','#include "stale-manual.h"').replace('#include "decky_sidebar_automatic_repair.h"','#include "stale-auto.h"'));
 // Bypass only the phase predicate at entry, fanout and per-stage dispatch.
 // The other guards, and the separate public Decky delivery, stay intact.
 const wakeReady=/g_powerLifecycle\.load\(std::memory_order_acquire\)[ \t]*[!=]=[ \t]*PowerLifecycle::Ready\b/g;
 const phaseGuards=[
  [/if \(generation == 0 \|\| generation != currentPowerGeneration\(\) \|\|\s*(g_powerLifecycle\.load\(std::memory_order_acquire\)[ \t]*!=[ \t]*PowerLifecycle::Ready)\) return 0;/g,'false','entry'],
  [/if \((g_powerLifecycle\.load\(std::memory_order_acquire\)[ \t]*==[ \t]*PowerLifecycle::Ready)\) \{\s*fanout\.active=true;/g,'true','fanout'],
  [/if\(g_exitRequested\.load\(std::memory_order_acquire\)\|\|generation!=currentPowerGeneration\(\)\|\|\s*(g_powerLifecycle\.load\(std::memory_order_acquire\)[ \t]*!=[ \t]*PowerLifecycle::Ready)\)return false;/g,'false','dispatch']
 ];
 const edits=phaseGuards.map(([guard,replacement,label])=>{
  const matches=[...wakeBody.matchAll(guard)];
  if(matches.length!==1)throw Error('Review unique native wake power '+label+' mutation anchor');
  const [statement,predicate]=matches[0];
  return {at:matches[0].index+statement.indexOf(predicate),length:predicate.length,replacement};
 }).sort((a,b)=>b.at-a.at);
 const readyChecks=[...wakeBody.matchAll(wakeReady)];
 if(readyChecks.length!==edits.length||readyChecks.some(match=>!edits.some(edit=>edit.at===match.index)))
  throw Error('Review native wake power phase predicate set');
 let noPowerBody=wakeBody;
 for(const {at,length,replacement} of edits)noPowerBody=noPowerBody.slice(0,at)+replacement+noPowerBody.slice(at+length);
 const powerOwner=uniqueAnchor(fixed,wakeBody,'wake power guard owner');
 outputs.set('wake-power-guard-negative.cpp',fixed.slice(0,powerOwner)+noPowerBody+fixed.slice(powerOwner+wakeBody.length));
 // Public Ready owns the Decky requests; the commit message still owns its own fanout guards.
 const deckyWake=ready.publicReady?readySource:wakeBody;
 const wakeMutation=(needle,replacement,label)=>{
  const offset=uniqueAnchor(deckyWake,needle,label);
  const begin=uniqueAnchor(fixed,deckyWake,label+' owner')+offset;
  return fixed.slice(0,begin)+replacement+fixed.slice(begin+needle.length);
 };
 outputs.set('wake-binding-negative.cpp',wakeMutation('g_deckySidebarRuntime.requestBindingRefresh(generation)','false','wake binding mutation'));
 const noReadyEvent=fixed.replace('ipc_emit("power.resumed",','ipc_emit("power.pending",');if(noReadyEvent===fixed)throw Error('Native wake ready event mutation missing');outputs.set('wake-ready-event-negative.cpp',noReadyEvent);
 const noAutoGuard=repairHeader.replace('if(origin==Origin::Automatic)return refuse("steam-ui-refresh-requires-user");','');if(noAutoGuard===repairHeader)throw Error('Automatic UI guard anchor missing');
 outputs.set('unsafe-automatic-manual.h',noAutoGuard);outputs.set('unsafe-automatic.h',automaticHeader.replace('#include "decky_sidebar_manual_repair.h"','#include "unsafe-automatic-manual.h"'));
 outputs.set('repair-auto-ui-negative.cpp',fixed.replace('#include "decky_sidebar_manual_repair.h"','#include "unsafe-automatic-manual.h"').replace('#include "decky_sidebar_automatic_repair.h"','#include "unsafe-automatic.h"'));
 const repeatHeader=automaticHeader.replace('if(spent_||','if(false||');if(automaticHeader===repeatHeader)throw Error('Automatic budget mutation anchor missing');
 outputs.set('repeat-auto.h',repeatHeader);outputs.set('repair-auto-repeat-negative.cpp',fixed.replace('#include "decky_sidebar_automatic_repair.h"','#include "repeat-auto.h"'));
 const prewarmBlocked=fixed.replace('static std::string deckySidebarPrepareEnvironment(const std::function<bool()>& current) {','static std::string deckySidebarPrepareEnvironment(const std::function<bool()>& current) {\n    if(g_steamSettingsObserver.steamPresence()!=1)return "preparation-deferred";');if(prewarmBlocked===fixed)throw Error('Native prewarm negative anchor missing');outputs.set('prewarm-negative.cpp',prewarmBlocked);
 outputs.set('wake-preparation-negative.cpp',wakeMutation('g_deckySidebarRuntime.requestPreparationRefresh(generation)','false','wake preparation mutation'));
 return {outputs,extraction};
}

// The original committed delivery selector uses the same lexical extraction.
function includeS4Redelivery(main,timer,parts,take,template){
 if(!timer.includes('sgRedeliverS4Wake(h);'))return;
 const line=anchor=>{const at=uniqueAnchor(main,anchor,anchor);return main.slice(at,main.indexOf('\n',at));};
 const state=[...main.matchAll(/^static struct SgFailedS4Delivery \{[\s\S]*?^\} g_sgFailedS4Delivery;/gm)];
 if(state.length!==1)throw Error('Unique original S4 delivery state');
 parts.MESSAGES+='\n'+line('#define WM_SG_S4_WAKE ');
 const redelivery=take('s4Redeliver','static void sgRedeliverS4Wake(HWND window) {');
 for(const [name,anchor] of [
  ['WM_SG_S0_WAKE','#define WM_SG_S0_WAKE '],
  ['WM_FAN_KERNEL_POWER_FACT','static constexpr UINT WM_FAN_KERNEL_POWER_FACT '],
  ['g_sgLatestSleepEntryFileTime','static std::atomic<ULONGLONG> g_sgLatestSleepEntryFileTime{'],
  ['g_sgClosedSleepIntentFileTime','static std::atomic<ULONGLONG> g_sgClosedSleepIntentFileTime{']
 ])if(redelivery.includes(name)&&!template.includes(anchor))parts.MESSAGES+='\n'+line(anchor);
 const ingress=redelivery.includes('sgKernelEventSupersededByS4(')?[
  line('static SgFailedS4Delivery::Wake g_sgOriginalHibernateIngress;'),
  take('s4Supersession','static bool sgKernelEventSupersededByS4(unsigned long long generation,ULONGLONG originalTime) {')
 ]:[];
 parts.DELIVERY+='\n'+[line('static std::mutex g_sgFailedS4Mutex;'),state[0][0],...ingress,redelivery].join('\n');
}

export function prepareCommittedDeliverySource({main,template}){
 const extraction={};
 const bind=(name,begin,end)=>{const code=main.slice(begin,end);extraction[name]={begin,end,sha256:crypto.createHash('sha256').update(code).digest('hex')};return code;};
 const fragment=(name,anchor)=>{const at=uniqueAnchor(main,anchor,name),block=cppBlock(main,main.indexOf('{',at),name);return bind(name,at,block.end);};
 const commitAt=uniqueAnchor(main,'    case WM_POWER_RESUME_COMMIT: {','committed message'),commitBlock=cppBlock(main,main.indexOf('{',commitAt),'committed message');
 const commit=main.slice(commitBlock.open,commitBlock.end);extraction.commit={begin:commitBlock.open,end:commitBlock.end,sha256:crypto.createHash('sha256').update(commit).digest('hex')};
 const timerAt=uniqueAnchor(main,'if(w==kSleepPowerRedeliveryTimer){','original redelivery timer'),timerBlock=cppBlock(main,main.indexOf('{',timerAt),'original redelivery timer'),timer=main.slice(timerAt,timerBlock.end);extraction.timer={begin:timerAt,end:timerBlock.end,sha256:crypto.createHash('sha256').update(timer).digest('hex')};
 const lines=['#define WM_POWER_RESUME_COMMIT ','#define WM_POWER_INPUT_RECOVER ','static constexpr UINT kSleepPowerForwardMessage=','static constexpr UINT kSharedPowerStateMessage=','static constexpr UINT_PTR kSleepPowerRedeliveryTimer=','static std::atomic<unsigned long long> g_powerResumeCommitRedelivery'].map(anchor=>{const matches=main.split('\n').filter(line=>line.startsWith(anchor));if(matches.length!==1)throw Error('Unique committed delivery declaration '+anchor);return matches[0];});
 const enumeration=main.match(/^enum class PowerLifecycle\s*:\s*uint8_t\s*\{[^\n]+\};$/m);if(!enumeration)throw Error('Committed delivery power enum missing');
 const parts={POWER_ENUM:enumeration[0],MESSAGES:lines.slice(0,5).join('\n'),DELIVERY:lines[5]+'\n'+['sleepPowerArmRedelivery',...(main.includes('static void sleepPowerArmUnknownRedelivery(')?['sleepPowerArmUnknownRedelivery']:[]),...(main.includes('static void sleepPowerRetainCommittedDelivery(')?['sleepPowerRetainCommittedDelivery']:[]),'sleepPowerPostResumeCommit','sleepPowerRedeliverCommitted'].map(name=>fragment(name,name==='sleepPowerArmRedelivery'?'static void sleepPowerArmRedelivery(HWND window) {':'static void '+name+'(')).join('\n'),COMMIT:commit,TIMER:timer};
 includeS4Redelivery(main,timer,parts,fragment,template);
 if(template.includes('@@OWNER_EVENT_REPLAY@@'))parts.OWNER_EVENT_REPLAY=fragment('ownerEventReplay','static void sleepPowerReplayWindowDelivery(');
 const broadcast=uniqueAnchor(main,'    case kSleepPowerForwardMessage:','ordered power broadcast');
 const claimStart=main.indexOf('\n',broadcast)+1;
 if(!main.startsWith('    case WM_POWERBROADCAST: {',claimStart))throw Error('Missing adjacent window power broadcast');
 const claimEnd=main.indexOf('        // Independent observers',broadcast);
 const completion=uniqueAnchor(main,'        struct SharedPowerDeliveryCompletion {','ordered power completion');
 const completionEnd=uniqueAnchor(main,'        } sharedPowerDeliveryCompletion{h,powerReceipt};','ordered power completion instance');
 if(!(broadcast<claimEnd&&claimEnd<completion&&completion<completionEnd))throw Error('Ordered power delivery extraction mismatched');
 parts.POWER_BROADCAST=bind('powerBroadcastClaim',main.indexOf('\n',claimStart)+1,claimEnd)+'\n'+bind('powerBroadcastCompletion',completion,main.indexOf('\n',completionEnd));
 const suspendEnum=uniqueAnchor(main,'enum class SgSuspendAdmissionKind {','shared suspend admission enum'),suspendStruct=uniqueAnchor(main,'struct SgSuspendAdmission {','shared suspend admission');
 const suspendStructEnd=cppBlock(main,main.indexOf('{',suspendStruct),'shared suspend admission').end;
 parts.SUSPEND_ADMISSION=bind('suspendAdmissionTypes',suspendEnum,suspendStructEnd+1)+'\n'+fragment('suspendAdmission','static SgSuspendAdmission sgAdmitSuspendNotification(')+'\n'+fragment('suspendIgnore','static bool sgIgnoreLateSuspendNotification(');
 const admissionEnd=uniqueAnchor(main,'        if(suspendAdmission.privateIsolationRequired())','shared suspend private boundary');
 parts.SUSPEND_HANDOFF=bind('suspendAdmissionHandoff',main.indexOf('\n',completionEnd)+1,admissionEnd);
 let code=(commit.includes('deckySidebarResumeAfterIntent(')?'#define YMCC_FIXTURE_LEGACY_DECKY_COMMIT 1\n':'')+template;for(const [name,value]of Object.entries(parts)){const marker='@@'+name+'@@';uniqueAnchor(code,marker,'committed fixture '+name);code=code.replace(marker,()=>value);}if(/@@[A-Z_]+@@/.test(code))throw Error('Unknown committed fixture slot');return {code,extraction};
}

// Public Native power API and synchronous cancellation boundaries. This is a
// fragment consumer of the original owner, not a second power state machine.
export function prepareSharedPowerRevocationSource({main,template,remoteProvider=false}){
 const extraction={};
 const bind=(name,begin,end,code=main.slice(begin,end))=>{extraction[name]={begin,end,sha256:crypto.createHash('sha256').update(code).digest('hex')};return code;};
 const fragment=(name,anchor)=>{const at=uniqueAnchor(main,anchor,name),block=cppBlock(main,main.indexOf('{',at),name);return bind(name,at,block.end);};
 const enumeration=main.match(/^enum class PowerLifecycle\s*:\s*uint8_t\s*\{[^\n]+\};\r?$/m);if(!enumeration)throw Error('Shared power enum missing');
 const parts={POWER_ENUM:bind('powerEnum',enumeration.index,enumeration.index+enumeration[0].length)};
 const dispatchAnchor='static ymcc::sleeppower::Dispatch g_sleepPowerDispatch',dispatchAt=uniqueAnchor(main,dispatchAnchor,'shared power dispatcher');
 const after=main[dispatchAt+dispatchAnchor.length];
 if(after===';'){
  bind('dispatcher',dispatchAt,dispatchAt+dispatchAnchor.length+1);
  parts.DISPATCH_FACTORY='static std::unique_ptr<Dispatch> configuredDispatch(){return std::unique_ptr<Dispatch>(new Dispatch{});}';
 }else{
  if(after!=='{')throw Error('Unsupported shared power dispatcher initializer');
  const block=cppBlock(main,dispatchAt+dispatchAnchor.length,'shared power dispatcher initializer');
  const initializer=bind('dispatcher',block.open,block.end);
  parts.DISPATCH_FACTORY='static std::unique_ptr<Dispatch> configuredDispatch(){return std::unique_ptr<Dispatch>(new Dispatch'+initializer+');}';
 }
 if(main.includes('static void revokeSharedPowerMirrors() noexcept'))parts.DISPATCH_FACTORY='static ymcc::sleeppower::RecoveryBoundaryMirror g_recoveryPowerBoundary;\n'+fragment('recoveryRevoke','static void revokeSharedPowerMirrors() noexcept')+'\n'+fragment('recoveryClose','static void closeSharedPowerMirrors() noexcept')+'\n'+parts.DISPATCH_FACTORY;
 parts.READER=[fragment('hardwareWriteAllowed','static bool hardwareWriteAllowed() {'),fragment('currentPowerGeneration','static unsigned long long currentPowerGeneration() {'),fragment('readSharedSystemPower','static int __cdecl readSharedSystemPower(ymcc::sleeppower::SharedPowerV1* output,')].join('\n');
 const accessorAnchor='[[maybe_unused]] static bool captureSharedPowerLifecycle(';
 if(main.includes(accessorAnchor)){
  parts.CURRENT_ACCESSORS=[fragment('captureAccessor',accessorAnchor),fragment('currentAccessor','[[maybe_unused]] static bool sharedPowerLifecycleCurrent('),fragment('watchAccessor','[[maybe_unused]] static ymcc::sleeppower::RevocationMirrors::Registration observeSharedPowerRevocation(')].join('\n');
 }else{
  // Before-source control uses the proposed API, explicitly marked synthetic;
  // its real old dispatcher/gate/phase/exit entry points remain unchanged.
  extraction.proposedAccessorsOnlyForOldEntryControl=true;
  parts.CURRENT_ACCESSORS='static bool captureSharedPowerLifecycle(SharedPowerV1& value) noexcept{return ymcc::sleeppower::capturePower(&readSharedSystemPower,value);}\nstatic bool sharedPowerLifecycleCurrent(const SharedPowerV1& value) noexcept{return ymcc::sleeppower::powerCurrent(&readSharedSystemPower,value);}\nstatic RevocationMirrors::Registration observeSharedPowerRevocation(const SharedPowerV1& value) noexcept{return g_sharedPowerRevocations.observe(&readSharedSystemPower,value);}';
 }
 parts.CLOSE_GATE=fragment('closeHardwareWriteGate','static void closeHardwareWriteGate(const char* reason) {');
 const phase=/^([ \t]*)g_powerLifecycle\.store\(PowerLifecycle::(?:Resuming|Suspending|Suspended), std::memory_order_release\);\r?$/gm,phaseRows=[...main.matchAll(phase)];
 if(phaseRows.length!==11)throw Error('Review original11 production NonReady stores');
 const functions=phaseRows.map((row,index)=>{let end=row.index+row[0].length;const tail=main.slice(end).match(/^\r?\n[ \t]*(?:g_sharedPowerRevocations\.revoke|revokeSharedPowerMirrors)\(\);/);if(tail)end+=tail[0].length;const code=bind('nonReady'+index,row.index,end);return 'static void phaseHook'+index+'(){\n'+code+'\n}';});
 parts.PHASE_HOOKS=functions.join('\n')+'\nstatic const std::vector<void(*)()> phaseHookFunctions{'+phaseRows.map((_,index)=>'&phaseHook'+index).join(',')+'};';
 const normalAt=uniqueAnchor(main,'if (!g_exitRequested.compare_exchange_strong(expected, true)) return;','normal exit CAS');
 let normalEnd=normalAt+'if (!g_exitRequested.compare_exchange_strong(expected, true)) return;'.length;const normalTail=main.slice(normalEnd).match(/^\r?\n[ \t]*(?:g_sharedPowerRevocations\.close|closeSharedPowerMirrors)\(\);/);if(normalTail)normalEnd+=normalTail[0].length;
 const fallbackAt=uniqueAnchor(main,'if(g_exitRequested.compare_exchange_strong(expected,true)){','fallback exit CAS');
 let fallbackEnd=fallbackAt+'if(g_exitRequested.compare_exchange_strong(expected,true)){'.length;const fallbackTail=main.slice(fallbackEnd).match(/^\r?\n[ \t]*(?:g_sharedPowerRevocations\.close|closeSharedPowerMirrors)\(\);/);if(fallbackTail)fallbackEnd+=fallbackTail[0].length;
 parts.EXIT_HOOKS='static void actualNormalExitFence(){bool expected=false;'+bind('normalExitFence',normalAt,normalEnd)+'\n}\nstatic void actualFallbackExitFence(){bool expected=false;'+bind('fallbackExitFence',fallbackAt,fallbackEnd)+'\n}}';
 parts.REMOTE_PROVIDER_INCLUDE=remoteProvider?'#include "sleep_power_remote_provider.h"\n'+fragment('nativeRemoteProvider','[[maybe_unused]] static ymcc::sleeppower::remote::NativeProvider& sharedNativeRemotePowerProvider() noexcept')+'\n#include "shared-power-remote-provider.h"':'';
 parts.REMOTE_PROVIDER_CASES=remoteProvider?' run("remote-provider-scope-lifecycle",[]{remoteProviderLifecycle();});\n run("remote-provider-request-settlement-control",[]{remoteProviderRequestControl();});\n run("remote-provider-native-accessor",[]{remoteProviderNativeAccessor();});':'';
 let code=template;for(const[name,value]of Object.entries(parts)){const marker='@@'+name+'@@';uniqueAnchor(code,marker,'power revocation fixture '+name);code=code.replace(marker,()=>value);}if(/@@[A-Z_]+@@/.test(code))throw Error('Unknown shared power revocation slot');
 const caseRows=[...code.matchAll(/\brun\("([^"\r\n]+)",/g)].map(row=>({name:row[1],at:row.index}));
 const loopAt=uniqueAnchor(code,'for(unsigned index=0;index<phaseHookFunctions.size();++index)run("nonready-site-"','original phase cases');
 caseRows.push(...phaseRows.map((_,index)=>({name:'nonready-site-'+index,at:loopAt+index/phaseRows.length})));caseRows.sort((a,b)=>a.at-b.at);
 const caseNames=caseRows.map(row=>row.name);if(new Set(caseNames).size!==caseNames.length)throw Error('Duplicate shared power revocation case');
 return{code,extraction,caseNames};
}

// Current-fact broadcast uses the same original ID, with real Native message,
// publisher, Reader and retry timer. Provider/device/UI transport leaves are inert.
export function prepareSharedPowerProjectionSource({main,template,msiHeader,recoveryRuntime,recoveryAuto,classificationSource}){
 const extraction={};
 const readyBegin=uniqueAnchor(main,'static bool powerLifecycleMatches(PowerLifecycle phase, unsigned long long generation) {', 'Ready receipt helpers');
 const readyEnd=uniqueAnchor(main,'static bool commitPowerResumeGate(', 'Ready commit boundary');
 const readyReceipt=main.slice(readyBegin,readyEnd);
 if(readyEnd<=readyBegin)throw Error('Ready receipt helper bounds');
 extraction.readyReceipt={begin:readyBegin,end:readyEnd,sha256:crypto.createHash('sha256').update(readyReceipt).digest('hex'),source:'native/main.cpp'};
 const fragment=(source,name,anchor)=>{const at=uniqueAnchor(source,anchor,name),block=cppBlock(source,source.indexOf('{',at),name),code=source.slice(at,block.end);extraction[name]={begin:at,end:block.end,sha256:crypto.createHash('sha256').update(code).digest('hex'),source:source===main?'native/main.cpp':'native/msi_native_foreground_isolation_runtime.h'};return code;};
 const fn=(name,anchor)=>fragment(main,name,anchor),line=anchor=>{const rows=main.split('\n').filter(row=>row.startsWith(anchor));if(rows.length!==1)throw Error('Unique Native projection declaration '+anchor);return rows[0];};
 const focusIsolation=[fn('focusIsolationTicket','struct FocusLightIsolationTicket {')+';',fn('focusIsolationEnd','static void focusEndIsolationOnUi() {'),fn('focusIsolationSharedPowerChanged','static bool focusIsolationSharedPowerChanged(').replace('static bool focusIsolationSharedPowerChanged(','static bool productionFocusIsolationSharedPowerChanged(')];
 const focusReturnTimer=fn('focusReturnTimer','if (w == RETURN_GAME_FOCUS_TIMER_ID) {');
 const enumeration=main.match(/^enum class PowerLifecycle\s*:\s*uint8_t\s*\{[^\n]+\};\r?$/m);if(!enumeration)throw Error('Shared projection power enum missing');
 const at=uniqueAnchor(main,'    case kSharedPowerStateMessage: {','current projection message'),block=cppBlock(main,main.indexOf('{',at),'current projection message'),windowCase=main.slice(block.open,block.end);extraction.windowCase={begin:block.open,end:block.end,sha256:crypto.createHash('sha256').update(windowCase).digest('hex')};
 const projection=main.includes('static void projectSharedPowerLifecycle(HWND window) {')?fn('projection','static void projectSharedPowerLifecycle(HWND window) {'):'static LRESULT actualLegacySharedProjection([[maybe_unused]] HWND h) '+windowCase+'\nstatic void projectSharedPowerLifecycle(HWND window){(void)actualLegacySharedProjection(window);}';
 const tagged=main.includes('static void sleepPowerProjectionPostConsumed(WPARAM receipt) {');
 const delegate=tagged?windowCase.replace('sleepPowerProjectionPostConsumed(w);',''):windowCase;
 if(tagged&&!windowCase.includes('sleepPowerProjectionPostConsumed(w);'))throw Error('Projection window post receipt missing');
 if(main.includes('static void projectSharedPowerLifecycle(HWND window) {')&&!/^\{\s*projectSharedPowerLifecycle\(h\);\s*(?:inputCaptureSensorThreadRecoverReturned\(h\);\s*)?return 0;\s*\}$/.test(delegate))throw Error('Projection window delegate changed');
 const timerAt=uniqueAnchor(main,'if(w==kSleepPowerRedeliveryTimer){','current projection timer'),timerBlock=cppBlock(main,main.indexOf('{',timerAt),'current projection timer'),timer=main.slice(timerAt,timerBlock.end);extraction.timer={begin:timerAt,end:timerBlock.end,sha256:crypto.createHash('sha256').update(timer).digest('hex')};
 const classificationExtract=(name,anchor)=>{const at=uniqueAnchor(classificationSource,anchor,name),block=cppBlock(classificationSource,classificationSource.indexOf('{',at),name),code=classificationSource.slice(at,block.end);extraction[name]={begin:at,end:block.end,sha256:crypto.createHash('sha256').update(code).digest('hex'),source:'native/sleep_resume_classification_native.inc'};return code;};
 const classificationState=classificationExtract('classificationState','struct SgResumeClassificationState {')+';\nstatic SgResumeClassificationState g_sgResumeClassification;\nstatic std::atomic<unsigned long long> g_sgWakeClassificationBlockedGeneration{0};\nstatic bool sgResumeClassificationOwned(unsigned long long);\nstatic void sgCompleteSharedWakeClassification(unsigned long long);\n';
 const finishDeclaration=line('static void sgFinishResumeClassification(');
 if(!/^static void sgFinishResumeClassification\([^{}\r\n]*\);\r?$/.test(finishDeclaration))throw Error('Review original Finish forward declaration');
 const finishDeclarationAt=uniqueAnchor(main,finishDeclaration,'Finish forward declaration');
 extraction.classificationFinishDeclaration={begin:finishDeclarationAt,end:finishDeclarationAt+finishDeclaration.length,sha256:crypto.createHash('sha256').update(finishDeclaration).digest('hex'),source:'native/main.cpp'};
 const classification=classificationState+finishDeclaration+'\n'+['sharedSleepWakeClassified','sgBlockSharedWakeClassification','sgResumeClassificationOwned','sgCompleteSharedWakeClassification','sgStopResumeClassificationWait','sgFinishResumeClassification'].map(name=>classificationExtract(name,'static '+(name==='sharedSleepWakeClassified'||name==='sgResumeClassificationOwned'?'bool ':'void ')+name+'(')).join('\n');
 const recoveryExtract=(source,name,anchor)=>{const at=uniqueAnchor(source,anchor,name),block=cppBlock(source,source.indexOf('{',at),name),code=source.slice(at,block.end);extraction[name]={begin:at,end:block.end,sha256:crypto.createHash('sha256').update(code).digest('hex'),source:source===recoveryRuntime?'native/steam_original_recovery_runtime.h':'native/sleep_recovery_auto.h'};return code;};
 const recoveryConsumerCode='namespace SteamOriginalRecovery {\n'+recoveryExtract(recoveryRuntime,'recoveryControl','struct Control {')+';\n'+recoveryExtract(recoveryRuntime,'recoveryEnqueue','static void enqueue(')+'\n}\n'+recoveryExtract(recoveryRuntime,'recoveryReady','static void steamOriginalRecoveryPowerReady(')+'\n'+'[[maybe_unused]] '+recoveryExtract(recoveryAuto,'recoveryNotify','static void sleepRecoveryAutoPowerReady(');
 const selectorRows=[...recoveryRuntime.matchAll(/^\s*if\(c->startupPending&&pending\.kind!=Trigger::SuspendCapture&&pending\.kind!=Trigger::Wake\)pending\.kind=Trigger::Startup;\r?$/gm)];
 let recoverySelector;
 if(selectorRows.length===1){recoverySelector=selectorRows[0][0].trim();extraction.recoveryStartupSelector={source:'native/steam_original_recovery_runtime.h',kind:'original-startup-pending-line',begin:selectorRows[0].index,end:selectorRows[0].index+selectorRows[0][0].length,sha256:crypto.createHash('sha256').update(recoverySelector).digest('hex')};}
 else{
  if(selectorRows.length)throw Error('Unique original Runtime startup selector');
  const anchor=uniqueAnchor(recoveryRuntime,'// Startup belongs to the exact Steam/game pair in each original ledger.','current pair Startup contract');
  const selectorTail=recoveryRuntime.slice(anchor).match(/if\(pending\.kind!=Trigger::Wake&&pending\.kind!=Trigger::SuspendCapture(?:&&pending\.kind!=Trigger::RunningLost)?\)\{/);
  if(!selectorTail)throw Error('Actual pair Startup admission selector missing');
  const begin=anchor+selectorTail.index,block=cppBlock(recoveryRuntime,recoveryRuntime.indexOf('{',begin),'current pair Startup selector');
  if(begin<anchor||!recoveryRuntime.slice(begin,block.end).includes('routeAdmission.preview(scope,startup,enabled).accepted'))throw Error('Actual pair Startup admission selector missing');
  recoverySelector=recoveryRuntime.slice(begin,block.end);extraction.recoveryStartupSelector={source:'native/steam_original_recovery_runtime.h',kind:'actual-pair-ledger-selector-block',begin,end:block.end,sha256:crypto.createHash('sha256').update(recoverySelector).digest('hex')};
 }
 const admissionAnchor='{std::lock_guard<std::mutex> lock(c->mx);decision=routeAdmission.admit(scope,{pending.kind,pending.generation,pending.sequence},enabled);';
 const admissionBegin=uniqueAnchor(recoveryRuntime,admissionAnchor,'current route final admission'),admissionBlock=cppBlock(recoveryRuntime,admissionBegin,'current route final admission');
 const recoveryFinalAdmission=recoveryRuntime.slice(admissionBegin,admissionBlock.end);extraction.recoveryFinalAdmission={source:'native/steam_original_recovery_runtime.h',begin:admissionBegin,end:admissionBlock.end,sha256:crypto.createHash('sha256').update(recoveryFinalAdmission).digest('hex')};
 const fanReceipt=main.includes('enum class FanReadyDelivery {');
 const fanReturn=fanReceipt?'FanReadyDelivery':'void';
 const physicalPublish=`static ${fanReturn} fanHostPublishPhysicalWake(unsigned long long generation`;
 const physicalArgs=main.includes(physicalPublish+', bool refreshAdmission) noexcept {')?', bool refreshAdmission':'';
 const fanResume=[...(fanReceipt?[fn('fanReadyDelivery','enum class FanReadyDelivery {')+';']:[]),fn('fanScheduleResult','struct FanResumeScheduleResult {')+';',fn('currentFanPowerGeneration','static unsigned long long currentFanPowerGeneration() {'),fn('fanHostPhysicalWakeCurrent','static bool fanHostPhysicalWakeCurrent() {'),fn('fanHostPowerMayWrite','static bool fanHostPowerMayWrite() {'),fn('fanHostPowerMayResume','static bool fanHostPowerMayResume() {'),fn('fanHttpPowerAdmission','struct FanHttpPowerAdmission {')+';', 'static FanResumeScheduleResult fanHostScheduleFanResume(unsigned long long,bool=false,unsigned long long=0);',fn('fanPowerStamp','static ymcc::fan_resume::PowerStamp fanResumeRequestPowerStamp('),fn('fanScheduleResume','static FanResumeScheduleResult fanHostScheduleFanResume(unsigned long long generation, bool explicitManual, unsigned long long continuingManual) {'),line(physicalPublish+(physicalArgs?physicalArgs+' = false':'')+') noexcept;'),fn('fanPhysicalPublish',physicalPublish+physicalArgs+') noexcept {'),'[[maybe_unused]] '+fn('fanSharedResume',`static ${fanReturn} fanHostScheduleEmergencyResume(unsigned long long sharedGeneration) {`)].join('\n');
 const cssConfig=main.split('\n').filter(row=>row.trim()==='cssGuard.powerReader = &readSharedSystemPower;');if(cssConfig.length>1)throw Error('Duplicate CSS public Reader binding');
 const parts={CSS_GUARD_CONFIG:cssConfig.join('\n'),CSS_MANUAL_GUARD_CONFIG:cssConfig.join('\n'),RECOVERY_PENDING:recoveryExtract(recoveryRuntime,'recoveryPending','struct Pending {')+';',FAN_RESUME:fanResume,POWER_ENUM:enumeration[0],PHASE_NAME:fn('powerLifecycleName','static const char* powerLifecycleName('),MESSAGES:['#define WM_POWER_RESUME_COMMIT ','#define WM_POWER_INPUT_RECOVER ','static constexpr UINT kSleepPowerForwardMessage=','static constexpr UINT kSharedPowerStateMessage=','static constexpr UINT_PTR kSleepPowerRedeliveryTimer='].map(line).join('\n'),READER:[fn('hardwareWriteAllowed','static bool hardwareWriteAllowed() {'),fn('currentPowerGeneration','static unsigned long long currentPowerGeneration() {'),fn('readSharedSystemPower','static int __cdecl readSharedSystemPower(ymcc::sleeppower::SharedPowerV1* output,')].join('\n'),ACCESSORS:[fn('captureAccessor','[[maybe_unused]] static bool captureSharedPowerLifecycle('),fn('currentAccessor','[[maybe_unused]] static bool sharedPowerLifecycleCurrent(')].join('\n'),SNAPSHOT:fn('snapshot','static json sharedPowerLifecycleSnapshot() {').replace('static json sharedPowerLifecycleSnapshot()','static json productionSharedPowerLifecycleSnapshot()'),MSI_SHARED_READY:fragment(msiHeader,'msiSharedReady','static bool msiNativeForegroundSharedPowerReady(').replace('static bool msiNativeForegroundSharedPowerReady(','static bool productionMsiNativeForegroundSharedPowerReady('),DELIVERY:[line('static std::atomic<unsigned long long> g_powerResumeCommitRedelivery'),fn('arm','static void sleepPowerArmRedelivery(HWND window) {'),...(main.includes('static void sleepPowerArmUnknownRedelivery(')?[fn('armUnknownDelivery','static void sleepPowerArmUnknownRedelivery(')]:[]),...(main.includes('static void sleepPowerRetainCommittedDelivery(')?[fn('retainDelivery','static void sleepPowerRetainCommittedDelivery(')]:[]),fn('postCommit','static void sleepPowerPostResumeCommit('),fn('redeliver','static void sleepPowerRedeliverCommitted(')].join('\n'),CSS_RESUME:fn('cssLegacyReady','static bool steamAppearanceResumeCommitted(')+'\n'+(main.includes('static void steamAppearancePublicReady(')?fn('cssPublicReady','static void steamAppearancePublicReady(unsigned long long generation) noexcept {'):''),READY_RECEIPT:readyReceipt,READY_COMMIT:fn('gateCommit','static bool commitPowerResumeGate(const PowerResumeReadyReceipt& receipt, uint64_t notification,')+'\n'+fn('readyCommit','static bool commitPowerResume(unsigned long long generation, const char* source, uint64_t ticket) {'),RECOVERY_CONSUMER:recoveryConsumerCode,RECOVERY_STARTUP_SELECTOR:recoverySelector,RECOVERY_FINAL_ADMISSION:recoveryFinalAdmission,CLASSIFICATION:classification,FAILURE_LOG:main.includes('static void sharedPowerProjectionConsumerFailed(')?fn('failureLog','static void sharedPowerProjectionConsumerFailed('):'',PUBLISHER:fn('publisher','static void publishSharedPowerLifecycle(const char* reason) {'),PROJECTION:projection,TIMER:timer};
 if(main.includes('static bool ipc_emit_shared_power_fact('))
  parts.PROJECTION=fn('sharedPowerIpc','static bool ipc_emit_shared_power_fact(')+'\n'+parts.PROJECTION;
 includeS4Redelivery(main,timer,parts,fn,template);
 if(template.includes('@@RETRY_STOP@@'))parts.RETRY_STOP=fn('retryStop','static void sgStopSleepRetry(bool preserveEntryObservation) {');
 if(parts.SNAPSHOT.includes('g_gamepadUiWakeGeneration')){
  const anchor='static std::atomic<uint64_t> g_gamepadUiWakeGeneration';
  const start=uniqueAnchor(main,anchor,'gamepad wake projection storage'),end=main.indexOf(';',start)+1;
  if(end<=start)throw Error('Missing gamepad wake projection storage terminator');
  const code=main.slice(start,end);
  extraction.gamepadWakeProjectionStorage={begin:start,end,sha256:crypto.createHash('sha256').update(code).digest('hex'),source:'native/main.cpp'};
  parts.SNAPSHOT=code+'\n'+parts.SNAPSHOT;
 }
 if(template.includes('@@FAN_WAKE_DELIVERY@@')){
  parts.FAN_WAKE_DELIVERY='';
  if(fanResume.includes('publishSharedFanWake(generation);')){
   const begin=uniqueAnchor(main,'static constexpr UINT_PTR kSharedFanWakeDeliveryTimer=','Fan wake timer');
   fn('fanWakeDelivery','static void publishSharedFanWake(unsigned long long generation) noexcept {');
   const end=extraction.fanWakeDelivery.end;
   if(begin>=extraction.fanWakeDelivery.begin)throw Error('Fan wake delivery declarations out of order');
   const code=main.slice(begin,end);
   extraction.fanWakeDelivery={begin,end,sha256:crypto.createHash('sha256').update(code).digest('hex'),source:'native/main.cpp'};
   parts.FAN_RESUME='static void publishSharedFanWake(unsigned long long generation) noexcept;\n'+parts.FAN_RESUME;
   parts.FAN_WAKE_DELIVERY=code;
  }
 }
 const startupWake=projection.includes('startupClaimCurrent(')||parts.SNAPSHOT.includes('fanStartupWakeGeneration');
 if(startupWake){
  uniqueAnchor(projection,'fanStartupWakeCurrent(power,fanGeneration,startupEntry)','Fan startup projection');
  uniqueAnchor(parts.SNAPSHOT,'fanStartupWakeCurrent(state,fan,latestEntry)','Fan startup snapshot');
  uniqueAnchor(projection,'fanHostQueueSuspend("startup-fan-wake-close",fanGeneration);','Fan startup Close port');
  uniqueAnchor(template,'static void fanHostQueueSuspend(const char* reason,unsigned long long generation) {','private Fan startup Close port');
  const helpers=[fn('fanStartupWakeCurrent','static bool fanStartupWakeCurrent('),fn('fanPublishedWakeGeneration','static unsigned long long fanPublishedWakeGeneration('),fn('fanClaimStartupWake','static json fanClaimStartupWake(')];
  if(main.includes('static ymcc::fan_wake::Observation::OffRequest fanStartupOffRequest('))helpers.unshift(
   fn('fanStartupOffRequest','static ymcc::fan_wake::Observation::OffRequest fanStartupOffRequest('));
  parts.SNAPSHOT=['#define YMCC_FIXTURE_FAN_STARTUP_WAKE 1',...helpers,parts.SNAPSHOT].join('\n');
 }
 if(template.includes('@@FAN_KERNEL_LEAF@@'))parts.FAN_KERNEL_LEAF=startupWake?[
  fn('fanHostRequestPhysicalWakeDelivery','static void fanHostRequestPhysicalWakeDelivery(unsigned long long generation) {'),
  fn('fanHostObserveKernelEntry','static void fanHostObserveKernelEntry(unsigned long long generation, ULONGLONG eventFileTime) {'),
   fn('fanHostObserveKernelExit','static void fanHostObserveKernelExit('),
  ...(main.includes('static json fanStartupOffStatus(')?[
   fn('fanStartupOffReply','static json fanStartupOffReply('),
   fn('fanStartupOffFence','static ULONGLONG fanStartupOffFence('),
   fn('fanStartupOffStatus','static json fanStartupOffStatus(')]:[]),
  fn('fanCancelStartupWake','static json fanCancelStartupWake(')
 ].join('\n'):'';
 if(template.includes('@@PROJECTION_WINDOW@@')){
  if(tagged){
   const hasGameWake=parts.PUBLISHER.includes('queueSharedPowerPublication(');
   const queue=parts.PUBLISHER.includes('sleepPowerQueueProjection(')||hasGameWake?fn('queueProjectionPost','static bool sleepPowerQueueProjection(HWND window) {')+'\n':'';
   if(hasGameWake){
    parts.SNAPSHOT=line('static std::atomic<uint64_t> g_gameWakeRecoveryRevision{')+'\n'+parts.SNAPSHOT;
    parts.PUBLISHER=fn('queueSharedPowerPublication','static void queueSharedPowerPublication(HWND window) {')+'\n'+parts.PUBLISHER+'\n'+
      line('static std::atomic<HWND> g_wakeGameRecoveryStatusWindow{')+'\n'+
      fn('postWakeGameRecoveryStatus','static void postWakeGameRecoveryStatus() noexcept {');
   }
   parts.PUBLISHER=['static std::mutex g_sharedPowerProjectionPostMutex;','static bool g_sharedPowerProjectionPosted=','static constexpr WPARAM kSharedPowerProjectionPostReceipt='].map(line).join('\n')+'\n'+queue+parts.PUBLISHER+'\n'+fn('consumeProjectionPost','static void sleepPowerProjectionPostConsumed(WPARAM receipt) {');
  }
  parts.PROJECTION_WINDOW='static LRESULT productionProjectionWindow(HWND h,[[maybe_unused]] WPARAM w) '+(main.includes('static void projectSharedPowerLifecycle(HWND window) {')?windowCase:'{projectSharedPowerLifecycle(h);return 0;}');
 }else if(tagged)throw Error('Projection fixture must bind the actual posted window receipt');
 parts.FOCUS_TICKET=focusIsolation[0];
 if(template.includes('@@RADIAL_PUBLIC_POWER@@'))parts.RADIAL_PUBLIC_POWER=main.includes('static bool radialOverlaySharedPowerChanged(')?
  [fn('radialPowerPeriod','static bool radialOverlaySamePowerPeriod('),fn('radialPublicPower','static bool radialOverlaySharedPowerChanged('),
   fn('radialPowerCreate','static json radialCreateForPower(')].join('\n'):'';
 parts.FOCUS_ISOLATION=focusIsolation.slice(1).join('\n');
 parts.FOCUS_RETURN_TIMER=fn('focusSessionPowerAllowsAction','static bool focusSessionPowerAllowsAction(')+'\nstatic LRESULT productionFocusReturnTimer(WPARAM w){\n'+focusReturnTimer+'\nreturn 0;\n}';
 if(template.includes('@@POWER_SOURCE_RECOVERY@@')){
  const anchor='static bool beginPowerSourceRecoveryForReady(HWND h,const ymcc::sleeppower::SharedPowerV1& power,bool sourceEdge) {';
  parts.POWER_SOURCE_RECOVERY=[line('#define TIMER_ID_POWER_SOURCE_RECOVERY '),line('static ymcc::powerSource::Heartbeat g_powerSourceHeartbeat;'),
   line('static bool g_powerSourceRecoveryRunning='),line('static std::uint64_t g_powerSourceRecoverySerial='),line('static int g_lastPowerEventAc='),
   ...(main.includes('static void stopPowerSourceRecovery(HWND h) {')?[fn('powerSourceStop','static void stopPowerSourceRecovery(HWND h) {')]:[]),
   ...(main.includes(anchor)?[fn('powerSourcePublicReady',anchor).replace('bool sourceEdge) {','bool sourceEdge=false) {')]:[]),
   ...(main.includes('static bool projectPowerSourceRecovery(HWND h,const ymcc::sleeppower::SharedPowerV1& power) {')?
    [fn('powerSourcePublicProjection','static bool projectPowerSourceRecovery(HWND h,const ymcc::sleeppower::SharedPowerV1& power) {')]:[]),
   fn('powerSourceBegin','static void beginPowerSourceRecovery(HWND h,bool sourceEdge) {'),
   fn('powerSourceHeartbeat','static void reconcileAcDcSourceHeartbeat(HWND h) {')].join('\n');
 }
 if(parts.MSI_SHARED_READY.includes('msiNativeForegroundContinueDeferredReady(power)')){
  parts.MSI_SHARED_READY_PORTS=fn('msiReadyWork','struct MsiNativeForegroundReadyWork {')+';\n'+
   'static std::atomic<uint64_t> g_msiNativeForegroundEdge{1};\n'+
   'static void msiNativeForegroundQueue(const MsiNativeForegroundReadyWork* work){if(!work||!sharedPowerLifecycleCurrent(work->power))std::abort();stage("msi",work->power.generation);}\n'+
   (msiHeader.includes('static bool msiNativeForegroundReadyWorkCurrent(')?
     'static bool inputHostExitFenceRaised(){return false;}\n'+
     fragment(msiHeader,'msiReadyWorkCurrent','static bool msiNativeForegroundReadyWorkCurrent(')+'\n':'')+
   fragment(msiHeader,'msiContinueDeferredReady','static void msiNativeForegroundContinueDeferredReady(');
 }else parts.MSI_SHARED_READY_PORTS='';
 if(template.includes('@@MSI_RAW_WAKE@@'))parts.MSI_RAW_WAKE=[
  fragment(msiHeader,'msiHoldPower','static void msiNativeForegroundHoldPowerLocked('),
  fragment(msiHeader,'msiRawWake','static void msiNativeForegroundPowerEvent('),
  fragment(msiHeader,'msiSharedBoundary','static bool msiNativeForegroundSharedPowerBoundary('),
  fragment(msiHeader,'msiCommitCatchup','static bool msiNativeForegroundTryReleasePowerHold(')
 ].join('\n');
 if(template.includes('@@WALLPAPER_POWER_READY@@')){
  const current='static bool steamWallpaperForegroundSharedPowerChanged(const ymcc::sleeppower::SharedPowerV1& power) {';
  const previous='static bool steamWallpaperForegroundPublicReady(const ymcc::sleeppower::SharedPowerV1& power) {';
  const anchor=main.includes(current)?current:previous;
  parts.WALLPAPER_POWER_READY=main.includes(anchor)?[
   fn('wallpaperForegroundWake','static bool steamWallpaperForegroundWake() {'),
   fn('wallpaperPublicReady',anchor),
  ].join('\n'):'';
 }
 if(template.includes('@@DECKY_REPEATED_COMMIT@@')){
  const current='static bool deckySidebarRecheckPublicReady(const ymcc::sleeppower::SharedPowerV1& power) {';
  const previous='static bool deckySidebarRecheckPublicReady(const ymcc::sleeppower::SharedPowerV1& power) noexcept {';
  const anchor='static bool deckySidebarRecheckPublicReady(const ymcc::sleeppower::SharedPowerV1& power,bool observeOnly) noexcept {';
  const legacy='static void deckySidebarRecheckRepeatedCommit(';
  parts.DECKY_REPEATED_COMMIT=main.includes(current)?fn('deckyPublicReady',current):main.includes(previous)?fn('deckyPublicReady',previous):main.includes(anchor)?fn('deckyPublicReady',anchor):main.includes(legacy)?fn('deckyRepeatedCommit',legacy):'';
  if(template.includes('@@DECKY_FIRST_READY@@'))parts.DECKY_FIRST_READY=main.includes(current)?
    fn('deckyFirstReady','static bool deckySidebarResumeAfterIntent('):
    '[[maybe_unused]] static bool deckySidebarResumeAfterIntent(unsigned long long generation,const char*){if(deckyFirstAdmission){auto admit=std::exchange(deckyFirstAdmission,{});if(!admit())return false;}if(generation!=currentPowerGeneration())return false;++deckyFirstOffers;return true;}';
 }
 let code=template;for(const[name,value]of Object.entries(parts)){const marker='@@'+name+'@@';uniqueAnchor(code,marker,'current projection fixture '+name);code=code.replace(marker,()=>value);}if(/@@[A-Z_]+@@/.test(code))throw Error('Unknown shared projection slot');
 const caseNames=[...code.matchAll(/\brun\("([^"\r\n]+)",/g)].map(row=>row[1]);if(!caseNames.length||new Set(caseNames).size!==caseNames.length)throw Error('Empty/duplicate shared projection cases');return{code,extraction,caseNames};
}
