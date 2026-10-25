// Sidecar: extract actual crash-recovery control functions and run portable fake I/O.
// Does not include main.cpp, start the product/Host, or call device/registry/service APIs.
// All writes are new fixture artifacts under the explicitly selected output directory.
// Owned-return observer/command outcomes are controlled sidecar simulations. This
// verifier covers its legacy load/migration/identity/owned-configuration/WAL and
// retirement contracts. It does not verify two stable signal windows, observer
// policy/retry integration, physical input, Steam consumption, or product startup.
// The production observer chain is covered by verify-msi-startup-owned-return-runtime.mjs.
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const cp = require('child_process');
const repo = path.resolve(__dirname, '..');
const args = process.argv.slice(2);
const outputAt = args.indexOf('--output');
if (outputAt < 0 || !args[outputAt + 1]) throw Error('usage: node tools/verify-msi-crash-recovery-runtime.cjs --output NEW_DIRECTORY [--generate-only]');
const out = path.resolve(args[outputAt + 1]);
if (fs.existsSync(out)) throw Error('Refusing to overwrite an existing fixture directory: ' + out);
fs.mkdirSync(out, {recursive: true});
const sha = b => crypto.createHash('sha256').update(b).digest('hex');
function endBrace(t, start) {
  let depth = 0, state = 'code';
  for (let i = start; i < t.length; i++) {
    const c = t[i], n = t[i + 1];
    if (state === 'line') { if (c === '\n') state = 'code'; continue; }
    if (state === 'block') { if (c === '*' && n === '/') { state = 'code'; i++; } continue; }
    if (state === 'string' || state === 'char') {
      if (c === '\\') { i++; continue; }
      if (c === (state === 'string' ? '"' : "'")) state = 'code';
      continue;
    }
    if (c === '/' && n === '/') { state = 'line'; i++; continue; }
    if (c === '/' && n === '*') { state = 'block'; i++; continue; }
    if (c === '"' || c === "'") { state = c === '"' ? 'string' : 'char'; continue; }
    if (c === '{') depth++;
    else if (c === '}' && --depth === 0) return i + 1;
  }
  throw Error('Unclosed actual function');
}
function extract(text, name) {
  const re = new RegExp('^static[^\\n;{]*\\b' + name + '\\s*\\(', 'mg');
  for (let m; (m = re.exec(text));) {
    const b = text.indexOf('{', m.index), semi = text.indexOf(';', m.index);
    if (b < 0 || (semi >= 0 && semi < b)) continue;
    const body = text.slice(m.index, endBrace(text, b));
    return {name, line: text.slice(0, m.index).split('\n').length, sha256: sha(body), body};
  }
  throw Error('Missing actual definition: ' + name);
}
const runtimePath = path.join(repo, 'native/msi_crash_recovery_runtime.h');
const journalPath = path.join(repo, 'native/msi_crash_recovery_journal.h');
const runtimeBytes = fs.readFileSync(runtimePath), journalBytes = fs.readFileSync(journalPath);
const runtime = runtimeBytes.toString('utf8'), journal = journalBytes.toString('utf8');
const names = ['msiCrashHasDuty', 'msiCrashNote', 'msiCrashDecision', 'msiCrashLegacyRecord', 'msiCrashStoreLocked', 'msiCrashLoadLocked', 'msiCrashLocations', 'msiCrashPhysicalXinputSnapshotEligible', 'msiCrashPrepareOwner',
  'msiCrashRecoveryBeforeModeWrite', 'msiCrashRecoveryBeforeHide', 'msiCrashRecoveryAfterOwnedRestore',
  'msiCrashRecoveryAfterModeDischarge', 'msiCrashRecoveryInitialize', 'msiCrashResolveDinputIdentity', 'msiCrashRetireStartupJournal', 'msiCrashAdoptStartupModeDuty', 'msiCrashRecoveryMaintenance'];
const functions = names.map(n => extract(runtime, n));
const mainPath=path.join(repo,'native/main.cpp');
const mainSource=fs.readFileSync(mainPath,'utf8');
const gateFunctions=['msiMode1TerminalReason','msiMode1WritebackVerdict','msiDinputDeviceTargetAbsent'].map(n=>extract(mainSource,n));
const verdictStart=mainSource.indexOf('struct MsiMode1WritebackVerdict {');
if(verdictStart<0)throw Error('Missing existing verdict type');
const verdictBody=mainSource.slice(verdictStart,endBrace(mainSource,mainSource.indexOf('{',verdictStart))+1);
const verdictType={name:'MsiMode1WritebackVerdict',line:mainSource.slice(0,verdictStart).split('\n').length,sha256:sha(verdictBody),body:verdictBody};
const gateActual=verdictBody+'\n'+gateFunctions.map(f=>f.body).join('\n\n');
const resolverFunctions=['hidHideDeviceInstanceId','hidHideResolveInterfaceIdentity'].map(n=>extract(mainSource,n));
const resolverActual=resolverFunctions.map(f=>f.body).join('\n\n');
const endpointFunction=extract(mainSource,'hidHideVirtualEndpointAbsent'),restoreFunction=extract(mainSource,'hidHideRestorePhysicalTransaction');
const predicateStart=restoreFunction.body.indexOf('const bool msiOwnedReturn='),predicateEnd=restoreFunction.body.indexOf('json restoreResultRow',predicateStart),tailStart=restoreFunction.body.indexOf('msiCrashRecoveryAfterOwnedRestore(restoreAfter,restoreReadback);');
if(predicateStart<0||predicateEnd<0||tailStart<0)throw Error('Actual restore predicate/tail anchor changed');
const restorePredicate=restoreFunction.body.slice(predicateStart,predicateEnd).trim(),restoreTail=restoreFunction.body.slice(tailStart,restoreFunction.body.lastIndexOf('}')).trim();
const startFunction=extract(mainSource,'inputHostStart'),startGateOffset=startFunction.body.indexOf('if (g_inputHostReleaseQuarantined ||');
if(startGateOffset<0)throw Error('Actual start visibility gate anchor changed');
const startGateBody=startFunction.body.slice(startGateOffset,endBrace(startFunction.body,startFunction.body.indexOf('{',startGateOffset)));
const restoreActual=endpointFunction.body+'\nstatic bool fixtureActualRestoreResult(Journal& journal,const HidHideVisibilitySnapshot& restoreAfter,bool restoreReadback,bool virtualEndpointAbsent,int restoreTargetCount,int restoreTargetsChecked,int restoreRemaining,int restoreMissingIdentity,int restoreUnknown){\n'+restorePredicate+'\nreturn restored;\n}\n';
const restoreBinding={path:mainPath,productionFunction:{name:restoreFunction.name,line:restoreFunction.line,sha256:restoreFunction.sha256},predicate:{sha256:sha(restorePredicate),body:restorePredicate},tail:{sha256:sha(restoreTail),body:restoreTail},endpointFunction:{name:endpointFunction.name,line:endpointFunction.line,sha256:endpointFunction.sha256},startGate:{productionFunction:startFunction.name,line:startFunction.line+startFunction.body.slice(0,startGateOffset).split('\n').length-1,sha256:sha(startGateBody),body:startGateBody},scope:'actual production predicate/tail and start visibility gate; restore transport/readback and applications are fake; no product start'};
fs.writeFileSync(path.join(out,'actual-restore-predicate-tail.inc'),restoreActual+'\n// Actual restore tail\n'+restoreTail+'\n// Actual startup visibility gate\n'+startGateBody);
const boundarySnapshotPath='G:/YeManCC-Work/Build/Validation/G3E-141537-startup-off-failure-20261014/main.cpp.before-recovery-resolver';
const boundaryBytes=fs.existsSync(boundarySnapshotPath)?fs.readFileSync(boundarySnapshotPath):null;
const resolverBoundaryMatches=boundaryBytes?resolverFunctions.every(f=>extract(boundaryBytes.toString('utf8'),f.name).sha256===f.sha256):null;
if(resolverBoundaryMatches===false)throw Error('Resolver changed from the supplied main boundary snapshot');
fs.writeFileSync(path.join(out,'actual-interface-resolver.inc'),resolverActual);
const maintenance=functions.find(f=>f.name==='msiCrashRecoveryMaintenance');
const helperCall='if(!msiCrashResolveDinputIdentity(source,physical))return;';
if(!maintenance.body.includes(helperCall))throw Error('Actual maintenance does not call the new helper');
const legacyMaintenance=maintenance.body.replace('msiCrashRecoveryMaintenance(','msiCrashRecoveryMaintenanceLegacyCall(')
  .replace(helperCall,'if(!hidHideResolveInterfaceIdentity(U2W(source.interfacePath),nullptr,INVALID_HANDLE_VALUE,physical))return;');
fs.writeFileSync(path.join(out,'explicit-old-call-mutation.inc'),legacyMaintenance);
const retirementHelper=functions.find(f=>f.name==='msiCrashRetireStartupJournal');
if(!/^static bool\s+msiCrashRetireStartupJournal/.test(retirementHelper.body))throw Error('Retirement helper return contract changed');
if(!maintenance.body.includes('msiCrashRetireStartupJournal('))throw Error('Actual maintenance omits retirement helper');
const legacyRetirementMaintenance=maintenance.body.replace('msiCrashRecoveryMaintenance(','msiCrashRecoveryMaintenanceBeforeRetirement(').replaceAll('msiCrashRetireStartupJournal(','fixtureRetirementDisabled(');
fs.writeFileSync(path.join(out,'explicit-pre-retirement-maintenance.inc'),legacyRetirementMaintenance);
fs.writeFileSync(path.join(out,'existing-mode1-gate.inc'),gateActual);
const globalStart = runtime.indexOf('namespace msiRecovery =');
const globalEnd = runtime.indexOf('static std::wstring msiCrashRecoveryPath');
if (globalStart < 0 || globalEnd <= globalStart) throw Error('Actual globals anchor changed');
const globals = runtime.slice(globalStart, globalEnd);
// This dependency adapter exposes the runtime's expected enum name. It delegates
// decoding/validation/encoding to the captured REAL pure journal, changing no
// extracted runtime body. The adapter is separately disclosed, never evidence
// that the current production pair compiles without a fix.
const decodeApiMismatch = /msiRecovery::ReadState::Record/.test(runtime) && !/\b(?:enum\s+class|struct)\s+ReadState\b|using\s+ReadState\s*=/.test(journal);
const adapter = decodeApiMismatch ? 'namespace ymcc::msi::recovery { struct ReadState { static constexpr DecodeState Record = DecodeState::Valid; }; }\n' : '';
fs.writeFileSync(path.join(out, 'runtime-snapshot.h'), runtimeBytes);
fs.writeFileSync(path.join(out, 'msi_crash_recovery_journal.h'), journalBytes);
fs.copyFileSync(path.join(repo, 'deps/json/json.hpp'), path.join(out, 'json.hpp'));
const archiveAt = args.indexOf('--archive');
const archiveFiles = {}, archiveBindings = [];
let archiveBoardKey = '';
let archiveFixtureRows = {};
if (archiveAt >= 0) {
  const archiveRoot = path.resolve(args[archiveAt + 1]);
  for (const name of ['native-lifecycle.log.1','native-lifecycle.log','native-detail.log.1','native-detail.log','msi-controller-visibility.log.1','msi-controller-visibility.log']) {
    const file = path.join(archiveRoot,name);
    if (!fs.existsSync(file)) continue;
    const bytes = fs.readFileSync(file); archiveFiles[name] = bytes.toString('utf8');
    archiveBindings.push({path:file,bytes:bytes.length,sha256:sha(bytes)});
  }
  for (const line of (archiveFiles['native-lifecycle.log'] || '').split(/\r?\n/)) {
    try { const row=JSON.parse(line); if(row.event==='machine-identity' && row.pid===9956) {
      archiveBoardKey=[row.manufacturer,row.product,row.systemProduct,row.deviceClass].join('|');break;
    }} catch {}
  }
  if(!archiveBoardKey)throw Error('Required 9956 board fixture not found in the allowed archive');
  // Select original data rows solely as fixture inputs. Migration decisions stay
  // inside the extracted actual function, never in this JS data preparation.
  const lifecycleRows=(archiveFiles['native-lifecycle.log']||'').split(/\r?\n/).flatMap(line=>{try{return [JSON.parse(line)];}catch{return [];}});
  const samePid=lifecycleRows.filter(row=>row.pid===9956);
  const admission=samePid.filter(row=>row.event==='hidhide.p-hid-admitted').at(-1);
  const location=samePid.find(row=>row.event==='msi-foreground-isolation' &&
    row.sourceRoot===admission?.sources?.[0]?.baseContainerDeviceInstanceId && row.physicalLocationPaths?.length===2);
  const board=samePid.find(row=>row.event==='machine-identity');
  const restore=samePid.find(row=>row.event==='hidhide.p-hid-restore'&&row.ownedTargetsReturned===true);
  const detailRows=(archiveFiles['native-detail.log']||'').split(/\r?\n/).flatMap(line=>{try{return [JSON.parse(line)];}catch{return [];}});
  const discharge=detailRows.find(row=>row.pid===9956&&row.event==='msi-claw-return-debt'&&row.action==='discharged');
  if(!admission||!location||!board||!restore||!discharge)throw Error('Required exact migration boundary data rows missing');
  archiveFixtureRows={board,admission,location,restore,discharge};
}
const startupAt=args.indexOf('--startup-archive');
let startupFixture=null;
if(startupAt>=0){
  if(!args[startupAt+1])throw Error('Missing --startup-archive directory');
  const startupRoot=path.resolve(args[startupAt+1]);
  const captured={};
  for(const name of ['native-lifecycle.log','YeManCC-export-manifest.txt']){
    const file=path.join(startupRoot,name),bytes=fs.readFileSync(file);captured[name]=bytes.toString('utf8');
    archiveBindings.push({path:file,bytes:bytes.length,sha256:sha(bytes),scope:'141537 startup observations; not durable journal bytes'});
  }
  const rows=captured['native-lifecycle.log'].split(/\r?\n/).flatMap((line,i)=>{try{return [{line:i+1,row:JSON.parse(line)}];}catch{return [];}});
  const loaded=rows.find(x=>x.row.event==='msi-crash-return'&&x.row.phase==='loaded'&&x.row.time.startsWith('2026-10-14T14:14:'));
  if(!loaded)throw Error('141537 loaded WAL observation missing');
  const pid=loaded.row.pid,board=rows.find(x=>x.row.event==='machine-identity'&&x.row.pid===pid);
  const off=rows.find(x=>x.row.event==='startup-input-state'&&x.row.pid===pid&&x.row.state==='disabled');
  const source=rows.find(x=>x.row.event==='msi-main-source-record'&&x.row.pid===pid&&x.row.phase==='published'&&x.row.productId===0x1902);
  if(!board||!off||!source||!loaded.row.modeReturnPending||!loaded.row.ownedHiddenPaths.length)throw Error('141537 OFF/inherited/source fixture incomplete');
  const manifest=captured['YeManCC-export-manifest.txt'];
  startupFixture={loaded,board,off,source,boardKey:[board.row.manufacturer,board.row.product,board.row.systemProduct,board.row.deviceClass].join('|'),
    exportedAtLocal:manifest.match(/^exportedAtLocal=(.+)$/m)?.[1]?.trim(),nativeSha256:manifest.match(/^nativeSha256=(.+)$/m)?.[1]?.trim(),
    scope:'actual archive row metadata; reconstructed journal using real codec; liveness, current freshness, locations and I/O are fake; no captured durable bytes'};
}
const retirementAt=args.indexOf('--startup-retirement-archive');let retirementFixture=null;
if(retirementAt>=0){
 if(!args[retirementAt+1])throw Error('Missing --startup-retirement-archive directory');const root=path.resolve(args[retirementAt+1]),captured={};
 for(const name of ['native-lifecycle.log','YeManCC-export-manifest.txt']){const file=path.join(root,name),bytes=fs.readFileSync(file);captured[name]=bytes.toString('utf8');archiveBindings.push({path:file,bytes:bytes.length,sha256:sha(bytes),scope:'150048 PID9792 observations; not durable journal bytes'});}
 const rows=captured['native-lifecycle.log'].split(/\r?\n/).flatMap((line,i)=>{try{return [{line:i+1,row:JSON.parse(line)}];}catch{return [];}}),same=rows.filter(x=>x.row.pid===9792);
 const loaded=same.find(x=>x.row.event==='msi-crash-return'&&x.row.phase==='loaded'),off=same.find(x=>x.row.event==='startup-input-state'&&x.row.state==='disabled'),source=same.find(x=>x.row.event==='msi-main-source-record'&&x.row.phase==='published'&&x.row.productId===0x1902),board=same.find(x=>x.row.event==='machine-identity');
 const restore=same.find(x=>x.row.event==='hidhide.p-hid-restore'&&x.row.reason==='msi-startup-owned-return'&&x.row.ownedTargetsReturned&&x.row.virtualEndpointAbsent===false),settled=same.find(x=>x.row.event==='msi-crash-return'&&x.row.phase==='record-committed'&&!x.row.modeReturnPending&&x.row.ownedHiddenPaths?.length===0),blocked=same.filter(x=>x.row.event==='input-host-start-blocked'&&x.row.reason==='visibility-transaction-active');
 if(!loaded||!off||!source||!board||!restore||!settled||blocked.length!==12)throw Error('150048 exact PID9792 partial recovery evidence missing');
 retirementFixture={loaded,off,source,board,restore,settled,blocked,boardKey:[board.row.manufacturer,board.row.product,board.row.systemProduct,board.row.deviceClass].join('|'),nativeSha256:captured['YeManCC-export-manifest.txt'].match(/^nativeSha256=(.+)$/m)?.[1]?.trim(),scope:'actual PID9792 metadata and 12 denials; reconstructed journal with real codec; locations, owner death, fresh source, storage, driver and mode I/O fake'};
}
if(args.includes('--startup-retirement-only')&&!retirementFixture)throw Error('--startup-retirement-only requires --startup-retirement-archive');
fs.writeFileSync(path.join(out,'archive-input.json'),JSON.stringify({files:archiveFiles,boardKey:archiveBoardKey,fixtureRows:archiveFixtureRows,startupFixture,retirementFixture,retirementOnly:args.includes('--startup-retirement-only')}));
fs.writeFileSync(path.join(out,'archive-source-binding.json'),JSON.stringify({readOnly:true,files:archiveBindings},null,2));
const prelude = String.raw`
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <cstddef>
#include <functional>
#include <fstream>
#include <sstream>
#include <iostream>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>
#include "msi_crash_recovery_journal.h"
using json = nlohmann::json;
using DWORD = std::uint32_t; using BYTE = std::uint8_t; using ULONGLONG = std::uint64_t;
using HANDLE = std::uintptr_t; using HDEVINFO=HANDLE; using BOOL=int; using USHORT=std::uint16_t; using CONFIGRET=DWORD;
constexpr BOOL FALSE=0,TRUE=1;constexpr CONFIGRET CR_SUCCESS=0;
constexpr DWORD FILE_SHARE_READ=1,FILE_SHARE_WRITE=2,OPEN_EXISTING=3,FILE_ATTRIBUTE_NORMAL=128;
constexpr DWORD DIGCF_PRESENT=2,DIGCF_DEVICEINTERFACE=16,ERROR_NO_MORE_ITEMS=259,ERROR_INSUFFICIENT_BUFFER=122,ERROR_INVALID_HANDLE=6,ERROR_INVALID_DATA=13;
struct GUID {DWORD token;};constexpr GUID kYeManGuidDeviceInterfaceHid{1};
struct SP_DEVINFO_DATA {DWORD cbSize=0;GUID ClassGuid{};DWORD DevInst=0;std::uintptr_t Reserved=0;};
struct SP_DEVICE_INTERFACE_DATA {DWORD cbSize=0;GUID InterfaceClassGuid{};DWORD Flags=0;std::uintptr_t Reserved=0;};
struct SP_DEVICE_INTERFACE_DETAIL_DATA_W {DWORD cbSize;wchar_t DevicePath[1];};
struct HIDD_ATTRIBUTES {DWORD Size=0;USHORT VendorID=0,ProductID=0,VersionNumber=0;};
struct HIDP_CAPS {USHORT UsagePage=0,Usage=0;};
struct FakePreparsed {HANDLE handle;};using PHIDP_PREPARSED_DATA=FakePreparsed*;constexpr int HIDP_STATUS_SUCCESS=1;
enum class HidResolveReject {None,OpenFailed,ProbeFailed,VirtualMarker,NotGamepad,NoBaseContainer};
enum class HidBaseWalk {Complete,Incomplete};
constexpr HANDLE INVALID_HANDLE_VALUE = static_cast<HANDLE>(-1);
constexpr DWORD INVALID_FILE_ATTRIBUTES = static_cast<DWORD>(-1);
constexpr DWORD ERROR_SUCCESS=0, ERROR_FILE_NOT_FOUND = 2, ERROR_PATH_NOT_FOUND = 3;
enum class PowerLifecycle { Ready, Suspended };
struct HidHideVisibilitySnapshot { bool valid = true, cloaking = true; std::vector<std::wstring> hidden, applications; };
struct HidHidePhysicalIdentity { bool resolved = false; unsigned vendorId = 0x0db0, productId = 0x1902; std::string baseContainerDeviceInstanceId, deviceInstanceId, sourcePath, baseContainerDevicePath, physicalIdentity; USHORT usagePage=0,usage=0; };
struct HidHideMsiMainSourceRecord { bool valid = true; unsigned generation = 3, vendorId = 0x0db0, productId = 0x1902; std::string usbInstance, interfacePath; };
struct MsiClawHidDiag { std::string interfacePath; };
struct MsiClawCommandOutcome { struct { bool ok=false; } write; };
struct Journal { bool active = false,applicationAdded=false,cloakChanged=false,ownedApplicationResponsibilityUnknown=false; std::vector<std::wstring> ownedApplications; HidHideVisibilitySnapshot before{false,false,{},{}}; std::string transactionId; HidHidePhysicalIdentity physical; std::vector<HidHidePhysicalIdentity> physicalSources; std::vector<std::wstring> hiddenByUs; };
struct Target { bool virtualTargetEnabled = false; };
std::string W2U(const std::wstring& s) { return {s.begin(), s.end()}; }
std::wstring U2W(const std::string& s) { return {s.begin(), s.end()}; }
std::string ascii_lower(std::string value) {for(auto& c:value)if(c>='A'&&c<='Z')c=static_cast<char>(c-'A'+'a');return value;}
std::string hidHideUpper(std::string value) { for(auto& c:value)if(c>='a'&&c<='z')c=static_cast<char>(c-'a'+'A');return value; }
bool hidHideContainsPath(const std::vector<std::wstring>& paths, const std::string& s) {
  for (const auto& p : paths) if (ymcc::msi::recovery::detail::equalAscii(W2U(p), s)) return true;
  return false;
}
struct InputCaptureGamepadSnapshot {bool live=false;DWORD primarySlot=0,connectedMask=0;std::string inputSource="none";unsigned long long readAttemptSequence=0,readPowerGeneration=0;unsigned readTopologyGeneration=0;bool readContextStable=false;};
InputCaptureGamepadSnapshot g_inputCaptureGamepadSnapshot;
std::mutex g_inputCaptureGamepadSnapshotMx;
std::mutex g_inputHostMx, g_msiPersonaPumpMx;
Target g_realStickTest; HANDLE g_inputHostProcess = 0; bool g_inputHostPrepared = false;
std::atomic<PowerLifecycle> g_powerLifecycle{PowerLifecycle::Ready};
std::atomic<bool> g_msiDinputSourceValid{true}, g_msiDinputRouteDinputArmed{false},
  g_msiDinputRouteConfirmed{false}, g_msiDinputReturnOwed{false}, g_msiMode1TerminalAttempted{false};
Journal g_hidHideVisibilityJournal;bool g_inputHostReleaseQuarantined=false;std::atomic<bool> g_wakeHoldPhysicalUnhide{false};DWORD g_xinputConnectedMask=0;
std::atomic<std::uint64_t> g_msiFeedbackSamplePower{2}, g_msiFeedbackSampleMode{3}, g_msiFeedbackSampleTopology{4}, g_msiFeedbackModeGeneration{3};std::atomic<unsigned> g_xinputTopologyEpoch{4};
std::atomic<std::uint64_t> g_msiOwnedReturnSourceEvent{0};
std::atomic<std::uint64_t> g_msiDinputReturnDebtEpisode{0};
std::atomic<bool> g_msiNativeForegroundCyclePending{false},g_msiNativeForegroundPowerHold{false};
std::uint64_t fakePower = 2, fakeEpoch = 1;
struct FakeDinputState { void* pad=nullptr; std::atomic<bool> mainInterfaceMatched{false}; std::atomic<unsigned> productId{0}; } g_msiDinput;
bool g_xinputPhysicalSourceLocked = false; DWORD g_xinputPhysicalSourceMask = 0;
std::uint64_t currentPowerGeneration() { return fakePower; }
std::uint64_t inputHostEpochToken() { return fakeEpoch; }
namespace fake {
  constexpr const char* root = "USB\\VID_0DB0&PID_1902\\FAKEDEVICE";
  constexpr const char* owned = "HID\\VID_0DB0&PID_1902&MI_00&COL01\\FAKEGAME";
  constexpr const char* foreign = "HID\\OTHER-OWNER\\KEEP";
  struct Cut : std::exception {};
  bool knownBoard = true, seam = false, ownerDead = true, sourcePresent = true,
    resolveOk = true, exitFence = false, hardwareGate = true, storePresent = false,
    failCommit = false, failRead = false, cutBeforeCommit = false, cutAfterCommit = false;
  DWORD pid = 101; std::string creation = "2000", storage;
  unsigned commits = 0, reads = 0, restores = 0, unhideWrites = 0, modeWrites = 0, mode2Writes = 0;
  bool unhideOk = true, modeOk = false, visibilityReadOk = true;
  unsigned snapshotReads = 0;
  // These counters describe a fake observer boundary, never real signal proof.
  unsigned observerInitials=0,observerCompletions=0;
  std::uint64_t observerEpisode=0;
  bool observerSawDurableOwner=false,borrowBusy=false;
  HidHideMsiMainSourceRecord source;
  std::vector<std::wstring> hidden;
  std::vector<json> logs;
  std::function<void()> resolveHook, afterCommit,snapshotHook;bool snapshotValid=true;std::vector<std::wstring> applications;
  std::map<std::string,std::string> archiveFiles;
  std::string boardKey = "MICRO-STAR|MS-1T91|FAKE-SYSTEM|ClawCG3EM";
  std::vector<std::wstring> locations{L"PCIROOT(0)#PCI(1400)#USBROOT(0)#USB(4)"};
  std::vector<HidHidePhysicalIdentity> additionalSources;
  struct Interface {std::string path,root,instance;USHORT vid=0x0db0,pid=0x1902;DWORD devInst=0;};
  struct DeviceSet {std::vector<Interface> rows;};
  std::map<HDEVINFO,DeviceSet> sets;std::map<HANDLE,Interface> handles;std::vector<Interface> interfaces;
  HDEVINFO nextSet=100;HANDLE nextHandle=100000;DWORD lastError=ERROR_FILE_NOT_FOUND;
  bool setupOpenFail=false,detailFillFail=false,invalidDetailData=false;
  int enumErrorAt=-1;long long detailSizeOverride=-1;
  unsigned setCreates=0,setDestroys=0,detailFills=0,invalidSetUses=0,invalidDataUses=0,instanceQueries=0,metadataOpens=0;
  void resetSetup(){sets.clear();handles.clear();interfaces.clear();setupOpenFail=detailFillFail=invalidDetailData=false;
    enumErrorAt=-1;detailSizeOverride=-1;setCreates=setDestroys=detailFills=invalidSetUses=invalidDataUses=instanceQueries=metadataOpens=0;lastError=ERROR_FILE_NOT_FOUND;}
}
bool msiClawKnownBoardAuthority() { return fake::knownBoard; }
bool ymStartSeamActive() { return fake::seam; }
bool hardwareWriteAllowed() { return fake::hardwareGate && g_powerLifecycle == PowerLifecycle::Ready; }
bool msiForegroundIsolationBorrowBusy() { return fake::borrowBusy; }
bool inputHostExitFenceRaised() { return fake::exitFence; }
std::string inputHostConfiguredPersona() { return g_realStickTest.virtualTargetEnabled ? "dualshock4" : "disabled"; }
DWORD GetCurrentProcessId() { return fake::pid; }
HANDLE GetCurrentProcess() { return 1; }
std::string msiCrashProcessCreation(HANDLE) { return fake::creation; }
std::string msiCrashCurrentOwner() { return std::to_string(fake::pid) + ":" + fake::creation; }
std::string msiCrashBoardKey() { return fake::boardKey; }
std::wstring app_data_dir() { return L"FAKE-ARCHIVE-DIR"; }
std::wstring msiCrashRecoveryPath() { return L"FAKE-STORAGE-ONLY"; }
bool msiCrashOldOwnerDead(const ymcc::msi::recovery::Record&) { return fake::ownerDead; }
DWORD GetFileAttributesW(const wchar_t*) {fake::lastError=fake::storePresent?0:ERROR_FILE_NOT_FOUND;return fake::storePresent?0:INVALID_FILE_ATTRIBUTES;}
DWORD GetLastError() {return fake::lastError;}
bool msiCrashReadBounded(const std::wstring& path, std::size_t limit, std::string& text) {
  ++fake::reads; if (fake::failRead) return false;
  if (path == msiCrashRecoveryPath()) {
    if (!fake::storePresent || fake::storage.size() > limit) return false; text = fake::storage; return true;
  }
  const auto name = W2U(path); const auto at = name.find_last_of("\\/");
  const auto found = fake::archiveFiles.find(name.substr(at == std::string::npos ? 0 : at + 1));
  if (found == fake::archiveFiles.end() || found->second.size() > limit) return false;
  text = found->second; return true;
}
bool sgWriteFileAtomic(const std::wstring&, const std::string& bytes) {
  if (fake::cutBeforeCommit) { fake::cutBeforeCommit = false; throw fake::Cut{}; }
  if (fake::failCommit) return false;
  fake::storage = bytes; fake::storePresent = true; ++fake::commits;
  if (fake::afterCommit) fake::afterCommit();
  if (fake::cutAfterCommit) { fake::cutAfterCommit = false; throw fake::Cut{}; }
  return true;
}
void appendNativeLifecycleLog(const char* event, const json& row) {
  auto value=row; value["event"]=event; fake::logs.push_back(std::move(value));
}
std::vector<std::wstring> msiForegroundIsolationLocations(const std::string&) { return fake::locations; }
bool deviceInterfaceUsbInstance(const wchar_t*, std::wstring& root) {
  if(!fake::resolveOk)return false; root=U2W(fake::source.usbInstance);return true;
}
bool hidHideGetPhysicalIdentities(std::vector<HidHidePhysicalIdentity>& out) {
  HidHidePhysicalIdentity p; p.baseContainerDeviceInstanceId=fake::source.usbInstance;
  p.productId=fake::source.productId;p.deviceInstanceId=fake::owned;p.sourcePath=fake::source.interfacePath;
  out={p};out.insert(out.end(),fake::additionalSources.begin(),fake::additionalSources.end());return true;
}
bool msiDinputMainSourceRecordSnapshot(HidHideMsiMainSourceRecord& out) {
  if (!fake::sourcePresent) return false; out = fake::source; return true;
}
bool fakeValidSet(HDEVINFO set){if(fake::sets.contains(set))return true;++fake::invalidSetUses;fake::lastError=ERROR_INVALID_HANDLE;return false;}
HDEVINFO SetupDiGetClassDevsW(const GUID* guid,const wchar_t*,void*,DWORD flags){
  if(fake::setupOpenFail||guid!=&kYeManGuidDeviceInterfaceHid||flags!=(DIGCF_PRESENT|DIGCF_DEVICEINTERFACE)){fake::lastError=5;return INVALID_HANDLE_VALUE;}
  const auto set=++fake::nextSet;fake::DeviceSet value;value.rows=fake::interfaces;
  if(value.rows.empty())value.rows.push_back({fake::source.interfacePath,fake::source.usbInstance,fake::owned,static_cast<USHORT>(fake::source.vendorId),static_cast<USHORT>(fake::source.productId)});
  for(std::size_t i=0;i<value.rows.size();++i)value.rows[i].devInst=static_cast<DWORD>(set*1024+i+1);
  fake::sets.emplace(set,std::move(value));++fake::setCreates;return set;
}
BOOL SetupDiDestroyDeviceInfoList(HDEVINFO set){if(!fakeValidSet(set))return FALSE;fake::sets.erase(set);++fake::setDestroys;return TRUE;}
BOOL SetupDiEnumDeviceInterfaces(HDEVINFO set,SP_DEVINFO_DATA*,const GUID* guid,DWORD index,SP_DEVICE_INTERFACE_DATA* out){
  if(!fakeValidSet(set))return FALSE;
  if(guid!=&kYeManGuidDeviceInterfaceHid||!out||out->cbSize!=sizeof(*out)){fake::lastError=ERROR_INVALID_DATA;return FALSE;}
  if(fake::enumErrorAt==static_cast<int>(index)){fake::lastError=5;return FALSE;}
  if(index>=fake::sets.at(set).rows.size()){fake::lastError=ERROR_NO_MORE_ITEMS;return FALSE;}
  out->Reserved=index;return TRUE;
}
BOOL SetupDiGetDeviceInterfaceDetailW(HDEVINFO set,SP_DEVICE_INTERFACE_DATA* iface,SP_DEVICE_INTERFACE_DETAIL_DATA_W* out,DWORD bytes,DWORD* required,SP_DEVINFO_DATA* data){
  if(!fakeValidSet(set)||!iface||iface->Reserved>=fake::sets.at(set).rows.size())return FALSE;
  const auto& row=fake::sets.at(set).rows.at(iface->Reserved);const auto wide=U2W(row.path);
  const auto need=static_cast<DWORD>(std::max(sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W),offsetof(SP_DEVICE_INTERFACE_DETAIL_DATA_W,DevicePath)+(wide.size()+1)*sizeof(wchar_t)));
  if(required)*required=fake::detailSizeOverride>=0?static_cast<DWORD>(fake::detailSizeOverride):need;
  if(!out){fake::lastError=ERROR_INSUFFICIENT_BUFFER;return FALSE;}
  if(fake::detailFillFail||bytes<need||out->cbSize!=sizeof(*out)||!data||data->cbSize!=sizeof(*data)){fake::lastError=ERROR_INVALID_DATA;return FALSE;}
  std::memcpy(out->DevicePath,wide.c_str(),(wide.size()+1)*sizeof(wchar_t));++fake::detailFills;
  if(!fake::invalidDetailData){data->DevInst=row.devInst;data->Reserved=set;}return TRUE;
}
BOOL SetupDiGetDeviceInstanceIdW(HDEVINFO set,SP_DEVINFO_DATA* data,wchar_t* out,DWORD chars,DWORD* required){
  ++fake::instanceQueries;if(!fakeValidSet(set))return FALSE;
  if(!data||data->cbSize!=sizeof(*data)||!data->DevInst||data->Reserved!=set){++fake::invalidDataUses;fake::lastError=ERROR_INVALID_DATA;return FALSE;}
  const auto& rows=fake::sets.at(set).rows;const auto at=std::find_if(rows.begin(),rows.end(),[&](const auto& row){return row.devInst==data->DevInst;});
  if(at==rows.end()){++fake::invalidDataUses;return FALSE;}
  const auto wide=U2W(at->instance);if(required)*required=static_cast<DWORD>(wide.size()+1);
  if(!out||chars<wide.size()+1){fake::lastError=ERROR_INSUFFICIENT_BUFFER;return FALSE;}
  std::memcpy(out,wide.c_str(),(wide.size()+1)*sizeof(wchar_t));return TRUE;
}
HANDLE CreateFileW(const wchar_t* path,DWORD,DWORD,void*,DWORD,DWORD,std::nullptr_t){
  if(fake::resolveHook)fake::resolveHook();if(!fake::resolveOk)return INVALID_HANDLE_VALUE;
  for(const auto& [set,value]:fake::sets)for(const auto& row:value.rows)if(ascii_lower(row.path)==ascii_lower(W2U(path))){const auto handle=++fake::nextHandle;fake::handles.emplace(handle,row);++fake::metadataOpens;return handle;}
  // The old invalid-set call can open metadata, but cannot derive an instance ID.
  const auto handle=++fake::nextHandle;fake::handles.emplace(handle,fake::Interface{W2U(path),fake::source.usbInstance,fake::owned,static_cast<USHORT>(fake::source.vendorId),static_cast<USHORT>(fake::source.productId)});++fake::metadataOpens;return handle;
}
BOOL CloseHandle(HANDLE handle){return fake::handles.erase(handle)?TRUE:FALSE;}
BOOL HidD_GetAttributes(HANDLE handle,HIDD_ATTRIBUTES* out){if(!fake::handles.contains(handle))return FALSE;const auto& row=fake::handles.at(handle);out->VendorID=row.vid;out->ProductID=row.pid;return TRUE;}
BOOL HidD_GetPreparsedData(HANDLE handle,PHIDP_PREPARSED_DATA* out){if(!fake::handles.contains(handle))return FALSE;*out=new FakePreparsed{handle};return TRUE;}
int HidP_GetCaps(PHIDP_PREPARSED_DATA data,HIDP_CAPS* out){if(!data||!fake::handles.contains(data->handle))return 0;out->UsagePage=1;out->Usage=5;return HIDP_STATUS_SUCCESS;}
BOOL HidD_FreePreparsedData(PHIDP_PREPARSED_DATA data){delete data;return TRUE;}
bool hidHideIsVirtualIdentity(const std::string& instance,const std::string&){return instance.find("VIRTUAL")!=std::string::npos;}
std::string hidHideNearestBaseInstance(DWORD devInst,const std::string&,HidBaseWalk* walk,CONFIGRET* error,bool* depth,std::string* top,bool* live){
  for(const auto& [set,value]:fake::sets)for(const auto& row:value.rows)if(row.devInst==devInst){*walk=HidBaseWalk::Complete;*error=CR_SUCCESS;*depth=false;*top=row.root;*live=true;return row.root;}return {};
}
std::wstring hidHideResolveXusbInterfacePath(const std::string&){return {};}

void msiDinputReleasePad(const char*) { fake::sourcePresent = false; g_msiDinput.pad=nullptr; }
void msiDinputClearOwnedState(const char*) { g_msiDinputSourceValid = false;g_msiDinput.mainInterfaceMatched=false;g_msiDinput.productId=0; }
bool hidHideCaptureVisibilitySnapshot(HidHideVisibilitySnapshot&);
bool hidHideRestorePhysicalTransaction(const char*);
bool msiClawRestoreXInputMode(const char*);
static void msiOwnedReturnRecordInitial(const char*,const MsiClawCommandOutcome&);
std::vector<std::wstring> hidHideApplicationReturnPaths(const Journal& j){return j.ownedApplications;}
bool hidHideShouldReturnApplication(const Journal& j,const std::wstring& p){return hidHideContainsPath(j.ownedApplications,W2U(p));}
`;
const actual = resolverActual+'\n'+restoreActual+'\n'+gateActual+'\n'+adapter + globals + functions.map(f => f.body).join('\n\n')+'\n'+legacyMaintenance+'\nstatic bool fixtureRetirementDisabled(const msiRecovery::Record&){return true;}\n'+legacyRetirementMaintenance;
const support = String.raw`
bool hidHideCaptureVisibilitySnapshot(HidHideVisibilitySnapshot& out) {
  ++fake::snapshotReads; if (!fake::visibilityReadOk) return false;
  out.hidden=fake::hidden;out.applications=fake::applications;out.valid=fake::snapshotValid;if(fake::snapshotHook)fake::snapshotHook();return true;
}
bool hidHideRestorePhysicalTransaction(const char* reason) {
 ++fake::restores;auto& journal=g_hidHideVisibilityJournal;
 for(const auto& p:journal.hiddenByUs){if(!hidHideContainsPath(fake::hidden,W2U(p)))continue;++fake::unhideWrites;if(fake::unhideOk)fake::hidden.erase(std::remove(fake::hidden.begin(),fake::hidden.end(),p),fake::hidden.end());}
 HidHideVisibilitySnapshot restoreAfter;restoreAfter.hidden=fake::hidden;restoreAfter.applications=fake::applications;restoreAfter.valid=fake::snapshotValid;
 const bool restoreReadback=fake::visibilityReadOk&&restoreAfter.valid;const int targetCount=static_cast<int>(journal.hiddenByUs.size());int remaining=0;for(const auto& p:journal.hiddenByUs)if(hidHideContainsPath(fake::hidden,W2U(p)))++remaining;
 const bool virtualEndpointAbsent=hidHideVirtualEndpointAbsent(g_xinputPhysicalSourceLocked,g_xinputConnectedMask,g_xinputPhysicalSourceMask);
 const bool restored=fixtureActualRestoreResult(journal,restoreAfter,restoreReadback,virtualEndpointAbsent,targetCount,restoreReadback?targetCount:0,remaining,0,restoreReadback?0:1);
 appendNativeLifecycleLog("hidhide.p-hid-restore",{{"reason",reason},{"transactionId",journal.transactionId},{"virtualEndpointAbsent",virtualEndpointAbsent},{"ownedTargetsReturned",restoreReadback&&remaining==0},{"ownedApplicationsReturned",!journal.ownedApplicationResponsibilityUnknown&&journal.ownedApplications.empty()},{"restored",restored},{"status",restored?"restored":"release-unproven"}});
 // Source-bound production tail below: only restored=true clears the journal.
${restoreTail}
}
bool fixtureActualStartVisibilityGate(){
${startGateBody}
 return true;
}
// Controlled stand-in for the production observer; the real policy is not linked.
static void msiOwnedReturnRecordInitial(const char* reason,const MsiClawCommandOutcome& outcome) {
  ++fake::observerInitials;fake::observerEpisode=g_msiDinputReturnDebtEpisode.load();
  fake::observerSawDurableOwner=g_msiCrashRecord.modeReturnPending &&
    g_msiCrashRecord.ownerPid==GetCurrentProcessId() &&
    g_msiCrashRecord.ownerCreationTime==msiCrashProcessCreation(GetCurrentProcess()) &&
    ymcc::msi::recovery::encodeRecord(g_msiCrashRecord)==fake::storage;
  appendNativeLifecycleLog("fixture-owned-observer",{{"action","initial"},{"reason",reason},
    {"simulated",true},{"returnDebtEpisodeId",fake::observerEpisode},
    {"durableOwnerAdopted",fake::observerSawDurableOwner},{"commandWriteOk",outcome.write.ok},
    {"physicalSignalVerified",false},{"twoStableSignalWindowsVerified",false}});
}
bool fixtureOwnedObserverCompleteModeDuty() {
  // Explicit test input to the existing durable close hook. No signal is sampled.
  if(!fake::observerInitials || !fake::observerEpisode ||
     fake::observerEpisode!=g_msiDinputReturnDebtEpisode.load())return false;
  ++fake::observerCompletions;
  appendNativeLifecycleLog("fixture-owned-observer",{{"action","controlled-mode-completion"},
    {"simulated",true},{"returnDebtEpisodeId",fake::observerEpisode},
    {"physicalSignalVerified",false},{"twoStableSignalWindowsVerified",false}});
  msiCrashRecoveryAfterModeDischarge();
  if(g_msiCrashRecord.modeReturnPending)return false;
  g_msiDinputReturnOwed=false;return true;
}
bool msiClawRestoreXInputMode(const char* reason) {
  // Fake command transport, actual existing terminal verdict and target absence.
  if (!msiMode1WritebackVerdict(g_msiDinputReturnOwed.load(),inputHostExitFenceRaised(),
    !g_msiDinputRouteDinputArmed.load(),msiDinputDeviceTargetAbsent(),false,"input-host-stop").allowed ||
    g_msiMode1TerminalAttempted.exchange(true)) return false;
  MsiClawHidDiag d{"FAKE-COMMAND-INTERFACE"};
  if (!msiCrashRecoveryBeforeModeWrite(1, d)) return false;
  ++fake::modeWrites;
  MsiClawCommandOutcome outcome;outcome.write.ok=fake::modeOk;
  msiOwnedReturnRecordInitial(reason,outcome);return fake::modeOk;
}
ymcc::msi::recovery::Record record(bool mode = true, bool paths = true) {
  ymcc::msi::recovery::Record r; r.boardKey = msiCrashBoardKey(); r.ownerPid = 7;
  r.ownerCreationTime = "1000"; r.transactionId = "fake-prior-transaction";
  r.rootInstance = fake::root; r.modeReturnPending = mode;
  for(const auto& p:fake::locations)r.locationPaths.push_back(W2U(p));
  if (paths) r.hiddenPaths = {fake::owned};
  if (!ymcc::msi::recovery::validRecord(r)) throw std::runtime_error("invalid real-codec seed");
  return r;
}
void save(const ymcc::msi::recovery::Record& r) {
  fake::storage = ymcc::msi::recovery::encodeRecord(r); if (fake::storage.empty()) throw std::runtime_error("codec rejected seed");
  fake::storePresent = true;
}
ymcc::msi::recovery::Record durable() {
  ymcc::msi::recovery::Record r; if (!ymcc::msi::recovery::decode(fake::storage, r)) throw std::runtime_error("durable fixture record invalid"); return r;
}
void restart(DWORD pid = 101) {
  fake::pid = pid; fake::creation = std::to_string(2000 + pid);
  g_msiCrashRecord = {}; g_msiCrashLoaded = false; g_msiCrashStoreInvalid = false;
  g_msiCrashInheritedPending = false; g_msiCrashStartupAttempted = false; g_msiCrashNextTick = 0; g_msiCrashLastDecision.clear(); g_msiCrashUnhideAttempts=0; g_msiCrashSettlementChecks=0; g_msiCrashDecisionRows=0;
  g_hidHideVisibilityJournal = {};g_msiCrashStartupTransactionId.clear();g_msiCrashStartupHiddenPaths.clear();g_inputHostReleaseQuarantined=false;g_wakeHoldPhysicalUnhide=false;g_xinputConnectedMask=0; g_realStickTest.virtualTargetEnabled = false;
  g_inputCaptureGamepadSnapshot={};
  g_inputHostProcess = 0; g_inputHostPrepared = false;
  g_powerLifecycle = PowerLifecycle::Ready; fake::exitFence = false;
  g_msiDinputSourceValid = true; g_msiDinputRouteDinputArmed = false; g_msiDinputRouteConfirmed = false;
  g_msiDinputReturnOwed = false; g_msiMode1TerminalAttempted = false;
  g_msiDinputReturnDebtEpisode=0;g_msiNativeForegroundCyclePending=false;g_msiNativeForegroundPowerHold=false;
  fake::observerInitials=fake::observerCompletions=0;fake::observerEpisode=0;
  fake::observerSawDurableOwner=fake::borrowBusy=false;
  fake::sourcePresent = true; fake::source = {}; fake::source.usbInstance = fake::root;
  fake::source.interfacePath = "FAKE-DINPUT-GAME-INTERFACE";
  fakePower=2;fakeEpoch=1;g_msiDinput.pad=reinterpret_cast<void*>(1);g_msiDinput.mainInterfaceMatched=true;g_msiDinput.productId=0x1902;
  g_msiFeedbackSamplePower=2;g_msiFeedbackSampleMode=g_msiFeedbackModeGeneration=3;
  g_msiOwnedReturnSourceEvent=0;
  g_msiFeedbackSampleTopology=g_xinputTopologyEpoch=4;g_xinputPhysicalSourceLocked=false;g_xinputPhysicalSourceMask=0;
  fake::locations={L"PCIROOT(0)#PCI(1400)#USBROOT(0)#USB(4)"};fake::additionalSources.clear();
  fake::resetSetup();fake::resolveOk = true; fake::resolveHook = {}; fake::afterCommit = {};fake::snapshotHook={};fake::snapshotValid=true;fake::applications.clear();
  fake::failCommit = fake::failRead = fake::cutBeforeCommit = fake::cutAfterCommit = false;
  fake::seam = false; fake::knownBoard = fake::ownerDead = fake::hardwareGate = true;
  fake::unhideOk = true; fake::modeOk = false; fake::visibilityReadOk=true; fake::snapshotReads=0;
}
void seed(bool mode = true, bool paths = true) {
  fake::storage.clear(); fake::storePresent = false; fake::archiveFiles.clear();fake::boardKey="MICRO-STAR|MS-1T91|FAKE-SYSTEM|ClawCG3EM"; restart();
  fake::commits = fake::reads = fake::restores = fake::unhideWrites = fake::modeWrites = fake::mode2Writes = 0;
  fake::logs.clear(); fake::hidden = {U2W(fake::foreign)};
  if (paths) fake::hidden.push_back(U2W(fake::owned)); save(record(mode, paths));
  msiCrashRecoveryInitialize();
}
void directMode1EntryReady() {
  g_msiDinputReturnOwed=true;g_msiDinputRouteDinputArmed=false;
  fake::sourcePresent=false;g_msiDinputSourceValid=false;g_msiDinput.pad=nullptr;
  g_msiDinput.mainInterfaceMatched=false;g_msiDinput.productId=0;
}
json results = json::array(); unsigned assertions = 0, failures = 0;
void check(const char* id, bool ok, const char* invariant, json evidence = json::object()) {
  ++assertions; if (!ok) ++failures;
  results.push_back({{"id", id}, {"passed", ok}, {"invariant", invariant}, {"evidence", std::move(evidence)}, {"trace",fake::logs}});
  std::cout << (ok ? "PASS " : "FAIL ") << id << "\n";
}
void admitXinput() {
  fake::sourcePresent=false;fake::source.productId=0x1901;fake::source.usbInstance="USB\\VID_0DB0&PID_1901\\DIFFERENTMODETAIL";
  g_xinputPhysicalSourceLocked=true;g_xinputPhysicalSourceMask=1;
  g_inputCaptureGamepadSnapshot={true,0,1,"xinput",1,fakePower,static_cast<unsigned>(g_xinputTopologyEpoch.load()),true};
}


void runStartupAdoptionBoundaryTests() {
  // These tests exercise actual startup owner adoption and durable close hooks.
  // Every observer event/completion below is an explicit sidecar input; no real
  // policy, two-window signal qualification or production retry is executed.
  seed();const auto expected=g_msiCrashRecord;
  bool commitPrecedesEpisode=false;
  fake::afterCommit=[&]{const auto stored=durable();commitPrecedesEpisode=
    stored.ownerPid==fake::pid&&stored.ownerCreationTime==fake::creation&&stored.modeReturnPending&&
    g_msiDinputReturnDebtEpisode.load()==0&&!g_msiDinputReturnOwed.load();};
  const bool adopted=msiCrashAdoptStartupModeDuty(expected);
  check("startup-adoption-durable-owner-before-nonzero-episode",adopted&&commitPrecedesEpisode&&
    durable().ownerPid==fake::pid&&durable().ownerCreationTime==fake::creation&&
    durable().modeReturnPending&&durable().hiddenPaths==expected.hiddenPaths&&
    durable().transactionId==expected.transactionId&&g_msiDinputReturnDebtEpisode.load()==1&&
    g_msiDinputReturnOwed.load()&&fake::observerInitials==0&&fake::modeWrites==0&&fake::restores==0,
    "Actual helper persists the new owner before publishing an episode; adoption alone neither writes mode nor settles duty");
  for(const auto& kind:std::vector<std::string>{"board","root","locations","owner-pid","owner-creation","attempt-owner","transaction"}) {
    seed();auto stale=g_msiCrashRecord;const auto bytes=fake::storage;
    if(kind=="board")stale.boardKey="OTHER|BOARD|SYSTEM|ClawCG3EM";
    else if(kind=="root")stale.rootInstance="USB\\VID_0DB0&PID_1902\\OTHER";
    else if(kind=="locations")stale.locationPaths={"OTHER-LOCATION"};
    else if(kind=="owner-pid")++stale.ownerPid;else if(kind=="owner-creation")stale.ownerCreationTime="99999";
    else if(kind=="attempt-owner")stale.modeAttemptOwner="7:1000";else stale.transactionId="OTHER-TRANSACTION";
    const auto id="startup-adoption-stale-"+kind+"-denied";
    check(id.c_str(),!msiCrashAdoptStartupModeDuty(stale)&&fake::storage==bytes&&fake::commits==0&&
      g_msiDinputReturnDebtEpisode.load()==0&&!g_msiDinputReturnOwed.load()&&fake::modeWrites==0&&fake::restores==0,
      "A stale expected record cannot adopt durable ownership or create an observer episode");
  }
  for(const auto& kind:std::vector<std::string>{"unknown-board","seam","write-gate","exit","new-ON","Host","prepared-Host","shield-cycle","shield-power","borrow","not-loaded","store-invalid","mode-not-pending","old-owner-alive","current-record-invalid"}) {
    seed();
    if(kind=="unknown-board")fake::knownBoard=false;else if(kind=="seam")fake::seam=true;
    else if(kind=="write-gate")fake::hardwareGate=false;else if(kind=="exit")fake::exitFence=true;
    else if(kind=="new-ON")g_realStickTest.virtualTargetEnabled=true;else if(kind=="Host")g_inputHostProcess=123;
    else if(kind=="prepared-Host")g_inputHostPrepared=true;else if(kind=="shield-cycle")g_msiNativeForegroundCyclePending=true;
    else if(kind=="shield-power")g_msiNativeForegroundPowerHold=true;else if(kind=="borrow")fake::borrowBusy=true;
    else if(kind=="not-loaded")g_msiCrashLoaded=false;else if(kind=="store-invalid")g_msiCrashStoreInvalid=true;
    else if(kind=="mode-not-pending"){g_msiCrashRecord.modeReturnPending=false;save(g_msiCrashRecord);}
    else if(kind=="old-owner-alive")fake::ownerDead=false;else g_msiCrashRecord.ownerPid=0;
    const auto candidate=g_msiCrashRecord;const auto bytes=fake::storage;
    const auto id="startup-adoption-guard-"+kind;
    check(id.c_str(),!msiCrashAdoptStartupModeDuty(candidate)&&fake::storage==bytes&&fake::commits==0&&
      g_msiDinputReturnDebtEpisode.load()==0&&!g_msiDinputReturnOwed.load()&&fake::observerInitials==0&&
      fake::modeWrites==0&&fake::restores==0,
      "An actual adoption guard denial preserves prior bytes and publishes no episode or device mutation");
  }
  seed();fake::failCommit=true;msiCrashRecoveryMaintenance(1000);
  check("startup-adoption-failed-store-blocks-configuration-and-mode",durable().ownerPid==7&&
    durable().modeReturnPending&&!durable().hiddenPaths.empty()&&fake::commits==0&&
    !g_msiCrashStartupAttempted&&g_msiDinputReturnDebtEpisode.load()==0&&!g_msiDinputReturnOwed.load()&&
    fake::observerInitials==0&&fake::restores==0&&fake::modeWrites==0&&hidHideContainsPath(fake::hidden,fake::owned),
    "Failed owner adoption cannot reach owned unhide, command dispatch or the fake observer");
  for(const bool afterCommit:{false,true}) {
    seed();const auto candidate=g_msiCrashRecord;fake::cutBeforeCommit=!afterCommit;fake::cutAfterCommit=afterCommit;
    bool cut=false;try{(void)msiCrashAdoptStartupModeDuty(candidate);}catch(const fake::Cut&){cut=true;}
    const auto id=afterCommit?"startup-adoption-cut-after-store-retains-duty-without-episode":"startup-adoption-cut-before-store-retains-old-owner";
    check(id,cut&&durable().ownerPid==(afterCommit?fake::pid:candidate.ownerPid)&&
      durable().modeReturnPending&&durable().hiddenPaths==candidate.hiddenPaths&&
      g_msiDinputReturnDebtEpisode.load()==0&&!g_msiDinputReturnOwed.load()&&fake::observerInitials==0&&
      fake::modeWrites==0&&fake::restores==0,
      "A cut on either side of the durable adoption retains outstanding duty and issues no observer event or device command");
  }
  seed();admitXinput();msiCrashRecoveryMaintenance(1000);
  const auto xinputEpisode=g_msiDinputReturnDebtEpisode.load();
  check("owned-observer-boundary-xinput-initial-no-command",durable().hiddenPaths.empty()&&
    durable().modeReturnPending&&durable().modeAttemptOwner.empty()&&durable().ownerPid==fake::pid&&
    xinputEpisode!=0&&fake::observerInitials==1&&fake::observerEpisode==xinputEpisode&&
    fake::observerSawDurableOwner&&fake::modeWrites==0&&fake::mode2Writes==0,
    "An eligible fake XInput source adopts durable ownership and starts only the simulated observer; one source sample does not discharge mode");
  msiCrashRecoveryMaintenance(3000);msiCrashRecoveryMaintenance(5000);
  check("owned-observer-boundary-startup-ticks-cannot-clear-mode",durable().modeReturnPending&&
    g_msiCrashInheritedPending&&g_msiDinputReturnOwed.load()&&g_msiDinputReturnDebtEpisode.load()==xinputEpisode&&
    fake::observerInitials==1&&fake::observerCompletions==0&&fake::modeWrites==0,
    "Repeated actual legacy maintenance ticks retain mode duty for the external observer; no stable signal windows are evaluated here");
  fake::failCommit=true;const bool failedComplete=fixtureOwnedObserverCompleteModeDuty();
  check("owned-observer-boundary-failed-durable-close-retains-duty",!failedComplete&&
    durable().modeReturnPending&&g_msiDinputReturnOwed.load()&&fake::modeWrites==0,
    "Even an explicit simulated observer completion cannot clear durable mode duty when storage rejects settlement");
  fake::failCommit=false;const bool complete=fixtureOwnedObserverCompleteModeDuty();
  check("owned-observer-boundary-explicit-completion-closes-mode",complete&&!durable().modeReturnPending&&
    !g_msiDinputReturnOwed.load()&&durable().modeAttemptOwner.empty()&&fake::modeWrites==0&&fake::mode2Writes==0,
    "Explicit sidecar completion exercises only the real durable close hook; it is not physical-signal qualification");
  seed(true,false);fake::modeOk=true;msiCrashRecoveryMaintenance(1000);admitXinput();
  msiCrashRecoveryMaintenance(3000);msiCrashRecoveryMaintenance(5000);
  check("owned-observer-boundary-command-success-is-not-mode-discharge",durable().modeReturnPending&&
    durable().ownerPid==fake::pid&&g_msiDinputReturnDebtEpisode.load()!=0&&g_msiDinputReturnOwed.load()&&
    fake::modeWrites==1&&fake::observerInitials==1&&fake::observerSawDurableOwner&&fake::observerCompletions==0,
    "A fake successful initial mode1 plus fake XInput availability retain durable duty until an explicit observer input; no production observer runs");
  for(const auto& kind:std::vector<std::string>{"power","epoch","mode","topology","shield-cycle","shield-power","borrow","write-gate","exit","new-ON"}) {
    seed(true,false);
    fake::afterCommit=[&]{if(kind=="power")++fakePower;else if(kind=="epoch")++fakeEpoch;
      else if(kind=="mode")++g_msiFeedbackModeGeneration;else if(kind=="topology")++g_xinputTopologyEpoch;
      else if(kind=="shield-cycle")g_msiNativeForegroundCyclePending=true;
      else if(kind=="shield-power")g_msiNativeForegroundPowerHold=true;else if(kind=="borrow")fake::borrowBusy=true;
      else if(kind=="write-gate")fake::hardwareGate=false;else if(kind=="exit")fake::exitFence=true;
      else g_realStickTest.virtualTargetEnabled=true;};
    msiCrashRecoveryMaintenance(1000);const auto id="startup-adoption-post-store-"+kind+"-observer-only";
    check(id.c_str(),durable().modeReturnPending&&durable().ownerPid==fake::pid&&
      g_msiDinputReturnDebtEpisode.load()!=0&&fake::modeWrites==0&&fake::observerInitials==1&&fake::observerSawDurableOwner,
      "A context change across adoption storage suppresses the actual initial mode dispatch and hands retained duty to the simulated observer");
  }
}

void emitFixtureReceipt(const char* scope){
 std::cout<<"SIDECAR_RESULT "<<json({{"assertions",assertions},{"failures",failures},{"results",results},{"scope",scope},
 {"ownedObserver","controlled-sidecar-simulation"},{"twoStableSignalWindowsVerified",false},
 {"productionEntrypointExecuted",false},{"realDeviceCalls",0},{"realRegistryCalls",0},{"realHostCalls",0},
 {"storage","fake bytes using actual captured journal codec"},{"restart","logical"}}).dump()<<"\n";
}
int runStartupRetirement(const json& archive){
 const auto fixture=archive.at("retirementFixture");
 const auto retiredRows=[](){return std::count_if(fake::logs.begin(),fake::logs.end(),[](const json& row){return row.value("phase",std::string{})=="startup-transaction-retired";});};
 auto archiveSeed=[&]{
  fake::storage.clear();fake::storePresent=false;fake::archiveFiles.clear();restart(9792);fake::boardKey=fixture.at("boardKey");fake::logs.clear();
  fake::commits=fake::reads=fake::restores=fake::unhideWrites=fake::modeWrites=fake::mode2Writes=0;
  const auto& source=fixture.at("source").at("row");fake::source.interfacePath=source.at("interfacePath");fake::source.usbInstance=source.at("usbInstance");
  fake::source.generation=source.at("generation");fake::source.vendorId=source.at("vendorId");fake::source.productId=source.at("productId");
  auto r=record();const auto& loaded=fixture.at("loaded").at("row");r.rootInstance=loaded.at("rootInstance");r.ownerPid=loaded.at("recordOwnerPid");r.ownerCreationTime=loaded.at("recordOwnerCreationTime");
  r.hiddenPaths=loaded.at("ownedHiddenPaths").get<std::vector<std::string>>();r.modeReturnPending=true;
  const std::string transaction=fixture.at("restore").at("row").at("transactionId");r.transactionId=transaction.substr(0,transaction.size()-8);
  fake::hidden={U2W(fake::foreign)};for(const auto& owned:r.hiddenPaths)fake::hidden.push_back(U2W(owned));save(r);fake::modeOk=true;msiCrashRecoveryInitialize();
 };
 const auto freshUnlockedXinput=[](){admitXinput();g_xinputPhysicalSourceLocked=false;g_xinputPhysicalSourceMask=0;g_xinputConnectedMask=1;};
 auto readyForRetirement=[&]{archiveSeed();msiCrashRecoveryMaintenance(1000);freshUnlockedXinput();(void)fixtureOwnedObserverCompleteModeDuty();};
 // The production endpoint predicate requires a selected-source lock, even OFF.
 check("150048-OFF-no-source-lock-does-not-prove-endpoint-absence",!hidHideVirtualEndpointAbsent(false,0,0)&&hidHideVirtualEndpointAbsent(true,1,1),"Extracted production endpoint predicate distinguishes configuration return from endpoint absence");
 archiveSeed();msiCrashRecoveryMaintenanceBeforeRetirement(1000);
 const auto restoreRow=std::find_if(fake::logs.begin(),fake::logs.end(),[](const json& row){return row.value("event",std::string{})=="hidhide.p-hid-restore";});
 check("150048-production-restore-tail-keeps-startup-journal",restoreRow!=fake::logs.end()&&restoreRow->at("ownedTargetsReturned").get<bool>()&&restoreRow->at("ownedApplicationsReturned").get<bool>()&&!restoreRow->at("virtualEndpointAbsent").get<bool>()&&g_hidHideVisibilityJournal.active&&durable().hiddenPaths.empty()&&fake::unhideWrites==2&&fake::modeWrites==1,
  "Actual production predicate/tail keep the journal after two owned returns when endpoint absence is unproven",{{"archiveRestore",fixture.at("restore")},{"journalActive",g_hidHideVisibilityJournal.active}});
 // The historical no-duty scenario now requires explicit fake observer completion.
 freshUnlockedXinput();(void)fixtureOwnedObserverCompleteModeDuty();msiCrashRecoveryMaintenanceBeforeRetirement(3000);msiCrashRecoveryMaintenanceBeforeRetirement(5000);
 g_realStickTest.virtualTargetEnabled=true;const bool oldStartAllowed=fixtureActualStartVisibilityGate();
 check("150048-old-no-duty-journal-stuck-new-ON-blocked",!msiCrashHasDuty(durable())&&!g_msiCrashInheritedPending&&g_hidHideVisibilityJournal.active&&!oldStartAllowed&&fake::modeWrites==1&&fake::mode2Writes==0,
  "Pre-retirement actual maintenance disables itself with no durable duty but active local journal; extracted startup gate rejects new ON",{{"archiveSettled",fixture.at("settled")},{"archiveStartupDenials",fixture.at("blocked").size()}});
 archiveSeed();msiCrashRecoveryMaintenance(1000);
 check("150048-duty-keeps-journal-before-mode-return",g_hidHideVisibilityJournal.active&&durable().hiddenPaths.empty()&&durable().modeReturnPending&&retiredRows()==0,"No retirement while the durable mode duty remains");
 freshUnlockedXinput();msiCrashRecoveryMaintenance(3000);
 check("owned-observer-boundary-retirement-waits-for-explicit-completion",g_hidHideVisibilityJournal.active&&
  durable().modeReturnPending&&retiredRows()==0&&fake::observerInitials==1&&fake::observerCompletions==0,
  "Fresh fake XInput alone cannot retire a startup journal while durable mode duty remains");
 const bool observerCompleted=fixtureOwnedObserverCompleteModeDuty();
 check("owned-observer-boundary-retirement-completion-is-simulated",observerCompleted&&
  !durable().modeReturnPending&&g_hidHideVisibilityJournal.active&&fake::observerCompletions==1,
  "The sidecar explicitly injects mode completion before separately testing the actual retirement helper");
 msiCrashRecoveryMaintenance(4000);
 const auto durableBefore=durable();const auto sourceBefore=fake::source;const auto owedBefore=g_msiDinputReturnOwed.load();const bool lockedBefore=g_xinputPhysicalSourceLocked;const auto storageBefore=fake::storage;const auto commitsBefore=fake::commits;
 msiCrashRecoveryMaintenance(5000);
 const auto row=std::find_if(fake::logs.begin(),fake::logs.end(),[](const json& value){return value.value("phase",std::string{})=="startup-transaction-retired";});
 check("150048-fixed-no-duty-startup-ledger-retired",!msiCrashHasDuty(durable())&&!g_hidHideVisibilityJournal.active&&g_msiCrashStartupTransactionId.empty()&&g_msiCrashStartupHiddenPaths.empty()&&retiredRows()==1&&row!=fake::logs.end()&&row->at("virtualEndpointAbsenceProven")==false&&row->at("steamConsumptionProven")==false&&fake::unhideWrites==2&&fake::modeWrites==1&&fake::mode2Writes==0&&hidHideContainsPath(fake::hidden,fake::foreign),
  "Actual helper is reached before no-duty exit and retires only its process-local startup transaction after fresh readback",{{"retirementEvent",row!=fake::logs.end()?*row:json(nullptr)},{"fakeLocationsAndFreshness",true}});
 check("150048-retirement-preserves-source-debt-and-quarantine",fake::storage==storageBefore&&fake::commits==commitsBefore&&durable().rootInstance==durableBefore.rootInstance&&durable().modeReturnPending==durableBefore.modeReturnPending&&g_msiDinputReturnOwed.load()==owedBefore&&g_xinputPhysicalSourceLocked==lockedBefore&&fake::source.interfacePath==sourceBefore.interfacePath&&fake::source.usbInstance==sourceBefore.usbInstance&&!g_inputHostReleaseQuarantined&&!g_inputHostProcess&&!g_inputHostPrepared,
  "Ledger retirement does not reset shared source, return debt, Host or quarantine and is not endpoint/Steam proof");
 g_realStickTest.virtualTargetEnabled=true;check("150048-fixed-new-ON-passes-visibility-gate",fixtureActualStartVisibilityGate(),"Only the extracted visibility gate is tested; no Host or mode2 is started");
 g_realStickTest.virtualTargetEnabled=false;msiCrashRecoveryMaintenance(7000);check("150048-retirement-event-once",retiredRows()==1,"A retired ledger is not retired or logged again");
 for(const auto& kind:std::vector<std::string>{"snapshot-fail","snapshot-invalid","still-hidden","foreign-transaction","missing-self-tag","new-ON","active-Host","prepared-Host","quarantine","application-added","owned-application","application-unknown","cloak-changed","unknown-board","power-not-ready","exit","durable-mode-duty","durable-hidden-duty","durable-mode-settlement-fail","hidden-path-added","hidden-path-replaced","hidden-path-empty","before-hidden","before-application","before-cloak","seam","write-gate","store-invalid","not-loaded","startup-not-attempted","missing-tracked-paths"}){
  readyForRetirement();
  if(kind=="snapshot-fail")fake::visibilityReadOk=false;else if(kind=="snapshot-invalid")fake::snapshotValid=false;
  else if(kind=="seam")fake::seam=true;else if(kind=="write-gate")fake::hardwareGate=false;else if(kind=="store-invalid")g_msiCrashStoreInvalid=true;else if(kind=="not-loaded")g_msiCrashLoaded=false;else if(kind=="startup-not-attempted")g_msiCrashStartupAttempted=false;else if(kind=="missing-tracked-paths")g_msiCrashStartupHiddenPaths.clear();
  else if(kind=="still-hidden")fake::hidden.push_back(g_hidHideVisibilityJournal.hiddenByUs.front());
  else if(kind=="foreign-transaction")g_hidHideVisibilityJournal.transactionId="FOREIGN#startup";else if(kind=="missing-self-tag")g_msiCrashStartupTransactionId.clear();
  else if(kind=="new-ON")g_realStickTest.virtualTargetEnabled=true;else if(kind=="active-Host")g_inputHostProcess=123;else if(kind=="prepared-Host")g_inputHostPrepared=true;
  else if(kind=="quarantine")g_inputHostReleaseQuarantined=true;else if(kind=="application-added")g_hidHideVisibilityJournal.applicationAdded=true;
  else if(kind=="owned-application")g_hidHideVisibilityJournal.ownedApplications={L"FAKE-OWNED-APPLICATION"};else if(kind=="application-unknown")g_hidHideVisibilityJournal.ownedApplicationResponsibilityUnknown=true;
  else if(kind=="cloak-changed")g_hidHideVisibilityJournal.cloakChanged=true;else if(kind=="unknown-board")fake::knownBoard=false;else if(kind=="power-not-ready")g_powerLifecycle=PowerLifecycle::Suspended;else if(kind=="exit")fake::exitFence=true;
  else if(kind=="durable-mode-duty"){g_msiCrashRecord.modeReturnPending=true;save(g_msiCrashRecord);}
  else if(kind=="durable-hidden-duty"){g_msiCrashRecord.hiddenPaths={fake::owned};save(g_msiCrashRecord);}
  else if(kind=="durable-mode-settlement-fail"){g_msiCrashRecord.modeReturnPending=true;save(g_msiCrashRecord);fake::failCommit=true;msiCrashRecoveryAfterModeDischarge();}
  else if(kind=="hidden-path-added")g_hidHideVisibilityJournal.hiddenByUs.push_back(L"OTHER-OWNERS-PATH");else if(kind=="hidden-path-replaced")g_hidHideVisibilityJournal.hiddenByUs.front()=L"OTHER-OWNERS-PATH";else if(kind=="hidden-path-empty")g_hidHideVisibilityJournal.hiddenByUs.clear();
  else if(kind=="before-hidden"){g_hidHideVisibilityJournal.before.valid=true;g_hidHideVisibilityJournal.before.hidden={L"OTHER-BASELINE-PATH"};}else if(kind=="before-application"){g_hidHideVisibilityJournal.before.valid=true;g_hidHideVisibilityJournal.before.applications={L"OTHER-BASELINE-APP"};}else if(kind=="before-cloak"){g_hidHideVisibilityJournal.before.valid=true;g_hidHideVisibilityJournal.before.cloaking=true;}
  const auto writes=fake::modeWrites,unhides=fake::unhideWrites;const auto logs=retiredRows();const auto tx=g_hidHideVisibilityJournal.transactionId,tag=g_msiCrashStartupTransactionId;
  {const auto capturedRecord=g_msiCrashRecord;std::scoped_lock lifecycle(g_inputHostMx,g_msiPersonaPumpMx);msiCrashRetireStartupJournal(capturedRecord);}
  const auto id="retirement-guard-"+kind;
  check(id.c_str(),g_hidHideVisibilityJournal.active&&g_hidHideVisibilityJournal.transactionId==tx&&g_msiCrashStartupTransactionId==tag&&fake::modeWrites==writes&&fake::unhideWrites==unhides&&retiredRows()==logs,
    "Guard denial retains the startup ledger and performs no device/mode mutation");
 }
 for(const auto& kind:std::vector<std::string>{"power","epoch","mode","topology","new-intent","Host","quarantine","transaction","self-tag","owned-app","snapshot-duty","durable-transaction","durable-root","durable-locations","durable-owner-pid","durable-owner-creation","durable-board","store-invalid","not-loaded"}){
  readyForRetirement();const auto capturedRecord=g_msiCrashRecord;
  fake::snapshotHook=[&]{if(kind=="power")++fakePower;else if(kind=="epoch")++fakeEpoch;else if(kind=="mode")++g_msiFeedbackModeGeneration;
   else if(kind=="topology")++g_xinputTopologyEpoch;else if(kind=="new-intent")g_realStickTest.virtualTargetEnabled=true;else if(kind=="Host")g_inputHostProcess=123;
   else if(kind=="quarantine")g_inputHostReleaseQuarantined=true;else if(kind=="transaction")g_hidHideVisibilityJournal.transactionId="REPLACED#startup";
   else if(kind=="self-tag")g_msiCrashStartupTransactionId="REPLACED#startup";else if(kind=="owned-app")g_hidHideVisibilityJournal.ownedApplications={L"NEW-APP"};
   else if(kind=="store-invalid")g_msiCrashStoreInvalid=true;else if(kind=="not-loaded")g_msiCrashLoaded=false;
   else {if(kind=="snapshot-duty")g_msiCrashRecord.modeReturnPending=true;else if(kind=="durable-transaction")g_msiCrashRecord.transactionId="NEW-DURABLE-TRANSACTION";else if(kind=="durable-root")g_msiCrashRecord.rootInstance="USB\\VID_0DB0&PID_1901\\OTHER";else if(kind=="durable-locations")g_msiCrashRecord.locationPaths={"OTHER-LOCATION"};else if(kind=="durable-owner-pid")++g_msiCrashRecord.ownerPid;else if(kind=="durable-owner-creation")g_msiCrashRecord.ownerCreationTime="999999";else if(kind=="durable-board")g_msiCrashRecord.boardKey="OTHER|BOARD|SYSTEM|ClawCG3EM";save(g_msiCrashRecord);}};
  const auto writes=fake::modeWrites,unhides=fake::unhideWrites;const auto logs=retiredRows();{std::scoped_lock lifecycle(g_inputHostMx,g_msiPersonaPumpMx);msiCrashRetireStartupJournal(capturedRecord);}
  const auto id="retirement-readback-recheck-"+kind;
  check(id.c_str(),g_hidHideVisibilityJournal.active&&fake::modeWrites==writes&&fake::unhideWrites==unhides&&retiredRows()==logs,
   "Changed context across fresh readback rejects retirement rather than clearing a newer transaction");
 }
 readyForRetirement();g_hidHideVisibilityJournal={};const auto noLedgerWrites=fake::modeWrites;const auto noLedgerUnhides=fake::unhideWrites;
 msiCrashRecoveryMaintenance(5000);
 check("150048-already-inactive-journal-clears-only-startup-markers",!g_msiCrashInheritedPending&&g_msiCrashStartupTransactionId.empty()&&g_msiCrashStartupHiddenPaths.empty()&&retiredRows()==0&&fake::modeWrites==noLedgerWrites&&fake::unhideWrites==noLedgerUnhides,"An already cleared local journal drops its own markers without emitting a retirement event or writing mode");
 seed(false,false);msiCrashRecoveryMaintenance(1000);g_realStickTest.virtualTargetEnabled=true;
 check("150048-clean-inherited-record-never-reconstructs-startup-journal",!g_msiCrashInheritedPending&&!g_hidHideVisibilityJournal.active&&g_msiCrashStartupTransactionId.empty()&&g_msiCrashStartupHiddenPaths.empty()&&fake::restores==0&&fake::modeWrites==0&&fixtureActualStartVisibilityGate(),"A clean inherited record creates no startup journal and permits the existing visibility gate; no real Host started");
 emitFixtureReceipt("150048 startup journal retirement; controlled sidecar observer completion; two stable signal windows NOT verified; historical suite not rerun");return failures?1:0;
}

int main(int argc,char** argv) { try {
  json archive=json::object();if(argc>1){std::ifstream file(argv[1]);file>>archive;}
  if(archive.value("retirementOnly",false))return runStartupRetirement(archive);
  runStartupAdoptionBoundaryTests();
  const auto logged=[](const std::string& reason){return std::any_of(fake::logs.begin(),fake::logs.end(),[&](const json& row){return row.value("reason",std::string{})==reason;});};
  const auto releasedSet=[](){return fake::setCreates==1&&fake::setDestroys==1&&fake::sets.empty()&&fake::handles.empty();};
  seed();HidHidePhysicalIdentity oldIdentity;
  const bool oldAllowed=hidHideResolveInterfaceIdentity(U2W(fake::source.interfacePath),nullptr,INVALID_HANDLE_VALUE,oldIdentity);
  check("old-null-invalid-resolver-call-rejected",!oldAllowed&&!oldIdentity.resolved&&fake::invalidSetUses>0&&fake::handles.empty(),
    "Actual main resolver refuses the original null SPdata/invalid set call even when fake HID metadata opens");
  seed();msiCrashRecoveryMaintenanceLegacyCall(1000);
  check("old-call-maintenance-reproduces-silent-stall",fake::restores==0&&fake::modeWrites==0&&g_msiCrashInheritedPending&&fake::invalidSetUses>0,
    "Explicitly mutated old actual call cannot restore owned entries or return mode; no contract adaptation");
  seed();const auto set=SetupDiGetClassDevsW(&kYeManGuidDeviceInterfaceHid,nullptr,nullptr,DIGCF_PRESENT|DIGCF_DEVICEINTERFACE);
  SP_DEVINFO_DATA defaultData{};defaultData.cbSize=sizeof(defaultData);HidHidePhysicalIdentity defaultIdentity;
  const bool defaultAllowed=hidHideResolveInterfaceIdentity(U2W(fake::source.interfacePath),&defaultData,set,defaultIdentity);
  check("valid-set-default-SPdata-rejected",!defaultAllowed&&fake::invalidDataUses>0,"A valid set alone cannot replace the SPdata filled by interface detail");
  SetupDiDestroyDeviceInfoList(set);
  seed();HidHidePhysicalIdentity validIdentity;const bool validAllowed=msiCrashResolveDinputIdentity(fake::source,validIdentity);
  check("actual-helper-real-resolver-valid-set-data",validAllowed&&validIdentity.resolved&&releasedSet()&&fake::detailFills==1&&fake::instanceQueries==2&&fake::invalidDataUses==0&&fake::invalidSetUses==0,
    "Extracted helper supplies detail-derived SPdata, extracted real resolver uses it, RAII destroys the borrowed set");
  for(const auto& kind:std::vector<std::string>{"no-match","enum-error","size-zero","size-small","size-large","detail-fail","invalid-SPdata","vid","pid","root","resolver-reject","set-open"}){
    seed(false);std::string expected;
    if(kind=="no-match"){fake::interfaces={{"OTHER-INTERFACE",fake::source.usbInstance,fake::owned}};expected="identity-current-interface-not-present";}
    else if(kind=="enum-error"){fake::enumErrorAt=0;expected="identity-present-enumeration-failed";}
    else if(kind=="size-zero"||kind=="size-small"||kind=="size-large"){fake::detailSizeOverride=kind=="size-zero"?0:kind=="size-small"?sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W)-1:65537;expected="identity-current-interface-unproven";}
    else if(kind=="detail-fail"){fake::detailFillFail=true;expected="identity-current-interface-unproven";}
    else if(kind=="invalid-SPdata"){fake::invalidDetailData=true;expected="identity-current-interface-resolver-rejected";}
    else if(kind=="resolver-reject"){fake::resolveOk=false;expected="identity-current-interface-resolver-rejected";}
    else if(kind=="set-open"){fake::setupOpenFail=true;expected="identity-present-set-open-failed";}
    else {fake::Interface row{fake::source.interfacePath,fake::source.usbInstance,fake::owned};if(kind=="vid")row.vid=0x1234;else if(kind=="pid")row.pid=0x1901;else row.root="USB\\VID_0DB0&PID_1902\\OTHER";fake::interfaces={row};expected="identity-current-source-binding-mismatch";}
    msiCrashRecoveryMaintenance(1000);const auto id="helper-"+kind+"-observable-denial";
    check(id.c_str(),fake::restores==0&&fake::modeWrites==0&&!durable().hiddenPaths.empty()&&logged(expected)&&
      fake::setCreates==fake::setDestroys&&fake::sets.empty()&&fake::handles.empty(),
      "Denied current identity emits its actual failure decision, keeps ownership and destroys every acquired set",{{"reason",expected},{"setsCreated",fake::setCreates},{"setsDestroyed",fake::setDestroys}});
  }
  for(const auto& kind:std::vector<std::string>{"generation","interface","source-valid","source-gone"}){
    seed(false);fake::resolveHook=[&]{if(kind=="generation")++fake::source.generation;else if(kind=="interface")fake::source.interfacePath="NEW-INTERFACE";else if(kind=="source-valid")g_msiDinputSourceValid=false;else fake::sourcePresent=false;};
    msiCrashRecoveryMaintenance(1000);const auto id="helper-recheck-"+kind+"-denied";
    check(id.c_str(),fake::restores==0&&fake::modeWrites==0&&logged("context-changed-during-DInput-identity-resolution")&&releasedSet(),
      "Actual maintenance rejects a source changed during resolver I/O and logs the recheck failure");
  }
  for(const unsigned count:{512u,513u}){
    seed(false);for(unsigned i=0;i<count;++i)fake::interfaces.push_back({i+1==count?fake::source.interfacePath:"OTHER-"+std::to_string(i),fake::source.usbInstance,fake::owned});
    HidHidePhysicalIdentity bounded;const bool found=msiCrashResolveDinputIdentity(fake::source,bounded);
    check(count==512?"helper-index-511-admitted":"helper-index-512-bounded-denial",found==(count==512)&&releasedSet()&&(found||logged("identity-present-enumeration-limit")),"Actual helper has an inclusive index-511 ceiling and always destroys its set");
  }
  seed(false);fake::interfaces={{hidHideUpper(fake::source.interfacePath),fake::source.usbInstance,fake::owned}};HidHidePhysicalIdentity caseIdentity;
  check("helper-exact-case-insensitive-path",msiCrashResolveDinputIdentity(fake::source,caseIdentity)&&releasedSet(),"Only exact ASCII case-insensitive interface equality is admitted");
  if(archive.contains("startupFixture")&&!archive.at("startupFixture").is_null()){
    const auto startup=archive.at("startupFixture");
    auto startupSeed=[&]{
      fake::storage.clear();fake::storePresent=false;fake::archiveFiles.clear();restart(startup.at("loaded").at("row").at("pid").get<DWORD>());
      fake::boardKey=startup.at("boardKey").get<std::string>();fake::logs.clear();fake::commits=fake::reads=fake::restores=fake::unhideWrites=fake::modeWrites=fake::mode2Writes=0;
      const auto& observed=startup.at("source").at("row");fake::source.interfacePath=observed.at("interfacePath");fake::source.usbInstance=observed.at("usbInstance");
      fake::source.vendorId=observed.at("vendorId");fake::source.productId=observed.at("productId");fake::source.generation=observed.at("generation");
      auto r=record();const auto& loaded=startup.at("loaded").at("row");r.rootInstance=loaded.at("rootInstance");r.ownerPid=loaded.at("recordOwnerPid");
      r.ownerCreationTime=loaded.at("recordOwnerCreationTime");r.modeReturnPending=loaded.at("modeReturnPending");r.hiddenPaths=loaded.at("ownedHiddenPaths").get<std::vector<std::string>>();r.transactionId="141537-reconstructed-inherited-journal";
      fake::hidden={U2W(fake::foreign)};for(const auto& p:r.hiddenPaths)fake::hidden.push_back(U2W(p));save(r);msiCrashRecoveryInitialize();
    };
    startupSeed();msiCrashRecoveryMaintenanceLegacyCall(1000);
    check("141537-old-call-reproduces-loaded-OFF-stall",g_msiCrashInheritedPending&&fake::restores==0&&fake::modeWrites==0&&fake::invalidSetUses>0,
      "Archive-loaded ownership and OFF plus fresh fake DInput still fail with the original invalid resolver call",{{"archive",startup}});
    startupSeed();fake::modeOk=true;msiCrashRecoveryMaintenance(1000);const auto returned=durable();
    msiCrashRecoveryMaintenance(3000);msiCrashRecoveryMaintenance(5000);
    const auto entered=std::count_if(fake::logs.begin(),fake::logs.end(),[](const json& row){return row.value("reason",std::string{})=="maintenance-entered";});
    bool exactReturned=true;for(const auto& p:startup.at("loaded").at("row").at("ownedHiddenPaths"))exactReturned&=!hidHideContainsPath(fake::hidden,p.get<std::string>());
    check("141537-OFF-inherited-fresh-DInput-owned-return-single-mode1",exactReturned&&returned.hiddenPaths.empty()&&fake::unhideWrites==2&&fake::modeWrites==1&&fake::mode2Writes==0&&hidHideContainsPath(fake::hidden,fake::foreign)&&returned.modeReturnPending&&entered==1&&releasedSet(),
      "Actual fixed helper/maintenance return the two archived owned paths and one mode1; write success retains unverified mode debt; no Host/mode2",{{"archive",startup},{"journal",json::parse(fake::storage)},{"modeWrites",fake::modeWrites},{"maintenanceEnteredRows",entered},{"locationsAreFake",true},{"freshnessAndOldOwnerDeathAreFake",true}});
  }
  // Actual load -> actual maintenance -> fake driver callback -> actual settlement.
  seed(false); msiCrashRecoveryMaintenance(1000);
  check("healthy-hidden-only", !hidHideContainsPath(fake::hidden, fake::owned) && durable().hiddenPaths.empty() && fake::modeWrites == 0,
    "Exact owned paths are returned without an unnecessary mode write");
  check("foreign-preserved", hidHideContainsPath(fake::hidden, fake::foreign), "Unknown preexisting entry survives recovery");

  seed(); fake::ownerDead = false; msiCrashRecoveryMaintenance(1000);
  check("live-old-owner-denied", fake::restores == 0 && fake::modeWrites == 0, "A live/unknown prior owner grants no recovery write");
  seed(); g_realStickTest.virtualTargetEnabled = true; msiCrashRecoveryMaintenance(1000);
  check("virtual-on-denied", fake::restores == 0 && fake::modeWrites == 0, "An enabled virtual target blocks startup return");

  seed(false); fake::unhideOk = false; msiCrashRecoveryMaintenance(1000);
  const auto first = fake::restores; fake::unhideOk = true; msiCrashRecoveryMaintenance(3000);
  check("unhide-failure-self-heals", !hidHideContainsPath(fake::hidden, fake::owned) && durable().hiddenPaths.empty(),
    "A failed exact unhide can recover on a later admitted maintenance tick",
    {{"initialRestoreCalls", first}, {"finalRestoreCalls", fake::restores}, {"startupAttempted", g_msiCrashStartupAttempted}, {"sourcePresent", fake::sourcePresent}, {"durableHiddenPaths", durable().hiddenPaths}});
  check("unhide-retry-is-bounded", fake::restores==2 && g_msiCrashUnhideAttempts==2,
    "The later successful admitted retry uses the same bounded hidden-return budget");

  seed(false); admitXinput();
  msiCrashRecoveryMaintenance(1000);
  check("xinput-hidden-only-return", !hidHideContainsPath(fake::hidden, fake::owned) && durable().hiddenPaths.empty() && fake::modeWrites == 0,
    "A current matching XInput source permits hidden-only recovery", {{"restores", fake::restores}, {"pending", g_msiCrashInheritedPending.load()}});

  seed(false);admitXinput();g_xinputPhysicalSourceLocked=false;g_xinputPhysicalSourceMask=0;
  msiCrashRecoveryMaintenance(1000);
  check("disabled-startup-xinput-needs-no-virtual-source-lock",durable().hiddenPaths.empty()&&fake::modeWrites==0,
    "OFF startup uses fresh physical read evidence and exact root ownership without requiring a virtual acquisition lock");
  for(const auto& kind:std::vector<std::string>{"unknown","virtual","stale-power","stale-topology","unstable","empty-mask"}) {
    seed(false);admitXinput();auto& sample=g_inputCaptureGamepadSnapshot;
    if(kind=="unknown")sample.inputSource="none";else if(kind=="virtual")sample.inputSource="virtual";
    else if(kind=="stale-power")--sample.readPowerGeneration;else if(kind=="stale-topology")--sample.readTopologyGeneration;
    else if(kind=="unstable")sample.readContextStable=false;else sample.connectedMask=0;
    msiCrashRecoveryMaintenance(1000);const auto id="xinput-read-proof-"+kind+"-denied";
    check(id.c_str(),fake::restores==0&&fake::modeWrites==0&&!durable().hiddenPaths.empty(),
      "Exact PnP root alone cannot recover without eligible current-generation physical read evidence");
  }
  seed(true, false); msiCrashRecoveryMaintenance(1000); const unsigned writes = fake::modeWrites;
  const auto claimed = durable(); restart(102); msiCrashRecoveryInitialize(); msiCrashRecoveryMaintenance(1000);
  check("new-process-attempt-is-explicit-owner-scope", fake::modeWrites == writes+1 && durable().modeAttemptOwner != claimed.modeAttemptOwner,
    "Current contract permits one new consume after the old owner is proven dead; it is per-process-owner, not per-original-transaction",
    {{"writesBeforeRestart", writes}, {"writesAfterRestart", fake::modeWrites}, {"firstAttemptOwner", claimed.modeAttemptOwner}, {"secondAttemptOwner", durable().modeAttemptOwner}});

  seed(false); fake::failCommit = true; msiCrashRecoveryMaintenance(1000);
  check("failed-close-keeps-durable-debt", !durable().hiddenPaths.empty(), "Failed durable settlement retains the prior durable responsibility");
  fake::failCommit = false; msiCrashRecoveryMaintenance(3000);
  check("failed-close-can-settle-later", durable().hiddenPaths.empty(),
    "Returned configuration can be durably settled on a later tick after storage resumes", {{"restores", fake::restores}, {"startupAttempted", g_msiCrashStartupAttempted}});

  seed(); fake::hardwareGate = false; msiCrashRecoveryMaintenance(1000);
  check("hardware-gate-denied", fake::restores == 0 && fake::modeWrites == 0,
    "Recovery respects the existing hardware-write gate", {{"restores", fake::restores}, {"modeWrites", fake::modeWrites}});
  seed(); g_inputHostPrepared = true; msiCrashRecoveryMaintenance(1000);
  check("prepared-host-denied", fake::restores == 0 && fake::modeWrites == 0,
    "Prepared/shared target ownership is not treated as absence merely because the current process handle is null",
    {{"restores", fake::restores}, {"modeWrites", fake::modeWrites}});
  seed(); fake::resolveHook = [] { g_realStickTest.virtualTargetEnabled = true; };
  msiCrashRecoveryMaintenance(1000);
  check("query-time-context-change-denied", fake::restores == 0 && fake::modeWrites == 0,
    "A target intent change during identity query is revalidated before return writes",
    {{"restores", fake::restores}, {"modeWrites", fake::modeWrites}});

  // Targeted new WAL boundary tests. sgWriteFileAtomic mutates fake facts
  // after committing real-codec bytes, before the actual hook returns.
  const auto walPhysical=[] {HidHidePhysicalIdentity p;p.baseContainerDeviceInstanceId=fake::root;return p;}();
  HidHideVisibilitySnapshot walBefore;walBefore.hidden={U2W(fake::foreign)};
  for(const auto& kind:std::vector<std::string>{"power","epoch","exit","hardware"}) {
    seed(false,false);fake::afterCommit=[&] {if(kind=="power")++fakePower;else if(kind=="epoch")++fakeEpoch;else if(kind=="exit")fake::exitFence=true;else fake::hardwareGate=false;};
    const bool allowed=msiCrashRecoveryBeforeModeWrite(2,{"FAKE-COMMAND"});
    const auto id="after-mode2-WAL-"+kind+"-denied";
    check(id.c_str(),!allowed&&durable().modeReturnPending,
      "Changed context after mode2 durable commit denies dispatch and retains intent",{{"allowed",allowed},{"commits",fake::commits}});
    seed(false,false);fake::afterCommit=[&] {if(kind=="power")++fakePower;else if(kind=="epoch")++fakeEpoch;else if(kind=="exit")fake::exitFence=true;else fake::hardwareGate=false;};
    const bool hideAllowed=msiCrashRecoveryBeforeHide(walPhysical,{fake::owned},walBefore,"wal-context-change");
    const auto hideId="after-hide-WAL-"+kind+"-denied";
    check(hideId.c_str(),!hideAllowed&&durable().hiddenPaths.size()==1&&!hidHideContainsPath(fake::hidden,fake::owned),
      "Changed context after hide durable commit denies dispatch while durable ownership remains",{{"allowed",hideAllowed},{"commits",fake::commits}});
  }
  for(const auto& kind:std::vector<std::string>{"stable","power","epoch","owed","route","target","hardware","exit"}) {
    seed(true,false);directMode1EntryReady();fake::afterCommit=[&] {
      if(kind=="power")++fakePower;else if(kind=="epoch")++fakeEpoch;else if(kind=="owed")g_msiDinputReturnOwed=false;
      else if(kind=="route")g_msiDinputRouteDinputArmed=true;else if(kind=="target")g_msiDinput.pad=reinterpret_cast<void*>(1);
      else if(kind=="hardware")fake::hardwareGate=false;else if(kind=="exit")fake::exitFence=true;
    };
    const bool allowed=msiCrashRecoveryBeforeModeWrite(1,{"FAKE-COMMAND"});
    const bool expected=kind=="stable"||kind=="exit";
    const auto id="after-mode1-WAL-"+kind+(expected?"-allowed":"-denied");
    check(id.c_str(),allowed==expected&&durable().modeReturnPending&&!durable().modeAttemptOwner.empty(),
      "Direct mode1 starts owed, route disarmed, actual target absence; after-WAL uses the captured actual existing terminal verdict",
      {{"allowed",allowed},{"expected",expected},{"actualTargetAbsent",msiDinputDeviceTargetAbsent()},{"commits",fake::commits}});
  }

  // Storage cut points: external fake state survives logical process restart.
  auto walSeed = [] { seed(false, false); fake::hidden = {U2W(fake::foreign)}; };
  HidHidePhysicalIdentity physical; physical.baseContainerDeviceInstanceId = fake::root;
  HidHideVisibilitySnapshot before; before.hidden = {U2W(fake::foreign)};
  walSeed(); fake::cutBeforeCommit = true;
  try { (void)msiCrashRecoveryBeforeHide(physical, {fake::owned}, before, "wal-cut"); } catch (const fake::Cut&) {}
  check("cut-before-WAL-has-no-hidden-addition", durable().hiddenPaths.empty() && !hidHideContainsPath(fake::hidden, fake::owned),
    "A cut before durable write-ahead commit creates no external hidden entry");
  walSeed(); fake::cutAfterCommit = true;
  try { (void)msiCrashRecoveryBeforeHide(physical, {fake::owned}, before, "wal-cut"); } catch (const fake::Cut&) {}
  check("cut-after-WAL-before-hide-retains-intent", durable().hiddenPaths.size() == 1 && !hidHideContainsPath(fake::hidden, fake::owned),
    "Durable intent survives a cut before the external hide");
  restart(102); msiCrashRecoveryInitialize(); msiCrashRecoveryMaintenance(1000);
  check("no-effect-WAL-can-settle", durable().hiddenPaths.empty() && fake::unhideWrites == 0,
    "Readback closes an unapplied hide intent without issuing an unhide");
  walSeed(); const bool admitted = msiCrashRecoveryBeforeHide(physical, {fake::owned}, before, "wal-hide");
  if (admitted) fake::hidden.push_back(U2W(fake::owned));
  restart(102); msiCrashRecoveryInitialize(); msiCrashRecoveryMaintenance(1000);
  check("cut-after-hide-recovers-owned-path", admitted && durable().hiddenPaths.empty() && !hidHideContainsPath(fake::hidden, fake::owned),
    "Exact hide responsibility survives the process that dispatched the hide");
  walSeed(); fake::failCommit = true;
  check("failed-hide-WAL-denies-dispatch", !msiCrashRecoveryBeforeHide(physical, {fake::owned}, before, "failed-wal") && durable().hiddenPaths.empty(),
    "Failed write-ahead persistence does not authorize an external hide");
  seed(true, false); directMode1EntryReady(); fake::failCommit = true;
  check("failed-mode-WAL-denies-dispatch", !msiCrashRecoveryBeforeModeWrite(1, {"FAKE-COMMAND"}) && durable().modeAttemptOwner.empty(),
    "Failed mode-attempt persistence grants no mode write");
  seed(true, false); directMode1EntryReady(); fake::cutAfterCommit = true;
  try { (void)msiCrashRecoveryBeforeModeWrite(1, {"FAKE-COMMAND"}); } catch (const fake::Cut&) {}
  const auto cutClaim = durable(); restart(102); msiCrashRecoveryInitialize(); msiCrashRecoveryMaintenance(1000);
  check("cut-after-mode-claim-new-owner-scope", !cutClaim.modeAttemptOwner.empty() && fake::modeWrites == 1 && durable().modeAttemptOwner!=cutClaim.modeAttemptOwner,
    "Current explicit per-owner contract grants one different-owner attempt after a cut following durable claim", {{"modeWrites", fake::modeWrites}});

  seed(true, false); fake::resolveOk = false;
  check("command-identity-refusal-denies-mode-WAL", !msiCrashRecoveryBeforeModeWrite(2, {"FAKE-VENDOR-COMMAND"}) && fake::commits == 0,
    "Runtime denies a command interface refused by its selected identity resolver; resolver suitability needs separate source review");

  seed(false);fake::unhideOk=false;
  for(unsigned n=1;n<=5;++n)msiCrashRecoveryMaintenance(n*1000);
  check("three-unhide-limit",fake::restores==3&&g_msiCrashUnhideAttempts==3&&!durable().hiddenPaths.empty(),
    "At most three idempotent hidden-return attempts retain unresolved duty",{{"restoreCalls",fake::restores}});
  seed();fake::unhideOk=false;msiCrashRecoveryMaintenance(1000);admitXinput();fake::unhideOk=true;msiCrashRecoveryMaintenance(3000);
  check("mode-transition-does-not-strand-hidden-retry",durable().hiddenPaths.empty()&&fake::modeWrites==1,
    "A fresh matching XInput source lets the remaining hidden return finish without a second mode write");
  seed(false);fake::failCommit=true;for(unsigned n=1;n<=3;++n)msiCrashRecoveryMaintenance(n*1000);
  fake::failCommit=false;msiCrashRecoveryMaintenance(4000);
  check("storage-close-retry-independent-of-unhide-budget",durable().hiddenPaths.empty(),
    "A storage-only close failure can settle already returned configuration after device-write retries are exhausted",
    {{"restoreCalls",fake::restores},{"unhideWrites",fake::unhideWrites},{"unhideAttempts",g_msiCrashUnhideAttempts}});
  seed(false);fake::unhideOk=false;
  for(unsigned n=1;n<=30;++n)msiCrashRecoveryMaintenance(n*1000);
  check("readback-settlement-has-independent-bounded-budget",fake::restores==3&&fake::snapshotReads==20&&g_msiCrashSettlementChecks==20&&!durable().hiddenPaths.empty(),
    "After three driver attempts, twenty read-only checks retain unresolved responsibility and perform no extra writes",
    {{"restores",fake::restores},{"snapshotReads",fake::snapshotReads},{"modeWrites",fake::modeWrites}});
  seed(false);fake::failCommit=true;for(unsigned n=1;n<=3;++n)msiCrashRecoveryMaintenance(n*1000);
  fake::failCommit=false;fake::visibilityReadOk=false;msiCrashRecoveryMaintenance(4000);
  check("failed-readback-cannot-settle-return",!durable().hiddenPaths.empty()&&fake::restores==3&&fake::snapshotReads==1,
    "A failed fresh readback cannot clear durable hidden ownership even after fake external state was restored");
  fake::visibilityReadOk=true;msiCrashRecoveryMaintenance(5000);
  check("readback-recovery-settles-without-driver-retry",durable().hiddenPaths.empty()&&fake::restores==3&&fake::snapshotReads==2&&fake::modeWrites==0,
    "A later valid snapshot can durably close returned state with no fourth unhide or mode write");
  restart(102);
  check("fresh-owner-resets-settlement-budget",g_msiCrashSettlementChecks==0&&g_msiCrashUnhideAttempts==0,
    "The isolated fixture resets both budgets when simulating the actual production globals of a fresh process");
  seed(false);g_msiFeedbackSampleTopology=3;msiCrashRecoveryMaintenance(1000);
  check("stale-topology-denied",fake::restores==0&&fake::modeWrites==0,"A stale DInput source cannot grant recovery");
  seed(false);g_msiFeedbackSamplePower=1;msiCrashRecoveryMaintenance(1000);
  check("stale-power-denied",fake::restores==0&&fake::modeWrites==0,"A stale power sample cannot grant recovery");
  seed(false);g_msiFeedbackSampleMode=2;msiCrashRecoveryMaintenance(1000);
  check("stale-mode-denied",fake::restores==0&&fake::modeWrites==0,"A stale mode sample cannot grant recovery");
  seed(false);fake::resolveHook=[] { ++fakePower; };msiCrashRecoveryMaintenance(1000);
  check("power-changed-during-query-denied",fake::restores==0&&fake::modeWrites==0,
    "Power generation is rechecked after identity query and before recovery dispatch");
  seed(false);fake::locations={L"PCIROOT(0)#PCI(1400)#USBROOT(0)#USB(9)"};admitXinput();msiCrashRecoveryMaintenance(1000);
  check("cross-mode-location-conflict-denied",fake::restores==0&&fake::modeWrites==0,
    "Different cross-mode roots require exact matching retained location evidence");
  seed(false);admitXinput();HidHidePhysicalIdentity duplicate;duplicate.baseContainerDeviceInstanceId=fake::source.usbInstance;duplicate.productId=0x1901;
  fake::additionalSources={duplicate};msiCrashRecoveryMaintenance(1000);
  check("multiple-matching-xinput-sources-denied",fake::restores==0,"Ambiguous matching physical candidates grant no return");
  seed(false);for(unsigned n=0;n<80;++n){g_realStickTest.virtualTargetEnabled=(n%2)==0;fake::sourcePresent=false;msiCrashRecoveryMaintenance((n+1)*1000);}
  check("decision-log-budget-32",g_msiCrashDecisionRows<=32,"Changed recovery-decision rows stay within the configured per-process budget",
    {{"decisionRows",g_msiCrashDecisionRows}});

  if(archive.contains("files") && !archive.at("files").empty()) {
    auto migrationSeed=[&] {
      fake::storage.clear();fake::storePresent=false;restart(9740);fake::logs.clear();
      fake::boardKey=archive.at("boardKey").get<std::string>();fake::archiveFiles=archive.at("files").get<std::map<std::string,std::string>>();
      fake::commits=fake::reads=fake::restores=fake::unhideWrites=fake::modeWrites=0;
      fake::hidden={U2W(fake::foreign)};
    };
    migrationSeed();msiCrashRecoveryInitialize();const auto migrated=durable();
    check("1131519-actual-legacy-migration",migrated.ownerPid==9956&&migrated.hiddenPaths.size()==4&&migrated.modeReturnPending,
      "Actual parser retains the latest admission after the earlier discharged detail event; exact four owned paths",
      {{"record",json::parse(fake::storage)},{"fakeStorageCommits",fake::commits}});
    const auto expectedLocations=archive.at("fixtureRows").at("location").at("physicalLocationPaths").get<std::vector<std::string>>();
    check("1131519-real-location-paths-bound",migrated.locationPaths==expectedLocations,
      "Actual migration binds both retained full physicalLocationPaths to the same PID and exact sourceRoot",
      {{"expectedLocations",expectedLocations},{"actualLocations",migrated.locationPaths},
       {"lastAdmission",archive.at("fixtureRows").at("admission").at("time")},
       {"olderDischarge",archive.at("fixtureRows").at("discharge").at("time")}});
    fake::source.usbInstance=migrated.rootInstance;
    for(const auto& p:migrated.hiddenPaths)fake::hidden.push_back(U2W(p));
    msiCrashRecoveryMaintenance(1000);
    check("1131519-exact-root-capture-and-return",durable().hiddenPaths.empty()&&fake::modeWrites==1&&hidHideContainsPath(fake::hidden,fake::foreign)&&!durable().locationPaths.empty(),
      "Exact legacy root is observed before synthetic current locations are captured and only migrated owned entries are returned",
      {{"locationsAreSynthetic",true},{"durable",json::parse(fake::storage)},{"modeWrites",fake::modeWrites},{"unhideWrites",fake::unhideWrites}});
    // Full archive + fake current cross-mode observation. Its location values are
    // genuine retained archive data; live identity/liveness/driver state are fake.
    migrationSeed();msiCrashRecoveryInitialize();const auto xinputMigrated=g_msiCrashRecord;
    for(const auto& p:xinputMigrated.hiddenPaths)fake::hidden.push_back(U2W(p));
    fake::locations.clear();for(const auto& p:expectedLocations)fake::locations.push_back(U2W(p));
    admitXinput();msiCrashRecoveryMaintenance(1000);
    check("1131519-current-xinput-unhide-only",g_msiCrashRecord.hiddenPaths.empty()&&g_msiCrashRecord.modeReturnPending&&fake::modeWrites==0&&fake::unhideWrites==4&&fake::observerInitials==1&&fake::observerEpisode!=0&&fake::observerSawDurableOwner,
      "Retained locations bridge fake current 1901 root without PID substitution; owned paths return without mode1; adopted mode duty waits for the simulated observer",
      {{"currentObservationIsFake",true},{"locationValuesFromArchive",true},{"record",json::parse(fake::storage)},
       {"modeWrites",fake::modeWrites},{"unhideWrites",fake::unhideWrites}});
    auto boundarySeed=[&] {
      migrationSeed();const auto& rows=archive.at("fixtureRows");
      fake::archiveFiles["native-lifecycle.log.1"].clear();
      fake::archiveFiles["native-lifecycle.log"]=rows.at("board").dump()+"\n"+rows.at("location").dump()+"\n"+rows.at("admission").dump()+"\n";
      fake::archiveFiles["native-detail.log.1"].clear();fake::archiveFiles["native-detail.log"].clear();
    };
    auto appendRestore=[&](const char* status,const char* time,const char* transaction=nullptr) {
      auto row=archive.at("fixtureRows").at("restore");row["status"]=status;row["ownedTargetsReturned"]=true;row["time"]=time;
      row["transactionId"]=transaction?json(transaction):archive.at("fixtureRows").at("admission").at("transactionId");
      fake::archiveFiles["native-lifecycle.log"]+=row.dump()+"\n";
    };
    auto appendDischarge=[&](const char* time,const char* basis="xinput-readmitted",DWORD pid=9956) {
      auto row=archive.at("fixtureRows").at("discharge");row["time"]=time;row["basis"]=basis;row["pid"]=pid;
      fake::archiveFiles["native-detail.log"]+=row.dump()+"\n";
    };
    auto stateEvidence=[&] {return json{{"syntheticFollowupEvents",true},{"recordPresent",fake::storePresent},
      {"hiddenPaths",g_msiCrashRecord.hiddenPaths},{"modeReturnPending",g_msiCrashRecord.modeReturnPending},
      {"pending",g_msiCrashInheritedPending.load()},{"record",fake::storePresent?json::parse(fake::storage):json(nullptr)}};};
    boundarySeed();appendRestore("release-unproven","2026-10-14T11:30:01.000");msiCrashRecoveryInitialize();
    check("legacy-ownedTargetsReturned-clears-only-hidden",g_msiCrashRecord.hiddenPaths.empty()&&g_msiCrashRecord.modeReturnPending,
      "Owned config readback discharges hidden debt even when virtual endpoint absence is release-unproven; mode debt remains",stateEvidence());
    boundarySeed();appendRestore("restored","2026-10-14T11:30:01.000");msiCrashRecoveryInitialize();
    check("legacy-restored-not-mode-discharge",g_msiCrashRecord.hiddenPaths.empty()&&g_msiCrashRecord.modeReturnPending,
      "HidHide status restored is not a qualified mode-debt discharge",stateEvidence());
    boundarySeed();appendDischarge("2026-10-14T11:30:02.000");msiCrashRecoveryInitialize();
    check("legacy-qualified-discharge-clears-only-mode",g_msiCrashRecord.hiddenPaths.size()==4&&!g_msiCrashRecord.modeReturnPending,
      "Native detail qualified debt discharge after latest admission clears only mode responsibility",stateEvidence());
    boundarySeed();appendRestore("release-unproven","2026-10-14T11:30:01.000");appendDischarge("2026-10-14T11:30:02.000");msiCrashRecoveryInitialize();
    const bool allSettled=!g_msiCrashInheritedPending&&!msiCrashHasDuty(g_msiCrashRecord);
    check("legacy-settled-record-not-active",allSettled,
      "Both independently settled debts cannot revive a stale legacy root1902 transaction",stateEvidence());
    admitXinput();MsiClawHidDiag manual{"FAKE-COMMAND-INTERFACE"};
    const bool manualAllowed=msiCrashRecoveryBeforeModeWrite(2,manual);
    check("legacy-settled-allows-new-manual-mode2",manualAllowed&&g_msiCrashRecord.rootInstance==fake::source.usbInstance&&g_msiCrashRecord.modeReturnPending,
      "A completed legacy transaction cannot block a new manual mode2 WAL on current exact root1901",stateEvidence());
    boundarySeed();appendRestore("release-unproven","2026-10-14T11:30:01.000","DIFFERENT-TRANSACTION");msiCrashRecoveryInitialize();
    check("legacy-unrelated-restore-does-not-settle",g_msiCrashRecord.hiddenPaths.size()==4&&g_msiCrashRecord.modeReturnPending,
      "Later timestamp alone cannot attribute another transaction's ownedTargetsReturned to this admission",stateEvidence());
    boundarySeed();appendRestore("release-unproven","2026-10-14T11:19:00.000");msiCrashRecoveryInitialize();
    check("legacy-older-owned-return-does-not-settle",g_msiCrashRecord.hiddenPaths.size()==4&&g_msiCrashRecord.modeReturnPending,
      "An old owned-target readback cannot settle a newer complete admission",stateEvidence());
    boundarySeed();appendDischarge("2026-10-14T11:20:20.854");msiCrashRecoveryInitialize();
    check("legacy-older-discharge-does-not-settle",g_msiCrashRecord.hiddenPaths.size()==4&&g_msiCrashRecord.modeReturnPending,
      "The archive's earlier qualified discharge is before final 11:20:30.136 admission",stateEvidence());
    boundarySeed();appendDischarge("2026-10-14T11:30:02.000","release-unproven");msiCrashRecoveryInitialize();
    check("legacy-unqualified-basis-does-not-settle",g_msiCrashRecord.modeReturnPending,
      "A discharged action with an unqualified basis cannot clear mode duty",stateEvidence());
    boundarySeed();appendDischarge("2026-10-14T11:30:02.000","xinput-readmitted",9876);msiCrashRecoveryInitialize();
    check("legacy-other-pid-discharge-does-not-settle",g_msiCrashRecord.modeReturnPending,
      "Other process's debt receipt cannot clear candidate owner duty",stateEvidence());
    boundarySeed();appendRestore("release-unproven","2026-10-14T11:30:01.000");appendDischarge("2026-10-14T11:30:02.000");
    auto laterAdmission=archive.at("fixtureRows").at("admission");laterAdmission["time"]="2026-10-14T11:31:00.000";laterAdmission["transactionId"]="FAKE-NEW-ADMISSION";
    fake::archiveFiles["native-lifecycle.log"]+=laterAdmission.dump()+"\n";msiCrashRecoveryInitialize();
    check("legacy-later-admission-rearms-debts",g_msiCrashRecord.hiddenPaths.size()==4&&g_msiCrashRecord.modeReturnPending&&g_msiCrashRecord.transactionId=="FAKE-NEW-ADMISSION",
      "A later complete admission starts responsibility after earlier settlement",stateEvidence());
    boundarySeed();const auto savedLifecycle=fake::archiveFiles["native-lifecycle.log"];
    fake::archiveFiles["native-lifecycle.log"].clear();
    fake::archiveFiles["native-lifecycle.log"]=archive.at("fixtureRows").at("board").dump()+"\n";
    auto wrongLocation=archive.at("fixtureRows").at("location");wrongLocation["sourceRoot"]="USB\\VID_0DB0&PID_1902\\OTHER-ROOT";
    fake::archiveFiles["native-lifecycle.log"]+=wrongLocation.dump()+"\n"+archive.at("fixtureRows").at("admission").dump()+"\n";msiCrashRecoveryInitialize();
    check("legacy-other-root-location-not-bound",g_msiCrashRecord.locationPaths.empty(),
      "Retained location rows require exact sourceRoot, never shared VID or PID matching",stateEvidence());
    boundarySeed();auto wrongPid=archive.at("fixtureRows").at("location");wrongPid["pid"]=9876;
    fake::archiveFiles["native-lifecycle.log"]=archive.at("fixtureRows").at("board").dump()+"\n"+wrongPid.dump()+"\n"+archive.at("fixtureRows").at("admission").dump()+"\n";
    msiCrashRecoveryInitialize();check("legacy-other-pid-location-not-bound",g_msiCrashRecord.locationPaths.empty(),
      "Retained location evidence must bind to the admission PID",stateEvidence());
    migrationSeed();fake::archiveFiles["msi-controller-visibility.log"].clear();fake::archiveFiles["msi-controller-visibility.log.1"].clear();
    msiCrashRecoveryInitialize();
    check("migration-without-creation-proof-denied",!fake::storePresent&&!g_msiCrashInheritedPending,
      "Missing retained process creation evidence cannot be replaced by current hidden-list membership");
    migrationSeed();fake::boardKey="OTHER|BOARD|SYSTEM|CLASS";msiCrashRecoveryInitialize();
    check("migration-board-conflict-denied",!fake::storePresent&&!g_msiCrashInheritedPending,"A differing board binding cannot migrate old paths");
    migrationSeed();fake::ownerDead=false;msiCrashRecoveryInitialize();
    check("migration-owner-alive-denied",!fake::storePresent&&!g_msiCrashInheritedPending,"A live or unknown old owner prevents migration");
    migrationSeed();fake::cutAfterCommit=true;try{msiCrashRecoveryInitialize();}catch(const fake::Cut&){}
    const auto committedMigration=fake::storage;restart(9741);msiCrashRecoveryInitialize();
    check("migration-cut-after-commit-reloads",g_msiCrashRecord.ownerPid==9956&&g_msiCrashRecord.hiddenPaths.size()==4&&fake::storage==committedMigration,
      "A cut after migration commit reloads durable exact ownership instead of reconstructing it from driver state");
  }
  std::cout << "SIDECAR_RESULT " << json({{"assertions", assertions}, {"failures", failures}, {"results", results},
    {"ownedObserver", "controlled-sidecar-simulation"}, {"twoStableSignalWindowsVerified", false},
    {"productionEntrypointExecuted", false}, {"realDeviceCalls", 0}, {"realRegistryCalls", 0}, {"realHostCalls", 0},
    {"storage", "fake bytes using real captured journal codec"}, {"restart", "logical; persistent fake bytes and driver state survive"}}).dump() << "\n";
  return failures ? 1 : 0;
} catch (const std::exception& e) { std::cerr << "FIXTURE_ERROR " << e.what() << "\n"; return 2; } }
`;
const cpp = prelude + '\n' + actual + '\n' + support;
fs.writeFileSync(path.join(out, 'actual-functions.inc'), actual);
fs.writeFileSync(path.join(out, 'runtime-fake.cpp'), cpp);
const ownedObserver = {
  scope: 'controlled sidecar simulation of msiOwnedReturnRecordInitial and explicit mode-completion callback; production observer policy is not linked',
  covered: ['legacy load/migration/identity', 'owned configuration return', 'durable-before-write gate', 'startup owner/episode handoff', 'journal retirement'],
  twoStableSignalWindowsVerified: false, physicalSignalVerified: false, steamConsumptionVerified: false,
  productionChainVerifier: 'tools/verify-msi-startup-owned-return-runtime.mjs'
};
const binding = {scope: 'actual extracted crash runtime functions; real journal codec; fake storage/process/driver/source and owned-observer boundaries',
  ownedObserver,
  runtime: {path: runtimePath, sha256: sha(runtimeBytes)}, journal: {path: journalPath, sha256: sha(journalBytes)},
  restoreSemantics:restoreBinding,
  interfaceResolver:{path:mainPath,functions:resolverFunctions.map(({body,...rest})=>rest),mockScope:"fake SetupAPI/HID/parent-chain leaves; actual resolver and actual instance-ID function; no Win32 linkage",boundarySnapshot:boundaryBytes?{path:boundarySnapshotPath,sha256:sha(boundaryBytes),resolverFunctionsMatch:resolverBoundaryMatches}:null},
  retirementDisabledMutation:{scope:"actual maintenance with retirement helper bypassed as success to reproduce old unconditional no-duty shutdown; restores use true production predicate/tail",originalMaintenanceSha256:maintenance.sha256,mutatedSha256:sha(legacyRetirementMaintenance)},
  legacyCallMutation:{scope:"explicit old null/invalid call failure control only",originalMaintenanceSha256:maintenance.sha256,mutatedSha256:sha(legacyMaintenance)},
  existingMode1Gate:{path:mainPath,functions:[verdictType,...gateFunctions].map(({body,...rest})=>rest),transport:"fake command boundary; no production entrypoint"},
  archiveFiles:archiveBindings, functions: functions.map(({body, ...rest}) => rest), dependencyAdapter: decodeApiMismatch ? 'ReadState::Record maps to real DecodeState::Valid; production API mismatch remains a finding' : null,
  productionCompilationVerified: false, runnerSha256: sha(cpp)};
fs.writeFileSync(path.join(out, 'source-binding.json'), JSON.stringify(binding, null, 2));
if (args.includes('--generate-only')) { console.log(JSON.stringify({generated: out, ...binding}, null, 2)); process.exit(0); }
const compiler = cp.spawnSync('cl.exe', ['/nologo', '/std:c++20', '/utf-8', '/EHsc', '/MT', '/O2', '/W4', 'runtime-fake.cpp', '/Foruntime-fake.obj', '/Feruntime-fake.exe', '/link', '/SUBSYSTEM:CONSOLE'], {cwd: out, encoding: 'utf8'});
fs.writeFileSync(path.join(out, 'compile.txt'), (compiler.stdout || '') + (compiler.stderr || '') + (compiler.error ? String(compiler.error) : ''));
if (compiler.status !== 0) throw Error('Pure fixture compile failed; see ' + path.join(out, 'compile.txt'));
const run = cp.spawnSync(path.join(out, 'runtime-fake.exe'), [path.join(out,'archive-input.json')], {cwd: out, encoding: 'utf8', timeout: 30000});
fs.writeFileSync(path.join(out, 'run.txt'), (run.stdout || '') + (run.stderr || ''));
const row = (run.stdout || '').split(/\r?\n/).find(s => s.startsWith('SIDECAR_RESULT '));
if (!row) throw Error('Fixture did not emit a complete receipt; see run.txt');
const result = JSON.parse(row.slice('SIDECAR_RESULT '.length));
result.runnerExitCode = run.status;
const afterMainSource=fs.readFileSync(mainPath,'utf8');
result.resolverDependenciesChangedDuringRun=resolverFunctions.some(f=>extract(afterMainSource,f.name).sha256!==f.sha256);
const afterRestore=extract(afterMainSource,'hidHideRestorePhysicalTransaction'),afterStart=extract(afterMainSource,'inputHostStart');
const afterPredicateStart=afterRestore.body.indexOf('const bool msiOwnedReturn='),afterPredicateEnd=afterRestore.body.indexOf('json restoreResultRow',afterPredicateStart),afterTailStart=afterRestore.body.indexOf('msiCrashRecoveryAfterOwnedRestore(restoreAfter,restoreReadback);'),afterGateStart=afterStart.body.indexOf('if (g_inputHostReleaseQuarantined ||');
result.restoreDependenciesChangedDuringRun=extract(afterMainSource,'hidHideVirtualEndpointAbsent').sha256!==endpointFunction.sha256||sha(afterRestore.body.slice(afterPredicateStart,afterPredicateEnd).trim())!==sha(restorePredicate)||sha(afterRestore.body.slice(afterTailStart,afterRestore.body.lastIndexOf('}')).trim())!==sha(restoreTail)||sha(afterStart.body.slice(afterGateStart,endBrace(afterStart.body,afterStart.body.indexOf('{',afterGateStart))))!==sha(startGateBody);
result.sourceChangedDuringRun = sha(fs.readFileSync(runtimePath)) !== binding.runtime.sha256 || sha(fs.readFileSync(journalPath)) !== binding.journal.sha256 || result.resolverDependenciesChangedDuringRun || result.restoreDependenciesChangedDuringRun;
result.dependencyAdapter = binding.dependencyAdapter;
result.ownedObserver = binding.ownedObserver;
result.assertionCounts = {
  original: result.results.filter(r => !/^(startup-adoption-|owned-observer-boundary-)/.test(r.id)).length,
  added: result.results.filter(r => /^(startup-adoption-|owned-observer-boundary-)/.test(r.id)).length,
  total: result.assertions
};
result.retirementDisabledMutation=binding.retirementDisabledMutation;result.restoreSemantics=binding.restoreSemantics;result.existingMode1Gate=binding.existingMode1Gate;result.interfaceResolver=binding.interfaceResolver;result.legacyCallMutation=binding.legacyCallMutation;
fs.writeFileSync(path.join(out, 'result.json'), JSON.stringify(result, null, 2));
fs.writeFileSync(path.join(out,'trace.json'),JSON.stringify({scope:binding.scope,source:binding.runtime,journal:binding.journal,ownedObserver:binding.ownedObserver,archive:archiveBindings,cases:result.results},null,2));
console.log(JSON.stringify({output: out, assertions: result.assertions, failures: result.failures, runnerExitCode: run.status,
  sourceChangedDuringRun: result.sourceChangedDuringRun, dependencyAdapter: result.dependencyAdapter,
  assertionCounts: result.assertionCounts, ownedObserver: result.ownedObserver,
  failed: result.results.filter(r => !r.passed).map(r => r.id)}, null, 2));
process.exitCode = run.status === 2 ? 2 : result.failures || result.sourceChangedDuringRun || run.status !== 0 ? 1 : 0;