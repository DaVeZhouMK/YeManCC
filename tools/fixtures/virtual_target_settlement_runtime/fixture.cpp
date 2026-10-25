#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "json.hpp"
#include "input_host_transaction_mutex.h"
#include "virtual_start_protection.h"
#include "ps_endpoint_ownership.h"
namespace fileTrace {
inline unsigned attempts=0,commits=0,failures=0;inline DWORD lastError=0;
static BOOL replace(LPCWSTR from,LPCWSTR to,DWORD flags){++attempts;const auto ok=::MoveFileExW(from,to,flags);const DWORD error=ok?ERROR_SUCCESS:GetLastError();if(ok)++commits;else {++failures;lastError=error;}SetLastError(error);return ok;}
static void reset(){attempts=commits=failures=0;lastError=0;}
}
// Actual store API is observed, never replaced with a fake commit result.
#define MoveFileExW fileTrace::replace
#include "virtual_target_return_session.h"
#undef MoveFileExW
#include "virtual_target_return_process.h"
using json=nlohmann::json;namespace duty=ymcc::targetReturn;
using InputHostMutex=ymcc::inputHost::TransactionMutex<>;static InputHostMutex g_inputHostMx;
static HANDLE fixtureHostEvent=nullptr,g_inputHostProcess=nullptr;
static const duty::ProcessIdentity fixtureHostIdentity{700148,"132000000000000148"};
static std::string g_inputHostHostInstanceId,g_inputHostRunId,fixturePersona;
static std::uint64_t g_inputHostEpoch=0,g_inputHostSwitchRecoveryStartDeadline=0,g_inputHostSwitchRecoveryStartRequestId=0,g_inputHostSwitchRecoveryStartPower=0;
static const char* kInputHostTargetId="target148";static ymcc::VirtualStartProtection g_virtualStartProtection;
static bool fixtureFence=false;static unsigned actualParentCaptures=0,modeledHostCaptures=0;
static std::wstring fixtureRoot,fixtureRecordPath;static json logs=json::array();
static std::string W2U(const std::wstring& input){std::string result;for(const auto c:input){if(unsigned(c)>127)throw std::runtime_error("private fixture ASCII-only path");result.push_back(char(c));}return result;}
static std::wstring inputHostTargetReturnRecordPath(){return fixtureRecordPath;}
static std::string inputHostTargetReturnMachineBinding(){return "windows-machine-guid:private148";}
static std::uint64_t inputHostStartProtectionNow(){return GetTickCount64();}
static std::uint64_t currentPowerGeneration(){return 1;}
static bool inputHostExitFenceRaised(){return fixtureFence;}
static std::string inputHostBoundOrCurrentPersona(){return fixturePersona;}
static void appendNativeLifecycleLog(const char* name,json row){logs.push_back({{"name",name},{"row",std::move(row)}});}
static std::optional<duty::ProcessIdentity> inputHostTargetReturnCaptureOwner(HANDLE process){
    if(process==GetCurrentProcess()){++actualParentCaptures;return duty::process::capture(process);}
    if(process==fixtureHostEvent&&process){++modeledHostCaptures;return fixtureHostIdentity;}
    throw std::runtime_error("foreign process capture forbidden");
}
// The extracted consumers may not reach an unbound external operation.
#define DeviceIoControl forbidden_DeviceIoControl
#define CreateProcessW forbidden_CreateProcessW
#define OpenProcess forbidden_OpenProcess
#define SetupDiGetClassDevsW forbidden_SetupDiGetClassDevsW
#define CM_Get_DevNode_Status forbidden_CM_Get_DevNode_Status
#define RegOpenKeyExW forbidden_RegOpenKeyExW
#include "actual-runtime.inc"
namespace rt=ymcc::inputHost::targetReturnRuntime;
namespace {
json cases=json::array(),failures=json::array(),observation;unsigned assertions=0,caseSequence=0;std::string active;
void need(bool ok,const char* reason){if(!ok)throw std::runtime_error(reason);}
void check(bool ok,const char* assertion){++assertions;if(!ok)failures.push_back({{"case",active},{"assertion",assertion}});}
std::string read(const std::wstring& path){std::ifstream file(std::filesystem::path(path),std::ios::binary);need(bool(file),"read only own private file");return {std::istreambuf_iterator<char>(file),{}};}
std::string encoded(const duty::Record& record){std::string text;need(duty::encode(record,text),"encode actual record");return text;}
json originJson(const duty::Origin& o){return {{"id",o.transactionId},{"parent",{{"pid",o.parent.pid},{"birth",o.parent.creationTime}}},{"host",{{"pid",o.host.pid},{"birth",o.host.creationTime}}},{"hostInstanceId",o.hostInstanceId},{"runId",o.runId},{"epoch",o.epoch},{"targetId",o.targetId},{"persona",o.persona},{"physicalRoot",o.physicalRoot},{"cliWriterProtocol",o.cliWriterProtocol}};}
json localState(){json targets=json::array(),proven=json::array();for(const auto& entry:rt::state.targets)targets.push_back({{"origin",originJson(entry.origin)},{"sequence",entry.sequence}});for(const auto& o:rt::state.proven)proven.push_back(originJson(o));return {{"targets",targets},{"proven",proven},{"serial",rt::state.serial}};}
void reset(const std::string& persona="dualsense-edge"){
    rt::state=rt::State{};if(fixtureHostEvent)CloseHandle(fixtureHostEvent);fixtureHostEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);need(fixtureHostEvent!=nullptr,"own private event handle");g_inputHostProcess=fixtureHostEvent;g_inputHostHostInstanceId="host-instance148";g_inputHostRunId="run148";g_inputHostEpoch=148;fixturePersona=persona;fixtureFence=false;g_inputHostSwitchRecoveryStartDeadline=g_inputHostSwitchRecoveryStartRequestId=g_inputHostSwitchRecoveryStartPower=0;
    logs=json::array();actualParentCaptures=modeledHostCaptures=0;const auto dir=fixtureRoot+L"\\c"+std::to_wstring(++caseSequence);need(CreateDirectoryW(dir.c_str(),nullptr)!=FALSE,"fresh private case directory");fixtureRecordPath=dir+L"\\record.json";fileTrace::reset();
}
void finish(){rt::state=rt::State{};if(fixtureHostEvent)CloseHandle(fixtureHostEvent);fixtureHostEvent=nullptr;g_inputHostProcess=nullptr;}
duty::Origin reserve(std::uint64_t sequence,const char* command="PREPARE_TARGET"){
    need(inputHostTargetReturnBeforeCreateLocked(command,sequence),"actual before-create target reservation succeeds");const auto found=std::find_if(rt::state.targets.begin(),rt::state.targets.end(),[&](const auto& entry){return entry.sequence==sequence;});need(found!=rt::state.targets.end(),"actual reservation in State.targets");return found->origin;
}
duty::Origin history(std::string id="historical148",bool currentFlow=true){const auto owner=rt::currentOwner();need(bool(owner),"real current parent and modeled Host capture");auto old=*owner;old.transactionId=std::move(id);if(!currentFlow){old.runId="old-run148";old.parent.creationTime="132000000000000999";}need(rt::state.session->reserveTarget(rt::state.session->record()->revision,old,{true,true}),"actual Session historical seed");return old;}
void addLocal(const duty::Origin& origin,std::uint64_t sequence){need(rt::state.session->reserveTarget(rt::state.session->record()->revision,origin,{true,true}),"actual Session local origin seed");rt::state.targets.push_back({origin,sequence});}
void addPin(const duty::Origin& o){need(rt::state.session->attachAcceptedPsPin(rt::state.session->record()->revision,o,{"SWD\\HIDMAESTRO_VID_054C_PID_0DF2\\ROOT148",{"HID\\VID_054C&PID_0DF2\\HID148"},5}),"actual accepted pin stored");}
ymcc::psEndpointOwnership::Retirement retirement(const duty::Origin& o){ymcc::psEndpointOwnership::Retirement r;r.required=true;r.owner={o.hostInstanceId,o.runId,o.targetId,o.persona,o.epoch};r.target.capable=true;r.target.valid=true;r.target.rootInstanceId="SWD\\HIDMAESTRO_VID_054C_PID_0DF2\\ROOT148";r.target.hidInstanceIds={"HID\\VID_054C&PID_0DF2\\HID148"};r.target.generation=5;return r;}
json receipt(std::uint64_t sequence=20){return {{"ok",true},{"command","RECREATE_TARGET"},{"status","target-prepared"},{"hostInstanceId",g_inputHostHostInstanceId},{"runId",g_inputHostRunId},{"epoch",g_inputHostEpoch},{"targetId",kInputHostTargetId},{"persona",fixturePersona},{"sequence",sequence}};}
struct DenyReplace {HANDLE h=INVALID_HANDLE_VALUE;explicit DenyReplace(const std::wstring& path){h=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);need(h!=INVALID_HANDLE_VALUE,"held private file blocks actual atomic replace");}~DenyReplace(){if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);}};
struct Snapshot {std::string memory,disk;json state;unsigned attempts;Snapshot():memory(encoded(*rt::state.session->record())),disk(read(fixtureRecordPath)),state(localState()),attempts(fileTrace::attempts){}};
void unchanged(const Snapshot& before,bool targets=true){check(encoded(*rt::state.session->record())==before.memory,"rejected guard or commit keeps exact in-memory duties");check(read(fixtureRecordPath)==before.disk,"rejected guard or commit keeps original disk record");if(targets)check(localState()==before.state,"rejected guard leaves current runtime targets and proven unchanged");}
void zeroConsume(const Snapshot& before){unchanged(before);check(fileTrace::attempts==before.attempts,"rejected guard performs zero consume/store attempts");}
void observe(){observation["state"]=localState();observation["disk"]=rt::state.session&&rt::state.session->record()?json::parse(encoded(*rt::state.session->record())):json(nullptr);observation["store"]={{"attempts",fileTrace::attempts},{"commits",fileTrace::commits},{"failures",fileTrace::failures},{"lastError",fileTrace::lastError}};observation["notes"]=logs;observation["actualCurrentParentCaptures"]=actualParentCaptures;observation["modeledOwnedHostCaptures"]=modeledHostCaptures;observation["recordFile"]=W2U(fixtureRecordPath);}
void run(const std::string& name,const std::function<void()>& test){active=name;observation=json::object();const auto before=assertions;const auto bad=failures.size();try{reset();test();observe();}catch(const std::exception& e){check(false,(std::string("fixture-exception:")+e.what()).c_str());}observation["name"]=name;observation["assertions"]=assertions-before;observation["failures"]=failures.size()-bad;cases.push_back(std::move(observation));finish();}
void psReject(const std::string& name,const std::function<void(duty::Origin&,ymcc::psEndpointOwnership::Retirement&,bool&)>& change,bool locked=true){run(name,[&]{std::unique_lock<InputHostMutex> lock(g_inputHostMx,std::defer_lock);if(locked)lock.lock();auto old=reserve(10);addPin(old);auto retired=retirement(old);g_inputHostProcess=nullptr;bool absence=true;change(old,retired,absence);Snapshot before;const bool result=inputHostTargetReturnAfterPsQuarantineLocked(retired,absence);check(fileTrace::attempts==before.attempts,"PS invalid proof never reaches consumeOrigin");zeroConsume(before);observation["entryResult"]=result;});}
void psTests(){
    psReject("PS/absence-false",[](auto&,auto&,bool& absent){absent=false;});
    psReject("PS/live-host",[](auto&,auto&,bool&){g_inputHostProcess=fixtureHostEvent;});
    psReject("PS/lifecycle-not-owned",[](auto&,auto&,bool&){},false);
    psReject("PS/retirement-not-required",[](auto&,auto& retired,bool&){retired.required=false;});
    psReject("PS/wrong-run",[](auto&,auto& r,bool&){r.owner.runId="foreign-run";});
    psReject("PS/wrong-epoch",[](auto&,auto& r,bool&){++r.owner.epoch;});
    psReject("PS/zero-epoch",[](auto&,auto& r,bool&){r.owner.epoch=0;});
    psReject("PS/wrong-persona",[](auto&,auto& r,bool&){r.owner.persona="steamdeck";});
    psReject("PS/wrong-hostInstanceId",[](auto&,auto& r,bool&){r.owner.hostInstanceId="foreign-host";});
    psReject("PS/wrong-target",[](auto&,auto& r,bool&){r.owner.targetId="foreign-target";});
    psReject("PS/wrong-parent-pid",[](auto&,auto&,bool&){++rt::state.targets[0].origin.parent.pid;});
    psReject("PS/wrong-parent-birth",[](auto&,auto&,bool&){rt::state.targets[0].origin.parent.creationTime="132000000000000777";});
    psReject("PS/wrong-pin-root",[](auto&,auto& r,bool&){r.target.rootInstanceId="SWD\\HIDMAESTRO_VID_054C_PID_0DF2\\OTHER";});
    psReject("PS/wrong-pin-HID",[](auto&,auto& r,bool&){r.target.hidInstanceIds={"HID\\VID_054C&PID_0DF2\\OTHER"};});
    psReject("PS/wrong-pin-generation",[](auto&,auto& r,bool&){++r.target.generation;});
    run("PS/mixed-physical-path-rejected",[]{std::lock_guard<InputHostMutex> lock(g_inputHostMx);need(rt::open(),"actual Session open");auto o=*rt::currentOwner();o.transactionId="mixed148";o.physicalRoot="USB\\VID_1234&PID_5678\\ROOT148";need(rt::state.session->reservePath(rt::state.session->record()->revision,o,"HID\\VID_1234&PID_5678\\OWNED148",{true,true,true}),"mixed actual path seed");rt::state.targets.push_back({o,10});auto retired=retirement(o);g_inputHostProcess=nullptr;Snapshot before;check(!inputHostTargetReturnAfterPsQuarantineLocked(retired,true),"PS target settlement refuses mixed physical duty");zeroConsume(before);});
    run("PS/exact-current-target-history-preserved",[]{std::lock_guard<InputHostMutex> lock(g_inputHostMx);const auto exact=reserve(10);addPin(exact);const auto old=history();const auto different=history("different-history148",false);const auto retired=retirement(exact);g_inputHostProcess=nullptr;const auto revision=rt::state.session->record()->revision;check(inputHostTargetReturnAfterPsQuarantineLocked(retired,true),"exact current PS target-only proof commits");check(!rt::contains(exact)&&rt::contains(old)&&rt::contains(different),"only current State.targets origin consumed; historical origins retained");check(rt::state.targets.empty()&&rt::state.proven.empty(),"success prunes exact current runtime responsibility");check(rt::state.session->record()->revision==revision+1,"exact PS settlement performs one real committed consume");check(actualParentCaptures>0,"current parent PID and birth captured from actual current process");});
    run("PS/no-local-target-no-historical-consume",[]{std::lock_guard<InputHostMutex> lock(g_inputHostMx);const auto exact=reserve(10);addPin(exact);auto retired=retirement(exact);rt::state.targets.clear();g_inputHostProcess=nullptr;Snapshot before;(void)inputHostTargetReturnAfterPsQuarantineLocked(retired,true);zeroConsume(before);});
    run("PS/unpinned-target-only-valid-proof",[]{std::lock_guard<InputHostMutex> lock(g_inputHostMx);const auto exact=reserve(10);const auto retired=retirement(exact);g_inputHostProcess=nullptr;check(inputHostTargetReturnAfterPsQuarantineLocked(retired,true)&&!rt::contains(exact),"target-only no-pin debt consumes only by supplied exact qualified absence");});
    run("PS/actual-commit-failure-retry-original-entry",[]{std::lock_guard<InputHostMutex> lock(g_inputHostMx);const auto exact=reserve(10);addPin(exact);const auto old=history();const auto retired=retirement(exact);g_inputHostProcess=nullptr;Snapshot before;{DenyReplace held(fixtureRecordPath);check(!inputHostTargetReturnAfterPsQuarantineLocked(retired,true),"PS actual atomic replacement failure must return false");unchanged(before);check(rt::contains(exact)&&rt::state.targets.size()==1,"PS failed commit retains exact runtime and durable duty");check(fileTrace::failures==1&&rt::state.lastError=="atomic-replace-failed","PS real file fault reaches actual store error");}check(inputHostTargetReturnAfterPsQuarantineLocked(retired,true),"PS original entry retries after private file blocker released");check(!rt::contains(exact)&&rt::contains(old)&&rt::state.targets.empty(),"PS retry settles current only and preserves history");});
}
struct Flow {duty::Origin older,replacement,newer,historical,foreign;};
Flow flow(){Flow f;f.older=reserve(10);f.replacement=reserve(20,"RECREATE_TARGET");f.newer=reserve(30,"RECREATE_TARGET");f.historical=history();f.foreign=f.older;f.foreign.transactionId="foreign-local148";f.foreign.runId="foreign-run148";addLocal(f.foreign,5);return f;}
void receiptReject(const std::string& name,const std::function<void(json&,Flow&)>& change,bool locked=true){run(name,[&]{std::unique_lock<InputHostMutex> lock(g_inputHostMx,std::defer_lock);if(locked)lock.lock();auto f=flow();auto r=receipt();change(r,f);const bool replacementWasPresent=rt::contains(f.replacement);Snapshot before;inputHostTargetReturnAcceptedRecreateLocked(r);zeroConsume(before);check(rt::contains(f.older)&&rt::contains(f.replacement)==replacementWasPresent,"negative lost foreign or malformed RECREATE preserves old and new duties");});}
void recreateTests(){
    receiptReject("recreate/negative-ACK",[](auto& r,auto&){r["ok"]=false;});
    receiptReject("recreate/lost-ACK-empty",[](auto& r,auto&){r=json::object();});
    receiptReject("recreate/null-receipt",[](auto& r,auto&){r=nullptr;});
    receiptReject("recreate/array-receipt",[](auto& r,auto&){r=json::array();});
    receiptReject("recreate/wrong-command",[](auto& r,auto&){r["command"]="PREPARE_TARGET";});
    receiptReject("recreate/unknown-status",[](auto& r,auto&){r["status"]="UNKNOWN";});
    receiptReject("recreate/missing-status",[](auto& r,auto&){r.erase("status");});
    receiptReject("recreate/wrong-run",[](auto& r,auto&){r["runId"]="foreign-run";});
    receiptReject("recreate/wrong-epoch",[](auto& r,auto&){r["epoch"]=149;});
    receiptReject("recreate/wrong-persona",[](auto& r,auto&){r["persona"]="steamdeck";});
    receiptReject("recreate/wrong-hostInstanceId",[](auto& r,auto&){r["hostInstanceId"]="foreign-host";});
    receiptReject("recreate/wrong-target",[](auto& r,auto&){r["targetId"]="foreign-target";});
    receiptReject("recreate/wrong-parent",[](auto&,auto&){for(auto& entry:rt::state.targets)entry.origin.parent.creationTime="132000000000000555";});
    receiptReject("recreate/wrong-Host-birth",[](auto&,auto&){for(auto& entry:rt::state.targets)entry.origin.host.creationTime="132000000000000555";});
    receiptReject("recreate/unreserved-sequence",[](auto& r,auto&){r["sequence"]=25;});
    receiptReject("recreate/new-reservation-absent-from-disk",[](auto&,auto& f){need(rt::state.session->consumeOrigin(rt::state.session->record()->revision,f.replacement),"private setup remove new durable reservation");});
    receiptReject("recreate/sequence-zero",[](auto& r,auto&){r["sequence"]=0;});
    receiptReject("recreate/sequence-negative",[](auto& r,auto&){r["sequence"]=-1;});
    receiptReject("recreate/sequence-string",[](auto& r,auto&){r["sequence"]="20";});
    receiptReject("recreate/sequence-bool",[](auto& r,auto&){r["sequence"]=true;});
    receiptReject("recreate/sequence-float",[](auto& r,auto&){r["sequence"]=20.0;});
    receiptReject("recreate/sequence-null",[](auto& r,auto&){r["sequence"]=nullptr;});
    receiptReject("recreate/sequence-missing",[](auto& r,auto&){r.erase("sequence");});
    receiptReject("recreate/ok-malformed",[](auto& r,auto&){r["ok"]="true";});
    receiptReject("recreate/tuple-malformed",[](auto& r,auto&){r["epoch"]=json::object();});
    receiptReject("recreate/lifecycle-not-owned",[](auto&,auto&){},false);
    run("recreate/exact-older-only-retain-newer-history",[]{std::lock_guard<InputHostMutex> lock(g_inputHostMx);const auto f=flow();inputHostTargetReturnAcceptedRecreateLocked(receipt());check(!rt::contains(f.older),"exact accepted replacement settles strictly older current-flow duty");check(rt::contains(f.replacement)&&rt::contains(f.newer),"accepted sequence and newer reservations must remain durable");check(rt::contains(f.historical)&&rt::contains(f.foreign),"historical and foreign flow duties cannot be consumed by RECREATE ACK");check(rt::state.proven.empty()&&rt::state.targets.size()==3,"committed older target removed, remaining runtime state preserved");check(modeledHostCaptures>0&&actualParentCaptures>0,"tuple capture uses real parent plus explicitly modeled private Host");});
    run("recreate/same-sequence-no-older-no-consume",[]{std::lock_guard<InputHostMutex> lock(g_inputHostMx);const auto exact=reserve(20,"RECREATE_TARGET");Snapshot before;inputHostTargetReturnAcceptedRecreateLocked(receipt());zeroConsume(before);check(rt::contains(exact),"same accepted reservation never deleted as older");});
    run("recreate/mixed-older-preserved",[]{std::lock_guard<InputHostMutex> lock(g_inputHostMx);need(rt::open(),"open real Session");auto mixed=*rt::currentOwner();mixed.transactionId="mixed-old148";mixed.physicalRoot="USB\\VID_1234&PID_5678\\ROOT148";need(rt::state.session->reservePath(rt::state.session->record()->revision,mixed,"HID\\VID_1234&PID_5678\\OWNED148",{true,true,true}),"mixed old path seed");rt::state.targets.push_back({mixed,10});const auto replacement=reserve(20,"RECREATE_TARGET");inputHostTargetReturnAcceptedRecreateLocked(receipt());check(rt::contains(mixed)&&rt::contains(replacement),"target replacement does not consume mixed physical origin");check(rt::state.session->record()->obligations[0].paths.size()==1,"physical debt remains for exact physical-return consumer");});
    run("recreate/actual-store-failure-next-beforeCreate-flush",[]{std::lock_guard<InputHostMutex> lock(g_inputHostMx);const auto f=flow();Snapshot before;{DenyReplace held(fixtureRecordPath);inputHostTargetReturnAcceptedRecreateLocked(receipt());unchanged(before,false);check(rt::contains(f.older)&&rt::contains(f.replacement)&&rt::state.targets.size()==4,"failed RECREATE settlement preserves new reservation and old runtime targets");check(rt::state.proven.size()==1&&duty::sameOrigin(rt::state.proven[0],f.older),"failed settlement retains exact proven old debt for next existing flush");check(fileTrace::failures==1&&rt::state.lastError=="atomic-replace-failed","actual RECREATE consume fails on held private record");}
        const auto next=reserve(40,"RECREATE_TARGET");check(!rt::contains(f.older)&&rt::contains(f.replacement)&&rt::contains(f.newer)&&rt::contains(next),"actual next BeforeCreate flush settles proven old before durable new reservation");check(rt::contains(f.historical)&&rt::contains(f.foreign)&&rt::state.proven.empty(),"retry preserves history and clears only proven local debt");});
    run("continuity/proven-old-pin-A-before-quarantine-pin-B",[]{
        std::lock_guard<InputHostMutex> lock(g_inputHostMx);
        const auto old=reserve(10);addPin(old);
        const auto replacement=reserve(20,"RECREATE_TARGET");
        const duty::AcceptedPsPin newPin{"SWD\\HIDMAESTRO_VID_054C_PID_0DF2\\ROOT-B148",{"HID\\VID_054C&PID_0DF2\\HID-B148"},6};
        need(rt::state.session->attachAcceptedPsPin(rt::state.session->record()->revision,replacement,newPin),"actual new pin-B seed");
        const auto beforeRecreate=read(fixtureRecordPath);
        {DenyReplace held(fixtureRecordPath);inputHostTargetReturnAcceptedRecreateLocked(receipt());}
        check(rt::contains(old)&&rt::contains(replacement)&&read(fixtureRecordPath)==beforeRecreate,"failed RECREATE settlement leaves both exact durable pins");
        check(rt::state.proven.size()==1&&duty::sameOrigin(rt::state.proven[0],old),"only old pin-A origin is already proven by exact accepted RECREATE");
        auto retired=retirement(replacement);retired.target.rootInstanceId=newPin.sdkRootInstanceId;retired.target.hidInstanceIds=newPin.hidInstanceIds;retired.target.generation=newPin.bindingGeneration;
        g_inputHostProcess=nullptr;
        const auto beforeQuarantine=read(fixtureRecordPath);const auto beforeAttempts=fileTrace::attempts;
        const bool result=inputHostTargetReturnAfterPsQuarantineLocked(retired,true);
        observation["quarantineResult"]=result;observation["storeAttemptsDuringQuarantine"]=fileTrace::attempts-beforeAttempts;observation["stateImmediatelyAfterQuarantine"]=localState();observation["diskImmediatelyAfterQuarantine"]=json::parse(read(fixtureRecordPath));observation["notesImmediatelyAfterQuarantine"]=logs;
        check(result,"qualified pin-B quarantine flushes previously proven pin-A debt before exact pin-B settlement");
        check(!rt::contains(old)&&!rt::contains(replacement),"proven old pin-A and qualified current pin-B settle without a new Start");
        check(rt::state.targets.empty()&&rt::state.proven.empty(),"continuous lifecycle leaves no already-proven target duty stuck");
        bool attributionResolved=result&&!rt::contains(old)&&!rt::contains(replacement);
        if(!result){
            const bool repeated=inputHostTargetReturnAfterPsQuarantineLocked(retired,true);observation["sameEntryRetryResult"]=repeated;observation["retryWrites"]=fileTrace::attempts-beforeAttempts;observation["diskUnchangedAcrossRetries"]=read(fixtureRecordPath)==beforeQuarantine;
            // Attribution control only, after all original expected-outcome
            // assertions: the existing exact proven flush can finish A, then
            // the original quarantine entry still validates and settles B.
            const bool flushed=rt::flushProven();const bool settled=inputHostTargetReturnAfterPsQuarantineLocked(retired,true);
            observation["existingFlushAttributionControl"]={{"flushProven",flushed},{"subsequentExactPinBEntry",settled},{"remainingOrigins",rt::state.session->record()->obligations.size()}};
            attributionResolved=flushed&&settled&&!rt::contains(old)&&!rt::contains(replacement);
        }
        check(attributionResolved,"existing exact proven flush plus guarded pin-B entry resolves the demonstrated obstruction");
    });
    run("flush/mixed-proven-origin-refuses-create",[]{std::lock_guard<InputHostMutex> lock(g_inputHostMx);need(rt::open(),"open actual Session");auto mixed=*rt::currentOwner();mixed.transactionId="mixed-proven148";mixed.physicalRoot="USB\\VID_1234&PID_5678\\ROOT148";need(rt::state.session->reservePath(rt::state.session->record()->revision,mixed,"HID\\VID_1234&PID_5678\\OWNED148",{true,true,true}),"mixed physical debt seed");rt::state.targets.push_back({mixed,10});rt::state.proven.push_back(mixed);Snapshot before;check(!inputHostTargetReturnBeforeCreateLocked("RECREATE_TARGET",40),"existing flush refuses to erase mixed path duty");zeroConsume(before);});
}
}
int wmain(int argc,wchar_t** argv){if(argc!=3)return 2;fixtureRoot=argv[1];psTests();recreateTests();json result={{"pass",failures.empty()},{"caseCount",cases.size()},{"assertions",assertions},{"failures",failures},{"cases",cases},{"actualConsumer","verbatim virtual_target_return_runtime.h PS quarantine and accepted RECREATE plus original BeforeCreate/flush chain"},{"actualSessionStoreRecord",true},{"currentParentCaptureActual",true},{"hostIdentity","own event handle with explicit synthetic PID/birth; no real Host liveness proof"},{"realDeviceCalls",0},{"realCliOrHostRuns",0},{"installedAppDataAccessed",false}};std::ofstream out(std::filesystem::path(argv[2]),std::ios::binary);out<<result.dump(2)<<"\n";std::cout<<json({{"pass",failures.empty()},{"cases",cases.size()},{"assertions",assertions},{"failures",failures}}).dump()<<"\n";return failures.empty()?0:1;}