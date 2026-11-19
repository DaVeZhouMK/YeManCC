// Optional scope of the existing GamingHome flow fixture. Real processes,
// held tokens/births, hidden HWNDs, WM_COPYDATA, CNG nonce and shared mapping
// remain original. Only AppModel metadata and fixed deployment location are
// declared fixture leaves; no package, certificate or system setting changes.
#include <windows.h>
#include <shellapi.h>
#include <appmodel.h>
#include <filesystem>
#include <fstream>
#include <vector>
#include <string>
#include <algorithm>
#include <cstring>
#include <json.hpp>
#include "information_package_adapter.inc"
#define GetPackageId HomePackageAdapter::packageId
#define GetPackageFullName HomePackageAdapter::fullName
#define GetPackagePathByFullName HomePackageAdapter::packagePath
#include "steam_information_client.h"
#undef GetPackagePathByFullName
#undef GetPackageFullName
#undef GetPackageId
namespace HomeInformationFixture {
inline std::wstring providerPath;
inline std::wstring expectedProvider(){return ymcc::steaminfo::pathKey(providerPath);}
}
namespace ymcc::steaminfo {
// Fixture deployment path and expected identity feed the shared production
// transport; the actual package metadata adapter is declared above.
#include "information_client_read.inc"
}
namespace HomeInformationFixture {
using Json=nlohmann::json;
namespace si=ymcc::steaminfo;
namespace fs=std::filesystem;
inline unsigned censusResolutions=0,readerCalls=0,ingressCalls=0;
inline DWORD expectedCaller=0;
inline std::wstring scenario;
inline HWND providerWindow=nullptr;
inline Json assertions=Json::array();
inline void expect(bool ok,const char* label){assertions.push_back({{"name",label},{"pass",ok}});if(!ok)throw std::runtime_error(label);}
inline FARPROC WINAPI countedGetProcAddress(HMODULE module,LPCSTR name){
    if(name&&reinterpret_cast<std::uintptr_t>(name)>65535&&std::strcmp(name,"NtQuerySystemInformation")==0)++censusResolutions;
    return ::GetProcAddress(module,name);
}
}
#define GetProcAddress HomeInformationFixture::countedGetProcAddress
#include "information_source_owner.inc"
#include "steam_information_producer.h"
#undef GetProcAddress
namespace HomeInformationFixture {
inline const std::vector<std::wstring>& cases(){
    static const std::vector<std::wstring> values={L"normal",L"null-resource-parent",L"null-resource-both",L"null-name",L"null-publisher",L"null-publisher-id",L"wrong-name",L"wrong-publisher",L"wrong-arch",L"resource-package",
        L"oversize-id",L"bad-id-pointer",L"unterminated-full",L"full-name-mismatch",L"wrong-root",L"unpackaged",L"stale-birth",L"combined-mask",L"reader-unavailable",L"source-no-owner",L"source-revoked-in-reader",L"source-rotated-in-reader",
        L"bound-owner-exact",L"bound-owner-wrong-pid",L"bound-owner-wrong-birth"};
    return values;
}
inline bool known(const std::wstring& value){return std::find(cases().begin(),cases().end(),value)!=cases().end();}
inline std::wstring selfImage(){wchar_t path[32768]{};const auto n=GetModuleFileNameW(nullptr,path,32768);expect(n&&n<32768,"actual executable image");return std::wstring(path,n);}
inline si::Facts readFacts(std::uint32_t mask,const std::shared_ptr<const ymcc::processInventory::Snapshot>& table,const ymcc::steamsession::SourceOwner::Read& reading){
    ++readerCalls;expect(mask==si::StartupWindow,"native reader receives exact startup-window mask");
    expect(!table,"startup-window ingress does not acquire ancestry census");
    auto& source=*HomeInformationSourceFixture::source;
    const ymcc::steamsession::SourceOwner other;
    expect(reading.currentFor(source)&&!reading.currentFor(other),"reader borrows only the admitted owner lease");
    si::Facts facts;if(scenario==L"reader-unavailable")return facts;
    si::Peer self;expect(self.load(GetCurrentProcessId()),"real provider Peer loaded for fixture field binding");
    facts.available=true;facts.requested=mask;facts.observedAt=GetTickCount64();
    // Source selection itself was executed separately with actual Observer and
    // an isolated Steam-named window. Here a declared terminal window receipt
    // exercises the exact original wire; it is never an action permission.
    facts.presence=1;facts.processPid=self.pid;facts.processBirth=self.birth;facts.processPath=si::utf8(self.path);facts.processEpoch=17;facts.accountEpoch=19;
    if(scenario==L"source-revoked-in-reader"||scenario==L"source-rotated-in-reader"){
        source.suspendReads();
        if(scenario==L"source-rotated-in-reader")source.resumeReads();
        expect(!reading.currentFor(source),"entry lease stays revoked after source retirement or restart");
    }
    auto& window=facts.startupWindow;window.completed=true;window.known=true;window.window=reinterpret_cast<std::uintptr_t>(providerWindow);
    window.pid=self.pid;window.processBirth=self.birth;window.observedAt=facts.observedAt;
    window.processEpoch=17;window.accountEpoch=19;window.windowsSession=self.session;window.root=si::utf8(fs::path(self.path).parent_path().wstring());return facts;
}
inline LRESULT CALLBACK windowProc(HWND hwnd,UINT msg,WPARAM from,LPARAM data){
    if(msg==WM_COPYDATA){
        ++ingressCalls;const auto* input=reinterpret_cast<const COPYDATASTRUCT*>(data);
        if(input&&input->dwData==si::messageTag&&input->cbData==sizeof(si::Request)&&input->lpData){
            si::Request request;std::memcpy(&request,input->lpData,sizeof(request));
            if(scenario==L"stale-birth")++request.callerBirth;
            else if(scenario==L"combined-mask")request.mask|=si::Installations;
            COPYDATASTRUCT changed=*input;changed.lpData=&request;
            return si::serve(hwnd,reinterpret_cast<HWND>(from),&changed,readFacts,HomeInformationSourceFixture::read);
        }
        return si::serve(hwnd,reinterpret_cast<HWND>(from),input,readFacts,HomeInformationSourceFixture::read);
    }
    return DefWindowProcW(hwnd,msg,from,data);
}
inline void writeReport(const fs::path& path,const Json& result){
    const auto text=result.dump();si::Handle file(CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));
    expect(file.value!=INVALID_HANDLE_VALUE&&file.value,"exclusive owned child result file");DWORD n=0;
    expect(WriteFile(file.value,text.data(),static_cast<DWORD>(text.size()),&n,nullptr)&&n==text.size(),"owned child result bytes written");
}
inline int child(const fs::path& root,const std::wstring& actualProvider,const std::wstring& inputMode){
    providerPath=actualProvider;HomePackageAdapter::mode=inputMode==L"null-resource-both"?L"null-resource":L"normal";HomePackageAdapter::packageRoot=(root/L"Package").wstring();HomePackageAdapter::admittedPid=GetCurrentProcessId();
    si::Peer self;expect(self.load(GetCurrentProcessId()),"actual child caller Peer");
    expect(HomePackageAdapter::packageRoot==fs::path(self.path).parent_path().wstring(),"actual child path is declared package root");
    const auto facts=si::fixtureHomeInformationRead(si::StartupWindow,inputMode);
    writeReport(root/L"child.json",Json({{"available",facts.available},{"requested",facts.requested},{"known",facts.startupWindow.known},
        {"window",facts.startupWindow.window},{"pid",facts.startupWindow.pid},{"birth",std::to_string(facts.startupWindow.processBirth)},
        {"root",facts.startupWindow.root},{"processEpoch",facts.startupWindow.processEpoch},{"accountEpoch",facts.startupWindow.accountEpoch},
        {"packageIdCalls",HomePackageAdapter::idCalls},{"packageFullNameCalls",HomePackageAdapter::fullCalls},{"packagePathCalls",HomePackageAdapter::pathCalls},
        {"callerPid",GetCurrentProcessId()},{"checks",assertions},{"appModelIsFixtureLeaf",true},{"realSteamActions",0}}));return 0;
}
inline int parent(const fs::path& root,const std::wstring& name){
    expect(known(name),"exact original-scope package case selector");scenario=name;
    expect(root.is_absolute()&&!fs::exists(root),"fresh explicit owned case directory");fs::create_directories(root/L"Package");
    providerPath=selfImage();expect(fs::path(providerPath).filename()==L"YeManCC.exe","actual provider uses exact native executable leaf");
    const auto client=root/L"Package"/L"YeManGamingHome.exe";expect(CopyFileW(providerPath.c_str(),client.c_str(),TRUE)!=FALSE,"exclusive owned exact client executable copy");
    WNDCLASSW cls{};cls.lpfnWndProc=windowProc;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"YMCC.HomeInformation.Fixture";
    expect(RegisterClassW(&cls)!=0,"owned provider class registered");providerWindow=CreateWindowExW(0,cls.lpszClassName,L"owned hidden information provider",0,0,0,0,0,nullptr,nullptr,cls.hInstance,nullptr);
    expect(providerWindow!=nullptr,"owned provider top-level window created");
    struct Window{~Window(){if(providerWindow){RemovePropW(providerWindow,si::property);DestroyWindow(providerWindow);providerWindow=nullptr;}}}window;
    expect(SetPropW(providerWindow,si::property,reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(si::protocol)))!=FALSE,"original information endpoint property published");
    HomePackageAdapter::packageRoot=(root/L"Package").wstring();HomePackageAdapter::mode=(name==L"null-resource-parent"||name==L"null-resource-both")?L"null-resource":name.find(L"source-")==0?L"normal":name;
    STARTUPINFOW startup{sizeof(startup)};PROCESS_INFORMATION pi{};
    std::wstring command=L"\""+client.wstring()+L"\" --home-information-child \""+root.wstring()+L"\" \""+providerPath+L"\" \""+name+L"\"";
    expect(CreateProcessW(client.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_SUSPENDED|CREATE_NO_WINDOW,nullptr,root.c_str(),&startup,&pi)!=FALSE,"actual suspended child caller created");
    si::Handle process(pi.hProcess),thread(pi.hThread);HomePackageAdapter::admittedPid=pi.dwProcessId;expectedCaller=pi.dwProcessId;
    HomeInformationSourceFixture::start(scenario==L"source-no-owner");
    DWORD baseline=0;expect(GetProcessHandleCount(GetCurrentProcess(),&baseline)!=FALSE,"actual provider handle baseline captured");
    expect(ResumeThread(thread.value)!=DWORD(-1),"actual child caller resumed after binding");
    const auto end=GetTickCount64()+5000;
    while(WaitForSingleObject(process.value,0)==WAIT_TIMEOUT&&GetTickCount64()<end){
        MsgWaitForMultipleObjects(1,&process.value,FALSE,25,QS_ALLINPUT);MSG message{};
        while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}
    }
    expect(WaitForSingleObject(process.value,0)==WAIT_OBJECT_0,"actual child caller joined before parent exit");DWORD exit=0;expect(GetExitCodeProcess(process.value,&exit)&&exit==0,"child caller natural success exit");
    Json report;{std::ifstream in(root/L"child.json",std::ios::binary);expect(bool(in),"actual child result opened");in>>report;}
    DWORD after=0;expect(GetProcessHandleCount(GetCurrentProcess(),&after)!=FALSE,"actual provider handle closing snapshot captured");
    expect(after==baseline,"request handles return to original held child baseline");
    const bool wrongOwner=name==L"bound-owner-wrong-pid"||name==L"bound-owner-wrong-birth";
    expect(ingressCalls==(wrongOwner?0u:1u),wrongOwner?"mismatched held owner has no WM_COPYDATA request":"one real WM_COPYDATA request only");expect(censusResolutions==0,"new package window requests never resolve private process census");
    const bool accepted=name==L"normal"||name==L"null-resource-parent"||name==L"null-resource-both"||name==L"bound-owner-exact";const bool revoked=name==L"source-revoked-in-reader"||name==L"source-rotated-in-reader";const bool read=accepted||name==L"reader-unavailable"||revoked;
    if(name==L"source-no-owner")expect(readerCalls==0,"nonowner sole source rejects before information reader");
    if(revoked)expect(!report.at("available").get<bool>(),"revoked source cannot publish completed information");
    expect(readerCalls==(read?1u:0u),"strict native package/mask/birth checks gate reader");
    expect(report.at("available").get<bool>()==accepted,"child sees only qualified native reply");
    expect(report.at("callerPid").get<DWORD>()==expectedCaller,"kernel caller identity matches held created child");
    if(accepted){si::Peer self;expect(self.load(GetCurrentProcessId()),"qualified provider closing Peer");
        expect(report.at("known").get<bool>()&&report.at("pid").get<DWORD>()==GetCurrentProcessId()&&report.at("birth")==std::to_string(self.birth),"native window facts preserve actual provider PID and exact birth");
        expect(report.at("window").get<std::uint64_t>()==reinterpret_cast<std::uintptr_t>(providerWindow)&&report.at("processEpoch")==17&&report.at("accountEpoch")==19,"typed window and epochs survive original mapping");}
    std::cout<<Json({{"status","PASS"},{"selection",si::utf8(name)},{"checks",assertions},{"child",report},
        {"readerCalls",readerCalls},{"ingressCalls",ingressCalls},{"censusResolutions",censusResolutions},{"handlesBefore",baseline},{"handlesAfter",after},
        {"actualChildJoined",true},{"actualCngNonceMappingAndWM",!wrongOwner},{"appModelIsFixtureLeaf",true},{"deploymentPathIsFixtureLeaf",true},{"realMsixPositive",false},{"realSteamActions",0},{"sourceReady",false}}).dump()<<std::endl;return 0;
}
inline int main(int,char**){
    int count=0;auto args=CommandLineToArgvW(GetCommandLineW(),&count);if(!args)return 2;
    struct Args{LPWSTR* value;~Args(){LocalFree(value);}}owned{args};
    try{
        if(count==5&&std::wstring_view(args[1])==L"--home-information-child")return child(fs::path(args[2]),args[3],args[4]);
        if(count!=4||std::wstring_view(args[1])!=L"--home-information"||!known(args[3])){
            std::cout<<Json({{"status","SELECTION_REJECTED"},{"casesStarted",0},{"sourceReady",false}}).dump()<<std::endl;return 2;}
        return parent(fs::absolute(args[2]),args[3]);
    }catch(const std::exception& error){
        std::cout<<Json({{"status","FAIL"},{"error",error.what()},{"checks",assertions},{"readerCalls",readerCalls},{"ingressCalls",ingressCalls},{"sourceReady",false}}).dump()<<std::endl;return 1;
    }
}
}
