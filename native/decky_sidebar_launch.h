#pragma once
// Third-party loader launch: suspended, caller-owned and always unelevated.
// UAC TokenLinkedToken may be identification-only and is used only as an identity
// reference. Obtain a primary token from the verified interactive Windows shell.
#include <windows.h>
#include <filesystem>
#include <string>
#include <vector>
namespace ymcc::deckylaunch {
struct Handle {
    HANDLE value=nullptr;
    ~Handle(){if(value)CloseHandle(value);}
    Handle()=default;Handle(const Handle&)=delete;Handle& operator=(const Handle&)=delete;
};
struct Result {bool created=false;DWORD error=0;std::string stage;std::string mode;};
inline std::vector<unsigned char> tokenData(HANDLE token,TOKEN_INFORMATION_CLASS kind){
    DWORD size=0;GetTokenInformation(token,kind,nullptr,0,&size);
    if(!size || size>65536)return {};
    std::vector<unsigned char> data(size);
    if(!GetTokenInformation(token,kind,data.data(),size,&size))return {};
    return data;
}
inline bool equalUser(HANDLE left,HANDLE right){
    const auto a=tokenData(left,TokenUser),b=tokenData(right,TokenUser);
    return !a.empty()&&!b.empty()&&EqualSid(reinterpret_cast<const TOKEN_USER*>(a.data())->User.Sid,reinterpret_cast<const TOKEN_USER*>(b.data())->User.Sid);
}
inline bool sameSession(HANDLE left,HANDLE right){
    DWORD a=0,b=0,size=0;
    return GetTokenInformation(left,TokenSessionId,&a,sizeof(a),&size)&&GetTokenInformation(right,TokenSessionId,&b,sizeof(b),&size)&&a==b;
}
inline bool sameLogon(HANDLE left,HANDLE right){
    TOKEN_STATISTICS a{},b{};DWORD size=0;
    return GetTokenInformation(left,TokenStatistics,&a,sizeof(a),&size)&&GetTokenInformation(right,TokenStatistics,&b,sizeof(b),&size)&&
        a.AuthenticationId.LowPart==b.AuthenticationId.LowPart&&a.AuthenticationId.HighPart==b.AuthenticationId.HighPart;
}
inline bool unelevated(HANDLE token){
    TOKEN_ELEVATION elevation{};DWORD size=0;
    if(!GetTokenInformation(token,TokenElevation,&elevation,sizeof(elevation),&size)||elevation.TokenIsElevated)return false;
    const auto data=tokenData(token,TokenIntegrityLevel);if(data.empty())return false;
    const auto sid=reinterpret_cast<const TOKEN_MANDATORY_LABEL*>(data.data())->Label.Sid;
    if(!IsValidSid(sid))return false;const auto count=*GetSidSubAuthorityCount(sid);
    return count>0&&*GetSidSubAuthority(sid,count-1)<=SECURITY_MANDATORY_MEDIUM_RID;
}
inline Result create(const std::wstring& executable,std::wstring& command,wchar_t* environment,
                     const std::wstring& directory,STARTUPINFOW& startup,PROCESS_INFORMATION& child){
    Result result;auto fail=[&](const char* stage,DWORD error){result.stage=stage;result.error=error?error:ERROR_ACCESS_DENIED;return result;};
    Handle caller;
    if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&caller.value))return fail("caller-token-open",GetLastError());
    TOKEN_ELEVATION elevation{};DWORD size=0;
    if(!GetTokenInformation(caller.value,TokenElevation,&elevation,sizeof(elevation),&size))return fail("caller-token-elevation",GetLastError());
    const DWORD flags=CREATE_SUSPENDED|CREATE_NO_WINDOW|CREATE_UNICODE_ENVIRONMENT;
    Handle expected,primary,shell;
    HANDLE identity=caller.value;
    if(!elevation.TokenIsElevated){
        result.mode="unelevated-direct";
        if(!unelevated(caller.value))return fail("caller-token-integrity",ERROR_ACCESS_DENIED);
        if(!CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,FALSE,flags,environment,directory.c_str(),&startup,&child))
            return fail("create-process",GetLastError());
    }else{
        result.mode="verified-shell-primary";
        TOKEN_LINKED_TOKEN linked{};
        if(!GetTokenInformation(caller.value,TokenLinkedToken,&linked,sizeof(linked),&size))return fail("linked-token-query",GetLastError());
        expected.value=linked.LinkedToken;identity=expected.value;
        if(!unelevated(identity)||!equalUser(caller.value,identity)||!sameSession(caller.value,identity))return fail("linked-token-identity",ERROR_ACCESS_DENIED);
        HWND shellWindow=GetShellWindow();DWORD shellPid=0;
        if(!shellWindow||!GetWindowThreadProcessId(shellWindow,&shellPid)||!shellPid)return fail("shell-not-available",ERROR_NOT_FOUND);
        shell.value=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,shellPid);
        if(!shell.value)return fail("shell-process-open",GetLastError());
        wchar_t image[32768]{},windows[32768]{};DWORD imageSize=static_cast<DWORD>(_countof(image));
        if(!QueryFullProcessImageNameW(shell.value,0,image,&imageSize))return fail("shell-image-query",GetLastError());
        const auto windowsSize=GetWindowsDirectoryW(windows,static_cast<UINT>(_countof(windows)));
        if(!windowsSize||windowsSize>=_countof(windows))return fail("shell-windows-path",ERROR_PATH_NOT_FOUND);
        const auto expectedImage=(std::filesystem::path(windows)/L"explorer.exe").wstring();
        if(_wcsicmp(image,expectedImage.c_str()))return fail("shell-image-mismatch",ERROR_ACCESS_DENIED);
        Handle source;
        if(!OpenProcessToken(shell.value,TOKEN_QUERY|TOKEN_DUPLICATE,&source.value))return fail("shell-token-open",GetLastError());
        TOKEN_TYPE sourceType{};
        if(!GetTokenInformation(source.value,TokenType,&sourceType,sizeof(sourceType),&size)||sourceType!=TokenPrimary||
           !unelevated(source.value)||!equalUser(identity,source.value)||!sameSession(identity,source.value)||!sameLogon(identity,source.value))
            return fail("shell-token-identity",ERROR_ACCESS_DENIED);
        // Secondary Logon on this host also requires default/session adjustment
        // rights on the duplicated handle; no privilege is added to the token.
        if(!DuplicateTokenEx(source.value,TOKEN_QUERY|TOKEN_DUPLICATE|TOKEN_ASSIGN_PRIMARY|TOKEN_ADJUST_DEFAULT|TOKEN_ADJUST_SESSIONID,nullptr,SecurityImpersonation,TokenPrimary,&primary.value))
            return fail("shell-token-duplicate",GetLastError());
        if(!unelevated(primary.value)||!equalUser(identity,primary.value)||!sameSession(identity,primary.value)||!sameLogon(identity,primary.value))
            return fail("primary-token-identity",ERROR_ACCESS_DENIED);
        DWORD currentShellPid=0;
        if(GetShellWindow()!=shellWindow||!GetWindowThreadProcessId(shellWindow,&currentShellPid)||currentShellPid!=shellPid||WaitForSingleObject(shell.value,0)!=WAIT_TIMEOUT)
            return fail("shell-process-changed",ERROR_ACCESS_DENIED);
        // No elevated CreateProcess fallback, even on error 5 or privilege failure.
        // Explorer has already loaded this same logon profile; do not reload it.
        if(!CreateProcessWithTokenW(primary.value,0,executable.c_str(),command.data(),flags,environment,directory.c_str(),&startup,&child))
            return fail("create-process-with-token",GetLastError());
    }
    Handle childToken;
    if(!OpenProcessToken(child.hProcess,TOKEN_QUERY,&childToken.value)||!unelevated(childToken.value)||
       !equalUser(identity,childToken.value)||!sameSession(identity,childToken.value)||!sameLogon(identity,childToken.value)){
        // This PID is our still-suspended child, never Steam or an existing process.
        TerminateProcess(child.hProcess,1);WaitForSingleObject(child.hProcess,5000);
        CloseHandle(child.hThread);CloseHandle(child.hProcess);child={};
        return fail("child-token-verification",ERROR_ACCESS_DENIED);
    }
    result.created=true;result.stage="child-token-verified";return result;
}
}
