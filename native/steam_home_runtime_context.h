#pragma once
#include "steam_information_client.h"
#include <atomic>
#include <functional>
#include <memory>

namespace ymcc::steamaction {
// Captured before queueing. This is the optional Home activation's current
// Windows context, separate from package identity and Steam observations.
class HomeRuntimeContext final {
    using Flag=BOOL(WINAPI*)();
    std::shared_ptr<const steaminfo::Peer> caller_;
    HMODULE api_=nullptr;
    Flag supported_=nullptr,active_=nullptr;
    std::wstring selected_;
    std::atomic<bool> revoked_{false};
public:
    ~HomeRuntimeContext(){if(api_)FreeLibrary(api_);}
    bool initialize(std::shared_ptr<const steaminfo::Peer> authenticated)noexcept{
        try{
            if(caller_||!authenticated||!authenticated->live())return false;
            caller_=std::move(authenticated);
            UINT32 chars=0;
            if(GetPackageFamilyName(caller_->process.value,&chars,nullptr)!=ERROR_INSUFFICIENT_BUFFER||chars<2||chars>256)return false;
            const auto capacity=chars;std::vector<wchar_t> family(capacity);
            if(GetPackageFamilyName(caller_->process.value,&chars,family.data())!=ERROR_SUCCESS||
                !steaminfo::gaming_home_identity_detail::countedString(family.data(),chars,capacity,256))return false;
            selected_.assign(family.data(),chars-1);selected_+=L"!Home";
            api_=LoadLibraryExW(L"api-ms-win-gaming-experience-l1-1-0.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
            if(!api_)return false;
            supported_=reinterpret_cast<Flag>(GetProcAddress(api_,"IsGamingFullScreenExperienceSupported"));
            active_=reinterpret_cast<Flag>(GetProcAddress(api_,"IsGamingFullScreenExperienceActive"));
            return current();
        }catch(...){revoked_.store(true,std::memory_order_release);return false;}
    }
    bool current()noexcept{
        if(revoked_.load(std::memory_order_acquire))return false;
        bool ok=false;
        try{
            wchar_t value[512]{};DWORD bytes=sizeof(value);
            const auto status=RegGetValueW(HKEY_CURRENT_USER,
                L"Software\\Microsoft\\Windows\\CurrentVersion\\GamingConfiguration",L"GamingHomeApp",
                RRF_RT_REG_SZ,nullptr,value,&bytes);
            ok=caller_&&caller_->live()&&supported_&&active_&&supported_()&&active_()&&status==ERROR_SUCCESS&&
                bytes>=sizeof(wchar_t)&&bytes<=sizeof(value)&&bytes%sizeof(wchar_t)==0&&
                value[bytes/sizeof(wchar_t)-1]==L'\0'&&std::wstring_view(value,bytes/sizeof(wchar_t)-1)==selected_;
        }catch(...){ok=false;}
        if(!ok)revoked_.store(true,std::memory_order_release);
        return ok;
    }
};
inline std::function<bool()> bindHomeRuntime(std::shared_ptr<const steaminfo::Peer> authenticated) {
    auto context=std::make_shared<HomeRuntimeContext>();
    if(!context->initialize(std::move(authenticated)))return {};
    return [context=std::move(context)]{return context->current();};
}
}
