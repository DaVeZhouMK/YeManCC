#pragma once
// FxSound 1.2.6+ controller. Processing and preset application stay in FxSound.
// Uses shell launch, settings and JUCE normal quit; no menus, UIA or input.
#include "fxsound_recovery.h"

#include <shellapi.h>
#include <shlobj.h>
#include <knownfolders.h>
#include <set>
#include <cstdint>
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
    DWORD session=0;if(!ProcessIdToSessionId(GetCurrentProcessId(),&session))return {};
    try {
        const auto snapshot=ymcc::processInventory::read(ymcc::processInventory::resolveQuery(),
            ymcc::processInventory::currentTime,0);
        if(snapshot->status!=ymcc::processInventory::Status::Complete)return {};
        for(const auto& entry:snapshot->rows) {
            if(_wcsicmp(entry.name.c_str(),L"FxSound.exe"))continue;
            DWORD owner=0;if(!ProcessIdToSessionId(entry.pid,&owner)||owner!=session)continue;
            HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,entry.pid);if(!process)continue;
            wchar_t name[32768]{};DWORD n=32768; FILETIME created{},ended{},kernel{},user{};
            DWORD heldSession=0;FxIdentity result;
            if(GetProcessId(process)==entry.pid&&ProcessIdToSessionId(entry.pid,&heldSession)&&heldSession==session&&
                WaitForSingleObject(process,0)==WAIT_TIMEOUT&&QueryFullProcessImageNameW(process,0,name,&n)&&
                _wcsicmp(name,expected.c_str())==0&&GetProcessTimes(process,&created,&ended,&kernel,&user))
                result={entry.pid,(ULONGLONG(created.dwHighDateTime)<<32)|created.dwLowDateTime};
            CloseHandle(process);if(result.pid)return result;
        }
    } catch (...) {}
    return {};
}
inline bool current(const FxIdentity& expected) {auto now=identity();return now.pid==expected.pid&&now.birth==expected.birth&&now.pid;}
inline std::vector<HWND> windows(DWORD pid) {
    struct Data {DWORD pid;std::vector<HWND> result;} data{pid,{}};
    EnumWindows([](HWND window,LPARAM raw)->BOOL{auto& d=*reinterpret_cast<Data*>(raw);DWORD pid=0;GetWindowThreadProcessId(window,&pid);if(pid==d.pid)d.result.push_back(window);return TRUE;},reinterpret_cast<LPARAM>(&data));
    return data.result;
}
// FxSound owns every unrecognized setting and opaque FAC byte. Only the five
// silent-control VALUEs are edited; XML validation precedes all output changes.
struct FxConfigValue { std::wstring key,value; bool hasValue=false; };
struct FxConfigXml { std::vector<FxConfigValue> children; };
inline bool fxConfigAttribute(IXmlReader* xml,const wchar_t* name,std::wstring& value) {
    const wchar_t* text=nullptr;const auto moved=xml->MoveToAttributeByName(name,nullptr);
    const bool ok=moved==S_OK&&xml->GetValue(&text,nullptr)==S_OK&&text;if(ok)value=text;xml->MoveToElement();return ok;
}
inline bool fxConfigXml(const std::string& bytes,FxConfigXml& output,bool* unsupported=nullptr) {
    if(unsupported)*unsupported=false;
    if(bytes.empty()||bytes.size()>1024*1024)return false;
    ComPtr<IStream> stream;stream.Attach(SHCreateMemStream(reinterpret_cast<const BYTE*>(bytes.data()),static_cast<UINT>(bytes.size())));
    ComPtr<IXmlReader> xml;if(!stream||FAILED(CreateXmlReader(__uuidof(IXmlReader),reinterpret_cast<void**>(xml.GetAddressOf()),nullptr))||FAILED(xml->SetInput(stream.Get()))||
        FAILED(xml->SetProperty(XmlReaderProperty_DtdProcessing,DtdProcessing_Prohibit))||FAILED(xml->SetProperty(XmlReaderProperty_MaxElementDepth,64)))return false;
    FxConfigXml parsed;bool root=false;XmlNodeType node{};HRESULT hr=S_OK;unsigned nodes=0;
    while((hr=xml->Read(&node))==S_OK){if(++nodes>65536)return false;if(node!=XmlNodeType_Element)continue;
        UINT depth=0;const wchar_t* name=nullptr;const wchar_t* ns=nullptr;
        if(FAILED(xml->GetDepth(&depth))||FAILED(xml->GetLocalName(&name,nullptr))||FAILED(xml->GetNamespaceUri(&ns,nullptr)))return false;
        if(depth==0){if(root)return false;if(!name||wcscmp(name,L"PROPERTIES")||(ns&&*ns)){if(unsupported)*unsupported=true;return false;}root=true;}
        if(depth==1){FxConfigValue field;if(name&&!wcscmp(name,L"VALUE")){std::wstring key;fxConfigAttribute(xml.Get(),L"name",key);
            if(ns&&*ns&&(key==L"preset"||key==L"power"||key==L"run_minimized"||key==L"hide_notifications"||key==L"version")){if(unsupported)*unsupported=true;return false;}
            if(!ns||!*ns){field.key=std::move(key);field.hasValue=fxConfigAttribute(xml.Get(),L"val",field.value);}}parsed.children.push_back(std::move(field));}
    }
    if(hr!=S_FALSE||!root)return false;output=std::move(parsed);return true;
}
inline bool fxConfigKnownEncoding(const std::string& bytes) {
    if(bytes.starts_with(std::string("\xff\xfe",2))||bytes.starts_with(std::string("\xfe\xff",2))||
        (bytes.size()>=4&&((bytes[0]=='<'&&bytes[1]==0&&bytes[3]==0)||(bytes[0]==0&&bytes[1]=='<'&&bytes[2]==0))))return true;
    const std::size_t start=bytes.starts_with(std::string("\xef\xbb\xbf",3))?3:0;if(bytes.compare(start,5,"<?xml"))return true;
    const auto end=bytes.find("?>",start+5);if(end==std::string::npos)return true;if(end>1024)return false;
    auto index=bytes.find("encoding",start+5);if(index==std::string::npos||index>=end)return true;index+=8;
    const auto space=[](char c){return c==' '||c=='\r'||c=='\n'||c=='\t';};while(index<end&&space(bytes[index]))++index;if(index>=end||bytes[index++]!='=')return false;while(index<end&&space(bytes[index]))++index;if(index>=end||(bytes[index]!='\''&&bytes[index]!='\"'))return false;
    const auto quote=bytes[index++];std::string encoding;while(index<end&&bytes[index]!=quote){char c=bytes[index++];if(c>='A'&&c<='Z')c=static_cast<char>(c-'A'+'a');encoding+=c;}
    return index<end&&(encoding=="utf-8"||encoding=="utf-16"||encoding=="utf-16le"||encoding=="utf-16be");
}
inline bool fxConfigVersion(const std::string& version) {
    if(version.empty()||version.size()>23)return false;unsigned parts=0,value=0,digits=0;
    for(const auto c:version){if(c=='.'){if(!digits||++parts>3)return false;value=0;digits=0;}
        else{if(c<'0'||c>'9'||++digits>5)return false;value=value*10+static_cast<unsigned>(c-'0');if(value>65535)return false;}}
    return parts==3&&digits>0;
}
struct FxConfigText { std::wstring text; int encoding=0; bool bom=false; }; // 0=UTF8,1=UTF16LE,2=UTF16BE
inline bool fxConfigDecode(const std::string& raw,FxConfigText& decoded) {
    FxConfigText out;std::size_t offset=0;
    if(raw.starts_with(std::string("\xff\xfe",2))){out.encoding=1;out.bom=true;offset=2;}
    else if(raw.starts_with(std::string("\xfe\xff",2))){out.encoding=2;out.bom=true;offset=2;}
    else if(raw.size()>=4&&raw[0]=='<'&&raw[1]==0&&raw[3]==0)out.encoding=1;
    else if(raw.size()>=4&&raw[0]==0&&raw[1]=='<'&&raw[2]==0)out.encoding=2;
    else if(raw.starts_with(std::string("\xef\xbb\xbf",3))){out.bom=true;offset=3;}
    if(out.encoding){if((raw.size()-offset)%2)return false;for(auto i=offset;i<raw.size();i+=2){const auto a=static_cast<unsigned char>(raw[i]),b=static_cast<unsigned char>(raw[i+1]);out.text.push_back(static_cast<wchar_t>(out.encoding==1?(a|(b<<8)):((a<<8)|b)));}}
    else{const auto count=static_cast<int>(raw.size()-offset);const int size=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,raw.data()+offset,count,nullptr,0);if(size<=0)return false;out.text.resize(size);if(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,raw.data()+offset,count,out.text.data(),size)!=size)return false;}
    if(!out.text.empty()&&WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,out.text.data(),static_cast<int>(out.text.size()),nullptr,0,nullptr,nullptr)<=0)return false;
    decoded=std::move(out);return true;
}
inline bool fxConfigEncode(const FxConfigText& text,std::string& output) {
    std::string out;if(text.encoding){if(text.bom)out=text.encoding==1?std::string("\xff\xfe",2):std::string("\xfe\xff",2);
        for(const auto c:text.text){out.push_back(static_cast<char>(text.encoding==1?c&255:(c>>8)&255));out.push_back(static_cast<char>(text.encoding==1?(c>>8)&255:c&255));}}
    else{const int size=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.text.data(),static_cast<int>(text.text.size()),nullptr,0,nullptr,nullptr);if(size<=0)return false;if(text.bom)out="\xef\xbb\xbf";const auto start=out.size();out.resize(start+size);if(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.text.data(),static_cast<int>(text.text.size()),out.data()+start,size,nullptr,nullptr)!=size)return false;}
    output=std::move(out);return true;
}
inline std::wstring fxConfigEscape(const std::wstring& text){std::wstring out;for(const auto c:text){switch(c){case L'&':out+=L"&amp;";break;case L'<':out+=L"&lt;";break;case L'\"':out+=L"&quot;";break;case L'\'':out+=L"&apos;";break;default:out+=c;}}return out;}
struct FxConfigSpan { std::size_t begin=0,end=0,insert=0,valueBegin=0,valueEnd=0;bool hasValue=false; };
inline bool fxConfigSpans(const std::wstring& text,std::vector<FxConfigSpan>& spans,std::size_t& closing,bool& emptyRoot) {
    std::size_t pos=0,depth=0,active=std::wstring::npos;closing=std::wstring::npos;emptyRoot=false;
    const auto ws=[](wchar_t c){return c==L' '||c==L'\r'||c==L'\n'||c==L'\t';};
    while((pos=text.find(L'<',pos))!=std::wstring::npos){
        if(text.compare(pos,4,L"<!--")==0){const auto end=text.find(L"-->",pos+4);if(end==std::wstring::npos)return false;pos=end+3;continue;}
        if(text.compare(pos,9,L"<![CDATA[")==0){const auto end=text.find(L"]]>",pos+9);if(end==std::wstring::npos)return false;pos=end+3;continue;}
        if(text.compare(pos,2,L"<?")==0){const auto end=text.find(L"?>",pos+2);if(end==std::wstring::npos)return false;pos=end+2;continue;}
        std::size_t end=pos+1;wchar_t quote=0;
        for(;end<text.size();++end){const auto c=text[end];if(quote){if(c==quote)quote=0;}else if(c==L'\''||c==L'\"')quote=c;else if(c==L'>')break;}
        if(end==text.size())return false;const bool ending=text[pos+1]==L'/',selfClosing=end>pos&&text[end-1]==L'/';
        if(ending){if(!depth)return false;if(depth==1)closing=pos;if(depth==2&&active!=std::wstring::npos){spans[active].end=end+1;active=std::wstring::npos;}--depth;}
        else{std::size_t nameEnd=pos+1;while(nameEnd<end&&!ws(text[nameEnd])&&text[nameEnd]!=L'/')++nameEnd;
            if(depth==0&&selfClosing){closing=end-1;emptyRoot=true;}
            if(depth==1){FxConfigSpan span;span.begin=pos;span.insert=selfClosing?end-1:end;span.end=selfClosing?end+1:0;std::size_t i=nameEnd;
                while(i<span.insert){while(i<span.insert&&ws(text[i]))++i;if(i>=span.insert)break;const auto keyBegin=i;while(i<span.insert&&!ws(text[i])&&text[i]!=L'=')++i;const auto key=text.substr(keyBegin,i-keyBegin);while(i<span.insert&&ws(text[i]))++i;if(i>=span.insert||text[i++]!=L'=')return false;while(i<span.insert&&ws(text[i]))++i;if(i>=span.insert||(text[i]!=L'\''&&text[i]!=L'\"'))return false;const auto delimiter=text[i++];const auto begin=i;while(i<span.insert&&text[i]!=delimiter)++i;if(i>=span.insert)return false;if(key==L"val"){span.hasValue=true;span.valueBegin=begin;span.valueEnd=i;}++i;}
                spans.push_back(span);if(!selfClosing)active=spans.size()-1;}
            if(!selfClosing)++depth;
        }pos=end+1;
    }return depth==0&&closing!=std::wstring::npos;
}

struct FxConfigImage {
    enum class Kind { missing,readable,oversized,unreadable } kind=Kind::unreadable;
    std::string bytes;BY_HANDLE_FILE_INFORMATION info{};
};
inline bool fxConfigSameInfo(const BY_HANDLE_FILE_INFORMATION& a,const BY_HANDLE_FILE_INFORMATION& b){return a.dwVolumeSerialNumber==b.dwVolumeSerialNumber&&a.nFileIndexHigh==b.nFileIndexHigh&&a.nFileIndexLow==b.nFileIndexLow&&a.nFileSizeHigh==b.nFileSizeHigh&&a.nFileSizeLow==b.nFileSizeLow&&a.ftLastWriteTime.dwHighDateTime==b.ftLastWriteTime.dwHighDateTime&&a.ftLastWriteTime.dwLowDateTime==b.ftLastWriteTime.dwLowDateTime;}
inline bool fxConfigSameImage(const FxConfigImage& a,const FxConfigImage& b){return a.kind==b.kind&&(a.kind==FxConfigImage::Kind::missing||(fxConfigSameInfo(a.info,b.info)&&a.bytes==b.bytes));}
inline FxConfigImage fxConfigImage(const std::filesystem::path& path) {
    FxConfigImage result;HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(file==INVALID_HANDLE_VALUE){const auto error=GetLastError();if(error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND)result.kind=FxConfigImage::Kind::missing;return result;}
    struct Close {HANDLE file;~Close(){CloseHandle(file);}} close{file};
    if(GetFileType(file)!=FILE_TYPE_DISK||!GetFileInformationByHandle(file,&result.info)||(result.info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)))return result;
    if(result.info.nFileSizeHigh||result.info.nFileSizeLow>1024*1024){result.kind=FxConfigImage::Kind::oversized;return result;}
    result.bytes.resize(result.info.nFileSizeLow);DWORD read=0;
    if(!ReadFile(file,result.bytes.data(),static_cast<DWORD>(result.bytes.size()),&read,nullptr)||read!=result.bytes.size()){result.bytes.clear();return result;}
    BY_HANDLE_FILE_INFORMATION after{};if(!GetFileInformationByHandle(file,&after)||!fxConfigSameInfo(result.info,after)){result.bytes.clear();return result;}
    result.kind=FxConfigImage::Kind::readable;return result;
}
inline bool readConfigFile(const std::filesystem::path& path,std::string& bytes,bool& exists,std::wstring& reason) {
    bytes.clear();exists=false;const auto image=fxConfigImage(path);
    if(image.kind==FxConfigImage::Kind::missing)return true;
    if(image.kind==FxConfigImage::Kind::unreadable){reason=L"FxSound 配置读取失败（文件被占用或权限不足），未修改";return false;}
    exists=true;if(image.kind==FxConfigImage::Kind::oversized){reason=L"FxSound 配置大小无法确认，未修改";return false;}
    bytes=image.bytes;return true;
}
inline bool fxConfigState(const std::string& bytes,std::wstring& preset,bool& power) {
    FxConfigXml parsed;if(!fxConfigXml(bytes,parsed))return false;std::wstring selected;bool enabled=false,havePreset=false,havePower=false;
    for(const auto& value:parsed.children){if(value.key==L"preset"){if(havePreset||!value.hasValue)return false;havePreset=true;selected=value.value;}
        else if(value.key==L"power"){if(havePower||!value.hasValue||(value.value!=L"0"&&value.value!=L"1"))return false;havePower=true;enabled=value.value==L"1";}}
    if(!havePreset||!havePower)return false;preset=std::move(selected);power=enabled;return true;
}
struct FxConfigLock {
    HANDLE handle=nullptr;bool held=false;
    explicit FxConfigLock(const std::filesystem::path& path){auto text=std::filesystem::absolute(path).lexically_normal().wstring();std::wstring lower(text.size(),L'\0');
        if(LCMapStringEx(LOCALE_NAME_INVARIANT,LCMAP_LOWERCASE,text.data(),static_cast<int>(text.size()),lower.data(),static_cast<int>(lower.size()),nullptr,nullptr,0)!=static_cast<int>(lower.size()))return;
        std::uint64_t hash=14695981039346656037ULL;for(const auto c:lower){hash^=static_cast<std::uint16_t>(c);hash*=1099511628211ULL;}
        const auto name=L"Local\\YMCCFxConfig-v1-"+std::to_wstring(hash);handle=CreateMutexW(nullptr,FALSE,name.c_str());if(!handle)return;const auto wait=WaitForSingleObject(handle,2000);held=wait==WAIT_OBJECT_0||wait==WAIT_ABANDONED;
    }
    ~FxConfigLock(){if(held)ReleaseMutex(handle);if(handle)CloseHandle(handle);}
};
inline std::wstring fxConfigSuffix(){static std::atomic<unsigned long long> serial{0};return std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L"-"+std::to_wstring(++serial);}

inline bool settings(std::wstring& preset,bool& power) {
    const auto base=roaming();if(base.empty())return false;
    std::string bytes;bool exists=false;std::wstring reason;
    return readConfigFile(base/L"FxSound"/L"FxSound.settings",bytes,exists,reason)&&exists&&fxConfigState(bytes,preset,power);
}
inline ControlState observe(ULONGLONG maxAge = 0) {
    ControlState state;state.installed=!executable().empty();
    const auto process=processState(maxAge);
    std::atomic<bool> enabled{true};ComPtr<IMMDeviceEnumerator> enumerator;
    if(SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&enumerator)))){WindowsPort port(enumerator.Get(),enabled);state.audio=port.read(process);}
    state.running=process==ProcessState::running;
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
    copied=false;const auto regular=[](DWORD flags){return flags!=INVALID_FILE_ATTRIBUTES&&!(flags&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT));};
    const auto current=GetFileAttributesW(target.c_str());if(regular(current))return true;
    if(current!=INVALID_FILE_ATTRIBUTES){reason=L"预设路径不是普通文件";return false;}
    const auto error=GetLastError();if(error!=ERROR_FILE_NOT_FOUND&&error!=ERROR_PATH_NOT_FOUND){reason=L"预设路径无法读取，未修改";return false;}
    std::error_code directoryError;std::filesystem::create_directories(target.parent_path(),directoryError);if(directoryError){reason=L"无法创建预设目录";return false;}
    for(const auto& source:sources){if(!regular(GetFileAttributesW(source.c_str())))continue;
        if(CopyFileW(source.c_str(),target.c_str(),TRUE)){copied=true;return true;}
        const auto copyError=GetLastError();if(copyError==ERROR_FILE_EXISTS||copyError==ERROR_ALREADY_EXISTS)return regular(GetFileAttributesW(target.c_str()));
        reason=L"补齐预设失败";return false;
    }reason=L"缺少移动端通用音频优化预设备份";return false;
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
    if(before.size()>1024*1024||!fxConfigVersion(version)||(!before.empty()&&!fxConfigKnownEncoding(before))){reason=L"FxSound 配置或版本无法确认";return false;}
    const bool fresh=before.empty();const std::string seed="<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<PROPERTIES>\n</PROPERTIES>\n";FxConfigXml parsed;
    const auto& input=fresh?seed:before;if(!fxConfigXml(input,parsed)){reason=L"FxSound 配置格式不支持，未修改";return false;}
    FxConfigText text;if(!fxConfigDecode(input,text)){reason=L"FxSound 配置编码暂不支持，未修改";return false;}
    std::vector<FxConfigSpan> spans;std::size_t closing=0;bool emptyRoot=false;
    if(!fxConfigSpans(text.text,spans,closing,emptyRoot)||spans.size()!=parsed.children.size()){reason=L"FxSound 配置结构无法确认，未修改";return false;}
    std::vector<std::pair<std::wstring,std::wstring>> desired{{L"preset",presetName},{L"power",L"1"},{L"run_minimized",L"1"},{L"hide_notifications",L"1"},{L"version",std::wstring(version.begin(),version.end())}};
    if(fresh)desired.push_back({L"survey_displayed",L"1"});
    struct Edit{std::size_t begin,length;std::wstring replacement;};std::vector<Edit> edits;std::set<std::wstring> seen;
    for(std::size_t i=0;i<spans.size();++i){const auto& value=parsed.children[i];const auto target=std::find_if(desired.begin(),desired.end(),[&](const auto& item){return item.first==value.key;});if(target==desired.end())continue;const auto& span=spans[i];
        if(!seen.insert(value.key).second){edits.push_back({span.begin,span.end-span.begin,L""});continue;}
        const auto escaped=fxConfigEscape(target->second);if(span.hasValue)edits.push_back({span.valueBegin,span.valueEnd-span.valueBegin,escaped});else edits.push_back({span.insert,0,L" val=\""+escaped+L"\""});
    }
    std::wstring missing;for(const auto& item:desired)if(!seen.contains(item.first))missing+=L"  <VALUE name=\""+item.first+L"\" val=\""+fxConfigEscape(item.second)+L"\"/>\n";
    if(emptyRoot)edits.push_back({closing,2,L">"+missing+L"</PROPERTIES>"});else if(!missing.empty())edits.push_back({closing,0,missing});
    std::sort(edits.begin(),edits.end(),[](const Edit& a,const Edit& b){return a.begin>b.begin;});for(const auto& edit:edits)text.text.replace(edit.begin,edit.length,edit.replacement);
    std::string candidate;FxConfigXml verified;
    if(!fxConfigEncode(text,candidate)||candidate.size()>1024*1024||!fxConfigXml(candidate,verified)){reason=L"FxSound 静默配置验证失败，未修改";return false;}
    for(const auto& target:desired){unsigned count=0;for(const auto& value:verified.children)if(value.key==target.first){++count;if(!value.hasValue||value.value!=target.second){reason=L"FxSound 静默配置回读不一致";return false;}}if(count!=1){reason=L"FxSound 静默配置字段不唯一";return false;}}
    after=std::move(candidate);return true;
}
inline bool prepareConfigAt(const std::filesystem::path& path,const std::string& version,const std::function<bool()>& stopped,std::wstring& reason) {
    struct Temporary{std::filesystem::path path;bool owned=false;~Temporary(){if(owned)DeleteFileW(path.c_str());}} temporary;
    try{
        if(!path.is_absolute()||!fxConfigVersion(version)){reason=L"FxSound 配置路径或版本无法确认";return false;}
        FxConfigLock lock(path);if(!lock.held){reason=L"FxSound 配置正在处理，请稍后重试";return false;}
        const auto before=fxConfigImage(path);if(before.kind==FxConfigImage::Kind::unreadable){reason=L"FxSound 配置读取失败（文件被占用或权限不足），未修改";return false;}
        std::string after;FxConfigXml parsed;bool unsupported=false;
        const bool valid=before.kind==FxConfigImage::Kind::readable&&fxConfigXml(before.bytes,parsed,&unsupported);
        if(unsupported||(before.kind==FxConfigImage::Kind::readable&&!before.bytes.empty()&&!fxConfigKnownEncoding(before.bytes))){reason=L"FxSound 配置格式或编码暂不支持，原件未修改";return false;}
        const bool recover=before.kind==FxConfigImage::Kind::oversized||(before.kind==FxConfigImage::Kind::readable&&!valid);
        if(!rewriteConfig(recover?std::string():before.bytes,version,after,reason))return false;
        if(!recover&&before.kind==FxConfigImage::Kind::readable&&before.bytes==after)return true;
        if(!stopped()){reason=L"FxSound 被其他入口启动，未写配置";return false;}
        std::error_code error;std::filesystem::create_directories(path.parent_path(),error);if(error){reason=L"无法创建 FxSound 配置目录";return false;}
        temporary.path=path.wstring()+L".ymcc-"+fxConfigSuffix()+L".tmp";HANDLE file=CreateFileW(temporary.path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file==INVALID_HANDLE_VALUE){reason=L"写入静默配置失败";return false;}temporary.owned=true;
        DWORD written=0;const bool saved=WriteFile(file,after.data(),static_cast<DWORD>(after.size()),&written,nullptr)&&written==after.size()&&FlushFileBuffers(file);CloseHandle(file);
        if(!saved){reason=L"写入静默配置失败";return false;}
        if(!stopped()){reason=L"FxSound 被其他入口启动，未写配置";return false;}
        if(!fxConfigSameImage(before,fxConfigImage(path))){reason=L"FxSound 配置已被其他入口更改";return false;}
        std::filesystem::path archive;
        if(recover){archive=path.wstring()+L".ymcc-invalid-"+fxConfigSuffix();
            if(!MoveFileExW(path.c_str(),archive.c_str(),MOVEFILE_WRITE_THROUGH)){reason=L"无法保留损坏的 FxSound 配置，未重置";return false;}
            if(!fxConfigSameImage(before,fxConfigImage(archive))||fxConfigImage(path).kind!=FxConfigImage::Kind::missing){reason=L"FxSound 原配置归档尚未确认";return false;}}
        if(!stopped()){
            if(!archive.empty()&&fxConfigImage(path).kind==FxConfigImage::Kind::missing)MoveFileExW(archive.c_str(),path.c_str(),MOVEFILE_WRITE_THROUGH);
            reason=L"FxSound 在保存前重新启动，未写配置";return false;}
        if((!recover&&!fxConfigSameImage(before,fxConfigImage(path)))||(recover&&(fxConfigImage(path).kind!=FxConfigImage::Kind::missing||!fxConfigSameImage(before,fxConfigImage(archive))))){reason=L"FxSound 配置在发布前改变，原记录保留";return false;}
        const DWORD flags=MOVEFILE_WRITE_THROUGH|(!recover&&before.kind==FxConfigImage::Kind::readable?MOVEFILE_REPLACE_EXISTING:0);
        if(!MoveFileExW(temporary.path.c_str(),path.c_str(),flags)){
            // A foreign newly created path is never overwritten by rollback.
            if(!archive.empty()&&fxConfigImage(path).kind==FxConfigImage::Kind::missing)MoveFileExW(archive.c_str(),path.c_str(),MOVEFILE_WRITE_THROUGH);
            reason=L"保存静默配置失败";return false;}
        temporary.owned=false;const auto actual=fxConfigImage(path);std::wstring preset;bool power=false;
        if(actual.kind!=FxConfigImage::Kind::readable||actual.bytes!=after||!fxConfigState(actual.bytes,preset,power)||preset!=presetName||!power){reason=L"FxSound 配置保存后回读未确认";return false;}
        return true;
    }catch(...){reason=L"FxSound 配置处理失败，原记录保留待重试";return false;}
}
inline bool prepareConfig(std::wstring& reason) {
    const auto base=roaming();if(base.empty()){reason=L"无法定位当前用户配置目录";return false;}
    return prepareConfigAt(base/L"FxSound"/L"FxSound.settings",installedVersion(),[]{return processState()==ProcessState::stopped;},reason);
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
