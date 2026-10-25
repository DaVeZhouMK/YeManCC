#pragma once
// YMCC local repair cache. This consumes an ALREADY validated current package
// contract supplied by decky_sidebar_setup.h; cache metadata is never authority.
// Two fixed slots plus two provenance-owned scratch leaves bound disk growth,
// including hard-stopped preparation. Unknown staging/owner records are preserved.
// Only verified bytes enter
// .bin files; no executable/helper/service is added. No timer or network fetch.
// inspect/status never calls this writer. Existing enable/Ready preparation owns
// its cancellation; each replacement rechecks consent, path and target content.
#include <winsock2.h>
#include <windows.h>
#include <filesystem>
#include <functional>
#include <fstream>
#include <atomic>
#include <array>
#include <string>
#include <vector>
#include <stdexcept>
#include "json.hpp"
#pragma comment(lib, "ws2_32.lib")
namespace ymcc::deckyresources {
using Json=nlohmann::json;
using Digest=std::function<std::string(const std::wstring&)>;
using SafePath=std::function<bool(const std::filesystem::path&)>;
struct Options {
    bool enabled=true;
    bool allowRepair=true;
    unsigned short backendPort=1337; // Production fixed; fixtures use an isolated ephemeral port.
    std::function<bool()> mayWrite;
    std::function<Json()> isolatedPreparation; // Parent consumes verified child result without repeating disk IO.
};
struct Handle {
    HANDLE value=INVALID_HANDLE_VALUE;
    ~Handle(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
    Handle()=default;Handle(const Handle&)=delete;
};
struct PortLease {
    bool winsock=false;SOCKET value=INVALID_SOCKET;
    explicit PortLease(unsigned short port){
        WSADATA data{};if(WSAStartup(MAKEWORD(2,2),&data)!=0)return;winsock=true;
        value=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);if(value==INVALID_SOCKET)return;
        const BOOL exclusive=TRUE;sockaddr_in address{};address.sin_family=AF_INET;
        address.sin_addr.s_addr=htonl(INADDR_ANY);address.sin_port=htons(port);
        if(setsockopt(value,SOL_SOCKET,SO_EXCLUSIVEADDRUSE,reinterpret_cast<const char*>(&exclusive),sizeof(exclusive))==SOCKET_ERROR||
            bind(value,reinterpret_cast<const sockaddr*>(&address),sizeof(address))==SOCKET_ERROR){closesocket(value);value=INVALID_SOCKET;}
    }
    ~PortLease(){if(value!=INVALID_SOCKET)closesocket(value);if(winsock)WSACleanup();}
    PortLease(const PortLease&)=delete;
    bool acquired()const{return value!=INVALID_SOCKET;}
};
inline void require(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
class Cache {
    std::filesystem::path home_,root_;
    Json manifest_;
    const Digest& digest_;const SafePath& safe_;const std::function<bool()>& current_;
    Options options_;
    struct Entry {std::filesystem::path relative;std::uintmax_t bytes;std::string sha;};
    std::vector<Entry> entries_;
    bool stagingOwned_=false;
    bool current()const{return !current_||current_();}
    bool mayRepair()const{return current()&&options_.allowRepair&&(!options_.mayWrite||options_.mayWrite());}
    static bool exists(const std::filesystem::path& file){return GetFileAttributesW(file.c_str())!=INVALID_FILE_ATTRIBUTES;}
    void safe(const std::filesystem::path& path)const{require(safe_&&safe_(path),"resource-cache-unsafe-path");}
    bool matches(const std::filesystem::path& file,const Entry& entry)const{
        safe(file);std::error_code error;
        return current()&&std::filesystem::is_regular_file(file,error)&&!error&&std::filesystem::file_size(file,error)==entry.bytes&&!error&&digest_(file.wstring())==entry.sha;
    }
    std::string observed(const std::filesystem::path& file)const{
        safe(file);if(!exists(file))return {};
        require(std::filesystem::is_regular_file(file),"resource-cache-target-not-file");
        const auto hash=digest_(file.wstring());require(!hash.empty()&&current(),"resource-cache-read-cancelled");return hash;
    }
    struct CopyProgress {Cache* self;bool destinationObserved=false;};
    static DWORD CALLBACK copyProgress(LARGE_INTEGER,LARGE_INTEGER,LARGE_INTEGER,LARGE_INTEGER,DWORD,DWORD,HANDLE,HANDLE destination,LPVOID context){
        auto& progress=*static_cast<CopyProgress*>(context);if(destination&&destination!=INVALID_HANDLE_VALUE)progress.destinationObserved=true;
        return progress.self->current()?PROGRESS_CONTINUE:PROGRESS_CANCEL;
    }
    static constexpr const wchar_t* copyStage=L"copy-stage.tmp";
    static constexpr const wchar_t* metadataStage=L"metadata-stage.tmp";
    void retireOwnedStage(const wchar_t* name){
        require(stagingOwned_&&current(),"resource-cache-write-cancelled");
        const auto file=root_/name;safe(file);
        const auto attributes=GetFileAttributesW(file.c_str());
        if(attributes==INVALID_FILE_ATTRIBUTES){const auto error=GetLastError();require(error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND,"resource-cache-stage-inspection-failed");return;}
        // Bind the checked object. Never follow a junction or recurse/delete a
        // computed user path. The exact two leaf names come from our owner record.
        Handle lease;lease.value=CreateFileW(file.c_str(),DELETE|FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
            nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS,nullptr);
        require(lease.value!=INVALID_HANDLE_VALUE,"resource-cache-stage-busy");BY_HANDLE_FILE_INFORMATION info{};
        require(GetFileInformationByHandle(lease.value,&info)&&!(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)),"resource-cache-stage-unsafe");
        require(current(),"resource-cache-write-cancelled");FILE_DISPOSITION_INFO remove{TRUE};
        require(SetFileInformationByHandle(lease.value,FileDispositionInfo,&remove,sizeof(remove))!=FALSE,"resource-cache-stage-remove-failed");
    }
    void ownStaging(){
        const auto ownership=root_/L"staging-owner.json";safe(ownership);
        const Json expected={{"schemaVersion",1},{"owner","YMCC"},{"purpose","resource-cache-staging"},{"files",Json::array({"copy-stage.tmp","metadata-stage.tmp"})}};
        if(exists(ownership)){
            require(std::filesystem::is_regular_file(ownership)&&std::filesystem::file_size(ownership)<=1024,"resource-cache-stage-owner-invalid");
            std::ifstream input(ownership,std::ios::binary);Json observed;
            try{observed=Json::parse(input);}catch(...){throw std::runtime_error("resource-cache-stage-owner-invalid");}
            require(observed==expected,"resource-cache-stage-owner-invalid");
        }else{
            // Missing provenance cannot authorize deleting even a familiar name.
            for(const auto* name:{copyStage,metadataStage}){const auto file=root_/name;safe(file);require(!exists(file),"resource-cache-stage-unowned");}
            require(current(),"resource-cache-write-cancelled");Handle file;
            file.value=CreateFileW(ownership.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
            require(file.value!=INVALID_HANDLE_VALUE,"resource-cache-stage-owner-create-failed");
            const auto bytes=expected.dump();DWORD written=0;
            require(WriteFile(file.value,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr)&&written==bytes.size()&&FlushFileBuffers(file.value),"resource-cache-stage-owner-write-failed");
        }
        stagingOwned_=true;
        retireOwnedStage(copyStage);retireOwnedStage(metadataStage);
    }
    std::filesystem::path temporary(bool metadata){
        require(stagingOwned_&&current(),"resource-cache-write-cancelled");
        const auto* name=metadata?metadataStage:copyStage;
        retireOwnedStage(name);const auto path=root_/name;safe(path);return path;
    }
    void copyAtomic(const std::filesystem::path& source,const std::filesystem::path& destination,const Entry& entry,bool live,const std::string& before={}){
        require(current()&&(!live||mayRepair()),"resource-cache-write-cancelled");safe(source);safe(destination);
        std::filesystem::create_directories(destination.parent_path());safe(destination.parent_path());
        const auto staged=temporary(false);safe(staged);bool owned=false;
        try{
            BOOL cancel=FALSE;CopyProgress progress{this};
            const auto copied=CopyFileExW(source.c_str(),staged.c_str(),copyProgress,&progress,&cancel,COPY_FILE_FAIL_IF_EXISTS);
            owned=copied||progress.destinationObserved;
            require(copied!=FALSE,"resource-cache-copy-failed");
            require(matches(staged,entry),"resource-cache-copy-hash-mismatch");
            require(current()&&(!live||mayRepair()),"resource-cache-write-cancelled");safe(destination);
            if(live)require(observed(destination)==before,"resource-cache-target-changed");
            require(current()&&(!live||mayRepair()),"resource-cache-write-cancelled");
            require(MoveFileExW(staged.c_str(),destination.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE,"resource-cache-replace-failed");
            owned=false;require(matches(destination,entry),"resource-cache-final-hash-mismatch");
        }catch(...){if(owned&&safe_(staged))DeleteFileW(staged.c_str());throw;}
    }
    void writeMetadata(const std::filesystem::path& directory,const Json& metadata){
        require(current(),"resource-cache-write-cancelled");safe(directory);std::filesystem::create_directories(directory);safe(directory);
        const auto destination=directory/L"manifest.json",staged=temporary(true);safe(destination);safe(staged);
        Handle file;file.value=CreateFileW(staged.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        require(file.value!=INVALID_HANDLE_VALUE,"resource-cache-metadata-create-failed");
        bool owned=true;
        try{
            const auto bytes=metadata.dump();DWORD written=0;
            require(WriteFile(file.value,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr)&&written==bytes.size()&&FlushFileBuffers(file.value),"resource-cache-metadata-write-failed");
            CloseHandle(file.value);file.value=INVALID_HANDLE_VALUE;
            require(current(),"resource-cache-write-cancelled");safe(destination);
            require(MoveFileExW(staged.c_str(),destination.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE,"resource-cache-metadata-replace-failed");owned=false;
        }catch(...){if(file.value!=INVALID_HANDLE_VALUE){CloseHandle(file.value);file.value=INVALID_HANDLE_VALUE;}if(owned&&safe_(staged))DeleteFileW(staged.c_str());throw;}
    }
    struct Slot {std::filesystem::path root;Json metadata;bool owned=false,empty=false;std::filesystem::file_time_type written{};};
    Slot readSlot(const wchar_t* name)const{
        Slot result;result.root=root_/name;safe(result.root);
        if(!exists(result.root)){result.empty=true;return result;}
        require(std::filesystem::is_directory(result.root),"resource-cache-slot-not-directory");
        const auto metadata=result.root/L"manifest.json";safe(metadata);
        if(!exists(metadata)){result.empty=std::filesystem::is_empty(result.root);return result;}
        try{
            if(!std::filesystem::is_regular_file(metadata)||std::filesystem::file_size(metadata)>131072)return result;
            std::ifstream input(metadata,std::ios::binary);result.metadata=Json::parse(input);
            result.owned=result.metadata.value("schemaVersion",0)==1&&result.metadata.value("owner",std::string{})=="YMCC"&&result.metadata.contains("manifest")&&result.metadata["manifest"].is_object();
            result.written=std::filesystem::last_write_time(metadata);
        }catch(...){result.owned=false;}
        return result;
    }
    bool same(const Slot& slot)const{return slot.owned&&slot.metadata["manifest"]==manifest_;}
public:
    Cache(const std::filesystem::path& home,Json manifest,const Digest& digest,const SafePath& safe,const std::function<bool()>& current,Options options)
      :home_(home),root_(home/L".ymcc-resource-cache"),manifest_(std::move(manifest)),digest_(digest),safe_(safe),current_(current),options_(std::move(options)){
        require(home_.is_absolute()&&home_.root_name().native().size()==2&&home_.root_name().native()[1]==L':',"resource-cache-local-path-required");
        for(const auto& entry:manifest_.at("files"))entries_.push_back({std::filesystem::u8path(entry.at("path").get<std::string>()),entry.at("bytes").get<std::uintmax_t>(),entry.at("sha256").get<std::string>()});
        require(entries_.size()==7,"resource-cache-contract-invalid");
    }
    Json run(){
        Json result={{"resourceCacheAvailable",false},{"resourcesRepaired",0u},{"resourceRepairAttempted",false}};
        try{
            require(options_.enabled,"resource-cache-disabled");
            require(current(),"resource-cache-write-cancelled");safe(root_);std::filesystem::create_directories(root_);safe(root_);
            const auto lockPath=root_/L"owner.lock";safe(lockPath);Handle lock;
            lock.value=CreateFileW(lockPath.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_ALWAYS,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
            require(lock.value!=INVALID_HANDLE_VALUE,"resource-cache-busy");BY_HANDLE_FILE_INFORMATION info{};
            require(GetFileInformationByHandle(lock.value,&info)&&!(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)),"resource-cache-unsafe-path");
            ownStaging();
            auto slots=std::array<Slot,2>{readSlot(L"a"),readSlot(L"b")};
            struct Repair {std::size_t index;std::filesystem::path source;std::string before;};std::vector<Repair> pending;
            for(std::size_t i=0;i<entries_.size();++i){
                const auto& entry=entries_[i];const auto live=home_/entry.relative;
                if(matches(live,entry))continue;
                result["resourceRepairAttempted"]=true;require(mayRepair(),"resource-cache-repair-not-admitted");
                std::filesystem::path source;
                for(const auto& slot:slots)if(same(slot)){
                    const auto candidate=slot.root/(std::to_wstring(i)+L".bin");if(matches(candidate,entry)){source=candidate;break;}
                }
                require(!source.empty(),"resource-cache-unavailable");pending.push_back({i,source,observed(live)});
            }
            if(!pending.empty()){
                require(mayRepair(),"resource-cache-write-cancelled");PortLease port(options_.backendPort);require(port.acquired(),"resource-cache-backend-in-use");
                for(const auto& repair:pending){const auto& entry=entries_[repair.index];copyAtomic(repair.source,home_/entry.relative,entry,true,repair.before);result["resourcesRepaired"]=result.at("resourcesRepaired").get<unsigned>()+1;}
            }
            require(current(),"resource-cache-write-cancelled");
            for(const auto& entry:entries_)require(matches(home_/entry.relative,entry),"resource-cache-live-verification-failed");
            // Reuse a completed matching slot without rewriting it. Hashes are
            // always checked again before a cached blob is used for live repair.
            for(const auto& slot:slots)if(same(slot)&&slot.metadata.value("complete",false)){
                bool available=true;for(std::size_t i=0;i<entries_.size();++i){const auto blob=slot.root/(std::to_wstring(i)+L".bin");if(!matches(blob,entries_[i])){available=false;break;}}
                if(available){result["resourceCacheAvailable"]=true;return result;}
            }
            Slot* selected=nullptr;
            for(auto& slot:slots)if(same(slot)){selected=&slot;break;}
            if(!selected)for(auto& slot:slots)if(slot.empty){selected=&slot;break;}
            if(!selected)for(auto& slot:slots)if(slot.owned&&(!selected||slot.written<selected->written))selected=&slot;
            require(selected!=nullptr,"resource-cache-unowned-slots");
            const Json metadata={{"schemaVersion",1},{"owner","YMCC"},{"complete",false},{"manifest",manifest_}};
            writeMetadata(selected->root,metadata);
            for(std::size_t i=0;i<entries_.size();++i)copyAtomic(home_/entries_[i].relative,selected->root/(std::to_wstring(i)+L".bin"),entries_[i],false);
            auto completed=metadata;completed["complete"]=true;writeMetadata(selected->root,completed);
            result["resourceCacheAvailable"]=true;
        }catch(const std::exception& error){const std::string reason=error.what();result["resourceRepairReason"]=reason.starts_with("resource-cache-")&&reason.size()<=96?reason:"resource-cache-io-failed";}
        catch(...){result["resourceRepairReason"]="resource-cache-failed";}
        return result;
    }
};
}
