#pragma once
#include <cstdint>
#include <limits>
#include <string>

// Private, in-memory proof. No JSON/serialization or operating-system calls.
namespace ymcc::currentrecord {
constexpr std::uint64_t maximumBracketMs = 5000;
struct Proof {
    std::uint64_t account64=0,gameId64=0,steamBirth=0,gameBirth=0;
    std::uint64_t generation=0,epoch=0,rulesEpoch=0,serial=0,sequence=0;
    std::uint64_t uiStartedTick=0,uiFinishedTick=0,uiStartedFileTime=0,uiFinishedFileTime=0;
    std::uint32_t account=0,appid=0,steamPid=0,gamePid=0,windowsSession=0;
    std::string steamRoot,gamePath;
    bool available=false,accountPinned=false,liveAccountPinned=false,localAccountSource=false;
    bool replyOk=false,replyUnmutated=false;
};
inline bool eligible(const Proof& p) {
    return p.available&&p.replyOk&&p.replyUnmutated&&p.accountPinned&&p.liveAccountPinned&&p.localAccountSource&&
        p.account64&&p.account&&static_cast<std::uint32_t>(p.account64)==p.account&&p.gameId64&&p.appid&&
        p.steamPid&&p.steamBirth&&!p.steamRoot.empty()&&p.gamePid&&p.gameBirth&&!p.gamePath.empty()&&
        p.generation&&p.epoch&&p.rulesEpoch&&p.serial&&p.sequence&&p.uiStartedTick&&p.uiStartedFileTime&&
        p.uiStartedTick<=p.uiFinishedTick&&p.uiStartedFileTime<=p.uiFinishedFileTime&&
        p.uiFinishedTick-p.uiStartedTick<=maximumBracketMs;
}
inline bool sameOwner(const Proof& a,const Proof& b) {
    return a.account64==b.account64&&a.gameId64==b.gameId64&&a.appid==b.appid&&a.account==b.account&&
        a.steamPid==b.steamPid&&a.steamBirth==b.steamBirth&&a.steamRoot==b.steamRoot&&
        a.gamePid==b.gamePid&&a.gameBirth==b.gameBirth&&a.gamePath==b.gamePath&&a.windowsSession==b.windowsSession&&
        a.generation==b.generation&&a.epoch==b.epoch&&a.rulesEpoch==b.rulesEpoch&&a.serial==b.serial;
}
inline bool canRead(const Proof& pre,std::uint64_t now,std::uint64_t fileTime,bool owner,bool capturedOrPrepared) {
    return owner&&!capturedOrPrepared&&eligible(pre)&&now>=pre.uiFinishedTick&&
        fileTime>=pre.uiFinishedFileTime&&now-pre.uiStartedTick<=maximumBracketMs;
}
inline bool canCommit(const Proof& pre,const Proof& post,
    std::uint64_t readStart,std::uint64_t readFinish,std::uint64_t readStartFileTime,std::uint64_t readFinishFileTime,
    std::uint64_t commitTick,std::uint64_t commitFileTime,bool owner,bool capturedOrPrepared) {
    return owner&&!capturedOrPrepared&&eligible(pre)&&eligible(post)&&sameOwner(pre,post)&&
        pre.sequence!=std::numeric_limits<std::uint64_t>::max()&&post.sequence==pre.sequence+1&&
        pre.uiFinishedTick<=readStart&&readStart<=readFinish&&readFinish<=post.uiStartedTick&&
        post.uiFinishedTick<=commitTick&&commitTick>=pre.uiStartedTick&&commitTick-pre.uiStartedTick<=maximumBracketMs&&
        pre.uiFinishedFileTime<=readStartFileTime&&readStartFileTime<=readFinishFileTime&&
        readFinishFileTime<=post.uiStartedFileTime&&post.uiFinishedFileTime<=commitFileTime;
}
} // namespace ymcc::currentrecord
