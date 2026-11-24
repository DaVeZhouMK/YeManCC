#pragma once
// Private directed fixture only. A bounded real process supplies the process
// handle required by the unmodified public offer producer. It never claims an
// offer, acknowledges Host business, or represents a real installed InputHost.
struct FixtureNativePeer {
    HANDLE original=nullptr,observed=nullptr,stop=nullptr;
    DWORD pid=0;
    std::uint64_t birth=0;
};
static std::vector<FixtureNativePeer> fixtureNativePeers;
static json fixtureNativePeerReport=json::array();
static bool fixtureNativePeersFinished=false;
static std::uint64_t fixtureNativePeerBirth(HANDLE process) noexcept {
    FILETIME created{},exited{},kernel{},user{};
    if(!GetProcessTimes(process,&created,&exited,&kernel,&user))return 0;
    ULARGE_INTEGER value{};value.LowPart=created.dwLowDateTime;value.HighPart=created.dwHighDateTime;return value.QuadPart;
}
static FixtureNativePeer* fixtureFindNativePeer(HANDLE process) noexcept {
    if(!process)return nullptr;
    const auto pid=GetProcessId(process);const auto birth=fixtureNativePeerBirth(process);
    for(auto& peer:fixtureNativePeers)if(peer.pid==pid&&peer.birth==birth)return &peer;
    return nullptr;
}
static HANDLE fixtureNativePeerRecordedHandle(const ymcc::targetReturn::ProcessIdentity& owner) noexcept {
    if(owner.creationTime.empty()||owner.creationTime.size()>20||owner.creationTime.front()=='0')return nullptr;
    std::uint64_t birth=0;for(const auto digit:owner.creationTime){
        if(digit<'0'||digit>'9')return nullptr;const auto value=static_cast<unsigned>(digit-'0');
        if(birth>((~std::uint64_t{0})-value)/10)return nullptr;birth=birth*10+value;
    }
    for(const auto& peer:fixtureNativePeers)if(peer.pid==owner.pid&&peer.birth==birth&&
        peer.observed&&GetProcessId(peer.observed)==peer.pid&&fixtureNativePeerBirth(peer.observed)==peer.birth)
        return peer.observed;
    return nullptr;
}
static int fixtureNativePeerChild(int argc,wchar_t** argv) noexcept {
    if(argc<2||std::wstring_view(argv[1])!=L"--ymcc-private-native-power-peer")return -1;
    if(argc!=6)return 121;
    wchar_t *a=nullptr,*b=nullptr,*c=nullptr,*d=nullptr;
    const auto stop=reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(std::wcstoull(argv[2],&a,10)));
    const auto parent=reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(std::wcstoull(argv[3],&b,10)));
    const auto pid=std::wcstoull(argv[4],&c,10),birth=std::wcstoull(argv[5],&d,10);
    DWORD flags=0;const bool valid=a&&!*a&&b&&!*b&&c&&!*c&&d&&!*d&&pid&&pid<=MAXDWORD&&birth&&
        stop&&parent&&stop!=parent&&GetProcessId(parent)==pid&&pid!=GetCurrentProcessId()&&
        fixtureNativePeerBirth(parent)==birth&&GetHandleInformation(stop,&flags)&&
        WaitForSingleObject(parent,0)==WAIT_TIMEOUT;
    DWORD outcome=WAIT_FAILED;
    if(valid){HANDLE waits[]={stop,parent};outcome=WaitForMultipleObjects(2,waits,FALSE,120000);}
    const bool stopClosed=stop&&CloseHandle(stop),parentClosed=parent&&CloseHandle(parent);
    return valid&&outcome==WAIT_OBJECT_0&&stopClosed&&parentClosed?0:122;
}
static BOOL fixtureCreateNativePeer(STARTUPINFOW& input,PROCESS_INFORMATION& output) {
    if(!fixtureNativePeerEnabled||fixtureFullPipe||fixtureNativePeersFinished)return FALSE;
    if(fixtureNativePeers.size()>=32)throw std::runtime_error("native-private-peer-finite-capacity");
    // Reserve the owning record before process creation; every later failure
    // keeps or retires this exact child, never a PID lookup or a peer process.
    fixtureNativePeers.reserve(32);fixtureNativePeers.emplace_back();auto& peer=fixtureNativePeers.back();
    HANDLE parent=nullptr;PROCESS_INFORMATION made{};
    SECURITY_ATTRIBUTES security{sizeof(security),nullptr,TRUE};
    peer.stop=CreateEventW(&security,TRUE,FALSE,nullptr);
    if(!peer.stop||!DuplicateHandle(GetCurrentProcess(),GetCurrentProcess(),GetCurrentProcess(),&parent,
        SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,TRUE,0)){
        const auto error=GetLastError();if(peer.stop)CloseHandle(peer.stop);if(parent)CloseHandle(parent);
        fixtureNativePeers.pop_back();SetLastError(error);return FALSE;
    }
    const auto parentBirth=fixtureNativePeerBirth(parent);
    wchar_t executable[32768]{};const auto length=GetModuleFileNameW(nullptr,executable,32768);
    SIZE_T bytes=0;InitializeProcThreadAttributeList(nullptr,1,0,&bytes);
    std::vector<unsigned char> storage;STARTUPINFOEXW start{};start.StartupInfo=input;
    start.StartupInfo.cb=sizeof(start);
    bool attributes=false;HANDLE inherited[]={peer.stop,parent};
    std::wstring command;
    const auto failBefore=[&](DWORD error){if(attributes)DeleteProcThreadAttributeList(start.lpAttributeList);CloseHandle(parent);CloseHandle(peer.stop);fixtureNativePeers.pop_back();SetLastError(error);return FALSE;};
    try{
        if(!length||length>=32768||!parentBirth||executable[0]==L'\0')return failBefore(ERROR_INVALID_DATA);
        storage.resize(bytes);start.lpAttributeList=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
        attributes=InitializeProcThreadAttributeList(start.lpAttributeList,1,0,&bytes)!=FALSE;
        if(!attributes||!UpdateProcThreadAttribute(start.lpAttributeList,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
            inherited,sizeof(inherited),nullptr,nullptr))return failBefore(GetLastError());
        command=L"\""+std::wstring(executable)+L"\" --ymcc-private-native-power-peer "+
            std::to_wstring(reinterpret_cast<std::uintptr_t>(peer.stop))+L" "+
            std::to_wstring(reinterpret_cast<std::uintptr_t>(parent))+L" "+
            std::to_wstring(GetCurrentProcessId())+L" "+std::to_wstring(parentBirth);
        if(!CreateProcessW(executable,command.data(),nullptr,nullptr,TRUE,
            CREATE_NO_WINDOW|EXTENDED_STARTUPINFO_PRESENT,nullptr,g_yemanccDataDir.c_str(),&start.StartupInfo,&made))
            return failBefore(GetLastError());
    }catch(...){return failBefore(ERROR_NOT_ENOUGH_MEMORY);}
    DeleteProcThreadAttributeList(start.lpAttributeList);CloseHandle(parent);
    peer.original=made.hProcess;peer.pid=made.dwProcessId;peer.birth=fixtureNativePeerBirth(made.hProcess);
    if(!peer.birth||!DuplicateHandle(GetCurrentProcess(),made.hProcess,GetCurrentProcess(),&peer.observed,0,FALSE,DUPLICATE_SAME_ACCESS)){
        // Creation has happened; preserve an independently actionable handle
        // if retirement cannot be proven. The enclosing private Job also owns it.
        const auto error=GetLastError();SetEvent(peer.stop);const auto waited=WaitForSingleObject(made.hProcess,5000);
        CloseHandle(made.hThread);peer.observed=made.hProcess;
        if(waited==WAIT_OBJECT_0){CloseHandle(peer.observed);CloseHandle(peer.stop);fixtureNativePeers.pop_back();}
        SetLastError(error?error:ERROR_INVALID_DATA);return FALSE;
    }
    try{ymStartSeamTrace("create-process");}catch(...){
        SetEvent(peer.stop);const auto waited=WaitForSingleObject(peer.observed,5000);
        CloseHandle(made.hThread);CloseHandle(made.hProcess);
        if(waited==WAIT_OBJECT_0){CloseHandle(peer.observed);CloseHandle(peer.stop);fixtureNativePeers.pop_back();}
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);return FALSE;
    }
    output=made;return TRUE;
}
static DWORD fixtureNativePeerExitWait(HANDLE process) {
    auto* peer=fixtureFindNativePeer(process);if(!peer)return WAIT_FAILED;
    if(g_startEntrySeam.hostExitWait!=WAIT_OBJECT_0)return WaitForSingleObject(peer->observed,0);
    if(!SetEvent(peer->stop))return WAIT_FAILED;
    return WaitForSingleObject(peer->observed,5000);
}
static bool fixtureNativePeersFinish() noexcept {
    if(fixtureNativePeersFinished)return true;
    bool all=true;
    for(auto& peer:fixtureNativePeers){
        const bool signaled=peer.stop&&SetEvent(peer.stop)!=FALSE;
        const auto wait=peer.observed?WaitForSingleObject(peer.observed,5000):WAIT_FAILED;
        DWORD exit=999;const bool exitKnown=peer.observed&&GetExitCodeProcess(peer.observed,&exit)!=FALSE;
        const bool owned=peer.observed&&GetProcessId(peer.observed)==peer.pid&&fixtureNativePeerBirth(peer.observed)==peer.birth;
        const bool stopClosed=peer.stop&&CloseHandle(peer.stop)!=FALSE;
        const bool observedClosed=peer.observed&&CloseHandle(peer.observed)!=FALSE;
        all=all&&signaled&&wait==WAIT_OBJECT_0&&exitKnown&&exit==0&&owned&&stopClosed&&observedClosed;
        try{fixtureNativePeerReport.push_back({{"pid",peer.pid},{"birth100ns",std::to_string(peer.birth)},
            {"wait",wait},{"exitKnown",exitKnown},{"exitCode",exit},{"exactCreatedIdentity",owned},
            {"controlEventClosed",stopClosed},{"processObserverClosed",observedClosed},
            {"realInputHostBusiness",false},{"offerClaimedByFixturePeer",false},{"childMaximumWaitMs",120000}});}catch(...){all=false;}
        peer.stop=nullptr;peer.observed=nullptr;
    }
    fixtureNativePeersFinished=true;return all;
}

static BOOL fixtureNativePeerSignal(HANDLE process) {
    auto* peer=fixtureFindNativePeer(process);
    if(!peer)return SetEvent(process);
    if(!SetEvent(peer->stop))return FALSE;
    return WaitForSingleObject(peer->observed,5000)==WAIT_OBJECT_0;
}
static void fixtureNativePeersBeginCase() {
    if(!fixtureNativePeerEnabled)return;
    if(!fixtureNativePeers.empty()){
        if(g_inputHostProcess||g_inputHostPrimaryThread||g_inputHostPrepared)
            throw std::logic_error("private-peer-previous-product-owner-not-returned");
        if(!fixtureNativePeersFinish())throw std::logic_error("private-peer-previous-child-not-returned");
        fixtureNativePeers.clear();fixtureNativePeersFinished=false;
    }
}
struct FixtureNativePeerOwner final {
    ~FixtureNativePeerOwner(){if(!fixtureNativePeersFinished)(void)fixtureNativePeersFinish();}
};
