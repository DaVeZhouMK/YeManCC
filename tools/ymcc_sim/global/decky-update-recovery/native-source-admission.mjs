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

export function prepareNativeSource({main,template,repairHeader,automaticHeader,mainSha256}){
 const outputs=new Map();
 // Each required slot must be filled exactly once before any producer starts.
 for(const name of ['SESSION_DIAGNOSTIC','PREPARE_ENVIRONMENT','WAKE_READY','ADMISSION','MODE_CHANGED','STABLE_BIGPICTURE','WAKE_COMMIT','WAKE_MESSAGE','WAKE_DELIVERY'])uniqueAnchor(template,'/*__'+name+'__*/','fixture template '+name);
 const a=uniqueAnchor(main,'[]() -> ymcc::deckysidebar::LaunchAdmission {','launch admission'),m=uniqueAnchor(main,'modeObserver.changed = []','mode observer');
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
 const readyStart=uniqueAnchor(main,'static bool deckySidebarResumeAfterIntent(','shared Decky Ready consumer'),readyBlock=cppBlock(main,main.indexOf('{',readyStart),'shared Decky Ready consumer');
 const prewarmStart=uniqueAnchor(main,'static std::string deckySidebarPrepareEnvironment(','native environment preparation'),prewarmBlock=cppBlock(main,main.indexOf('{',prewarmStart),'native environment preparation'),prewarmEnd=prewarmBlock.end;
 const diagnosticStart=uniqueAnchor(main,'static void deckySidebarSessionDiagnostic(','session diagnostic'),diagnosticBlock=cppBlock(main,main.indexOf('{',diagnosticStart),'session diagnostic');
 const fixed=template.replace('/*__SESSION_DIAGNOSTIC__*/',main.slice(diagnosticStart,diagnosticBlock.end)).replace('/*__PREPARE_ENVIRONMENT__*/',main.slice(prewarmStart,prewarmEnd)).replace('/*__WAKE_READY__*/',main.slice(readyStart,readyBlock.end)).replace('/*__ADMISSION__*/',admission).replace('/*__MODE_CHANGED__*/',changed).replace('/*__STABLE_BIGPICTURE__*/',stable).replace('/*__WAKE_COMMIT__*/',wakeBody).replace('/*__WAKE_MESSAGE__*/',wakeMessage+'\n'+commitMessage).replace('/*__WAKE_DELIVERY__*/',delivery);outputs.set('fixed.cpp',fixed);
 const legacy=fixed.replace('if (mode == "bigpicture") {','if (mode == "bigpicture") { if(!g_steamLinkAdmission.allows(session)&&!g_deckyUpdateRecovery.allows(session))return {false,"ymcc-startup-observation-only",false};');if(legacy===fixed)throw Error('Legacy origin-gate mutation anchor changed');outputs.set('legacy.cpp',legacy);
 const retainedNegative=fixed.replace('return {true,"retained-frontend-rebind",false,{},true};','return {false,"decky-context-already-loaded",false};');if(retainedNegative===fixed)throw Error('Retained frontend admission mutation missing');outputs.set('retained-negative.cpp',retainedNegative);
 const uncertainDispatch=/^([ \t]*)dispatchEntered=true;(\r?\n[ \t]*Json result;)/gm;
 if([...repairHeader.matchAll(uncertainDispatch)].length!==1)throw Error('Uncertain manual-dispatch mutation anchor must be unique');
 const uncertainHeader=repairHeader.replace(uncertainDispatch,'$1dispatchEntered=false;$2');
 outputs.set('uncertain-manual.h',uncertainHeader);outputs.set('uncertain-auto.h',automaticHeader.replace('#include "decky_sidebar_manual_repair.h"','#include "uncertain-manual.h"'));
 outputs.set('repair-uncertain-negative.cpp',fixed.replace('#include "decky_sidebar_manual_repair.h"','#include "uncertain-manual.h"').replace('#include "decky_sidebar_automatic_repair.h"','#include "uncertain-auto.h"'));
 const retainedRepairHeader=repairHeader.replace('(runtime.at("phase")=="frontend-attached"&&runtime.at("reason")=="retained-frontend-connected")','false');
 if(retainedRepairHeader===repairHeader)throw Error('Retained backend repair mutation anchor missing');
 outputs.set('retained-manual.h',retainedRepairHeader);outputs.set('retained-auto.h',automaticHeader.replace('#include "decky_sidebar_manual_repair.h"','#include "retained-manual.h"'));
 outputs.set('repair-retained-negative.cpp',fixed.replace('#include "decky_sidebar_manual_repair.h"','#include "retained-manual.h"').replace('#include "decky_sidebar_automatic_repair.h"','#include "retained-auto.h"'));
 const staleHeader=repairHeader.replace('origin!=Origin::Automatic&&deps.freshMode?deps.freshMode(session):deps.mode(session)','deps.mode(session)');if(staleHeader===repairHeader)throw Error('Manual stale-cache mutation anchor missing');
 outputs.set('stale-manual.h',staleHeader);outputs.set('stale-auto.h',automaticHeader.replace('#include "decky_sidebar_manual_repair.h"','#include "stale-manual.h"'));
 outputs.set('repair-stale-mode-negative.cpp',fixed.replace('#include "decky_sidebar_manual_repair.h"','#include "stale-manual.h"').replace('#include "decky_sidebar_automatic_repair.h"','#include "stale-auto.h"'));
 // The message now rejects an uncommitted phase before every follow-up. The
 // original negative must remove both phase checks while retaining generation
 // ownership; removing only the later check no longer tests missing admission.
 let noPowerBody=wakeBody.replace('if (g_powerLifecycle.load(std::memory_order_acquire) == PowerLifecycle::Ready)', 'if (true)');
 if(wakeBody.includes('const auto generation = static_cast<unsigned long long>(l);')){
  const early=/g_powerLifecycle\.load\(std::memory_order_acquire\)\s*!= PowerLifecycle::Ready/g;
  if([...noPowerBody.matchAll(early)].length<1)throw Error('Native early wake phase mutation missing');
  noPowerBody=noPowerBody.replace(early,'false');
 }
 const noPowerGate=fixed.replace(wakeBody,noPowerBody);if(noPowerGate===fixed)throw Error('Native wake power guard mutation missing');outputs.set('wake-power-guard-negative.cpp',noPowerGate);
 const noWakeBinding=fixed.replace('const auto rebound=g_deckySidebarRuntime.requestBindingRefresh(generation);','const auto rebound=false;');if(noWakeBinding===fixed)throw Error('Native wake binding mutation missing');outputs.set('wake-binding-negative.cpp',noWakeBinding);
 const noReadyEvent=fixed.replace('ipc_emit("power.resumed",','ipc_emit("power.pending",');if(noReadyEvent===fixed)throw Error('Native wake ready event mutation missing');outputs.set('wake-ready-event-negative.cpp',noReadyEvent);
 const noAutoGuard=repairHeader.replace('if(origin==Origin::Automatic)return refuse("steam-ui-refresh-requires-user");','');if(noAutoGuard===repairHeader)throw Error('Automatic UI guard anchor missing');
 outputs.set('unsafe-automatic-manual.h',noAutoGuard);outputs.set('unsafe-automatic.h',automaticHeader.replace('#include "decky_sidebar_manual_repair.h"','#include "unsafe-automatic-manual.h"'));
 outputs.set('repair-auto-ui-negative.cpp',fixed.replace('#include "decky_sidebar_manual_repair.h"','#include "unsafe-automatic-manual.h"').replace('#include "decky_sidebar_automatic_repair.h"','#include "unsafe-automatic.h"'));
 const repeatHeader=automaticHeader.replace('if(spent_||','if(false||');if(automaticHeader===repeatHeader)throw Error('Automatic budget mutation anchor missing');
 outputs.set('repeat-auto.h',repeatHeader);outputs.set('repair-auto-repeat-negative.cpp',fixed.replace('#include "decky_sidebar_automatic_repair.h"','#include "repeat-auto.h"'));
 const prewarmBlocked=fixed.replace('static std::string deckySidebarPrepareEnvironment(const std::function<bool()>& current) {','static std::string deckySidebarPrepareEnvironment(const std::function<bool()>& current) {\n    if(g_steamSettingsObserver.steamPresence()!=1)return "preparation-deferred";');if(prewarmBlocked===fixed)throw Error('Native prewarm negative anchor missing');outputs.set('prewarm-negative.cpp',prewarmBlocked);
 const wakePreparationMissing=fixed.replace('g_deckySidebarRuntime.requestPreparationRefresh(generation)','false');if(wakePreparationMissing===fixed)throw Error('Committed preparation negative anchor missing');outputs.set('wake-preparation-negative.cpp',wakePreparationMissing);
 return {outputs,extraction};
}

// The original committed delivery selector uses the same lexical extraction.
export function prepareCommittedDeliverySource({main,template}){
 const extraction={};
 const fragment=(name,anchor)=>{const at=uniqueAnchor(main,anchor,name),block=cppBlock(main,main.indexOf('{',at),name),code=main.slice(at,block.end);extraction[name]={begin:at,end:block.end,sha256:crypto.createHash('sha256').update(code).digest('hex')};return code;};
 const commitAt=uniqueAnchor(main,'    case WM_POWER_RESUME_COMMIT: {','committed message'),commitBlock=cppBlock(main,main.indexOf('{',commitAt),'committed message');
 const commit=main.slice(commitBlock.open,commitBlock.end);extraction.commit={begin:commitBlock.open,end:commitBlock.end,sha256:crypto.createHash('sha256').update(commit).digest('hex')};
 const timerAt=uniqueAnchor(main,'if(w==kSleepPowerRedeliveryTimer){','original redelivery timer'),timerBlock=cppBlock(main,main.indexOf('{',timerAt),'original redelivery timer'),timer=main.slice(timerAt,timerBlock.end);extraction.timer={begin:timerAt,end:timerBlock.end,sha256:crypto.createHash('sha256').update(timer).digest('hex')};
 const lines=['#define WM_POWER_RESUME_COMMIT ','#define WM_POWER_INPUT_RECOVER ','static constexpr UINT kSleepPowerForwardMessage=','static constexpr UINT kSharedPowerStateMessage=','static constexpr UINT_PTR kSleepPowerRedeliveryTimer=','static std::atomic<unsigned long long> g_powerResumeCommitRedelivery'].map(anchor=>{const matches=main.split('\n').filter(line=>line.startsWith(anchor));if(matches.length!==1)throw Error('Unique committed delivery declaration '+anchor);return matches[0];});
 const enumeration=main.match(/^enum class PowerLifecycle\s*:\s*uint8_t\s*\{[^\n]+\};$/m);if(!enumeration)throw Error('Committed delivery power enum missing');
 const parts={POWER_ENUM:enumeration[0],MESSAGES:lines.slice(0,5).join('\n'),DELIVERY:lines[5]+'\n'+['sleepPowerArmRedelivery',...(main.includes('static void sleepPowerArmUnknownRedelivery(')?['sleepPowerArmUnknownRedelivery']:[]),...(main.includes('static void sleepPowerRetainCommittedDelivery(')?['sleepPowerRetainCommittedDelivery']:[]),'sleepPowerPostResumeCommit','sleepPowerRedeliverCommitted'].map(name=>fragment(name,name==='sleepPowerArmRedelivery'?'static void sleepPowerArmRedelivery(HWND window) {':'static void '+name+'(')).join('\n'),COMMIT:commit,TIMER:timer};
 let code=template;for(const [name,value]of Object.entries(parts)){const marker='@@'+name+'@@';uniqueAnchor(code,marker,'committed fixture '+name);code=code.replace(marker,()=>value);}if(/@@[A-Z_]+@@/.test(code))throw Error('Unknown committed fixture slot');return {code,extraction};
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
 const fragment=(source,name,anchor)=>{const at=uniqueAnchor(source,anchor,name),block=cppBlock(source,source.indexOf('{',at),name),code=source.slice(at,block.end);extraction[name]={begin:at,end:block.end,sha256:crypto.createHash('sha256').update(code).digest('hex'),source:source===main?'native/main.cpp':'native/msi_native_foreground_isolation_runtime.h'};return code;};
 const fn=(name,anchor)=>fragment(main,name,anchor),line=anchor=>{const rows=main.split('\n').filter(row=>row.startsWith(anchor));if(rows.length!==1)throw Error('Unique Native projection declaration '+anchor);return rows[0];};
 const focusIsolation=[fn('focusIsolationTicket','struct FocusLightIsolationTicket {')+';',fn('focusIsolationEnd','static void focusEndIsolationOnUi() {'),fn('focusIsolationSharedPowerChanged','static bool focusIsolationSharedPowerChanged(').replace('static bool focusIsolationSharedPowerChanged(','static bool productionFocusIsolationSharedPowerChanged(')];
 const enumeration=main.match(/^enum class PowerLifecycle\s*:\s*uint8_t\s*\{[^\n]+\};\r?$/m);if(!enumeration)throw Error('Shared projection power enum missing');
 const at=uniqueAnchor(main,'    case kSharedPowerStateMessage: {','current projection message'),block=cppBlock(main,main.indexOf('{',at),'current projection message'),windowCase=main.slice(block.open,block.end);extraction.windowCase={begin:block.open,end:block.end,sha256:crypto.createHash('sha256').update(windowCase).digest('hex')};
 const projection=main.includes('static void projectSharedPowerLifecycle(HWND window) {')?fn('projection','static void projectSharedPowerLifecycle(HWND window) {'):'static LRESULT actualLegacySharedProjection([[maybe_unused]] HWND h) '+windowCase+'\nstatic void projectSharedPowerLifecycle(HWND window){(void)actualLegacySharedProjection(window);}';
 if(main.includes('static void projectSharedPowerLifecycle(HWND window) {')&&!/^\{\s*projectSharedPowerLifecycle\(h\);\s*(?:inputCaptureSensorThreadRecoverReturned\(h\);\s*)?return 0;\s*\}$/.test(windowCase))throw Error('Projection window delegate changed');
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
 const parts={CSS_GUARD_CONFIG:cssConfig.join('\n'),CSS_MANUAL_GUARD_CONFIG:cssConfig.join('\n'),RECOVERY_PENDING:recoveryExtract(recoveryRuntime,'recoveryPending','struct Pending {')+';',FAN_RESUME:fanResume,POWER_ENUM:enumeration[0],PHASE_NAME:fn('powerLifecycleName','static const char* powerLifecycleName('),MESSAGES:['#define WM_POWER_RESUME_COMMIT ','#define WM_POWER_INPUT_RECOVER ','static constexpr UINT kSleepPowerForwardMessage=','static constexpr UINT kSharedPowerStateMessage=','static constexpr UINT_PTR kSleepPowerRedeliveryTimer='].map(line).join('\n'),READER:[fn('hardwareWriteAllowed','static bool hardwareWriteAllowed() {'),fn('currentPowerGeneration','static unsigned long long currentPowerGeneration() {'),fn('readSharedSystemPower','static int __cdecl readSharedSystemPower(ymcc::sleeppower::SharedPowerV1* output,')].join('\n'),ACCESSORS:[fn('captureAccessor','[[maybe_unused]] static bool captureSharedPowerLifecycle('),fn('currentAccessor','[[maybe_unused]] static bool sharedPowerLifecycleCurrent(')].join('\n'),SNAPSHOT:fn('snapshot','static json sharedPowerLifecycleSnapshot() {').replace('static json sharedPowerLifecycleSnapshot()','static json productionSharedPowerLifecycleSnapshot()'),MSI_SHARED_READY:fragment(msiHeader,'msiSharedReady','static bool msiNativeForegroundSharedPowerReady(').replace('static bool msiNativeForegroundSharedPowerReady(','static bool productionMsiNativeForegroundSharedPowerReady('),DELIVERY:[line('static std::atomic<unsigned long long> g_powerResumeCommitRedelivery'),fn('arm','static void sleepPowerArmRedelivery(HWND window) {'),...(main.includes('static void sleepPowerArmUnknownRedelivery(')?[fn('armUnknownDelivery','static void sleepPowerArmUnknownRedelivery(')]:[]),...(main.includes('static void sleepPowerRetainCommittedDelivery(')?[fn('retainDelivery','static void sleepPowerRetainCommittedDelivery(')]:[]),fn('postCommit','static void sleepPowerPostResumeCommit('),fn('redeliver','static void sleepPowerRedeliverCommitted(')].join('\n'),CSS_RESUME:fn('cssLegacyReady','static bool steamAppearanceResumeCommitted(')+'\n'+(main.includes('static void steamAppearancePublicReady(')?fn('cssPublicReady','static void steamAppearancePublicReady(unsigned long long generation) noexcept {'):''),READY_COMMIT:fn('openReadyGate','static void openHardwareWriteGate() {')+'\n'+fn('readyCommit','static bool commitPowerResume(unsigned long long generation, const char* source) {'),RECOVERY_CONSUMER:recoveryConsumerCode,RECOVERY_STARTUP_SELECTOR:recoverySelector,RECOVERY_FINAL_ADMISSION:recoveryFinalAdmission,CLASSIFICATION:classification,FAILURE_LOG:main.includes('static void sharedPowerProjectionConsumerFailed(')?fn('failureLog','static void sharedPowerProjectionConsumerFailed('):'',PUBLISHER:fn('publisher','static void publishSharedPowerLifecycle(const char* reason) {'),PROJECTION:projection,TIMER:timer};
 parts.FOCUS_TICKET=focusIsolation[0];
 parts.FOCUS_ISOLATION=focusIsolation.slice(1).join('\n');
 if(template.includes('@@DECKY_REPEATED_COMMIT@@')){
  const anchor='static void deckySidebarRecheckRepeatedCommit(';
  parts.DECKY_REPEATED_COMMIT=main.includes(anchor)?fn('deckyRepeatedCommit',anchor):'';
 }
 let code=template;for(const[name,value]of Object.entries(parts)){const marker='@@'+name+'@@';uniqueAnchor(code,marker,'current projection fixture '+name);code=code.replace(marker,()=>value);}if(/@@[A-Z_]+@@/.test(code))throw Error('Unknown shared projection slot');
 const caseNames=[...code.matchAll(/\brun\("([^"\r\n]+)",/g)].map(row=>row[1]);if(!caseNames.length||new Set(caseNames).size!==caseNames.length)throw Error('Empty/duplicate shared projection cases');return{code,extraction,caseNames};
}
