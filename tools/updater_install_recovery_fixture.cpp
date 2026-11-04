#define NOMINMAX
#include <windows.h>
#include <aclapi.h>
#include <sddl.h>
#include <functional>
// The non-admin journal-storage selection replaces privileged ACL lookup only.
// Normal Win32 file reads, locks and all descriptor validation remain real.
static bool fixtureJournalAclPort=false;
static DWORD WINAPI fixtureRecoveryGetSecurityInfo(HANDLE h,SE_OBJECT_TYPE type,SECURITY_INFORMATION info,PSID* owner,PSID* group,PACL* dacl,PACL* sacl,PSECURITY_DESCRIPTOR* sd){
    if(!fixtureJournalAclPort)return ::GetSecurityInfo(h,type,info,owner,group,dacl,sacl,sd);
    return ConvertStringSecurityDescriptorToSecurityDescriptorW(L"O:BAG:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)",SDDL_REVISION_1,sd,nullptr)?ERROR_SUCCESS:GetLastError();
}

static bool fixtureCreateAclPort=false,fixtureShortWrite=false,fixtureFlushFailure=false,fixturePublishFailure=false,fixtureCorruptPublication=false,fixtureChangeBeforePublish=false;
static HANDLE WINAPI fixtureRecoveryCreateFileW(LPCWSTR name,DWORD access,DWORD share,LPSECURITY_ATTRIBUTES attributes,DWORD creation,DWORD flags,HANDLE templateFile){return ::CreateFileW(name,access,share,fixtureCreateAclPort&&attributes?nullptr:attributes,creation,flags,templateFile);}
static BOOL WINAPI fixtureRecoveryWriteFile(HANDLE h,LPCVOID data,DWORD bytes,LPDWORD count,LPOVERLAPPED overlap){if(fixtureShortWrite&&bytes){fixtureShortWrite=false;return ::WriteFile(h,data,bytes-1,count,overlap);}return ::WriteFile(h,data,bytes,count,overlap);}
static BOOL WINAPI fixtureRecoveryFlush(HANDLE h){if(fixtureFlushFailure){fixtureFlushFailure=false;SetLastError(ERROR_WRITE_FAULT);return FALSE;}return ::FlushFileBuffers(h);}
static BOOL WINAPI fixtureRecoveryCopy(LPCWSTR from,LPCWSTR to,BOOL failExists){auto ok=::CopyFileW(from,to,failExists);if(ok&&fixtureChangeBeforePublish){fixtureChangeBeforePublish=false;HANDLE h=::CreateFileW(from,GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,TRUNCATE_EXISTING,0,nullptr);if(h!=INVALID_HANDLE_VALUE){const char raw[]="{concurrent-change";DWORD n;::WriteFile(h,raw,sizeof(raw)-1,&n,nullptr);CloseHandle(h);}}return ok;}
static BOOL WINAPI fixtureRecoveryMove(LPCWSTR from,LPCWSTR to,DWORD flags){if(fixturePublishFailure){fixturePublishFailure=false;SetLastError(ERROR_ACCESS_DENIED);return FALSE;}auto ok=::MoveFileExW(from,to,flags);if(ok&&fixtureCorruptPublication){fixtureCorruptPublication=false;HANDLE h=::CreateFileW(to,GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,TRUNCATE_EXISTING,0,nullptr);if(h!=INVALID_HANDLE_VALUE){const char raw[]="{post-publish-corruption";DWORD n;::WriteFile(h,raw,sizeof(raw)-1,&n,nullptr);CloseHandle(h);}}return ok;}
#define CreateFileW fixtureRecoveryCreateFileW
#define WriteFile fixtureRecoveryWriteFile
#define FlushFileBuffers fixtureRecoveryFlush
#define CopyFileW fixtureRecoveryCopy
#define MoveFileExW fixtureRecoveryMove
#define GetSecurityInfo fixtureRecoveryGetSecurityInfo
#include "update_install_recovery.h"
#undef GetSecurityInfo
#undef CreateFileW
#undef WriteFile
#undef FlushFileBuffers
#undef CopyFileW
#undef MoveFileExW
#include <iostream>
#include <fstream>
#include <limits>
#include "production-admission.h"
using namespace ymcc::updaterecovery;
static int checks=0;
static void check(bool ok,const char* name){if(!ok)throw std::runtime_error(name);++checks;}

static int journalStorageStorm(const std::filesystem::path& root){
    std::filesystem::create_directories(root);fixtureJournalAclPort=true;
    Json rows=Json::array();
    const auto exe=(root/L"YeManCC.exe").wstring(),pc=(root/L"PowerControl").wstring();
    const Json good={{"schema",1},{"owner","YMCC.FullInstall"},{"operationId","config-storm"},{"phase","applying"},{"version","0.0.99"},{"sha256",std::string(64,'a')},{"exePath",utf8(exe)},{"powerControl",utf8(pc)}};
    auto put=[&](const std::filesystem::path& file,const std::string& raw){std::ofstream stream(file,std::ios::binary|std::ios::trunc);stream.write(raw.data(),static_cast<std::streamsize>(raw.size()));stream.close();if(!stream)throw std::runtime_error("fixture write failed");};
    auto test=[&](const std::string& name,const std::function<void(const std::filesystem::path&)>& body){const auto file=root/(std::to_wstring(rows.size())+L".json");try{body(file);rows.push_back({{"name",name},{"ok",true}});}catch(const std::exception& error){rows.push_back({{"name",name},{"ok",false},{"error",error.what()}});}};
    auto rejected=[&](const std::string& name,const std::string& raw){test(name,[&](const auto& file){put(file,raw);check(readAt(root.wstring(),file.wstring()).empty(),"invalid protected journal was consumed");check(!matches(readAt(root.wstring(),file.wstring()),exe,pc),"invalid protected journal matched authority");});};
    test("valid protected journal remains readable",[&](const auto& file){put(file,good.dump());auto j=readAt(root.wstring(),file.wstring());check(matches(j,exe,pc)&&authorizes(j,exe,pc,"0.0.99",std::string(64,'a')),"valid protected journal lost");});
    for(const auto& raw:std::vector<std::string>{""," ","{bad","null","[]","["+good.dump()+"]","true","42"})rejected("invalid root "+std::to_string(rows.size()),raw);
    for(const auto& pair:std::vector<std::pair<std::string,Json>>{{"schemaVersion",2},{"schemaVersion",true},{"schema","1"},{"schema",1.25},{"schema",true},{"schema",2},{"owner",Json::array()},{"operationId",std::string(129,'x')},{"operationId",std::string("safe\0other",10)},{"version","bad"},{"version","0.00.99"},{"sha256",std::string(64,'x')},{"phase","unknown"},{"phase",7},{"phase",Json::object()},{"exePath",7},{"exePath","relative.exe"},{"powerControl","relative"},{"authorizedAt","123"},{"authorizedAt",-1},{"helperPid",true},{"helperPid",4294967296ULL},{"helperCreation",1},{"helperImage",false},{"updatedAt",-1}}){auto bad=good;bad[pair.first]=pair.second;rejected("invalid field "+pair.first+" "+std::to_string(rows.size()),bad.dump());}
    for(const auto& key:std::vector<std::string>{"schema","owner","phase","operationId","sha256"}){auto raw=good.dump();raw.pop_back();raw+=",\""+key+"\":"+good.at(key).dump()+"}";rejected("decoded duplicate "+key,raw);}
    {auto raw=good.dump();raw.pop_back();raw+=",\"\\u0073chema\":1}";rejected("escaped duplicate schema",raw);}
    {auto raw=good.dump();raw.pop_back();raw+=",\"Schema\":1}";rejected("case conflicting schema",raw);}
    {auto raw=good.dump();raw.pop_back();raw+=",\"unknown\":{\"x\":1,\"x\":2}}";rejected("nested duplicate unknown field",raw);}
    rejected("oversize journal",std::string(65537,' '));
    {auto raw=good.dump();raw.pop_back();raw+=",\"unknown\":"+std::string(1000,'[')+"0"+std::string(1000,']')+"}";rejected("depth limited before recursive parse",raw);}
    test("utf8 BOM remains supported",[&](const auto& file){put(file,std::string("\xef\xbb\xbf",3)+good.dump());check(matches(readAt(root.wstring(),file.wstring()),exe,pc),"utf8 BOM rejected");});
    for(const bool be:{false,true})test(be?"utf16 BE journal":"utf16 LE journal",[&](const auto& file){const auto text=wide(good.dump());std::string raw=be?std::string("\xfe\xff",2):std::string("\xff\xfe",2);for(wchar_t c:text){raw+=static_cast<char>(be?c>>8:c&255);raw+=static_cast<char>(be?c&255:c>>8);}put(file,raw);check(matches(readAt(root.wstring(),file.wstring()),exe,pc),"legitimate UTF16 journal rejected");});
    rejected("invalid utf8",std::string("{\"bad\":\"\xff\"}",11));
    test("unknown fields survive consumer read",[&](const auto& file){auto value=good;value["futureExtra"]={{"value",42}};put(file,value.dump());check(readAt(root.wstring(),file.wstring()).at("futureExtra")==value.at("futureExtra"),"unknown valid field lost");});
    test("lock release permits retry",[&](const auto& file){put(file,good.dump());HANDLE h=CreateFileW(file.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr);check(h!=INVALID_HANDLE_VALUE,"fixture lock failed");auto blocked=readAt(root.wstring(),file.wstring());CloseHandle(h);check(blocked.empty(),"locked journal consumed");check(matches(readAt(root.wstring(),file.wstring()),exe,pc),"released journal permanently stuck");});
    test("directory is not journal",[&](const auto& file){std::filesystem::create_directory(file);check(readAt(root.wstring(),file.wstring()).empty(),"directory consumed");});
    test("repeated corruption does not poison repaired source",[&](const auto& file){for(unsigned i=0;i<8;++i){put(file,"{");check(readAt(root.wstring(),file.wstring()).empty(),"damage retained authority");put(file,good.dump());check(matches(readAt(root.wstring(),file.wstring()),exe,pc),"fixed source not re-read");}});

    fixtureCreateAclPort=true;
    auto readRaw=[](const std::filesystem::path& file){std::ifstream stream(file,std::ios::binary);return std::string(std::istreambuf_iterator<char>(stream),{});};
    auto writerCase=[&](const std::string& name,const std::function<void(const std::filesystem::path&,const std::wstring&,const std::wstring&,const std::wstring&)>& body){test(name,[&](const auto& file){const auto dir=file.parent_path()/file.stem();std::filesystem::create_directories(dir);const auto targetExe=(dir/L"YeManCC.exe").wstring(),targetPc=(dir/L"PowerControl").wstring(),target=pathFor(dir.wstring(),targetExe);body(dir,targetExe,targetPc,target);});};
    auto writeGood=[&](const auto& dir,const auto& targetExe,const auto& targetPc,const auto& target){auto j=good;j["exePath"]=utf8(targetExe);j["powerControl"]=utf8(targetPc);j["phase"]="committed";j["extra"]={{"preserve",42}};put(target,j.dump());return j;};
    writerCase("missing journal creates confirmed authority",[&](const auto& dir,const auto& targetExe,const auto& targetPc,const auto& target){auto path=writeAt(dir.wstring(),targetExe,targetPc,"new-operation","0.0.99",std::string(64,'a'));check(path==target&&matches(readAt(dir.wstring(),path),targetExe,targetPc),"new writer failed");});
    writerCase("valid old journal preserved before new authority",[&](const auto& dir,const auto& targetExe,const auto& targetPc,const auto& target){auto old=writeGood(dir,targetExe,targetPc,target);const auto bytes=readRaw(target);writeAt(dir.wstring(),targetExe,targetPc,"next-operation","0.0.99",std::string(64,'b'));check(readAt(dir.wstring(),target).at("extra")==old.at("extra"),"unknown old fields lost");unsigned backups=0;for(const auto& file:std::filesystem::directory_iterator(dir))if(file.path().filename().wstring().find(L".ymcc-before-")!=std::wstring::npos){++backups;check(readRaw(file.path())==bytes,"preimage bytes changed");}check(backups==1,"preimage absent or duplicated");});
    for(const auto& kind:{"damaged","future","directory","foreign"})writerCase(std::string("writer protects old ")+kind,[&](const auto& dir,const auto& targetExe,const auto& targetPc,const auto& target){std::string raw;if(std::string(kind)=="directory")std::filesystem::create_directory(target);else{auto old=writeGood(dir,targetExe,targetPc,target);if(std::string(kind)=="damaged")raw="{broken";else{if(std::string(kind)=="future")old["schema"]=2;else old["operationId"]=42;raw=old.dump();}put(target,raw);}bool refused=false;try{writeAt(dir.wstring(),targetExe,targetPc,"new-operation","0.0.99",std::string(64,'a'));}catch(...){refused=true;}check(refused,"unconfirmed old journal overwritten");check(std::string(kind)=="directory"?std::filesystem::is_directory(target):readRaw(target)==raw,"protected old original changed");if(std::string(kind)!="directory"){writeGood(dir,targetExe,targetPc,target);writeAt(dir.wstring(),targetExe,targetPc,"repaired","0.0.99",std::string(64,'a'));check(readAt(dir.wstring(),target).at("operationId")=="repaired","trusted source repair did not unblock");}});
    for(const auto& kind:{"short-write","flush","publish","readback","source-change"})writerCase(std::string("writer fault ")+kind,[&](const auto& dir,const auto& targetExe,const auto& targetPc,const auto& target){writeGood(dir,targetExe,targetPc,target);const auto before=readRaw(target);auto k=std::string(kind);fixtureShortWrite=k=="short-write";fixtureFlushFailure=k=="flush";fixturePublishFailure=k=="publish";fixtureCorruptPublication=k=="readback";fixtureChangeBeforePublish=k=="source-change";bool refused=false;try{writeAt(dir.wstring(),targetExe,targetPc,"new-operation","0.0.99",std::string(64,'a'));}catch(...){refused=true;}fixtureShortWrite=fixtureFlushFailure=fixturePublishFailure=fixtureCorruptPublication=fixtureChangeBeforePublish=false;check(refused,"writer fault reported success");if(k!="readback"&&k!="source-change")check(readRaw(target)==before,"failed save changed known original");else check(readRaw(target).starts_with("{"),"failed save lost divergent bytes");writeGood(dir,targetExe,targetPc,target);writeAt(dir.wstring(),targetExe,targetPc,"retry","0.0.99",std::string(64,'a'));check(readAt(dir.wstring(),target).at("operationId")=="retry","retry after fault did not recover");});
    unsigned failed=0;for(const auto& row:rows)if(!row.at("ok").get<bool>())++failed;
    std::ofstream(root/L"journal-storage.json")<<Json{{"caseCount",rows.size()},{"failed",failed},{"cases",rows},{"scope","actual native readAt/matches/authorizes; owned Win32 files and locks real; privileged ACL lookup explicit inert port"}}.dump(2);
    std::cout<<Json{{"status",failed?"FAIL":"PASS"},{"cases",rows.size()},{"failed",failed}}.dump()<<"\n";
    return failed?1:0;
}

int wmain(int argc,wchar_t** argv){try{
    if(argc>2&&std::wstring(argv[2])==L"--journal-config")return journalStorageStorm(argv[1]);
    wchar_t image[32768]{};GetModuleFileNameW(nullptr,image,32768);
    Json j={{"schema",1},{"owner","YMCC.FullInstall"},{"operationId","owned"},{"phase","applying"},{"version","0.0.36"},{"sha256",std::string(64,'a')},{"exePath",utf8(image)},{"powerControl","D:\\fixture\\PowerControl"}};
    const std::wstring pc=L"D:\\fixture\\PowerControl";
    check(authorizes(j,image,pc,"0.0.36",std::string(64,'A')),"matching unfinished transaction");
    check(!authorizes(j,image,pc,"0.0.37",std::string(64,'a')),"version bound");
    check(!authorizes(j,image,pc,"0.0.36",std::string(64,'b')),"sha bound");
    check(!authorizes(j,L"D:\\other\\YeManCC.exe",pc,"0.0.36",std::string(64,'a')),"exe bound");
    check(!authorizes(j,image,L"D:\\other\\PowerControl","0.0.36",std::string(64,'a')),"PowerControl authority bound");
    for(const auto& phase:{"committed","rolled-back","failed","", "started"}){j["phase"]=phase;check(!authorizes(j,image,pc,"0.0.36",std::string(64,'a')),"terminal phase rejected");}
    j["phase"]="repair-required";check(authorizes(j,image,pc,"0.0.36",std::string(64,'a')),"failed repair retains authorization");
    j["phase"]=42;check(!unfinished(j),"malformed phase bounded");j["phase"]="applying";
    FILETIME c{},e{},k{},u{};GetProcessTimes(GetCurrentProcess(),&c,&e,&k,&u);
    j["helperPid"]=GetCurrentProcessId();j["helperCreation"]=std::to_string(filetime(c));j["helperImage"]=utf8(image);
    check(live(j),"live helper real process");j["helperCreation"]="0";check(!live(j),"reused pid rejected");
    j["helperCreation"]=std::to_string(filetime(c));j["helperImage"]="D:\\unrelated.exe";check(!live(j),"wrong image rejected");
    j.erase("helperPid");j["phase"]="authorized";j["authorizedAt"]=now();j["launcherPid"]=GetCurrentProcessId();j["launcherCreation"]=std::to_string(filetime(c));j["launcherImage"]=utf8(image);
    check(live(j),"bounded launch gap");j["authorizedAt"]=now()-16000;check(!live(j),"launch gap expires");
    j["authorizedAt"]=now()+60000;check(!live(j),"future timestamp cannot freeze");
    for(const wchar_t* sddl:{L"O:BAG:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)",L"O:BAG:BAD:(A;;FA;;;SY)(A;;FA;;;BA)",L"O:BAG:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;FA;;;BU)",L"O:BAG:BAD:P(D;;FA;;;BU)(A;;FA;;;SY)(A;;FA;;;BA)",L"O:BAG:BAD:P(A;;FA;;;BA)"}){
        PSECURITY_DESCRIPTOR sd=nullptr;check(ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl,SDDL_REVISION_1,&sd,nullptr)!=0,"parse actual OS descriptor");
        bool expect=std::wstring(sddl)==L"O:BAG:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)";check(protectedDescriptor(sd)==expect,"actual OS descriptor admission");LocalFree(sd);
    }
    if(argc>1){std::filesystem::path root=argv[1];std::filesystem::create_directories(root);auto path=root/L"forged.json";std::ofstream(path)<<j.dump();check(readAt(root.wstring(),path.wstring()).empty(),"real writable file never authorizes recovery");}
    auto rejected=[](const std::string& v,const std::string& sha,bool background=false){try{requireInstallableUpdateVersion(v,sha,background);return false;}catch(...){return true;}};
    fixtureJournal=Json::object();check(!rejected("0.0.37",std::string(64,'a')),"ordinary newer version admitted");
    check(rejected("0.0.36",std::string(64,'a')),"ordinary equal version rejected");
    j["phase"]="repair-required";j.erase("helperPid");fixtureJournal=j;
    check(!rejected("0.0.36",std::string(64,'a')),"production guard admits authorized same-version repair");
    check(rejected("0.0.35",std::string(64,'a')),"production guard rejects downgrade");
    check(rejected("0.0.36",std::string(64,'a'),true),"background never repairs equal version");
    check(rejected("0.0.36",std::string(64,'b')),"production guard rejects wrong package");
    fixtureJournal["phase"]="committed";check(rejected("0.0.36",std::string(64,'a')),"committed journal cannot authorize reinstallation");
    fixtureJournal=j;fixtureJournal["helperPid"]=GetCurrentProcessId();fixtureJournal["helperCreation"]=std::to_string(filetime(c));fixtureJournal["helperImage"]=utf8(image);
    check(rejected("0.0.37",std::string(64,'a')),"live helper blocks competing successor");
    std::cout<<"{\"status\":\"PASS\",\"checks\":"<<checks<<",\"scope\":\"production policy, OS security descriptors and owned process identity; elevated writer untested\"}\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
