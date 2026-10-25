#include <cstdint>
#include <cwchar>
#include <string>
#include <functional>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include "json.hpp"
#include "msi_quarantine_diagnostics.h"
#include "msi_slot_api_evidence.h"
using json=nlohmann::json;using DWORD=uint32_t;using BYTE=uint8_t;using HMODULE=void*;
static bool board=true,detail=true,g_inputHostReleaseQuarantined=true,g_xinputPhysicalSourceLocked=true;
static DWORD g_xinputConnectedMask=1,g_xinputPhysicalSourceMask=0,g_xinputVirtualEndpointMask=0;
static uint64_t g_inputHostEpoch=7;static std::string g_inputHostRunId="offline";static void*g_inputHostProcess=nullptr;
static struct{bool virtualTargetEnabled=true;}g_realStickTest;
static ymcc::msi::quarantine::Budget g_msiQuarantineDiagnosticBudget;
static std::function<json()>g_msiQuarantineSlotEvidenceForTest;
static unsigned paths=0,indices=0,hooks=0,receipts=0,resolvers=0,modules=0,lastErrors=0,sets=0,refreshes=0;
static bool cacheAvailable=true,sharedGate=true;static unsigned throwStage=0,moduleMode=0;
static HMODULE g_ymccOpenXInputModule=reinterpret_cast<void*>(1),g_ymccXInput14Module=reinterpret_cast<void*>(1);
static json msiQuarantineCachedEvidence(){return {{"available",cacheAvailable},{"connectedMask",g_xinputConnectedMask},{"physicalSourceLocked",g_xinputPhysicalSourceLocked},{"physicalSourceMask",g_xinputPhysicalSourceMask},{"virtualEndpointMask",g_xinputVirtualEndpointMask},{"scope","synthetic snapshot; actual lock/copy helper verified separately in native-entry"}};}
static bool msiClawKnownBoardAuthority(){return board;}static bool domainLogEnabled(const char*){return detail;}
static uint64_t GetTickCount64(){return 1000;}static std::string inputHostConfiguredPersona(){return "elite";}
static std::string W2U(const std::wstring&w){return std::string(w.begin(),w.end());}
// Every platform entry referenced by the extracted real helper is a local fake;
// no Windows headers, import library, DLL loader or controller I/O is linked.
static bool ymccOpenXInputAvailable(){++resolvers;return sharedGate;}
static DWORD GetModuleFileNameW(HMODULE,wchar_t*buffer,DWORD count){++modules;
    if(moduleMode==1)return 0;if(moduleMode==2)return count;
    const std::wstring p=L"G:\\FAKE\\Xinput1_4.dll";std::copy(p.begin(),p.end(),buffer);buffer[p.size()]=0;return DWORD(p.size());}
static DWORD GetLastError(){++lastErrors;return moduleMode==1?126u:122u;}
static DWORD fakeGetPath(DWORD,wchar_t*buffer,DWORD*count){++paths;
    if(!buffer){*count=9;if(throwStage==1)throw std::runtime_error("size");return 0;}
    if(throwStage==2){*count=7;throw std::runtime_error("read");}
    const std::wstring p=L"FAKE-API";std::copy(p.begin(),p.end(),buffer);buffer[p.size()]=0;*count=9;return 0;}
static DWORD fakeGetIndex(const wchar_t*,BYTE*index){++indices;*index=0;if(throwStage==3)throw std::runtime_error("reverse");return 0;}
static DWORD fakeSetIndex(const wchar_t*,BYTE,bool){++sets;return 0;}
static DWORD fakeRefresh(){++refreshes;return 0;}
static auto g_ymccOpenXInputGetDevicePath=&fakeGetPath;
static auto g_ymccOpenXInputGetUserIndex=&fakeGetIndex;
static auto g_ymccOpenXInputSetUserIndex=&fakeSetIndex;
static auto g_ymccOpenXInputRefreshMappings=&fakeRefresh;
static json lastReceipt;static void appendNativeLifecycleLog(const char*,const json&r){++receipts;lastReceipt=r;}
// Generated from both real native helper bodies, not a handwritten substitute.
#include "msi_quarantine_actual_helper.inc"
int main(int argc,char**argv){if(argc!=2)return 2;
    json out={{"allPlatformApisStubbed",true},{"realHardwareAccess",false},{"copiedActualHelperBody",true},{"copiedActualSlotApiHelperBody",true},{"cases",json::array()}};
    unsigned failed=0;const bool enabled=ymcc::msi::quarantine::buildEnabled;
#if defined(YMCC_FAULT_INJECT) || defined(YMCC_MSI_FOREGROUND_ISOLATION_TEST)
    const bool offline=true;
#else
    const bool offline=false;
#endif
    auto reset=[](){board=detail=g_inputHostReleaseQuarantined=cacheAvailable=sharedGate=true;paths=indices=hooks=receipts=resolvers=modules=lastErrors=sets=refreshes=0;throwStage=moduleMode=0;g_msiQuarantineDiagnosticBudget.begin();g_msiQuarantineSlotEvidenceForTest={};lastReceipt=nullptr;g_xinputConnectedMask=1;g_ymccOpenXInputModule=g_ymccXInput14Module=reinterpret_cast<void*>(1);g_ymccOpenXInputGetDevicePath=&fakeGetPath;g_ymccOpenXInputGetUserIndex=&fakeGetIndex;g_ymccOpenXInputSetUserIndex=&fakeSetIndex;g_ymccOpenXInputRefreshMappings=&fakeRefresh;};
    auto noApi=[&](){return paths+indices+resolvers+modules+lastErrors+sets+refreshes==0;};
    auto record=[&](const char*name,bool pass){pass&=sets+refreshes==0;if(!pass)++failed;out["cases"].push_back({{"case",name},{"passed",pass},{"mappingPathCalls",paths},{"mappingReverseCalls",indices},{"resolverCalls",resolvers},{"moduleCalls",modules},{"lastErrorCalls",lastErrors},{"setCalls",sets},{"refreshCalls",refreshes},{"hookCalls",hooks},{"receipts",receipts},{"lastReceipt",lastReceipt}});};
    auto normalApi=[&](){return paths==(enabled?2u:0u)&&indices==(enabled?1u:0u)&&resolvers==(enabled?1u:0u)&&modules==resolvers&&lastErrors==0;};
    reset();g_msiQuarantineSlotEvidenceForTest=[](){++hooks;return json::array();};msiQuarantineDiagnosticReceipt();
    record("hook-present",offline?(hooks==(enabled?1u:0u)&&noApi()):(normalApi()&&hooks==0));
    reset();msiQuarantineDiagnosticReceipt();record("hook-absent",offline?(noApi()&&receipts==(enabled?1u:0u)&&(!receipts||(lastReceipt.at("slots").empty()&&lastReceipt.at("slotEvidenceSource")=="fault-hook-missing"))):(normalApi()&&(!receipts||lastReceipt.at("slotEvidenceSource")=="open-xinput-api")));
    reset();board=false;msiQuarantineDiagnosticReceipt();record("not-msi",noApi()&&hooks+receipts==0);
    reset();detail=false;msiQuarantineDiagnosticReceipt();record("detail-off",noApi()&&hooks+receipts==0);
    reset();g_inputHostReleaseQuarantined=false;msiQuarantineDiagnosticReceipt();record("not-quarantined",noApi()&&hooks+receipts==0);
    for(auto mask:{0u,0x80000000u}){reset();g_xinputConnectedMask=mask;msiQuarantineDiagnosticReceipt();record(mask?"only-non-slot-bits":"mask-zero",noApi());}
    reset();g_msiQuarantineSlotEvidenceForTest=[]()->json{++hooks;throw 1;};msiQuarantineDiagnosticReceipt();record("throwing-hook",offline?(noApi()&&receipts==0&&hooks==(enabled?1u:0u)):(normalApi()&&hooks==0));
    reset();cacheAvailable=false;msiQuarantineDiagnosticReceipt();record("cache-unavailable",noApi()&&hooks==0&&(!receipts||(lastReceipt.at("connectedMask").is_null()&&lastReceipt.at("mappingQueriesSkippedForUnavailableCache")==true)));
    reset();msiQuarantineDiagnosticReceipt();msiQuarantineDiagnosticReceipt();record("budget-no-immediate-retry",offline?(noApi()&&receipts==(enabled?1u:0u)):normalApi());
    if(offline){reset();const auto direct=msiQuarantineSlotApiEvidence(0xf);record("offline-direct-slot-helper-sealed",noApi()&&direct.at("capabilities").at("provider")=="offline-real-api-disabled"&&direct.at("slots").empty());}
    if(!offline){
        reset();g_xinputConnectedMask=0xf;msiQuarantineDiagnosticReceipt();record("four-slots-no-retry",paths==(enabled?8u:0u)&&indices==(enabled?4u:0u)&&resolvers==(enabled?1u:0u)&&modules==resolvers);
        reset();sharedGate=false;msiQuarantineDiagnosticReceipt();bool pass=paths+indices==0&&resolvers==(enabled?1u:0u);if(receipts){const auto&r=lastReceipt.at("slots").at(0);pass&=r.at("queryStatus")=="shared-availability-gate-false"&&r.at("sizeCode").is_null();}record("shared-gate-false",pass);
        reset();g_ymccOpenXInputGetDevicePath=nullptr;msiQuarantineDiagnosticReceipt();pass=paths+indices==0;if(receipts)pass&=lastReceipt.at("slots").at(0).at("queryStatus")=="path-export-unavailable";record("path-export-missing",pass);
        for(unsigned stage=1;stage<=3;++stage){reset();throwStage=stage;msiQuarantineDiagnosticReceipt();pass=paths==(enabled?(stage==1?1u:2u):0u)&&indices==(enabled&&stage==3?1u:0u)&&receipts==(enabled?1u:0u);
            if(receipts){const auto&r=lastReceipt.at("slots").at(0);const char*field=stage==1?"size":stage==2?"read":"reverse";pass&=r.at("queryStatus")=="diagnostic-query-exception"&&r.at(std::string(field)+"Attempted")==true&&r.at(std::string(field)+"Returned")==false&&r.at(std::string(field)+"Code").is_null()&&!r.at("pathReturned").get<bool>()&&r.at("reverseMatches").is_null();if(stage==1)pass&=r.at("requestedCount").is_null();if(stage==2)pass&=r.at("returnedCount").is_null();if(stage==3)pass&=r.at("reverseUserIndex").is_null();}
            record(stage==1?"size-throw-knownness":stage==2?"read-throw-knownness":"reverse-throw-knownness",pass);}
        reset();g_ymccXInput14Module=nullptr;msiQuarantineDiagnosticReceipt();record("canonical-module-unknown",!receipts||(!lastReceipt.at("mappingCapabilities").at("sameModuleAsCanonicalKnown").get<bool>()&&lastReceipt.at("mappingCapabilities").at("sameModuleAsCanonical").is_null()));
        reset();g_ymccOpenXInputModule=nullptr;sharedGate=false;msiQuarantineDiagnosticReceipt();record("open-module-absent",paths+indices+modules+lastErrors==0&&(!receipts||lastReceipt.at("mappingCapabilities").at("modulePathError").is_null()));
        for(unsigned mode=1;mode<=2;++mode){reset();moduleMode=mode;msiQuarantineDiagnosticReceipt();pass=modules==(enabled?1u:0u)&&lastErrors==modules;if(receipts){const auto&cap=lastReceipt.at("mappingCapabilities");pass&=!cap.at("modulePathKnown").get<bool>()&&cap.at("modulePathErrorKnown")==true&&cap.at("modulePathError")==uint32_t(mode==1?126:122);}record(mode==1?"module-path-error":"module-path-truncated",pass);}
        reset();msiQuarantineDiagnosticReceipt();record("module-success-error-unknown",!receipts||(lastReceipt.at("mappingCapabilities").at("modulePathKnown")==true&&lastReceipt.at("mappingCapabilities").at("modulePathError").is_null()&&lastReceipt.at("mappingCapabilities").at("modulePathErrorKnown")==false));
    }
    out["diagnosticEnabled"]=enabled;out["snapshotIsSynthetic"]=true;out["failureCount"]=failed;out["passed"]=failed==0;out["faultIoBoundarySealed"]=failed==0;out["caseCount"]=out["cases"].size();
    std::ofstream f(argv[1]);f<<out.dump(2);f.close();std::cout<<"MSI_QUARANTINE_FAKE_IO "<<out["caseCount"]<<" failures="<<failed<<"\n";return failed?3:0;
}
