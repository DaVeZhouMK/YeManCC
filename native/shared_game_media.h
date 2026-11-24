#pragma once
// One durable media owner shared by the main renderer and library editor.
// All writes use a cross-process mutex and replace one versioned document.
#include <windows.h>
#include <bcrypt.h>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <functional>
#include <chrono>
#include <cmath>
#include <thread>
#include <mutex>
#include <memory>
#include <sstream>
#include <set>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include "json.hpp"
#include "custom-steam-library/custom_steam_library_paths.h"
#include "shared_game_media_capacity.h"
#include "shared_game_media_frame_probe.h"
#include "shared_game_media_power.h"
#include "custom-steam-library/custom_steam_library_identity_cache.h"
#pragma comment(lib, "bcrypt.lib")
namespace ymcc::media {
using Json = nlohmann::json;
inline constexpr int videoQualityPolicy=3;
inline constexpr int playback30Policy=1;
namespace steamDemand {
using Reader=std::function<Json()>;
inline std::mutex readerMutex;
inline Reader reader;
inline void setForegroundReader(Reader value){std::lock_guard lock(readerMutex);reader=std::move(value);}
inline Json foreground(){
    Reader read;{std::lock_guard lock(readerMutex);read=reader;}
    try{if(read)return read();}catch(...){}
    return {{"source","native-steam-bigpicture-foreground"},{"known",false},{"foreground",false},{"current",false},{"mode","unknown"},{"pid",0},{"birth","0"},{"window","0"},{"ownerEpoch",""},{"revision",0},{"reason","foreground-owner-unavailable"}};
}
inline bool steam(const Json& a){return a.is_object()&&a.value("demandConsumer",std::string{})=="steam";}
inline bool integer(const Json& object,const char* key,uint64_t maximum,bool positive=false){
    const auto it=object.find(key);if(it==object.end()||!it->is_number_integer())return false;
    if(!it->is_number_unsigned()&&it->get<int64_t>()<0)return false;
    const auto value=it->get<uint64_t>();return value<=maximum&&(!positive||value>0);
}
inline bool positiveToken(const Json& object,const char* key){
    const auto it=object.find(key);if(it==object.end()||!it->is_string())return false;const auto text=it->get<std::string>();
    return !text.empty()&&text.size()<=20&&text.front()>='1'&&text.front()<='9'&&
        std::all_of(text.begin(),text.end(),[](char c){return c>='0'&&c<='9';})&&
        (text.size()<20||text<=std::string("18446744073709551615"));
}
inline bool allowed(const Json& a){
    if(!steam(a))return true;
    if(!powerPolicy::allowed(a))return false;
    try{
        const auto proof=foreground();if(!proof.is_object())return false;const auto epoch=a.value("demandEpoch",std::string{});
        return !epoch.empty()&&epoch.size()<=128&&integer(a,"demandRevision",9007199254740991ULL)&&
            integer(proof,"revision",9007199254740991ULL)&&integer(proof,"pid",MAXDWORD,true)&&
            positiveToken(proof,"birth")&&positiveToken(proof,"window")&&
            proof.value("source",std::string{})=="native-steam-bigpicture-foreground"&&
            proof.value("known",false)&&proof.value("foreground",false)&&proof.value("current",false)&&
            proof.value("mode",std::string{})=="bigpicture"&&proof.value("ownerEpoch",std::string{})==epoch&&
            proof["revision"].get<uint64_t>()==a["demandRevision"].get<uint64_t>();
    }catch(...){return false;}
}
inline Json inactive(){const auto reason=powerPolicy::reason();return {{"ready",false},{"acquired",false},{"status","consumer-inactive"},{"demandConsumer","steam"},{"reason",reason.empty()?"media-steam-foreground-inactive":reason}};}
inline void require(const Json& a){if(!allowed(a))throw std::runtime_error("media-stale-steam-demand");}
} // namespace steamDemand
namespace workDemand {
inline bool allowed(const Json& a){return powerPolicy::allowed(a)&&steamDemand::allowed(a);}
inline void require(const Json& a){powerPolicy::require(a);steamDemand::require(a);}
} // namespace workDemand
inline int preferredVideoHeight(const Json& entry){
    const auto selected=entry.value("selectedVideo",Json::object());
    const auto height=selected.value("preferredHeight",720);
    return height==480||height==720||height==1080?height:720;
}
// A playable fallback is not a receipt for the user's replacement choice.
// taskSlots also recovers failed records whose old host consumed refresh early.
inline bool pendingMediaRefresh(const Json& e,const std::string& slot){
    if(e.value("deleted",false))return false;
    const auto deleted=e.find("deletedSlots");
    if(deleted!=e.end()&&deleted->is_object()&&deleted->value(slot,false))return false;
    if(e.value("refreshRequested",false)){
        const auto requested=e.value("refreshSlot",std::string{"dynamic"});
        if(requested==slot||(requested=="both"&&(slot=="dynamic"||slot=="static")))return true;
    }
    const auto selected=e.find("selectedVideo"),tasks=e.find("taskSlots");
    return slot=="dynamic"&&selected!=e.end()&&selected->is_object()&&!selected->empty()&&
        tasks!=e.end()&&tasks->is_array()&&std::find(tasks->begin(),tasks->end(),slot)!=tasks->end();
}
// Merge independently accepted edits; a publication settles only its own slot.
inline void setMediaRefresh(Json& e,const std::string& slot,bool requested){
    const bool dynamic=slot=="dynamic"?requested:pendingMediaRefresh(e,"dynamic");
    const bool image=slot=="static"?requested:pendingMediaRefresh(e,"static");
    e["refreshRequested"]=dynamic||image;
    if(dynamic||image)e["refreshSlot"]=dynamic?(image?"both":"dynamic"):"static";
}
inline bool needsVideoQualityUpgrade(const Json& e,int64_t time){
    return e.value("dynamicReady",false)&&!e.value("deleted",false)&&
        !e.value("manualSlots",Json::object()).value("dynamic",false)&&!e.value("deletedSlots",Json::object()).value("dynamic",false)&&
        e.value("videoUnavailableUntil",int64_t{0})<=time&&
        e.value("assets",Json::object()).value("dynamic",Json::object()).value("qualityPolicy",0)<videoQualityPolicy&&
        (e.value("assets",Json::object()).value("dynamic",Json::object()).value("selectedHeight",0)<=0||
         e.value("assets",Json::object()).value("dynamic",Json::object()).value("selectedHeight",0)>preferredVideoHeight(e));
}
inline std::string stringField(const Json& object,const char* key,const std::string& fallback={}){if(!object.is_object())return fallback;const auto item=object.find(key);return item!=object.end()&&item->is_string()?item->get<std::string>():fallback;}
namespace fs = std::filesystem;
inline int64_t now() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count(); }
inline std::wstring wide(const std::string& v) { if(v.empty())return {};int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,v.data(),static_cast<int>(v.size()),nullptr,0);if(n<=0)throw std::runtime_error("media-invalid-utf8");std::wstring r(n,0);MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,v.data(),static_cast<int>(v.size()),r.data(),n);return r; }
inline std::string utf8(const std::wstring& v) { if(v.empty())return {};int n=WideCharToMultiByte(CP_UTF8,0,v.data(),static_cast<int>(v.size()),nullptr,0,nullptr,nullptr);std::string r(n,0);WideCharToMultiByte(CP_UTF8,0,v.data(),static_cast<int>(v.size()),r.data(),n,nullptr,nullptr);return r; }
inline std::string normalized(const std::string& v) { auto p=wide(v);std::replace(p.begin(),p.end(),L'/',L'\\');for(auto& c:p)c=static_cast<wchar_t>(towlower(c));return utf8(p); }
inline std::string digestKey(const std::string& s) { uint64_t n=1469598103934665603ull;for(unsigned char c:s){n^=c;n*=1099511628211ull;}std::ostringstream o;o<<std::hex<<n;return o.str(); }
inline bool safeKey(const std::string& s) { return !s.empty()&&s.size()<=100&&std::all_of(s.begin(),s.end(),[](unsigned char c){return (c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-';}); }
inline Json cadenceRecord(frameProbe::Cadence cadence) {
    return {{"schema",frameProbe::cadenceSchema},{"samples",cadence.samples},{"timescale",cadence.timescale},
        {"ticks",cadence.ticks},{"exceeds30",cadence.exceeds30()}};
}
inline Json parseMediaBytes(const std::string& bytes){
    auto document=Json::parse(bytes.begin(),bytes.end());
    if(!document.is_object())throw std::runtime_error("media-index-invalid");
    return document;
}
template<class Parsed,class Bytes>
inline auto readMediaObserved(const fs::path& p,Parsed&& parsed,Bytes&& fromBytes) {
    const auto disk=custom_steam_library::ioPath(p);std::error_code ec;const bool exists=fs::exists(disk,ec);
    if(ec)throw std::runtime_error("media-index-unreadable");if(!exists)return parsed(Json::object());
    const auto size=fs::file_size(disk,ec);
    if(ec||size>16*1024*1024)throw std::runtime_error("media-index-unreadable");
    std::ifstream in(disk,std::ios::binary);if(!in)throw std::runtime_error("media-index-unreadable");
    // Small complete documents can use the same parser's cheaper iterator path.
    // Keep stream semantics for size races, embedded NULs and large documents.
    if(size<=1024*1024){
        std::string bytes(static_cast<size_t>(size),'\0');
        if(in.read(bytes.data(),static_cast<std::streamsize>(size))&&in.peek()==std::char_traits<char>::eof()&&
            in.eof()&&!in.bad()&&bytes.find('\0')==std::string::npos){
            return fromBytes(std::move(bytes));
        }
        in.clear();in.seekg(0);
        if(!in)throw std::runtime_error("media-index-unreadable");
    }
    auto document=Json::parse(in);
    if(!in.eof()||!document.is_object())throw std::runtime_error("media-index-invalid");
    return parsed(std::move(document));
}
inline Json read(const fs::path& p) {
    return readMediaObserved(p,[](Json document){return document;},parseMediaBytes);
}
// A cold library process consumes the queue's durable preference once. Its
// existing download watcher refreshes it on queue events; no settings polling.
inline void refreshMediaPowerPreference(const fs::path& root){
    // Settings already own this process; consume() would ignore the disk copy.
    if(powerPolicy::ownsPreference())return;
    const auto document=read(root/L"config"/L"game-media-downloads.json");
    const auto value=document.find("wallpaperPauseOnBattery");
    if(value!=document.end()&&value->is_boolean())powerPolicy::consume(value->get<bool>());
}
inline void bindMediaPowerPreference(const fs::path& root){
    static std::mutex mutex;static std::set<fs::path> bound;
    std::lock_guard lock(mutex);const auto key=fs::absolute(root).lexically_normal();
    if(bound.contains(key))return;refreshMediaPowerPreference(root);bound.insert(key);
}
struct Lock { HANDLE value=nullptr;explicit Lock(const fs::path& root,DWORD timeout=10000){auto name=L"Local\\YMCC-SharedMedia-"+wide(digestKey(normalized(utf8(fs::absolute(root).wstring()))));value=CreateMutexW(nullptr,FALSE,name.c_str());if(!value)throw std::runtime_error("media-lock-unavailable");auto result=WaitForSingleObject(value,timeout);if(result!=WAIT_OBJECT_0&&result!=WAIT_ABANDONED){CloseHandle(value);value=nullptr;throw std::runtime_error("media-owner-busy");}}~Lock(){if(value){ReleaseMutex(value);CloseHandle(value);}}Lock(const Lock&)=delete; };
inline void atomicWrite(const fs::path& p,const Json& j) { fs::create_directories(custom_steam_library::ioPath(p.parent_path()));auto tmp=p;tmp+=L".tmp."+std::to_wstring(GetCurrentProcessId())+L"."+std::to_wstring(GetTickCount64());const auto temporary=custom_steam_library::ioPath(tmp),disk=custom_steam_library::ioPath(p);{std::ofstream o(temporary,std::ios::binary|std::ios::trunc);o<<j.dump(2);o.flush();if(!o)throw std::runtime_error("media-write-failed");}if(!MoveFileExW(temporary.c_str(),disk.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){std::error_code e;fs::remove(temporary,e);throw std::runtime_error("media-publish-failed");} }
inline std::string randomId() { unsigned char b[16];if(BCryptGenRandom(nullptr,b,sizeof b,BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0)throw std::runtime_error("media-entropy-unavailable");std::string r;for(auto c:b){r+="0123456789abcdef"[c>>4];r+="0123456789abcdef"[c&15];}return r; }
inline std::string mediaKind(const fs::path& p) { std::ifstream in(custom_steam_library::ioPath(p),std::ios::binary);if(!in)throw std::runtime_error("media-storage-unavailable");unsigned char b[16]{};in.read(reinterpret_cast<char*>(b),sizeof b);if(in.bad())throw std::runtime_error("media-storage-unavailable");auto n=in.gcount();if(n>=8&&b[4]=='f'&&b[5]=='t'&&b[6]=='y'&&b[7]=='p')return "mp4";if(n>=4&&b[0]==0x1a&&b[1]==0x45&&b[2]==0xdf&&b[3]==0xa3)return "webm";if(n>=8&&b[0]==0x89&&b[1]=='P'&&b[2]=='N'&&b[3]=='G')return "png";if(n>=3&&b[0]==0xff&&b[1]==0xd8&&b[2]==0xff)return "jpg";if(n>=12&&b[0]=='R'&&b[1]=='I'&&b[2]=='F'&&b[3]=='F'&&b[8]=='W'&&b[9]=='E'&&b[10]=='B'&&b[11]=='P')return "webp";if(n>=2&&b[0]=='B'&&b[1]=='M')return "bmp";if(n>=4&&b[0]==0&&b[1]==0&&b[2]==1&&b[3]==0)return "ico";throw std::runtime_error("media-unsupported-container"); }
inline std::string processBirthToken(DWORD pid){HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);if(!process)return {};FILETIME created{},exit{},kernel{},user{};const bool ok=GetProcessTimes(process,&created,&exit,&kernel,&user)!=FALSE;CloseHandle(process);if(!ok)return {};ULARGE_INTEGER value{};value.LowPart=created.dwLowDateTime;value.HighPart=created.dwHighDateTime;return std::to_string(value.QuadPart);}
inline bool leaseOwnerGone(const Json& entry){const auto pid=entry.value("leasePid",DWORD{0});if(!pid)return false;HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,pid);if(!process)return GetLastError()==ERROR_INVALID_PARAMETER;const bool exited=WaitForSingleObject(process,0)==WAIT_OBJECT_0;CloseHandle(process);if(exited)return true;const auto birth=processBirthToken(pid);return !birth.empty()&&!entry.value("leaseBirth",std::string{}).empty()&&birth!=entry.value("leaseBirth",std::string{});}
inline Json identityWorkerArguments(const std::string& executable,const fs::path& output){
    if(executable.empty()||!output.is_absolute())throw std::runtime_error("media-identity-arguments-invalid");
    return Json::array({executable,"--steam-identity-only",utf8(output.wstring())});
}
class Store {
    fs::path root_;
    fs::path index()const{return root_/L"config"/L"game-media.json";}
    std::shared_ptr<const Json> readInventoryState()const{
        const auto build=[](Json document){return std::make_shared<const Json>(std::move(document));};
        return readMediaObserved(root_/L"state"/L"library-scan.json",build,[&](std::string bytes){
            // Reuse one parsed scan only after a fresh, complete byte read.
            struct Entry{std::string bytes;std::shared_ptr<const Json> document;};
            static std::mutex mutex;
            static Entry cached;
            {
                Entry retired;
                std::lock_guard lock(mutex);
                if(cached.document&&cached.bytes==bytes)return cached.document;
                std::swap(cached,retired);
            }
            auto document=build(parseMediaBytes(bytes));
            Entry next{std::move(bytes),document};
            {std::lock_guard lock(mutex);std::swap(cached,next);}
            return document;
        });
    }
    static std::string keyFor(const Json& a){
        const auto key=a.value("key",std::string{});
        if(!key.empty()){if(!safeKey(key))throw std::runtime_error("media-invalid-key");return key;}
        const auto id=a.value("appId",int64_t{0});
        if(id>0&&id<=2147483647)return "steam-"+std::to_string(id);
        const auto exe=a.value("executable",std::string{});
        if(!exe.empty())return "exe-"+digestKey(normalized(exe));
        throw std::runtime_error("media-identity-required");
    }
    static void normalizeEntryInPlace(Json& e){
        if(!e.contains("assets"))e["assets"]=Json::object();
        if(e.contains("asset")&&e["asset"].is_object()&&e["assets"].empty()){
            const auto slot=e["asset"].value("kind",std::string{})=="video"?"dynamic":"static";
            // A tombstone is newer than the compatibility asset projection.
            // Deleting the only variant must not resurrect it on normalization.
            if(!e.value("deletedSlots",Json::object()).value(slot,false)){
                e["assets"][slot]=e["asset"];
                if(e.value("manual",false))e["manualSlots"][slot]=true;
            }
        }
        if(!e.contains("manualSlots"))e["manualSlots"]=Json::object();
        if(!e.contains("deletedSlots"))e["deletedSlots"]=Json::object();
        if(!e.value("refreshRequested",false)&&pendingMediaRefresh(e,"dynamic")){e["refreshRequested"]=true;e["refreshSlot"]="dynamic";}
        static const Json defaultPlayback={{"start",8},{"end",38},{"fit","cover"}};
        if(!e.contains("playback"))e["playback"]=defaultPlayback;
        if(!e.contains("playbackExplicit"))e["playbackExplicit"]=e["playback"]!=defaultPlayback;
        // Older editor builds marked a trim as manual video ownership, even
        // before a video existed. Retain that trim but allow initial collection.
        if(e.value("playbackExplicit",false)&&!e["assets"].contains("dynamic")&&!e["deletedSlots"].value("dynamic",false))e["manualSlots"].erase("dynamic");
        if(!e.contains("invalidSlots"))e["invalidSlots"]=Json::object();
        const auto usable=[&](const std::string& slot){return e["assets"].contains(slot)&&!e["deletedSlots"].value(slot,false)&&e["invalidSlots"].value(slot,std::string{})!=e["assets"][slot].value("id",std::string{});};
        const auto mode=e.value("modeExplicit",false)?e.value("wallpaperMode",std::string{"static"}):(e["assets"].contains("dynamic")?std::string{"dynamic"}:std::string{"static"});e["wallpaperMode"]=mode=="static"?"static":"dynamic";
        const auto chosen=e["wallpaperMode"].get<std::string>();
        if(usable(chosen))e["asset"]=e["assets"][chosen];
        else if(chosen=="dynamic"&&usable("static"))e["asset"]=e["assets"]["static"];
        else e.erase("asset");
        e["ready"]=e.contains("asset")&&!e.value("deleted",false);
        e["staticReady"]=usable("static");
        e["dynamicReady"]=usable("dynamic");
        e["manual"]=e["manualSlots"].value(chosen,false);
    }
    static Json normalizeEntry(Json e){normalizeEntryInPlace(e);return e;}
    static void releasePreparation(Json& e){e["taskId"]="";e["leaseUntil"]=0;e["taskSlots"]=Json::array();}
    static std::string canonicalKey(const Json& j,const Json& args){auto key=keyFor(args);for(int n=0;n<4;n++){if(!j.contains("aliases")||!j["aliases"].contains(key))return key;const auto next=j["aliases"][key].get<std::string>();if(!safeKey(next)||next==key)throw std::runtime_error("media-alias-invalid");key=next;}throw std::runtime_error("media-alias-cycle");}
    static Json normalizeDocument(Json j,Json* original=nullptr){
        if(!j.contains("entries"))j["entries"]=Json::object();
        if(!j["entries"].is_object()||j.value("schemaVersion",1)!=1)throw std::runtime_error("media-schema-unsupported");
        if(original)*original=j; // Recovery preserves unrelated raw records after full validation.
        for(auto& e:j["entries"])normalizeEntryInPlace(e);
        return j;
    }
    Json document(Json* original=nullptr)const{return normalizeDocument(read(index()),original);}
    std::shared_ptr<const Json> readOnlyDocument()const{
        const auto build=[](Json j){return std::make_shared<const Json>(normalizeDocument(std::move(j)));};
        return readMediaObserved(index(),build,[&](std::string bytes){
            if(bytes.size()>256*1024)return build(parseMediaBytes(bytes));
            // One immutable document per process, keyed by fresh complete bytes.
            // No modification time, timer or change event is a freshness proof.
            struct Entry{std::string bytes;std::shared_ptr<const Json> value;};
            static std::mutex mutex;
            static Entry cached;
            {
                std::lock_guard lock(mutex);
                if(cached.value&&cached.bytes==bytes)return cached.value;
            }
            auto value=build(parseMediaBytes(bytes));
            Entry next{std::move(bytes),value};
            {std::lock_guard lock(mutex);std::swap(cached,next);}
            return value;
        });
    }
    void save(Json& j){
        j.erase("_mediaChanged");j["schemaVersion"]=1;j["revision"]=j.value("revision",uint64_t{0})+1;j["updatedAt"]=now();atomicWrite(index(),j);
        for(const auto* role:{L"main",L"library"}){
            const auto name=L"Local\\YMCC-MediaChanged-"+wide(digestKey(normalized(utf8(fs::absolute(root_).wstring()))))+L"-"+role;
            HANDLE event=OpenEventW(EVENT_MODIFY_STATE,FALSE,name.c_str());if(event){SetEvent(event);CloseHandle(event);}
        }
    }
    Json seed(Json& j,const Json& a){
        const auto key=canonicalKey(j,a);
        const auto exe=normalized(a.value("executable",std::string{}));const auto appId=a.value("appId",int64_t{0});
        if(appId>0&&!exe.empty()&&a.value("verified",false)){
            const auto fileKey=keyFor(Json{{"executable",a.value("executable",std::string{})}});
            const auto previous=canonicalKey(j,Json{{"executable",a.value("executable",std::string{})}});
            if(previous!=fileKey&&previous!=key){
                // A fresh exact-file official cache may correct an earlier
                // mistaken media alias. Caller verified=true alone never can.
                const auto proof=identity(a.value("executable",std::string{}));
                if(key!="steam-"+std::to_string(appId)||!proof.value("verified",false)||proof.value("appId",int64_t{0})!=appId)
                    throw std::runtime_error("media-identity-binding-conflict");
                if(j["entries"].contains(previous)){
                    auto& old=j["entries"][previous];
                    if(normalized(old.value("executable",std::string{}))==exe){
                        const auto revision=old.value("bindingRevision",uint64_t{0});
                        if(revision==UINT64_MAX)throw std::runtime_error("media-binding-revision-exhausted");
                        // Keep assets and user edits with their original AppID.
                        // Revoke only the retired EXE link and its publication.
                        old["executable"]="";old["gameDirectory"]="";releasePreparation(old);
                        old["bindingRevision"]=revision+1;old["updatedAt"]=now();
                    }
                }
                j["aliases"][fileKey]=key;j["_mediaChanged"]=true;
            }
        }
        if(appId>0&&!exe.empty()&&a.value("verified",false))for(auto it=j["entries"].begin();it!=j["entries"].end();){
            if(it.key()==key||it.value().value("appId",int64_t{0})>0||normalized(it.value().value("executable",std::string{}))!=exe){++it;continue;}
            const auto oldKey=it.key();auto old=normalizeEntry(it.value());
            if(!j["entries"].contains(key)){old["key"]=key;old["appId"]=appId;old["bindingRevision"]=old.value("bindingRevision",uint64_t{0})+1;j["entries"][key]=old;}
            else {
                auto current=normalizeEntry(j["entries"][key]);bool bindingChanged=false;
                for(const auto* slot:{"static","dynamic"}){
                    if(old["assets"].contains(slot)&&(!current["assets"].contains(slot)||old["manualSlots"].value(slot,false)&&!current["manualSlots"].value(slot,false))){
                        current["assets"][slot]=old["assets"][slot];current["manualSlots"][slot]=old["manualSlots"].value(slot,false);
                        if(std::string(slot)=="static"&&old.contains("libraryStaticSource"))current["libraryStaticSource"]=old["libraryStaticSource"];
                        if(old["invalidSlots"].contains(slot))current["invalidSlots"][slot]=old["invalidSlots"][slot];else current["invalidSlots"].erase(slot);
                        bindingChanged=true;
                    }
                    if(old["deletedSlots"].value(slot,false)&&!current["manualSlots"].value(slot,false)&&!current["deletedSlots"].value(slot,false)){current["deletedSlots"][slot]=true;if(std::string(slot)=="static"&&old.contains("libraryStaticSource"))current["libraryStaticSource"]=old["libraryStaticSource"];bindingChanged=true;}
                }
                if(old.value("modeExplicit",false)&&!current.value("modeExplicit",false)){current["wallpaperMode"]=old["wallpaperMode"];current["modeExplicit"]=true;bindingChanged=true;}
                if(old.value("playbackExplicit",false)&&!current.value("playbackExplicit",false)){current["playback"]=old["playback"];current["playbackExplicit"]=true;bindingChanged=true;}
                // Metadata-only aliases preserve a download already owned by another
                // request. Real edits supersede it, without leaving a stale 10m lease.
                if(bindingChanged){
                    current["bindingRevision"]=std::max(current.value("bindingRevision",uint64_t{0}),old.value("bindingRevision",uint64_t{0}))+1;
                    releasePreparation(current);current["retryAt"]=0;
                    current=normalizeEntry(current);current["status"]=current.value("ready",false)?"ready":"missing";
                }
                j["entries"][key]=normalizeEntry(current);
            }
            j["aliases"][oldKey]=key;j["_mediaChanged"]=true;it=j["entries"].erase(it);
        }
        if(!j["entries"].contains(key))j["entries"][key]={{"key",key},{"appId",a.value("appId",int64_t{0})},{"name",a.value("name",std::string{})},{"executable",a.value("executable",std::string{})},{"bucket","unclassified"},{"bindingRevision",1},{"status","missing"},{"manual",false},{"deleted",false},{"updatedAt",now()},{"assets",Json::object()},{"wallpaperMode","static"},{"modeExplicit",false}};
        if(appId>0&&!exe.empty()&&a.value("verified",false)){
            const auto fileKey=keyFor(Json{{"executable",a.value("executable",std::string{})}});
            if(j.contains("aliases")&&j["aliases"].contains(fileKey)&&canonicalKey(j,Json{{"executable",a.value("executable",std::string{})}})!=key)throw std::runtime_error("media-identity-binding-conflict");
            if(fileKey!=key&&(!j.contains("aliases")||!j["aliases"].contains(fileKey))){j["aliases"][fileKey]=key;j["_mediaChanged"]=true;}
        }
        auto e=normalizeEntry(j["entries"][key]);
        if(a.value("verified",false)&&appId>0&&!e.value("identityVerified",false)){e["identityVerified"]=true;j["_mediaChanged"]=true;}
        for(const auto* field:{"executable","gameDirectory"})if(e.value(field,std::string{}).empty()&&!a.value(field,std::string{}).empty()){e[field]=a.value(field,std::string{});j["_mediaChanged"]=true;}
        return e;
    }
    static std::string canonicalDirectoryId(const fs::path& path){
        std::error_code ec;auto full=fs::weakly_canonical(path,ec);if(ec)full=fs::absolute(path);
        auto text=utf8(full.wstring());std::replace(text.begin(),text.end(),'/','\\');
        for(auto& c:text)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        BCRYPT_ALG_HANDLE algorithm=nullptr;unsigned char bytes[32]{};
        if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)throw std::runtime_error("media-hash-unavailable");
        const auto result=BCryptHash(algorithm,nullptr,0,reinterpret_cast<PUCHAR>(text.data()),static_cast<ULONG>(text.size()),bytes,sizeof bytes);
        BCryptCloseAlgorithmProvider(algorithm,0);if(result<0)throw std::runtime_error("media-hash-failed");
        std::string id;for(auto b:bytes){id+="0123456789abcdef"[b>>4];id+="0123456789abcdef"[b&15];}return id;
    }
    Json reference(const fs::path& path)const{
        std::error_code ec;auto relative=fs::relative(custom_steam_library::ioPath(path),custom_steam_library::ioPath(root_),ec);if(ec||relative.empty()||relative.is_absolute())return Json::object();
        for(const auto& part:relative)if(part==L"..")return Json::object();
        if(*relative.begin()!=L"artwork")return Json::object();
        auto bytes=fs::file_size(custom_steam_library::ioPath(path),ec);if(ec||!bytes||bytes>30ull*1024*1024)return Json::object();
        const auto ext=mediaKind(path);if(ext=="mp4"||ext=="webm")return Json::object();
        const auto stamp=fs::last_write_time(custom_steam_library::ioPath(path),ec).time_since_epoch().count();if(ec)return Json::object();
        const auto id="library-"+digestKey(utf8(relative.generic_wstring())+std::to_string(stamp)+std::to_string(bytes));
        return {{"id",id},{"revision",id},{"extension",ext},{"kind","image"},{"mime",ext=="jpg"?"image/jpeg":ext=="png"?"image/png":"image/"+ext},{"bytes",bytes},{"storage",utf8(relative.generic_wstring())}};
    }
    Json localStatic(const std::string& executable,const std::string& gameDirectory={})const{
        if(executable.empty()&&gameDirectory.empty())return Json::object();
        auto overrides=read(root_/L"config"/L"manual-overrides.json");const auto wanted=normalized(executable);
        if(overrides.contains("items")&&overrides["items"].is_object())for(auto it=overrides["items"].begin();it!=overrides["items"].end();++it){
            if(normalized(it.key())!=wanted)continue;const auto& item=it.value();
            const auto policy=item.value("artworkProtection",Json::object()).value("wallpaper",std::string{});
            const auto selection=stringField(item,"wallpaperSourceRevision");
            if(policy=="deleted")return {{"deleted",true},{"selectionRevision",selection}};
            if(item.contains("wallpaper")){const auto& w=item["wallpaper"];const auto file=w.is_string()?w.get<std::string>():w.value("portableFile",w.value("file",std::string{}));
                auto asset=reference(fs::path(wide(file)));if(!asset.empty())return {{"asset",asset},{"manual",true},{"selectionRevision",selection}};}
        }
        std::vector<std::string> directories;if(!gameDirectory.empty())directories.push_back(gameDirectory);
        else {
            const auto state=read(root_/L"state"/L"library-scan.json");
            const auto games=state.find("games");
            // Unresolved scan rows can have a null primaryExecutable. They do
            // not participate in executable-based artwork lookup.
            if(games!=state.end()&&games->is_array())for(const auto& game:*games){
                const auto primary=stringField(game,"primaryExecutable");
                if(primary.empty()||normalized(primary)!=wanted)continue;
                const auto directory=stringField(game,"gameDirectory");
                if(!directory.empty())directories.push_back(directory);
            }
        }
        for(const auto& directory:directories){
            const auto manifest=read(root_/L"artwork"/wide(canonicalDirectoryId(fs::path(wide(directory))))/L"manifest.json");
            for(const auto& item:manifest.value("artwork",Json::array())){
                const auto type=item.value("type",std::string{});if(type!="hero"&&type!="wallpaper")continue;
                const auto file=item.value("portableFile",item.value("file",std::string{}));if(file.empty())continue;
                const auto asset=reference(fs::path(wide(file)));if(!asset.empty())return {{"asset",asset},{"manual",false}};
            }
        }
        return Json::object();
    }
    bool adoptStatic(Json& entry)const{
        const auto local=localStatic(entry.value("executable",std::string{}),entry.value("gameDirectory",std::string{}));
        const bool known=entry.contains("libraryStaticSource")&&entry["libraryStaticSource"].is_object();
        if(known&&entry["libraryStaticSource"]==local)return false;
        if(!known&&local.empty())return false;
        const bool protectedSlot=entry["manualSlots"].value("static",false)||entry["deletedSlots"].value("static",false);
        entry["libraryStaticSource"]=local;
        // A repeated library snapshot is not a new user selection. Old entries
        // without a baseline retain their explicit shared decision on migration.
        if(!known&&protectedSlot)return true;
        if(local.empty())return true;
        if(local.value("deleted",false)){
            entry["deletedSlots"]["static"]=true;entry["manualSlots"]["static"]=true;
            entry["assets"].erase("static");entry["invalidSlots"].erase("static");return true;
        }
        if(!local.contains("asset")||protectedSlot&&!local.value("manual",false))return true;
        entry["assets"]["static"]=local["asset"];entry["manualSlots"]["static"]=local.value("manual",false);
        entry["deletedSlots"]["static"]=false;entry["invalidSlots"].erase("static");return true;
    }

public:
    explicit Store(fs::path root):root_(std::move(root)){bindMediaPowerPreference(root_);}
    const fs::path& root()const{return root_;}
    Json snapshot(bool includeWallpaperPlayback=false)const{
        Lock lock(root_);auto j=*readOnlyDocument();
        for(auto& e:j["entries"]){
            if(e.value("status",std::string{})=="pending"&&leaseOwnerGone(e)){e["leaseUntil"]=0;e["leaseAbandoned"]=true;}
            if(includeWallpaperPlayback){
                auto id=wallpaperPlaybackStamp(e);
                if(!id.empty())e["wallpaperPlaybackStamp"]=std::move(id);
            }
        }
        return j;
    }
    Json resolve(const Json& a)const{
        if(!workDemand::allowed(a))return steamDemand::inactive();Lock lock(root_);
        if(!workDemand::allowed(a))return steamDemand::inactive();const auto document=readOnlyDocument();const auto& j=*document;
        if(!workDemand::allowed(a))return steamDemand::inactive();auto exact=resolveDocument(j,a);
        if(a.value("currentGameWallpaper",false)&&!a.contains("key")&&!a.contains("appId")){
            auto fallback=currentGameVideo(j,a,exact);
            if(!workDemand::allowed(a))return steamDemand::inactive();
            if(!fallback.empty())return fallback;
        }
        return exact;
    }
    Json resolveMany(const Json& targets)const{
        if(!targets.is_array()||targets.size()>4)throw std::runtime_error("media-prewarm-target-limit");
        if(targets.empty())return Json::array();
        const auto hasDemand=[&](){bool any=false;for(const auto& target:targets){if(!target.is_object())throw std::runtime_error("media-prewarm-target-invalid");if(workDemand::allowed(target))any=true;}return any;};
        const auto inactive=[&](){Json entries=Json::array();for(const auto& target:targets)entries.push_back(steamDemand::inactive());return entries;};
        if(!hasDemand())return inactive();Lock lock(root_);if(!hasDemand())return inactive();const auto document=readOnlyDocument();const auto& j=*document;Json entries=Json::array();
        for(const auto& target:targets)entries.push_back(workDemand::allowed(target)?resolveDocument(j,target):steamDemand::inactive());return entries;
    }
private:
    Json resolveDocument(const Json& j,const Json& a)const{
        auto key=canonicalKey(j,a);
        if(!j["entries"].contains(key)){
            // Explicit media keys and store IDs may use a persisted alias,
            // but a missing binding must not borrow media by EXE alone.
            if(!stringField(a,"key").empty())return {{"key",key},{"status","missing"},{"bindingRevision",0},{"ready",false}};
            const auto appId=a.value("appId",int64_t{0});
            if(appId>0&&appId<=2147483647)return {{"key",key},{"status","missing"},{"bindingRevision",0},{"ready",false}};
            const auto exe=normalized(a.value("executable",std::string{}));std::string found;
            if(!exe.empty())for(auto it=j["entries"].begin();it!=j["entries"].end();++it)if(normalized(it.value().value("executable",std::string{}))==exe){if(!found.empty())return {{"key",key},{"status","ambiguous"},{"bindingRevision",0}};found=it.key();}
            if(!found.empty())key=found;
        }
        if(!j["entries"].contains(key))return {{"key",key},{"status","missing"},{"bindingRevision",0},{"ready",false}};
        auto e=normalizeEntry(j["entries"][key]);std::error_code ec;
        if(e.contains("asset"))e["ready"]=fs::is_regular_file(custom_steam_library::ioPath(assetPath(e["asset"])),ec)&&!ec;
        for(const auto* slot:{"static","dynamic"}){const bool exists=e[std::string(slot)+"Ready"].get<bool>()&&fs::is_regular_file(custom_steam_library::ioPath(assetPath(e["assets"][slot])),ec)&&!ec;e[std::string(slot)+"Ready"]=exists;ec.clear();}
        if(!e.value("ready",false)&&!e.value("deleted",false)&&e.value("wallpaperMode",std::string{})=="dynamic"&&e.value("staticReady",false)){e["asset"]=e["assets"]["static"];e["ready"]=true;}
        return e;
    }
    // Current-game display only: reusing a video never writes a media alias
    // or changes the identity used for Steam membership and queue claims.
    Json currentGameVideo(const Json& j,const Json& a,const Json& exact)const{
        if(!stringField(exact,"key").starts_with("exe-")||exact.value("dynamicReady",false)||exact.value("deleted",false)||
           exact.value("identityVerified",false)||exact.value("manual",false)||exact.value("playbackExplicit",false)||
           (exact.value("modeExplicit",false)&&stringField(exact,"wallpaperMode")=="static")||
           exact.value("deletedSlots",Json::object()).value("dynamic",false)||
           exact.value("manualSlots",Json::object()).value("dynamic",false)||exact.value("manualSlots",Json::object()).value("static",false)||
           exact.value("invalidSlots",Json::object()).contains("dynamic")||exact.contains("selectedVideo"))return Json::object();
        try {
            namespace cache=custom_steam_library::identity_cache;
            const auto exe=stringField(a,"executable"),wanted=cache::executableKey(exe);
            const auto playable=[](const Json& video,int64_t appId){
                return video.value("appId",int64_t{0})==appId&&video.value("identityVerified",false)&&
                       video.value("ready",false)&&video.value("dynamicReady",false)&&!video.value("deleted",false)&&
                       stringField(video.value("asset",Json::object()),"kind")=="video";
            };
            const auto snapshot=cache::readSnapshot(cache::cachePath(root_,exe));
            if(snapshot.exists){
                // Only an absent exact cache permits launcher inference.
                if(!cache::recordValid(snapshot,exe,now()))return Json::object();
                const auto appId=cache::integer(snapshot.value["match"],"appId");if(appId<=0)return Json::object();
                const Json target={{"appId",appId}};
                const auto expected=keyFor(target);
                if(canonicalKey(j,target)!=expected)return Json::object();
                auto video=resolveDocument(j,target);
                if(stringField(video,"key")!=expected||!playable(video,appId))return Json::object();
                video["currentGameWallpaperFallback"]=true;
                return video;
            }
            const auto key=[](const std::string& text){try{return cache::executableKey(text);}catch(...){return std::string{};}};
            const auto scan=read(root_/L"state"/L"library-scan.json"),games=scan.value("games",Json::array());
            if(!games.is_array()||games.size()>10000)return Json::object();
            std::string launcher,directory;
            const Json* matched=nullptr;
            for(const auto& game:games){
                if(!game.is_object())continue;
                const auto primary=key(stringField(game,"primaryExecutable"));
                if(primary!=wanted)continue;
                if(!launcher.empty()||stringField(game,"status")!="ready"||stringField(game,"contentType")!="game"||
                   stringField(game,"primaryRole")!="main-game"||!game.value("scanComplete",false)||
                   !game.value("uniquePrimaryInvariant",false)||custom_steam_library::builtinExcludedSteamTool(game))return Json::object();
                directory=key(stringField(game,"gameDirectory"));
                if(directory.empty()||!wanted.starts_with(directory+"\\"))return Json::object();
                const auto candidates=game.value("candidates",Json::array());
                if(!candidates.is_array()||candidates.size()>1024)return Json::object();
                unsigned selected=0,primaryRows=0;
                for(const auto& item:candidates){
                    if(!item.is_object())return Json::object();
                    const auto path=stringField(item,"path"),itemKey=key(path),role=stringField(item,"role");
                    if(itemKey==wanted)++primaryRows;
                    if(item.value("selected",false)){
                        if(itemKey!=wanted||role!="main-game")return Json::object();
                        ++selected;
                    }
                    if(role=="launcher"){
                        if(!launcher.empty()||itemKey.empty()||itemKey==wanted||!itemKey.starts_with(directory+"\\"))return Json::object();
                        launcher=path;
                    }
                }
                if(selected!=1||primaryRows!=1||launcher.empty())return Json::object();
                matched=&game;
            }
            if(launcher.empty())return Json::object();
            const auto launcherKey=key(launcher);
            for(const auto& game:games)if(game.is_object() && &game!=matched){
                if(key(stringField(game,"primaryExecutable"))==launcherKey)return Json::object();
                const auto candidates=game.find("candidates");
                if(candidates==game.end()||!candidates->is_array())continue;
                for(const auto& item:*candidates)if(item.is_object()){
                    const auto path=key(stringField(item,"path"));
                    if(path==launcherKey||path==wanted)return Json::object();
                }
            }
            const auto config=read(root_/L"config"/L"library-config.json");
            const auto buckets=config.value("manualBuckets",Json::object());
            if(!buckets.is_object())return Json::object();
            for(auto it=buckets.begin();it!=buckets.end();++it){
                const auto bucket=key(it.key());
                if((bucket==wanted||bucket==launcherKey||bucket==directory)&&it.value().is_object()&&stringField(it.value(),"bucket")=="non-game")return Json::object();
            }
            const auto proof=identity(launcher);
            if(!proof.value("verified",false))return Json::object();
            const auto appId=proof.value("appId",int64_t{0});if(appId<=0)return Json::object();
            auto video=resolveDocument(j,{{"appId",appId}});
            if(!playable(video,appId)||key(stringField(video,"executable"))!=launcherKey)return Json::object();
            video["currentGameWallpaperFallback"]=true;
            return video;
        }catch(...){return Json::object();}
    }
public:
    Json registerEntry(const Json& a){
        if(!workDemand::allowed(a))return steamDemand::inactive();
        Lock lock(root_);if(!workDemand::allowed(a))return steamDemand::inactive();auto j=document();const auto key=canonicalKey(j,a);const bool exists=j["entries"].contains(key);auto e=seed(j,a);
        const bool changed=adoptStatic(e);if(changed){e["bindingRevision"]=e.value("bindingRevision",uint64_t{0})+1;releasePreparation(e);}
        e=normalizeEntry(e);if(!exists||changed||j.value("_mediaChanged",false)){j["entries"][key]=e;workDemand::require(a);save(j);}return e;
    }
    Json begin(const Json& a,const std::string& manualRequest={}){
        if(!workDemand::allowed(a))return steamDemand::inactive();
        Lock lock(root_);if(!workDemand::allowed(a))return steamDemand::inactive();auto j=document();const auto originalKey=keyFor(a);const bool existed=j["entries"].contains(canonicalKey(j,a));auto e=seed(j,a);const bool adopted=adoptStatic(e);if(adopted){e["bindingRevision"]=e.value("bindingRevision",uint64_t{0})+1;releasePreparation(e);}e=normalizeEntry(e);
        const bool revoked=(!powerPolicy::allowed(e)||steamDemand::steam(e)&&e.value("leaseUntil",int64_t{0})>0&&!e.value("taskId",std::string{}).empty()&&e.value("leasePid",DWORD{0})==GetCurrentProcessId()&&!workDemand::allowed(e));
        if(revoked){releasePreparation(e);e["status"]=e.value("ready",false)?"ready":"missing";}
        const bool retryOnce=safeKey(manualRequest)&&e.value("lastManualRetry",std::string{})!=manualRequest;
        Json slots=Json::array();for(const auto* slot:{"dynamic","static"}){
            bool exists=e.value(std::string(slot)+"Ready",false);std::error_code ec;if(exists)exists=fs::is_regular_file(custom_steam_library::ioPath(assetPath(e["assets"][slot])),ec)&&!ec;
            const bool refresh=pendingMediaRefresh(e,slot)||(std::string(slot)=="dynamic"&&a.value("videoQualityPolicy",0)>=videoQualityPolicy&&needsVideoQualityUpgrade(e,now()));
            if((!exists||refresh)&&!e["deletedSlots"].value(slot,false)&&(!e["manualSlots"].value(slot,false)||refresh)&&!(e.value(std::string(slot)=="dynamic"?"videoUnavailableUntil":"staticUnavailableUntil",int64_t{0})>now()&&!refresh&&!retryOnce))slots.push_back(slot);
        }
        const auto returnExisting=[&](){if(!existed||adopted||revoked||j.value("_mediaChanged",false)){j["entries"][e["key"].get<std::string>()]=e;workDemand::require(a);save(j);}e["acquired"]=false;return e;};
        if(e.value("deleted",false)||slots.empty())return returnExisting();
        if((e.value("leaseUntil",int64_t{0})>now()&&!leaseOwnerGone(e))||(e.value("retryAt",int64_t{0})>now()&&!retryOnce))return returnExisting();
        if(retryOnce){e["lastManualRetry"]=manualRequest;e["retryAt"]=0;e["attempts"]=0;e["error"]="";}
        if(!workDemand::allowed(a))return steamDemand::inactive();
        if(steamDemand::steam(a)){e["demandEpoch"]=a["demandEpoch"];e["demandRevision"]=a["demandRevision"];}else{e.erase("demandEpoch");e.erase("demandRevision");}
        if(!e.value("refreshRequested",false)&&pendingMediaRefresh(e,"dynamic")){e["refreshRequested"]=true;e["refreshSlot"]="dynamic";}
        powerPolicy::stamp(e);e["demandConsumer"]=a.value("demandConsumer",std::string{"library"});e["taskId"]=randomId();e["taskSlots"]=slots;e["leasePid"]=GetCurrentProcessId();e["leaseBirth"]=processBirthToken(GetCurrentProcessId());e["leaseUntil"]=now()+600000;e["status"]="pending";e["updatedAt"]=now();
        if(e.value("name",std::string{}).empty())e["name"]=a.value("name",std::string{});
        if(e.value("executable",std::string{}).empty())e["executable"]=a.value("executable",std::string{});
        j["entries"][e["key"].get<std::string>()]=e;workDemand::require(a);workDemand::require(e);save(j);e["acquired"]=true;return e;
    }
    fs::path assetPath(const Json& asset)const{
        const auto id=asset.value("id",std::string{}),ext=asset.value("extension",std::string{});
        if(!safeKey(id)||(ext!="mp4"&&ext!="webm"&&ext!="jpg"&&ext!="png"&&ext!="webp"&&ext!="bmp"&&ext!="ico"))throw std::runtime_error("media-invalid-asset");
        const auto storage=asset.value("storage",std::string{});if(storage.empty())return root_/L"artwork"/L"video"/wide(id+"."+ext);
        const auto relative=fs::path(wide(storage));if(relative.is_absolute()||relative.empty()||*relative.begin()!=L"artwork")throw std::runtime_error("media-invalid-storage");
        for(const auto& part:relative)if(part==L"..")throw std::runtime_error("media-invalid-storage");
        // Resolve at the IO boundary, then keep the public asset path portable.
        const auto canonical=[](const fs::path& input){
            auto value=fs::weakly_canonical(custom_steam_library::ioPath(input)).wstring();
            if(value.starts_with(LR"(\\?\UNC\)"))value=LR"(\\)"+value.substr(8);
            else if(value.starts_with(LR"(\\?\)"))value=value.substr(4);
            return fs::path(value);
        };
        const auto path=canonical(root_/relative);const auto base=normalized(utf8(canonical(root_).wstring()))+"\\";
        if(normalized(utf8(path.wstring())).rfind(base,0)!=0)throw std::runtime_error("media-storage-escape");return path;
    }
    static const Json* playback30Asset(const Json& original) {
        const auto it=original.find("playback30");
        if(it==original.end()||!it->is_object()||it->value("policy",0)!=playback30Policy||
           it->value("sourceRevision",std::string{})!=original.value("revision",std::string{})||
           it->value("sourceId",std::string{})!=original.value("id",std::string{}))return nullptr;
        const auto asset=it->find("asset");
        if(asset==it->end()||!asset->is_object()||asset->value("extension",std::string{})!="mp4"||
           asset->value("kind",std::string{})!="video"||asset->contains("storage")||
           !capacity::ownedName(wide(asset->value("id",std::string{})+".mp4")))return nullptr;
        const auto cadence=asset->value("cadence",Json::object());
        const auto schema=cadence.is_object()?cadence.value("schema",0):0;
        // These derivatives were validated at publication, independently of the original's probe schema.
        return schema>=1&&schema<=frameProbe::cadenceSchema&&!cadence.value("exceeds30",true)?&*asset:nullptr;
    }
    Json playback30Selection(const Json& entry) const {
        if(!entry.value("ready",false)||entry.value("deleted",false)||preferredVideoHeight(entry)==1080)return Json::object();
        const auto selected=entry.find("asset");if(selected==entry.end()||!selected->is_object())return Json::object();
        return readyPlayback30(*selected);
    }
    // This only decides whether to look for an optional optimization.
    static bool originalWallpaperNeedsPlayback30(const Json& entry) {
        const auto selected=entry.find("asset");
        if(selected==entry.end()||stringField(*selected,"kind")!="video"||preferredVideoHeight(entry)==1080)return false;
        const auto ext=stringField(*selected,"extension");
        if(ext!="mp4"&&ext!="webm")return false;
        const auto cadence=selected->find("cadence");
        return cadence==selected->end()||!cadence->is_object()||
               !cadence->contains("schema")||(*cadence)["schema"]!=frameProbe::cadenceSchema||
               !cadence->contains("exceeds30")||(*cadence)["exceeds30"]!=false||
               !steamDemand::integer(*cadence,"samples",UINT32_MAX,true)||(*cadence)["samples"]<2||
               !steamDemand::integer(*cadence,"timescale",UINT32_MAX,true)||!steamDemand::integer(*cadence,"ticks",UINT64_MAX,true);
    }
    static std::string wallpaperPlaybackStamp(const Json& entry) {
        const auto selected=entry.find("asset");
        if(!entry.value("ready",false)||entry.value("deleted",false)||selected==entry.end()||
           stringField(*selected,"kind")!="video"||preferredVideoHeight(entry)==1080)return {};
        // Recording cadence alone does not change the currently playable asset.
        const auto* variant=playback30Asset(*selected);
        return variant?stringField(*variant,"id"):stringField(*selected,"id");
    }
    static Json playback30Claim(const Json& entry) {
        if(!entry.value("ready",false)||entry.value("deleted",false)||preferredVideoHeight(entry)==1080)return Json::object();
        const auto selected=entry.find("asset");
        if(selected==entry.end()||stringField(*selected,"kind")!="video"||
           selected->value("playback30RejectedPolicy",0)==playback30Policy)return Json::object();
        if(!originalWallpaperNeedsPlayback30(entry))return Json::object();
        // The local queue can discard an older request for this row. The
        // worker still authorizes the exact asset, independent of other edits.
        return {{"key",entry.at("key")},{"id",selected->at("id")},{"revision",selected->at("revision")},
                {"bindingRevision",entry.value("bindingRevision",uint64_t{0})}};
    }
    Json readyPlayback30(const Json& original)const {
        const auto* variant=playback30Asset(original);if(!variant)return Json::object();
        const auto path=custom_steam_library::ioPath(assetPath(*variant));
        std::error_code ec;if(!fs::is_regular_file(fs::symlink_status(path,ec))||ec)return Json::object();
        const auto bytes=fs::file_size(path,ec);
        return !ec&&bytes&&bytes==variant->value("bytes",uint64_t{0})?*variant:Json::object();
    }
    // Accept identities, never paths from the caller. Also recheck at commit
    // so an old conversion cannot attach to a replacement or a deleted row.
    Json playback30Source(const Json& claim)const {
        Lock lock(root_);const auto j=readOnlyDocument();
        if(const auto* row=playback30Row(*j,claim))return (*row)["assets"]["dynamic"];
        return Json::object();
    }
    fs::path playback30Staging(const std::string& token)const {
        if(token.size()!=32||!capacity::ownedName(wide(token+".mp4")))throw std::runtime_error("media-playback30-job-invalid");
        return root_/L"cache"/L"jobs"/L"game-media-playback"/wide(token+".mp4");
    }
    bool recordPlayback30Cadence(const Json& claim,frameProbe::Cadence cadence,const std::function<void()>& check) {
        if(!cadence)return false;
        Lock lock(root_);check();Json raw;const auto j=document(&raw);const auto* current=playback30Row(j,claim);
        if(!current)return false;
        auto row=*current;auto& original=row["assets"]["dynamic"];
        const auto value=cadenceRecord(cadence);
        if(original.value("cadence",Json::object())==value)return true;
        original["cadence"]=value;raw["entries"][claim["key"].get<std::string>()]=normalizeEntry(std::move(row));
        check();save(raw);return true;
    }
    bool publishPlayback30(const Json& claim,const std::string& token,unsigned encodedFrames,
                           frameProbe::Cadence sourceCadence,const std::function<void()>& check) {
        check();const auto staged=playback30Staging(token);const auto disk=custom_steam_library::ioPath(staged);
        std::error_code ec;
        if(!fs::is_regular_file(fs::symlink_status(disk,ec))||ec)throw std::runtime_error("media-playback30-file-invalid");
        const auto cadence=frameProbe::inspectMp4(disk);
        if(!sourceCadence.exceeds30()||!cadence||cadence.exceeds30()||cadence.averageFps()<29.99||cadence.samples!=encodedFrames||
           std::abs(double(sourceCadence.ticks)/sourceCadence.timescale-double(cadence.ticks)/cadence.timescale)>0.035)
            throw std::runtime_error("media-playback30-cadence-invalid");
        const auto bytes=fs::file_size(disk,ec);
        if(ec||!bytes||bytes>capacity::maxAssetBytes)throw std::runtime_error("media-playback30-size-invalid");
        Lock lock(root_);check();Json raw;const auto j=document(&raw);const auto* current=playback30Row(j,claim);
        if(!current||!playback30Selection(*current).empty())return false;
        capacity::require(root_,bytes,true);
        auto row=*current;auto& original=row["assets"]["dynamic"];
        const auto id=randomId();const Json newAsset={{"id",id},{"revision",id},{"extension","mp4"},
            {"kind","video"},{"mime","video/mp4"},{"bytes",bytes},{"cadence",cadenceRecord(cadence)}};
        const auto destination=custom_steam_library::ioPath(assetPath(newAsset));
        original["cadence"]=cadenceRecord(sourceCadence);
        original["playback30"]={{"policy",playback30Policy},{"sourceId",claim["id"]},
            {"sourceRevision",claim["revision"]},{"asset",newAsset}};
        raw["entries"][claim["key"].get<std::string>()]=normalizeEntry(std::move(row));
        check();fs::rename(disk,destination);
        try{check();save(raw);}catch(...){fs::remove(destination,ec);throw;}
        return true;
    }
private:
    static const Json* playback30Row(const Json& document,const Json& claim) {
        const auto key=claim.value("key",std::string{});
        if(!safeKey(key))return nullptr;
        const auto& entries=document["entries"];const auto row=entries.find(key);
        if(row==entries.end()||row->value("deleted",false)||row->value("wallpaperMode",std::string{})!="dynamic"||
           !row->value("dynamicReady",false)||preferredVideoHeight(*row)==1080)return nullptr;
        const auto& source=(*row)["assets"]["dynamic"];
        if(source.value("playback30RejectedPolicy",0)==playback30Policy)return nullptr;
        const auto id=source.value("id",std::string{}),revision=source.value("revision",std::string{});
        const auto ext=stringField(source,"extension");
        return !id.empty()&&!revision.empty()&&(ext=="mp4"||ext=="webm")&&
            id==claim.value("id",std::string{})&&revision==claim.value("revision",std::string{})?&*row:nullptr;
    }
public:
    Json publish(const Json& a,const fs::path& source,bool manual){
        if(manual)steamDemand::require(a);else workDemand::require(a);
        const auto ext=mediaKind(source);std::error_code ec;const auto bytes=fs::file_size(custom_steam_library::ioPath(source),ec);if(ec||bytes==0||bytes>capacity::maxAssetBytes)throw std::runtime_error("media-size-invalid");
        const auto slot=ext=="mp4"||ext=="webm"?"dynamic":"static";
        if(a.contains("slot")&&a.value("slot",std::string{})!=slot)throw std::runtime_error("media-slot-container-mismatch");
        const auto cadence=ext=="mp4"?frameProbe::inspectMp4(custom_steam_library::ioPath(source)):frameProbe::Cadence{};
        Lock lock(root_);if(manual)steamDemand::require(a);else workDemand::require(a);auto j=document();auto e=seed(j,a);if(!manual)workDemand::require(e);const auto expected=a.value("bindingRevision",uint64_t{0});
        if(expected!=e.value("bindingRevision",uint64_t{0})||(!manual&&(e["manualSlots"].value(slot,false)||e["deletedSlots"].value(slot,false)||e.value("taskId",std::string{})!=a.value("taskId",std::string{}))))throw std::runtime_error("media-stale-publication");
        if(manual&&std::string(slot)=="static")e["libraryStaticSource"]=localStatic(e.value("executable",std::string{}),e.value("gameDirectory",std::string{}));
        const auto id=randomId();Json asset={{"id",id},{"revision",id},{"extension",ext},{"kind",std::string(slot)=="dynamic"?"video":"image"},{"mime",ext=="mp4"?"video/mp4":ext=="webm"?"video/webm":ext=="jpg"?"image/jpeg":"image/"+ext},{"bytes",bytes}};
        if(cadence)asset["cadence"]=cadenceRecord(cadence);
        if(!manual&&std::string(slot)=="dynamic"&&a.value("videoQualityPolicy",0)>=videoQualityPolicy){asset["qualityPolicy"]=videoQualityPolicy;asset["targetHeight"]=preferredVideoHeight(e);if(a.value("selectedHeight",0)>0)asset["selectedHeight"]=a["selectedHeight"];}
        capacity::require(root_,bytes,!manual);
        const auto path=assetPath(asset);fs::create_directories(custom_steam_library::ioPath(path.parent_path()));fs::copy_file(custom_steam_library::ioPath(source),custom_steam_library::ioPath(path),fs::copy_options::none);
        try{if(manual)steamDemand::require(a);else{workDemand::require(a);workDemand::require(e);}}catch(...){fs::remove(custom_steam_library::ioPath(path),ec);throw;}
        e["assets"][slot]=asset;e["invalidSlots"].erase(slot);e["manualSlots"][slot]=manual;e["deletedSlots"][slot]=false;e["bindingRevision"]=expected+1;e["manual"]=e["manualSlots"].value(e["wallpaperMode"].get<std::string>(),false);e["deleted"]=false;
        if(e.contains("taskSlots")){Json remaining=Json::array();for(const auto& value:e["taskSlots"])if(value!=slot)remaining.push_back(value);e["taskSlots"]=remaining;}
        if(pendingMediaRefresh(e,slot))setMediaRefresh(e,slot,false);
        if(manual){releasePreparation(e);if(std::string(slot)=="dynamic")e.erase("selectedVideo");}
        e["status"]="ready";e["retryAt"]=0;e["error"]="";e["updatedAt"]=now();e["source"]=manual?"manual":"steam";if(!manual&&a.value("verified",false))e["identityVerified"]=true;
        if(!manual&&!a.value("name",std::string{}).empty())e["name"]=a.value("name",std::string{});
        if(!manual&&a.contains("legacySourceFingerprint"))e["legacySourceFingerprint"]=a["legacySourceFingerprint"];
        e=normalizeEntry(e);j["entries"][e["key"].get<std::string>()]=e;
        try{save(j);}catch(...){fs::remove(custom_steam_library::ioPath(path),ec);throw;}return e;
    }
    Json importLegacy(const Json& a,const fs::path& source,const std::function<bool()>& admitted){
        if(!workDemand::allowed(a))return steamDemand::inactive();
        auto current=resolve(a);if(current.value("dynamicReady",false)||current.value("deleted",false)||current.value("manualSlots",Json::object()).value("dynamic",false)||current.value("deletedSlots",Json::object()).value("dynamic",false))return current;
        const auto fingerprint=digestKey(normalized(utf8(fs::absolute(source).wstring()))+":"+std::to_string(fs::file_size(custom_steam_library::ioPath(source)))+":"+std::to_string(fs::last_write_time(custom_steam_library::ioPath(source)).time_since_epoch().count()));
        if(current.value("legacySourceFingerprint",std::string{})==fingerprint)return current;
        const bool explicitLease=!a.value("taskId",std::string{}).empty();
        const bool ownLease=current.value("leasePid",DWORD{0})==GetCurrentProcessId()&&!leaseOwnerGone(current)&&current.value("leaseUntil",int64_t{0})>now()&&!current.value("taskId",std::string{}).empty();
        const bool borrowed=explicitLease||ownLease;auto lease=borrowed?current:begin(a);
        if(borrowed&&(lease.value("leasePid",DWORD{0})!=GetCurrentProcessId()||leaseOwnerGone(lease)||explicitLease&&(lease.value("taskId",std::string{})!=a.value("taskId",std::string{})||lease.value("bindingRevision",uint64_t{0})!=a.value("bindingRevision",uint64_t{0}))))throw std::runtime_error("media-stale-legacy-lease");
        if(!borrowed&&!lease.value("acquired",false))return current;
        try{if(!admitted()){if(!borrowed)finish(lease);return resolve(a);}auto publication=lease;publication["verified"]=a.value("verified",false);publication["legacySourceFingerprint"]=fingerprint;auto entry=publish(publication,source,false);if(!explicitLease)finish(entry);return entry;}
        catch(...){if(!borrowed)fail(lease);throw;}
    }
    Json finish(const Json& a){
        if(!workDemand::allowed(a))return steamDemand::inactive();
        Lock lock(root_);if(!workDemand::allowed(a))return steamDemand::inactive();auto j=document();const auto key=canonicalKey(j,a);if(!j["entries"].contains(key))return Json::object();auto e=seed(j,a);if(!workDemand::allowed(e))return steamDemand::inactive();
        if(e.value("taskId",std::string{})!=a.value("taskId",std::string{})||e.value("bindingRevision",uint64_t{0})!=a.value("bindingRevision",uint64_t{0}))return e;
        if(a.value("videoUnavailable",false))e["videoUnavailableUntil"]=now()+24ll*60*60*1000;
        if(a.value("staticUnavailable",false))e["staticUnavailableUntil"]=now()+24ll*60*60*1000;
        if(a.value("videoUnavailable",false)&&e.value("selectedVideo",Json::object()).empty()&&pendingMediaRefresh(e,"dynamic"))setMediaRefresh(e,"dynamic",false);
        if(a.value("staticUnavailable",false)&&pendingMediaRefresh(e,"static"))setMediaRefresh(e,"static",false);
        e["leaseUntil"]=0;e["taskSlots"]=Json::array();e["retryAt"]=0;e["error"]="";e["attempts"]=0;e["status"]=e.contains("asset")?"ready":"missing";j["entries"][key]=e;workDemand::require(a);workDemand::require(e);save(j);return e;
    }
    // Cancellation releases only the exact owned lease. It is cleanup, so a
    // revoked power/foreground ticket cannot prevent it; budgets stay intact.
    Json cancel(const Json& a){
        Lock lock(root_);auto j=document();const auto key=canonicalKey(j,a);
        if(!j["entries"].contains(key))return Json::object();auto e=j["entries"][key];
        if(a.value("taskId",std::string{}).empty()||e.value("taskId",std::string{})!=a.value("taskId",std::string{})||
           e.value("bindingRevision",uint64_t{0})!=a.value("bindingRevision",uint64_t{0})||
           e.value("leasePid",DWORD{0})!=GetCurrentProcessId()||leaseOwnerGone(e))return e;
        e["taskId"]="";e["leaseUntil"]=0;e["status"]=e.value("ready",false)?"ready":"missing";
        j["entries"][key]=e;save(j);return e;
    }
    Json fail(const Json& a){
        if(!workDemand::allowed(a))return steamDemand::inactive();
        Lock lock(root_);if(!workDemand::allowed(a))return steamDemand::inactive();auto j=document();const auto key=canonicalKey(j,a);if(!j["entries"].contains(key))return Json::object();auto e=seed(j,a);if(!workDemand::allowed(e))return steamDemand::inactive();
        if(e.value("taskId",std::string{})!=a.value("taskId",std::string{})||e.value("bindingRevision",uint64_t{0})!=a.value("bindingRevision",uint64_t{0}))return e;
        // Preserve conclusive slot absence alongside a different slot's error.
        // Only the exact current lease's requested slots may acquire a deadline.
        if(a.value("videoUnavailable",false)||a.value("staticUnavailable",false)){
            const auto slots=e.value("taskSlots",Json::array());
            const auto absent=[&](const char* slot,const char* flag,const char* deadline){
                if(!a.value(flag,false)||std::find(slots.begin(),slots.end(),slot)==slots.end())return;
                e[deadline]=now()+24ll*60*60*1000;
                if(pendingMediaRefresh(e,slot))setMediaRefresh(e,slot,false);
            };
            absent("dynamic","videoUnavailable","videoUnavailableUntil");
            absent("static","staticUnavailable","staticUnavailableUntil");
        }
        const auto attempts=std::min(8,e.value("attempts",0)+1);e["status"]="error";e["attempts"]=attempts;e["leaseUntil"]=0;e["retryAt"]=now()+(e.value("demandConsumer",std::string{})=="background"?20000ll:std::min<int64_t>(3600000,30000ll*(1ll<<attempts)));e["error"]=a.value("reason",std::string{"prepare-failed"}).substr(0,160);j["entries"][key]=e;workDemand::require(a);workDemand::require(e);save(j);return e;
    }
    Json edit(const Json& a){
        if(!steamDemand::allowed(a))return steamDemand::inactive();
        Lock lock(root_);if(!steamDemand::allowed(a))return steamDemand::inactive();const bool recovering=a.value("action",std::string{})=="playback-recovered";
        Json original;auto j=document(recovering?&original:nullptr);
        if(recovering){
            // Maintenance follows an explicit successful decode of these exact
            // retained bytes. Time alone never revives an invalid asset.
            const auto key=stringField(a,"key"),slot=stringField(a,"slot"),assetId=stringField(a,"assetId");
            if(!safeKey(key)||(slot!="static"&&slot!="dynamic")||assetId.empty()||!a.contains("bindingRevision")||!a["bindingRevision"].is_number_integer())throw std::runtime_error("media-recovery-identity-required");
            if(canonicalKey(j,a)!=key||!j["entries"].contains(key))throw std::runtime_error("media-recovery-key-mismatch");
            auto e=j["entries"][key];const auto revision=e.value("bindingRevision",uint64_t{0});
            if(a["bindingRevision"].get<uint64_t>()!=revision||revision==UINT64_MAX)throw std::runtime_error("media-edit-conflict");
            if(e.value("deleted",false)||e["deletedSlots"].value(slot,false))throw std::runtime_error("media-recovery-deleted");
            if(stringField(e,"key")!=key||!e["assets"].contains(slot)||e["assets"][slot].value("id",std::string{})!=assetId||e["invalidSlots"].value(slot,std::string{})!=assetId)throw std::runtime_error("media-recovery-not-invalid");
            // A revision-changing recovery must not take over a preparation
            // lease or cancel a pending replacement owned by another request.
            if(e.value("leaseUntil",int64_t{0})!=0||!e.value("taskSlots",Json::array()).empty())throw std::runtime_error("media-recovery-preparing");
            const auto& asset=e["assets"][slot];std::error_code ec;const auto path=assetPath(asset);
            if(!fs::is_regular_file(custom_steam_library::ioPath(path),ec)||ec)throw std::runtime_error("media-recovery-file-unavailable");
            const auto bytes=fs::file_size(custom_steam_library::ioPath(path),ec);
            if(ec||!bytes||bytes!=asset.value("bytes",uint64_t{0}))throw std::runtime_error("media-recovery-file-changed");
            e["invalidSlots"].erase(slot);
            if(e.value("error",std::string{})=="playback-failed"&&e["invalidSlots"].empty()){e["error"]="";e["retryAt"]=0;e["attempts"]=0;}
            e["bindingRevision"]=revision+1;e["updatedAt"]=now();normalizeEntryInPlace(e);
            if(e.value("error",std::string{}).empty())e["status"]=e.value("ready",false)?"ready":"missing";
            original["entries"][key]=e;save(original);return e;
        }
        auto e=seed(j,a);if(a.value("bindingRevision",uint64_t{0})!=e.value("bindingRevision",uint64_t{0}))throw std::runtime_error("media-edit-conflict");
        const auto action=a.value("action",std::string{});const auto slot=a.value("slot",e.value("wallpaperMode",std::string{"dynamic"}));if(slot!="static"&&slot!="dynamic")throw std::runtime_error("media-slot-invalid");
        if(slot=="static"&&(action=="delete"||action=="redownload"))e["libraryStaticSource"]=localStatic(e.value("executable",std::string{}),e.value("gameDirectory",std::string{}));
        if(action=="delete"){if(slot=="dynamic")e.erase("selectedVideo");e["assets"].erase(slot);e["deletedSlots"][slot]=true;e["manualSlots"][slot]=true;setMediaRefresh(e,slot,false);e["status"]="deleted";}
        else if(action=="select-video"){const auto selected=a.value("selectedVideo",Json::object());if(selected.value("appId",int64_t{0})!=e.value("appId",int64_t{0})||stringField(selected,"url").empty())throw std::runtime_error("media-video-choice-invalid");e["selectedVideo"]=selected;e["deletedSlots"]["dynamic"]=false;e["manualSlots"]["dynamic"]=false;e["videoUnavailableUntil"]=0;e["retryAt"]=0;e["deleted"]=false;setMediaRefresh(e,"dynamic",true);e["status"]="missing";}
        else if(action=="redownload"){
            // Queuing a replacement does not make the retained asset playable.
            e["deletedSlots"][slot]=false;e["manualSlots"][slot]=false;e["manual"]=false;e["deleted"]=false;setMediaRefresh(e,slot,true);e["videoUnavailableUntil"]=0;e["staticUnavailableUntil"]=0;e["retryAt"]=0;e["status"]="missing";
        }
        else if(action=="playback-failed"){
            const auto assetId=a.value("assetId",std::string{});if(!e["assets"].contains(slot)||assetId.empty())throw std::runtime_error("media-stale-playback");
            auto& source=e["assets"][slot];
            const auto* reduced=slot=="dynamic"?playback30Asset(source):nullptr;
            if(reduced&&reduced->value("id",std::string{})==assetId){
                // Fence old playback responses without invalidating or downloading the source.
                source.erase("playback30");source["playback30RejectedPolicy"]=playback30Policy;
            }else{
                if(source.value("id",std::string{})!=assetId)throw std::runtime_error("media-stale-playback");
                if(e["invalidSlots"].value(slot,std::string{})==assetId)return e;
                e["invalidSlots"][slot]=assetId;e["status"]="error";e["error"]="playback-failed";const auto attempts=std::min(8,e.value("attempts",0)+1);e["attempts"]=attempts;e["retryAt"]=now()+std::min<int64_t>(3600000,30000ll*(1ll<<attempts));
            }
        }
        else if(action=="rename"){const auto name=a.value("name",std::string{});if(name.empty()||name.size()>512)throw std::runtime_error("media-invalid-name");e["name"]=name;}
        else if(action=="mode"){const auto mode=a.value("mode",std::string{});if(mode!="static"&&mode!="dynamic")throw std::runtime_error("media-mode-invalid");e["wallpaperMode"]=mode;e["modeExplicit"]=true;}
        else if(action=="playback"){const double start=a.value("start",0.0),end=a.value("end",0.0);const auto fit=a.value("fit",std::string{"cover"});if(!std::isfinite(start)||!std::isfinite(end)||start<0||start>86400||end<0||end>86400||(end>0&&end<=start)||(fit!="cover"&&fit!="contain"))throw std::runtime_error("media-playback-range-invalid");e["playback"]={{"start",start},{"end",end},{"fit",fit}};e["playbackExplicit"]=true;}
        else throw std::runtime_error("media-edit-action-invalid");
        e["bindingRevision"]=e.value("bindingRevision",uint64_t{0})+1;releasePreparation(e);e["updatedAt"]=now();e=normalizeEntry(e);j["entries"][e["key"].get<std::string>()]=e;steamDemand::require(a);save(j);return e;
    }
    Json rememberIdentity(const std::string& executable,const fs::path& manifestPath,int64_t startedAt){
        namespace cache=custom_steam_library::identity_cache;
        try {
            const auto target=cache::executableKey(executable);
            const auto manifest=cache::readSnapshot(manifestPath);
            // Worker manifests use schema 2; the durable identity cache is
            // still schema 1. Do not weaken the cache reader or admit future
            // worker schemas just to consume this known production document.
            const auto version=manifest.value.find("schemaVersion");
            const bool workerV2=version!=manifest.value.end()&&version->is_number_integer()&&
                *version==2&&!manifest.value.contains("schema");
            if(!manifest.parsed||(!cache::knownSchema(manifest.value)&&!workerV2))throw std::runtime_error("identity-observation-unknown");
            const auto& observation=manifest.value;
            if(!observation.contains("exe")||!observation["exe"].is_string()||cache::executableKey(observation["exe"].get<std::string>())!=target)
                throw std::runtime_error("identity-observation-target-mismatch");
            const auto match=observation.value("match",Json::object());
            if(!custom_steam_library::verifiedSteamIdentityMatch(match))return identity(executable);
            if(observation.contains("metadata")&&!observation["metadata"].is_object())throw std::runtime_error("identity-observation-metadata-invalid");
            const auto time=now();if(startedAt<=0||startedAt>time+300000)throw std::runtime_error("identity-observation-time-invalid");
            struct Owner {HANDLE mutex=nullptr;bool acquired=false;~Owner(){if(mutex){if(acquired)ReleaseMutex(mutex);CloseHandle(mutex);}}} owner;
            owner.mutex=CreateMutexW(nullptr,FALSE,L"Local\\YeManCustomSteamLibraryDataTransaction");
            if(!owner.mutex)throw std::runtime_error("identity-lock-failed");
            const auto waited=WaitForSingleObject(owner.mutex,10000);owner.acquired=waited==WAIT_OBJECT_0||waited==WAIT_ABANDONED;
            if(!owner.acquired)throw std::runtime_error("identity-owner-busy");
            const auto path=cache::cachePath(root_,executable);const auto previous=cache::readSnapshot(path);
            if(previous.parsed&&cache::futureSchema(previous.value))throw std::runtime_error("identity-future-cache-preserved");
            if(previous.parsed&&previous.value.contains("executable")&&previous.value["executable"].is_string()){
                std::string oldTarget;try{oldTarget=cache::executableKey(previous.value["executable"].get<std::string>());}catch(...){}
                if(!oldTarget.empty()&&oldTarget!=target)throw std::runtime_error("identity-foreign-binding-preserved");
            }
            bool valid=false;try{valid=cache::recordValid(previous,executable,time);}catch(...){valid=false;}
            if(valid&&cache::integer(previous.value,"updatedAt")>startedAt){auto winner=identity(executable);winner["cachePersisted"]=false;winner["cacheError"]="newer-observation-retained";return winner;}
            auto value=valid?previous.value:Json::object();
            value["schemaVersion"]=1;value["executable"]=executable;value["manifestPath"]=utf8(manifestPath.wstring());value["updatedAt"]=time;
            value["match"]=match;value["metadata"]=observation.value("metadata",Json::object());
            value["formalName"]=stringField(match,"formalName",stringField(match,"displayName"));value["steamAppId"]=cache::integer(match,"appId");
            // A new observation is authority only while its exact bytes/file identity remain current.
            if(!cache::sameSnapshot(manifest,cache::readSnapshot(manifestPath)))throw std::runtime_error("identity-observation-changed");
            cache::commit(path,previous,value,previous.exists&&!valid,[&]{
                if(!cache::sameSnapshot(manifest,cache::readSnapshot(manifestPath)))throw std::runtime_error("identity-observation-changed");
            });
            auto result=identity(executable);
            if(!result.value("verified",false)||result.value("appId",int64_t{0})!=cache::integer(match,"appId"))throw std::runtime_error("identity-final-consumer-readback-failed");
            result["cachePersisted"]=true;return result;
        }catch(const std::exception& error){return {{"verified",false},{"cachePersisted",false},{"cacheError",std::string(error.what()).substr(0,160)}};}
        catch(...){return {{"verified",false},{"cachePersisted",false},{"cacheError","identity-observation-unavailable"}};}
    }
    Json identity(const std::string& executable,bool* storageUnavailable=nullptr)const{
        if(storageUnavailable)*storageUnavailable=false;
        try{namespace cache=custom_steam_library::identity_cache;
            const auto snapshot=cache::readSnapshot(cache::cachePath(root_,executable));
            if(!cache::recordValid(snapshot,executable,now()))return Json::object();
            const auto& value=snapshot.value;const auto& match=value["match"];
            return {{"appId",cache::integer(match,"appId")},{"name",stringField(value,"formalName",stringField(match,"formalName",stringField(match,"displayName")))},
                {"verified",true},{"source","library-identity"}};
        }catch(const std::exception& error){
            if(storageUnavailable)*storageUnavailable=custom_steam_library::identity_cache::storageFailure(error.what());
            return Json::object();
        }catch(...){return Json::object();} // Unknown cache state never supplies an instance or Steam ID.
    }
    // Display/cache reuse only. Steam membership still comes from the current
    // account VDF; this never changes the EXE used to validate that membership.
    Json shortcutWallpaperIdentity(const std::string& executable,const Json* inventoryState=nullptr)const{
        const auto exact=identity(executable);if(exact.value("verified",false))return exact;
        try{
            namespace cache=custom_steam_library::identity_cache;
            // A saved, corrupt or future exact binding cannot be bypassed by an alias.
            if(cache::readSnapshot(cache::cachePath(root_,executable)).exists)return Json::object();
            const auto key=[](const std::string& value){
                const fs::path path(wide(value));if(!path.is_absolute())return std::string{};
                for(const auto& part:path)if(part==L".."||part==L".")return std::string{};
                return normalized(utf8(path.lexically_normal().wstring()));
            };
            const auto wanted=key(executable);if(wanted.empty())return Json::object();
            const auto owned=inventoryState?nullptr:readInventoryState();
            const auto& state=inventoryState?*inventoryState:*owned;
            if(!state.is_object()||!state.contains("games")||!state["games"].is_array())return Json::object();
            const auto config=read(root_/L"config"/L"library-config.json");
            Json found=Json::object();unsigned associations=0;
            for(const auto& game:state["games"]){
                if(!game.is_object()||!game.contains("candidates")||!game["candidates"].is_array())continue;
                unsigned aliases=0;for(const auto& item:game["candidates"])if(item.is_object()&&key(stringField(item,"path"))==wanted){
                    if(stringField(item,"role")!="launcher")return Json::object();++aliases;
                }
                if(!aliases)continue;if(aliases!=1||++associations!=1)return Json::object();
                if(stringField(game,"status")!="ready"||stringField(game,"contentType")!="game"||
                   stringField(game,"primaryRole")!="main-game"||!game.value("scanComplete",false)||
                   !game.value("uniquePrimaryInvariant",false)||custom_steam_library::builtinExcludedSteamTool(game))return Json::object();
                const auto primary=stringField(game,"primaryExecutable"),primaryKey=key(primary),directory=key(stringField(game,"gameDirectory"));
                if(directory.empty()||directory.back()=='\\'||primaryKey==wanted||
                   !wanted.starts_with(directory+"\\")||!primaryKey.starts_with(directory+"\\"))return Json::object();
                unsigned selected=0;for(const auto& item:game["candidates"])if(item.is_object()&&item.value("selected",false)){
                    if(key(stringField(item,"path"))!=primaryKey||stringField(item,"role")!="main-game")return Json::object();++selected;
                }
                if(selected!=1)return Json::object();
                if(config.contains("manualBuckets")&&config["manualBuckets"].is_object())for(auto it=config["manualBuckets"].begin();it!=config["manualBuckets"].end();++it){
                    const auto excluded=key(it.key());if((excluded==wanted||excluded==primaryKey||excluded==directory)&&it.value().is_object()&&stringField(it.value(),"bucket")=="non-game")return Json::object();
                }
                found=identity(primary);if(!found.value("verified",false))return Json::object();
                found["source"]="library-launcher-wallpaper";
            }
            return associations==1?found:Json::object();
        }catch(...){return Json::object();} // Missing/ambiguous library facts remain unknown.
    }
    Json collectOrphans()const{
        Lock lock(root_);const auto j=document();std::set<std::wstring> used;
        for(const auto& e:j["entries"])for(const auto& a:e["assets"])if(a.is_object()){
            used.insert(assetPath(a).filename().wstring());
            if(const auto* variant=playback30Asset(a))used.insert(assetPath(*variant).filename().wstring());
        }
        const auto directory=root_/L"artwork"/L"video";std::error_code ec;size_t examined=0,removed=0;
        for(fs::directory_iterator it(directory,ec),end;!ec&&it!=end&&examined<10000;it.increment(ec),++examined){
            if(it->is_symlink(ec)||!it->is_regular_file(ec)||used.count(it->path().filename().wstring()))continue;
            const auto extension=it->path().extension().wstring();if(extension!=L".mp4"&&extension!=L".webm"&&extension!=L".jpg"&&extension!=L".png"&&extension!=L".webp"&&extension!=L".bmp"&&extension!=L".ico")continue;
            const auto id=utf8(it->path().stem().wstring());if(id.size()!=32||!safeKey(id))continue;
            const auto age=fs::file_time_type::clock::now()-it->last_write_time(ec);if(ec||age<std::chrono::hours(24))continue;
            if(fs::remove(it->path(),ec))++removed;ec.clear();
        }return {{"examined",examined},{"removed",removed},{"temporaryRemoved",capacity::collectTemporary(root_)}};
    }
    Json inventory(const Json& shortcuts=Json::array())const{
        const auto scan=readInventoryState();const auto& state=*scan;
        const auto config=read(root_/L"config"/L"library-config.json");Json result=Json::array();
        std::map<std::string,const Json*> added;
        if(shortcuts.is_array())for(const auto& item:shortcuts)if(item.is_object()){
            const auto executable=stringField(item,"executable");if(!executable.empty())added[normalized(executable)]=&item;
        }
        std::set<std::string> seen;
        static constexpr std::wstring_view excludedStems[]={
#include "game_detection_blacklist_entries.h"
        };
        const auto accept=[&](const Json& game){
            if(!game.is_object())return;
            const auto exe=stringField(game,"primaryExecutable");if(exe.empty())return;
            const auto exeKey=normalized(exe);if(!seen.insert(exeKey).second)return;
            if(stringField(game,"contentType")=="non-game"||stringField(game,"status")=="unrecognized-tool"||custom_steam_library::builtinExcludedSteamTool(game))return;
            auto stem=wide(normalized(utf8(fs::path(wide(exe)).stem().wstring())));bool excluded=false;
            for(auto pattern:excludedStems){if(pattern.ends_with(L"*")){pattern.remove_suffix(1);if(stem.starts_with(pattern)){excluded=true;break;}}else if(stem==pattern){excluded=true;break;}}
            if(excluded)return;const auto directory=stringField(game,"gameDirectory");
            if(const auto buckets=config.find("manualBuckets");buckets!=config.end()&&buckets->is_object()){
                const auto directoryKey=directory.empty()?std::string{}:normalized(directory);
                for(auto it=buckets->begin();it!=buckets->end();++it){const auto key=normalized(it.key());if((key==exeKey||!directory.empty()&&key==directoryKey)&&it.value().is_object()&&stringField(it.value(),"bucket")=="non-game")return;}
            }
            const auto found=added.find(exeKey);
            auto id=found!=added.end()?shortcutWallpaperIdentity(exe,&state):identity(exe);if(!id.value("verified",false))id={{"appId",0},{"name",stringField(game,"primaryProductName",stringField(game,"directoryName"))},{"verified",false}};
            id["executable"]=exe;id["gameDirectory"]=directory;id["sourceTier"]=2;id["catalogSource"]="ymcc-confirmed";
            if(found!=added.end()){id["catalogSource"]="steam-shortcut";for(const auto* field:{"shortcutAppId","steamAccount","lastPlayed"})if(found->second->contains(field))id[field]=(*found->second)[field];}
            result.push_back(std::move(id));
        };
        // Keep confirmed rows first without copying the library scan tree.
        if(const auto games=state.find("games");games!=state.end()&&games->is_array())for(const auto& game:*games)accept(game);
        if(shortcuts.is_array())for(const auto& item:shortcuts)if(item.is_object()&&item.contains("executable"))accept({{"primaryExecutable",item["executable"]},{"directoryName",stringField(item,"name")}});
        return result;
    }

};
inline std::wstring inventoryEventName(const fs::path& root){return L"Local\\YMCC-MediaInventoryChanged-"+wide(digestKey(normalized(utf8(fs::absolute(root).wstring()))))+L"-main";}
inline bool signalInventoryChanged(const fs::path& root){HANDLE event=OpenEventW(EVENT_MODIFY_STATE,FALSE,inventoryEventName(root).c_str());if(!event)return false;const bool signaled=SetEvent(event)!=FALSE;CloseHandle(event);return signaled;}
inline bool publishInventoryFile(const fs::path& root,const fs::path& file){
    const auto name=normalized(utf8(file.filename().wstring()));if(name!="library-scan.json"&&name!="steam-import-plan.json"&&name!="steam-add-plan.json"&&name!="library-steam-add-plan.json"&&name!="last-steam-add-all-accounts.json"&&name!="library-config.json"&&name!="manual-overrides.json")return false;
    const auto base=normalized(utf8(fs::absolute(root).lexically_normal().wstring()))+"\\";const auto target=normalized(utf8(fs::absolute(file).lexically_normal().wstring()));
    if(target!=base+"state\\library-scan.json"&&target!=base+"state\\steam-import-plan.json"&&target!=base+"state\\steam-add-plan.json"&&target!=base+"state\\library-steam-add-plan.json"&&target!=base+"state\\last-steam-add-all-accounts.json"&&target!=base+"config\\library-config.json"&&target!=base+"config\\manual-overrides.json")return false;
    return signalInventoryChanged(root);
}
class Watcher {
    HANDLE changed_=nullptr,stop_=nullptr,inventory_=nullptr;std::thread worker_;
public:
    Watcher(const fs::path& root,std::function<void()> changed,const std::wstring& role=L"main",std::function<void()> inventoryChanged={}){
        auto name=L"Local\\YMCC-MediaChanged-"+wide(digestKey(normalized(utf8(fs::absolute(root).wstring()))))+L"-"+role;
        changed_=CreateEventW(nullptr,FALSE,FALSE,name.c_str());stop_=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        if(inventoryChanged)inventory_=CreateEventW(nullptr,FALSE,FALSE,inventoryEventName(root).c_str());
        if(!changed_||!stop_||(inventoryChanged&&!inventory_)){if(changed_)CloseHandle(changed_);if(stop_)CloseHandle(stop_);if(inventory_)CloseHandle(inventory_);throw std::runtime_error("media-watch-unavailable");}
        worker_=std::thread([this,changed,inventoryChanged]{HANDLE events[]={stop_,changed_,inventory_};const DWORD count=inventory_?3:2;
            for(;;){auto r=WaitForMultipleObjects(count,events,FALSE,INFINITE);if(r==WAIT_OBJECT_0+1){try{changed();}catch(...){}}else if(inventory_&&r==WAIT_OBJECT_0+2){try{inventoryChanged();}catch(...){}}else return;}
        });
    }
    ~Watcher(){SetEvent(stop_);if(worker_.joinable())worker_.join();CloseHandle(changed_);CloseHandle(stop_);if(inventory_)CloseHandle(inventory_);}
};
} // namespace ymcc::media
