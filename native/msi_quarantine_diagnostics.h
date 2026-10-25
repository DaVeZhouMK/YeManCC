#pragma once
#include <cstdint>
#include <mutex>
namespace ymcc::msi::quarantine {
#ifdef YMCC_MSI_QUARANTINE_DIAGNOSTIC
inline constexpr bool buildEnabled=true;
#else
inline constexpr bool buildEnabled=false;
#endif
class Budget {
    std::mutex mx;uint64_t episode=0,last=0;unsigned count=0;
public:
    void begin(){std::lock_guard<std::mutex> l(mx);++episode;last=0;count=0;}
    uint64_t take(bool eligible,uint64_t now){std::lock_guard<std::mutex> l(mx);
        if(!eligible||count>=3||(count&&now-last<5000))return 0;
        if(!episode)episode=1;++count;last=now;return episode;
    }
};
}
