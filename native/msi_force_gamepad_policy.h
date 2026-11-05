#pragma once
#include "oem_gamepad_mode_session.h"
#include "oem_gamepad_mode_journal.h"
#include <windows.h>
#include <functional>
#include <mutex>
#include <utility>

// Include after MSI identity helpers. Caller supplies the existing MSI/shield
// admission and serial lifecycle; current must be non-reentrant and valid for
// this operation (including exit restoration). No hardware, worker or timer I/O.
namespace ymcc::msi::forcePolicy {
using namespace ymcc::oem::gamepad;
inline constexpr size_t maxJournalBytes=8192;
inline constexpr char ownerTag[]="ymcc-msi-force-policy-v1";
inline Value text(std::u16string_view s) {
    Value v;v.exists=true;v.type=REG_SZ;
    for(char16_t c:s){v.bytes.push_back(uint8_t(c&255));v.bytes.push_back(uint8_t(c>>8));}
    v.bytes.push_back(0);v.bytes.push_back(0);return v;
}
inline std::vector<Patch> patches() {
    return {{{RegistryView::View32,L"SOFTWARE\\MSI\\MSI Center M\\Component\\User Scenario",L"AutoSwitchMode"},text(u"0")},
            {{RegistryView::View32,L"SOFTWARE\\MSI\\MSI Center M\\OsdEditor",L"ControlModeUserSet"},text(u"XInput")}};
}
inline bool recognized(size_t i,const Value& v) {
    return i==0 ? v==text(u"0")||v==text(u"1") : v==text(u"XInput")||v==text(u"Desktop");
}
inline bool permitted(const std::function<bool()>& current) noexcept {
    try{return current&&current();}catch(...){return false;}
}
inline std::string boardBinding() {
    if(!msiClawKnownBoardAuthority())return {};
    std::string out=ownerTag;
    for(const auto* part:{&g_machineIdentity.manufacturer,&g_machineIdentity.product,&g_machineIdentity.deviceClass}) {
        if(part->empty()||part->size()>128)return {};
        for(char c:*part)if(uint8_t(c)<32||uint8_t(c)>126||c=='|')return {};
        out+='|';out+=*part;
    }
    return out;
}
inline uint64_t creation(HANDLE process) {
    FILETIME born{},exited{},kernel{},user{};
    if(!GetProcessTimes(process,&born,&exited,&kernel,&user))return 0;
    return (uint64_t(born.dwHighDateTime)<<32)|born.dwLowDateTime;
}
inline Owner self() {
    // Private journal codec slots carry PID/birth; these are not Host/power or
    // firmware authority. Do not expose this as another lifecycle owner.
    return {ownerTag,GetCurrentProcessId(),creation(GetCurrentProcess()),1,1};
}
inline bool ownerShape(const Owner& o) {
    return validOwner(o)&&o.processNonce==ownerTag&&o.session<=MAXDWORD&&o.hostEpoch&&
        o.powerGeneration==1&&o.intentRevision==1;
}
inline bool recoverableOwner(const Owner& old,const Owner& now) {
    if(!ownerShape(old)||!ownerShape(now))return false;
    if(old==now)return true;
    HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,DWORD(old.session));
    if(!process)return GetLastError()==ERROR_INVALID_PARAMETER;
    const auto born=creation(process);const DWORD wait=WaitForSingleObject(process,0);CloseHandle(process);
    return born&&(born!=old.hostEpoch||wait==WAIT_OBJECT_0);
}
class NarrowAdapter final:public Adapter {
    std::string binding_;
public:
    explicit NarrowAdapter(std::string b):binding_(std::move(b)){}
    Capability capability()const override {
        Capability c;c.vendor=Vendor::Msi;c.binding=binding_;c.policyCapabilityVerified=!binding_.empty();
        // PolicySession requires an intent route; this labels preference only.
        c.nativeRoute={Vendor::Msi,InputProtocol::XInput,"msi.force-policy.preference-only"};return c;
    }
    std::vector<Patch> patches(Intent)const override{return forcePolicy::patches();}
    bool validRecord(const Record& r)const override {
        if(binding_.empty()||r.schema!=contractVersion||r.vendor!=Vendor::Msi||
           r.binding!=binding_||r.intent!=Intent::NativeGamepad||!ownerShape(r.owner)||r.entries.size()!=2)return false;
        const auto expected=forcePolicy::patches();
        for(size_t i=0;i<2;++i) {
            const auto& e=r.entries[i];
            if(!(e.patch.address==expected[i].address)||!(e.patch.desired==expected[i].desired)||
               !recognized(i,e.original)||(!r.pending&&e.mayHaveWritten)||
               (e.mayHaveWritten&&e.original==e.patch.desired))return false;
        }
        return true;
    }
};
struct State {
    std::mutex mutex;
    PolicySession session;
    HANDLE claim=nullptr;
    bool active=false,observed=false,exists=false;
    std::string binding,exact,tentative,reason="not-run";
    Result last;
    DWORD error=0;
};
static State state;
inline std::wstring path(){return POWER_CONTROL_DIR+L"\\msi-force-gamepad-policy.json";}
inline bool claim(State& s) {
    if(s.claim)return true;
    // The named object's lifetime, not thread-affine mutex ownership, excludes
    // concurrent processes/PowerControl roots until release or process death.
    SetLastError(ERROR_SUCCESS);
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,L"Global\\YeManCC.MsiForceGamepadPolicy.v1");
    const DWORD error=GetLastError();
    if(!event||error==ERROR_ALREADY_EXISTS){if(event)CloseHandle(event);s.error=event?ERROR_BUSY:error;s.reason="owner-busy-or-unknown";return false;}
    s.claim=event;s.observed=false;s.exact.clear();s.tentative.clear();return true;
}
inline bool readFile(std::string& bytes,bool& exists,DWORD& error) {
    bytes.clear();exists=false;
    HANDLE file=CreateFileW(path().c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(file==INVALID_HANDLE_VALUE){error=GetLastError();if(error!=ERROR_FILE_NOT_FOUND&&error!=ERROR_PATH_NOT_FOUND)return false;error=0;return true;}
    exists=true;
    struct Close {HANDLE value;~Close(){CloseHandle(value);}} close{file};
    BY_HANDLE_FILE_INFORMATION info{};LARGE_INTEGER size{};
    if(!GetFileInformationByHandle(file,&info)||!GetFileSizeEx(file,&size)){error=GetLastError();return false;}
    if(GetFileType(file)!=FILE_TYPE_DISK||(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))||
       size.QuadPart<=0||size.QuadPart>static_cast<LONGLONG>(maxJournalBytes)){error=ERROR_INVALID_DATA;return false;}
    bytes.resize(size_t(size.QuadPart));DWORD count=0;
    if(!ReadFile(file,bytes.data(),DWORD(bytes.size()),&count,nullptr)){error=GetLastError();return false;}
    if(count!=bytes.size()){error=ERROR_RETRY;return false;}
    error=0;return true;
}
class Store final:public JournalStore {
    State& s_;const NarrowAdapter& adapter_;Owner owner_;const std::function<bool()>& current_;
    bool observe() {
        std::string bytes;bool exists=false;
        if(!readFile(bytes,exists,s_.error)){s_.reason="journal-read-failed";return false;}
        const bool tentative=exists&&!s_.tentative.empty()&&bytes==s_.tentative;
        if(s_.observed&&!tentative&&(exists!=s_.exists||(exists&&bytes!=s_.exact))) {
            s_.reason="journal-changed";return false;
        }
        s_.observed=true;s_.exists=exists;s_.exact=std::move(bytes);s_.tentative.clear();return true;
    }
public:
    Store(State& s,const NarrowAdapter& a,Owner owner,const std::function<bool()>& c):s_(s),adapter_(a),owner_(std::move(owner)),current_(c){}
    JournalRead load()override {
        if(!permitted(current_)||!observe())return {};
        if(!s_.exists)return {JournalState::Absent,{}};
        auto r=decodeRecord(s_.exact);
        if(r.state!=JournalState::Record||!adapter_.validRecord(r.record)) {s_.reason="journal-invalid";return {};}
        if(r.record.pending&&!recoverableOwner(r.record.owner,owner_)){s_.reason="old-owner-alive-or-unknown";return {};}
        return r;
    }
    bool save(const Record& r)override {
        if(!permitted(current_)||boardBinding()!=s_.binding||!adapter_.validRecord(r)||
           !recoverableOwner(r.owner,owner_)||!observe())return false;
        const auto bytes=encodeRecord(r).dump();if(bytes.size()>maxJournalBytes)return false;
        s_.tentative=bytes;
        const bool written=sgWriteFileAtomic(path(),bytes);
        std::string after;bool exists=false;
        if(!written||!readFile(after,exists,s_.error)||!exists||after!=bytes){s_.reason="journal-commit-unverified";return false;}
        s_.exact=bytes;s_.exists=true;s_.tentative.clear();return true;
    }
};
class Registry final:public RegistryIo {
    State& s_;const std::function<bool()>& current_;
    bool admitted(const Address& a)const {
        const auto allowed=forcePolicy::patches();
        return (a==allowed[0].address||a==allowed[1].address)&&permitted(current_)&&boardBinding()==s_.binding;
    }
public:
    Registry(State& s,const std::function<bool()>& c):s_(s),current_(c){}
    ReadResult read(const Address& a)override {
        if(!admitted(a))return {};
        HKEY key=nullptr;
        LSTATUS status=RegOpenKeyExW(HKEY_LOCAL_MACHINE,a.subkey.c_str(),0,KEY_QUERY_VALUE|KEY_WOW64_32KEY,&key);
        if(status!=ERROR_SUCCESS){s_.error=status;return {status==ERROR_FILE_NOT_FOUND?ReadState::MissingKey:ReadState::Unknown,{}};}
        struct Close {HKEY value;~Close(){RegCloseKey(value);}} close{key};
        DWORD type=0,size=0;status=RegQueryValueExW(key,a.name.c_str(),nullptr,&type,nullptr,&size);
        if(status!=ERROR_SUCCESS){s_.error=status;return {status==ERROR_FILE_NOT_FOUND?ReadState::MissingValue:ReadState::Unknown,{}};}
        if(size>1024){s_.error=ERROR_MORE_DATA;return {};}
        Value value;value.exists=true;value.type=type;value.bytes.resize(size);
        DWORD fetched=size,readType=0;
        status=RegQueryValueExW(key,a.name.c_str(),nullptr,&readType,value.bytes.empty()?nullptr:value.bytes.data(),&fetched);
        if(status!=ERROR_SUCCESS||readType!=type||fetched!=size){s_.error=status==ERROR_SUCCESS?ERROR_RETRY:status;return {};}
        return {ReadState::Value,std::move(value)};
    }
    bool write(const Address& a,const Value& value)override {
        if(!admitted(a)||(!recognized(a.name==L"AutoSwitchMode"?0:1,value)))return false;
        HKEY key=nullptr;
        LSTATUS status=RegOpenKeyExW(HKEY_LOCAL_MACHINE,a.subkey.c_str(),0,KEY_SET_VALUE|KEY_WOW64_32KEY,&key);
        if(status!=ERROR_SUCCESS){s_.error=status;return false;}
        struct Close {HKEY value;~Close(){RegCloseKey(value);}} close{key};
        if(!permitted(current_)||boardBinding()!=s_.binding)return false;
        status=RegSetValueExW(key,a.name.c_str(),0,value.type,value.bytes.data(),DWORD(value.bytes.size()));
        s_.error=status;return status==ERROR_SUCCESS;
    }
};
class Gate final:public AuthorityGate {
    const std::function<bool()>& current_;
public:
    explicit Gate(const std::function<bool()>& c):current_(c){}
    bool current(const Owner&)override{return permitted(current_);}
};
inline bool settled(const Result& r) {
    return !r.cleanupPending&&(r.status==Status::Released||r.status==Status::Recovered||r.status==Status::NoLease);
}
inline bool finish(State& s,const char* action,bool ok)noexcept {
    if(!s.session.ownsLease()){s.active=false;if(s.claim){CloseHandle(s.claim);s.claim=nullptr;}}
    try{appendNativeLifecycleLog("msi-claw-gamepad-mode",{{"action",action},{"ok",ok},
        {"status",statusName(s.last.status)},{"reason",s.reason},{"win32Error",s.error},
        {"policyVerified",ok&&s.active},{"cleanupPending",s.last.cleanupPending||(s.session.ownsLease()&&!s.active)},
        {"leaseActive",s.session.ownsLease()},{"attemptedWrites",s.last.attemptedWrites},
        {"verifiedWrites",s.last.verifiedWrites},{"externalChangesPreserved",s.last.externalChangesPreserved},
        {"hardwareModeVerified",false},{"allModeWritersExcluded",false}});}catch(...){}
    return ok;
}
inline bool run(bool apply,const std::function<bool()>& current) {
    auto& s=state;std::lock_guard<std::mutex> lock(s.mutex);
    const char* action=apply?"force-policy-apply":"force-policy-restore";
    s.error=0;s.reason.clear();s.last={};
    try {
        const auto binding=boardBinding();const auto owner=self();
        if(binding.empty()||!ownerShape(owner)){s.reason="identity-unavailable";return finish(s,action,false);}
        if(!permitted(current)){s.last.status=Status::Stale;s.reason="intent-not-current";return finish(s,action,false);}
        if(s.session.ownsLease()&&s.binding!=binding){s.reason="board-changed";return finish(s,action,false);}
        s.binding=binding;
        if(!claim(s)){s.last.status=Status::Busy;return finish(s,action,false);}
        NarrowAdapter adapter(binding);Registry registry(s,current);Store journal(s,adapter,owner,current);Gate gate(current);
        if(s.session.ownsLease()&&journal.load().state!=JournalState::Record) {
            s.last.status=Status::JournalInvalid;s.last.cleanupPending=true;return finish(s,action,false);
        }
        if(!apply||!s.session.ownsLease()||!s.active) {
            s.active=false;
            s.last=s.session.ownsLease()?s.session.release(s.session.record()->owner,adapter,registry,journal):
                s.session.recover(adapter,registry,journal);
            if(!settled(s.last))return finish(s,action,false);
            if(!apply)return finish(s,action,permitted(current));
        }
        const auto desired=forcePolicy::patches();bool matching=true;
        // Both existing values must be recognized before PolicySession can write.
        for(size_t i=0;i<desired.size();++i) {
            const auto read=registry.read(desired[i].address);
            if(read.state!=ReadState::Value||!recognized(i,read.value)) {
                s.last.status=Status::ReadUnknown;s.reason="existing-values-unrecognized";return finish(s,action,false);
            }
            matching=matching&&read.value==desired[i].desired;
        }
        Result released;
        if(s.active) {
            if(matching) {
                s.last.status=Status::AlreadyOwned;s.reason="current-values-verified";
                return finish(s,action,permitted(current));
            }
            // A fresh explicit event may reapply recognized settings. Release
            // conditionally first so external values become the new originals;
            // retain the process claim across both transactions. No polling.
            s.active=false;action="force-policy-reapply";
            s.last=s.session.release(s.session.record()->owner,adapter,registry,journal);
            if(!settled(s.last))return finish(s,action,false);
            released=s.last;s.reason="explicit-event-reapply";
        }
        // acquire rereads and strictly validates BOTH originals after release.
        s.last=s.session.acquire({owner,Intent::NativeGamepad,true},adapter,registry,journal,gate);
        s.last.attemptedWrites+=released.attemptedWrites;s.last.verifiedWrites+=released.verifiedWrites;
        s.last.externalChangesPreserved+=released.externalChangesPreserved;
        s.active=s.last.status==Status::Acquired&&s.last.policyVerified;
        if(!s.active&&s.session.ownsLease()) {
            const auto failed=s.last;
            if(journal.load().state==JournalState::Record)
                s.last=s.session.release(s.session.record()->owner,adapter,registry,journal);
            else {s.last.status=Status::JournalInvalid;s.last.cleanupPending=true;}
            finish(s,"force-policy-rollback",settled(s.last));
            s.last=failed;s.last.cleanupPending=s.session.ownsLease();
        }
        return finish(s,action,s.active&&permitted(current));
    }catch(...) {
        s.last.status=Status::JournalFailure;s.last.cleanupPending=s.session.ownsLease();
        s.active=false;s.reason="exception-retained-debt";return finish(s,action,false);
    }
}
} // namespace ymcc::msi::forcePolicy

static bool msiForceGamepadPolicyApply(const std::function<bool()>& current) {
    return ymcc::msi::forcePolicy::run(true,current);
}
static bool msiForceGamepadPolicyRestore(const std::function<bool()>& current) {
    return ymcc::msi::forcePolicy::run(false,current);
}
static nlohmann::json msiForceGamepadPolicySnapshot() {
    auto& s=ymcc::msi::forcePolicy::state;std::lock_guard<std::mutex> lock(s.mutex);
    return {{"schema",1},{"active",s.active},{"leaseActive",s.session.ownsLease()},
        {"cleanupPending",s.last.cleanupPending||(s.session.ownsLease()&&!s.active)},
        {"status",ymcc::oem::gamepad::statusName(s.last.status)},{"reason",s.reason},{"win32Error",s.error},
        {"hardwareModeVerified",false},{"allModeWritersExcluded",false}};
}



