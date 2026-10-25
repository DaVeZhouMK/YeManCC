#pragma once
#include <windows.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <type_traits>
#include "json.hpp"
#include "keyboard_modifier_event_evidence.h"

namespace ymcc::selfSendEvidence {
using json=nlohmann::json;
inline constexpr UINT inspectionLimit=64,entryLimit=16;
inline constexpr std::size_t queueCapacity=32;
struct ScalarSnapshot {bool known=false;DWORD value=0,error=0;};
struct TokenSnapshot {ScalarSnapshot integrity{},uiAccess{},type{};};
struct ContextSnapshot {
    DWORD processId=0,threadId=0;
    ULONGLONG captureBeginTick=0,captureEndTick=0;
    bool processOpened=false,threadPresenceKnown=false,threadPresent=false;
    DWORD processError=0,threadError=0;
    TokenSnapshot process{},thread{};
};
struct ModifierEntry {UINT index=0;WORD vk=0,scan=0;DWORD flags=0;std::uintptr_t extraInfo=0;bool down=false;};
struct RequestSnapshot {
    bool retain=false;
    UINT requested=0,inspected=0,keyboard=0,other=0,ordinary=0,unclassified=0,modifierCount=0,entryCount=0;
    unsigned downMask=0,upMask=0;
    std::array<ModifierEntry,entryLimit> entries{};
};
struct BatchRecord {
    RequestSnapshot request{};ContextSnapshot context{};
    ymcc::keyboardEvents::SourceId source=ymcc::keyboardEvents::SourceId::Other;
    bool contextKnown=false,returned=false;
    std::uint64_t callId=0,constructionFailures=0;
    ULONGLONG returnTick=0;
    UINT inserted=0;DWORD sendError=0;
};
static_assert(std::is_trivially_copyable_v<BatchRecord> && sizeof(BatchRecord)<=1024);
struct TokenHandle {
    HANDLE value=nullptr;
    ~TokenHandle(){if(value)CloseHandle(value);}
    TokenHandle()=default;TokenHandle(const TokenHandle&)=delete;
};

// Sender-side capture: fixed storage only. No JSON construction/destruction,
// input call, token mutation, file logger, worker, polling or dynamic container.
inline TokenSnapshot captureToken(HANDLE handle) {
    TokenSnapshot out{};alignas(TOKEN_MANDATORY_LABEL) std::array<BYTE,512> data{};DWORD needed=0;
    if(GetTokenInformation(handle,TokenIntegrityLevel,data.data(),static_cast<DWORD>(data.size()),&needed)){
        out.integrity.error=ERROR_INVALID_DATA;
        if(needed>=sizeof(TOKEN_MANDATORY_LABEL)&&needed<=data.size()){
            const auto sid=reinterpret_cast<const TOKEN_MANDATORY_LABEL*>(data.data())->Label.Sid;
            const auto start=reinterpret_cast<std::uintptr_t>(data.data());const auto address=reinterpret_cast<std::uintptr_t>(sid);
            if(address>=start&&address<=start+needed-8){
                const auto count=*GetSidSubAuthorityCount(sid);
                if(count>0&&count<=SID_MAX_SUB_AUTHORITIES&&8u+4u*count<=start+needed-address&&IsValidSid(sid)){
                    const SID_IDENTIFIER_AUTHORITY expected=SECURITY_MANDATORY_LABEL_AUTHORITY;
                    const auto& actual=static_cast<const SID*>(sid)->IdentifierAuthority;
                    bool mandatory=true;for(unsigned i=0;i<6;++i)mandatory=mandatory&&actual.Value[i]==expected.Value[i];
                    if(mandatory)out.integrity={true,*GetSidSubAuthority(sid,count-1),ERROR_SUCCESS};
                }
            }
        }
    }else out.integrity.error=GetLastError();
    DWORD ui=0;needed=0;
    if(GetTokenInformation(handle,TokenUIAccess,&ui,sizeof(ui),&needed)){
        const bool valid=needed==sizeof(ui)&&ui<=1;out.uiAccess={valid,valid?ui:0,static_cast<DWORD>(valid?ERROR_SUCCESS:ERROR_INVALID_DATA)};
    }else out.uiAccess.error=GetLastError();
    TOKEN_TYPE type=TokenPrimary;needed=0;
    if(GetTokenInformation(handle,TokenType,&type,sizeof(type),&needed)){
        const bool valid=needed==sizeof(type)&&(type==TokenPrimary||type==TokenImpersonation);
        out.type={valid,valid?static_cast<DWORD>(type):0,static_cast<DWORD>(valid?ERROR_SUCCESS:ERROR_INVALID_DATA)};
    }else out.type.error=GetLastError();
    return out;
}
inline ContextSnapshot captureContext() {
    const DWORD saved=GetLastError();struct Restore {DWORD value;~Restore(){SetLastError(value);}} restore{saved};
    ContextSnapshot out{};out.captureBeginTick=GetTickCount64();out.processId=GetCurrentProcessId();out.threadId=GetCurrentThreadId();
    TokenHandle process;
    if(OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&process.value)){
        out.processOpened=true;out.process=captureToken(process.value);
    }else out.processError=GetLastError();
    TokenHandle thread;
    if(OpenThreadToken(GetCurrentThread(),TOKEN_QUERY,TRUE,&thread.value)){
        out.threadPresenceKnown=true;out.threadPresent=true;out.thread=captureToken(thread.value);
    }else{out.threadError=GetLastError();out.threadPresenceKnown=out.threadError==ERROR_NO_TOKEN;}
    out.captureEndTick=GetTickCount64();return out;
}
inline RequestSnapshot captureRequest(const INPUT* inputs,UINT count) noexcept {
    RequestSnapshot out{};out.requested=count;
    if(!inputs||!count)return out;
    out.inspected=count<inspectionLimit?count:inspectionLimit;
    for(UINT i=0;i<out.inspected;++i){const auto& input=inputs[i];
        if(input.type!=INPUT_KEYBOARD){++out.other;continue;}++out.keyboard;
        if(input.ki.dwFlags&(KEYEVENTF_SCANCODE|KEYEVENTF_UNICODE)){++out.unclassified;continue;}
        const auto bit=ymcc::keyboardEvents::bit(input.ki.wVk);
        if(!bit){++out.ordinary;continue;}++out.modifierCount;
        const bool released=(input.ki.dwFlags&KEYEVENTF_KEYUP)!=0;(released?out.upMask:out.downMask)|=bit;
        if(out.entryCount<entryLimit)out.entries[out.entryCount++]={i,input.ki.wVk,input.ki.wScan,input.ki.dwFlags,input.ki.dwExtraInfo,!released};
    }
    out.retain=out.modifierCount||out.unclassified||count>inspectionLimit;return out;
}

// JSON is constructed only by the existing drain/export owner (and inert tests).
inline json toJson(const TokenSnapshot& value) {
    return {{"integrity",{{"known",value.integrity.known},{"rid",value.integrity.known?json(value.integrity.value):json(nullptr)},{"error",value.integrity.error}}},
        {"uiAccess",{{"known",value.uiAccess.known},{"value",value.uiAccess.known?json(value.uiAccess.value!=0):json(nullptr)},{"error",value.uiAccess.error}}},
        {"type",{{"known",value.type.known},{"value",value.type.known?json(value.type.value==TokenPrimary?"primary":"impersonation"):json(nullptr)},{"error",value.type.error}}}};
}
inline json toJson(const ContextSnapshot& value) {
    json out={{"processId",value.processId},{"threadId",value.threadId},{"captureBeginTick",value.captureBeginTick},{"captureEndTick",value.captureEndTick},
        {"scope","caller token context before send; not proof of kernel-used token or external sender"},
        {"processToken",{{"opened",value.processOpened},{"error",value.processError}}},
        {"threadToken",{{"presenceKnown",value.threadPresenceKnown},{"present",value.threadPresenceKnown?json(value.threadPresent):json(nullptr)},{"error",value.threadError}}},
        {"openAsSelf",true},{"maxTokenQueries",8},{"loadedSourceIdentityProven",false}};
    if(value.processOpened)out["processToken"]["properties"]=toJson(value.process);
    if(value.threadPresent)out["threadToken"]["properties"]=toJson(value.thread);
    return out;
}
inline json toJson(const RequestSnapshot& value) {
    if(!value.retain)return nullptr;
    json entries=json::array();for(UINT i=0;i<value.entryCount;++i){const auto& e=value.entries[i];entries.push_back({{"index",e.index},{"virtualKey",e.vk},{"scanCode",e.scan},{"flags",e.flags},{"extraInfo",e.extraInfo},{"down",e.down}});}
    return {{"schema",1},{"scope","YMCC untagged shortcut and focus fallback batches only"},{"requestedInputs",value.requested},
        {"inspectionLimit",inspectionLimit},{"inspectionComplete",value.requested<=inspectionLimit},
        {"keyboardInputsInspected",value.keyboard},{"otherInputsInspected",value.other},{"ordinaryKeysOmitted",value.ordinary},
        {"scanCodeOrUnicodeInputsUnclassified",value.unclassified},{"modifierEntryCount",value.modifierCount},{"modifierEntries",std::move(entries)},
        {"modifierEntriesTruncated",value.modifierCount>entryLimit},{"attemptedModifierDownMask",value.downMask},{"attemptedModifierUpMask",value.upMask},
        {"modifierClassificationComplete",value.requested<=inspectionLimit&&!value.unclassified},
        {"inputOrTagChanged",false},{"ordinaryKeyContentsRetained",false},{"hookEventUniquePairingProven",false},
        {"osOrGameDeliveryProven",false},{"beginRecordIsSendEntryProof",false},{"logWriteReceiptAvailable",false}};
}
inline json toJson(const BatchRecord& value) {
    auto out=toJson(value.request);if(!out.is_object())return out;
    out["callId"]=value.callId;out["callIdScope"]="within writer process lifetime";out["source"]=ymcc::keyboardEvents::sourceName(value.source);
    out["callerContextBeforeSendKnown"]=value.contextKnown;
    out["callerContextBeforeSend"]=value.contextKnown?toJson(value.context):json{{"known",false},{"reason","context-capture-failed"}};
    out["phase"]=value.returned?"returned":"begin";out["diagnosticFailuresBefore"]=value.constructionFailures;
    out["senderCppDynamicAllocations"]=false;
    if(value.returned){out["returnTick"]=value.returnTick;out["insertedInputs"]=value.inserted;out["fullBatchInserted"]=value.inserted==value.request.requested;
        out["win32ErrorHint"]=value.inserted==value.request.requested?0:value.sendError;out["errorHintIsDefinitive"]=false;out["returnTokenContextCaptured"]=false;}
    return out;
}
// Existing formatter entry points are kept for isolated compatibility checks;
// the send wrapper calls only capture* and the typed queue, never these helpers.
inline json token(HANDLE handle){return toJson(captureToken(handle));}
inline json context(){const DWORD saved=GetLastError();struct Restore{DWORD value;~Restore(){SetLastError(value);}}restore{saved};return toJson(captureContext());}
inline json request(const INPUT* inputs,UINT count){return toJson(captureRequest(inputs,count));}

struct QueueSnapshot {
    std::array<BatchRecord,queueCapacity> rows{};
    std::size_t count=0;bool taken=false,changed=false;
    std::uint64_t revision=0,droppedContention=0,droppedOverflow=0,recordFailures=0,discardedDisabled=0;
};
namespace detail {struct QueueTestAccess;}
class Queue {
    friend struct detail::QueueTestAccess;
    std::mutex mutex_;std::array<BatchRecord,queueCapacity> rows_{};std::size_t count_=0;
    std::uint64_t lastTakenRevision_=0;
    std::atomic<bool> hasRows_{false};std::atomic<std::uint64_t> revision_{0},contention_{0},overflow_{0},failures_{0},discarded_{0};
public:
    bool hasRows()const noexcept{return hasRows_.load(std::memory_order_acquire);}
    void record(const BatchRecord& row) noexcept {
        if(!row.request.retain||!row.callId){failures_.fetch_add(1,std::memory_order_relaxed);revision_.fetch_add(1,std::memory_order_release);return;}
        try {
            std::unique_lock<std::mutex> lock(mutex_,std::try_to_lock);
            if(!lock.owns_lock()){contention_.fetch_add(1,std::memory_order_relaxed);revision_.fetch_add(1,std::memory_order_release);return;}
            if(count_==queueCapacity){overflow_.fetch_add(1,std::memory_order_relaxed);revision_.fetch_add(1,std::memory_order_release);return;}
            rows_[count_++]=row;hasRows_.store(true,std::memory_order_release);revision_.fetch_add(1,std::memory_order_release);
        }catch(...){failures_.fetch_add(1,std::memory_order_relaxed);revision_.fetch_add(1,std::memory_order_release);}
    }
    void discardDisabled() noexcept {
        if(!hasRows())return;
        try {
            std::unique_lock<std::mutex> lock(mutex_,std::try_to_lock);
            if(!lock.owns_lock()){contention_.fetch_add(1,std::memory_order_relaxed);revision_.fetch_add(1,std::memory_order_release);return;}
            if(count_){discarded_.fetch_add(count_,std::memory_order_relaxed);for(std::size_t i=0;i<count_;++i)rows_[i]={};
                count_=0;hasRows_.store(false,std::memory_order_release);revision_.fetch_add(1,std::memory_order_release);}
        }catch(...){failures_.fetch_add(1,std::memory_order_relaxed);revision_.fetch_add(1,std::memory_order_release);}
    }
    QueueSnapshot take() noexcept {
        QueueSnapshot out;
        try {
            std::unique_lock<std::mutex> lock(mutex_,std::try_to_lock);
            if(!lock.owns_lock()){contention_.fetch_add(1,std::memory_order_relaxed);revision_.fetch_add(1,std::memory_order_release);return out;}
            out.taken=true;out.count=count_;for(std::size_t i=0;i<count_;++i){out.rows[i]=rows_[i];rows_[i]={};}
            count_=0;hasRows_.store(false,std::memory_order_release);
            out.revision=revision_.load(std::memory_order_acquire);out.changed=out.revision!=lastTakenRevision_;lastTakenRevision_=out.revision;
            out.droppedContention=contention_.load(std::memory_order_relaxed);out.droppedOverflow=overflow_.load(std::memory_order_relaxed);
            out.recordFailures=failures_.load(std::memory_order_relaxed);out.discardedDisabled=discarded_.load(std::memory_order_relaxed);
        }catch(...){out.taken=false;failures_.fetch_add(1,std::memory_order_relaxed);revision_.fetch_add(1,std::memory_order_release);}
        return out;
    }
};
} // namespace ymcc::selfSendEvidence
