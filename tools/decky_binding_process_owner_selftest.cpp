#define NOMINMAX
#include "../native/decky_binding_process_owner.h"
#include <processsnapshot.h>
#include <json.hpp>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <set>
#include <thread>
using json = nlohmann::json;
namespace db = ymcc::deckybinding;
namespace {
constexpr DWORD deadlineMs = 3200;
struct WinHandle {
    HANDLE h = nullptr;
    explicit WinHandle(HANDLE value = nullptr) : h(value) {}
    ~WinHandle() { if (h && h != INVALID_HANDLE_VALUE) CloseHandle(h); }
    WinHandle(const WinHandle&) = delete; WinHandle& operator=(const WinHandle&) = delete;
};
void need(bool okay, const char* text) { if (!okay) throw std::runtime_error(text); }
std::vector<std::uint8_t> bytes(const std::string& text) { return {text.begin(), text.end()}; }
json snap(const db::Snapshot& s) {
    return {{"generation",s.generation},{"pid",s.processId},{"started",s.started},
        {"stopRequested",s.stopRequested},{"childExited",s.childExited},{"exitCode",s.exitCode},
        {"handles",s.handles},{"activeCalls",s.activeCalls},{"pendingIo",s.pendingIo},
        {"jobActiveProcesses",s.jobActiveProcesses},{"jobLimitFlags",s.jobLimitFlags},
        {"jobTerminationAttempted",s.jobTerminationAttempted},{"fullyDestroyed",s.fullyDestroyed},
        {"cleanupError",s.cleanupError}};
}
json result(const db::Result& r) {
    return {{"code",static_cast<int>(r.code)},{"error",r.error},{"fullyDestroyed",r.fullyDestroyed},
        {"ioPending",r.ioPending},{"generation",r.generation},{"payloadBytes",r.payload.size()}};
}
DWORD handleCount() {
    DWORD count = 0; need(GetProcessHandleCount(GetCurrentProcess(), &count) != FALSE,"GetProcessHandleCount"); return count;
}
std::set<std::uintptr_t> handles() {
    HPSS snapshot = nullptr; HPSSWALK marker = nullptr;
    need(PssCaptureSnapshot(GetCurrentProcess(), PSS_CAPTURE_HANDLES, 0, &snapshot) == ERROR_SUCCESS,"PssCaptureSnapshot");
    const auto markerResult = PssWalkMarkerCreate(nullptr, &marker);
    if (markerResult != ERROR_SUCCESS) { PssFreeSnapshot(GetCurrentProcess(),snapshot); need(false,"PssWalkMarkerCreate"); }
    std::set<std::uintptr_t> out;
    PSS_HANDLE_ENTRY entry{}; DWORD status = ERROR_SUCCESS;
    while ((status = PssWalkSnapshot(snapshot,PSS_WALK_HANDLES,marker,&entry,sizeof(entry))) == ERROR_SUCCESS)
        out.insert(reinterpret_cast<std::uintptr_t>(entry.Handle));
    PssWalkMarkerFree(marker); PssFreeSnapshot(GetCurrentProcess(),snapshot);
    need(status == ERROR_NO_MORE_ITEMS,"PssWalkSnapshot"); return out;
}
void waitThread(std::thread& thread) {
    // join is performed only after the real native thread handle is signalled.
    if (WaitForSingleObject(thread.native_handle(), 3000) != WAIT_OBJECT_0) {
        std::cerr << "fixture thread failed bounded drain; guardian will close this test job\n";
        ExitProcess(91);
    }
    thread.join();
}
struct Deadline {
    WinHandle timer{CreateWaitableTimerW(nullptr,TRUE,nullptr)};
    ULONGLONG begun = GetTickCount64();
    Deadline() {
        need(timer.h != nullptr,"deadline timer");
        LARGE_INTEGER due{}; due.QuadPart = -static_cast<LONGLONG>(deadlineMs) * 10000;
        need(SetWaitableTimer(timer.h,&due,0,nullptr,nullptr,FALSE) != FALSE,"set deadline timer");
    }
    void wait() { need(WaitForSingleObject(timer.h,deadlineMs + 500) == WAIT_OBJECT_0,"deadline wait"); }
};
// Negative-source harness only: actual Win32 resources, a distinct fixture image,
// and the real ChildChannel. Normal cases continue to use the public Owner.
struct FixtureProcess {
    std::unique_ptr<db::detail::State> state = std::make_unique<db::detail::State>();
    FixtureProcess() {
        auto& s = *state;
        s.changed.value = CreateEventW(nullptr,FALSE,FALSE,nullptr);
        s.job.value = CreateJobObjectW(nullptr,nullptr);
        s.port.value = CreateIoCompletionPort(INVALID_HANDLE_VALUE,nullptr,0,1);
        need(s.changed.value && s.job.value && s.port.value,"source harness resources");
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        need(SetInformationJobObject(s.job.value,JobObjectExtendedLimitInformation,&limits,sizeof(limits)) != FALSE,"source harness job limits");
        JOBOBJECT_ASSOCIATE_COMPLETION_PORT completion{};
        completion.CompletionKey = &s; completion.CompletionPort = s.port.value;
        need(SetInformationJobObject(s.job.value,JobObjectAssociateCompletionPortInformation,&completion,sizeof(completion)) != FALSE,"source harness job notifications");
    }
    ~FixtureProcess() noexcept {
        if (!stop().fullyDestroyed) db::detail::retain(state.release());
    }
    db::Result stop() noexcept { return db::detail::stop(*state,0,1500,db::StopPolicy::terminateAfterGrace); }
    void createSuspended(const std::wstring& image,std::wstring command,bool inherit) {
        auto& s = *state;
        HANDLE inherited[] = {s.peer.value,s.cancel.value};
        db::detail::Attributes attributes;
        if (inherit) need(attributes.initialize(inherited),"source harness exact HANDLE_LIST");
        STARTUPINFOEXW startup{};
        startup.StartupInfo.cb = sizeof(startup);
        startup.StartupInfo.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
        startup.StartupInfo.wShowWindow = SW_HIDE;
        startup.StartupInfo.hStdInput = startup.StartupInfo.hStdOutput = startup.StartupInfo.hStdError = INVALID_HANDLE_VALUE;
        startup.lpAttributeList = inherit ? attributes.list : nullptr;
        PROCESS_INFORMATION info{};
        need(CreateProcessW(image.c_str(),command.data(),nullptr,nullptr,inherit ? TRUE : FALSE,
            CREATE_SUSPENDED | CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT,
            nullptr,nullptr,&startup.StartupInfo,&info) != FALSE,"source harness CreateProcess");
        s.process.value = info.hProcess; s.thread.value = info.hThread;
        s.pid = info.dwProcessId; s.childExited = false;
        need(AssignProcessToJobObject(s.job.value,s.process.value) != FALSE,"source harness assign before resume");
        s.assigned = true;
    }
    void resume() {
        auto& s = *state;
        need(s.peer.close(s.cleanupError),"source harness parent peer close");
        need(ResumeThread(s.thread.value) != MAXDWORD,"source harness resume");
        need(s.thread.close(s.cleanupError),"source harness thread close");
        s.started = true;
    }
};
json sourceServer(const std::wstring& target) {
    FixtureProcess child; auto& s = *child.state;
    SECURITY_ATTRIBUTES inherit{sizeof(SECURITY_ATTRIBUTES),nullptr,TRUE};
    s.cancel.value = CreateEventW(&inherit,TRUE,FALSE,nullptr);
    const auto name = L"\\\\.\\pipe\\ymcc-source-negative-" + std::to_wstring(GetCurrentProcessId());
    s.pipe.value = CreateNamedPipeW(name.c_str(),PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,1,4096,4096,0,nullptr);
    if (s.pipe.value == INVALID_HANDLE_VALUE) s.pipe.value = nullptr;
    need(s.cancel.value && s.pipe.value,"foreign source pipe/event");
    s.peer.value = CreateFileW(name.c_str(),GENERIC_READ | GENERIC_WRITE,0,&inherit,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr);
    if (s.peer.value == INVALID_HANDLE_VALUE) s.peer.value = nullptr;
    need(s.peer.value != nullptr,"foreign source actual client endpoint");
    need(ConnectNamedPipe(s.pipe.value,nullptr) || GetLastError() == ERROR_PIPE_CONNECTED,"foreign source connected pipe");
    DWORD pipeFlags = 0, cancelFlags = 0, pipeInfo = 0;
    need(GetHandleInformation(s.peer.value,&pipeFlags) && GetHandleInformation(s.cancel.value,&cancelFlags) &&
        GetNamedPipeInfo(s.peer.value,&pipeInfo,nullptr,nullptr,nullptr),"foreign source handle audit");
    const bool legal = (pipeFlags & HANDLE_FLAG_INHERIT) && (cancelFlags & HANDLE_FLAG_INHERIT) &&
        GetFileType(s.peer.value) == FILE_TYPE_PIPE && !(pipeInfo & PIPE_SERVER_END);
    // Queue one small, valid frame while both endpoints exist; no pending fixture I/O.
    const auto payload = bytes("cooperate"); const auto size = static_cast<std::uint32_t>(payload.size());
    std::vector<std::uint8_t> frame(sizeof(size) + payload.size());
    std::memcpy(frame.data(),&size,sizeof(size)); std::memcpy(frame.data() + sizeof(size),payload.data(),payload.size());
    DWORD written = 0;
    need(WriteFile(s.pipe.value,frame.data(),static_cast<DWORD>(frame.size()),&written,nullptr) && written == frame.size(),"foreign source valid queued frame");
    const auto command = L"\"" + target + L"\" " + db::childSwitch + L" " +
        std::to_wstring(reinterpret_cast<std::uintptr_t>(s.peer.value)) + L" " +
        std::to_wstring(reinterpret_cast<std::uintptr_t>(s.cancel.value));
    child.createSuspended(target,command,true);
    DWORD serverSession = 0, workerSession = 0; BOOL inJob = FALSE;
    need(ProcessIdToSessionId(GetCurrentProcessId(),&serverSession) && ProcessIdToSessionId(s.pid,&workerSession) &&
        IsProcessInJob(s.process.value,s.job.value,&inJob),"foreign source session/job audit");
    const auto sourceImage = db::detail::executable();
    std::wstring targetImage(32768,L'\0'); DWORD length = static_cast<DWORD>(targetImage.size());
    need(QueryFullProcessImageNameW(s.process.value,0,targetImage.data(),&length) != FALSE,"foreign source target image");
    targetImage.resize(length);
    const bool sameImage = CompareStringOrdinal(sourceImage.data(),static_cast<int>(sourceImage.size()),
        targetImage.data(),static_cast<int>(targetImage.size()),TRUE) == CSTR_EQUAL;
    child.resume();
    DWORD waited = WaitForSingleObject(s.process.value,1500);
    DWORD available = 0;
    PeekNamedPipe(s.pipe.value,nullptr,0,nullptr,&available,nullptr);
    if (waited == WAIT_TIMEOUT) {
        need(SetEvent(s.cancel.value) != FALSE,"foreign harness cooperative cleanup");
        waited = WaitForSingleObject(s.process.value,1500);
    }
    DWORD exitCode = STILL_ACTIVE;
    need(GetExitCodeProcess(s.process.value,&exitCode) != FALSE,"foreign target exit code");
    const auto cleanup = child.stop();
    const bool rejected = waited == WAIT_OBJECT_0 && exitCode == ERROR_INVALID_PARAMETER;
    return {{"legalInheritedClientPipeAndCancelEvent",legal},{"sameSession",serverSession == workerSession},
        {"serverSession",serverSession},{"workerSession",workerSession},{"workerInOwnedJob",inJob != FALSE},
        {"serverImage",sourceImage},{"workerImage",targetImage},{"sameImage",sameImage},
        {"workerExited",waited == WAIT_OBJECT_0},{"workerExitCode",exitCode},{"workerResponseBytes",available},
        {"sourceRejected",rejected},{"ordinaryFallbackTaken",exitCode == 88},{"cleanup",result(cleanup)}};
}
std::vector<std::uintptr_t> initialChildHandles, initialInheritedHandles, initialStdHandles;
DWORD handlesBeforePss = 0, handlesAfterPss = 0;
int worker(db::ChildChannel& channel, const wchar_t* pipeArgument) {
    const auto first = channel.readFrame(5000);
    if (!first) return 20;
    const std::string operation(first.payload.begin(),first.payload.end());
    if (operation == "natural") return 0;
    if (operation == "oversize-incoming") {
        // Deliberately malformed peer frame; production helper is used for the handshake.
        const auto raw = reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(_wcstoui64(pipeArgument,nullptr,10)));
        auto* io = new db::detail::Io(true);
        io->buffer.resize(sizeof(std::uint32_t));
        const std::uint32_t oversized = db::maxFrameBytes + 1;
        std::memcpy(io->buffer.data(),&oversized,sizeof(oversized));
        DWORD sent = 0;
        const BOOL immediate = WriteFile(raw,io->buffer.data(),sizeof(oversized),&sent,&io->overlapped);
        if (!immediate && GetLastError() == ERROR_IO_PENDING) {
            if (WaitForSingleObject(io->event.value,1500) != WAIT_OBJECT_0) return 24; // Heap state retained until process exit.
            if (!GetOverlappedResult(raw,&io->overlapped,&sent,FALSE)) return 25;
        } else if (!immediate) return 26;
        delete io;
        channel.waitCancelled(5000); return 0;
    }
    WinHandle cancelCopy;
    if (operation == "cooperate") {
        if (!DuplicateHandle(GetCurrentProcess(),channel.cancelEvent(),GetCurrentProcess(),
            &cancelCopy.h,SYNCHRONIZE,FALSE,0)) return 30;
    }
    WinHandle descendantProcess, descendantThread;
    DWORD descendantPid = 0, breakawayError = 0;
    if (operation == "descendant") {
        const auto image = db::detail::executable();
        std::wstring command = L"\"" + image + L"\" --fixture-owned-descendant";
        STARTUPINFOW startup{}; startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES; startup.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION escaped{};
        if (CreateProcessW(image.c_str(),command.data(),nullptr,nullptr,FALSE,
            CREATE_SUSPENDED | CREATE_NO_WINDOW | CREATE_BREAKAWAY_FROM_JOB,
            nullptr,nullptr,&startup,&escaped)) {
            WinHandle process{escaped.hProcess}, thread{escaped.hThread};
            TerminateProcess(process.h,93); WaitForSingleObject(process.h,1500);
            return 31; // Unexpectedly permitted escape is a failure, never left running.
        }
        breakawayError = GetLastError();
        PROCESS_INFORMATION child{};
        if (!CreateProcessW(image.c_str(),command.data(),nullptr,nullptr,FALSE,
            CREATE_SUSPENDED | CREATE_NO_WINDOW,nullptr,nullptr,&startup,&child)) return 32;
        descendantProcess.h = child.hProcess; descendantThread.h = child.hThread; descendantPid = child.dwProcessId;
        if (ResumeThread(descendantThread.h) == MAXDWORD) return 33;
    }
    DWORD sourcePid = 0, sourceSession = 0, currentSession = 0;
    if (!GetNamedPipeServerProcessId(channel.pipeHandle(),&sourcePid) ||
        !GetNamedPipeServerSessionId(channel.pipeHandle(),&sourceSession) ||
        !ProcessIdToSessionId(GetCurrentProcessId(),&currentSession)) return 34;
    BOOL inJob = FALSE; IsProcessInJob(GetCurrentProcess(),nullptr,&inJob);
    json ready = {{"ready",true},{"inJob",inJob != FALSE},{"initialHandles",initialChildHandles},
        {"cancelHandle",reinterpret_cast<std::uintptr_t>(channel.cancelEvent())},
        {"inheritedHandles",initialInheritedHandles},{"cancelDuplicated",cancelCopy.h != nullptr},
        {"descendantPid",descendantPid},{"breakawayError",breakawayError},{"sourceServerPid",sourcePid},
        {"sourceSession",sourceSession},{"workerSession",currentSession},
        {"handlesBeforePss",handlesBeforePss},{"handlesAfterPss",handlesAfterPss},{"initialStdHandles",initialStdHandles}};
    if (!channel.writeFrame(bytes(ready.dump()),1500)) return 21;
    if (operation == "stall" || operation == "descendant") {
        WinHandle blocker{CreateEventW(nullptr,TRUE,FALSE,nullptr)};
        // Intentional blocking operation that never observes the inherited cancel event.
        WaitForSingleObject(blocker.h,60000); return 22;
    }
    if (operation == "cooperate") {
        const DWORD waited = channel.waitCancelled(10000);
        return waited == WAIT_OBJECT_0 && channel.cancelled() &&
            WaitForSingleObject(cancelCopy.h,0) == WAIT_OBJECT_0 ? 0 : 23;
    }
    if (operation != "echo") return 27;
    for (;;) {
        auto request = channel.readFrame(10000);
        if (!request) return channel.cancelled() ? 0 : 28;
        if (!channel.writeFrame(request.payload,5000)) return channel.cancelled() ? 0 : 29;
    }
}
json begin(db::Owner& owner,const char* operation) {
    need(static_cast<bool>(owner.start()),"owner start");
    need(static_cast<bool>(owner.writeFrame(bytes(operation),1500)),"worker command frame");
    auto ready = owner.readFrame(1500);
    need(static_cast<bool>(ready),"worker ready frame");
    return json::parse(ready.payload.begin(),ready.payload.end());
}
WinHandle observer(const db::Owner& owner) {
    return WinHandle(OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,FALSE,owner.snapshot().processId));
}
void cleaned(db::Owner& owner,HANDLE process) {
    const auto s = owner.snapshot();
    need(s.fullyDestroyed && s.handles == 0 && s.activeCalls == 0 && s.pendingIo == 0,"owner resources actually drained");
    need(process && WaitForSingleObject(process,0) == WAIT_OBJECT_0,"independent real child exit");
}
json blocked(bool writing,bool force) {
    db::Owner owner;
    begin(owner,"stall");
    auto process = observer(owner); need(process.h != nullptr,"owned child observer");
    db::Result io;
    const std::vector<std::uint8_t> big(db::maxFrameBytes,0x5a);
    std::thread thread([&] { io = writing ? owner.writeFrame(big,10000) : owner.readFrame(10000); });
    const bool pending = owner.waitForPendingIo(1500);
    const auto pendingState = owner.snapshot();
    Deadline clock;
    const auto stopped = owner.stop(150,force ? 1500 : 0,
        force ? db::StopPolicy::terminateAfterGrace : db::StopPolicy::cooperativeOnly);
    const auto before = owner.snapshot();
    const auto elapsedBeforeDeadline = GetTickCount64() - clock.begun;
    const bool childExitedBefore = WaitForSingleObject(process.h,0) == WAIT_OBJECT_0;
    waitThread(thread);
    clock.wait();
    const auto atDeadline = owner.snapshot();
    const bool childAtDeadline = WaitForSingleObject(process.h,0) == WAIT_TIMEOUT;
    const auto kernelHandlesAtDeadline = handleCount();
    const auto finalStop = owner.stop(0,1500);
    cleaned(owner,process.h);
    need(pending && pendingState.pendingIo > 0,"actual pending IO before stop");
    need(io.code == db::Code::cancelled || io.code == db::Code::stopped,"pending IO awakened by stop");
    if (force) {
        need(stopped.fullyDestroyed && before.fullyDestroyed && childExitedBefore,"forced cleanup before deadline");
        need(elapsedBeforeDeadline < deadlineMs && !childAtDeadline,"3200ms positive deadline");
        need(before.jobTerminationAttempted,"exact owned job fallback used");
    } else {
        need(!stopped.fullyDestroyed && !before.fullyDestroyed && childAtDeadline,"negative control detects surviving child");
        need(atDeadline.handles > 0 && !atDeadline.childExited && !atDeadline.jobTerminationAttempted,"negative resources retained honestly");
    }
    return {{"pendingBefore",snap(pendingState)},{"stopResult",result(stopped)},
        {"stateBeforeDeadline",snap(before)},{"observedBeforeDeadlineMs",elapsedBeforeDeadline},
        {"childExitedBeforeDeadline",childExitedBefore},{"deadlineMs",deadlineMs},
        {"stateAtDeadline",snap(atDeadline)},{"childAliveAtDeadline",childAtDeadline},
        {"parentKernelHandlesAtDeadline",kernelHandlesAtDeadline},
        {"ioResult",result(io)},{"negativeControl",!force},{"negativeDetected",!force && childAtDeadline},
        {"finalCleanup",result(finalStop)},{"finalState",snap(owner.snapshot())}};
}
} // namespace
int wmain(int argc,wchar_t** argv) {
    // This is deliberately the first fixture action; malformed worker mode never runs parent tests.
    const auto classification = db::ChildChannel::classify(argc,argv);
    if (classification != db::ChildMode::ordinary) {
        if (classification == db::ChildMode::invalid) return ERROR_INVALID_PARAMETER;
        handlesBeforePss = handleCount();
        for (const DWORD kind : {STD_INPUT_HANDLE,STD_OUTPUT_HANDLE,STD_ERROR_HANDLE})
            initialStdHandles.push_back(reinterpret_cast<std::uintptr_t>(GetStdHandle(kind)));
        const auto captured = handles(); initialChildHandles.assign(captured.begin(),captured.end());
        handlesAfterPss = handleCount();
        for (const auto value : initialChildHandles) {
            DWORD flags = 0;
            if (GetHandleInformation(reinterpret_cast<HANDLE>(value),&flags) && (flags & HANDLE_FLAG_INHERIT))
                initialInheritedHandles.push_back(value);
        }
        db::ChildChannel channel;
        int workerExit = 0; db::ChildMode mode{};
        const bool handled = channel.tryRunCommandLine(GetCommandLineW(),workerExit,mode);
        if (!handled || mode == db::ChildMode::ordinary) return 88; // Detect forbidden normal-startup fallback.
        if (mode != db::ChildMode::worker) return workerExit ? workerExit : ERROR_INVALID_PARAMETER;
        return worker(channel,argv[2]);
    }
    if (argc == 4 && std::wstring_view(argv[1]) == L"--source-server") {
        try { std::ofstream(std::filesystem::path(argv[3])) << sourceServer(argv[2]).dump(2) << '\n'; return 0; }
        catch (const std::exception& e) { std::ofstream(std::filesystem::path(argv[3])) << json{{"error",e.what()}}.dump(2); return 2; }
    }
    if (argc == 2 && std::wstring_view(argv[1]) == L"--fixture-owned-descendant") {
        WinHandle blocker{CreateEventW(nullptr,TRUE,FALSE,nullptr)};
        WaitForSingleObject(blocker.h,60000); return 0;
    }
    if (argc != 3 || std::wstring_view(argv[1]) != L"--report") return ERROR_INVALID_PARAMETER;
    const std::filesystem::path reportPath(argv[2]);
    json report = {{"componentOnly",true},{"runtimeIntegrated",false},{"originalStalledGateFixed",false},
        {"officialLoaderExecuted",false},{"steamOrDevicesTouched",false},{"deadlineMs",deadlineMs},
        {"cases",json::array()},{"passed",false}};
    auto save = [&] { std::ofstream stream(reportPath); stream << report.dump(2) << '\n'; };
    save();
    auto run = [&](const char* name,const std::function<json()>& test) {
        json entry = {{"id",name},{"passed",false}};
        try {
            const auto beforeCount = handleCount();
            const auto beforeHandles = handles();
            entry["measurements"] = test();
            const auto afterHandles = handles();
            const auto afterCount = handleCount();
            entry["parentHandleCountBefore"] = beforeCount; entry["parentHandleCountAfter"] = afterCount;
            std::vector<std::uintptr_t> added;
            std::set_difference(afterHandles.begin(),afterHandles.end(),beforeHandles.begin(),beforeHandles.end(),std::back_inserter(added));
            entry["addedParentHandles"] = added;
            need(beforeCount == afterCount && added.empty(),"real parent handle set/count returned to baseline");
            need(db::Owner::retainedFailures().empty(),"unexpected retained cleanup failure");
            entry["passed"] = true;
        } catch (const std::exception& e) { entry["error"] = e.what(); }
        report["cases"].push_back(entry); save();
        std::cout << name << ": " << (entry["passed"].get<bool>() ? "PASS" : "FAIL") << '\n';
    };
    // Initialize Shell32 command-line parsing once before measuring its process-lifetime handles.
    { db::ChildChannel warm; int code = 0; db::ChildMode mode{}; warm.tryRunCommandLine(L"x --report x",code,mode); }
    // Warm CRT/PSS and thread machinery before measuring kernel handle baselines.
    { std::thread warm([]{}); waitThread(warm); handles(); db::Owner warmOwner; begin(warmOwner,"cooperate"); warmOwner.stop(); }
    run("echo-empty-binary-max-and-explicit-inheritance",[] {
        WinHandle unrelated{CreateEventW(nullptr,TRUE,FALSE,nullptr)};
        need(unrelated.h && SetHandleInformation(unrelated.h,HANDLE_FLAG_INHERIT,HANDLE_FLAG_INHERIT),"unrelated inheritable sentinel");
        db::Owner owner; const auto ready = begin(owner,"echo"); auto process = observer(owner);
        const auto inherited = ready.at("initialHandles").get<std::vector<std::uintptr_t>>();
        need(std::find(inherited.begin(),inherited.end(),reinterpret_cast<std::uintptr_t>(unrelated.h)) == inherited.end(),"unlisted handle not inherited");
        need(ready.at("inheritedHandles").size() == 2,("inheritance audit: " + ready.dump()).c_str());
        need(ready.at("sourceServerPid").get<DWORD>() == GetCurrentProcessId(),"borrowed pipe identifies actual parent owner");
        need(ready.at("sourceSession") == ready.at("workerSession"),"same Windows session admitted");
        need(ready.at("inJob").get<bool>(),"worker inside Job before bootstrap");
        const auto limits = owner.snapshot().jobLimitFlags;
        need(limits == JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE,"no breakaway flags");
        const std::vector<std::vector<std::uint8_t>> frames = {{},{0,1,0xff,0x80,0},std::vector<std::uint8_t>(db::maxFrameBytes,0x42)};
        for (const auto& frame : frames) {
            need(static_cast<bool>(owner.writeFrame(frame,1500)),"echo request");
            auto reply = owner.readFrame(1500); need(reply && reply.payload == frame,"same-exe actual frame echo");
        }
        const auto stopped = owner.stop(); cleaned(owner,process.h);
        return json{{"frames",frames.size()},{"maxBytes",db::maxFrameBytes},{"ready",ready},{"stop",result(stopped)}};
    });
    run("cooperative-child-uses-duplicated-cancel-event",[] {
        db::Owner owner; const auto ready = begin(owner,"cooperate"); auto process = observer(owner);
        need(ready.at("cancelDuplicated").get<bool>(),"production token can duplicate borrowed cancel event");
        const auto stopped = owner.stop(); cleaned(owner,process.h);
        need(stopped.fullyDestroyed && owner.snapshot().exitCode == 0 && !owner.snapshot().jobTerminationAttempted,"cooperative cancel requires no job force");
        return json{{"ready",ready},{"stop",result(stopped)},{"final",snap(owner.snapshot())}};
    });
    run("owned-descendant-cannot-break-away-and-is-destroyed",[] {
        db::Owner owner; const auto ready = begin(owner,"descendant"); auto process = observer(owner);
        const auto descendantPid = ready.at("descendantPid").get<DWORD>();
        WinHandle descendant{OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,FALSE,descendantPid)};
        need(descendant.h != nullptr,"exact fixture descendant observer");
        need(ready.at("breakawayError").get<DWORD>() == ERROR_ACCESS_DENIED,"Job forbids explicit breakaway");
        const auto before = owner.snapshot(); const auto begun = GetTickCount64();
        const auto stopped = owner.stop(); const auto elapsed = GetTickCount64() - begun;
        cleaned(owner,process.h);
        const bool descendantSignalledAtReturn = WaitForSingleObject(descendant.h,0) == WAIT_OBJECT_0;
        // Job accounting reaches zero before all process-object exit signalling on some runs.
        // Independently require the descendant's real signal within the same 3200ms deadline.
        const DWORD remaining = elapsed < deadlineMs ? deadlineMs - static_cast<DWORD>(elapsed) : 0;
        need(WaitForSingleObject(descendant.h,remaining) == WAIT_OBJECT_0,"owned descendant truly exited by deadline");
        need(GetTickCount64() - begun < deadlineMs,"descendant destruction bounded");
        return json{{"ready",ready},{"before",snap(before)},{"stop",result(stopped)},
            {"descendantExited",true},{"descendantSignalledAtStopReturn",descendantSignalledAtReturn},
            {"elapsedMs",elapsed},{"descendantExitObservedMs",GetTickCount64() - begun},{"final",snap(owner.snapshot())}};
    });
    run("ignore-cancel-job-termination",[] {
        db::Owner owner; begin(owner,"stall"); auto process = observer(owner);
        Deadline deadline; const auto stopped = owner.stop(); const auto before = owner.snapshot();
        const auto elapsed = GetTickCount64() - deadline.begun;
        const bool exited = WaitForSingleObject(process.h,0) == WAIT_OBJECT_0;
        deadline.wait(); const auto at = owner.snapshot(); cleaned(owner,process.h);
        need(stopped.fullyDestroyed && exited && elapsed < deadlineMs && before.jobTerminationAttempted,"bounded uncooperative child job kill");
        return json{{"beforeDeadline",snap(before)},{"atDeadline",snap(at)},{"elapsedMs",elapsed},{"deadlineMs",deadlineMs},{"independentChildExited",exited}};
    });
    run("pending-read-stop",[] { return blocked(false,true); });
    run("pending-write-stop",[] { return blocked(true,true); });
    run("negative-no-job-termination-catches-live-child",[] { return blocked(false,false); });
    run("oversize-outgoing-rejected-without-poisoning",[] {
        db::Owner owner; begin(owner,"echo"); auto process = observer(owner);
        const auto oversized = owner.writeFrame(std::vector<std::uint8_t>(db::maxFrameBytes + 1),1000);
        need(oversized.code == db::Code::tooLarge,"oversize rejected");
        need(static_cast<bool>(owner.writeFrame(bytes("after-rejection"),1000)),"valid frame after rejection");
        const auto reply = owner.readFrame(1000); need(reply && reply.payload == bytes("after-rejection"),"no oversized prefix leaked");
        owner.stop(); cleaned(owner,process.h); return result(oversized);
    });
    run("oversize-incoming-rejected",[] {
        db::Owner owner; need(static_cast<bool>(owner.start()),"oversize start"); auto process = observer(owner);
        need(static_cast<bool>(owner.writeFrame(bytes("oversize-incoming"),1000)),"oversize command");
        const auto reply = owner.readFrame(1500); need(reply.code == db::Code::tooLarge,"incoming limit checked before allocation");
        owner.stop(); cleaned(owner,process.h); return result(reply);
    });
    run("natural-child-exit-and-repeated-stop",[] {
        db::Owner owner; need(static_cast<bool>(owner.start()),"natural start"); auto process = observer(owner);
        need(static_cast<bool>(owner.writeFrame(bytes("natural"),1000)),"natural command");
        need(WaitForSingleObject(process.h,1500) == WAIT_OBJECT_0,"natural process exit");
        const auto first = owner.stop(); const auto again = owner.stop(); cleaned(owner,process.h);
        need(first.fullyDestroyed && again.fullyDestroyed && !owner.snapshot().jobTerminationAttempted,"natural exit no force");
        return json{{"first",result(first)},{"again",result(again)}};
    });
    run("stop-before-start-and-old-generation-cannot-revive",[] {
        db::Owner old; const auto oldGeneration = old.snapshot().generation;
        need(old.stop().fullyDestroyed,"stop unopened owner");
        need(old.start().code == db::Code::stopped,"late start veto");
        need(old.writeFrame(bytes("late"),100,oldGeneration).code == db::Code::stopped,"late write veto");
        db::Owner current; begin(current,"echo"); auto process = observer(current);
        const auto stale = current.readFrame(100,oldGeneration);
        need(stale.code == db::Code::stopped && !current.snapshot().stopRequested,"old generation rejected without affecting new owner");
        need(static_cast<bool>(current.writeFrame(bytes("current"),1000)),"new-generation write");
        need(current.readFrame(1000).payload == bytes("current"),"new-generation echo");
        current.stop(); cleaned(current,process.h);
        need(current.start().code == db::Code::stopped,"stopped generation permanently sealed");
        return json{{"oldGeneration",oldGeneration},{"currentGeneration",current.snapshot().generation},{"staleResult",result(stale)}};
    });
    run("read-timeout-retains-then-drains-real-io",[] {
        db::Owner owner; begin(owner,"stall"); auto process = observer(owner);
        const auto timed = owner.readFrame(25); const auto after = owner.snapshot();
        need(timed.code == db::Code::timeout,"read deadline observed");
        const auto stopped = owner.stop(); cleaned(owner,process.h);
        return json{{"timeoutResult",result(timed)},{"afterTimeout",snap(after)},{"stop",result(stopped)}};
    });
    run("concurrent-stops-and-late-io",[] {
        db::Owner owner; begin(owner,"stall"); auto process = observer(owner);
        db::Result first,second;
        std::thread a([&]{ first = owner.stop(); });
        std::thread b([&]{ second = owner.stop(); });
        waitThread(a); waitThread(b); const auto final = owner.stop(); cleaned(owner,process.h);
        need(first.fullyDestroyed || second.fullyDestroyed,"one concurrent stop completes");
        need(owner.readFrame(0).code == db::Code::stopped,"late read veto");
        need(owner.writeFrame(bytes("late"),0).code == db::Code::stopped,"late write veto");
        return json{{"first",result(first)},{"second",result(second)},{"final",result(final)}};
    });
    run("foreign-exe-valid-inherited-resources-rejected-without-fallback",[&] {
        const std::filesystem::path image(db::detail::executable());
        const auto foreignImage = image.parent_path() / L"foreign-owner-fixture.exe";
        const auto foreignReport = reportPath.parent_path() / L"foreign-source-result.json";
        need(std::filesystem::is_regular_file(foreignImage),"separate test-only foreign image exists");
        const auto command = L"\"" + foreignImage.wstring() + L"\" --source-server \"" + image.wstring() + L"\" \"" + foreignReport.wstring() + L"\"";
        FixtureProcess process; process.createSuspended(foreignImage.wstring(),command,false); process.resume();
        need(WaitForSingleObject(process.state->process.value,5000) == WAIT_OBJECT_0,"foreign fixture bounded real exit");
        DWORD exitCode = STILL_ACTIVE;
        need(GetExitCodeProcess(process.state->process.value,&exitCode) && exitCode == 0,"foreign fixture completed");
        const auto cleanup = process.stop();
        need(cleanup.fullyDestroyed,"foreign fixture job/pipe/process resources destroyed");
        std::ifstream stream(foreignReport); json observed; stream >> observed;
        need(observed.at("legalInheritedClientPipeAndCancelEvent").get<bool>(),"negative uses legal native pipe/event");
        need(observed.at("sameSession").get<bool>() && observed.at("workerInOwnedJob").get<bool>(),"negative isolates different server exe");
        need(!observed.at("sameImage").get<bool>() && observed.at("sourceRejected").get<bool>(),"different live source exe refused");
        need(!observed.at("ordinaryFallbackTaken").get<bool>() && observed.at("workerResponseBytes").get<DWORD>() == 0,"no worker dispatch or ordinary startup fallback");
        need(observed.at("cleanup").at("fullyDestroyed").get<bool>(),"foreign target truly cleaned");
        observed["outerCleanup"] = result(cleanup);
        return observed;
    });
    run("strict-worker-switch-no-normal-fallback",[] {
        const wchar_t* malformed[][5] = {{L"x",db::childSwitch},{L"x",L"--ymcc-decky-binding-workerX"},
            {L"x",db::childSwitch,L"0",L"1"},{L"x",db::childSwitch,L"1",L"1"},
            {L"x",L"ordinary",db::childSwitch},{L"x",db::childSwitch,L"18446744073709551616",L"2"}};
        const int counts[] = {2,2,4,4,3,4};
        for (unsigned i = 0; i < std::size(counts); ++i)
            need(db::ChildChannel::classify(counts[i],malformed[i]) == db::ChildMode::invalid,"strict malformed worker rejection");
        db::ChildChannel channel; int exitCode = 0; db::ChildMode mode{};
        need(channel.tryRunCommandLine(L"x --ymcc-decky-binding-worker 0 1",exitCode,mode) &&
            mode == db::ChildMode::invalid && exitCode != 0,"early invalid mode handled");
        return json{{"invalidCases",std::size(counts) + 1},{"normalFallback",false}};
    });
    report["retainedFailures"] = db::Owner::retainedFailures().size();
    report["passed"] = std::all_of(report["cases"].begin(),report["cases"].end(),[](const json& c){ return c.at("passed").get<bool>(); });
    report["caseCount"] = report["cases"].size(); save();
    return report["passed"].get<bool>() ? 0 : 1;
}