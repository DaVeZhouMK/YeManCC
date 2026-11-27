// Compile the production identity, selector, transform and source checks; no devices or app startup.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { extract } from './fixtures/legion_motion_consumer/extract_main.mjs';
import { calibrationToolchainPreamble } from './fixtures/hc_reviewed_calibration/toolchain.mjs';
import { buildMotionArtifacts } from './lib/hc_motion_catalog.mjs';

const repo = path.resolve(import.meta.dirname, '..');
const read = file => fs.readFileSync(path.join(repo, file), 'utf8');
assert(process.argv.length === 4 && process.argv[2] === '--out', 'Use --out <fresh directory>');
const out = path.resolve(process.argv[3]);
fs.mkdirSync(out);
const catalog = JSON.parse(read('capabilities/hc-device-catalog.json'));
const generated = buildMotionArtifacts({ repo, catalog });
assert.deepEqual(generated.metadata, catalog.domains.motion);
for (const [file, content] of Object.entries(generated.artifacts)) assert.equal(read(file), content);

const main = read('native/main.cpp');
const fn = name => extract(main, name).code;
const struct = name => extract(main, name, 'struct').code;
const families = main.match(/^enum class YmccFamily : int \{[\s\S]*?^\};/m)?.[0];
assert(families);
assert.match(fn('inputCaptureWindowsProcessingAdopt'), /if\(\(max3\|\|inputCaptureGenericHcMotionRecipe\(g_machineIdentity\)\)&&\s*!inputCaptureWindowsStrictPairCurrent\(view,max3,GetTickCount64\(\)\)\)return unavailable\(\);/);

const cpp = `#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include "HcOemInputCapabilityBinding.h"
#include "HcMotionCapabilityCatalog.h"
#include "hc_model_motion_catalog.h"
#include "hc_default_motion_catalog.h"
#include "hc_generic_motion_catalog.h"
#include "hc_legacy_motion_catalog.h"
#include "msi_motion_capabilities.h"
#include "hc_oem_windows_motion_catalog.h"
#include "windows_source_view.h"
#include "gyro_calibration_policy.h"
${families}
${struct('MachineIdentity')}
${struct('HcMotionSelectorReceipt')}
${struct('InputCaptureSensorMeta')}
using WindowsSensorView=ymcc::sensors::view::Snapshot<InputCaptureSensorMeta>;
static MachineIdentity g_machineIdentity;
static HcMotionSelectorReceipt g_hcMotionSelector;
static std::string sgRegSZ(HKEY,const wchar_t*,const wchar_t*) { std::abort(); }
${['trim_ascii','normalizeHcDmi','ymccResolveFamily','inputCaptureSensorProviderKey',
    'inputCaptureGenericHcMotionRecipe','inputCaptureResolveGenericHcMotionSelector','inputCaptureGenericHcMotionSelectorBound',
    'inputCaptureDefaultHcMotionIdentity','inputCaptureResolveDefaultHcMotionSelector','inputCaptureDefaultHcMotionSelectorBound',
    'inputCaptureResolveNewHcMotionSelector','inputCaptureNewHcMotionSelectorBound',
    'inputCaptureResolveLegacyHcMotionSelector','inputCaptureLegacyHcMotionSelectorBound',
    'inputCaptureResolveMsiMotionSelector','inputCapturePrivateHcMotionSelectorRecipe','inputCaptureMotionSelectorBound',
    'inputCaptureResolveHcMotionSelector','inputCaptureHcWindowsLegacyNeeded','ymccApplyNamedImuMatrix',
    'inputCaptureWindowsStrictPairCurrent'].map(fn).join('\n')}
static int checks=0;
static void check(bool ok, const char* message) {
    if(!ok){std::cerr<<message<<'\\n';std::exit(1);} ++checks;
}
int main(){
    using namespace ymcc::capabilities::motion;
    auto select=[](const char* vendor,const char* board){
        g_machineIdentity={vendor,board,board,"AMD RYZEN AI MAX+ 388", "STANDARD"};
        g_machineIdentity.attempted=true;
        ymccResolveFamily(g_machineIdentity);g_hcMotionSelector={};inputCaptureResolveHcMotionSelector();
        return descriptor(g_hcMotionSelector.unifiedBinding);
    };
    auto row=select("GPD","G1638-01");
    check(row&&row->deviceId=="ymcc.device.GPDWinMax3"&&row->primaryKind==Kind::Default,"MAX3 exact catalog binding");
    check(inputCaptureMotionSelectorBound(),"MAX3 live selector");
    check(!inputCaptureGenericHcMotionRecipe(g_machineIdentity),"MAX3 is no longer an unknown machine");
    check(!inputCaptureHcWindowsLegacyNeeded(g_machineIdentity,false,false),"MAX3 does not start unproven SensorAPI fallback");
    for(bool accel:{false,true}){
        auto id=accel?g_hcMotionSelector.accelMatrixId:g_hcMotionSelector.gyroMatrixId;
        double x,y,z;
        check(ymccApplyNamedImuMatrix(id,2,-3,5,x,y,z)&&x==2&&y==5&&z==-3,"legacy observed transform preserved");
    }
    g_hcMotionSelector.matrixSourceSha256="wrong";
    check(!inputCaptureMotionSelectorBound(),"reject stale or foreign receipt");
    row=select("GPD","G1618-05");check(row&&row->deviceId=="hc.device.GPDWin5"&&inputCaptureMotionSelectorBound(),"Win5 unchanged");
    row=select("GPD","G1618-03");check(row&&row->deviceId=="hc.device.GPDWin3"&&inputCaptureMotionSelectorBound(),"Win3 default unchanged");
    for(const auto& [vendor,board]:{std::pair{"GPD","G1638-01-PRO"},std::pair{"OTHER","G1638-01"},std::pair{"GPD","OTHER"}}){
        row=select(vendor,board);check(!row||row->deviceId!="ymcc.device.GPDWinMax3","reject board, vendor or CPU alias");
    }
    WindowsSensorView view{};
    view.metadata.gyroDeviceId=R"(\\\\?\\ACPI#BMI0160#1#{09485f5a-759e-42c2-bd4b-a349b75c8643}\\{sensor})";
    view.metadata.accelDeviceId=R"(\\\\?\\ACPI#BMI0160#1#{c2fb0f5f-e2d2-4c78-bcd0-352a9582819d}\\{sensor})";
    view.metadata.gyroFamily=view.metadata.accelFamily="winrt";
    view.submission.bindingGeneration=6;
    for(auto& part:view.parts){part.usable=true;part.generation=6;part.timestamp100ns=1;part.sequence=1;part.receipt=1000;}
    check(inputCaptureWindowsStrictPairCurrent(view,true,1350),"MAX3 current exact pair");
    for(const auto age:{351,422,438,485})check(!inputCaptureWindowsStrictPairCurrent(view,true,1000+age),"stale samples never admitted");
    auto bad=view;bad.parts[1].generation=5;
    check(!inputCaptureWindowsStrictPairCurrent(bad,true,1001),"previous binding generation rejected");
    bad=view;bad.parts[1].axes[0]=std::numeric_limits<double>::quiet_NaN();
    check(!inputCaptureWindowsStrictPairCurrent(bad,true,1001),"invalid axis rejected");
    bad=view;bad.metadata.accelDeviceId="ACPI#BMI0160#2#{accel}";
    check(!inputCaptureWindowsStrictPairCurrent(bad,true,1001),"do not mix providers");
    bad=view;bad.metadata.gyroDeviceId=bad.metadata.accelDeviceId="ACPI#OTHER#1#{sensor}";
    check(!inputCaptureWindowsStrictPairCurrent(bad,true,1001),"unobserved MAX3 provider rejected");
    check(inputCaptureWindowsStrictPairCurrent(bad,false,1001),"existing generic provider behavior unchanged");
    bad=view;bad.metadata.gyroFamily=bad.metadata.accelFamily="sensorapi";
    check(!inputCaptureWindowsStrictPairCurrent(bad,true,1001),"unobserved MAX3 SensorAPI route rejected");
    check(inputCaptureWindowsStrictPairCurrent(bad,false,1001),"existing generic SensorAPI unchanged");
    auto selected=view;
    selected.privateSource=true;
    selected.providers={inputCaptureSensorProviderKey(view.metadata.gyroDeviceId),inputCaptureSensorProviderKey(view.metadata.accelDeviceId)};
    check(inputCaptureWindowsStrictPairCurrent(selected,true,1350),"explicit source admits the observed pair");
    check(!inputCaptureWindowsStrictPairCurrent(selected,true,1351),"explicit source must also stay fresh");
    selected.providers[1]="ACPI#BMI0160#2";
    check(!inputCaptureWindowsStrictPairCurrent(selected,true,1001),"explicit source cannot mix IMUs");
    std::cout<<"PASS "<<checks<<" checks; no hardware IO\\n";
}
`;
fs.writeFileSync(path.join(out,'test.cpp'),cpp);
const vcvars = 'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat';
fs.writeFileSync(path.join(out,'run.cmd'), calibrationToolchainPreamble(vcvars) +
    `cl /nologo /std:c++20 /EHsc /utf-8 /I"${path.join(repo,'native')}" /I"${path.join(repo,'capabilities')}" test.cpp\r\nif errorlevel 1 exit /b 1\r\ntest.exe\r\n`);
const run = spawnSync(process.env.ComSpec || 'cmd.exe', ['/d','/c','run.cmd'], { cwd:out, encoding:'utf8', timeout:60000, windowsHide:true });
const output = (run.stdout || '') + (run.stderr || '');
fs.writeFileSync(path.join(out,'run.log'),output);
assert.equal(run.status,0,output || String(run.error));
assert.match(output,/PASS \d+ checks; no hardware IO/);
console.log(output.trim());
