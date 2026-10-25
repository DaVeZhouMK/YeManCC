#pragma once
#include "hidhide_cli_writer_store.h"
#include "virtual_target_return_process.h"
#include <memory>

// Called under the existing g_hidHideMx. This is an exact native CLI-writer
// fence, not another virtual-controller state machine or target return owner.
static std::string inputHostTargetReturnMachineBinding();
static std::wstring hidHideCliWriterRecordPath() {
    const auto directory=fspath::path(yemancc_data_dir())/L"controller-return";
    std::error_code error;fspath::create_directories(directory,error);
    return (directory/L"hidhide-cli-writer.json").wstring();
}
static std::string hidHideCliWriterMachineBinding() {
    return inputHostTargetReturnMachineBinding();
}
static std::optional<ymcc::targetReturn::ProcessIdentity> hidHideCliWriterCapture(HANDLE process) {
    return ymcc::targetReturn::process::capture(process);
}
static ymcc::targetReturn::process::Probe hidHideCliWriterProbe(
        const ymcc::targetReturn::ProcessIdentity& identity) {
    return ymcc::targetReturn::process::probeRecordedOwner(identity);
}
namespace ymcc::hidHideCliWriterRuntime {
namespace writer=ymcc::hidhideCliWriter;
namespace process=ymcc::targetReturn::process;
enum class JobState { Absent, Idle, Active, Unknown };
}
static ymcc::hidHideCliWriterRuntime::JobState hidHideCliWriterProbeJob(const std::string& name) {
    using State=ymcc::hidHideCliWriterRuntime::JobState;
    const auto wide=U2W(name);
    HANDLE handle=OpenJobObjectW(JOB_OBJECT_QUERY,FALSE,wide.c_str());
    if(!handle)return GetLastError()==ERROR_FILE_NOT_FOUND?State::Absent:State::Unknown;
    JOBOBJECT_BASIC_ACCOUNTING_INFORMATION info{};
    const bool known=QueryInformationJobObject(handle,JobObjectBasicAccountingInformation,&info,sizeof(info),nullptr)!=FALSE;
    CloseHandle(handle);
    return !known?State::Unknown:info.ActiveProcesses?State::Active:State::Idle;
}
namespace ymcc::hidHideCliWriterRuntime {
static void note(const char* phase,const char* reason,const writer::Attempt* attempt=nullptr) noexcept {
    try {appendNativeLifecycleLog("hidhide-cli-writer",{
        {"phase",phase},{"reason",reason},{"recordPath",W2U(hidHideCliWriterRecordPath())},
        {"attemptId",attempt?attempt->attemptId:std::string{}},
        {"parentPid",attempt?attempt->parent.pid:0},{"parentBirth",attempt?attempt->parent.creationTime:std::string{}},
        {"childPid",attempt&&attempt->child?attempt->child->pid:0},
        {"childBirth",attempt&&attempt->child?attempt->child->creationTime:std::string{}},
        {"jobName",attempt?attempt->jobName:std::string{}},
        {"scope","CLI writer only; no target/physical-return/game proof"}});
    }catch(...){}
}
static std::unique_ptr<writer::Session> open() {
    try {
        auto result=std::unique_ptr<writer::Session>(new writer::Session(
            writer::Session::open(hidHideCliWriterRecordPath(),hidHideCliWriterMachineBinding())));
        if(!result->ready()){note("deferred",result->error().reason.c_str());return {};}
        return result;
    }catch(...){note("deferred","writer-record-open-exception");return {};}
}
// A Reserved record precedes process creation; MayRun is committed BEFORE any
// ResumeThread. Old native death never proves its child/subtree has finished.
// No old process or job is terminated here. Existing calls observe only once.
static bool clearSettled(writer::Session& session) {
    if(!session.ready())return false;
    const auto* record=session.record();if(!record->active)return true;
    const auto attempt=*record->active;
    const auto current=hidHideCliWriterCapture(GetCurrentProcess());
    const bool same=current&&ymcc::targetReturn::detail::sameProcess(*current,attempt.parent);
    if(!same&&hidHideCliWriterProbe(attempt.parent).state!=process::State::Dead) {
        note("deferred","recorded-native-owner-alive-or-unknown",&attempt);return false;
    }
    if(attempt.child&&hidHideCliWriterProbe(*attempt.child).state!=process::State::Dead) {
        note("deferred","recorded-CLI-child-alive-or-unknown",&attempt);return false;
    }
    const auto job=hidHideCliWriterProbeJob(attempt.jobName);
    if(job!=JobState::Absent&&job!=JobState::Idle) {
        note("deferred","recorded-CLI-subtree-active-or-unknown",&attempt);return false;
    }
    if(!session.settle(record->revision,attempt.attemptId)) {
        note("deferred",session.error().reason.c_str(),&attempt);return false;
    }
    note("settled","exact-native-child-and-job-writer-quiescent",&attempt);return true;
}
static bool settleOwned(writer::Session& session,const std::string& id) {
    const auto* record=session.record();if(!record)return false;
    if(!record->active)return true;
    if(record->active->attemptId!=id)return false;
    const auto attempt=*record->active;
    if(!session.settle(record->revision,id)){note("deferred",session.error().reason.c_str(),&attempt);return false;}
    note("settled","exact-owned-CLI-root-and-subtree-exited",&attempt);return true;
}
static bool quiescent() {
    auto session=open();return session&&clearSettled(*session);
}
} // namespace ymcc::hidHideCliWriterRuntime
