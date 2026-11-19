#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>
#include <functional>
#include <map>
#include <mutex>
#include <string>
// Test-only socket adapter: the production HTTPS allowlist still runs first.
// All network reads, HTTP framing and timeouts use the real WinHTTP engine.
static INTERNET_PORT fixturePort = 0;
static bool supervisedFixture = false;
static std::function<void()> afterRead;
static std::function<void()> afterPublication;
static HINTERNET WINAPI fixtureOpen(LPCWSTR agent, DWORD, LPCWSTR, LPCWSTR, DWORD flags) {
  return WinHttpOpen(agent, WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME,
                     WINHTTP_NO_PROXY_BYPASS, flags);
}
static HINTERNET WINAPI fixtureConnect(HINTERNET session, LPCWSTR, INTERNET_PORT, DWORD reserved) {
  return WinHttpConnect(session, L"127.0.0.1", fixturePort, reserved);
}
static HINTERNET WINAPI fixtureRequest(HINTERNET connection, LPCWSTR verb, LPCWSTR path,
    LPCWSTR version, LPCWSTR referer, LPCWSTR* types, DWORD flags) {
  return WinHttpOpenRequest(connection, verb, path, version, referer, types,
                            flags & ~WINHTTP_FLAG_SECURE);
}
static BOOL WINAPI fixtureTimeouts(HINTERNET handle, int, int, int, int) {
  const int timeout = supervisedFixture ? 15000 : 1000;
  return WinHttpSetTimeouts(handle, timeout, timeout, timeout, timeout);
}
static BOOL WINAPI fixtureOption(HINTERNET handle, DWORD option, LPVOID data, DWORD size) {
  if (option == WINHTTP_OPTION_RECEIVE_RESPONSE_TIMEOUT) {
    DWORD accelerated = supervisedFixture ? 15000 : 1000;
    return WinHttpSetOption(handle, option, &accelerated, sizeof accelerated);
  }
  return WinHttpSetOption(handle, option, data, size);
}
static BOOL WINAPI fixtureRead(HINTERNET request, LPVOID buffer, DWORD count, LPDWORD read) {
  const auto ok = WinHttpReadData(request, buffer, count, read);
  if (ok && *read && afterRead) { auto action = std::move(afterRead); afterRead = {}; action(); }
  return ok;
}
static BOOL WINAPI fixturePower(LPSYSTEM_POWER_STATUS status) {
  *status = {}; status->ACLineStatus = 1; status->BatteryLifePercent = 100; return TRUE;
}
#define WinHttpOpen fixtureOpen
#define WinHttpConnect fixtureConnect
#define WinHttpOpenRequest fixtureRequest
#define WinHttpSetTimeouts fixtureTimeouts
#define WinHttpSetOption fixtureOption
#define WinHttpReadData fixtureRead
#define GetSystemPowerStatus fixturePower
#include "shared_game_media.h"
namespace ymcc::media {
class ObservedFixtureStore : public Store {
public:
 using Store::Store;
 Json publish(const Json& args,const fs::path& source,bool manual){
  auto result=Store::publish(args,source,manual);
  if(!manual&&afterPublication)afterPublication();
  return result;
 }
};
}
#define Store ObservedFixtureStore
#include "shared_game_media_download_worker.h"
#undef Store
#undef WinHttpOpen
#undef WinHttpConnect
#undef WinHttpOpenRequest
#undef WinHttpSetTimeouts
#undef WinHttpSetOption
#undef WinHttpReadData
#undef GetSystemPowerStatus
#include "shared_game_media_download_supervisor.h"
#include <iostream>
using namespace ymcc::media;
using namespace ymcc::media::downloads;

static Json publicationGuards(const fs::path& root){
 int checks=0;const auto check=[&](bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);};
 const auto poster=root/L"guard.jpg",video=root/L"guard.mp4";{std::ofstream out(poster,std::ios::binary);out.write("\xff\xd8\xff\xe0",4);}{std::ofstream out(video,std::ios::binary);out.write("\0\0\0\x18" "ftypisom",12);}
 const Json target={{"appId",570},{"verified",true}};
 const auto seed=[&](Store& store,Queue& queue){auto e=store.registerEntry(target);e=store.publish(e,poster,true);e=store.publish(e,video,true);store.finish(e);e["action"]="select-video";e["selectedVideo"]={{"appId",570},{"url","https://cdn.akamai.steamstatic.com/store_trailers/570/preview.mp4"}};e=store.edit(e);e["action"]="redownload";e["slot"]="static";store.edit(e);queue.enqueue(Json::array({target}));const auto token=queue.start();queue.update(token,[](Json& j){j["workerPid"]=GetCurrentProcessId();j["workerBirth"]=processBirthToken(GetCurrentProcessId());j["jobs"]["steam-570"]["attempts"]=2;j["jobs"]["steam-570"]["automaticElapsedMs"]=1234;});queue.claim(token);return token;};
 Store store(root/L"data");Queue queue(store.root());const auto token=seed(store,queue);auto lease=store.begin(target);const auto published=store.publish(lease,poster,false);
 const auto refuse=[&](const std::string& session,const std::string& key,const Json& before,const Json& after,const char* why){const auto old=queue.document();const auto file=queue.root()/L"config"/L"game-media-downloads.json";const auto stamp=fs::last_write_time(file);check(!queue.advancePublishedBinding(session,key,before,after)&&queue.document()==old&&fs::last_write_time(file)==stamp,why);};
 refuse(randomId(),"steam-570",lease,published,"foreign session cannot acknowledge a publication");
 refuse(token,"steam-620",lease,published,"another key cannot acknowledge this publication");
 auto forged=published;forged["bindingRevision"]=forged["bindingRevision"].get<uint64_t>()+1;refuse(token,"steam-570",lease,forged,"unobserved revision cannot advance a request");
 forged=published;forged["taskId"]=randomId();refuse(token,"steam-570",lease,forged,"another task cannot borrow the Store receipt");
 auto foreign=lease;forged=published;foreign["leasePid"]=GetCurrentProcessId()+1;forged["leasePid"]=GetCurrentProcessId()+1;refuse(token,"steam-570",foreign,forged,"a different PID cannot acknowledge this worker's publication");
 foreign=lease;forged=published;foreign["leaseBirth"]="1";forged["leaseBirth"]="1";refuse(token,"steam-570",foreign,forged,"reused PID birth cannot borrow publication ownership");
 forged=published;forged["assets"]["static"]["id"]=randomId();refuse(token,"steam-570",lease,forged,"unpersisted asset metadata is refused");
 queue.update(token,[](Json& j){j["jobs"]["steam-570"]["provenanceRevision"]=1;});refuse(token,"steam-570",lease,published,"changed provenance fences a former active claim");queue.update(token,[](Json& j){j["jobs"]["steam-570"]["provenanceRevision"]=0;});
 check(queue.advancePublishedBinding(token,"steam-570",lease,published),"exact current owned Store publication advances once");const auto acknowledged=queue.document();
 check(acknowledged["jobs"]["steam-570"].value("requestBindingRevision",uint64_t{0})==published.value("bindingRevision",uint64_t{0})&&acknowledged["active"]["steam-570"]["requestBindingRevision"]==published["bindingRevision"]&&acknowledged["jobs"]["steam-570"].value("attempts",0)==2&&acknowledged["jobs"]["steam-570"].value("automaticElapsedMs",int64_t{0})==1234,"publication updates owned binding without refunding retries or time");
 refuse(token,"steam-570",lease,published,"duplicate publication acknowledgement is inert and writes nothing");
 queue.end(token,"deferred");const auto newer=queue.start();queue.claim(newer);refuse(token,"steam-570",lease,published,"late previous worker cannot acknowledge into a new session");queue.end(newer,"deferred");store.cancel(published);
 Store late(root/L"late");Queue lateQueue(late.root());const auto lateToken=seed(late,lateQueue);auto oldLease=late.begin(target);auto receipt=late.publish(oldLease,poster,false);auto edit=receipt;edit["action"]="rename";edit["name"]="newer user edit";late.edit(edit);const auto oldState=lateQueue.document();
 check(!lateQueue.advancePublishedBinding(lateToken,"steam-570",oldLease,receipt)&&lateQueue.document()==oldState,"real intervening edit fences a now stale Store receipt");lateQueue.end(lateToken,"deferred");
 Store failed(root/L"failed-unavailable");auto owned=failed.begin(target);auto unavailable=owned;unavailable["reason"]="media-http-404";unavailable["videoUnavailable"]=true;
 const auto document=read(failed.root()/L"config"/L"game-media.json");auto wrongFailure=unavailable;wrongFailure["taskId"]=randomId();failed.fail(wrongFailure);check(read(failed.root()/L"config"/L"game-media.json")==document,"foreign task cannot settle missing media or error debt");
 wrongFailure=unavailable;wrongFailure["bindingRevision"]=wrongFailure.value("bindingRevision",uint64_t{0})+1;failed.fail(wrongFailure);check(read(failed.root()/L"config"/L"game-media.json")==document,"stale revision cannot settle missing media or error debt");
 const auto result=failed.fail(unavailable);check(result.value("videoUnavailableUntil",int64_t{0})>0&&!result.value("staticUnavailableUntil",int64_t{0})&&result.value("error",std::string{})=="media-http-404"&&result.value("attempts",0)==1&&result.value("retryAt",int64_t{0})>0,"owned missing slot and real failure debt settle together");
 Store single(root/L"failed-single-slot");auto only=single.registerEntry(target);only=single.publish(only,video,true);only=single.begin(target);auto rejected=only;rejected["reason"]="media-http-404";rejected["videoUnavailable"]=true;const auto singleResult=single.fail(rejected);check(only["taskSlots"]==Json::array({"static"})&&!singleResult.value("videoUnavailableUntil",int64_t{0})&&!singleResult.value("staticUnavailableUntil",int64_t{0})&&singleResult["assets"]==only["assets"],"owned one-slot failure cannot mark unrequested cached media unavailable");
 return {{"ok",true},{"mode","publication-guards"},{"checks",checks},{"scope","actual Queue/Store owned publication and strict token/key/revision/task/PID/birth/provenance guards; no network or external process"}};
}
int wmain(int argc,wchar_t**argv){try{
 ymcc::powerSource::setExternalReader(+[]() noexcept {ymcc::powerSource::Snapshot s;s.readable=s.known=true;s.ac=1;s.status.ACLineStatus=1;s.status.SystemStatusFlag=0;return s;});
 struct ResetPower{~ResetPower(){ymcc::powerSource::setExternalReader(nullptr);}}resetPower;
 if(argc!=4)throw std::runtime_error("fresh isolated root, loopback port, case required");const auto root=fs::absolute(argv[1]);fixturePort=static_cast<INTERNET_PORT>(_wtoi(argv[2]));const std::string mode=utf8(argv[3]);
 const bool paired=mode=="pair-page-fails"||mode=="cached-pair-page-fails"||mode=="pair-page-recover"||mode=="pair-no-video-poster-fails"||mode=="pair-no-poster-video-fails";
 const bool cached=mode.starts_with("cached-static-")||mode.starts_with("cached-pair-")||mode.starts_with("slot-");const bool resume=mode=="cached-static-recover"||mode=="pair-page-recover";
 if(fs::exists(root)!=resume)throw std::runtime_error(resume?"existing owned recovery root required":"fresh root required");fs::create_directories(root);
 const auto resultPath=root/(cached||paired?wide("result-"+mode+".json"):L"result.json");if(fs::exists(resultPath))throw std::runtime_error("fresh result required");
 if(mode=="publication-guards"){auto report=publicationGuards(root);atomicWrite(resultPath,report);std::cout<<report.dump()<<std::endl;return 0;}
 Queue q(root);Store store(root);const Json target={{"appId",570},{"name","worker real transport regression"},{"verified",true}};Json original=Json::object();
 if(cached&&!resume){
  const auto video=root/L"retained.mp4",poster=root/L"retained.jpg";{std::ofstream out(video,std::ios::binary);out.write("\0\0\0\x18" "ftypisom",12);}{std::ofstream out(poster,std::ios::binary);out.write("\xff\xd8\xff\xe0",4);}
  auto entry=store.registerEntry(target);entry=store.publish(entry,poster,true);entry=store.publish(entry,video,true);store.finish(entry);original=store.resolve(target);atomicWrite(root/L"original-entry.json",original);
  if(mode=="cached-pair-fail"||mode=="cached-pair-page-fails"){entry["action"]="select-video";entry["selectedVideo"]={{"appId",570},{"url","https://cdn.akamai.steamstatic.com/store_trailers/570/preview.mp4"}};entry=store.edit(entry);}
  entry["action"]="redownload";entry["slot"]=mode.starts_with("slot-dynamic-")?"dynamic":"static";store.edit(entry);
 }else if(resume){
  original=read(root/L"original-entry.json");const auto current=q.document().value("session",std::string{});q.update(current,[](Json& j){j["jobs"]["steam-570"]["retryAt"]=0;});
  auto media=read(root/L"config"/L"game-media.json");media["entries"]["steam-570"]["retryAt"]=0;atomicWrite(root/L"config"/L"game-media.json",media); // fixture accelerates only existing deadlines
 }
 if(mode=="pair-page-fails"||mode=="pair-no-video-poster-fails"||mode=="pair-no-poster-video-fails"){original=store.resolve(target);atomicWrite(root/L"original-entry.json",original);}
 if(mode=="demo-manual-retry"){
  q.enqueue(Json::array({target,{{"appId",777},{"name","unrelated stopped job"},{"verified",true}}}));
  auto oldLease=store.begin(target);oldLease["staticUnavailable"]=true;oldLease["videoUnavailable"]=true;store.finish(oldLease);
  const auto oldToken=q.start();const auto claim=q.claim(oldToken);if(claim.is_null()||claim.value("key",std::string{})!="steam-570")throw std::runtime_error("old demo fixture claim failed");
  q.result(oldToken,"steam-570",true,"not-game-or-no-video");q.update(oldToken,[](Json& j){j["jobs"]["steam-777"]["attempts"]=3;j["jobs"]["steam-777"]["automaticElapsedMs"]=1234;j["jobs"]["steam-777"]["autoStopped"]=true;j["jobs"]["steam-777"]["state"]="unavailable";j["jobs"]["steam-777"]["lastError"]="media-http-502";});q.end(oldToken,"complete");
  original=store.resolve(target);atomicWrite(root/L"demo-stopped-before-retry.json",{{"entry",original},{"queue",q.document()}});q.requestSupplement(true,"steam-570");
 }
 q.enqueue(Json::array({target}));const auto token=q.start();const auto started=GetTickCount64();const auto limits=mode=="slow-bytes"||mode=="slow-metadata"||mode=="session"?Limits{90000,500,mode=="session"?1400ull:6000ull}:Limits{};
 HANDLE inventoryBegin=nullptr,inventoryTried=nullptr,inventoryDone=nullptr;std::thread inventory;
 std::atomic<bool> inventoryBlocked{false},inventoryEntered{false},publicationObserved{false};
 struct JoinInventory{std::thread& thread;HANDLE& begin;HANDLE& tried;HANDLE& done;~JoinInventory(){afterPublication={};if(begin)SetEvent(begin);if(thread.joinable())thread.join();for(const auto event:{begin,tried,done})if(event)CloseHandle(event);}}joinInventory{inventory,inventoryBegin,inventoryTried,inventoryDone};
 if(mode=="cached-pair-fail"){
  inventoryBegin=CreateEventW(nullptr,TRUE,FALSE,nullptr);inventoryTried=CreateEventW(nullptr,TRUE,FALSE,nullptr);inventoryDone=CreateEventW(nullptr,TRUE,FALSE,nullptr);
  if(!inventoryBegin||!inventoryTried||!inventoryDone)throw std::runtime_error("owned inventory event setup failed");
  inventory=std::thread([&]{if(WaitForSingleObject(inventoryBegin,5000)!=WAIT_OBJECT_0)return;
   const auto name=L"Local\\YMCC-SharedMedia-"+wide(digestKey(normalized(utf8(fs::absolute(root).wstring()))));HANDLE mutex=CreateMutexW(nullptr,FALSE,name.c_str());
   if(mutex){bool acquired=false;for(int n=0;n<20;++n){const auto state=WaitForSingleObject(mutex,0);if(state==WAIT_OBJECT_0||state==WAIT_ABANDONED){acquired=true;ReleaseMutex(mutex);break;}if(state!=WAIT_TIMEOUT)break;Sleep(10);}inventoryBlocked=!acquired;CloseHandle(mutex);}
   SetEvent(inventoryTried);try{q.enqueue(Json::array({target}));inventoryEntered=true;}catch(...){}SetEvent(inventoryDone);
  });
  afterPublication=[&]{if(publicationObserved.exchange(true))return;SetEvent(inventoryBegin);if(WaitForSingleObject(inventoryTried,2500)!=WAIT_OBJECT_0)throw std::runtime_error("owned inventory did not reach publication window");if(!inventoryBlocked.load()&&WaitForSingleObject(inventoryDone,2500)!=WAIT_OBJECT_0)throw std::runtime_error("owned inventory did not complete in unlocked publication window");};
 }

#ifdef YMCC_MEDIA_OLD_WORKER
 const auto code=Worker(root,token,GetCurrentProcess()).run();
#else
 const auto code=Worker(root,token,GetCurrentProcess(),{},{},limits).run();
#endif
if(inventory.joinable())inventory.join();afterPublication={};
const auto e=Store(root).resolve({{"appId",570}});const auto settled=q.document();
if(mode=="cached-pair-fail")for(int n=0;n<20;++n)q.enqueue(Json::array({target}));
Json report={{"ok",true},{"mode",mode},{"workerCode",code},{"elapsedMs",GetTickCount64()-started},{"entry",e},{"state",q.snapshot()},{"jobs",settled["jobs"]},{"afterRepeatedInventory",q.document()},{"originalEntry",original},{"publicationObserved",publicationObserved.load()},{"inventoryBlockedUntilAcknowledgement",inventoryBlocked.load()},{"concurrentInventoryCompleted",inventoryEntered.load()}};
atomicWrite(resultPath,report);std::cout<<report.dump()<<std::endl;return 0;
 }catch(const std::exception&e){std::cerr<<e.what()<<std::endl;return 2;}}
