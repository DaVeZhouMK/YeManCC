import fs from'node:fs';import assert from'node:assert/strict';import {extract}from'../fixtures/legion_motion_consumer/extract_main.mjs';
export function gameDetectionPowerSource(root,publicCode){
 const main=fs.readFileSync(root+'/native/main.cpp','utf8'),template=fs.readFileSync(root+'/tools/fixtures/steam_original_recovery_valve_guard/concurrency-cases.cpp.in','utf8'),shell=fs.readFileSync(root+'/tools/boot_modes_fixture/shellOwner.cpp.in','utf8');
 const structure=name=>{const start=main.indexOf('struct '+name+' {'),end=main.indexOf('\n};',start)+3;assert(start>0&&end>start);return main.slice(start,end);};
 const enumeration=name=>{const start=main.indexOf('enum class '+name),end=main.indexOf(';',start)+1;assert(start>0&&end>start);return main.slice(start,end);};
 let prefix=shell.slice(0,shell.indexOf('// ORIGINAL_SHARED_POWER_FUNCTIONS')).replace('// ORIGINAL_STEAM_PATH_LOWER','')+publicCode+'\n#include "monitor_identity.h"\n#include "game_rule_literal.h"\n#include <cstdint>\n#include <cwchar>\n';
 let ports=template.slice(template.indexOf('std::string W2U('),template.indexOf('namespace frozen {')).replace(/using DWORD=.*?\r?\n/,'').replace(/constexpr bool FALSE=.*?\r?\n/,'');
 ports=ports.replace('@@DISCOVERY_TYPES@@',[enumeration('NativeGameDiscoveryState'),enumeration('NativeOnePidResult'),extract(main,'nativeGameDiscoveryStateName').code].join('\n')).replace('@@GAME_STRUCT@@',structure('NativeDetectedGame'));
 const overlayPorts=ports.indexOf('namespace ymcc::sleeppower {struct SharedPowerV1');
 const overlayIf=ports.indexOf('#ifdef YMCC_TEST_OVERLAY',overlayPorts);
 assert(overlayPorts>=0&&overlayIf>overlayPorts,'Expected the private overlay power ports');
 ports=ports.slice(0,overlayPorts)+ports.slice(overlayIf); // Use the real public power functions above.
 ports=ports.replace('Point{None,Rules,FirstIdentity,FinalIdentity,','Point{None,Rules,FirstIdentity,FinalIdentity,Scan,').replace('*path=ports::s.process.path;*born=ports::s.process.processCreated;','if(path)*path=ports::s.process.path;if(born)*born=ports::s.process.processCreated;');
 ports=ports.replace('++ports::s.scans;if(topologyChanged)','++ports::s.scans;ports::hit(ports::Point::Scan);if(topologyChanged)').replace('++ports::s.scans;if(discoveryState)','++ports::s.scans;ports::hit(ports::Point::Scan);if(discoveryState)');
 const scanTail='ULONGLONG*qualificationObservedAt=nullptr){ports::step();';
 assert(ports.includes(scanTail),'Current game discovery fixture must expose the topology deadline port');
 ports=ports.replace(scanTail,'ULONGLONG*qualificationObservedAt=nullptr,ULONGLONG* = nullptr){ports::step();');
 ports+='\nstatic void nativeReconcileGameRulesLocked(bool) { nativeRefreshGameRulesEpochFromDiskLocked(); }\n';
 for(const pattern of [/static unsigned long long fixturePower=7;\r?\n/,/static unsigned long long currentPowerGeneration\(\).*?\r?\n/,/enum class PowerLifecycle .*?\r?\n/,/static std::atomic<PowerLifecycle>g_powerLifecycle.*?\r?\n/,/static std::atomic<bool>g_exitRequested.*?\r?\n/])ports=ports.replace(pattern,'');
 ports=ports.replace('namespace ymcc::gameInventory {static std::atomic<unsigned long long>invalidation{9};}','namespace ymcc::gameInventory {static std::atomic<unsigned long long>invalidation{9};struct ManualScope{explicit ManualScope(bool){}};static void invalidate(){invalidation.fetch_add(1);}}');
 prefix+='\n#define GetCurrentProcessId fixtureGetCurrentProcessId\n#define ProcessIdToSessionId fixtureProcessIdToSessionId\n#define OpenProcess fixtureOpenProcess\n#define CloseHandle fixtureCloseHandle\n#define GetTickCount64 fixtureGetTickCount64\n#define PostMessageW fixturePostMessageW\n';
 const names=['sgBaseName','sgNamePatternMatches','sgNameExcludedBy','nativeValveTargetStillValid','nativeValveCommitLocked','nativeValveTouchLocked','nativeValveClearLocked','nativeGameTargetJson','nativeValveAcquire'];const functions=names.map(n=>extract(main,n));const start=main.indexOf('    ipc_on("game.detect",'),end=main.indexOf('\n    ipc_on("process.identity",',start),handler=main.slice(start,end);assert(start>0&&end>start);
 const globals=structure('NativeGameValveSnapshot')+['','static NativeGameValveSnapshot g_gameValveTarget;static unsigned long long g_gameValveGeneration=0,g_gameNoGameEpoch=1,g_gameValveSettledGeneration=0;static std::mutex g_gameValveMx,g_gameValvePublishMx;static std::atomic<std::shared_ptr<const NativeGameValveSnapshot>>g_gameValveObservation;','template<class F>static void steamOriginalRecoveryBusinessCommit(bool,F&& f){f();}','struct FocusTargetSnapshot{DWORD pid=0;ULONGLONG processCreated=0;std::wstring path,className,monitorDevice;HWND hwnd=nullptr;RECT monitorRect{};bool valid=false,fullscreen=false;};','static unsigned focusRemembered=0;static HWND focusFindWindowForPid(DWORD){return nullptr;}static std::wstring focusWindowClass(HWND){return {};}static void focusMonitorInfo(HMONITOR,std::wstring*,RECT*){}static bool focusWindowLooksFullscreen(HWND,const RECT&){return false;}static void focusLosslessRememberDetectedGame(DWORD,ULONGLONG,const std::wstring&,const FocusTargetSnapshot&){++focusRemembered;}','static std::map<std::string,std::function<json(const json&)>>commands;static void ipc_on(const char* name,std::function<json(const json&)> handler){commands[name]=std::move(handler);}','static json nativeGameTargetJson(const NativeGameValveSnapshot&);',''].join('\n');
 // Keep the real handler and valve, without filesystem or foreground sampling.
 const automaticPort=`
 static const struct AutomaticGameDetectionPort {
  void sample(const ymcc::sleeppower::SharedPowerV1&,NativeGameValveSnapshot* output,const std::function<bool()>& admission) const {
   nativeValveAcquire(0,0,output,admission);
  }
 } automaticGameDetectionPort;
 static const auto& nativeAutomaticGameDetection(){return automaticGameDetectionPort;}
 `;
 const cases=fs.readFileSync(root+'/tools/boot_modes_fixture/gameDetectionPower.cpp.in','utf8');return {source:prefix+ports+globals+extract(main,'nativeValvePublish').code+'\n'+functions.map(r=>r.code).join('\n')+automaticPort+'\nstatic void registerDetection(){\n'+handler+'\n}\n'+cases,handler,functions};
}
