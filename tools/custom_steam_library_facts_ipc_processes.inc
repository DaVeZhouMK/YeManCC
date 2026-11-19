#pragma once

// Include at global scope after the original consumer supplies json/fs aliases.
// The controller owns the rawrunner Job. All paths/argv are caller constrained.
#include <windows.h>
#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

// Keep the original consumer's fault ports outside this resource-only helper.
#pragma push_macro("CreateFileW")
#pragma push_macro("CreateProcessW")
#pragma push_macro("CloseHandle")
#pragma push_macro("ReadFile")
#pragma push_macro("WriteFile")
#pragma push_macro("FlushFileBuffers")
#undef CreateFileW
#undef CreateProcessW
#undef CloseHandle
#undef ReadFile
#undef WriteFile
#undef FlushFileBuffers

namespace LibraryFactsIpc {

// Controller-thread counters, including destructor-only failures. Read after
// all children/desktops leave scope; these are observations, never PASS flags.
inline unsigned processStarts=0,processJoins=0,processCloseFailures=0;
inline unsigned exceptionalJoins=0,cleanupFailures=0;
inline unsigned desktopStarts=0,desktopCloses=0;
inline DWORD lastFailureError=ERROR_SUCCESS,lastFailurePid=0;
inline DWORD lastFailureWait=WAIT_OBJECT_0,lastExceptionalExit=STILL_ACTIVE;
inline const char* lastFailureOperation="";
inline unsigned desktopSerial=0;
inline constexpr DWORD maxWaitMs=10000;
inline constexpr DWORD maxJsonBytes=65535;

inline void RecordCleanupFailure(const char* operation,DWORD error,DWORD pid=0,
                                 DWORD wait=WAIT_OBJECT_0) noexcept {
    ++cleanupFailures;lastFailureOperation=operation;lastFailureError=error;
    lastFailurePid=pid;lastFailureWait=wait;
}
struct ResourceException:std::runtime_error {
    const char* operation;
    DWORD error;
    ResourceException(const char* step,DWORD code):
        std::runtime_error(std::string(step)+" (Win32 "+std::to_string(code)+")"),
        operation(step),error(code) {}
};
[[noreturn]] inline void ResourceError(const char* operation,DWORD error) {
    throw ResourceException(operation,error);
}
inline void CheckTimeout(DWORD ms) {
    if(ms>maxWaitMs)ResourceError("owned wait exceeds 10000ms",ERROR_INVALID_PARAMETER);
}
inline std::wstring QuoteArgument(const std::wstring& argument) {
    if(argument.find(L'\0')!=std::wstring::npos)
        ResourceError("embedded NUL in child argument",ERROR_INVALID_PARAMETER);
    std::wstring result=L"\"";size_t slashes=0;
    for(const wchar_t c:argument) {
        if(c==L'\\'){++slashes;continue;}
        result.append(c==L'"'?slashes*2+1:slashes,L'\\');
        result.push_back(c);slashes=0;
    }
    result.append(slashes*2,L'\\');result.push_back(L'"');return result;
}

struct OwnedDesktop {
    HDESK original=nullptr,owned=nullptr;
    DWORD ownerThread=GetCurrentThreadId();
    std::wstring name,fullName;

    OwnedDesktop() {
        original=GetThreadDesktop(ownerThread);
        if(!original)ResourceError("original thread desktop",GetLastError());
        wchar_t station[256]{};DWORD needed=0;
        if(!GetUserObjectInformationW(GetProcessWindowStation(),UOI_NAME,station,
                                      sizeof(station),&needed))
            ResourceError("private desktop window station",GetLastError());
        name=L"YMCC.LibraryFactsIpc."+std::to_wstring(GetCurrentProcessId())+L"."+
            std::to_wstring(GetTickCount64())+L"."+std::to_wstring(++desktopSerial);
        fullName=std::wstring(station)+L"\\"+name;
        owned=CreateDesktopW(name.c_str(),nullptr,nullptr,0,GENERIC_ALL,nullptr);
        if(!owned)ResourceError("private desktop create",GetLastError());
        if(!SetThreadDesktop(owned)) {
            const DWORD error=GetLastError();
            if(!CloseDesktop(owned))RecordCleanupFailure("failed desktop create close",GetLastError());
            owned=nullptr;ResourceError("private thread desktop",error);
        }
        ++desktopStarts;
    }
    OwnedDesktop(const OwnedDesktop&)=delete;
    OwnedDesktop& operator=(const OwnedDesktop&)=delete;
    std::wstring full() const {return fullName;}
    ~OwnedDesktop() noexcept {
        if(!owned)return;
        if(GetCurrentThreadId()!=ownerThread) {
            RecordCleanupFailure("desktop destroyed on other thread",ERROR_INVALID_THREAD_ID);
        } else if(!SetThreadDesktop(original)) {
            RecordCleanupFailure("original desktop restore",GetLastError());
        }
        if(!CloseDesktop(owned))RecordCleanupFailure("private desktop close",GetLastError());
        else {owned=nullptr;++desktopCloses;}
    }
};

struct OwnedChild;
static bool MessagePumpUntil(OwnedChild&,DWORD timeoutMs);

struct OwnedChild {
    HANDLE process=nullptr,thread=nullptr;
    DWORD pid=0,exitCode=STILL_ACTIVE;
    ULONGLONG birth=0;
    bool joined=false;
    DWORD lastWait=WAIT_OBJECT_0;

    OwnedChild()=default;
    OwnedChild(const OwnedChild&)=delete;
    OwnedChild& operator=(const OwnedChild&)=delete;

    void start(fs::path exe,const std::vector<std::wstring>& args,
               const std::wstring& desktop=L"") {
        if(process||thread||started_||!exe.is_absolute()||
           desktop.find(L'\0')!=std::wstring::npos)
            ResourceError("invalid owned child launch",ERROR_INVALID_PARAMETER);
        std::wstring command=QuoteArgument(exe.wstring());
        for(const auto& arg:args)command+=L" "+QuoteArgument(arg);
        if(command.size()>=32767)ResourceError("owned child command too long",ERROR_BAD_LENGTH);
        std::vector<wchar_t> buffer(command.begin(),command.end());buffer.push_back(0);
        const auto directory=exe.parent_path().wstring();
        BOOL inJob=FALSE;
        if(!IsProcessInJob(GetCurrentProcess(),nullptr,&inJob))
            ResourceError("controller Job observation",GetLastError());
        if(!inJob)ResourceError("controller is outside rawrunner Job",ERROR_ACCESS_DENIED);
        STARTUPINFOW startup{};startup.cb=sizeof(startup);
        startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;
        startup.lpDesktop=desktop.empty()?nullptr:const_cast<LPWSTR>(desktop.c_str());
        PROCESS_INFORMATION created{};
        // No breakaway flag, no handle inheritance and no independent Job.
        if(!CreateProcessW(exe.c_str(),buffer.data(),nullptr,nullptr,FALSE,
                           CREATE_SUSPENDED|CREATE_NO_WINDOW,nullptr,directory.c_str(),
                           &startup,&created))
            ResourceError("owned child create suspended",GetLastError());
        process=created.hProcess;thread=created.hThread;started_=true;++processStarts;
        pid=GetProcessId(process);
        if(!pid)ResourceError("owned child process ID",GetLastError());
        if(pid!=created.dwProcessId)ResourceError("owned child ID mismatch",ERROR_INVALID_DATA);
        FILETIME creation{},ended{},kernel{},user{};
        if(!GetProcessTimes(process,&creation,&ended,&kernel,&user))
            ResourceError("owned child creation time",GetLastError());
        birth=(ULONGLONG(creation.dwHighDateTime)<<32)|creation.dwLowDateTime;
        if(!birth)ResourceError("owned child missing creation time",ERROR_INVALID_DATA);
        BOOL childInJob=FALSE;
        if(!IsProcessInJob(process,nullptr,&childInJob))
            ResourceError("owned child Job observation",GetLastError());
        if(!childInJob)ResourceError("owned child did not inherit a Job",ERROR_ACCESS_DENIED);
        const DWORD previous=ResumeThread(thread);
        if(previous==DWORD(-1))ResourceError("owned child resume",GetLastError());
        if(previous!=1)ResourceError("owned child unexpected suspend count",ERROR_INVALID_DATA);
    }
    bool done() {
        if(joined)return true;
        if(!process)ResourceError("owned child not started",ERROR_INVALID_HANDLE);
        const DWORD result=WaitForSingleObject(process,0);lastWait=result;
        if(result==WAIT_OBJECT_0)return true;
        if(result==WAIT_TIMEOUT)return false;
        ResourceError("owned child completion wait",result==WAIT_FAILED?GetLastError():ERROR_INVALID_DATA);
    }
    void join(DWORD ms=10000) {
        CheckTimeout(ms);
        if(joined)return;
        // Pump the provider thread while waiting for a WM_COPYDATA client.
        if(!MessagePumpUntil(*this,ms))ResourceError("owned child join timeout",WAIT_TIMEOUT);
        DWORD code=STILL_ACTIVE;
        if(!GetExitCodeProcess(process,&code))ResourceError("owned child exit code",GetLastError());
        // A signalled process may legally have exit code 259; the wait is proof.
        exitCode=code;joined=true;++processJoins;
    }
    void close() {
        if(!joined)ResourceError("owned child close before join",ERROR_BUSY);
        bool failed=false;DWORD firstError=ERROR_SUCCESS;
        if(thread) {
            if(CloseHandle(thread))thread=nullptr;
            else {firstError=GetLastError();failed=true;++processCloseFailures;
                RecordCleanupFailure("child thread close",firstError,pid);}
        }
        if(process) {
            if(CloseHandle(process))process=nullptr;
            else {const DWORD error=GetLastError();if(!failed)firstError=error;
                failed=true;++processCloseFailures;RecordCleanupFailure("child process close",error,pid);}
        }
        if(failed)ResourceError("owned child handle close",firstError);
    }
    ~OwnedChild() noexcept {
        if(process&&!joined) {
            try {join(maxWaitMs);++exceptionalJoins;lastExceptionalExit=exitCode;}
            catch(const ResourceException& error) {
                RecordCleanupFailure(error.operation,error.error,pid,lastWait);
            }
            catch(...) {RecordCleanupFailure("unjoined child cleanup exception",ERROR_UNHANDLED_EXCEPTION,pid,lastWait);}
        }
        // Never terminate a process here. A timeout/failure remains a failure;
        // the original rawrunner alone owns final Job-tree cleanup.
        if(thread) {
            if(!CloseHandle(thread)){++processCloseFailures;RecordCleanupFailure("destructor thread close",GetLastError(),pid);}
            else thread=nullptr;
        }
        if(process) {
            if(!CloseHandle(process)){++processCloseFailures;RecordCleanupFailure("destructor process close",GetLastError(),pid);}
            else process=nullptr;
        }
    }
private:
    bool started_=false;
};

static bool MessagePumpUntil(OwnedChild& child,DWORD timeoutMs) {
    CheckTimeout(timeoutMs);
    const ULONGLONG began=GetTickCount64();
    for(;;) {
        // One message per turn keeps a posted-message flood from starving the
        // process/deadline checks. PeekMessage also dispatches sent messages.
        MSG message{};
        if(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
            if(message.message==WM_QUIT) {
                PostQuitMessage(static_cast<int>(message.wParam));
                ResourceError("message pump interrupted by quit",ERROR_CANCELLED);
            }
            TranslateMessage(&message);DispatchMessageW(&message);
        }
        if(child.done())return true;
        const ULONGLONG elapsed=GetTickCount64()-began;
        if(elapsed>=timeoutMs)return false;
        const DWORD slice=static_cast<DWORD>((std::min)(ULONGLONG(2),ULONGLONG(timeoutMs)-elapsed));
        const DWORD result=WaitForSingleObject(child.process,slice);child.lastWait=result;
        if(result==WAIT_OBJECT_0)return true;
        if(result!=WAIT_TIMEOUT)
            ResourceError("message pump child wait",result==WAIT_FAILED?GetLastError():ERROR_INVALID_DATA);
    }
}

struct JsonFileHandle {
    HANDLE value=INVALID_HANDLE_VALUE;
    explicit JsonFileHandle(HANDLE handle):value(handle) {}
    JsonFileHandle(const JsonFileHandle&)=delete;
    JsonFileHandle& operator=(const JsonFileHandle&)=delete;
    ~JsonFileHandle() noexcept {
        if(value!=INVALID_HANDLE_VALUE&&!CloseHandle(value))
            RecordCleanupFailure("JSON file destructor close",GetLastError());
    }
    void close() {
        if(!CloseHandle(value)) {
            const DWORD error=GetLastError();RecordCleanupFailure("JSON file close",error);
            ResourceError("JSON file close",error);
        }
        value=INVALID_HANDLE_VALUE;
    }
};

inline void ExclusiveJsonWrite(fs::path file,const json& value) {
    const std::string bytes=value.dump(2);
    if(bytes.empty()||bytes.size()>maxJsonBytes)
        ResourceError("exclusive JSON write size",ERROR_BAD_LENGTH);
    JsonFileHandle output(CreateFileW(file.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,
                                      FILE_ATTRIBUTE_NORMAL,nullptr));
    if(output.value==INVALID_HANDLE_VALUE)ResourceError("exclusive JSON create",GetLastError());
    DWORD written=0;
    if(!WriteFile(output.value,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr))
        ResourceError("exclusive JSON write",GetLastError());
    if(written!=bytes.size())ResourceError("exclusive JSON short write",ERROR_WRITE_FAULT);
    if(!FlushFileBuffers(output.value))ResourceError("exclusive JSON flush",GetLastError());
    LARGE_INTEGER size{};
    if(!GetFileSizeEx(output.value,&size))ResourceError("exclusive JSON written size",GetLastError());
    if(size.QuadPart!=static_cast<LONGLONG>(bytes.size()))
        ResourceError("exclusive JSON written size mismatch",ERROR_BAD_LENGTH);
    output.close(); // Partial/failed CREATE_NEW artifacts are retained, never overwritten.
}

inline json ExclusiveJsonRead(fs::path file) {
    JsonFileHandle input(CreateFileW(file.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,
                                     FILE_ATTRIBUTE_NORMAL,nullptr));
    if(input.value==INVALID_HANDLE_VALUE)ResourceError("exclusive JSON open",GetLastError());
    LARGE_INTEGER size{};
    if(!GetFileSizeEx(input.value,&size))ResourceError("exclusive JSON read size",GetLastError());
    if(size.QuadPart<=0||size.QuadPart>maxJsonBytes)
        ResourceError("exclusive JSON read size limit",ERROR_BAD_LENGTH);
    std::string bytes(static_cast<size_t>(size.QuadPart),'\0');DWORD read=0;
    if(!ReadFile(input.value,bytes.data(),static_cast<DWORD>(bytes.size()),&read,nullptr))
        ResourceError("exclusive JSON read",GetLastError());
    if(read!=bytes.size())ResourceError("exclusive JSON short read",ERROR_HANDLE_EOF);
    char extra=0;DWORD trailing=0;
    if(!ReadFile(input.value,&extra,1,&trailing,nullptr))ResourceError("exclusive JSON EOF read",GetLastError());
    if(trailing)ResourceError("exclusive JSON trailing bytes",ERROR_BAD_LENGTH);
    LARGE_INTEGER after{};
    if(!GetFileSizeEx(input.value,&after))ResourceError("exclusive JSON final size",GetLastError());
    if(after.QuadPart!=size.QuadPart)ResourceError("exclusive JSON size changed",ERROR_BAD_LENGTH);
    input.close();
    return json::parse(bytes); // Strict parser consumes the entire bounded payload.
}

} // namespace LibraryFactsIpc

#pragma pop_macro("FlushFileBuffers")
#pragma pop_macro("WriteFile")
#pragma pop_macro("ReadFile")
#pragma pop_macro("CloseHandle")
#pragma pop_macro("CreateProcessW")
#pragma pop_macro("CreateFileW")
