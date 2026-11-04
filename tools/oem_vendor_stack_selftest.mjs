import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {spawnSync} from 'node:child_process';
import {createHash} from 'node:crypto';
import {calibrationToolchainPreamble} from './fixtures/hc_reviewed_calibration/toolchain.mjs';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const out=path.resolve(process.env.YMCC_VENDOR_OUT || path.join(root,'../../Build/Validation/oem-vendor-stack-r15'));
fs.mkdirSync(out,{recursive:true});
const mainPath=process.env.YMCC_VENDOR_SOURCE || path.join(root,'native/main.cpp');
const main=fs.readFileSync(mainPath,'utf8');
const modern=main.includes('#include "oem_vendor_stack.h"');
const block=modern ? fs.readFileSync(path.join(path.dirname(mainPath),'oem_vendor_stack.h'),'utf8') : main.slice(main.indexOf('struct OemVendorServiceLease {'),main.indexOf('static bool sgShouldKeepInputDuringStandby()',main.indexOf('struct OemVendorServiceLease {')));
assert(block.includes('oemVendorStackRequest('));
const enumStart=main.indexOf('enum class YmccFamily');
const family=main.slice(enumStart,main.indexOf('};',enumStart)+2);
const pre=fs.readFileSync(path.join(root,'tools/oem_vendor_stack_fixture.h'),'utf8').replace('/* FAMILY_ENUM */',family);

const nativeJournalIo=process.argv.includes('--journal-io');
assert(!(nativeJournalIo&&process.argv.includes('--journal-storm')),'Select one original native consumer branch');
const hasShieldPolicy = block.includes('static bool msiControllerShieldRequested(');
function productionSlice(start, end) {
  const begin = main.indexOf(start), finish = main.indexOf(end, begin);
  assert(begin >= 0 && finish > begin, 'Missing production shield boundary: ' + start);
  return main.slice(begin, finish);
}
// Execute the real IPC lambdas and boolean-CAS function. Only settings storage,
// device identification and Win32 boundaries are replaced by in-memory fixtures.
const shieldIpc = hasShieldPolicy ? '\n#define YMCC_SHIELD_IPC_FIXTURE\n#include <limits>\n' + String.raw`
static json g_shieldIpcSettings = json::object();
static bool g_shieldWriteFails = false, g_shieldWriteConflict = false;
static unsigned g_shieldWrites = 0;
static std::map<std::string, std::function<json(const json&)>> g_shieldIpc;
struct SettingsFileGuard {};
static json ymSettingsReadUnlocked() { return g_shieldIpcSettings; }
static json ymSettingsSection(const char* section) {
    const auto entry = g_shieldIpcSettings.find(section);
    return entry != g_shieldIpcSettings.end() && entry->is_object() ? *entry : json::object();
}
static bool ymSettingsWriteDocumentUnlocked(const std::string& text, void*, void*, void*, bool,
                                            long long, bool* conflict) {
    ++g_shieldWrites;
    *conflict = g_shieldWriteConflict;
    if (g_shieldWriteFails || g_shieldWriteConflict) return false;
    g_shieldIpcSettings = json::parse(text);
    return true;
}
static void ymccResolveMachineIdentityOnce() {}
static const char* ymccFamilyName(YmccFamily family) { return family == YmccFamily::MsiClaw ? "MsiClaw" : "Other"; }
static void ipc_on(const char* command, std::function<json(const json&)> handler) { g_shieldIpc[command] = std::move(handler); }
` + productionSlice('static bool ymSettingsSetInputBooleanCas(', '\nstatic bool ymSettingsExists()') +
productionSlice('static json msiControllerShieldTargetProcessesJson()', '\nstatic bool sgShouldKeepInputDuringStandby()') +
'\nstatic void registerShieldIpcFixture() {\n' + productionSlice('    ipc_on("msiControllerShield.get",', main.includes('    nativeShortcutActionRegisterIpc();') ? '    nativeShortcutActionRegisterIpc();' : '    ipc_on("shortcut.recording.set",') + '\n}\n' : '';
if (hasShieldPolicy) {
  assert.equal((main.match(/msiControllerShieldRequested\(input\)/g) || []).length, 2,
    'Startup refresh and getter must use the same default policy');
  assert(main.includes('msiControllerShieldRequested(ymSettingsSection("input"))'),
    'Rejected writes must read the same saved/default policy');
  assert(!main.includes('input.value("msiControllerShieldEnabled", false)'), 'No leftover off-by-default reader');
}

const wakePower = productionSlice('static bool msiControllerShieldWakePowerCurrent(', '#include "oem_vendor_stack.h"');
const wakeHook = productionSlice('static void sgHandheldResumeInputOnWake(', '\nstatic void handlePowerResumeNotification(');
assert(wakeHook.includes('oemVendorStackShieldWakeBegin(currentPowerGeneration(), reason)'), 'Accepted handheld wake must start guard');
assert(main.includes('sgHandheldResumeInputOnWake(reason ? reason : "pbt-resume")'), 'Windows resume path must use wake hook');
assert(main.includes('sgHandheldResumeInputOnWake(userWake ? "s0-wake-user" : "s0-wake-auto")'), 'S0 resume path must use wake hook');
const pin=file=>{const bytes=fs.readFileSync(file);return {path:path.resolve(file),bytes:bytes.length,sha256:createHash('sha256').update(bytes).digest('hex')};};
const declaredInputs=[mainPath,...(modern?[path.join(path.dirname(mainPath),'oem_vendor_stack.h'),...(block.includes('#include "msi_task_xml_control.h"')?[path.join(path.dirname(mainPath),'msi_task_xml_control.h')]:[])]:[]),path.join(root,'tools/oem_vendor_stack_selftest.cpp'),path.join(root,'tools/oem_vendor_stack_fixture.h'),path.join(root,'tools/msi_controller_shield_wake_cases.h'),path.join(root,'tools/oem_vendor_stack_selftest.mjs'),path.join(root,'tools/fixtures/hc_reviewed_calibration/toolchain.mjs'),path.join(root,'deps/json/json.hpp')].map(pin);
const journalWriter=nativeJournalIo?productionSlice('bool sgWriteFileAtomicFlags(', 'static bool sgWriteFileAtomicTransient('):'';
const writerBridge=nativeJournalIo?'\n#define sgWriteFileAtomic oemFixtureProductionWrite\n'+journalWriter+'\n#undef sgWriteFileAtomic\n':'';
const generated=path.join(out,'oem-vendor-stack.fixture.cpp');
fs.writeFileSync(generated,(modern?'#define YMCC_VENDOR_STACK_FIXTURE\n#define YMCC_VENDOR_R15\n':'')+(nativeJournalIo?'#define YMCC_VENDOR_NATIVE_IO\n':'')+pre+'\n'+writerBridge+wakePower+block+shieldIpc+wakeHook+'\n#include "'+path.join(root,'tools/oem_vendor_stack_selftest.cpp').replaceAll('\\','/')+'"\n');
const exe=path.join(out,'oem-vendor-stack.fixture.exe');
const cmd=path.join(out,'compile.cmd');
fs.writeFileSync(cmd,calibrationToolchainPreamble('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat')+'cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /Bv /sourceDependencies "'+path.join(out,'source-dependencies.json')+'" /I"'+path.join(root,'deps/json')+'" "'+generated+'" /Fo"'+path.join(out,'fixture.obj')+'" /Fe"'+exe+'"\r\nexit /b %errorlevel%\r\n');
const compile=spawnSync('cmd.exe',['/d','/c',cmd],{encoding:'utf8',cwd:out});
fs.writeFileSync(path.join(out,'build.log'),(compile.stdout||'')+(compile.stderr||''));
process.stdout.write(compile.stdout||'');process.stderr.write(compile.stderr||'');
assert.equal(compile.status,0,'Fixture must compile');
const nativeArgs=nativeJournalIo?['--journal-io']:process.argv.includes('--journal-storm')?['--journal-storm']:[];
const run=spawnSync(exe,nativeArgs,{encoding:'utf8',cwd:out});
const output=(run.stdout||'')+(run.stderr||'');
fs.writeFileSync(path.join(out,'results.txt'),output);process.stdout.write(output);
fs.writeFileSync(path.join(out,'source-binding.json'),JSON.stringify({main:mainPath,mainSha256:createHash('sha256').update(main).digest('hex'),blockSha256:createHash('sha256').update(block).digest('hex'),fixtureSha256:createHash('sha256').update(fs.readFileSync(generated)).digest('hex'),productionLeaseMethods:true,memoryScmTaskProcessFileQueueBoundaries:!nativeJournalIo,memoryScmTaskProcessBoundaries:true,realOwnedJournalWin32Io:nativeJournalIo,productionWriterSha256:nativeJournalIo?createHash('sha256').update(journalWriter).digest('hex'):null,liveMutations:0,exitCode:run.status,nativeArgs,compilePid:compile.pid,runPid:run.pid,exe:pin(exe),compileCommand:pin(cmd),dependencies:pin(path.join(out,'source-dependencies.json')),declaredInputs:declaredInputs.map(before=>({before,after:pin(before.path),unchanged:before.sha256===pin(before.path).sha256}))},null,2)+'\n');
for(const before of declaredInputs)assert.equal(pin(before.path).sha256,before.sha256,'Original input drift '+before.path);
assert.equal(run.status,0,'Vendor-stack regression');
