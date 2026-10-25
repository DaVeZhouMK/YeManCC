#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>
#include <shellapi.h>
#include <shlobj.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <functional>
#include <vector>
#include <string>
#include <array>
#include <map>
#include <set>
#include <algorithm>
#include <cwctype>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include "json.hpp"
#ifndef YMCC_NOIO_CASE_DIR
#error Explicit owned YMCC_NOIO_CASE_DIR is required
#endif
namespace ymcc_noio {
namespace fs=std::filesystem;
inline fs::path caseRoot(){return fs::path(YMCC_NOIO_CASE_DIR);}
inline fs::path runtimeRoot(){return caseRoot()/L"runtime";}
inline fs::path scratch(){return runtimeRoot()/L"ymcc-steam-native-42420-900000";}
inline std::wstring norm(const fs::path& p){auto s=p.lexically_normal().wstring();for(auto& c:s){if(c==L'/')c=L'\\';c=std::towlower(c);}return s;}
inline bool below(const fs::path& root,const fs::path& p){auto a=norm(root),b=norm(p);return b==a||(b.size()>a.size()&&b.compare(0,a.size(),a)==0&&b[a.size()]==L'\\');}
inline std::wstring module(){if(!__argv||!__argv[0])std::abort();return fs::path(__argv[0]).lexically_normal().wstring();}
inline bool isChild(){return norm(fs::path(module()).filename())==L"inert-addon.exe";}
struct State{uint64_t tick=900000;uintptr_t nextToken=0x71000000;std::set<HANDLE> tokens,files,processes,threads;unsigned createCalls=0,resumed=0,waited=0,denied=0,memoryCalls=0;std::vector<nlohmann::json> events;bool elevated=false,manualSteamMode=false,manualSteamPresent=false,manualShellOkay=true;unsigned manualPrimaryCalls=0,manualShellCalls=0;std::set<HANDLE> manualSnapshots;};
inline State& state(){static State* value=new State;return *value;}
inline void event(const char* api,const char* decision,const std::wstring& detail=L""){auto& s=state();if(s.events.size()>=4096)std::abort();s.events.push_back({{"api",api},{"decision",decision},{"detail",fs::path(detail).string()}});}
inline void denied(const char* api){++state().denied;event(api,"DENY_NO_NATIVE_CALL");::SetLastError(ERROR_ACCESS_DENIED);}
// Preserve the exact approved target while using the Win32 extended-length spelling.
// fileAllowed/directoryAllowed and the owned ancestor checks remain authoritative.
inline std::wstring ownedNativePath(const fs::path& p){auto value=p.lexically_normal().make_preferred().wstring();if(value.size()>2&&value[1]==L':'&&value[2]==L'\\')return L"\\\\?\\"+value;return value;}
inline bool owned(const fs::path& p){if(!p.is_absolute()||!below(caseRoot(),p))return false;for(auto c=p.lexically_normal();below(caseRoot(),c);c=c.parent_path()){auto attr=::GetFileAttributesW(ownedNativePath(c).c_str());if(attr!=INVALID_FILE_ATTRIBUTES&&(attr&FILE_ATTRIBUTE_REPARSE_POINT))return false;if(norm(c)==norm(caseRoot()))break;}return true;}
inline bool fileAllowed(const fs::path& p){if(!owned(p))return false;auto value=norm(p);const auto start=norm(scratch()/L".earlystart");if(value==start)return true;if(value.starts_with(start+L".ymcc-42420-")){const auto tail=value.substr((start+L".ymcc-42420-").size());if(!tail.empty()&&tail.find_first_not_of(L"0123456789")==tail.npos){const auto n=std::stoull(tail);if(n>=900001&&n<=900128)return true;}return false;}for(const auto& f:{caseRoot()/L"launch.exe",scratch()/L"inert-addon.exe",scratch()/L"inert-addon.exe.started",scratch()/L"Scaling App.exe",scratch()/L"CssLoader-Standalone-Headless.exe",scratch()/L"steam.exe",scratch()/L"YeManCC"/L"steam-launch"/L"launch-17-42-7.receipt",runtimeRoot()/L"profile"/L".earlystart",runtimeRoot()/L"local"/L"YeManCC"/L"steam-launch"/L"launch-17-42-7.receipt"})if(value==norm(f))return true;for(const auto* n:{L"one.txt",L"duplicate.txt",L"hidden-css.txt",L"disabled.txt.disabled",L"bad.txt"})if(value==norm(scratch()/L"PowerControl"/L"YeManSteam"/n))return true;else {const auto prefix=norm(scratch()/L"PowerControl"/L"YeManSteam"/n)+L".ymcc-42420-";if(value.starts_with(prefix)){const auto tail=value.substr(prefix.size());if(!tail.empty()&&tail.find_first_not_of(L"0123456789")==tail.npos){const auto counter=std::stoull(tail);if(counter>=900001&&counter<=900128)return true;}}}return false;}
inline bool directoryAllowed(const fs::path& p){if(!owned(p))return false;for(const auto& f:{caseRoot(),runtimeRoot(),scratch(),scratch()/L"PowerControl",scratch()/L"PowerControl"/L"YeManSteam",scratch()/L"YeManCC",scratch()/L"YeManCC"/L"steam-launch",runtimeRoot()/L"profile",runtimeRoot()/L"local",runtimeRoot()/L"local"/L"YeManCC",runtimeRoot()/L"local"/L"YeManCC"/L"steam-launch"})if(norm(p)==norm(f))return true;return false;}
inline bool handle(HANDLE h){return state().files.contains(h);}
inline HANDLE token(){auto h=reinterpret_cast<HANDLE>(state().nextToken++);state().tokens.insert(h);return h;}
inline HANDLE current(){++state().memoryCalls;return reinterpret_cast<HANDLE>(uintptr_t(0x70000001));}
inline BOOL openToken(HANDLE h,DWORD access,PHANDLE out){event("OpenProcessToken","MEMORY");if(!out||(access&~(TOKEN_QUERY|TOKEN_DUPLICATE))||(h!=current()&&!state().processes.contains(h))){denied("OpenProcessToken");return FALSE;}*out=token();return TRUE;}
struct Sid{BYTE count=1;DWORD rid=SECURITY_MANDATORY_MEDIUM_RID;};inline Sid sid;
template<class T>inline BOOL infoValue(PVOID out,DWORD capacity,PDWORD size,const T& value){if(size)*size=sizeof(T);if(!out||capacity<sizeof(T)){::SetLastError(ERROR_INSUFFICIENT_BUFFER);return FALSE;}std::memcpy(out,&value,sizeof(value));return TRUE;}
inline BOOL tokenInfo(HANDLE h,TOKEN_INFORMATION_CLASS kind,LPVOID out,DWORD cap,PDWORD size){event("GetTokenInformation","MEMORY");if(!state().tokens.contains(h)){denied("GetTokenInformation");return FALSE;}switch(kind){case TokenElevation:{TOKEN_ELEVATION v{state().elevated?1u:0u};return infoValue(out,cap,size,v);}case TokenElevationType:{auto v=state().elevated?TokenElevationTypeFull:TokenElevationTypeDefault;return infoValue(out,cap,size,v);}case TokenSessionId:{DWORD v=7;return infoValue(out,cap,size,v);}case TokenStatistics:{TOKEN_STATISTICS v{};v.AuthenticationId={42,17};return infoValue(out,cap,size,v);}case TokenType:{TOKEN_TYPE v=TokenPrimary;return infoValue(out,cap,size,v);}case TokenUser:{TOKEN_USER v{};v.User.Sid=&sid;return infoValue(out,cap,size,v);}case TokenIntegrityLevel:{TOKEN_MANDATORY_LABEL v{};v.Label.Sid=&sid;return infoValue(out,cap,size,v);}case TokenLinkedToken:{TOKEN_LINKED_TOKEN v{token()};return infoValue(out,cap,size,v);}default:denied("GetTokenInformation:class");return FALSE;}}
inline BOOL equalSid(PSID a,PSID b){return a==&sid&&b==&sid;}
inline BOOL validSid(PSID a){return a==&sid;}
inline PUCHAR sidCount(PSID a){if(a!=&sid)std::abort();return &sid.count;}
inline PDWORD sidRid(PSID a,DWORD n){if(a!=&sid||n!=0)std::abort();return &sid.rid;}
inline HRESULT folder(HWND,int kind,HANDLE,DWORD,wchar_t* out){fs::path p;switch(kind){case CSIDL_PROFILE:p=runtimeRoot()/L"profile";break;case CSIDL_LOCAL_APPDATA:p=runtimeRoot()/L"local";break;case CSIDL_STARTUP:p=runtimeRoot()/L"startup";break;default:denied("SHGetFolderPathW:kind");return E_ACCESSDENIED;}event("SHGetFolderPathW","MEMORY",p.wstring());const auto s=p.wstring();if(!out||s.size()>=MAX_PATH)return E_FAIL;std::wcscpy(out,s.c_str());return S_OK;}
inline DWORD moduleName(HMODULE h,wchar_t* out,DWORD n){if(h){denied("GetModuleFileNameW:foreign-module");return 0;}auto s=module();if(!owned(s)||(!isChild()&&norm(s)!=norm(caseRoot()/L"launch.exe"))||(isChild()&&norm(s)!=norm(scratch()/L"inert-addon.exe")))std::abort();if(n<=s.size())return 0;std::wmemcpy(out,s.c_str(),s.size()+1);event("GetModuleFileNameW","MEMORY",s);return DWORD(s.size());}
inline DWORD pid(){return 42420;}
inline ULONGLONG tick(){return state().tick++;}
inline DWORD attributes(LPCWSTR p){if(!p||(!fileAllowed(p)&&!directoryAllowed(p))){denied("GetFileAttributesW");return INVALID_FILE_ATTRIBUTES;}event("GetFileAttributesW","OWNED_FILE",p);return ::GetFileAttributesW(ownedNativePath(fs::path(p)).c_str());}
inline HANDLE createFile(LPCWSTR p,DWORD access,DWORD share,LPSECURITY_ATTRIBUTES sa,DWORD disposition,DWORD flags,HANDLE templateFile){if(!p||!fileAllowed(p)||sa||templateFile||(access&~(GENERIC_READ|GENERIC_WRITE))){denied("CreateFileW");return INVALID_HANDLE_VALUE;}event("CreateFileW","OWNED_FILE",p);auto h=::CreateFileW(ownedNativePath(fs::path(p)).c_str(),access,share,sa,disposition,flags,templateFile);if(h!=INVALID_HANDLE_VALUE)state().files.insert(h);return h;}
inline BOOL fileSize(HANDLE h,PLARGE_INTEGER v){if(!handle(h)){denied("GetFileSizeEx");return FALSE;}return ::GetFileSizeEx(h,v);}
inline BOOL readFile(HANDLE h,LPVOID b,DWORD n,LPDWORD read,LPOVERLAPPED o){if(!handle(h)||o){denied("ReadFile");return FALSE;}return ::ReadFile(h,b,n,read,o);}
inline BOOL writeFile(HANDLE h,LPCVOID b,DWORD n,LPDWORD written,LPOVERLAPPED o){if(!handle(h)||o){denied("WriteFile");return FALSE;}return ::WriteFile(h,b,n,written,o);}
inline BOOL flush(HANDLE h){if(!handle(h)){denied("FlushFileBuffers");return FALSE;}return ::FlushFileBuffers(h);}
inline BOOL fileInfo(HANDLE h,LPBY_HANDLE_FILE_INFORMATION info){if(!handle(h)){denied("GetFileInformationByHandle");return FALSE;}return ::GetFileInformationByHandle(h,info);}
inline BOOL seek(HANDLE h,LARGE_INTEGER v,PLARGE_INTEGER result,DWORD method){if(!handle(h)){denied("SetFilePointerEx");return FALSE;}return ::SetFilePointerEx(h,v,result,method);}
inline BOOL end(HANDLE h){if(!handle(h)){denied("SetEndOfFile");return FALSE;}return ::SetEndOfFile(h);}
inline BOOL moveFile(LPCWSTR from,LPCWSTR to,DWORD flags){if(!from||!to||!fileAllowed(from)||!fileAllowed(to)){denied("MoveFileExW");return FALSE;}event("MoveFileExW","OWNED_FILE",to);return ::MoveFileExW(ownedNativePath(fs::path(from)).c_str(),ownedNativePath(fs::path(to)).c_str(),flags);}
inline BOOL deleteFile(LPCWSTR p){if(!p||!fileAllowed(p)){denied("DeleteFileW");return FALSE;}event("DeleteFileW","OWNED_FILE",p);return ::DeleteFileW(ownedNativePath(fs::path(p)).c_str());}
inline std::vector<unsigned char> fileBytes(const fs::path& p){if(!fileAllowed(p))std::abort();std::ifstream f(p,std::ios::binary);if(!f)std::abort();return {std::istreambuf_iterator<char>(f),{}};}
inline uint32_t rotate(uint32_t x,unsigned n){return(x>>n)|(x<<(32-n));}
inline std::string sha256(std::vector<unsigned char> bytes){static constexpr uint32_t k[]={0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};uint64_t bits=uint64_t(bytes.size())*8;bytes.push_back(0x80);while(bytes.size()%64!=56)bytes.push_back(0);for(int i=7;i>=0;--i)bytes.push_back(static_cast<unsigned char>(bits>>(8*i)));uint32_t h[]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};for(size_t off=0;off<bytes.size();off+=64){uint32_t w[64];for(int i=0;i<16;++i)w[i]=(uint32_t(bytes[off+4*i])<<24)|(uint32_t(bytes[off+4*i+1])<<16)|(uint32_t(bytes[off+4*i+2])<<8)|bytes[off+4*i+3];for(int i=16;i<64;++i){auto s0=rotate(w[i-15],7)^rotate(w[i-15],18)^(w[i-15]>>3),s1=rotate(w[i-2],17)^rotate(w[i-2],19)^(w[i-2]>>10);w[i]=w[i-16]+s0+w[i-7]+s1;}auto a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],v=h[7];for(int i=0;i<64;++i){auto s1=rotate(e,6)^rotate(e,11)^rotate(e,25),ch=(e&f)^(~e&g),t1=v+s1+ch+k[i]+w[i],s0=rotate(a,2)^rotate(a,13)^rotate(a,22),maj=(a&b)^(a&c)^(b&c),t2=s0+maj;v=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;}uint32_t r[]={a,b,c,d,e,f,g,v};for(int i=0;i<8;++i)h[i]+=r[i];}static constexpr char hex[]="0123456789abcdef";std::string out;for(auto v:h)for(int n=7;n>=0;--n)out+=hex[(v>>(n*4))&15];return out;}
inline BOOL createProcess(LPCWSTR app,LPWSTR cmd,LPSECURITY_ATTRIBUTES pa,LPSECURITY_ATTRIBUTES ta,BOOL inherit,DWORD flags,LPVOID environment,LPCWSTR cwd,LPSTARTUPINFOW startup,LPPROCESS_INFORMATION child){if(state().manualSteamMode){
 const auto steam=scratch()/L"steam.exe";
 const std::wstring arguments=L" -bigpicture -offline -noverifyfiles -nobootstrapupdate -skipinitialbootstrap -no-browser -nofriendsui";
 if(!app||norm(app)!=norm(steam)||!cmd||std::wstring(cmd)!=L"\""+steam.wstring()+L"\""+arguments||!cwd||norm(cwd)!=norm(scratch())||pa||ta||inherit||environment||flags!=(CREATE_SUSPENDED|CREATE_NO_WINDOW|CREATE_UNICODE_ENVIRONMENT|NORMAL_PRIORITY_CLASS)||!startup||startup->dwFlags!=STARTF_USESHOWWINDOW||startup->wShowWindow!=SW_SHOWNORMAL||!child){denied("CreateProcessW:manual-contract");return FALSE;}
 ++state().manualPrimaryCalls;event("CreateProcessW","MEMORY_MANUAL_FAILURE_NO_CHILD",steam.wstring());*child={};::SetLastError(ERROR_ACCESS_DENIED);return FALSE;
}const auto target=scratch()/L"inert-addon.exe";auto expected=std::getenv("YMCC_NOIO_EXPECTED_EXE_SHA256");const DWORD wanted=CREATE_SUSPENDED|CREATE_NO_WINDOW|CREATE_UNICODE_ENVIRONMENT|NORMAL_PRIORITY_CLASS;if(!app||norm(app)!=norm(target)||!cmd||std::wstring(cmd)!=L"\""+target.wstring()+L"\""||!cwd||norm(cwd)!=norm(scratch())||pa||ta||inherit||environment||flags!=wanted||!startup||startup->dwFlags!=STARTF_USESHOWWINDOW||startup->wShowWindow!=SW_HIDE||!child||state().createCalls||!expected||sha256(fileBytes(target))!=expected||sha256(fileBytes(caseRoot()/L"launch.exe"))!=expected){denied("CreateProcessW");return FALSE;}struct LockedImage{HANDLE value;~LockedImage(){if(value!=INVALID_HANDLE_VALUE)::CloseHandle(value);}};LockedImage imageLock{::CreateFileW(target.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr)},parentLock{::CreateFileW((caseRoot()/L"launch.exe").c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr)};if(imageLock.value==INVALID_HANDLE_VALUE||parentLock.value==INVALID_HANDLE_VALUE||sha256(fileBytes(target))!=expected||sha256(fileBytes(caseRoot()/L"launch.exe"))!=expected){denied("CreateProcessW:image-lock-or-bytes");return FALSE;}event("CreateProcessW","EXACT_OWNED_CHILD_LOCKED_SHA256",target.wstring());if(!::CreateProcessW(app,cmd,pa,ta,inherit,flags,environment,cwd,startup,child))return FALSE;++state().createCalls;state().processes.insert(child->hProcess);state().threads.insert(child->hThread);return TRUE;}
inline DWORD resume(HANDLE h){if(!state().threads.contains(h)){denied("ResumeThread");return DWORD(-1);}++state().resumed;return ::ResumeThread(h);}
inline DWORD wait(HANDLE h,DWORD n){if(!state().processes.contains(h)||n>5000){denied("WaitForSingleObject");return WAIT_FAILED;}++state().waited;return ::WaitForSingleObject(h,n);}
inline BOOL terminate(HANDLE h,UINT code){if(!state().processes.contains(h)){denied("TerminateProcess");return FALSE;}event("TerminateProcess","OWNED_FAILURE_CLEANUP");return ::TerminateProcess(h,code);}
inline BOOL close(HANDLE h){if(state().manualSnapshots.erase(h)){event("CloseHandle","MEMORY_SNAPSHOT");return TRUE;}if(state().tokens.erase(h)){event("CloseHandle","MEMORY_TOKEN");return TRUE;}if(state().files.erase(h)||state().threads.erase(h)){event("CloseHandle","OWNED_NATIVE_HANDLE");return ::CloseHandle(h);}if(state().processes.contains(h)){const auto done=wait(h,5000);if(done!=WAIT_OBJECT_0)std::abort();state().processes.erase(h);return ::CloseHandle(h);}denied("CloseHandle:unknown");return FALSE;}
inline BOOL image(HANDLE h,DWORD,wchar_t* out,DWORD* n){if(!state().processes.contains(h)){denied("QueryFullProcessImageNameW");return FALSE;}const auto s=(scratch()/L"inert-addon.exe").wstring();if(!out||!n||*n<=s.size())return FALSE;std::wmemcpy(out,s.c_str(),s.size()+1);*n=DWORD(s.size());event("QueryFullProcessImageNameW","MEMORY_OWNED_CHILD");return TRUE;}
inline HANDLE openProcess(DWORD,BOOL,DWORD){denied("OpenProcess");return nullptr;}
inline HANDLE snapshot(DWORD flags,DWORD pid){if(state().manualSteamMode&&flags==TH32CS_SNAPPROCESS&&pid==0){auto h=reinterpret_cast<HANDLE>(state().nextToken++);state().manualSnapshots.insert(h);event("CreateToolhelp32Snapshot","MEMORY_STEAM_ABSENCE");return h;}denied("CreateToolhelp32Snapshot");return INVALID_HANDLE_VALUE;}
inline BOOL first(HANDLE h,LPPROCESSENTRY32W entry){if(state().manualSteamMode&&state().manualSnapshots.contains(h)&&entry){event("Process32FirstW","MEMORY_STEAM_PRESENCE");if(state().manualSteamPresent){entry->th32ProcessID=42499;std::wcscpy(entry->szExeFile,L"steam.exe");return TRUE;}::SetLastError(ERROR_NO_MORE_FILES);return FALSE;}denied("Process32FirstW");return FALSE;}
inline BOOL next(HANDLE h,LPPROCESSENTRY32W){if(state().manualSteamMode&&state().manualSnapshots.contains(h)){event("Process32NextW","MEMORY_END");::SetLastError(ERROR_NO_MORE_FILES);return FALSE;}denied("Process32NextW");return FALSE;}
inline LSTATUS registry(HKEY key,LPCWSTR sub,LPCWSTR name,DWORD flags,LPDWORD type,PVOID out,LPDWORD size){
 if(state().manualSteamMode&&key==HKEY_CURRENT_USER&&sub&&std::wstring(sub)==L"Software\\Valve\\Steam"&&name&&std::wstring(name)==L"SteamExe"&&flags==RRF_RT_REG_SZ&&!type&&out&&size){
  const auto value=(scratch()/L"steam.exe").wstring();const DWORD bytes=DWORD((value.size()+1)*sizeof(wchar_t));if(*size<bytes)return ERROR_MORE_DATA;std::memcpy(out,value.c_str(),bytes);*size=bytes;event("RegGetValueW","MEMORY_EXACT_STEAM_PATH");return ERROR_SUCCESS;
 }denied("RegGetValueW");return ERROR_ACCESS_DENIED;}
inline BOOL duplicate(HANDLE,DWORD,LPSECURITY_ATTRIBUTES,SECURITY_IMPERSONATION_LEVEL,TOKEN_TYPE,PHANDLE){denied("DuplicateTokenEx");return FALSE;}
inline HWND shell(){denied("GetShellWindow");return nullptr;}
inline DWORD shellPid(HWND,LPDWORD){denied("GetWindowThreadProcessId");return 0;}
inline UINT windowsDir(LPWSTR,UINT){denied("GetWindowsDirectoryW");return 0;}
inline BOOL withToken(HANDLE,DWORD,LPCWSTR,LPWSTR,DWORD,LPVOID,LPCWSTR,LPSTARTUPINFOW,LPPROCESS_INFORMATION){denied("CreateProcessWithTokenW");return FALSE;}
inline BOOL processQuery(HANDLE,PROCESS_INFORMATION_CLASS,LPVOID,DWORD){denied("GetProcessInformation");return FALSE;}
inline BOOL processSet(HANDLE,PROCESS_INFORMATION_CLASS,LPVOID,DWORD){denied("SetProcessInformation");return FALSE;}
inline DWORD priority(HANDLE){denied("GetPriorityClass");return 0;}
inline BOOL setPriority(HANDLE,DWORD){denied("SetPriorityClass");return FALSE;}
inline BOOL shellExecute(SHELLEXECUTEINFOW* request){
 const auto steam=scratch()/L"steam.exe";const std::wstring arguments=L" -bigpicture -offline -noverifyfiles -nobootstrapupdate -skipinitialbootstrap -no-browser -nofriendsui";
 if(!state().manualSteamMode||!request||request->cbSize!=sizeof(*request)||request->fMask!=(SEE_MASK_NOCLOSEPROCESS|SEE_MASK_NOASYNC|SEE_MASK_FLAG_NO_UI)||!request->lpVerb||std::wstring(request->lpVerb)!=L"open"||!request->lpFile||norm(request->lpFile)!=norm(steam)||!request->lpParameters||std::wstring(request->lpParameters)!=arguments||!request->lpDirectory||norm(request->lpDirectory)!=norm(scratch())||request->nShow!=SW_SHOWNORMAL||request->hwnd||state().manualSteamPresent){denied("ShellExecuteExW:manual-contract");return FALSE;}
 ++state().manualShellCalls;event("ShellExecuteExW","MEMORY_FIXED_MANUAL_STEAM_ONLY",steam.wstring());request->hProcess=nullptr;::SetLastError(state().manualShellOkay?ERROR_SUCCESS:ERROR_ACCESS_DENIED);return state().manualShellOkay?TRUE:FALSE;
}
inline BOOL processTimes(HANDLE,LPFILETIME,LPFILETIME,LPFILETIME,LPFILETIME){denied("GetProcessTimes");return FALSE;}
inline void systemTime(LPFILETIME out){if(out)*out={};event("GetSystemTimeAsFileTime","MEMORY");}
inline int compareTime(const FILETIME*,const FILETIME*){denied("CompareFileTime");return 0;}
inline HWND console(){event("GetConsoleWindow","MEMORY_NO_WINDOW");return nullptr;}
inline BOOL visible(HWND h){if(h)denied("IsWindowVisible:foreign-window");return FALSE;}
inline LPWSTR* commandArgs(LPCWSTR line,int* count){std::vector<std::wstring> values;const std::wstring text=line?line:L"";if(text==L"\"x:\\ymcc.exe\" --steam-logon-start --dev ignored")values={L"x:\\ymcc.exe",L"--steam-logon-start",L"--dev",L"ignored"};else if(text==L"\"x:\\ymcc.exe\" --minimized")values={L"x:\\ymcc.exe",L"--minimized"};else{denied("CommandLineToArgvW:unreviewed-input");return nullptr;}size_t bytes=(values.size()+1)*sizeof(wchar_t*);for(const auto& v:values)bytes+=(v.size()+1)*sizeof(wchar_t);auto p=static_cast<LPWSTR*>(std::malloc(bytes));if(!p)return nullptr;auto cursor=reinterpret_cast<wchar_t*>(p+values.size()+1);for(size_t i=0;i<values.size();++i){p[i]=cursor;std::wcscpy(cursor,values[i].c_str());cursor+=values[i].size()+1;}p[values.size()]=nullptr;*count=int(values.size());event("CommandLineToArgvW","MEMORY_REVIEWED_INPUT");return p;}
inline HLOCAL localFree(HLOCAL p){std::free(p);return nullptr;}
inline void sleep(DWORD n){if(n>400)std::abort();::Sleep(n);}
inline void finish(){auto& s=state();const auto file=caseRoot()/(isChild()?L"terminal-child.json":L"terminal-parent.json");std::ofstream out(file);out<<nlohmann::json{{"kind","steam-native-memory-terminal"},{"realTokenCalls",0},{"realForeignProcessCalls",0},{"realRegistryCalls",0},{"manualSteamPrimaryMemoryCalls",s.manualPrimaryCalls},{"manualSteamShellMemoryCalls",s.manualShellCalls},{"unclosedManualSnapshots",s.manualSnapshots.size()},{"ownedChildCreates",s.createCalls},{"ownedChildResumes",s.resumed},{"ownedChildWaits",s.waited},{"denied",s.denied},{"unclosedMemoryTokens",s.tokens.size()},{"unclosedOwnedFiles",s.files.size()},{"events",s.events}}.dump(2);}
inline struct Lifecycle{Lifecycle(){std::atexit(finish);}} lifecycle;
}
namespace std::filesystem {inline path ymcc_noio_temp(){return ymcc_noio::runtimeRoot();}}
namespace ymcc_noio {inline BOOL copyFileDenied(LPCWSTR,LPCWSTR,BOOL){denied("CopyFileW:startup-repair-not-executed-by-this-fixture");return FALSE;}}
#define temp_directory_path ymcc_noio_temp
#define CopyFileW ymcc_noio::copyFileDenied
#define GetCurrentProcess ymcc_noio::current
#define GetCurrentProcessId ymcc_noio::pid
#define GetTickCount64 ymcc_noio::tick
#define GetModuleFileNameW ymcc_noio::moduleName
#define SHGetFolderPathW ymcc_noio::folder
#define OpenProcessToken ymcc_noio::openToken
#define GetTokenInformation ymcc_noio::tokenInfo
#define EqualSid ymcc_noio::equalSid
#define IsValidSid ymcc_noio::validSid
#define GetSidSubAuthorityCount ymcc_noio::sidCount
#define GetSidSubAuthority ymcc_noio::sidRid
#define CloseHandle ymcc_noio::close
#define GetFileAttributesW ymcc_noio::attributes
#define CreateFileW ymcc_noio::createFile
#define GetFileSizeEx ymcc_noio::fileSize
#define ReadFile ymcc_noio::readFile
#define WriteFile ymcc_noio::writeFile
#define FlushFileBuffers ymcc_noio::flush
#define GetFileInformationByHandle ymcc_noio::fileInfo
#define SetFilePointerEx ymcc_noio::seek
#define SetEndOfFile ymcc_noio::end
#define MoveFileExW ymcc_noio::moveFile
#define DeleteFileW ymcc_noio::deleteFile
#define CreateProcessW ymcc_noio::createProcess
#define ResumeThread ymcc_noio::resume
#define WaitForSingleObject ymcc_noio::wait
#define TerminateProcess ymcc_noio::terminate
#define QueryFullProcessImageNameW ymcc_noio::image
#define OpenProcess ymcc_noio::openProcess
#define CreateToolhelp32Snapshot ymcc_noio::snapshot
#define Process32FirstW ymcc_noio::first
#define Process32NextW ymcc_noio::next
#define RegGetValueW ymcc_noio::registry
#define DuplicateTokenEx ymcc_noio::duplicate
#define GetShellWindow ymcc_noio::shell
#define GetWindowThreadProcessId ymcc_noio::shellPid
#define GetWindowsDirectoryW ymcc_noio::windowsDir
#define CreateProcessWithTokenW ymcc_noio::withToken
#define GetProcessInformation ymcc_noio::processQuery
#define SetProcessInformation ymcc_noio::processSet
#define GetPriorityClass ymcc_noio::priority
#define SetPriorityClass ymcc_noio::setPriority
#define ShellExecuteExW ymcc_noio::shellExecute
#define GetProcessTimes ymcc_noio::processTimes
#define GetSystemTimeAsFileTime ymcc_noio::systemTime
#define CompareFileTime ymcc_noio::compareTime
#define GetConsoleWindow ymcc_noio::console
#define IsWindowVisible ymcc_noio::visible
#define CommandLineToArgvW ymcc_noio::commandArgs
#define LocalFree ymcc_noio::localFree
#define Sleep ymcc_noio::sleep
