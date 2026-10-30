#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <condition_variable>
#include <mutex>
static HANDLE cacheCrashReady=nullptr;
static unsigned cacheCrashPhase=0,cacheMetadataWrites=0;
static void cacheCrashCheckpoint(){SetEvent(cacheCrashReady);std::mutex mutex;std::condition_variable stopped;std::unique_lock lock(mutex);stopped.wait(lock,[]{return false;});}
// Fault checkpoint at the Windows IO boundary. The real WriteFile still runs;
// no product method or return value is replaced, and only owned children arm it.
static BOOL WINAPI cacheFixtureWriteFile(HANDLE file,LPCVOID bytes,DWORD length,LPDWORD written,LPOVERLAPPED overlapped){
    const auto result=::WriteFile(file,bytes,length,written,overlapped);
    if(result&&cacheCrashReady&&(cacheCrashPhase==1||cacheCrashPhase==2)){
        wchar_t path[32768]{};const auto size=GetFinalPathNameByHandleW(file,path,32768,FILE_NAME_NORMALIZED);
        if(size&&size<32768&&std::filesystem::path(path).filename()==L"metadata-stage.tmp"){
            ++cacheMetadataWrites;if(cacheMetadataWrites==cacheCrashPhase)cacheCrashCheckpoint();
        }
    }
    return result;
}
#define WriteFile cacheFixtureWriteFile
#include "../native/decky_sidebar_setup.h"
#undef WriteFile
#include "../native/decky_sidebar_diagnostics.h"
#include <bcrypt.h>
#include <iostream>
#include <iterator>
#include <vector>
#include <condition_variable>
#include <mutex>
#pragma comment(lib,"bcrypt.lib")
using namespace ymcc::deckysetup;
namespace fs=std::filesystem;
static const std::string inertLoader="inert-verified-loader-v330";
static Json cases=Json::array(),interruptionMeasurements=Json::array();static unsigned serial=0;static std::string selectedCase;
static void need(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
static std::string text(const fs::path& path){std::ifstream file(path,std::ios::binary);return {std::istreambuf_iterator<char>(file),{}};}
static void put(const fs::path& path,const std::string& bytes){fs::create_directories(path.parent_path());std::ofstream out(path,std::ios::binary|std::ios::trunc);out<<bytes;need(out.good(),"fixture write failed");}
static std::string realDigest(const std::wstring& file){
    const auto bytes=text(fs::path(file));BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;DWORD length=0,used=0;
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)return {};
    BCryptGetProperty(algorithm,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&length),sizeof(length),&used,0);
    std::vector<unsigned char> object(length),result(32);bool ok=BCryptCreateHash(algorithm,&hash,object.data(),length,nullptr,0,0)>=0;
    if(ok&&!bytes.empty())ok=BCryptHashData(hash,reinterpret_cast<PUCHAR>(const_cast<char*>(bytes.data())),static_cast<ULONG>(bytes.size()),0)>=0;
    if(ok)ok=BCryptFinishHash(hash,result.data(),32,0)>=0;
    if(hash)BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(algorithm,0);if(!ok)return {};
    std::string output;const char* hex="0123456789abcdef";for(auto b:result){output+=hex[b>>4];output+=hex[b&15];}return output;
}
// Only the inert Loader sentinel aliases the public pin. All plugin/cache bytes
// use real SHA256, including same-length corruption and destination races.
static std::string digest(const std::wstring& path){return text(fs::path(path))==inertLoader?std::string(loaderSha):realDigest(path);}
struct Fixture {
    fs::path root,steam,home,receipt,cache;Json manifest;Digest hash=digest;
    explicit Fixture(const fs::path& base,const std::string& name,bool exact=false):root(base/(exact?name:name+std::to_string(++serial))),steam(root/L"Steam"),home(root/L"PowerControl"/L"decky"),receipt(home/L".ymcc-managed.receipt"),cache(home/L".ymcc-resource-cache"){
        put(steam/L"steam.exe","inert");put(steam/L".cef-enable-remote-debugging","");
        for(const auto* file:managedFiles)put(home/file,"fixture-"+pathText(fs::path(file)));
        put(home/managedFiles[0],inertLoader);put(home/managedFiles[3],"{\"name\":\"ymcc-sidebar\"}");refreshManifest();
    }
    void refreshManifest(){manifest={{"schemaVersion",1},{"owner","YMCC"},{"origin","official-release"},{"loaderVersion",ymcc::deckyidentity::version},{"loaderSha256",loaderSha},{"files",Json::array()}};for(const auto* file:managedFiles)manifest["files"].push_back({{"path",pathText(fs::path(file))},{"bytes",fs::file_size(home/file)},{"sha256",hash((home/file).wstring())}});put(receipt,manifest.dump());}
    Json prepare(const std::function<bool()>& current={},ymcc::deckyresources::Options options={}){options.backendPort=options.backendPort==1337?0:options.backendPort;return ymcc::deckysetup::prepare(steam,home,hash,false,current,{},options);}
    Json seeded(){auto result=prepare();need(result.value("ready",false)&&result.value("resourceCacheAvailable",false),"cache seed not actually verified");return result;}
};
template<class F>static void check(const char* name,F&& work){if(!selectedCase.empty()&&selectedCase!=name)return;try{work();cases.push_back({{"name",name},{"pass",true}});}catch(const std::exception& error){cases.push_back({{"name",name},{"pass",false},{"error",error.what()}});}}
static unsigned short boundPort(const ymcc::deckyresources::PortLease& lease){sockaddr_in address{};int size=sizeof(address);need(getsockname(lease.value,reinterpret_cast<sockaddr*>(&address),&size)==0,"cannot inspect fixture port");return ntohs(address.sin_port);}
static int crashWhileStaging(const fs::path& root,const wchar_t* readyName,unsigned phase){
    ymcc::deckyresources::Handle ready;ready.value=OpenEventW(EVENT_MODIFY_STATE,FALSE,readyName);need(ready.value&&ready.value!=INVALID_HANDLE_VALUE,"crash child event unavailable");
    cacheCrashReady=ready.value;cacheCrashPhase=phase;cacheMetadataWrites=0;
    const Digest paused=[&](const std::wstring& path){const auto hash=digest(path);if(phase==0&&fs::path(path).extension()==L".tmp")cacheCrashCheckpoint();return hash;};
    ymcc::deckyresources::Options options;options.backendPort=0;
    ymcc::deckysetup::prepare(root/L"Steam",root/L"PowerControl"/L"decky",paused,false,{}, {},options);return 93;
}
static void interruptOwnedCache(const fs::path& root,unsigned phase=0){
    const auto readyName=L"Local\\YMCC-Cache-Interruption-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(++serial);
    ymcc::deckyresources::Handle ready,job,process,thread;ready.value=CreateEventW(nullptr,TRUE,FALSE,readyName.c_str());job.value=CreateJobObjectW(nullptr,nullptr);
    need(ready.value&&job.value,"interruption fixture handles failed");JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    need(SetInformationJobObject(job.value,JobObjectExtendedLimitInformation,&limits,sizeof(limits))!=FALSE,"fixture Job setup failed");
    wchar_t image[32768]{};need(GetModuleFileNameW(nullptr,image,32768)>0,"fixture image unavailable");
    std::wstring command=L"\""+std::wstring(image)+L"\" --cache-interrupt-child \""+root.wstring()+L"\" \""+readyName+L"\" "+std::to_wstring(phase);
    STARTUPINFOW startup{};startup.cb=sizeof(startup);startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;PROCESS_INFORMATION child{};
    need(CreateProcessW(image,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW|CREATE_SUSPENDED,nullptr,nullptr,&startup,&child)!=FALSE,"fixture child start failed");
    process.value=child.hProcess;thread.value=child.hThread;
    if(!AssignProcessToJobObject(job.value,process.value)){TerminateProcess(process.value,94);throw std::runtime_error("fixture child Job assignment failed");}
    need(ResumeThread(thread.value)!=static_cast<DWORD>(-1),"fixture child resume failed");
    HANDLE wait[]{ready.value,process.value};need(WaitForMultipleObjects(2,wait,FALSE,5000)==WAIT_OBJECT_0,"cache child did not reach staged-copy checkpoint");
    const auto started=GetTickCount64();need(TerminateJobObject(job.value,0)!=FALSE&&WaitForSingleObject(process.value,1000)==WAIT_OBJECT_0,"interrupted owned child survived");
    JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting{};
    while(GetTickCount64()-started<=3200){need(QueryInformationJobObject(job.value,JobObjectBasicAccountingInformation,&accounting,sizeof(accounting),nullptr)!=FALSE,"fixture Job accounting unavailable");if(accounting.ActiveProcesses==0)break;Sleep(1);}
    need(GetTickCount64()-started<=3200&&accounting.ActiveProcesses==0,"interrupted cache child exceeded original resource stop bound");
    Json temporary=Json::array();for(const auto& file:fs::recursive_directory_iterator(root/L"PowerControl"/L"decky"))if(file.is_regular_file()&&file.path().extension()==L".tmp")temporary.push_back(pathText(file.path().filename()));
    need(!temporary.empty(),"interruption checkpoint left no actual staged file");
    interruptionMeasurements.push_back({{"phase",phase},{"childPid",child.dwProcessId},{"childExited",true},{"jobActiveProcesses",accounting.ActiveProcesses},{"stopElapsedMs",GetTickCount64()-started},{"deadlineMs",3200},{"remainingStaging",temporary}});
}
int wmain(int argc,wchar_t** argv){try{
    if(argc==5&&std::wstring(argv[1])==L"--cache-interrupt-child")return crashWhileStaging(argv[2],argv[3],static_cast<unsigned>(_wtoi(argv[4])));
    need(argc==4||(argc==6&&std::wstring(argv[4])==L"--case"),"expected explicit evidence, result and official data-file arguments");if(argc==6){const auto bytes=fs::path(argv[5]).u8string();selectedCase=std::string(bytes.begin(),bytes.end());}const auto base=fs::absolute(argv[1]);need(base.filename()==L"resource-fixture","fixture scope rejected");fs::create_directories(base);
    check("read-only inspection never creates the local cache",[&]{Fixture f(base,"readonly");need(inspect(f.steam,f.home,f.hash).value("ready",false),"read-only resources not ready");need(!fs::exists(f.cache),"status created repair cache");});
    check("only fully verified live resources seed a completed .bin cache",[&]{Fixture f(base,"seed");auto state=f.seeded();need(state.value("resourcesRepaired",9u)==0,"seed falsely counted repair");const auto metadata=Json::parse(text(f.cache/L"a"/L"manifest.json"));need(metadata["manifest"]==f.manifest&&metadata["complete"]==true,"cache marker not verified");for(unsigned i=0;i<7;++i)need(text(f.cache/L"a"/(std::to_wstring(i)+L".bin"))==text(f.home/managedFiles[i]),"cached bytes differ");});
    check("missing Loader and plugin are automatically restored before ready",[&]{Fixture f(base,"missing");f.seeded();fs::remove(f.home/managedFiles[0]);fs::remove(f.home/managedFiles[2]);auto state=f.prepare();need(state.value("ready",false)&&state.value("resourcesRepaired",0u)==2,"missing pair did not heal");need(text(f.home/managedFiles[0])==inertLoader,"Loader bytes not restored");need(ymcc::deckydiag::record("prepare",state).at("resourcesRepaired")==2,"actual repair count lost from diagnostics");need(inspect(f.steam,f.home,f.hash).value("ready",false),"repair counter substituted for actual readiness");});
    check("equal-length plugin corruption is repaired by SHA256",[&]{Fixture f(base,"hash");f.seeded();auto bad=text(f.home/managedFiles[2]);bad[0]^=1;put(f.home/managedFiles[2],bad);auto state=f.prepare();need(state.value("ready",false)&&state.value("resourcesRepaired",0u)==1,"equal-length bad plugin survived");});
    check("corrupt trust metadata is not hidden by cached metadata",[&]{Fixture f(base,"trust");f.seeded();fs::remove(f.home/managedFiles[2]);put(f.receipt,"broken");auto state=f.prepare();need(!state.value("ready",true)&&state["reason"]=="resource-package-invalid","bad receipt was masked");need(!fs::exists(f.home/managedFiles[2]),"untrusted cache recreated plugin");});
    check("absent authority never grants repair even when cached blobs exist",[&]{Fixture f(base,"noauthority");f.seeded();fs::remove(f.receipt);fs::remove(f.home/managedFiles[0]);auto state=f.prepare();need(!state.value("ready",true)&&!fs::exists(f.home/managedFiles[0]),"orphan cache became authority");});
    check("corrupt cached blob never reaches live code",[&]{Fixture f(base,"badblob");f.seeded();auto bad=text(f.cache/L"a"/L"2.bin");bad[0]^=1;put(f.cache/L"a"/L"2.bin",bad);fs::remove(f.home/managedFiles[2]);auto state=f.prepare();need(!state.value("ready",true)&&state.value("resourceRepairReason",std::string{})=="resource-cache-unavailable","bad cached blob accepted");need(!fs::exists(f.home/managedFiles[2]),"bad blob was copied");});
    check("healthy current files rebuild a corrupted local cache before reporting available",[&]{Fixture f(base,"cache-repair");f.seeded();auto bad=text(f.cache/L"a"/L"2.bin");bad[0]^=1;put(f.cache/L"a"/L"2.bin",bad);auto state=f.prepare();need(state.value("ready",false)&&state.value("resourceCacheAvailable",false)&&text(f.cache/L"a"/L"2.bin")==text(f.home/managedFiles[2]),"damaged cache was declared available");fs::remove(f.home/managedFiles[2]);state=f.prepare();need(state.value("ready",false)&&state.value("resourcesRepaired",0u)==1,"refilled cache cannot heal subsequent loss");});
    check("another generation's cache cannot repair the current package",[&]{Fixture f(base,"generation");f.seeded();auto metadata=Json::parse(text(f.cache/L"a"/L"manifest.json"));metadata["manifest"]["loaderVersion"]="v99.0.0";put(f.cache/L"a"/L"manifest.json",metadata.dump());fs::remove(f.home/managedFiles[2]);auto state=f.prepare();need(!state.value("ready",true)&&!fs::exists(f.home/managedFiles[2]),"cache crossed package generation");});
    check("busy backend is preserved and prevents live replacement",[&]{Fixture f(base,"port");f.seeded();fs::remove(f.home/managedFiles[2]);ymcc::deckyresources::PortLease sentinel(0);need(sentinel.acquired(),"sentinel failed");ymcc::deckyresources::Options options;options.backendPort=boundPort(sentinel);auto state=f.prepare({},options);need(!state.value("ready",true)&&state.value("resourceRepairReason",std::string{})=="resource-cache-backend-in-use","busy backend allowed replacement");need(!fs::exists(f.home/managedFiles[2])&&sentinel.acquired(),"foreign backend or live files changed");});
    check("Loader ownership gate prevents resource repair",[&]{Fixture f(base,"owner");f.seeded();fs::remove(f.home/managedFiles[2]);ymcc::deckyresources::Options options;options.mayWrite=[]{return false;};auto state=f.prepare({},options);need(!state.value("ready",true)&&!fs::exists(f.home/managedFiles[2]),"owned Loader gate bypassed");});
    check("isolation denies repair without changing user files",[&]{Fixture f(base,"isolate");f.seeded();fs::remove(f.home/managedFiles[2]);ymcc::deckyresources::Options options;options.allowRepair=false;auto state=f.prepare({},options);need(!state.value("ready",true)&&!fs::exists(f.home/managedFiles[2]),"isolation bypassed");});
    check("isolated mode can disable every cache write even with healthy live resources",[&]{Fixture f(base,"nocache");ymcc::deckyresources::Options options;options.enabled=false;auto state=f.prepare({},options);need(state.value("ready",false)&&!fs::exists(f.cache),"isolation wrote healthy resource cache");});
    check("OFF before preparation creates no cache or marker",[&]{Fixture f(base,"off");fs::remove(f.steam/L".cef-enable-remote-debugging");auto state=f.prepare([]{return false;});need(state["reason"]=="preparation-cancelled"&&!fs::exists(f.cache)&&!fs::exists(f.steam/L".cef-enable-remote-debugging"),"OFF mutated preparation");});
    check("OFF after cached hash prevents replacement and returns the port",[&]{Fixture f(base,"lateoff");f.seeded();fs::remove(f.home/managedFiles[2]);bool allowed=true;f.hash=[&](const std::wstring& path){const auto result=digest(path);if(fs::path(path).filename()==L"2.bin")allowed=false;return result;};auto state=f.prepare([&]{return allowed;});need(!state.value("ready",true)&&!fs::exists(f.home/managedFiles[2]),"late OFF recreated plugin");});
    check("cancel during staged-copy hash leaves no live write or temporary file",[&]{Fixture f(base,"stageoff");f.seeded();fs::remove(f.home/managedFiles[2]);bool allowed=true;f.hash=[&](const std::wstring& path){const auto result=digest(path);if(fs::path(path).extension()==L".tmp")allowed=false;return result;};auto state=f.prepare([&]{return allowed;});need(!state.value("ready",true)&&!fs::exists(f.home/managedFiles[2]),"cancelled staged copy committed");for(const auto& file:fs::recursive_directory_iterator(f.home))need(file.path().extension()!=L".tmp","cancelled staged file remained");});
    check("late concurrent code changes are preserved at the restore step",[&]{Fixture f(base,"race");f.seeded();auto bad=text(f.home/managedFiles[2]);bad[0]^=1;put(f.home/managedFiles[2],bad);unsigned reads=0;const auto target=f.home/managedFiles[2];f.hash=[&](const std::wstring& path){if(fs::path(path)==target&&++reads==4)put(target,"late-concurrent-owner");return digest(path);};auto state=f.prepare();need(!state.value("ready",true)&&text(target)=="late-concurrent-owner","late concurrent code was overwritten");need(state.value("resourceRepairReason",std::string{})=="resource-cache-target-changed","late change was not diagnosed");});
    check("locked target is not killed or replaced",[&]{Fixture f(base,"locked");f.seeded();auto bad=text(f.home/managedFiles[2]);bad[0]^=1;put(f.home/managedFiles[2],bad);ymcc::deckyresources::Handle owner;owner.value=CreateFileW((f.home/managedFiles[2]).c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);need(owner.value!=INVALID_HANDLE_VALUE,"cannot hold fixture file");auto state=f.prepare();need(!state.value("ready",true)&&text(f.home/managedFiles[2])==bad,"locked target replaced");need(WaitForSingleObject(GetCurrentProcess(),0)==WAIT_TIMEOUT,"fixture owner not alive");});
    check("cache rotation stays within two slots and preserves user plugin settings",[&]{Fixture f(base,"rotate");f.seeded();put(f.home/L"settings"/L"loader.json","KEEP");put(f.home/L"plugins"/L"other"/L"main.py","KEEP-OTHER");for(unsigned i=0;i<4;++i){put(f.home/managedFiles[2],"generation-"+std::to_string(i));f.refreshManifest();f.seeded();}unsigned directories=0;for(const auto& item:fs::directory_iterator(f.cache))if(item.is_directory())++directories;need(directories==2,"cache grew beyond two slots");need(text(f.home/L"settings"/L"loader.json")=="KEEP"&&text(f.home/L"plugins"/L"other"/L"main.py")=="KEEP-OTHER","user data changed");});
    check("unknown cache slot content is preserved while valid live code remains usable",[&]{Fixture f(base,"unknown");put(f.cache/L"a"/L"user.txt","KEEP-A");put(f.cache/L"b"/L"user.txt","KEEP-B");auto state=f.prepare();need(state.value("ready",false)&&!state.value("resourceCacheAvailable",true),"optional cache blocked valid resources or overwrote foreign slots");need(text(f.cache/L"a"/L"user.txt")=="KEEP-A"&&text(f.cache/L"b"/L"user.txt")=="KEEP-B","unknown cache content removed");});
    check("reparse cache slot cannot redirect recovery writes",[&]{Fixture f(base,"reparse",true);need(fs::exists(f.cache/L"a"),"runner did not provide junction");auto state=f.prepare();need(state.value("ready",false)&&!state.value("resourceCacheAvailable",true),"cache followed junction");need(text(base/L"outside"/L"KEEP.txt")=="OUTSIDE"&&!fs::exists(base/L"outside"/L"manifest.json"),"cache escaped managed home");});
    check("first old-updater install seeds cache through program package identity",[&]{Fixture f(base,"first");const auto program=f.root/L"Program"/L"decky-package.json";put(program,f.manifest.dump());fs::remove(f.receipt);ymcc::deckyresources::Options options;options.backendPort=0;auto state=ymcc::deckysetup::prepare(f.steam,f.home,f.hash,false,{},program,options);need(state.value("ready",false)&&state.value("resourceCacheAvailable",false)&&state["managedPackageSource"]=="package","first-upgrade cache missing");fs::remove(f.home/managedFiles[2]);state=ymcc::deckysetup::prepare(f.steam,f.home,f.hash,false,{},program,options);need(state.value("ready",false)&&state.value("resourcesRepaired",0u)==1,"first-upgrade cache did not repair");});
    check("cache read and replacement handles are returned after failed repair",[&]{Fixture f(base,"handles");f.seeded();fs::remove(f.home/managedFiles[2]);DWORD before=0,after=0;need(GetProcessHandleCount(GetCurrentProcess(),&before),"handle baseline missing");ymcc::deckyresources::Options options;options.mayWrite=[]{return false;};for(unsigned i=0;i<3;++i)f.prepare({},options);need(GetProcessHandleCount(GetCurrentProcess(),&after)&&after==before,"failed repair leaked own handles");});
    check("unmodified public Windows Loader bytes cache and restore within original preparation budget",[&]{Fixture f(base,"official");f.hash=realDigest;fs::copy_file(argv[3],f.home/managedFiles[0],fs::copy_options::overwrite_existing);need(realDigest((f.home/managedFiles[0]).wstring())==loaderSha,"official source identity mismatch");f.refreshManifest();const auto started=GetTickCount64();auto state=f.prepare([&]{return GetTickCount64()-started<2500;});need(state.value("ready",false)&&state.value("resourceCacheAvailable",false),"official bytes exceeded bounded preparation or did not cache");fs::remove(f.home/managedFiles[0]);state=f.prepare();need(state.value("ready",false)&&state.value("resourcesRepaired",0u)==1&&realDigest((f.home/managedFiles[0]).wstring())==loaderSha,"real public bytes did not restore");});
    check("repeated hard interruption retains only bounded owned staging and can resume preparation",[&]{
        Fixture f(base,"hard-interrupt");
        for(unsigned round=0;round<4;++round){
            interruptOwnedCache(f.root);
            unsigned pending=0;for(const auto& item:fs::recursive_directory_iterator(f.home))if(item.is_regular_file()&&item.path().extension()==L".tmp")++pending;
            need(pending<=2,"hard-interrupted preparation accumulated unbounded staging files");
        }
        const auto state=f.prepare();need(state.value("ready",false)&&state.value("resourceCacheAvailable",false),"fresh preparation could not resume interrupted cache seed");
        for(const auto& item:fs::recursive_directory_iterator(f.home))need(item.path().extension()!=L".tmp","owned staging survived successful resumption");
        fs::remove(f.home/managedFiles[0]);need(f.prepare().value("ready",false),"resumed cache could not heal later Loader loss");
    });
    check("hard interruption during live repair resumes without growing scratch files or losing user state",[&]{
        Fixture f(base,"live-interrupt");f.seeded();put(f.home/L"settings"/L"keep.json","KEEP");fs::remove(f.home/managedFiles[2]);
        for(unsigned n=0;n<4;++n){interruptOwnedCache(f.root);unsigned pending=0;for(const auto& file:fs::recursive_directory_iterator(f.home))if(file.is_regular_file()&&file.path().extension()==L".tmp")++pending;need(pending<=2&&!fs::exists(f.home/managedFiles[2]),"interrupted live repair grew scratch space or prematurely committed");}
        const auto result=f.prepare();need(result.value("ready",false)&&result.value("resourcesRepaired",0u)==1&&text(f.home/L"settings"/L"keep.json")=="KEEP","live repair could not resume safely");
    });
    check("incomplete metadata at either publication step can resume in bounded staging",[&]{
        for(const auto phase:{1u,2u}){Fixture f(base,"metadata-interrupt");for(unsigned n=0;n<3;++n){interruptOwnedCache(f.root,phase);unsigned pending=0;for(const auto& file:fs::recursive_directory_iterator(f.home))if(file.is_regular_file()&&file.path().extension()==L".tmp")++pending;need(pending<=2,"interrupted metadata accumulated temporary files");}
            auto result=f.prepare();need(result.value("ready",false)&&result.value("resourceCacheAvailable",false),"metadata interruption prevented cache resumption");
            fs::remove(f.home/managedFiles[2]);need(f.prepare().value("ready",false),"resumed metadata did not allow actual resource healing");}
    });
    check("missing staging provenance never authorizes deleting a familiar temporary filename",[&]{
        Fixture f(base,"unowned-stage");put(f.cache/L"copy-stage.tmp","KEEP-UNKNOWN");const auto result=f.prepare();need(result.value("ready",false)&&!result.value("resourceCacheAvailable",true)&&text(f.cache/L"copy-stage.tmp")=="KEEP-UNKNOWN"&&!fs::exists(f.cache/L"staging-owner.json"),"unowned staging was overwritten");
    });
    check("invalid staging owner cannot retire arbitrary or owned-looking content",[&]{
        Fixture f(base,"invalid-stage");f.seeded();put(f.cache/L"copy-stage.tmp","KEEP-STAGE");put(f.cache/L"outside-user.txt","KEEP-USER");
        put(f.cache/L"staging-owner.json","{\"schemaVersion\":1,\"owner\":\"YMCC\",\"purpose\":\"resource-cache-staging\",\"files\":[\"../outside-user.txt\"]}");
        const auto result=f.prepare();need(result.value("ready",false)&&!result.value("resourceCacheAvailable",true)&&text(f.cache/L"copy-stage.tmp")=="KEEP-STAGE"&&text(f.cache/L"outside-user.txt")=="KEEP-USER","invalid staging owner deleted content");
    });
    check("directory collision at a staging leaf cannot trigger recursive deletion",[&]{
        Fixture f(base,"stage-directory");f.seeded();put(f.cache/L"metadata-stage.tmp"/L"user.txt","KEEP-DIR");const auto result=f.prepare();need(result.value("ready",false)&&!result.value("resourceCacheAvailable",true)&&text(f.cache/L"metadata-stage.tmp"/L"user.txt")=="KEEP-DIR","staging directory was recursively deleted");
    });
    check("locked residue is preserved until its owner releases it",[&]{
        Fixture f(base,"stage-locked");f.seeded();put(f.cache/L"copy-stage.tmp","PENDING");{
            ymcc::deckyresources::Handle owner;owner.value=CreateFileW((f.cache/L"copy-stage.tmp").c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);need(owner.value!=INVALID_HANDLE_VALUE,"fixture lock unavailable");
            const auto result=f.prepare();need(result.value("ready",false)&&!result.value("resourceCacheAvailable",true)&&text(f.cache/L"copy-stage.tmp")=="PENDING","locked scratch was changed");
        }need(f.prepare().value("resourceCacheAvailable",false)&&!fs::exists(f.cache/L"copy-stage.tmp"),"released staging did not heal on next preparation");
    });
    check("cleanup touches only the two proven staging leaves and preserves unknown old-looking files",[&]{
        Fixture f(base,"unknown-legacy");f.seeded();put(f.cache/L"old-user.tmp","KEEP-UNKNOWN");put(f.home/L"plugins"/L"ymcc-sidebar"/L"dist"/L".ymcc-cache-user.tmp","KEEP-OLD-LOOKING");
        const auto state=f.prepare();need(state.value("ready",false)&&state.value("resourceCacheAvailable",false)&&text(f.cache/L"old-user.tmp")=="KEEP-UNKNOWN"&&text(f.home/L"plugins"/L"ymcc-sidebar"/L"dist"/L".ymcc-cache-user.tmp")=="KEEP-OLD-LOOKING","cleanup guessed ownership from a filename prefix");
    });
    const auto passed=std::count_if(cases.begin(),cases.end(),[](const auto& c){return c.at("pass")==true;});const Json result={{"pass",passed==static_cast<int>(cases.size())},{"count",cases.size()},{"passed",passed},{"cases",cases},{"interruptionMeasurements",interruptionMeasurements},{"win32WriteBoundary","real WriteFile plus owned metadata publication pause"},{"realSteamActions",0},{"productProcessesLaunched",0},{"physicalSleep",false},{"officialLoaderExecuted",false},{"loaderFixtureAliasOnly",true}};put(argv[2],result.dump(2));std::cout<<result.dump()<<std::endl;return result.at("pass")==true?0:2;
 }catch(const std::exception& error){std::cerr<<error.what()<<std::endl;return 3;}}
