// Compile and exercise the production recorder/IPC/catalog/consumption policy with memory-only boundaries.
import{captureOemInputHeaders}from'./lib/hc_oem_input_fixture.mjs';
import fs from'node:fs';import path from'node:path';import assert from'node:assert/strict';import{spawnSync}from'node:child_process';import{createHash}from'node:crypto';
const root=process.cwd(),out=path.resolve(process.argv[2]||'G:/YeManCC-Work/Build/Tasks/ControllerOemRecording-20261101/native');fs.mkdirSync(out,{recursive:true});const sourceFile=path.resolve(process.env.YMCC_OEM_RECORDING_MAIN_SOURCE||path.join(root,'native/main.cpp')),source=fs.readFileSync(sourceFile,'utf8');
const oemHeaders=captureOemInputHeaders({repo:root,out,source});
const sourceInputs=['native/main.cpp','native/msi_qs_keyboard_mirror.h','native/keyboard_shortcut_sequence.h','deps/json/json.hpp','tools/controller_oem_recording_native_selftest.mjs','tools/lib/hc_oem_input_fixture.mjs'].map(file=>({file,sourcePath:file==='native/main.cpp'?sourceFile:path.join(root,file),sha256:createHash('sha256').update(fs.readFileSync(file==='native/main.cpp'?sourceFile:path.join(root,file))).digest('hex')}));
function fn(name){const m=new RegExp(String.raw`^static [^\n;]*\b${name}\([^;]*?\)\s*\{[\s\S]*?\n\}`,'m').exec(source);assert(m,name);return m[0]}
function declaration(name){const a=source.indexOf('enum class '+name),b=source.indexOf('};',a);assert(a>0&&b>a);return source.slice(a,b+2)}
const catalog=source.slice(source.indexOf('enum class OemKeyForm'),source.indexOf('// Per-thread immutable binding only;',source.indexOf('enum class OemKeyForm')));
const oemGetStart=source.indexOf('    ipc_on("oem.keys.get",'),oemGetEnd=source.indexOf('    // Read-only presentation of the SAME capability-bound OEM catalog.',oemGetStart);
assert(oemGetStart>0&&oemGetEnd>oemGetStart);const oemGetIpc=source.slice(oemGetStart,oemGetEnd);
const recordIpc=source.slice(source.indexOf('    ipc_on("shortcut.recording.set",'),source.indexOf('    // 模拟鼠标方案与统一开关。',source.indexOf('    ipc_on("shortcut.recording.set",')));
const recorderStart=source.indexOf('static void gamepadEval()'),focusStart=source.indexOf('    if (g_shortcutRecordingActive.load',recorderStart),focusEnd=source.indexOf('    if (radialHasInputOwner())',focusStart);assert(recorderStart>0&&focusEnd>focusStart);const focus=source.slice(focusStart,focusEnd);
const models=[['AsusRogAlly','XboxROGAlly'],['AsusRogAllyX','XboxROGAllyX'],['AsusRogAllyClassic','ROGAlly'],['AsusRogAllyXClassic','ROGAllyX'],['MsiClaw','ClawA1M'],['MsiClaw','ClawG3E'],['LenovoLegionGo','LegionGoTablet'],['Gpd','GPDWin5'],['Ayanero','AYANEO2'],['Ayanero','AYANEOKUN'],['Ayanero','AYANEONEXT2'],['Aokzoe','AOKZOEA1'],['Ayn','AynLoki'],['ZotacGamingZone','GamingZone'],['OneXPlayer','OneXPlayerX1'],['OneXPlayer','OneXPlayer2'],['OneXPlayer','OneXPlayerX2'],['OneXPlayer','OneXPlayer3'],['ValveSteamDeck','SteamDeck'],['Unknown','unknown']];
// Include canonical/aliased factory identities to prove the recording mask uses the same bound catalog as the editor.
for(const match of fs.readFileSync(path.join(root,'capabilities/hc-device-catalog.generated.h'),'utf8').matchAll(/\{"hc\.device\.[^"]+", "([^"]+)", "([^"]+)"/g)){
 const cls=match[1],consumer=match[2];
 if(/OneXPlayer/.test(consumer))for(const identity of [cls,consumer])if(!models.some(([f,name])=>f==='OneXPlayer'&&'HandheldCompanion.Devices.'+name===identity))models.push(['OneXPlayer',identity.replace('HandheldCompanion.Devices.','')]);
 if(cls===consumer)continue;
 const family=/ROGAlly/.test(consumer)?(/XboxROGAllyX/.test(consumer)?'AsusRogAllyX':/XboxROGAlly/.test(consumer)?'AsusRogAlly':/ROGAllyX/.test(consumer)?'AsusRogAllyXClassic':'AsusRogAllyClassic'):/^HandheldCompanion\.Devices\.AYANEO/.test(consumer)?'Ayanero':/GPD/.test(consumer)?'Gpd':/Claw/.test(consumer)?'MsiClaw':/OneXPlayer/.test(consumer)?'OneXPlayer':/LegionGo/.test(consumer)?'LenovoLegionGo':/AOKZOE/.test(consumer)?'Aokzoe':/Ayn/.test(consumer)?'Ayn':/SuiPlay/.test(consumer)?'SuiPlay':/GamingZone/.test(consumer)?'ZotacGamingZone':null;
 if(family&&!models.some(([f,name])=>f===family&&'HandheldCompanion.Devices.'+name===cls))models.push([family,cls.replace('HandheldCompanion.Devices.','')]);
}
if(!models.some(([f,n])=>f==='OneXPlayer'&&n==='OneXPlayerX2MiniPro'))models.push(['OneXPlayer','OneXPlayerX2MiniPro']);
const cpp=String.raw`
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <Xinput.h>
#include <string>
#include <vector>
#include <array>
#include <atomic>
#include <algorithm>
#include <iostream>
#include <fstream>
#include <functional>
#include <map>
#include <deque>
#include <cstring>
#include "json.hpp"
#include "msi_qs_keyboard_mirror.h"
#include "keyboard_shortcut_sequence.h"
${oemHeaders.preamble}
using json=nlohmann::json;
${declaration('YmccFamily')}
${fn('ymccFamilyName')}
${declaration('OemChordProfileId')}
static struct {YmccFamily family=YmccFamily::Unknown;std::string deviceClass;}g_machineIdentity;
${catalog}
${fn('oemKeyCaptureImplemented')}
${fn('oemChordProfileIdFor')}
${fn('oemUnifiedInputBindingFor')}
${fn('oemBoundCatalogForFamily')}
${fn('nativeKeyboardShortcutMaskBit')}
${fn('gamepadOemPressedKeys')}
static std::atomic<bool> g_shortcutRecordingActive{false},g_oemHooksWanted{true},g_oemKeyHooksActive{true},g_exitRequested{false};
static std::atomic<uint64_t> g_shortcutRecordingOemMask{0},g_msiQsKeyboardMirrorPolicy{0},g_msiQsKeyboardMirrorSourceGeneration{0},g_msiWmiActiveGeneration{1},g_oemConsumePolicyMask{0};
static std::atomic<int>g_oemBackProfileId{int(OemChordProfileId::None)},g_oemConsumePolicyProfile{int(OemChordProfileId::None)};
static std::atomic<uint32_t>g_oemShortcutEdgeEpoch{1},g_oemConsumePolicyEpoch{1};
static std::atomic<bool>g_msiNativeRearDemand{false},g_gpdBackIfaceReady{true};
static bool wmi=true;static uint64_t physicalAuthority=0,dinputAuthority=0;
static bool msiWmiShortcutAuthoritative(){return wmi;}static bool msiClawKnownBoardAuthority(){return true;}static bool msiQsKeyboardMirrorSourceCurrent(uint64_t n){return n==1;}static int msiDinputBackAuthoritativeMask(){return int(dinputAuthority);}static uint64_t oemProducerAuthorityMask(){return physicalAuthority;}
${[...source.matchAll(/^static constexpr uint64_t kOemBit\w+ = [^\n]+/gm)].map(m=>m[0]).join('\n')}
struct {bool replayFault=false;}g_oemKeyboardConsume;
static constexpr int kOemProducerCount=8;
${source.match(/struct NativeKeyboardShortcutRule \{[\s\S]*?\n\};/)[0]}
static std::vector<NativeKeyboardShortcutRule>g_nativeKeyboardShortcuts;
${fn('nativeOemKeyboardConsumePublish')}
${fn('oemKeyboardConsumeEligibleMask')}
static uint64_t g_shortcutRecordingMask=0;static bool g_shortcutRecordingAwaitNeutral=false;
static WORD g_curW=0,g_prevW=0;static XINPUT_GAMEPAD g_curPad{},g_prevPad{};static uint64_t displayMask=0;
static json events=json::array();static void ipc_emit(const std::string&name,const json&data){events.push_back({{"name",name},{"data",data}});}
static uint64_t nativeOemKeyMask(bool){return displayMask;}
static uint64_t gamepadShortcutRecordingMask(WORD w,const XINPUT_GAMEPAD&){return w;}
${fn('gamepadRecordShortcutChord')}
${fn('gamepadRecordShortcutClick')}
static bool vendorRequested=false,vendorApplied=false,eligible=true,custom=false;
static void oemVendorStackRecordingRequest(bool wanted){vendorRequested=wanted;}
static json oemVendorStackRecordingSnapshot(){return{{"requested",vendorRequested},{"applied",vendorApplied},{"pending",false},{"failed",!vendorApplied}};}
static bool gamepadUiInputEligible(){return eligible;}static bool customSteamLibraryInputSuppressed(){return custom;}
${fn('nativeShortcutRecordingCatalogMask')}
${fn('nativeShortcutRecordingSnapshot')}
${fn('nativeShortcutRecordingStop')}
// Exercise the production queued keyboard consumer, not a direct recorder call.
static std::atomic<uint32_t>g_gpdBackIfaceGen{1};
${fn('oemShortcutEdgeToken')}
${fn('oemShortcutMessageTick')}
${fn('oemShortcutEdgeCurrent')}
static unsigned shortcutEvaluations=0;
static void nativeKeyboardShortcutsRefresh(ULONGLONG){}

static void msiNativeRearKeyboardEdgeNote(uint64_t){}
static uint64_t oemProducerActionMask(bool=false){return 0;}
static json edgeLogs=json::array();
static void appendNativeLifecycleLog(const char* name,const json&data){edgeLogs.push_back({{"event",name},{"data",data}});}
${fn('nativeLifecycleEventIsBase')}
${fn('nativeOneXOemObserve')}
${declaration('OemProducerId')}
${fn('oemProducerName')}
${source.match(/struct OemProducerTokens \{[\s\S]*?\n\};/)[0]}
${source.match(/struct OemProducerEdgeReceipt \{[\s\S]*?\n\};/)[0]}
${source.match(/static constexpr size_t kOemProducerEdgeCapacity = [^\n]+/)[0]}
${source.match(/static constexpr std::array<uint64_t, kOemProducerCount> kOemProducerMasks = \{[\s\S]*?\n\};/)[0]}
static std::deque<OemProducerEdgeReceipt>producerQueue;static bool producerCurrent=true;
static bool oemProducerTryPop(OemProducerEdgeReceipt& receipt){if(producerQueue.empty())return false;receipt=producerQueue.front();producerQueue.pop_front();return true;}
static bool oemProducerCurrent(OemProducerId,uint64_t generation){return producerCurrent&&generation==1;}
static void nativeOnexX2HomeDispatch(const OemProducerEdgeReceipt&){}
static void nativeRogClickDispatch(const OemProducerEdgeReceipt&){}
static void nativeKeyboardShortcutsEvaluate(WORD,const XINPUT_GAMEPAD&,ULONGLONG,bool,const uint64_t*,bool,uint64_t=0,const OemProducerTokens* = nullptr){++shortcutEvaluations;}
// Model audit sink is an observation boundary, not an input/action authority.
static void modelAdaptationFlush(){}
static void modelAdaptationProducer(unsigned,uint64_t,unsigned){}
${fn('nativeOemProducerDrain')}
static LONG fixtureGetMessageTime(){return LONG(GetTickCount64());}
#define GetMessageTime fixtureGetMessageTime
${fn('nativeKeyboardShortcutHandleOemEdge')}
#undef GetMessageTime

static std::map<std::string,std::function<json(const json&)>>ipc;
static void ipc_on(const std::string& name,std::function<json(const json&)>handler){ipc[name]=handler;}
static bool domainLogEnabled(const char*){return false;}
static void registerIpc(){${recordIpc}
${oemGetIpc}}
static void focusTick(WORD w=0){const bool customSessionSuppressed=custom;
${focus}
}
static json modelCatalogs=json::object();
static int checks=0;static json failures=json::array();static void check(bool ok,const std::string&name){++checks;if(!ok){failures.push_back(name);std::cout<<"FAIL "<<name<<'\n';}}
int main(){registerIpc();
${models.map(([family,name])=>`{g_machineIdentity.family=YmccFamily::${family};g_machineIdentity.deviceClass="HandheldCompanion.Devices.${name}";
`).join('\n')}
return 0;
}
`;
// Expand one self-contained model loop body into each model block.
const caseBody=String.raw`
const auto& binding=oemUnifiedInputBindingFor(g_machineIdentity.family,g_machineIdentity.deviceClass);const auto catalog=binding.catalog;const auto consumerClass=binding.model?std::string(binding.model->consumerClass):g_machineIdentity.deviceClass;const auto model=g_machineIdentity.deviceClass;
const bool knownG1NoChords=g_machineIdentity.family==YmccFamily::OneXPlayer&&model.find("OneXPlayerG1")!=std::string::npos;
check(knownG1NoChords?catalog.count==0:(g_machineIdentity.family==YmccFamily::Unknown||catalog.count>0),"test model has its actual catalog or explicitly empty G1 chord support "+model);
const auto apiCatalog=ipc["oem.keys.get"](json::object());modelCatalogs[model]=apiCatalog;
check(apiCatalog.contains("deviceClass")&&apiCatalog["deviceClass"]==model,"actual OEM preview API carries exact observation identity "+model);
check(apiCatalog["keys"].size()==static_cast<size_t>(catalog.count),"actual OEM preview API preserves complete model catalog "+model);
physicalAuthority=~0ull;g_oemBackProfileId=int(OemChordProfileId::AyaneoCec);g_nativeKeyboardShortcuts.clear();vendorApplied=false;eligible=true;custom=false;displayMask=0;g_curW=0;g_exitRequested=false;
const auto started=ipc["shortcut.recording.set"]({{"active",true}});check(started["active"]==true,"failed suppression still permits recording "+model);check(started["suppression"]["applied"]==false,"failed suppression is not reported as success "+model);
const auto mask=g_shortcutRecordingOemMask.load();uint64_t expected=0;int available=0;for(int i=0;i<catalog.count;i++){uint64_t bit=0;const auto& key=catalog.keys[i];if(oemKeyCaptureImplemented(g_machineIdentity.family,consumerClass,key.keyId)&&nativeKeyboardShortcutMaskBit(key.keyId,bit)){expected|=bit;++available;}}
check(mask==expected,"recording mask is exactly the implemented machine catalog "+model);
check(oemKeyboardConsumeEligibleMask()==expected,"recording attempts keyboard/mouse suppression for all known keys despite independent action authority "+model);
for(int i=0;i<catalog.count;i++){const auto& key=catalog.keys[i];uint64_t bit=0;if(!oemKeyCaptureImplemented(g_machineIdentity.family,consumerClass,key.keyId)||!nativeKeyboardShortcutMaskBit(key.keyId,bit))continue;
 ipc["shortcut.recording.set"]({{"active",false}});ipc["shortcut.recording.set"]({{"active",true}});events=json::array();gamepadRecordShortcutChord(0);gamepadRecordShortcutClick(bit);
 bool captured=false;for(const auto& event:events)if(event["name"]=="shortcut.recording.gamepad")for(const auto& input:event["data"]["inputs"])if(input["source"]=="oem"&&input["code"]==key.keyId)captured=true;
 check(captured,"terminal/held recorder retains actual key "+model+" "+key.keyId);
 // The physical event may already be visible when its queued receipt is overtaken by loss of focus.
 events=json::array();displayMask=bit;eligible=false;focusTick();bool flushed=false,stopAfter=false;for(const auto& event:events){if(event["name"]=="shortcut.recording.gamepad"&&event["data"]["released"]==true)for(const auto& input:event["data"]["inputs"])if(input["source"]=="oem"&&input["code"]==key.keyId)flushed=true;if(event["name"]=="shortcut.recording.stopped")stopAfter=flushed;}
 check(flushed&&stopAfter,"focus loss publishes captured OEM before stopped "+model+" "+key.keyId);check(!g_shortcutRecordingActive&&!vendorRequested&&g_shortcutRecordingOemMask==0,"focus stop releases only temporary capture ownership "+model);displayMask=0;eligible=true;
}
ipc["shortcut.recording.set"]({{"active",false}});check(!g_shortcutRecordingActive&&!vendorRequested&&g_shortcutRecordingOemMask==0,"explicit cancel cleans up recording for "+model);
check(g_nativeKeyboardShortcuts.empty(),"recording never creates or changes saved runtime rules "+model);
}
`;
let code=cpp;const begin=code.indexOf(models.map(([family,name])=>`{g_machineIdentity.family=YmccFamily::${family};g_machineIdentity.deviceClass="HandheldCompanion.Devices.${name}";\n`).join('\n'));
assert(begin>0);const old=code.slice(begin,code.indexOf('return 0;',begin));code=code.replace(old,models.map(([family,name])=>`{g_machineIdentity.family=YmccFamily::${family};g_machineIdentity.deviceClass="HandheldCompanion.Devices.${name}";\n`+caseBody).join('\n'));
code=code.replace('return 0;\n}',String.raw`
// Field reproduction: OneX keyboard press and release can both arrive between
// gamepad snapshots. Deliver the real posted masks while the live mask is zero.
g_machineIdentity.family=YmccFamily::OneXPlayer;g_machineIdentity.deviceClass="HandheldCompanion.Devices.OneXPlayerApex";
g_oemBackProfileId=int(OemChordProfileId::OneXApex);g_oemHooksWanted=true;g_oemKeyHooksActive=true;physicalAuthority=dinputAuthority=0;wmi=false;eligible=true;custom=false;
for(const char* id:{"turbo","keyboard","orange"}){
 uint64_t bit=0;check(nativeKeyboardShortcutMaskBit(id,bit),"OneX field key resolves");
 ipc["shortcut.recording.set"]({{"active",false}});ipc["shortcut.recording.set"]({{"active",true}});gamepadRecordShortcutChord(0);events=json::array();displayMask=0;
 const auto token=oemShortcutEdgeToken(g_oemShortcutEdgeEpoch.load(),g_gpdBackIfaceGen.load());
 const auto beforeEvaluations=shortcutEvaluations;
 nativeKeyboardShortcutHandleOemEdge(WPARAM(bit),LPARAM(token));nativeKeyboardShortcutHandleOemEdge(0,LPARAM(token));
 bool captured=false,released=false;for(const auto& event:events)if(event["name"]=="shortcut.recording.gamepad")for(const auto& input:event["data"]["inputs"])if(input["source"]=="oem"&&input["code"]==id){captured=true;if(event["data"]["released"]==true)released=true;}
 check(captured&&released,std::string("OneX posted short chord reaches recorder with live state already neutral ")+id);
 check(shortcutEvaluations==beforeEvaluations,"recording still never executes actions");
}
ipc["shortcut.recording.set"]({{"active",false}});
// Observation is an input receipt, not a held sample or an executed action.
check(nativeLifecycleEventIsBase("onex-oem-input-edge")&&!nativeLifecycleEventIsBase("onex-oem-input-edge-forged"),"OneX receipt survives ordinary log export with exact base admission");
uint64_t keyboardBit=0;nativeKeyboardShortcutMaskBit("keyboard",keyboardBit);
auto token=oemShortcutEdgeToken(g_oemShortcutEdgeEpoch.load(),g_gpdBackIfaceGen.load());
events=json::array();nativeKeyboardShortcutHandleOemEdge(WPARAM(keyboardBit),LPARAM(token));nativeKeyboardShortcutHandleOemEdge(0,LPARAM(token));
check(events.size()==1&&events[0]["name"]=="oem.keys.observed"&&events[0]["data"]["observedKeys"]==json::array({"keyboard"})&&events[0]["data"]["actionExecutionProven"]==false,"OneX ordinary short edge produces one observed input receipt without inventing a held state or action success");
check(edgeLogs.back()["data"]["observedKeys"].empty(),"OneX release remains visible in ordinary evidence");
check(events[0]["data"]["configuredRuleCount"]==0&&events[0]["data"]["actionStatus"]=="no-configured-rule-for-observed-keys","OneX unbound field keys report missing action rules without inventing defaults");
NativeKeyboardShortcutRule observedRule;observedRule.requiredMask=keyboardBit;g_nativeKeyboardShortcuts={observedRule};
events=json::array();nativeOneXOemObserve(keyboardBit,"keyboard-chord");
check(events[0]["data"]["configuredRuleCount"]==1&&events[0]["data"]["matchingRuleCount"]==1&&events[0]["data"]["actionStatus"]=="configured-awaiting-evaluation"&&events[0]["data"]["actionExecutionProven"]==false,"OneX configured receipt still does not claim execution");
g_nativeKeyboardShortcuts.clear();events=json::array();nativeOneXOemObserve(0,"keyboard-chord");
check(events.empty()&&edgeLogs.back()["data"]["actionStatus"]=="neutral-observation","neutral evidence never creates a press or default action");
events=json::array();const auto beforeStale=shortcutEvaluations;nativeKeyboardShortcutHandleOemEdge(WPARAM(keyboardBit),LPARAM(token^(1ull<<32)));
check(events.empty()&&shortcutEvaluations==beforeStale,"stale rule epoch cannot update OneX observed feedback or execute actions");
g_oemKeyHooksActive=false;nativeKeyboardShortcutHandleOemEdge(WPARAM(keyboardBit),LPARAM(token));check(events.empty(),"disabled hook cannot create an observed OneX press");g_oemKeyHooksActive=true;
for(const bool customOwner:{false,true}){
 ipc["shortcut.recording.set"]({{"active",true}});gamepadRecordShortcutChord(0);events=json::array();custom=customOwner;eligible=!customOwner?false:true;
 const auto before=shortcutEvaluations;nativeKeyboardShortcutHandleOemEdge(WPARAM(keyboardBit),LPARAM(oemShortcutEdgeToken(g_oemShortcutEdgeEpoch.load(),g_gpdBackIfaceGen.load())));
 check(!g_shortcutRecordingActive&&shortcutEvaluations==before,"OneX posted recorder still stops on focus or owner loss without actions");eligible=true;custom=false;
}
// Exercise current/stale vendor queue consumption through the unchanged source
// generation gate, including the reviewed OP3 full-held route.
for(const auto src:{OemProducerId::OneX,OemProducerId::OneXX2Rear,OemProducerId::OneXX2Home}){
 g_machineIdentity.deviceClass=src==OemProducerId::OneX?"HandheldCompanion.Devices.OneXPlayerApex":"HandheldCompanion.Devices.OneXPlayer3";
 uint64_t bit=0;nativeKeyboardShortcutMaskBit(src==OemProducerId::OneXX2Home?"home":"l4",bit);
 OemProducerEdgeReceipt receipt;receipt.source=src;receipt.generation=1;receipt.ruleEpoch=g_oemShortcutEdgeEpoch.load();receipt.mask=receipt.combinedMask=bit;
 for(const bool fresh:{true,false}){events=json::array();producerCurrent=fresh;producerQueue.push_back(receipt);nativeOemProducerDrain();check(fresh?events.size()==1:events.empty(),"OneX vendor observation admits only the current queued source generation");}producerCurrent=true;
}
g_machineIdentity.family=YmccFamily::MsiClaw;g_machineIdentity.deviceClass="HandheldCompanion.Devices.ClawA1M";g_oemBackProfileId=int(OemChordProfileId::Msi);
ipc["shortcut.recording.set"]({{"active",true}});gamepadRecordShortcutChord(0);events=json::array();nativeKeyboardShortcutHandleOemEdge(WPARAM(kOemBitQs),LPARAM(oemShortcutEdgeToken(g_oemShortcutEdgeEpoch.load(),g_gpdBackIfaceGen.load())));check(events.empty(),"MSI queued keyboard recording behavior is outside OneX enhancement");ipc["shortcut.recording.set"]({{"active",false}});
// Existing ordinary-path policy and source freshness must remain unchanged after recording.
physicalAuthority=0;g_machineIdentity.family=YmccFamily::MsiClaw;g_machineIdentity.deviceClass="HandheldCompanion.Devices.ClawA1M";g_oemBackProfileId=int(OemChordProfileId::Msi);NativeKeyboardShortcutRule rule;rule.needsNeutral=false;rule.requiredMask=kOemBitQs;rule.trigger="press";g_nativeKeyboardShortcuts={rule};nativeOemKeyboardConsumePublish();check(oemKeyboardConsumeEligibleMask()==kOemBitQs,"existing configured MSI QS mirror retains its ordinary owner");g_oemConsumePolicyEpoch=0;check(oemKeyboardConsumeEligibleMask()==0,"stale policy never consumes input");eligible=false;const auto denied=ipc["shortcut.recording.set"]({{"active",true}});check(denied["active"]==false&&!g_shortcutRecordingActive,"recording still requires foreground ownership to begin");
json report={{"modelCatalogs",modelCatalogs},{"pass",failures.empty()},{"checks",checks},{"failures",failures},{"models",${models.length}},{"deviceIo",false},{"serviceIo",false},{"actualProductionRecorderAndIpc",true},{"actualKeyboardSuppressionPolicy",true},{"actualQueuedKeyboardConsumer",true},{"actualVendorQueueDrain",true}};std::ofstream("RESULT.json")<<report.dump(2);std::cout<<"CHECKS "<<checks<<" FAILURES "<<failures.size()<<'\n';return failures.empty()?0:1;
}`.replace('${models.length}',String(models.length)));
const generated=path.join(out,'oem-recording.cpp');fs.writeFileSync(generated,code);const sdk='C:/Program Files (x86)/Windows Kits/10',v='10.0.26100.0',exe=path.join(out,'oem-recording.exe'),batch=path.join(out,'build.cmd');fs.writeFileSync(batch,'@echo off\r\ncall "C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat" '+v+' >nul\r\ncl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 '+['um','shared','ucrt'].map(p=>'/I"'+sdk+'/Include/'+v+'/'+p+'"').join(' ')+' /I"'+root+'/native" /I"'+root+'/deps/json" "'+generated+'" /Fo"'+out+'/oem-recording.obj" /Fe"'+exe+'" /link /LIBPATH:"'+sdk+'/Lib/'+v+'/um/x64" /LIBPATH:"'+sdk+'/Lib/'+v+'/ucrt/x64"\r\nexit /b %errorlevel%\r\n');const build=spawnSync('cmd.exe',['/d','/c',batch],{cwd:out,encoding:'utf8',windowsHide:true});fs.writeFileSync(path.join(out,'BUILD.log'),(build.stdout||'')+(build.stderr||''));assert.equal(build.status,0,(build.stdout||'')+(build.stderr||''));const run=spawnSync(exe,[],{cwd:out,encoding:'utf8',windowsHide:true});fs.writeFileSync(path.join(out,'RUN.log'),(run.stdout||'')+(run.stderr||''));fs.writeFileSync(path.join(out,'SOURCE.json'),JSON.stringify({sourceFile,privateSource:sourceFile!==path.join(root,'native/main.cpp'),mainSha256:createHash('sha256').update(source).digest('hex'),generatedSha256:createHash('sha256').update(code).digest('hex'),inputs:[...sourceInputs,...oemHeaders.inputs]},null,2));assert(oemHeaders.unchanged(),'actual OEM header closure changed during recording test');assert(sourceInputs.every(r=>createHash('sha256').update(fs.readFileSync(r.sourcePath)).digest('hex')===r.sha256),'recording inputs changed during test');console.log(run.stdout+run.stderr);assert.equal(run.status,0,'Production OEM recorder regression');
