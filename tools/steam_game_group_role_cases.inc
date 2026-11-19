// Private explicit selector only. No default/canonical group is invoked here.
// Compile/run status is NOT_RUN until the parent runner supplies actual receipts.
#include <type_traits>
#include <optional>
#include <array>
#include "steam_game_role_contract.h"
#include "steam_game_group_lease.h"

namespace SteamGameGroupRoleFixture {
using namespace ymcc::steamgamefacts;
namespace roles = ymcc::steamgameroles;
static json checks = json::array();
static void check(const char* name, bool pass) {
    checks.push_back({{"name",name},{"pass",pass}});
}
template<class T> concept PublicGroupAcquire = requires(const T& owner,
    const std::shared_ptr<ProcessLease>& selected, const ProcessGroupSeed& seed) {
    owner.acquireGroup(selected,seed);
};
template<class T> concept PublicBoundaryOverride = requires(T& owner,
    const SteamLogSourceIdentity& source, const std::shared_ptr<ProcessLease>& selected,
    const TrackerOwnerCurrent& current) {
    owner.observeProcessGroup(source,selected,current,std::uint64_t{1});
};
template<class T> concept HasAcceptedMember = requires(T result) { result.accepted; };
static_assert(!std::is_default_constructible_v<SteamProcessGroupObservation>);
static_assert(!std::is_aggregate_v<SteamProcessGroupObservation>);
static_assert(!std::is_constructible_v<SteamProcessGroupObservation,roles::Graph>);
static_assert(!std::is_default_constructible_v<ProcessGroupLease>);
static_assert(!std::is_constructible_v<ProcessGroupLease,ProcessGroupSeed>);
static_assert(!PublicGroupAcquire<ProcessOwner>);
static_assert(!PublicBoundaryOverride<TrackerOwner>);
static_assert(!HasAcceptedMember<roles::Result>);
static_assert(!std::is_convertible_v<roles::Result,bool>);

static roles::Graph chain() {
    roles::Graph graph{};
    graph.count=3;graph.selectedPid=30;
    graph.members[0]={10,7,900,100};
    graph.members[1]={20,7,10,200};
    graph.members[2]={30,7,20,300};
    return graph;
}
static bool outcome(const roles::Result& result,roles::State state,roles::Reason reason) {
    return result.state==state&&result.reason==reason;
}
static void pureCases() {
    using S=roles::State;using R=roles::Reason;
    auto graph=chain();graph.count=1;graph.members[0]={30,7,900,300};
    auto result=roles::inspect(graph,30,30);
    check("pure-single-exact-selected",outcome(result,S::ExactSelected,R::None)&&result.nativePrimaryExact==1&&result.nativeForegroundExact==1);
    graph=chain();result=roles::inspect(graph,10,30);
    check("pure-root-primary-selected-foreground",outcome(result,S::VerifiedGroupRoles,R::None)&&result.selected.pid==30&&result.root.pid==10&&result.nativePrimaryExact==0&&result.nativeForegroundExact==1);
    result=roles::inspect(graph,30,10);
    check("pure-selected-primary-root-foreground",outcome(result,S::VerifiedGroupRoles,R::None)&&result.nativePrimaryExact==1&&result.nativeForegroundExact==0);
    result=roles::inspect(graph,10,20);
    check("pure-independent-native-ancestor-roles",outcome(result,S::VerifiedGroupRoles,R::None)&&result.nativePrimary.pid==10&&result.nativeForeground.pid==20);
    graph.count=0;
    check("pure-count-zero-refused",outcome(roles::inspect(graph,10,30),S::Invalid,R::CountOutOfRange));
    graph.count=33;
    check("pure-count-thirty-three-refused",outcome(roles::inspect(graph,10,30),S::Invalid,R::CountOutOfRange));
    graph=chain();graph.members[2].pid=10;
    check("pure-duplicate-pid-refused",outcome(roles::inspect(graph,10,30),S::Invalid,R::DuplicateMemberPid));
    graph=chain();graph.members[0].parentPid=30;
    check("pure-cycle-refused",outcome(roles::inspect(graph,10,30),S::Invalid,R::Cycle));
    graph=chain();graph.members[1].session=8;
    check("pure-mixed-session-refused",outcome(roles::inspect(graph,10,30),S::Invalid,R::SessionMismatch));
    graph=chain();graph.members[1].birth=graph.members[0].birth;
    check("pure-equal-parent-child-birth-refused",outcome(roles::inspect(graph,10,30),S::Invalid,R::ParentBirthNotEarlier));
    graph=chain();graph.members[1].birth=99;
    check("pure-younger-parent-refused",outcome(roles::inspect(graph,10,30),S::Invalid,R::ParentBirthNotEarlier));
    graph=chain();graph.selectedPid=99;
    check("pure-missing-selected-refused",outcome(roles::inspect(graph,10,30),S::Unknown,R::SelectedMissing));
    graph=chain();
    check("pure-missing-native-primary-refused",outcome(roles::inspect(graph,99,30),S::Unknown,R::NativePrimaryMissing));
    check("pure-missing-native-foreground-refused",outcome(roles::inspect(graph,10,99),S::Unknown,R::NativeForegroundMissing));
    graph.count=4;graph.members[3]={40,7,10,400};
    check("pure-sibling-primary-refused",outcome(roles::inspect(graph,40,30),S::Invalid,R::NativePrimaryNotAncestor));
    check("pure-sibling-foreground-refused",outcome(roles::inspect(graph,10,40),S::Invalid,R::NativeForegroundNotAncestor));
    check("pure-nonrole-sibling-does-not-reselect-game",outcome(roles::inspect(graph,10,30),S::VerifiedGroupRoles,R::None)&&roles::inspect(graph,10,30).selected.pid==30);
    graph=chain();graph.members[1].parentPid=901;
    check("pure-two-independent-roots-refused",outcome(roles::inspect(graph,30,30),S::Invalid,R::MultipleRoots));
    graph=chain();graph.members[0].birth=0;
    check("pure-missing-member-birth-unknown",outcome(roles::inspect(graph,10,30),S::Unknown,R::MemberBirthZero));
    graph=chain();graph.members[0].pid=0;
    check("pure-zero-member-pid-refused",outcome(roles::inspect(graph,10,30),S::Invalid,R::MemberPidZero));
    graph=chain();graph.members[1].parentPid=20;
    check("pure-self-parent-refused",outcome(roles::inspect(graph,10,30),S::Invalid,R::SelfParent));
    graph=chain();graph.selectedPid=0;
    check("pure-zero-selected-unknown",outcome(roles::inspect(graph,10,30),S::Unknown,R::SelectedPidZero));
    graph=chain();
    check("pure-zero-primary-unknown",outcome(roles::inspect(graph,0,30),S::Unknown,R::NativePrimaryPidZero));
    check("pure-zero-foreground-unknown",outcome(roles::inspect(graph,10,0),S::Unknown,R::NativeForegroundPidZero));
    graph={};graph.count=32;graph.selectedPid=1031;
    for(std::uint32_t i=0;i<graph.count;++i)graph.members[i]={1000+i,0,i?999+i:900,100+i};
    check("pure-thirty-two-members-session-zero-valid",outcome(roles::inspect(graph,1000,1031),S::VerifiedGroupRoles,R::None));
    check("literal-graph-and-seed-cannot-mint-source-or-lease",!std::is_constructible_v<SteamProcessGroupObservation,roles::Graph>&&!std::is_constructible_v<ProcessGroupLease,ProcessGroupSeed>&&!PublicGroupAcquire<ProcessOwner>);
    check("role-result-is-enum-state-without-bool-acceptance",!HasAcceptedMember<roles::Result>&&!std::is_convertible_v<roles::Result,bool>);
    check("caller-cannot-supply-historical-console-boundary",!PublicBoundaryOverride<TrackerOwner>);
}

// Fixture-only root -> shipping harness. Actual CreateProcessW ancestry;
// no parent-query override, synthetic parent port, or real Steam operation.
struct OwnedHandle {
    HANDLE value=nullptr;
    OwnedHandle()=default;
    explicit OwnedHandle(HANDLE h):value(h){}
    OwnedHandle(const OwnedHandle&)=delete;
    OwnedHandle& operator=(const OwnedHandle&)=delete;
    ~OwnedHandle(){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
    bool close()noexcept {
        if(!value||value==INVALID_HANDLE_VALUE){value=nullptr;return true;}
        if(!CloseHandle(value))return false;value=nullptr;return true;
    }
};
static std::uint64_t birth(HANDLE process) {
    FILETIME b{},e{},k{},u{};
    if(!GetProcessTimes(process,&b,&e,&k,&u))throw std::runtime_error("group-role actual process birth");
    return (std::uint64_t(b.dwHighDateTime)<<32)|b.dwLowDateTime;
}
struct ChildManifest {DWORD magic=0,pid=0;std::uint64_t created=0;};
static_assert(sizeof(ChildManifest)==16);
static std::optional<int> childMain(int argc,wchar_t** argv) {
    if(argc>=2&&std::wstring_view(argv[1])==L"--group-role-leaf") {
        if(argc!=3)return 20;
        OwnedHandle stop(OpenEventW(SYNCHRONIZE,FALSE,argv[2]));
        if(!stop.value)return 21;
        return WaitForSingleObject(stop.value,30000)==WAIT_OBJECT_0?0:22;
    }
    if(argc<2||std::wstring_view(argv[1])!=L"--group-role-root")return {};
    if(argc!=7)return 23;
    OwnedHandle rootStop(OpenEventW(SYNCHRONIZE,FALSE,argv[2]));
    OwnedHandle leafStop(OpenEventW(SYNCHRONIZE|EVENT_MODIFY_STATE,FALSE,argv[3]));
    OwnedHandle ready(OpenEventW(EVENT_MODIFY_STATE,FALSE,argv[4]));
    if(!rootStop.value||!leafStop.value||!ready.value)return 24;
    // The real parent must have a strictly earlier creation stamp.
    Sleep(15);
    std::wstring command=L"\""+std::wstring(argv[6])+L"\" --group-role-leaf \""+argv[3]+L"\"";
    STARTUPINFOW si{sizeof(si)};PROCESS_INFORMATION pi{};
    if(!CreateProcessW(argv[6],command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi))return 25;
    OwnedHandle thread(pi.hThread),process(pi.hProcess);thread.close();
    const auto fail=[&](int code){SetEvent(leafStop.value);WaitForSingleObject(process.value,3000);return std::optional<int>(code);};
    ChildManifest manifest{0x47524f4cU,pi.dwProcessId,0};
    try{manifest.created=birth(process.value);}catch(...){return fail(26);}
    OwnedHandle file(CreateFileW(argv[5],GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));
    if(file.value==INVALID_HANDLE_VALUE)return fail(27);
    DWORD written=0;
    if(!WriteFile(file.value,&manifest,sizeof(manifest),&written,nullptr)||written!=sizeof(manifest)||!file.close())return fail(28);
    if(!SetEvent(ready.value))return fail(29);
    const HANDLE waits[]={rootStop.value,process.value};
    const auto ended=WaitForMultipleObjects(2,waits,FALSE,30000);
    // Deliberate root-only normal exit leaves shipping for its original owner.
    if(ended==WAIT_OBJECT_0||ended==WAIT_OBJECT_0+1)return 0;
    return fail(30);
}
struct InertTree {
    OwnedHandle job,rootProcess,shippingProcess,rootStop,shippingStop,ready;
    DWORD rootPid=0,shippingPid=0;
    std::uint64_t rootCreated=0,shippingCreated=0;
    bool forced=false,zero=false,closed=false;
    std::wstring marker;
    inline static std::atomic<unsigned> sequence{0};
    static HANDLE event(const std::wstring& name) {
        HANDLE h=CreateEventW(nullptr,TRUE,FALSE,name.c_str());
        const auto error=GetLastError();
        if(!h||error==ERROR_ALREADY_EXISTS){if(h)CloseHandle(h);throw std::runtime_error("group-role exclusive event");}
        return h;
    }
    InertTree(const std::wstring& root,const std::wstring& rootExe,const std::wstring& shippingExe) {
        const auto unique=std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L"-"+std::to_wstring(++sequence);
        marker=root+L"\\group-role-child-"+unique+L".bin";
        const auto prefix=L"Local\\YMCCSteamObserverTest-GroupRoles-"+unique;
        const auto rootName=prefix+L"-root",shipName=prefix+L"-shipping",readyName=prefix+L"-ready";
        try {
            rootStop.value=event(rootName);shippingStop.value=event(shipName);ready.value=event(readyName);
            job.value=CreateJobObjectW(nullptr,nullptr);
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION limit{};limit.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            if(!job.value||!SetInformationJobObject(job.value,JobObjectExtendedLimitInformation,&limit,sizeof(limit)))throw std::runtime_error("group-role local Job");
            STARTUPINFOW si{sizeof(si)};PROCESS_INFORMATION pi{};
            std::wstring command=L"\""+rootExe+L"\" --group-role-root \""+rootName+L"\" \""+shipName+L"\" \""+readyName+L"\" \""+marker+L"\" \""+shippingExe+L"\"";
            if(!CreateProcessW(rootExe.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW|CREATE_SUSPENDED,nullptr,nullptr,&si,&pi))throw std::runtime_error("group-role actual root start");
            rootProcess.value=pi.hProcess;OwnedHandle thread(pi.hThread);rootPid=pi.dwProcessId;
            if(!AssignProcessToJobObject(job.value,rootProcess.value)) {
                forced=true;TerminateProcess(rootProcess.value,31);WaitForSingleObject(rootProcess.value,3000);
                throw std::runtime_error("group-role root Job assign");
            }
            if(ResumeThread(thread.value)==static_cast<DWORD>(-1))throw std::runtime_error("group-role root resume");
            if(!thread.close())throw std::runtime_error("group-role root thread close");
            if(WaitForSingleObject(ready.value,3000)!=WAIT_OBJECT_0)throw std::runtime_error("group-role root child ready");
            OwnedHandle file(CreateFileW(marker.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));
            ChildManifest manifest{};DWORD read=0;LARGE_INTEGER size{};
            if(file.value==INVALID_HANDLE_VALUE||!GetFileSizeEx(file.value,&size)||size.QuadPart!=sizeof(manifest)||
                !ReadFile(file.value,&manifest,sizeof(manifest),&read,nullptr)||read!=sizeof(manifest)||manifest.magic!=0x47524f4cU||!manifest.pid||!manifest.created)
                throw std::runtime_error("group-role child manifest");
            shippingProcess.value=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,manifest.pid);
            if(!shippingProcess.value||GetProcessId(shippingProcess.value)!=manifest.pid||birth(shippingProcess.value)!=manifest.created)
                throw std::runtime_error("group-role actual shipping identity");
            shippingPid=manifest.pid;shippingCreated=manifest.created;rootCreated=birth(rootProcess.value);
        }catch(...){finish();throw;}
    }
    bool finishRoot() {
        return rootStop.value&&SetEvent(rootStop.value)&&WaitForSingleObject(rootProcess.value,3000)==WAIT_OBJECT_0;
    }
    bool finishShipping() {
        return shippingStop.value&&SetEvent(shippingStop.value)&&WaitForSingleObject(shippingProcess.value,3000)==WAIT_OBJECT_0;
    }
    bool finish()noexcept {
        if(closed)return zero&&!forced;
        if(rootStop.value)SetEvent(rootStop.value);
        if(shippingStop.value)SetEvent(shippingStop.value);
        bool ended=(!rootProcess.value||WaitForSingleObject(rootProcess.value,3000)==WAIT_OBJECT_0)&&
            (!shippingProcess.value||WaitForSingleObject(shippingProcess.value,3000)==WAIT_OBJECT_0);
        if(job.value) {
            const auto end=GetTickCount64()+3000;
            do {
                JOBOBJECT_BASIC_ACCOUNTING_INFORMATION info{};
                if(QueryInformationJobObject(job.value,JobObjectBasicAccountingInformation,&info,sizeof(info),nullptr)&&!info.ActiveProcesses){zero=true;break;}
                Sleep(5);
            }while(GetTickCount64()<end);
            if(!zero){forced=true;TerminateJobObject(job.value,32);ended=false;}
        }
        bool handles=true;
        handles=shippingProcess.close()&&handles;handles=rootProcess.close()&&handles;
        handles=ready.close()&&handles;handles=shippingStop.close()&&handles;
        handles=rootStop.close()&&handles;handles=job.close()&&handles;
        closed=handles;return ended&&zero&&handles&&!forced;
    }
    ~InertTree(){finish();}
};

static std::shared_ptr<ProcessGroupLease> captured(const SteamProcessGroupObservation& observed) {
    return observed.processCapture()?observed.processCapture()->lease:nullptr;
}
static bool rejected(const SteamProcessGroupObservation& observed,ProcessGroupRejection reason) {
    return !captured(observed)&&observed.processCapture()&&observed.processCapture()->rejection==reason;
}
static bool role(const std::shared_ptr<ProcessGroupLease>& lease,DWORD primary,DWORD foreground,roles::State expected) {
    const auto result=lease?lease->inspectNativeRoles(primary,foreground):std::nullopt;
    return result&&result->state==expected&&result->reason==roles::Reason::None;
}
static void write(const std::wstring& file,const std::string& bytes) {
    std::ofstream out(std::filesystem::path(file),std::ios::binary|std::ios::trunc);
    out.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));out.close();
    if(!out)throw std::runtime_error("group-role isolated source write");
}
static std::string add(std::uint64_t id,DWORD pid,const std::wstring& path,std::uint64_t created) {
    return "["+SleepSteamRecovery::localSecond(created)+"] AppID "+std::to_string(id)+" adding PID "+std::to_string(pid)+" as a tracked process \""+W2U(path)+"\"\n";
}
static std::string primary(std::uint64_t id,DWORD pid,std::uint64_t created) {
    return "["+SleepSteamRecovery::localSecond(created)+"] Game process added : AppID "+std::to_string(id)+" fixture, ProcID "+std::to_string(pid)+", IP 0.0.0.0:0\n";
}
static void actualCases(const std::wstring& root,const std::wstring& childPath,Registry& registry,bool optInOnly=false) {
    const auto rootExe=root+L"\\group-role-launcher.exe",shippingExe=root+L"\\group-role-shipping.exe",outsideExe=root+L"\\group-role-independent.exe";
    if(!CopyFileW(childPath.c_str(),rootExe.c_str(),TRUE)||!CopyFileW(childPath.c_str(),shippingExe.c_str(),TRUE)||!CopyFileW(childPath.c_str(),outsideExe.c_str(),TRUE))
        throw std::runtime_error("group-role exclusive inert images");
    Child steam(childPath);Sleep(15);InertTree tree(root,rootExe,shippingExe);Child independent(outsideExe);
    registry.root(root);registry.number(L"ActiveUser",42);registry.number(L"pid",steam.pi.dwProcessId);
    if(!g_steamSettingsObserver.start([](bool){},[]{return false;}))throw std::runtime_error("group-role Observer start");
    struct ObserverStop {~ObserverStop(){fixtureFreshReadHook={};g_steamSettingsObserver.stop();}} observerStop;
    if(!until([&]{const auto p=g_steamSettingsObserver.identitySnapshot();return p&&p->presence==1&&p->session.pid==steam.pi.dwProcessId&&p->session.account==42&&p->windowsSessionKnown;}))
        throw std::runtime_error("group-role actual Observer identity");
    const ProcessWitness selected{tree.shippingPid,tree.shippingCreated,shippingExe};
    auto selectedLease=processOwner().acquire(g_steamSettingsObserver,g_steamSettingsObserver.identitySnapshot(),selected);
    if(!selectedLease)throw std::runtime_error("group-role actual selected ProcessLease");
    auto source=MainTrackerFactsAdapter::sourceKey({steam.pi.dwProcessId,birth(steam.pi.hProcess),root});
    if(!source)throw std::runtime_error("group-role actual source key");
    constexpr std::uint32_t app=2399932135U;
    constexpr std::uint64_t full=(std::uint64_t(app)<<32)|0x02000000ULL;
    const auto shipping=add(full,tree.shippingPid,shippingExe,tree.shippingCreated);
    const auto launcher=add(full,tree.rootPid,rootExe,tree.rootCreated);
    const auto extra=add(full,independent.pi.dwProcessId,outsideExe,birth(independent.pi.hProcess));
    const auto console=primary(full,tree.rootPid,tree.shippingCreated);
    const auto gameFile=root+L"\\logs\\gameprocess_log.txt",consoleFile=root+L"\\logs\\console_log.txt";
    std::filesystem::create_directories(root+L"\\logs");
    const auto seed=[&](bool selectedFirst=false){write(gameFile,selectedFirst?shipping+launcher:launcher+shipping);write(consoleFile,console);};
    auto& owner=trackerOwner();const TrackerOwnerCurrent current=[](){return true;};

    if(optInOnly){
        seed();
        NativeDetectedGame selectedGame;selectedGame.pid=selected.pid;selectedGame.processCreated=selected.created;selectedGame.path=selected.image;
        const auto scope=nativeGameFactScope(selectedGame,g_gameRulesEpoch.load(),currentPowerGeneration());
        const auto key=scope?nativeSteamGameTrackerFactKey(*scope):nullptr;
        if(!key)throw std::runtime_error("opt-in original selected fact key");
        const auto issue=[&](){auto ticket=owner.beginDemand(key,{},current);return owner.claim(std::move(ticket.demand),current,TrackerReadMode::RequireNewSource);};
        const auto games=fixtureFreshGameReads,consoles=fixtureFreshConsoleReads;
        auto ordinaryClaim=issue();if(!ordinaryClaim.token)throw std::runtime_error("opt-in ordinary token");
        auto ordinary=owner.observeFresh(std::move(ordinaryClaim.token),selectedLease,current);
        check("ordinary-fresh-default-has-no-unused-group-capture",ordinary.complete&&!ordinary.processGroupCapture);
        check("ordinary-fresh-default-keeps-one-source-pair",fixtureFreshGameReads==games+1&&fixtureFreshConsoleReads==consoles+1);
        auto explicitClaim=issue();if(!explicitClaim.token)throw std::runtime_error("opt-in explicit token");
        auto requested=owner.observeFresh(std::move(explicitClaim.token),selectedLease,current,TrackerGroupCapturePolicy::Capture);
        auto held=requested.processGroupCapture?requested.processGroupCapture->lease:nullptr;
        check("explicit-fresh-opt-in-mints-current-two-role-lease",requested.complete&&role(held,tree.rootPid,tree.shippingPid,roles::State::VerifiedGroupRoles));
        check("explicit-fresh-opt-in-reuses-one-source-pair",fixtureFreshGameReads==games+2&&fixtureFreshConsoleReads==consoles+2);
        if(held)held->cancel();
        check("explicit-fresh-opt-in-cancel-cannot-revive",held&&!held->processCurrent()&&!held->inspectNativeRoles(tree.rootPid,tree.shippingPid));
        requested={};held.reset();ordinary={};selectedLease.reset();independent.finish();g_steamSettingsObserver.stop();steam.finish();
        const bool returned=tree.finish();
        check("opt-in-inert-tree-and-observer-resources-returned",returned&&tree.zero&&tree.closed&&!tree.forced&&WaitForSingleObject(independent.pi.hProcess,0)==WAIT_OBJECT_0&&WaitForSingleObject(steam.pi.hProcess,0)==WAIT_OBJECT_0&&!g_steamSettingsObserver.identitySnapshot());
        return;
    }
    check("actual-root-created-shipping-without-parent-port",ymcc::steamgamefacts::detail::parent(tree.shippingProcess.value)==tree.rootPid&&tree.rootCreated<tree.shippingCreated&&WaitForSingleObject(tree.rootProcess.value,0)==WAIT_TIMEOUT&&WaitForSingleObject(tree.shippingProcess.value,0)==WAIT_TIMEOUT);
    write(gameFile,shipping);write(consoleFile,primary(full,tree.shippingPid,tree.shippingCreated));
    const auto gamesBefore=fixtureFreshGameReads,consoleBefore=fixtureFreshConsoleReads;
    auto exact=owner.observeProcessGroup(source,selectedLease,current);auto exactLease=captured(exact);
    check("actual-single-exact-through-public-source-and-pin",exact.sourcesComplete()&&role(exactLease,tree.shippingPid,tree.shippingPid,roles::State::ExactSelected)&&exactLease->graph().count==1);
    check("actual-one-common-game-and-console-read",fixtureFreshGameReads==gamesBefore+1&&fixtureFreshConsoleReads==consoleBefore+1&&exact.gameSource()&&exact.consoleSource()&&exact.parsedSource()&&exact.nativePrimary());
    seed();auto grouped=owner.observeProcessGroup(source,selectedLease,current);auto groupedLease=captured(grouped);
    check("actual-root-primary-shipping-foreground-verified-roles",grouped.sourcesComplete()&&role(groupedLease,tree.rootPid,tree.shippingPid,roles::State::VerifiedGroupRoles)&&groupedLease->selectedGame().pid==tree.shippingPid&&groupedLease->graph().count==2);
    check("actual-group-preserves-false-primary-exact",groupedLease&&groupedLease->inspectNativeRoles(tree.rootPid,tree.shippingPid)&&groupedLease->inspectNativeRoles(tree.rootPid,tree.shippingPid)->nativePrimaryExact==0&&groupedLease->inspectNativeRoles(tree.rootPid,tree.shippingPid)->nativeForegroundExact==1);
    check("actual-selected-source-row-after-root-kept",grouped.parsedSource()&&grouped.parsedSource()->processLifetime.group.selectedSourceLine==2&&groupedLease&&groupedLease->graph().members[0].pid==tree.shippingPid);
    seed(true);auto reordered=owner.observeProcessGroup(source,selectedLease,current);auto reorderedLease=captured(reordered);
    check("actual-selected-source-row-before-root-kept",reordered.parsedSource()&&reordered.parsedSource()->processLifetime.group.selectedSourceLine==1&&role(reorderedLease,tree.rootPid,tree.shippingPid,roles::State::VerifiedGroupRoles));
    check("actual-native-foreground-root-is-separate-role",role(reorderedLease,tree.shippingPid,tree.rootPid,roles::State::VerifiedGroupRoles));
    write(gameFile,launcher+shipping+extra);write(consoleFile,console);
    const auto independentGroup=owner.observeProcessGroup(source,selectedLease,current);
    check("actual-same-full-id-independent-root-refused",independentGroup.sourcesComplete()&&rejected(independentGroup,ProcessGroupRejection::InvalidTopology));
    write(gameFile,shipping+add(app,tree.rootPid,rootExe,tree.rootCreated));
    const auto shortGroup=owner.observeProcessGroup(source,selectedLease,current);
    check("actual-related-short-id-incomplete-no-lease",!captured(shortGroup)&&shortGroup.parsedSource()&&shortGroup.parsedSource()->processLifetime.group.status==TrackerGroupStatus::Incomplete&&shortGroup.parsedSource()->processLifetime.group.relatedFullGameIdUnknown);
    write(gameFile,shipping+add(full,tree.rootPid,rootExe,tree.rootCreated-100000000ULL));
    const auto oldRoot=owner.observeProcessGroup(source,selectedLease,current);
    check("actual-old-root-source-birth-window-refused",rejected(oldRoot,ProcessGroupRejection::BirthWindowMismatch));
    write(gameFile,add(full,tree.shippingPid,shippingExe,tree.shippingCreated-600000000ULL));write(consoleFile,primary(full,tree.shippingPid,tree.shippingCreated));
    const auto oldSelected=owner.observeProcessGroup(source,selectedLease,current);
    check("actual-old-selected-source-cannot-bind-new-birth",!captured(oldSelected)&&oldSelected.parsedSource()&&oldSelected.parsedSource()->processLifetime.binding.status!="tracked");
    ProcessWitness reused=selected;++reused.created;
    const auto wrongBirth=processOwner().acquire(g_steamSettingsObserver,g_steamSettingsObserver.identitySnapshot(),reused);
    const auto beforeReuse=fixtureProjectionSourceReads;
    const auto reusedSource=owner.observeProcessGroup(source,wrongBirth,current);
    check("actual-same-pid-different-birth-cannot-mint-or-read",!wrongBirth&&!captured(reusedSource)&&fixtureProjectionSourceReads==beforeReuse);
    seed();write(consoleFile,primary(full,independent.pi.dwProcessId,tree.shippingCreated));
    const auto missingRole=owner.observeProcessGroup(source,selectedLease,current);
    check("actual-native-role-outside-current-members-refused",rejected(missingRole,ProcessGroupRejection::InvalidTopology));
    seed();const auto beforeCancel=fixtureProjectionSourceReads;
    const auto cold=owner.observeProcessGroup(source,selectedLease,[](){return false;});
    check("actual-cold-cancel-has-no-source-read",!cold.sourcesComplete()&&!captured(cold)&&fixtureProjectionSourceReads==beforeCancel);
    bool admitted=true;fixtureFreshReadHook=[&](const std::wstring& file){if(file.ends_with(L"console_log.txt"))admitted=false;};
    const auto midCancel=owner.observeProcessGroup(source,selectedLease,[&](){return admitted;});fixtureFreshReadHook={};admitted=true;
    check("actual-cancel-during-console-read-not-revived",!midCancel.sourcesComplete()&&!captured(midCancel)&&!captured(cold));
    seed();unsigned validCalls=0;auto countProbe=owner.observeProcessGroup(source,selectedLease,[&](){++validCalls;return true;});
    check("actual-final-admission-control-mints-only-current-lease",captured(countProbe)&&validCalls>0&&captured(countProbe)->processCurrent());
    unsigned failedCalls=0;const auto finalCancel=owner.observeProcessGroup(source,selectedLease,[&](){return ++failedCalls!=validCalls;});
    check("actual-final-current-failure-clears-capture-context-changed",failedCalls==validCalls&&!finalCancel.sourcesComplete()&&rejected(finalCancel,ProcessGroupRejection::ContextChanged));
    check("actual-final-current-late-true-cannot-revive-returned-capture",!captured(finalCancel)&&captured(countProbe)&&captured(countProbe)->processCurrent());
    seed();const auto preRead=sgNowFileTime();bool crossedSecond=false;
    fixtureFreshReadHook=[&](const std::wstring& file){
        if(!file.ends_with(L"gameprocess_log.txt"))return;
        const auto before=SleepSteamRecovery::localSecond(sgNowFileTime());
        crossedSecond=until([&](){return SleepSteamRecovery::localSecond(sgNowFileTime())!=before;},2000);
        if(!crossedSecond)throw std::runtime_error("group-role clock second boundary");
        write(consoleFile,console+"["+SleepSteamRecovery::localSecond(sgNowFileTime())+"] Game process removed: AppID "+std::to_string(full)+" fixture, ProcID "+std::to_string(tree.rootPid)+"\n");
    };
    const auto lateRemoved=owner.observeProcessGroup(source,selectedLease,current);fixtureFreshReadHook={};
    bool removedObserved=false;
    if(lateRemoved.nativePrimary()) {
        const auto removed=lateRemoved.nativePrimary()->rows.find(app);
        removedObserved=removed!=lateRemoved.nativePrimary()->rows.end()&&removed->second.removed;
    }
    check("actual-source-owned-read-end-boundary-sees-late-removal",crossedSecond&&lateRemoved.absoluteBoundary()>=preRead&&removedObserved&&!captured(lateRemoved));
    seed();auto explicitCancel=owner.observeProcessGroup(source,selectedLease,current);auto cancelledLease=captured(explicitCancel);
    if(cancelledLease)cancelledLease->cancel();
    check("actual-explicit-lease-cancel-is-sticky",cancelledLease&&!cancelledLease->processCurrent()&&!cancelledLease->inspectNativeRoles(tree.rootPid,tree.shippingPid)&&!cancelledLease->processCurrent());
    auto accountObservation=owner.observeProcessGroup(source,selectedLease,current);auto accountLease=captured(accountObservation);
    const auto oldIdentity=g_steamSettingsObserver.identitySnapshot();registry.number(L"ActiveUser",43);
    const bool accountChanged=until([&](){const auto p=g_steamSettingsObserver.identitySnapshot();return p&&p->session.account==43&&p->accountEpoch!=oldIdentity->accountEpoch;});
    check("actual-observer-account-epoch-revokes-group",accountChanged&&accountLease&&!accountLease->processCurrent()&&!accountLease->inspectNativeRoles(tree.rootPid,tree.shippingPid));
    registry.number(L"ActiveUser",42);
    const bool accountReturned=until([&](){const auto p=g_steamSettingsObserver.identitySnapshot();return p&&p->session.account==42&&p->accountEpoch!=oldIdentity->accountEpoch;});
    check("actual-account-aba-does-not-revive-old-group",accountReturned&&accountLease&&!accountLease->processCurrent()&&!accountLease->inspectNativeRoles(tree.rootPid,tree.shippingPid));
    selectedLease=processOwner().acquire(g_steamSettingsObserver,g_steamSettingsObserver.identitySnapshot(),selected);
    source=MainTrackerFactsAdapter::sourceKey({steam.pi.dwProcessId,birth(steam.pi.hProcess),root});
    if(!selectedLease||!source)throw std::runtime_error("group-role fresh account process pin");
    seed();auto beforeExit=owner.observeProcessGroup(source,selectedLease,current);auto rootExitLease=captured(beforeExit);
    check("actual-new-request-after-account-return-needs-new-pin",role(rootExitLease,tree.rootPid,tree.shippingPid,roles::State::VerifiedGroupRoles)&&accountLease&&!accountLease->processCurrent());
    const bool rootExited=tree.finishRoot();
    check("actual-root-exit-revokes-old-role-lease",rootExited&&WaitForSingleObject(tree.shippingProcess.value,0)==WAIT_TIMEOUT&&rootExitLease&&!rootExitLease->processCurrent()&&!rootExitLease->inspectNativeRoles(tree.rootPid,tree.shippingPid));
    const auto staleRoot=owner.observeProcessGroup(source,selectedLease,current);
    check("actual-dead-root-still-in-source-cannot-repin",!captured(staleRoot)&&rootExitLease&&!rootExitLease->processCurrent());
    write(gameFile,shipping);write(consoleFile,primary(full,tree.shippingPid,tree.shippingCreated));
    auto survivor=owner.observeProcessGroup(source,selectedLease,current);auto survivorLease=captured(survivor);
    check("actual-normal-root-exit-allows-fresh-surviving-exact-role",role(survivorLease,tree.shippingPid,tree.shippingPid,roles::State::ExactSelected)&&survivorLease->graph().count==1&&rootExitLease&&!rootExitLease->processCurrent());
    const bool selectedExited=tree.finishShipping();
    check("actual-selected-exit-revokes-survivor-lease",selectedExited&&survivorLease&&!survivorLease->processCurrent()&&!survivorLease->inspectNativeRoles(tree.shippingPid,tree.shippingPid));
    const auto afterSelectedExit=owner.observeProcessGroup(source,selectedLease,current);
    check("actual-selected-exit-cannot-revive-from-old-source",!captured(afterSelectedExit)&&survivorLease&&!survivorLease->processCurrent());
    exactLease.reset();groupedLease.reset();reorderedLease.reset();cancelledLease.reset();accountLease.reset();rootExitLease.reset();survivorLease.reset();selectedLease.reset();
    independent.finish();g_steamSettingsObserver.stop();steam.finish();
    const bool fixtureProcessesEnded=WaitForSingleObject(independent.pi.hProcess,0)==WAIT_OBJECT_0&&WaitForSingleObject(steam.pi.hProcess,0)==WAIT_OBJECT_0;
    const bool treeReturned=tree.finish();
    check("actual-inert-tree-job-zero-before-close-without-force",treeReturned&&tree.zero&&tree.closed&&!tree.forced&&fixtureProcessesEnded&&!g_steamSettingsObserver.identitySnapshot());
}
static int run(const std::wstring& root,const std::wstring& childPath,Registry& registry,bool optInOnly=false) {
    check("explicit-selector-excludes-default-projection-and-fresh-groups",tests==0&&projectionChecks.empty()&&freshChecks.empty()&&sourceChecks.empty());
    if(!optInOnly)pureCases();actualCases(root,childPath,registry,optInOnly);
    unsigned failures=0;for(const auto& row:checks)if(!row.at("pass").get<bool>())++failures;
    std::cout<<"TRACKER_ROLE_JSON "<<json({{"selection","tracker-process-group-roles"},{"checks",checks},{"failures",failures},
        {"defaultGroupExecuted",false},{"projectionGroupExecuted",false},{"freshGroupExecuted",false},
        {"actualHeldProcessLease",true},{"actualRootCreatedShipping",true},{"realSteamOperations",0},
        {"gameReads",fixtureFreshGameReads},{"consoleReads",fixtureFreshConsoleReads}}).dump()<<std::endl;
    return failures?2:0;
}
} // namespace SteamGameGroupRoleFixture
