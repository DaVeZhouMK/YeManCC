import {calibrationToolchainPreamble} from './fixtures/hc_reviewed_calibration/toolchain.mjs';
// Actual shared native exclusions and admission expression; synthetic candidates.
// No process launch/control, sensor reads, saved settings, or Steam requests.
import {extract as extractNative} from './fixtures/legion_motion_consumer/extract_main.mjs';
import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';import cp from 'node:child_process';import assert from 'node:assert/strict';
const root=process.cwd(),out=path.resolve(process.env.YMCC_MONITOR_REPAIR_OUT||'../../../Build/Tasks/YMCC-Steam-HUD-Probe/monitor-repair-20261015');fs.mkdirSync(out,{recursive:true});
const source=fs.readFileSync('native/main.cpp','utf8');
const literalRuleFile=path.join(root,'native/game_rule_literal.h'),literalRuleBytes=fs.readFileSync(literalRuleFile);
function extract(start){const i=source.indexOf(start);assert(i>=0,start);let depth=0,opened=false;for(let j=source.indexOf('{',i);j<source.length;++j){if(source[j]==='{'){depth++;opened=true}else if(source[j]==='}'&&--depth===0&&opened)return source.slice(i,j+1)}throw Error('Unclosed function '+start)}
const matcher=extract('static bool sgNamePatternMatches('),excludes=extract('static bool sgNameExcludedBy('),builtin=extract('static bool nativeMonitorExcluded(');
const predicate='(!isWhitelisted && (nativeMonitorExcluded(name) || sgNameExcludedBy(name, userExcludes)))';assert(source.includes(predicate));
const rulesArray=[...source.matchAll(/^static const wchar_t\* SG_BLACKLIST\[\] = \{[^}]+\};/gm)];assert.equal(rulesArray.length,1);
const functions=['sgBaseName','sgNormalizeGameRule','sgExcludesFromText','sgExcludes','nativeIsConfiguredGameExe','nativeValveTargetStillValid','nativeValveReadCurrentTarget'];
const bodies=functions.map(symbol=>extractNative(source,symbol));
const structs=['NativeDetectedGame','NativeGameValveSnapshot'].map(symbol=>extractNative(source,symbol,'struct'));
const policyFiles=['native/game_detection_blacklist_entries.h','native/game_library_blacklist_entries.h','native/monitor_identity.h'];
const policyPins=Object.fromEntries(policyFiles.map(p=>[p,crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex')]));
const nativeRoot=path.join(root,'native').split(path.sep).join('/');
const rulesCpp=String.raw`
#include <atomic>
#include <clocale>
#include <mutex>
#include <sstream>
#include "${nativeRoot}/monitor_identity.h"
${rulesArray[0][0]}
static std::wstring sgSelfBase(){return L"yemancc";}
// Only ASCII is used in the synthetic rule-file inputs.
static std::wstring U2W(const std::string& s){return {s.begin(),s.end()};}
static const std::wstring SG_GAME_SYSTEM_BLACKLIST=L"system",SG_GAME_PLAYER_BLACKLIST=L"player";
static std::string playerRules;
static std::string sgReadGameRulesText(const std::wstring& p){return p==SG_GAME_PLAYER_BLACKLIST?playerRules:std::string{};}
${bodies.slice(0,5).map(b=>b.code).join('\n')}
enum class NativeGameDiscoveryState { Complete, Pending, Unavailable };
${structs.map(b=>b.code).join('\n')}
static NativeGameValveSnapshot g_gameValveTarget;
static std::mutex g_gameValveMx,g_gameRulesMx;
static std::atomic<unsigned long long> g_gameRulesEpoch{1};
static std::vector<std::wstring> explicitWhitelist;
static std::vector<std::wstring> sgGameWhitelist(){return explicitWhitelist;}
static std::vector<std::wstring> nativeConfiguredGameExes(){return {};}
static void nativeRefreshGameRulesEpochFromDiskLocked(){}
static constexpr SIZE_T SG_MIN_WS=500*1024*1024;
static DWORD sessionForTarget=7;
static ULONGLONG actualCreation=0;
static NativeDetectedGame targetIdentity;
namespace fakeOs {
 struct PROCESS_MEMORY_COUNTERS { SIZE_T cb; SIZE_T WorkingSetSize; };
 using PROCESS_MEMORY_COUNTERS_EX=PROCESS_MEMORY_COUNTERS;
 DWORD GetCurrentProcessId(){return 77;}
 BOOL ProcessIdToSessionId(DWORD pid,DWORD* out){*out=pid==77?7:sessionForTarget;return TRUE;}
 HANDLE OpenProcess(DWORD,BOOL,DWORD){assert(false && "unexpected working-set query");return nullptr;}
 BOOL GetProcessMemoryInfo(HANDLE,PROCESS_MEMORY_COUNTERS*,DWORD){return FALSE;}
 BOOL CloseHandle(HANDLE){return TRUE;}
 static bool focusQueryProcessIdentity(DWORD pid,std::wstring* p,ULONGLONG* created){if(pid!=targetIdentity.pid)return false;*p=targetIdentity.path;*created=actualCreation;return true;}
 ${bodies.slice(5).map(b=>b.code).join('\n')}
}
static void publishSyntheticTarget(const wchar_t* path){
 g_gameValveTarget={};
 g_gameValveTarget.game.pid=5732;
 g_gameValveTarget.game.processCreated=134396567340745940ULL;
 g_gameValveTarget.game.name=sgBaseName(path);
 g_gameValveTarget.game.path=path;
 g_gameValveTarget.rulesEpoch=1;
 g_gameValveTarget.generation=9;
 g_gameValveTarget.identityGeneration=8;
 targetIdentity=g_gameValveTarget.game;
 actualCreation=targetIdentity.processCreated;
}
static json storePauseAdmissions(){
 json result=json::array();
 const auto check=[&](const char* name,bool pass){result.push_back({{"name",name},{"pass",pass}});};
 publishSyntheticTarget(L"C:/Program Files/WindowsApps/Microsoft.GamingApp_2701.1001.26.0_x64__8wekyb3d8bbwe/XboxPcApp.Exe");
 check("stock game rules reject the retained Xbox store target",!fakeOs::nativeValveReadCurrentTarget());
 explicitWhitelist={L"xboxpcapp"};
 check("explicit whitelist retains precedence for the same identity",fakeOs::nativeValveReadCurrentTarget());
 ++actualCreation;
 check("an explicit entry cannot admit reused PID with changed birth",!fakeOs::nativeValveReadCurrentTarget());
 explicitWhitelist.clear();
 publishSyntheticTarget(L"C:/Program Files/WindowsApps/Microsoft.GamingApp/XboxPcAppFT.exe");
 check("existing Xbox FT exclusion is retained",!fakeOs::nativeValveReadCurrentTarget());
 publishSyntheticTarget(L"D:/Games/xboxpcapp-adventure.exe");
 NativeDetectedGame matched;unsigned long long generation=0;
 const bool accepted=fakeOs::nativeValveReadCurrentTarget(&matched,&generation);
 check("exact-name rule keeps a similarly named independent game and its identity",accepted&&matched.pid==5732&&matched.processCreated==134396567340745940ULL&&generation==9);
 sessionForTarget=8;
 check("other logon session cannot consume that game lease",!fakeOs::nativeValveReadCurrentTarget());
 sessionForTarget=7;
 playerRules="xboxpcapp-adventure";
 check("existing player exclusion still applies",!fakeOs::nativeValveReadCurrentTarget());
 return result;
}`;
const code=`#include <algorithm>\n#include <string>\n#include <vector>\n#include <iostream>\n#include <cassert>\n#include <fstream>\n#include "${root.replaceAll('\\','/')}/deps/json/json.hpp"\nusing json=nlohmann::json;\n#include "${root.replaceAll('\\','/')}/native/monitor_frame_output.h"\n#include "${root.replaceAll('\\','/')}/native/game_rule_literal.h"\n${matcher}\n${excludes}\n${builtin}\n${rulesCpp}\nint main(int argc,char**argv){assert(argc==2);json checks=json::array();
 const auto denied=[&](const std::wstring&name,bool isWhitelisted,const std::vector<std::wstring>&userExcludes){return ${predicate};};
 for(const auto*name:{L"cl",L"clang",L"clang-cl",L"clang++",L"gcc",L"g++",L"cc1",L"cc1plus",L"link",L"lld",L"lld-link",L"ld",L"rustc",L"msbuild",L"cmake",L"ninja",L"python3",L"python3.13",L"python3.13t",L"python3.14",L"pythonw3.13",L"python",L"pythonw",L"node",L"dotnet"}){assert(denied(name,false,{}));assert(!denied(name,true,{}));}
 checks.push_back("Known build/interpreter processes are rejected by the actual shared predicate; explicit whitelist keeps precedence");
 assert(denied(L"optiscalerclient",false,{}));assert(!denied(L"optiscalerclient",true,{}));assert(!denied(L"optiscaler-adventure",false,{}));
 checks.push_back("Optiscaler Client is a tool; an explicit whitelist and unrelated similarly named games keep the shared policy");
 for(const auto*name:{L"cyberpunk2077",L"ace7game",L"eldenring",L"ff7rebirth",L"my-custom-game",L"clangame",L"pythonquest",L"link-adventure"})assert(!denied(name,false,{}));
 checks.push_back("Actual game stems and similarly prefixed custom names remain candidates");
 assert(denied(L"cyberpunk2077",false,{L"cyberpunk*"}));assert(!denied(L"cyberpunk2077",true,{L"cyberpunk*"}));
 checks.push_back("Existing user exclusions and explicit whitelist behavior stay intact");
 // Reproduce the observed large-build-worker competition using the actual
 // predicate. Working-set ordering is a synthetic model, not a live scanner.
 struct Candidate{const wchar_t*name;unsigned memory;};std::vector<Candidate> candidates{{L"cl",8192},{L"python3.13",4096},{L"cyberpunk2077",2048}};const wchar_t*best=nullptr;unsigned memory=0;for(const auto&v:candidates)if(!denied(v.name,false,{})&&v.memory>memory){best=v.name;memory=v.memory;}assert(best&&std::wstring(best)==L"cyberpunk2077");
 checks.push_back("Observed compiler/interpreter names cannot displace a real game solely through a large working set");
 ymcc::monitorframe::Output frames;const unsigned long long at=1700000000000ULL;
 frames.bind(77,99);frames.hardware(at/1000,120,80,10,30,"cyberpunk2077",at,8.333,1000);assert(!frames.packet(at).is_null());
 for(const auto*name:{L"cl",L"python3.13"}){const bool reject=denied(name,false,{});frames.bind(reject?0:88,reject?0:100);frames.hardware(at/1000,120,80,10,30,"compiler",at,8.333,1000);assert(frames.packet(at).is_null());}
 frames.bind(77,99);frames.hardware(at/1000+1,120,80,10,30,"cyberpunk2077",at+1000,8.333,1000);assert(frames.packet(at+1000)["fps"]==120);
 checks.push_back("Shared exclusion plus actual FPS Output clears compiler-associated data and admits a genuine game afterward");
 const auto sleepTargetAdmission=storePauseAdmissions();
 const bool ok=std::all_of(sleepTargetAdmission.begin(),sleepTargetAdmission.end(),[](const auto& row){return row.at("pass")==true;});
 json result={{"ok",ok},{"checks",checks},{"sleepTargetAdmission",sleepTargetAdmission},{"scope","Extracted shared rules, game selection reader and identity validation; OS process ports and rule storage synthetic."}};
 std::ofstream(argv[1])<<result.dump(2);std::cout<<"Shared game exclusions: "<<checks.size()<<" original cases and "<<sleepTargetAdmission.size()<<" sleep target cases\\n";
 return ok?0:26;
}`;
fs.writeFileSync(path.join(out,'process-exclusions.cpp'),code);
fs.writeFileSync(path.join(out,'build-process-exclusions.cmd'),calibrationToolchainPreamble('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat',{boundInheritedPath:true})+`cl /nologo /std:c++20 /utf-8 /EHsc /W4 /DNOMINMAX /DUNICODE /D_UNICODE "${out}/process-exclusions.cpp" /I"${nativeRoot}" /Fo"${out}/process-exclusions.obj" /Fe"${out}/process-exclusions.exe"\r\nif errorlevel 1 exit /b 1\r\n"${out}/process-exclusions.exe" "${out}/PROCESS-EXCLUSIONS-RESULT.json"\r\nexit /b %errorlevel%\r\n`);
const run=cp.spawnSync(process.env.ComSpec||'cmd.exe',['/d','/c',path.join(out,'build-process-exclusions.cmd')],{encoding:'utf8',windowsHide:true});fs.writeFileSync(path.join(out,'PROCESS-EXCLUSIONS-BUILD.log'),run.stdout+run.stderr);if(run.status!==0)throw Error(run.stdout+run.stderr);
const report=JSON.parse(fs.readFileSync(path.join(out,'PROCESS-EXCLUSIONS-RESULT.json'),'utf8'));assert(report.ok);for(const [file,hash] of Object.entries(policyPins))assert.equal(crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex'),hash,`Policy input changed during compile: ${file}`);
assert.deepEqual(fs.readFileSync(literalRuleFile),literalRuleBytes,'Literal rule dependency changed during native compile/consumer');
fs.writeFileSync(path.join(out,'PROCESS-EXCLUSIONS-PIN.json'),JSON.stringify({generatedAt:new Date().toISOString(),source:'native/main.cpp',policyPins,pauseReaderSources:[...bodies,...structs].map(({symbol,sha256})=>({symbol,sha256})),matcherSha256:crypto.createHash('sha256').update(matcher).digest('hex'),literalRuleHeader:{path:literalRuleFile,sha256:crypto.createHash('sha256').update(literalRuleBytes).digest('hex')},exclusionsSha256:crypto.createHash('sha256').update(builtin).digest('hex')},null,2));
console.log('Shared game exclusions PASS:',report.checks.length,'cases');

