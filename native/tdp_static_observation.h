#pragma once
// Static evidence from existing operations/files only. No process launch,
// register access, controller decisions, polling thread, or control state.
#include <windows.h>
#include <array>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <regex>
#include <string>
#include <vector>
#include "json.hpp"
namespace ymcc::tdpdiag {
using Json = nlohmann::json;
namespace fs = std::filesystem;
inline std::string boundedText(std::string value, size_t limit = 4096) {
    try {
        // Broken/bootstrap stderr may end in an incomplete UTF-8 sequence.
        // Preserve it with replacement markers rather than losing the entire JSON row.
        value = Json::parse(Json(value).dump(-1, ' ', false, Json::error_handler_t::replace)).get<std::string>();
        value = std::regex_replace(value, std::regex(R"((?:[A-Za-z]:[\\/]+Users[\\/]+)[^\\/\r\n\"']+)", std::regex::icase), "<USER>");
        value = std::regex_replace(value, std::regex(R"((authorization[\"'\s]*[:=][\"'\s]*(?:bearer\s+)?)[^\r\n,}\"']+)", std::regex::icase), "$1<REDACTED>");
        value = std::regex_replace(value, std::regex(R"((bearer\s+)[A-Za-z0-9._~+/=-]+)", std::regex::icase), "$1<REDACTED>");
        value = std::regex_replace(value, std::regex(R"((sessionToken|authorization|pipeSecret|password|secret)[\"'\s]*[:=][\"'\s]*[^\s,\r\n}\"]+)", std::regex::icase), "$1=<REDACTED>");
    } catch (...) { return "redaction-error"; }
    if (value.size() > limit) {
        size_t end = limit;
        while (end && (static_cast<unsigned char>(value[end]) & 0xc0) == 0x80) --end;
        value.resize(end);
    }
    return value;
}
inline std::string bootstrapKind(const std::string& text, bool processStarted) {
    if (!processStarted) return "process-not-started";
    if (text.find("encodings") != std::string::npos && (text.find("No module named") != std::string::npos || text.find("ModuleNotFoundError") != std::string::npos)) return "encodings-import-failed";
    if (text.find("Failed to start embedded python") != std::string::npos || text.find("Failed to initialize Python") != std::string::npos) return "embedded-python-start-failed";
    if (text.find("DLL load") != std::string::npos || text.find("Failed to load Python DLL") != std::string::npos) return "python-dll-load-failed";
    return "not-observed"; // usage rc=2 is not bootstrap failure
}
inline std::uint16_t u16(const unsigned char* p) { return p[0] | (p[1] << 8); }
inline std::uint32_t u32(const unsigned char* p) { return u16(p) | (static_cast<std::uint32_t>(u16(p+2)) << 16); }
inline Json encodingsZip(const fs::path& path) {
    Json result={{"status","not-checked"},{"entries",Json::object()}};
    std::ifstream in(path,std::ios::binary); if(!in){result["status"]="zip-open-failed";return result;}
    in.seekg(0,std::ios::end);const auto size=in.tellg();if(size<22){result["status"]="zip-open-failed";return result;}
    const size_t tailSize=static_cast<size_t>((std::min)(static_cast<std::streamoff>(size),static_cast<std::streamoff>(65557)));
    std::vector<unsigned char> tail(tailSize);in.seekg(static_cast<std::streamoff>(size)-tailSize);in.read(reinterpret_cast<char*>(tail.data()),tailSize);
    size_t eocd=tailSize;for(size_t i=tailSize-22;;--i){if(u32(tail.data()+i)==0x06054b50 && i+22+u16(tail.data()+i+20)==tailSize){eocd=i;break;}if(!i)break;}
    if(eocd==tailSize){result["status"]="zip-open-failed";return result;}
    const auto* e=tail.data()+eocd;const auto count=u16(e+10); const auto centralBytes=u32(e+12); const auto offset=u32(e+16);
    if(u16(e+4)||u16(e+6)||centralBytes>1024*1024||static_cast<std::uint64_t>(offset)+centralBytes>static_cast<std::uint64_t>(size)) {result["status"]="not-checked";return result;}
    std::vector<unsigned char> central(centralBytes);in.clear();in.seekg(offset);in.read(reinterpret_cast<char*>(central.data()),centralBytes);
    if(!in){result["status"]="zip-open-failed";return result;}
    const std::array<std::string,3> wanted{"encodings/__init__.pyc","encodings/aliases.pyc","encodings/utf_8.pyc"};
    std::array<bool,3> found{};size_t cursor=0;
    for(unsigned i=0;i<count;++i){
        if(cursor+46>central.size()||u32(central.data()+cursor)!=0x02014b50){result["status"]="zip-open-failed";return result;}
        const auto* h=central.data()+cursor;const size_t nameBytes=u16(h+28),extra=u16(h+30),comment=u16(h+32);
        if(cursor+46+nameBytes+extra+comment>central.size()){result["status"]="zip-open-failed";return result;}
        const std::string name(reinterpret_cast<const char*>(h+46),nameBytes);
        for(size_t j=0;j<wanted.size();++j)if(name==wanted[j])found[j]=true;
        cursor+=46+nameBytes+extra+comment;
    }
    bool complete=true;for(size_t i=0;i<wanted.size();++i){result["entries"][wanted[i]]=found[i];complete=complete&&found[i];}
    result["status"]=complete?"ok":"encodings-entry-missing";return result;
}
class RuntimeIdentityCache {
public:
    Json snapshot(const fs::path& exe, std::uint64_t now, bool failure, const std::function<std::string(const std::wstring&)>& sha) {
        // Filesystem metadata is checked at most every 30s of ACTUAL requests;
        // no timer, no monitor ticks. One extra snapshot per failed identity.
        if(exe==path_ && known_ && now-lastChecked_<30000 && (!failure||failureCollected_))return cached_;
        const auto internal=exe.parent_path()/L"_internal";
        std::vector<fs::path> files{exe,internal/L"base_library.zip"};
        std::error_code ec;unsigned dlls=0;
        for(fs::directory_iterator it(internal,ec),end; !ec&&it!=end;it.increment(ec)){
            const auto name=it->path().filename().wstring();if(name.rfind(L"python",0)==0&&it->path().extension()==L".dll"&&dlls++<4)files.push_back(it->path());
        }
        if(files.size()==2)files.push_back(internal/L"python*.dll");
        std::wstring signature;
        auto metadata=files;metadata.push_back(exe.parent_path()/L"tdp-runtime-manifest.json");
        for(const auto& file:metadata){ec.clear();auto size=fs::file_size(file,ec);signature+=file.wstring()+L":"+std::to_wstring(ec?0:size)+L":"+std::to_wstring(ec?ec.value():0);ec.clear();auto m=fs::last_write_time(file,ec);signature+=L":"+std::to_wstring(ec?0:m.time_since_epoch().count());}
        if(exe==path_&&known_&&signature==signature_&&(!failure||failureCollected_)){lastChecked_=now;return cached_;}
        if(signature!=signature_||exe!=path_)failureCollected_=false;
        path_=exe;signature_=signature;known_=true;lastChecked_=now;if(failure)failureCollected_=true;
        cached_={{"schemaVersion",1},{"revision",++revision_},{"sampleTick",now},{"files",Json::array()},
            {"manifestCheck","not-checked-no-version-matched-manifest"},{"evidenceScope","installed-files-only-not-helper-execution"}};
        for(const auto& file:files){
            Json item={{"name",boundedText(file.filename().string())},{"status","missing"},{"readable",false},{"sha256","not-observed"}};
            ec.clear();const auto size=fs::file_size(file,ec);
            if(!ec){std::ifstream probe(file,std::ios::binary);item["bytes"]=size;item["readable"]=static_cast<bool>(probe);item["status"]=probe?"present":"unreadable";if(probe){const auto hash=sha(file.wstring());item["sha256"]=hash.empty()?"read-error":hash;}}
            else item["errorCode"]=ec.value();
            cached_["files"].push_back(std::move(item));
        }
        cached_["baseLibraryEncodings"]=encodingsZip(internal/L"base_library.zip");
        const auto manifestPath=exe.parent_path()/L"tdp-runtime-manifest.json";
        std::ifstream manifest(manifestPath,std::ios::binary);
        if(manifest){
            try{
                manifest.seekg(0,std::ios::end);const auto bytes=manifest.tellg();manifest.seekg(0);
                if(bytes<=0||bytes>1024*1024)throw std::runtime_error("manifest-size");
                std::string text(static_cast<size_t>(bytes),'\0');manifest.read(text.data(),bytes);
                const auto declared=Json::parse(text);
                const auto normalized=[](std::string value){std::transform(value.begin(),value.end(),value.begin(),[](unsigned char c){return char(std::tolower(c));});return value;};
                const auto actualHash=cached_["files"][0].value("sha256",std::string{});
                const auto expectedHash=declared.value("helperSha256",std::string{});
                if(actualHash.size()!=64||expectedHash.size()!=64||normalized(actualHash)!=normalized(expectedHash))
                    cached_["manifestCheck"]={{"status","not-checked-manifest-for-other-helper-hash"},{"evidenceScope","manifest-not-applied-to-a-different-runtime"}};
                else{
                    Json issues=Json::array();
                    if(!declared.contains("files")||!declared["files"].is_array())throw std::runtime_error("manifest-files");
                    for(const auto& item:declared["files"]){
                        if(!item.is_object())continue;const auto name=item.value("name",std::string{});
                        // No reads of arbitrary paths from a manifest. Compare the existing bounded identity set only.
                        const auto filename=fs::path(name).filename().string();
                        if(name!="YeManTdpCtl.exe"&&name!="_internal/base_library.zip"&&!(name.rfind("_internal/python",0)==0&&fs::path(name).extension()==".dll"&&name.find("..") == std::string::npos))continue;
                        const auto actual=std::find_if(cached_["files"].begin(),cached_["files"].end(),[&](const auto& f){return f.value("name",std::string{})==filename;});
                        if(actual==cached_["files"].end()||actual->value("status",std::string{})=="missing")issues.push_back({{"name",name},{"status","missing"}});
                        else if(!actual->value("readable",false))issues.push_back({{"name",name},{"status","unreadable"}});
                        else if(normalized(actual->value("sha256",std::string{}))!=normalized(item.value("sha256",std::string{})))issues.push_back({{"name",name},{"status","hash-differs"}});
                    }
                    cached_["manifestCheck"]={{"status",issues.empty()?"matched":"identity-differs"},{"issues",issues},
                        {"helperSha256",actualHash},{"pythonVersion",declared.value("pythonVersion",std::string("not-observed"))}};
                }
            }catch(...){cached_["manifestCheck"]={{"status","read-error"},{"evidenceScope","no-control-gate-changed"}};}
        }
        return cached_;
    }
private:
    fs::path path_;std::wstring signature_;Json cached_;bool known_=false,failureCollected_=false;
    std::uint64_t lastChecked_=0,revision_=0;
};
// Transient STDERR snapshot for the ALREADY-REQUESTED daemon launch. Never a
// polling worker; caller settles it on its existing RPC/launch boundaries.
// An explicit handle list prevents diagnostic redirection leaking other handles.
class DaemonStartupCapture {
public:
    ~DaemonStartupCapture() { cleanup(); }
    DaemonStartupCapture(const DaemonStartupCapture&) = delete;
    DaemonStartupCapture& operator=(const DaemonStartupCapture&) = delete;
    DaemonStartupCapture() = default;
    bool prepare(const fs::path& root) noexcept {
        try {
            static std::atomic<unsigned long long> sequence{0};
            path_ = root / (L"tdp-bootstrap-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(sequence++) + L".stderr.tmp");
            SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
            error_ = CreateFileW(path_.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                &sa, CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY, nullptr);
            if(error_ == INVALID_HANDLE_VALUE) { error_ = nullptr; path_.clear(); return false; }
            null_ = CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if(null_ == INVALID_HANDLE_VALUE) { null_=nullptr; cleanup(); return false; }
            SIZE_T bytes=0;InitializeProcThreadAttributeList(nullptr,1,0,&bytes);
            attributes_.resize(bytes);
            auto list=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes_.data());
            if(!InitializeProcThreadAttributeList(list,1,0,&bytes)){cleanup();return false;}
            initialized_=true;
            // The attribute value storage must outlive process creation. A local
            // stack array can be reused by command-line construction before launch.
            inheritedHandles_={error_,null_};
            if(!UpdateProcThreadAttribute(list,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,inheritedHandles_.data(),sizeof(inheritedHandles_),nullptr,nullptr)){cleanup();return false;}
            startup_={};startup_.StartupInfo.cb=sizeof(startup_);
            startup_.StartupInfo.dwFlags=STARTF_USESTDHANDLES;
            startup_.StartupInfo.hStdError=error_;startup_.StartupInfo.hStdOutput=null_;startup_.StartupInfo.hStdInput=null_;
            startup_.lpAttributeList=list;return true;
        }catch(...){cleanup();return false;}
    }
    LPSTARTUPINFOW startup() noexcept { return &startup_.StartupInfo; }
    void started(HANDLE process,DWORD pid) noexcept {
        pid_=pid;
        DuplicateHandle(GetCurrentProcess(),process,GetCurrentProcess(),&process_,PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,0);
        closeLaunchHandles();
    }
    bool settle(bool serverReady, Json& row) noexcept {
        try {
            DWORD exitCode=STILL_ACTIVE;
            const bool exited=process_ && GetExitCodeProcess(process_,&exitCode) && exitCode!=STILL_ACTIVE;
            if(!serverReady&&!exited)return false;
            row["daemonPid"]=pid_;row["processStarted"]=true;row["daemonReady"]=serverReady;
            row["daemonExitCode"]=exited?Json(exitCode):Json("not-observed-running");
            row["captureScope"]="static-startup-stderr-only-no-daemon-stdout-poll";
            HANDLE input=CreateFileW(path_.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
            std::string text;LARGE_INTEGER size{};
            if(input!=INVALID_HANDLE_VALUE){
                if(GetFileSizeEx(input,&size)&&size.QuadPart>=0){
                    const auto n=static_cast<DWORD>((std::min)(size.QuadPart,LONGLONG(4096)));text.resize(n);DWORD read=0;
                    if(!n||ReadFile(input,text.data(),n,&read,nullptr))text.resize(read);else text.clear();
                    row["stderrTruncated"]=size.QuadPart>4096;
                }CloseHandle(input);
            }else row["stderrReadError"]=GetLastError();
            row["stderr"]=boundedText(text);row["bootstrapFailureKind"]=bootstrapKind(text,true);
            return true;
        }catch(...){return false;}
    }
private:
    void closeLaunchHandles() noexcept {
        if(error_){CloseHandle(error_);error_=nullptr;}if(null_){CloseHandle(null_);null_=nullptr;}
        if(initialized_){DeleteProcThreadAttributeList(reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes_.data()));initialized_=false;}
    }
    void cleanup() noexcept {
        closeLaunchHandles();if(process_){CloseHandle(process_);process_=nullptr;}
        if(!path_.empty()){DeleteFileW(path_.c_str());path_.clear();} // only our CREATE_NEW temporary evidence
    }
    fs::path path_;HANDLE error_=nullptr,null_=nullptr,process_=nullptr;DWORD pid_=0;
    bool initialized_=false;std::array<HANDLE,2> inheritedHandles_{};std::vector<unsigned char> attributes_;STARTUPINFOEXW startup_{};
};

}
