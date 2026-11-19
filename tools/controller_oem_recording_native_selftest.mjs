// Compile and exercise the production recorder/IPC/catalog/consumption policy with memory-only boundaries.
import{captureOemInputHeaders}from'./lib/hc_oem_input_fixture.mjs';
import{fileURLToPath}from'node:url';
import fs from'node:fs';import path from'node:path';import assert from'node:assert/strict';import{spawnSync}from'node:child_process';import{createHash}from'node:crypto';
import{calibrationToolchainPreamble}from'./fixtures/hc_reviewed_calibration/toolchain.mjs';
const root=process.cwd(),out=path.resolve(process.argv[2]||'G:/YeManCC-Work/Build/Tasks/ControllerOemRecording-20261101/native');fs.mkdirSync(out,{recursive:true});const sourceFile=path.resolve(process.env.YMCC_OEM_RECORDING_MAIN_SOURCE||path.join(root,'native/main.cpp')),source=fs.readFileSync(sourceFile,'utf8');
const hasDesktopFunction=source.includes('ymcc::input::desktopFunction::');
const hasRecordedChord=source.includes('static bool nativeDesktopChordCapture(');
const desktopFunctionHeader=path.join(path.dirname(sourceFile),'desktop_function_shortcut.h');
const desktopFunctionParameter=hasDesktopFunction?',const ymcc::input::desktopFunction::Edge* = nullptr':'';
const actionCases=process.env.YMCC_OEM_ACTION_CASES||'';assert(!actionCases||['aya-actions','aya-editor-actions','onex-actions','onex-editor-actions','onex-vendor-actions','aokzoe-actions','gpd-actions','native-oem-actions','native-oem-init'].includes(actionCases),'Unknown OEM action selection');if(actionCases)assert(!fs.existsSync(path.join(out,'RESULT.json')),'Existing action evidence is immutable');
const recordingCase=process.env.YMCC_OEM_RECORDING_CASES||'';assert(['','definitions','retained-mouse-guide','retained-family-edges','retained-fallback-recording','retained-fallback-cancel'].includes(recordingCase)&&!(recordingCase&&actionCases),'Unknown or mixed recording selection');
const nativeOemActions=actionCases==='native-oem-actions'||actionCases==='native-oem-init';
const nativeOemControl=process.env.YMCC_OEM_NATIVE_BEFORE_CONTROL==='1';
assert(!nativeOemControl||nativeOemActions,'Native OEM control requires its exact selection');
const vendorActions=actionCases==='onex-vendor-actions';
const editorInputsPath=process.env.YMCC_OEM_EDITOR_SAVED_INPUTS||'';
assert(actionCases.endsWith('-editor-actions')===Boolean(editorInputsPath),'Editor selection requires its saved-rule evidence exclusively');
const editorInputsBytes=editorInputsPath?fs.readFileSync(editorInputsPath):null;
const editorInputs=editorInputsBytes?JSON.parse(editorInputsBytes):null;
if(editorInputs){const prefix=actionCases==='onex-editor-actions'?'HandheldCompanion.Devices.OneXPlayer':'HandheldCompanion.Devices.AYANEO';assert(Array.isArray(editorInputs)&&editorInputs.length,'Empty editor rule evidence');for(const row of editorInputs)assert(row.model?.startsWith(prefix)&&row.keyId&&row.ruleId&&row.input?.buttonMapping?.rules?.shortcutRules?.length,'Malformed editor snapshot');}
const oemHeaders=captureOemInputHeaders({repo:root,out,source});
const sourceInputs=['native/main.cpp','native/msi_qs_keyboard_mirror.h','native/keyboard_shortcut_sequence.h','native/gyro_telemetry_record.h',...(actionCases?['native/keyboard_shortcut_sequence_runtime.h','native/shortcut_action_protocol.h','native/shortcut_action_runtime.h']:[]),'deps/json/json.hpp','tools/controller_oem_recording_native_selftest.mjs','tools/lib/hc_oem_input_fixture.mjs','tools/fixtures/hc_reviewed_calibration/toolchain.mjs'].map(file=>{const sourcePath=file==='native/main.cpp'?sourceFile:file.startsWith('tools/')?path.join(path.dirname(fileURLToPath(import.meta.url)),path.relative('tools',file)):path.join(root,file);return{file,sourcePath,sha256:createHash('sha256').update(fs.readFileSync(sourcePath)).digest('hex')}});
if(hasDesktopFunction)for(const file of ['desktop_function_shortcut.h','sleep_power_state.h']){const sourcePath=path.join(path.dirname(sourceFile),file);sourceInputs.push({file:'native/'+file,sourcePath,sha256:createHash('sha256').update(fs.readFileSync(sourcePath)).digest('hex')});}
if(vendorActions){const file='tools/fixtures/unified_oem_input/onex_vendor_actions.cpp';sourceInputs.push({file,sourcePath:path.join(root,file),sha256:createHash('sha256').update(fs.readFileSync(path.join(root,file))).digest('hex')});}

if(editorInputs){
 const seen=new Set(sourceInputs.map(x=>x.sourcePath));
 const capture=file=>{file=path.resolve(file);if(seen.has(file))return;seen.add(file);const bytes=fs.readFileSync(file);sourceInputs.push({file:path.relative(root,file),sourcePath:file,sha256:createHash('sha256').update(bytes).digest('hex')});for(const match of bytes.toString('utf8').matchAll(/^\s*#include\s+"([^"]+)"/gm)){const next=[path.resolve(path.dirname(file),match[1]),path.join(root,'native',match[1]),path.join(root,'deps/json',match[1])].find(p=>fs.existsSync(p));assert(next,'Missing fixture header '+match[1]);capture(next)}};
 for(const header of ['input_shortcut_toggle.h','shortcut_frontend_button.h','boot_mode_shortcuts.h'])capture(path.join(root,'native',header));
 sourceInputs.push({file:'editor-saved-inputs',sourcePath:path.resolve(editorInputsPath),sha256:createHash('sha256').update(editorInputsBytes).digest('hex')});
}
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
 if(/AYANEO/.test(consumer))for(const identity of [cls,consumer])if(!models.some(([f,name])=>f==='Ayanero'&&'HandheldCompanion.Devices.'+name===identity))models.push(['Ayanero',identity.replace('HandheldCompanion.Devices.','')]);
 if(/^HandheldCompanion\.Devices\.GPD/.test(consumer))for(const identity of [cls,consumer])if(!models.some(([f,name])=>f==='Gpd'&&'HandheldCompanion.Devices.'+name===identity))models.push(['Gpd',identity.replace('HandheldCompanion.Devices.','')]);
 if(/^HandheldCompanion\.Devices\.AOKZOEA(?:1|1Pro|1X|2)$/.test(consumer))for(const identity of [cls,consumer])if(!models.some(([f,name])=>f==='Aokzoe'&&'HandheldCompanion.Devices.'+name===identity))models.push(['Aokzoe',identity.replace('HandheldCompanion.Devices.','')]);
 if(cls===consumer)continue;
 const family=/ROGAlly/.test(consumer)?(/XboxROGAllyX/.test(consumer)?'AsusRogAllyX':/XboxROGAlly/.test(consumer)?'AsusRogAlly':/ROGAllyX/.test(consumer)?'AsusRogAllyXClassic':'AsusRogAllyClassic'):/^HandheldCompanion\.Devices\.AYANEO/.test(consumer)?'Ayanero':/GPD/.test(consumer)?'Gpd':/Claw/.test(consumer)?'MsiClaw':/OneXPlayer/.test(consumer)?'OneXPlayer':/LegionGo/.test(consumer)?'LenovoLegionGo':/AOKZOE/.test(consumer)?'Aokzoe':/Ayn/.test(consumer)?'Ayn':/SuiPlay/.test(consumer)?'SuiPlay':/GamingZone/.test(consumer)?'ZotacGamingZone':null;
 if(family&&!models.some(([f,name])=>f===family&&'HandheldCompanion.Devices.'+name===cls))models.push([family,cls.replace('HandheldCompanion.Devices.','')]);
}
if(!models.some(([f,n])=>f==='OneXPlayer'&&n==='OneXPlayerX2MiniPro'))models.push(['OneXPlayer','OneXPlayerX2MiniPro']);
// Explicit source-reviewed AYA extensions already selected by production identity/catalog code.
for(const name of ['AYANEO3','AYANEOAIR2','AYANEOAIRPro1S','AYANEOAIRPlusNxx','AYANEOFlip11'])if(!models.some(([family,n])=>family==='Ayanero'&&n===name))models.push(['Ayanero',name]);
const hasKeyboardDefinitions=source.includes('static json nativeOemKeyboardContributions(');
const profileStart=source.indexOf('struct OemChordRule {'),profileEnd=source.indexOf(fn('oemChordProfileFor'))+fn('oemChordProfileFor').length;
const recordingDefinitions=hasKeyboardDefinitions?source.match(/enum OemVkBit : uint32_t \{[\s\S]*?\};/)[0]+'\n'+source.slice(profileStart,profileEnd).replace(declaration('OemChordProfileId'),'')+'\n'+fn('oemBackHookVkBit')+'\n'+fn('nativeInputShortcutDesktopKey')+'\n'+fn('nativeOemKeyboardContributions'):'';
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
#include "gyro_telemetry_record.h"
${hasDesktopFunction?'#include '+JSON.stringify(desktopFunctionHeader):''}
${oemHeaders.preamble}
using json=nlohmann::json;
${source.match(/struct GamepadSerialEvent \{[\s\S]*?\n\};/)[0]}
${declaration('YmccFamily')}
${fn('ymccFamilyName')}
${declaration('OemChordProfileId')}
static struct {YmccFamily family=YmccFamily::Unknown;std::string deviceClass;bool resolved=false;}g_machineIdentity;
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
${recordingDefinitions}
struct {bool replayFault=false;}g_oemKeyboardConsume;
static constexpr int kOemProducerCount=8;
${source.match(/struct NativeKeyboardShortcutRule \{[\s\S]*?\n\};/)[0]}
static std::vector<NativeKeyboardShortcutRule>g_nativeKeyboardShortcuts;
${hasDesktopFunction?String.raw`
static std::atomic<uint32_t>g_desktopFunctionCaptureMask{0},g_desktopFunctionArmedMask{0},g_desktopFunctionKeys{0},g_desktopFunctionPhysicalKeys{0};
static std::atomic<bool>g_desktopFunctionHookActive{false};
static uint32_t g_desktopFunctionRuleEpoch=0;
static std::array<std::atomic<uint64_t>,12>g_desktopFunctionCancellations{};
static ymcc::input::desktopFunction::EdgeQueue g_desktopFunctionEdges;
static bool sharedPowerLifecycleCurrent(const ymcc::sleeppower::SharedPowerV1&);
static bool captureSharedPowerLifecycle(ymcc::sleeppower::SharedPowerV1&);
`:''}
static std::atomic<bool>g_oemRawPathActive{false};
${source.includes('static uint64_t rogOemKeyboardFallbackMask() {')?fn('ymccIsRogFamily')+'\n'+fn('oemBoundChordProfileIdFor')+'\n'+fn('rogOemKeyboardFallbackMask'):''}
${hasRecordedChord?String.raw`
// Original recorder owns policy/init only. Live LL/gate IO belongs to the
// shortcut fixture; these explicit memory ports never imply hook acceptance.
static std::atomic<bool>g_desktopChordWanted{false};
static std::atomic<uint64_t>g_desktopChordCancellation{0};
static ymcc::input::desktopFunction::KeyboardKeys g_desktopChordCaptureKeys,g_desktopChordArmedKeys,g_desktopChordKeys;
static std::vector<ymcc::input::desktopFunction::KeyboardChord>g_desktopChordRules;
static bool g_oemGateTargetEnabled=false,g_oemGateBackMapping=false;
static std::string g_oemGatePersona="disabled";
static void oemBackMappingRefreshGate(bool,const std::string&,bool){throw std::runtime_error("RECORDED_KEYBOARD_HOOK_GATE_OUTSIDE_RECORDER_POLICY_SCOPE");}
static ymcc::input::desktopFunction::KeyboardKeys nativeDesktopChordPhysicalKeys(){return g_desktopChordKeys;}
`:''}
${hasRecordedChord?fn('nativeDesktopChordKeyboardKey'):''}
${fn('nativeOemKeyboardConsumePublish')}
${fn('oemKeyboardConsumeEligibleMask')}
static uint64_t g_shortcutRecordingMask=0;static bool g_shortcutRecordingAwaitNeutral=false;
static WORD g_curW=0,g_prevW=0;static XINPUT_GAMEPAD g_curPad{},g_prevPad{};static uint64_t displayMask=0;
static json events=json::array();static void ipc_emit(const std::string&name,const json&data){events.push_back({{"name",name},{"data",data}});}
static uint64_t nativeOemKeyMask(bool){return displayMask;}
static uint64_t gamepadShortcutRecordingMask(WORD w,const XINPUT_GAMEPAD&){return w;}
${fn('gamepadRecordShortcutChord')}
${fn('gamepadRecordShortcutClick')}
static std::atomic<bool>g_inputReleaseRequired{false};
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
static void nativeKeyboardShortcutsEvaluate(WORD,const XINPUT_GAMEPAD&,ULONGLONG,bool,const uint64_t*,bool,uint64_t=0,const OemProducerTokens* = nullptr${desktopFunctionParameter}){++shortcutEvaluations;}
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
static const std::vector<std::string>ayaModels={${models.filter(([family])=>family==='Ayanero').map(([,name])=>JSON.stringify('HandheldCompanion.Devices.'+name)).join(',')}};
int main(){registerIpc();
${models.map(([family,name])=>`{g_machineIdentity.family=YmccFamily::${family};g_machineIdentity.deviceClass="HandheldCompanion.Devices.${name}";
`).join('\n')}
return 0;
}
`;
// Expand one self-contained model loop body into each model block.
const caseBody=String.raw`
const auto& binding=oemUnifiedInputBindingFor(g_machineIdentity.family,g_machineIdentity.deviceClass);const auto catalog=binding.catalog;const auto consumerClass=binding.model?std::string(binding.model->consumerClass):g_machineIdentity.deviceClass;const auto model=g_machineIdentity.deviceClass;
if(model=="HandheldCompanion.Devices.AYANEONEXT2"){
 int rearCount=0;for(int k=0;k<catalog.count;k++)if(catalog.keys[k].backIndex>0)++rearCount;
 check(rearCount==4,"NEXT2 catalog exposes four source-proven rear controls");
 for(const auto& pair:std::vector<std::pair<std::string,int>>{{"m1",1},{"l4",3},{"m2",2},{"r4",4}}){bool found=false;for(int k=0;k<catalog.count;k++)if(catalog.keys[k].keyId==pair.first&&catalog.keys[k].backIndex==pair.second)found=true;check(found,"NEXT2 exact rear position "+pair.first);}
}
if(model=="HandheldCompanion.Devices.AYANEO3"){
 int rearCount=0;for(int k=0;k<catalog.count;k++)if(catalog.keys[k].backIndex>0)++rearCount;
 check(rearCount==0,"AYA3 keeps unproven operation attribution pending rather than inherits NEXT2 four rear controls");
}
const bool knownG1NoChords=g_machineIdentity.family==YmccFamily::OneXPlayer&&model.find("OneXPlayerG1")!=std::string::npos;
const bool knownWin2NoChords=g_machineIdentity.family==YmccFamily::Gpd&&model=="HandheldCompanion.Devices.GPDWin2";
check(knownG1NoChords||knownWin2NoChords?catalog.count==0:(g_machineIdentity.family==YmccFamily::Unknown||catalog.count>0),"test model has its actual catalog or source-declared empty G1/Win2 chord support "+model);
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
// AYA complete keyboard edges can precede the next sampled held state.
for(const auto& model:ayaModels){
 g_machineIdentity.family=YmccFamily::Ayanero;g_machineIdentity.deviceClass=model;
 const auto& binding=oemUnifiedInputBindingFor(g_machineIdentity.family,model);
 g_oemBackProfileId=binding.profileId;g_oemHooksWanted=true;g_oemKeyHooksActive=true;
 physicalAuthority=dinputAuthority=0;wmi=false;eligible=true;custom=false;g_exitRequested=false;g_nativeKeyboardShortcuts.clear();
 for(int i=0;i<binding.catalog.count;i++){
  const auto& key=binding.catalog.keys[i];uint64_t bit=0;
  if(!oemKeyCaptureImplemented(g_machineIdentity.family,model,key.keyId)||!nativeKeyboardShortcutMaskBit(key.keyId,bit))continue;
  ipc["shortcut.recording.set"]({{"active",false}});ipc["shortcut.recording.set"]({{"active",true}});gamepadRecordShortcutChord(0);events=json::array();displayMask=0;
  const auto token=oemShortcutEdgeToken(g_oemShortcutEdgeEpoch.load(),g_gpdBackIfaceGen.load());const auto evaluated=shortcutEvaluations;
  nativeKeyboardShortcutHandleOemEdge(WPARAM(bit),LPARAM(token));nativeKeyboardShortcutHandleOemEdge(0,LPARAM(token));
  bool captured=false,released=false;for(const auto& event:events)if(event["name"]=="shortcut.recording.gamepad")for(const auto& input:event["data"]["inputs"])if(input["source"]=="oem"&&input["code"]==key.keyId){captured=true;if(event["data"]["released"]==true)released=true;}
  check(captured&&released,"AYA posted short chord reaches recorder with neutral live state "+model+" "+key.keyId);
  check(shortcutEvaluations==evaluated,"AYA recording owns the edge without actions "+model);
  ipc["shortcut.recording.set"]({{"active",false}});events=json::array();edgeLogs=json::array();
  const auto previewToken=oemShortcutEdgeToken(g_oemShortcutEdgeEpoch.load(),g_gpdBackIfaceGen.load());
  nativeKeyboardShortcutHandleOemEdge(WPARAM(bit),LPARAM(previewToken));nativeKeyboardShortcutHandleOemEdge(0,LPARAM(previewToken));
  check(events.size()==1&&events[0]["name"]=="oem.keys.observed"&&events[0]["data"]["familyId"]=="ayanero"&&events[0]["data"]["deviceClass"]==model&&events[0]["data"]["observedKeys"]==json::array({key.keyId}),"AYA short edge exact family/model preview "+model);
  if(!events.empty())check(events[0]["data"]["actionExecutionProven"]==false&&events[0]["data"]["actionStatus"]=="no-configured-rule-for-observed-keys","AYA observation never claims execution or invents rules "+model);
  if(!edgeLogs.empty())check(edgeLogs.back()["event"]=="aya-oem-input-edge"&&edgeLogs.back()["data"]["observedKeys"].empty(),"AYA release receipt is neutral "+model);
  events=json::array();const auto beforeStale=shortcutEvaluations;
  nativeKeyboardShortcutHandleOemEdge(WPARAM(bit),LPARAM(previewToken^(1ull<<32)));
  check(events.empty()&&shortcutEvaluations==beforeStale,"AYA stale epoch rejects preview/action "+model);
  g_oemKeyHooksActive=false;nativeKeyboardShortcutHandleOemEdge(WPARAM(bit),LPARAM(previewToken));check(events.empty(),"AYA stopped hook rejects queued key "+model);g_oemKeyHooksActive=true;
  for(const bool customOwner:{false,true}){
   ipc["shortcut.recording.set"]({{"active",true}});gamepadRecordShortcutChord(0);events=json::array();custom=customOwner;eligible=customOwner;
   const auto before=shortcutEvaluations;nativeKeyboardShortcutHandleOemEdge(WPARAM(bit),LPARAM(oemShortcutEdgeToken(g_oemShortcutEdgeEpoch.load(),g_gpdBackIfaceGen.load())));
   check(!g_shortcutRecordingActive&&shortcutEvaluations==before,"AYA focus/owner loss cancels recording without actions "+model);
   ipc["shortcut.recording.set"]({{"active",false}});eligible=true;custom=false;
  }
 }
}
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
wmi=true;ipc["shortcut.recording.set"]({{"active",true}});gamepadRecordShortcutChord(0);events=json::array();nativeKeyboardShortcutHandleOemEdge(WPARAM(kOemBitQs),LPARAM(oemShortcutEdgeToken(g_oemShortcutEdgeEpoch.load(),g_gpdBackIfaceGen.load())));check(events.empty(),"MSI authoritative WMI prevents duplicate keyboard recording");ipc["shortcut.recording.set"]({{"active",false}});
// Existing ordinary-path policy and source freshness must remain unchanged after recording.
physicalAuthority=0;g_machineIdentity.family=YmccFamily::MsiClaw;g_machineIdentity.deviceClass="HandheldCompanion.Devices.ClawA1M";g_oemBackProfileId=int(OemChordProfileId::Msi);NativeKeyboardShortcutRule rule;rule.needsNeutral=false;rule.requiredMask=kOemBitQs;rule.trigger="press";g_nativeKeyboardShortcuts={rule};nativeOemKeyboardConsumePublish();check(oemKeyboardConsumeEligibleMask()==kOemBitQs,"existing configured MSI QS mirror retains its ordinary owner");g_oemConsumePolicyEpoch=0;check(oemKeyboardConsumeEligibleMask()==0,"stale policy never consumes input");eligible=false;const auto denied=ipc["shortcut.recording.set"]({{"active",true}});check(denied["active"]==false&&!g_shortcutRecordingActive,"recording still requires foreground ownership to begin");
json report={{"modelCatalogs",modelCatalogs},{"pass",failures.empty()},{"checks",checks},{"failures",failures},{"models",${models.length}},{"deviceIo",false},{"serviceIo",false},{"actualProductionRecorderAndIpc",true},{"actualKeyboardSuppressionPolicy",true},{"actualQueuedKeyboardConsumer",true},{"actualVendorQueueDrain",true}};std::ofstream("RESULT.json")<<report.dump(2);std::cout<<"CHECKS "<<checks<<" FAILURES "<<failures.size()<<'\n';return failures.empty()?0:1;
}`.replace('${models.length}',String(models.length)));



if(recordingCase==='retained-mouse-guide'){
 const mainAt=code.indexOf('int main()');assert(mainAt>0);code=code.slice(0,mainAt)+String.raw`
static void posted(uint64_t mask){nativeKeyboardShortcutHandleOemEdge(WPARAM(mask),LPARAM(oemShortcutEdgeToken(g_oemShortcutEdgeEpoch.load(),g_gpdBackIfaceGen.load())));}
static bool recordedPair(const char* key){bool pressed=false,released=false;for(const auto& e:events)if(e["name"]=="shortcut.recording.gamepad")for(const auto& i:e["data"]["inputs"])if(i["source"]=="oem"&&i["code"]==key){pressed=pressed||!e["data"]["released"].get<bool>();released=released||e["data"]["released"].get<bool>();}return pressed&&released;}
static void beginAynRecording(){g_machineIdentity.family=YmccFamily::Ayn;g_machineIdentity.deviceClass="HandheldCompanion.Devices.AynLoki";g_oemBackProfileId=int(oemChordProfileIdFor(g_machineIdentity.family,g_machineIdentity.deviceClass));g_oemKeyHooksActive=true;g_exitRequested=false;physicalAuthority=dinputAuthority=0;wmi=false;eligible=true;custom=false;displayMask=0;g_curW=0;g_curPad={};g_nativeKeyboardShortcuts.clear();ipc["shortcut.recording.set"]({{"active",false}});ipc["shortcut.recording.set"]({{"active",true}});gamepadRecordShortcutChord(0);events=json::array();}
int main(){registerIpc();
 beginAynRecording();uint64_t guide=0;check(nativeKeyboardShortcutMaskBit("guide",guide)&&guide==kOemBitGuide&&oemKeyCaptureImplemented(g_machineIdentity.family,g_machineIdentity.deviceClass,"guide"),"Ayn Guide uses existing capability-backed canonical key");
 const auto catalog=ipc["oem.keys.get"](json::object());modelCatalogs[g_machineIdentity.deviceClass]=catalog;
 const auto before=shortcutEvaluations;posted(guide);posted(0);
 check(recordedPair("guide"),"Ayn complete posted mouse Guide pulse survives neutral live sampling during recording");
 check(shortcutEvaluations==before&&g_nativeKeyboardShortcuts.empty(),"Ayn recording cannot dispatch or create a saved action");
 check(g_shortcutRecordingActive&&displayMask==0,"Guide retained delivery does not fabricate a live held input");
 beginAynRecording();posted(kOemBitLcc);posted(0);check(recordedPair("lcc"),"Ayn declared keyboard LCC retains both posted edges under same family consumer");
 for(const auto reason:{"focus","owner","exit","hook","epoch"}){
  beginAynRecording();const auto count=shortcutEvaluations;
  if(std::strcmp(reason,"focus")==0)eligible=false;if(std::strcmp(reason,"owner")==0)custom=true;
  if(std::strcmp(reason,"exit")==0)g_exitRequested=true;if(std::strcmp(reason,"hook")==0)g_oemKeyHooksActive=false;
  const auto token=oemShortcutEdgeToken(g_oemShortcutEdgeEpoch.load(),g_gpdBackIfaceGen.load());
  nativeKeyboardShortcutHandleOemEdge(WPARAM(guide),LPARAM(std::strcmp(reason,"epoch")==0?token^(1ull<<32):token));
  check(!recordedPair("guide")&&shortcutEvaluations==count,std::string("Ayn queued Guide rejects revoked ")+reason);
  if(std::strcmp(reason,"focus")==0||std::strcmp(reason,"owner")==0)check(!g_shortcutRecordingActive,std::string("Ayn recording releases temporary owner on ")+reason);
 }
 beginAynRecording();posted(guide);eligible=false;posted(0);check(!g_shortcutRecordingActive,"Ayn focus loss after admitted press stops recording");check(recordedPair("guide"),"Ayn focus loss flushes admitted Guide before stopped");
 beginAynRecording();ipc["shortcut.recording.set"]({{"active",false}});const auto count=shortcutEvaluations;posted(guide);posted(0);check(shortcutEvaluations==count+2,"Ayn ordinary configured-action route still evaluates posted press and release");
 beginAynRecording();ipc["shortcut.recording.set"]({{"active",false}});g_machineIdentity.family=YmccFamily::Ayanero;g_machineIdentity.deviceClass="HandheldCompanion.Devices.AYANEOKUN";g_oemBackProfileId=int(oemChordProfileIdFor(g_machineIdentity.family,g_machineIdentity.deviceClass));ipc["shortcut.recording.set"]({{"active",true}});gamepadRecordShortcutChord(0);events=json::array();posted(guide);posted(0);check(recordedPair("guide"),"Existing AYA KUN canonical Guide recording remains retained");ipc["shortcut.recording.set"]({{"active",false}});
 json report={{"pass",failures.empty()},{"checks",checks},{"failures",failures},{"selection","retained-mouse-guide"},{"modelCatalogs",modelCatalogs},{"actualPostedConsumerAndRecorder",true},{"hookOrMouseSuppressionExecuted",false},{"virtualOutput","disabled"},{"hardwareIo",false},{"scope","retained posted OEM recording and cancellation; existing hook suppression not revalidated"}};std::ofstream("RESULT.json")<<report.dump(2);std::cout<<"CHECKS "<<checks<<" FAILURES "<<failures.size()<<'\n';return failures.empty()?0:1;
}
`;
}


if(recordingCase==='retained-family-edges'){
 const mainAt=code.indexOf('int main()');assert(mainAt>0);code=code.slice(0,mainAt)+String.raw`
static void posted(uint64_t mask){nativeKeyboardShortcutHandleOemEdge(WPARAM(mask),LPARAM(oemShortcutEdgeToken(g_oemShortcutEdgeEpoch.load(),g_gpdBackIfaceGen.load())));}
static bool recordedPair(const char* key){bool down=false,up=false;for(const auto& e:events)if(e["name"]=="shortcut.recording.gamepad")for(const auto& i:e["data"]["inputs"])if(i["source"]=="oem"&&i["code"]==key){down=down||!e["data"]["released"].get<bool>();up=up||e["data"]["released"].get<bool>();}return down&&up;}
static void beginRecording(YmccFamily family,const char* cls){g_machineIdentity.family=family;g_machineIdentity.deviceClass=cls;g_oemBackProfileId=int(oemChordProfileIdFor(family,cls));g_oemKeyHooksActive=true;g_exitRequested=false;physicalAuthority=dinputAuthority=0;wmi=false;eligible=true;custom=false;displayMask=0;g_curW=0;g_curPad={};g_nativeKeyboardShortcuts.clear();ipc["shortcut.recording.set"]({{"active",false}});ipc["shortcut.recording.set"]({{"active",true}});gamepadRecordShortcutChord(0);events=json::array();}
int main(){registerIpc();unsigned keyCount=0;json coverage=json::array();
 struct Model{YmccFamily family;const char* cls;};
 for(const auto model:{Model{YmccFamily::ZotacGamingZone,"HandheldCompanion.Devices.GamingZone"},Model{YmccFamily::SuiPlay,"HandheldCompanion.Devices.SuiPlay0X1"}}){
  beginRecording(model.family,model.cls);const auto& binding=oemUnifiedInputBindingFor(model.family,model.cls);check(binding.catalog.count==5,std::string("source-backed family keeps exact five OEM operations ")+model.cls);check(binding.profileId==g_oemBackProfileId.load(),std::string("existing exact family chord profile selected ")+model.cls);
  modelCatalogs[model.cls]=ipc["oem.keys.get"](json::object());uint64_t first=0;
  for(int i=0;i<binding.catalog.count;++i){const auto& key=binding.catalog.keys[i];uint64_t bit=0;const std::string label=std::string(model.cls)+" "+key.keyId;
   beginRecording(model.family,model.cls);check(nativeKeyboardShortcutMaskBit(key.keyId,bit)&&oemKeyCaptureImplemented(model.family,model.cls,key.keyId),"recording uses declared canonical source "+label);if(!first)first=bit;++keyCount;
   const auto count=shortcutEvaluations;posted(bit);posted(0);check(recordedPair(key.keyId),"complete posted keyboard pulse retained without live sample "+label);check(shortcutEvaluations==count&&g_nativeKeyboardShortcuts.empty()&&displayMask==0,"recording owns input without action or fabricated held state "+label);
  }
  for(const auto reason:{"focus","owner","exit","hook","epoch"}){
   beginRecording(model.family,model.cls);const auto count=shortcutEvaluations;
   if(std::strcmp(reason,"focus")==0)eligible=false;if(std::strcmp(reason,"owner")==0)custom=true;if(std::strcmp(reason,"exit")==0)g_exitRequested=true;if(std::strcmp(reason,"hook")==0)g_oemKeyHooksActive=false;
   const auto token=oemShortcutEdgeToken(g_oemShortcutEdgeEpoch.load(),g_gpdBackIfaceGen.load());nativeKeyboardShortcutHandleOemEdge(WPARAM(first),LPARAM(std::strcmp(reason,"epoch")==0?token^(1ull<<32):token));
   bool captured=false;for(const auto& e:events)if(e["name"]=="shortcut.recording.gamepad")captured=true;
   check(!captured&&shortcutEvaluations==count,std::string("retired posted source rejects ")+reason+" "+model.cls);
   if(std::strcmp(reason,"focus")==0||std::strcmp(reason,"owner")==0)check(!g_shortcutRecordingActive,std::string("temporary recorder owner stops on ")+reason+" "+model.cls);
  }
  beginRecording(model.family,model.cls);posted(first);eligible=false;posted(0);check(!g_shortcutRecordingActive,std::string("focus loss after press stops recorder ")+model.cls);check(recordedPair(binding.catalog.keys[0].keyId),std::string("focus loss flushes admitted pulse before stop ")+model.cls);
  beginRecording(model.family,model.cls);ipc["shortcut.recording.set"]({{"active",false}});const auto count=shortcutEvaluations;posted(first);posted(0);check(shortcutEvaluations==count+2,std::string("ordinary nonrecording action evaluation retained ")+model.cls);
  coverage.push_back({{"deviceClass",model.cls},{"operationEntries",binding.catalog.count},{"profileId",binding.profileId}});
 }
 // Distinct MSI/ROG producer/recording authority is deliberately not broadened.
 for(const auto model:{Model{YmccFamily::MsiClaw,"HandheldCompanion.Devices.ClawA1M"},Model{YmccFamily::AsusRogAllyClassic,"HandheldCompanion.Devices.ROGAlly"}}){
  beginRecording(model.family,model.cls);if(model.family==YmccFamily::MsiClaw)dinputAuthority=kOemBitM1;else g_oemRawPathActive=true;const auto count=shortcutEvaluations;posted(kOemBitM1);posted(0);check(events.empty()&&shortcutEvaluations==count,std::string("unrelated MSI DInput or ROG Raw authority remains independent ")+model.cls);dinputAuthority=0;g_oemRawPathActive=false;
 }
 json result={{"pass",failures.empty()},{"checks",checks},{"failures",failures},{"selection","retained-family-edges"},{"coverage",coverage},{"operationEntries",keyCount},{"modelCatalogs",modelCatalogs},{"actualPostedConsumerAndRecorder",true},{"hookSuppressionExecuted",false},{"virtualOutput","disabled"},{"hardwareIo",false}};std::ofstream("RESULT.json")<<result.dump(2);std::cout<<"CHECKS "<<checks<<" FAILURES "<<failures.size()<<'\n';return failures.empty()?0:1;
}
`;
}


if(recordingCase==='retained-fallback-recording'||recordingCase==='retained-fallback-cancel'){
 const mainAt=code.indexOf('int main()');assert(mainAt>0);code=code.slice(0,mainAt)+String.raw`
static void posted(uint64_t mask){nativeKeyboardShortcutHandleOemEdge(WPARAM(mask),LPARAM(oemShortcutEdgeToken(g_oemShortcutEdgeEpoch.load(),g_gpdBackIfaceGen.load())));}
static bool recordedPair(const char* key){bool down=false,up=false;for(const auto& e:events)if(e["name"]=="shortcut.recording.gamepad")for(const auto& i:e["data"]["inputs"])if(i["source"]=="oem"&&i["code"]==key){down=down||!e["data"]["released"].get<bool>();up=up||e["data"]["released"].get<bool>();}return down&&up;}
static void beginRecording(bool rog,bool neutral=true){g_machineIdentity.family=rog?YmccFamily::AsusRogAllyClassic:YmccFamily::MsiClaw;g_machineIdentity.deviceClass=rog?"HandheldCompanion.Devices.ROGAlly":"HandheldCompanion.Devices.ClawA1M";g_machineIdentity.resolved=true;g_oemBackProfileId=int(oemChordProfileIdFor(g_machineIdentity.family,g_machineIdentity.deviceClass));g_oemRawPathActive=false;g_oemKeyHooksActive=true;g_exitRequested=false;physicalAuthority=dinputAuthority=0;wmi=false;eligible=true;custom=false;displayMask=0;g_curW=0;g_curPad={};g_nativeKeyboardShortcuts.clear();ipc["shortcut.recording.set"]({{"active",false}});ipc["shortcut.recording.set"]({{"active",true}});if(neutral)gamepadRecordShortcutChord(0);events=json::array();}
int main(){registerIpc();json coverage=json::array();
 ${recordingCase==='retained-fallback-cancel'?String.raw`
 for(bool rog:{false,true})for(const auto reason:{"focus","owner"}){
  beginRecording(rog);const std::string label=rog?"ROG validated LL fallback":"MSI keyboard fallback";const auto count=shortcutEvaluations;if(std::strcmp(reason,"focus")==0)eligible=false;else custom=true;
  posted(kOemBitM1);bool captured=false,stopped=false;for(const auto& e:events){if(e["name"]=="shortcut.recording.gamepad")captured=true;if(e["name"]=="shortcut.recording.stopped"&&e["data"]["reason"]==(std::strcmp(reason,"focus")==0?"focus-lost":"owner-changed"))stopped=true;}
  check(!captured&&shortcutEvaluations==count&&!g_shortcutRecordingActive&&stopped,label+" rejects revoked "+reason);
 }
 json cancelResult={{"pass",failures.empty()},{"checks",checks},{"failures",failures},{"selection","retained-fallback-cancel"},{"scope","only4 original failed focus/owner guards; same actual candidate product"},{"hookSuppressionExecuted",false},{"virtualOutput","disabled"}};std::ofstream("RESULT.json")<<cancelResult.dump(2);std::cout<<"CHECKS "<<checks<<" FAILURES "<<failures.size()<<'\n';return failures.empty()?0:1;
 `:''}
 for(bool rog:{false,true}){
  beginRecording(rog);const std::string label=rog?"ROG validated LL fallback":"MSI keyboard fallback";
  const std::vector<std::pair<const char*,uint64_t>> keys=rog?std::vector<std::pair<const char*,uint64_t>>{{"m1",kOemBitM1},{"m2",kOemBitM2}}:std::vector<std::pair<const char*,uint64_t>>{{"m1",kOemBitM1},{"m2",kOemBitM2},{"qs",kOemBitQs}};
  for(const auto& key:keys){beginRecording(rog);check(oemKeyCaptureImplemented(g_machineIdentity.family,g_machineIdentity.deviceClass,key.first),label+" retains existing key "+key.first);const auto count=shortcutEvaluations;posted(key.second);posted(0);check(recordedPair(key.first),label+" short posted press/release survives neutral live state "+key.first);check(shortcutEvaluations==count&&g_nativeKeyboardShortcuts.empty(),label+" recording never dispatches or creates actions "+key.first);}
  for(const auto reason:{"focus","owner","exit","hook","epoch"}){
   beginRecording(rog);const auto count=shortcutEvaluations;if(std::strcmp(reason,"focus")==0)eligible=false;if(std::strcmp(reason,"owner")==0)custom=true;if(std::strcmp(reason,"exit")==0)g_exitRequested=true;if(std::strcmp(reason,"hook")==0)g_oemKeyHooksActive=false;
   const auto token=oemShortcutEdgeToken(g_oemShortcutEdgeEpoch.load(),g_gpdBackIfaceGen.load());nativeKeyboardShortcutHandleOemEdge(WPARAM(kOemBitM1),LPARAM(std::strcmp(reason,"epoch")==0?token^(1ull<<32):token));bool captured=false;for(const auto& e:events)if(e["name"]=="shortcut.recording.gamepad")captured=true;check(!captured&&shortcutEvaluations==count,label+" rejects revoked "+reason);if(std::strcmp(reason,"focus")==0||std::strcmp(reason,"owner")==0)check(!g_shortcutRecordingActive,label+" releases recording owner on "+reason);
  }
  beginRecording(rog,false);posted(kOemBitM1);posted(0);check(!recordedPair("m1"),label+" initial held source must reach real neutral");posted(kOemBitM1);posted(0);check(recordedPair("m1"),label+" real new press after neutral is recorded");
  beginRecording(rog);posted(kOemBitM1);eligible=false;posted(0);check(!g_shortcutRecordingActive&&recordedPair("m1"),label+" focus loss flushes admitted press before stopped");
  beginRecording(rog);physicalAuthority=kOemBitM1;posted(kOemBitM1);posted(0);check(events.empty(),label+" other physical producer authority rejects duplicate keyboard source");
  beginRecording(rog);ipc["shortcut.recording.set"]({{"active",false}});const auto count=shortcutEvaluations;posted(kOemBitM1);posted(0);check(shortcutEvaluations==count+2,label+" ordinary action evaluation remains unchanged");coverage.push_back({{"route",label},{"canonicalKeys",keys.size()}});
 }
 for(const auto guard:{"raw-active","identity-unknown","profile-mismatch"}){beginRecording(true);if(std::strcmp(guard,"raw-active")==0)g_oemRawPathActive=true;if(std::strcmp(guard,"identity-unknown")==0)g_machineIdentity.resolved=false;if(std::strcmp(guard,"profile-mismatch")==0)g_oemBackProfileId=int(OemChordProfileId::Msi);posted(kOemBitM1);posted(0);check(events.empty(),std::string("ROG LL recorder cannot borrow ")+guard);}
 beginRecording(false);dinputAuthority=kOemBitM1|kOemBitM2;posted(kOemBitM1|kOemBitM2);posted(0);check(events.empty(),"MSI DInput authoritative rear source rejects keyboard mirror");
 beginRecording(false);wmi=true;posted(kOemBitQs|kOemBitClaw);posted(0);check(events.empty(),"MSI authoritative WMI prevents duplicate keyboard recording");
 beginRecording(false);dinputAuthority=kOemBitM1|kOemBitM2;posted(kOemBitQs);posted(0);check(recordedPair("qs"),"MSI rear DInput authority does not block independent QS keyboard fallback");
 beginRecording(false);wmi=true;posted(kOemBitM1);posted(0);check(recordedPair("m1"),"MSI front WMI authority does not block independent rear keyboard fallback");
 // Same two authority guards as the existing Zotac/SuiPlay selector, no rerun of its56 checks.
 for(bool rog:{false,true}){beginRecording(rog);if(!rog)dinputAuthority=kOemBitM1;else g_oemRawPathActive=true;const auto count=shortcutEvaluations;posted(kOemBitM1);posted(0);check(events.empty()&&shortcutEvaluations==count,std::string("unrelated MSI DInput or ROG Raw authority remains independent ")+g_machineIdentity.deviceClass);dinputAuthority=0;g_oemRawPathActive=false;}

 for(const auto authority:{"msi-dinput","msi-wmi","physical-producer","rog-raw"}){
  const bool rog=std::strcmp(authority,"rog-raw")==0;beginRecording(rog);const auto token=oemShortcutEdgeToken(g_oemShortcutEdgeEpoch.load(),g_gpdBackIfaceGen.load());const auto bit=std::strcmp(authority,"msi-wmi")==0?kOemBitQs:kOemBitM1;
  if(std::strcmp(authority,"msi-dinput")==0)dinputAuthority=bit;if(std::strcmp(authority,"msi-wmi")==0)wmi=true;if(std::strcmp(authority,"physical-producer")==0)physicalAuthority=bit;if(rog)g_oemRawPathActive=true;
  nativeKeyboardShortcutHandleOemEdge(WPARAM(bit),LPARAM(token));check(events.empty(),std::string("already-posted fallback message cannot survive new authority ")+authority);
 }
 for(bool rog:{false,true}){beginRecording(rog);const auto token=oemShortcutEdgeToken(g_oemShortcutEdgeEpoch.load(),g_gpdBackIfaceGen.load());g_oemShortcutEdgeEpoch.fetch_add(1);nativeKeyboardShortcutHandleOemEdge(WPARAM(kOemBitM1),LPARAM(token));check(events.empty(),std::string("already-posted fallback message cannot survive real rule epoch change ")+(rog?"ROG":"MSI"));}
 json result={{"pass",failures.empty()},{"checks",checks},{"failures",failures},{"selection","retained-fallback-recording"},{"coverage",coverage},{"actualPostedConsumerAndRecorder",true},{"actualRogFallbackPredicate",true},{"msiAuthorityBoundary","controlled existing WMI/DInput/producer observations, not actual device/provider execution"},{"hookSuppressionExecuted",false},{"hardwareIo",false},{"virtualOutput","disabled"}};std::ofstream("RESULT.json")<<result.dump(2);std::cout<<"CHECKS "<<checks<<" FAILURES "<<failures.size()<<'\n';return failures.empty()?0:1;
}
`;
}

if(recordingCase==='definitions'){
 const end=code.indexOf('int main(){registerIpc();');assert(end>0);code=code.slice(0,end)+String.raw`
int main(){registerIpc();
 const std::vector<std::pair<YmccFamily,std::string>> cases={
 {YmccFamily::Gpd,"GPDWin5"},{YmccFamily::AsusRogAlly,"XboxROGAlly"},
 {YmccFamily::MsiClaw,"ClawG3E"},{YmccFamily::Ayanero,"AYANEO3"},
 {YmccFamily::Ayanero,"AYANEONEXT2"},{YmccFamily::OneXPlayer,"OneXPlayer3"},
 {YmccFamily::OneXPlayer,"OneXPlayerX2MiniPro"},{YmccFamily::Aokzoe,"AOKZOEA1"},
 {YmccFamily::ValveSteamDeck,"SteamDeck"},{YmccFamily::Unknown,"unknown"}};
 for(const auto& entry:cases){g_machineIdentity.family=entry.first;g_machineIdentity.deviceClass=entry.second=="unknown"?entry.second:"HandheldCompanion.Devices."+entry.second;
  const auto& binding=oemUnifiedInputBindingFor(g_machineIdentity.family,g_machineIdentity.deviceClass);
  const auto result=ipc["oem.keys.get"](json::object());modelCatalogs[g_machineIdentity.deviceClass]=result;
  check(result["keys"].size()==size_t(binding.catalog.count),"definition query preserves exact catalog "+entry.second);
  for(const auto& key:result["keys"]){check(key.contains("keyboardContributions")&&key["keyboardContributions"].is_array(),"every real key has a contribution list "+entry.second+key["keyId"].get<std::string>());
   for(const auto& rule:key["keyboardContributions"]){check(rule["scope"]=="profile-keyboard-contribution"&&rule["bindingEquivalenceProven"]==false,"projection never grants equivalence");check(rule["chordMatch"]=="subset"&&rule["configuredMaskMatch"]=="exact"&&rule["modifierSides"]=="folded","distinct match layers retained");}
  }
 }
 auto key=[&](const char* cls,const char* id){for(const auto& k:modelCatalogs[std::string("HandheldCompanion.Devices.")+cls]["keys"])if(k["keyId"]==id)return k;return json();};
 auto contribution=[&](const char* cls,const char* id){return key(cls,id).at("keyboardContributions");};
 check(contribution("GPDWin5","gamepad")[0]["keyGroups"]==json::array({json::array({"F13"})}),"Win5 F13 contributes Gamepad without renaming raw input");
 check(contribution("GPDWin5","gamepad")[0]["requiresBoundInterface"]==false,"Gamepad does not borrow rear interface gate");
 check(contribution("GPDWin5","l4")[0]["requiresBoundInterface"]==true&&contribution("GPDWin5","r4")[0]["requiresBoundInterface"]==true,"Win5 F14 and F15 retain their interface requirement");
 const auto home=contribution("GPDWin5","home")[0]["keyGroups"];
 check(home==json::array({json::array({"MetaLeft","MetaRight"}),json::array({"KeyD"})}),"Win5 Home keeps folded Win plus D instead of aliasing Gamepad");
 check(contribution("GPDWin5","keyboard")[0]["keyGroups"]==json::array({json::array({"ControlLeft","ControlRight"}),json::array({"MetaLeft","MetaRight"}),json::array({"KeyO"})}),"Win5 Keyboard retains Ctrl Win O");
 check(key("ClawG3E","claw")["triggerCapability"]=="click-only"&&key("ClawG3E","qs")["triggerCapability"]=="click-only","MSI click-only policy unchanged by keyboard metadata");
 check(contribution("OneXPlayer3","gkey").empty(),"OP3 vendor G-Key receives no invented keyboard contribution");
 check(contribution("OneXPlayerX2MiniPro","l4")[0]["keyGroups"]==json::array({json::array({"F15"})})&&contribution("OneXPlayerX2MiniPro","l4")[0]["requiresBoundInterface"]==false,"MiniPro F15 does not inherit Win5 profile gate");
 check(contribution("AYANEO3","cksmall")[0]["keyGroups"]==json::array({json::array({"F24"})}),"AYA3 Small keeps its own F24 definition");
 check(nativeOemKeyboardContributions(-1,"gamepad").empty()&&nativeOemKeyboardContributions(int(OemChordProfileId::GpdWin5),"unknown").empty(),"unknown profile and key grant no mapping");
 check(contribution("AOKZOEA1","home")[0]["exclusiveGroup"]!=0,"AOKZOE chord exclusivity remains explicit");
 check(!contribution("AOKZOEA1","home")[0]["earlierGroupRules"].empty(),"AOKZOE Home retains earlier competing group expressions");
 check(contribution("GPDWin5","gamepad")[0]["outputMask"]==std::to_string(kOemBitGamepad),"canonical output mask is lossless text");
 check(contribution("GPDWin5","gamepad")[0]["earlierGroupRules"].empty(),"independent Gamepad rule has no invented competitors");
 check(events.empty()&&g_nativeKeyboardShortcuts.empty()&&!g_shortcutRecordingActive,"definition query does not record or create an action");
 json report={{"pass",failures.empty()},{"checks",checks},{"failures",failures},{"modelCatalogs",modelCatalogs},{"selection","definitions"},{"hardwareIo",false},{"virtualOutput","disabled"},{"actualProductionProjectionAndCatalogIpc",true},{"physicalIdentityProven",false}};
 std::ofstream("RESULT.json")<<report.dump(2);std::cout<<"CHECKS "<<checks<<" FAILURES "<<failures.size()<<'\n';return failures.empty()?0:1;
}
`;
}

// Optional bounded continuation of this same producer: exercise the actual
// evaluator/dispatcher/serial guards and keyboard worker, with OS IO in memory.
// Rules are supplied at the native consumer boundary; UI saving/parsing and
// Windows/Steam receipt are deliberately not inferred from these assertions.
if(actionCases){
 const actionFamily=actionCases==='gpd-actions'||nativeOemActions?'Gpd':actionCases.startsWith('onex-')?'OneXPlayer':actionCases==='aokzoe-actions'?'Aokzoe':'Ayanero';
 const actionModels=models.filter(([family])=>family===actionFamily).map(([,name])=>'HandheldCompanion.Devices.'+name);
 assert(actionModels.length>0,'Empty selected OEM action model set');
 if(actionCases==='aokzoe-actions')assert.deepEqual([...actionModels].sort(),['AOKZOEA1','AOKZOEA1Pro','AOKZOEA1X','AOKZOEA2'].map(name=>'HandheldCompanion.Devices.'+name).sort(),'AOKZOE selection must contain exactly the four reviewed concrete models, excluding OneXAOKZOE base class');
 code=code.replace('#include <cstring>','#include <cstring>\n#include <mutex>\n#include <condition_variable>\n#include <thread>\n#include <chrono>\n#include "json.hpp"\n#include "shortcut_action_protocol.h"\n#define YMCC_SHORTCUT_FIXTURE');
 const oldEvaluator='static void nativeKeyboardShortcutsEvaluate(WORD,const XINPUT_GAMEPAD&,ULONGLONG,bool,const uint64_t*,bool,uint64_t=0,const OemProducerTokens* = nullptr'+desktopFunctionParameter+'){++shortcutEvaluations;}';
 assert(code.includes(oldEvaluator),'existing evaluator port');
 const nativeStartupDefinitions=nativeOemActions?String.raw`
static void check(bool,const std::string&);
static std::atomic<bool> g_inputCaptureReady{false},g_oemInputCaptureOnly{false},g_oemInputCaptureInitRequested{false},g_inputCaptureStop{false};
enum class PowerLifecycle {Ready,Suspending};
static std::atomic<PowerLifecycle> g_powerLifecycle{PowerLifecycle::Ready};
static std::atomic<bool> g_inputReady{true};
static struct {bool virtualTargetEnabled=false,motionEnabled=false,standaloneTestLane=false;} g_realStickTest;
static unsigned nativeFullInits=0,nativeConfigLoads=0,nativeColdReturns=0,nativeCrashLoads=0;
static bool nativeAssets=false,nativeConfiguredVirtual=false,nativeConfiguredMotion=false;
static std::string nativeInitFailure;
static void inputCaptureLoadRealStickTestConfig(){check(g_oemInputCaptureOnly,"OEM-only admission precedes startup settings load");++nativeConfigLoads;g_realStickTest.virtualTargetEnabled=nativeConfiguredVirtual;g_realStickTest.motionEnabled=nativeConfiguredMotion;}
static void msiCrashRecoveryInitialize(){++nativeCrashLoads;}
static void inputHostTargetReturnQueueColdStartup(){++nativeColdReturns;}
static void inputCaptureInit(){check(g_machineIdentity.family==YmccFamily::ValveSteamDeck||g_oemInputCaptureOnly,"known OEM startup closes Host admission before optional init");++nativeFullInits;if(!nativeAssets)return;if(!nativeInitFailure.empty())throw std::runtime_error(nativeInitFailure);g_inputCaptureReady=true;}
${fn('inputCaptureOemConsumersAvailable')}
${nativeOemControl?'static void inputCaptureInitializeConsumers(bool){inputCaptureInit();}':fn('inputCaptureInitializeConsumers')}
${fn('inputCaptureReconcileRequestedInitialization')}
static void resetNativeStartup(){g_inputCaptureReady=false;g_oemInputCaptureOnly=false;g_oemInputCaptureInitRequested=false;g_inputCaptureStop=false;g_exitRequested=false;g_inputReady=true;g_powerLifecycle=PowerLifecycle::Ready;g_realStickTest={};nativeFullInits=nativeConfigLoads=nativeColdReturns=nativeCrashLoads=0;nativeAssets=false;nativeConfiguredVirtual=nativeConfiguredMotion=false;nativeInitFailure.clear();}
static void testNativeStartupConsumers(){
 struct Model {YmccFamily family;const char* cls;};
 const Model models[]={{YmccFamily::MsiClaw,"ClawCG3EM"},{YmccFamily::AsusRogAllyClassic,"ROGAlly"},{YmccFamily::Gpd,"GPDWin5"},{YmccFamily::OneXPlayer,"OneXPlayer3"},{YmccFamily::Aokzoe,"AOKZOEA1X"},{YmccFamily::Ayanero,"AYANEO2"}};
 for(const auto& model:models){
  resetNativeStartup();g_machineIdentity.family=model.family;g_machineIdentity.deviceClass=std::string("HandheldCompanion.Devices.")+model.cls;
  check(inputCaptureOemConsumersAvailable(),"current total-library source is consumable "+g_machineIdentity.deviceClass);
  nativeInitFailure="sensor STA unavailable or quarantined";
  inputCaptureInitializeConsumers(true);
  check(g_oemInputCaptureOnly&&!g_inputCaptureReady&&nativeFullInits==0,"native cold startup avoids optional assets and sensor init "+g_machineIdentity.deviceClass);
  check(nativeConfigLoads==1&&nativeColdReturns==1&&nativeCrashLoads==1,"native cold startup preserves config and prior owned returns "+g_machineIdentity.deviceClass);
  ${nativeOemControl?'return;':''}
  for(int i=0;i<3;++i)inputCaptureReconcileRequestedInitialization();
  check(nativeFullInits==0,"idle ticks do not promote or retry optional initialization");
  g_realStickTest.virtualTargetEnabled=true;g_oemInputCaptureInitRequested=true;
  g_powerLifecycle=PowerLifecycle::Suspending;inputCaptureReconcileRequestedInitialization();
  check(nativeFullInits==0&&g_oemInputCaptureInitRequested,"closed shared power retains explicit request and never initializes");
  g_powerLifecycle=PowerLifecycle::Ready;g_inputReady=false;inputCaptureReconcileRequestedInitialization();
  check(nativeFullInits==0&&g_oemInputCaptureInitRequested,"input-not-ready retains explicit request");
  g_inputReady=true;inputCaptureReconcileRequestedInitialization();
  check(nativeFullInits==1&&g_oemInputCaptureOnly&&!g_inputCaptureReady,"explicit target request with missing assets keeps OEM-only and no ready output");
  nativeAssets=true;g_oemInputCaptureInitRequested=true;inputCaptureReconcileRequestedInitialization();
  check(nativeFullInits==2&&g_oemInputCaptureOnly&&!g_inputCaptureReady,"known optional sensor failure keeps OEM consumer owner");
  inputCaptureReconcileRequestedInitialization();check(nativeFullInits==2,"failed optional initialization has no idle retry");
  nativeInitFailure.clear();g_oemInputCaptureInitRequested=true;inputCaptureReconcileRequestedInitialization();
  check(nativeFullInits==3&&g_inputCaptureReady&&!g_oemInputCaptureOnly,"explicit successful output initialization returns to existing full consumer");
  g_realStickTest.virtualTargetEnabled=false;inputCaptureReconcileRequestedInitialization();
  check(g_inputCaptureReady&&!g_oemInputCaptureOnly&&nativeFullInits==3,"ON to native OFF retains successful collection instead of tearing down OEM owner");
  g_inputCaptureReady=false;g_oemInputCaptureOnly=true;g_realStickTest.virtualTargetEnabled=true;g_oemInputCaptureInitRequested=true;g_inputCaptureStop=true;
  inputCaptureReconcileRequestedInitialization();check(nativeFullInits==3,"stop cannot be revived by queued promotion");
  g_inputCaptureStop=false;g_exitRequested=true;g_oemInputCaptureInitRequested=true;inputCaptureReconcileRequestedInitialization();check(nativeFullInits==3,"exit cannot be revived by queued promotion");g_exitRequested=false;
  nativeInitFailure="unrelated owner failure";bool threw=false;try{inputCaptureInitializeConsumers(false);}catch(const std::runtime_error&){threw=true;}check(threw,"unrelated initialization failures retain original failure boundary");
 }
 resetNativeStartup();g_machineIdentity.family=YmccFamily::ValveSteamDeck;g_machineIdentity.deviceClass="HandheldCompanion.Devices.SteamDeck";
 check(!inputCaptureOemConsumersAvailable(),"catalog-only embedded keys cannot invent an OEM source");inputCaptureInitializeConsumers(true);check(!g_oemInputCaptureOnly&&nativeFullInits==1,"unimplemented input retains original full initialization path");
 resetNativeStartup();g_machineIdentity.family=YmccFamily::Gpd;g_machineIdentity.deviceClass="HandheldCompanion.Devices.GPDWin5";inputCaptureInitializeConsumers(true);
}
`:'';
 const actualActionDefinitions=nativeStartupDefinitions+String.raw`
static const std::vector<std::string> selectedActionModels=${'{'+actionModels.map(name=>JSON.stringify(name)).join(',')+'}'};
static constexpr YmccFamily selectedActionFamily=YmccFamily::${actionFamily};
static std::array<std::atomic<uint64_t>,kOemProducerCount>g_oemProducerGenerations{},g_oemProducerAuthorities{};
static bool oemProducerContextCurrent(OemProducerId){return true;}
${fn('oemProducerTokensFor')}
${fn('oemProducerTokensCurrent')}
static bool g_nativeWmiAuthoritySeen=false;
static uint32_t g_nativeOemEdgeEpochSeen=0,g_nativeShortcutLegacyOverrideMask=0;
static bool g_nativeShortcutInventoryDirty=true;
static std::atomic<uint32_t>g_oemShortcutEdgePostFailures{0};
static void nativeOemKeyboardConsumeMaintain(ULONGLONG){}
static void nativeMsiWmiShortcutDrain(){}
static void nativeOemProducerDrain();
${nativeOemActions?'':'static std::atomic<bool>g_inputReady{true};'}
static std::atomic<bool>fixtureRadialOwner{false},fixturePowerReady{true};
static std::atomic<uint64_t>fixturePower{1},fixtureWindow{42};
static uint64_t currentPowerGeneration(){return fixturePower.load();}
${hasDesktopFunction?'static bool sharedPowerLifecycleCurrent(const ymcc::sleeppower::SharedPowerV1& value){return fixturePowerReady.load()&&value.generation==fixturePower.load();}\nstatic bool captureSharedPowerLifecycle(ymcc::sleeppower::SharedPowerV1& value){value={};value.generation=fixturePower.load();value.flags=ymcc::sleeppower::kKnown|ymcc::sleeppower::kHardwareAllowed;return fixturePowerReady.load()&&value.generation!=0;}':''}
static bool sgVirtualWriterAllowedNow(){return fixturePowerReady.load();}
static bool radialHasInputOwner(){return fixtureRadialOwner.load();}
static bool msiWmiGenerationCurrent(uint64_t){return false;}
static json fixtureSettings={{"revision",1},{"virtualEnabled",false}};
static json ymSettingsSection(const char*){return fixtureSettings;}
static json ymSettingsFields(const char*,std::initializer_list<const char*>){return fixtureSettings;}
static uint64_t gameInputOwnerSession(){return 1;}
namespace ymcc{static json inputShortcutContext(const json&,uint64_t){return json::object();}}
static void nativeInputShortcutToggle(const std::string&,const std::string&,const json&){throw std::runtime_error("unselected input action");}
static bool nativeYmccShortcutEmit(const std::string&,int,uint64_t,uint64_t,uint64_t,bool,OemProducerTokens){throw std::runtime_error("unselected own action");}
static SHORT actionAsyncKeyState(int){return 0;}
static HWND actionForeground(){return reinterpret_cast<HWND>(uintptr_t(fixtureWindow.load()));}
#define GetAsyncKeyState actionAsyncKeyState
#define GetForegroundWindow actionForeground
namespace ymcc::keyboardEvents{enum class SourceId{NativeKeyboardShortcut};}
enum class SerialJobKind{YmccOwn,ExternalInjection};
static constexpr uint64_t kOemShortcutUnboundEpoch=~0ull;
static std::deque<std::function<void()>>actionJobs;
static bool fixtureQueueAccepts=true;
static bool gamepadSerialSubmit(std::function<void()>job,SerialJobKind){if(!fixtureQueueAccepts)return false;actionJobs.push_back(std::move(job));return true;}
static thread_local unsigned long long g_gamepadSerialActionPower=0;
${fn('gamepadSerialSubmitCurrentPower')}
${fn('nativeYmccShortcutSubmit')}
static void actionDrain(){while(!actionJobs.empty()){auto job=std::move(actionJobs.front());actionJobs.pop_front();job();}}
static std::mutex actionTraceMx;static std::condition_variable actionTraceCv;
static std::vector<std::pair<WORD,bool>>actionTrace;
static UINT keyboardSelfSendWithEvidence(ymcc::keyboardEvents::SourceId,UINT count,INPUT*inputs){
 std::lock_guard<std::mutex>lock(actionTraceMx);for(UINT i=0;i<count;++i)actionTrace.push_back({inputs[i].ki.wVk,(inputs[i].ki.dwFlags&KEYEVENTF_KEYUP)!=0});actionTraceCv.notify_all();return count;
}
${fn('nativeKeyboardShortcutInputFlags')}
#include "keyboard_shortcut_sequence_runtime.h"
#include "shortcut_action_runtime.h"
${fn('nativeKeyboardShortcutDispatch')}
${fn('nativeKeyboardShortcutsEvaluate')}
`;
 code=code.replace(oldEvaluator,actualActionDefinitions);
 const mainAt=code.indexOf('int main()');assert(mainAt>0);
 code=code.slice(0,mainAt)+String.raw`
static void clearTrace(){std::lock_guard<std::mutex>lock(actionTraceMx);actionTrace.clear();}
static size_t traceSize(){std::lock_guard<std::mutex>lock(actionTraceMx);return actionTrace.size();}
static void waitReleased(){std::unique_lock<std::mutex>lock(actionTraceMx);actionTraceCv.wait_for(lock,std::chrono::milliseconds(1000),[]{return actionTrace.size()>=2;});}
static void posted(uint64_t mask){nativeKeyboardShortcutHandleOemEdge(WPARAM(mask),LPARAM(oemShortcutEdgeToken(g_oemShortcutEdgeEpoch.load(),g_gpdBackIfaceGen.load())));}
int main(){registerIpc();nativeShortcutActionRegisterIpc();unsigned testedKeys=0;json coverage=json::array();
 ${nativeOemActions?'testNativeStartupConsumers();':''}
 ${actionCases==='native-oem-init'?String.raw`json result={{"pass",failures.empty()},{"checks",checks},{"failures",failures},{"scope","actual native-OEM init and reconciliation consumers only; callback/full worker separate"}};std::ofstream("RESULT.json")<<result.dump(2);std::cout<<"CHECKS "<<checks<<" FAILURES "<<failures.size()<<'\n';return failures.empty()?0:1;`:''}
 ${nativeOemControl?String.raw`json control={{"pass",failures.empty()},{"checks",checks},{"failures",failures},{"scope","prior unconditional full initialization control; no action cases run"}};std::ofstream("RESULT.json")<<control.dump(2);std::cout<<"CHECKS "<<checks<<" FAILURES "<<failures.size()<<'\n';return failures.empty()?0:1;`:''}
 physicalAuthority=dinputAuthority=0;wmi=false;eligible=true;custom=false;displayMask=0;
 for(const auto& model:selectedActionModels){
  g_machineIdentity.family=selectedActionFamily;g_machineIdentity.deviceClass=model;
  const auto& binding=oemUnifiedInputBindingFor(g_machineIdentity.family,model);g_oemBackProfileId=binding.profileId;
  const auto catalog=ipc["oem.keys.get"](json::object());modelCatalogs[model]=catalog;unsigned modelKeys=0;
  for(int i=0;i<binding.catalog.count;i++){
   const auto& key=binding.catalog.keys[i];uint64_t bit=0;
   if(!oemKeyCaptureImplemented(g_machineIdentity.family,model,key.keyId)||!nativeKeyboardShortcutMaskBit(key.keyId,bit))continue;
   ++testedKeys;++modelKeys;const auto label=model+" "+key.keyId;
   g_nativeKeyboardShortcuts.clear();events=json::array();posted(0);posted(bit);posted(0);
   check(actionJobs.empty()&&traceSize()==0,"unbound OEM input cannot invent an action "+label);
   bool noRule=false;for(const auto& event:events)if(event["name"]=="oem.keys.observed"&&event["data"]["actionStatus"]=="no-configured-rule-for-observed-keys")noRule=true;
   check(noRule,"unbound OEM receipt explains missing rule "+label);
   NativeKeyboardShortcutRule rule;rule.requiredMask=bit;rule.outputId=label;rule.virtualKey='K';
   const auto resolve=[](const std::string& action,const std::string& code,unsigned short& vk,bool& shift){vk='K';shift=false;return action=="keyboard.key"&&code=="KeyK";};
   check(ymcc::keyboardSequence::parse(json{{"key","KeyK"}},"keyboard.key",resolve,rule.keyboardPlan),"production sequence parser creates native keyboard plan "+label);
   g_nativeKeyboardShortcuts={rule};posted(0);events=json::array();posted(bit);posted(bit);posted(0);
   check(actionJobs.size()==1,"real evaluator queues exactly one OEM action per press "+label);
   actionDrain();waitReleased();
   {std::lock_guard<std::mutex>lock(actionTraceMx);check(actionTrace==std::vector<std::pair<WORD,bool>>{{'K',false},{'K',true}},"real keyboard worker calls one matching down/up without virtual output "+label);actionTrace.clear();}
   ${nativeOemActions?String.raw`
   nativeAssets=true;g_realStickTest.virtualTargetEnabled=true;g_oemInputCaptureOnly=true;g_inputCaptureReady=false;g_oemInputCaptureInitRequested=true;
   inputCaptureReconcileRequestedInitialization();check(g_inputCaptureReady&&!g_oemInputCaptureOnly,"native source promotes only for explicit virtual request "+label);
   g_realStickTest.virtualTargetEnabled=false;g_nativeKeyboardShortcuts={rule};posted(0);posted(bit);posted(0);actionDrain();waitReleased();
   {std::lock_guard<std::mutex>lock(actionTraceMx);check(actionTrace==std::vector<std::pair<WORD,bool>>{{'K',false},{'K',true}},"same bound rear/dedicated action executes after ON to native OFF "+label);actionTrace.clear();}
   `:''}
   // Exercise the production action mailbox and ACK consumer independently of
   // the page handler. An injected FAILED outcome must stay visibly failed.
   rule.keyboardPlan={};rule.uiAction="monitor.toggle";rule.uiParams={{"operation","toggle"}};g_nativeKeyboardShortcuts={rule};posted(0);events=json::array();posted(bit);posted(0);actionDrain();
   json request;for(const auto& event:events)if(event["name"]=="input.shortcutAction")request=event["data"];
   check(!request.is_null()&&request["actionId"]=="monitor.toggle","real native OEM UI action request carries configured action "+label);
   if(!request.is_null()){
    const auto id=request["id"].get<std::string>();const json outcome={{"state","failed"},{"title","fixture consumer"},{"message","deliberate failed leaf; no physical or UI success claim"}};
    check(ipc["input.shortcutAction.complete"]({{"id",id},{"outcome",outcome}})["ok"]==false,"unclaimed OEM UI result rejected "+label);
    check(ipc["input.shortcutAction.claim"]({{"id",id}})["ok"]==true,"actual action request can be claimed "+label);
    check(ipc["input.shortcutAction.complete"]({{"id",id},{"outcome",outcome}})["ok"]==true,"actual failed outcome consumed "+label);
    bool failed=false;for(const auto& event:events)if(event["name"]=="input.shortcutFeedback"&&event["data"]["state"]=="failed")failed=true;
    check(failed,"actual feedback preserves failure instead of claiming success "+label);
    check(ipc["input.shortcutAction.complete"]({{"id",id},{"outcome",outcome}})["ok"]==false,"duplicate OEM completion rejected "+label);
   }
   for(const auto cancel:{"epoch","recording","exit","radial","power","window","input-not-ready"}){
    rule.uiAction.clear();rule.uiParams=json::object();rule.keyboardPlan.steps={{{{'K',false}},80}};g_nativeKeyboardShortcuts={rule};posted(0);posted(bit);posted(0);
    check(actionJobs.size()==1,"OEM cancellation starts with an admitted real action "+label+" "+cancel);
    if(std::strcmp(cancel,"epoch")==0)g_oemShortcutEdgeEpoch.fetch_add(1);
    if(std::strcmp(cancel,"recording")==0)g_shortcutRecordingActive=true;
    if(std::strcmp(cancel,"exit")==0)g_exitRequested=true;
    if(std::strcmp(cancel,"radial")==0)fixtureRadialOwner=true;
    if(std::strcmp(cancel,"power")==0)fixturePower.fetch_add(1);
    if(std::strcmp(cancel,"window")==0)fixtureWindow.fetch_add(1);
    if(std::strcmp(cancel,"input-not-ready")==0)g_inputReady=false;
    actionDrain();check(traceSize()==0,"actual queued OEM action cancellation prevents output "+label+" "+cancel);
    g_shortcutRecordingActive=false;g_exitRequested=false;fixtureRadialOwner=false;g_inputReady=true;
   }
   fixtureQueueAccepts=false;g_nativeKeyboardShortcuts={rule};posted(0);posted(bit);posted(0);check(actionJobs.empty(),"full action queue cannot report execution "+label);fixtureQueueAccepts=true;
   g_nativeKeyboardShortcuts={rule};ipc["shortcut.recording.set"]({{"active",true}});gamepadRecordShortcutChord(0);posted(bit);posted(0);
   check(actionJobs.empty()&&traceSize()==0,"OEM recording still owns input without executing configured action "+label);
   if(selectedActionFamily==YmccFamily::Aokzoe||selectedActionFamily==YmccFamily::Gpd){
    bool captured=false,released=false;
    for(const auto& event:events)if(event["name"]=="shortcut.recording.gamepad")for(const auto& input:event["data"]["inputs"])if(input["source"]=="oem"&&input["code"]==key.keyId){captured=true;if(event["data"]["released"]==true)released=true;}
    check(captured&&released,"selected family short posted press/release is retained with neutral live state "+label);
    const char* event=selectedActionFamily==YmccFamily::Gpd?"gpd-oem-input-edge":"aokzoe-oem-input-edge";
    check(nativeLifecycleEventIsBase(event)&&!nativeLifecycleEventIsBase((std::string(event)+"-forged").c_str()),"selected family receipt has exact log admission "+label);
    if(selectedActionFamily==YmccFamily::Aokzoe){const auto& meta=catalog["keys"][i];
    check(meta.contains("physicalKeyIds")&&meta["captureRoute"]=="keyboard-vk","AOKZOE operation carries source-backed physical identity "+label);}
   }ipc["shortcut.recording.set"]({{"active",false}});
   g_nativeKeyboardShortcuts.clear();
  }
  if(selectedActionFamily==YmccFamily::Aokzoe){
   check(modelKeys==6,"AOKZOE preserves six declared operations "+model);
   for(const bool loseFocus:{false,true}){
    ipc["shortcut.recording.set"]({{"active",true}});gamepadRecordShortcutChord(0);events=json::array();eligible=!loseFocus;custom=!loseFocus;
    posted(kOemBitHome);check(!g_shortcutRecordingActive&&actionJobs.empty(),"AOKZOE recording stops on focus/owner loss "+model);eligible=true;custom=false;
   }
   events=json::array();nativeKeyboardShortcutHandleOemEdge(WPARAM(kOemBitHome),LPARAM(oemShortcutEdgeToken(g_oemShortcutEdgeEpoch.load(),g_gpdBackIfaceGen.load())^(1ull<<32)));
   check(events.empty()&&actionJobs.empty(),"AOKZOE stale rule generation cannot create a press or action "+model);
  }
  if(selectedActionFamily==YmccFamily::Gpd){
   const bool win5=model=="HandheldCompanion.Devices.GPDWin5";
   const bool win2=model=="HandheldCompanion.Devices.GPDWin2";
   check(modelKeys==(win5?5u:win2?0u:2u),"GPD preserves declared per-generation keys without inventing a Menu chord "+model);
   if(modelKeys){
    const auto bit=win5?kOemBitKeyboard:kOemBitL4;
    ipc["shortcut.recording.set"]({{"active",true}});gamepadRecordShortcutChord(0);events=json::array();
    g_gpdBackIfaceReady=false;posted(bit);posted(0);g_gpdBackIfaceReady=true;
    bool captured=false;for(const auto& e:events)if(e["name"]=="shortcut.recording.gamepad")for(const auto& input:e["data"]["inputs"])if(input["source"]=="oem")captured=true;
    check(captured,"GPD dedicated keyboard or legacy key works without a Win5 HID interface "+model);
    ipc["shortcut.recording.set"]({{"active",false}});events=json::array();
    const auto token=oemShortcutEdgeToken(g_oemShortcutEdgeEpoch.load(),g_gpdBackIfaceGen.load());
    nativeKeyboardShortcutHandleOemEdge(WPARAM(bit),LPARAM(token^(1ull<<32)));
    check(events.empty()&&actionJobs.empty(),"GPD stale rule generation rejects queued preview and actions "+model);
    if(win5){
     g_gpdBackIfaceGen.fetch_add(1);nativeKeyboardShortcutHandleOemEdge(WPARAM(bit),LPARAM(token));
     check(events.empty()&&actionJobs.empty(),"Win5 retired interface generation rejects a stale queued key "+model);
     NativeKeyboardShortcutRule rule;rule.requiredMask=kOemBitGamepad;rule.outputId="gpd-authority-test";rule.virtualKey='K';
     const auto resolve=[](const std::string&,const std::string&,unsigned short& vk,bool& shift){vk='K';shift=false;return true;};
     check(ymcc::keyboardSequence::parse(json{{"key","KeyK"}},"keyboard.key",resolve,rule.keyboardPlan),"GPD authority fixture uses real parser");
     g_nativeKeyboardShortcuts={rule};physicalAuthority=kOemBitGamepad;posted(0);posted(kOemBitGamepad);posted(0);
     check(actionJobs.empty(),"Win5 authoritative HID suppresses duplicate keyboard action");physicalAuthority=0;g_nativeKeyboardShortcuts.clear();
    }
   }
  }
  coverage.push_back({{"deviceClass",model},{"operationEntries",modelKeys}});
 }
 nativeKeyboardSequenceStop();
 json report={{"pass",failures.empty()},{"checks",checks},{"failures",failures},{"selectedCases",${JSON.stringify(actionCases)}},{"models",selectedActionModels.size()},{"operationEntries",testedKeys},{"modelCoverage",coverage},{"actualNativeEvaluator",true},{"actualDispatchAndSerialGuards",true},{"actualKeyboardWorker",true},{"actualUiActionMailboxAndAckConsumer",true},{"ruleInputBoundary","native rule structure; editor saving and complete settings parser not covered"},{"uiBusinessActionExecuted",false},{"osKeyboardLeaf","memory only"},{"virtualOutput","disabled"},{"hardwareIo",false},{"physicalActionSuccessProven",false},{"nativeOemInitializationConsumer",${nativeOemActions}},{"completeCaptureWorkerExecuted",false},{"beforeControl",${nativeOemControl}}};
 std::ofstream("RESULT.json")<<report.dump(2);std::cout<<"CHECKS "<<checks<<" FAILURES "<<failures.size()<<'\n';return failures.empty()?0:1;
}
`;
}


if(vendorActions){
 // Extend the existing action producer with the actual input producer, rather
 // than proving a vendor path by injecting synthetic keyboard hook messages.
 const replace=(before,after)=>{assert(code.includes(before),'vendor fixture port: '+before.slice(0,90));code=code.replace(before,after);};
 const producerStart=source.indexOf('enum class OemProducerId'),producerEnd=source.indexOf('// Only the new OneX sources use this context fence.',producerStart);
 assert(producerStart>0&&producerEnd>producerStart);
 const producerState=source.slice(producerStart,producerEnd);
 for(const block of [declaration('OemProducerId'),source.match(/struct OemProducerTokens \{[\s\S]*?\n\};/)[0],source.match(/struct OemProducerEdgeReceipt \{[\s\S]*?\n\};/)[0],source.match(/static constexpr size_t kOemProducerEdgeCapacity = [^\n]+/)[0],source.match(/static constexpr std::array<uint64_t, kOemProducerCount> kOemProducerMasks = \{[\s\S]*?\n\};/)[0]])replace(block,'');
 replace('static constexpr int kOemProducerCount=8;',producerState);
 replace('static std::array<std::atomic<uint64_t>,kOemProducerCount>g_oemProducerGenerations{},g_oemProducerAuthorities{};','');
 replace('static uint64_t oemProducerAuthorityMask(){return physicalAuthority;}','static uint64_t oemProducerAuthorityMask();');
 replace('static uint64_t oemProducerActionMask(bool=false){return 0;}','static uint64_t oemProducerActionMask(bool=false,bool=false);');
 replace('static uint64_t nativeOemKeyMask(bool){return displayMask;}','static uint64_t nativeOemKeyMask(bool);');
 replace('static std::deque<OemProducerEdgeReceipt>producerQueue;static bool producerCurrent=true;','');
 replace('static bool oemProducerCurrent(OemProducerId,uint64_t generation){return producerCurrent&&generation==1;}','');
 const functions=['oemProducerCurrent','oemProducerAuthorityMask','oemProducerBegin','oemProducerCancel','oemProducerActionMask','oemProducerPhysicalMask','nativeOemKeyboardActionMask','oemProducerPublish','oemProducerTryPop'];
 const producerCode=String.raw`
// Legacy OneX and snapshot GPD use the production always-current context branch;
// X2 power/topology admission is excluded from this bounded legacy-protocol case.
static bool oemProducerContextCurrent(OemProducerId id){return id!=OemProducerId::OneXX2Rear&&id!=OemProducerId::OneXX2Home;}
static std::atomic<int>g_oemBackState{0};static std::atomic<uint64_t>g_oemKeyMaskHid{0},g_oemKeyMaskMsiDinput{0};
static uint64_t oemBackMergedKeyMask(){return displayMask|g_oemKeyMaskHid.load();}
static uint64_t oemProducerPhysicalMask();static uint64_t nativeOemKeyboardActionMask();
static HWND g_hwnd=reinterpret_cast<HWND>(uintptr_t(42));static bool fixturePostAccepts=true;
${source.match(/^#define WM_OEM_PRODUCER_EDGE[^\n]+/m)[0]}
static BOOL fixturePostMessage(HWND,UINT,WPARAM,LPARAM){return fixturePostAccepts;}
#define PostMessageW fixturePostMessage
${functions.map(name=>fn(name).replace('bool msiOnly = false','bool msiOnly').replace('bool allSources = false','bool allSources')).join('\n')}
#undef PostMessageW
static uint64_t nativeOemKeyMask(bool){return nativeOemKeyboardActionMask()|oemProducerActionMask()|oemProducerActionMask(true);}
${source.match(/^enum class OnexInitProfile[^\n]+/m)[0]}
${source.match(/struct OnexCapDescriptor \{[\s\S]*?\n\};/)[0]}
${fn('onexCapDescriptorFor')}
${source.match(/struct OnexBackSession \{[\s\S]*?\n\};/)[0]}
static OnexBackSession g_onexBack;
${[...source.matchAll(/^static constexpr [^\n]*\bkOnex\w+[^\n]*;/gm)].map(m=>m[0]).join('\n')}
static unsigned fixtureReinitRequests=0;
static void onexBackRequestReinit(){++fixtureReinitRequests;}
static void modelAdaptationOnexReport(const BYTE*,size_t,bool){}
${fn('onexBackParse')}
`;
 replace('static bool oemProducerTryPop(OemProducerEdgeReceipt& receipt){if(producerQueue.empty())return false;receipt=producerQueue.front();producerQueue.pop_front();return true;}',producerCode);
 replace('static bool oemProducerContextCurrent(OemProducerId){return true;}','');
 const mainAt=code.indexOf('int main()');assert(mainAt>0);
 code=code.slice(0,mainAt)+fs.readFileSync(path.join(root,'tools/fixtures/unified_oem_input/onex_vendor_actions.cpp'),'utf8');
}


if(editorInputs){
 code=code.replace('#include <fstream>','#include <fstream>\n#include <set>');
 code=code.replace('static uint64_t gameInputOwnerSession(){return 1;}','static std::string gameInputOwnerSession(){return \"fixture-session\";}');
 code=code.replace('static void nativeKeyboardShortcutsRefresh(ULONGLONG){}','static void nativeKeyboardShortcutsRefresh(ULONGLONG);');
 code=code.replace('namespace ymcc{static json inputShortcutContext(const json&,uint64_t){return json::object();}}','');
 const parserFunctions=['nativeKeyboardShortcutDuration','nativeKeyboardShortcutRequiredMask',...(hasKeyboardDefinitions?[]:['nativeInputShortcutDesktopKey']),'nativeInputShortcutDesktopKeys','nativeKeyboardShortcutVirtualKey','nativeKeyboardShortcutAllInputs','nativeYmccShortcutAction','nativeYmccShortcutDelta','nativeShortcutLegacyDefaultSlot','nativeShortcutLegacyTemplateMatches','nativeShortcutLegacyDefaultAllowed','nativeKeyboardShortcutsRefresh'];
 const parser=String.raw`
#include "input_shortcut_toggle.h"
#include "shortcut_frontend_button.h"
#include "boot_mode_shortcuts.h"
namespace ymcc::radial{static bool userAvailable(){return false;}static bool userActionAvailable(const std::string& action){return action!="radial.summon";}}
static bool g_nativeShortcutRulesConfigured=false;
static ULONGLONG g_nativeKeyboardShortcutLastRefresh=0;
static long long g_nativeKeyboardShortcutRevision=-1;
static constexpr ULONGLONG NATIVE_KEYBOARD_SHORTCUT_REFRESH_MS=400;
${parserFunctions.map(fn).join('\n')}
`;
 // inputShortcutContext must be declared before the actual dispatcher uses it.
 code=code.replace('#include "shortcut_action_protocol.h"','#include "shortcut_action_protocol.h"\n#include "input_shortcut_toggle.h"');
 const evaluator=fn('nativeKeyboardShortcutsEvaluate');assert(code.includes(evaluator));code=code.replace(evaluator,parser+'\n'+evaluator);
 const snapshots=JSON.stringify(editorInputs);assert(!snapshots.includes(')YMCC_EDITOR"'),'Unexpected editor payload delimiter');
 const mainAt=code.indexOf('int main()');assert(mainAt>0);
 code=code.slice(0,mainAt)+String.raw`
static void parseEditorInput(const json& input){fixtureSettings=input;g_nativeKeyboardShortcutLastRefresh=0;g_nativeKeyboardShortcutRevision=-1;nativeKeyboardShortcutsRefresh(GetTickCount64());}
int main(){registerIpc();nativeShortcutActionRegisterIpc();unsigned snapshots=0;json coverage=json::array();std::set<std::string> models,operations;
 std::ifstream savedStream(R"YMCC_EDITOR(${path.resolve(editorInputsPath).replaceAll("\\","/")})YMCC_EDITOR");json saved;savedStream>>saved;
 physicalAuthority=dinputAuthority=0;wmi=false;eligible=true;custom=false;displayMask=0;
 for(const auto& row:saved){
  ++snapshots;const auto model=row.at("model").get<std::string>(),key=row.at("keyId").get<std::string>(),id=row.at("ruleId").get<std::string>(),label=row.at("name").get<std::string>();models.insert(model);operations.insert(model+" "+key);
  g_machineIdentity.family=selectedActionFamily;g_machineIdentity.deviceClass=model;
  const auto& binding=oemUnifiedInputBindingFor(g_machineIdentity.family,model);g_oemBackProfileId=binding.profileId;
  const auto catalog=ipc["oem.keys.get"](json::object());check(catalog==row.at("catalog"),"current native catalog equals editor input catalog "+label);
  uint64_t bit=0;check(nativeKeyboardShortcutMaskBit(key,bit)&&oemKeyCaptureImplemented(g_machineIdentity.family,model,key.c_str()),"saved OEM key is still physically registered "+label);
  check(row.at("input").value("virtualEnabled",true)==false,"saved editor snapshot keeps virtual output disabled "+label);
  g_nativeKeyboardShortcuts.clear();parseEditorInput(row.at("input"));
  const auto target=std::find_if(g_nativeKeyboardShortcuts.begin(),g_nativeKeyboardShortcuts.end(),[&](const auto& rule){return rule.outputId==id;});
  check(target!=g_nativeKeyboardShortcuts.end()&&target->requiredMask==bit&&!target->keyboardPlan.steps.empty(),"real settings parser creates exact saved keyboard rule "+label);
  posted(0);events=json::array();edgeLogs=json::array();clearTrace();posted(bit);posted(bit);posted(0);
  check(actionJobs.size()==1,"real evaluator queues one action from actual editor snapshot "+label);actionDrain();waitReleased();
  unsigned admissions=0;uint64_t request=0;
  for(const auto& event:edgeLogs)if(event["event"]=="oem-shortcut-edge-delivery"&&event["data"].value("scope","")=="keyboard-action-admission"){
   const auto& data=event["data"];check(data["accepted"]==true&&!data["actionExecutionProven"].get<bool>()&&!data["physicalEffectVerified"].get<bool>(),"admission receipt never promotes keyboard or physical success "+label);
   if(!request)request=data["requestSequence"].get<uint64_t>();check(data["requestSequence"]==request,"serial/worker receipts pair to one OEM request "+label);++admissions;
  }
  check(admissions==2,"saved OEM action has separate queue and worker receipts "+label);edgeLogs=json::array();
  {std::lock_guard<std::mutex>lock(actionTraceMx);check(actionTrace==std::vector<std::pair<WORD,bool>>{{'K',false},{'K',true}},"real dispatcher and keyboard worker consume saved rule once "+label);actionTrace.clear();}
  // A successful parse after editing cannot replay a key already held.
  parseEditorInput(row.at("input"));posted(bit);posted(0);actionDrain();check(traceSize()==0,"saved-rule revision does not replay a held key "+label);
  parseEditorInput(row.at("input"));posted(0);ipc["shortcut.recording.set"]({{"active",true}});gamepadRecordShortcutChord(0);posted(bit);posted(0);
  check(actionJobs.empty()&&traceSize()==0,"recording owns saved OEM rule without executing it "+label);ipc["shortcut.recording.set"]({{"active",false}});
  json disabled=row.at("input");for(auto& rule:disabled["buttonMapping"]["rules"]["shortcutRules"])if(rule.value("id","")==id)rule["enabled"]=false;
  parseEditorInput(disabled);posted(0);posted(bit);posted(0);actionDrain();check(traceSize()==0,"disabled saved OEM rule cannot produce output "+label);
  json malformed=row.at("input");for(auto& rule:malformed["buttonMapping"]["rules"]["shortcutRules"])if(rule.value("id","")==id)rule["inputs"]=json::array({json{{"source","oem"},{"code","unknown-aya-key"}}});
  parseEditorInput(malformed);posted(0);posted(bit);posted(0);actionDrain();check(traceSize()==0,"unknown saved OEM source fails closed "+label);
  coverage.push_back({{"model",model},{"keyId",key},{"height",row.at("height")},{"nativeParsedRules",g_nativeKeyboardShortcuts.size()}});
 }
 // Preserve the two distinct refusal boundaries without performing OS input.
 NativeKeyboardShortcutRule refused;refused.requiredMask=1ull<<20;refused.outputId="admission-refusal";refused.keyboardPlan.steps={{{{'K',false}},80}};
 edgeLogs=json::array();fixtureQueueAccepts=false;
 check(!nativeKeyboardSequenceDispatch(refused,g_oemShortcutEdgeEpoch.load(),0,0,false,{}),"serial queue rejection remains a rejected dispatch");
 check(edgeLogs.size()==1&&edgeLogs[0]["data"]["stage"]=="serial-queue-submit"&&edgeLogs[0]["data"]["accepted"]==false,"rejected serial queue has no fictional worker admission");
 fixtureQueueAccepts=true;nativeKeyboardSequenceStop();edgeLogs=json::array();
 check(nativeKeyboardSequenceDispatch(refused,g_oemShortcutEdgeEpoch.load(),0,0,false,{}),"serial queue may admit a job before its stopped worker refuses");actionDrain();
 check(edgeLogs.size()==2&&edgeLogs[0]["data"]["stage"]=="serial-queue-submit"&&edgeLogs[0]["data"]["accepted"]==true&&edgeLogs[1]["data"]["stage"]=="keyboard-worker-submit"&&edgeLogs[1]["data"]["accepted"]==false,"worker refusal is recorded separately from serial acceptance");
 check(traceSize()==0,"refused worker cannot produce keyboard output");
 edgeLogs=json::array();refused.requiredMask=0;nativeKeyboardSequenceDispatch(refused,g_oemShortcutEdgeEpoch.load(),0,0,false,{});actionDrain();check(edgeLogs.empty(),"non-OEM keyboard rule has no new OEM admission logging");
 json result={{"pass",failures.empty()},{"checks",checks},{"failures",failures},{"selectedCases",${JSON.stringify(actionCases)}},{"savedSnapshots",snapshots},{"models",models},{"uniqueOperations",operations.size()},{"coverage",coverage},{"actualSettingsParser",true},{"actualNativeEvaluator",true},{"actualDispatcherAndKeyboardWorker",true},{"settingsIo","browser memory CAS -> exported JSON -> actual native parser"},{"actualNativeDiskCommit",false},{"oemVendorSideEffects","excluded by existing YMCC_SHORTCUT_FIXTURE; not validated"},{"osKeyboardLeaf","memory only"},{"virtualOutput","disabled"},{"hardwareIo",false},{"physicalActionSuccessProven",false}};
 std::ofstream("RESULT.json")<<result.dump(2);std::cout<<"CHECKS "<<checks<<" FAILURES "<<failures.size()<<'\n';return failures.empty()?0:1;
}
`;
}

const generated=path.join(out,'oem-recording.cpp');fs.writeFileSync(generated,code);if(process.env.YMCC_OEM_RECORDING_PREPARE_ONLY==='1'){fs.writeFileSync(path.join(out,'PREPARED.json'),JSON.stringify({status:'PREPARED_ONLY',selection:recordingCase,sourceInputs,headers:oemHeaders.inputs,generatedSha256:createHash('sha256').update(code).digest('hex')},null,2));process.exit(0);}const exe=path.join(out,'oem-recording.exe'),batch=path.join(out,'build.cmd');fs.writeFileSync(batch,calibrationToolchainPreamble('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat',{boundInheritedPath:true})+'cl /Bv /sourceDependencies "'+out+'/source-dependencies.json" /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /I"'+root+'/native" /I"'+root+'/deps/json" "'+generated+'" /Fo"'+out+'/oem-recording.obj" /Fe"'+exe+'"\r\nexit /b %errorlevel%\r\n');const build=spawnSync('cmd.exe',['/d','/c',batch],{cwd:out,encoding:'utf8',windowsHide:true,timeout:60000});fs.writeFileSync(path.join(out,'BUILD.log'),(build.stdout||'')+(build.stderr||''));assert.equal(build.status,0,(build.stdout||'')+(build.stderr||''));const run=spawnSync(exe,[],{cwd:out,encoding:'utf8',windowsHide:true,timeout:60000});fs.writeFileSync(path.join(out,'RUN.log'),(run.stdout||'')+(run.stderr||''));fs.writeFileSync(path.join(out,'SOURCE.json'),JSON.stringify({sourceFile,privateSource:sourceFile!==path.join(root,'native/main.cpp'),mainSha256:createHash('sha256').update(source).digest('hex'),generatedSha256:createHash('sha256').update(code).digest('hex'),inputs:[...sourceInputs,...oemHeaders.inputs]},null,2));assert(oemHeaders.unchanged(),'actual OEM header closure changed during recording test');assert(sourceInputs.every(r=>createHash('sha256').update(fs.readFileSync(r.sourcePath)).digest('hex')===r.sha256),'recording inputs changed during test');console.log(run.stdout+run.stderr);assert.equal(run.status,0,'Production OEM recorder regression');
