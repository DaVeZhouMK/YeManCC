"""Offline default-off/high-frequency buffering gate. Never runs YMCC or hardware.

Compile extracted real startup/IPC/domain-writer functions with fake settings and
fixture-only paths. Uses the existing MSVC compiler; no install or policy change.
"""
from __future__ import annotations
import argparse, datetime, hashlib, json, os, pathlib, subprocess
from log_frequency_selftest import ROOT, extract

SIGNATURES = [
    'static bool domainLogEnabled(const char* domain) {',
    'static bool sensorDetailLoggingGate() {',
    'static bool logGateShouldRecover(bool sectionHasVirtual, bool sectionVirtual, bool flagPresent) {',
    'static bool logSettingExplicitlyEnabled(const json& section, const char* key) {',
    'static bool detailedLogFlagEnabled(const std::wstring& path) {',
    'static void domainLogsLoadConfig() {',
    'static void domainLogsSaveConfig() {',
    'static void domainLogGateRecord(const char* source, bool enabled) {',
    'static void inputHostCostFlagSync() {',
    'static void rotateOneLogFile(const std::wstring& path, unsigned long long maxBytes) {',
    'static std::wstring domainLogPath(const char* domain) {',
    'static int domainLogStreamIndex(const char* domain) {',
    'static void flushDomainLogs(bool closeStreams) {',
    'static void appendDomainLog(const char* domain, const char* event, const json& detail) {',
]
HANDLERS = ['logs.domainSetEnabled', 'logs.domainClear', 'logs.domainExport', 'fanLog.setEnabled']

PRELUDE = r"""
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>
#include "json.hpp"
#include "log_frequency_policy.h"
#include "buffered_domain_log.h"
using json = nlohmann::json;
namespace fspath = std::filesystem;
static std::wstring g_fixtureRoot, g_exportRoot;
static std::wstring yemancc_data_dir() { return g_fixtureRoot; }
static std::wstring fan_host_state_dir() { return g_fixtureRoot; }
static std::string W2U(const std::wstring& v) { return std::string(v.begin(), v.end()); }
static unsigned long long kLogMaxBytes = 5ull * 1024 * 1024;
static std::mutex g_domainLogMx;
static ymcc::logging::BufferedDomainLog g_domainLogStreams[3];
static std::atomic<bool> g_domainLogEnabled[2] = {false, false};
static std::atomic<bool> g_sensorDetailLogging{false}, g_fanLogEnabled{false};
static std::wstring g_fanLogFile;
static std::mutex g_domainLogGateTimelineMx;
static std::vector<std::string> g_domainLogGateTimeline;
static bool g_domainLogGateKnown = false, g_domainLogGateLast = false;
static json fixtureSettings = json::object(), baseRows = json::array();
static unsigned settingsWrites = 0;
static std::string nativeLifecycleTimestamp() { return "2000-01-01T00:00:00.000"; }
static json ymSettingsSection(const char* name) { return fixtureSettings.value(name, json::object()); }
static bool ymSettingsWriteSection(const char* name, json value) { ++settingsWrites; fixtureSettings[name] = std::move(value); return true; }
static void appendNativeLifecycleLog(const char* event, json detail) { detail["event"] = event; baseRows.push_back(std::move(detail)); }
static void domainLogsSaveConfig();
static void domainLogGateRecord(const char* source, bool enabled);
static void inputHostCostFlagSync();
static void flushDomainLogs(bool closeStreams = false);
static void flushNativeLifecycleLogs(bool closeDetail = false) { flushDomainLogs(closeDetail); }
static void fanDiagnosticsApplyEnabled(bool enabled) { g_fanLogEnabled = enabled; }
static HRESULT fixtureGetDesktop(int, DWORD, HANDLE, PWSTR* result) {
    *result = new wchar_t[g_exportRoot.size()+1];
    std::copy(g_exportRoot.begin(), g_exportRoot.end(), *result); (*result)[g_exportRoot.size()] = 0;
    return S_OK;
}
static void fixtureFree(void* ptr) { delete[] static_cast<wchar_t*>(ptr); }
#define SHGetKnownFolderPath fixtureGetDesktop
#define FOLDERID_Desktop 0
#define CoTaskMemFree fixtureFree
"""
TESTS = r"""
static json checks = json::array();
static void check(const char* name, bool pass) { checks.push_back({{"name", name}, {"pass", pass}}); }
static std::vector<json> readRows(const fspath::path& path) {
    std::vector<json> rows; std::ifstream f(path);
    for(std::string line; std::getline(f,line);) if(!line.empty()) rows.push_back(json::parse(line));
    return rows;
}
static void reset(const fspath::path& root, const wchar_t* name) {
    flushDomainLogs(true);
    g_fixtureRoot = (root/name).wstring(); fspath::create_directories(g_fixtureRoot);
    g_exportRoot = (root/L"exports").wstring(); fspath::create_directories(g_exportRoot);
    fixtureSettings = json::object(); baseRows = json::array(); settingsWrites=0;
    g_domainLogEnabled[0]=false; g_domainLogEnabled[1]=false; g_sensorDetailLogging=false; g_fanLogEnabled=false;
    g_domainLogGateKnown=false; g_domainLogGateLast=false; g_domainLogGateTimeline.clear();
    kLogMaxBytes = 5ull * 1024 * 1024;
}
static void flag(std::string_view content) {
    std::ofstream f(fspath::path(g_fixtureRoot)/L"fan-logging-enabled.flag", std::ios::binary); f.write(content.data(),content.size());
}
int wmain(int argc, wchar_t** argv) {
    if(argc!=2)return 2;
    const fspath::path root(argv[1]);
    reset(root,L"fresh"); domainLogsLoadConfig();
    check("fresh-no-config-all-high-frequency-gates-off", !g_domainLogEnabled[0] && !g_domainLogEnabled[1] && !g_sensorDetailLogging && !g_fanLogEnabled);
    check("fresh-no-config-no-cost-flag-no-settings-write", !fspath::exists(fspath::path(g_fixtureRoot)/L"input-host-cost-enabled.flag") && settingsWrites==0);
    for(int i=0;i<1000;++i) appendDomainLog("virtual","fixture-sample",{{"index",i}});
    check("off-1000-samples-no-file-or-open-stream", !fspath::exists(fspath::path(g_fixtureRoot)/L"virtual-gamepad.log") && !g_domainLogStreams[1].isOpen());
    reset(root,L"explicit-off"); fixtureSettings["domainLogs"]={{"virtual",false},{"gyro",true}}; fixtureSettings["fan"]={{"diagnosticLoggingEnabled",true}}; flag("enabled"); domainLogsLoadConfig();
    check("explicit-off-wins-over-stale-gyro-legacy-and-enabled-flag", !g_domainLogEnabled[0] && !g_domainLogEnabled[1] && settingsWrites==0 && !g_sensorDetailLogging && !g_fanLogEnabled);
    reset(root,L"explicit-on"); fixtureSettings["domainLogs"]={{"virtual",true},{"gyro",false}};domainLogsLoadConfig();
    check("explicit-on-retained-and-unified-after-restart", g_domainLogEnabled[0] && g_domainLogEnabled[1] && g_sensorDetailLogging && g_fanLogEnabled && settingsWrites==0);
    reset(root,L"legacy");fixtureSettings["input"]={{"diagnostics",{{"inputLoggingEnabled",true}}}};domainLogsLoadConfig();
    check("legacy-fields-do-not-enable-unified-detail", !g_domainLogEnabled[0] && !g_domainLogEnabled[1] && settingsWrites==0);
    reset(root,L"legacy-malformed");fixtureSettings["fan"]={{"diagnosticLoggingEnabled","true"}};fixtureSettings["input"]={{"diagnostics",{{"inputLoggingEnabled",1}}}};fixtureSettings["sleep"]={{"factMonitorEnabled",json::array()}};domainLogsLoadConfig();
    check("malformed-legacy-is-not-opt-in", !g_domainLogEnabled[0] && !g_domainLogEnabled[1] && settingsWrites==0);
    reset(root,L"flag-empty");flag("");domainLogsLoadConfig();check("empty-leftover-flag-does-not-enable", !g_domainLogEnabled[1] && settingsWrites==0);
    reset(root,L"flag-disabled");flag("disabled");domainLogsLoadConfig();check("disabled-leftover-flag-does-not-enable", !g_domainLogEnabled[1] && settingsWrites==0);
    reset(root,L"flag-enabled");flag(" \r\nEnAbLeD\t");domainLogsLoadConfig();check("explicit-enabled-flag-does-not-enable-by-itself", !g_domainLogEnabled[0] && !g_domainLogEnabled[1] && settingsWrites==0);
    reset(root,L"flag-one");flag("1");domainLogsLoadConfig();check("explicit-one-flag-does-not-enable-by-itself", !g_domainLogEnabled[1] && settingsWrites==0);
    reset(root,L"flag-large");flag(std::string(200,'x'));domainLogsLoadConfig();check("oversized-flag-is-not-consent", !g_domainLogEnabled[1] && settingsWrites==0);
    reset(root,L"flag-unreadable");flag("enabled");const auto flagPath=fspath::path(g_fixtureRoot)/L"fan-logging-enabled.flag";
    HANDLE heldFlag=CreateFileW(flagPath.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);domainLogsLoadConfig();
    check("unreadable-flag-is-not-consent", heldFlag!=INVALID_HANDLE_VALUE && !g_domainLogEnabled[1] && settingsWrites==0);if(heldFlag!=INVALID_HANDLE_VALUE)CloseHandle(heldFlag);
    reset(root,L"explicit-malformed");fixtureSettings["domainLogs"]={{"virtual","true"},{"gyro",true}};flag("enabled");domainLogsLoadConfig();check("malformed-authoritative-key-is-fail-closed", !g_domainLogEnabled[0] && !g_domainLogEnabled[1] && settingsWrites==0);
    reset(root,L"setter");
    auto missing=domainSet({{"domain","virtual"}});
    check("setter-missing-enabled-rejected-no-side-effects", missing["ok"]==false && missing["reason"]=="enabled-boolean-required" && !g_domainLogEnabled[1] && settingsWrites==0);
    bool badTypeSafe=true;
    for(auto value:json::array({1,"true",nullptr,json::object(),json::array()})) {
        auto result=domainSet({{"domain","virtual"},{"enabled",value}});
        badTypeSafe=badTypeSafe && result["ok"]==false && !g_domainLogEnabled[1] && settingsWrites==0;
    }
    check("setter-nonboolean-enabled-rejected-no-side-effects",badTypeSafe);
    check("setter-unknown-domain-rejected",domainSet({{"domain","unknown"},{"enabled",true}})["ok"]==false && settingsWrites==0);
    check("setter-explicit-on-unifies-derived-gates",domainSet({{"domain","virtual"},{"enabled",true}})["ok"]==true && g_domainLogEnabled[0] && g_domainLogEnabled[1] && g_sensorDetailLogging && g_fanLogEnabled);
    check("setter-explicit-off-unifies-derived-gates",domainSet({{"domain","gyro"},{"enabled",false}})["ok"]==true && !g_domainLogEnabled[0] && !g_domainLogEnabled[1] && !g_sensorDetailLogging && !g_fanLogEnabled && !fspath::exists(fspath::path(g_fixtureRoot)/L"input-host-cost-enabled.flag"));
    const auto saves=settingsWrites;fanSet(json::object());check("fan-getter-missing-enabled-remains-read-only",settingsWrites==saves && !g_domainLogEnabled[1]);
    reset(root,L"rows");domainSet({{"domain","virtual"},{"enabled",true}});
    for(int i=0;i<1000;++i)appendDomainLog("virtual","fixture-sample",{{"index",i},{"original",{{"value",i*2}}}});
    check("domain-writer-reuses-open-stream",g_domainLogStreams[1].isOpen() && g_domainLogStreams[1].pendingRows()<16);
    flushDomainLogs();const auto rows=readRows(fspath::path(g_fixtureRoot)/L"virtual-gamepad.log");bool order=rows.size()==1000;
    for(size_t i=0;i<rows.size();++i)order=order && rows[i]["index"]==i && rows[i]["original"]["value"]==i*2 && rows[i]["event"]=="fixture-sample";
    check("all-1000-rows-and-fields-retained-in-order",order);
    for(int i=0;i<3;++i)appendDomainLog("gyro","fixture-sample",{{"index",i}});
    for(int i=0;i<3;++i)appendDomainLog("focus","fixture-sample",{{"index",i}});
    flushDomainLogs();check("three-domain-streams-isolated",readRows(fspath::path(g_fixtureRoot)/L"gyro-motion.log").size()==3 && readRows(fspath::path(g_fixtureRoot)/L"focus-trace.log").size()==3 && readRows(fspath::path(g_fixtureRoot)/L"virtual-gamepad.log").size()==1000);
    appendDomainLog("virtual","fixture-sample",{{"tail",true}});domainSet({{"domain","virtual"},{"enabled",false}});
    check("gate-off-flushes-tail-and-closes-all-domains",readRows(fspath::path(g_fixtureRoot)/L"virtual-gamepad.log").size()==1001 && !g_domainLogStreams[0].isOpen() && !g_domainLogStreams[1].isOpen() && !g_domainLogStreams[2].isOpen());
    appendDomainLog("virtual","fixture-sample",{{"afterOff",true}});check("gate-off-never-reopens-or-adds-data",!g_domainLogStreams[1].isOpen() && readRows(fspath::path(g_fixtureRoot)/L"virtual-gamepad.log").size()==1001);
    reset(root,L"failure");domainSet({{"domain","virtual"},{"enabled",true}});appendDomainLog("virtual","fixture-sample",{{"ok",true}});appendDomainLog("virtual","fixture-result",{{"ok",false}});
    check("explicit-failure-flushes-itself-and-prior-tail",readRows(fspath::path(g_fixtureRoot)/L"virtual-gamepad.log").size()==2 && g_domainLogStreams[1].pendingRows()==0);
    appendDomainLog("virtual","pipe-timeout",{{"detail","fixture"}});check("error-event-flushes-immediately",readRows(fspath::path(g_fixtureRoot)/L"virtual-gamepad.log").size()==3 && g_domainLogStreams[1].pendingRows()==0);
    reset(root,L"export");domainSet({{"domain","virtual"},{"enabled",true}});appendDomainLog("virtual","fixture-sample",{{"tail",true}});auto exported=domainExport({{"domain","virtual"}});
    check("single-domain-export-flushes-partial-batch",exported["ok"]==true && readRows(fspath::path(exported["path"].get<std::string>())).size()==1);
    g_exportRoot=(root/L"missing-export-parent"/L"nested").wstring();auto failedExport=domainExport({{"domain","virtual"}});
    check("single-domain-export-copy-failure-not-false-success",failedExport["ok"]==false && failedExport["reason"]=="copy-failed");
    reset(root,L"clear");domainSet({{"domain","virtual"},{"enabled",true}});appendDomainLog("virtual","fixture-sample",{{"beforeClear",true}});auto cleared=domainClear({{"domain","virtual"}});
    check("clear-closes-handle-before-removing-active-file",cleared["ok"]==true && !g_domainLogStreams[1].isOpen() && !fspath::exists(fspath::path(g_fixtureRoot)/L"virtual-gamepad.log"));
    appendDomainLog("virtual","fixture-sample",{{"afterClear",true}});flushDomainLogs();const auto afterClear=readRows(fspath::path(g_fixtureRoot)/L"virtual-gamepad.log");check("clear-does-not-resurrect-old-buffer",afterClear.size()==1 && afterClear[0]["afterClear"]==true);
    flushDomainLogs(true);const auto clearPath=fspath::path(g_fixtureRoot)/L"virtual-gamepad.log";HANDLE clearHeld=CreateFileW(clearPath.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);auto blockedClear=domainClear({{"domain","virtual"}});
    check("blocked-clear-reports-failure-and-retains-data",clearHeld!=INVALID_HANDLE_VALUE && blockedClear["ok"]==false && fspath::exists(clearPath));if(clearHeld!=INVALID_HANDLE_VALUE)CloseHandle(clearHeld);
    reset(root,L"threshold");kLogMaxBytes=300;domainSet({{"domain","virtual"},{"enabled",true}});for(int i=0;i<5;++i)appendDomainLog("virtual","fixture-sample",{{"index",i},{"padding",std::string(70,'x')}});flushDomainLogs();
    const auto activePath=fspath::path(g_fixtureRoot)/L"virtual-gamepad.log";check("domain-rotation-retains-backup-and-current",fspath::exists(activePath.wstring()+L".1") && !readRows(activePath).empty());
    reset(root,L"writer-batches");ymcc::logging::BufferedDomainLog writer;const auto batchPath=(fspath::path(g_fixtureRoot)/L"direct.log").wstring();unsigned rotations=0;auto rotate=[&](const std::wstring&,unsigned long long){++rotations;};
    writer.append(batchPath,"row",100,false,1000000,rotate);check("first-normal-row-buffered-no-forced-flush",writer.isOpen() && writer.pendingRows()==1);
    for(int i=1;i<16;++i)writer.append(batchPath,"row",100+i,false,1000000,rotate);check("sixteen-row-batch-flushes-without-timer",writer.pendingRows()==0 && fspath::file_size(batchPath)==64 && rotations==0);
    writer.append(batchPath,"late",1200,false,1000000,rotate);check("next-write-after-one-second-flushes-tail",writer.pendingRows()==0 && fspath::file_size(batchPath)==69);
    writer.close();
    reset(root,L"failed-rotation");const auto failedPath=(fspath::path(g_fixtureRoot)/L"direct.log").wstring();{std::ofstream f(failedPath);f<<std::string(50,'x')<<'\n';}
    unsigned attempts=0;ymcc::logging::BufferedDomainLog failWriter;auto cannotRotate=[&](const std::wstring&,unsigned long long){++attempts;};
    for(int i=0;i<100;++i)failWriter.append(failedPath,"retained",2000,false,10,cannotRotate);
    check("failed-rotation-not-retried-per-row",attempts==1);failWriter.append(failedPath,"next",3001,true,10,cannotRotate);check("failed-rotation-retries-once-after-one-second",attempts==2);failWriter.close();
    std::ifstream preserved(failedPath);std::stringstream full;full<<preserved.rdbuf();check("failed-rotation-keeps-active-evidence",full.str().starts_with(std::string(50,'x')) && full.str().find("retained")!=std::string::npos && full.str().find("next")!=std::string::npos);
    reset(root,L"unwritable");g_domainLogEnabled[1]=true;g_fixtureRoot=(root/L"missing-writer-parent"/L"nested").wstring();bool survived=true;try{appendDomainLog("virtual","fixture-sample",{{"x",1}});}catch(...){survived=false;}check("writer-io-failure-never-changes-caller-control-flow",survived && !g_domainLogStreams[1].isOpen());
    flushDomainLogs(true);unsigned failures=0;for(const auto& c:checks)if(!c["pass"].get<bool>())++failures;
    std::cout<<json({{"checks",checks},{"failures",failures},{"scope","EXTRACTED_REAL_LOG_STARTUP_IPC_AND_DOMAIN_WRITER_FIXTURE_SETTINGS_AND_PATHS_ONLY"}}).dump(2)<<std::endl;
    return failures?3:0;
}
"""

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir', type=pathlib.Path,
                        default=ROOT.parents[1] / 'Build/Validation/HighFrequencyLogging')
    output = parser.parse_args().output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    run = output / ('run-' + datetime.datetime.now().strftime('%Y%m%d-%H%M%S') + '-' + str(os.getpid()))
    run.mkdir(exist_ok=False)
    source_path = ROOT/'native/main.cpp'; source_bytes = source_path.read_bytes()
    source = source_bytes.decode('utf-8-sig').replace('\r\n','\n')
    # Normal text-file newline normalization, not textual backslash replacement.
    source = source_bytes.decode('utf-8-sig').replace(chr(13)+chr(10),chr(10))
    bound = {rel:(ROOT/rel).read_bytes() for rel in ['native/log_frequency_policy.h','native/buffered_domain_log.h']}
    (run/'bound-main.cpp').write_bytes(source_bytes)
    for rel,content in bound.items():(run/('bound-'+pathlib.Path(rel).name)).write_bytes(content)
    functions = [extract(source,x) for x in SIGNATURES]
    handlers = {name:extract(source,'ipc_on("'+name+'", [](const json& a) -> json {') for name in HANDLERS}
    names={'logs.domainSetEnabled':'domainSet','logs.domainClear':'domainClear','logs.domainExport':'domainExport','fanLog.setEnabled':'fanSet'}
    handler_code='\n'.join('static json '+names[name]+'(const json& a) '+code[code.index('{'):] for name,code in handlers.items())
    cpp=run/'high-frequency-harness.cpp';cpp.write_text(PRELUDE+'\n\n'.join(functions)+'\n'+handler_code+'\n'+TESTS,encoding='utf-8')
    vcvars=pathlib.Path(r'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat')
    if not vcvars.is_file():raise RuntimeError('Existing compiler unavailable; no installation attempted')
    exe=run/'high-frequency-harness.exe'
    command='call "'+str(vcvars)+'" >nul && cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 "'+str(cpp)+'" /I"'+str(ROOT/'deps/json')+'" /I"'+str(ROOT/'native')+'" /Fo"'+str(run/'harness.obj')+'" /Fe"'+str(exe)+'"'
    batch=run/'build-harness.cmd'
    batch.write_text('@echo off\n'+command+'\n',encoding='utf-8')
    build=subprocess.run(['cmd.exe','/d','/c',str(batch)],cwd=run,capture_output=True,text=True,errors='replace')
    (run/'compile.log').write_text(build.stdout+build.stderr,encoding='utf-8')
    if build.returncode:print(build.stdout+build.stderr);return build.returncode
    result=subprocess.run([str(exe),str(run/'fixtures')],cwd=run,capture_output=True,text=True,errors='replace')
    (run/'result.json').write_text(result.stdout,encoding='utf-8')
    if result.stderr:(run/'stderr.log').write_text(result.stderr,encoding='utf-8')
    report=json.loads(result.stdout)
    current_bytes=source_path.read_bytes();current=current_bytes.decode('utf-8-sig').replace(chr(13)+chr(10),chr(10))
    signatures_stable=all(extract(current,x)==f for x,f in zip(SIGNATURES,functions))
    handlers_stable=all(extract(current,'ipc_on("'+name+'", [](const json& a) -> json {')==f for name,f in handlers.items())
    export_all=extract(source,'ipc_on("logs.exportAll", [](const json&) -> json {')
    native_flush=extract(source,'static void flushNativeLifecycleLogs(bool closeDetail) {')
    default_checks={
        'native-domain-array-default-off':'g_domainLogEnabled[2] = { false, false }' in source,
        'frontend-detail-switch-default-off':'const detailLoggingEnabled = ref(false);' in (ROOT/'src/views/SettingsView.vue').read_text(encoding='utf-8-sig'),
        'off-check-before-path-or-json':functions[-1].index('!domainLogEnabled(domain)')<functions[-1].index('domainLogPath(') and functions[-1].index('!domainLogEnabled(domain)')<functions[-1].index('json line'),
        'writer-rechecks-gate-under-lock':functions[-1].index('std::lock_guard')<functions[-1].index('if (!domainLogEnabled(domain)) return;'),
        'export-all-flushes-before-collecting':export_all.index('flushNativeLifecycleLogs();')<export_all.index('std::vector<std::wstring> files'),
        'native-flush-includes-domain-flush':'flushDomainLogs(closeDetail);' in native_flush,
        'clear-closes-domain-stream-before-remove':handlers['logs.domainClear'].index('.close();')<handlers['logs.domainClear'].index('fspath::remove(path,'),
        'bound-log-functions-and-handlers-stable':signatures_stable and handlers_stable,
        'policy-and-buffer-headers-stable':all((ROOT/rel).read_bytes()==b for rel,b in bound.items()),
    }
    report.update(sourceChecks=default_checks,sourceSha256=hashlib.sha256(source_bytes).hexdigest().upper(),currentSourceSha256=hashlib.sha256(current_bytes).hexdigest().upper(),wholeSourceStableDuringSelftest=source_bytes==current_bytes,headers={rel:hashlib.sha256(b).hexdigest().upper() for rel,b in bound.items()},runDirectory=str(run),realProductOrHardwareExecuted=False,realUserSettingsRead=False,executionPolicyChanged=False)
    report['failures']+=sum(not v for v in default_checks.values())
    (output/'report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+chr(10),encoding='utf-8')
    print(json.dumps(report,ensure_ascii=False,indent=2))
    return 3 if report['failures'] else result.returncode

if __name__=='__main__':raise SystemExit(main())
