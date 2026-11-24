#pragma once
#include <cstdint>
#include <optional>
#include <system_error>

namespace ymcc::media::playback30 {
inline constexpr uint64_t workerHardMs = 180000;
class Deadline {
    static constexpr uint64_t softMs = 65000, hardMs = workerHardMs+5000;
    static constexpr uint64_t pollMs = 5000, graceMs = 15000;
    uint64_t nextAt_ = softMs-graceMs, lastGrowthAt_ = 0;
    std::optional<uint64_t> previousSize_;
public:
    constexpr uint64_t expires() const {
        const auto progress = lastGrowthAt_ ? lastGrowthAt_+graceMs : 0;
        return progress<=softMs ? softMs : progress<hardMs ? progress : hardMs;
    }
    constexpr bool shouldObserve(uint64_t elapsed) const {return elapsed>=nextAt_&&elapsed<expires();}
    constexpr void observe(uint64_t elapsed,std::optional<uint64_t> bytes) {
        if(!shouldObserve(elapsed))return;
        nextAt_=elapsed+pollMs;
        if(!bytes)return;
        // A first observation or a reset file size is not proof of progress.
        if(previousSize_&&*bytes>*previousSize_)lastGrowthAt_=elapsed;
        else if(previousSize_&&*bytes<*previousSize_)lastGrowthAt_=0;
        previousSize_=bytes;
    }
    void observeFileSize(uint64_t elapsed,uint64_t bytes,const std::error_code& error) {
        // An observed missing output is a zero-byte baseline, not an unreadable size.
        if(!error)observe(elapsed,bytes);
        else if(error==std::errc::no_such_file_or_directory)observe(elapsed,0);
        else observe(elapsed,std::nullopt);
    }
};
} // namespace ymcc::media::playback30
