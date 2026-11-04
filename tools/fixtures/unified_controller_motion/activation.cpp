// Controlled OS boundary for the actual native Lenovo activation owners.
// Production types/functions and library headers are frozen by activation.mjs.
#include <windows.h>
#include <hidusage.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>
#include "deps/json/json.hpp"
using json=nlohmann::json;
static int checks=0,failures=0;
#define CHECK(x) do{++checks;if(!(x)){++failures;std::printf("FAIL line=%d %s\n",__LINE__,#x);}}while(0)
static std::uint64_t clockMs=100;
enum class IoMode{Sync,Pending,Partial,Failed};
static IoMode mode=IoMode::Sync;
static bool completePending=false;
static unsigned closeFailures=0,duplicates=0,cancels=0;
static std::uintptr_t nextHandle=100;
struct Packet{HANDLE source;std::vector<BYTE> bytes;};
struct Pending{HANDLE handle;const BYTE* buffer;std::vector<BYTE> bytes;bool terminal=false;DWORD completedBytes=0;};
static std::vector<Packet> writes;
static std::map<HANDLE,HANDLE> outputs;
static std::set<HANDLE> events;
static std::map<LPOVERLAPPED,Pending> operations;
static BOOL fakeDuplicate(HANDLE,HANDLE source,HANDLE,HANDLE* target,DWORD,BOOL,DWORD){
    if(source!=reinterpret_cast<HANDLE>(7)&&source!=reinterpret_cast<HANDLE>(17)){SetLastError(ERROR_INVALID_HANDLE);return FALSE;}
    *target=reinterpret_cast<HANDLE>(++nextHandle);outputs[*target]=source;++duplicates;return TRUE;
}
static HANDLE fakeEvent(LPSECURITY_ATTRIBUTES,BOOL,BOOL,LPCWSTR){auto h=reinterpret_cast<HANDLE>(++nextHandle);events.insert(h);return h;}
static BOOL fakeReset(HANDLE h){CHECK(events.count(h)==1);return TRUE;}
static BOOL fakeClose(HANDLE h){
    for(const auto& row:operations)CHECK(row.second.handle!=h);
    if(closeFailures){--closeFailures;SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
    if(outputs.erase(h)||events.erase(h))return TRUE;
    CHECK(false);SetLastError(ERROR_INVALID_HANDLE);return FALSE;
}
static BOOL fakeWrite(HANDLE h,LPCVOID buffer,DWORD size,LPDWORD count,LPOVERLAPPED operation){
    CHECK(outputs.count(h)==1);CHECK(events.count(operation->hEvent)==1);
    const auto source=outputs[h];const DWORD expected=source==reinterpret_cast<HANDLE>(17)?65:64;
    CHECK(size==expected);if(size!=expected){SetLastError(ERROR_INVALID_PARAMETER);return FALSE;}
    const auto* bytes=static_cast<const BYTE*>(buffer);writes.push_back({source,{bytes,bytes+size}});
    if(mode==IoMode::Failed){SetLastError(ERROR_WRITE_FAULT);return FALSE;}
    CHECK(count==nullptr);
    const auto completed=mode==IoMode::Partial?size-1:size;
    operations[operation]={h,bytes,{bytes,bytes+size},mode!=IoMode::Pending,completed};
    if(mode==IoMode::Pending){operation->Internal=0x103;SetLastError(ERROR_IO_PENDING);return FALSE;}
    operation->Internal=0;return TRUE;
}
static BOOL fakeResult(HANDLE h,LPOVERLAPPED operation,LPDWORD count,BOOL){
    const auto row=operations.find(operation);CHECK(row!=operations.end());if(row==operations.end()){SetLastError(ERROR_INVALID_PARAMETER);return FALSE;}
    CHECK(row->second.handle==h);CHECK(std::memcmp(row->second.buffer,row->second.bytes.data(),row->second.bytes.size())==0);
    if(!completePending&&!row->second.terminal){SetLastError(ERROR_IO_INCOMPLETE);return FALSE;}
    *count=row->second.completedBytes;operation->Internal=0;operations.erase(row);return TRUE;
}
static BOOL fakeCancel(HANDLE h,LPOVERLAPPED operation){
    CHECK(operations.count(operation)==1);if(operations.count(operation))CHECK(operations.at(operation).handle==h);
    ++cancels;return TRUE;
}
static ULONGLONG fakeTick(){return clockMs;}
#define DuplicateHandle fakeDuplicate
#define CreateEventW fakeEvent
#define ResetEvent fakeReset
#define CloseHandle fakeClose
#define WriteFile fakeWrite
#define GetOverlappedResult fakeResult
#define CancelIoEx fakeCancel
#define GetTickCount64 fakeTick
#include "native/legion_motion_activation_win32.h"
#include "native/controller_motion_processor.h"
#include "capabilities/HcControllerMotionCapabilityCatalog.h"
#include "production-types.h"
enum class YmccFamily{Unknown,LenovoLegionGo};
static struct{YmccFamily family=YmccFamily::LenovoLegionGo;std::string deviceClass;}g_machineIdentity;
static std::mutex g_hidHidePhysicalIdentityMx,g_controllerCalibrationMx;
static std::optional<ControllerCalibrationRequest>g_controllerCalibrationRequest;
static std::atomic<unsigned>g_legionMotionSensorIndex{0};
static HidHidePhysicalIdentity g_hidHidePhysicalIdentity;
static std::atomic<bool> g_inputCaptureStop{false},g_legionRawInputSubscribed{true},g_controllerCalibrationHasRequest{false};
static std::atomic<std::uint64_t>g_hidResolveCacheGeneration{2},g_legionMotionCaptureEpoch{3};
static bool g_inputCaptureReady=true;
static struct{bool motionActionEnabled=false;}g_realStickTest;
enum class PowerLifecycle{Ready,Suspending};
static std::atomic<PowerLifecycle>g_powerLifecycle{PowerLifecycle::Ready};
static std::uint64_t powerGeneration=1;
static std::uint64_t currentPowerGeneration(){return powerGeneration;}
static auto& g_legionBack=*new LegionBackSession;
static auto& g_legionSBack=*new LegionSBackSession;
static std::vector<json> lifecycle;
static void appendNativeLifecycleLog(const char*,const json& row){lifecycle.push_back(row);}
#include "production-functions.h"
static void pump(unsigned count=1){for(unsigned i=0;i<count;++i){++clockMs;legionMotionActivationTick(g_legionBack);legionMotionActivationTick(g_legionSBack);}}
static void newContext(LegionBackSession& owner){++powerGeneration;++g_hidResolveCacheGeneration;++g_legionMotionCaptureEpoch;++owner.actionGeneration;}
static void drainAll(){
    completePending=true;legionMotionActivationStop(g_legionBack);legionMotionActivationStop(g_legionSBack);
    CHECK(operations.empty());CHECK(outputs.empty());CHECK(events.empty());mode=IoMode::Sync;completePending=false;
}
static void configure(const char* model,bool goS,unsigned ordinal){
    drainAll();g_machineIdentity.family=YmccFamily::LenovoLegionGo;g_machineIdentity.deviceClass=std::string("HandheldCompanion.Devices.")+model;
    auto& active=goS?static_cast<LegionBackSession&>(g_legionSBack):g_legionBack;
    auto& other=goS?g_legionBack:static_cast<LegionBackSession&>(g_legionSBack);
    other.enabled=other.bound=false;active.enabled=active.bound=true;active.closing=false;active.device=reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(goS?17:7));
    active.buf.assign(goS?65:64,0);active.motionOutputBytes=goS?65:64;active.actionGeneration=100+ordinal;newContext(active);
    g_hidHidePhysicalIdentity={};g_hidHidePhysicalIdentity.resolved=true;g_hidHidePhysicalIdentity.vendorId=goS?0x1A86:0x17EF;
    g_hidHidePhysicalIdentity.productId=goS?0xE310:0x61EB;g_hidHidePhysicalIdentity.usagePage=0xFFA0;g_hidHidePhysicalIdentity.usage=1;
    g_hidHidePhysicalIdentity.baseContainerDeviceInstanceId=goS?"USB\\VID_1A86&PID_E310\\ONE":"USB\\VID_17EF&PID_61EB\\ONE";
    active.motionPhysicalIdentity=legionMotionStableIdentity(g_hidHidePhysicalIdentity);
    g_inputCaptureReady=true;g_inputCaptureStop.store(false);g_legionRawInputSubscribed.store(true);g_powerLifecycle.store(PowerLifecycle::Ready);
    g_realStickTest.motionActionEnabled=false;g_controllerCalibrationHasRequest.store(false);g_controllerCalibrationRequest.reset();g_legionMotionSensorIndex.store(0);writes.clear();lifecycle.clear();
}
static void verifyPackets(std::size_t start,bool goS){
    const std::vector<std::vector<BYTE>> expected=goS?std::vector<std::vector<BYTE>>{{0,4,7,1},{0,4,5,1}}:
        std::vector<std::vector<BYTE>>{{5,6,106,2,3,1,1},{5,6,106,7,3,2,1},{5,6,106,2,4,1,1},{5,6,106,7,4,2,1}};
    CHECK(writes.size()==start+expected.size());
    if(writes.size()!=start+expected.size())return;
    for(unsigned i=0;i<expected.size();++i){const auto& packet=writes[start+i];CHECK(packet.source==reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(goS?17:7)));CHECK(packet.bytes.size()==(goS?65u:64u));
        for(unsigned j=0;j<packet.bytes.size();++j)CHECK(packet.bytes[j]==(j<expected[i].size()?expected[i][j]:0));}
}
static void runModel(const char* model,bool goS,unsigned ordinal){
    configure(model,goS,ordinal);auto& owner=goS?static_cast<LegionBackSession&>(g_legionSBack):g_legionBack;
    const unsigned commandCount=goS?2:4;const auto startChecks=checks,startFailures=failures;
    const auto binding=legionMotionModelBinding();CHECK(binding.token()==ordinal);CHECK(binding.bankCount()==(goS?1u:2u));CHECK(binding.activationSupported());
    CHECK(binding.activationCommands().count==commandCount);CHECK(binding.hardwareAdmitted(goS?0x1A86:0x17EF,goS?0xE311:0x6182,legionBackIsPid,legionSBackIsPid));
    CHECK(!binding.hardwareAdmitted(goS?0x17EF:0x1A86,goS?0xE311:0x6182,legionBackIsPid,legionSBackIsPid));
    pump(20);CHECK(writes.empty());
    g_controllerCalibrationHasRequest.store(true);pump(3);CHECK(writes.empty()); // An orphan flag grants no device write.
    const ymcc::controller_motion::Key calibrationKey{owner.motionPhysicalIdentity,powerGeneration,g_hidResolveCacheGeneration.load(),g_legionMotionCaptureEpoch.load(),0};
    g_controllerCalibrationRequest=ControllerCalibrationRequest{calibrationKey,11,clockMs,true};
    g_controllerCalibrationRequest->key.physicalIdentity="retired-request-source";pump(3);CHECK(writes.empty());
    g_controllerCalibrationRequest->key=calibrationKey;pump(8);verifyPackets(0,goS);
    CHECK(owner.activation.inspect().phase==ymcc::legion_activation::Phase::Complete);CHECK(!owner.activationOutstanding.load());CHECK(outputs.empty()&&events.empty());
    g_controllerCalibrationHasRequest.store(false);g_controllerCalibrationRequest.reset();g_realStickTest.motionActionEnabled=true;pump(50);CHECK(writes.size()==commandCount);
    const auto cycleWrites=writes.size();g_realStickTest.motionActionEnabled=false;pump(3);g_realStickTest.motionActionEnabled=true;pump(8);verifyPackets(cycleWrites,goS);
    const auto stableWrites=writes.size();
    newContext(owner);const auto expectedIdentity=owner.motionPhysicalIdentity;owner.motionPhysicalIdentity="wrong-container";pump(3);CHECK(writes.size()==stableWrites);owner.motionPhysicalIdentity=expectedIdentity;
    const auto pid=g_hidHidePhysicalIdentity.productId;g_hidHidePhysicalIdentity.productId=0x9999;pump(3);CHECK(writes.size()==stableWrites);g_hidHidePhysicalIdentity.productId=pid;
    g_powerLifecycle.store(PowerLifecycle::Suspending);pump(3);CHECK(writes.size()==stableWrites);g_powerLifecycle.store(PowerLifecycle::Ready);pump(8);verifyPackets(stableWrites,goS);
    newContext(owner);mode=IoMode::Pending;pump();const auto pendingWrites=writes.size();const auto cancelBefore=cancels;
    ++g_legionMotionCaptureEpoch;pump();CHECK(cancels==cancelBefore+1);CHECK(writes.size()==pendingWrites);CHECK(owner.activationOutstanding.load());CHECK(!operations.empty());
    completePending=true;pump();CHECK(writes.size()==pendingWrites);CHECK(!owner.activationOutstanding.load());mode=IoMode::Sync;completePending=false;const auto recoveryStart=writes.size();pump(8);verifyPackets(recoveryStart,goS);
    newContext(owner);mode=IoMode::Pending;pump();const auto offWrites=writes.size();g_realStickTest.motionActionEnabled=false;pump(3);CHECK(writes.size()==offWrites);CHECK(owner.activationOutstanding.load());
    completePending=true;pump();CHECK(!owner.activationOutstanding.load());pump(10);CHECK(writes.size()==offWrites);mode=IoMode::Sync;completePending=false;
    g_realStickTest.motionActionEnabled=true;newContext(owner);mode=IoMode::Pending;pump();const auto timeoutWrites=writes.size();const auto timeoutCancels=cancels;clockMs+=750;pump();CHECK(cancels==timeoutCancels+1);CHECK(owner.activationOutstanding.load());
    completePending=true;pump();CHECK(owner.activation.inspect().phase==ymcc::legion_activation::Phase::Failed);pump(20);CHECK(writes.size()==timeoutWrites);mode=IoMode::Sync;completePending=false;
    const auto failedRetryWrites=writes.size();g_realStickTest.motionActionEnabled=false;pump(3);g_realStickTest.motionActionEnabled=true;pump(8);verifyPackets(failedRetryWrites,goS);
    newContext(owner);mode=IoMode::Partial;const auto partialWrites=writes.size();pump(10);CHECK(writes.size()==partialWrites+1);CHECK(owner.activation.port.error==ERROR_WRITE_FAULT);CHECK(!owner.activationOutstanding.load());
    mode=IoMode::Sync;newContext(owner);owner.motionOutputBytes=goS?64:6;const auto shapeWrites=writes.size();pump(10);CHECK(writes.size()==shapeWrites);CHECK(owner.activation.inspect().phase==ymcc::legion_activation::Phase::Failed);
    owner.motionOutputBytes=goS?65:64;if(goS){newContext(owner);owner.buf.resize(64);pump(10);CHECK(writes.size()==shapeWrites);owner.buf.resize(65);}
    newContext(owner);const auto closeWrites=writes.size();pump(commandCount-1);closeFailures=1;pump();CHECK(owner.activationOutstanding.load());pump();CHECK(!owner.activationOutstanding.load());pump(10);verifyPackets(closeWrites,goS);
    newContext(owner);mode=IoMode::Pending;pump();const auto stopWrites=writes.size();owner.enabled=false;legionMotionActivationStop(owner);CHECK(owner.activationOutstanding.load());pump(5);CHECK(writes.size()==stopWrites);
    completePending=true;pump();CHECK(!owner.activationOutstanding.load());CHECK(outputs.empty()&&events.empty()&&operations.empty());mode=IoMode::Sync;completePending=false;
    bool receipt=false;for(const auto& row:lifecycle)if(row.at("commandCount")==commandCount&&row.at("capabilityToken")==ordinal)receipt=true;CHECK(receipt);
    std::printf("CASE family-%s checks=%d failures=%d\n",model,checks-startChecks,failures-startFailures);
}
int main(){
    runModel("LegionGoTablet",false,1);runModel("LegionGoTablet2",false,2);runModel("LegionGoSZ1",true,3);runModel("LegionGoSZ2",true,4);
    drainAll();std::printf("LEGION_ACTIVATION_COMPLETED models=4 checks=%d failures=%d writes=%zu physicalHardware=false\n",checks,failures,writes.size());return failures?1:0;
}
