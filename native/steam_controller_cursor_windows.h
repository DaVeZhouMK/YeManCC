#pragma once
#include <windows.h>
#include <shellapi.h>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace ymcc::steamcursor {
inline constexpr std::array<DWORD,13> systemCursorIds{32512,32513,32514,32515,32516,32642,32643,32644,32645,32646,32648,32649,32650};
class CursorBackups {
    std::array<HCURSOR,13> originals_{};
public:
    ~CursorBackups(){clear();}
    void clear() noexcept {for(auto& cursor:originals_)if(cursor){DestroyCursor(cursor);cursor=nullptr;}}
    bool capture(std::size_t i) noexcept {
        if(originals_[i])return true;
        const auto shared=LoadCursorW(nullptr,MAKEINTRESOURCEW(systemCursorIds[i]));
        originals_[i]=shared?static_cast<HCURSOR>(CopyImage(shared,IMAGE_CURSOR,0,0,0)):nullptr;
        return originals_[i]!=nullptr;
    }
    bool restore(std::array<bool,13>& replaced) noexcept {
        if(SystemParametersInfoW(SPI_SETCURSORS,0,nullptr,0)){replaced.fill(false);return true;}
        bool complete=true;
        for(std::size_t i=0;i<replaced.size();++i)if(replaced[i]){
            const auto copy=originals_[i]?static_cast<HCURSOR>(CopyImage(originals_[i],IMAGE_CURSOR,0,0,0)):nullptr;
            if(copy&&SetSystemCursor(copy,systemCursorIds[i]))replaced[i]=false;
            else{if(copy)DestroyCursor(copy);complete=false;}
        }
        return complete;
    }
};
struct GuardMouseRestore {
    static inline thread_local GuardMouseRestore* active=nullptr;
    static constexpr UINT restoreMessage=WM_APP+12;
    bool posted=false;
    static LRESULT CALLBACK observe(int code,WPARAM kind,LPARAM data) noexcept {
        const DWORD saved=GetLastError();
        if(code==HC_ACTION&&active&&!active->posted&&kind>=WM_MOUSEFIRST&&kind<=WM_MOUSELAST)
            active->posted=PostThreadMessageW(GetCurrentThreadId(),restoreMessage,0,0)!=FALSE;
        SetLastError(saved);
        return CallNextHookEx(nullptr,code,kind,data);
    }
    static bool recover(CursorBackups& originals,std::array<bool,13>& replaced,HANDLE armed) noexcept {
        GuardMouseRestore state;active=&state;
        MSG event{};PeekMessageW(&event,nullptr,WM_USER,WM_USER,PM_NOREMOVE);
        const auto hook=SetWindowsHookExW(WH_MOUSE_LL,observe,GetModuleHandleW(nullptr),0);
        if(!hook){active=nullptr;return false;}
        bool restored=WaitForSingleObject(armed,0)==WAIT_TIMEOUT;
        // This emergency observer exists only after rollback has failed. No
        // timer or background sampling: retry on mouse events and always pass
        // the event through before attempting a User32 restore in this loop.
        while(!restored&&GetMessageW(&event,nullptr,0,0)>0){
            if(event.message==restoreMessage){
                state.posted=false;
                restored=WaitForSingleObject(armed,0)==WAIT_TIMEOUT||originals.restore(replaced);
            }
        }
        UnhookWindowsHookEx(hook);active=nullptr;return restored;
    }
};
// Same-image, wait-only rollback owner. It runs before normal YMCC startup and
// never registers power notifications, accesses settings, or controls input.
inline bool guardCommandLine(const wchar_t* command,int& exitCode) {
    int argc=0;auto argv=CommandLineToArgvW(command,&argc);
    if(!argv)return false;
    const bool selected=argc>1&&wcscmp(argv[1],L"--steam-controller-cursor-guard")==0;
    if(!selected){LocalFree(argv);return false;}
    exitCode=2;std::array<HANDLE,4> handles{};bool valid=argc==6;
    for(int i=0;valid&&i<4;++i){
        wchar_t* end=nullptr;const auto number=_wcstoui64(argv[i+2],&end,10);
        DWORD flags=0;handles[i]=reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(number));
        valid=number&&end&&!*end&&GetHandleInformation(handles[i],&flags);
    }
    LocalFree(argv);
    if(!valid)return true;
    wchar_t parentImage[32768]{},ownImage[32768]{};DWORD size=32768;
    valid=GetProcessId(handles[0])!=0&&GetProcessId(handles[0])!=GetCurrentProcessId()&&
        QueryFullProcessImageNameW(handles[0],0,parentImage,&size)&&GetModuleFileNameW(nullptr,ownImage,32768)&&
        _wcsicmp(parentImage,ownImage)==0;
    CursorBackups originals;
    // Keep an independent original before acknowledging readiness. Scheme
    // reload failure must also have a fallback if the parent has crashed.
    for(std::size_t i=0;valid&&i<systemCursorIds.size();++i)valid=originals.capture(i);
    if(valid&&SetEvent(handles[3])){
        const HANDLE waits[]{handles[0],handles[2]};
        const DWORD wait=WaitForMultipleObjects(2,waits,FALSE,INFINITE);
        // Armed is set before the first cursor replacement, and cleared only
        // after restoration succeeds. A crash at any intermediate point rolls back.
        const bool armed=WaitForSingleObject(handles[1],0)==WAIT_OBJECT_0;
        std::array<bool,13> replaced{};replaced.fill(true);
        bool restored=!armed||originals.restore(replaced);
        if(!restored)restored=GuardMouseRestore::recover(originals,replaced,handles[1]);
        if(restored)ResetEvent(handles[1]);
        exitCode=(wait==WAIT_OBJECT_0||wait==WAIT_OBJECT_0+1)&&restored?0:3;
    }
    for(auto handle:handles)CloseHandle(handle);
    return true;
}
class CursorVisual {
    HANDLE parent_=nullptr,armed_=nullptr,stop_=nullptr,ready_=nullptr,guard_=nullptr;
    bool changed_=false;
    bool restoreFailed_=false;
    std::array<bool,13> replaced_{};
    CursorBackups originals_;
    bool ensureGuard() {
        if(guard_)return WaitForSingleObject(guard_,0)==WAIT_TIMEOUT&&WaitForSingleObject(ready_,0)==WAIT_OBJECT_0;
        SECURITY_ATTRIBUTES security{sizeof(security),nullptr,TRUE};
        parent_=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,TRUE,GetCurrentProcessId());
        armed_=CreateEventW(&security,TRUE,FALSE,nullptr);stop_=CreateEventW(&security,TRUE,FALSE,nullptr);ready_=CreateEventW(&security,TRUE,FALSE,nullptr);
        if(!parent_||!armed_||!stop_||!ready_){close();return false;}
        const HANDLE inherited[]{parent_,armed_,stop_,ready_};SIZE_T bytes=0;
        InitializeProcThreadAttributeList(nullptr,1,0,&bytes);
        std::vector<unsigned char> buffer(bytes);STARTUPINFOEXW startup{};startup.StartupInfo.cb=sizeof(startup);
        startup.lpAttributeList=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(buffer.data());
        if(!InitializeProcThreadAttributeList(startup.lpAttributeList,1,0,&bytes)){close();return false;}
        const bool updated=UpdateProcThreadAttribute(startup.lpAttributeList,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
            const_cast<HANDLE*>(inherited),sizeof(inherited),nullptr,nullptr)!=FALSE;
        wchar_t image[32768]{};GetModuleFileNameW(nullptr,image,32768);
        std::wstring command=L"\""+std::wstring(image)+L"\" --steam-controller-cursor-guard";
        for(auto handle:inherited)command+=L" "+std::to_wstring(reinterpret_cast<std::uintptr_t>(handle));
        PROCESS_INFORMATION process{};BOOL inJob=FALSE;
        const bool jobKnown=IsProcessInJob(GetCurrentProcess(),nullptr,&inJob)!=FALSE;
        // A kill-on-close parent Job must not kill the rollback owner first.
        const DWORD flags=EXTENDED_STARTUPINFO_PRESENT|CREATE_NO_WINDOW|(inJob?CREATE_BREAKAWAY_FROM_JOB:0);
        const bool created=updated&&jobKnown&&CreateProcessW(image,command.data(),nullptr,nullptr,TRUE,flags,nullptr,nullptr,&startup.StartupInfo,&process);
        DeleteProcThreadAttributeList(startup.lpAttributeList);
        if(!created){close();return false;}
        CloseHandle(process.hThread);guard_=process.hProcess;
        // Readiness is an event, never a UI-thread wait. If not ready yet, the
        // existing 5/30-second budget timer tries later without another poller.
        return WaitForSingleObject(ready_,0)==WAIT_OBJECT_0;
    }
public:
    CursorVisual()=default;CursorVisual(const CursorVisual&)=delete;
    ~CursorVisual(){close();}
    bool hidden() const noexcept{return changed_;}
    bool restore() noexcept {
        if(!changed_)return true;
        if(!originals_.restore(replaced_)){
            restoreFailed_=true;if(stop_)SetEvent(stop_);return false;
        }
        changed_=false;replaced_.fill(false);originals_.clear();if(armed_)ResetEvent(armed_);return true;
    }
    bool hide(HWND game) {
        if(restoreFailed_)return false; // Failed cleanup disables later hiding.
        // A windowed game must never hide a cursor resting on another window.
        POINT point{};CURSORINFO cursor{sizeof(cursor)};
        if(!game||GetForegroundWindow()!=game||!GetCursorPos(&point)||
            GetAncestor(WindowFromPoint(point),GA_ROOT)!=GetAncestor(game,GA_ROOT)||!GetCursorInfo(&cursor))return false;
        if(!(cursor.flags&CURSOR_SHOWING))return true; // Already hidden by the game.
        for(std::size_t i=0;i<systemCursorIds.size();++i){
            if(cursor.hCursor!=LoadCursorW(nullptr,MAKEINTRESOURCEW(systemCursorIds[i])))continue;
            if(replaced_[i])return true;
            if(!originals_.capture(i)||!ensureGuard())return false;
            POINT latestPoint{};CURSORINFO latestCursor{sizeof(latestCursor)};
            if(GetForegroundWindow()!=game||!GetCursorPos(&latestPoint)||
               GetAncestor(WindowFromPoint(latestPoint),GA_ROOT)!=GetAncestor(game,GA_ROOT)||
               !GetCursorInfo(&latestCursor)||latestCursor.hCursor!=cursor.hCursor||!(latestCursor.flags&CURSOR_SHOWING)||!SetEvent(armed_))return false;
            std::array<BYTE,128> andMask{},xorMask{};andMask.fill(0xff);
            const auto blank=CreateCursor(GetModuleHandleW(nullptr),0,0,32,32,andMask.data(),xorMask.data());
            if(!blank)return false;
            if(!SetSystemCursor(blank,systemCursorIds[i])){DestroyCursor(blank);if(!changed_)ResetEvent(armed_);return false;}
            replaced_[i]=true;changed_=true;return true;
        }
        return false; // Private/software game cursors are outside this feature.
    }
    bool close() noexcept {
        const bool restored=restore();
        if(stop_)SetEvent(stop_);
        const bool stopped=!guard_||WaitForSingleObject(guard_,1500)==WAIT_OBJECT_0;
        for(auto* handle:{&guard_,&parent_,&armed_,&stop_,&ready_})if(*handle){CloseHandle(*handle);*handle=nullptr;}
        if(restored&&stopped)restoreFailed_=false;
        return restored&&stopped;
    }
};
} // namespace ymcc::steamcursor
