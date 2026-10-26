#pragma once
#include "native_refresh_writer_store.h"
#include "virtual_target_return_process.h"
#include "hidhide_cli_job_guard.h"
#include <atomic>
#include <memory>

// Writer fencing only. No source selection, node operation, helper creation,
// wait/retry thread or controller-success claim. The existing lifecycle owns
// CreateProcess and its exact root HANDLE. It serializes this scope as well.
namespace ymcc::nativeRefreshWriter {
class LaunchScope final {
    std::shared_ptr<Session> session_;
    hidHideCli::JobGuard job_;
    std::optional<Attempt> attempt_;
    HANDLE inheritedLease_=INVALID_HANDLE_VALUE;
    HANDLE root_=nullptr; // borrowed; the native foreground lease is sole owner
    bool reserved_=false,creationReturned_=false,settled_=false;
    DWORD error_=ERROR_SUCCESS;
    std::string reason_;
    bool reject(const char* reason,DWORD error=ERROR_SUCCESS) {
        reason_=reason;error_=error;return false;
    }
    void closeLaunchLease() noexcept {
        if(inheritedLease_!=INVALID_HANDLE_VALUE){CloseHandle(inheritedLease_);inheritedLease_=INVALID_HANDLE_VALUE;}
    }
    bool settlePrevious() { return reconcilePrevious(*session_,error_,reason_); }

    void prepareAttempt(const std::string& root,const std::string& target,const std::vector<std::string>& locations) {
        if(!session_){reject("native-writer-shared-session-missing",ERROR_INVALID_HANDLE);return;}

        if(!session_->ready()){reject(session_->error().reason.c_str(),session_->error().win32Error);return;}
        if(!settlePrevious())return;
        const auto parent=targetReturn::process::capture(GetCurrentProcess(),&error_);
        if(!parent){reject("native-writer-parent-identity-unavailable",error_);return;}
        static std::atomic<std::uint64_t> sequence{0};
        const auto id=std::to_string(parent->pid)+"-"+parent->creationTime+"-"+
            std::to_string(GetTickCount64())+"-"+std::to_string(sequence.fetch_add(1,std::memory_order_relaxed));
        Attempt attempt{id,*parent,std::nullopt,std::string(jobPrefix)+id,root,target,locations};
        if(!detail::attempt(attempt)){reject("native-writer-attempt-invalid",ERROR_INVALID_DATA);return;}
        if(!session_->duplicateOwnerLeaseForChild(inheritedLease_)){
            reject("native-writer-inherited-owner-lease-unavailable",GetLastError());return;
        }
        attempt_=std::move(attempt);
    }

public:
    // Reuse the same positive inherited-owner fence for cold visibility return.
    // This reconciles writer quiescence only; it grants no device/unhide proof.
    static bool reconcilePrevious(Session& session,DWORD& errorOut,std::string& reason) {
        errorOut=ERROR_SUCCESS;reason.clear();
        const auto reject=[&](const char* why,DWORD code=ERROR_SUCCESS){reason=why;errorOut=code;return false;};
        if(!session.ready())return reject(session.error().reason.c_str(),session.error().win32Error);
        const auto* record=session.record();
        if(!record->active)return true;
        const auto previous=*record->active;
        using namespace targetReturn::process;
        // Successful Session.open acquired the SAME stable share=0 owner file.
        // Every producer of this dedicated sidecar passes a duplicate of that
        // file object via HANDLE_LIST before the child's first instruction.
        // Acquiring it after exact parent/child death is the positive writer
        // fence. A missing named Job is NOT a quiet observation on its own.
        if(probeRecordedOwner(previous.parent).state!=State::Dead)
            return reject("previous-native-writer-parent-alive-or-unknown");
        if(previous.child && probeRecordedOwner(*previous.child).state!=State::Dead)
            return reject("previous-native-writer-child-alive-or-unknown");
        const auto name=std::wstring(previous.jobName.begin(),previous.jobName.end());
        HANDLE observed=OpenJobObjectW(JOB_OBJECT_QUERY,FALSE,name.c_str());
        if(observed){
            JOBOBJECT_BASIC_ACCOUNTING_INFORMATION info{};
            const bool known=QueryInformationJobObject(observed,JobObjectBasicAccountingInformation,&info,sizeof(info),nullptr)!=FALSE;
            const DWORD error=known?ERROR_SUCCESS:GetLastError();CloseHandle(observed);
            if(!known || info.ActiveProcesses)return reject("previous-native-writer-job-active-or-unknown",error);
        } else if(GetLastError()!=ERROR_FILE_NOT_FOUND) {
            return reject("previous-native-writer-job-query-unknown",GetLastError());
        }
        if(!session.settle(record->revision,previous.attemptId))
            return reject(session.error().reason.c_str(),session.error().win32Error);
        return true;
    }
    LaunchScope(const std::wstring& path,const std::string& machine,
                const std::string& root,const std::string& target,const std::vector<std::string>& locations) {
        session_.reset(new Session(Session::open(path,machine)));prepareAttempt(root,target,locations);
    }
    // Native visibility and its helper share one existing owner-file lease.
    // Opening a second Session here would conflict with our own exclusion.
    LaunchScope(std::shared_ptr<Session> session,const std::string& root,
                const std::string& target,const std::vector<std::string>& locations)
        : session_(std::move(session)) { prepareAttempt(root,target,locations); }

    ~LaunchScope(){closeLaunchLease();} // tracked Job is nonkilling; child retains the file exclusion
    LaunchScope(const LaunchScope&)=delete;
    LaunchScope& operator=(const LaunchScope&)=delete;
    bool ready() const noexcept {return session_ && session_->ready() && attempt_.has_value() && inheritedLease_!=INVALID_HANDLE_VALUE;}
    bool reserved() const noexcept {return reserved_;}
    bool settled() const noexcept {return settled_;}
    DWORD error() const noexcept {return error_;}
    const std::string& reason() const noexcept {return reason_;}
    const Attempt* attempt() const noexcept {return attempt_?&*attempt_:nullptr;}
    hidHideCli::JobGuard& job() noexcept {return job_;}
    HANDLE inheritedLease() const noexcept {return inheritedLease_;}
    std::wstring jobName() const {return attempt_?std::wstring(attempt_->jobName.begin(),attempt_->jobName.end()):std::wstring{};}
    // The caller has prepared ONE JOB_LIST + exact HANDLE_LIST containing our
    // lease. This commit MUST precede its sole CreateProcess; there is no fallback.
    bool reserve() {
        if(!ready() || reserved_ || !job_.ready() || !job_.idle() || !job_.tracksExactLease(jobName(),inheritedLease_))
            return reject("native-writer-launch-not-prepared",ERROR_INVALID_STATE);
        if(!session_->reserve(session_->record()->revision,*attempt_))
            return reject(session_->error().reason.c_str(),session_->error().win32Error);
        reserved_=true;return true;
    }
    // Called exactly once after CreateProcess returns. The temporary parent
    // duplicate closes only then; a successful child already has its own exact
    // inherited duplicate, including when the parent dies before markChild.
    bool creationReturned(HANDLE exactRoot) {
        if(!reserved_ || creationReturned_)return reject("native-writer-create-receipt-invalid",ERROR_INVALID_STATE);
        creationReturned_=true;root_=exactRoot;closeLaunchLease();
        if(!root_)return true;
        const auto child=targetReturn::process::capture(root_,&error_);
        if(!child)return reject("native-writer-child-identity-unavailable",error_);
        if(!session_->markChild(session_->record()->revision,attempt_->attemptId,*child))
            return reject(session_->error().reason.c_str(),session_->error().win32Error);
        attempt_->child=*child;return true;
    }
    // Root terminal alone is insufficient. Hold the original process and
    // pending flag until its exact job is idle AND settlement is durable.
    bool retire(HANDLE exactRoot) {
        if(settled_)return true;
        if(!reserved_ || !creationReturned_ || exactRoot!=root_)
            return reject("native-writer-retirement-receipt-invalid",ERROR_INVALID_STATE);
        if(root_ && WaitForSingleObject(root_,0)!=WAIT_OBJECT_0)
            return reject("native-writer-root-not-terminal");
        if(!job_.idle())return reject("native-writer-job-not-idle");
        if(!session_->settle(session_->record()->revision,attempt_->attemptId))
            return reject(session_->error().reason.c_str(),session_->error().win32Error);
        settled_=true;reason_.clear();error_=ERROR_SUCCESS;return true;
    }
};
} // namespace ymcc::nativeRefreshWriter
