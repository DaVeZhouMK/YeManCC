#pragma once
#include "steam_native_current_bracket.h"

// Include after sleep_steam_recovery.h. This reader never calls liveRequest.
namespace SleepSteamRecovery {
struct CurrentRecordPre {
    ymcc::currentrecord::Proof proof;
    Session client;
    Game game;
};
struct CurrentRecordRead {
    CurrentRecordPre pre;
    json observed;
    std::uint64_t startedTick=0,finishedTick=0,startedFileTime=0,finishedFileTime=0;
    bool attempted=false,shortcutMetadataInvoked=false;
    ymcc::shortcutmetadatawin::Witness shortcutMetadata; // private only; never JSON/logged
};
// Only a fixed allowlist of scalar metadata may escape a discarded sample.
// Missing/provider-zero counts never imply that no memory read took place.
static json currentRecordReadMetadata(const CurrentRecordRead& read,bool committed) {
    if(!read.attempted)return nullptr;
    const auto count=[](const json& provider,const char* key)->json {
        if(!provider.is_object())return nullptr;
        const auto i=provider.find(key);
        if(i==provider.end())return nullptr;
        if(i->is_number_unsigned())return *i;
        if(i->is_number_integer()&&i->get<std::int64_t>()>=0)return *i;
        return nullptr;
    };
    const auto calls=count(read.observed,"readCalls"),bytes=count(read.observed,"readBytes");
    const auto metadata=read.observed.find("shortcutMetadataDiagnostic");
    const bool metadataCounts=read.shortcutMetadataInvoked&&metadata!=read.observed.end()&&metadata->is_object();
    const auto metadataCalls=metadataCounts?count(*metadata,"readCalls"):json(nullptr);
    const auto metadataBytes=metadataCounts?count(*metadata,"readBytes"):json(nullptr);
    return {{"source","independent-current-native-read-metadata"},{"authority","diagnostic-only-no-admission"},
        {"nativeObservationInvoked",true},{"nativeDiagnosticCommitAccepted",committed},{"discarded",!committed},
        {"readCalls",calls},{"readBytes",bytes},{"countsKnown",!calls.is_null()&&!bytes.is_null()},
        {"shortcutMetadataObservationInvoked",read.shortcutMetadataInvoked},
        {"shortcutMetadataReadCalls",metadataCalls},{"shortcutMetadataReadBytes",metadataBytes},
        {"countsScope","provider-reported-not-whole-call-accounting"},{"zeroIsProofOfNoRead",false},
        {"readStartedTick",read.startedTick},{"readFinishedTick",read.finishedTick},
        {"readStartedFileTime",std::to_string(read.startedFileTime)},{"readFinishedFileTime",std::to_string(read.finishedFileTime)}};
}
static bool currentRecordCapturePresent(unsigned long long generation) {
    std::lock_guard<std::mutex> lock(captureMx);
    // Any prepared boundary or same-generation original blocks this new path,
    // including unbound originals and originals from another config epoch.
    return preparedPending||captured.generation==generation;
}
static bool currentRecordProcessOwner(const CurrentRecordPre& pre) {
    const auto& p=pre.proof;
    if(!ymcc::currentrecord::eligible(p)||!g_sleepRecoveryAutoEnabled.load()||g_exitRequested.load()||
        g_powerLifecycle.load()!=PowerLifecycle::Ready||currentPowerGeneration()!=p.generation||
        g_sleepRecoveryAutoConfigEpoch.load()!=p.epoch||g_gameRulesEpoch.load()!=p.rulesEpoch||
        !validSession(pre.client)||!pre.client.accountPinned||!pre.client.liveAccountPinned||
        pre.client.accountSource!="running-link-independent-steam-ui-read"||pre.client.account!=p.account||
        pre.client.pid!=p.steamPid||pre.client.created!=p.steamBirth||pathKey(W2U(pre.client.root))!=p.steamRoot||
        pre.game.pid!=p.gamePid||pre.game.created!=p.gameBirth||pathKey(W2U(pre.game.path))!=p.gamePath||!sameGame(pre.game))return false;
    const auto nowTick=GetTickCount64();
    if(nowTick<p.uiFinishedTick||nowTick-p.uiStartedTick>ymcc::currentrecord::maximumBracketMs||sgNowFileTime()<p.uiFinishedFileTime)return false;
    const auto now=session();
    if(!sameClientProcess(pre.client,now)||(now.account&&now.account!=pre.client.account))return false;
    // Accept only the precise "global binding absent" fallback. Do not convert
    // ambiguity, explicit logout, or a registered-process account refusal into it.
    if(!now.account&&(now.source!="verified-session-observer"||!now.foreignRegistryAccountIgnored||now.reason!="steam-account-not-ready"))return false;
    DWORD steamSession=0,gameSession=0;
    return ProcessIdToSessionId(pre.client.pid,&steamSession)&&ProcessIdToSessionId(pre.game.pid,&gameSession)&&
        steamSession==p.windowsSession&&gameSession==p.windowsSession;
}
static bool currentRecordOwner(const CurrentRecordPre& pre) {
    return !currentRecordCapturePresent(pre.proof.generation)&&currentRecordProcessOwner(pre);
}
static CurrentRecordPre currentRecordProof(const Session& client,const Game& game,DWORD appid,
    const ymcc::steamrecordidentity::Read& identity,const json& reply,unsigned long long generation,unsigned long long epoch,
    unsigned long long rulesEpoch,unsigned long long serial,unsigned long long sequence,DWORD windowsSession,const std::string& exactGameId,
    std::uint64_t startedTick,std::uint64_t finishedTick,std::uint64_t startedFileTime,std::uint64_t finishedFileTime) {
    CurrentRecordPre value;
    std::uint64_t fullGameId=0;
    if(!identity.available||identity.appid!=appid||!ymcc::sleepsteam::unsignedDecimal(exactGameId,fullGameId)||
        identity.gameId64!=fullGameId||!flag(reply,"ok")||!reply.contains("mutated")||
        !reply["mutated"].is_boolean()||flag(reply,"mutated"))return value;
    auto& p=value.proof;
    p.account64=identity.account64;p.gameId64=identity.gameId64;p.appid=appid;p.account=client.account;
    p.steamPid=client.pid;p.steamBirth=client.created;p.steamRoot=pathKey(W2U(client.root));
    p.gamePid=game.pid;p.gameBirth=game.created;p.gamePath=pathKey(W2U(game.path));p.windowsSession=windowsSession;
    p.generation=generation;p.epoch=epoch;p.rulesEpoch=rulesEpoch;p.serial=serial;p.sequence=sequence;
    p.uiStartedTick=startedTick;p.uiFinishedTick=finishedTick;p.uiStartedFileTime=startedFileTime;p.uiFinishedFileTime=finishedFileTime;
    p.available=identity.available;p.accountPinned=client.accountPinned;p.liveAccountPinned=client.liveAccountPinned;
    p.localAccountSource=client.accountSource=="running-link-independent-steam-ui-read";p.replyOk=true;p.replyUnmutated=true;
    value.client=client;value.game=game;
    if(!currentRecordOwner(value))return {};
    return value;
}
static CurrentRecordRead readCurrentRecordBeforeExistingUi(const CurrentRecordPre& pre,const std::function<bool()>& active) {
    CurrentRecordRead value;
    const auto owned=[&]{return active&&active()&&currentRecordOwner(pre);};
    const auto tick=GetTickCount64(),fileTime=sgNowFileTime();
    if(!ymcc::currentrecord::canRead(pre.proof,tick,fileTime,owned(),currentRecordCapturePresent(pre.proof.generation)))return value;
    value.pre=pre;value.startedTick=GetTickCount64();value.startedFileTime=sgNowFileTime();value.attempted=true;
    ymcc::steamrecordwin::Context context;
    context.steamPid=pre.client.pid;context.steamBirth=pre.client.created;context.steamRoot=pre.client.root;
    context.gamePid=pre.game.pid;context.gameBirth=pre.game.created;context.gamePath=pre.game.path;
    context.account64=pre.proof.account64;context.gameId64=pre.proof.gameId64;
    context.windowsSession=pre.proof.windowsSession;context.sessionBound=true;
    value.observed=ymcc::steamrecordwin::observe(context,[](const std::wstring& path){return sha256File(path);},owned);
    auto metadata=ymcc::shortcutmetadatawin::unavailable("native-record-original-primary-unavailable");
    if(flag(value.observed,"available")&&flag(value.observed,"originalPrimaryMatched")&&owned()) {
        ymcc::shortcutmetadatawin::Request request;
        request.original=context;request.appid=pre.proof.appid;
        request.generation=pre.proof.generation;request.epoch=pre.proof.epoch;
        request.phase=ymcc::shortcutmetadatawin::Phase::Baseline;
        request.beforeCaptureAdmitted=!currentRecordCapturePresent(pre.proof.generation);
        value.shortcutMetadataInvoked=true;
        metadata=ymcc::shortcutmetadatawin::observe(request,[](const std::wstring& path){return sha256File(path);},owned);
    }
    if(flag(metadata.diagnostic,"available")&&metadata.witness.valid)value.shortcutMetadata=std::move(metadata.witness);
    // Carry the complete metadata diagnostic through the existing POST bracket;
    // snapshot availability and allocation/transition fields grant no admission.
    value.observed["shortcutMetadataDiagnostic"]=std::move(metadata.diagnostic);
    value.finishedTick=GetTickCount64();value.finishedFileTime=sgNowFileTime();
    return value;
}
static bool commitCurrentRecordAfterExistingUi(const CurrentRecordRead& read,const CurrentRecordPre& post,
    const json& ui,const std::function<bool()>& active,json& diagnostic) {
    if(!read.attempted||!flag(read.observed,"available")||!active||!active()||!currentRecordOwner(read.pre)||!currentRecordOwner(post))return false;
    const auto commitTick=GetTickCount64(),commitFileTime=sgNowFileTime();
    if(!ymcc::currentrecord::canCommit(read.pre.proof,post.proof,read.startedTick,read.finishedTick,read.startedFileTime,read.finishedFileTime,
        commitTick,commitFileTime,true,currentRecordCapturePresent(post.proof.generation)))return false;
    auto observed=read.observed;
    observed["observerScope"]="independent-read-only-running-link";observed["captureIdentityBound"]=false;
    observed["identityBasis"]=read.observed.value("boundedBaselineIdentityPostflightInvoked",false)
        ?"owned-ui-read-plus-bounded-identity-postflight":"consecutive-existing-ui-private-identity-bracket";
    observed["uiAndNativeAtomic"]=false;observed["loginGenerationLease"]="unknown";
    observed["privateUiIdentityMatchedBeforeAfter"]=true;
    observed["nativeRecordReadStartedTick"]=read.startedTick;observed["nativeRecordReadFinishedTick"]=read.finishedTick;
    observed["nativeRecordReadStartedFileTime"]=std::to_string(read.startedFileTime);
    observed["nativeRecordReadFinishedFileTime"]=std::to_string(read.finishedFileTime);
    observed["preUiStartedTick"]=read.pre.proof.uiStartedTick;observed["preUiFinishedTick"]=read.pre.proof.uiFinishedTick;
    observed["postUiStartedTick"]=post.proof.uiStartedTick;observed["postUiFinishedTick"]=post.proof.uiFinishedTick;
    observed["observationFileTime"]=std::to_string(read.finishedFileTime);
    if(ui.contains("overviews")&&ui["overviews"].is_array())for(const auto& row:ui["overviews"]){
        std::uint64_t rowGameId=0;
        if(row.is_object()&&number(row,"appid")==post.proof.appid&&row.contains("gameidString")&&row["gameidString"].is_string()&&
            ymcc::sleepsteam::unsignedDecimal(row["gameidString"].get<std::string>(),rowGameId)&&rowGameId==post.proof.gameId64){
            observed["uiOverview"]={{"localDisplayStatus",row.value("localDisplayStatus",json())},{"inRunningList",row.value("inRunningList",json())},{"source","steam-ui-native-overview-store"}};break;
        }
    }
    NativeRecordPrefetch value;value.client=post.client;value.game=post.game;
    value.identity.account64=post.proof.account64;value.identity.gameId64=post.proof.gameId64;value.identity.appid=post.proof.appid;
    value.identity.windowsSession=post.proof.windowsSession;value.identity.bound=true;value.identity.reason="native-record-pre-capture-ui-bound";
    // Copy the private baseline witness already obtained by this read; public
    // allocation/transition diagnostics never select or rebuild its pointers.
    value.identity.shortcutMetadata=read.shortcutMetadata;
    // Timestamp completion, never the earlier PRE: a capture already started
    // during the read must not treat this proof as preceding its boundary.
    value.witness={post.proof.generation,post.proof.epoch,commitTick,commitFileTime};value.nativeBefore=observed;
    // Assign the public diagnostic first; any allocation failure prevents commit.
    // The caller publishes only after true is returned.
    diagnostic=std::move(observed);
    {
        std::lock_guard<std::mutex> captureLock(captureMx);
        if(preparedPending||captured.generation==post.proof.generation||!active()||
            !currentRecordProcessOwner(read.pre)||!currentRecordProcessOwner(post))return false;
        const auto finalTick=GetTickCount64(),finalFileTime=sgNowFileTime();
        if(!ymcc::currentrecord::canCommit(read.pre.proof,post.proof,read.startedTick,read.finishedTick,read.startedFileTime,read.finishedFileTime,
            finalTick,finalFileTime,true,false))return false;
        value.witness={post.proof.generation,post.proof.epoch,finalTick,finalFileTime};
        std::lock_guard<std::mutex> prefetchLock(nativeRecordPrefetchMx);
        nativeRecordPrefetch=std::move(value);
    }
    return true;
}
} // namespace SleepSteamRecovery

