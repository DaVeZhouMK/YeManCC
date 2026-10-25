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
SRWLOCK management=SRWLOCK_INIT;std::atomic<bool> preparationAttempted{false};struct ManagementGuard{ManagementGuard(){AcquireSRWLockExclusive(&management);}~ManagementGuard(){ReleaseSRWLockExclusive(&management);}};tr::Packet bound{};tr::Telemetry live{};rw::Module client,ui;std::optional<tr::NativeProfile> nativeProfile;rc::ProfileId clientProfile{};ac::ProfileId uiProfile{};ymcc::steamcompat::Qualification clientCompatibility,uiCompatibility;rw::Handle game,steamSelf,ymcc,cef;rc::detail::Selection originalUser;rc::detail::Header originalHeader;rc::detail::Target originalTarget;std::uint64_t originalRecord=0,flagsAddress=0;osd::PrepareResult prepared{};
bool legacyQualification(){return bound.qualificationRoute==tr::QualificationRoute::LegacyDynamic;}
// Cleanup clears identity, but the consumed DLL keeps its original wire route.
void clearBoundIdentity(){const auto route=bound.qualificationRoute;bound={};bound.qualificationRoute=route;}
bool readLocal(std::uint64_t a,void*p,std::size_t n){SIZE_T got=0;return ymcc::steamappstatewin::readableRegion(GetCurrentProcess(),a,n)&&ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(a),p,n,&got)&&got==n;}
bool identity(){return GetCurrentProcessId()==bound.steamPid&&rw::exactProcess(steamSelf.h,bound.steamPid,bound.steamBirth,bound.steamPath,bound.session)&&game.valid()&&rw::exactProcess(game.h,bound.gamePid,bound.gameBirth,bound.gamePath,bound.session)&&ymcc.valid()&&rw::exactProcess(ymcc.h,bound.ymcc.pid,bound.ymcc.birth,bound.ymcc.path,bound.ymcc.session)&&cef.valid()&&rw::exactProcess(cef.h,bound.cef.pid,bound.cef.birth,bound.cef.path,bound.cef.session);}
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
bool snapshot(bool before){try{const auto tick=GetTickCount64();if(!nativeProfile||!identity()||!tr::validQualificationRoute(bound.qualificationRoute))return false;const rc::Read read=legacyQualification()?rc::Read(readLocal):ymcc::steamcompat::adaptProcessRead(clientCompatibility,client.base,client.size,readLocal);rc::Query q{client.base,legacyQualification()?client.size:ymcc::steamcompat::referenceImageSize(clientCompatibility,client.size),bound.account,bound.gameId,bound.gamePid,true,clientProfile};rc::detail::Reader r(read);const auto s=rc::detail::selectUser(r,q);if(!(s==originalUser))return false;const auto h=rc::detail::readHeader(r,s.user,clientProfile);const auto targets=rc::detail::readTargets(r,q,h);if(targets.size()!=1)return false;const auto&t=targets[0];const auto record=h.records+rc::detail::profileLayout(clientProfile).recordStride*t.index;if(record!=originalRecord||t.primary!=bound.gamePid||t.foreground!=originalTarget.foreground)return false;rc::detail::checkSelection(r,q,s);if(!(rc::detail::readHeader(r,s.user,clientProfile)==h)||rc::detail::readTargets(r,q,h)!=targets)return false;
 const ac::Read uiRead=legacyQualification()?ac::Read(readLocal):ymcc::steamcompat::adaptProcessRead(uiCompatibility,ui.base,ui.size,readLocal);ac::Query aq{ui.base,legacyQualification()?ui.size:ymcc::steamcompat::referenceImageSize(uiCompatibility,ui.size),tr::shortcutAppId(bound.gameId),true,uiProfile};ac::detail::Reader ar(uiRead);ac::detail::checkAnchors(ar,aq);const auto flags=ac::detail::readFlags(ar,aq);if(ar.fields.empty()||ar.fields.back().address!=flagsAddress||!matchesSavedHealthy(s,record,flagsAddress))return false;std::array<unsigned char,14> entry{};if(!readLocal(client.base+nativeProfile->getterRva,entry.data(),entry.size())||std::memcmp(entry.data(),prepared.detourBytes,entry.size()))return false;
 if(before){live.flagsBefore=flags;live.primaryBefore=t.primary;live.foregroundBefore=t.foreground;}else{live.flagsAfter=flags;live.primaryAfter=t.primary;live.foregroundAfter=t.foreground;}return identity()&&GetTickCount64()-tick<100&&(!before||flags==(bound.noPublish?8196u:4u));
 }catch(...){return false;}}
osd::CallbackResult __fastcall publish(void*){
 live.sourceThread=GetCurrentThreadId();InterlockedExchange(&live.stage,1);if(!snapshot(true)){live.rejectCode=1;InterlockedExchange(&live.callbackResult,2);return osd::CallbackResult::Rejected;}InterlockedExchange(&live.guardBefore,1);
 if(!bound.noPublish){if(GetTickCount64()>=bound.deadlineTick){live.rejectCode=2;return osd::CallbackResult::Rejected;}InterlockedIncrement(&live.publisherCalls);using Publisher=void(__fastcall*)(void*,const std::uint64_t*,bool);auto call=reinterpret_cast<Publisher>(client.base+nativeProfile->publisherRva);const auto fullGameId=bound.gameId;call(reinterpret_cast<void*>(originalUser.user+0x3800),&fullGameId,true);InterlockedExchange(&live.publisherReturned,1);}
 const bool good=snapshot(false);InterlockedExchange(&live.guardAfter,good?1:0);InterlockedExchange(&live.stage,2);InterlockedExchange(&live.callbackResult,good?1:2);return good?osd::CallbackResult::Completed:osd::CallbackResult::Rejected;
}
DWORD prepare(tr::Packet&p){if(p.version!=tr::abi||p.size!=sizeof p||!tr::validQualificationRoute(p.qualificationRoute)||(!tr::shortcutAppId(p.gameId)||!ymcc::steamappstatewin::gameIdMatches(p.gameId,tr::shortcutAppId(p.gameId))||!tr::validPacketPaths(p))||!p.account||p.steamPid!=GetCurrentProcessId()||p.noPublish>1)return 100;if(!validHealthyProof(p.healthyProof)||p.noPublish||p.ymcc.session!=p.session||p.cef.session!=p.session)return 108;osd::Status prior{};prior.size=sizeof prior;OsdGetStatus(&prior);if(prior.state!=osd::State::Empty||prior.inFlight||preparationAttempted.exchange(true))return 200+static_cast<DWORD>(osd::Error::Busy);bound=p;if(steamSelf.valid())CloseHandle(steamSelf.h);steamSelf.h=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,GetCurrentProcessId());if(game.valid())CloseHandle(game.h);game.h=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,p.gamePid);if(ymcc.valid())CloseHandle(ymcc.h);ymcc.h=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,p.ymcc.pid);if(cef.valid())CloseHandle(cef.h);cef.h=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,p.cef.pid);if(!identity())return 101;if(!rw::exactModule(p.steamPid,tr::steamModulePath(p.steamPath,L"steamclient64.dll"),client)||!rw::exactModule(p.steamPid,tr::steamModulePath(p.steamPath,L"SteamUI.dll"),ui))return 102;
 // Fixed legacy qualification is independent of the content-contract route.
 // Exact known disk pair selects offsets; the original loaded getter, UI
 // anchors, publisher prologue and identity/pin guards below still must pass.
 if(legacyQualification()){
  const auto clientHash=diskHash(client.path),uiHash=diskHash(ui.path);
  nativeProfile=tr::profileForHashes(clientHash,uiHash);
  const auto record=rc::profileIdForSha256(clientHash);const auto state=ac::profileIdForSha256(uiHash);
  if(!nativeProfile||!record||!state)return 102;clientProfile=*record;uiProfile=*state;
 }else{
 clientCompatibility=ymcc::steamcompat::qualifyFile(client.path,ymcc::steamcompat::ModuleKind::Client,false);
 uiCompatibility=ymcc::steamcompat::qualifyFile(ui.path,ymcc::steamcompat::ModuleKind::Ui,false);
 if(!clientCompatibility.layout||!uiCompatibility.layout)return 102;
 nativeProfile=tr::profileForCompatibility(*clientCompatibility.layout,*uiCompatibility.layout);if(!nativeProfile)return 102;
 nativeProfile->getterRva=ymcc::steamcompat::resolvedRva(clientCompatibility,nativeProfile->getterRva);nativeProfile->publisherRva=ymcc::steamcompat::resolvedRva(clientCompatibility,nativeProfile->publisherRva);if(!nativeProfile->getterRva||!nativeProfile->publisherRva)return 102;
 clientProfile=*clientCompatibility.layout==ymcc::steamcompat::Layout::Public?rc::ProfileId::Public1792618285:rc::ProfileId::Legacy1791918634;
 uiProfile=*uiCompatibility.layout==ymcc::steamcompat::Layout::Public?ac::ProfileId::Public1792618285:ac::ProfileId::Legacy1791918634;
 const auto loadedRead=[](const rw::Module& module){return ymcc::steamcompat::ReadRva([module](std::uint64_t rva,void* target,std::size_t bytes){
   return rva<module.size&&bytes<=module.size-rva&&readLocal(module.base+rva,target,bytes);});};
 if(!ymcc::steamcompat::qualifyLoaded(clientCompatibility,ymcc::steamcompat::ModuleKind::Client,loadedRead(client),client.base,client.size,{},[](const ymcc::steamcompat::ImportSlot& slot,std::uint64_t target){return ymcc::steamcompat::resolvedImportTarget(GetCurrentProcess(),slot,target);}).matched||
    !ymcc::steamcompat::qualifyLoaded(uiCompatibility,ymcc::steamcompat::ModuleKind::Ui,loadedRead(ui),ui.base,ui.size,{},[](const ymcc::steamcompat::ImportSlot& slot,std::uint64_t target){return ymcc::steamcompat::resolvedImportTarget(GetCurrentProcess(),slot,target);}).matched||!identity())return 102;
 if(ymcc::steamcompat::resolvedRva(clientCompatibility,rc::detail::profileLayout(clientProfile).getterRva)!=nativeProfile->getterRva)return 102;
 }
 const rc::Read read=legacyQualification()?rc::Read(readLocal):ymcc::steamcompat::adaptProcessRead(clientCompatibility,client.base,client.size,readLocal);rc::Query q{client.base,legacyQualification()?client.size:ymcc::steamcompat::referenceImageSize(clientCompatibility,client.size),p.account,p.gameId,p.gamePid,true,clientProfile};rc::detail::Reader r(read);rc::detail::checkGetter(r,q);originalUser=rc::detail::selectUser(r,q);originalHeader=rc::detail::readHeader(r,originalUser.user,clientProfile);const auto targets=rc::detail::readTargets(r,q,originalHeader);if(targets.size()!=1||targets[0].primary!=p.gamePid||targets[0].foreground!=p.gamePid)return 103;originalTarget=targets[0];originalRecord=originalHeader.records+rc::detail::profileLayout(clientProfile).recordStride*originalTarget.index;const ac::Read uiRead=legacyQualification()?ac::Read(readLocal):ymcc::steamcompat::adaptProcessRead(uiCompatibility,ui.base,ui.size,readLocal);ac::Query aq{ui.base,legacyQualification()?ui.size:ymcc::steamcompat::referenceImageSize(uiCompatibility,ui.size),tr::shortcutAppId(bound.gameId),true,uiProfile};ac::detail::Reader ar(uiRead);ac::detail::checkAnchors(ar,aq);if(ac::detail::readFlags(ar,aq)!=4)return 104;flagsAddress=ar.fields.back().address;if(!matchesSavedHealthy(originalUser,originalRecord,flagsAddress))return 109;
 std::array<unsigned char,10> src{};const std::array<unsigned char,10> expected{0x48,0x8b,0xc4,0x48,0x81,0xec,0x88,0x01,0x00,0x00};if(!readLocal(client.base+nativeProfile->publisherRva,src.data(),src.size())||src!=expected)return 105;
 HANDLE thread=OpenThread(THREAD_QUERY_INFORMATION,FALSE,p.threadId);if(!thread)return 106;FILETIME ft{},ex{},kr{},us{};const bool okay=GetProcessIdOfThread(thread)==p.steamPid&&GetThreadTimes(thread,&ft,&ex,&kr,&us)&&((std::uint64_t(ft.dwHighDateTime)<<32)|ft.dwLowDateTime)==p.threadBirth;PWSTR desc=nullptr;const auto hr=GetThreadDescription(thread,&desc);const bool named=hr>=0&&desc&&std::wstring(desc)==L"IPC:CSteamEngine";if(desc)LocalFree(desc);CloseHandle(thread);if(!okay||!named)return 107;
 osd::PrepareRequest request{};request.size=sizeof request;request.abiVersion=osd::kAbiVersion;request.originalGetter=reinterpret_cast<osd::Getter>(client.base+nativeProfile->getterRva);std::memcpy(request.originalBytes,osd::kGetterBytes,14);request.expectedSelf=reinterpret_cast<void*>(originalUser.user+rc::detail::profileLayout(clientProfile).userInterfaceOffset);request.expectedAccount=p.account;request.expectedThreadId=p.threadId;request.expectedThreadBirth=p.threadBirth;request.deadlineTick=p.deadlineTick;request.nonce=p.nonce;request.callback=publish;request.cookie=nullptr;request.callbackContract=osd::kCallbackContractV1;prepared={};prepared.size=sizeof prepared;const auto code=OsdPrepare(&request,&prepared);if(code!=osd::Error::Ok)return 200+static_cast<DWORD>(code);live={};p.prepared=prepared;p.telemetryAddress=reinterpret_cast<std::uint64_t>(&live);return 0;
}
}
extern "C" __declspec(dllexport) DWORD WINAPI RunningTrialControl(void* data){auto*p=static_cast<tr::Packet*>(data);if(!p||p->size!=sizeof(*p)||p->version!=tr::abi||!tr::validQualificationRoute(p->qualificationRoute))return 99;ManagementGuard guard;if(p->op!=tr::Op::Prepare&&p->qualificationRoute!=bound.qualificationRoute)return 99;DWORD code=0;try{switch(p->op){case tr::Op::Prepare:code=prepare(*p);break;case tr::Op::Arm:code=static_cast<DWORD>(OsdArm(p->nonce));break;case tr::Op::Disarm:code=static_cast<DWORD>(OsdDisarm(p->nonce));break;case tr::Op::Cleanup:code=static_cast<DWORD>(OsdCleanupAfterRestore(p->nonce,p->cleanupProof));if(!code){clearBoundIdentity();if(game.valid())CloseHandle(game.h);game.h=nullptr;if(steamSelf.valid())CloseHandle(steamSelf.h);steamSelf.h=nullptr;originalUser={};originalHeader={};originalTarget={};originalRecord=flagsAddress=0;clientCompatibility={};uiCompatibility={};nativeProfile.reset();}break;case tr::Op::Status:break;default:code=98;break;}}catch(...){code=97;}p->returnCode=code;p->status={};p->status.size=sizeof p->status;OsdGetStatus(&p->status);p->telemetry=live;return code;}




