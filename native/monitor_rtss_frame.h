#pragma once
#include <windows.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <cstddef>
#include <cmath>
#include <string>
// Read-only RTSS v2 SDK fields, in YMCC's sole monitor worker. No OSD writes,
// injection, RTSSHooks loading, pipe ownership, or second hardware collector.
namespace ymcc::monitorframe {
struct RtssHeader {
    std::uint32_t signature,version,appEntrySize,appOffset,appCount;
    std::uint32_t osdEntrySize,osdOffset,osdCount,osdFrame,busy;
};
struct RtssAppPrefix {
    std::uint32_t pid; char name[260]; std::uint32_t flags;
    std::uint32_t time0,time1,frames,frameTimeUs;
    std::uint32_t statFlags,statTime0,statTime1,statFrames,statCount,statFpsMin,statFpsAvg,statFpsMax;
    std::uint32_t osdX,osdY,osdPixel,osdColor,osdFrame,screenFlags; char screenPath[260];
    std::uint32_t osdBg,videoFlags; char videoPath[260];
    std::uint32_t videoFps,videoSize,videoFormat,videoQuality,videoThreads,screenQuality,screenThreads;
    std::uint32_t audioFlags,videoFlagsEx,audioFlags2,statFrameMin,statFrameAvg,statFrameMax,statFrameCount;
    std::uint32_t frameBuffer[1024],frameBufferPos,frameBufferFps;
};
static_assert(offsetof(RtssAppPrefix,frameTimeUs)==280);
static_assert(offsetof(RtssAppPrefix,frameBufferPos)==5020);
struct RtssFrame {
    bool valid = false;
    double milliseconds = 0, fps = -1;
    std::uint64_t observedAt = 0;
};
struct RtssMarker {
    std::uint32_t time1=0,frames=0,statFrames=0,position=0,microseconds=0;
    bool operator==(const RtssMarker& b) const { return time1==b.time1 && frames==b.frames && statFrames==b.statFrames && position==b.position && microseconds==b.microseconds; }
};
inline bool rtssHeaderValid(const RtssHeader& h,std::size_t bytes) {
    constexpr std::uint32_t signature = 0x52545353; // MSVC SDK 'RTSS'
    if (h.signature!=signature || (h.version>>16)!=2 || h.version<0x20005 ||
        (h.appOffset%4) || (h.appEntrySize%4) || h.appEntrySize<sizeof(RtssAppPrefix) || h.appEntrySize>65536 ||
        !h.appCount || h.appCount>1024 || h.appOffset<36 ||
        (h.version>=0x2000e && (h.appOffset<sizeof(RtssHeader) || (h.busy&1)))) return false;
    const std::uint64_t end=std::uint64_t(h.appOffset)+std::uint64_t(h.appEntrySize)*h.appCount;
    return end<=bytes;
}
class RtssReader {
    std::wstring mappingName_;
    HANDLE mapping_=nullptr,process_=nullptr;
    const unsigned char* view_=nullptr;
    std::size_t bytes_=0,entry_=0;
    std::uint32_t pid_=0;
    std::uint64_t birth_=0,retryAt_=0,entryRetryAt_=0,progressUntil_=0;
    RtssHeader searchHeader_{};
    bool searchKnown_=false;
    // Discovery is cheap when a matching slot is cached. A missing slot gets
    // a bounded retry; a frozen mapping must not be pinned for the game lifetime.
    static constexpr std::uint64_t entryRetryMs=1000,mappingRecheckMs=5000;
    RtssMarker marker_{};
    bool markerKnown_=false;
    void unmap() {
        if(view_)UnmapViewOfFile(view_);if(mapping_)CloseHandle(mapping_);
        view_=nullptr;mapping_=nullptr;bytes_=entry_=0;markerKnown_=false;
        entryRetryAt_=progressUntil_=0;searchKnown_=false;
    }
    std::uint32_t field(std::size_t offset) const {
        return *reinterpret_cast<const volatile std::uint32_t*>(view_+offset);
    }
public:
    explicit RtssReader(const wchar_t* mappingName=L"RTSSSharedMemoryV2"):mappingName_(mappingName){}
    ~RtssReader(){reset();}
    RtssReader(const RtssReader&)=delete;
    RtssReader& operator=(const RtssReader&)=delete;
    void reset() { unmap();if(process_)CloseHandle(process_);process_=nullptr;pid_=0;birth_=retryAt_=0; }
    bool alive() const { return process_ && WaitForSingleObject(process_,0)==WAIT_TIMEOUT; }
    bool terminated() const { return process_ && WaitForSingleObject(process_,0)==WAIT_OBJECT_0; }
    RtssFrame sample(std::uint32_t pid,std::uint64_t birth,std::uint64_t tick,std::uint64_t epoch) {
        if(!pid || !birth){reset();return {};}
        if(pid_!=pid || birth_!=birth){reset();pid_=pid;birth_=birth;}
        if(!process_){
            if(tick<retryAt_)return {};
            process_=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
            FILETIME created{},exit{},kernel{},user{};
            if(!process_ || !GetProcessTimes(process_,&created,&exit,&kernel,&user) ||
                ((std::uint64_t(created.dwHighDateTime)<<32)|created.dwLowDateTime)!=birth){
                if(process_)CloseHandle(process_);process_=nullptr;retryAt_=tick+1000;return {};
            }
        }
        if(!alive()){unmap();return {};}
        // Reuse the existing sampling clock. No progress for one grace window
        // drops our mapping reference before rediscovery, with normal open backoff.
        if(view_ && tick>=progressUntil_)unmap();
        if(!view_){
            if(tick<retryAt_)return {};
            retryAt_=tick+1000;
            mapping_=OpenFileMappingW(FILE_MAP_READ,FALSE,mappingName_.c_str());
            if(!mapping_)return {};
            view_=static_cast<const unsigned char*>(MapViewOfFile(mapping_,FILE_MAP_READ,0,0,0));
            MEMORY_BASIC_INFORMATION info{};
            if(!view_ || !VirtualQuery(view_,&info,sizeof(info)) || info.RegionSize<sizeof(RtssHeader)){unmap();return {};}
            bytes_=info.RegionSize;progressUntil_=tick+mappingRecheckMs;
        }
        RtssHeader h{};std::memcpy(&h,view_,sizeof(h));
        if(!rtssHeaderValid(h,bytes_)){
            // A writer's short busy window is a miss, not a new producer.
            if(h.signature!=0x52545353)unmap();
            return {};
        }
        if(entry_<h.appOffset || (entry_-h.appOffset)%h.appEntrySize || (entry_-h.appOffset)/h.appEntrySize>=h.appCount || entry_+sizeof(RtssAppPrefix)>bytes_ || field(entry_)!=pid){
            const bool sameSearch=searchKnown_ && searchHeader_.signature==h.signature &&
                searchHeader_.version==h.version && searchHeader_.appOffset==h.appOffset &&
                searchHeader_.appEntrySize==h.appEntrySize && searchHeader_.appCount==h.appCount;
            if(sameSearch && tick<entryRetryAt_)return {};
            searchHeader_=h;searchKnown_=true;entryRetryAt_=tick+entryRetryMs;
            entry_=0;markerKnown_=false;
            for(std::uint32_t i=0;i<h.appCount;++i){
                const std::size_t at=h.appOffset+std::size_t(i)*h.appEntrySize;
                if(field(at)==pid){entry_=at;break;}
            }
            if(!entry_)return {};
            entryRetryAt_=0;
        }
        const auto get=[&](std::size_t offset){return field(entry_+offset);};
        RtssMarker a{get(offsetof(RtssAppPrefix,time1)),get(offsetof(RtssAppPrefix,frames)),get(offsetof(RtssAppPrefix,statFrames)),get(offsetof(RtssAppPrefix,frameBufferPos)),get(offsetof(RtssAppPrefix,frameTimeUs))};
        const auto time0=get(offsetof(RtssAppPrefix,time0));
        MemoryBarrier();
        RtssMarker b{get(offsetof(RtssAppPrefix,time1)),get(offsetof(RtssAppPrefix,frames)),get(offsetof(RtssAppPrefix,statFrames)),get(offsetof(RtssAppPrefix,frameBufferPos)),get(offsetof(RtssAppPrefix,frameTimeUs))};
        if(!(a==b) || field(entry_)!=pid || field(0)!=h.signature || field(4)!=h.version || field(8)!=h.appEntrySize || field(12)!=h.appOffset || field(16)!=h.appCount ||
            (h.version>=0x2000e && (field(offsetof(RtssHeader,busy))&1)))return {};
        const bool advanced=markerKnown_ && !(a==marker_);
        marker_=a;markerKnown_=true;
        // A first read/unchanged cache has no proof of current rendering.
        // Current per-frame duration is in microseconds; do not use reciprocal FPS.
        if(!advanced || !a.microseconds || a.microseconds>10000000)return {};
        progressUntil_=tick+mappingRecheckMs;
        const double ms=a.microseconds/1000.0;
        const std::uint32_t interval=a.time1-time0; // unsigned Windows tick wrap
        const double fps=time0 && interval && interval<=10000 ? 1000.0*a.frames/interval : -1;
        return {true,ms,std::isfinite(fps)&&fps>=0&&fps<=1000000?fps:-1,epoch};
    }
};
}
