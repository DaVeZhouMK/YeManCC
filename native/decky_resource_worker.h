#pragma once
// Resource IO runs inside the existing private same-image owned child channel.
// A stalled disk/hash cannot retain the parent Runtime thread after OFF/exit.
#include "decky_binding_client.h"
#include "decky_sidebar_setup.h"
#include <bcrypt.h>
#include <memory>
#pragma comment(lib,"bcrypt.lib")
namespace ymcc::deckyresources::worker {
inline constexpr const char* schema="ymcc.decky.resources.v1";
inline std::string digest(const std::wstring& path,const std::function<bool()>& current){
    struct Hash{BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;~Hash(){if(hash)BCryptDestroyHash(hash);if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);}} state;
    if(!current()||BCryptOpenAlgorithmProvider(&state.algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)return {};
    DWORD length=0,written=0;if(BCryptGetProperty(state.algorithm,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&length),sizeof(length),&written,0)<0)return {};
    std::vector<unsigned char> object(length),buffer(65536),value(32);
    if(BCryptCreateHash(state.algorithm,&state.hash,object.data(),length,nullptr,0,0)<0)return {};
    std::ifstream file(std::filesystem::path(path),std::ios::binary);if(!file)return {};
    while(file&&current()){file.read(reinterpret_cast<char*>(buffer.data()),buffer.size());const auto n=file.gcount();if(n>0&&BCryptHashData(state.hash,buffer.data(),static_cast<ULONG>(n),0)<0)return {};}
    if(!current()||file.bad()||BCryptFinishHash(state.hash,value.data(),32,0)<0)return {};
    const char* hex="0123456789abcdef";std::string result;for(const auto b:value){result+=hex[b>>4];result+=hex[b&15];}return result;
}
inline std::string text(const std::filesystem::path& value){const auto bytes=value.u8string();return {bytes.begin(),bytes.end()};}
// Only the authenticated inherited pipe can invoke this operation. No command
// line path or public IPC starts it, and the child never enters application init.
template<class Emit>bool tryRun(const Json& request,HANDLE cancel,Emit&& emit,int& exit){
    if(!request.is_object()||request.value("schema",std::string{})!=schema)return false;
    Json detail={{"ready",false},{"reason","resource-worker-invalid"}};
    try{
        ymcc::deckybinding::worker::require(request.size()==8&&request.value("operation",std::string{})=="prepare"&&request.at("allowRepair").is_boolean(),"resource-worker-invalid");
        const auto id=request.at("id").get<std::string>();ymcc::deckybinding::worker::require(ymcc::deckybinding::worker::hex(id,32),"resource-worker-invalid");
        ymcc::deckybinding::worker::decimal(request.at("generation"));
        const auto home=std::filesystem::path(ymcc::deckybinding::worker::wide(request.at("home")));
        const auto steam=std::filesystem::path(ymcc::deckybinding::worker::wide(request.at("steam")));
        const auto manifest=std::filesystem::path(ymcc::deckybinding::worker::wide(request.at("manifest")));
        ymcc::deckybinding::worker::require(home.is_absolute()&&steam.is_absolute()&&manifest.is_absolute(),"resource-worker-invalid");
        const auto current=[&]{return cancel&&WaitForSingleObject(cancel,0)==WAIT_TIMEOUT;};
        const Digest hash=[&](const std::wstring& file){return digest(file,current);};
        Options options;options.allowRepair=request.at("allowRepair").get<bool>();options.mayWrite=current;
        // Debug-marker creation remains in the existing parent preparation.
        // This operation touches only exact resources and the bounded local cache.
        // Bounded legacy receipt migration belongs to this already owned worker,
        // never the parent/UI thread. Refuse writes while another Loader owns the port.
        std::unique_ptr<PortLease> receiptLease;
        const auto mayMigrate=[&]{if(!current()||!options.allowRepair)return false;if(!receiptLease)receiptLease=std::make_unique<PortLease>(options.backendPort);return receiptLease->acquired()&&current();};
        const auto migration=ymcc::deckysetup::migrateLegacyManagedReceipt(home,mayMigrate);
        receiptLease.reset(); // Cache obtains its own lease after the atomic move completes.
        Json contract,description;const auto invalid=ymcc::deckysetup::readManagedContract(home,description,contract,manifest);
        if(!migration.empty())detail={{"ready",false},{"reason",migration}};
        else detail=ymcc::deckysetup::inspect(steam,home,hash,manifest);
        // Launch preflight passes allowRepair=false: validate every installed
        // byte above, but keep optional cache hashing/copying off that deadline.
        // Cache seeding/repair remains on the original writable preparation.
        if(migration.empty()&&invalid.empty()&&!contract.is_null()&&!options.allowRepair)
            detail["resourceMaintenanceDeferred"]=true;
        if(migration.empty()&&invalid.empty()&&!contract.is_null()&&options.allowRepair){
            const SafePath safe=[](const auto& path){return ymcc::deckysetup::safeManagedPath(path);};
            const auto maintenance=Cache(home,contract,hash,safe,current,options).run();
            if(maintenance.value("resourcesRepaired",0u)>0)detail=ymcc::deckysetup::inspect(steam,home,hash,manifest);
            detail.update(maintenance);
        }
        if(!current()){detail["ready"]=false;detail["reason"]="preparation-cancelled";}
    }catch(...){detail={{"ready",false},{"reason","resource-worker-invalid"}};}
    exit=detail.value("ready",false)||detail.value("reason",std::string{})=="steam-debug-missing"?0:2;
    try{return emit(Json{{"schema",schema},{"id",request.value("id",std::string{})},{"generation",request.value("generation",std::string{})},{"detail",detail}}),true;}
    catch(...){exit=3;return true;}
}
// Parent owns the existing Owner + event-driven reader. No unjoined helper
// thread, no polling service; the 50ms cancellation check exists only during
// this bounded preparation call. stop() hard-revokes the precise owned Job.
inline Json prepare(const std::filesystem::path& steam,const std::filesystem::path& home,const std::filesystem::path& manifest,const std::function<bool()>& current,bool allowRepair){
    using namespace ymcc::deckybinding;
    if(steam.empty())return {{"ready",false},{"reason","steam-not-installed"}};
    Json result={{"ready",false},{"reason","resource-worker-unavailable"}};
    if(!current||!current())return {{"ready",false},{"reason","preparation-cancelled"}};
    Owner owner;clientdetail::EventReader reader;ymcc::deckybinding::detail::Handle child;
    const auto deadline=GetTickCount64()+2500;
    try{
        auto started=owner.start();require(static_cast<bool>(started),"resource-worker-start-failed");
        const auto generation=std::to_string(started.generation);
        char id[33]{};snprintf(id,sizeof(id),"%016llx%016llx",static_cast<unsigned long long>(GetCurrentProcessId()),static_cast<unsigned long long>(started.generation));
        const Json request={{"schema",schema},{"operation","prepare"},{"id",id},{"generation",generation},{"home",text(home)},{"steam",text(steam)},{"manifest",text(manifest)},{"allowRepair",allowRepair}};
        const auto bytes=request.dump();require(static_cast<bool>(owner.writeFrame(std::vector<std::uint8_t>(bytes.begin(),bytes.end()),150)),"resource-worker-send-failed");
        child.value=owner.copyChildProcess();require(child.value&&reader.attach(owner.copyReadPipe()),"resource-worker-read-failed");
        for(;;){
            if(!current()){result["reason"]="preparation-cancelled";break;}
            const auto now=GetTickCount64();if(now>=deadline){result["reason"]="resource-worker-deadline";break;}
            std::vector<std::uint8_t> payload;DWORD error=0;const auto packet=reader.advance(payload,error);
            if(packet==clientdetail::EventReader::Read::frame){
                const auto reply=Json::parse(payload);require(reply.is_object()&&reply.size()==4&&reply.value("schema",std::string{})==schema&&reply.at("id")==id&&reply.at("generation")==generation&&reply.at("detail").is_object(),"resource-worker-reply-invalid");result=reply.at("detail");break;
            }
            if(packet!=clientdetail::EventReader::Read::pending){result["reason"]="resource-worker-read-failed";break;}
            HANDLE waiting[]{reader.event(),child.value};const auto wait=WaitForMultipleObjects(2,waiting,FALSE,static_cast<DWORD>((std::min)(50ull,deadline-now)));
            if(wait==WAIT_FAILED){result["reason"]="resource-worker-wait-failed";break;}
        }
    }catch(const std::exception& error){const std::string reason=error.what();result={{"ready",false},{"reason",reason.starts_with("resource-worker-")?reason:"resource-worker-failed"}};}
    catch(...){result={{"ready",false},{"reason","resource-worker-failed"}};}
    owner.requestCancel();reader.requestCancel();
    const auto stopped=owner.stop(0,400);const auto readerStopped=reader.stop(200);if(child.value)CloseHandle(child.value);child.value=nullptr;
    const auto snapshot=owner.snapshot();const auto resourcesDestroyed=stopped.fullyDestroyed&&readerStopped&&snapshot.handles==0&&snapshot.pendingIo==0&&snapshot.jobActiveProcesses==0;
    result["resourceWorkerDestroyed"]=resourcesDestroyed;
    if(!resourcesDestroyed){result["ready"]=false;result["reason"]="resource-worker-cleanup-incomplete";}
    return result;
}
}
