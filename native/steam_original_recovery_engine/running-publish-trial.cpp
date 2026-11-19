#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>
#include "vendor/steam_native_app_state_win32.h"
namespace rw=ymcc::steamrecordwin;
// Product-local version of the original read-only SHA256 dependency.
std::string diskHash(const std::wstring& path){
 BCRYPT_ALG_HANDLE alg=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;
 if(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)return {};
 std::string result;
 if(BCryptCreateHash(alg,&hash,nullptr,0,nullptr,0,0)>=0){
  std::ifstream file{std::filesystem::path(path),std::ios::binary};std::vector<unsigned char> bytes(65536);bool okay=file.good();
  while(file&&okay){file.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));const auto got=file.gcount();if(got>0&&BCryptHashData(hash,bytes.data(),static_cast<ULONG>(got),0)<0)okay=false;}
  unsigned char digest[32]{};if(okay&&file.eof()&&BCryptFinishHash(hash,digest,32,0)>=0){for(auto b:digest){result+="0123456789abcdef"[b>>4];result+="0123456789abcdef"[b&15];}}
  BCryptDestroyHash(hash);
 }
 BCryptCloseAlgorithmProvider(alg,0);return result;
}
#include "running-publish-trial.h"
#include <atomic>
namespace tr=running_trial;
namespace rc=ymcc::steamrecord;namespace ac=ymcc::steamappstate;
namespace {
SRWLOCK management=SRWLOCK_INIT;std::atomic<bool> preparationAttempted{false};struct ManagementGuard{ManagementGuard(){AcquireSRWLockExclusive(&management);}~ManagementGuard(){ReleaseSRWLockExclusive(&management);}};tr::Packet bound{};tr::Telemetry live{};tr::PublicationRevocation publicationRevocation;tr::GroupPublicationRevocation groupPublicationRevocation;rw::Module client,ui;std::optional<tr::NativeProfile> nativeProfile;rc::ProfileId clientProfile{};ac::ProfileId uiProfile{};ymcc::steamcompat::Qualification clientCompatibility,uiCompatibility;rw::Handle game,steamSelf,ymcc,cef;rc::detail::Selection originalUser;rc::detail::Header originalHeader;rc::detail::Target originalTarget;std::uint64_t originalRecord=0,flagsAddress=0;osd::PrepareResult prepared{};
namespace roles=ymcc::steamgameroles;
std::array<rw::Handle,roles::kMaxMembers> roleHandles;
std::atomic<bool> roleRevoked{true};
// Protected by management. A failed Close retains its exact handle for a later
// explicit Cleanup; no callback can be active when this flag is set.
bool roleCleanupPending=false;
bool releaseRoleHandles() noexcept {
 roleRevoked.store(true,std::memory_order_release);
 bool released=true;
 for(auto& held:roleHandles)if(held.valid()){
  if(CloseHandle(held.h))held.h=nullptr;else released=false;
 }
 return released;
}
HANDLE roleHandle(std::uint32_t index) noexcept {
 return bound.roleGroup.graph.members[index].pid==bound.gamePid?game.h:roleHandles[index].h;
}
// Query the held process, never an unpinned PID inventory. Unlike a DWORD-only
// parent helper, failure is distinct from a legitimate external/root parent 0.
bool roleParentMatches(HANDLE held,const roles::MemberIdentity& expected) noexcept {
 using Query=LONG(NTAPI*)(HANDLE,ULONG,PVOID,ULONG,PULONG);
 struct Basic {LONG status;PVOID peb;ULONG_PTR affinity;LONG priority;ULONG_PTR pid,parent;};
 static_assert(sizeof(Basic)==48&&offsetof(Basic,pid)==32&&offsetof(Basic,parent)==40);
 static const auto query=reinterpret_cast<Query>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"NtQueryInformationProcess"));
 Basic value{};ULONG returned=0;
 return query&&query(held,0,&value,sizeof value,&returned)>=0&&returned==sizeof value&&
        value.pid==expected.pid&&value.parent<=MAXDWORD&&
        static_cast<DWORD>(value.parent)==expected.parentPid;
}
bool roleObjectsCurrent() noexcept {
 if(!bound.roleGroup.enabled)return true;
 if(roleRevoked.load(std::memory_order_acquire))return false;
 try {
  if(!tr::validRoleGroupWire(bound)){roleRevoked.store(true,std::memory_order_release);return false;}
  const auto& wire=bound.roleGroup;
  for(std::uint32_t i=0;i<wire.graph.count;++i){
   const auto held=roleHandle(i);const auto& expected=wire.identities[i];
   if(!held||held==INVALID_HANDLE_VALUE||GetProcessId(held)!=expected.pid||
      !rw::exactProcess(held,expected.pid,expected.birth,expected.path,expected.session)||
      !roleParentMatches(held,wire.graph.members[i])){
    roleRevoked.store(true,std::memory_order_release);return false;
   }
  }
  // A later member query must not conceal an earlier member exiting meanwhile.
  // This remains a bounded checkpoint, not an atomic multi-process snapshot.
  for(std::uint32_t i=0;i<wire.graph.count;++i)
   if(WaitForSingleObject(roleHandle(i),0)!=WAIT_TIMEOUT){roleRevoked.store(true,std::memory_order_release);return false;}
  return !roleRevoked.load(std::memory_order_acquire);
 }catch(...){roleRevoked.store(true,std::memory_order_release);return false;}
}
// The receiver owns only duplicates. Host source/export slots remain borrowed
// until the original dispatcher/child return has been proved by the host.
bool groupCancellationCurrent() noexcept {
 if(roleRevoked.load(std::memory_order_acquire))return false;
 if(!tr::validRoleGroupWire(bound)){roleRevoked.store(true,std::memory_order_release);return false;}
 if(!bound.roleGroup.enabled&&tr::emptyRoleCancellationWire(bound.roleGroup.cancellation))return true;
 if(!groupPublicationRevocation.current()){roleRevoked.store(true,std::memory_order_release);return false;}
 return !roleRevoked.load(std::memory_order_acquire);
}
bool roleGroupCurrent() noexcept {
 return groupCancellationCurrent()&&roleObjectsCurrent()&&groupCancellationCurrent();
}
bool pinRoleMembersOnce() {
 if(!tr::validRoleGroupWire(bound))return false;
 if(!bound.roleGroup.enabled){roleRevoked.store(false,std::memory_order_release);return true;}
 const auto& wire=bound.roleGroup;
 for(std::uint32_t i=0;i<wire.graph.count;++i){
  const auto& member=wire.identities[i];const auto path=rw::pathKey(member.path);
  if(path==rw::pathKey(bound.steamPath)||path==rw::pathKey(bound.ymcc.path)||path==rw::pathKey(bound.cef.path))return false;
  if(member.pid==bound.gamePid)continue;
  if(roleHandles[i].valid())return false;
  // Only one query/wait Open per additional object; never VM_READ/write or a
  // permission fallback, and never reopen a revoked/exited object's PID.
  roleHandles[i].h=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,member.pid);
  if(!roleHandles[i].valid())return false;
 }
 roleRevoked.store(false,std::memory_order_release);
 // Prepare checks held objects before the cancellation receiver is bound.
 return roleObjectsCurrent();
}
bool targetRolesMatch(const rc::detail::Target& target) noexcept {
 if(!bound.roleGroup.enabled)return target.primary==bound.gamePid&&target.foreground==bound.gamePid;
 const auto& wire=bound.roleGroup;
 if(!groupCancellationCurrent()||target.primary!=wire.expectedPrimary||target.foreground!=wire.expectedForeground)return false;
 if(wire.version==3)return roles::directLauncherTuple(wire.graph,target.primary,target.foreground);
 const auto result=roles::inspect(wire.graph,target.primary,target.foreground);
 if(target.primary==result.root.pid&&target.foreground==result.root.pid)return false;
 return (result.nativePrimaryExact||result.nativeForegroundExact)&&
        (result.state==roles::State::ExactSelected||result.state==roles::State::VerifiedGroupRoles);
}
using ProcessReadViews=ymcc::steamdiagnostic::ReadViews;
ProcessReadViews qualifiedProcessReadViews(const ymcc::steamcompat::Qualification& qualification,
    std::uint64_t base,std::uint64_t size,const ymcc::steamcompat::ReadRva& raw){
 auto views=ymcc::steamcompat::makeProcessReadViews(qualification,base,size,raw);
 if(!views.rawObject||!views.canonicalModule||!views.explicitModulePointerNormalizer)
  throw std::runtime_error("native-process-read-views-unavailable");
 return views;
}
bool legacyQualification(){return bound.qualificationRoute==tr::QualificationRoute::LegacyDynamic;}
bool fixedOriginalQualification=false;
bool rawQualification(){return legacyQualification()&&fixedOriginalQualification;}
// Cleanup clears identity, but the consumed DLL keeps its original wire route.
void clearBoundIdentity(){const auto route=bound.qualificationRoute;bound={};bound.qualificationRoute=route;}
bool readLocal(std::uint64_t a,void*p,std::size_t n){SIZE_T got=0;return ymcc::steamappstatewin::readableRegion(GetCurrentProcess(),a,n)&&ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(a),p,n,&got)&&got==n;}
bool baseIdentity() noexcept {try{
 const bool exact=GetCurrentProcessId()==bound.steamPid&&rw::exactProcess(steamSelf.h,bound.steamPid,bound.steamBirth,bound.steamPath,bound.session)&&game.valid()&&rw::exactProcess(game.h,bound.gamePid,bound.gameBirth,bound.gamePath,bound.session)&&ymcc.valid()&&rw::exactProcess(ymcc.h,bound.ymcc.pid,bound.ymcc.birth,bound.ymcc.path,bound.ymcc.session)&&cef.valid()&&rw::exactProcess(cef.h,bound.cef.pid,bound.cef.birth,bound.cef.path,bound.cef.session);
 if(!exact){roleRevoked.store(true,std::memory_order_release);return false;}
 return true;
 }catch(...){roleRevoked.store(true,std::memory_order_release);return false;}}
bool identity() noexcept {return baseIdentity()&&roleGroupCurrent();}

bool validHealthyProof(const tr::SavedHealthyProof& p){
 if(p.healthyFlags!=8196||p.pinNonce[36]||std::strlen(p.pinNonce)!=36)return false;
 for(const auto* pin:{p.ownerPin,p.recordPin,p.uiObjectPin}){if(pin[64]||std::strlen(pin)!=64)return false;for(unsigned j=0;j<64;j++)if(!((pin[j]>='0'&&pin[j]<='9')||(pin[j]>='a'&&pin[j]<='f')))return false;}
 return true;
}
std::string pinValues(std::initializer_list<std::uint64_t> values){
 BCRYPT_ALG_HANDLE alg=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;std::string out;
 if(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)return {};
 if(BCryptCreateHash(alg,&hash,nullptr,0,nullptr,0,0)<0){BCryptCloseAlgorithmProvider(alg,0);return {};}
 bool okay=BCryptHashData(hash,reinterpret_cast<PUCHAR>(bound.healthyProof.pinNonce),36,0)>=0;
 for(auto x:values)okay=okay&&BCryptHashData(hash,reinterpret_cast<PUCHAR>(&x),sizeof x,0)>=0;
 unsigned char d[32]{};okay=okay&&BCryptFinishHash(hash,d,32,0)>=0;BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(alg,0);
 if(!okay)return {};for(auto b:d){out+="0123456789abcdef"[b>>4];out+="0123456789abcdef"[b&15];}return out;
}
bool matchesSavedHealthy(const rc::detail::Selection& s,std::uint64_t record,std::uint64_t address){
 return address>=0x20&&pinValues({client.base,bound.steamBirth,s.engine,s.user,bound.account})==bound.healthyProof.ownerPin&&pinValues({bound.steamBirth,s.user,record,bound.gameId})==bound.healthyProof.recordPin&&pinValues({bound.steamBirth,ui.base,address-0x20,bound.gameId})==bound.healthyProof.uiObjectPin;
}
bool snapshot(bool before){try{const auto tick=GetTickCount64();if(!publicationRevocation.current()||!nativeProfile||!identity()||!tr::validQualificationRoute(bound.qualificationRoute))return false;const auto recordViews=rawQualification()?ProcessReadViews{rc::Read(readLocal),{},{}}:qualifiedProcessReadViews(clientCompatibility,client.base,client.size,readLocal);rc::Query q{client.base,rawQualification()?client.size:ymcc::steamcompat::referenceImageSize(clientCompatibility,client.size),bound.account,bound.gameId,bound.gamePid,true,clientProfile};rc::detail::Reader r(recordViews);const auto s=rc::detail::selectUser(r,q);if(!(s==originalUser))return false;const auto h=rc::detail::readHeader(r,s.user,clientProfile);const auto targets=rc::detail::readTargets(r,q,h);if(targets.size()!=1)return false;const auto&t=targets[0];const auto record=h.records+rc::detail::profileLayout(clientProfile).recordStride*t.index;if(record!=originalRecord||!(t==originalTarget)||!targetRolesMatch(t))return false;rc::detail::checkSelection(r,q,s);if(!(rc::detail::readHeader(r,s.user,clientProfile)==h)||rc::detail::readTargets(r,q,h)!=targets)return false;
 const auto uiViews=rawQualification()?ProcessReadViews{ac::Read(readLocal),{},{}}:qualifiedProcessReadViews(uiCompatibility,ui.base,ui.size,readLocal);ac::Query aq{ui.base,rawQualification()?ui.size:ymcc::steamcompat::referenceImageSize(uiCompatibility,ui.size),tr::shortcutAppId(bound.gameId),true,uiProfile};ac::detail::Reader ar(uiViews);ac::detail::checkAnchors(ar,aq);const auto flags=ac::detail::readFlags(ar,aq);if(ar.fields.empty()||ar.fields.back().address!=flagsAddress||!matchesSavedHealthy(s,record,flagsAddress))return false;std::array<unsigned char,14> entry{};if(!readLocal(client.base+nativeProfile->getterRva,entry.data(),entry.size())||std::memcmp(entry.data(),prepared.detourBytes,entry.size()))return false;
 if(before){live.flagsBefore=flags;live.primaryBefore=t.primary;live.foregroundBefore=t.foreground;}else{live.flagsAfter=flags;live.primaryAfter=t.primary;live.foregroundAfter=t.foreground;}return identity()&&publicationRevocation.current()&&GetTickCount64()-tick<100&&(!before||flags==(bound.noPublish?8196u:4u));
 }catch(...){return false;}}
osd::CallbackResult __fastcall publish(void*){
 live.sourceThread=GetCurrentThreadId();InterlockedExchange(&live.stage,1);if(!publicationRevocation.current()){live.rejectCode=3;InterlockedExchange(&live.callbackResult,2);return osd::CallbackResult::Rejected;}if(!snapshot(true)){live.rejectCode=1;InterlockedExchange(&live.callbackResult,2);return osd::CallbackResult::Rejected;}InterlockedExchange(&live.guardBefore,1);
 if(!bound.noPublish){if(GetTickCount64()>=bound.deadlineTick){live.rejectCode=2;return osd::CallbackResult::Rejected;}if(!publicationRevocation.current()||!identity()||!publicationRevocation.current()||!groupCancellationCurrent()){live.rejectCode=4;InterlockedExchange(&live.callbackResult,2);return osd::CallbackResult::Rejected;}if(GetTickCount64()>=bound.deadlineTick){live.rejectCode=2;InterlockedExchange(&live.callbackResult,2);return osd::CallbackResult::Rejected;}InterlockedIncrement(&live.publisherCalls);using Publisher=void(__fastcall*)(void*,const std::uint64_t*,bool);auto call=reinterpret_cast<Publisher>(client.base+nativeProfile->publisherRva);const auto fullGameId=bound.gameId;call(reinterpret_cast<void*>(originalUser.user+nativeProfile->publisherSelfOffset),&fullGameId,true);InterlockedExchange(&live.publisherReturned,1);}
 const bool good=snapshot(false);InterlockedExchange(&live.guardAfter,good?1:0);InterlockedExchange(&live.stage,2);InterlockedExchange(&live.callbackResult,good?1:2);return good?osd::CallbackResult::Completed:osd::CallbackResult::Rejected;
}
DWORD prepare(tr::Packet&p){const tr::Packet input=p;if(input.version!=tr::abi||input.size!=sizeof p||!tr::validQualificationRoute(input.qualificationRoute)||(!tr::shortcutAppId(input.gameId)||!ymcc::steamappstatewin::gameIdMatches(input.gameId,tr::shortcutAppId(input.gameId))||!tr::validPacketPaths(input))||!input.account||input.steamPid!=GetCurrentProcessId()||input.noPublish>1)return 100;if(!validHealthyProof(input.healthyProof)||input.noPublish||input.ymcc.session!=input.session||input.cef.session!=input.session)return 108;if(!tr::validPublicationRevocation(input.revocation))return 110;if(!tr::validRoleGroupWire(input))return 112;osd::Status prior{};prior.size=sizeof prior;OsdGetStatus(&prior);if(prior.state!=osd::State::Empty||prior.inFlight||preparationAttempted.exchange(true))return 200+static_cast<DWORD>(osd::Error::Busy);bound=input;
 // Prepare never arms the dispatcher. Failed preparation retires the attempt
 // and returns auxiliary pins; a failed Close is retained for explicit Cleanup.
 struct PreparationLifetime{bool accepted=false;~PreparationLifetime(){if(!accepted){publicationRevocation.retire();groupPublicationRevocation.retire();roleCleanupPending=!releaseRoleHandles();}}} preparationLifetime;
if(steamSelf.valid())CloseHandle(steamSelf.h);steamSelf.h=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,GetCurrentProcessId());if(game.valid())CloseHandle(game.h);game.h=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,input.gamePid);if(ymcc.valid())CloseHandle(ymcc.h);ymcc.h=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,input.ymcc.pid);if(cef.valid())CloseHandle(cef.h);cef.h=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,input.cef.pid);if(!pinRoleMembersOnce())return 113;if(!baseIdentity())return 101;if(input.roleGroup.enabled){if(!groupPublicationRevocation.bind(ymcc.h,input.roleGroup.cancellation))return 115;}else if(input.roleGroup.version==2&&!groupPublicationRevocation.bindOriginal(ymcc.h,input.roleGroup.cancellation))return 115;if(!identity())return 101;if(!publicationRevocation.bind(ymcc.h,input.revocation))return 111;if(!rw::exactModule(input.steamPid,tr::steamModulePath(input.steamPath,L"steamclient64.dll"),client)||!rw::exactModule(input.steamPid,tr::steamModulePath(input.steamPath,L"SteamUI.dll"),ui))return 102;
 // Preserve the original fixed/raw path for the three known pairs. For
 // unknown files this DLL independently proves the complete public contracts;
 // a caller's route, boolean, JSON or offset cannot grant qualification.
 fixedOriginalQualification=false;
 if(legacyQualification()){
  const auto clientHash=diskHash(client.path),uiHash=diskHash(ui.path);
  nativeProfile=tr::profileForHashes(clientHash,uiHash);
  const auto record=rc::profileIdForSha256(clientHash);const auto uiLayout=tr::knownUiLayout(uiHash);
  const auto state=uiLayout&&*uiLayout==ymcc::steamcompat::Layout::OwnerScoped?std::optional<ac::ProfileId>(ac::ProfileId::OwnerScoped):ac::profileIdForSha256(uiHash);
  if(nativeProfile&&record&&state){clientProfile=*record;uiProfile=*state;fixedOriginalQualification=true;}
  else if(tr::knownClientLayout(clientHash)&&tr::knownUiLayout(uiHash))return 102;
 }
 if(!fixedOriginalQualification){
 clientCompatibility=ymcc::steamcompat::qualifyFile(client.path,ymcc::steamcompat::ModuleKind::Client,false);
 uiCompatibility=ymcc::steamcompat::qualifyFile(ui.path,ymcc::steamcompat::ModuleKind::Ui,false);
 if(!clientCompatibility.layout||!uiCompatibility.layout)return 102;
 nativeProfile=tr::profileForCompatibility(*clientCompatibility.layout,*uiCompatibility.layout);if(!nativeProfile)return 102;
 nativeProfile->getterRva=ymcc::steamcompat::resolvedRva(clientCompatibility,nativeProfile->getterRva);nativeProfile->publisherRva=ymcc::steamcompat::resolvedRva(clientCompatibility,nativeProfile->publisherRva);if(!nativeProfile->getterRva||!nativeProfile->publisherRva)return 102;
 clientProfile=*clientCompatibility.layout==ymcc::steamcompat::Layout::OwnerScoped?rc::ProfileId::OwnerScoped:*clientCompatibility.layout==ymcc::steamcompat::Layout::Public?rc::ProfileId::Public1792618285:rc::ProfileId::Legacy1791918634;
 uiProfile=*uiCompatibility.layout==ymcc::steamcompat::Layout::OwnerScoped?ac::ProfileId::OwnerScoped:*uiCompatibility.layout==ymcc::steamcompat::Layout::Public?ac::ProfileId::Public1792618285:ac::ProfileId::Legacy1791918634;
 const auto loadedRead=[](const rw::Module& module){return ymcc::steamcompat::ReadRva([module](std::uint64_t rva,void* target,std::size_t bytes){
   return rva<module.size&&bytes<=module.size-rva&&readLocal(module.base+rva,target,bytes);});};
 if(!ymcc::steamcompat::qualifyLoaded(clientCompatibility,ymcc::steamcompat::ModuleKind::Client,loadedRead(client),client.base,client.size,{},[](const ymcc::steamcompat::ImportSlot& slot,std::uint64_t target){return ymcc::steamcompat::resolvedImportTarget(GetCurrentProcess(),slot,target);}).matched||
    !ymcc::steamcompat::qualifyLoaded(uiCompatibility,ymcc::steamcompat::ModuleKind::Ui,loadedRead(ui),ui.base,ui.size,{},[](const ymcc::steamcompat::ImportSlot& slot,std::uint64_t target){return ymcc::steamcompat::resolvedImportTarget(GetCurrentProcess(),slot,target);}).matched||!identity())return 102;
 if(ymcc::steamcompat::resolvedRva(clientCompatibility,rc::detail::profileLayout(clientProfile).getterRva)!=nativeProfile->getterRva)return 102;
 }
 const auto recordViews=rawQualification()?ProcessReadViews{rc::Read(readLocal),{},{}}:qualifiedProcessReadViews(clientCompatibility,client.base,client.size,readLocal);rc::Query q{client.base,rawQualification()?client.size:ymcc::steamcompat::referenceImageSize(clientCompatibility,client.size),input.account,input.gameId,input.gamePid,true,clientProfile};rc::detail::Reader r(recordViews);rc::detail::checkGetter(r,q);originalUser=rc::detail::selectUser(r,q);originalHeader=rc::detail::readHeader(r,originalUser.user,clientProfile);const auto targets=rc::detail::readTargets(r,q,originalHeader);if(targets.size()!=1||!targetRolesMatch(targets[0]))return 103;originalTarget=targets[0];originalRecord=originalHeader.records+rc::detail::profileLayout(clientProfile).recordStride*originalTarget.index;const auto uiViews=rawQualification()?ProcessReadViews{ac::Read(readLocal),{},{}}:qualifiedProcessReadViews(uiCompatibility,ui.base,ui.size,readLocal);ac::Query aq{ui.base,rawQualification()?ui.size:ymcc::steamcompat::referenceImageSize(uiCompatibility,ui.size),tr::shortcutAppId(bound.gameId),true,uiProfile};ac::detail::Reader ar(uiViews);ac::detail::checkAnchors(ar,aq);if(ac::detail::readFlags(ar,aq)!=4)return 104;flagsAddress=ar.fields.back().address;if(!matchesSavedHealthy(originalUser,originalRecord,flagsAddress))return 109;
 std::array<unsigned char,10> src{};const auto& expected=nativeProfile->publisherEntry;if(!readLocal(client.base+nativeProfile->publisherRva,src.data(),src.size())||src!=expected)return 105;
 HANDLE thread=OpenThread(THREAD_QUERY_INFORMATION,FALSE,input.threadId);if(!thread)return 106;FILETIME ft{},ex{},kr{},us{};const bool okay=GetProcessIdOfThread(thread)==input.steamPid&&GetThreadTimes(thread,&ft,&ex,&kr,&us)&&((std::uint64_t(ft.dwHighDateTime)<<32)|ft.dwLowDateTime)==input.threadBirth;PWSTR desc=nullptr;const auto hr=GetThreadDescription(thread,&desc);const bool named=hr>=0&&desc&&std::wstring(desc)==L"IPC:CSteamEngine";if(desc)LocalFree(desc);CloseHandle(thread);if(!okay||!named)return 107;
 // Bracket the original target again before installing any dispatcher state.
 // Neither Prepare nor a later callback may silently elect a different tuple.
 rc::detail::checkSelection(r,q,originalUser);
 if(!(rc::detail::readHeader(r,originalUser.user,clientProfile)==originalHeader)||
    rc::detail::readTargets(r,q,originalHeader)!=targets||
    !(targets[0]==originalTarget)||!targetRolesMatch(originalTarget)||
    !identity()||!publicationRevocation.current())return 103;
 osd::PrepareRequest request{};request.size=sizeof request;request.abiVersion=osd::kAbiVersion;request.originalGetter=reinterpret_cast<osd::Getter>(client.base+nativeProfile->getterRva);const auto& getterBytes=rc::detail::profileLayout(clientProfile).getterBytes;std::memcpy(request.originalBytes,getterBytes.data(),getterBytes.size());request.expectedSelf=reinterpret_cast<void*>(originalUser.user+rc::detail::profileLayout(clientProfile).userInterfaceOffset);request.expectedAccount=input.account;request.expectedThreadId=input.threadId;request.expectedThreadBirth=input.threadBirth;request.deadlineTick=input.deadlineTick;request.nonce=input.nonce;request.callback=publish;request.cookie=nullptr;request.callbackContract=osd::kCallbackContractV1;prepared={};prepared.size=sizeof prepared;const auto code=OsdPrepare(&request,&prepared);if(code!=osd::Error::Ok)return 200+static_cast<DWORD>(code);live={};p.prepared=prepared;p.telemetryAddress=reinterpret_cast<std::uint64_t>(&live);preparationLifetime.accepted=true;return 0;
}
}
extern "C" __declspec(dllexport) DWORD WINAPI RunningTrialControl(void* data){auto*p=static_cast<tr::Packet*>(data);if(!p||p->size!=sizeof(*p)||p->version!=tr::abi||!tr::validQualificationRoute(p->qualificationRoute))return 99;ManagementGuard guard;if(p->op!=tr::Op::Prepare&&p->qualificationRoute!=bound.qualificationRoute)return 99;DWORD code=0;try{switch(p->op){case tr::Op::Prepare:code=prepare(*p);break;case tr::Op::Arm:
 if(!publicationRevocation.current()||!identity()){code=113;break;}
 code=static_cast<DWORD>(OsdArm(p->nonce));break;case tr::Op::Disarm:code=static_cast<DWORD>(OsdDisarm(p->nonce));if(!code){publicationRevocation.retire();roleRevoked.store(true,std::memory_order_release);}break;case tr::Op::Cleanup:// Retrying only a retained Close debt cannot reenter a publisher. The initial
 // successful dispatcher cleanup (or unarmed Prepare failure) already excluded
 // callbacks; keep the exact original nonce/route and explicit cleanup proof.
 if(roleCleanupPending){
  if(p->nonce.low!=bound.nonce.low||p->nonce.high!=bound.nonce.high||p->cleanupProof!=osd::kCleanupProof){code=114;break;}
 }else code=static_cast<DWORD>(OsdCleanupAfterRestore(p->nonce,p->cleanupProof));
 if(!code){publicationRevocation.retire();groupPublicationRevocation.retire();roleCleanupPending=!releaseRoleHandles();if(roleCleanupPending){code=114;break;}clearBoundIdentity();if(game.valid())CloseHandle(game.h);game.h=nullptr;if(steamSelf.valid())CloseHandle(steamSelf.h);steamSelf.h=nullptr;originalUser={};originalHeader={};originalTarget={};originalRecord=flagsAddress=0;clientCompatibility={};uiCompatibility={};nativeProfile.reset();}break;case tr::Op::Status:break;default:code=98;break;}}catch(...){code=97;}p->returnCode=code;p->status={};p->status.size=sizeof p->status;OsdGetStatus(&p->status);p->telemetry=live;return code;}




