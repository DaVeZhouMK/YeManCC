#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "shared_game_media.h"
#include "shared_game_media_download_queue.h"
#include <iostream>
using namespace ymcc::media;
static int checks=0;
static void check(bool value,const char* label){++checks;if(!value)throw std::runtime_error(label);}
static void file(const fs::path& p,const std::string& value){fs::create_directories(p.parent_path());std::ofstream out(p,std::ios::binary);out<<value;}
int wmain(int argc,wchar_t** argv){try{
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
 auto marker=read(tempQueue.temporaryRecordPath());marker["files"][temporary]["workerPid"]=MAXDWORD;marker["files"][temporary]["workerBirth"]="missing-fixture-pid";atomicWrite(tempQueue.temporaryRecordPath(),marker);tempQueue.cleanupTemporary();
 check(!fs::exists(temporaryPath)&&!fs::exists(tempQueue.temporaryRecordPath()),"exited writer temporary file and marker are reclaimed");
 const auto outside=tempLane/L"outside.part";file(outside,"keep");
 marker["files"][temporary]["name"]="../outside.part";atomicWrite(tempQueue.temporaryRecordPath(),marker);tempQueue.cleanupTemporary();
 check(fs::exists(outside),"arbitrary temporary filename cannot delete outside staging");
 fs::create_directories(temporaryPath);marker["files"][temporary]["name"]=temporary;atomicWrite(tempQueue.temporaryRecordPath(),marker);tempQueue.cleanupTemporary();
 check(fs::is_directory(temporaryPath),"recovery never deletes a directory masquerading as a part file");
 const auto resumeName=std::string(32,'b')+".part";const auto resumePath=temporaryPath.parent_path()/wide(resumeName);
 file(resumePath,"partial");auto resumeMeta=resumePath;resumeMeta.replace_extension(L".resume.json");
 atomicWrite(resumeMeta,{{"resumeProtocol",1},{"identity","owned-resume"},{"finalSource","https://cdn.akamai.steamstatic.com/video.mp4"},{"validatorType","etag"},{"validator","\"v1\""},{"expectedSize",100}});
 tempQueue.trackTemporary(token,resumeName,true);marker=read(tempQueue.temporaryRecordPath());marker["files"][resumeName]["workerPid"]=MAXDWORD;marker["files"][resumeName]["workerBirth"]="missing-fixture-pid";atomicWrite(tempQueue.temporaryRecordPath(),marker);tempQueue.cleanupTemporary();
 check(fs::exists(resumePath)&&!read(tempQueue.temporaryRecordPath()).value("files",Json::object()).contains(resumeName),"verified resumable partial survives exited writer and releases ownership");
 atomicWrite(tempQueue.temporaryRecordPath(),{{"name",42},{"workerPid","damaged"}});tempQueue.cleanupTemporary();
 check(!tempQueue.start().empty(),"damaged ownership record does not block next queue session");
 size_t probes=0;for(const auto& e:fs::recursive_directory_iterator(base))if(e.path().filename().wstring().starts_with(L".ymcc-data-health-"))++probes;check(!probes,"write-health files never remain behind");
 Json result={{"ok",true},{"checks",checks},{"root",utf8(base.wstring())},{"scope","real C-drive isolated file/queue/capacity checks; absent/blocked D represented only by candidate layout; no actual volume or ACL changes"}};atomicWrite(base/L"results.json",result);std::cout<<result.dump()<<std::endl;return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}}
