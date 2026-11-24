#pragma once
#include <windows.h>
#include <atomic>
#include <cwchar>

// Fixed property-name identifiers only. Every value is still queried from the
// supplied current HWND. A string property on the owned HWND holds the name;
// no GlobalAddAtom reference or cross-call property value is retained.
namespace ymcc::windowPropertyNames {
struct Win32 {
    static BOOL set(HWND w,const wchar_t* name,HANDLE value) noexcept {return SetPropW(w,name,value);}
    static HANDLE get(HWND w,const wchar_t* name) noexcept {return GetPropW(w,name);}
    static HANDLE remove(HWND w,const wchar_t* name) noexcept {return RemovePropW(w,name);}
    static ATOM find(const wchar_t* name) noexcept {return GlobalFindAtomW(name);}
    static int enumerate(HWND w,PROPENUMPROCEXW callback,LPARAM state) noexcept {return EnumPropsExW(w,callback,state);}
};

template<class Api=Win32> class Names {
    const wchar_t* names_[2];
    SRWLOCK lock_=SRWLOCK_INIT;
    std::atomic<bool> ready_{false};
    std::atomic<HWND> owner_{nullptr};
    ATOM atoms_[2]{};
    struct Presence {Names& self;bool found=false;};
    static int CALLBACK existing(HWND,wchar_t* name,HANDLE,ULONG_PTR context) noexcept {
        auto& state=*reinterpret_cast<Presence*>(context);
        wchar_t buffer[256]{};
        const wchar_t* text=name;
        if(IS_INTRESOURCE(name)) {
            if(!GlobalGetAtomNameW(static_cast<ATOM>(reinterpret_cast<ULONG_PTR>(name)),buffer,256))return TRUE;
            text=buffer;
        }
        if(text&&(CompareStringOrdinal(text,-1,state.self.names_[0],-1,TRUE)==CSTR_EQUAL||
                  CompareStringOrdinal(text,-1,state.self.names_[1],-1,TRUE)==CSTR_EQUAL))state.found=true;
        return TRUE;
    }
public:
    Names(const wchar_t* first,const wchar_t* second) noexcept:names_{first,second}{}
    Names(const Names&)=delete;
    Names& operator=(const Names&)=delete;
    bool attach(HWND window) noexcept {
        if(!window)return false;
        AcquireSRWLockExclusive(&lock_);
        if(owner_.load(std::memory_order_relaxed)){ReleaseSRWLockExclusive(&lock_);return false;}
        Presence presence{*this};
        SetLastError(ERROR_SUCCESS);
        const int enumerated=Api::enumerate(window,&existing,reinterpret_cast<LPARAM>(&presence));
        if(presence.found||(enumerated==-1&&GetLastError()!=ERROR_SUCCESS)){
            ReleaseSRWLockExclusive(&lock_);return false;
        }
        // Host-owner marker 1 never claims the child parent-owner marker 2.
        const bool first=Api::set(window,names_[0],reinterpret_cast<HANDLE>(ULONG_PTR{1}))!=FALSE;
        const bool second=first&&Api::set(window,names_[1],reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(GetCurrentProcessId())))!=FALSE;
        const ATOM a=second?Api::find(names_[0]):0,b=second?Api::find(names_[1]):0;
        if(!second||!a||!b){
            if(second)(void)Api::remove(window,names_[1]);
            if(first)(void)Api::remove(window,names_[0]);
            ReleaseSRWLockExclusive(&lock_);return false;
        }
        atoms_[0]=a;atoms_[1]=b;
        ready_.store(true,std::memory_order_release);
        owner_.store(window,std::memory_order_release);
        ReleaseSRWLockExclusive(&lock_);return true;
    }
    void detach(HWND window) noexcept {
        AcquireSRWLockExclusive(&lock_);
        if(window&&owner_.load(std::memory_order_relaxed)==window){
            owner_.store(nullptr,std::memory_order_release);
            ready_.store(false,std::memory_order_release);
            atoms_[0]=atoms_[1]=0;
            (void)Api::remove(window,names_[1]);
            (void)Api::remove(window,names_[0]);
        }
        ReleaseSRWLockExclusive(&lock_);
    }
    bool isAttachedWindow(HWND window) const noexcept {
        return window&&owner_.load(std::memory_order_acquire)==window;
    }
    HANDLE get(HWND window,unsigned index) noexcept {
        if(!ready_.load(std::memory_order_acquire))return Api::get(window,names_[index]);
        AcquireSRWLockShared(&lock_);
        const auto atom=atoms_[index];
        const auto value=Api::get(window,atom?reinterpret_cast<const wchar_t*>(static_cast<ULONG_PTR>(atom)):names_[index]);
        ReleaseSRWLockShared(&lock_);return value;
    }
};
}
