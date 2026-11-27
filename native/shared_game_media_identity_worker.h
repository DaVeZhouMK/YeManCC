#pragma once
#include "shared_game_media_shortcut.h"
#include "process_efficiency.h"
#include "steam_source_lease_contract.h"
namespace ymcc::media::downloads {
// Consumer only. The caller owns one public SourceLease until its entire
// request and owned child drain; acquiring fresh metadata cannot renew it.
template<class Lease>
inline bool readShortcutCurrentSource(const Json& target,const std::shared_ptr<Lease>& source,Json* observation=nullptr,const fs::path& dataRoot={}){
  DWORD account=0;bool accountKnown=false;
  const auto reject=[&](const char* reason){
    if(observation){*observation={{"reason",reason},{"observedAt",now()},{"activeAccountKnown",accountKnown}};
      if(accountKnown)(*observation)["activeAccount"]=account;}
    return false;
  };
  const auto requireSource=[&]{if(!source||!source->current())throw std::runtime_error("media-steam-facts-unavailable");};
  requireSource();
  const auto& snapshot=source->snapshot();
  using Selection=decltype(snapshot.selection);
  const auto& facts=snapshot.facts;
  if(!snapshot.sourceRevision||!snapshot.powerGeneration||facts.rootError!=ERROR_SUCCESS||facts.root.empty())
    throw std::runtime_error("media-steam-facts-unavailable");
  account=snapshot.selectedAccount;
  const bool fromUi=snapshot.selection==Selection::UiLocal;
  const bool fromRegistry=snapshot.selection==Selection::RegistryCandidate;
  if(!account||(!fromUi&&!fromRegistry)||
     (fromUi&&(!facts.uiAccountKnown||facts.uiAccount!=account))||
     (fromRegistry&&(facts.accountError!=ERROR_SUCCESS||facts.registryAccount!=account)))
    throw std::runtime_error("media-steam-facts-unavailable");
  accountKnown=true;
  if(std::to_string(account)!=stringField(target,"steamAccount"))return reject("active-account-mismatch");
  try{
    const auto file=fs::path(wide(facts.root))/L"userdata"/std::to_wstring(account)/L"config"/L"shortcuts.vdf";
    const auto row=shortcutIdentity(file,stringField(target,"shortcutAppId"),dataRoot,std::to_string(account));
    // A read that races with revocation is Unknown, including an apparently
    // absent/changed row. It must not consume a game's identity failure budget.
    requireSource();
    if(row.empty())return reject("shortcut-removed");
    if(normalized(stringField(row,"executable"))!=normalized(stringField(target,"executable")))return reject("shortcut-target-changed");
    return true;
  }catch(const std::exception& error){
    const std::string reason=error.what();
    if(reason=="media-steam-facts-unavailable")throw;
    requireSource();
    if(reason=="media-shortcuts-unavailable"||reason=="media-shortcuts-changed")throw std::runtime_error("media-storage-unavailable");
    return reject("shortcut-data-invalid");
  }catch(...){requireSource();return reject("shortcut-observation-failed");}
}
// Legacy source-less entry points have no authority in a child. The parent
// acquires and transfers a lease; membership never discovers or renews one.
inline bool readShortcutCurrent(const Json&,Json* =nullptr){throw std::runtime_error("media-steam-facts-unavailable");}
inline bool shortcutCurrent(const Json& target){return readShortcutCurrent(target,nullptr);}
struct ShortcutMembershipRejected:std::runtime_error {
  Json observation;
  explicit ShortcutMembershipRejected(Json value):std::runtime_error("media-shortcut-no-longer-current"),observation(std::move(value)){}
};
inline fs::path identityAppRoot(){wchar_t path[32768]{};const auto n=GetModuleFileNameW(nullptr,path,32768);if(!n||n>=32768)throw std::runtime_error("media-identity-worker-unavailable");return fs::path(path).parent_path();}
inline std::wstring identityQuote(const std::wstring& text){std::wstring out=L"\"";size_t slashes=0;for(auto c:text){if(c==L'\\'){++slashes;continue;}out.append(slashes*(c==L'"'?2:1)+(c==L'"'?1:0),L'\\');out+=c;slashes=0;}out.append(slashes*2,L'\\');return out+L'"';}
// Retry ownership is a negative capability scoped to this child. Copy the
// inherited block without changing process-global environment or CLI protocol.
inline std::vector<wchar_t> identityEnvironment(const fs::path& root){
  if(root.empty()||!root.is_absolute())throw std::runtime_error("media-identity-launch-failed");
  const auto inherited=GetEnvironmentStringsW();if(!inherited)throw std::runtime_error("media-identity-launch-failed");
  struct Release{LPWCH value;~Release(){FreeEnvironmentStringsW(value);}}release{inherited};
  constexpr wchar_t name[]=L"YEMAN_MEDIA_IDENTITY_RETRY_OWNER=";
  constexpr wchar_t dataName[]=L"YEMAN_STEAM_BIG_PICTURE_DATA_ROOT=";
  std::vector<std::wstring> entries;
  for(const auto* p=inherited;*p;p+=wcslen(p)+1)
    if(_wcsnicmp(p,name,std::size(name)-1)!=0&&_wcsnicmp(p,dataName,std::size(dataName)-1)!=0)entries.emplace_back(p);
  entries.emplace_back(L"YEMAN_MEDIA_IDENTITY_RETRY_OWNER=shared-media-v1");
  entries.emplace_back(std::wstring(dataName)+root.wstring());
  std::sort(entries.begin(),entries.end(),[](const auto& a,const auto& b){return _wcsicmp(a.c_str(),b.c_str())<0;});
  std::vector<wchar_t> block;
  for(const auto& entry:entries){block.insert(block.end(),entry.begin(),entry.end());block.push_back(0);}
  block.push_back(0);return block;
}
struct IdentityPorts {
  fs::path appRoot;
  std::function<bool(const Json&)> current=shortcutCurrent;
  uint64_t timeoutMs=90000;
  // Retain the parent's exact lease through owned child/Job resource return.
  // Appended after the existing aggregate fields for custom-provider callers.
  std::shared_ptr<ymcc::steaminfo::SourceLeaseV2> source;
  void requireCurrent(const Json& target,const fs::path& dataRoot={}) const {
    Json detail;
    // Custom providers keep their original one-call interface without a source.
    // The default always consumes this same lease, never the source-less entry.
    if(source&&!source->current())throw std::runtime_error("media-steam-facts-unavailable");
    const auto original=current.target<bool(*)(const Json&)>();
    const bool accepted=original&&*original==shortcutCurrent?readShortcutCurrentSource(target,source,&detail,dataRoot):current(target);
    // A source revoked during even an early account mismatch remains Unknown.
    if(source&&!source->current())throw std::runtime_error("media-steam-facts-unavailable");
    if(accepted)return;
    if(detail.is_null())detail={{"reason","membership-provider-refused"},{"observedAt",now()}};
    throw ShortcutMembershipRejected(std::move(detail));
  }
};
// The helper already distinguishes an unavailable network from an unrecognized
// title. Consume only this invocation's bounded, exact-file failure receipt;
// it grants retry eligibility, never a Steam identity.
struct IdentityLookupRejected:std::runtime_error {
  Json observation;
  IdentityLookupRejected(const std::string& reason,Json value):std::runtime_error(reason),observation(std::move(value)){}
};
inline bool identityNetworkUnavailable(const fs::path& output,const std::string& executable,Json* observation=nullptr){
  const auto status=[&](const char* value){if(observation)(*observation)["receiptStatus"]=value;};
  try{namespace cache=custom_steam_library::identity_cache;
    const auto receipt=cache::readSnapshot(output/L"rounds-failed.json");
    if(!receipt.exists){status("missing");return false;}
    if(!receipt.parsed){status(receipt.oversized?"oversized":"invalid-json");return false;}
    if(!cache::knownSchema(receipt.value)){status("unknown-schema");return false;}
    if(cache::executableKey(receipt.value.value("executable",std::string{}))!=cache::executableKey(executable)){status("target-mismatch");return false;}
    status("matching-target");
    const auto failure=receipt.value.value("failure",Json::object());
    if(observation){
      const auto kind=stringField(failure,"kind");
      for(const auto* known:{"network-unavailable","unrecognized","manual-id-rejected"})if(kind==known)(*observation)["failureKind"]=kind;
      if(kind=="unrecognized")cache::copySteamLookupStage(failure,*observation);
      for(const auto* field:{"successfulResponses","retryableFailures","nonRetryableResponses"}){
        const auto it=failure.find(field);
        if(it!=failure.end()&&it->is_number_integer()&&(it->is_number_unsigned()?it->get<uint64_t>()<=UINT32_MAX:(it->get<int64_t>()>=0&&it->get<int64_t>()<=UINT32_MAX)))(*observation)[field]=*it;
      }
      if(failure.contains("retryWhenNetworkReturns")&&failure["retryWhenNetworkReturns"].is_boolean())(*observation)["retryWhenNetworkReturns"]=failure["retryWhenNetworkReturns"];
    }
    return stringField(failure,"kind")=="network-unavailable"&&
        failure.value("retryWhenNetworkReturns",false)&&cache::integer(failure,"retryableFailures")>0;
  }catch(const std::exception& error){
    // The observation uses this same bounded read and never supplies identity.
    if(custom_steam_library::identity_cache::storageFailure(error.what())){
      status("storage-unavailable");throw std::runtime_error("media-storage-unavailable");
    }
    status("invalid-fields");return false;
  }catch(...){status("invalid-fields");return false;}
}
inline Json identifyShortcut(const fs::path& root,const Json& target,const IdentityPorts& ports,const std::function<void()>& heartbeat){
  // Declared before the cleanup owners: borrowed handles outlive their teardown.
  const auto source=ports.source;
  heartbeat();ports.requireCurrent(target,root);
  Store store(root);const auto exe=stringField(target,"executable");bool storageUnavailable=false;auto cached=store.identity(exe,&storageUnavailable);
  // Reuse the same confirmed launcher/primary projection as the inventory.
  // Known exact-cache IO failure keeps its original no-helper refusal.
  if(!cached.value("verified",false)&&!storageUnavailable)cached=store.shortcutWallpaperIdentity(exe);
  if(cached.value("verified",false)){ports.requireCurrent(target,root);return cached;}
  // Reuse the existing cache read: known IO denial cannot benefit from a helper.
  if(storageUnavailable)throw std::runtime_error("media-storage-unavailable");
  const auto helper=(ports.appRoot.empty()?identityAppRoot():ports.appRoot)/L"SteamArtworkLab.exe";
  if(!fs::is_regular_file(helper))throw std::runtime_error("media-identity-helper-unavailable");
  if(!fs::is_regular_file(fs::path(wide(exe))))throw std::runtime_error("media-identity-target-unavailable");
  const auto output=root/L"cache"/L"jobs"/L"game-media-identity"/wide(randomId());fs::create_directories(output);
  struct Cleanup{fs::path path;~Cleanup(){std::error_code ec;fs::remove_all(custom_steam_library::ioPath(path,true),ec);}} cleanup{output};
  auto arguments=identityWorkerArguments(exe,output);arguments[1]="--steam-comprehensive-identity-only";auto command=identityQuote(helper.wstring());for(const auto& arg:arguments)command+=L" "+identityQuote(wide(arg.get<std::string>()));
  std::vector<wchar_t> line(command.begin(),command.end());line.push_back(0);
  HANDLE job=CreateJobObjectW(nullptr,nullptr);if(!job)throw std::runtime_error("media-identity-launch-failed");
  struct Job{HANDLE h;~Job(){CloseHandle(h);}} closeJob{job};JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  if(!SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limits,sizeof limits))throw std::runtime_error("media-identity-launch-failed");
  STARTUPINFOW startup{sizeof startup};startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;PROCESS_INFORMATION child{};
  auto environment=identityEnvironment(root);
  if(!ymcc::processEfficiency::createProcess(helper.c_str(),line.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW|CREATE_SUSPENDED|IDLE_PRIORITY_CLASS|CREATE_UNICODE_ENVIRONMENT,environment.data(),helper.parent_path().c_str(),&startup,&child))throw std::runtime_error("media-identity-launch-failed");
  struct Child{PROCESS_INFORMATION p;~Child(){if(WaitForSingleObject(p.hProcess,0)==WAIT_TIMEOUT){TerminateProcess(p.hProcess,16);WaitForSingleObject(p.hProcess,2000);}CloseHandle(p.hThread);CloseHandle(p.hProcess);}} closeChild{child};
  if(!AssignProcessToJobObject(job,child.hProcess))throw std::runtime_error("media-identity-launch-failed");
  std::array<HANDLE,5> waitHandles{};
  if(source){
    const auto& events=source->cancellationEvents();
    for(size_t i=0;i<events.size();++i)waitHandles[i]=static_cast<HANDLE>(events[i]);
    // [2] is Native registration/request cancellation, not a hardware cause.
    // The provider has synchronization rights only; never query its PID.
    waitHandles[3]=static_cast<HANDLE>(source->providerProcess());
  }
  waitHandles[4]=child.hProcess;
  const auto requireSource=[&]{
    if(!source||!source->current())throw std::runtime_error("media-steam-facts-unavailable");
    for(size_t i=0;i<4;++i)if(!waitHandles[i]||waitHandles[i]==INVALID_HANDLE_VALUE)
      throw std::runtime_error("media-steam-facts-unavailable");
    // These handles are borrowed. Only wait; never inherit, close or reset them.
    if(WaitForMultipleObjects(4,waitHandles.data(),FALSE,0)!=WAIT_TIMEOUT||!source->current())
      throw std::runtime_error("media-steam-facts-unavailable");
  };
  if(source)requireSource();
  const auto tick=GetTickCount64();const auto started=now();auto membershipAt=tick;
  if(ResumeThread(child.hThread)==DWORD(-1))throw std::runtime_error("media-identity-launch-failed");
  for(;;){
    // Preserve the original one-second heartbeat and business timeout. The
    // source-less custom-provider path retains its original single wait.
    auto state=source?WaitForMultipleObjects(DWORD(waitHandles.size()),waitHandles.data(),FALSE,DWORD((std::min)(uint64_t{1000},ports.timeoutMs))):
      WaitForSingleObject(child.hProcess,DWORD((std::min)(uint64_t{1000},ports.timeoutMs)));
    if(source){
      // Lower source indices win a simultaneous child exit. Recheck also closes
      // revocation during/after a child-only wake, before accepting its output.
      if(state>=WAIT_OBJECT_0&&state<WAIT_OBJECT_0+4)throw std::runtime_error("media-steam-facts-unavailable");
      requireSource();
      if(state==WAIT_OBJECT_0+4)state=WAIT_OBJECT_0;
    }
    heartbeat();if(state==WAIT_OBJECT_0||GetTickCount64()-membershipAt>=5000){membershipAt=GetTickCount64();ports.requireCurrent(target,root);}
    if(state==WAIT_OBJECT_0)break;if(state!=WAIT_TIMEOUT)throw std::runtime_error("media-identity-launch-failed");
    if(GetTickCount64()-tick>=ports.timeoutMs)throw std::runtime_error("media-identity-timeout");
  }
  DWORD exit=1;if(!GetExitCodeProcess(child.hProcess,&exit))throw std::runtime_error("media-identity-unconfirmed");
  if(exit){
    Json observation={{"observedAt",now()},{"helperExitCode",exit}};bool network=false;
    try{network=identityNetworkUnavailable(output,exe,&observation);}
    catch(const std::exception& error){throw IdentityLookupRejected(error.what(),std::move(observation));}
    throw IdentityLookupRejected(network?"media-identity-network-unavailable":"media-identity-unconfirmed",std::move(observation));
  }
  std::error_code ec;size_t examined=0,manifests=0;const auto scanRoot=custom_steam_library::ioPath(output,true);
  for(fs::recursive_directory_iterator it(scanRoot,fs::directory_options::none,ec),end;!ec&&it!=end&&examined<64;it.increment(ec),++examined){
    if(it.depth()>2)it.disable_recursion_pending();
    const bool link=it->is_symlink(ec);if(ec)break;if(link)continue;
    const bool regular=it->is_regular_file(ec);if(ec)break;
    if(!regular||it->path().filename()!=L"manifest.json")continue;
    ++manifests;heartbeat();ports.requireCurrent(target,root);
    cached=store.rememberIdentity(exe,output/it->path().lexically_relative(scanRoot),started);if(cached.value("verified",false))return cached;
    // A valid lookup is not an identity rejection when its cache cannot be
    // read or committed. Reuse storage backoff without spending identity debt.
    // Keep other manifests eligible; malformed/foreign/future authority remains
    // an unconfirmed identity, never an excuse to accept or overwrite it.
    if(custom_steam_library::identity_cache::storageFailure(stringField(cached,"cacheError")))storageUnavailable=true;
  }
  // A denied directory read is not evidence that the helper found no identity.
  // Keep malformed/missing/unsafe output under the original identity refusal.
  if(ec&&(ec==std::errc::permission_denied||ec==std::errc::device_or_resource_busy||ec==std::errc::resource_unavailable_try_again||ec==std::errc::io_error||
      (ec.category()==std::system_category()&&(ec.value()==ERROR_SHARING_VIOLATION||ec.value()==ERROR_LOCK_VIOLATION||ec.value()==ERROR_NOT_READY||ec.value()==ERROR_DEVICE_NOT_CONNECTED))))storageUnavailable=true;
  if(storageUnavailable)throw std::runtime_error("media-storage-unavailable");
  // Preserve only facts from this same bounded scan before its output is
  // removed. A successful helper exit cannot supply identity by itself.
  throw IdentityLookupRejected("media-identity-unconfirmed",{{"observedAt",now()},{"helperExitCode",exit},
    {"receiptStatus",manifests?"manifest-unconfirmed":"manifest-missing"},{"examinedEntries",examined},{"manifestCandidates",manifests}});
}
} // namespace ymcc::media::downloads
