#pragma once
#include <cstdint>
#include <string>
namespace ymcc::steaminfo {
// A window observation only. Neither a handle value nor an epoch grants an
// action; the consumer must revalidate this exact window/process before use.
struct StartupWindowFacts {
    bool known=false;
    std::uint64_t window=0,processBirth=0,observedAt=0,processEpoch=0,accountEpoch=0;
    std::uint32_t pid=0,windowsSession=0;
    std::string root;
    bool completed=false; // A complete window observation, including no match. Never process-absence authority.
};
inline bool startupWindowCandidate(bool visible,bool sameSession,bool ownedImage,
    bool sdlClass,bool appWindow,bool resizable,int width,int height) {
    return visible&&sameSession&&ownedImage&&sdlClass&&appWindow&&!resizable&&width>=480&&height>=270;
}
}
