#if defined(YMCC_MEDIA_NORMALIZATION_CONSTANT_ONLY)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <cstdlib>
#include <new>
#include <iostream>
#include <atomic>
static thread_local bool normalizationMeasure=false;
static thread_local unsigned long long normalizationAllocs=0,normalizationBytes=0;
void* operator new(std::size_t n){if(void* p=std::malloc(n?n:1)){if(normalizationMeasure){++normalizationAllocs;normalizationBytes+=n;}return p;}throw std::bad_alloc();}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}
#include "../native/shared_game_media.h"
using namespace ymcc::media;
static unsigned constantChecks=0;
static void constantCheck(bool value,const char* label){++constantChecks;if(!value)throw std::runtime_error(label);}
static unsigned long long threadCpu(){FILETIME c{},e{},k{},u{};constantCheck(GetThreadTimes(GetCurrentThread(),&c,&e,&k,&u)!=FALSE,"thread time");ULARGE_INTEGER a{},b{};a.LowPart=k.dwLowDateTime;a.HighPart=k.dwHighDateTime;b.LowPart=u.dwLowDateTime;b.HighPart=u.dwHighDateTime;return a.QuadPart+b.QuadPart;}
static Json constantEntry(int id){return {{"key","steam-"+std::to_string(id)},{"appId",id},{"bindingRevision",4},{"status","missing"},{"name","Fixture "+std::to_string(id)},{"assets",Json::object()},{"manualSlots",Json::object()},{"deletedSlots",Json::object()},{"invalidSlots",Json::object()},{"playback",{{"start",8},{"end",38},{"fit","cover"}}},{"playbackExplicit",false},{"modeExplicit",false},{"wallpaperMode","static"},{"unknownField",{{"preserved",true}}}};}
static std::string constantReadBytes(const fs::path& p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
int wmain(int argc,wchar_t** argv){try{
 constantCheck(argc==2||(argc==3&&std::wstring_view(argv[2])==L"--default-ownership-only"),"fresh owned root and known selection required");const fs::path root=fs::absolute(argv[1]);constantCheck(!fs::exists(root),"owned root must be fresh");fs::create_directories(root);Store store(root);const auto index=root/L"config"/L"game-media.json";Json costs=Json::array(),semantics=Json::array();

 if(argc==3){
  auto entry=constantEntry(570);entry.erase("playback");entry.erase("playbackExplicit");auto second=entry;second["key"]="steam-571";second["appId"]=571;atomicWrite(index,{{"schemaVersion",1},{"entries",{{"steam-570",entry},{"steam-571",second}}}});const auto disk=constantReadBytes(index);
  std::vector<fs::path> paths;for(int i=0;i<8;i++){const auto path=root/(L"concurrent-"+std::to_wstring(i));paths.push_back(path);atomicWrite(path/L"config"/L"game-media.json",{{"schemaVersion",1},{"entries",{{"steam-570",entry}}}});}
  std::vector<Json> results(8);std::vector<std::string> errors(8);std::atomic<unsigned> ready=0;std::atomic<bool> go=false;std::vector<std::thread> threads;
  for(int i=0;i<8;i++)threads.emplace_back([&,i]{ready.fetch_add(1);while(!go.load())std::this_thread::yield();try{results[i]=Store(paths[i]).resolve({{"appId",570}});}catch(const std::exception& e){errors[i]=e.what();}});
  while(ready.load()!=8)std::this_thread::yield();go.store(true);for(auto& t:threads)t.join();const Json expected={{"start",8},{"end",38},{"fit","cover"}};
  for(int i=0;i<8;i++)constantCheck(errors[i].empty()&&results[i]["playback"]==expected,"concurrent first-use default is complete and stable");
  results[0]["playback"]["start"]=99;for(int i=1;i<8;i++)constantCheck(results[i]["playback"]==expected,"returned record does not alias another record");
  auto first=store.resolve({{"appId",570}});constantCheck(first["playback"]==expected,"caller mutation cannot alter immutable default");first["playback"]["end"]=99;auto next=store.resolve({{"appId",571}});constantCheck(next["playback"]==expected,"other record receives independent default");constantCheck(store.resolve({{"appId",570}})["playback"]==expected,"subsequent read remains fresh and independent");constantCheck(constantReadBytes(index)==disk,"default ownership checks cannot write source index");
  std::cout<<Json({{"status","PASS_NORMALIZATION_DEFAULT_OWNERSHIP_ONLY"},{"checks",constantChecks},{"storeResolveCalls",11},{"threadResults",results},{"next",next},{"threadCount",8},{"sourceIndexUnchanged",true},{"networkOrAppControl",false}}).dump()<<'\n';return 0;
 }
 for(int count:{16,128,512}){Json entries=Json::object();for(int i=0;i<count;i++)entries["steam-"+std::to_string(570+i)]=constantEntry(570+i);atomicWrite(index,{{"schemaVersion",1},{"revision",7},{"entries",entries}});const auto disk=constantReadBytes(index);const Json request={{"appId",570}},expected=store.resolve(request);LARGE_INTEGER from{},to{},frequency{};QueryPerformanceFrequency(&frequency);QueryPerformanceCounter(&from);const auto cpu=threadCpu();normalizationAllocs=normalizationBytes=0;normalizationMeasure=true;std::vector<Json> results;results.reserve(12);for(int i=0;i<12;i++)results.push_back(store.resolve(request));normalizationMeasure=false;const auto usedCpu=threadCpu()-cpu;QueryPerformanceCounter(&to);const auto allocs=normalizationAllocs,bytes=normalizationBytes;for(const auto& result:results)constantCheck(result==expected,"repeated normalized result unchanged");constantCheck(constantReadBytes(index)==disk,"read-only resolve cannot rewrite index");costs.push_back({{"entries",count},{"resolveCalls",12},{"allocations",allocs},{"allocatedBytes",bytes},{"cpuMs",double(usedCpu)/10000.0},{"wallMs",double(to.QuadPart-from.QuadPart)*1000.0/double(frequency.QuadPart)},{"result",expected}});}
 const Json image={{"id","fixture-static"},{"kind","image"},{"extension","jpg"},{"mime","image/jpeg"},{"revision","1"},{"bytes",4}};const Json video={{"id","fixture-video"},{"kind","video"},{"extension","mp4"},{"mime","video/mp4"},{"revision","1"},{"bytes",12}};fs::create_directories(store.assetPath(image).parent_path());{std::ofstream file(store.assetPath(image),std::ios::binary);file<<"jpeg";}{std::ofstream file(store.assetPath(video),std::ios::binary);file<<"ftypisom";}
 for(const auto& kind:{"defaults","legacy-image","legacy-video","explicit-trim","deleted-video","invalid-video","static-fallback","alias"}){auto entry=constantEntry(570);if(std::string(kind)!="defaults")entry["asset"]=std::string(kind)=="legacy-image"?image:video;if(std::string(kind)=="legacy-image")entry["manual"]=true;if(std::string(kind)=="explicit-trim"){entry["playback"]={{"start",3},{"end",19},{"fit","contain"}};entry["playbackExplicit"]=true;}if(std::string(kind)=="deleted-video")entry["deletedSlots"]["dynamic"]=true;if(std::string(kind)=="invalid-video"){entry["assets"]["dynamic"]=video;entry["invalidSlots"]["dynamic"]="fixture-video";}if(std::string(kind)=="static-fallback"){entry["assets"]={{"static",image},{"dynamic",video}};entry["invalidSlots"]["dynamic"]="fixture-video";entry["wallpaperMode"]="dynamic";entry["modeExplicit"]=true;}Json document={{"schemaVersion",1},{"entries",{{"steam-570",entry}}}};if(std::string(kind)=="alias")document["aliases"]={{"steam-571","steam-570"}};atomicWrite(index,document);const auto before=constantReadBytes(index);const auto result=store.resolve({{"appId",std::string(kind)=="alias"?571:570}});constantCheck(result["unknownField"]["preserved"]==true,"unrelated metadata preserved");constantCheck(constantReadBytes(index)==before,"semantic read does not write");semantics.push_back({{"case",kind},{"result",result}});}
 for(const auto& bad:{std::string("{"),std::string("{\"schemaVersion\":2,\"entries\":{}}"),std::string("{\"entries\":[]}")}){fs::create_directories(index.parent_path());{std::ofstream file(index,std::ios::binary);file<<bad;}bool rejected=false;try{store.resolve({{"appId",570}});}catch(...){rejected=true;}constantCheck(rejected,"bad index remains rejected");constantCheck(constantReadBytes(index)==bad,"bad index remains unchanged");}
 std::cout<<Json({{"status","PASS_NORMALIZATION_PUBLIC_STORE_ONLY"},{"checks",constantChecks},{"costs",costs},{"semantics",semantics},{"sampleScope","Actual Store resolve and owned file IO, warm immutable literal, fixed12 lookups per16/128/512 entries"},{"networkOrAppControl",false},{"globalCpuPercent",nullptr}}).dump()<<'\n';return 0;
}catch(const std::exception& e){normalizationMeasure=false;std::cerr<<e.what()<<'\n';return 1;}}
#else
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#ifdef YMCC_IDENTITY_STORM_PORTS
// Owned-file fault ports for this original consumer's narrow identity mode.
#include <winsock2.h>
#include <windows.h>
#include <winioctl.h>
#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <map>
#include <vector>
#include <thread>
namespace identityFixture {
inline bool active=false;
inline std::wstring root,canonical;
inline std::string fault;
inline std::function<void()> beforeStage,afterCommit;
inline std::mutex mx;
inline std::map<HANDLE,std::wstring> handles;
inline unsigned faults=0,network=0,process=0,outside=0;
inline std::wstring clean(std::wstring x){if(x.starts_with(L"\\\\?\\"))x=x.substr(4);std::replace(x.begin(),x.end(),L'/',L'\\');for(auto& c:x)c=static_cast<wchar_t>(towlower(c));return x;}
inline HANDLE WINAPI createFile(LPCWSTR name,DWORD access,DWORD share,LPSECURITY_ATTRIBUTES security,DWORD disposition,DWORD attributes,HANDLE prototype){
 const auto file=clean(name);if(active&&!file.starts_with(clean(root)+L"\\")){++outside;SetLastError(ERROR_ACCESS_DENIED);return INVALID_HANDLE_VALUE;}
 if(active&&file.find(L".identity-next-")!=std::wstring::npos&&(access&GENERIC_WRITE)&&beforeStage){auto f=std::move(beforeStage);beforeStage={};f();}
 if(active&&fault=="create"&&file.find(L".identity-next-")!=std::wstring::npos){++faults;SetLastError(ERROR_ACCESS_DENIED);return INVALID_HANDLE_VALUE;}
 const auto h=CreateFileW(name,access,share,security,disposition,attributes,prototype);if(h!=INVALID_HANDLE_VALUE){std::lock_guard<std::mutex> guard(mx);handles[h]=file;}return h;
}
inline BOOL WINAPI closeHandle(HANDLE h){{std::lock_guard<std::mutex> guard(mx);handles.erase(h);}return CloseHandle(h);}
inline bool stage(HANDLE h){std::lock_guard<std::mutex> guard(mx);const auto i=handles.find(h);return i!=handles.end()&&i->second.find(L".identity-next-")!=std::wstring::npos;}
inline BOOL WINAPI readFile(HANDLE h,LPVOID p,DWORD n,LPDWORD read,LPOVERLAPPED over){bool canonicalRead=false;{std::lock_guard<std::mutex> guard(mx);const auto i=handles.find(h);canonicalRead=i!=handles.end()&&i->second==clean(canonical);}if(active&&fault=="read"&&canonicalRead){++faults;if(read)*read=0;SetLastError(ERROR_READ_FAULT);return FALSE;}return ReadFile(h,p,n,read,over);}
inline BOOL WINAPI writeFile(HANDLE h,LPCVOID p,DWORD n,LPDWORD written,LPOVERLAPPED over){if(active&&stage(h)&&(fault=="write"||fault=="short")){++faults;if(written)*written=0;SetLastError(ERROR_WRITE_FAULT);return fault=="short"?TRUE:FALSE;}return WriteFile(h,p,n,written,over);}
inline BOOL WINAPI flush(HANDLE h){if(active&&stage(h)&&fault=="flush"){++faults;SetLastError(ERROR_WRITE_FAULT);return FALSE;}return FlushFileBuffers(h);}
inline BOOL WINAPI move(LPCWSTR from,LPCWSTR to,DWORD flags){const bool commit=active&&clean(to)==clean(canonical)&&clean(from).find(L".identity-next-")!=std::wstring::npos;
 if(commit&&(fault=="rename"||fault=="false-ack")){++faults;SetLastError(ERROR_WRITE_FAULT);return fault=="false-ack"?TRUE:FALSE;}
 if(active&&fault=="backup"&&clean(to).find(L".identity-corrupt-")!=std::wstring::npos){++faults;SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
 const auto ok=MoveFileExW(from,to,flags);if(ok&&commit&&afterCommit){auto f=std::move(afterCommit);afterCommit={};f();}return ok;}
inline HANDLE WINAPI createMutex(LPSECURITY_ATTRIBUTES attr,BOOL owned,LPCWSTR name){if(active&&name&&std::wstring(name)==L"Local\\YeManCustomSteamLibraryDataTransaction")return CreateMutexW(attr,owned,(L"Local\\YMCC-LibraryIdentity-Owned-"+std::to_wstring(GetCurrentProcessId())).c_str());return CreateMutexW(attr,owned,name);}
inline HANDLE WINAPI openProcess(DWORD a,BOOL b,DWORD c){if(active){++process;SetLastError(ERROR_ACCESS_DENIED);return nullptr;}return OpenProcess(a,b,c);}
inline BOOL WINAPI createProcess(LPCWSTR a,LPWSTR b,LPSECURITY_ATTRIBUTES c,LPSECURITY_ATTRIBUTES d,BOOL e,DWORD f,LPVOID g,LPCWSTR h,LPSTARTUPINFOW i,LPPROCESS_INFORMATION j){if(active){++process;SetLastError(ERROR_ACCESS_DENIED);return FALSE;}return CreateProcessW(a,b,c,d,e,f,g,h,i,j);}
inline SOCKET WSAAPI socketPort(int a,int b,int c){if(active){++network;WSASetLastError(WSAEACCES);return INVALID_SOCKET;}return socket(a,b,c);}
}
#define CreateFileW identityFixture::createFile
#define CloseHandle identityFixture::closeHandle
#define ReadFile identityFixture::readFile
#define WriteFile identityFixture::writeFile
#define FlushFileBuffers identityFixture::flush
#define MoveFileExW identityFixture::move
#define CreateMutexW identityFixture::createMutex
#define OpenProcess identityFixture::openProcess
#define CreateProcessW identityFixture::createProcess
#define socket(...) identityFixture::socketPort(__VA_ARGS__)
#endif
#include "../native/shared_game_media_server.h"
#include "../native/shared_game_media_shortcut.h"
#include "../native/decky_sidebar_diagnostics.h"
#include <iostream>
#include <atomic>
using namespace ymcc::media;
static std::string httpGet(const std::string& url,const std::string& method="GET",const std::string& extra="",bool validToken=true,bool validHost=true){
 const auto slash=url.find('/',17);const auto port=std::stoi(url.substr(17,slash-17));auto target=url.substr(slash);if(!validToken)target=target.substr(0,target.find('?'));
 SOCKET socketHandle=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);sockaddr_in endpoint{};endpoint.sin_family=AF_INET;endpoint.sin_addr.s_addr=htonl(INADDR_LOOPBACK);endpoint.sin_port=htons(static_cast<unsigned short>(port));
 if(connect(socketHandle,reinterpret_cast<sockaddr*>(&endpoint),sizeof endpoint))throw std::runtime_error("fixture-http-connect");
 DWORD timeout=4000;setsockopt(socketHandle,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<char*>(&timeout),sizeof timeout);
 const auto request=method+" "+target+" HTTP/1.1\r\nHost: "+(validHost?"127.0.0.1":"untrusted.invalid")+":"+std::to_string(port)+"\r\nConnection: close\r\n"+extra+"\r\n";
 send(socketHandle,request.data(),static_cast<int>(request.size()),0);std::string result;char bytes[4096];for(;;){const int count=recv(socketHandle,bytes,sizeof bytes,0);if(count<=0)break;result.append(bytes,count);}closesocket(socketHandle);return result;
}

#ifdef YMCC_IDENTITY_STORM_PORTS
namespace identityStorm {
static unsigned assertions=0;
static void require(bool condition,const char* why){++assertions;if(!condition)throw std::runtime_error(why);}
static void bytes(const fs::path& path,const std::string& text){fs::create_directories(path.parent_path());std::ofstream out(custom_steam_library::ioPath(path),std::ios::binary|std::ios::trunc);out.write(text.data(),static_cast<std::streamsize>(text.size()));require(bool(out),"owned-file-write");}
static std::string bytes(const fs::path& path){std::ifstream in(custom_steam_library::ioPath(path),std::ios::binary);return {std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()};}
static Json match(int64_t id=555){return {{"appId",id},{"formalName","Owned Game"},{"primaryProvider","steam"},{"identityStatus","verified"},{"steamVerificationStatus","steam-verified"},{"resolverVersion","steam-official-fuzzy-global-v4"}};}
struct Fixture {fs::path root,manifest,cache;std::string exe;Store store;
 explicit Fixture(const fs::path& base):root(base/L"data"),manifest(base/L"worker.json"),exe(utf8((base/L"games"/L"Game.exe").wstring())),store(root){auto key=normalized(exe);for(auto& c:key)if(!std::isalnum(static_cast<unsigned char>(c))&&c!='-'&&c!='_')c='_';cache=root/L"cache"/L"identity"/wide(key+".json");identityFixture::canonical=cache.wstring();observation();}
 void observation(const Json& m=match()){bytes(manifest,Json{{"exe",exe},{"match",m},{"metadata",Json::object()}}.dump());}
 Json record(const Json& m=match())const{return {{"executable",exe},{"match",m},{"updatedAt",now()},{"steamAppId",m.value("appId",int64_t{0})},{"metadata",Json::object()}};}
 void saved(const Json& j){bytes(cache,j.dump());}
 Json remember(int64_t start=now()){return store.rememberIdentity(exe,manifest,start);}
 bool unknown()const{return !store.identity(exe).value("verified",false);}
 bool preserved(const std::string& raw)const{if(!fs::exists(cache.parent_path()))return false;for(const auto& e:fs::directory_iterator(cache.parent_path()))if(e.is_regular_file()&&e.path()!=cache&&bytes(e.path())==raw)return true;return false;}
};
static int run(const fs::path& requested,const std::string& selected){
 const auto root=fs::absolute(requested);if(fs::exists(root))throw std::runtime_error("fresh-identity-root-required");fs::create_directories(root);identityFixture::active=true;identityFixture::root=root.wstring();Json cases=Json::array();
 auto test=[&](const char* name,auto fn){if(!selected.empty()&&selected!=name)return;identityFixture::fault={};identityFixture::beforeStage={};identityFixture::afterCommit={};identityFixture::faults=0;Json row={{"name",name}};const auto start=assertions;try{Fixture f(root/std::to_wstring(cases.size()));fn(f);row["passed"]=true;}catch(const std::exception& e){row["passed"]=false;row["reason"]=e.what();}catch(...){row["passed"]=false;row["reason"]="unknown-exception";}row["assertions"]=assertions-start;cases.push_back(row);};
 test("identity-original-valid-roundtrip",[](auto& f){auto value=f.remember();require(value.value("verified",false)&&value.value("appId",0)==555,"valid-observation-not-reusable");require(f.store.identity(f.exe).value("appId",0)==555,"canonical-reread-failed");});
 test("identity-no-hints-grant-id",[](auto& f){f.observation({{"appId",555},{"formalName","Owned Game"}});require(!f.remember().value("verified",false)&&f.unknown(),"numeric-title-hints-granted-id");});
 test("identity-future-cache-unknown-preserved",[](auto& f){auto record=f.record();record["schemaVersion"]=99;f.saved(record);const auto raw=bytes(f.cache);require(f.unknown(),"future-cache-consumed");auto result=f.remember();require(!result.value("cachePersisted",false)&&bytes(f.cache)==raw,"future-cache-overwritten");});
 test("identity-future-manifest-unknown",[](auto& f){auto value=Json::parse(bytes(f.manifest));value["schemaVersion"]=99;bytes(f.manifest,value.dump());require(!f.remember().value("verified",false)&&f.unknown(),"future-worker-document-consumed");});
 test("identity-duplicate-authority-unknown",[](auto& f){bytes(f.cache,"{\"executable\":"+Json(f.exe).dump()+",\"match\":{},\"match\":"+match().dump()+"}");require(f.unknown(),"duplicate-authority-promoted");});
 test("identity-duplicate-manifest-unknown",[](auto& f){bytes(f.manifest,"{\"exe\":"+Json(f.exe).dump()+",\"match\":{},\"match\":"+match().dump()+"}");require(!f.remember().value("verified",false)&&f.unknown(),"duplicate-worker-authority-promoted");});
 test("identity-deep-cache-unknown",[](auto& f){auto record=f.record();Json nesting=0;for(int n=0;n<90;++n)nesting=Json::array({nesting});record["extension"]=nesting;f.saved(record);require(f.unknown(),"deep-cache-consumed");});
 test("identity-root-fields-types",[](auto& f){for(const char* key:{"updatedAt","steamAppId","metadata","manifestPath","schemaVersion"}){auto record=f.record();record[key]=Json::array();f.saved(record);require(f.unknown(),"wrong-root-field-consumed");}});
 test("identity-match-authority-types",[](auto&){for(const char* key:{"matchedContentAppId","baseGameAppId","source","query","formalName"}){auto m=match();m[key]=Json::array();require(!custom_steam_library::verifiedSteamIdentityMatch(m),"wrong-match-field-promoted");}});
 test("identity-future-timestamp-unknown",[](auto& f){auto record=f.record();record["updatedAt"]=now()+86400000;f.saved(record);require(f.unknown(),"future-observation-consumed");});
 test("identity-numeric-range-and-excluded",[](auto&){for(const auto& id:Json::array({-1,0,2147483648LL,18446744073709551615ULL,228980})){auto m=match();m["appId"]=id;require(!custom_steam_library::verifiedSteamIdentityMatch(m),"invalid-appid-promoted");}auto m=match();m["source"]="local-install-metadata-offline";require(!custom_steam_library::verifiedSteamIdentityMatch(m),"offline-title-promoted");});
 test("identity-invalid-executable-no-throw",[](auto& f){for(const auto& exe:std::vector<std::string>{"", "relative.exe", "C:relative.exe", "C:\\Games\\..\\Other.exe", std::string("C:\\bad")+static_cast<char>(static_cast<unsigned char>(255))+".exe"})require(!f.store.identity(exe).value("verified",false),"invalid-executable-accepted");});
 test("identity-cached-executable-mismatch",[](auto& f){auto record=f.record();record["executable"]=f.exe+".other";f.saved(record);require(f.unknown(),"other-executable-promoted");});
 test("identity-cache-path-collision-preserved",[](auto& f){auto record=f.record();record["executable"]=f.exe+".other";f.saved(record);const auto original=bytes(f.cache);auto value=f.remember();require(!value.value("cachePersisted",false)&&bytes(f.cache)==original,"foreign-binding-overwritten");});
 test("identity-manifest-target-mismatch-safe",[](auto& f){bytes(f.manifest,Json{{"exe",f.exe+".other"},{"match",match()}}.dump());require(!f.remember().value("verified",false)&&f.unknown(),"other-worker-target-promoted");});
 test("identity-preserve-corrupt-original-retry",[](auto& f){const std::string raw=std::string("{broken\0raw",11);bytes(f.cache,raw);require(f.unknown(),"corrupt-cache-promoted");auto result=f.remember();require(result.value("verified",false)&&result.value("cachePersisted",false)&&f.preserved(raw),"corrupt-original-not-preserved-and-rebuilt");require(f.store.identity(f.exe).value("appId",0)==555,"rebuilt-cache-not-reused");});
 test("identity-empty-and-root-shapes-rebuild",[](auto& f){for(const auto& raw:std::vector<std::string>{"","null","[]","7","{","true"}){bytes(f.cache,raw);require(f.unknown(),"bad-root-consumed");require(f.remember().value("verified",false)&&f.preserved(raw),"bad-root-not-preserved-before-rebuild");}});
 test("identity-oversize-preserve-rebuild",[](auto& f){const std::string raw(16*1024*1024+1,'x');bytes(f.cache,raw);require(f.unknown(),"oversize-cache-consumed");require(f.remember().value("verified",false)&&f.preserved(raw),"oversize-raw-bytes-not-preserved");});
 test("identity-readable-unknown-extension-retained",[](auto& f){auto record=f.record();record["extension"]={{"keep",7}};f.saved(record);require(f.remember().value("verified",false),"healthy-edit-failed");require(Json::parse(bytes(f.cache)).value("extension",Json::object()).value("keep",0)==7,"unknown-extension-lost");});
 test("identity-read-denial-does-not-overwrite",[](auto& f){f.saved(f.record());const auto raw=bytes(f.cache);const auto h=CreateFileW(f.cache.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr);require(h!=INVALID_HANDLE_VALUE,"deny-port-setup");Json result;try{require(f.unknown(),"denied-cache-promoted");result=f.remember();}catch(...){CloseHandle(h);throw;}CloseHandle(h);require(!result.value("cachePersisted",false)&&bytes(f.cache)==raw,"denied-cache-treated-missing");require(f.remember().value("verified",false),"read-denial-did-not-recover");});
 for(const auto fault:{"create","write","short","flush","rename","false-ack"})test((std::string("identity-write-fault-")+fault).c_str(),[fault](auto& f){identityFixture::fault=fault;auto result=f.remember();require(identityFixture::faults>0,"fault-port-not-reached");require(!result.value("cachePersisted",false)&&!result.value("verified",false)&&f.unknown(),"write-failure-reported-success");identityFixture::fault={};require(f.remember().value("verified",false),"explicit-write-retry-failed");});
 test("identity-backup-failure-preserves-and-retries",[](auto& f){const std::string raw="{broken";bytes(f.cache,raw);identityFixture::fault="backup";auto result=f.remember();require(identityFixture::faults>0&&!result.value("cachePersisted",false)&&bytes(f.cache)==raw,"backup-failure-overwrote-original");identityFixture::fault={};require(f.remember().value("verified",false)&&f.preserved(raw),"backup-release-retry-failed");});
 test("identity-concurrent-stage-change-refuses",[](auto& f){f.saved(f.record());auto foreign=f.record(match(777));foreign["extension"]="foreign";identityFixture::beforeStage=[&]{bytes(f.cache,foreign.dump());};auto result=f.remember();require(!identityFixture::beforeStage,"concurrent-port-not-reached");require(!result.value("cachePersisted",false)&&Json::parse(bytes(f.cache))["extension"]=="foreign","concurrent-new-snapshot-clobbered");require(f.remember().value("verified",false),"concurrent-retry-failed");});
 test("identity-canonical-readback-detects-change",[](auto& f){identityFixture::afterCommit=[&]{bytes(f.cache,"{");};auto result=f.remember();require(!identityFixture::afterCommit,"readback-port-not-reached");require(!result.value("cachePersisted",false)&&!result.value("verified",false),"changed-readback-reported-success");});
 test("identity-newer-cache-beats-old-observation",[](auto& f){auto record=f.record(match(777));record["updatedAt"]=now()+1000;f.saved(record);const auto raw=bytes(f.cache);auto result=f.remember(now()-2000);require(result.value("appId",0)==777&&bytes(f.cache)==raw&&!result.value("cachePersisted",false),"old-observation-overwrote-newer-cache");});
 test("identity-64-parallel-read-snapshots",[](auto& f){require(f.remember().value("verified",false),"parallel-seed");std::vector<Json> results(64);std::vector<std::thread> threads;for(size_t n=0;n<results.size();++n)threads.emplace_back([&,n]{results[n]=f.store.identity(f.exe);});for(auto& thread:threads)thread.join();for(const auto& result:results)require(result.value("appId",0)==555,"parallel-read-changed-identity");results[0]["appId"]=777;require(results[1]["appId"]==555&&f.store.identity(f.exe)["appId"]==555,"read-snapshots-not-independent");});
 test("identity-recovery-storm-no-fabrication",[](auto& f){for(int n=0;n<24;++n){bytes(f.cache,n%2?"{":"null");for(int i=0;i<8;++i)require(f.unknown(),"storm-bad-cache-promoted");require(f.remember().value("verified",false),"explicit-good-observation-did-not-recover");}require(f.store.identity(f.exe).value("appId",0)==555,"storm-final-reread");});

 test("identity-read-io-fault-is-not-corruption",[](auto& f){f.saved(f.record());const auto raw=bytes(f.cache);identityFixture::fault="read";require(f.unknown(),"failed-read-consumed");auto value=f.remember();require(identityFixture::faults>0&&!value.value("cachePersisted",false)&&bytes(f.cache)==raw&&!f.preserved(raw),"io-failure-reclassified-as-bad-json");identityFixture::fault={};require(f.remember().value("verified",false),"explicit-read-io-retry-failed");});
 test("identity-manifest-change-during-stage-refuses",[](auto& f){identityFixture::beforeStage=[&]{f.observation(match(777));};auto value=f.remember();require(!identityFixture::beforeStage,"manifest-race-port-not-reached");require(!value.value("verified",false)&&!value.value("cachePersisted",false)&&f.unknown(),"changed-worker-snapshot-published");require(f.remember().value("appId",0)==777,"new-worker-observation-could-not-retry");});
 test("identity-interruption-after-backup-retries",[](auto& f){const std::string raw="{interrupted";bytes(f.cache,raw);identityFixture::fault="rename";const auto value=f.remember();require(identityFixture::faults>0&&!value.value("cachePersisted",false)&&f.preserved(raw),"interrupted-publish-lost-original");require(f.unknown(),"interrupted-publish-fabricated-cache");identityFixture::fault={};require(f.remember().value("verified",false)&&f.preserved(raw),"post-interruption-retry-failed");});
 test("identity-hardlink-file-refused",[](auto& f){const auto other=f.root/L"owned-original.json";bytes(other,f.record().dump());fs::create_directories(f.cache.parent_path());require(CreateHardLinkW(f.cache.c_str(),other.c_str(),nullptr)!=FALSE,"owned-hardlink-setup");const auto raw=bytes(other);require(f.unknown(),"hardlink-cache-consumed");const auto value=f.remember();require(!value.value("cachePersisted",false)&&bytes(other)==raw&&bytes(f.cache)==raw,"hardlink-alias-overwritten");});
 test("identity-64-read-and-eight-writer-storm",[](auto& f){require(f.remember().value("verified",false),"writer-storm-seed");std::vector<std::thread> threads;std::vector<Json> readers(64),writers(8);const auto started=now();for(size_t n=0;n<writers.size();++n)threads.emplace_back([&,n]{try{writers[n]=f.remember(started);}catch(const std::exception& e){writers[n]={{"verified",false},{"error",e.what()}};}catch(...){writers[n]={{"verified",false},{"error","original-writer-unknown-exception"}};}});for(size_t n=0;n<readers.size();++n)threads.emplace_back([&,n]{readers[n]=f.store.identity(f.exe);});for(auto& thread:threads)thread.join();for(const auto& result:readers)require(result.empty()||result.value("appId",0)==555,"mixed-read-invented-identity");for(const auto& result:writers)require(result.value("appId",0)==555,"cooperating-writer-failed");require(f.store.identity(f.exe).value("appId",0)==555,"writer-storm-final-reread");});

 test("identity-invalid-cache-path-can-rebuild",[](auto& f){auto record=f.record();record["executable"]="relative.exe";f.saved(record);const auto raw=bytes(f.cache);require(f.unknown(),"relative-binding-consumed");require(f.remember().value("verified",false)&&f.preserved(raw),"bad-path-not-rebuildable");});
 test("identity-same-stat-new-bytes-refuse",[](auto& f){auto previous=f.record();f.saved(previous);const auto stamp=fs::last_write_time(f.cache);auto foreign=previous;foreign["match"]["appId"]=777;foreign["steamAppId"]=777;const auto foreignBytes=foreign.dump();identityFixture::beforeStage=[&]{bytes(f.cache,foreignBytes);fs::last_write_time(f.cache,stamp);};const auto value=f.remember();require(!identityFixture::beforeStage&&!value.value("cachePersisted",false)&&bytes(f.cache)==foreignBytes,"same-stat-writer-clobbered");});
 test("identity-concurrent-missing-now-created-refuses",[](auto& f){const auto foreign=f.record(match(777)).dump();identityFixture::beforeStage=[&]{bytes(f.cache,foreign);};const auto value=f.remember();require(!identityFixture::beforeStage&&!value.value("cachePersisted",false)&&bytes(f.cache)==foreign,"concurrent-creation-overwritten");});
 test("identity-directory-obstruction-refuses-retry",[](auto& f){fs::create_directories(f.cache);auto value=f.remember();require(!value.value("cachePersisted",false)&&fs::is_directory(f.cache)&&f.unknown(),"directory-obstruction-overwritten");fs::remove(f.cache);require(f.remember().value("verified",false),"obstruction-release-did-not-retry");});
 test("identity-junction-ancestor-refused",[](auto& f){
  const auto target=f.root/L"owned-junction-target",junction=f.root/L"cache";fs::create_directories(target/L"identity");fs::create_directories(junction);
  const auto substitute=L"\\??\\"+target.wstring();const auto printable=target.wstring();std::vector<unsigned char> buffer(16+(substitute.size()+1+printable.size()+1)*sizeof(wchar_t));
  const DWORD tag=IO_REPARSE_TAG_MOUNT_POINT;const WORD size=static_cast<WORD>(buffer.size()-8),zero=0,subSize=static_cast<WORD>(substitute.size()*sizeof(wchar_t)),printOffset=static_cast<WORD>((substitute.size()+1)*sizeof(wchar_t)),printSize=static_cast<WORD>(printable.size()*sizeof(wchar_t));
  memcpy(buffer.data(),&tag,4);memcpy(buffer.data()+4,&size,2);memcpy(buffer.data()+6,&zero,2);memcpy(buffer.data()+8,&zero,2);memcpy(buffer.data()+10,&subSize,2);memcpy(buffer.data()+12,&printOffset,2);memcpy(buffer.data()+14,&printSize,2);memcpy(buffer.data()+16,substitute.c_str(),(substitute.size()+1)*sizeof(wchar_t));memcpy(buffer.data()+16+printOffset,printable.c_str(),(printable.size()+1)*sizeof(wchar_t));
  const auto handle=CreateFileW(junction.c_str(),GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS,nullptr);require(handle!=INVALID_HANDLE_VALUE,"owned-junction-handle");DWORD returned=0;const bool ok=DeviceIoControl(handle,FSCTL_SET_REPARSE_POINT,buffer.data(),static_cast<DWORD>(buffer.size()),nullptr,0,&returned,nullptr)!=FALSE;CloseHandle(handle);require(ok,"owned-junction-setup");
  const auto actual=target/L"identity"/f.cache.filename();const auto raw=f.record().dump();bytes(actual,raw);require(f.unknown(),"reparse-ancestor-consumed");const auto value=f.remember();require(!value.value("cachePersisted",false)&&bytes(actual)==raw,"reparse-ancestor-target-overwritten");
 });
 unsigned passed=0;for(const auto& row:cases)if(row.value("passed",false))++passed;require(!cases.empty(),"selection-empty");Json result={{"schema","ymcc.library-identity-owned-consumer.v1"},{"success",passed==cases.size()&&identityFixture::network==0&&identityFixture::process==0&&identityFixture::outside==0},{"tests",cases.size()},{"passed",passed},{"assertions",assertions},{"cases",cases},{"networkOperations",identityFixture::network},{"processOperations",identityFixture::process},{"outsideRootIo",identityFixture::outside},{"scope","Original Store rememberIdentity and identity with real owned files; native socket/process ports inert; no installed application or worker launched"}};std::cout<<result.dump(2)<<'\n';identityFixture::active=false;return result["success"].get<bool>()?0:1;
}
}
#endif

static int normalizationCost(const fs::path& root){
 if(fs::exists(root))throw std::runtime_error("fresh-normalization-root-required");fs::create_directories(root/L"config");
 Json entries=Json::object();const Json dynamic={{"id",digestKey("normalization-video")},{"revision","1"},{"extension","mp4"},{"kind","video"},{"mime","video/mp4"},{"bytes",4096}},poster={{"id",digestKey("normalization-image")},{"revision","1"},{"extension","jpg"},{"kind","image"},{"mime","image/jpeg"},{"bytes",1024}};
 for(int id=1;id<=512;++id){const auto key="steam-"+std::to_string(id);Json e={{"key",key},{"appId",id},{"identityVerified",true},{"bindingRevision",uint64_t(id)},{"status","ready"},{"name","Recorded game "+std::to_string(id)},{"unknownMetadata",{{"source","retained"},{"labels",Json::array({"installed","known"})}}}};
  if(id%4==0){e["asset"]=dynamic;e["manual"]=true;}else if(id%4==1){e["assets"]={{"static",poster},{"dynamic",dynamic}};e["modeExplicit"]=true;e["wallpaperMode"]="static";}else if(id%4==2){e["asset"]=dynamic;e["deletedSlots"]={{"dynamic",true}};}else{e["playback"]={{"start",5},{"end",35},{"fit","contain"}};e["manualSlots"]={{"dynamic",true}};}entries[key]=std::move(e);
 }
 const Json input={{"schemaVersion",1},{"revision",7},{"updatedAt",0},{"entries",entries},{"aliases",{{"exe-legacy","steam-1"}}}};const auto file=root/L"config"/L"game-media.json";atomicWrite(file,input);const auto raw=input.dump();Store store(root);
 const auto cpu=[](){FILETIME a{},z{},k{},u{};if(!GetProcessTimes(GetCurrentProcess(),&a,&z,&k,&u))throw std::runtime_error("normalization-cpu-unavailable");ULARGE_INTEGER x{},y{};x.LowPart=k.dwLowDateTime;x.HighPart=k.dwHighDateTime;y.LowPart=u.dwLowDateTime;y.HighPart=u.dwHighDateTime;return x.QuadPart+y.QuadPart;};
 IO_COUNTERS a{},z{};if(!GetProcessIoCounters(GetCurrentProcess(),&a))throw std::runtime_error("normalization-io-unavailable");const auto started=GetTickCount64(),used=cpu();Json snapshot,result;
 for(int n=0;n<64;++n){snapshot=store.snapshot();result=store.resolve({{"appId",1+n%4}});if(snapshot["entries"].size()!=512||!result.contains("bindingRevision"))throw std::runtime_error("normalization-workload-result-invalid");}
 const auto spent=cpu()-used,elapsed=GetTickCount64()-started;if(!GetProcessIoCounters(GetCurrentProcess(),&z))throw std::runtime_error("normalization-io-unavailable");
 // Corrupt unrelated records must still reject a selected-entry read, and a
 // future schema cannot be accepted. Failed/pure reads never persist changes.
 auto corrupted=input;corrupted["entries"]["steam-512"]["playbackExplicit"]="invalid";atomicWrite(file,corrupted);bool rejected=false;try{store.resolve({{"appId",1}});}catch(...){rejected=true;}if(!rejected||read(file)!=corrupted)throw std::runtime_error("unrelated-malformed-entry-not-rejected");auto future=input;future["schemaVersion"]=99;atomicWrite(file,future);rejected=false;try{store.resolve({{"appId",1}});}catch(...){rejected=true;}if(!rejected||read(file)!=future)throw std::runtime_error("future-store-not-rejected");atomicWrite(file,input);
 auto next=input;next["entries"]["steam-1"]["bindingRevision"]=999;atomicWrite(file,next);if(store.resolve({{"appId",1}})["bindingRevision"]!=999)throw std::runtime_error("lookup-reused-an-older-operation");atomicWrite(file,input);if(read(file)!=input)throw std::runtime_error("read-mutated-input");
 const Json output={{"snapshot",snapshot},{"resolved",result}};atomicWrite(root/L"normalized-output.json",output);atomicWrite(root/L"input.json",input);const Json metrics={{"ok",true},{"items",512},{"cycles",64},{"snapshotCalls",64},{"resolveCalls",64},{"cpuMs",double(spent)/10000.0},{"elapsedMs",elapsed},{"readOperations",z.ReadOperationCount-a.ReadOperationCount},{"readBytes",z.ReadTransferCount-a.ReadTransferCount},{"writeOperations",z.WriteOperationCount-a.WriteOperationCount},{"writtenBytes",z.WriteTransferCount-a.WriteTransferCount},{"inputDigest",digestKey(raw)},{"outputDigest",digestKey(output.dump())},{"unrelatedCorruptionRejected",true},{"futureSchemaRejected",true},{"sequentialFreshReadPreserved",true},{"scope","fixed isolated Store reads and normalization; no installed CPU measurement"}};atomicWrite(root/L"normalization-cost.json",metrics);std::cout<<metrics.dump()<<std::endl;return 0;
}
// Explicit maintenance only: the caller supplies independent successful decode
// evidence. A held media file and the normal root mutex bind its hash to the CAS.
static std::string recoveryFileHash(HANDLE file){
 struct Hash {BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE value=nullptr;std::vector<unsigned char> object;~Hash(){if(value)BCryptDestroyHash(value);if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);}} hash;
 if(BCryptOpenAlgorithmProvider(&hash.algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)throw std::runtime_error("recovery-hash-unavailable");
 DWORD length=0,received=0;if(BCryptGetProperty(hash.algorithm,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&length),sizeof length,&received,0)<0)throw std::runtime_error("recovery-hash-unavailable");
 hash.object.resize(length);if(BCryptCreateHash(hash.algorithm,&hash.value,hash.object.data(),length,nullptr,0,0)<0)throw std::runtime_error("recovery-hash-unavailable");
 unsigned char buffer[65536];for(;;){DWORD count=0;if(!ReadFile(file,buffer,sizeof buffer,&count,nullptr))throw std::runtime_error("recovery-file-read-failed");if(!count)break;if(BCryptHashData(hash.value,buffer,count,0)<0)throw std::runtime_error("recovery-hash-failed");}
 unsigned char digest[32];if(BCryptFinishHash(hash.value,digest,sizeof digest,0)<0)throw std::runtime_error("recovery-hash-failed");std::string result;for(auto byte:digest){result+="0123456789abcdef"[byte>>4];result+="0123456789abcdef"[byte&15];}return result;
}
static int recoverDecodedPlayback(int argc,char** argv){
 if(argc!=8)throw std::runtime_error("usage: --recover-playback[-check] absolute-root canonical-key slot asset-id binding-revision verified-sha256");
 const bool apply=std::string(argv[1])=="--recover-playback";const auto root=fs::path(wide(argv[2]));
 if(!root.is_absolute()||!fs::is_directory(root))throw std::runtime_error("recovery-explicit-existing-root-required");
 const std::string key=argv[3],slot=argv[4],assetId=argv[5],rawRevision=argv[6],expectedHash=argv[7];
 if(rawRevision.empty()||!std::all_of(rawRevision.begin(),rawRevision.end(),[](char c){return c>='0'&&c<='9';})||expectedHash.size()!=64||!std::all_of(expectedHash.begin(),expectedHash.end(),[](char c){return c>='0'&&c<='9'||c>='a'&&c<='f';}))throw std::runtime_error("recovery-verified-identity-required");
 const auto revision=std::stoull(rawRevision);Lock transaction(root);Store store(root);const auto index=root/L"config"/L"game-media.json";
 const auto before=read(index);if(!before.value("entries",Json::object()).contains(key))throw std::runtime_error("recovery-entry-missing");const auto previous=before["entries"][key];
 if(previous.value("bindingRevision",uint64_t{0})!=revision||!previous.value("assets",Json::object()).contains(slot)||previous["assets"][slot].value("id",std::string{})!=assetId||previous.value("invalidSlots",Json::object()).value(slot,std::string{})!=assetId)throw std::runtime_error("recovery-verified-binding-changed");
 const auto path=store.assetPath(previous["assets"][slot]);struct File {HANDLE value=INVALID_HANDLE_VALUE;~File(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}} file;
 file.value=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);if(file.value==INVALID_HANDLE_VALUE)throw std::runtime_error("recovery-file-not-stable");
 const auto actualHash=recoveryFileHash(file.value);if(actualHash!=expectedHash)throw std::runtime_error("recovery-decoded-file-hash-mismatch");
 const auto entry=store.resolve({{"key",key}});if(entry.value("deleted",false)||entry.value("deletedSlots",Json::object()).value(slot,false)||entry.value("leaseUntil",int64_t{0})!=0||!entry.value("taskSlots",Json::array()).empty())throw std::runtime_error("recovery-state-not-quiescent");
 if(entry.value("key",std::string{})!=key||entry.value("bindingRevision",uint64_t{0})!=revision||entry.value("invalidSlots",Json::object()).value(slot,std::string{})!=assetId||entry.value("assets",Json::object())[slot].value("id",std::string{})!=assetId)throw std::runtime_error("recovery-verified-binding-changed");
 LARGE_INTEGER size{};if(!GetFileSizeEx(file.value,&size)||size.QuadPart<=0||static_cast<uint64_t>(size.QuadPart)!=previous["assets"][slot].value("bytes",uint64_t{0}))throw std::runtime_error("recovery-verified-size-changed");
 Json receipt={{"ok",true},{"applied",apply},{"root",utf8(root.wstring())},{"key",key},{"slot",slot},{"assetId",assetId},{"verifiedSha256",actualHash},{"before",previous},{"recordedAt",now()},{"scope","explicit decoded-file maintenance; no download or installed process control"}};
 if(apply){
  auto restored=store.edit({{"action","playback-recovered"},{"key",key},{"slot",slot},{"assetId",assetId},{"bindingRevision",revision}});const auto after=read(index);auto expected=before;
  expected["entries"][key]=after["entries"][key];for(const auto* field:{"revision","updatedAt","schemaVersion"})expected[field]=after[field];
  if(after!=expected||!restored.value(slot+"Ready",false)||restored.value("bindingRevision",uint64_t{0})!=revision+1||restored.value("assets",Json::object())[slot].value("id",std::string{})!=assetId)throw std::runtime_error("recovery-applied-but-readback-failed");
  receipt["after"]=after["entries"][key];receipt["otherEntriesAndMetadataUnchanged"]=true;receipt["beforeIndexRevision"]=before.value("revision",uint64_t{0});receipt["afterIndexRevision"]=after.value("revision",uint64_t{0});
 }else receipt["wouldInvoke"]={{"action","playback-recovered"},{"key",key},{"slot",slot},{"assetId",assetId},{"bindingRevision",revision}};
 std::cout<<receipt.dump(2)<<std::endl;return 0;
}
int main(int argc,char** argv){try{
 if(argc>1&&(std::string(argv[1])=="--recover-playback"||std::string(argv[1])=="--recover-playback-check"))return recoverDecodedPlayback(argc,argv);
 if(argc==3&&std::string(argv[1])=="--normalize-cost")return normalizationCost(fs::path(wide(argv[2])));
#ifdef YMCC_IDENTITY_STORM_PORTS
 if((argc==3||argc==4)&&std::string(argv[1])=="--identity-storm")return identityStorm::run(fs::path(wide(argv[2])),argc==4?argv[3]:"");
#endif
 if(argc==3&&std::string(argv[1])=="--inventory"){const auto root=fs::path(wide(argv[2]));return publishInventoryFile(root,root/L"state"/L"library-scan.json")?0:2;}
 if(argc==3&&std::string(argv[1])=="--lease"){Store(fs::path(wide(argv[2]))).begin({{"appId",333},{"name","dead-owner"}});return 0;}
 if(argc==4&&std::string(argv[1])=="--writer"){Store store(fs::path(wide(argv[2])));const int first=std::stoi(argv[3]);for(int i=0;i<12;i++)store.registerEntry({{"appId",first+i},{"name","concurrent fixture"}});return 0;}
 if(argc!=2)throw std::runtime_error("fixture-root-required");const auto fixtureRoot=fs::path(wide(argv[1]));fs::create_directories(fixtureRoot);Store store(fixtureRoot);int checks=0;
 const auto check=[&](bool ok,const char* label){++checks;if(!ok)throw std::runtime_error(label);};
 const auto mp4=fixtureRoot/L"sample.mp4",jpg=fixtureRoot/L"sample.jpg";
 {std::ofstream out(mp4,std::ios::binary);const char bytes[]={0,0,0,24,'f','t','y','p','i','s','o','m',0,0,0,0};out.write(bytes,sizeof bytes);}
 {std::ofstream out(jpg,std::ios::binary);const unsigned char bytes[]={0xff,0xd8,0xff,0xe0,0,0,0,0,0,0,0,0};out.write(reinterpret_cast<const char*>(bytes),sizeof bytes);}
 Json target={{"appId",570},{"name","Fixture Game"}};
 check(!store.resolve(target).value("ready",false),"starts missing");auto lease=store.begin(target);check(lease.value("acquired",false),"first owner acquires preparation");check(lease["playback"].value("start",0)==8&&lease["playback"].value("end",0)==38,"wire default matches8-second skip and30-second duration");auto same=store.begin(target);check(!same.value("acquired",true)&&same["taskId"]==lease["taskId"],"second request reuses lease");
 auto video=store.publish(lease,mp4,false);check(store.resolve(target).value("dynamicReady",false),"video is durable");auto pair=store.publish(video,jpg,false);check(pair["assets"].contains("static")&&pair["assets"].contains("dynamic"),"paired variants retained");check(pair["asset"]["kind"]=="video","auto selects dynamic when ready");store.finish(pair);
 auto mode=pair;mode["action"]="mode";mode["mode"]="static";auto staticMode=store.edit(mode);check(staticMode["asset"]["kind"]=="image","static selection active");check(staticMode["assets"]["dynamic"]==video["asset"],"mode switch retains video bytes");
 auto deletion=staticMode;deletion["action"]="delete";deletion["slot"]="dynamic";auto deleted=store.edit(deletion);check(deleted.value("staticReady",false)&&!deleted.value("dynamicReady",true),"delete only dynamic preserves static");check(!store.begin(target).value("acquired",true),"deleted dynamic not automatically recreated");
 bool stale=false;try{store.publish(lease,mp4,false);}catch(...){stale=true;}check(stale,"stale download cannot replace manual state");
 auto request=deleted;request["action"]="redownload";request["slot"]="dynamic";auto requested=store.edit(request);auto refreshed=store.begin(target);check(refreshed.value("acquired",false),"explicit dynamic refresh allowed");auto replacement=store.publish(refreshed,mp4,true);check(replacement["manualSlots"].value("dynamic",false),"manual replacement protected");store.finish(replacement);
 auto invalid=replacement;invalid["action"]="playback";invalid["start"]=10;invalid["end"]=2;bool badRange=false;try{store.edit(invalid);}catch(...){badRange=true;}check(badRange,"invalid trim range rejected");
 const auto existing=fixtureRoot/L"artwork"/L"overrides"/L"existing"/L"wallpaper.jpg";fs::create_directories(existing.parent_path());fs::copy_file(jpg,existing);
 const auto exe=std::string{"G:\\FixtureGames\\Existing\\game.exe"};atomicWrite(fixtureRoot/L"config"/L"manual-overrides.json",{{"items",{{normalized(exe),{{"wallpaper",{{"portableFile",utf8(existing.wstring())}}},{"artworkProtection",{{"wallpaper","manual"}}}}}}}});
 auto adopted=store.registerEntry({{"appId",999},{"name","Existing static"},{"executable",exe}});check(adopted["assets"].contains("static"),"existing library static cache reused");check(store.assetPath(adopted["assets"]["static"])==fs::weakly_canonical(existing),"no duplicate static file copy");check(adopted["manualSlots"].value("static",false),"existing static manual protection reused");
 auto r=parseRange("",100);check(r.valid&&r.last==99,"full range");r=parseRange("bytes=20-29",100);check(r.valid&&r.first==20&&r.last==29,"bounded range");r=parseRange("bytes=-10",100);check(r.valid&&r.first==90,"suffix range");check(!parseRange("bytes=100-",100).valid,"unsatisfiable range");check(!parseRange("bytes=0-1,3-4",100).valid,"multiple ranges rejected");check(!parseRange("bytes=18446744073709551616-",100).valid,"overflow rejected");check(!safeKey("../escape")&&!safeKey("x%2fy"),"path traversal rejected");
 Server server;const auto url=server.expose(store,replacement);auto response=httpGet(url);check(response.starts_with("HTTP/1.1 200")&&response.find("Content-Length: 12")!=std::string::npos,"actual full HTTP stream");
 // Current selected mode is static, so the source is the shared JPEG.
 response=httpGet(url,"GET","Range: bytes=2-5\r\n");check(response.starts_with("HTTP/1.1 206")&&response.find("Content-Range: bytes 2-5/12")!=std::string::npos,"actual HTTP byte range");check(response.size()-response.find("\r\n\r\n")-4==4,"HTTP range body length");
 response=httpGet(url,"HEAD");check(response.starts_with("HTTP/1.1 200")&&response.size()==response.find("\r\n\r\n")+4,"HEAD omits body");check(httpGet(url,"GET","Range: bytes=999-\r\n").starts_with("HTTP/1.1 416"),"HTTP416");check(httpGet(url,"GET","",false).starts_with("HTTP/1.1 403"),"token required");check(httpGet(url,"GET","",true,false).starts_with("HTTP/1.1 403"),"host bound to loopback");check(httpGet(url,"GET","Origin: https://untrusted.invalid\r\n").starts_with("HTTP/1.1 403"),"foreign origin rejected");
 HANDLE notifiedMain=CreateEventW(nullptr,TRUE,FALSE,nullptr),notifiedLibrary=CreateEventW(nullptr,TRUE,FALSE,nullptr);
 {Watcher mainWatcher(fixtureRoot,[&]{SetEvent(notifiedMain);});Watcher libraryWatcher(fixtureRoot,[&]{SetEvent(notifiedLibrary);},L"library");store.registerEntry({{"appId",777},{"name","watch fixture"}});check(WaitForSingleObject(notifiedMain,1500)==WAIT_OBJECT_0,"main receives change event");check(WaitForSingleObject(notifiedLibrary,1500)==WAIT_OBJECT_0,"library receives same change event");}CloseHandle(notifiedMain);CloseHandle(notifiedLibrary);
 wchar_t module[32768]{};GetModuleFileNameW(nullptr,module,32768);std::vector<PROCESS_INFORMATION> processes;
 for(int i=0;i<4;i++){auto command=L"\""+std::wstring(module)+L"\" --writer \""+fixtureRoot.wstring()+L"\" "+std::to_wstring(10000+i*100);STARTUPINFOW startup{};startup.cb=sizeof startup;PROCESS_INFORMATION process{};check(CreateProcessW(module,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process)!=FALSE,"concurrent writer starts");processes.push_back(process);}
 for(auto& process:processes){check(WaitForSingleObject(process.hProcess,10000)==WAIT_OBJECT_0,"concurrent writer completes");DWORD exitCode=1;GetExitCodeProcess(process.hProcess,&exitCode);check(exitCode==0,"concurrent writer succeeds");CloseHandle(process.hThread);CloseHandle(process.hProcess);}

 // Merge an EXE-local edit only after that same file has a confirmed Steam identity.
 const auto aliasExe=std::string{"G:\\FixtureGames\\Alias\\game.exe"};auto unbound=store.registerEntry({{"executable",aliasExe},{"name","local"}});auto localVideo=store.publish(unbound,mp4,true);auto merged=store.registerEntry({{"appId",555},{"verified",true},{"executable",aliasExe},{"name","confirmed"}});
 check(merged["assets"]["dynamic"]["id"]==localVideo["assets"]["dynamic"]["id"],"identity merge reuses same bytes");check(store.resolve({{"key",unbound["key"]}})["key"]=="steam-555","old key resolves through alias");check(!store.snapshot()["entries"].contains(unbound["key"].get<std::string>()),"alias merge has one record");
 // Confirming an EXE alias must not invalidate an unrelated active canonical download.
 const auto leaseExe=std::string{R"(G:\FixtureGames\PendingAlias\game.exe)"};auto canonicalLease=store.begin({{"appId",700}});auto fileLease=store.begin({{"executable",leaseExe}});
 auto leaseMerged=store.registerEntry({{"appId",700},{"executable",leaseExe},{"verified",true}});
 check(leaseMerged["taskId"]==canonicalLease["taskId"]&&leaseMerged["bindingRevision"]==canonicalLease["bindingRevision"],"metadata-only alias preserves current publication lease");
 auto concurrentVideo=store.publish(canonicalLease,mp4,false);check(concurrentVideo.value("dynamicReady",false),"in-flight canonical download survives identity merge");store.finish(concurrentVideo);
 check(store.resolve({{"key",fileLease["key"]}})["key"]=="steam-700","in-flight file request joins canonical identity");
 // An alias with actual edits takes effect atomically and releases the superseded lease.
 const auto editedExe=std::string{R"(G:\FixtureGames\EditedAlias\game.exe)"};auto edited=store.registerEntry({{"executable",editedExe}});edited=store.publish(edited,mp4,true);
 auto trim=edited;trim["action"]="playback";trim["start"]=5;trim["end"]=65;edited=store.edit(trim);
 auto failed=edited;failed["action"]="playback-failed";failed["slot"]="dynamic";failed["assetId"]=edited["assets"]["dynamic"]["id"];edited=store.edit(failed);
 auto superseded=store.begin({{"appId",701}});auto editedMerged=store.registerEntry({{"appId",701},{"executable",editedExe},{"verified",true}});
 check(editedMerged["playback"].value("start",0)==5&&editedMerged["playback"].value("end",0)==65,"alias keeps user trim across identity confirmation");
 check(editedMerged["invalidSlots"].value("dynamic",std::string{})==edited["assets"]["dynamic"].value("id",std::string{}),"failed manual video never revived by alias merge");
 check(editedMerged.value("leaseUntil",int64_t{0})==0&&editedMerged.value("taskId",std::string{}).empty(),"edited alias releases obsolete canonical lease");
 bool obsoleteRejected=false;try{store.publish(superseded,mp4,false);}catch(...){obsoleteRejected=true;}check(obsoleteRejected,"pre-edit canonical publisher rejected");
 auto editedRetry=store.begin({{"appId",701}});check(editedRetry.value("acquired",false),"remaining static fallback can start immediately");check(editedRetry["taskSlots"]==Json::array({"static"}),"manual invalid dynamic is not auto-overwritten");
 auto editedStatic=store.publish(editedRetry,jpg,false);check(editedStatic.value("ready",false)&&editedStatic["asset"]["kind"]=="image","merged invalid video uses newly prepared static fallback");store.finish(editedStatic);
 // Editing a time range protects configuration, without blocking first-time video collection.
 auto newTrim=store.registerEntry({{"appId",702}});newTrim["action"]="playback";newTrim["start"]=3;newTrim["end"]=63;newTrim=store.edit(newTrim);
 check(newTrim.value("playbackExplicit",false)&&!newTrim["manualSlots"].value("dynamic",false),"trim preferences do not claim ownership of absent video bytes");
 auto trimLease=store.begin({{"appId",702}});check(trimLease.value("acquired",false)&&std::find(trimLease["taskSlots"].begin(),trimLease["taskSlots"].end(),"dynamic")!=trimLease["taskSlots"].end(),"trim before download still prepares dynamic");auto trimmedVideo=store.publish(trimLease,mp4,false);check(trimmedVideo["playback"].value("start",0)==3&&trimmedVideo["playback"].value("end",0)==63,"download retains edited range");store.finish(trimmedVideo);
 // Existing static artwork can supersede an earlier download without retaining its lease.
 const auto adoptedExe=std::string{R"(G:\FixtureGames\AdoptWhilePending\game.exe)"};auto manualState=read(fixtureRoot/L"config"/L"manual-overrides.json");manualState["items"][normalized(adoptedExe)]={{"wallpaper",{{"portableFile",utf8(existing.wstring())}}},{"artworkProtection",{{"wallpaper","manual"}}}};atomicWrite(fixtureRoot/L"config"/L"manual-overrides.json",manualState);
 auto beforeAdoption=store.begin({{"appId",703}});auto pendingAdoption=store.registerEntry({{"appId",703},{"executable",adoptedExe},{"verified",true}});check(pendingAdoption.value("leaseUntil",int64_t{0})==0&&pendingAdoption.value("taskId",std::string{}).empty(),"adopted static releases outdated lease");check(pendingAdoption.value("staticReady",false),"adopted static is immediately usable");auto afterAdoption=store.begin({{"appId",703}});check(afterAdoption.value("acquired",false)&&afterAdoption["taskSlots"]==Json::array({"dynamic"}),"dynamic complement immediately follows static adoption");store.finish(afterAdoption);
 auto beforeImport=store.begin({{"appId",704}});auto manualImport=store.publish(beforeImport,jpg,true);check(manualImport.value("leaseUntil",int64_t{0})==0&&manualImport.value("taskId",std::string{}).empty(),"manual import releases obsolete publisher");auto afterImport=store.begin({{"appId",704}});check(afterImport.value("acquired",false)&&afterImport["taskSlots"]==Json::array({"dynamic"}),"manual static import can complete the other variant immediately");store.finish(afterImport);
 // Repair the older editor's trim-only/manual-without-video cache shape.
 auto oldTrim=store.registerEntry({{"appId",705}});auto legacyTrimState=store.snapshot();auto& legacyTrim=legacyTrimState["entries"]["steam-705"];legacyTrim["playback"]={{"start",5},{"end",35},{"fit","cover"}};legacyTrim.erase("playbackExplicit");legacyTrim["manualSlots"]["dynamic"]=true;atomicWrite(fixtureRoot/L"config"/L"game-media.json",legacyTrimState);
 auto repairedTrim=store.resolve({{"appId",705}});check(!repairedTrim["manualSlots"].value("dynamic",false)&&repairedTrim["playback"].value("start",0)==5,"legacy trim-only cache keeps range and releases nonexistent byte protection");auto repairedLease=store.begin({{"appId",705}});check(repairedLease.value("acquired",false),"legacy trim-only entry resumes initial collection");store.finish(repairedLease);
 // A crashed publisher is reclaimable immediately, with no ten-minute wait.
 auto leaseCommand=L"\""+std::wstring(module)+L"\" --lease \""+fixtureRoot.wstring()+L"\"";STARTUPINFOW leaseStartup{};leaseStartup.cb=sizeof leaseStartup;PROCESS_INFORMATION leaseProcess{};
 check(CreateProcessW(module,leaseCommand.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&leaseStartup,&leaseProcess)!=FALSE,"lease child starts");check(WaitForSingleObject(leaseProcess.hProcess,10000)==WAIT_OBJECT_0,"lease child exits");CloseHandle(leaseProcess.hThread);CloseHandle(leaseProcess.hProcess);
 auto reclaimed=store.begin({{"appId",333}});check(reclaimed.value("acquired",false),"dead owner reclaimed before deadline");store.finish(reclaimed);
 auto wrongBirth=store.snapshot();wrongBirth["entries"]["steam-333"]["leaseUntil"]=now()+600000;wrongBirth["entries"]["steam-333"]["leasePid"]=GetCurrentProcessId();wrongBirth["entries"]["steam-333"]["leaseBirth"]="wrong-creation";atomicWrite(fixtureRoot/L"config"/L"game-media.json",wrongBirth);check(store.begin({{"appId",333}}).value("acquired",false),"reused PID cannot retain old lease");
 // A renderer may invalidate only the exact delivered asset and revision.
 auto selected=store.resolve({{"appId",570}});auto bad=selected;bad["action"]="playback-failed";bad["slot"]="dynamic";bad["assetId"]=selected["assets"]["dynamic"]["id"];
 auto dynamicView=selected;dynamicView["asset"]=selected["assets"]["dynamic"];const auto oldVideoUrl=server.expose(store,dynamicView);
 auto failedVideo=store.edit(bad);check(!failedVideo.value("dynamicReady",true)&&failedVideo.value("staticReady",false),"bad video falls back to retained static");check(failedVideo["assets"].contains("dynamic"),"manual bad source remains recoverable");server.expose(store,failedVideo);check(httpGet(oldVideoUrl).starts_with("HTTP/1.1 404"),"invalidated asset URL revoked");
 bool staleError=false;try{store.edit(bad);}catch(...){staleError=true;}check(staleError,"old failure cannot invalidate a newer revision");
 // A successfully decoded retained file can be restored only by exact maintenance CAS.
 // This consumer uses synthetic headers; the incident's real decode/seek evidence is separate.
 {
  const auto recoveryRoot=fixtureRoot/L"playback-recovery";Store recovery(recoveryRoot);Server recoveryServer;
  auto prepared=recovery.begin({{"appId",1361510},{"name","TMNT retained-video recovery"},{"verified",true}});
  prepared=recovery.publish(prepared,mp4,false);prepared=recovery.publish(prepared,jpg,false);recovery.finish(prepared);
  auto original=recovery.resolve({{"appId",1361510}});const auto recoveryAsset=original["assets"]["dynamic"];
  const auto revokedUrl=recoveryServer.expose(recovery,original);
  auto report=original;report["action"]="playback-failed";report["slot"]="dynamic";report["assetId"]=recoveryAsset["id"];
  auto invalid=recovery.edit(report);recoveryServer.expose(recovery,invalid);
  check(!invalid.value("dynamicReady",true)&&invalid.value("staticReady",false),"retained current-game failure has static fallback");
  check(httpGet(revokedUrl).starts_with("HTTP/1.1 404"),"retained video token revoked before successful revalidation");
  const auto index=recoveryRoot/L"config"/L"game-media.json";
  const auto bytesAt=[](const fs::path& file){std::ifstream in(file,std::ios::binary);return std::string(std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>());};
  auto saved=recovery.snapshot();auto& retained=saved["entries"]["steam-1361510"];
  retained["refreshRequested"]=true;retained["refreshSlot"]="static";retained["manualSlots"]["static"]=true;
  retained["playback"]={{"start",3},{"end",42},{"fit","contain"}};retained["playbackExplicit"]=true;
  retained["selectedVideo"]={{"appId",1361510},{"url","https://fixture.invalid/selected.mp4"}};
  retained["unrelatedMetadata"]={{"keep",true}};saved["aliases"]["exe-recovery-alias"]="steam-1361510";atomicWrite(index,saved);
  saved["entries"]["steam-999999"]={{"key","steam-999999"},{"bindingRevision",7},{"legacyUnknown",{{"preserve",true}}}};atomicWrite(index,saved);
  const Json recover={{"action","playback-recovered"},{"key","steam-1361510"},{"slot","dynamic"},{"assetId",recoveryAsset["id"]},{"bindingRevision",invalid["bindingRevision"]}};
  const auto reject=[&](Json request,const char* label){const auto before=bytesAt(index);bool rejected=false;try{recovery.edit(request);}catch(...){rejected=true;}check(rejected,label);check(bytesAt(index)==before,"rejected recovery leaves index byte-identical");};
  auto staleRecovery=recover;staleRecovery["bindingRevision"]=invalid["bindingRevision"].get<uint64_t>()-1;reject(staleRecovery,"stale decoded recovery rejected");
  auto otherAsset=recover;otherAsset["assetId"]="different-asset";reject(otherAsset,"decoded recovery cannot clear another asset");
  auto otherSlot=recover;otherSlot["slot"]="static";reject(otherSlot,"dynamic decode does not authorize static recovery");
  auto noKey=recover;noKey.erase("key");noKey["appId"]=1361510;reject(noKey,"recovery requires explicit canonical key");
  auto aliasKey=recover;aliasKey["key"]="exe-recovery-alias";reject(aliasKey,"alias cannot stand in for exact recovery key");
  auto noSlot=recover;noSlot.erase("slot");reject(noSlot,"recovery never infers slot from current mode");
  auto noRevision=recover;noRevision.erase("bindingRevision");reject(noRevision,"recovery requires explicit binding revision");
  auto floatingRevision=recover;floatingRevision["bindingRevision"]=invalid["bindingRevision"].get<double>();reject(floatingRevision,"fractional representation cannot authorize recovery CAS");
  auto missingKey=recover;missingKey["key"]="steam-1361511";reject(missingKey,"missing recovery key cannot create an entry");
  auto deletedState=saved;deletedState["entries"]["steam-1361510"]["deleted"]=true;atomicWrite(index,deletedState);reject(recover,"deleted entry cannot be recovered by decode");atomicWrite(index,saved);
  deletedState=saved;deletedState["entries"]["steam-1361510"]["deletedSlots"]["dynamic"]=true;atomicWrite(index,deletedState);reject(recover,"deleted dynamic slot cannot be recovered by decode");atomicWrite(index,saved);
  auto replacedState=saved;replacedState["entries"]["steam-1361510"]["assets"]["dynamic"]["id"]="replacement-asset";atomicWrite(index,replacedState);reject(recover,"replacement asset cannot consume previous decode");atomicWrite(index,saved);
  auto preparingState=saved;preparingState["entries"]["steam-1361510"]["leaseUntil"]=now()+600000;preparingState["entries"]["steam-1361510"]["taskSlots"]=Json::array({"static"});preparingState["entries"]["steam-1361510"]["taskId"]="another-owner";atomicWrite(index,preparingState);reject(recover,"recovery does not take over active preparation");atomicWrite(index,saved);
  auto malformedUnrelated=saved;malformedUnrelated["entries"]["steam-999999"]["modeExplicit"]="malformed";atomicWrite(index,malformedUnrelated);reject(recover,"recovery retains full unrelated-entry normalization validation");atomicWrite(index,saved);
  const auto cachedPath=recovery.assetPath(recoveryAsset),heldPath=fs::path(cachedPath.wstring()+L".held");fs::rename(cachedPath,heldPath);reject(recover,"missing retained file cannot be marked recovered");fs::rename(heldPath,cachedPath);
  {std::ofstream append(cachedPath,std::ios::binary|std::ios::app);append.put('x');}reject(recover,"changed retained file size cannot consume decode");fs::copy_file(mp4,cachedPath,fs::copy_options::overwrite_existing);
  const auto queuePath=recoveryRoot/L"config"/L"game-media-downloads.json";atomicWrite(queuePath,{{"enabled",true},{"pauseReason","game-running"},{"workerPid",0},{"jobs",{{"steam-1361510",{{"state","pending"}}}}}});const auto queueBefore=bytesAt(queuePath);
  const auto fileBefore=bytesAt(cachedPath);auto restored=recovery.edit(recover);
  check(restored.value("dynamicReady",false)&&restored.value("staticReady",false)&&restored["asset"]["id"]==recoveryAsset["id"],"verified retained video restored without replacement download");
  check(restored["bindingRevision"].get<uint64_t>()==invalid["bindingRevision"].get<uint64_t>()+1,"successful recovery advances exact binding revision");
  check(restored["invalidSlots"].empty()&&restored.value("error",std::string{}).empty()&&restored.value("status",std::string{})=="ready","successful recovery clears only resolved playback failure");
  check(restored["assets"]==retained["assets"]&&bytesAt(cachedPath)==fileBefore,"recovery preserves retained asset identity and bytes");
  for(const auto* field:{"manualSlots","deletedSlots","refreshRequested","refreshSlot","playback","playbackExplicit","wallpaperMode","modeExplicit","selectedVideo","unrelatedMetadata","taskId","taskSlots","leaseUntil"})check(restored.value(field,Json())==retained.value(field,Json()),"recovery retains independent edits and download intent");
  check(bytesAt(queuePath)==queueBefore,"decode recovery does not alter game-running download policy or queue");
  check(read(index)["entries"]["steam-999999"]==saved["entries"]["steam-999999"],"recovery leaves unrelated legacy entry exactly unchanged");
  const auto newUrl=recoveryServer.expose(recovery,restored);check(httpGet(newUrl).starts_with("HTTP/1.1 200"),"restored retained video obtains fresh serving token");check(recovery.resolve({{"key","steam-1361510"}}).value("bindingRevision",uint64_t{0})==restored["bindingRevision"].get<uint64_t>(),"explicit successful recovery serves the current binding");
  reject(recover,"replayed recovery cannot consume old binding twice");auto replayCurrent=recover;replayCurrent["bindingRevision"]=restored["bindingRevision"];reject(replayCurrent,"already recovered slot cannot consume another recovery");
  auto twoInvalid=saved;auto& two=twoInvalid["entries"]["steam-1361510"];two["invalidSlots"]["static"]=two["assets"]["static"]["id"];atomicWrite(index,twoInvalid);auto partial=recovery.edit(recover);check(partial.value("dynamicReady",false)&&!partial.value("staticReady",true)&&partial["invalidSlots"].contains("static")&&partial.value("error",std::string{})=="playback-failed","one-slot recovery preserves other invalid slot and failure");
  auto otherFailure=saved;otherFailure["entries"]["steam-1361510"]["error"]="download-network-failed";atomicWrite(index,otherFailure);auto keptFailure=recovery.edit(recover);check(keptFailure.value("dynamicReady",false)&&keptFailure.value("error",std::string{})=="download-network-failed"&&keptFailure["retryAt"]==retained["retryAt"]&&keptFailure["attempts"]==retained["attempts"],"recovery never clears independent download failure or backoff");
  auto manualState=saved;manualState["entries"]["steam-1361510"]["manualSlots"]["dynamic"]=true;manualState["entries"]["steam-1361510"]["wallpaperMode"]="static";manualState["entries"]["steam-1361510"]["modeExplicit"]=true;atomicWrite(index,manualState);auto manualRestored=recovery.edit(recover);check(manualRestored.value("dynamicReady",false)&&manualRestored["manualSlots"].value("dynamic",false)&&manualRestored["asset"]["kind"]=="image","decode recovery retains manual ownership and explicit static selection");
 }
 // Cleanup excludes referenced media and original library artwork.
 const auto orphan=fixtureRoot/L"artwork"/L"video"/L"00000000000000000000000000000001.mp4";fs::copy_file(mp4,orphan);fs::last_write_time(orphan,fs::file_time_type::clock::now()-std::chrono::hours(48));const auto cleaned=store.collectOrphans();check(cleaned.value("removed",0)==1&&!fs::exists(orphan),"old owned orphan cleaned");check(fs::exists(store.assetPath(merged["assets"]["dynamic"]))&&fs::exists(existing),"cleanup retains live media and library original");
 check(shortcutAppId("4294967295")==UINT32_MAX,"full unsigned shortcut appid preserved");check(shortcutAppId(std::to_string((uint64_t{0xffffffff}<<32)|0x02000000ull))==UINT32_MAX,"full 64-bit GameID preserved");bool badId=false;try{shortcutAppId("18446744073709551615");}catch(...){badId=true;}check(badId,"unconfirmed GameID type rejected");
 response=httpGet(url,"OPTIONS","Origin: https://steamloopback.host\r\nAccess-Control-Request-Method: GET\r\nAccess-Control-Request-Private-Network: true\r\n");check(response.starts_with("HTTP/1.1 204")&&response.find("Access-Control-Allow-Private-Network: true")!=std::string::npos,"allowlisted CEF private-network preflight");check(httpGet(url,"OPTIONS","Origin: https://untrusted.invalid\r\nAccess-Control-Request-Method: GET\r\n").starts_with("HTTP/1.1 403"),"foreign preflight rejected");
 std::string vdf;const auto text=[&](const std::string& value){vdf+=value;vdf.push_back(0);};vdf.push_back(0);text("shortcuts");vdf.push_back(0);text("0");vdf.push_back(2);text("appid");for(int i=0;i<4;i++)vdf.push_back(char(0xff));vdf.push_back(1);text("appname");text("exact fixture");vdf.push_back(1);text("exe");text("\"G:\\FixtureGames\\Alias\\game.exe\"");vdf.push_back(8);vdf.push_back(8);vdf.push_back(8);
 const auto shortcutFile=fixtureRoot/L"shortcuts.vdf";{std::ofstream out(shortcutFile,std::ios::binary);out.write(vdf.data(),vdf.size());}const auto resolvedShortcut=shortcutIdentity(shortcutFile,std::to_string((uint64_t{0xffffffff}<<32)|0x02000000ull));check(resolvedShortcut.value("executable",std::string{})==aliasExe,"binary VDF exact GameID resolves EXE without titles");check(shortcutIdentity(shortcutFile,"4294967294").empty(),"other shortcut ID cannot reuse name");
 bool truncated=false;try{ShortcutVdf(vdf.substr(0,vdf.size()-1)).parse();}catch(...){truncated=true;}check(truncated,"truncated shortcut file fails closed");
 const auto identityManifest=fixtureRoot/L"verified-manifest.json";atomicWrite(identityManifest,{{"exe",aliasExe},{"match",{{"appId",555},{"primaryProvider","steam"},{"identityStatus","verified"},{"steamVerificationStatus","steam-verified"},{"resolverVersion","steam-official-fuzzy-global-v4"}}}});store.rememberIdentity(aliasExe,identityManifest,now());
 atomicWrite(fixtureRoot/L"state"/L"library-scan.json",{{"games",Json::array({{{"primaryExecutable",aliasExe},{"directoryName","confirmed file"}},{{"primaryExecutable","G:\\Games\\A.exe"},{"directoryName","unknown A"}},{{"primaryExecutable","G:\\Games\\B.exe"},{"directoryName","unknown B"}},{{"primaryExecutable","G:\\Games\\Tool.exe"},{"contentType","non-game"}},{{"primaryExecutable","G:\\Games\\Excluded.exe"}}})}});
 atomicWrite(fixtureRoot/L"config"/L"library-config.json",{{"manualBuckets",{{"G:\\Games\\Excluded.exe",{{"bucket","non-game"}}}}}});auto nullableState=read(fixtureRoot/L"state"/L"library-scan.json");nullableState["games"].push_back({{"primaryExecutable","G:/Games/Nullable.exe"},{"directoryName","nullable product"},{"primaryProductName",nullptr},{"contentType",nullptr},{"status",nullptr},{"gameDirectory",nullptr}});atomicWrite(fixtureRoot/L"state"/L"library-scan.json",nullableState);const auto inventory=store.inventory();check(inventory.size()==4,"inventory includes unidentified games while respecting excluded entries");check(inventory[0].value("verified",false)&&inventory[0].value("appId",0)==555,"inventory uses verified identity cache first");check(inventory[1].value("appId",-1)==0&&!inventory[1].value("executable",std::string{}).empty(),"unidentified file remains eligible for file resolver");
 check(inventory.back().value("name",std::string{})=="nullable product","nullable library product metadata falls back to directory name");
 // Enumerate all shortcuts in one bounded VDF read. Bad rows do not hide valid EXEs.
 const auto bulkShortcuts=fixtureRoot/L"shortcuts-inventory.vdf";
 const auto writeShortcuts=[&](const Json& rows){std::string data;const auto word=[&](const std::string& value){data+=value;data.push_back(0);};data.push_back(0);word("shortcuts");size_t ordinal=0;for(const auto& row:rows){data.push_back(0);word(std::to_string(ordinal++));data.push_back(2);word("appid");const auto id=row[0].get<uint32_t>();for(int i=0;i<4;i++)data.push_back(static_cast<char>(id>>(8*i)));data.push_back(1);word("appname");word("candidate only");data.push_back(1);word("exe");word(row[1].get<std::string>());data.push_back(8);}data.push_back(8);data.push_back(8);std::ofstream out(bulkShortcuts,std::ios::binary);out.write(data.data(),data.size());};
 writeShortcuts(Json::array({Json::array({0x90000000u,R"(G:\Games\AmbiguousA.exe)"}),Json::array({0x90000000u,R"(G:\Games\AmbiguousB.exe)"}),Json::array({0x90000001u,"https://invalid.example"}),Json::array({0x90000002u,"relative.exe"}),Json::array({0x90000003u,std::string(1,char(34))+R"(G:\Games\OnlyViaSteam\GAME.EXE)"+char(34)}),Json::array({0x90000004u,R"(G:\Tools\chrome.exe)"}),Json::array({0x90000005u,aliasExe}),Json::array({0x90000006u,R"(G:\Games\Tool.exe)"}),Json::array({0x90000007u,R"(G:\Games\Excluded.exe)"})}));
 const auto shortcutRows=shortcutInventory(bulkShortcuts);check(shortcutRows.size()==5,"bulk shortcuts skip ambiguous IDs, URLs and relative paths");
 bool duplicateShortcut=false;try{shortcutIdentity(bulkShortcuts,"2415919104");}catch(...){duplicateShortcut=true;}check(duplicateShortcut,"ambiguous shortcut never supplies file identity");
 check(shortcutInventoryForAccount(bulkShortcuts,17,[]{return 18u;}).empty(),"old account shortcuts never enter current inventory");int accountRead=0;check(shortcutInventoryForAccount(bulkShortcuts,17,[&]{return ++accountRead==1?17u:18u;}).empty(),"account change during VDF read drops shortcut snapshot");check(shortcutInventoryForAccount(bulkShortcuts,17,[]{return 17u;}).size()==5,"stable active account admits snapshot");
 const auto extendedInventory=store.inventory(shortcutRows);check(extendedInventory.size()==5,"all Steam-only games join local inventory exactly once");check(std::any_of(extendedInventory.begin(),extendedInventory.end(),[](const auto& e){return e.value("executable",std::string{})==R"(G:\Games\OnlyViaSteam\GAME.EXE)"&&!e.value("verified",true)&&e.value("appId",-1)==0;}),"shortcut display name is never confirmed Steam identity");
 Store noLibrary(fixtureRoot/L"without-library");check(noLibrary.inventory(shortcutRows).size()==4,"shortcut inventory works before the first YMCC library scan");
 // Library commits use a separate named event, with no idle polling or asset-event scan loop.
 HANDLE inventoryNotice=CreateEventW(nullptr,FALSE,FALSE,nullptr),assetNotice=CreateEventW(nullptr,FALSE,FALSE,nullptr);
 {Watcher both(fixtureRoot,[&]{SetEvent(assetNotice);},L"main",[&]{SetEvent(inventoryNotice);});
 check(!publishInventoryFile(fixtureRoot,fixtureRoot/L"config"/L"game-media.json"),"media-index writes never trigger inventory scans");check(!publishInventoryFile(fixtureRoot,fixtureRoot/L"other"/L"library-scan.json"),"same filename outside canonical state is ignored");check(WaitForSingleObject(inventoryNotice,0)==WAIT_TIMEOUT,"unrelated changes cause no inventory callback");
 const std::wstring quote(1,wchar_t(34));auto command=quote+module+quote+L" --inventory "+quote+fixtureRoot.wstring()+quote;STARTUPINFOW startup{};startup.cb=sizeof startup;PROCESS_INFORMATION publisher{};
 check(CreateProcessW(module,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&publisher)!=FALSE,"inventory publisher child starts");check(WaitForSingleObject(inventoryNotice,1500)==WAIT_OBJECT_0,"child library publication wakes main inventory consumer");check(WaitForSingleObject(publisher.hProcess,10000)==WAIT_OBJECT_0,"inventory child completes");DWORD result=1;GetExitCodeProcess(publisher.hProcess,&result);check(result==0,"cross-process inventory signal delivered");CloseHandle(publisher.hThread);CloseHandle(publisher.hProcess);
 check(WaitForSingleObject(assetNotice,0)==WAIT_TIMEOUT,"inventory signal never masquerades as asset update");store.registerEntry({{"appId",18899}});check(WaitForSingleObject(assetNotice,1500)==WAIT_OBJECT_0,"asset notifications retain their channel");check(WaitForSingleObject(inventoryNotice,0)==WAIT_TIMEOUT,"asset publication never schedules full library scan");
 }CloseHandle(inventoryNotice);CloseHandle(assetNotice);
 // A source download alone cannot promote a name candidate into a verified binding.
 auto candidateLease=store.begin({{"appId",18880},{"verified",false}});auto candidateVideo=store.publish(candidateLease,mp4,false);check(!candidateVideo.value("identityVerified",false),"downloaded bytes do not grant file identity");store.finish(candidateVideo);
 auto verifiedLease=store.begin({{"appId",18881},{"verified",true}});check(verifiedLease.value("identityVerified",false),"verified owner preserves confirmed identity");store.finish(verifiedLease);
 // Legacy reuse shares the held lease and preserves an existing static variant.
 auto legacyLease=store.begin({{"appId",18800},{"verified",true}});legacyLease=store.publish(legacyLease,jpg,false);auto legacyArgs=legacyLease;legacyArgs["verified"]=true;auto importedLegacy=store.importLegacy(legacyArgs,mp4,[]{return true;});check(importedLegacy.value("dynamicReady",false)&&importedLegacy.value("staticReady",false),"legacy video complements existing static instead of being skipped");check(importedLegacy["taskId"]==legacyLease["taskId"]&&importedLegacy["taskSlots"].empty(),"queued migration retains original lease and remaining slots");store.finish(importedLegacy);
 auto badLegacy=importedLegacy;badLegacy["action"]="playback-failed";badLegacy["slot"]="dynamic";badLegacy["assetId"]=importedLegacy["assets"]["dynamic"]["id"];auto failedLegacy=store.edit(badLegacy);auto repeatedLegacy=store.importLegacy({{"appId",18800},{"verified",true}},mp4,[]{return true;});check(repeatedLegacy["bindingRevision"]==failedLegacy["bindingRevision"]&&!repeatedLegacy.value("dynamicReady",true),"failed old source is not copied forever under new asset IDs");
 auto activeLegacy=store.begin({{"appId",18801},{"verified",true}});auto immediateLegacy=store.importLegacy({{"appId",18801},{"verified",true}},mp4,[]{return true;});check(immediateLegacy.value("dynamicReady",false),"local legacy video can bypass an unrelated network wait");check(store.resolve({{"appId",18801}}).value("leaseUntil",int64_t{0})==0,"standalone cache migration releases interrupted network lease");auto complementLegacy=store.begin({{"appId",18801}});check(complementLegacy.value("acquired",false)&&complementLegacy["taskSlots"]==Json::array({"static"}),"remaining static can prepare without stale-lease delay");store.finish(complementLegacy);
 auto deniedLegacy=store.begin({{"appId",18802},{"verified",true}});auto noMigration=store.importLegacy({{"appId",18802},{"verified",true}},mp4,[]{return false;});check(!noMigration.value("dynamicReady",false)&&noMigration["bindingRevision"]==deniedLegacy["bindingRevision"]&&noMigration["taskId"]==deniedLegacy["taskId"],"failed process admission never cancels another valid preparation");auto staleLegacy=deniedLegacy;staleLegacy["bindingRevision"]=0;bool rejectedLegacy=false;try{store.importLegacy(staleLegacy,mp4,[]{return true;});}catch(...){rejectedLegacy=true;}check(rejectedLegacy,"outdated migration lease cannot publish");store.finish(deniedLegacy);
 // A damaged per-file identity cache is a miss, then only a verified worker result repairs it.
 fs::path identityCache;for(const auto& item:fs::directory_iterator(fixtureRoot/L"cache"/L"identity"))if(stringField(read(item.path()),"executable")==aliasExe){identityCache=item.path();break;}check(!identityCache.empty(),"identity cache fixture exists");{std::ofstream corrupt(identityCache,std::ios::binary|std::ios::trunc);corrupt<<"{invalid";}
 check(store.identity(aliasExe).empty(),"corrupt identity cache cannot break inventory or grant identity");check(store.inventory(shortcutRows).size()==5,"one damaged identity cache does not hide other files");auto repairedIdentity=store.rememberIdentity(aliasExe,identityManifest,now());check(repairedIdentity.value("verified",false)&&repairedIdentity.value("appId",0)==555,"verified exact-file result repairs corrupt cache without deleting user settings");
 // Wallpaper diagnostics expose bounded aggregate facts, never game paths or tokens.
 const auto diagnostic=ymcc::deckydiag::record("wallpaper-inventory",{{"ok",true},{"targetCount",41u},{"shortcutCount",5u},{"verifiedCount",9u},{"reason","ready"},{"accountId","secret"},{"executable","G:/private/game.exe"},{"url","http://127.0.0.1/asset?token=secret"}});check(diagnostic.value("targetCount",0)==41&&diagnostic.value("verifiedCount",0)==9,"inventory diagnostics retain aggregate counts");check(!diagnostic.contains("accountId")&&!diagnostic.contains("executable")&&!diagnostic.contains("url"),"wallpaper diagnostics remove identities and access URLs");const auto badDiagnostic=ymcc::deckydiag::record("wallpaper-prepare",{{"stage","file://secret"},{"reason","https://private/?token=secret"},{"error","secret"},{"pid",123}});check(badDiagnostic.size()==1,"arbitrary details cannot masquerade as diagnostic reason or stage");
 auto snapshot=store.snapshot();for(int i=0;i<4;i++)for(int n=0;n<12;n++)check(snapshot["entries"].contains("steam-"+std::to_string(10000+i*100+n)),"cross-process update retained");

 // Disk admission is independent of cached reads and never deletes media.
 const capacity::Snapshot room{true,true,4*capacity::diskReserveBytes,0};
 check(!*capacity::reason(room,capacity::maxAssetBytes,true),"ordinary capacity permits a bounded asset");
 auto tight=room;tight.available=capacity::diskReserveBytes;
 check(std::string(capacity::reason(tight,1,true))=="media-disk-space-low","disk reserve stays available");
 tight.available=capacity::diskReserveBytes+capacity::maxAssetBytes;
 check(!*capacity::reason(tight,capacity::maxAssetBytes,true),"exact publication boundary fits");
 check(std::string(capacity::reason(tight,0,true,2*capacity::maxAssetBytes))=="media-disk-space-low","download admits peak temp and publication space");
 auto full=room;full.owned=capacity::automaticCacheBytes;
 check(std::string(capacity::reason(full,1,true))=="media-cache-budget-full","automatic assets respect shared cache budget");
 check(!*capacity::reason(full,1,false),"manual replacement is not blocked by automatic cache budget");
 full.cacheKnown=false;
 check(std::string(capacity::reason(full,1,true))=="media-cache-budget-unknown","bounded scan uncertainty pauses automatic downloads");
 check(!*capacity::reason(full,1,false),"scan uncertainty does not forbid explicit manual import");
 check(capacity::downloadFits(0,capacity::maxAssetBytes,capacity::maxAssetBytes),"exact byte cap accepted");
 check(!capacity::downloadFits(capacity::maxAssetBytes,1,capacity::maxAssetBytes),"unknown content length cannot stream beyond cap");
 check(!capacity::downloadFits(UINT64_MAX,1,capacity::maxAssetBytes),"download bound cannot wrap");
 check(capacity::downloadFits(UINT64_MAX,UINT64_MAX,0),"unspecified limit retains unrelated downloader behavior");
 check(capacity::ownedName(fs::path(L"0123456789abcdef0123456789abcdef.mp4")),"cache inventory counts exact managed asset filenames");
 check(!capacity::ownedName(fs::path(L"user-video.mp4")),"foreign filenames are outside automatic owned budget");
 const auto realCapacity=capacity::inspect(fixtureRoot);check(realCapacity.diskKnown&&realCapacity.cacheKnown&&realCapacity.owned>0,"production inspection reads actual fixture files and volume space");
 const auto boundedCapacity=capacity::inspect(fixtureRoot,0);check(!boundedCapacity.cacheKnown,"directory bound cannot silently grant more cache space");

 const auto jobs=fixtureRoot/L"cache"/L"jobs"/L"game-media";fs::create_directories(jobs);
 const auto oldPart=jobs/L"0123456789abcdef0123456789abcdef.part",oldFragment=jobs/L"1123456789abcdef0123456789abcdef.part.segment",recentPart=jobs/L"2123456789abcdef0123456789abcdef.part",foreignPart=jobs/L"user-video.part";
 for(const auto& file:{oldPart,oldFragment,recentPart,foreignPart}){std::ofstream(file)<<"fixture";}
 for(const auto& file:{oldPart,oldFragment,foreignPart})fs::last_write_time(file,fs::file_time_type::clock::now()-std::chrono::hours(25));
 const auto tempRemoved=capacity::collectTemporary(fixtureRoot);check(tempRemoved==2&&!fs::exists(oldPart)&&!fs::exists(oldFragment),"abandoned own download fragments are reclaimed after a day");
 check(fs::exists(recentPart)&&fs::exists(foreignPart),"active download and foreign file are preserved");
 check(capacity::collectTemporary(fixtureRoot)==0,"temporary cleanup is idempotent");

 const auto identityArgs=identityWorkerArguments("G:\\Fixture Game\\game.exe",fs::absolute(fixtureRoot/L"identity worker output"));
 check(identityArgs.size()==3&&identityArgs[0]=="G:\\Fixture Game\\game.exe"&&identityArgs[1]=="--steam-identity-only"&&identityArgs[2]==utf8(fs::absolute(fixtureRoot/L"identity worker output").wstring()),"identity worker uses positional output exactly as its real CLI parser expects");
 check(std::none_of(identityArgs.begin(),identityArgs.end(),[](const Json& value){return value=="--output";}),"unsupported output flag cannot return to the native identity request");

 // Real Range server boundary: browser source removal closes its own sockets.
 // Two slow readers must not leave the two media workers parked for timeout.
 uint64_t abortedStreamReleaseMs=0;
 {
  const auto large=fixtureRoot/L"abort-stream.mp4";
  {std::ofstream out(large,std::ios::binary);out.write("\0\0\0\x18" "ftypisom-fixture",20);std::array<char,65536> zeros{};for(int n=0;n<256;++n)out.write(zeros.data(),zeros.size());}
  auto lease=store.begin({{"appId",38801},{"name","stream abort fixture"}});auto asset=store.publish(lease,large,true);store.finish(asset);const auto endpointUrl=server.expose(store,asset);
  const auto slash=endpointUrl.find('/',17);const auto port=std::stoi(endpointUrl.substr(17,slash-17));
  std::array<SOCKET,2> held{INVALID_SOCKET,INVALID_SOCKET};
  struct CloseHeld{std::array<SOCKET,2>& held;~CloseHeld(){for(auto& socket:held)if(socket!=INVALID_SOCKET)closesocket(socket);}} closeHeld{held};
  for(auto& socket:held){socket=::socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);address.sin_port=htons(static_cast<unsigned short>(port));check(connect(socket,reinterpret_cast<sockaddr*>(&address),sizeof address)==0,"owned slow media socket connects");DWORD timeout=2000;setsockopt(socket,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<char*>(&timeout),sizeof timeout);const auto request="GET "+endpointUrl.substr(slash)+" HTTP/1.1\r\nHost: 127.0.0.1:"+std::to_string(port)+"\r\nConnection: close\r\n\r\n";send(socket,request.data(),static_cast<int>(request.size()),0);char first[512];check(recv(socket,first,sizeof first,0)>0,"slow reader starts an actual stream");}
  const auto start=GetTickCount64();for(auto& socket:held){linger reset{1,0};setsockopt(socket,SOL_SOCKET,SO_LINGER,reinterpret_cast<char*>(&reset),sizeof reset);closesocket(socket);socket=INVALID_SOCKET;}
  const auto response=httpGet(endpointUrl,"HEAD");abortedStreamReleaseMs=GetTickCount64()-start;
  check(response.starts_with("HTTP/1.1 200"),"aborted sources release media workers for next request");check(abortedStreamReleaseMs<2500,"media workers do not wait for five-second slow-client timeout after source close");
 }
 std::cout<<Json({{"ok",true},{"checks",checks},{"abortedStreamReleaseMs",abortedStreamReleaseMs},{"scope","production paired store, actual HTTP/aborted streams and cross-process writers; synthetic media headers"}}).dump()<<std::endl;return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}}

#endif // YMCC_MEDIA_NORMALIZATION_CONSTANT_ONLY
