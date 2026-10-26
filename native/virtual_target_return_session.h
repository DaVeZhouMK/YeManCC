#pragma once
#include "virtual_target_return_store.h"
#ifdef _WIN32

// A cooperating-writer file session only. Every record remains historical data.
// No device/process work, cold cleanup authority, retries, timers or discovery.
namespace ymcc::targetReturn {
enum class SessionErrorCode { None, Invalid, RetryableIo, TransformRejected };
struct SessionError {
    SessionErrorCode code=SessionErrorCode::None;
    DWORD win32Error=0;
    std::string reason;
};
namespace sessionDetail {
inline constexpr DWORD ownerShareMode=0;
}
// The .owner handle serializes writers across Session objects/processes only.
// Every call on the same Session, including reads, must be serialized by the
// existing single Host lifecycle lock. Session has no internal thread safety.
class Session final {
public:
    // Returns an inspectable failed session on any open error; never an empty
    // fallback record. Nonmovable, with C++17 guaranteed factory copy elision.
    static Session open(const std::wstring& absolutePath,const std::string& machineBinding){return Session(absolutePath,machineBinding);}
    ~Session()=default;
    Session(const Session&)=delete;
    Session& operator=(const Session&)=delete;
    Session(Session&&)=delete;
    Session& operator=(Session&&)=delete;

    bool ready()const noexcept{return owner_.valid()&&record_.has_value();}
    const SessionError& error()const noexcept{return error_;}
    // Null on open failure. A successful load is no current output admission,
    // owner-death proof, target absence proof or permission to consume anything.
    const Record* record()const noexcept{return ready()?&*record_:nullptr;}
    LoadState openedFrom()const noexcept{return openedFrom_;}

    bool reservePath(std::uint64_t expectedRevision,const Origin& exactOrigin,const std::string& path,ReservationProof proof){
        return update([&](Record& candidate,std::string& error){return targetReturn::reservePath(*record_,expectedRevision,exactOrigin,path,proof,candidate,&error);});
    }
    bool reserveNativeOwnedSet(std::uint64_t expectedRevision,const Origin& exactOrigin,
            const std::vector<std::string>& paths,NativeOwnedSetProof proof){
        return update([&](Record& candidate,std::string& error){return targetReturn::reserveNativeOwnedSet(*record_,expectedRevision,exactOrigin,paths,proof,candidate,&error);});
    }
    bool reserveTarget(std::uint64_t expectedRevision,const Origin& exactOrigin,TargetCreationProof proof){
        return update([&](Record& candidate,std::string& error){return targetReturn::reserveTargetCreation(*record_,expectedRevision,exactOrigin,proof,candidate,&error);});
    }
    bool attachAcceptedPsPin(std::uint64_t expectedRevision,const Origin& exactOrigin,const AcceptedPsPin& pin){
        return update([&](Record& candidate,std::string& error){return targetReturn::attachAcceptedPsPin(*record_,expectedRevision,exactOrigin,pin,candidate,&error);});
    }
    // Caller must first prove the exact path has returned / target origin is
    // settled. Disk data and this session never supply that external proof.
    bool consumePath(std::uint64_t expectedRevision,const Origin& exactOrigin,const std::string& path){
        return update([&](Record& candidate,std::string& error){return targetReturn::consumePath(*record_,expectedRevision,exactOrigin,path,candidate,&error);});
    }
    bool consumeOrigin(std::uint64_t expectedRevision,const Origin& exactOrigin){
        return update([&](Record& candidate,std::string& error){return targetReturn::consumeOrigin(*record_,expectedRevision,exactOrigin,candidate,&error);});
    }
private:
    fileDetail::Handle owner_{INVALID_HANDLE_VALUE};
    std::wstring recordPath_;
    std::optional<Record> record_;
    LoadState openedFrom_=LoadState::Invalid;
    SessionError error_;

    bool setError(SessionErrorCode code,const char* reason,DWORD win32Error=0){error_={code,win32Error,reason};return false;}
    bool clearError(){error_={};return true;}
    Session(const std::wstring& absolutePath,const std::string& machineBinding){
        std::wstring ownerPath;
        if(!fileDetail::normalize(absolutePath,recordPath_)||!fileDetail::normalize(recordPath_+L".owner",ownerPath)||!detail::text(machineBinding,4096)){
            setError(SessionErrorCode::Invalid,"invalid-explicit-path-or-machine");return;
        }
        DWORD e=0;bool invalid=false;
        if(!fileDetail::parents(recordPath_,e,invalid)){setError(invalid?SessionErrorCode::Invalid:SessionErrorCode::RetryableIo,"parent-directory-unavailable-or-unsafe",e);return;}
        const DWORD attributes=GetFileAttributesW(ownerPath.c_str());
        if(attributes!=INVALID_FILE_ATTRIBUTES&&(attributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))){setError(SessionErrorCode::Invalid,"owner-file-kind-invalid");return;}
        if(attributes==INVALID_FILE_ATTRIBUTES){e=GetLastError();if(e!=ERROR_FILE_NOT_FOUND){setError(SessionErrorCode::RetryableIo,"owner-attributes-unavailable",e);return;}}
        // Do not truncate, delete or replace an existing owner sentinel. It must
        // retain one file identity across clean exits and process crashes.
        fileDetail::Handle h(CreateFileW(ownerPath.c_str(),GENERIC_READ|GENERIC_WRITE,sessionDetail::ownerShareMode,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
        if(!h.valid()){
            e=GetLastError();if(e!=ERROR_FILE_EXISTS&&e!=ERROR_ALREADY_EXISTS){setError(SessionErrorCode::RetryableIo,"owner-create-or-lock-failed",e);return;}
            h.value=CreateFileW(ownerPath.c_str(),GENERIC_READ|GENERIC_WRITE,sessionDetail::ownerShareMode,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
            if(!h.valid()){setError(SessionErrorCode::RetryableIo,"owner-open-or-lock-failed",GetLastError());return;}
        }
        // A zero-byte regular single-link file is the complete owner sentinel.
        // CREATE_NEW has no follow-up content initialization to strand on crash.
        // Only the live share=0 handle supplies exclusion; no file grants target
        // provenance or cleanup authority. Nonempty files are never repaired.
        BY_HANDLE_FILE_INFORMATION before{},after{};
        if(!fileDetail::info(h.value,before,e)){setError(e==ERROR_INVALID_DATA?SessionErrorCode::Invalid:SessionErrorCode::RetryableIo,"owner-file-kind-or-info-invalid",e);return;}
        if(before.nFileSizeHigh||before.nFileSizeLow!=0){setError(SessionErrorCode::Invalid,"owner-file-not-empty");return;}
        if(!fileDetail::info(h.value,after,e)||!fileDetail::sameInfo(before,after)){setError(SessionErrorCode::RetryableIo,"owner-snapshot-changed",e);return;}
        const auto loaded=targetReturn::load(recordPath_,machineBinding);openedFrom_=loaded.state;
        if(loaded.state!=LoadState::Missing&&loaded.state!=LoadState::Valid){setError(loaded.state==LoadState::Invalid?SessionErrorCode::Invalid:SessionErrorCode::RetryableIo,loaded.reason.c_str(),loaded.win32Error);return;}
        if(loaded.state==LoadState::Missing){Record initial;initial.machineBinding=machineBinding;record_=std::move(initial);}
        else record_=loaded.record;
        owner_.value=h.value;h.value=INVALID_HANDLE_VALUE;
    }
    template<class Transform> bool update(Transform&& transform){
        if(!ready())return false; // Preserve the original open error and no record.
        Record candidate;std::string reason;
        if(!transform(candidate,reason))return setError(SessionErrorCode::TransformRejected,reason.c_str());
        if(candidate.revision==record_->revision)return clearError();
        const auto saved=targetReturn::store(recordPath_,candidate);
        if(!saved.committed())return setError(saved.state==StoreState::Invalid?SessionErrorCode::Invalid:SessionErrorCode::RetryableIo,saved.reason.c_str(),saved.win32Error);
        record_=std::move(candidate);return clearError();
    }
};
} // namespace ymcc::targetReturn
#endif
