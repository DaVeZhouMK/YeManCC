#pragma once
#include <filesystem>
#include <string>
#include <vector>
#include <stdexcept>
#include <cstdint>
#include <algorithm>
#include <cwctype>
#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace ymcc::steamOriginalStorage {
struct Receipt { bool legacyImported=false; std::size_t filesCopied=0,filesAlreadyPresent=0; };
namespace detail {
namespace fs=std::filesystem;
inline constexpr std::uintmax_t fileLimit=2u*1024*1024,totalLimit=128u*1024*1024;
inline constexpr std::size_t countLimit=16384;
inline constexpr char markerBytes[]="{\"schema\":\"ymcc.steam-original-storage.v2\",\"committed\":true}\n";
[[noreturn]] inline void fail(const char* reason){throw std::runtime_error(reason);}
inline void need(bool ok,const char* reason){if(!ok)fail(reason);}
inline bool present(const fs::path& p){std::error_code ec;auto s=fs::symlink_status(p,ec);if(ec==std::errc::no_such_file_or_directory)return false;need(!ec,"storage-inspect-failed");return s.type()!=fs::file_type::not_found;}
inline void plain(const fs::path& p){
    std::error_code ec;auto s=fs::symlink_status(p,ec);need(!ec,"storage-inspect-failed");
    need(!fs::is_symlink(s),"storage-reparse-refused");
#ifdef _WIN32
    const auto a=GetFileAttributesW(p.c_str());need(a!=INVALID_FILE_ATTRIBUTES,"storage-inspect-failed");need(!(a&FILE_ATTRIBUTE_REPARSE_POINT),"storage-reparse-refused");
#endif
    need(fs::is_directory(s)||fs::is_regular_file(s),"storage-type-refused");
}
inline void ancestry(const fs::path& p){fs::path part;for(const auto& c:p){part/=c;if(present(part))plain(part);}}
inline fs::path root(const fs::path& p){
    need(!p.empty()&&p.is_absolute(),"storage-root-invalid");
    for(const auto& c:p){const auto s=c.wstring();need(s!=L".."&&s!=L".","storage-root-invalid");
#ifdef _WIN32
        if(c!=p.root_name()&&c!=p.root_directory())need(s.find(L':')==std::wstring::npos&&s.find_first_of(L"*?\"")==std::wstring::npos&&s.back()!=L'.'&&s.back()!=L' ',"storage-root-invalid");
#endif
    }
    const auto n=p.lexically_normal();need(n!=n.root_path(),"storage-root-invalid");
#ifdef _WIN32
    need(n.native().rfind(L"\\\\",0)!=0,"storage-root-invalid");
#endif
    return n;
}
inline std::wstring key(const fs::path& p){auto s=p.generic_wstring();while(s.size()>1&&s.back()==L'/')s.pop_back();
#ifdef _WIN32
    std::transform(s.begin(),s.end(),s.begin(),[](wchar_t c){return static_cast<wchar_t>(std::towlower(c));});
#endif
    return s;
}
inline bool within(const std::wstring& a,const std::wstring& b){return a==b||(a.size()>b.size()&&a.compare(0,b.size(),b)==0&&a[b.size()]==L'/');}
inline void directory(const fs::path& p){ancestry(p);std::error_code ec;fs::create_directories(p,ec);need(!ec,"storage-directory-failed");ancestry(p);need(fs::is_directory(p),"storage-type-refused");}
inline std::string read(const fs::path& p){
    ancestry(p);plain(p);need(fs::is_regular_file(p),"storage-type-refused");std::error_code ec;const auto size=fs::file_size(p,ec);need(!ec,"storage-read-failed");need(size<=fileLimit,"storage-file-limit");std::string bytes(static_cast<std::size_t>(size),'\0');
#ifdef _WIN32
    HANDLE h=CreateFileW(p.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);need(h!=INVALID_HANDLE_VALUE,"storage-read-failed");BY_HANDLE_FILE_INFORMATION info{};bool ok=GetFileInformationByHandle(h,&info)&&!(info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))&&info.nNumberOfLinks==1;DWORD got=0;ok=ok&&ReadFile(h,bytes.data(),static_cast<DWORD>(bytes.size()),&got,nullptr)&&got==bytes.size();char extra;DWORD more=0;ok=ok&&ReadFile(h,&extra,1,&more,nullptr)&&more==0;CloseHandle(h);need(ok,"storage-read-failed");
#else
    int fd=::open(p.c_str(),O_RDONLY|O_NOFOLLOW);need(fd>=0,"storage-read-failed");std::size_t at=0;while(at<bytes.size()){auto n=::read(fd,bytes.data()+at,bytes.size()-at);if(n<=0){::close(fd);fail("storage-read-failed");}at+=static_cast<std::size_t>(n);}char extra;auto n=::read(fd,&extra,1);::close(fd);need(n==0,"storage-read-failed");
#endif
    return bytes;
}
// Failed or interrupted writes remain on disk. Never erase a partial ledger.
inline void create(const fs::path& p,const std::string& bytes){ancestry(p.parent_path());
#ifdef _WIN32
    HANDLE h=CreateFileW(p.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_WRITE_THROUGH,nullptr);need(h!=INVALID_HANDLE_VALUE,"storage-create-failed");DWORD n=0;const bool ok=WriteFile(h,bytes.data(),static_cast<DWORD>(bytes.size()),&n,nullptr)&&n==bytes.size()&&FlushFileBuffers(h);CloseHandle(h);need(ok,"storage-flush-failed");
#else
    int fd=::open(p.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);need(fd>=0,"storage-create-failed");std::size_t at=0;bool ok=true;while(at<bytes.size()){auto n=::write(fd,bytes.data()+at,bytes.size()-at);if(n<=0){ok=false;break;}at+=static_cast<std::size_t>(n);}ok=ok&&::fsync(fd)==0;::close(fd);need(ok,"storage-flush-failed");
#endif
}
inline bool controls(const fs::path& n){return n=="enabled.json"||n=="seed.json"||n=="DISABLED.json";}
inline bool ledger(const fs::path& n){return n=="references"||n=="consumed"||n=="disabled"||n=="audited-prepublication-transfer";}
inline bool ignored(const fs::path& n){return n=="work"||n=="monitor"||n=="status.json"||n=="summary.json"||n=="last-recovery-summary.json"||n=="last-summary.json"||n=="last-event.json"||n=="current-export.json"||n=="last-dual-route-summary.json"||n=="last-legacy-dynamic-event.json"||n=="last-semi-static-event.json"||n=="last-legacy-dynamic-summary.json"||n=="last-semi-static-summary.json"||n.extension()==".dll";}
struct Budget {std::size_t entries=0,files=0;std::uintmax_t bytes=0;void entry(){need(++entries<=countLimit*4,"storage-count-limit");}void file(const fs::path& p){need(++files<=countLimit,"storage-count-limit");std::error_code ec;auto n=fs::file_size(p,ec);need(!ec,"storage-read-failed");need(n<=fileLimit,"storage-file-limit");need(n<=totalLimit-bytes,"storage-total-limit");bytes+=n;}};
inline void inspectIgnored(const fs::path& p,Budget& b){plain(p);if(fs::is_directory(p))for(const auto& e:fs::recursive_directory_iterator(p)){b.entry();plain(e.path());}}
inline std::vector<fs::path> scan(const fs::path& base,bool target){
    std::vector<fs::path> files;Budget b;ancestry(base);if(!present(base))return files;need(fs::is_directory(base),"storage-type-refused");
    for(const auto& e:fs::directory_iterator(base)){b.entry();plain(e.path());auto n=e.path().filename();
        if(controls(n)){need(e.is_regular_file(),"storage-type-refused");b.file(e.path());files.push_back(n);}
        else if(ledger(n)){need(e.is_directory(),"storage-type-refused");for(const auto& f:fs::directory_iterator(e.path())){b.entry();plain(f.path());need(f.is_regular_file()&&f.path().extension()==".json","storage-unknown-entry");b.file(f.path());files.push_back(n/f.path().filename());}}
        else if(target&&(n=="storage-layout-v2.json"||n=="storage-layout-v2.pending")){need(e.is_regular_file(),"storage-type-refused");}
        else if(ignored(n)){inspectIgnored(e.path(),b);}
        else fail("storage-unknown-entry");
    }return files;
}
inline Receipt run(const fs::path& legacyInput,const fs::path& runtimeInput){
    const auto legacy=root(legacyInput),runtime=root(runtimeInput);const auto l=key(legacy),r=key(runtime);need(!within(l,r)&&!within(r,l),"storage-root-overlap");ancestry(runtime);
    const auto marker=runtime/"storage-layout-v2.json",pending=runtime/"storage-layout-v2.pending";
    need(!present(pending),"storage-marker-interrupted");
    if(present(marker)){need(read(marker)==markerBytes,"storage-marker-invalid");scan(runtime,true);return {};}
    const auto source=scan(legacy,false);const auto existing=scan(runtime,true);
    // A target-only ledger without a commit cannot safely be attributed to this migration.
    for(const auto& p:existing)need(std::find(source.begin(),source.end(),p)!=source.end(),"storage-target-uncommitted");
    for(const auto& p:source)if(present(runtime/p))need(read(legacy/p)==read(runtime/p),"storage-copy-conflict");
    directory(runtime);Budget copiedBudget;Receipt receipt;receipt.legacyImported=present(legacy);
    for(const auto& p:source){const auto bytes=read(legacy/p);need(bytes.size()<=totalLimit-copiedBudget.bytes,"storage-total-limit");copiedBudget.bytes+=bytes.size();directory((runtime/p).parent_path());if(present(runtime/p)){need(read(runtime/p)==bytes,"storage-copy-conflict");++receipt.filesAlreadyPresent;}else{create(runtime/p,bytes);++receipt.filesCopied;}}
    // Pending is a durable refusal sentinel until the final no-replace commit.
    create(pending,markerBytes);
#ifdef _WIN32
    need(MoveFileExW(pending.c_str(),marker.c_str(),MOVEFILE_WRITE_THROUGH)!=0,"storage-marker-commit-failed");
#else
    need(::link(pending.c_str(),marker.c_str())==0,"storage-marker-commit-failed");need(::unlink(pending.c_str())==0,"storage-marker-commit-failed");int fd=::open(runtime.c_str(),O_RDONLY|O_DIRECTORY);need(fd>=0,"storage-marker-commit-failed");bool ok=::fsync(fd)==0;::close(fd);need(ok,"storage-marker-commit-failed");
#endif
    return receipt;
}
}
// Reasons are fixed identifiers; filesystem paths and account bytes never escape.
inline Receipt prepare(const std::filesystem::path& legacyEvidenceRoot,const std::filesystem::path& persistentRuntimeRoot){
    try{return detail::run(legacyEvidenceRoot,persistentRuntimeRoot);}catch(const std::filesystem::filesystem_error&){detail::fail("storage-filesystem-failed");}catch(const std::runtime_error&){throw;}catch(...){detail::fail("storage-operation-failed");}
}
}

