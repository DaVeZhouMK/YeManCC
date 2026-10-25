"""Compare extracted ROG binder before/after using fake IO only; verify diagnostic meaning."""
import argparse
import hashlib
import json
import pathlib
import subprocess
from log_frequency_selftest import ROOT, extract

PRELUDE = r'''
#define NOMINMAX
#include <windows.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>
#include <iostream>
#include "json.hpp"
using json=nlohmann::json;
struct Session {bool suppressed=false,hasControl=false,identityMatched=false,hasStableInput=false;
 HANDLE control=INVALID_HANDLE_VALUE,auraControl=INVALID_HANDLE_VALUE;
 std::wstring controlPath,fallbackControlPath;unsigned short productId=0,featureLen=0,auraFeatureLen=0,inputLen=0,fallbackFeatureLen=0;unsigned epoch=0;};
static Session g_rog;static std::mutex g_rogAuraMx;
static std::atomic<unsigned> g_entryTestBindCalls{0};static std::atomic<unsigned long long>g_rogRemapBoundSig{0};
static std::atomic<bool>g_inputReady{true};static unsigned long long g_rogRemapLastPathHash=0;
static bool g_oemGateTargetEnabled=true,g_oemGateBackMapping=true;static int g_oemGatePersona=1;
static GUID kYeManGuidDeviceInterfaceHid{};
static int scenario=0;static DWORD fakeError=0;static json traces,logs;
static void trace(const char* s){traces.push_back(s);}
static void writeLog(const std::string&,const std::string&){}
static void appendNativeLifecycleLog(const char* e,json d){d["event"]=e;logs.push_back(d);}
static void rogCloseSession(bool){trace("close-session");g_rog=Session{};}
static unsigned long long rogRemapPackSig(unsigned e,bool m,unsigned short p,unsigned long long h){return e+m+p+h;}
static unsigned long long rogRemapPathHash15(const std::wstring& p){return p.empty()?0:7;}
static void rogRemapNoteDeviceGeneration(const char*){trace("generation");}
static bool rogIsAllyPid(unsigned short p){return p==0x1ABE;}
static void rogInspectDevice(HANDLE h,const wchar_t* path,unsigned short){trace("inspect");
 if(scenario==4){g_rog.fallbackControlPath=path;g_rog.fallbackFeatureLen=64;}
 else {g_rog.control=h;g_rog.controlPath=path;g_rog.featureLen=64;g_rog.hasControl=true;}}
static bool rogEnsureControllerRemap(bool,const char*){trace("remap");return true;}
static void oemBackMappingRefreshGate(bool,int,bool){trace("refresh-gate");}
static HDEVINFO fakeGetSet(const GUID*,const wchar_t*,HWND,DWORD){trace("get-set");if(scenario==1){fakeError=5;return INVALID_HANDLE_VALUE;}return (HDEVINFO)1;}
static BOOL fakeEnum(HDEVINFO,void*,const GUID*,DWORD i,SP_DEVICE_INTERFACE_DATA*){
 traces.push_back(std::string("enum-")+std::to_string(i));
 if(scenario==0||scenario==2||i>0){fakeError=scenario==2?ERROR_ACCESS_DENIED:ERROR_NO_MORE_ITEMS;return FALSE;}return TRUE;}
static BOOL fakeDetail(HDEVINFO,SP_DEVICE_INTERFACE_DATA*,SP_DEVICE_INTERFACE_DETAIL_DATA_W* p,DWORD,DWORD* n,void*){
 trace(p?"detail-read":"detail-size");if(n)*n=sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W)+64;
 if(!p)return FALSE;if(scenario==5)return FALSE;wcscpy_s(p->DevicePath,32,L"fixture-hid");return TRUE;}
static HANDLE fakeOpen(const wchar_t*,DWORD,DWORD,void*,DWORD,DWORD,HANDLE){trace("open");return scenario==6?INVALID_HANDLE_VALUE:(HANDLE)2;}
static BOOL fakeClose(HANDLE){trace("close-handle");return TRUE;}
static BOOLEAN fakeAttributes(HANDLE,HIDD_ATTRIBUTES* a){trace("attributes");a->VendorID=scenario==7?0x9999:0x0B05;a->ProductID=0x1ABE;return TRUE;}
static BOOL fakeDestroy(HDEVINFO){trace("destroy-set");return TRUE;}
static DWORD fakeLastError(){return fakeError;}
#define SetupDiGetClassDevsW fakeGetSet
#define SetupDiEnumDeviceInterfaces fakeEnum
#define SetupDiGetDeviceInterfaceDetailW fakeDetail
#define CreateFileW fakeOpen
#define CloseHandle fakeClose
#define HidD_GetAttributes fakeAttributes
#define SetupDiDestroyDeviceInfoList fakeDestroy
#define GetLastError fakeLastError
static void reset(int s){scenario=s;fakeError=0;g_rog=Session{};g_rogRemapLastPathHash=0;g_rogRemapBoundSig=0;g_entryTestBindCalls=0;traces=json::array();logs=json::array();
 if(s==8)g_rog.suppressed=true;if(s==9){g_rog.hasControl=true;g_rog.identityMatched=true;}}
static json state(){return {{"control",g_rog.hasControl},{"matched",g_rog.identityMatched},{"pid",g_rog.productId},{"epoch",g_rog.epoch},
 {"featureLen",g_rog.featureLen},{"stableInput",g_rog.hasStableInput},{"signature",g_rogRemapBoundSig.load()},{"entryCalls",g_entryTestBindCalls.load()}};}
'''
TEST = r'''
int main(){json checks=json::array();unsigned failures=0;
 auto check=[&](std::string n,bool ok){checks.push_back({{"name",n},{"pass",ok}});if(!ok)++failures;};
 for(int s=0;s<10;++s){reset(s);baselineRogBindInputHid();const auto beforeTrace=traces,beforeState=state();
 reset(s);rogBindInputHid();check("same-control-io-and-state-scenario-"+std::to_string(s),beforeTrace==traces&&beforeState==state());
 if(s>=8){check("early-return-no-fabricated-discovery-"+std::to_string(s),logs.empty());continue;}
 const auto summary=logs.front();check("schema2-no-unconditional-discovery-success-"+std::to_string(s),summary["schema"]==2&&!summary.contains("discoveryComplete")&&summary["physicalAbsenceProven"]==false&&summary["interfaceInspectionCoverage"]=="not-measured");
 check("enumeration-termination-is-measured-"+std::to_string(s),summary["enumerationComplete"]==(s!=1&&s!=2)&&summary["enumerationError"]==((s==1||s==2)?5:0));
 if(s==4)check("fallback-before-and-after-receipt",summary["controlBeforeFallback"]==false&&summary["hasControl"]==true);
 }
 std::cout<<json({{"checks",checks},{"failures",failures},{"hardwareAccess",false},{"productLaunched",false}}).dump(2)<<std::endl;return failures?1:0;}
'''

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--baseline',type=pathlib.Path,required=True)
    p.add_argument('--output-dir',type=pathlib.Path,required=True)
    a=p.parse_args();out=a.output_dir.resolve();out.mkdir(parents=True,exist_ok=True)
    raw=(ROOT/'native/main.cpp').read_bytes();source=raw.decode('utf-8-sig');baseline=a.baseline.read_text(encoding='utf-8-sig')
    signature='static void rogBindInputHid() {'
    before=extract(baseline,signature).replace('static void rogBindInputHid()', 'static void baselineRogBindInputHid()',1)
    current=extract(source,signature)
    cpp=out/'harness.cpp';cpp.write_text(PRELUDE+before+'\n'+current+TEST,encoding='utf-8')
    vcvars=r'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
    script=out/'build.cmd';exe=out/'harness.exe'
    script.write_text(f'@echo off\ncall "{vcvars}" >nul\ncl /nologo /std:c++20 /utf-8 /EHsc /MT "{cpp}" /I"{ROOT / "deps/json"}" /Fo"{out / "harness.obj"}" /Fe"{exe}"\n',encoding='utf-8')
    build=subprocess.run(['cmd.exe','/d','/c',str(script)],cwd=out,capture_output=True)
    (out/'compile.log').write_bytes(build.stdout+build.stderr)
    if build.returncode:return build.returncode
    run=subprocess.run([str(exe)],cwd=out,capture_output=True)
    (out/'result.json').write_bytes(run.stdout)
    report=json.loads(run.stdout);report.update(sourceSha256=hashlib.sha256(raw).hexdigest(),
        baselineSha256=hashlib.sha256(a.baseline.read_bytes()).hexdigest(),
        binderStableDuringTest=extract((ROOT/'native/main.cpp').read_text(encoding='utf-8-sig'),signature)==current)
    (out/'report.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    print(json.dumps({'checks':len(report['checks']),'failures':report['failures'],'binderStableDuringTest':report['binderStableDuringTest']}))
    return run.returncode or (0 if report['binderStableDuringTest'] else 1)

if __name__=='__main__':raise SystemExit(main())
