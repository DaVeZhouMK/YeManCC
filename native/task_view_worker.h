#pragma once
#include <windows.h>
#include <shellapi.h>
#include <shldisp.h>
#include <cwchar>
#include <string_view>

// Same executable, short-lived mode. This header owns no UI, settings, devices,
// single-instance state, keyboard injection or background service.
namespace ymcc::taskView::worker {
inline constexpr wchar_t mode[]=L"--ymcc-task-view-worker";
inline constexpr wchar_t eventPrefix[]=L"Local\\YeManTaskView-";
inline constexpr DWORD executionGateBudgetMs=4500;
enum class Arguments { Normal, Invalid, Worker };
inline bool validEventPair(std::wstring_view ready,std::wstring_view execute) noexcept {
    constexpr std::wstring_view prefix=eventPrefix,readySuffix=L"-ready",executeSuffix=L"-execute";
    if(ready.size()>160||execute.size()>160||!ready.starts_with(prefix)||!ready.ends_with(readySuffix)||!execute.ends_with(executeSuffix))return false;
    const auto stem=ready.substr(0,ready.size()-readySuffix.size());
    if(execute.substr(0,execute.size()-executeSuffix.size())!=stem)return false;
    const auto identity=stem.substr(prefix.size());
    unsigned fields=1,digits=0;
    for(const auto c:identity){
        if(c==L'-'){if(!digits||fields==3)return false;++fields;digits=0;}
        else if(c<L'0'||c>L'9'||++digits>20)return false;
    }
    return fields==3&&digits!=0;
}
inline Arguments classify(int argc,const wchar_t* const* argv) noexcept {
    if(argc<2||!argv||!argv[1])return Arguments::Normal;
    const std::wstring_view argument=argv[1];
    if(!argument.starts_with(mode))return Arguments::Normal;
    if(argument!=mode||argc!=4||!argv[2]||!argv[3]||!validEventPair(argv[2],argv[3]))return Arguments::Invalid;
    return Arguments::Worker;
}
inline HRESULT failure(DWORD error) noexcept {return HRESULT_FROM_WIN32(error?error:ERROR_GEN_FAILURE);}
struct WindowsWorkerOps {
    HANDLE openReady(const wchar_t* name)const noexcept{return OpenEventW(EVENT_MODIFY_STATE,FALSE,name);}
    HANDLE openExecute(const wchar_t* name)const noexcept{return OpenEventW(SYNCHRONIZE,FALSE,name);}
    DWORD error()const noexcept{return GetLastError();}
    HRESULT initialize()const noexcept{return CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED|COINIT_DISABLE_OLE1DDE);}
    void initializeQueue()const noexcept{MSG message{};PeekMessageW(&message,nullptr,0,0,PM_NOREMOVE);}
    bool signalReady(HANDLE ready)const noexcept{return SetEvent(ready)!=FALSE;}
    DWORD waitExecute(HANDLE execute)const noexcept{return WaitForSingleObject(execute,executionGateBudgetMs);}
    HRESULT switchWindows()const noexcept{
        IShellDispatch5* shell=nullptr;
        HRESULT result=CoCreateInstance(CLSID_Shell,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&shell));
        if(SUCCEEDED(result))result=shell?shell->WindowSwitcher():E_UNEXPECTED;
        if(shell)shell->Release();
        return result;
    }
    void uninitialize()const noexcept{CoUninitialize();}
    void close(HANDLE handle)const noexcept{CloseHandle(handle);}
};
template<class Ops> HRESULT run(Ops& ops,const wchar_t* readyName,const wchar_t* executeName) noexcept {
    const auto ready=ops.openReady(readyName);
    if(!ready)return failure(ops.error());
    const auto execute=ops.openExecute(executeName);
    if(!execute){const auto result=failure(ops.error());ops.close(ready);return result;}
    HRESULT result=ops.initialize();
    if(SUCCEEDED(result)){
        // Parent must not grant foreground rights until User32 is initialized.
        ops.initializeQueue();
        if(!ops.signalReady(ready))result=failure(ops.error());
        else {
            const auto wait=ops.waitExecute(execute);
            if(wait!=WAIT_OBJECT_0)result=failure(wait==WAIT_TIMEOUT?ERROR_TIMEOUT:ops.error());
        }
        // Exactly one Shell call, only after the parent grants and releases.
        if(SUCCEEDED(result))result=ops.switchWindows();
        ops.uninitialize();
    }
    ops.close(execute);ops.close(ready);
    return result;
}
template<class Ops> bool tryRunArgs(int argc,const wchar_t* const* argv,Ops& ops,int& exitCode) noexcept {
    const auto action=classify(argc,argv);
    if(action==Arguments::Normal)return false;
    if(action==Arguments::Invalid){exitCode=static_cast<int>(E_INVALIDARG);return true;}
    const auto result=run(ops,argv[2],argv[3]);
    exitCode=SUCCEEDED(result)?0:static_cast<int>(result);
    return true;
}
inline bool tryRunCommandLine(const wchar_t* commandLine,int& exitCode) noexcept {
    int argc=0;auto argv=CommandLineToArgvW(commandLine,&argc);
    // Parsing failure must not fall through into full application startup.
    if(!argv){exitCode=static_cast<int>(failure(GetLastError()));return true;}
    WindowsWorkerOps ops;
    const bool handled=tryRunArgs(argc,argv,ops,exitCode);
    LocalFree(argv);return handled;
}
} // namespace ymcc::taskView::worker
