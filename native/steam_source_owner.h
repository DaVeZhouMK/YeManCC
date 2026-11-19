#pragma once
#include <windows.h>
#include <sddl.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <utility>
#include <string>
#include <vector>
namespace ymcc::steamsession {
// Cooperative process-lifetime ownership among updated Native participants.
// Object existence, never mutex acquisition: start/stop thread affinity does
// not own this slot. No process, installation, app setting or build is a key.
class SourceOwner final {
    HANDLE slot_=nullptr;
    std::atomic<bool> owned_{false};
    std::atomic<DWORD> error_{ERROR_NOT_READY};
    std::mutex readsMutex_;
    std::condition_variable readsDone_;
    unsigned reads_=0;
    unsigned long long readGeneration_=0;
    bool accepting_=false;
    const std::shared_ptr<std::atomic<unsigned long long>> activity_=
        std::make_shared<std::atomic<unsigned long long>>(0);
    void releaseRead()noexcept{
        std::lock_guard lock(readsMutex_);if(reads_&&--reads_==0)readsDone_.notify_all();
    }
    static bool scope(std::wstring& name) noexcept {
        try{
            HANDLE token=nullptr;
            if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token))return false;
            struct Token{HANDLE h;~Token(){CloseHandle(h);}} held{token};
            DWORD bytes=0;GetTokenInformation(token,TokenUser,nullptr,0,&bytes);
            if(!bytes)return false;
            std::vector<BYTE> buffer(bytes);
            if(!GetTokenInformation(token,TokenUser,buffer.data(),bytes,&bytes))return false;
            LPWSTR text=nullptr;
            if(!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buffer.data())->User.Sid,&text))return false;
            struct Text{LPWSTR p;~Text(){LocalFree(p);}} sid{text};
            DWORD session=0;if(!ProcessIdToSessionId(GetCurrentProcessId(),&session))return false;
            name=L"Local\\YeManCC.SteamSource.v1."+std::wstring(text)+L"."+std::to_wstring(session);
            return true;
        }catch(...){return false;}
    }
public:
    SourceOwner()=default;
    SourceOwner(const SourceOwner&)=delete;
    SourceOwner& operator=(const SourceOwner&)=delete;
    ~SourceOwner(){drain();close();}
    // Memory-only revocation of one admitted request. Queues retain this cell,
    // not a read count or SourceOwner address. Stop/restart never revalidates it.
    class View final {
        std::shared_ptr<const std::atomic<unsigned long long>> activity_;
        unsigned long long expected_=0;
        friend class SourceOwner;
        View(std::shared_ptr<const std::atomic<unsigned long long>> value,unsigned long long expected)
            :activity_(std::move(value)),expected_(expected){}
    public:
        View()=default;
        bool current()const noexcept{return activity_&&expected_&&activity_->load(std::memory_order_acquire)==expected_;}
    };
private:
    View view(unsigned long long generation)const noexcept{return View(activity_,generation);}
public:
    class Read final {
        SourceOwner* owner_=nullptr;
        unsigned long long generation_=0;
        friend class SourceOwner;
        Read(SourceOwner* owner,unsigned long long generation):owner_(owner),generation_(generation){}
    public:
        Read()=default;Read(const Read&)=delete;Read& operator=(const Read&)=delete;
        Read(Read&& other)noexcept:owner_(std::exchange(other.owner_,nullptr)),generation_(other.generation_){}
        ~Read(){if(owner_)owner_->releaseRead();}
        explicit operator bool()const noexcept{return owner_!=nullptr;}
        View view()const noexcept{return owner_?owner_->view(generation_):View{};}
        bool current()const noexcept{
            if(!owner_)return false;
            std::lock_guard lock(owner_->readsMutex_);
            return owner_->owned()&&owner_->accepting_&&owner_->readGeneration_==generation_;
        }
        bool currentFor(const SourceOwner& owner)const noexcept{return owner_==&owner&&current();}
    };
    Read read(){
        std::lock_guard lock(readsMutex_);if(!owned()||!accepting_)return {};
        ++reads_;return Read(this,readGeneration_);
    }
    void resumeReads(){std::lock_guard lock(readsMutex_);++readGeneration_;accepting_=true;activity_->store(readGeneration_,std::memory_order_release);}
    void suspendReads(){std::lock_guard lock(readsMutex_);accepting_=false;++readGeneration_;activity_->store(0,std::memory_order_release);}
    void drain(){std::unique_lock lock(readsMutex_);accepting_=false;++readGeneration_;activity_->store(0,std::memory_order_release);readsDone_.wait(lock,[&]{return reads_==0;});}
    // Serialized by Observer lifecycle. Failure cannot authorize a fallback
    // name, a second source, or a false Steam-absent publication.
    bool claim() noexcept {
        if(slot_)return owned_.load(std::memory_order_acquire);
        std::wstring name;
        if(!scope(name)){error_=ERROR_NOT_READY;return false;}
        SetLastError(ERROR_SUCCESS);
        HANDLE created=CreateMutexW(nullptr,FALSE,name.c_str());
        const DWORD error=GetLastError();
        if(!created||error!=ERROR_SUCCESS){
            error_=error?error:ERROR_NOT_READY;
            if(created)CloseHandle(created);
            return false;
        }
        slot_=created;error_=ERROR_SUCCESS;owned_.store(true,std::memory_order_release);return true;
    }
    bool owned()const noexcept{return owned_.load(std::memory_order_acquire);}
    DWORD error()const noexcept{return error_.load(std::memory_order_acquire);}
    // Only after observer joins and actual original fact reads have drained.
    // A rejected close retains ownership and cannot authorize replacement.
    bool close() noexcept {
        std::lock_guard lock(readsMutex_);accepting_=false;activity_->store(0,std::memory_order_release);
        if(reads_){error_=ERROR_BUSY;return false;}
        if(!slot_)return true;
        if(!CloseHandle(slot_)){error_=GetLastError();return false;}
        slot_=nullptr;owned_.store(false,std::memory_order_release);return true;
    }
};
}
