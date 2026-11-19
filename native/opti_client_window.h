#pragma once
#include <windows.h>
#include <tlhelp32.h>
#include <algorithm>
#include <cwctype>
#include <memory>
#include <string>
#include <vector>

// Explicit Open action only. Automatic catalog maintenance never calls this.
namespace ymcc::optiClientWindow {
enum class State { absent, running, unknown };
struct Result { State state=State::unknown;bool focused=false; };
struct Candidate { DWORD pid=0;std::shared_ptr<void> handle; };
struct Survey {bool known=false,seen=false;std::vector<Candidate> admitted;};
struct Handle {HANDLE value=nullptr;~Handle(){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);}};
inline bool sameUser(HANDLE process) {
    Handle current,target;
    if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&current.value)||!OpenProcessToken(process,TOKEN_QUERY,&target.value))return false;
    const auto user=[](HANDLE token,std::vector<unsigned char>& bytes){
        DWORD n=0;GetTokenInformation(token,TokenUser,nullptr,0,&n);
        if(!n||n>65536)return false;bytes.resize(n);
        return GetTokenInformation(token,TokenUser,bytes.data(),n,&n)!=FALSE&&IsValidSid(reinterpret_cast<TOKEN_USER*>(bytes.data())->User.Sid);
    };
    std::vector<unsigned char> a,b;
    return user(current.value,a)&&user(target.value,b)&&EqualSid(reinterpret_cast<TOKEN_USER*>(a.data())->User.Sid,reinterpret_cast<TOKEN_USER*>(b.data())->User.Sid);
}
struct WindowsOps {
    Survey survey()const {
        Survey result;DWORD currentSession=0;
        if(!ProcessIdToSessionId(GetCurrentProcessId(),&currentSession))return result;
        Handle snapshot{CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0)};
        if(snapshot.value==INVALID_HANDLE_VALUE)return result;
        PROCESSENTRY32W entry{sizeof(entry)};
        if(!Process32FirstW(snapshot.value,&entry)){
            result.known=GetLastError()==ERROR_NO_MORE_FILES;return result;
        }
        do {
            if(_wcsicmp(entry.szExeFile,L"OptiscalerClient.exe")!=0)continue;
            result.seen=true;DWORD session=0;
            if(!ProcessIdToSessionId(entry.th32ProcessID,&session)||session!=currentSession)continue;
            HANDLE opened=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,entry.th32ProcessID);
            if(!opened)continue;
            std::shared_ptr<void> process(opened,[](void* value){CloseHandle(value);});
            if(WaitForSingleObject(process.get(),0)!=WAIT_TIMEOUT)continue;
            std::wstring image(32768,L'\0');DWORD size=static_cast<DWORD>(image.size());
            if(!QueryFullProcessImageNameW(process.get(),0,image.data(),&size))continue;
            image.resize(size);std::replace(image.begin(),image.end(),L'/',L'\\');
            if(_wcsicmp(image.c_str(),L"C:\\SOFT\\OptiscalerClient\\OptiscalerClient.exe")!=0||!sameUser(process.get()))continue;
            result.admitted.push_back({entry.th32ProcessID,std::move(process)});
        }while(Process32NextW(snapshot.value,&entry));
        result.known=GetLastError()==ERROR_NO_MORE_FILES;return result;
    }
    bool alive(const Candidate& row)const {return row.handle&&WaitForSingleObject(row.handle.get(),0)==WAIT_TIMEOUT;}
    bool owns(const Candidate& row,HWND window)const {
        DWORD pid=0;return alive(row)&&IsWindow(window)&&GetWindowThreadProcessId(window,&pid)&&pid==row.pid;
    }
    HWND windowFor(const Candidate& row)const {
        struct Search {DWORD pid;HWND found=nullptr;}search{row.pid};
        EnumWindows([](HWND window,LPARAM parameter)->BOOL{
            auto& value=*reinterpret_cast<Search*>(parameter);DWORD pid=0;GetWindowThreadProcessId(window,&pid);
            if(pid!=value.pid||!IsWindowVisible(window)||GetWindow(window,GW_OWNER))return TRUE;
            value.found=window;return FALSE;
        },reinterpret_cast<LPARAM>(&search));
        if(!search.found)return nullptr;
        const auto popup=GetLastActivePopup(search.found);
        return popup&&IsWindowVisible(popup)&&owns(row,popup)?popup:search.found;
    }
    bool iconic(HWND window)const{return IsIconic(window)!=FALSE;}
    bool restore(const Candidate& row,HWND window)const{return owns(row,window)&&ShowWindowAsync(window,SW_RESTORE)!=FALSE;}
    bool activate(const Candidate& row,HWND window)const{
        if(!owns(row,window))return false;
        if(GetForegroundWindow()!=window)SetForegroundWindow(window);
        return owns(row,window)&&GetForegroundWindow()==window;
    }
};
template<class Ops>Result reuse(Ops& ops) {
    const auto observed=ops.survey();
    if(!observed.known)return {State::unknown,false};
    if(!observed.seen)return {State::absent,false};
    for(const auto& row:observed.admitted){
        if(!ops.alive(row))continue;
        const auto window=ops.windowFor(row);
        if(!window||!ops.owns(row,window))continue;
        if(ops.iconic(window)&&!ops.restore(row,window))continue;
        if(!ops.owns(row,window))continue;
        return {State::running,ops.activate(row,window)};
    }
    // A different, unreadable, starting or exiting client never grants another launch.
    return {State::running,false};
}
inline Result reuse(){WindowsOps ops;return reuse(ops);}
} // namespace ymcc::optiClientWindow
