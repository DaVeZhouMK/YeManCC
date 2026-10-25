#!/usr/bin/env python3
"""Offline actual main.cpp callback/anomaly/atomic regression; stdlib + MSVC only.

Runs private fixtures, never a real timer, input device, Steam, or the product.
"""
from __future__ import annotations
import argparse
import datetime as dt
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUT = ROOT.parents[2] / 'Build/Validation/VC-TIMER-EVIDENCE-20261022/adapter'
VCVARS = Path('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat')
COUNTERS = ('g_inputCaptureBusBusySkips', 'g_tickCostBusySkipCount')
GLOBALS = ('g_domainLogEnabled', 'g_inputCaptureStop', 'g_inputCaptureBusTickMx',
           'g_inputCaptureBusTickGlobal', 'g_inputTimerEvidence', 'g_tickCostWindowTicks', *COUNTERS)
FORBIDDEN = ('winmm.dll', 'user32.dll', 'win32u.dll', 'xinput', 'dinput8.dll',
             'hid.dll', 'setupapi.dll', 'steam_api', 'timesetevent', 'timekillevent',
             'sendinput', 'getasynckeystate', 'getrawinputdata', 'deviceiocontrol')


def sha(data):
    return hashlib.sha256(data).hexdigest()


def save(path, data):
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


def need(ok, message):
    if not ok:
        raise RuntimeError(message)


def mask_cpp(text):
    # Offsets are preserved so snippets are exact source bytes, including comments.
    pattern = re.compile(r'//[^\r\n]*|/\*[\s\S]*?\*/|"(?:\\[\s\S]|[^"\\])*"|\'(?:\\[\s\S]|[^\'\\])*\'')
    return pattern.sub(lambda m: ''.join('\n' if c == '\n' else ' ' for c in m.group()), text)


def extract(text):
    code = mask_cpp(text)
    parts = {}
    def add(name, start, end, **extra):
        parts[name] = dict(text=text[start:end], start=start, end=end,
                           line=text.count('\n', 0, start)+1, **extra)
    for name, pattern in (
        ('callback', r'^static\s+void\s+CALLBACK\s+inputCaptureTimerEventProc\s*\('),
        ('scope', r'^class\s+InputTimerCallbackEvidenceScope\s*\{'),
    ):
        matches = list(re.finditer(pattern, code, re.M))
        need(len(matches) == 1, 'Expected unique actual ' + name)
        start = matches[0].start()
        opening = code.index('{', start)
        depth = 0
        for index in range(opening, len(code)):
            depth += (code[index] == '{') - (code[index] == '}')
            if depth == 0:
                end = index+1
                break
        else:
            raise RuntimeError('Unclosed actual ' + name)
        if name == 'scope':
            end = code.index(';', end)+1
        add(name, start, end)
    for name in GLOBALS:
        matches = list(re.finditer(r'^static\s+[^;\n]*\b' + name + r'\b[^;]*;', code, re.M))
        need(len(matches) == 1, 'Expected unique actual declaration: ' + name)
        add('global.' + name, matches[0].start(), matches[0].end())
    start = text.index('struct InputTimerAnomalyEvidence {')
    add('anomalyBank', start, text.index('// ── HC tick parity', start))
    for name in COUNTERS:
        matches = list(re.finditer(name + r'\.fetch_add\s*\([^;]+\);', code))
        need(len(matches) == 1, 'Expected actual increment: ' + name)
        add(name + '.increment', matches[0].start(), matches[0].end())
        matches = list(re.finditer(r'const\s+(?:auto|unsigned\s+long\s+long)\s+(\w+)\s*=\s*' + name + r'\.exchange\s*\([^;]+\);', code))
        need(len(matches) == 1, 'Expected actual window exchange: ' + name)
        add(name + '.collect', matches[0].start(), matches[0].end(), local=matches[0].group(1))
        need(not re.search(name + r'(?:\.store\s*\(\s*0\b|\s*=\s*0\b)', code), 'Lost-count reset: ' + name)
    callback = mask_cpp(parts['callback']['text'])
    need(re.search(r'\{\s*InputTimerCallbackEvidenceScope\s+timerEvidence\(timerId,\s*user\);', callback), 'Scope must be first callback statement')
    for call in ('inputCaptureBusTick', 'ymccLogBusTickThreadRole', 'inputCaptureRefreshEffectiveTickPeriod', 'SetEvent', 'tickCostAccumulate'):
        need(len(re.findall(r'\b'+call+r'\s*\(', callback)) == 1, 'Original call count changed: ' + call)
    for args in (r'2', r'3', r'1,\s*g_inputCaptureBusTickGlobal'):
        need(len(re.findall(r'timerEvidence\.finish\(\s*'+args+r'\s*\)', callback)) == 1, 'Missing finish: ' + args)
    need('std::try_to_lock' in callback and 'std::unique_lock<std::mutex>' in callback, 'Actual BUS mutex changed')
    return parts


PRELUDE = r'''
#include <atomic>
#include <chrono>
#include <cstdint>
#include <future>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include "input_timer_evidence.h"
using json = nlohmann::json;
using UINT = unsigned;
using DWORD = unsigned long;
using DWORD_PTR = std::uintptr_t;
using HANDLE = void*;
#define CALLBACK
struct Calls {
    unsigned role=0, refresh=0, bus=0, event=0, scopeEnter=0, scopeExit=0;
    unsigned accumulate=0, elapsed=0, gates=0, clock=0, tid=0, power=0;
    bool costEnabled=false;
    DWORD_PTR lastEvent=0;
    unsigned long long now=100000, generation=77;
    DWORD threadId=0x6251;
};
static thread_local Calls calls;
static unsigned long long fakeGetTickCount64() noexcept { ++calls.clock; return ++calls.now; }
static DWORD fakeGetCurrentThreadId() noexcept { ++calls.tid; return calls.threadId; }
static bool fakeSetEvent(HANDLE event) noexcept { ++calls.event; calls.lastEvent=reinterpret_cast<DWORD_PTR>(event); return true; }
#define GetTickCount64 fakeGetTickCount64
#define GetCurrentThreadId fakeGetCurrentThreadId
#define SetEvent fakeSetEvent
'''

FAKES = r'''
static unsigned long long currentPowerGeneration() noexcept { ++calls.power; return calls.generation; }
static bool domainLogEnabled(const char* domain) noexcept {
    return std::string(domain)=="virtual" && g_domainLogEnabled[1].load(std::memory_order_acquire);
}
static bool tickCostEnabled() noexcept { ++calls.gates; return g_domainLogEnabled[1].load(std::memory_order_acquire); }
static void ymccLogBusTickThreadRole() noexcept { ++calls.role; }
static void inputCaptureRefreshEffectiveTickPeriod() noexcept { ++calls.refresh; }
static unsigned long long tickCostEpochUs(std::chrono::steady_clock::time_point) noexcept { return 11; }
static unsigned long long tickCostThreadCycleSample() noexcept { return 12; }
static unsigned long long tickCostElapsedUs(std::chrono::steady_clock::time_point) noexcept { ++calls.elapsed; return 13; }
static constexpr int kTickCostTickTotal=4;
static void tickCostAccumulate(int stage, unsigned long long value) {
    if(stage!=kTickCostTickTotal || value!=13) throw std::runtime_error("cost argument changed");
    ++calls.accumulate;
}
namespace callbackCost {
enum class Source { multimedia };
class Scope {
public:
    template<class Clock, class Cycle, class Tid>
    Scope(bool enabled, Source, Clock, Cycle, Tid) noexcept { ++calls.scopeEnter; calls.costEnabled=enabled; }
    ~Scope() noexcept { ++calls.scopeExit; }
};
}
static void inputCaptureBusTick() noexcept { ++calls.bus; ++g_inputCaptureBusTickGlobal; }
'''

TESTS = r'''
static unsigned checks=0;
static void check(bool ok, const char* message) { ++checks; if(!ok) throw std::runtime_error(message); }
static json snapshot() { return ymcc::inputTimerEvidence::snapshotJson(g_inputTimerEvidence.snapshot()); }
static unsigned long long count(const json& j, const char* name) { return j.at("counts").at(name).get<unsigned long long>(); }
static void claims(const json& j) {
    check(j.at("known")==true,"snapshot unknown");
    for(const char* key:{"ownerBindingKnown","callbacksDrainedProven","busCountProven","steamConsumptionProven"})
        check(j.at(key)==false,"passive recorder overclaimed");
}
// The mutex owner is a different thread from every caller exercising try_lock.
class OtherThreadLock {
    std::promise<void> ready_, release_;
    std::thread thread_;
public:
    explicit OtherThreadLock(std::mutex& mutex):thread_([this,&mutex] {
        std::unique_lock<std::mutex> held(mutex);
        ready_.set_value();
        release_.get_future().wait_for(std::chrono::seconds(10));
    }) {
        if(ready_.get_future().wait_for(std::chrono::seconds(5))!=std::future_status::ready) {
            release_.set_value(); thread_.join(); throw std::runtime_error("holder failed to acquire");
        }
    }
    ~OtherThreadLock() { release_.set_value(); thread_.join(); }
};
static json callbackCases() {
    json rows=json::array();
    for(bool enabled:{false,true}) for(unsigned outcome:{1u,2u,3u}) for(bool eventPresent:{true,false}) {
        g_domainLogEnabled[0].store(true);
        g_domainLogEnabled[1].store(enabled);
        g_inputCaptureStop.store(outcome==3);
        const UINT timerId=0x9100+outcome*4+(eventPresent?1u:0u);
        const DWORD_PTR user=eventPresent?static_cast<DWORD_PTR>(0x123456780000ULL+timerId):0;
        const auto before=snapshot();
        const auto busyBefore=g_inputCaptureBusBusySkips.load();
        const auto costBefore=g_tickCostBusySkipCount.load();
        const auto windowsBefore=g_tickCostWindowTicks.load();
        const auto busBefore=g_inputCaptureBusTickGlobal;
        Calls observed;
        auto invoke=[&] {
            std::thread callback([&] {
                calls=Calls{};
                inputCaptureTimerEventProc(timerId,0x1357,user,0x2468,0x369a);
                observed=calls;
            }); callback.join();
        };
        if(outcome==2) { OtherThreadLock held(g_inputCaptureBusTickMx); invoke(); }
        else invoke();
        const auto after=snapshot();
        check(observed.role==1 && observed.refresh==1,"role/refresh call counts changed");
        check(observed.scopeEnter==1 && observed.scopeExit==1 && observed.costEnabled==enabled,"cost scope count/gate changed");
        check(observed.bus==(outcome==1?1u:0u),"BUS count changed");
        check(observed.event==(eventPresent?1u:0u) && observed.lastEvent==user,"SetEvent count/actual user changed");
        check(observed.gates==(outcome==2?3u:2u),"cost gate call count changed");
        check(observed.accumulate==(enabled?1u:0u) && observed.elapsed==(enabled?1u:0u),"callback cost accumulation changed");
        check(observed.clock==(enabled?2u:0u) && observed.tid==(enabled?1u:0u) && observed.power==(enabled?1u:0u),"evidence gate clock/thread/power reads changed");
        check(g_inputCaptureBusTickGlobal-busBefore==(outcome==1?1u:0u),"BUS ordinal changed");
        check(g_inputCaptureBusBusySkips.load()-busyBefore==(outcome==2?1u:0u),"busy skip increment changed");
        check(g_tickCostBusySkipCount.load()-costBefore==(enabled&&outcome==2?1u:0u),"cost busy skip gate changed");
        check(g_tickCostWindowTicks.load()-windowsBefore==(enabled?1u:0u),"cost window ticks changed");
        claims(after);
        if(!enabled) check(before==after,"disabled callback wrote a Recorder event");
        else {
            check(count(after,"entered")==count(before,"entered")+1 && count(after,"exited")==count(before,"exited")+1,"callback must enter/leave exactly once");
            check(count(after,"matchedLeaves")==count(before,"matchedLeaves")+1 && count(after,"inFlight")==0,"callback leave unmatched");
            const char* key=outcome==1?"busCalled":outcome==2?"busySkip":"stopReturn";
            check(count(after,key)==count(before,key)+1,"wrong callback outcome");
            const auto& exit=after.at("trace").at("recent").back();
            check(exit.at("type")=="exit" && exit.at("outcome")==outcome,"missing callback exit");
            check(exit.at("timerId")==timerId && exit.at("eventHandle")==user,"actual timer ID/user truncated or replaced");
            check(exit.at("threadId")==observed.threadId && exit.at("powerGeneration")==observed.generation,"fake OS identity not preserved");
            check(exit.at("busOrdinal")== (outcome==1?g_inputCaptureBusTickGlobal:0),"BUS ordinal not preserved");
            check(exit.at("stopObserved")== (outcome==3),"stop observation changed");
        }
        rows.push_back({{"enabled",enabled},{"outcome",outcome},{"timerId",timerId},{"user",user},{"snapshot",after}});
    }
    g_inputCaptureStop.store(false);
    return rows;
}
static json scopeCase() {
    g_domainLogEnabled[1].store(true); calls=Calls{};
    const auto before=snapshot();
    {
        InputTimerCallbackEvidenceScope scope(0xabc1,0x123456789abcULL);
        const auto entered=snapshot();
        check(count(entered,"entered")==count(before,"entered")+1 && count(entered,"exited")==count(before,"exited"),"scope did not enter once");
        scope.finish(2);
        check(snapshot()==entered,"finish must stage outcome until destructor");
    }
    const auto after=snapshot();
    check(count(after,"exited")==count(before,"exited")+1 && count(after,"busySkip")==count(before,"busySkip")+1,"destructor must leave once");
    check(calls.bus==0 && calls.event==0 && calls.role==0 && calls.refresh==0 && calls.scopeEnter==0 && calls.accumulate==0,"scope performed control/cost work");
    return after;
}
static void exportMarkers(const json& j) {
    check(j.at("clock").get<std::string>().find("GetTickCount64 milliseconds")!=std::string::npos,"missing export clock scope");
    check(j.at("callbackCoverage").get<std::string>().find("WinMM callback only")!=std::string::npos,"missing WinMM-only coverage");
    check(j.at("callbackCoverage").get<std::string>().find("waitable/sleep fallback")!=std::string::npos,"fallback coverage overclaim");
    check(j.at("callbackGateScope").get<std::string>().find("virtual detailed logging enabled")!=std::string::npos,"missing detailed gate scope");
    check(j.at("steamConsumptionProven")==false && j.at("controlsChanged")==false,"export overclaimed");
    claims(j.at("current"));
}
static json anomalyCases() {
    g_domainLogEnabled[1].store(true);
    json exports=json::array(), expected=json::array();
    for(unsigned i=1;i<=3;++i) {
        inputCaptureTimerEventProc(0xabd1,0,0x123456780abcULL,0,0);
        expected.push_back(snapshot());
        inputTimerRememberAnomaly(10*i,3*i,1000*i);
        const auto j=inputTimerEvidenceExportSnapshot();
        exportMarkers(j);
        check(j.at("anomalySnapshotKnown")==true && j.at("anomalyCaptureCount")==i,"capture count changed");
        const auto& retained=j.at("retainedAnomalies");
        check(retained.at("first").at("snapshot")==expected.at(0),"first snapshot overwritten");
        check(retained.at("latest").at("snapshot")==expected.at(i-1),"latest snapshot not retained");
        check(retained.at("first").at("busTicks")==10 && retained.at("latest").at("busTicks")==10*i,"anomaly BUS metadata changed");
        check(retained.at("latest").at("busySkips")==3*i && retained.at("latest").at("windowMs")==1000*i,"window metadata changed");
        check(retained.at("overwrittenIntermediate")== (i>2?i-2:0),"intermediate retention count wrong");
        check(retained.at("scope").get<std::string>().find("not a completed-window or drain proof")!=std::string::npos,"anomaly scope overclaim");
        check(j.at("detailedEnabledAtExport")==true,"export detailed gate incorrect");
        exports.push_back(j);
    }
    const auto bankBefore=exports.back().at("retainedAnomalies");
    const auto dropsBefore=g_inputTimerAnomalyDrops.load();
    json contended;
    {
        OtherThreadLock held(g_inputTimerAnomalyEvidenceMx);
        const auto start=std::chrono::steady_clock::now();
        inputTimerRememberAnomaly(999,999,999);
        check(std::chrono::steady_clock::now()-start<std::chrono::seconds(1),"remember blocked on bank mutex");
        check(g_inputTimerAnomalyDrops.load()==dropsBefore+1,"contended capture did not count drop");
        contended=inputTimerEvidenceExportSnapshot();
        check(std::chrono::steady_clock::now()-start<std::chrono::seconds(1),"export blocked on bank mutex");
        check(contended.at("anomalySnapshotKnown")==false && contended.at("retainedAnomalies").is_null(),"contended export claimed bank known");
        exportMarkers(contended);
    }
    g_domainLogEnabled[1].store(false);
    const auto after=inputTimerEvidenceExportSnapshot();
    check(after.at("retainedAnomalies")==bankBefore && after.at("anomalyCaptureCount")==3,"failed capture overwrote bank");
    check(after.at("anomalyCaptureDrops")==dropsBefore+1,"export lost capture drop");
    check(after.at("detailedEnabledAtExport")==false,"export gate stale");
    exportMarkers(after);
    return {{"captures",exports},{"contendedExport",contended},{"afterContention",after}};
}
// Main is the sole collector. Both counter operations below are exact extracts.
static json conservation(bool actualCallback) {
    constexpr unsigned producers=8, perProducer=12500, total=producers*perProducer;
    g_domainLogEnabled[1].store(false);
    g_inputCaptureStop.store(false);
    collectBus(); collectCost();
    const auto before=snapshot();
    std::atomic<unsigned> ready{0}, done{0};
    std::atomic<bool> start{false};
    std::vector<std::thread> workers;
    unsigned long long busCollected=0,costCollected=0,exchanges=0;
    bool overlap=false;
    std::unique_lock<std::mutex> busHeld(g_inputCaptureBusTickMx);
    for(unsigned i=0;i<producers;++i) workers.emplace_back([&] {
        calls=Calls{};
        ready.fetch_add(1,std::memory_order_release);
        while(!start.load(std::memory_order_acquire)) std::this_thread::yield();
        for(unsigned n=0;n<perProducer;++n) {
            if(actualCallback) inputCaptureTimerEventProc(0xc123,0,0x1234567890ULL,0,0);
            else { incrementBus(); incrementCost(); }
            if((n&127u)==0) std::this_thread::yield();
        }
        done.fetch_add(1,std::memory_order_release);
    });
    while(ready.load(std::memory_order_acquire)!=producers) std::this_thread::yield();
    start.store(true,std::memory_order_release);
    while(done.load(std::memory_order_acquire)!=producers) {
        const auto bus=collectBus(); const auto cost=collectCost();
        busCollected+=bus; costCollected+=cost; ++exchanges;
        if(bus>0 && done.load(std::memory_order_acquire)<producers) overlap=true;
        std::this_thread::yield();
    }
    for(auto& worker:workers) worker.join();
    busCollected+=collectBus(); costCollected+=collectCost();
    check(busCollected==total,"100000 BUS increments not conserved");
    check(costCollected==(actualCallback?0u:total),"100000 cost increments not conserved/gated");
    check(collectBus()==0 && collectCost()==0,"tail not drained exactly once");
    check(overlap && exchanges>0,"collection did not overlap producers");
    check(snapshot()==before,"disabled concurrent callbacks changed Recorder");
    return {{"actualCallback",actualCallback},{"producers",producers},{"incrementsPerProducer",perProducer},
        {"total",total},{"busCollected",busCollected},{"costCollected",costCollected},
        {"mainThreadExchanges",exchanges},{"collectionOverlappedProducers",overlap}};
}
int main() {
    try {
        json result;
        result["cases"]=callbackCases();
        result["scope"]=scopeCase();
        result["anomalies"]=anomalyCases();
        result["conservation"]={conservation(true),conservation(false)};
        result["unknown"]={"late token across owners","actual WinMM kill/drain","waitable/sleep callback coverage","physical input/Steam consumption"};
        result["pass"]=true; result["checks"]=checks;
        std::cout<<result.dump()<<'\n'; return 0;
    } catch(const std::exception& e) {
        std::cout<<json({{"pass",false},{"checks",checks},{"error",e.what()}}).dump()<<'\n'; return 1;
    }
}
'''


def render(parts):
    def body(name):
        part = parts[name]
        return f'\n#line {part["line"]} "actual-main.cpp"\n'+part['text']+'\n#line 1 "fixture-generated.cpp"\n'
    declarations = ''.join(body('global.'+name) for name in GLOBALS)
    helpers = ''
    probes = '#include <atomic>\n'
    for name, suffix in zip(COUNTERS, ('Bus','Cost')):
        inc = body(name+'.increment')
        collect = body(name+'.collect')
        local = parts[name+'.collect']['local']
        code = (f'static void increment{suffix}() {{\n{inc}}}\n'
                f'static unsigned long long collect{suffix}() {{\n{collect}return {local};\n}}\n')
        helpers += code
        probes += body('global.'+name)+code
    fixture = PRELUDE+declarations+FAKES+body('scope')+body('anomalyBank')+body('callback')+helpers+TESTS
    probes = re.sub(r'^#line[^\n]*\n', '', probes, flags=re.M)
    return fixture, probes


def execute(argv, directory, env, log, timeout=120):
    result = subprocess.run([str(arg) for arg in argv], cwd=directory, env=env,
                            capture_output=True, timeout=timeout, creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
    for stream in ('stdout', 'stderr'):
        raw = getattr(result, stream)
        try:
            decoded = raw.decode('utf-8')
        except UnicodeDecodeError:
            decoded = raw.decode('mbcs' if os.name == 'nt' else 'utf-8', errors='replace')
        setattr(result, stream, decoded)
    log.write_text(result.stdout+result.stderr, encoding='utf-8')
    return result


def compiler_environment(run):
    need(VCVARS.is_file(), 'Missing vcvars64: '+str(VCVARS))
    windows = Path(os.environ.get('SystemRoot','C:/Windows'))
    env = {k:v for k,v in os.environ.items() if k.upper() not in ('PATH','CL','_CL_','LINK','_LINK_')}
    env['PATH'] = str(windows/'System32')+os.pathsep+str(windows)
    env['VSLANG'] = '1033'
    script = run/'compiler-environment.cmd'
    script.write_text('@echo off\r\ncall "'+str(VCVARS)+'" >nul\r\nif errorlevel 1 exit /b %errorlevel%\r\n'
                      'echo VCToolsInstallDir=%VCToolsInstallDir%\r\necho VCToolsVersion=%VCToolsVersion%\r\n'
                      'echo WindowsSDKVersion=%WindowsSDKVersion%\r\necho INCLUDE=%INCLUDE%\r\necho LIB=%LIB%\r\necho LIBPATH=%LIBPATH%\r\n', encoding='utf-8')
    result = execute([windows/'System32/cmd.exe','/d','/c',script],run,env,run/'compiler-environment.log',45)
    need(result.returncode == 0, 'vcvars64 failed')
    for line in result.stdout.splitlines():
        key, sep, value = line.partition('=')
        if sep and key in ('VCToolsInstallDir','VCToolsVersion','WindowsSDKVersion','INCLUDE','LIB','LIBPATH'):
            env[key] = value
    compiler = Path(env['VCToolsInstallDir'])/'bin/Hostx64/x64/cl.exe'
    need(compiler.is_file(), 'vcvars did not identify x64 cl.exe')
    env['PATH'] = str(compiler.parent)+os.pathsep+env['PATH']
    return compiler, env


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-root',type=Path,default=DEFAULT_OUT)
    parser.add_argument('--extract-only',action='store_true',help='Freeze/validate sources without compiling; not a passing receipt')
    args = parser.parse_args()
    output = args.output_root.resolve()
    run = output/('run-'+dt.datetime.now().strftime('%Y%m%d-%H%M%S-%f')+'-'+str(os.getpid()))
    run.mkdir(parents=True)
    receipt = {'pass':False,'status':'RUNNING','run':str(run),'repo':str(ROOT),
               'scope':'offline actual main callback/wrapper/anomaly bank; real Recorder; fake Win32/BUS/role/cost',
               'unknown':['late token across owners','actual timer kill/retirement/drain','waitable/sleep fallback callbacks','real input/Steam consumption'],
               'profiles':[]}
    def publish():
        receipt['artifacts'] = {str(p.relative_to(run)):sha(p.read_bytes()) for p in run.rglob('*') if p.is_file() and p.name!='RESULT.json'}
        save(run/'RESULT.json',receipt)
        save(output/'RESULT.json',receipt)
    try:
        sources = {name:(ROOT/name).read_bytes() for name in
                   ('native/main.cpp','native/input_timer_evidence.h','deps/json/json.hpp','tools/input_timer_evidence_adapter_selftest.py')}
        text = sources['native/main.cpp'].decode('utf-8-sig')
        parts = extract(text)
        bom = 3 if sources['native/main.cpp'].startswith(b'\xef\xbb\xbf') else 0
        receipt['binding'] = {
            'sources':{name:{'path':str(ROOT/name),'sha256':sha(data)} for name,data in sources.items()},
            'fragments':{name:{'source':'native/main.cpp','line':p['line'],
                               'endLine':text.count('\n',0,p['end'])+1,
                               'byteOffset':bom+len(text[:p['start']].encode('utf-8')),
                               'sha256':sha(p['text'].encode('utf-8'))} for name,p in parts.items()}}
        save(run/'actual-fragments.json',parts)
        for name,data in sources.items():
            frozen = run/'sources'/name
            frozen.parent.mkdir(parents=True,exist_ok=True)
            frozen.write_bytes(data)
        fixture,probes = render(parts)
        (run/'fixture.cpp').write_text(fixture,encoding='utf-8')
        (run/'counter-positive.cpp').write_text(probes,encoding='utf-8')
        if args.extract_only:
            receipt['status'] = 'EXTRACTED_ONLY'
            publish()
            print(json.dumps({'pass':False,'status':receipt['status'],'result':str(output/'RESULT.json')},ensure_ascii=False))
            return 0
        compiler,env = compiler_environment(run)
        receipt['compiler'] = {'path':str(compiler),'sha256':sha(compiler.read_bytes()),
                               'vcvarsPath':str(VCVARS),'vcvarsSha256':sha(VCVARS.read_bytes()),
                               'vcToolsVersion':env.get('VCToolsVersion'),'sdkVersion':env.get('WindowsSDKVersion'),
                               'pathPolicy':'toolchain x64 bin + Windows/System32 + Windows; inherited PATH discarded'}
        for profile,flags in (('R',['/O2','/DNDEBUG','/MT']),('C',['/Od','/RTC1','/D_DEBUG','/D_ITERATOR_DEBUG_LEVEL=2','/MTd'])):
            target = run/profile
            target.mkdir()
            exe = target/'adapter.exe'
            dependencies = target/'dependencies.json'
            common = [compiler,'/nologo','/std:c++17','/utf-8','/EHsc','/W4','/diagnostics:caret',*flags]
            command = common+['/sourceDependencies',dependencies,run/'fixture.cpp',
                              '/I'+str(run/'sources/native'),'/I'+str(run/'sources/deps/json'),
                              '/Fo'+str(target/'adapter.obj'),'/Fd'+str(target/'adapter.pdb'),
                              '/Fe'+str(exe),'/link','/INCREMENTAL:NO']
            built = execute(command,target,env,target/'compile.log')
            need(built.returncode==0,profile+' compile failed: '+str(target/'compile.log'))
            dep = json.loads(dependencies.read_text(encoding='utf-8-sig'))['Data']
            inputs = [dep['Source'],*dep['Includes']]
            save(target/'actual-compile-inputs.json',[{'path':p,'sha256':sha(Path(p).read_bytes())} for p in inputs])
            imported = execute([compiler.parent/'dumpbin.exe','/imports',exe],target,env,target/'imports.txt')
            need(imported.returncode==0,'dumpbin failed')
            forbidden = [name for name in FORBIDDEN if name in imported.stdout.lower()]
            need(not forbidden,'Forbidden real control imports: '+repr(forbidden))
            tested = execute([exe],target,env,target/'run.log',60)
            need(tested.returncode==0,profile+' fixture failed: '+tested.stdout[-1200:])
            observed = json.loads(tested.stdout)
            need(observed['pass'] is True,'Fixture did not report success')
            save(target/'OBSERVATIONS.json',observed)
            positive = execute(common+['/c',run/'counter-positive.cpp','/Fo'+str(target/'counter-positive.obj')],target,env,target/'counter-positive.log')
            need(positive.returncode==0,'Counter positive control failed')
            negatives = []
            for counter in COUNTERS:
                declaration = parts['global.'+counter]['text']
                ordinary,changed = re.subn(r'std::atomic\s*<\s*([^<>]+)\s*>',r'\1',declaration)
                need(changed==1 and probes.count(declaration)==1,'Counter declaration mutation must be unique')
                mutant = target/(counter+'-ordinary.cpp')
                mutant.write_text(probes.replace(declaration,ordinary,1),encoding='utf-8')
                negative = execute(common+['/c',mutant,'/Fo'+str(mutant.with_suffix('.obj'))],target,env,mutant.with_suffix('.log'))
                diagnostics = negative.stdout+negative.stderr
                # Require diagnostics at BOTH exact operation lines. MSVC reports
                # C2737 for an invalid exchange in a const initializer, in addition
                # to C2228 for fetch_add. Source preview text alone is insufficient.
                errors = {int(line): code for line, code in re.findall(
                    r'\.cpp\((\d+)(?:,\d+)?\): error (C\d+):', diagnostics)}
                mutated = mutant.read_text(encoding='utf-8')
                operation_errors = {}
                for operation, allowed in (('increment', ('C2228',)), ('collect', ('C2228', 'C2737'))):
                    snippet = parts[counter+'.'+operation]['text']
                    line = mutated.count('\n', 0, mutated.index(snippet))+1
                    code = errors.get(line)
                    need(negative.returncode!=0 and code in allowed,
                         'Ordinary counter must fail at actual '+operation+' line: '+counter)
                    operation_errors[operation] = {'line': line, 'code': code}
                negatives.append({'counter':counter,'pass':True,'exitCode':negative.returncode,
                                  'mutation':{'from':declaration,'to':ordinary},'diagnosticsAtActualOperations':operation_errors,
                                  'sourceSha256':sha(mutant.read_bytes())})
            receipt['profiles'].append({'profile':profile,'pass':True,'checks':observed['checks'],
                                        'cases':len(observed['cases']),'conservation':observed['conservation'],
                                        'negativeCompiles':negatives,'positiveCounterCompile':True,
                                        'realControlImportsAbsent':True,'compiledInputs':len(inputs),
                                        'executableSha256':sha(exe.read_bytes()),'buildArguments':[str(x) for x in command]})
            print(profile+': '+str(observed['checks'])+' runtime checks; 2 ordinary-counter compile negatives passed',flush=True)
        latest = (ROOT/'native/main.cpp').read_bytes()
        current = extract(latest.decode('utf-8-sig'))
        need(all(current[name]['text']==p['text'] for name,p in parts.items()),'Tested main fragments changed during run')
        need(all((ROOT/name).read_bytes()==data for name,data in sources.items() if name!='native/main.cpp'),'Header/JSON/runner changed during run')
        receipt['binding'].update(mainSha256After=sha(latest),wholeMainStable=latest==sources['native/main.cpp'],testedFragmentsStable=True)
        receipt['checks'] = {'runtime':sum(p['checks'] for p in receipt['profiles']),'negativeCompiles':4,'positiveCounterCompiles':2}
        receipt['pass'] = True
        receipt['status'] = 'PASS'
        publish()
        print(json.dumps({'pass':True,'checks':receipt['checks'],'result':str(output/'RESULT.json')},ensure_ascii=False))
        return 0
    except Exception as error:
        receipt['status'] = 'FAIL'
        receipt['error'] = str(error)
        publish()
        print(json.dumps({'pass':False,'error':str(error),'result':str(output/'RESULT.json')},ensure_ascii=False),file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
