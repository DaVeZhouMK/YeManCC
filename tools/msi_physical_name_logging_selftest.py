# Real metadata helpers/enumeration prefix; platform APIs are all inert fakes.
import argparse,datetime,json,pathlib,subprocess,hashlib
from log_frequency_selftest import ROOT,extract
PRE=r'''
#define NOMINMAX
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <array>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>
#include <iostream>
#include <cstdio>
#include <algorithm>
#include "json.hpp"
#include "msi_physical_name_evidence.h"
using json=nlohmann::json;
static std::vector<std::string> apiCalls;
static bool failOpen=false,failPreparsed=false,failCaps=false,failProduct=false,nonTerminated=false;static wchar_t productUnit=0x0409;
static std::string rawPath(LPCWSTR path){std::string value;if(!path)return "null";for(;*path;++path)value+=std::to_string(static_cast<unsigned short>(*path))+",";return value;}
static std::string fakeHandle(HANDLE h){return std::to_string(reinterpret_cast<uintptr_t>(h));}
static HANDLE fakeOpen(LPCWSTR path,DWORD access,DWORD share,LPSECURITY_ATTRIBUTES attributes,DWORD creation,DWORD flags,HANDLE fileTemplate){apiCalls.push_back("open:"+std::to_string(access)+":"+std::to_string(share)+":"+std::to_string(creation)+":"+std::to_string(flags)+":path="+rawPath(path)+":security="+std::to_string(attributes!=nullptr)+":template="+fakeHandle(fileTemplate));if(failOpen){SetLastError(5);return INVALID_HANDLE_VALUE;}return reinterpret_cast<HANDLE>(0x1234);}
static BOOLEAN fakePreparsed(HANDLE h,PHIDP_PREPARSED_DATA* out){apiCalls.push_back("preparsed:"+fakeHandle(h)+":out="+std::to_string(out!=nullptr));if(failPreparsed){SetLastError(6);return FALSE;}*out=reinterpret_cast<PHIDP_PREPARSED_DATA>(0x4321);return TRUE;}
static NTSTATUS fakeCaps(PHIDP_PREPARSED_DATA preparsed,PHIDP_CAPS caps){apiCalls.push_back("caps:"+fakeHandle(reinterpret_cast<HANDLE>(preparsed))+":out="+std::to_string(caps!=nullptr));if(failCaps)return HIDP_STATUS_INVALID_PREPARSED_DATA;caps->UsagePage=1;caps->Usage=5;return HIDP_STATUS_SUCCESS;}
static BOOLEAN fakeFree(PHIDP_PREPARSED_DATA preparsed){apiCalls.push_back("free:"+fakeHandle(reinterpret_cast<HANDLE>(preparsed)));return TRUE;}
static BOOLEAN fakeProduct(HANDLE h,PVOID buffer,ULONG bytes){apiCalls.push_back("product:"+std::to_string(bytes)+":handle="+fakeHandle(h)+":out="+std::to_string(buffer!=nullptr));if(failProduct){SetLastError(31);return FALSE;}auto* w=static_cast<wchar_t*>(buffer);if(nonTerminated){for(size_t i=0;i<bytes/2;++i)w[i]=productUnit;}else{w[0]=productUnit;w[1]=0;}return TRUE;}
static BOOL fakeClose(HANDLE h){apiCalls.push_back("close:"+fakeHandle(h));return TRUE;}
#define CreateFileW fakeOpen
#define HidD_GetPreparsedData fakePreparsed
#define HidP_GetCaps fakeCaps
#define HidD_FreePreparsedData fakeFree
#define HidD_GetProductString fakeProduct
#define CloseHandle fakeClose
struct FakeDevice{
 DIDEVICEINSTANCEW instance{};std::wstring path;DWORD vid=0x0db0,pid=0x1902;bool pathFails=false,capsFails=false;
 HRESULT GetProperty(REFGUID prop,LPDIPROPHEADER out){
  const auto arguments=":"+std::to_string(out->dwSize)+":"+std::to_string(out->dwHeaderSize)+":"+std::to_string(out->dwObj)+":"+std::to_string(out->dwHow);
  if(reinterpret_cast<uintptr_t>(&prop)==reinterpret_cast<uintptr_t>(&DIPROP_VIDPID)){apiCalls.push_back("dinput-vidpid"+arguments);reinterpret_cast<DIPROPDWORD*>(out)->dwData=MAKELONG(vid,pid);return S_OK;}
  apiCalls.push_back("dinput-path"+arguments);if(pathFails)return E_FAIL;auto* p=reinterpret_cast<DIPROPGUIDANDPATH*>(out);wcsncpy_s(p->wszPath,path.c_str(),_TRUNCATE);return S_OK;
 }
 HRESULT GetCapabilities(LPDIDEVCAPS caps){apiCalls.push_back("dinput-caps:"+std::to_string(caps->dwSize));if(capsFails)return E_FAIL;caps->dwAxes=6;caps->dwButtons=17;caps->dwPOVs=1;return S_OK;}
 ULONG Release(){apiCalls.push_back("dinput-release");return 0;}
};
struct FakeDI{
 std::vector<FakeDevice> devices;HRESULT enumerationResult=S_OK;
 HRESULT CreateDevice(REFGUID guid,FakeDevice** out,LPUNKNOWN){apiCalls.push_back("dinput-create");for(auto& d:devices)if(d.instance.guidInstance.Data1==guid.Data1){*out=&d;return S_OK;}*out=nullptr;return E_FAIL;}
 HRESULT EnumDevices(DWORD cls,LPDIENUMDEVICESCALLBACKW callback,LPVOID context,DWORD flags){apiCalls.push_back("dinput-enum:"+std::to_string(cls)+":"+std::to_string(flags));for(auto& d:devices)if(callback(&d.instance,context)==DIENUM_STOP)break;return enumerationResult;}
};
static HRESULT fakeDIcreate(HINSTANCE,DWORD,REFIID,LPVOID*,LPUNKNOWN){throw std::runtime_error("fixture-forbids-real-DirectInput-construction");}
#define DirectInput8Create fakeDIcreate
#define IDirectInput8W FakeDI
#define IDirectInputDevice8W FakeDevice
struct State{FakeDevice* pad=nullptr;FakeDI* di=nullptr;int openAttempts=0,vendorCandidates=0,dinputPidCandidates=0,otherPidCandidates=0,rejectedNonMainInterface=0,rejectedPath=0;};static State g_msiDinput;
struct Capability{DWORD vendorId=0x0db0,pidDInput=0x1902;const char* mainInterfaceToken="&mi_00&col01";};static Capability kMsiClawCapability;
static std::atomic<bool> g_msiDinputRouteDinputArmed{true};static bool detailed=true,msiBoard=true;static bool msiClawKnownBoardAuthority(){return msiBoard;}static bool domainLogEnabled(const char*){return detailed;}
static unsigned long long currentPowerGeneration(){return 7;}static unsigned long long inputHostEpochToken(){return 5;}
static std::atomic<unsigned long long> g_msiNameEvidenceSequence{0},g_msiNameEvidenceFailures{0};
static std::mutex g_msiEnumerationReceiptMx;static json g_msiEnumerationReceipt;static std::atomic<bool> g_msiEnumerationReceiptPending{false};static std::atomic<unsigned long long> g_msiEnumerationReceiptDropped{0};static unsigned long long g_msiEnumerationReceiptCoalesced=0;
static std::string W2U(const std::wstring& value){if(value.empty())return {};const auto n=WideCharToMultiByte(CP_UTF8,0,value.data(),int(value.size()),nullptr,0,nullptr,nullptr);std::string s(n,'\0');WideCharToMultiByte(CP_UTF8,0,value.data(),int(value.size()),s.data(),n,nullptr,nullptr);return s;}
'''
TEST=r'''
int main(){
 json checks=json::array();auto check=[&](const char* name,bool ok){checks.push_back({{"name",name},{"pass",ok}});};
 DIDEVICEINSTANCEW instance{};instance.tszProductName[0]=wchar_t(0x0609);DIDEVCAPS caps{};caps.dwAxes=6;caps.dwButtons=17;
 auto row=msiDinputEnumerationReceipt(instance,0x0db0,0x1902,L"fixture:main",S_OK,caps,S_OK,true);
 check("DInput-raw-name-and-text-unchanged-before-conversion",row["productUtf16"]==json::array({1545})&&row["product"]=="؉"&&row["productNameWitness"]["codeUnits"]==json::array({1545}));
 check("DInput-wide-API-provenance-and-bounded-terminator",row["productNameWitness"]["terminatedWithinBuffer"]==true&&row["productNameWitness"]["bufferCapacityCodeUnits"]==MAX_PATH&&row["directInputApiEncoding"]=="IID_IDirectInput8W;callback-DIDEVICEINSTANCEW");
 for(int scenario=0;scenario<6;++scenario){
  failOpen=scenario==1;failPreparsed=scenario==2;failCaps=scenario==3;failProduct=scenario==4;nonTerminated=scenario==5;apiCalls.clear();auto original=row;msiDinputEnumerationHidMetadataBaseline(original,L"fixture:main");const auto originalCalls=apiCalls;
  apiCalls.clear();auto observed=row;msiDinputEnumerationHidMetadata(observed,L"fixture:main");
  check("HID-all-old-API-calls-and-arguments-retained-for-every-success-failure",apiCalls==originalCalls);
  for(const auto* field:{"hidMetadataHandleOpened","hidCapabilitiesKnown","hidUsagePage","hidUsage","hidProductStringKnown","hidProductString","hidProductUtf16"})check("original-HID-return-fields-preserved",observed.contains(field)==original.contains(field)&&(!observed.contains(field)||observed[field]==original[field]));
  if(scenario==0){check("HID-versus-DInput-raw-difference-is-comparison-not-cause",observed["hidProductNameWitness"]["codeUnits"]==json::array({1033})&&observed["dinputAndHidRawComparison"]=="different-code-units");check("HID-call-time-error-and-buffer-size-recorded",observed["hidProductObservation"]["api"]=="HidD_GetProductString"&&observed["hidProductObservation"]["resultCode"]==0&&observed["hidProductNameWitness"]["bufferCapacityCodeUnits"]==128);}
  if(scenario==1)check("open-failure-code-captured-before-JSON-work",observed["hidMetadataOpenError"]==5&&observed["hidOpenObservation"]["resultCode"]==5);
  if(scenario==2)check("failed-preparsed-does-not-call-HidP-or-invent-caps",observed["hidCapsObservation"]["attempted"]==false&&observed["hidPreparsedObservation"]["resultCode"]==6&&!observed.contains("hidUsage"));
  if(scenario==4)check("product-failure-has-error-not-empty-success-name",observed["hidProductObservation"]["resultCode"]==31&&observed["dinputAndHidRawComparison"]=="not-observed"&&!observed.contains("hidProductUtf16"));
  if(scenario==5)check("full-bounded-buffer-no-NUL-not-silently-valid-string",observed["hidProductNameWitness"]["terminatedWithinBuffer"]==false&&observed["hidProductNameWitness"]["codeUnits"].size()==128);
 }
 failOpen=failPreparsed=failCaps=failProduct=nonTerminated=false;apiCalls.clear();productUnit=0x0609;auto sameName=row;msiDinputEnumerationHidMetadata(sameName,L"fixture:main");
 check("current-sample-equal-HID-and-DInput-raw-name-not-a-cause",sameName["hidProductUtf16"]==json::array({1545})&&sameName["dinputAndHidRawComparison"]=="same-code-units");productUnit=0x0409;
 apiCalls.clear();auto absent=row;msiDinputEnumerationHidMetadata(absent,L"");check("empty-existing-path-never-opens-extra-device",apiCalls.empty()&&absent["hidMetadataStatus"]=="not-attempted-empty-existing-path");
 wchar_t surrogate[3]={wchar_t(0xd800),wchar_t(0xdc00),0};check("raw-surrogate-code-units-not-rewritten",ymcc::msi::nameEvidence::utf16BufferWitness(surrogate,3,"fixture")["codeUnits"]==json::array({0xd800,0xdc00}));
 FakeDI di;auto reset=[&](bool chooseMain,int count){di.devices.clear();for(int i=0;i<count;++i){FakeDevice d;d.instance.guidInstance.Data1=i+1;d.instance.tszProductName[0]=0x0609;d.path=chooseMain&&i==0?L"\\\\?\\hid#vid_0db0&pid_1902&mi_00&col01":L"\\\\?\\hid#vid_0db0&pid_1902&mi_00&col02";di.devices.push_back(d);}g_msiDinput={};g_msiDinput.di=&di;msiDinputTakeEnumerationReceipt();apiCalls.clear();};
 for(bool selected:{false,true}){
  reset(selected,3);const bool oldSelected=msiDinputEnumerateBaseline();const auto oldCalls=apiCalls;msiDinputTakeEnumerationReceipt();g_msiDinput={};g_msiDinput.di=&di;apiCalls.clear();const bool selectedNow=msiDinputEnumerateObserved();const auto proof=msiDinputTakeEnumerationReceipt();
  check("existing-enumeration-main-selection-and-API-order-unchanged",oldSelected==selectedNow&&apiCalls==oldCalls);
  check("failed-or-selected-enumeration-both-retain-all-candidate-evidence",proof.is_object()&&proof["candidates"].size()==3&&proof["mainCandidateSelected"]==selected);
  check("capture-ID-native-PID-and-independent-API-windows",proof["nativePid"]==GetCurrentProcessId()&&proof["metadataSchemaVersion"]==2&&!proof["crossApiAtomicSnapshot"].get<bool>()&&proof["candidates"][0]["metadataCaptureId"]==proof["metadataCaptureId"]&&proof["candidates"][0].contains("pathObservation")&&proof["candidates"][0].contains("capsObservation"));
 }
 reset(false,0);di.enumerationResult=E_FAIL;msiDinputEnumerateObserved();auto enumFail=msiDinputTakeEnumerationReceipt();
 check("failed-EnumDevices-return-is-not-missing-return-or-success",enumFail["enumerationReturned"]==true&&enumFail["enumerationHresultSuccess"]==false&&enumFail["enumerationHresult"]==E_FAIL&&enumFail["mainCandidateSelected"]==false&&enumFail["candidates"].empty());di.enumerationResult=S_OK;
 reset(false,18);msiDinputEnumerateObserved();auto capped=msiDinputTakeEnumerationReceipt();check("bounded-catalogue-16-records-explicit-two-omitted",capped["candidates"].size()==16&&capped["unrecordedDinputCandidates"]==2);
 detailed=false;reset(true,3);msiDinputEnumerateObserved();check("detail-off-produces-no-catalogue-or-HID-metadata-I-O",msiDinputTakeEnumerationReceipt().is_null()&&std::find_if(apiCalls.begin(),apiCalls.end(),[](const auto& s){return s.starts_with("open:")||s.starts_with("product:");})==apiCalls.end());
 detailed=true;msiBoard=false;reset(true,3);msiDinputEnumerateObserved();check("board-authority-off-does-not-expand-metadata",msiDinputTakeEnumerationReceipt().is_null()&&std::none_of(apiCalls.begin(),apiCalls.end(),[](const auto& call){return call.starts_with("open:");}));msiBoard=true;
 g_msiDinputRouteDinputArmed.store(false);reset(true,3);msiDinputEnumerateObserved();check("DInput-route-off-does-not-expand-metadata",msiDinputTakeEnumerationReceipt().is_null()&&std::none_of(apiCalls.begin(),apiCalls.end(),[](const auto& call){return call.starts_with("open:");}));g_msiDinputRouteDinputArmed.store(true);
 reset(true,4);di.devices[1].pid=0x1901;di.devices[2].pid=0x1903;di.devices[3].vid=0xabcd;msiDinputEnumerateObserved();auto pidScope=msiDinputTakeEnumerationReceipt();check("other-PID-and-vendor-do-not-gain-HID-metadata-reads",pidScope["candidates"].size()==1&&pidScope["dinputPidCandidates"]==1&&pidScope["otherPidCandidates"]==2);
 reset(true,2);di.devices[0].pathFails=true;msiDinputEnumerateObserved();const auto pathFail=msiDinputTakeEnumerationReceipt();check("failed-existing-path-return-recorded-not-guessed-main",pathFail["mainCandidateSelected"]==false&&pathFail["rejectedPath"]==1&&pathFail["candidates"][0]["pathStatus"]=="read-error"&&!pathFail["candidates"][0]["selectedMain"].get<bool>());
 const auto emission=ymcc::msi::nameEvidence::logEmissionWitness(1234,json(1200));
 check("emission-delay-uses-emission-clock-not-audit-start",emission["emittedTick"]==1234&&emission["logEmissionDelayMs"]==34&&emission["logEmissionDelayStatus"]=="observed-monotonic-delay");
 check("emission-before-capture-not-invented-zero-delay",ymcc::msi::nameEvidence::logEmissionWitness(1200,json(1234))["logEmissionDelayMs"]=="not-observed");
 check("missing-or-invalid-capture-end-is-explicitly-unobserved",ymcc::msi::nameEvidence::logEmissionWitness(1200,json(nullptr))["logEmissionDelayStatus"]=="missing-capture-end"&&ymcc::msi::nameEvidence::logEmissionWitness(1200,json(-1))["logEmissionDelayStatus"]=="invalid-capture-end");
 wchar_t unterminated[2]={wchar_t(1545),wchar_t(1033)};auto witness=ymcc::msi::nameEvidence::utf16BufferWitness(unterminated,2,"fixture");
 check("bounded-raw-code-unit-witness-valid-JSON-without-NUL",json::parse(witness.dump())["codeUnits"]==json::array({1545,1033})&&witness["bufferBoundary"]=="no-nul-in-bounded-buffer");
 auto first=msiPhysicalNameCaptureContext(),next=msiPhysicalNameCaptureContext();check("unique-capture-ID-same-native-run",first["metadataCaptureId"]!=next["metadataCaptureId"]&&first["metadataRunId"]==next["metadataRunId"]);
 int failures=0;for(const auto& c:checks)if(!c["pass"].get<bool>())++failures;
 std::cout<<json({{"checks",checks},{"failures",failures},{"realHardwareAccess",false},{"allDeviceMetadataCallsFake",true},{"scope","ACTUAL_METADATA_FUNCTIONS_AND_ENUMERATION_PREFIX_FAKE_APIS_NO_HARDWARE"}}).dump(2);return failures?1:0;
}
'''
def main():
 p=argparse.ArgumentParser();p.add_argument('--output-dir',type=pathlib.Path,required=True);a=p.parse_args();run=a.output_dir/('run-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S'));run.mkdir(parents=True);source_bytes=(ROOT/'native/main.cpp').read_bytes();baseline_bytes=(a.output_dir.parent/'pre-edit/native/main.cpp').read_bytes();header_bytes=(ROOT/'native/msi_physical_name_evidence.h').read_bytes();source=source_bytes.decode('utf-8-sig').replace('\r\n','\n');old=baseline_bytes.decode('utf-8-sig').replace('\r\n','\n');(run/'msi_physical_name_evidence.h').write_bytes(header_bytes)
 signatures=['static std::string msiGuidToString(const GUID& g) {','static bool msiPathMentionsAsciiToken(const wchar_t* hay, const char* needleAscii) {','static std::string msiPhysicalNameRunId() {','static std::string msiPhysicalNameUtc100ns() {','static json msiPhysicalNameCaptureContext() noexcept {','static void msiDinputStageEnumerationReceipt(json row) {','static json msiDinputTakeEnumerationReceipt() {','static json msiDinputEnumerationReceipt(const DIDEVICEINSTANCEW& inst,DWORD vid,DWORD pid,','static void msiDinputEnumerationHidMetadata(json& row,const std::wstring& path) {']
 parts='\n'.join(extract(source,s)for s in signatures);oldHid=extract(old,signatures[-1]).replace('msiDinputEnumerationHidMetadata(','msiDinputEnumerationHidMetadataBaseline(',1)
 def prefix(text,name,hid):
  f=extract(text,'static bool msiDinputOpen() {');f=f[:f.index('    IDirectInputDevice8W* pad = ctx.pad;')];return f.replace('msiDinputOpen()',name+'()',1).replace('msiDinputEnumerationHidMetadata(',hid+'(')+'    return true;\n}'
 code=PRE+'\n'+parts+'\n'+oldHid+'\n'+prefix(source,'msiDinputEnumerateObserved','msiDinputEnumerationHidMetadata')+'\n'+prefix(old,'msiDinputEnumerateBaseline','msiDinputEnumerationHidMetadataBaseline')+'\n'+TEST
 cpp=run/'fixture.cpp';cpp.write_text(code,encoding='utf-8');exe=run/'fixture.exe';vc=r'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat';batch=run/'build.cmd';batch.write_text(f'@echo off\ncall "{vc}" >nul && cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 "{cpp}" /I"{ROOT/"native"}" /I"{ROOT/"deps/json"}" /Fo"{run/"fixture.obj"}" /Fe"{exe}" /link user32.lib dxguid.lib\n')
 built=subprocess.run(['cmd.exe','/d','/c',str(batch)],cwd=run,capture_output=True,text=True,errors='replace');(run/'compile.log').write_text(built.stdout+built.stderr,encoding='utf-8')
 if built.returncode:print(built.stdout+built.stderr);return built.returncode
 tested=subprocess.run([str(exe)],cwd=run,capture_output=True,encoding='utf-8',errors='replace');(run/'stderr.txt').write_text(tested.stderr,encoding='utf-8')
 if not tested.stdout:print(tested.stderr);return tested.returncode or 1
 report=json.loads(tested.stdout);report.update(sourceSha256=hashlib.sha256(source.encode()).hexdigest(),sourceSha256Meaning='UTF-8-no-BOM-LF-normalized-full-main.cpp',rawSourceSha256=hashlib.sha256(source_bytes).hexdigest(),baselineRawSourceSha256=hashlib.sha256(baseline_bytes).hexdigest(),evidenceHeaderSha256=hashlib.sha256(header_bytes).hexdigest(),currentRawSourceMatchesCaptured=(ROOT/'native/main.cpp').read_bytes()==source_bytes,currentHeaderMatchesCaptured=(ROOT/'native/msi_physical_name_evidence.h').read_bytes()==header_bytes,clockAndPidApisReal=True,fixtureSourceSha256=hashlib.sha256(cpp.read_bytes()).hexdigest(),fixtureArtifactSha256=hashlib.sha256(exe.read_bytes()).hexdigest(),runDirectory=str(run));text=json.dumps(report,indent=2)+'\n';(a.output_dir/'report.json').write_text(text,encoding='utf-8');(run/'report.json').write_text(text,encoding='utf-8');print(json.dumps(report,indent=2));return tested.returncode
if __name__=='__main__':raise SystemExit(main())
