#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>
#include <algorithm>
#include <string>
#include <map>
#include <vector>
#include <fstream>
// All transport handles below are heap objects in this process. URL parsing
// still uses production WinHTTP validation; no socket or session is opened.
namespace memory_wire {
struct Handle {std::wstring path,requestHeaders;std::string body;std::map<DWORD,std::string> headers;DWORD status=200;size_t at=0;bool video=false;};
static std::vector<std::wstring> ranges;
static int sidecarWrites=0,sidecarReplaces=0,failWriteAt=0,liveHandles=0;
static std::string lastSavedMetadata;
static bool modifiedValidator=false;
static bool replaceFailure=false,failRead=false;
static std::string movie(){
 const auto box=[](const std::string& type,const std::string& body){const auto n=static_cast<unsigned>(body.size()+8);std::string text;for(int shift:{24,16,8,0})text.push_back(static_cast<char>(n>>shift));return text+type+body;};
 return box("ftyp",std::string("isom")+std::string(12,'\0'))+box("moov",std::string(16,'\0'))+box("mdat",std::string(40,'q'));
}
static const std::string url="https://cdn.steamstatic.com/store_trailers/570/fixture.mp4";
static HINTERNET handle(){++liveHandles;return reinterpret_cast<HINTERNET>(new Handle);}
static HINTERNET WINAPI open(LPCWSTR,DWORD,LPCWSTR,LPCWSTR,DWORD){return handle();}
static HINTERNET WINAPI connect(HINTERNET,LPCWSTR,INTERNET_PORT,DWORD){return handle();}
static HINTERNET WINAPI request(HINTERNET,LPCWSTR,LPCWSTR path,LPCWSTR,LPCWSTR,LPCWSTR*,DWORD){auto h=handle();reinterpret_cast<Handle*>(h)->path=path;return h;}
static BOOL WINAPI close(HINTERNET h){delete reinterpret_cast<Handle*>(h);--liveHandles;return TRUE;}
static BOOL WINAPI timeouts(HINTERNET,int,int,int,int){return TRUE;}
static BOOL WINAPI option(HINTERNET,DWORD,LPVOID,DWORD){return TRUE;}
static BOOL WINAPI add(HINTERNET h,LPCWSTR text,DWORD,DWORD){reinterpret_cast<Handle*>(h)->requestHeaders=text;return TRUE;}
static BOOL WINAPI send(HINTERNET,LPCWSTR,DWORD,LPVOID,DWORD,DWORD,DWORD_PTR){return TRUE;}
static BOOL WINAPI receive(HINTERNET h,LPVOID){auto& r=*reinterpret_cast<Handle*>(h);
 if(r.path.find(L"/api/appdetails")!=std::wstring::npos){r.body=R"({"570":{"success":true,"data":{"steam_appid":570,"type":"game","name":"Memory fixture","movies":[{"mp4":{"720":")"+url+R"("}}]}}})";}
 else {r.video=true;r.body=movie();const auto start=r.requestHeaders.find(L"Range: bytes=");
  ranges.push_back(start==std::wstring::npos?L"":r.requestHeaders.substr(start));
  if(start!=std::wstring::npos){const auto offset=std::stoull(r.requestHeaders.substr(start+13));
   if(offset>=r.body.size()){r.status=416;r.headers[WINHTTP_QUERY_CONTENT_RANGE]="bytes */"+std::to_string(r.body.size());r.body.clear();}
   else {r.status=206;r.headers[WINHTTP_QUERY_CONTENT_RANGE]="bytes "+std::to_string(offset)+"-"+std::to_string(r.body.size()-1)+"/"+std::to_string(r.body.size());r.body=r.body.substr(offset);}}
  if(modifiedValidator){r.headers[WINHTTP_QUERY_LAST_MODIFIED]="Wed, 21 Oct 2015 07:28:00 GMT";r.headers[WINHTTP_QUERY_DATE]="Wed, 21 Oct 2015 07:30:00 GMT";}else r.headers[WINHTTP_QUERY_ETAG]="\"v1\"";
 }
 r.headers[WINHTTP_QUERY_CONTENT_LENGTH]=std::to_string(r.body.size());return TRUE;
}
static BOOL WINAPI query(HINTERNET h,DWORD key,LPCWSTR,LPVOID data,LPDWORD size,LPDWORD){auto& r=*reinterpret_cast<Handle*>(h);
 if(key==(WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER)){if(*size<sizeof(DWORD))return FALSE;*reinterpret_cast<DWORD*>(data)=r.status;*size=sizeof(DWORD);return TRUE;}
 auto it=r.headers.find(key);if(it==r.headers.end()){SetLastError(ERROR_WINHTTP_HEADER_NOT_FOUND);return FALSE;}
 const std::wstring text(it->second.begin(),it->second.end());const DWORD need=static_cast<DWORD>((text.size()+1)*sizeof(wchar_t));
 if(*size<need){*size=need;SetLastError(ERROR_INSUFFICIENT_BUFFER);return FALSE;}memcpy(data,text.c_str(),need);*size=need-sizeof(wchar_t);return TRUE;
}
static BOOL WINAPI read(HINTERNET h,LPVOID buffer,DWORD count,LPDWORD got){auto& r=*reinterpret_cast<Handle*>(h);
 if(r.video&&failRead&&r.at>=16){*got=0;SetLastError(ERROR_WINHTTP_CONNECTION_ERROR);return FALSE;}
 auto n=(std::min)(size_t(count),r.body.size()-r.at);if(r.video&&failRead)n=(std::min)(n,size_t(16));memcpy(buffer,r.body.data()+r.at,n);r.at+=n;*got=static_cast<DWORD>(n);return TRUE;
}
static BOOL WINAPI power(LPSYSTEM_POWER_STATUS p){*p={};p->ACLineStatus=1;return TRUE;}
static bool sidecar(HANDLE h){wchar_t name[32768]{};const auto n=GetFinalPathNameByHandleW(h,name,32768,FILE_NAME_NORMALIZED);return n&&n<32768&&std::wstring_view(name).find(L".resume.json.tmp.")!=std::wstring_view::npos;}
static BOOL WINAPI write(HANDLE h,LPCVOID data,DWORD size,LPDWORD done,LPOVERLAPPED overlap){
 if(sidecar(h)&&++sidecarWrites==failWriteAt){*done=0;SetLastError(ERROR_DISK_FULL);return FALSE;}return WriteFile(h,data,size,done,overlap);
}
static BOOL WINAPI replace(LPCWSTR from,LPCWSTR to,DWORD flags){
 const bool meta=std::wstring_view(to).ends_with(L".resume.json");
 if(meta){++sidecarReplaces;if(replaceFailure||(failWriteAt&&sidecarReplaces==failWriteAt)){SetLastError(replaceFailure?ERROR_ACCESS_DENIED:ERROR_DISK_FULL);return FALSE;}}
 const auto ok=MoveFileExW(from,to,flags);if(ok&&meta){std::ifstream in(to,std::ios::binary);lastSavedMetadata={std::istreambuf_iterator<char>(in),{}};}return ok;
}
}
#define WinHttpOpen memory_wire::open
#define WinHttpConnect memory_wire::connect
#define WinHttpOpenRequest memory_wire::request
#define WinHttpCloseHandle memory_wire::close
#define WinHttpSetTimeouts memory_wire::timeouts
#define WinHttpSetOption memory_wire::option
#define WinHttpAddRequestHeaders memory_wire::add
#define WinHttpSendRequest memory_wire::send
#define WinHttpReceiveResponse memory_wire::receive
#define WinHttpQueryHeaders memory_wire::query
#define WinHttpReadData memory_wire::read
#define GetSystemPowerStatus memory_wire::power
#define WriteFile memory_wire::write
#define MoveFileExW memory_wire::replace
#include "shared_game_media_download_worker.h"
#undef WinHttpOpen
#undef WinHttpConnect
#undef WinHttpOpenRequest
#undef WinHttpCloseHandle
#undef WinHttpSetTimeouts
#undef WinHttpSetOption
#undef WinHttpAddRequestHeaders
#undef WinHttpSendRequest
#undef WinHttpReceiveResponse
#undef WinHttpQueryHeaders
#undef WinHttpReadData
#undef GetSystemPowerStatus
#undef WriteFile
#undef MoveFileExW
#include <iostream>
using namespace ymcc::media;
static int checks=0;
static void check(bool value,const char* label){++checks;if(!value)throw std::runtime_error(label);}
static void file(const fs::path& p,const std::string& value){fs::create_directories(p.parent_path());std::ofstream out(p,std::ios::binary);out<<value;}

static std::string raw(const fs::path& p){std::ifstream in(p,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
static bool preserved(const fs::path& p,const std::string& bytes){
 if(fs::is_regular_file(p)&&raw(p)==bytes)return true;
 for(const auto& e:fs::directory_iterator(p.parent_path()))if(e.path().filename().wstring().starts_with(p.filename().wstring()+L".corrupt.")&&raw(e.path())==bytes)return true;
 return false;
}
static int sidecarStorm(const fs::path& base){
 using namespace downloads;using namespace memory_wire;
 if(fs::exists(base))throw std::runtime_error("fresh sidecar evidence directory required");fs::create_directories(base);Json results=Json::array();
 const std::string identity="steam-570\ndynamic\n"+url;
 const auto hex=[](std::string v){return std::string(16-v.size(),'0')+v;};const auto id=hex(digestKey(identity))+hex(digestKey("resume:"+identity));
 const Json valid={{"resumeProtocol",1},{"identity",identity},{"finalSource",url},{"validatorType","etag"},{"validator","\"v1\""},{"validatorDate",""},{"expectedSize",movie().size()},{"complete",false},{"unknown",{{"keep",7}}}};
 const Json target={{"appId",570},{"name","Memory fixture"},{"verified",true}};
 auto run=[&](const std::string& name,const std::function<void(const fs::path&,const fs::path&,const fs::path&)>& fn){const auto start=GetTickCount64();const auto root=base/wide(name);const auto part=root/L"cache"/L"jobs"/L"game-media"/wide(id+".part");auto meta=part;meta.replace_extension(L".resume.json");
  ranges.clear();modifiedValidator=false;lastSavedMetadata.clear();sidecarWrites=0;sidecarReplaces=0;failWriteAt=0;replaceFailure=false;failRead=false;
  try{fn(root,part,meta);check(liveHandles==0,"all in-memory transport handles released");results.push_back({{"case",name},{"status","PASS"},{"ms",GetTickCount64()-start}});}
  catch(const std::exception& e){results.push_back({{"case",name},{"status","FAIL"},{"error",e.what()},{"ms",GetTickCount64()-start}});}
  failWriteAt=0;replaceFailure=false;failRead=false;std::cout<<results.back().dump()<<std::endl;
 };
 auto consume=[&](const fs::path& root){Queue q(root);q.enqueue(Json::array({target}));const auto token=q.start();check(!token.empty(),"real queue admits verified fixture");const auto code=Worker(root,token,GetCurrentProcess()).run();return code;};
 std::vector<std::pair<std::string,std::string>> bad={{"broken-json","{\"resumeProtocol\":"},{"wrong-root","[]"},{"trailing-json",valid.dump()+" false"},{"oversized",std::string(65537,' ')}};
 for(const auto& [name,field,value]:std::vector<std::tuple<std::string,std::string,Json>>{
  {"wrong-protocol-type","resumeProtocol","1"},{"missing-validator","validator",""},{"wrong-validator-type","validator",42},{"weak-validator","validator","W/\"v1\""},{"control-validator","validator","\"v\t1\""},
  {"wrong-length-type","expectedSize","96"},{"negative-length","expectedSize",-1},{"too-large-length","expectedSize",capacity::maxAssetBytes+1},{"short-complete","complete",true},
  {"wrong-complete-type","complete","true"},{"wrong-identity","identity","steam-730\ndynamic\n"+url},{"wrong-source","finalSource","https://cdn.steamstatic.com/store_trailers/730/foreign.mp4"}}){auto j=valid;j[field]=value;bad.emplace_back(name,j.dump());}
 auto overrun=valid;overrun["expectedSize"]=8;bad.emplace_back("part-exceeds-length",overrun.dump());
 for(const auto& [name,text]:bad)run(name,[&](const fs::path& root,const fs::path& part,const fs::path& meta){const auto bytes=movie().substr(0,16);file(part,bytes);file(meta,text);consume(root);
  check(!ranges.empty()&&ranges.front().empty(),"invalid sidecar cannot authorize Range or If-Range");check(preserved(meta,text)&&preserved(part,bytes),"corrupt sidecar and original partial preserved");
  check(Store(root).resolve(target).value("dynamicReady",false),"fresh validated fake transfer actually publishes after recovery");
 });
 run("future-protocol",[&](const fs::path& root,const fs::path& part,const fs::path& meta){auto j=valid;j["resumeProtocol"]=99;file(part,movie().substr(0,16));file(meta,j.dump());const auto before=raw(meta),bytes=raw(part);fs::last_write_time(part,fs::file_time_type::clock::now()-std::chrono::hours(48));consume(root);
  check(raw(meta)==before&&raw(part)==bytes,"future protocol and partial remain byte exact");check(ranges.empty(),"future protocol is rejected before asset request");check(!Store(root).resolve(target).value("dynamicReady",false),"future protocol cannot fabricate ready media");
 });
 run("valid-resume-unknown",[&](const fs::path& root,const fs::path& part,const fs::path& meta){file(part,movie().substr(0,16));file(meta,valid.dump());consume(root);
  check(!ranges.empty()&&ranges.front().find(L"Range: bytes=16-")!=std::wstring::npos,"valid partial resumes through real Worker");check(downloads::Queue(root).snapshot().value("resumedBytes",uint64_t{0})==16,"resumed receipt reflects proven original bytes");
  check(Json::parse(lastSavedMetadata)["unknown"]==valid["unknown"],"unknown sidecar fields survive actual persistence");file(root/L"saved-sidecar-receipt.json",lastSavedMetadata);
 });
 run("last-modified-validator",[&](const fs::path& root,const fs::path& part,const fs::path& meta){auto j=valid;j["validatorType"]="last-modified";j["validator"]="Wed, 21 Oct 2015 07:28:00 GMT";j["validatorDate"]="Wed, 21 Oct 2015 07:30:00 GMT";modifiedValidator=true;file(part,movie().substr(0,16));file(meta,j.dump());consume(root);
  check(!ranges.empty()&&ranges.front().find(L"Range: bytes=16-")!=std::wstring::npos&&Store(root).resolve(target).value("dynamicReady",false),"legal strong Last-Modified resumes and publishes");
 });
 run("complete-416-revalidation",[&](const fs::path& root,const fs::path& part,const fs::path& meta){auto j=valid;j["complete"]=true;file(part,movie());file(meta,j.dump());consume(root);
  check(!ranges.empty()&&ranges.front().find(L"Range: bytes=96-")!=std::wstring::npos&&Store(root).resolve(target).value("dynamicReady",false),"complete checkpoint requires matching 416 validator and container proof");
 });
 run("old-orphan-ownership",[&](const fs::path& root,const fs::path& part,const fs::path& meta){file(part,movie().substr(0,16));file(meta,"{");fs::last_write_time(part,fs::file_time_type::clock::now()-std::chrono::hours(48));
  auto orphan=part.parent_path()/wide(std::string(32,'a')+".part");file(orphan,"old orphan");fs::last_write_time(orphan,fs::file_time_type::clock::now()-std::chrono::hours(48));
  WorkerContext context(root);const auto before=context.used;context.collectAbandoned(root);check(!fs::exists(orphan)&&fs::exists(part)&&context.used==before-10,"old unowned orphans reclaimed while corrupt checkpoint bytes remain budgeted");
 });
 run("budget-trim-protects-unconfirmed",[&](const fs::path& root,const fs::path& part,const fs::path& meta){file(part,movie().substr(0,16));file(meta,valid.dump());
  std::vector<fs::path> protectedParts;
  for(char kind:{'a','b','c'}){const auto foreign=part.parent_path()/wide(std::string(32,kind)+".part");auto sidecar=foreign;sidecar.replace_extension(L".resume.json");file(foreign,"owned evidence");auto data=valid;if(kind=='c')data["resumeProtocol"]=99;file(sidecar,kind=='b'?"{":data.dump());protectedParts.push_back(foreign);}
  WorkerContext context(root);context.used=WorkerContext::stagingLimit;context.trim(root);
  check(!fs::exists(part)&&context.used==WorkerContext::stagingLimit-16,"budget trim reclaims only a fully validated owned checkpoint");
  for(const auto& p:protectedParts)check(fs::exists(p),"corrupt future and identity-mismatched originals survive budget trim");
 });
 for(const auto mode:{"disk-full","atomic-replace-failure","interrupted-read"})run(mode,[&](const fs::path& root,const fs::path& part,const fs::path& meta){
  file(part,movie().substr(0,16));file(meta,valid.dump());if(std::string(mode)=="disk-full")failWriteAt=2;else if(std::string(mode)=="atomic-replace-failure")replaceFailure=true;else failRead=true;
  consume(root);failWriteAt=0;replaceFailure=false;failRead=false;Queue q(root);const auto failed=q.document();
  check(failed["jobs"]["steam-570"]["state"]!="done"&&!Store(root).resolve(target).value("dynamicReady",false),"failure never becomes a saved completed download");
  check(q.start().empty(),"failure preserves retry backoff");const auto due=(std::max)({failed["jobs"]["steam-570"].value("retryAt",int64_t{0}),failed.value("cooldownUntil",int64_t{0}),Store(root).resolve(target).value("retryAt",int64_t{0})});check(due>now(),"retry deadline retained");
  while(now()<=due)Sleep(20);
  const auto token=q.start();check(!token.empty(),"fault removal permits retry after original backoff");Worker(root,token,GetCurrentProcess()).run();
  check(Store(root).resolve(target).value("dynamicReady",false)&&q.document()["jobs"]["steam-570"]["state"]=="done","retry publishes and rereads actual fake media");
 });
 const bool ok=std::all_of(results.begin(),results.end(),[](const auto& r){return r["status"]=="PASS";});atomicWrite(base/L"results.json",{{"ok",ok},{"cases",results},{"checks",checks},{"scope","original Queue/Worker/Store; in-memory transport only; synthetic bytes; real isolated file IO; original 20s retry retained"}});return ok?0:1;
}
int wmain(int argc,wchar_t** argv){try{
 if(argc==3&&std::wstring_view(argv[1])==L"--storm")return sidecarStorm(fs::absolute(argv[2]));
 if(argc!=2)throw std::runtime_error("fresh C-drive evidence root required");
 const auto base=fs::absolute(argv[1]);if(fs::exists(base))throw std::runtime_error("evidence root already exists");
 check(custom_steam_library::lower(base.root_name().wstring())==L"c:","real file operations must be on the owned C-drive fixture");
 fs::create_directories(base);
 auto choose=[&](const std::string& label,bool volume){const auto lane=base/wide(label);fs::create_directories(lane);if(volume)fs::create_directories(lane/L"D-volume");return lane;};
 auto select=[](const fs::path& lane){return custom_steam_library::selectDefaultDataRoot(lane/L"package",lane/L"D-volume",lane/L"D-volume"/L"YeMan"/L"CustomSteamLibrary",lane/L"D-volume"/L"legacy",lane/L"user"/L"data");};
 auto lane=choose("c-only",false);auto selected=select(lane);check(selected==lane/L"package"/L"data","absent D selects portable C data");
 downloads::Queue queue(selected);queue.configure(false,"");check(queue.snapshot().value("enabled",true)==false,"C-drive queue state writes and reads");
 check(custom_steam_library::canCreateDataRoot(selected),"C data root writable");
 lane=choose("d-fresh",true);check(select(lane)==lane/L"D-volume"/L"YeMan"/L"CustomSteamLibrary"/L"data","available D remains default");
 lane=choose("d-blocked",true);file(lane/L"D-volume"/L"YeMan","blocked parent");check(select(lane)==lane/L"package"/L"data","blocked fresh D falls back to portable C");
 lane=choose("c-package-blocked",false);file(lane/L"package"/L"data","not a directory");check(select(lane)==lane/L"user"/L"data","blocked fresh portable root falls back to user C data");
 fs::create_directories(lane/L"user"/L"data");check(select(lane)==lane/L"user"/L"data","user C fallback is stable on next resolution");
 lane=choose("all-blocked",false);file(lane/L"package"/L"data","blocked");file(lane/L"user","blocked");bool failed=false;try{select(lane);}catch(const std::exception& e){failed=std::string(e.what())=="library-data-root-unwritable";}check(failed,"all blocked emits actionable storage error");
 lane=choose("existing-local",true);file(lane/L"package"/L"data"/L"state"/L"library-scan.json",std::string(80,' '));check(select(lane)==lane/L"package"/L"data","existing C library retained when D appears");
 lane=choose("existing-d",true);fs::create_directories(lane/L"D-volume"/L"YeMan"/L"CustomSteamLibrary"/L"data");check(select(lane)==lane/L"D-volume"/L"YeMan"/L"CustomSteamLibrary"/L"data","existing D data not silently forked");
 const auto capacity=capacity::inspect(selected);check(capacity.diskKnown,"C storage capacity detected using real filesystem");
 capacity::Snapshot unavailable;check(std::string(capacity::reason(unavailable,1,true))=="media-storage-unavailable","unknown volume blocks automatic download before network");
 capacity::Snapshot full;full.diskKnown=true;full.available=1;check(std::string(capacity::reason(full,1,true))=="media-disk-space-low","full C keeps explicit pause reason");
 full.available=capacity::diskReserveBytes+2*capacity::maxAssetBytes;check(!*capacity::reason(full,1,true),"space restoration clears storage gate");
 // Reclamation is limited to the owned filename after its writer exits.
 const auto tempLane=base/L"temporary-recovery";
 downloads::Queue tempQueue(tempLane);tempQueue.enqueue(Json::array({{{"appId",570},{"verified",true}}}));
 const auto token=tempQueue.start();const auto temporary=std::string(32,'a')+".part";
 const auto temporaryPath=tempLane/L"cache"/L"jobs"/L"game-media"/wide(temporary);
 file(temporaryPath,"partial");
 const auto queueStamp=fs::last_write_time(tempLane/L"config"/L"game-media-downloads.json");
 tempQueue.trackTemporary(token,temporary);
 check(fs::last_write_time(tempLane/L"config"/L"game-media-downloads.json")==queueStamp,"temporary ownership does not rewrite full inventory");
 tempQueue.cleanupTemporary();check(fs::exists(temporaryPath),"active worker temporary data is protected");
 auto marker=read(tempQueue.temporaryRecordPath());marker["files"][temporary]["workerPid"]=MAXDWORD;marker["files"][temporary]["workerBirth"]="1";atomicWrite(tempQueue.temporaryRecordPath(),marker);tempQueue.cleanupTemporary();
 check(!fs::exists(temporaryPath)&&!fs::exists(tempQueue.temporaryRecordPath()),"exited writer temporary file and marker are reclaimed");
 const auto outside=tempLane/L"outside.part";file(outside,"keep");
 marker["files"][temporary]["name"]="../outside.part";atomicWrite(tempQueue.temporaryRecordPath(),marker);tempQueue.cleanupTemporary();
 check(fs::exists(outside),"arbitrary temporary filename cannot delete outside staging");
 fs::create_directories(temporaryPath);marker["files"][temporary]["name"]=temporary;atomicWrite(tempQueue.temporaryRecordPath(),marker);tempQueue.cleanupTemporary();
 check(fs::is_directory(temporaryPath),"recovery never deletes a directory masquerading as a part file");
 const auto resumeName=std::string(32,'b')+".part";const auto resumePath=temporaryPath.parent_path()/wide(resumeName);
 file(resumePath,"partial");auto resumeMeta=resumePath;resumeMeta.replace_extension(L".resume.json");
 atomicWrite(resumeMeta,{{"resumeProtocol",1},{"identity","owned-resume"},{"finalSource","https://cdn.akamai.steamstatic.com/video.mp4"},{"validatorType","etag"},{"validator","\"v1\""},{"expectedSize",100}});
 tempQueue.trackTemporary(token,resumeName,true);marker=read(tempQueue.temporaryRecordPath());marker["files"][resumeName]["workerPid"]=MAXDWORD;marker["files"][resumeName]["workerBirth"]="1";atomicWrite(tempQueue.temporaryRecordPath(),marker);tempQueue.cleanupTemporary();
 check(fs::exists(resumePath)&&!read(tempQueue.temporaryRecordPath()).value("files",Json::object()).contains(resumeName),"verified resumable partial survives exited writer and releases ownership");
 atomicWrite(tempQueue.temporaryRecordPath(),{{"name",42},{"workerPid","damaged"}});tempQueue.cleanupTemporary();
 check(!tempQueue.start().empty(),"damaged ownership record does not block next queue session");
 size_t probes=0;for(const auto& e:fs::recursive_directory_iterator(base))if(e.path().filename().wstring().starts_with(L".ymcc-data-health-"))++probes;check(!probes,"write-health files never remain behind");
 Json result={{"ok",true},{"checks",checks},{"root",utf8(base.wstring())},{"scope","real C-drive isolated file/queue/capacity checks; absent/blocked D represented only by candidate layout; no actual volume or ACL changes"}};atomicWrite(base/L"results.json",result);std::cout<<result.dump()<<std::endl;return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}}
