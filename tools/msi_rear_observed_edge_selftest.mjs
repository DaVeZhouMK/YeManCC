// Exercises exact production UI edge delivery with no Windows input or hardware I/O.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';
import { calibrationToolchainPreamble } from './fixtures/hc_reviewed_calibration/toolchain.mjs';

const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
assert.equal(process.argv.length, 3, 'Pass a fresh output directory');
const out = path.resolve(process.argv[2]);
assert(!fs.existsSync(out) && !/["%\r\n]/.test(out + repo), 'Fresh output directory required');
fs.mkdirSync(out, { recursive: true });
const source = fs.readFileSync(path.join(repo, 'native/main.cpp'), 'utf8');
const fn = name => {
  const match = source.match(new RegExp(String.raw`^static [^\n;]*\b${name}\([^;]*?\)\s*\{[\s\S]*?\n\}`, 'm'));
  assert(match, name);
  return match[0];
};
const block = pattern => { const match = source.match(pattern); assert(match, pattern); return match[0]; };
const pump = fn('msiDinputPump');
const rawObservation = pump.match(/^    if \(stateReadAttempted && SUCCEEDED\(stateHr\)\) \{[\s\S]*?^    \}/m)?.[0];
assert(rawObservation?.includes('g_msiRearSourceObservation.dinput('));
assert.equal(pump.match(/g_msiRearSourceObservation\.dinput\(/g)?.length, 1);
assert(pump.indexOf('->GetDeviceState(') < pump.indexOf(rawObservation));
assert(pump.indexOf(rawObservation) < pump.indexOf('msiDinputAxisRangeAdmitsPublish('));
assert(pump.indexOf(rawObservation) < pump.indexOf('inputAdmission.observe('));

const cpp = String.raw`
#include "json.hpp"
#include <atomic>
#include <cstring>
#include <deque>
#include <iostream>
#include <stdexcept>
using json = nlohmann::json;
using BYTE = unsigned char;
using ULONGLONG = uint64_t;
enum class YmccFamily { MsiClaw, Ayanero, Aokzoe, Gpd, OneXPlayer, Rog };
struct Machine { YmccFamily family=YmccFamily::MsiClaw; std::string deviceClass="HandheldCompanion.Devices.ClawA2VM"; } g_machineIdentity;
${block(/^enum class OemProducerId[^\n]+/m)}
${block(/^static constexpr uint64_t kOemBitM1[^\n]+/m)}
constexpr uint64_t extra=1ull<<25;
struct OemProducerTokens { std::array<uint64_t,16> generations{},authorities{}; };
${block(/^struct OemProducerEdgeReceipt \{[\s\S]*?\n\};/m)}
constexpr size_t kOemProducerEdgeCapacity=32, kOemProducerCount=static_cast<size_t>(OemProducerId::Count);
std::array<uint64_t,kOemProducerCount> kOemProducerMasks{};
std::deque<OemProducerEdgeReceipt> queue;
std::atomic<bool> g_exitRequested=false,g_shortcutRecordingActive=false,g_inputReleaseRequired=false;
std::atomic<uint32_t> g_oemShortcutEdgeEpoch=4;
struct Rule {uint64_t requiredMask;std::vector<unsigned> desktopKeys;};
std::vector<Rule> g_nativeKeyboardShortcuts;
json events=json::array();
bool throwOnEmit=false;
unsigned actionEvaluations=0,recordingCalls=0;
int g_curW=0,g_curPad=0;
static json gamepadOemPressedKeys(uint64_t mask){json j=json::array();if(mask&kOemBitM1)j.push_back("m1");if(mask&kOemBitM2)j.push_back("m2");if(mask&extra)j.push_back("claw");return j;}
static void appendNativeLifecycleLog(const char*,const json&){}
static void ipc_emit(const char* name,const json& value){if(throwOnEmit)throw std::runtime_error("presentation unavailable");events.push_back({{"name",name},{"value",value}});}
static bool oemProducerCurrent(OemProducerId,uint64_t generation){return generation==3;}
static bool oemProducerTryPop(OemProducerEdgeReceipt& r){if(queue.empty())return false;r=queue.front();queue.pop_front();return true;}
static int GetTickCount64(){return 0;}
static void modelAdaptationFlush(){}
static void modelAdaptationProducer(unsigned,uint64_t,BYTE){}
static void nativeKeyboardShortcutsRefresh(int){}
static void gamepadRecordShortcutClick(uint64_t){++recordingCalls;}
static int gamepadShortcutRecordingMask(int,int){return 0;}
static void gamepadRecordShortcutChord(uint64_t){++recordingCalls;}
static void nativeOnexX2HomeDispatch(const OemProducerEdgeReceipt&){}
static void nativeRogClickDispatch(const OemProducerEdgeReceipt&){}
static bool customSteamLibraryInputSuppressed(){return false;}
static void nativeKeyboardShortcutsEvaluate(int,int,ULONGLONG,bool,const uint64_t*,bool,uint64_t,const OemProducerTokens*){++actionEvaluations;}
${fn('oemProducerName')}
${fn('nativeOneXOemObserve')}
${fn('nativeOemProducerDrain')}

int main(){
 unsigned checks=0;
 const auto check=[&](bool ok,const char* name){++checks;if(!ok)throw std::runtime_error(name);};
 const auto post=[&](uint64_t mask,uint64_t generation=3,uint32_t epoch=4,bool terminalClick=false){
  OemProducerEdgeReceipt r;r.source=OemProducerId::Msi;r.generation=generation;r.ruleEpoch=epoch;r.terminalClick=terminalClick;r.mask=r.combinedMask=mask;
  queue.push_back(r);nativeOemProducerDrain();
 };
 for(const auto bit:{kOemBitM1,kOemBitM2}){
  events.clear();post(bit|extra);
  check(events.size()==1,"one real DInput observation");
  const auto& data=events[0].at("value");
  check(events[0]["name"]=="oem.keys.observed"&&data["familyId"]=="msi-claw"&&data["deviceClass"]==g_machineIdentity.deviceClass&&data["route"]=="msi-dinput","exact MSI identity and route");
  check(data["observedKeys"]==json::array({bit==kOemBitM1?"m1":"m2"})&&data["actionExecutionProven"]==false,"no invented rear key, dedicated key or action");
 }
 events.clear();post(0);post(extra);post(kOemBitM1,2);post(kOemBitM1,3,3);post(kOemBitM1,3,4,true);
 check(events.empty(),"neutral, non-rear, stale source, stale rule and terminal click rejected");
 nativeOneXOemObserve(kOemBitM1,"keyboard-chord");check(events.empty(),"keyboard does not become DInput evidence");
 g_exitRequested=true;post(kOemBitM1);g_exitRequested=false;check(events.empty(),"exit rejects delivery");
 g_machineIdentity.family=YmccFamily::Rog;post(kOemBitM1);g_machineIdentity.family=YmccFamily::MsiClaw;check(events.empty(),"not a ROG source");
 throwOnEmit=true;const auto actionsBefore=actionEvaluations;post(kOemBitM1);check(actionEvaluations==actionsBefore+1,"presentation failure does not bypass evaluation");
 g_shortcutRecordingActive=true;post(kOemBitM1);check(recordingCalls==1&&actionEvaluations==actionsBefore+1,"presentation failure does not bypass recording or execute action");
 std::cout<<"MSI_REAR_OBSERVED_EDGE_OK checks="<<checks<<" hardware_io=none\n";
}
`;
const cppFile = path.join(out, 'probe.cpp');
const exe = path.join(out, 'probe.exe');
const buildFile = path.join(out, 'build.cmd');
fs.writeFileSync(cppFile, cpp);
const vcvars = 'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat';
fs.writeFileSync(buildFile, calibrationToolchainPreamble(vcvars, {boundInheritedPath:true}) +
  `cl /nologo /std:c++20 /utf-8 /EHsc /MT /I"${path.join(repo, 'deps/json')}" "${cppFile}" /Fe"${exe}"\r\nexit /b %errorlevel%\r\n`);
for (const [file, command, args] of [['BUILD.log','cmd.exe',['/d','/c',buildFile]], ['RUN.log',exe,[]]]) {
  const run = spawnSync(command, args, {cwd:out, encoding:'utf8', windowsHide:true, timeout:60000});
  const output = (run.stdout || '') + (run.stderr || '');
  fs.writeFileSync(path.join(out, file), output);
  assert.equal(run.status, 0, output || run.error?.message);
  if (file === 'RUN.log') console.log(output.trim());
}
