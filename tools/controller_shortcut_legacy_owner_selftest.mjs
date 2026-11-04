// Replays production rule selection and the actual LB+RB hold branch.
// The OS window/action, configuration and clock are in-memory seams only.
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import {spawnSync} from 'node:child_process';import {createRequire} from 'node:module';import {createHash} from 'node:crypto';
const root=path.resolve(import.meta.dirname,'..'),out=path.resolve(process.argv[2]||'G:/YeManCC-Work/Build/Tasks/ControllerSummonRebind-20261031/native');fs.mkdirSync(out,{recursive:true});
const sourceFile=process.argv[3]||path.join(root,'native/main.cpp'),source=fs.readFileSync(sourceFile,'utf8');
function extract(name){const regex=new RegExp('^static [^\\n;]*\\b'+name+'\\([^]*?\\n\\}','m'),match=regex.exec(source);assert(match,'Missing '+name);return match[0]}
const structure=source.match(/struct NativeKeyboardShortcutRule \{[\s\S]*?\n\};/)[0];
const funcs=['nativeKeyboardShortcutMaskBit','nativeKeyboardShortcutDuration','nativeKeyboardShortcutRequiredMask','nativeKeyboardShortcutAllInputs','nativeYmccShortcutAction','nativeYmccShortcutDelta','nativeShortcutLegacyDefaultSlot','nativeShortcutLegacyTemplateMatches','nativeShortcutLegacyDefaultAllowed','nativeKeyboardShortcutsRefresh'].map(extract);
const legacyStart=source.indexOf('    if (both && nativeShortcutLegacyDefaultAllowed("enabled"))');assert(legacyStart>0);const legacyEnd=source.indexOf('\n    // ── Start +',legacyStart);assert(legacyEnd>legacyStart);const legacy=source.slice(legacyStart,legacyEnd);
// Use the frontend's real default rows so native ownership cannot drift from the editor.
const require=createRequire(path.join(root,'package.json'));
const bundled=require('esbuild').buildSync({entryPoints:[path.join(root,'src/bridge/controllerShortcutRules.ts')],bundle:true,platform:'node',format:'cjs',write:false}).outputFiles[0].text;
const module={exports:{}};new Function('module','exports','require',bundled)(module,module.exports,require);
const defaults=module.exports.NATIVE_DEFAULT_SHORTCUTS.map(def=>module.exports.nativeDefaultRule(def.defaultKey));
const editorExports=process.argv[4]?JSON.parse(fs.readFileSync(process.argv[4],'utf8')):[];
const evaluatorStart=source.indexOf('static void nativeKeyboardShortcutsEvaluate(');
const coreStart=source.indexOf('        const uint64_t currentMask = ',evaluatorStart);
const coreEnd=source.indexOf('        if (emit) {',coreStart);
assert(coreStart>evaluatorStart&&coreEnd>coreStart);
const triggerCore=source.slice(coreStart,coreEnd);
const cpp=String.raw`
#define YMCC_SHORTCUT_FIXTURE
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <Xinput.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>
#include "input_shortcut_toggle.h"
#include "shortcut_action_protocol.h"
#include "shortcut_frontend_button.h"
#include "boot_mode_shortcuts.h"
using json=nlohmann::json;
namespace ymcc::radial{static bool userAvailable(){return false;}static bool userActionAvailable(const std::string& s){return s!="radial.summon";}}
static constexpr int kOemProducerCount=8;
${structure}
static std::vector<NativeKeyboardShortcutRule> g_nativeKeyboardShortcuts;
static uint32_t g_nativeShortcutLegacyOverrideMask=0;
static bool g_nativeShortcutRulesConfigured=false;
static ULONGLONG g_nativeKeyboardShortcutLastRefresh=0;
static long long g_nativeKeyboardShortcutRevision=-1;
static constexpr ULONGLONG NATIVE_KEYBOARD_SHORTCUT_REFRESH_MS=400;
static std::atomic<uint32_t> g_oemShortcutEdgeEpoch{0};
static json fixtureInput;
static json ymSettingsSection(const char*){return fixtureInput;}
// In-memory settings seam; the production getter is checked by its real Win32 leaf.
static json ymSettingsFields(const char* section, std::initializer_list<const char*> fields) {
    const auto input = ymSettingsSection(section);
    json selected = json::object();
    if (input.is_object()) for (const auto* key : fields) {
        const auto value = input.find(key);
        if (value != input.end()) selected[key] = *value;
    }
    return selected;
}
static bool nativeInputShortcutDesktopKeys(const json&,std::vector<WORD>&){return false;}
static bool nativeKeyboardShortcutVirtualKey(const std::string&,const std::string&,WORD&,bool&){return false;}
${funcs.join('\n')}
static ULONGLONG gpHoldStart=0;static bool gpArmed=true,g_summonEnabled=true;
static HWND g_hwnd=reinterpret_cast<HWND>(1);
struct FocusTargetSnapshot{};static FocusTargetSnapshot g_pendingSummonTarget;
static int legacySummons=0;
static void nativeYmccShortcutSummon(const FocusTargetSnapshot&){++legacySummons;}
static void legacyTick(WORD w,ULONGLONG now){const auto shoulderBits=XINPUT_GAMEPAD_LEFT_SHOULDER|XINPUT_GAMEPAD_RIGHT_SHOULDER;const bool both=(w&shoulderBits)==shoulderBits;const bool shoulderDown=(w&shoulderBits)!=0;constexpr ULONGLONG HOLD_MS=500;
${legacy}
}
static int configuredSummons=0;
static SHORT fixtureGetAsyncKeyState(int){return 0;}
#define GetAsyncKeyState fixtureGetAsyncKeyState
static void appendNativeLifecycleLog(const char*,const json&){}
static void configuredTick(uint64_t oemMask,ULONGLONG now){
 const bool oemRule=true;const uint64_t controllerMask=0;
 for(auto& rule:g_nativeKeyboardShortcuts){
${triggerCore}
  if(emit&&rule.ownAction=="window.summon")++configuredSummons;
  rule.matchedLastTick=matched;
 }
}
static int checks=0,regressions=0;
static void check(bool ok,const char* name){++checks;if(!ok)throw std::runtime_error(name);}
static void regression(bool ok,const char* name){++checks;if(!ok){++regressions;std::cout<<"REGRESSION "<<name<<'\n';}}
static json input(const char* code,const char* source="controller"){return{{"source",source},{"code",code}};}
static json rule(const char* id,const char* action,json inputs,const char* trigger="press",const char* slot=nullptr){json r={{"id",id},{"origin",slot?"native-default":"custom"},{"enabled",true},{"actionId",action},{"inputs",inputs},{"params",json::object()},{"trigger",trigger},{"holdMs",500},{"doubleWindowMs",500},{"intervalMs",150}};if(slot)r["defaultKey"]=slot;return r;}
static json defaultSummon(){return rule("native-enabled","window.summon",json::array({input("lb"),input("rb")}),"hold","enabled");}
static void load(json rules,bool configured=true){fixtureInput={{"revision",1},{"buttonMapping",{{"rules",json::object()}}}};if(configured){fixtureInput["buttonMapping"]["rules"]["shortcutRules"]=rules;fixtureInput["buttonMapping"]["rules"]["shortcutRulesInitialized"]=true;}g_nativeKeyboardShortcutRevision=-1;g_nativeKeyboardShortcutLastRefresh=0;nativeKeyboardShortcutsRefresh(10000);gpHoldStart=0;gpArmed=true;legacySummons=configuredSummons=0;g_summonEnabled=true;}
static void oldChord(){const WORD both=XINPUT_GAMEPAD_LEFT_SHOULDER|XINPUT_GAMEPAD_RIGHT_SHOULDER;legacyTick(0,11000);legacyTick(both,11100);legacyTick(both,11700);legacyTick(both,12500);legacyTick(0,12600);}
static void refreshMode(json rows){fixtureInput["startupObservedBoot"]="boot-A";fixtureInput["bootModeShortcuts"]={{"schema",1},{"mode","steam"},{"boot","boot-A"},{"rules",rows}};fixtureInput["revision"]=2;g_nativeKeyboardShortcutLastRefresh=0;nativeKeyboardShortcutsRefresh(20000);}
int main(){try{
 const auto frontendDefaults=json::parse(R"FRONTEND(${JSON.stringify(defaults)})FRONTEND");
 for(const auto& definition:frontendDefaults){
  const auto slot=definition["defaultKey"].get<std::string>();
  load(frontendDefaults);check(nativeShortcutLegacyDefaultAllowed(slot.c_str()),"every present pristine editor default keeps its single legacy owner");
  auto deleted=frontendDefaults;deleted.erase(std::remove_if(deleted.begin(),deleted.end(),[&](const json&r){return r.value("defaultKey","")==slot;}),deleted.end());
  load(deleted);regression(!nativeShortcutLegacyDefaultAllowed(slot.c_str()),"deleted default never survives outside its explicit rule list");
  for(const auto& other:deleted){const auto otherSlot=other["defaultKey"].get<std::string>();check(nativeShortcutLegacyDefaultAllowed(otherSlot.c_str()),"deleting one slot preserves every other explicitly configured default");}
  auto disabled=frontendDefaults;for(auto& row:disabled)if(row.value("defaultKey","")==slot)row["enabled"]=false;load(disabled);check(!nativeShortcutLegacyDefaultAllowed(slot.c_str()),"disabled editor default retires its original handler");
  auto edited=frontendDefaults;for(auto& row:edited)if(row.value("defaultKey","")==slot){row["inputs"]=json::array({input("l4","oem")});row["trigger"]="press";}
  load(edited);check(!nativeShortcutLegacyDefaultAllowed(slot.c_str())&&g_nativeKeyboardShortcuts.size()==1,"editing any default transfers ownership exactly once");
 }

 load(json::array(),false);oldChord();check(legacySummons==1,"first-run absent list keeps original LB+RB behavior");
 load(json::array({defaultSummon()}));oldChord();check(legacySummons==1&&g_nativeKeyboardShortcuts.empty(),"exact default delegates once to its legacy handler");
 for(const char* code:{"l4","r4","m1","m2","legionr"})for(const char* trigger:{"press","hold","release","double","repeat"}){
  auto edited=defaultSummon();edited["inputs"]=json::array({input(code,"oem")});edited["trigger"]=trigger;load(json::array({edited}));
  check(g_nativeKeyboardShortcuts.size()==1,"edited native default has one configured runtime owner");uint64_t bit=0;nativeKeyboardShortcutMaskBit(code,bit);check(g_nativeKeyboardShortcuts[0].requiredMask==bit,"OEM physical source identity is preserved");oldChord();check(legacySummons==0,"edited default never allows old shoulder summon");
  configuredTick(bit,10010);check(configuredSummons==0,"new bindings never inherit held inputs before a neutral sample");configuredTick(0,10020);
  configuredTick(bit,10100);configuredTick(bit,10700);configuredTick(0,10800);configuredTick(bit,10900);configuredTick(bit,11500);configuredTick(0,11600);
  const int expected=std::string(trigger)=="double"?0:2;
  if(std::string(trigger)=="double"){configuredTick(bit,11700);configuredTick(0,11720);configuredTick(bit,11800);configuredTick(0,11820);check(configuredSummons==1,"double source emits exactly once for the configured double press");}
  else check(configuredSummons==expected,"actual new-source trigger state machine dispatches configured summon only");
  const auto releasedCount=configuredSummons;configuredTick(0,12500);check(configuredSummons==releasedCount,"released source stops emitting new summon actions");
  edited["enabled"]=false;load(json::array({edited}));oldChord();check(legacySummons==0&&g_nativeKeyboardShortcuts.empty(),"disabled replacement never revives old binding");
 }
 auto changed=defaultSummon();changed["inputs"]=json::array({input("l4","oem")});
 auto replacement=changed;replacement["origin"]="custom";replacement.erase("defaultKey");replacement["id"]="replacement-L4";
 load(json::array({replacement}));oldChord();regression(legacySummons==0,"explicit custom L4 replacement must not leave hidden LB+RB fallback");check(g_nativeKeyboardShortcuts.size()==1,"replacement L4 parses to runtime");
 load(json::array());oldChord();regression(legacySummons==0,"explicitly empty shortcut list must not revive default summon");
 load(json::array({defaultSummon(),replacement}));oldChord();check(legacySummons==1&&g_nativeKeyboardShortcuts.size()==1,"two explicitly saved summon bindings both remain permitted");
 load(json::array({changed}));refreshMode(json::array({replacement}));oldChord();regression(legacySummons==0,"independent mode custom L4 list must not add implicit default summon");
 load(json::array({changed}));refreshMode(json::array({defaultSummon()}));oldChord();check(legacySummons==0&&g_nativeKeyboardShortcuts.size()==1,"retired independent default cannot override the edited global list");
 fixtureInput["bootModeShortcuts"]["boot"]="old-boot";fixtureInput["revision"]=3;g_nativeKeyboardShortcutLastRefresh=0;nativeKeyboardShortcutsRefresh(30000);legacySummons=0;gpHoldStart=0;gpArmed=true;oldChord();check(legacySummons==0,"expired mode falls back to the configured global replacement");
 load(json::array({changed}));auto wrong=defaultSummon();wrong["inputs"]=json::array({input("no-such-key","oem")});load(json::array({wrong}));oldChord();check(legacySummons==0&&g_nativeKeyboardShortcuts.empty(),"invalid edited input does not silently restore original default");
 const auto savedByEditor=json::parse(R"EDITOR(${JSON.stringify(editorExports)})EDITOR");
 for(const auto& saved:savedByEditor){
  fixtureInput=saved["input"];fixtureInput["revision"]=100;g_nativeKeyboardShortcutRevision=-1;g_nativeKeyboardShortcutLastRefresh=0;nativeKeyboardShortcutsRefresh(40000);gpArmed=true;gpHoldStart=0;legacySummons=configuredSummons=0;oldChord();
  check(legacySummons==0,"actual editor save receipt never retains implicit LB+RB");
  configuredTick(0,41000);configuredTick(1ull<<37,41100);configuredTick(0,41150);
  const bool enabled=saved["name"]=="edited-native-default-L4"||saved["name"]=="custom-replacement-L4";
  check(configuredSummons==(enabled?1:0),"actual editor saved L4 triggers only when its saved rule is enabled");
 }
 std::cout<<"CHECKS "<<checks<<" REGRESSIONS "<<regressions<<" device_io=none physical_input=none settings_io=memory\n";return regressions?2:0;
}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
`;
const generated=path.join(out,'summon-rebind.cpp');fs.writeFileSync(generated,cpp);
const sdk='C:/Program Files (x86)/Windows Kits/10',version='10.0.26100.0';const exe=path.join(out,'summon-rebind.exe'),batch=path.join(out,'build.cmd');
fs.writeFileSync(batch,'@echo off\r\ncall "C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat" '+version+' >nul\r\nif errorlevel 1 exit /b 1\r\ncl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 '+['um','shared','ucrt'].map(p=>'/I"'+sdk+'/Include/'+version+'/'+p+'"').join(' ')+' /I"'+root+'/native" /I"'+root+'/deps/json" "'+generated+'" /Fo"'+out+'/summon-rebind.obj" /Fe"'+exe+'" /link /LIBPATH:"'+sdk+'/Lib/'+version+'/um/x64" /LIBPATH:"'+sdk+'/Lib/'+version+'/ucrt/x64"\r\nexit /b %errorlevel%\r\n');
const build=spawnSync('cmd.exe',['/d','/c',batch],{cwd:out,encoding:'utf8',windowsHide:true});fs.writeFileSync(path.join(out,'BUILD.log'),(build.stdout||'')+(build.stderr||''));assert.equal(build.status,0,build.stdout+build.stderr);
const run=spawnSync(exe,[],{cwd:out,encoding:'utf8',windowsHide:true});fs.writeFileSync(path.join(out,'RUN.log'),(run.stdout||'')+(run.stderr||''));console.log((run.stdout||'')+(run.stderr||''));const report={pass:run.status===0,exit:run.status,result:run.stdout,source:sourceFile,sourceSha256:createHash('sha256').update(source).digest('hex'),actualRuleParser:true,actualLegacyShoulderHandler:true,actualTriggerStateMachine:true,hardwareIo:false,installedSettingsWritten:false};fs.writeFileSync(path.join(out,'ACCEPTANCE.json'),JSON.stringify(report,null,2));assert.equal(run.status,0,'Production summon binding regression');
