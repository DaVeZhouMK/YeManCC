#pragma once
// FxSound 1.2.6+ controller. Processing and preset application stay in FxSound.
// Uses shell launch, settings and JUCE normal quit; no menus, UIA or input.
#include "fxsound_recovery.h"

#include <shellapi.h>
#include <shlobj.h>
#include <knownfolders.h>
#include <regex>
#include <knownfolders.h>
#include <shlwapi.h>
#include <xmllite.h>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <chrono>
#include <winver.h>
#pragma comment(lib, "version.lib")

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "xmllite.lib")
namespace ymcc::fxsound {
inline constexpr wchar_t presetName[] = L"移动端通用音频优化";
inline constexpr wchar_t presetFile[] = L"移动端通用音频优化.fac";
struct ControlState {
    bool known=false, installed=false, running=false, enabled=false, selected=false;
    std::wstring preset, output, reason;
    Snapshot audio;
};
struct ControlResult { bool ok=false, copied=false, reused=false, repaired=false; std::wstring reason; ControlState state; };
inline std::filesystem::path roaming() {
    PWSTR value=nullptr; std::filesystem::path result;
    if(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData,0,nullptr,&value))) {result=value;CoTaskMemFree(value);}
    return result;
}
inline std::filesystem::path executable() {
    for(const auto* key : {L"ProgramW6432",L"ProgramFiles",L"ProgramFiles(x86)"}) {
        wchar_t buffer[32768]{}; if(!GetEnvironmentVariableW(key,buffer,32768))continue;
        auto path=std::filesystem::path(buffer)/L"FxSound LLC"/L"FxSound"/L"FxSound.exe";
        if(std::filesystem::is_regular_file(path))return path;
    }
    return {};
}
struct FxIdentity { DWORD pid=0; ULONGLONG birth=0; };
inline FxIdentity identity() {
    auto expected=executable();if(expected.empty())return {};
    DWORD session=0;ProcessIdToSessionId(GetCurrentProcessId(),&session);
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0); if(snapshot==INVALID_HANDLE_VALUE)return {};
    PROCESSENTRY32W entry{};entry.dwSize=sizeof(entry); FxIdentity result;
    if(Process32FirstW(snapshot,&entry))do {
        if(_wcsicmp(entry.szExeFile,L"FxSound.exe"))continue;
        DWORD owner=0;if(!ProcessIdToSessionId(entry.th32ProcessID,&owner)||owner!=session)continue;
        HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,entry.th32ProcessID);if(!process)continue;
        wchar_t name[32768]{};DWORD n=32768; FILETIME created{},ended{},kernel{},user{};
        if(QueryFullProcessImageNameW(process,0,name,&n)&&_wcsicmp(name,expected.c_str())==0&&GetProcessTimes(process,&created,&ended,&kernel,&user))
            result={entry.th32ProcessID,(ULONGLONG(created.dwHighDateTime)<<32)|created.dwLowDateTime};
        CloseHandle(process);if(result.pid)break;
    }while(Process32NextW(snapshot,&entry));
    CloseHandle(snapshot);return result;
}
inline bool current(const FxIdentity& expected) {auto now=identity();return now.pid==expected.pid&&now.birth==expected.birth&&now.pid;}
inline std::vector<HWND> windows(DWORD pid) {
    struct Data {DWORD pid;std::vector<HWND> result;} data{pid,{}};
    EnumWindows([](HWND window,LPARAM raw)->BOOL{auto& d=*reinterpret_cast<Data*>(raw);DWORD pid=0;GetWindowThreadProcessId(window,&pid);if(pid==d.pid)d.result.push_back(window);return TRUE;},reinterpret_cast<LPARAM>(&data));
    return data.result;
}
inline bool settings(std::wstring& preset,bool& power) {
    auto path=roaming()/L"FxSound"/L"FxSound.settings"; std::error_code error;
    if(!std::filesystem::is_regular_file(path,error)||std::filesystem::file_size(path,error)>1024*1024)return false;
    ComPtr<IStream> stream; if(FAILED(SHCreateStreamOnFileEx(path.c_str(),STGM_READ|STGM_SHARE_DENY_NONE,0,FALSE,nullptr,&stream)))return false;
    ComPtr<IXmlReader> xml;if(FAILED(CreateXmlReader(__uuidof(IXmlReader),reinterpret_cast<void**>(xml.GetAddressOf()),nullptr))||FAILED(xml->SetInput(stream.Get())))return false;
    xml->SetProperty(XmlReaderProperty_DtdProcessing,DtdProcessing_Prohibit);
    bool havePower=false,havePreset=false; XmlNodeType node{};HRESULT hr=S_OK;
    while((hr=xml->Read(&node))==S_OK) {
        if(node!=XmlNodeType_Element)continue;const wchar_t* tag=nullptr;xml->GetLocalName(&tag,nullptr);if(!tag||wcscmp(tag,L"VALUE"))continue;
        const wchar_t* key=nullptr;const wchar_t* value=nullptr;std::wstring name;
        if(SUCCEEDED(xml->MoveToAttributeByName(L"name",nullptr))&&SUCCEEDED(xml->GetValue(&key,nullptr))&&key)name=key;
        xml->MoveToElement();
        if(SUCCEEDED(xml->MoveToAttributeByName(L"val",nullptr))&&SUCCEEDED(xml->GetValue(&value,nullptr))&&value){
            if(name==L"preset"){preset=value;havePreset=true;}
            if(name==L"power"&&(wcscmp(value,L"0")==0||wcscmp(value,L"1")==0)){power=wcscmp(value,L"1")==0;havePower=true;}
        }
        xml->MoveToElement();
    }
    return hr==S_FALSE&&havePower&&havePreset;
}
inline ControlState observe() {
    ControlState state;state.installed=!executable().empty();
    std::atomic<bool> enabled{true};ComPtr<IMMDeviceEnumerator> enumerator;
    if(SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&enumerator)))){WindowsPort port(enumerator.Get(),enabled);state.audio=port.read();}
    const auto process=processState();state.running=process==ProcessState::running;
    if(process==ProcessState::unknown){state.reason=L"FxSound 进程状态待确认";return state;}
    bool power=false;const bool haveSettings=settings(state.preset,power);state.selected=haveSettings&&state.preset==presetName;
    state.known=!state.running||haveSettings&&state.audio.valid;
    // Disk settings alone cannot prove the audio route. All multimedia playback
    // must currently pass through the FxSound endpoint to display ON.
    state.enabled=state.running&&power&&state.selected&&state.audio.valid&&state.audio.isFx(1);
    if(state.audio.valid){const auto* device=state.audio.find(state.audio.defaults[1]);if(device)state.output=device->name;}
    return state;
}
inline bool supplementPreset(const std::filesystem::path& target,const std::vector<std::filesystem::path>& sources,bool& copied,std::wstring& reason) {
    std::error_code error;
    if(std::filesystem::is_regular_file(target,error))return true;
    if(std::filesystem::exists(target,error)){reason=L"预设路径不是文件";return false;}
    std::filesystem::create_directories(target.parent_path(),error);if(error){reason=L"无法创建预设目录";return false;}
    for(const auto& source:sources) {
        error.clear();if(!std::filesystem::is_regular_file(source,error))continue;
        // CopyFile fail-if-exists is atomic against simultaneous startup/manual use.
        if(CopyFileW(source.c_str(),target.c_str(),TRUE)){copied=true;return true;}
        if(GetLastError()==ERROR_FILE_EXISTS||GetLastError()==ERROR_ALREADY_EXISTS)return std::filesystem::is_regular_file(target,error);
        reason=L"补齐预设失败";return false;
    }
    reason=L"缺少移动端通用音频优化预设备份";return false;
}
inline bool ensurePreset(const std::filesystem::path& fallback,bool& copied,std::wstring& reason) {
    const auto base=roaming();if(base.empty()){reason=L"无法定位当前用户配置目录";return false;}
    return supplementPreset(base/L"FxSound"/L"Presets"/presetFile,
        {std::filesystem::path(LR"(C:\SOFT\FxSound\FxSound\Presets)")/presetFile,std::filesystem::path(LR"(C:\SOFT\FxSound)")/presetFile,fallback/presetFile},copied,reason);
}
// JUCE 6/7 dispatchNextMessage handles WM_QUIT by invoking the app's
// systemRequestedQuit(), which in FxSound calls quit() and its normal shutdown.
// No WM_CLOSE on the main window (that only hides it), no foreground/UI actions.
inline bool gracefulExit(const FxIdentity& process,const std::function<bool()>& allowed) {
    if(!current(process))return processState()==ProcessState::stopped;
    DWORD thread=0;
    for(HWND window:windows(process.pid)) {
        wchar_t title[256]{},cls[256]{};GetWindowTextW(window,title,256);GetClassNameW(window,cls,256);
        if(wcscmp(title,L"JUCEWindow")||wcsncmp(cls,L"JUCE_",5))continue;
        DWORD pid=0;const auto candidate=GetWindowThreadProcessId(window,&pid);
        if(pid==process.pid){thread=candidate;break;}
    }
    if(!thread||!allowed()||!current(process))return false;
    HANDLE handle=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,process.pid);if(!handle)return false;
    bool ok=PostThreadMessageW(thread,WM_QUIT,0,0)!=FALSE;
    if(ok)for(int n=0;n<100&&allowed();++n){if(WaitForSingleObject(handle,100)==WAIT_OBJECT_0)break;}
    ok=ok&&WaitForSingleObject(handle,0)==WAIT_OBJECT_0;CloseHandle(handle);return ok;
}
inline std::string installedVersion() {
    const auto exe=executable();DWORD ignored=0;const auto size=GetFileVersionInfoSizeW(exe.c_str(),&ignored);if(!size||size>1024*1024)return {};
    std::vector<unsigned char> bytes(size);if(!GetFileVersionInfoW(exe.c_str(),0,size,bytes.data()))return {};
    VS_FIXEDFILEINFO* info=nullptr;UINT length=0;
    if(!VerQueryValueW(bytes.data(),L"\\",reinterpret_cast<void**>(&info),&length)||!info||length<sizeof(*info))return {};
    return std::to_string(HIWORD(info->dwFileVersionMS))+"."+std::to_string(LOWORD(info->dwFileVersionMS))+"."+std::to_string(HIWORD(info->dwFileVersionLS))+"."+std::to_string(LOWORD(info->dwFileVersionLS));
}
inline bool rewriteConfig(const std::string& before,const std::string& version,std::string& after,std::wstring& reason) {
    if(before.size()>1024*1024||version.empty()){reason=L"FxSound 配置或版本无法确认";return false;}
    if(!before.empty()) {
        ComPtr<IStream> stream;stream.Attach(SHCreateMemStream(reinterpret_cast<const BYTE*>(before.data()),static_cast<UINT>(before.size())));
        ComPtr<IXmlReader> xml;if(!stream||FAILED(CreateXmlReader(__uuidof(IXmlReader),reinterpret_cast<void**>(xml.GetAddressOf()),nullptr))||FAILED(xml->SetInput(stream.Get()))){reason=L"FxSound 配置不可读";return false;}
        xml->SetProperty(XmlReaderProperty_DtdProcessing,DtdProcessing_Prohibit);XmlNodeType node{};HRESULT hr=S_OK;
        while((hr=xml->Read(&node))==S_OK){}if(hr!=S_FALSE||before.find("<PROPERTIES>")==std::string::npos||before.find("</PROPERTIES>")==std::string::npos){reason=L"FxSound 配置格式不支持，未修改";return false;}
    }
    after=before.empty()?"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<PROPERTIES>\n</PROPERTIES>\n":before;
    const auto utf8=[](const std::wstring& value){int n=WideCharToMultiByte(CP_UTF8,0,value.data(),(int)value.size(),nullptr,0,nullptr,nullptr);std::string result(n,0);WideCharToMultiByte(CP_UTF8,0,value.data(),(int)value.size(),result.data(),n,nullptr,nullptr);return result;};
    auto put=[&](const std::string& name,const std::string& value){
        std::regex field("(<VALUE\\s+name=\""+name+"\"\\s+val=\")[^\"]*(\"\\s*/>)");
        std::smatch match;if(std::regex_search(after,match,field))after.replace(match.position(0)+match[1].length(),match[0].length()-match[1].length()-match[2].length(),value);
        else after.insert(after.find("</PROPERTIES>"),"  <VALUE name=\""+name+"\" val=\""+value+"\"/>\n");
    };
    put("preset",utf8(presetName));put("power","1");put("run_minimized","1");put("hide_notifications","1");
    // FxSound forces its changelog window and clears run_minimized on a version
    // mismatch. This bookkeeping value prevents that foreground path. No EQ,
    // output device, language, hotkey or other user parameter is changed.
    put("version",version);
    if(before.empty())put("survey_displayed","1");
    return true;
}
inline bool readConfigFile(const std::filesystem::path& path,std::string& bytes,bool& exists,std::wstring& reason) {
    bytes.clear();exists=false;
    HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE){
        const auto error=GetLastError();
        if(error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND)return true;
        reason=L"FxSound 配置读取失败（文件被占用或权限不足），未修改";return false;
    }
    exists=true;LARGE_INTEGER size{};
    if(!GetFileSizeEx(file,&size)||size.QuadPart<0||size.QuadPart>1024*1024){CloseHandle(file);reason=L"FxSound 配置大小无法确认，未修改";return false;}
    bytes.resize(static_cast<size_t>(size.QuadPart));DWORD read=0;
    const bool ok=bytes.empty()||ReadFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&read,nullptr)&&read==bytes.size();
    CloseHandle(file);
    if(!ok){bytes.clear();reason=L"FxSound 配置读取不完整，未修改";return false;}
    return true;
}
inline bool prepareConfigAt(const std::filesystem::path& path,const std::string& version,const std::function<bool()>& stopped,std::wstring& reason) {
    std::string before,after;bool existed=false;
    if(!readConfigFile(path,before,existed,reason))return false;
    if(existed&&before.empty()){reason=L"FxSound 现有配置为空，未重置";return false;}
    if(!rewriteConfig(before,version,after,reason))return false;
    if(after==before)return true;
    if(!stopped()){reason=L"FxSound 被其他入口启动，未写配置";return false;}
    auto temp=path;temp+=L".ymcc-"+std::to_wstring(GetCurrentProcessId())+L".tmp";
    bool written=false;
    {std::ofstream output(temp,std::ios::binary|std::ios::trunc);output.write(after.data(),after.size());output.flush();written=output.good();}
    if(!written){DeleteFileW(temp.c_str());reason=L"写入静默配置失败";return false;}
    // Re-read both existence and bytes. An absent file and an unreadable/empty
    // existing file are different states and must never share a reset path.
    if(!stopped()){DeleteFileW(temp.c_str());reason=L"FxSound 被其他入口启动，未写配置";return false;}
    std::string currentBytes;bool currentExists=false;
    if(!readConfigFile(path,currentBytes,currentExists,reason)){DeleteFileW(temp.c_str());return false;}
    if(currentExists!=existed||currentBytes!=before){DeleteFileW(temp.c_str());reason=L"FxSound 配置已被其他入口更改";return false;}
    if(!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){DeleteFileW(temp.c_str());reason=L"保存静默配置失败";return false;}
    return true;
}
inline bool prepareConfig(std::wstring& reason) {
    return prepareConfigAt(roaming()/L"FxSound"/L"FxSound.settings",installedVersion(),[]{return processState()==ProcessState::stopped;},reason);
}
inline bool restoreStoppedOutput(ControlResult& result,const std::function<bool()>& allowed) {
    // Run the exact existing Settings repair consumer after graceful exit. A
    // simultaneous startup/manual launch cancels recovery rather than racing it.
    std::atomic<bool> repairEnabled{true};ComPtr<IMMDeviceEnumerator> enumerator;
    if(FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&enumerator)))){result.reason=L"无法读取音频设备";return false;}
    WindowsPort port(enumerator.Get(),repairEnabled);Recovery repair;
    while(!repair.finished&&allowed()){
        for(const auto& attempt:repair.step(port,GetTickCount64(),true)){if(!attempt.ok){result.reason=L"FxSound 已退出，系统扬声器修复失败";return false;}result.repaired=true;}
        if(!repair.finished)Sleep(250);
    }
    result.state=observe();
    if(!allowed()){result.reason=L"操作已取消";return false;}
    if(!result.state.known||result.state.running){result.reason=L"FxSound 已被开机项或其他入口重新启动";return false;}
    if(!result.state.audio.valid||result.state.audio.hasFxDefault()){result.reason=L"FxSound 已退出，扬声器恢复待确认";return false;}
    const auto* output=result.state.audio.find(result.state.audio.defaults[1]);
    if(!output||!output->usable){result.reason=L"系统默认输出不可用";return false;}
    return true;
}
inline ControlResult setEnabled(bool enabled,const std::filesystem::path& fallback,const std::function<bool()>& allowed=[] {return true;}) {
    static std::mutex transaction;std::unique_lock<std::mutex> lock(transaction,std::try_to_lock);
    ControlResult result;if(!lock.owns_lock()){result.reason=L"音质开关正在处理";return result;}
    auto finish=[&](const std::wstring& reason){
        result.reason=reason;result.state=observe();
        // A failed ON may have normally closed an older instance. Its virtual
        // default must not be left without a process after a config/start error.
        if(enabled&&!result.state.running&&result.state.audio.valid&&result.state.audio.hasFxDefault()&&allowed()){
            const auto original=result.reason;
            if(restoreStoppedOutput(result,allowed))result.reason=original+L"；系统扬声器已恢复";
            else result.reason=original+L"；"+result.reason;
        }
        return result;
    };
    if(!allowed())return finish(L"操作已取消");
    auto process=identity();result.reused=process.pid!=0;
    if(enabled) {
        if(!ensurePreset(fallback,result.copied,result.reason))return finish(result.reason);
        if(executable().empty())return finish(L"未安装 FxSound");
        const auto existing=observe();if(existing.enabled){result.ok=true;result.state=existing;result.reason=L"音质优化已开启";return result;}
        if(processState()==ProcessState::running&&!process.pid)return finish(L"FxSound 实例身份待确认");
        if(process.pid&&!gracefulExit(process,allowed))return finish(L"FxSound 后台正常退出失败");
        if(!allowed())return finish(L"操作已取消");
        if(!prepareConfig(result.reason))return finish(result.reason);
        if(processState()!=ProcessState::stopped)return finish(L"FxSound 已被其他入口启动，请重试");
        auto path=executable();auto parameters=std::wstring(L"--preset=\"")+presetName+L"\"";
        SHELLEXECUTEINFOW info{sizeof(info)};info.fMask=SEE_MASK_NOCLOSEPROCESS|SEE_MASK_FLAG_NO_UI;info.lpVerb=L"open";info.lpFile=path.c_str();info.lpParameters=parameters.c_str();info.nShow=SW_HIDE;
        if(!allowed()||!ShellExecuteExW(&info))return finish(L"系统启动 FxSound 失败");if(info.hProcess)CloseHandle(info.hProcess);
        for(int verify=0;verify<150&&allowed();++verify){Sleep(100);result.state=observe();if(result.state.enabled){result.ok=true;result.reason=L"音质优化已开启";return result;}}
        return finish(L"FxSound 已收到请求，实际音效或输出尚未确认");
    }
    if(processState()==ProcessState::running&&!process.pid)return finish(L"FxSound 实例身份待确认，未退出或修复");
    if(process.pid&&!gracefulExit(process,allowed))return finish(L"FxSound 后台正常退出失败");
    if(!allowed())return finish(L"操作已取消");
    if(!restoreStoppedOutput(result,allowed))return finish(result.reason);
    result.ok=true;result.reason=L"音质优化已关闭，系统扬声器已恢复";return result;
}
}
