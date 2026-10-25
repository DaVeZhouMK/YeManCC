#pragma once
#include "steam_shortcut_metadata_baseline_policy.h"
// Called only by the already-owned running-link worker before a capture exists.
// It leaves the normal cadence and the five-second identity window unchanged.
namespace SleepSteamRecovery {
struct ShortcutBaselineAttemptResult {
 bool attempted=false,committed=false,identityPostflightInvoked=false;
 bool postflightIdentityAccepted=false,postflightAccountChanged=false,postflightSessionChanged=false,postflightSameOwnerMismatch=false;
 CurrentRecordPre nextPre;
 json diagnostic,readMetadata;
};
static bool shortcutBaselineFresh(const CurrentRecordPre& pre,std::uint64_t now){
 NativeRecordPrefetch prior;{std::lock_guard<std::mutex> lock(nativeRecordPrefetchMx);prior=nativeRecordPrefetch;}
 const auto& p=pre.proof;
 return prior.identity.bound&&prior.identity.shortcutMetadata.valid&&
  prior.identity.account64==p.account64&&prior.identity.gameId64==p.gameId64&&prior.identity.appid==p.appid&&prior.identity.windowsSession==p.windowsSession&&
  prior.witness.generation==p.generation&&prior.witness.epoch==p.epoch&&ymcc::shortcutbaseline::fresh(now,prior.witness.observedTick)&&
  sameClientProcess(prior.client,pre.client)&&prior.client.account==pre.client.account&&
  prior.game.pid==pre.game.pid&&prior.game.created==pre.game.created&&pathKey(W2U(prior.game.path))==pathKey(W2U(pre.game.path));
}
static ShortcutBaselineAttemptResult boundedShortcutBaseline(const CurrentRecordPre& pre,std::uint64_t& identitySequence,const std::function<bool()>& active){
 ShortcutBaselineAttemptResult result;result.nextPre=pre;
 try{
  const auto& p=pre.proof;const auto now=GetTickCount64();
  const bool shortcut=p.appid>=0x80001388U&&(p.gameId64>>32)==p.appid&&(p.gameId64&0xffffffffULL)==0x02000000ULL;
  if(!shortcut||!active||!active()||!currentRecordOwner(pre)||identitySequence!=p.sequence||identitySequence==std::numeric_limits<std::uint64_t>::max())return result;
  const bool capturedNow=currentRecordCapturePresent(p.generation);const bool cached=shortcutBaselineFresh(pre,now);
  const ymcc::shortcutbaseline::Key key{p.account64,p.gameId64,p.steamBirth,p.gameBirth,p.generation,p.epoch,p.rulesEpoch,p.serial,p.steamPid,p.gamePid,p.windowsSession,p.steamRoot,p.gamePath};
  static std::mutex mx;static ymcc::shortcutbaseline::Attempt last;
  {std::lock_guard<std::mutex> lock(mx);if(!ymcc::shortcutbaseline::take(last,key,now,true,capturedNow,ymcc::currentrecord::eligible(p),cached))return result;}
  result.attempted=true;auto read=readCurrentRecordBeforeExistingUi(pre,active);
  result.readMetadata=currentRecordReadMetadata(read,false);
  if(!read.attempted||!flag(read.observed,"available")||!flag(read.observed,"originalPrimaryMatched")||
    !read.observed.contains("shortcutMetadataDiagnostic")||!flag(read.observed["shortcutMetadataDiagnostic"],"available"))return result;
  const auto owned=[&]{return active()&&currentRecordOwner(pre);};if(!owned())return result;
  // This is one real additional read-only request, not a fabricated proof step.
  // A failed identity reply consumes PRE; it cannot be silently skipped later.
  result.identityPostflightInvoked=true;result.nextPre={};const auto postSequence=++identitySequence;
  const auto uiStartedTick=GetTickCount64(),uiStartedFileTime=sgNowFileTime();ymcc::steamrecordidentity::Read identity;
  const auto reply=liveRequest(pre.client,{{"operation","sleep.session.inspect"},{"appids",json::array({p.appid})},{"runningLinkRead",true}},owned,&identity);
  const auto uiFinishedTick=GetTickCount64(),uiFinishedFileTime=sgNowFileTime();
  const auto replyReason=reply.value("reason",std::string());const auto observedAccount=number(reply,"observedAccount");
  result.postflightAccountChanged=replyReason=="steam-account-changed"||(observedAccount&&observedAccount!=p.account)||
   (identity.available&&identity.account64&&identity.account64!=p.account64);
  result.postflightSessionChanged=replyReason=="steam-session-changed";
  if(!owned()||result.postflightAccountChanged||result.postflightSessionChanged||observedAccount!=p.account)return result;
  const auto post=currentRecordProof(pre.client,pre.game,p.appid,identity,reply,p.generation,p.epoch,p.rulesEpoch,p.serial,postSequence,p.windowsSession,std::to_string(p.gameId64),uiStartedTick,uiFinishedTick,uiStartedFileTime,uiFinishedFileTime);
  if(!ymcc::currentrecord::eligible(post.proof))return result;
  result.postflightSameOwnerMismatch=!ymcc::currentrecord::sameOwner(pre.proof,post.proof);
  if(result.postflightSameOwnerMismatch)return result;
  result.postflightIdentityAccepted=true;result.nextPre=post;
  read.observed["boundedBaselineIdentityPostflightInvoked"]=true;
  read.observed["baselineIdentityPostflightStartedTick"]=uiStartedTick;read.observed["baselineIdentityPostflightFinishedTick"]=uiFinishedTick;
  json diagnostic;
  if(commitCurrentRecordAfterExistingUi(read,post,reply,active,diagnostic)){
   result.committed=true;result.diagnostic=std::move(diagnostic);result.readMetadata=currentRecordReadMetadata(read,true);
  }
 }catch(...){result.nextPre={};}
 return result;
}
}
