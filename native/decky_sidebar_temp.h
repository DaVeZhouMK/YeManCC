#pragma once
// Loader-only temporary extraction directories. No business settings/store.
// Cleanup is explicit and ONLY after our Job reports zero active processes.
#include <windows.h>
#include <bcrypt.h>
#include <filesystem>
#include <string>
#include <stdexcept>
#pragma comment(lib,"bcrypt.lib")
namespace ymcc::deckysidebar {
class LoaderTemporaryRun {
    std::filesystem::path base_,run_;
    std::string cleanupStage_="not-requested";
    std::filesystem::path cleanupFailure_;
    struct DirectoryLock {
        HANDLE h=INVALID_HANDLE_VALUE;
        explicit DirectoryLock(const std::filesystem::path& p){
            // No FILE_SHARE_DELETE: hold each ancestor while touching its children.
            h=CreateFileW(p.c_str(),FILE_LIST_DIRECTORY|FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,
                nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        }
        ~DirectoryLock(){if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);}
        bool regular()const{BY_HANDLE_FILE_INFORMATION info{};return h!=INVALID_HANDLE_VALUE&&GetFileInformationByHandle(h,&info)&&
            (info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)&&!(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT);}
        DirectoryLock(const DirectoryLock&)=delete;
    };
    static bool ordinaryAncestors(const std::filesystem::path& p){
        for(auto part=p;!part.empty();part=part.parent_path()){
            const DWORD attributes=GetFileAttributesW(part.c_str());
            if(attributes==INVALID_FILE_ATTRIBUTES||!(attributes&FILE_ATTRIBUTE_DIRECTORY)||(attributes&FILE_ATTRIBUTE_REPARSE_POINT))return false;
            if(part==part.parent_path())break;
        }return true;
    }
    static bool emptyDirectory(const std::filesystem::path& dir,size_t& entries,unsigned depth,std::filesystem::path& failure){
        failure=dir;
        if(depth>32)return false;
        DirectoryLock lock(dir);if(!lock.regular())return false;
        WIN32_FIND_DATAW item{};const auto pattern=dir/L"*";
        HANDLE scan=FindFirstFileW(pattern.c_str(),&item);
        if(scan==INVALID_HANDLE_VALUE)return GetLastError()==ERROR_FILE_NOT_FOUND;
        bool good=true;
        do{
            const std::wstring name=item.cFileName;if(name==L"."||name==L"..")continue;
            if(++entries>4096){good=false;break;}
            const auto path=dir/name;failure=path;
            if(item.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT){good=false;break;} // Never traverse OR delete foreign links.
            if(item.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY){
                if(!emptyDirectory(path,entries,depth+1,failure)||!RemoveDirectoryW(path.c_str())){good=false;break;}
            }else{
                // Readonly metadata may belong to a hard-linked file outside this run.
                // Never clear it through a temporary alias; retain rather than mutate it.
                if(item.dwFileAttributes&FILE_ATTRIBUTE_READONLY){good=false;break;}
                if(!DeleteFileW(path.c_str())){good=false;break;}
            }
        }while(FindNextFileW(scan,&item));
        const DWORD last=GetLastError();FindClose(scan);
        return good&&last==ERROR_NO_MORE_FILES;
    }
public:
    LoaderTemporaryRun()=default;
    LoaderTemporaryRun(const LoaderTemporaryRun&)=delete;
    LoaderTemporaryRun& operator=(const LoaderTemporaryRun&)=delete;
    std::wstring prepare(const std::wstring& home){
        if(!run_.empty())throw std::runtime_error("loader-temp-run-still-owned");
        const auto canonical=std::filesystem::absolute(home).lexically_normal();
        if(!ordinaryAncestors(canonical))throw std::runtime_error("loader-temp-home-not-ordinary");
        base_=canonical/L"runtime-temp";
        if(!CreateDirectoryW(base_.c_str(),nullptr)&&GetLastError()!=ERROR_ALREADY_EXISTS)throw std::runtime_error("loader-temp-base-failed");
        if(!ordinaryAncestors(base_))throw std::runtime_error("loader-temp-base-not-ordinary");
        for(unsigned attempt=0;attempt<8;++attempt){
            unsigned char bytes[16];if(BCryptGenRandom(nullptr,bytes,sizeof(bytes),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0)throw std::runtime_error("loader-temp-random-failed");
            const wchar_t hex[]=L"0123456789abcdef";std::wstring name=L"ymcc-decky-";
            for(const auto byte:bytes){name+=hex[byte>>4];name+=hex[byte&15];}
            const auto path=base_/name;
            if(CreateDirectoryW(path.c_str(),nullptr)){run_=path;return run_.wstring();}
            if(GetLastError()!=ERROR_ALREADY_EXISTS)throw std::runtime_error("loader-temp-run-create-failed");
        }throw std::runtime_error("loader-temp-name-collision");
    }
    const std::filesystem::path& path()const{return run_;}
    const std::string& cleanupStage()const{return cleanupStage_;}
    const std::filesystem::path& cleanupFailure()const{return cleanupFailure_;}
    bool cleanupAfterJobEmpty(bool noActiveProcesses){
        cleanupStage_="job-gate";
        if(run_.empty())return true;
        if(!noActiveProcesses)return false;
        // Revalidate resolved absolute scope immediately before every recursive cleanup.
        cleanupStage_="scope-validation";
        const auto base=std::filesystem::absolute(base_).lexically_normal();
        const auto target=std::filesystem::absolute(run_).lexically_normal();
        const auto name=target.filename().wstring();
        if(base.filename()!=L"runtime-temp"||target.parent_path()!=base||name.size()!=43||name.rfind(L"ymcc-decky-",0)!=0||
            name.substr(11).find_first_not_of(L"0123456789abcdef")!=std::wstring::npos||!ordinaryAncestors(base))return false;
        cleanupStage_="parent-lock";
        DirectoryLock parent(base);if(!parent.regular())return false;
        cleanupStage_="empty-owned-tree";
        size_t entries=0;if(!emptyDirectory(target,entries,0,cleanupFailure_))return false;
        cleanupStage_="remove-owned-run";
        if(!RemoveDirectoryW(target.c_str()))return false;
        cleanupStage_="complete";run_.clear();return true;
    }
    void abandon(){run_.clear();base_.clear();} // Unknown/live Job: retain files rather than touch them.
};
} // namespace ymcc::deckysidebar



