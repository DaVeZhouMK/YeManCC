#pragma once
// Included after sleep and input owners. IPC workers do not run controller or
// resume primitives directly: repair tickets authorize the existing owners only.
static std::wstring getKnownFolder(REFKNOWNFOLDERID id);
namespace SleepRecoveryApi {
using namespace ymcc::sleeprecovery;
struct Ticket {
    std::string id;
    ULONGLONG issued=0,generation=0,inputEpoch=0;
    std::vector<SgWakeRecoveryEntry> entries;
    bool stale=false,inputAllowed=false;
    json report;
};
static std::mutex mx;
static Ticket latest;
static unsigned long long serial=0;
struct Tail {std::string bytes;json meta;};
static Tail readTail(const std::wstring& path,DWORD cap=2*1024*1024) {
    Tail t;t.meta={{"available",false},{"truncated",false},{"limitBytes",cap}};
    const DWORD attr=GetFileAttributesW(path.c_str());
    if(attr==INVALID_FILE_ATTRIBUTES || (attr&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))) {
        t.meta["reason"]="missing-or-not-regular-file";return t;
    }
    HANDLE h=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
        nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(h==INVALID_HANDLE_VALUE){t.meta["reason"]="read-denied";t.meta["win32Error"]=GetLastError();return t;}
    LARGE_INTEGER size{};
    if(!GetFileSizeEx(h,&size)||size.QuadPart<0){CloseHandle(h);t.meta["reason"]="size-unavailable";return t;}
    const auto take=static_cast<DWORD>(std::min<LONGLONG>(size.QuadPart,cap));
    LARGE_INTEGER offset{};offset.QuadPart=size.QuadPart-take;
    DWORD got=0;t.bytes.resize(take);
    const bool ok=SetFilePointerEx(h,offset,nullptr,FILE_BEGIN)&&ReadFile(h,t.bytes.data(),take,&got,nullptr);
    const DWORD error=ok?0:GetLastError();CloseHandle(h);
    if(!ok){t.bytes.clear();t.meta["reason"]="read-failed";t.meta["win32Error"]=error;return t;}
    t.bytes.resize(got);t.meta["available"]=true;t.meta["originalBytes"]=size.QuadPart;
    t.meta["copiedBytes"]=got;t.meta["truncated"]=offset.QuadPart>0;
    return t;
}
struct Leases {std::vector<SgWakeRecoveryEntry> entries;size_t count=0;bool blocked=false,owner=false;};
// Requires g_sgOpMtx. Read-only, never modifies the automatic recovery batch.
static Leases readLeases() {
    Leases s;SgSleepTarget owner;s.owner=sgReadSleepTarget(owner)&&owner.markerOwned;
    std::error_code ec;const bool exists=fspath::exists(SG_SLEEP_LEASE_DIR,ec);
    if(ec){s.blocked=true;return s;}
    if(exists) {
        fspath::directory_iterator it(SG_SLEEP_LEASE_DIR,ec),end;
        if(ec){s.blocked=true;return s;}
        for(;it!=end;it.increment(ec)) {
            if(ec){s.blocked=true;break;}
            const auto& file=*it;if(file.path().extension()!=L".txt")continue;
            if(++s.count>64){s.blocked=true;break;}
            const auto stem=W2U(file.path().stem().wstring());unsigned long long pid=0,created=0,gen=0;
            auto tail=readTail(file.path().wstring(),8192);
            const auto& raw=tail.bytes;
            if(!decimal(stem,pid)||!pid||pid>MAXDWORD||!flag(tail.meta,"available")||flag(tail.meta,"truncated")||
                field(raw,"state")!="suspended"||!decimal(field(raw,"pid"),gen)||gen!=pid||
                !decimal(field(raw,"created"),created)||!created||!decimal(field(raw,"generation"),gen)||!gen) {
                s.blocked=true;continue;
            }
            SgWakeRecoveryEntry e{SG_SLEEP_LEASE_DIR,static_cast<DWORD>(pid),created,raw};
            if(s.owner&&owner.pid==pid&&owner.processCreated==created&&owner.powerGeneration==gen)
                e.sleepOwnerGeneration=gen;
            s.entries.push_back(std::move(e));
        }
    }
    if(ec)s.blocked=true;
    if(s.owner&&std::none_of(s.entries.begin(),s.entries.end(),[&](const auto& e){return e.pid==owner.pid;})) {
        const auto path=SG_SLEEP_LEASE_DIR+L"\\"+std::to_wstring(owner.pid)+L".txt";
        if(!fspath::exists(path,ec)&&!ec&&owner.pid&&owner.processCreated&&owner.powerGeneration) {
            SgWakeRecoveryEntry e{SG_SLEEP_LEASE_DIR,owner.pid,owner.processCreated,{}};
            e.sleepOwnerGeneration=owner.powerGeneration;s.entries.push_back(e);++s.count;
        } else s.blocked=true;
    }
    std::sort(s.entries.begin(),s.entries.end(),[](const auto& a,const auto& b){return a.pid<b.pid;});
    if(s.entries.size()>64)s.blocked=true;
    return s;
}
static bool sameLeases(const Leases& now,const Ticket& t) {
    if(now.blocked||now.entries.size()!=t.entries.size())return false;
    for(size_t i=0;i<t.entries.size();++i){const auto& a=now.entries[i];const auto& b=t.entries[i];
        if(a.pid!=b.pid||a.created!=b.created||a.markerText!=b.markerText||a.sleepOwnerGeneration!=b.sleepOwnerGeneration)return false;}
    return true;
}
static json inputSnapshot() {
    std::lock_guard lock(g_inputHostMx);
    return {{"epoch",g_inputHostEpochMirror.load()},{"boundGeneration",g_inputHostBoundPowerGeneration},{"keptTargetGeneration",g_wakeKeptTargetGeneration.load()},{"powerBindingProofGeneration",inputHostWakeBindingProofLocked()},
        {"alive",inputHostProcessAliveLocked()},{"prepared",g_inputHostPrepared},
        {"wanted",g_sleepRecoveryOutputWanted.load()},
        {"quarantined",g_inputHostReleaseQuarantined},{"transportFault",g_inputHostTransportFault},
        {"faultHold",g_inputHostFaultHoldsStart.load()},{"admissionRevoked",g_inputHostPowerAdmissionRevoked.load()},
        {"ready",g_inputReady.load()},{"gameInput","unknown"}};
}
static bool inputAllowed(const json& i,ULONGLONG generation,ULONGLONG epoch) {
    return inputGate(g_aiFanMockSession.isolated,g_exitRequested.load(),g_powerLifecycle.load()==PowerLifecycle::Ready,
        hardwareWriteAllowed(),flag(i,"ready"),flag(i,"wanted"),flag(i,"alive"),flag(i,"prepared"),
        flag(i,"quarantined"),flag(i,"transportFault"),generation,currentPowerGeneration(),epoch,number(i,"epoch")) &&
        inputBindingAllowed(number(i,"boundGeneration"),generation,number(i,"powerBindingProofGeneration"))&&!flag(i,"faultHold");
}
static std::string gate(const Ticket& t,const Leases& now,bool confirmed,bool tokenKnown) {
    return repairGate(confirmed,g_aiFanMockSession.isolated,g_exitRequested.load(),
        g_powerLifecycle.load()==PowerLifecycle::Ready,hardwareWriteAllowed(),sgSleepRetryActive(),
        g_sgWakeRecoveryPending.load(),tokenKnown,t.issued,GetTickCount64(),t.generation,currentPowerGeneration(),sameLeases(now,t));
}
static json inspect(const json& a) {
    json ids=json::array();
    if(a.contains("appid")&&!a["appid"].is_null()) {
        const auto id=number(a,"appid");if(!id||id>MAXDWORD)throw std::runtime_error("invalid-steam-appid");ids.push_back(id);
    }
    std::lock_guard<std::mutex> lock(mx);Ticket t;
    t.generation=currentPowerGeneration();t.id="sleep-check-"+std::to_string(GetTickCount64())+"-"+std::to_string(++serial);
    json state,games=json::array(),input;Leases leases;
    {
        std::lock_guard<std::mutex> op(g_sgOpMtx);leases=readLeases();t.entries=leases.entries;
        state={{"leaseCount",leases.count},{"ownerPresent",leases.owner},{"markerLimit",leases.blocked},
            {"inSuspend",g_sgInSuspend.load()},{"gameSuspended",g_sgGameActuallySuspended.load()},
            {"recoveryPending",g_sgWakeRecoveryPending.load()}};
        input=inputSnapshot();t.inputEpoch=number(input,"epoch");t.inputAllowed=inputAllowed(input,t.generation,t.inputEpoch);
        t.stale=!leases.count&&!leases.owner&&(flag(state,"inSuspend")||flag(state,"gameSuspended"));
    }
    std::vector<std::pair<DWORD,ULONGLONG>> targets;
    for(const auto& e:t.entries)targets.push_back({e.pid,e.created});
    if(targets.empty()){NativeDetectedGame game;if(nativeValveReadCurrentTarget(&game))targets.push_back({game.pid,game.processCreated});}
    for(const auto& pair:targets) {
        HANDLE h=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,pair.first);
        const bool same=h&&nativeProcessCreatedFromHandle(h)==pair.second&&WaitForSingleObject(h,0)==WAIT_TIMEOUT;
        const auto threads=same?WakeGameResume::queryThreads(pair.first):WakeGameResume::ThreadState{};
        // A thread snapshot may race exit; never substitute another process with the same PID.
        const bool stillSame=same&&nativeProcessCreatedFromHandle(h)==pair.second&&WaitForSingleObject(h,0)==WAIT_TIMEOUT;
        if(h)CloseHandle(h);
        games.push_back({{"pid",pair.first},{"created",std::to_string(pair.second)},{"identityMatches",stillSame},
            {"threadsKnown",stillSame&&threads.known},{"threadCount",threads.total},{"suspendedThreads",threads.suspended},
            {"allRunning",stillSame&&threads.allRunning()}});
    }
    auto tail=readTail(SG_FACT_LOG);std::vector<std::string> lines;
    std::istringstream stream(tail.bytes);std::string line;
    if(flag(tail.meta,"truncated"))std::getline(stream,line);
    while(std::getline(stream,line)){if(lines.size()==4096)lines.erase(lines.begin());lines.push_back(line);}
    {std::lock_guard<std::mutex> fact(g_sgFactMx);for(const auto& row:g_sgFacts)lines.push_back(row);}
    DWORD account=0;sofRegDWORD(HKEY_CURRENT_USER,L"Software\\Valve\\Steam\\ActiveProcess",L"ActiveUser",&account);
    json steam=steamLiveRequest(account,{{"operation","sleep.inspect"},{"appids",ids}});
    state["steamAvailable"]=flag(steam,"ok");state["steamTargetKnown"]=false;state["steamMismatch"]=false;
    // An explicit AppID is operator supplied, NOT a proven association with an OS PID.
    if(flag(steam,"ok")&&!ids.empty()&&steam.contains("overviews")&&steam["overviews"].is_array())
        for(const auto& o:steam["overviews"])if(number(o,"appid")==ids[0].get<ULONGLONG>()&&flag(o,"overviewAvailable"))state["steamTargetKnown"]=true;
    state["inputReady"]=flag(input,"ready");state["inputFault"]=flag(input,"quarantined")||flag(input,"transportFault")||flag(input,"faultHold");
    std::string refusal;
    {
        std::lock_guard<std::mutex> op(g_sgOpMtx);const auto now=readLeases();t.issued=GetTickCount64();refusal=gate(t,now,true,true);
        const auto fresh=inputSnapshot();if(refusal.empty()&&number(fresh,"epoch")!=t.inputEpoch)refusal="state-changed";
        t.inputAllowed=t.inputAllowed&&inputAllowed(fresh,t.generation,t.inputEpoch);
        if(refusal.empty()&&(!g_inputReady.load()||leases.blocked))refusal="inspection-incomplete";
        for(const auto& e:t.entries)if(sgMarkerIsSuspended(SG_MANUAL_DIR,e.pid))refusal="manual-pause-protected";
    }
    if(refusal.empty()&&t.entries.empty()&&!t.stale&&!t.inputAllowed)refusal="no-safe-action";
    state["generation"]=t.generation;state["inputEpoch"]=t.inputEpoch;
    const auto findings=checks(state,lines);
    t.report={{"ok",true},{"inspectionToken",t.id},{"expiresInMs",30000},{"generation",t.generation},
        {"healthVerified",false},{"inspectionEpoch",sgNowEpoch()},
        {"platform",{{"manufacturer",W2U(sofRegSZ(HKEY_LOCAL_MACHINE,L"HARDWARE\\DESCRIPTION\\System\\BIOS",L"SystemManufacturer"))},
            {"model",W2U(sofRegSZ(HKEY_LOCAL_MACHINE,L"HARDWARE\\DESCRIPTION\\System\\BIOS",L"SystemProductName"))},
            {"windowsBuild",W2U(sofRegSZ(HKEY_LOCAL_MACHINE,L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",L"CurrentBuildNumber"))}}},{"state",state},{"games",games},{"input",input},{"steam",steam},{"checks",findings},
        {"log",tail.meta},{"repair",{{"allowed",refusal.empty()},{"reason",refusal},{"sleepTargets",t.entries.size()},
            {"inputRefresh",t.inputAllowed},{"staleFlagReconcile",t.stale}}},
        {"limits",json::array({"No Steam restart/game launch/AppID spoofing/target recreation.",
            "Steam AppID is operator supplied; game input and Big Picture continue state need manual verification.",
            "Bounded log tail, current process/thread snapshot; no automatic upload."})}};
    latest=t;sgRecordFact("sleep-recovery-inspected",{{"inspectionToken",t.id},{"generation",t.generation},
        {"leaseCount",leases.count},{"repairAllowed",refusal.empty()},{"healthVerified",false}});
    return t.report;
}
static json repair(const json& a,unsigned long long autoEpoch=0) {
    std::lock_guard<std::mutex> lock(mx);std::lock_guard<std::mutex> op(g_sgOpMtx);
    if(autoEpoch&&(!sleepRecoveryAutoAllows(number(a,"generation"),autoEpoch)))
        return {{"ok",false},{"accepted",false},{"reason","automatic-recovery-cancelled"},{"healthVerified",false}};
    const auto now=readLeases();const bool known=!latest.id.empty()&&text(a,"inspectionToken")==latest.id;
    auto refusal=gate(latest,now,flag(a,"confirmed"),known);
    if(refusal.empty())for(const auto& e:latest.entries)if(sgMarkerIsSuspended(SG_MANUAL_DIR,e.pid))refusal="manual-pause-protected";
    const auto input=inputSnapshot();
    if(refusal.empty()&&(!g_inputReady.load()||number(input,"epoch")!=latest.inputEpoch))refusal="state-changed";
    if(refusal.empty()&&(!latest.report.contains("repair")||!flag(latest.report["repair"],"allowed")))refusal="inspection-not-repairable";
    if(!refusal.empty())return {{"ok",false},{"accepted",false},{"reason",refusal},{"healthVerified",false}};
    const std::string id=latest.id;latest.id.clear(); // single use; ambiguous IPC never retries a repair
    const bool queued=!latest.entries.empty()&&sgBeginUserSleepGameRecoveryUnlocked(latest.generation,latest.entries,id,autoEpoch);
    bool reconciled=false;
    if(latest.entries.empty()&&latest.stale&&!now.owner&&now.count==0){sgReconcileSleepGameFlagsAfterRecoveryCancel("user-inspected-repair");reconciled=true;}
    const bool refreshed=latest.inputAllowed&&inputAllowed(input,latest.generation,latest.inputEpoch)&&
        inputHostQueueLifecycleWork(((autoEpoch?"sleep-auto-input:":"sleep-repair-input:")+std::to_string(latest.generation)+":"+
            std::to_string(latest.inputEpoch)+(autoEpoch?":"+std::to_string(autoEpoch):std::string())).c_str());
    const bool accepted=queued||reconciled||refreshed;
    json result={{"ok",accepted},{"accepted",accepted},{"requestId",id},{"gameQueued",queued},{"flagsReconciled",reconciled},
        {"inputQueued",refreshed},{"reason",accepted?"accepted-not-health-verified":"state-changed"},{"healthVerified",false}};
    sgRecordFact("sleep-recovery-user-request",result);return result;
}
static bool write(const std::wstring& path,const std::string& bytes) {
    // No overwrite. Only new files under a freshly created feedback directory.
    HANDLE h=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(h==INVALID_HANDLE_VALUE)return false;DWORD done=0;
    const bool ok=WriteFile(h,bytes.data(),static_cast<DWORD>(bytes.size()),&done,nullptr)&&done==bytes.size();CloseHandle(h);return ok;
}
static json exportFeedback(const json& a) {
    json feedback=json::object();const json fields={{"gameInput",json::array({"unknown","working","broken"})},
        {"guide",json::array({"unknown","working","broken"})},{"bigPicture",json::array({"unknown","continue","start"})},
        {"overlay",json::array({"unknown","enabled","disabled"})}};
    for(auto it=fields.begin();it!=fields.end();++it){const auto v=text(a,it.key().c_str());
        if(std::find(it.value().begin(),it.value().end(),json(v))==it.value().end())throw std::runtime_error("invalid-feedback-choice");feedback[it.key()]=v;}
    const auto note=text(a,"note");if(note.size()>4096)throw std::runtime_error("feedback-note-too-long");feedback["note"]=note;
    json diagnosis;{std::lock_guard<std::mutex> lock(mx);diagnosis=latest.report;}
    if(diagnosis.is_null())diagnosis={{"ok",false},{"reason","inspection-not-performed"},{"healthVerified",false}};
    diagnosis["inspectionToken"]=nullptr;diagnosis["repairTicketReusable"]=false;diagnosis["feedbackExportEpoch"]=sgNowEpoch();
    feedback["source"]="operator-reported-not-automatic-verification";
    const auto desktop=getKnownFolder(FOLDERID_Desktop);if(desktop.empty())throw std::runtime_error("desktop-unavailable");
    GUID guid{};if(FAILED(CoCreateGuid(&guid)))throw std::runtime_error("feedback-id-unavailable");
    wchar_t id[40]{};StringFromGUID2(guid,id,40);const auto folder=desktop+L"\\YeMan-SleepFeedback-"+id;
    if(!CreateDirectoryW(folder.c_str(),nullptr))throw std::runtime_error("feedback-folder-create-failed");
    json manifest={{"schema",1},{"healthVerified",false},{"uploaded",false},{"diagnosisIsHistoricalSnapshot",true},
        {"privacy","May contain process names, paths, device identifiers and your note. Review before sharing; no credentials/config copied."},
        {"files",json::array()}};
    const bool diagnosisSaved=write(folder+L"\\diagnosis.json",diagnosis.dump(2,' ',false,json::error_handler_t::replace));
    const bool feedbackSaved=write(folder+L"\\operator-feedback.json",feedback.dump(2,' ',false,json::error_handler_t::replace));
    bool complete=diagnosisSaved&&feedbackSaved;
    manifest["files"].push_back({{"file","diagnosis.json"},{"saved",diagnosisSaved}});
    manifest["files"].push_back({{"file","operator-feedback.json"},{"saved",feedbackSaved}});
    const auto data=yemancc_data_dir();
    std::vector<std::pair<std::wstring,std::wstring>> logs={{L"sleep-facts.log",SG_FACT_LOG},{L"sleep-facts.log.1",SG_FACT_LOG+L".1"},
        {L"native-lifecycle.log",data+L"\\native-lifecycle.log"},{L"virtual-gamepad.log",data+L"\\virtual-gamepad.log"},
        {L"input-host-xbox-sdk.log",data+L"\\input-host-xbox-sdk.log"},{L"focus-trace.log",data+L"\\focus-trace.log"}};
    const auto root=sofRegSZ(HKEY_CURRENT_USER,L"Software\\Valve\\Steam",L"SteamPath");
    if(!root.empty()){logs.push_back({L"steam-gameprocess.log",root+L"\\logs\\gameprocess_log.txt"});
        logs.push_back({L"steam-controller.log",root+L"\\logs\\controller.txt"});}
    else {
        manifest["files"].push_back({{"file","steam-gameprocess.log"},{"available",false},{"reason","steam-root-unavailable"}});
        manifest["files"].push_back({{"file","steam-controller.log"},{"available",false},{"reason","steam-root-unavailable"}});
    }
    for(const auto& item:logs){auto tail=readTail(item.second);tail.meta["file"]=W2U(item.first);
        if(flag(tail.meta,"available")){const bool saved=write(folder+L"\\"+item.first,tail.bytes);tail.meta["saved"]=saved;complete=complete&&saved;}
        manifest["files"].push_back(tail.meta);}
    manifest["complete"]=complete;
    complete=write(folder+L"\\manifest.json",manifest.dump(2))&&complete;
    sgRecordFact("sleep-recovery-feedback-exported",{{"complete",complete},{"uploaded",false},{"healthVerified",false}});
    return {{"ok",complete},{"path",W2U(folder)},{"uploaded",false},{"reason",complete?"local-folder-created":"partial-export-review-folder"}};
}
}
static json sleepRecoveryInspect(const json& a){return SleepRecoveryApi::inspect(a);}
static json sleepRecoveryRepair(const json& a){return SleepRecoveryApi::repair(a);}
static json sleepRecoveryExport(const json& a){return SleepRecoveryApi::exportFeedback(a);}
