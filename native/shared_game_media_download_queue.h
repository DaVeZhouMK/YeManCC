#pragma once
#include "shared_game_media_shortcut.h"
#include <optional>

namespace ymcc::media::downloads {
// The queue outlives either UI. Only the short-lived library worker owns a
// download; the main program owns its process HANDLE and bounded restart
// policy.
inline constexpr uint64_t stallMs = 90000, taskMs = 240000, sessionMs = 900000;
inline constexpr const char* sharedSleepPause="media-shared-power-paused";
inline constexpr int maxRecoveryAttempts = 2, automaticAttemptLimit = 3, maximumParallel = 10;
inline constexpr int64_t retryMs = 20000, automaticAttemptMs = 180000, automaticGameMs = 360000;
// Explicit video choice may use the existing six-minute game budget in one
// serial transfer; automatic attempts retain their original three-minute cap.
inline constexpr int64_t manualVideoAttemptMs = automaticGameMs;
// Only the established, numeric built-in tool ID is excluded here. Preserve
// historical records and any still-owned activity until its owner settles it.
inline bool excludedQueueTool(const Json& job){const auto id=job.find("appId");return id!=job.end()&&id->is_number_integer()&&custom_steam_library::builtinExcludedSteamAppId(*id);}
inline bool inactiveExcludedQueueTool(const Json& state,const std::string& key,const Json& job){
  const auto active=state.find("active");return excludedQueueTool(job)&&(active==state.end()||(active->is_object()&&!active->contains(key)));
}
inline bool itemStopped(const Json& job){return excludedQueueTool(job)||job.value("autoStopped",false)||job.value("retiredPopular",false);}
inline void resetAutomaticBudget(Json& job){job.erase("sharedSleepWindow");job.erase("lastTimeoutObservation");job["autoStopped"]=false;job["automaticElapsedMs"]=0;job["attempts"]=0;job["retryAt"]=0;job.erase("stopReason");if(job.value("state",std::string{})=="unavailable")job["state"]="pending";}

// Durable continuation of an already admitted attempt,not a new allowance.
// Identity is business provenance only;Native public current is still required.
inline bool sameSleepWork(const Json& job,const Json& window){
  if(!window.is_object()||window.value("version",0)!=1)return false;
  for(const auto* f:{"requestBindingRevision","provenanceRevision","selectedVideo"})if(window.value(f,Json{})!=job.value(f,Json{}))return false;
  return true;
}
inline void chargeSleepWall(Json& job,Json& window,int64_t time){
  const auto issued=window.value("attemptIssuedAt",int64_t{0}),deadline=window.value("attemptDeadlineAt",int64_t{0}),charged=window.value("chargedAt",int64_t{0});
  const auto sessionStart=window.value("sessionIssuedAt",int64_t{0}),sessionEnd=window.value("sessionDeadlineAt",int64_t{0});
  if(issued<=0||deadline<issued||deadline-issued>manualVideoAttemptMs||charged<issued||charged>deadline||sessionStart<=0||sessionEnd<sessionStart||sessionEnd-sessionStart>int64_t(sessionMs)||time<charged)throw std::runtime_error("media-sleep-continuation-unconfirmed");
  // A historical checkpoint may already carry a charge beyond the session end.
  // Keep that debt, but never extend it beyond the earlier deadline.
  const auto until=(std::max)(charged,(std::min)({time,deadline,sessionEnd})),elapsed=until-charged;
  job["automaticElapsedMs"]=(std::min)(automaticGameMs,job.value("automaticElapsedMs",int64_t{0})+elapsed);window["chargedAt"]=until;
}
inline Json sleepWindowFor(const Json& state,const Json& active){
  Json w=active.value("sharedSleepWindow",Json::object());
  if(!sameSleepWork(active,w)){
    const auto began=active.value("taskStartedAt",now());const auto budget=(std::clamp)(active.value("attemptBudgetMs",automaticAttemptMs),int64_t{1},manualVideoAttemptMs);
    w={{"version",1},{"attemptIssuedAt",began},{"attemptDeadlineAt",began+budget},{"chargedAt",began},
       {"sessionIssuedAt",active.value("sessionIssuedAt",state.value("sessionIssuedAt",began))},
       {"sessionDeadlineAt",active.value("sessionDeadlineAt",state.value("sessionDeadlineAt",began+int64_t(sessionMs)))}};
    for(const auto* f:{"requestBindingRevision","provenanceRevision","selectedVideo"})if(active.contains(f))w[f]=active[f];
  }
  return w;
}


inline void markSleepActive(Json& state,const std::string& token,int64_t time){
  state["sharedSleepCancelledSession"]=token;
  state["sharedSleepSession"]={{"version",1},{"sessionIssuedAt",state.value("sessionIssuedAt",time)},{"sessionDeadlineAt",state.value("sessionDeadlineAt",time+int64_t(sessionMs))},{"manualRequestId",state.value("manualRequestId",std::string{})},{"manualUntil",state.value("manualUntil",int64_t{0})}};
  for(auto it=state["active"].begin();it!=state["active"].end();++it){
    if(!state["jobs"].contains(it.key()))continue;auto& job=state["jobs"][it.key()];auto& active=it.value();
    if(stringField(active,"session")!=token)continue;bool same=true;
    for(const auto* field:{"requestBindingRevision","provenanceRevision","selectedVideo"})if(job.value(field,Json{})!=active.value(field,Json{}))same=false;
    if(!same)continue;auto window=sleepWindowFor(state,active);chargeSleepWall(job,window,time);job["sharedSleepWindow"]=window;active["sharedSleepWindow"]=std::move(window);
  }
}

inline bool sharedFailure(const std::string& reason){return reason=="media-shared-power-unavailable"||reason==sharedSleepPause||reason.find("media-disk-")==0||reason.find("media-cache-")==0||reason.find("media-staging-")==0||reason=="media-storage-unavailable"||reason=="media-identity-network-unavailable"||reason=="media-steam-facts-unavailable"||reason=="download-source-cleanup-incomplete"||reason.find("upgrade-required")!=std::string::npos||reason.find("power-paused")!=std::string::npos||reason.find("cancelled")!=std::string::npos;}
struct Limits {
  uint64_t stall = stallMs, task = taskMs, session = sessionMs;
  DWORD monitor = 5000;
  uint64_t startup = 30000;
  DWORD recheck = static_cast<DWORD>(retryMs);
  uint64_t attempt = manualVideoAttemptMs; // Per-claim budget below still limits automatic work.
};
inline std::wstring eventName(const fs::path &root, const std::wstring &role) {
  return L"Local\\YMCC-MediaDownloads-" +
         wide(digestKey(normalized(utf8(fs::absolute(root).wstring())))) +
         L"-" + role;
}
inline void notify(const fs::path &root, bool wakeOwner = true) {
  for (const auto *role : {L"main", L"library", L"owner"}) {
    if (!wakeOwner && std::wstring_view(role) == L"owner")
      continue;
    HANDLE e =
        OpenEventW(EVENT_MODIFY_STATE, FALSE, eventName(root, role).c_str());
    if (e) {
      SetEvent(e);
      CloseHandle(e);
    }
  }
}
// Focus/visibility changes only invalidate resource admission. Queue state
// publications use notify() so both UI observers receive actual changes.
inline void notifyOwner(const fs::path &root) {
  HANDLE event=OpenEventW(EVENT_MODIFY_STATE,FALSE,eventName(root,L"owner").c_str());
  if(event){SetEvent(event);CloseHandle(event);}
}
// Preserve the saved 0/1/2/3 tiers. Shortcut provenance splits tier 2
// without reinterpreting a pre-upgrade confirmed-game queue.
inline int sourcePriority(const Json &target) {
  const auto tier=(std::clamp)(target.value("sourceTier",3),0,3);
  return tier<2?tier:stringField(target,"catalogSource")=="steam-shortcut"?2:tier==2?3:4;
}
inline bool pendingShortcut(const Json &target) {
  if(stringField(target,"catalogSource")!="steam-shortcut")return false;
  const auto exe=stringField(target,"executable"),account=stringField(target,"steamAccount");
  if(exe.empty()||account.empty()||account.size()>10||account.find_first_not_of("0123456789")!=std::string::npos)return false;
  try{const auto id=std::stoull(account);if(!id||id>UINT32_MAX)return false;shortcutAppId(stringField(target,"shortcutAppId"));
    const auto file=fs::path(wide(exe));return file.is_absolute()&&normalized(utf8(file.extension().wstring()))==".exe";
  }catch(...){return false;}
}
inline std::string targetKey(const Json &target) {
  const auto id = target.value("appId", int64_t{0});
  return id > 0 && id <= 2147483647 ? "steam-" + std::to_string(id)
      :target.value("identityPending",false)&&pendingShortcut(target)?"file-"+digestKey(normalized(stringField(target,"executable"))):std::string{};
}
inline bool unavailableResult(const std::string& reason){return reason=="no-dynamic-video"||reason=="store-unavailable"||reason=="not-game-or-no-video";}
inline bool complete(const Json &e, int64_t time) {
  if(pendingMediaRefresh(e,"dynamic")||pendingMediaRefresh(e,"static"))return false;
  for (const auto *slot : {"static", "dynamic"})
    if (!e.value(std::string(slot) + "Ready", false) &&
        !e.value("deletedSlots", Json::object()).value(slot, false) &&
        !e.value("manualSlots", Json::object()).value(slot, false) &&
        e.value(std::string(slot) == "dynamic" ? "videoUnavailableUntil"
                                               : "staticUnavailableUntil",
                int64_t{0}) <= time)
      return false;
  return true;
}
// A user request is a bounded run, independent from the automatic switch.
// Only CPU/game soft gates may be bypassed; power/startup/storage stay intact.
inline bool manualRun(const Json& j){const auto until=j.value("manualUntil",int64_t{0});return until>now()&&until<=now()+int64_t(sessionMs);}
inline bool policyBlocked(const Json& j){if(powerPolicy::blocked())return true;const auto pause=stringField(j,"pauseReason");return !pause.empty()&&!(manualRun(j)&&pause=="game-running");}
inline bool downloadsEnabled(const Json& j){return j.value("enabled",true)||manualRun(j);}
inline bool runnable(const Json &job, int64_t time) {
  return !job.value("recoveryUnconfirmed",false) && !itemStopped(job) && job.value("state", std::string{"pending"}) != "done" &&
         job.value("state", std::string{}) != "running" &&
         job.value("retryAt", int64_t{0}) <= time;
}
inline bool scopedManual(const Json& j){return manualRun(j)&&j.contains("manualTargets")&&j["manualTargets"].is_array()&&!j["manualTargets"].empty();}
inline bool manualTarget(const Json& j,const std::string& key){return !scopedManual(j)||std::find(j["manualTargets"].begin(),j["manualTargets"].end(),key)!=j["manualTargets"].end();}
inline std::string nextKey(const Json &jobs, int64_t time, const Json *active = nullptr,const Json *scope = nullptr,const std::string& preferred = {}) {
  std::string best;
  int64_t played = -1, added = INT64_MAX;
  const bool identifying=active&&std::any_of(active->begin(),active->end(),[](const auto& row){return row.value("identityPending",false);});
  const auto included=[&](const std::string& key){return !scope||std::find(scope->begin(),scope->end(),key)!=scope->end();};
  if(!preferred.empty()&&included(preferred)&&jobs.contains(preferred)&&runnable(jobs[preferred],time)&&(!active||!active->contains(preferred)))return preferred;
  // One recency order for native Steam, added shortcuts and confirmed games.
  // Source provenance remains available for identity checks, never admission.
  for (auto it=jobs.begin();it!=jobs.end();++it) {
    const auto& j=it.value();
    if(!included(it.key())||(active&&active->contains(it.key()))||!runnable(j,time)||(identifying&&j.value("identityPending",false)))continue;
    const auto recent=j.value("lastPlayed",int64_t{0}),created=j.value("addedAt",int64_t{0});
    if(best.empty()||recent>played||(recent==played&&(created<added||(created==added&&it.key()<best)))){best=it.key();played=recent;added=created;}
  }
  return best;
}
inline std::string nextQueueKey(const Json& j,int64_t time,const Json* active=nullptr){
 return nextKey(j["jobs"],time,active,scopedManual(j)?&j["manualTargets"]:nullptr,manualRun(j)?stringField(j,"manualPriorityKey"):std::string{});
}
inline bool manualTargetsComplete(const Json& j){
 if(!scopedManual(j))return false;
 for(const auto& key:j["manualTargets"]){const auto job=j["jobs"].value(key.get<std::string>(),Json::object());if(inactiveExcludedQueueTool(j,key.get<std::string>(),job))continue;const auto state=stringField(job,"state");if(state!="done"&&state!="unavailable")return false;}
 return true;
}
inline bool manualWorkComplete(const Json& j){
 if(!manualRun(j))return false;
 if(scopedManual(j))return manualTargetsComplete(j);
 if(j.value("catalogPending",false))return false;
 for(auto it=j["jobs"].begin();it!=j["jobs"].end();++it){if(inactiveExcludedQueueTool(j,it.key(),it.value()))continue;const auto state=stringField(it.value(),"state");if(state!="done"&&state!="unavailable")return false;}
 return true;
}
inline const char *expiry(uint64_t time, uint64_t started, uint64_t activity,
                          uint64_t taskStarted, Limits limits = {}) {
  if (time - started >= limits.session)
    return "session-timeout";
  if (taskStarted && time - taskStarted >= limits.task)
    return "task-timeout";
  if (time - activity >= limits.stall)
    return "worker-stalled";
  return "";
}
enum class TimeoutStage { unreported, metadata, transfer, assembly, validation, publication, supervisor };
inline bool workerTaskTimeout(std::string_view reason) {
  return reason=="media-task-time-budget" || reason=="media-task-timeout";
}
inline const char* timeoutStageName(TimeoutStage stage) {
  switch (stage) {
  case TimeoutStage::metadata: return "metadata";
  case TimeoutStage::transfer: return "transfer";
  case TimeoutStage::assembly: return "assembly";
  case TimeoutStage::validation: return "validation";
  case TimeoutStage::publication: return "publication";
  default: return nullptr;
  }
}
// These files are independent checkpoints. Never turn a failed read into
// evidence that work completed or a failed replacement into a saved request.
namespace checkpoint {
// Keep the existing error text for queue consumers; resume owners also need
// to distinguish a failed IO observation from readable malformed bytes.
struct ReadUnavailable : std::runtime_error {explicit ReadUnavailable(const char* reason):std::runtime_error(reason){} };
struct UpgradeRequired : std::runtime_error { UpgradeRequired():std::runtime_error("media-download-checkpoint-upgrade-required"){} };
inline Json read(const fs::path& path,size_t limit=16*1024*1024,int version=1,const char* field="schemaVersion") {
  const auto disk=custom_steam_library::ioPath(path);
  std::error_code ec;const auto status=fs::symlink_status(disk,ec);
  if(status.type()==fs::file_type::not_found||ec==std::errc::no_such_file_or_directory)return Json::object();
  if(ec)throw ReadUnavailable("media-cache-file-unsafe");
  if(!fs::is_regular_file(status)||fs::is_symlink(status))throw std::runtime_error("media-cache-file-unsafe");
  const auto size=fs::file_size(disk,ec);if(ec)throw ReadUnavailable("media-checkpoint-size-invalid");
  if(size>limit)throw std::runtime_error("media-checkpoint-size-invalid");
  std::ifstream in(disk,std::ios::binary);if(!in)throw ReadUnavailable("media-checkpoint-unreadable");
  std::string text(static_cast<size_t>(size),'\0');in.read(text.data(),text.size());
  if(!in||in.peek()!=std::char_traits<char>::eof()||in.bad())throw ReadUnavailable("media-checkpoint-unreadable");
  // JSON text cannot contain literal NUL bytes. The vendored lexer treats
  // NUL as EOF, which could otherwise hide trailing checkpoint contents.
  if(text.find('\0')!=std::string::npos)throw std::runtime_error("media-checkpoint-content-invalid");
  auto j=Json::parse(text,[](int depth,Json::parse_event_t,Json&){if(depth>64)throw std::runtime_error("media-checkpoint-depth-invalid");return true;});
  if(!j.is_object())throw std::runtime_error("media-checkpoint-root-invalid");
  if(j.contains(field)) {
    const auto& v=j[field];if(!v.is_number_integer()||(v.is_number_integer()&&!v.is_number_unsigned()&&v.get<int64_t>()<0))throw std::runtime_error("media-checkpoint-version-invalid");
    if(v.get<uint64_t>()>static_cast<uint64_t>(version))throw UpgradeRequired();
  }
  return j;
}
inline void preserve(const fs::path& path) {
  const auto disk=custom_steam_library::ioPath(path);
  std::error_code ec;if(!fs::exists(disk,ec)&&!ec)return;
  if(ec||!fs::is_regular_file(fs::symlink_status(disk,ec))||ec||fs::is_symlink(fs::symlink_status(disk,ec)))throw std::runtime_error("media-cache-file-unsafe");
  auto backup=path;backup+=L".corrupt."+wide(randomId());
  if(!CopyFileW(disk.c_str(),custom_steam_library::ioPath(backup).c_str(),TRUE))throw std::runtime_error("media-cache-checkpoint-preserve-failed");
}
inline void write(const fs::path& path,const Json& j) {
  fs::create_directories(custom_steam_library::ioPath(path.parent_path()));auto tmp=path;tmp+=L".tmp."+wide(randomId());
  const auto temporary=custom_steam_library::ioPath(tmp),disk=custom_steam_library::ioPath(path);
  HANDLE h=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
  if(h==INVALID_HANDLE_VALUE)throw std::runtime_error("media-cache-checkpoint-write-failed");
  try {
    const auto text=j.dump(2);DWORD count=0;
    if(text.size()>MAXDWORD||!WriteFile(h,text.data(),static_cast<DWORD>(text.size()),&count,nullptr)||count!=text.size()||!FlushFileBuffers(h))throw std::runtime_error("media-cache-checkpoint-write-failed");
    CloseHandle(h);h=INVALID_HANDLE_VALUE;
    if(!MoveFileExW(temporary.c_str(),disk.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("media-cache-checkpoint-replace-failed");
  } catch(...) {if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);std::error_code ec;fs::remove(temporary,ec);throw;}
}
inline void fields(const Json& j,bool row=false) {
  if(!j.is_object())throw std::runtime_error("media-queue-fields-invalid");
  const auto require=[&](const char* name,const std::function<bool(const Json&)>& valid){auto it=j.find(name);if(it!=j.end()&&!valid(*it))throw std::runtime_error(std::string("media-queue-field-invalid:")+name);};
  for(const auto* name:{"enabled","wallpaperPauseOnBattery","sessionClaimed","resourceChecked","catalogPending","identityPending","verified","retiredPopular","refreshRequested","recoveryRequired","recoveryUnconfirmed"})require(name,[](const Json& v){return v.is_boolean();});
  if(row)require("autoStopped",[](const Json& v){return v.is_boolean();});
  for(const auto* name:{"sharedSleepCancelledSession","wallpaperPowerCancelledSession","session","phase","reason","pauseReason","resourcePauseReason","workerBirth","ownerProtocolSession","launchTarget","inventoryReadyId","inventoryRequestId","manualRequestId","manualSession","manualSessionRequest","manualState","manualPriorityKey","catalogStatus","catalogError","queuePolicy","pendingReason","key","name","state","lastError","stopReason","identityPrerequisiteStamp","identityRecoveryReason","catalogSource","localCatalogSource","executable","steamAccount","shortcutAppId","recoveryReason"})require(name,[](const Json& v){return v.is_string();});
  for(const auto* name:{"sessionIssuedAt","sessionDeadlineAt","statusRevision","queueRevision","updatedAt","workerPid","ownerProtocol","maxParallel","parallelSlots","cpuThreshold","startupProtectionUntil","heartbeatAt","taskStartedAt","manualUntil","inventoryAwaitUntil","inventoryRequestedAt","cooldownUntil","catalogRetryAt","progressNotifiedAt","bytes","totalBytes","resumedBytes","sessionResumedBytes","activeCount","appId","addedAt","lastPlayed","attempts","automaticElapsedMs","retryAt","requestBindingRevision","provenanceRevision","admissionRevision","qualityUpgradeRevision","videoQualityPolicy","sourceTier","localSourceTier","chartRank","attemptBudgetMs","total","completed","remaining","failed","unavailable","manualTotal","manualCompleted","nextDueAt"})require(name,[&](const Json& v){return v.is_number_integer()&&(!v.is_number_unsigned()||v.get<uint64_t>()<=INT64_MAX)&&(v.get<int64_t>()>=0||(std::string_view(name)=="nextDueAt"&&v.get<int64_t>()==-1));});
  for(const auto* name:{"attempts","maxParallel","parallelSlots","cpuThreshold","ownerProtocol","videoQualityPolicy","sourceTier","localSourceTier","chartRank"})require(name,[](const Json& v){return v.get<int64_t>()<=INT_MAX;});
  require("workerPid",[](const Json& v){return v.get<uint64_t>()<=MAXDWORD;});
  require("appId",[](const Json& v){return v.get<uint64_t>()<=2147483647;});
  for(const auto* name:{"sharedSleepSession","sharedSleepWindow","jobs","active","current","localShortcut","selectedVideo","manualTargetRevisions","sourceCounts"})require(name,[](const Json& v){return v.is_object();});
  for(const auto* name:{"history","admissionHistory","manualTargets","recoveryBlockedKeys"})require(name,[](const Json& v){return v.is_array();});
  for(const auto* name:{"manualTargets","recoveryBlockedKeys"})if(j.contains(name))for(const auto& key:j[name])if(!key.is_string())throw std::runtime_error("media-queue-scope-invalid");
  if(j.value("workerPid",DWORD{0})) {
    const auto birth=stringField(j,"workerBirth");if(birth.empty()||birth.size()>20||birth.find_first_not_of("0123456789")!=std::string::npos||birth.find_first_not_of('0')==std::string::npos||stringField(j,"session").empty())throw std::runtime_error("media-queue-owner-unconfirmed");
  }
  if(!row&&j.contains("autoStopped")&&(!j["autoStopped"].is_number_integer()||j["autoStopped"].get<int64_t>()<0))throw std::runtime_error("media-queue-count-invalid");
  for(const auto* name:{"history","admissionHistory"})if(j.contains(name))for(const auto& entry:j[name])fields(entry);
  if(j.contains("localShortcut"))fields(j["localShortcut"],true);
  if(j.contains("selectedVideo")) {
    const auto& v=j["selectedVideo"];fields(v,true);
    for(const auto* key:{"url"})if(v.contains(key)&&!v[key].is_string())throw std::runtime_error("media-queue-video-invalid");
    for(const auto* key:{"preferredHeight","selectedHeight"})if(v.contains(key)&&(!v[key].is_number_integer()||v[key].get<int64_t>()<0||v[key].get<uint64_t>()>INT_MAX))throw std::runtime_error("media-queue-video-invalid");
  }
}
inline Json queue(const fs::path& path,bool status=false) {
  auto j=read(path);fields(j);
  for(const auto* name:{"jobs","active"})if(j.contains(name)) {
    if(j[name].size()>10000)throw std::runtime_error("media-queue-count-invalid");
    for(auto it=j[name].begin();it!=j[name].end();++it) {
      fields(it.value(),true);
      if(!safeKey(it.key())||targetKey(it.value())!=it.key()||(!it.value().value("verified",false)&&!it.value().value("identityPending",false)))throw std::runtime_error("media-queue-identity-unconfirmed");
    }
  }
  if(j.contains("current"))fields(j["current"],true);
  return j;
}
inline void guardQueue(const fs::path& path){try{queue(path);}catch(const UpgradeRequired&){throw;}catch(...){preserve(path);}}
} // namespace checkpoint

// Retry eligibility is based on a repaired file environment, never on an
// application version, wall-clock expiry or a guessed Steam identity.
inline std::string identityFileStamp(const fs::path& path) {
  HANDLE file=CreateFileW(path.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
      nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
  if(file==INVALID_HANDLE_VALUE)return "unavailable";
  BY_HANDLE_FILE_INFORMATION info{};
  const bool known=GetFileInformationByHandle(file,&info)&&GetFileType(file)==FILE_TYPE_DISK&&
      !(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT));
  CloseHandle(file);if(!known)return "unavailable";
  return normalized(utf8(path.wstring()))+":"+std::to_string(info.dwVolumeSerialNumber)+":"+
      std::to_string(info.nFileIndexHigh)+":"+std::to_string(info.nFileIndexLow)+":"+
      std::to_string(info.ftCreationTime.dwHighDateTime)+":"+std::to_string(info.ftCreationTime.dwLowDateTime)+":"+
      std::to_string(info.ftLastWriteTime.dwHighDateTime)+":"+std::to_string(info.ftLastWriteTime.dwLowDateTime)+":"+
      std::to_string(info.nFileSizeHigh)+":"+std::to_string(info.nFileSizeLow);
}
struct IdentityPrerequisites {
  bool ready=false;
  std::string stamp;
};
inline IdentityPrerequisites identityPrerequisites(const fs::path& helper,const std::string& executable) {
  const auto tool=identityFileStamp(helper),target=identityFileStamp(fs::path(wide(executable)));
  // This worker can now consume the helper's transient-network receipt. The
  // existing one-shot prerequisite recovery must also recognize that repaired
  // behavior when an older worker already exhausted an unconfirmed identity.
  return {tool!="unavailable"&&target!="unavailable",digestKey("identity-worker-manifest-v2\n"+tool+"\n"+target)};
}
inline bool recoverableIdentityFailure(const std::string& reason) {
  return reason=="media-identity-worker-unavailable"||reason=="media-identity-helper-unavailable"||
      reason=="media-identity-target-unavailable"||reason=="media-identity-launch-failed"||
      reason=="media-identity-timeout"||reason=="media-identity-unconfirmed";
}
class Queue {
  fs::path root_;
  static Json normalizedDocument(Json j, bool *invalidatesStatus = nullptr) {
    if (invalidatesStatus) *invalidatesStatus = false;
    if(j.contains("wallpaperPauseOnBattery")&&j["wallpaperPauseOnBattery"].is_boolean())powerPolicy::consume(j["wallpaperPauseOnBattery"].get<bool>());
    const auto changed = [&] { if (invalidatesStatus) *invalidatesStatus = true; };
    if (!j.contains("jobs")) { j["jobs"] = Json::object(); changed(); }
    if (!j.contains("enabled")) j["enabled"] = true;
    // Eligibility changes at this deadline even when no writer publishes a
    // new queue revision. Expired manual scope cannot keep a cached due time.
    const auto manualUntil=j.value("manualUntil",int64_t{0}),manualCheckedAt=now();
    if(manualUntil>0&&(manualUntil<=manualCheckedAt||manualUntil-manualCheckedAt>int64_t(sessionMs))){j["manualUntil"]=0;j["manualState"]="expired";changed();}
    if (!j.contains("active") || !j["active"].is_object()) { j["active"] = Json::object(); changed(); }
    if (invalidatesStatus && (j.value("maxParallel", maximumParallel) != maximumParallel ||
        stringField(j,"queuePolicy","recent-only-v1") != "recent-only-v1" ||
        j.value("catalogPending",false) || stringField(j,"catalogStatus","disabled") != "disabled" ||
        j.value("catalogRetryAt",int64_t{0}) != 0 || !stringField(j,"catalogError").empty())) changed();
    j["maxParallel"] = maximumParallel;
    j["queuePolicy"] = "recent-only-v1";
    j["catalogPending"]=false;j["catalogStatus"]="disabled";j["catalogRetryAt"]=0;j["catalogError"]="";
    // Remove only queue work introduced solely by the retired popular feed.
    // Existing media files and real local/shortcut provenance stay intact.
    for(auto it=j["jobs"].begin();it!=j["jobs"].end();) {
      auto& job=it.value();
      if(stringField(job,"catalogSource")=="popular") {
        const bool local=job.value("localSourceTier",0)==1||!stringField(job,"executable").empty()||!stringField(job.value("localShortcut",Json::object()),"executable").empty()||job.value("requestBindingRevision",uint64_t{0})>0;
        if(local){changed();job["sourceTier"]=job.value("localSourceTier",2);job["catalogSource"]=stringField(job,"localCatalogSource",!stringField(job,"executable").empty()?"ymcc-confirmed":"manual");if(stringField(job,"catalogSource")=="steam-shortcut"&&job.contains("localShortcut"))for(const auto* field:{"executable","steamAccount","shortcutAppId"})job[field]=job["localShortcut"][field];job.erase("chartRank");job.erase("retiredPopular");}
        else if(!j["active"].contains(it.key())){changed();it=j["jobs"].erase(it);continue;}
        else { if(!job.value("retiredPopular",false))changed();job["retiredPopular"]=true; }
      }
      if(job.value("state",std::string{})=="unavailable"||job.value("attempts",0)>=automaticAttemptLimit||job.value("automaticElapsedMs",int64_t{0})>=automaticGameMs){
        if(invalidatesStatus&&!(*invalidatesStatus)&&(!job.value("autoStopped",false)||job.value("state",std::string{})!="unavailable"||job.value("retryAt",int64_t{0})!=0||!job.contains("stopReason")))changed();
        job["autoStopped"]=true;job["state"]="unavailable";job["retryAt"]=0;if(!job.contains("stopReason"))job["stopReason"]=stringField(job,"lastError","automatic-budget-exhausted");}
      ++it;
    }
    const auto session=j.value("session",std::string{});
    for(auto it=j["active"].begin();it!=j["active"].end();) {
      if(stringField(it.value(),"session")!=session) {
        changed();
        if(j["jobs"].contains(it.key())&&j["jobs"][it.key()].value("state",std::string{})=="running")j["jobs"][it.key()]["state"]="pending";
        it=j["active"].erase(it);
      } else ++it;
    }
    return j;
  }
  // Bounded transition evidence; byte progress and no-op checks add nothing.
  // Ordinary exports already include this queue and its progress snapshot.
  static void recordAdmission(Json& j) {
    Json entry={{"enabled",j.value("enabled",true)},
                {"pauseReason",j.value("pauseReason",std::string{})},
                {"resourcePauseReason",j.value("resourcePauseReason",std::string{})},
                {"startupProtectionUntil",j.value("startupProtectionUntil",int64_t{0})},
                {"cpuThreshold",j.value("cpuThreshold",15)}};
    auto& history=j["admissionHistory"];
    if(!history.is_array())history=Json::array();
    if(!history.empty()){auto last=history.back();last.erase("time");if(last==entry)return;}
    entry["time"]=now();history.push_back(std::move(entry));
    while(history.size()>12)history.erase(history.begin());
  }
  Json storedQueue(bool* readable=nullptr) const {
    if(readable)*readable=false;
    const auto path=root_/L"config"/L"game-media-downloads.json";
    try{auto state=checkpoint::queue(path);if(readable)*readable=true;return state;}
    catch(const checkpoint::UpgradeRequired&){throw;}
    catch(const std::exception& e){
      Json safe={{"enabled",false},{"recoveryRequired",true},{"recoveryReason",e.what()},{"phase","deferred"},{"reason","queue-recovery-required"},{"jobs",Json::object()},{"recoveryBlockedKeys",Json::array()}};
      // Recover only independently valid rows. They remain ineligible until a
      // real inventory confirms them; retry counters/deadlines are not reset.
      try {
        const auto raw=checkpoint::read(path);
        for(auto it=raw.begin();it!=raw.end();++it) {
          const auto& key=it.key();
          if(key=="jobs"||key=="active"||key=="current"||key=="enabled"||key=="workerPid"||key=="workerBirth"||key=="session"||key=="ownerProtocolSession"||key=="sessionClaimed"||key=="phase"||key=="reason"||key=="manualUntil"||key=="manualTargets"||key=="manualTargetRevisions"||key=="recoveryRequired"||key=="recoveryReason"||key=="recoveryBlockedKeys")continue;
          try{checkpoint::fields(Json{{key,it.value()}});safe[key]=it.value();}catch(...){}
        }
        if(raw.contains("jobs")&&raw["jobs"].is_object())for(auto it=raw["jobs"].begin();it!=raw["jobs"].end();++it) {
          bool valid=false;
          try{checkpoint::fields(it.value(),true);valid=targetKey(it.value())==it.key()&&(it.value().value("verified",false)||it.value().value("identityPending",false));}catch(...){}
          if(valid) {auto job=it.value();job["recoveryUnconfirmed"]=true;if(stringField(job,"state")=="running"||stringField(job,"state")=="done")job["state"]="pending";safe["jobs"][it.key()]=std::move(job);}
          else safe["recoveryBlockedKeys"].push_back(it.key());
        }
      }catch(const checkpoint::UpgradeRequired&){throw;}catch(...){}
      return safe;
    }
  }
  Json load(bool *invalidatesStatus = nullptr,bool* readable=nullptr) const {return normalizedDocument(storedQueue(readable),invalidatesStatus);}
  Json readStatus() const {
    try{return checkpoint::queue(statusPath(),true);}catch(const checkpoint::UpgradeRequired&){throw;}catch(...){return Json::object();}
  }
  static bool statusMatches(const Json& status,const Json& durable) {
    if(status.empty()||stringField(status,"session")!=stringField(durable,"session")||
      status.value("statusRevision",uint64_t{0})<durable.value("statusRevision",uint64_t{0})||
      (status.contains("queueRevision")&&status["queueRevision"]!=durable.value("statusRevision",uint64_t{0})))return false;
    for(const auto* key:{"enabled","pauseReason","resourcePauseReason","cpuThreshold","startupProtectionUntil","workerPid","workerBirth","manualUntil","ownerProtocolSession"})
      if(status.value(key,Json{})!=durable.value(key,Json{}))return false;
    const auto active=durable.value("active",Json::object()),progress=status.value("active",Json::object());
    if(active.size()!=progress.size())return false;
    for(auto it=active.begin();it!=active.end();++it)
      if(!progress.contains(it.key())||progress[it.key()].value("taskStartedAt",int64_t{0})!=it.value().value("taskStartedAt",int64_t{0}))return false;
    return true;
  }
  static bool currentSummaryPolicy(const Json& status) {
    return status.value("summaryPolicyVersion",Json{})==Json(1);
  }
  Json liveStatus(const Json& durable, bool invalidated = false) const {
    const auto status=readStatus();
    if(!statusMatches(status,durable))return summary(durable);
    if(!invalidated&&currentSummaryPolicy(status))return status;
    // Normalization or summary policy changes scheduling, not owned progress. Rebuild
    // counts/deadlines from the current jobs while retaining only live fields.
    auto current=durable;
    for(const auto* field:{"active","current","bytes","totalBytes","resumedBytes",
                          "sessionResumedBytes","heartbeatAt","taskStartedAt",
                          "statusRevision","updatedAt","progressNotifiedAt","queueRevision"})
      if(status.contains(field))current[field]=status[field];
    return summary(std::move(current));
  }
  Json liveStatus() const {
    bool invalidated=false;const auto durable=load(&invalidated);
    return liveStatus(durable,invalidated);
  }
  void writeStatus(const Json& j) const {checkpoint::guardQueue(statusPath());checkpoint::write(statusPath(),j);}

  fs::path statusPath() const {
    return root_ / L"config" / L"game-media-download-progress.json";
  }
  static void activeSummary(Json &j) {
    if (!j.contains("active") || !j["active"].is_object()) return;
    auto &active = j["active"];
    j["activeCount"] = active.size();
    j["resumedBytes"]=j.value("sessionResumedBytes",uint64_t{0});
    if (active.empty()) return;
    uint64_t bytes = 0, total = 0, resumed = 0; bool known = true;
    int64_t oldest = INT64_MAX, heartbeat = j.value("heartbeatAt",int64_t{0});
    std::string phase = "resolving";
    for (const auto &item : active) {
      bytes += item.value("bytes",uint64_t{0});
      total += item.value("totalBytes",uint64_t{0});
      resumed += item.value("resumedBytes",uint64_t{0});
      known = known && item.value("totalBytes",uint64_t{0}) > 0;
      oldest = (std::min)(oldest,item.value("taskStartedAt",INT64_MAX));
      heartbeat = (std::max)(heartbeat,item.value("heartbeatAt",int64_t{0}));
      if (item.value("phase",std::string{}) == "downloading") phase = "downloading";
      else if (phase != "downloading" && item.value("phase",std::string{}) == "publishing") phase = "publishing";
    }
    const auto key = j.value("current",Json::object()).value("key",std::string{});
    j["current"] = active.contains(key) ? active[key] : active.begin().value();
    j["bytes"] = bytes; j["totalBytes"] = known ? total : 0;j["resumedBytes"]=j.value("sessionResumedBytes",resumed);
    j["taskStartedAt"] = oldest == INT64_MAX ? 0 : oldest;
    j["heartbeatAt"] = heartbeat;
    if (downloadsEnabled(j) && !policyBlocked(j) &&
        j.value("resourcePauseReason",std::string{}).empty() &&
        j.value("phase",std::string{}) != "paused") j["phase"] = phase;
  }
  static bool reclaimExited(Json &j) {
    const auto pid = j.value("workerPid",DWORD{0});
    const bool running=std::any_of(j["jobs"].begin(),j["jobs"].end(),[](const Json& row){return row.value("state",std::string{})=="running";});
    if(j.value("active",Json::object()).empty()&&!running&&!pid)return false;
    if (pid && !leaseOwnerGone({{"leasePid",pid},{"leaseBirth",j.value("workerBirth",std::string{})}})) return false;
    for (auto &job:j["jobs"]) if (job.value("state",std::string{}) == "running") job["state"]="pending";
    j["active"]=Json::object();j["activeCount"]=0;j["current"]=Json::object();
    j["taskStartedAt"]=0;j["workerPid"]=0;j["workerBirth"]="";return true;
  }
  static Json summary(Json j) {
    j["summaryPolicyVersion"]=1;
    size_t done = 0, waiting = 0, failed = 0, unavailable = 0, stopped = 0, excluded = 0;
    std::array<size_t,5> sources{};size_t identifying=0;std::string pendingReason;
    int64_t nextDue = -1;
    for (auto item=j["jobs"].begin();item!=j["jobs"].end();++item) {
      const auto& job=item.value();
      if(inactiveExcludedQueueTool(j,item.key(),job)){++excluded;continue;}
      if(job.value("autoStopped",false))++stopped;
      ++sources[sourcePriority(job)];if(job.value("identityPending",false))++identifying;
      if (manualTarget(j,item.key()) && !itemStopped(job) && job.value("state", std::string{}) != "done" &&
          job.value("state", std::string{}) != "running") {
        const auto due =
            (std::max)(int64_t{0}, job.value("retryAt", int64_t{0}));
        if (nextDue < 0 || due < nextDue)
          nextDue = due;
      }
      if (job.value("state", std::string{}) == "done" ||
          job.value("state", std::string{}) == "unavailable") {
        ++done;
        if (job.value("state", std::string{}) == "unavailable")
          ++unavailable;
      } else {
        ++waiting;if(pendingReason.empty())pendingReason=stringField(job,"lastError");
        if (job.value("attempts", 0) > 0)
          ++failed;
      }
    }
    activeSummary(j);
    if(!scopedManual(j)&&j.value("catalogPending",false)) {
      const auto catalogDue=j.value("catalogRetryAt",int64_t{0});
      if(nextDue<0||catalogDue<nextDue)nextDue=catalogDue;
    }
    if(nextDue>=0) nextDue=(std::max)(nextDue,j.value("cooldownUntil",int64_t{0}));
    size_t manualTotal=0,manualCompleted=0;
    if(j.contains("manualTargets")&&j["manualTargets"].is_array())for(const auto& value:j["manualTargets"]){const auto job=j["jobs"].value(value.get<std::string>(),Json::object());if(inactiveExcludedQueueTool(j,value.get<std::string>(),job))continue;++manualTotal;const auto state=stringField(job,"state");if(state=="done"||state=="unavailable")++manualCompleted;}
    Json manualItems=Json::object();
    if(j.contains("manualTargets")&&j["manualTargets"].is_array())for(const auto& key:j["manualTargets"]){
      if(manualItems.size()>=24)break;const auto job=j["jobs"].value(key.get<std::string>(),Json::object());Json item=Json::object();
      for(const auto* field:{"state","lastError","retryAt","requestBindingRevision","autoStopped","stopReason"})if(job.contains(field))item[field]=job[field];
      manualItems[key.get<std::string>()]=std::move(item);
    }
    j["manualItems"]=std::move(manualItems);
    j["manualTotal"]=manualTotal;j["manualCompleted"]=manualCompleted;
    j["nextDueAt"] = nextDue;
    j["completed"] = done;
    j["remaining"] = waiting;
    j["failed"] = failed;j["pendingReason"]=pendingReason;
    j["unavailable"] = unavailable;j["autoStopped"] = stopped;
    j["total"] = j["jobs"].size()-excluded;
    j["sourceCounts"]={{"popular",sources[0]},{"steamNative",sources[1]},{"steamShortcuts",sources[2]},{"ymccConfirmed",sources[3]},{"other",sources[4]},{"identifying",identifying}};
    j.erase("jobs");
    return j;
  }
  void save(Json &j, bool wakeOwner = true,
            const Json *validatedPrevious = nullptr) const {
    // The optional status was validated under this same root lock. Resource
    // publication does not yield or invoke callbacks before this save; guards
    // still validate both files immediately before their atomic replacements.
    auto previous = validatedPrevious ? *validatedPrevious : readStatus();
    if(!validatedPrevious&&!statusMatches(previous,load()))previous=Json::object();
    if (!previous.empty() &&
        previous.value("session", std::string{}) ==
            j.value("session", std::string{}) &&
        previous.value("current", Json::object()) ==
            j.value("current", Json::object()) &&
        previous.value("taskStartedAt", int64_t{0}) ==
            j.value("taskStartedAt", int64_t{0}) &&
        previous.value("heartbeatAt", int64_t{0}) >
            j.value("heartbeatAt", int64_t{0})) {
      for (const auto *field : {"bytes", "totalBytes", "heartbeatAt"})
        if (previous.contains(field))
          j[field] = previous[field];
      const auto active = [](const std::string &phase) {
        return phase == "starting" || phase == "resolving" ||
               phase == "downloading" || phase == "publishing";
      };
      if (downloadsEnabled(j) && !policyBlocked(j) &&
          j.value("resourcePauseReason", std::string{}).empty() &&
          active(j.value("phase", std::string{})) &&
          active(previous.value("phase", std::string{})))
        j["phase"] = previous["phase"];
    }
    if (previous.value("session",std::string{}) == j.value("session",std::string{}) &&
        j.contains("active") && previous.contains("active")) {
      for (auto item=j["active"].begin();item!=j["active"].end();++item) {
        const auto old = previous["active"].value(item.key(),Json::object());
        if (old.value("taskStartedAt",int64_t{-1}) == item.value().value("taskStartedAt",int64_t{0}) &&
            old.value("heartbeatAt",int64_t{0}) > item.value().value("heartbeatAt",int64_t{0}))
          for (const auto *field:{"bytes","totalBytes","heartbeatAt","phase","resumedBytes"})
            if (old.contains(field)) item.value()[field]=old[field];
      }
    }
    if(previous.value("session",std::string{})==j.value("session",std::string{}))
      j["sessionResumedBytes"]=(std::max)(j.value("sessionResumedBytes",uint64_t{0}),previous.value("sessionResumedBytes",uint64_t{0}));
    activeSummary(j);
    j["statusRevision"] = (std::max)(previous.value("statusRevision",uint64_t{0}),j.value("statusRevision",uint64_t{0})) + 1;
    j["updatedAt"] = now();
    j["schemaVersion"] = 1;
    const auto path=root_/L"config"/L"game-media-downloads.json";
    checkpoint::guardQueue(path);checkpoint::guardQueue(statusPath());
    checkpoint::write(path,j);
    auto status=summary(j);status["queueRevision"]=j["statusRevision"];writeStatus(status);
    notify(root_, wakeOwner);
  }

public:
  explicit Queue(fs::path root) : root_(std::move(root)) {}
  const fs::path &root() const { return root_; }
  Json snapshot() const {
    try {
      Lock lock(root_);
      bool invalidated=false,readable=false;const auto durable=load(&invalidated,&readable);
      auto status=liveStatus(durable,invalidated);status["checkpointReadable"]=readable;return status;
    } catch (const checkpoint::UpgradeRequired&) {
      return {{"enabled",false},{"phase","deferred"},{"reason","media-download-checkpoint-upgrade-required"},{"workerPid",0},{"checkpointReadable",false}};
    } catch (...) {
      return {{"enabled",false},{"phase", "deferred"},
              {"reason", "queue-unreadable"},{"checkpointReadable",false},
              {"workerPid", 0}};
    }
  }
  bool progress(const std::string &token,const std::string &phase,uint64_t bytes,uint64_t total) {
    return progress(token,"",phase,bytes,total);
  }
  bool progress(const std::string &token,const std::string &key,const std::string &phase,
                uint64_t bytes,uint64_t total) {
    Lock lock(root_);auto j=liveStatus();
    if (!policyAllowed(j,token) || !j.value("resourcePauseReason",std::string{}).empty()) return false;
    const auto priorPhase=j.value("phase",std::string{});
    const auto selected=key.empty()?j.value("current",Json::object()).value("key",std::string{}):key;
    if (!key.empty() && !j.value("active",Json::object()).contains(key)) return false;
    if (j.value("active",Json::object()).contains(selected)) {
      auto &item=j["active"][selected];item["phase"]=phase;item["bytes"]=bytes;item["totalBytes"]=total;item["heartbeatAt"]=now();
      activeSummary(j);
    } else {j["phase"]=phase;j["bytes"]=bytes;j["totalBytes"]=total;}
    j["statusRevision"]=j.value("statusRevision",uint64_t{0})+1;
    j["heartbeatAt"]=now();j["updatedAt"]=now();
    const bool emit=priorPhase!=j.value("phase",std::string{}) || now()-j.value("progressNotifiedAt",int64_t{0})>=1000;
    if (emit) j["progressNotifiedAt"]=now();
    writeStatus(j);if (emit) notify(root_,false);return true;
  }
  void resumed(const std::string &token,const std::string &key,uint64_t bytes) {
    if(!bytes) return;
    Lock lock(root_);auto j=liveStatus();
    if(token.empty()||j.value("session",std::string{})!=token||!j.value("active",Json::object()).contains(key)) return;
    auto &item=j["active"][key];item["resumedBytes"]=bytes+item.value("resumedBytes",uint64_t{0});
    j["sessionResumedBytes"]=bytes+j.value("sessionResumedBytes",uint64_t{0});
    j["statusRevision"]=j.value("statusRevision",uint64_t{0})+1;activeSummary(j);writeStatus(j);notify(root_,false);
  }
  Json document() const {
    Lock lock(root_);
    return load();
  }
  bool recheck() {
    Lock lock(root_);const auto stored=storedQueue();auto j=normalizedDocument(stored);
    // Only a proved-exited PID/birth lease may be reclaimed. No live worker,
    // completed job, retry deadline or saved user setting is changed.
    const bool reclaimed=reclaimExited(j);
    const auto status=readStatus();
    if(!reclaimed&&j==stored&&statusMatches(status,j)&&currentSummaryPolicy(status))return false;
    save(j);return true;
  }
  bool configureWallpaperPower(bool enabled){
    const bool changed=powerPolicy::own(enabled);
    Lock lock(root_,200);auto j=load();
    if(j.value("wallpaperPauseOnBattery",false)==enabled)return changed;
    j["wallpaperPauseOnBattery"]=enabled;save(j);return true;
  }
  void configure(bool enabled, const std::string &pause,
                 int cpuThreshold = 15) {
    Lock lock(root_);
    const auto stored=storedQueue();
    auto j=normalizedDocument(stored);const auto status=readStatus();
    const bool migrated=j!=stored||!statusMatches(status,j)||!currentSummaryPolicy(status);
    const bool reclaimed = reclaimExited(j);
    if (!reclaimed && !migrated && j.value("enabled", true) == enabled &&
        j.value("pauseReason", std::string{}) == pause &&
        j.value("cpuThreshold", 15) == cpuThreshold)
      return;
    j["enabled"] = enabled;
    j["cpuThreshold"] = (std::clamp)(cpuThreshold, 5, 90);
    j["pauseReason"] = pause;
    recordAdmission(j);
    save(j);
  }
  bool resource(const std::string &reason, std::optional<double> cpu = {},
                int64_t startupUntil = 0, const std::string &requiredSession = {},
                const Json *observedAdmission = nullptr) {
    Lock lock(root_);
    bool invalidated=false;auto durable = load(&invalidated);
    if (observedAdmission && !sameResourceAdmission(durable, *observedAdmission))
      return false;
    // Resource observations made during work cannot reopen a handed-back
    // session or overwrite a later cancellation, OFF setting or session.
    if (!requiredSession.empty() &&
        (!policyAllowed(durable, requiredSession) || terminalReceipt(durable, requiredSession)))
      return false;
    auto previous = readStatus();
    if (!statusMatches(previous, durable)) previous = Json::object();
    const auto status = previous.empty() ? summary(durable) : previous;
    // A fallback summary can describe the right resource state while the
    // published progress file is still missing/stale after a partial save.
    if (!invalidated && !previous.empty() && currentSummaryPolicy(previous) && status.value("resourcePauseReason", std::string{}) == reason &&
        status.value("resourceChecked", false) &&
        status.value("startupProtectionUntil", int64_t{0}) == startupUntil)
      return true;
    auto &j = durable;
    j["resourceChecked"] = true;
    j["resourcePauseReason"] = reason;
    j["startupProtectionUntil"] = startupUntil;
    if (cpu)
      j["systemCpuPct"] = std::round(*cpu * 10) / 10;
    if (!reason.empty())
      j["phase"] = "paused";
    else if (j.value("phase", std::string{}) == "paused")
      j["phase"] = j["jobs"].empty() ? "idle" : "pending";
    recordAdmission(j);
    save(j, false, &previous);
    return true;
  }
  // Existing owner/inventory wakes only. No timer, hashing or game launch.
  size_t recoverIdentityPrerequisites(const fs::path& helper,
      const std::function<bool(const Json&)>& shortcutIsCurrent = {}, bool ownerMayCompleteBulk = false) {
    bool beforeReadable=false;
    const auto before=[&]{Lock lock(root_);return load(nullptr,&beforeReadable);}();
    if(!downloadsEnabled(before)||policyBlocked(before)||powerPolicy::blocked()||before.value("workerPid",DWORD{0})>0)return 0;
    const auto currentMembership=[&](const Json& job){
      if(!shortcutIsCurrent)return false;
      try{return shortcutIsCurrent(job);}catch(...){return false;}
    };
    Store store(root_);
    const auto exactIdentity=[&](const Json& job){
      if(job.value("identityPending",false)||!job.value("verified",false))return false;
      const auto id=job.value("appId",int64_t{0});
      if(id<=0||id>2147483647||identityFileStamp(fs::path(wide(stringField(job,"executable"))))=="unavailable")return false;
      const auto identity=pendingShortcut(job)?store.shortcutWallpaperIdentity(stringField(job,"executable")):store.identity(stringField(job,"executable"));
      return identity.value("verified",false)&&identity.value("appId",int64_t{0})==id;
    };
    const auto currentCachedAppId=[&](const Json& job)->int64_t{
      const auto exe=stringField(job,"executable");
      if(identityFileStamp(fs::path(wide(exe)))=="unavailable")return 0;
      const auto identity=pendingShortcut(job)?store.shortcutWallpaperIdentity(exe):store.identity(exe);const auto id=identity.value("appId",int64_t{0});
      return identity.value("verified",false)&&id>0&&id<=2147483647?id:0;
    };
    std::vector<std::pair<std::string,Json>> candidates;
    for(auto it=before["jobs"].begin();it!=before["jobs"].end();++it) {
      const auto& job=it.value();
      if(!pendingShortcut(job)||!itemStopped(job))continue;
      // Returning to the original account/shortcut is a real prerequisite
      // repair even when the helper and EXE themselves have not changed.
      // A removed/foreign/unreadable shortcut supplies no recovery authority.
      const bool membershipRestored=stringField(job,"lastError")=="media-shortcut-no-longer-current";
      const bool identityPending=job.value("identityPending",false);
      if(membershipRestored?!currentMembership(job):!identityPending||!recoverableIdentityFailure(stringField(job,"lastError")))continue;
      if(!identityPending){
        // A canonical download already has its identity. Restored membership
        // needs the same exact cached AppID, never an identity-helper launch.
        if(membershipRestored&&it.key()==targetKey(job)&&exactIdentity(job))
          candidates.emplace_back(it.key(),Json{{"before",job},{"membershipRestored",true},{"verifiedMembership",true}});
        continue;
      }
      // identifyShortcut already consumes a current verified cache before it
      // needs a helper. The same exact-file repair can revive this placeholder.
      const auto cachedAppId=currentCachedAppId(job);
      if(cachedAppId>0&&it.key()==targetKey(job)&&currentMembership(job)){
        candidates.emplace_back(it.key(),Json{{"before",job},{"membershipRestored",true},{"cachedAppId",cachedAppId}});
        continue;
      }
      const auto proof=identityPrerequisites(helper,stringField(job,"executable"));
      if(proof.ready&&(membershipRestored||proof.stamp!=stringField(job,"identityPrerequisiteStamp"))&&
         (membershipRestored||currentMembership(job)))
        candidates.emplace_back(it.key(),Json{{"before",job},{"stamp",proof.stamp},{"membershipRestored",true}});
    }
    const auto bulkRequest=ownerMayCompleteBulk?stringField(before,"manualRequestId"):std::string{};
    const auto acknowledgedEmptyBulk=[&](const Json& state){
      const auto inventory=stringField(state,"inventoryRequestId");
      return !bulkRequest.empty()&&stringField(state,"manualRequestId")==bulkRequest&&
             manualRun(state)&&!scopedManual(state)&&stringField(state,"manualState")=="requested"&&
             !inventory.empty()&&stringField(state,"inventoryReadyId")==inventory&&
             stringField(state,"phase")!="starting"&&state.value("workerPid",DWORD{0})==0&&
             state["active"].empty()&&!state.value("recoveryRequired",false)&&manualWorkComplete(state);
    };
    // A receipt that arrives during this pass has its own wake. Closing on
    // it now could clear manual admission before restored membership is read.
    const bool closeEmptyBulk=candidates.empty()&&beforeReadable&&acknowledgedEmptyBulk(before);
    if(candidates.empty()&&!closeEmptyBulk)return 0;
    Lock lock(root_);bool readable=false;auto j=load(nullptr,&readable);
    if(!downloadsEnabled(j)||policyBlocked(j)||powerPolicy::blocked()||j.value("workerPid",DWORD{0})>0)return 0;
    if(closeEmptyBulk&&readable&&acknowledgedEmptyBulk(j)){
      j["manualUntil"]=0;j["manualState"]="completed";
      save(j,false);
      return 0;
    }
    size_t recovered=0;
    for(const auto& [key,proof]:candidates) {
      if(!j["jobs"].contains(key)||j["jobs"][key]!=proof["before"]||j["active"].contains(key))continue;
      if(proof.value("membershipRestored",false)&&!currentMembership(j["jobs"][key]))continue;
      if(proof.value("verifiedMembership",false)&&!exactIdentity(j["jobs"][key]))continue;
      if(proof.contains("cachedAppId")&&currentCachedAppId(j["jobs"][key])!=proof.value("cachedAppId",int64_t{0}))continue;
      auto& job=j["jobs"][key];
      if(proof.contains("stamp")){
        const auto current=identityPrerequisites(helper,stringField(job,"executable"));
        if(!current.ready||current.stamp!=stringField(proof,"stamp"))continue;
      }
      job["identityRecoveryReason"]=stringField(job,"lastError");
      if(proof.contains("stamp"))job["identityPrerequisiteStamp"]=proof["stamp"];
      resetAutomaticBudget(job);job["lastError"]="";++recovered;
    }
    if(recovered)save(j);
    return recovered;
  }
  void requestSupplement(bool manual=false,const std::string& onlyKey={}){
    {Lock lock(root_);auto j=load();
      if(!onlyKey.empty()&&(!manual||!j["jobs"].contains(onlyKey)||excludedQueueTool(j["jobs"][onlyKey])))throw std::runtime_error("media-download-target-unavailable");
      if(manual){
        if(manualRun(j)){
          if(onlyKey.empty())return;
          if(scopedManual(j)&&std::find(j["manualTargets"].begin(),j["manualTargets"].end(),onlyKey)==j["manualTargets"].end()){
            if(j["manualTargets"].size()>=24)throw std::runtime_error("media-download-selection-limit");j["manualTargets"].push_back(onlyKey);
          }
          const auto revision=j["jobs"][onlyKey].value("requestBindingRevision",uint64_t{0});
          if(!j.contains("manualTargetRevisions")||!j["manualTargetRevisions"].is_object())j["manualTargetRevisions"]=Json::object();
          if(j["manualTargetRevisions"].contains(onlyKey)&&j["manualTargetRevisions"][onlyKey]==revision)return;
          j["manualTargetRevisions"][onlyKey]=revision;j["manualRequestId"]=randomId();
          j["manualPriorityKey"]=onlyKey;resetAutomaticBudget(j["jobs"][onlyKey]);save(j);return;
        }
        j["manualUntil"]=now()+sessionMs;j["manualRequestId"]=randomId();j["manualState"]="requested";j["reason"]="";
        j.erase("manualTargets");j.erase("manualPriorityKey");j.erase("manualTargetRevisions");
        if(j.value("workerPid",DWORD{0})>0){j["manualSession"]=j.value("session",std::string{});j["manualSessionRequest"]=j["manualRequestId"];}
        if(!onlyKey.empty()){
          j["manualTargets"]=Json::array({onlyKey});j["manualTargetRevisions"]={{onlyKey,j["jobs"][onlyKey].value("requestBindingRevision",uint64_t{0})}};j["manualPriorityKey"]=onlyKey;j["inventoryAwaitUntil"]=0;resetAutomaticBudget(j["jobs"][onlyKey]);save(j);return;
        }
        j["inventoryAwaitUntil"]=now()+10000;
        if(j.value("catalogPending",false))j["catalogRetryAt"]=0;
        // One explicit bulk retry; scoped choices never reset unrelated backoff.
        for(auto& job:j["jobs"]){
          const auto state=stringField(job,"state");
          if(excludedQueueTool(job)||(state!="pending"&&state!="unavailable"))continue;
          // A bulk retry cannot repair a shortcut that now points to another EXE.
          // The existing wake can recover it once the exact membership returns.
          if(state=="unavailable"&&job.value("identityPending",false)&&pendingShortcut(job)&&
             stringField(job,"lastError")=="media-shortcut-no-longer-current"){
            const auto observed=job.find("lastShortcutObservation");
            if(observed!=job.end()&&observed->is_object()&&stringField(*observed,"reason")=="shortcut-target-changed"&&
               stringField(*observed,"expectedAccount")==stringField(job,"steamAccount")&&
               observed->contains("provenanceRevision")&&(*observed)["provenanceRevision"].is_number_integer()&&
               (*observed)["provenanceRevision"]==job.value("provenanceRevision",uint64_t{0}))continue;
          }
          job["state"]="pending";resetAutomaticBudget(job);
        }
      }

      j["inventoryRequestId"]=randomId();j["inventoryRequestedAt"]=now();save(j);
    }
    signalInventoryChanged(root_);
  }
  void inventoryReady(const std::string& id){if(id.empty())return;Lock lock(root_);auto j=load();if(j.value("inventoryRequestId",std::string{})!=id||j.value("inventoryReadyId",std::string{})==id)return;j["inventoryReadyId"]=id;save(j);}
  void cancelManual(){Lock lock(root_);auto j=load();if(!j.value("manualUntil",int64_t{0}))return;j["manualUntil"]=0;j["manualState"]="cancelled";save(j);}
  void rejectManualUpgrade(const std::string& token,const std::string& reason="download-manual-worker-upgrade-required"){update(token,[&](Json& j){j["manualUntil"]=0;j["manualState"]="component-upgrade-required";j["reason"]=reason;});}
  // Compatibility entry points for older UIs. Popular discovery is retired.
  void requestCatalog() {}
  bool catalogDue(const std::string&) const {return false;}
  void catalogResult(const std::string&,bool,const std::string& = {},size_t = 0) {}
  void enqueueCatalog(const Json&) {}
  void enqueue(const Json &targets,const std::string& requiredSession={},const Json& demand=Json::object(),const Json& replacementClaim=Json::object()) {
    if(!steamDemand::allowed(demand))return;
    if (!targets.is_array() || targets.size() > 10000)
      throw std::runtime_error("media-inventory-invalid");
    if (targets.empty())
      return;
    struct PreparedTarget {Json target;uint64_t revision;};
    std::vector<PreparedTarget> pending;
    Store store(root_);
    const auto entries = std::move(store.snapshot()["entries"]);
    const auto prepareMedia=[&](Json& target,Json e){
      for (const auto *slot : {"static", "dynamic"})
        if (e.value(std::string(slot) + "Ready", false)) {
          std::error_code ec;
          const auto assets = e.value("assets", Json::object());
          e[std::string(slot) + "Ready"] =
              assets.contains(slot) &&
              fs::is_regular_file(store.assetPath(assets[slot]), ec) && !ec;
        }
      const bool refresh = e.value("refreshRequested", false),upgrade=needsVideoQualityUpgrade(e,now());
      target["videoQualityPolicy"]=videoQualityPolicy;if(upgrade)target["qualityUpgradeRevision"]=e.value("bindingRevision",uint64_t{0});else target.erase("qualityUpgradeRevision");
      if (e.value("deleted", false) || complete(e, now()) && !refresh && !upgrade)
        return false;
      if(e.contains("selectedVideo"))target["selectedVideo"]=e["selectedVideo"];
      target.erase("requestBindingRevision");
      if (refresh)
        target["requestBindingRevision"] =
            e.value("bindingRevision", uint64_t{0});
      return true;
    };
    for (auto target : targets) {
      if(excludedQueueTool(target)||stringField(target,"catalogSource")=="popular")continue;
      const auto exe = target.value("executable", std::string{});
      if (!exe.empty()) {
        auto identity = pendingShortcut(target)?store.shortcutWallpaperIdentity(exe):store.identity(exe);
        if (!identity.value("verified", false)) {
          if(!pendingShortcut(target))continue;
          target["appId"]=0;target["verified"]=false;target["identityPending"]=true;
        } else {
          target["appId"] = identity.value("appId", int64_t{0});target["verified"] = true;target.erase("identityPending");
        }
      }
      if (targetKey(target).empty() || (!target.value("verified", false)&&!target.value("identityPending",false)))
        continue;
      auto e = entries.value(targetKey(target), Json::object());
      if(!prepareMedia(target,e))continue;
      pending.push_back({std::move(target),e.value("bindingRevision",uint64_t{0})});
    }
    Lock lock(root_);
    if(!steamDemand::allowed(demand))return;
    auto j = load();
    if(!requiredSession.empty()&&(j.value("session",std::string{})!=requiredSession||!downloadsEnabled(j)||policyBlocked(j)||!j.value("resourcePauseReason",std::string{}).empty()))return;
    if(pending.empty())return; // load above still validates checkpoint/version debt.
    // Store and Queue share this recursive mutex. A concurrent editor may have
    // finished or changed a slot while the inventory was checking identities.
    // Validate one fresh media snapshot for the whole batch before mutating it.
    const auto currentEntries=std::move(store.snapshot()["entries"]);
    const auto admissionRevision=j.value("statusRevision",uint64_t{0})+1;
    bool changed = false;
    for (auto& prepared : pending) {
      bool itemChanged=false;
      auto& target=prepared.target;
      // The original queue wait can outlive a library relationship or exact
      // cache edit. Keep this request's admitted AppID; a fresh inventory owns
      // any replacement. No extra work is performed for completed/empty batches.
      const auto executable=stringField(target,"executable");
      if(!executable.empty()&&target.value("verified",false)){
        const auto identity=pendingShortcut(target)?store.shortcutWallpaperIdentity(executable):store.identity(executable);
        if(!identity.value("verified",false)||identity.value("appId",int64_t{0})!=target.value("appId",int64_t{0}))continue;
      }
      auto key = targetKey(target);
      const auto current=currentEntries.value(key,Json::object());
      if(current.value("bindingRevision",uint64_t{0})!=prepared.revision){
        if(current.empty())continue; // A retired binding cannot be recreated.
        target.erase("selectedVideo");
        if(!prepareMedia(target,current))continue;
      }
      if(j.contains("recoveryBlockedKeys")&&std::find(j["recoveryBlockedKeys"].begin(),j["recoveryBlockedKeys"].end(),key)!=j["recoveryBlockedKeys"].end())continue;
      if (!j["jobs"].contains(key)) {
        if (j["jobs"].size() >= 10000){
          // Identity promotion replaces its own exact current placeholder.
          // Make room and accept its canonical successor in the same write;
          // the cap must not silently drop the successfully identified game.
          const auto retiring=stringField(replacementClaim,"key");
          if(requiredSession.empty()||!allowed(j,requiredSession)||retiring.empty()||retiring==key||
              !j["jobs"].contains(retiring)||!j["active"].contains(retiring))continue;
          const auto& source=j["jobs"][retiring];const auto& active=j["active"][retiring];
          const auto revision=replacementClaim.value("provenanceRevision",uint64_t{0});
          if(!source.value("identityPending",false)||!active.value("identityPending",false)||
              source.value("provenanceRevision",uint64_t{0})!=revision||active.value("provenanceRevision",uint64_t{0})!=revision||
              stringField(active,"session")!=requiredSession||stringField(active,"key")!=retiring||
              active.value("taskStartedAt",int64_t{0})!=replacementClaim.value("taskStartedAt",int64_t{0})||
              normalized(stringField(source,"executable"))!=normalized(stringField(target,"executable")))continue;
          j["jobs"].erase(retiring);j["active"].erase(retiring);
          if(stringField(j["current"],"key")==retiring)j["current"]=Json::object();
        }
        target["state"] = "pending";
        target["addedAt"] = now();
        target["attempts"] = 0;
        target.erase("provenanceRevision"); // Internal ownership never comes from caller metadata.
        if(stringField(target,"catalogSource")=="steam-shortcut"){target["localShortcut"]=Json::object();for(const auto* field:{"executable","steamAccount","shortcutAppId"})target["localShortcut"][field]=stringField(target,field);}
        j["jobs"][key] = target;
        itemChanged = true;
      } else {
        auto &job = j["jobs"][key];
        if(job.value("recoveryUnconfirmed",false)){job.erase("recoveryUnconfirmed");itemChanged=true;}
        if(target.contains("sourceTier")) {
          const auto incoming=(std::clamp)(target.value("sourceTier",3),0,3),previous=(std::clamp)(job.value("sourceTier",3),0,3);
          Json local={{"sourceTier",job.value("localSourceTier",previous==0?3:previous)},{"catalogSource",stringField(job,"localCatalogSource",previous==0?"remembered":stringField(job,"catalogSource"))}};
          if(incoming>0&&sourcePriority(target)<sourcePriority(local)){job["localSourceTier"]=incoming;job["localCatalogSource"]=stringField(target,"catalogSource");itemChanged=true;}
          if(incoming==0&&previous>0){job["localSourceTier"]=previous;job["localCatalogSource"]=stringField(job,"catalogSource");}
          if(sourcePriority(target)<sourcePriority(job)||(incoming==0&&previous==0&&target.value("chartRank",1000)!=job.value("chartRank",1000))) {
            job["sourceTier"]=incoming;job["catalogSource"]=stringField(target,"catalogSource");
            if(incoming==0)job["chartRank"]=(std::clamp)(target.value("chartRank",1000),1,1000);itemChanged=true;
          }
        }
        if(stringField(target,"catalogSource")=="steam-shortcut") {
          Json provenance=Json::object();for(const auto* field:{"executable","steamAccount","shortcutAppId"})provenance[field]=stringField(target,field);
          const auto previous=job.value("localShortcut",Json::object());
          // File job keys already use this path normalization. Retain its
          // representative spelling; a cosmetic path change is no new retry.
          if(previous.is_object()&&previous.contains("executable")&&previous["executable"].is_string()&&normalized(previous["executable"].get<std::string>())==normalized(stringField(provenance,"executable")))provenance["executable"]=previous["executable"];
          if(previous!=provenance){job["localShortcut"]=provenance;itemChanged=true;}
          if(stringField(job,"catalogSource")=="steam-shortcut"){
            bool ownerChanged=false;
            for(const auto* field:{"executable","steamAccount","shortcutAppId"}){
              const auto incoming=stringField(target,field),old=stringField(job,field);
              const bool differs=std::string_view(field)=="executable"?normalized(incoming)!=normalized(old):incoming!=old;
              if(differs){job[field]=incoming;resetAutomaticBudget(job);ownerChanged=true;itemChanged=true;}
            }
            if(ownerChanged){const auto revision=job.value("provenanceRevision",uint64_t{0});if(revision>=INT64_MAX)throw std::runtime_error("media-provenance-revision-exhausted");job["provenanceRevision"]=revision+1;}
          }
        }
        const auto played =
            (std::max)(int64_t{0}, target.value("lastPlayed", int64_t{0}));
        if (target.contains("lastPlayed") && played != job.value("lastPlayed", int64_t{0})) {
          job["lastPlayed"] = played;
          itemChanged = true;
        }
        if(target.value("videoQualityPolicy",0)>job.value("videoQualityPolicy",0)){job["videoQualityPolicy"]=target["videoQualityPolicy"];itemChanged=true;}
        if(target.value("qualityUpgradeRevision",uint64_t{0})>job.value("qualityUpgradeRevision",uint64_t{0})){job["qualityUpgradeRevision"]=target["qualityUpgradeRevision"];itemChanged=true;}
        if(target.contains("selectedVideo")&&target["selectedVideo"]!=job.value("selectedVideo",Json::object())){job["selectedVideo"]=target["selectedVideo"];itemChanged=true;}
        const bool refresh =
            target.contains("requestBindingRevision") &&
            target.value("requestBindingRevision", uint64_t{0}) >
                job.value("requestBindingRevision", uint64_t{0});
        if (refresh)
          job["requestBindingRevision"] = target["requestBindingRevision"];
        if (job.value("state", std::string{}) == "done" || refresh) {
          job["state"] = "pending";
          resetAutomaticBudget(job);
          itemChanged = true;
        }
      }
      if(itemChanged){if(admissionRevision>INT64_MAX)throw std::runtime_error("media-admission-revision-exhausted");j["jobs"][key]["admissionRevision"]=admissionRevision;changed=true;}
    }
    if (changed) {
      if(!steamDemand::allowed(demand))return;
      save(j);
    }
  }
  // Temporary ownership is a tiny independent record. Tracking two assets
  // must not serialize the complete game inventory four more times.
  fs::path temporaryRecordPath() const {
    return root_ / L"config" / L"game-media-download-temporary.json";
  }
  Json temporaryDocument() const {
    auto marker=checkpoint::read(temporaryRecordPath(),1024*1024,2);
    if(marker.contains("files")&&!marker["files"].is_object())throw std::runtime_error("media-temporary-invalid");
    if(!marker.contains("files")&&marker.contains("name")&&!marker["name"].is_string())throw std::runtime_error("media-temporary-invalid");
    return marker;
  }
  Json temporaryRecords(bool* readable=nullptr,bool* storageUnavailable=nullptr) const {
    if(readable)*readable=false;if(storageUnavailable)*storageUnavailable=false;
    Json marker;try{marker=temporaryDocument();}catch(const checkpoint::UpgradeRequired&){throw;}catch(const checkpoint::ReadUnavailable&){if(storageUnavailable)*storageUnavailable=true;return Json::object();}catch(...){return Json::object();}
    if(readable)*readable=true;
    if(marker.contains("files"))return marker["files"];
    const auto name=stringField(marker,"name");return name.empty()?Json::object():Json{{name,marker}};
  }
  void storeTemporaryRecords(const Json &files) const {
    Json old=Json::object();
    try{old=temporaryDocument();}catch(const checkpoint::UpgradeRequired&){throw;}catch(const checkpoint::ReadUnavailable&){throw std::runtime_error("media-storage-unavailable");}catch(...){checkpoint::preserve(temporaryRecordPath());}
    if(files.empty()){std::error_code ec;fs::remove(custom_steam_library::ioPath(temporaryRecordPath()),ec);if(ec)throw std::runtime_error("media-cache-temporary-remove-failed");}
    else {old["schemaVersion"]=2;old["files"]=files;old.erase("name");checkpoint::write(temporaryRecordPath(),old);}
  }

  void trackTemporary(const std::string &token,const std::string &name,bool resumable=false) {
    Lock lock(root_);
    if (token.empty()||liveStatus().value("session",std::string{})!=token) throw std::runtime_error("media-download-cancelled");
    bool storageUnavailable=false;auto files=temporaryRecords(nullptr,&storageUnavailable);
    if(storageUnavailable)throw std::runtime_error("media-storage-unavailable");
    if (!files.contains(name) && files.size()>=32) throw std::runtime_error("media-temporary-limit");
    files[name]={{"session",token},{"name",name},{"workerPid",GetCurrentProcessId()},
      {"workerBirth",processBirthToken(GetCurrentProcessId())},{"resumable",resumable}};
    storeTemporaryRecords(files);
  }
  void releaseTemporary(const std::string &token,const std::string &name) {
    Lock lock(root_);auto files=temporaryRecords();
    if (files.contains(name) && stringField(files[name],"session")==token) {files.erase(name);storeTemporaryRecords(files);}
  }
  void cleanupTemporary() {
    Lock lock(root_);auto files=temporaryRecords();bool changed=false;
    const auto folder=root_/L"cache"/L"jobs"/L"game-media";
    std::error_code ec;const auto ordinary=fs::absolute(folder).lexically_normal();
    auto canonical=fs::weakly_canonical(custom_steam_library::ioPath(folder),ec).wstring();
    // Compare logical ownership without leaking the extended IO spelling.
    if(canonical.starts_with(LR"(\\?\UNC\)"))canonical=LR"(\\)"+canonical.substr(8);
    else if(canonical.starts_with(LR"(\\?\)"))canonical=canonical.substr(4);
    if (ec||normalized(utf8(ordinary.wstring()))!=normalized(utf8(canonical))) return;
    for (auto it=files.begin();it!=files.end();) {
      const auto name=it.key();const auto marker=it.value();
      if (name.size()!=37||name.substr(32)!=".part"||name.substr(0,32).find_first_not_of("0123456789abcdef")!=std::string::npos) {++it;continue;}
      if(!marker.is_object()) {++it;continue;}
      const auto owner=marker.find("workerPid");
      if (owner==marker.end()||!owner->is_number_integer()) {++it;continue;}
      const auto value=owner->get<uint64_t>();
      const auto birth=stringField(marker,"workerBirth");
      if(birth.empty()||birth.size()>20||birth.find_first_not_of("0123456789")!=std::string::npos){++it;continue;}
      if (!value||value>MAXDWORD||!leaseOwnerGone({{"leasePid",static_cast<DWORD>(value)},
          {"leaseBirth",stringField(marker,"workerBirth")}})) {++it;continue;}
      const auto file=folder/wide(name);ec.clear();const auto status=fs::symlink_status(custom_steam_library::ioPath(file),ec);
      if (ec&&ec!=std::errc::no_such_file_or_directory) {++it;continue;}
      if (!ec&&status.type()!=fs::file_type::not_found&&(fs::is_symlink(status)||!fs::is_regular_file(status))) {++it;continue;}
      bool retain=false;
      if (marker.contains("resumable")&&marker["resumable"].is_boolean()&&marker["resumable"].get<bool>()) try {
        // A sidecar is unconfirmed evidence until Worker validates it. Cleanup
        // may release a proved-exited writer, but cannot erase that evidence.
        ec.clear();retain=fs::exists(custom_steam_library::ioPath(folder/wide(name.substr(0,32)+".resume.json")),ec)||bool(ec);
      } catch (...) {retain=true;}
      if (!retain) {ec.clear();fs::remove(custom_steam_library::ioPath(file),ec);if(ec){++it;continue;}}
      it=files.erase(it);changed=true;
    }
    if(changed) storeTemporaryRecords(files);
  }
  std::string start() {
    if(powerPolicy::blocked())return {};
    cleanupTemporary();
    Lock lock(root_);
    Json j;try{j=load();}catch(const checkpoint::UpgradeRequired&){return {};}
    const auto pid=j.value("workerPid",DWORD{0});
    if (pid&&!leaseOwnerGone({{"leasePid",pid},{"leaseBirth",j.value("workerBirth",std::string{})}})) return {};
    reclaimExited(j);
    if (j.value("cooldownUntil",int64_t{0})>now()) return {};
    if (!downloadsEnabled(j) ||
        policyBlocked(j) ||
        !j.value("resourcePauseReason", std::string{}).empty() ||
        (nextQueueKey(j, now()).empty()&&!(!scopedManual(j)&&j.value("catalogPending",false)&&j.value("catalogRetryAt",int64_t{0})<=now())))
      return {};
    if(manualRun(j)&&j.value("inventoryAwaitUntil",int64_t{0})>now()&&stringField(j,"inventoryReadyId")!=stringField(j,"inventoryRequestId"))return {};
    auto token = randomId();
    j["session"] = token;if(manualRun(j)){j["manualSession"]=token;j["manualSessionRequest"]=j["manualRequestId"];j["manualState"]="running";}
    j["ownerProtocol"]=2;j["ownerProtocolSession"]=token;
    j["sessionResumedBytes"]=0;j["resumedBytes"]=0;
    j["launchTarget"] = nextQueueKey(j, now());
    j["sessionClaimed"] = false;
    j["sessionIssuedAt"]=now();j["sessionDeadlineAt"]=now()+int64_t(sessionMs);
    j["phase"] = "starting";
    j["taskStartedAt"] = 0;
    j["workerPid"] = 0;
    j["workerBirth"] = "";
    j["reason"] = "";
    j["current"] = Json::object();
    j["bytes"] = 0;
    j["totalBytes"] = 0;
    j["heartbeatAt"] = now();
    save(j, false);
    return token;
  }
  bool markSharedSleep(const std::string& token){return update(token,[&](Json& state){markSleepActive(state,token,now());});}
  bool beginWorkerSession(const std::string& token,uint64_t budget){return update(token,[&](Json& state){const auto time=now();const auto prior=state.value("sharedSleepSession",Json::object());const auto issued=prior.value("sessionIssuedAt",int64_t{0}),deadline=prior.value("sessionDeadlineAt",int64_t{0});const bool same=prior.value("version",0)==1&&prior.value("manualRequestId",std::string{})==state.value("manualRequestId",std::string{})&&prior.value("manualUntil",int64_t{0})==state.value("manualUntil",int64_t{0});if(same&&issued>0&&time>=issued&&deadline>time&&deadline-issued<=int64_t(sessionMs)){state["sessionIssuedAt"]=issued;state["sessionDeadlineAt"]=deadline;}else{state["sessionIssuedAt"]=time;state["sessionDeadlineAt"]=time+static_cast<int64_t>((std::min)(budget,sessionMs));}state.erase("sharedSleepSession");});}
  bool update(const std::string &token,
              const std::function<void(Json &)> &edit) {
    Lock lock(root_);
    auto j = load();
    if (token.empty() || j.value("session", std::string{}) != token)
      return false;
    edit(j);
    save(j, false);
    return true;
  }
  static bool terminalReceipt(const Json &state, const std::string &token) {
    if (token.empty() || state.value("session", std::string{}) != token ||
        state.value("workerPid", DWORD{0}) != 0 ||
        !state.value("active", Json::object()).empty() ||
        !state.value("current", Json::object()).empty())
      return false;
    const auto phase = state.value("phase", std::string{});
    if (phase != "completed" && phase != "deferred" && phase != "paused")
      return false;
    const auto history = state.value("history", Json::array());
    return !history.empty() &&
           history.back().value("session", std::string{}) == token &&
           history.back().value("phase", std::string{}) == phase;
  }
  static bool sameResourceAdmission(const Json &current, const Json &observed) {
    if (!downloadsEnabled(current) || policyBlocked(current)) return false;
    // Byte progress is deliberately excluded. Only changes that invalidate the
    // captured admission/ownership intent fence an in-flight resource sample.
    for (const auto *field : {"session", "enabled", "pauseReason", "cpuThreshold",
                             "resourcePauseReason", "startupProtectionUntil",
                             "workerPid", "workerBirth", "history", "manualUntil",
                             "manualRequestId", "manualTargets", "manualTargetRevisions",
                             "manualPriorityKey", "manualState"})
      if (current.value(field, Json{}) != observed.value(field, Json{})) return false;
    return true;
  }
  static bool policyAllowed(const Json &j, const std::string &token) {
    return !token.empty() && j.value("session", std::string{}) == token &&
           j.value("wallpaperPowerCancelledSession",std::string{}) != token &&
           j.value("sharedSleepCancelledSession",std::string{}) != token &&
           downloadsEnabled(j) && !policyBlocked(j);
  }
  static bool allowed(const Json &j, const std::string &token) {
    return policyAllowed(j, token) &&
           j.value("resourcePauseReason", std::string{}).empty();
  }
  bool allowed(const std::string &token) const {
    return allowed(snapshot(), token);
  }
  // Every owned publication advances the same request, not its retry allowance.
  // Only its current owned Store receipt may update this claimed queue binding.
  bool advancePublishedBinding(const std::string& token,const std::string& key,
                               const Json& previous,const Json& published) {
    const auto from=previous.value("bindingRevision",uint64_t{0}),to=published.value("bindingRevision",uint64_t{0});
    const auto task=stringField(previous,"taskId");
    const bool partial=published.value("refreshRequested",false);
    const auto selected=previous.value("selectedVideo",Json::object());
    // Final acknowledgement belongs only to the current explicit selection.
    // Ordinary automatic completions retain their original write behavior.
    if(!partial&&(stringField(selected,"selectionToken").empty()||
       selected!=published.value("selectedVideo",Json::object())))return false;
    if(!from||from>=INT64_MAX||to!=from+1||task.empty()||
       stringField(previous,"key")!=key||stringField(published,"key")!=key||stringField(published,"taskId")!=task||
       previous.value("leasePid",DWORD{0})!=GetCurrentProcessId()||published.value("leasePid",DWORD{0})!=GetCurrentProcessId()||
       stringField(previous,"leaseBirth").empty()||stringField(previous,"leaseBirth")!=stringField(published,"leaseBirth"))return false;
    Lock lock(root_);auto j=load();
    if(!allowed(j,token)||j.value("workerPid",DWORD{0})!=GetCurrentProcessId()||
       stringField(j,"workerBirth")!=stringField(published,"leaseBirth")||!j["jobs"].contains(key)||!j["active"].contains(key))return false;
    auto& job=j["jobs"][key];auto& active=j["active"][key];
    if(stringField(active,"key")!=key||stringField(active,"session")!=token||
       stringField(job,"state")!="running"||active.value("identityPending",false)||
       active.value("requestBindingRevision",uint64_t{0})!=from||job.value("requestBindingRevision",uint64_t{0})!=from||
       active.value("provenanceRevision",uint64_t{0})!=job.value("provenanceRevision",uint64_t{0}))return false;
    if(!partial&&(!manualRun(j)||!manualTarget(j,key)||
       j.value("manualTargetRevisions",Json::object()).value(key,uint64_t{0})!=from||
       job.value("selectedVideo",Json::object())!=selected))return false;
    const auto entries=std::move(Store(root_).snapshot()["entries"]);const auto current=entries.value(key,Json::object());
    for(const auto* field:{"key","taskId","bindingRevision","leasePid","leaseBirth","assets"})
      if(current.value(field,Json{})!=published.value(field,Json{}))return false;
    if(!workDemand::allowed(current))return false;
    job["requestBindingRevision"]=to;active["requestBindingRevision"]=to;
    // This owned publication continues the same accepted selection.
    // It advances its receipt, never its request ID, deadline or retry budget.
    if(manualRun(j)&&manualTarget(j,key)&&
       j.value("manualTargetRevisions",Json::object()).value(key,uint64_t{0})==from&&
       !stringField(current.value("selectedVideo",Json::object()),"selectionToken").empty()&&
       current.value("selectedVideo",Json::object())==job.value("selectedVideo",Json::object()))
      j["manualTargetRevisions"][key]=to;
    if(stringField(j["current"],"key")==key)j["current"]["requestBindingRevision"]=to;
    save(j,false);return true;
  }
  // A current official video selection owns only its accepted manual claim.
  // Retained shortcut provenance continues to govern ordinary automatic work.
  bool currentManualVideoSelection(const std::string& token,const Json& target) const {
    const auto key=stringField(target,"key");const auto id=target.value("appId",int64_t{0});
    const auto revision=target.value("requestBindingRevision",uint64_t{0});
    const auto selected=target.value("selectedVideo",Json::object());
    if(target.value("identityPending",false)||!target.value("verified",false)||id<=0||id>2147483647||
        key!="steam-"+std::to_string(id)||!revision||selected.empty()||
        selected.value("appId",int64_t{0})!=id||stringField(selected,"selectionToken").empty())return false;
    Lock lock(root_);const auto j=load();
    if(!policyAllowed(j,token)||!manualRun(j)||!manualTarget(j,key)||
        j.value("workerPid",DWORD{0})!=GetCurrentProcessId()||
        j.value("manualTargetRevisions",Json::object()).value(key,uint64_t{0})!=revision||
        !j["jobs"].contains(key)||!j["active"].contains(key))return false;
    const auto& job=j["jobs"][key];const auto& active=j["active"][key];
    if(stringField(active,"session")!=token||stringField(active,"key")!=key||
        active.value("taskStartedAt",int64_t{0})!=target.value("taskStartedAt",int64_t{0})||
        job.value("provenanceRevision",uint64_t{0})!=target.value("provenanceRevision",uint64_t{0})||
        job.value("requestBindingRevision",uint64_t{0})!=revision||
        job.value("selectedVideo",Json::object())!=selected||active.value("selectedVideo",Json::object())!=selected)return false;
    const auto entry=Store(root_).resolve({{"key",key}});
    return entry.value("identityVerified",false)&&entry.value("appId",int64_t{0})==id&&
        entry.value("bindingRevision",uint64_t{0})==revision&&pendingMediaRefresh(entry,"dynamic")&&
        entry.value("selectedVideo",Json::object())==selected;
  }
  Json claim(const std::string &token) {
    Lock lock(root_);auto j=load();
    if (!policyAllowed(j,token) || !j.value("resourcePauseReason",std::string{}).empty() ||
        j["active"].size()>=size_t(j.value("maxParallel",maximumParallel))) return nullptr;
    const auto time=now();
    const auto key=nextQueueKey(j,time,&j["active"]);if (key.empty()) return nullptr;
    auto& queued=j["jobs"][key];
    int64_t continuationDeadline=0;
    if(queued.contains("sharedSleepWindow")){
      auto window=queued["sharedSleepWindow"];
      if(!sameSleepWork(queued,window))queued.erase("sharedSleepWindow");
      else{
        chargeSleepWall(queued,window,time);
        const auto attemptEnd=window.value("attemptDeadlineAt",int64_t{0});
        const auto sessionEnd=window.value("sessionDeadlineAt",int64_t{0});
        continuationDeadline=(std::min)(attemptEnd,sessionEnd);
        const bool exhausted=queued.value("automaticElapsedMs",int64_t{0})>=automaticGameMs;
        if(time>=continuationDeadline||exhausted){
          const auto attempts=(std::min)(automaticAttemptLimit,queued.value("attempts",0)+1);
          queued["attempts"]=attempts;
          queued["retryAt"]=time+retryMs;
          queued["lastError"]="media-task-time-budget";
          queued["lastTimeoutObservation"]={{"source","continuation"},
            {"elapsedMs",(std::max)(int64_t{0},(std::min)(time,continuationDeadline)-window.value("attemptIssuedAt",time))},
            {"continuationLimit",time<continuationDeadline?"game":sessionEnd<=attemptEnd?"session":"attempt"}};
          queued.erase("sharedSleepWindow");
          if(attempts>=automaticAttemptLimit||exhausted){
            queued["state"]="unavailable";queued["autoStopped"]=true;queued["retryAt"]=0;
            queued["stopReason"]=exhausted?"automatic-time-budget-exhausted":"automatic-attempt-budget-exhausted";
          }
          save(j,false);return nullptr;
        }
        queued["sharedSleepWindow"]=std::move(window);
      }
    }
    auto target=queued;target.erase("lastTimeoutObservation");target["key"]=key;target["session"]=token;target["state"]="running";target["taskStartedAt"]=time;
    const auto attemptLimit=manualRun(j)&&manualTarget(j,key)&&!target.value("selectedVideo",Json::object()).empty()?manualVideoAttemptMs:automaticAttemptMs;
    target["attemptBudgetMs"]=(std::max)(int64_t{1},(std::min)(attemptLimit,automaticGameMs-target.value("automaticElapsedMs",int64_t{0})));
    target["sessionIssuedAt"]=j.value("sessionIssuedAt",now());target["sessionDeadlineAt"]=j.value("sessionDeadlineAt",now()+int64_t(sessionMs));
    if(continuationDeadline){const auto& w=target["sharedSleepWindow"];target["attemptBudgetMs"]=(std::min)(target.value("attemptBudgetMs",automaticAttemptMs),continuationDeadline-time);target["sessionIssuedAt"]=w["sessionIssuedAt"];target["sessionDeadlineAt"]=w["sessionDeadlineAt"];}
    target["heartbeatAt"]=now();target["bytes"]=0;target["totalBytes"]=0;target["phase"]="resolving";
    j["jobs"][key]["state"]="running";j["active"][key]=target;
    j["current"]=target;j["sessionClaimed"]=true;j["phase"]="resolving";
    j["heartbeatAt"]=now();save(j,false);return target;
  }
  void result(const std::string &token, const std::string &key, bool ok,
              const std::string &reason = {}, int64_t retryAt = 0, const std::string& identityStamp = {}, const Json& shortcutObservation = Json{}, const Json& identityObservation = Json{}, TimeoutStage timeoutStage = TimeoutStage::unreported, const Json* expiredClaim = nullptr) {
    update(token, [&](Json &j) {
      if (powerPolicy::blocked()||j.value("wallpaperPowerCancelledSession",std::string{})==token||j.value("sharedSleepCancelledSession",std::string{})==token)return;
      if (!j["jobs"].contains(key))
        return;
      auto &job = j["jobs"][key];
      const auto current = j.value("active",Json::object()).value(key,j.value("current", Json::object()));
      const auto sameClaim=[&](const Json& observed){
        return stringField(current,"session")==token && stringField(current,"key")==key &&
            stringField(observed,"session")==token && stringField(observed,"key")==key &&
            observed.value("taskStartedAt",int64_t{0})==current.value("taskStartedAt",int64_t{0}) &&
            observed.value("provenanceRevision",uint64_t{0})==current.value("provenanceRevision",uint64_t{0}) &&
            observed.value("requestBindingRevision",uint64_t{0})==current.value("requestBindingRevision",uint64_t{0});
      };
      if(expiredClaim&&(!j["active"].contains(key)||!sameClaim(*expiredClaim)))return;
      if (current.value("key", std::string{}) == key &&
          (current.value("requestBindingRevision", uint64_t{0}) <
              job.value("requestBindingRevision", uint64_t{0}) ||
           current.value("provenanceRevision",uint64_t{0}) !=
              job.value("provenanceRevision",uint64_t{0}))) {
        if(job.value("state",std::string{})=="running")job["state"]="pending";
        j["active"].erase(key);
        j["current"] = Json::object();
        j["heartbeatAt"] = now();
        j["taskStartedAt"] = 0;
        return;
      }
      if(!identityStamp.empty()&&current.value("identityPending",false))job["identityPrerequisiteStamp"]=identityStamp;
      job["state"]=ok?(unavailableResult(reason)?"unavailable":"done"):"pending";
      job["lastError"]=reason.substr(0,120);
      job.erase("lastTimeoutObservation");
      const auto started=current.value("taskStartedAt",int64_t{0});
      if (!ok && started>0 && j["active"].contains(key) && stringField(current,"session")==token && stringField(current,"key")==key &&
          (workerTaskTimeout(reason) ||
           reason=="task-timeout" || reason=="session-timeout" || reason=="worker-stalled")) {
        Json observation={{"source",timeoutStage==TimeoutStage::supervisor?"supervisor":"worker"},
                          {"elapsedMs",(std::max)(int64_t{0},now()-started)}};
        if(const auto* stage=timeoutStageName(timeoutStage))observation["workerStage"]=stage;
        // Read only on settlement. A default claim is not a report from the lane.
        const auto status=readStatus();
        if(statusMatches(status,j)) {
          const auto& report=status["active"][key];
          const auto heartbeat=report.value("heartbeatAt",int64_t{0});
          const auto bytes=report.value("bytes",uint64_t{0});
          const auto phase=stringField(report,"phase");
          if(sameClaim(report) &&
             heartbeat>=started && (heartbeat>started || phase!="resolving" || bytes>0) &&
             (phase=="resolving" || phase=="identifying" || phase=="downloading" || phase=="retrying" || phase=="publishing")) {
            observation["lastReportedPhase"]=phase;
            observation["lastReportedAt"]=heartbeat;
            observation["lastReportedBytes"]=bytes;
            observation["lastReportedTotalBytes"]=report.value("totalBytes",uint64_t{0});
          }
        }
        job["lastTimeoutObservation"]=std::move(observation);
      }
      if(reason=="media-shortcut-no-longer-current"&&shortcutObservation.is_object()&&!shortcutObservation.empty()){
        Json detail={{"reason",stringField(shortcutObservation,"reason").substr(0,64)},
          {"observedAt",shortcutObservation.value("observedAt",int64_t{0})},
          {"expectedAccount",stringField(job,"steamAccount")},{"provenanceRevision",job.value("provenanceRevision",uint64_t{0})}};
        if(shortcutObservation.contains("activeAccountKnown")&&shortcutObservation["activeAccountKnown"].is_boolean())detail["activeAccountKnown"]=shortcutObservation["activeAccountKnown"];
        for(const auto* field:{"activeAccount","activeAccountAfter","win32Error"})
          if(shortcutObservation.contains(field)&&shortcutObservation[field].is_number_integer()&&shortcutObservation[field].get<int64_t>()>=0&&shortcutObservation[field].get<uint64_t>()<=UINT32_MAX)detail[field]=shortcutObservation[field];
        job["lastShortcutObservation"]=std::move(detail);
      }else job.erase("lastShortcutObservation");
      // Passive summary of this exited helper only, committed with the original
      // result after its session/provenance fence. Never persist raw provider data.
      job.erase("lastIdentityObservation");
      if(current.value("identityPending",false)&&identityObservation.is_object()&&!identityObservation.empty()){
        Json detail={{"provenanceRevision",job.value("provenanceRevision",uint64_t{0})}};
        for(const auto* field:{"observedAt","helperExitCode","successfulResponses","retryableFailures","nonRetryableResponses","examinedEntries","manifestCandidates"}){
          const auto it=identityObservation.find(field);
          if(it!=identityObservation.end()&&it->is_number_integer()&&(!it->is_number_unsigned()||it->get<uint64_t>()<=INT64_MAX)&&it->get<int64_t>()>=0&&
              (std::string_view(field)=="observedAt"||it->get<uint64_t>()<=UINT32_MAX)&&
              ((std::string_view(field)!="examinedEntries"&&std::string_view(field)!="manifestCandidates")||it->get<uint64_t>()<=64))detail[field]=*it;
        }
        for(const auto* value:{"missing","oversized","invalid-json","unknown-schema","target-mismatch","matching-target","storage-unavailable","invalid-fields","manifest-missing","manifest-unconfirmed"})
          if(stringField(identityObservation,"receiptStatus")==value)detail["receiptStatus"]=value;
        for(const auto* value:{"network-unavailable","unrecognized","manual-id-rejected"})
          if(stringField(identityObservation,"failureKind")==value)detail["failureKind"]=value;
        if(stringField(detail,"receiptStatus")=="matching-target"&&stringField(detail,"failureKind")=="unrecognized")
          custom_steam_library::identity_cache::copySteamLookupStage(identityObservation,detail);
        if(identityObservation.contains("retryWhenNetworkReturns")&&identityObservation["retryWhenNetworkReturns"].is_boolean())detail["retryWhenNetworkReturns"]=identityObservation["retryWhenNetworkReturns"];
        job["lastIdentityObservation"]=std::move(detail);
      }
      if(reason!=sharedSleepPause)job.erase("sharedSleepWindow");
      if(ok){
        job["attempts"]=0;job["retryAt"]=0;job["automaticElapsedMs"]=0;
        job["autoStopped"]=unavailableResult(reason);
        if(unavailableResult(reason))job["stopReason"]=reason;else job.erase("stopReason");
      }else if(reason=="cache-owned-or-deferred"){
        job["retryAt"]=(std::max)(retryAt,now()+retryMs);
      }else if(sharedFailure(reason)){
        job["retryAt"]=now()+retryMs;
      }else{
        const auto attempt=(std::min)(automaticAttemptLimit,job.value("attempts",0)+1);
        const auto chargedLimit=(std::clamp)(current.value("attemptBudgetMs",automaticAttemptMs),int64_t{1},manualVideoAttemptMs);
        const auto elapsed=(std::clamp)(now()-current.value("taskStartedAt",now()),int64_t{0},chargedLimit);
        const auto spent=(std::min)(automaticGameMs,job.value("automaticElapsedMs",int64_t{0})+elapsed);
        job["attempts"]=attempt;job["automaticElapsedMs"]=spent;
        // Retrying a confirmed absent prerequisite cannot produce a different
        // result. The existing recovery path checks the repaired files and membership.
        const bool missingPrerequisite=current.value("identityPending",false)&&
          (reason=="media-identity-target-unavailable"||reason=="media-identity-helper-unavailable");
        // An exact different target is not a transient account or storage failure.
        const bool changedTarget=current.value("identityPending",false)&&
          reason=="media-shortcut-no-longer-current"&&j["active"].contains(key)&&sameClaim(current)&&
          stringField(shortcutObservation,"reason")=="shortcut-target-changed";
        if(missingPrerequisite||changedTarget||attempt>=automaticAttemptLimit||spent>=automaticGameMs){job["state"]="unavailable";job["autoStopped"]=true;job["stopReason"]=(missingPrerequisite||changedTarget)?reason:spent>=automaticGameMs?"automatic-time-budget-exhausted":"automatic-attempt-budget-exhausted";job["retryAt"]=0;}
        else job["retryAt"]=(std::max)(retryAt,now()+retryMs);
      }
      j["active"].erase(key);
      j["activeCount"] = j["active"].size();
      j["current"] = Json::object();
      j["heartbeatAt"] = now();
      j["taskStartedAt"] = 0;
    });
  }
  bool promoteIdentity(const std::string& token,const Json& target) {
    // The claimed source, its cache use and retirement form one root-locked
    // handoff. A later account/shortcut epoch cannot borrow an old callback.
    Lock transaction(root_);auto state=load();const auto key=stringField(target,"key");
    if(!allowed(state,token)||!state["jobs"].contains(key)||!state["active"].contains(key))return false;
    const auto source=state["jobs"][key],active=state["active"][key];const auto revision=target.value("provenanceRevision",uint64_t{0});
    if(!source.value("identityPending",false)||!active.value("identityPending",false)||
        source.value("provenanceRevision",uint64_t{0})!=revision||active.value("provenanceRevision",uint64_t{0})!=revision||
        stringField(active,"key")!=key||stringField(active,"session")!=token||
        active.value("taskStartedAt",int64_t{0})!=target.value("taskStartedAt",int64_t{0}))return false;
    Store store(root_);const auto identity=pendingShortcut(source)?store.shortcutWallpaperIdentity(stringField(source,"executable")):store.identity(stringField(source,"executable"));
    if(!identity.value("verified",false))return false;
    auto resolved=source;resolved["appId"]=identity["appId"];resolved["verified"]=true;resolved.erase("identityPending");resolved.erase("key");
    const auto destination=state["jobs"].value(targetKey(resolved),Json::object());
    const auto sourceAdmission=source.value("admissionRevision",uint64_t{0}),destinationAdmission=destination.value("admissionRevision",uint64_t{0});
    const bool retainDestination=!destination.empty()&&(destinationAdmission>sourceAdmission||(!sourceAdmission&&!destinationAdmission));
    store.registerEntry(resolved);
    // A late lookup supplies identity, not a newer play/provenance observation.
    // Retain a newer canonical admission (or an unversioned legacy destination).
    enqueue(Json::array({retainDestination?destination:resolved}),token,Json::object(),target);
    const auto accepted=load();
    if(!accepted["jobs"].contains(targetKey(resolved))){
      const auto media=store.resolve(resolved);
      if(!media.value("deleted",false)&&!(complete(media,now())&&!media.value("refreshRequested",false)&&!needsVideoQualityUpgrade(media,now())))return false;
    }
    bool promoted=false;const bool applied=update(token,[&](Json& j){if(!downloadsEnabled(j)||policyBlocked(j)||!j.value("resourcePauseReason",std::string{}).empty())return;
      if(!j["jobs"].contains(key)){promoted=j["jobs"].contains(targetKey(resolved));return;}
      if(j["jobs"][key].value("identityPending",false)&&j["jobs"][key].value("provenanceRevision",uint64_t{0})==revision){j["jobs"].erase(key);j["active"].erase(key);j["current"]=Json::object();j["heartbeatAt"]=now();promoted=true;}
    });return applied&&promoted;
  }
  void deferUnsupportedQuality(const std::string& token){update(token,[](Json& j){for(auto& job:j["jobs"])if(job.value("qualityUpgradeRevision",uint64_t{0})>0){job["retryAt"]=now()+3600000;job["lastError"]="download-quality-worker-upgrade-required";}});}
  void deferUnsupportedIdentities(const std::string& token){update(token,[](Json& j){for(auto& job:j["jobs"])if(job.value("identityPending",false)){job["retryAt"]=now()+3600000;job["lastError"]="download-identity-worker-upgrade-required";}});}
  void end(const std::string &token, const std::string &phase,
           const std::string &reason = {}, bool invalidatePowerSession = false) {
    update(token, [&](Json &j) {
      if(phase=="paused"&&reason=="download-paused"&&(invalidatePowerSession||powerPolicy::blocked()))j["wallpaperPowerCancelledSession"]=token;
      if(phase=="paused"&&reason==sharedSleepPause)markSleepActive(j,token,now());
      for (auto &job:j["jobs"]) if (job.value("state",std::string{})=="running") job["state"]="pending";
      j["active"]=Json::object();j["activeCount"]=0;
      if(reason.find("media-disk-")==0||reason.find("media-cache-")==0||
          reason.find("media-staging-")==0||reason=="media-storage-unavailable")
        j["cooldownUntil"]=now()+retryMs;
      bool keepManualRetry=phase=="paused"&&reason==sharedSleepPause&&manualRun(j)&&!policyBlocked(j)&&!manualWorkComplete(j)&&stringField(j,"manualSession")==token&&stringField(j,"manualSessionRequest")==stringField(j,"manualRequestId");
      if(phase=="deferred"&&reason.empty()&&scopedManual(j)&&!policyBlocked(j)&&
         j.value("resourcePauseReason",std::string{}).empty()&&stringField(j,"manualSession")==token&&
         stringField(j,"manualSessionRequest")==stringField(j,"manualRequestId")){
        std::vector<std::string> retryKeys;const auto time=now(),until=j.value("manualUntil",int64_t{0});
        for(const auto& value:j["manualTargets"]){const auto key=value.get<std::string>();
          if(!j["jobs"].contains(key))continue;const auto& job=j["jobs"][key];
          const auto error=stringField(job,"lastError");
          const bool transient=error.rfind("media-network-",0)==0||error=="media-download-truncated"||
              error=="media-http-408"||error=="media-http-429"||
              (error.size()==14&&error.rfind("media-http-5",0)==0&&error[12]>='0'&&error[12]<='9'&&error[13]>='0'&&error[13]<='9');
          const auto revision=job.value("requestBindingRevision",uint64_t{0});const auto due=job.value("retryAt",int64_t{0});
          const auto selected=job.value("selectedVideo",Json::object());
          if(transient&&!itemStopped(job)&&stringField(job,"state")=="pending"&&
             !job.value("identityPending",false)&&job.value("verified",false)&&revision>0&&
             due>0&&due<until&&job.value("attempts",0)>0&&job.value("attempts",0)<automaticAttemptLimit&&
             job.value("automaticElapsedMs",int64_t{0})<automaticGameMs&&
             j.value("manualTargetRevisions",Json::object()).value(key,uint64_t{0})==revision&&
             selected.value("appId",int64_t{0})==job.value("appId",int64_t{0})&&
             !stringField(selected,"selectionToken").empty())retryKeys.push_back(key);
        }
        if(!retryKeys.empty())try{const auto entries=std::move(Store(root_).snapshot()["entries"]);
          for(const auto& key:retryKeys){const auto current=entries.value(key,Json::object());const auto& job=j["jobs"][key];
            if(current.value("identityVerified",false)&&current.value("appId",int64_t{0})==job.value("appId",int64_t{0})&&
               current.value("bindingRevision",uint64_t{0})==job.value("requestBindingRevision",uint64_t{0})&&
               pendingMediaRefresh(current,"dynamic")&&current.value("selectedVideo",Json::object())==job.value("selectedVideo",Json::object())){keepManualRetry=true;break;}}
        }catch(...){/* Unknown current selection does not authorize continuation. */}
      }
      if(keepManualRetry)j["manualState"]=phase;
      if(!keepManualRetry&&j.value("manualUntil",int64_t{0})>0&&j.value("manualSession",std::string{})==token&&
          ((j.value("manualSessionRequest",std::string{})==j.value("manualRequestId",std::string{})&&(phase!="completed"||!manualRun(j)||manualWorkComplete(j)))||manualWorkComplete(j))){j["manualUntil"]=0;j["manualState"]=phase;}
      j["phase"] = phase;
      std::string detail = reason;
      if (detail.empty() && phase == "deferred")
        for (const auto &job : j["jobs"])
          if (job.value("state", std::string{}) != "done" && job.value("state",std::string{})!="unavailable"&&
              !job.value("lastError", std::string{}).empty()) {
            detail = job.value("lastError", std::string{});
            break;
          }
      j["reason"] = detail;
      j["current"] = Json::object();
      j["workerPid"] = 0;
      j["workerBirth"] = "";
      j["taskStartedAt"] = 0;
      j["heartbeatAt"] = now();
      if (!j.contains("history"))
        j["history"] = Json::array();
      j["history"].push_back({{"phase", phase},
                              {"reason", detail},
                              {"time", now()},
                              {"session", token}});
      while (j["history"].size() > 16)
        j["history"].erase(j["history"].begin());
    });
  }
  // Used after a killed/crashed owner. Leases remain revision fenced; the
  // Store recognizes its dead PID/birth before a later worker can reacquire.
  void recover(const std::string &token, const std::string &reason,const std::string& failedKey = {},const Json* expiredClaim = nullptr) {
    const auto j = document();
    if (token.empty() || j.value("session", std::string{}) != token)
      return;
    if(j.value("catalogPending",false)&&j.value("catalogRetryAt",int64_t{0})<=now()&&!j.value("sessionClaimed",false)) {
      catalogResult(token,false,reason);end(token,"deferred",reason);return;
    }
    const auto active=j.value("active",Json::object());
    if (!active.empty()) {
      for (auto it=active.begin();it!=active.end();++it)if(failedKey.empty()||it.key()==failedKey)result(token,it.key(),false,reason,0,{}, {}, {},TimeoutStage::supervisor,expiredClaim?expiredClaim:&it.value());
    } else if(!expiredClaim) {
      auto key=j.value("current",Json::object()).value("key",std::string{});
      if (key.empty()&&!j.value("sessionClaimed",false)) key=j.value("launchTarget",std::string{});
      if (!key.empty()) result(token,key,false,reason);
    }
    end(token, "deferred", reason);
  }
  int64_t delay() const {
    auto j = snapshot();
    if (j.value("reason", std::string{}) == "queue-unreadable")
      return retryMs;
    if (!downloadsEnabled(j) ||
        policyBlocked(j))
      return -1;
    const auto time=now(),until=j.value("manualUntil",int64_t{0});
    const auto boundedByManual=[&](int64_t delay){
      if(until>0&&until<=time+int64_t(sessionMs)){
        const auto remaining=(std::max)(int64_t{0},until-time);
        return delay<0?remaining:(std::min)(delay,remaining);
      }
      return delay;
    };
    if(manualRun(j)&&j.value("inventoryAwaitUntil",int64_t{0})>time&&stringField(j,"inventoryReadyId")!=stringField(j,"inventoryRequestId"))return boundedByManual(j.value("inventoryAwaitUntil",int64_t{0})-time);
    if (j.contains("nextDueAt")) {
      const auto due = j.value("nextDueAt", int64_t{-1});
      return boundedByManual(due < 0 ? -1 : (std::max)(int64_t{0}, due - time));
    }
    // Compatibility with a pre-upgrade small snapshot; the next state write
    // installs the cached deadline without keeping a second polling owner.
    j = document();
    int64_t delay = -1;
    for (const auto &job : j["jobs"]) {
      if (itemStopped(job)||job.value("state", std::string{}) == "done")
        continue;
      const auto next =
          (std::max)(int64_t{0}, job.value("retryAt", int64_t{0}) - now());
      if (delay < 0 || next < delay)
        delay = next;
    }
    return boundedByManual(delay);
  }
};
class Watcher {
  HANDLE stop_ = nullptr, event_ = nullptr;
  std::thread thread_;
  std::mutex lifecycleMutex_;
  // Progress-only notices do not replace the durable queue. This observer
  // remembers only a successful file observation, never a second policy owner.
  // File identity distinguishes atomic replacement with equal size/write time.
  std::string preferenceStamp_;
  void refreshPowerPreference(const fs::path& root) {
    if (powerPolicy::ownsPreference()) return;
    const auto path=custom_steam_library::ioPath(root/L"config"/L"game-media-downloads.json");
    auto stamp=identityFileStamp(path);
    if (stamp!="unavailable" && stamp==preferenceStamp_) return;
    // Keep the original read path without a mutex wait. A replacement during
    // this read invalidates the observation instead of certifying old bytes.
    preferenceStamp_.clear();
    refreshMediaPowerPreference(root);
    if (stamp!="unavailable" && stamp==identityFileStamp(path)) preferenceStamp_=std::move(stamp);
  }

public:
  Watcher(const fs::path &root, const std::wstring &role,
          std::function<void()> changed) {
    event_ = CreateEventW(nullptr, FALSE, FALSE, eventName(root, role).c_str());
    stop_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!event_ || !stop_) {
      if (event_)
        CloseHandle(event_);
      if (stop_)
        CloseHandle(stop_);
      throw std::runtime_error("media-download-watch-failed");
    }
    thread_ = std::thread([this, root, changed] {
      HANDLE handles[] = {stop_, event_};
      while (WaitForMultipleObjects(2, handles, FALSE, INFINITE) ==
             WAIT_OBJECT_0 + 1) {
        try {
          refreshPowerPreference(root);
        } catch (...) {
        }
        // An unreadable preference must not hide the queue's unavailable state.
        // Keep the observer alive and let its existing snapshot path report it.
        try {
          changed();
        } catch (...) {
        }
      }
    });
  }
private:
  bool finish(bool waitForThread) noexcept {
    try{std::unique_lock lifecycle(lifecycleMutex_,std::defer_lock);
      if(waitForThread)lifecycle.lock();else if(!lifecycle.try_lock())return false;
      if(stop_&&!SetEvent(stop_))return false;
      if(thread_.joinable()){
        // The callback can still be publishing. Its events outlive it.
        if(!waitForThread&&WaitForSingleObject(thread_.native_handle(),0)!=WAIT_OBJECT_0)return false;
        thread_.join();
      }
      const auto close=[](HANDLE& value){if(!value)return true;if(!CloseHandle(value))return false;value=nullptr;return true;};
      const bool a=close(stop_),b=close(event_);return a&&b;
    }catch(...){return false;}
  }
public:
  bool finishForExit() noexcept {return finish(false);}
  ~Watcher(){(void)finish(true);}
};
} // namespace ymcc::media::downloads
