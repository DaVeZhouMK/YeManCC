#pragma once
#include "native_refresh_writer_record.h"
#include "virtual_target_return_store.h"
#ifdef _WIN32

// Bounded synchronous local file operations only. The containing directory must
// remain under the caller's control. No record content is process/Job authority.
namespace ymcc::nativeRefreshWriter {
namespace fileDetail=targetReturn::fileDetail;
enum class LoadState { Missing, Valid, RetryableIo, Invalid };
struct LoadResult {
    LoadState state=LoadState::Invalid;
    std::optional<Record> record;
    DWORD win32Error=0;
    std::string reason;
};
enum class StoreState { Committed, RetryableIo, Invalid };
struct StoreResult {
    StoreState state=StoreState::Invalid;
    DWORD win32Error=0;
    std::string reason;
    bool committed()const noexcept{return state==StoreState::Committed;}
};
inline LoadResult load(const std::wstring& rawPath,const std::string& machineBinding){
    LoadResult r;std::wstring path;
    if(!fileDetail::normalize(rawPath,path)||!detail::text(machineBinding,4096)){r.reason="invalid-explicit-path-or-machine";return r;}
    DWORD e=0;bool invalid=false;
    if(!fileDetail::parents(path,e,invalid)){r.state=invalid?LoadState::Invalid:LoadState::RetryableIo;r.win32Error=e;r.reason="unsafe-or-unavailable-parent";return r;}
    const DWORD attributes=GetFileAttributesW(path.c_str());
    if(attributes==INVALID_FILE_ATTRIBUTES){e=GetLastError();if(e!=ERROR_FILE_NOT_FOUND){r.state=LoadState::RetryableIo;r.win32Error=e;r.reason="record-attributes-unavailable";return r;}}
    else if(attributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)){r.reason="record-file-kind-invalid";return r;}
    fileDetail::Handle h(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
    if(!h.valid()){r.win32Error=GetLastError();r.state=r.win32Error==ERROR_FILE_NOT_FOUND?LoadState::Missing:LoadState::RetryableIo;r.reason=r.state==LoadState::Missing?"record-missing":"record-open-failed";return r;}
    BY_HANDLE_FILE_INFORMATION before{},after{};
    if(!fileDetail::info(h.value,before,e)){r.win32Error=e;r.state=e==ERROR_INVALID_DATA?LoadState::Invalid:LoadState::RetryableIo;r.reason="record-file-kind-or-info-invalid";return r;}
    if(before.nFileSizeHigh||before.nFileSizeLow==0||before.nFileSizeLow>maxRecordBytes){r.reason="record-size-invalid";return r;}
    std::string bytes(before.nFileSizeLow,'\0');DWORD got=0;
    if(!ReadFile(h.value,bytes.data(),before.nFileSizeLow,&got,nullptr)||got!=before.nFileSizeLow){r.state=LoadState::RetryableIo;r.win32Error=GetLastError();r.reason="record-read-incomplete";return r;}
    char extra=0;DWORD extraRead=0;
    if(!ReadFile(h.value,&extra,1,&extraRead,nullptr)||extraRead){r.state=LoadState::RetryableIo;r.win32Error=GetLastError();r.reason="record-read-snapshot-changed";return r;}
    if(!fileDetail::info(h.value,after,e)||!fileDetail::sameInfo(before,after)){r.state=LoadState::RetryableIo;r.win32Error=e;r.reason="record-info-snapshot-changed";return r;}
    Record record;
    if(!decode(bytes,record,&r.reason)||record.machineBinding!=machineBinding){r.reason="record-invalid-or-foreign-machine";return r;}
    r.state=LoadState::Valid;r.record=std::move(record);r.reason="writer-history-only";return r;
}
// Nonexclusive leaf: callers must serialize access; Session below holds the
// cross-process .owner exclusion. Atomic replacement is not independent CAS.
inline StoreResult store(const std::wstring& rawPath,const Record& record){
    StoreResult r;std::wstring path;std::string bytes;
    if(!fileDetail::normalize(rawPath,path)||!encode(record,bytes,&r.reason)){r.reason="invalid-explicit-path-or-record";return r;}
    DWORD error=0;bool invalid=false;
    if(!fileDetail::parents(path,error,invalid)){r.state=invalid?StoreState::Invalid:StoreState::RetryableIo;r.win32Error=error;r.reason="unsafe-or-unavailable-parent";return r;}
    const DWORD attributes=GetFileAttributesW(path.c_str());
    if(attributes!=INVALID_FILE_ATTRIBUTES&&(attributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))){r.reason="destination-file-kind-invalid";return r;}
    if(attributes==INVALID_FILE_ATTRIBUTES){error=GetLastError();if(error!=ERROR_FILE_NOT_FOUND){r.state=StoreState::RetryableIo;r.win32Error=error;r.reason="destination-attributes-unavailable";return r;}}
    {fileDetail::Handle existing(CreateFileW(path.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
        if(existing.valid()){BY_HANDLE_FILE_INFORMATION info{};if(!fileDetail::info(existing.value,info,error)){r.win32Error=error;r.state=error==ERROR_INVALID_DATA?StoreState::Invalid:StoreState::RetryableIo;r.reason="destination-file-kind-invalid";return r;}}
        else {error=GetLastError();if(error!=ERROR_FILE_NOT_FOUND){r.state=StoreState::RetryableIo;r.win32Error=error;r.reason="destination-open-failed";return r;}}}
    static std::atomic<std::uint64_t> sequence{0};
    const auto rawTemp=path+L".native-refresh-writer.tmp."+std::to_wstring(GetTickCount64())+L"."+std::to_wstring(sequence.fetch_add(1,std::memory_order_relaxed));
    std::wstring temp;if(!fileDetail::normalize(rawTemp,temp)){r.reason="temporary-path-invalid";return r;}
    fileDetail::Handle h(CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_WRITE_THROUGH|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
    if(!h.valid()){r.state=StoreState::RetryableIo;r.win32Error=GetLastError();r.reason="temporary-create-failed";return r;}
    auto fail=[&](const char* why,DWORD e){h.close();DeleteFileW(temp.c_str());r.state=StoreState::RetryableIo;r.win32Error=e;r.reason=why;return r;};
    BY_HANDLE_FILE_INFORMATION temporaryInfo{};
    if(!fileDetail::info(h.value,temporaryInfo,error))return fail("temporary-file-kind-invalid",error);
    DWORD written=0;
    if(!WriteFile(h.value,bytes.data(),DWORD(bytes.size()),&written,nullptr)||written!=bytes.size())return fail("temporary-write-incomplete",GetLastError());
    if(!FlushFileBuffers(h.value))return fail("temporary-flush-failed",GetLastError());
    if(!h.close())return fail("temporary-close-failed",GetLastError());
    if(!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))return fail("atomic-replace-failed",GetLastError());
    r.state=StoreState::Committed;r.reason="writer-file-committed";return r;
}
enum class SessionErrorCode { None, Invalid, RetryableIo, TransformRejected };
struct SessionError {SessionErrorCode code=SessionErrorCode::None;DWORD win32Error=0;std::string reason;};
namespace sessionDetail {inline constexpr DWORD ownerShareMode=0;}
// All access to one Session must be serialized by the caller. The owner handle
// excludes other Session/process writers until destruction; no retry or worker.
class Session final {
public:
    static Session open(const std::wstring& explicitAbsolutePath,const std::string& machineBinding){return Session(explicitAbsolutePath,machineBinding);}
    ~Session()=default;
    Session(const Session&)=delete;Session& operator=(const Session&)=delete;
    Session(Session&&)=delete;Session& operator=(Session&&)=delete;
    bool ready()const noexcept{return owner_.valid()&&record_.has_value();}
    const SessionError& error()const noexcept{return error_;}
    const Record* record()const noexcept{return ready()?&*record_:nullptr;}
    LoadState openedFrom()const noexcept{return openedFrom_;}
    // Caller passes nullptr/INVALID_HANDLE_VALUE and owns/closes a successful
    // duplicate. An occupied output is preserved with ERROR_INVALID_PARAMETER.
    // This retains the same share=0 file object; it never releases our exclusion.
    // Other failures leave out invalid and report GetLastError; SessionError is
    // unchanged so this is allocation-free/noexcept. Inheritance is caller wiring.
    bool duplicateOwnerLeaseForChild(HANDLE& out) noexcept {
        if(out!=nullptr&&out!=INVALID_HANDLE_VALUE){SetLastError(ERROR_INVALID_PARAMETER);return false;}
        out=INVALID_HANDLE_VALUE;
        if(!ready()){SetLastError(ERROR_INVALID_HANDLE);return false;}
        HANDLE duplicate=INVALID_HANDLE_VALUE;
        if(!DuplicateHandle(GetCurrentProcess(),owner_.value,GetCurrentProcess(),
                            &duplicate,0,TRUE,DUPLICATE_SAME_ACCESS))return false;
        out=duplicate;return true;
    }
    bool reserve(std::uint64_t expectedRevision,const Attempt& reserved){
        return update([&](Record& candidate,std::string& error){return nativeRefreshWriter::reserve(*record_,expectedRevision,reserved,candidate,&error);});
    }
    bool markChild(std::uint64_t expectedRevision,const std::string& attemptId,const ProcessIdentity& child){
        return update([&](Record& candidate,std::string& error){return nativeRefreshWriter::markChild(*record_,expectedRevision,attemptId,child,candidate,&error);});
    }
    // Caller first obtains positive exact Job quiet/terminal proof, or verifies
    // the producer's inherited-lease protocol, reacquires this same share=0 owner,
    // and proves every recorded exact parent/child identity Dead. No lease
    // protocol is inferred for old records. Missing named Job remains UNKNOWN.
    // This method only persists the transition; it does not verify either proof.
    bool settle(std::uint64_t expectedRevision,const std::string& attemptId){
        return update([&](Record& candidate,std::string& error){return nativeRefreshWriter::settle(*record_,expectedRevision,attemptId,candidate,&error);});
    }
    // All visibility transitions share this exact exclusive owner and commit
    // path with helper history. These methods only persist caller decisions.
    bool reserveVisibility(std::uint64_t expectedRevision,const Visibility& allReserved){
        return update([&](Record& candidate,std::string& error){return nativeRefreshWriter::reserveVisibility(*record_,expectedRevision,allReserved,candidate,&error);});
    }
    bool reserveVisibilityPath(std::uint64_t expectedRevision,const std::string& transactionId,const std::string& instanceId){
        return update([&](Record& candidate,std::string& error){return nativeRefreshWriter::reserveVisibilityPath(*record_,expectedRevision,transactionId,instanceId,candidate,&error);});
    }
    bool markVisibilityMayHaveIssued(std::uint64_t expectedRevision,const std::string& transactionId,const std::string& instanceId){
        return update([&](Record& candidate,std::string& error){return nativeRefreshWriter::markVisibilityMayHaveIssued(*record_,expectedRevision,transactionId,instanceId,candidate,&error);});
    }
    // Caller has proved exact per-path configuration return before this call.
    bool settleVisibilityPath(std::uint64_t expectedRevision,const std::string& transactionId,const std::string& instanceId){
        return update([&](Record& candidate,std::string& error){return nativeRefreshWriter::settleVisibilityPath(*record_,expectedRevision,transactionId,instanceId,candidate,&error);});
    }
    bool completeVisibility(std::uint64_t expectedRevision,const std::string& transactionId){
        return update([&](Record& candidate,std::string& error){return nativeRefreshWriter::completeVisibility(*record_,expectedRevision,transactionId,candidate,&error);});
    }
    // Caller has proved the complete replacement Hosted ledger is durable.
    bool transferVisibility(std::uint64_t expectedRevision,const std::string& transactionId){
        return update([&](Record& candidate,std::string& error){return nativeRefreshWriter::transferVisibility(*record_,expectedRevision,transactionId,candidate,&error);});
    }
private:
    fileDetail::Handle owner_{INVALID_HANDLE_VALUE};
    std::wstring recordPath_;
    std::optional<Record> record_;
    LoadState openedFrom_=LoadState::Invalid;
    SessionError error_;
    bool setError(SessionErrorCode code,const char* reason,DWORD error=0){error_={code,error,reason};return false;}
    bool clearError(){error_={};return true;}
    Session(const std::wstring& rawPath,const std::string& machineBinding){
        std::wstring ownerPath;
        if(!fileDetail::normalize(rawPath,recordPath_)||!fileDetail::normalize(recordPath_+L".owner",ownerPath)||!detail::text(machineBinding,4096)){
            setError(SessionErrorCode::Invalid,"invalid-explicit-path-or-machine");return;
        }
        DWORD e=0;bool invalid=false;
        if(!fileDetail::parents(recordPath_,e,invalid)){setError(invalid?SessionErrorCode::Invalid:SessionErrorCode::RetryableIo,"unsafe-or-unavailable-parent",e);return;}
        const DWORD attributes=GetFileAttributesW(ownerPath.c_str());
        if(attributes!=INVALID_FILE_ATTRIBUTES&&(attributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))){setError(SessionErrorCode::Invalid,"owner-file-kind-invalid");return;}
        if(attributes==INVALID_FILE_ATTRIBUTES){e=GetLastError();if(e!=ERROR_FILE_NOT_FOUND){setError(SessionErrorCode::RetryableIo,"owner-attributes-unavailable",e);return;}}
        fileDetail::Handle h(CreateFileW(ownerPath.c_str(),GENERIC_READ|GENERIC_WRITE,sessionDetail::ownerShareMode,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
        if(!h.valid()){
            e=GetLastError();if(e!=ERROR_FILE_EXISTS&&e!=ERROR_ALREADY_EXISTS){setError(SessionErrorCode::RetryableIo,"owner-create-or-lock-failed",e);return;}
            h.value=CreateFileW(ownerPath.c_str(),GENERIC_READ|GENERIC_WRITE,sessionDetail::ownerShareMode,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
            if(!h.valid()){setError(SessionErrorCode::RetryableIo,"owner-open-or-lock-failed",GetLastError());return;}
        }
        // Empty from CREATE_NEW, no initialization bytes: a crash cannot strand
        // a half-initialized sentinel. Never truncate, replace or delete it.
        BY_HANDLE_FILE_INFORMATION before{},after{};
        if(!fileDetail::info(h.value,before,e)){setError(e==ERROR_INVALID_DATA?SessionErrorCode::Invalid:SessionErrorCode::RetryableIo,"owner-file-kind-or-info-invalid",e);return;}
        if(before.nFileSizeHigh||before.nFileSizeLow!=0){setError(SessionErrorCode::Invalid,"owner-file-not-empty");return;}
        if(!fileDetail::info(h.value,after,e)||!fileDetail::sameInfo(before,after)){setError(SessionErrorCode::RetryableIo,"owner-snapshot-changed",e);return;}
        const auto loaded=nativeRefreshWriter::load(recordPath_,machineBinding);openedFrom_=loaded.state;
        if(loaded.state!=LoadState::Missing&&loaded.state!=LoadState::Valid){setError(loaded.state==LoadState::Invalid?SessionErrorCode::Invalid:SessionErrorCode::RetryableIo,loaded.reason.c_str(),loaded.win32Error);return;}
        if(loaded.state==LoadState::Missing){Record initial;initial.machineBinding=machineBinding;record_=std::move(initial);}
        else record_=loaded.record;
        owner_.value=h.value;h.value=INVALID_HANDLE_VALUE;
    }
    template<class Transform> bool update(Transform&& transform){
        if(!ready())return false;
        Record candidate;std::string reason;
        if(!transform(candidate,reason))return setError(SessionErrorCode::TransformRejected,reason.c_str());
        if(candidate.revision==record_->revision)return clearError();
        const auto saved=nativeRefreshWriter::store(recordPath_,candidate);
        if(!saved.committed())return setError(saved.state==StoreState::Invalid?SessionErrorCode::Invalid:SessionErrorCode::RetryableIo,saved.reason.c_str(),saved.win32Error);
        record_=std::move(candidate);return clearError();
    }
};
} // namespace ymcc::nativeRefreshWriter
#endif