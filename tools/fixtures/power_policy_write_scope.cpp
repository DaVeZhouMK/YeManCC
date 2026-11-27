#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <powrprof.h>
#include <atomic>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include "json.hpp"
#include "sleep_power_state.h"

using json = nlohmann::json;
using namespace ymcc::sleeppower;
static SharedPowerV1 power;
static std::atomic<unsigned int> g_hardwareWriteInFlight{0};
static std::unordered_map<std::string,std::function<json(const json&)>> handlers;
static int writes, openKeys;
static bool changeAfterWrite, changeBeforeFallback, wrongAfterLease, failPolicy, enumerateUnlock, revokeDuringUnlock;
static int __cdecl readSharedSystemPower(SharedPowerV1* value, uint32_t) noexcept { *value=power; return 1; }
static void traceLog(const char*,...) {}
static auto currentPowerGeneration() { return power.generation; }
static bool hardwareWriteAllowed() {
    if (wrongAfterLease && g_hardwareWriteInFlight.load()!=0) {
        wrongAfterLease=false;
        ++power.suspendSerial;
    }
    return powerAdmitted(power);
}
#include "capture.inc"
#include "lease.inc"

static void ipc_on(const char* command, std::function<json(const json&)> fn) { handlers[command]=std::move(fn); }
static std::wstring U2W(const std::string& s) { return std::wstring(s.begin(),s.end()); }
static HRESULT fakeGuid(const wchar_t*,GUID*) { return S_OK; }
static LSTATUS fakeOpen(HKEY,LPCWSTR,DWORD,REGSAM,PHKEY result) {
    *result=reinterpret_cast<HKEY>(1); ++openKeys; return ERROR_SUCCESS;
}
static LSTATUS fakeCreate(HKEY,LPCWSTR,DWORD,LPWSTR,DWORD,REGSAM,LPSECURITY_ATTRIBUTES,PHKEY result,LPDWORD) {
    *result=reinterpret_cast<HKEY>(1); ++openKeys; return ERROR_SUCCESS;
}
static LSTATUS fakeClose(HKEY) { --openKeys; return ERROR_SUCCESS; }
static LSTATUS fakeEnum(HKEY,DWORD index,LPWSTR name,LPDWORD length,LPDWORD,LPWSTR,LPDWORD,PFILETIME) {
    if(!enumerateUnlock||index!=0) return ERROR_NO_MORE_ITEMS;
    name[0]=L'x'; name[1]=0; *length=1; return ERROR_SUCCESS;
}
static LSTATUS fakeQuery(HKEY,LPCWSTR,LPDWORD,LPDWORD type,LPBYTE data,LPDWORD) {
    if(revokeDuringUnlock) { revokeDuringUnlock=false; ++power.suspendSerial; }
    *type=REG_DWORD; const DWORD hidden=POWER_ATTRIBUTE_HIDE;
    std::memcpy(data,&hidden,sizeof(hidden)); return ERROR_SUCCESS;
}
template<class... A> static DWORD fakeWrite(A...) {
    ++writes;
    if(changeAfterWrite) { changeAfterWrite=false; ++power.observedNotificationSequence; }
    return ERROR_SUCCESS;
}
template<class... A> static DWORD fakeIndex(A... args) {
    if(changeBeforeFallback) { changeBeforeFallback=false; ++power.generation; }
    if(failPolicy) return ERROR_ACCESS_DENIED;
    return fakeWrite(args...);
}
template<class... A> static DWORD fakeReadAttributes(A...) { return 0; }

#define CLSIDFromString fakeGuid
#define RegOpenKeyExW fakeOpen
#define RegCreateKeyExW fakeCreate
#define RegCloseKey fakeClose
#define RegEnumKeyExW fakeEnum
#define RegQueryValueExW fakeQuery
#define RegSetValueExW fakeWrite
#define PowerReadSettingAttributes fakeReadAttributes
#define PowerWriteSettingAttributes fakeWrite
#define PowerWriteACValueIndex fakeIndex
#define PowerWriteDCValueIndex fakeIndex
#define PowerSetActiveScheme fakeWrite
static void registerProductionHandlers() {
#include "registry.inc"
#include "active.inc"
}

static int failures;
static void run(const char* id,const char* endpoint,json args,int expectedWrites,bool denied) {
    bool threw=false;
    try { handlers.at(endpoint)(args); } catch(const std::runtime_error&) { threw=true; }
    const bool ok=writes==expectedWrites&&threw==denied&&g_hardwareWriteInFlight==0&&openKeys==0;
    std::cout<<id<<' '<<(ok?"PASS":"FAIL")<<' '<<writes<<' '<<g_hardwareWriteInFlight<<'\n';
    failures+=!ok;
    power={}; power.generation=4; power.suspendSerial=2; power.observedNotificationSequence=8;
    power.publicationRevision=20; power.flags=kKnown|kHardwareAllowed;
    writes=0; changeAfterWrite=changeBeforeFallback=wrongAfterLease=failPolicy=enumerateUnlock=revokeDuringUnlock=false;
}
int main() {
    registerProductionHandlers();
    const char* registry="registry.writePowerBatch", *active="power.setActiveScheme";
    const char* scheme="1cb8b882-a900-4b9f-9bac-99d151e64441";
    const json stamp={4,2,8};
    json batch={{"scheme",scheme},{"subGroup","fixture"},{"valueName","ACSettingIndex"},
        {"entries",json::array({{{"setting","one"},{"value",1}},{{"setting","two"},{"value",2}}})},{"powerScope",stamp}};
    json activation={{"scheme",scheme},{"powerScope",stamp}};
    power.generation=5; power.suspendSerial=3; power.observedNotificationSequence=9; power.flags=kKnown|kHardwareAllowed;
    run("queued-registry-after-wake",registry,batch,0,true);
    ++power.generation;
    run("queued-activation-after-wake",active,activation,0,true);
    ++power.suspendSerial;
    run("same-generation-new-suspend",active,activation,0,true);
    ++power.observedNotificationSequence;
    run("same-suspend-new-notification",registry,batch,0,true);
    changeAfterWrite=true;
    run("revoke-between-entries",registry,batch,1,true);
    changeBeforeFallback=true; failPolicy=true;
    run("revoke-before-registry-fallback",registry,batch,0,true);
    wrongAfterLease=true;
    run("revoke-while-taking-lease",active,activation,0,true);
    auto invalid=activation; invalid["powerScope"]={4,-1,8};
    run("invalid-scope",active,invalid,0,true);
    power.flags|=kNotificationPending;
    run("pending-denied",registry,batch,0,true);
    ++power.publicationRevision;
    run("current-scope-registry",registry,batch,2,false);
    run("current-scope-activation",active,activation,1,false);
    failPolicy=true;
    run("current-scope-fallback",registry,batch,2,false);
    auto processor=batch; processor["subGroup"]="54533251-82be-4824-96c1-47b60b740d00";
    enumerateUnlock=revokeDuringUnlock=true;
    run("revoke-during-attribute-unlock",registry,processor,0,true);
    enumerateUnlock=true;
    run("current-scope-retries-unlock",registry,processor,3,false);
    activation.erase("powerScope"); batch.erase("powerScope");
    run("legacy-activation",active,activation,1,false);
    run("legacy-registry",registry,batch,2,false);
    return failures?1:0;
}
