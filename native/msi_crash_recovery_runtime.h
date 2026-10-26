// Included by main.cpp after the existing MSI mode/visibility implementations.
// MSI-only write-ahead ownership; all hardware work stays on the lifecycle worker.
#include "msi_crash_recovery_journal.h"
#include "virtual_output_status.h"

namespace msiRecovery = ymcc::msi::recovery;
static std::mutex g_msiCrashMx;
static msiRecovery::Record g_msiCrashRecord;
static bool g_msiCrashLoaded = false;
static bool g_msiCrashStoreInvalid = false;
// Unknown durable authority blocks writes. Retry only bounded transient reads,
// on the existing serialized maintenance owner; malformed/foreign data is terminal.
static std::atomic<bool> g_msiCrashReadRetryPending{false};
static unsigned g_msiCrashReadAttempts = 0;
static ULONGLONG g_msiCrashReadRetryAt = 0, g_msiCrashReadStartedAt = 0;
static constexpr unsigned kMsiCrashReadMaxAttempts = 5;
static constexpr ULONGLONG kMsiCrashReadMaxMs = 30000;
static std::atomic<bool> g_msiCrashInheritedPending{false};
static bool g_msiCrashStartupAttempted = false;
// Only the exact journal reconstructed by this startup may be retired below.
static std::string g_msiCrashStartupTransactionId;
static std::vector<std::wstring> g_msiCrashStartupHiddenPaths;
static ULONGLONG g_msiCrashNextTick = 0;
static unsigned g_msiCrashUnhideAttempts = 0;
static unsigned g_msiCrashSettlementChecks = 0;
static constexpr unsigned kMsiCrashSettlementMaxChecks = 20;
static std::string g_msiCrashLastDecision;
static unsigned g_msiCrashDecisionRows = 0;

static std::wstring msiCrashRecoveryPath() { return app_data_dir()+L"\\msi-controller-return.json"; }
static std::string msiCrashBoardKey() {
    return g_machineIdentity.manufacturer+"|"+g_machineIdentity.product+"|"+
        g_machineIdentity.systemProduct+"|"+g_machineIdentity.deviceClass;
}
static std::string msiCrashProcessCreation(HANDLE process) {
    FILETIME creation{},exit{},kernel{},user{};
    if(!GetProcessTimes(process,&creation,&exit,&kernel,&user))return {};
    ULARGE_INTEGER value{};value.LowPart=creation.dwLowDateTime;value.HighPart=creation.dwHighDateTime;
    return std::to_string(value.QuadPart);
}
static std::string msiCrashCurrentOwner() {
    const auto creation=msiCrashProcessCreation(GetCurrentProcess());
    return creation.empty()?std::string{}:std::to_string(GetCurrentProcessId())+":"+creation;
}
static bool msiCrashOldOwnerDead(const msiRecovery::Record& record) {
    HANDLE handle=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,record.ownerPid);
    if(!handle)return GetLastError()==ERROR_INVALID_PARAMETER;
    const auto creation=msiCrashProcessCreation(handle);
    const DWORD wait=WaitForSingleObject(handle,0);CloseHandle(handle);
    return !creation.empty() && (creation!=record.ownerCreationTime || wait==WAIT_OBJECT_0);
}
static bool msiCrashHasDuty(const msiRecovery::Record& record) {
    return record.modeReturnPending || !record.hiddenPaths.empty();
}
static void msiCrashNote(const char* phase,const char* reason,bool persisted=false) {
    appendNativeLifecycleLog("msi-crash-return",{{"phase",phase},{"reason",reason},
        {"durableRecordCommitted",persisted},{"recordPath",W2U(msiCrashRecoveryPath())},
        {"modeReturnPending",g_msiCrashRecord.modeReturnPending},
        {"ownedHiddenPaths",g_msiCrashRecord.hiddenPaths},{"rootInstance",g_msiCrashRecord.rootInstance},
        {"recordOwnerPid",g_msiCrashRecord.ownerPid},{"recordOwnerCreationTime",g_msiCrashRecord.ownerCreationTime},
        {"modeAttemptOwner",g_msiCrashRecord.modeAttemptOwner},
        {"scope","known-MSI-board;exact-owned-paths;write-receipt-not-Steam-proof"}});
}
static void msiCrashDecision(const char* reason) {
    std::lock_guard<std::mutex> lock(g_msiCrashMx);
    if(g_msiCrashLastDecision==reason || g_msiCrashDecisionRows>=32)return;
    g_msiCrashLastDecision=reason;++g_msiCrashDecisionRows;msiCrashNote("recovery-observation",reason);
}
static bool msiCrashStoreLocked(const msiRecovery::Record& next,const char* reason) {
    if(!msiRecovery::validRecord(next) || !sgWriteFileAtomic(msiCrashRecoveryPath(),msiRecovery::encodeRecord(next))) {
        msiCrashNote("store-failed",reason);return false;
    }
    g_msiCrashRecord=next;msiCrashNote("record-committed",reason,true);return true;
}
static bool msiCrashReadBounded(const std::wstring& path,size_t limit,std::string& text) {
    WIN32_FILE_ATTRIBUTE_DATA attr{};
    if(!GetFileAttributesExW(path.c_str(),GetFileExInfoStandard,&attr) ||
       (attr.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)) ||
       attr.nFileSizeHigh || attr.nFileSizeLow>limit)return false;
    std::ifstream file(path,std::ios::binary);if(!file)return false;
    text.assign(std::istreambuf_iterator<char>(file),{});return text.size()<=limit && !file.bad();
}
// Conservative migration for an already affected installation. A hidden-list
// entry never establishes ownership: require the old full owned admission row,
// matching board identity and its independently retained process creation time.
static bool msiCrashLegacyRecord(msiRecovery::Record& record) {
    std::map<DWORD,std::string> boards,creations;std::map<DWORD,msiRecovery::Record> candidates;
    std::map<DWORD,std::string> latestTimes,lastMode2Times,lastDischargeTimes;
    std::map<std::string,std::vector<std::string>> historicalLocations;
    for(const auto* suffix:{L"\\msi-controller-visibility.log.1",L"\\msi-controller-visibility.log"}) {
        std::string text;if(!msiCrashReadBounded(app_data_dir()+suffix,8u<<20,text))continue;
        std::istringstream input(text);std::string line;
        while(std::getline(input,line)) {
            if(line.size()>32768 || line.find("\"event\":\"start\"")==std::string::npos)continue;
            try {const auto row=json::parse(line);if(row.value("event",std::string{})!="start")continue;
                const auto pid=row.at("parentPid").get<DWORD>();
                if(row.at("parentCreationTime").is_number_unsigned())creations[pid]=std::to_string(row.at("parentCreationTime").get<uint64_t>());
            }catch(...){}
        }
    }
    for(const auto* suffix:{L"\\native-lifecycle.log.1",L"\\native-lifecycle.log"}) {
        std::string text;if(!msiCrashReadBounded(app_data_dir()+suffix,8u<<20,text))continue;
        std::istringstream input(text);std::string line;
        while(std::getline(input,line)) {
            if(line.size()>65536 || (line.find("machine-identity")==std::string::npos &&
                line.find("hidhide.p-hid-admitted")==std::string::npos && line.find("hidhide.p-hid-restore\"")==std::string::npos &&
                line.find("msi-claw-gamepad-mode")==std::string::npos && line.find("msi-foreground-isolation")==std::string::npos))continue;
            try {const auto row=json::parse(line);const auto pid=row.at("pid").get<DWORD>();
                if(pid==GetCurrentProcessId())continue;
                const auto event=row.value("event",std::string{});
                if(event=="machine-identity") {
                    boards[pid]=row.at("manufacturer").get<std::string>()+"|"+row.at("product").get<std::string>()+"|"+
                        row.at("systemProduct").get<std::string>()+"|"+row.at("deviceClass").get<std::string>();continue;
                }
                if(boards[pid]!=msiCrashBoardKey() || !creations.count(pid))continue;
                if(event=="msi-foreground-isolation" && row.contains("physicalLocationPaths") && row.contains("sourceRoot")) {
                    const auto root=row.at("sourceRoot").get<std::string>();const auto paths=row.at("physicalLocationPaths").get<std::vector<std::string>>();
                    if(msiRecovery::validRootInstance(root) && paths.size()<=16 &&
                        std::all_of(paths.begin(),paths.end(),[](const auto& path){return msiRecovery::validLocationPath(path);}))
                        historicalLocations[std::to_string(pid)+"|"+hidHideUpper(root)]=paths;
                    continue;
                }
                if(event=="msi-claw-gamepad-mode" && row.value("action",std::string{})=="switch-dinput" && row.value("writeAttempted",false)) {
                    lastMode2Times[pid]=row.at("time").get<std::string>();continue;
                }
                if(event=="hidhide.p-hid-restore" && row.value("ownedTargetsReturned",false)) {
                    const auto found=candidates.find(pid);const auto transaction=row.value("transactionId",std::string{});
                    if(found!=candidates.end() && row.at("time").get<std::string>()>=latestTimes[pid] &&
                       (transaction==found->second.transactionId ||
                        transaction==found->second.transactionId+"#carried"))found->second.hiddenPaths.clear();
                    continue;
                }
                if(event!="hidhide.p-hid-admitted")continue;
                auto paths=row.at("physicalVisibilityTargets").get<std::vector<std::string>>();
                if(paths.empty() || row.at("hiddenByUs").get<size_t>()!=paths.size() || row.at("sources").size()!=1)continue;
                msiRecovery::Record next;next.boardKey=boards[pid];next.ownerPid=pid;next.ownerCreationTime=creations[pid];
                next.transactionId=row.at("transactionId").get<std::string>();next.modeReturnPending=true;
                next.rootInstance=row.at("sources").at(0).at("baseContainerDeviceInstanceId").get<std::string>();next.hiddenPaths=paths;
                if(!msiRecovery::validRecord(next))continue;
                candidates[pid]=next;latestTimes[pid]=row.at("time").get<std::string>();
            }catch(...){}
        }
    }
    // A later qualified mode discharge is separate from a hidden configuration return.
    for(const auto* suffix:{L"\\native-detail.log.1",L"\\native-detail.log"}) {
        std::string text;if(!msiCrashReadBounded(app_data_dir()+suffix,8u<<20,text))continue;
        std::istringstream input(text);std::string line;
        while(std::getline(input,line)) {
            if(line.size()>65536 || line.find("msi-claw-return-debt")==std::string::npos)continue;
            try {const auto row=json::parse(line);const auto pid=row.at("pid").get<DWORD>();
                if(row.value("event",std::string{})=="msi-claw-return-debt" && row.value("action",std::string{})=="discharged" &&
                   (row.value("basis",std::string{})=="xinput-readmitted" || row.value("basis",std::string{})=="readback-verified"))
                    lastDischargeTimes[pid]=(std::max)(lastDischargeTimes[pid],row.at("time").get<std::string>());
            }catch(...){}
        }
    }
    std::string newest;
    for(auto& item:candidates) {
        auto& candidate=item.second;
        const auto locations=historicalLocations.find(std::to_string(item.first)+"|"+hidHideUpper(candidate.rootInstance));
        if(locations!=historicalLocations.end())candidate.locationPaths=locations->second;
        // A later successful admission opens a new episode; old discharge never pays it.
        if(lastDischargeTimes[item.first]>=latestTimes[item.first] &&
           lastDischargeTimes[item.first]>=lastMode2Times[item.first])candidate.modeReturnPending=false;
        if(msiCrashHasDuty(candidate) && msiRecovery::validRecord(candidate) &&
           latestTimes[item.first]>newest && msiCrashOldOwnerDead(candidate)) {
            record=candidate;newest=latestTimes[item.first];
        }
    }
    return !newest.empty();
}
static bool msiCrashRetryableRecordReadError(DWORD error) {
    return error==ERROR_SHARING_VIOLATION || error==ERROR_LOCK_VIOLATION ||
        error==ERROR_BUSY || error==ERROR_RETRY || error==ERROR_NOT_READY ||
        error==ERROR_IO_DEVICE || error==ERROR_DEVICE_NOT_CONNECTED;
}
// Read a bounded ordinary file from one handle. A changing/short read is
// uncertainty, never decoded authority. No writes, legacy migration or device IO.
static DWORD msiCrashReadOwnedRecord(const std::wstring& path,std::string& text) {
    text.clear();
    HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
        nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(file==INVALID_HANDLE_VALUE)return GetLastError();
    struct CloseFile { HANDLE handle; ~CloseFile(){CloseHandle(handle);} } close{file};
    BY_HANDLE_FILE_INFORMATION before{},after{};
    if(!GetFileInformationByHandle(file,&before))return GetLastError();
    if(GetFileType(file)!=FILE_TYPE_DISK ||
       (before.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)) ||
       before.nFileSizeHigh || before.nFileSizeLow>65536)return ERROR_INVALID_DATA;
    std::string bytes(before.nFileSizeLow,'\0');DWORD received=0;
    if(!bytes.empty() && !ReadFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&received,nullptr))return GetLastError();
    if(received!=bytes.size())return ERROR_RETRY;
    if(!GetFileInformationByHandle(file,&after))return GetLastError();
    if(before.nFileSizeHigh!=after.nFileSizeHigh || before.nFileSizeLow!=after.nFileSizeLow ||
       before.ftLastWriteTime.dwHighDateTime!=after.ftLastWriteTime.dwHighDateTime ||
       before.ftLastWriteTime.dwLowDateTime!=after.ftLastWriteTime.dwLowDateTime)return ERROR_RETRY;
    text=std::move(bytes);return ERROR_SUCCESS;
}
static void msiCrashRecordLoadFailed(DWORD error,ULONGLONG now) {
    const bool missingAfterRetry=g_msiCrashReadRetryPending.load(std::memory_order_acquire) &&
        (error==ERROR_FILE_NOT_FOUND || error==ERROR_PATH_NOT_FOUND);
    const bool transient=msiCrashRetryableRecordReadError(error)||missingAfterRetry;
    const auto elapsed=now>=g_msiCrashReadStartedAt?now-g_msiCrashReadStartedAt:0;
    g_msiCrashStoreInvalid=true; // never authorize writes from an unknown record
    const bool retry=transient && g_msiCrashReadAttempts<kMsiCrashReadMaxAttempts && elapsed<kMsiCrashReadMaxMs;
    g_msiCrashLoaded=!retry;
    if(retry) {
        const ULONGLONG delay=1000ULL<<(g_msiCrashReadAttempts-1);
        g_msiCrashReadRetryAt=now+(std::min)(delay,kMsiCrashReadMaxMs-elapsed);
    }
    g_msiCrashReadRetryPending.store(retry,std::memory_order_release);
    msiCrashNote(retry?"record-read-retry-pending":"blocked",
        retry?"transient-owned-record-read;authority-retained-unknown":
        (transient?"record-read-retry-budget-exhausted":"record-read-rejected"));
    appendNativeLifecycleLog("msi-crash-record-read",{{"error",error},{"attempt",g_msiCrashReadAttempts},
        {"maxAttempts",kMsiCrashReadMaxAttempts},{"retryPending",retry},{"elapsedMs",elapsed},
        {"maxMs",kMsiCrashReadMaxMs},{"ownershipProven",false},{"deviceWriteAllowedByRead",false}});
}
static void msiCrashLoadLocked() {
    if(g_msiCrashLoaded)return;
    const auto now=GetTickCount64();
    if(g_msiCrashReadRetryPending.load(std::memory_order_acquire)) {
        if(now<g_msiCrashReadRetryAt)return;
        if(now>=g_msiCrashReadStartedAt && now-g_msiCrashReadStartedAt>=kMsiCrashReadMaxMs) {
            msiCrashRecordLoadFailed(ERROR_RETRY,now);return;
        }
    }
    if(!g_msiCrashReadAttempts)g_msiCrashReadStartedAt=now;
    ++g_msiCrashReadAttempts;
    std::string text;const DWORD error=msiCrashReadOwnedRecord(msiCrashRecoveryPath(),text);
    if(error==ERROR_SUCCESS) {
        const auto decoded=msiRecovery::decodeRecord(text);
        if(decoded.state!=msiRecovery::DecodeState::Valid) {
            g_msiCrashLoaded=true;g_msiCrashStoreInvalid=true;g_msiCrashReadRetryPending=false;
            msiCrashNote("blocked","record-invalid");return;
        }
        if(decoded.record.boardKey!=msiCrashBoardKey()) {
            g_msiCrashLoaded=true;g_msiCrashStoreInvalid=true;g_msiCrashReadRetryPending=false;
            msiCrashNote("blocked","board-mismatch");return;
        }
        g_msiCrashRecord=decoded.record;
        msiCrashNote("loaded","persisted-owned-responsibility");
    } else if((error==ERROR_FILE_NOT_FOUND || error==ERROR_PATH_NOT_FOUND) &&
            !g_msiCrashReadRetryPending.load(std::memory_order_acquire)) {
        msiRecovery::Record legacy;
        if(msiCrashLegacyRecord(legacy)) {
            if(!msiCrashStoreLocked(legacy,"legacy-exact-owned-admission")) {
                g_msiCrashLoaded=true;g_msiCrashStoreInvalid=true;return;
            }
        } else msiCrashNote("no-owned-record","no-proven-legacy-ownership;inherited-hidden-entries-not-claimed");
    } else {msiCrashRecordLoadFailed(error,now);return;}
    g_msiCrashLoaded=true;g_msiCrashStoreInvalid=false;g_msiCrashReadRetryPending=false;
    g_msiCrashInheritedPending.store(msiCrashHasDuty(g_msiCrashRecord),std::memory_order_release);
}
static std::vector<std::string> msiCrashLocations(const std::string& root) {
    std::vector<std::string> result;for(const auto& path:msiForegroundIsolationLocations(root))result.push_back(W2U(path));return result;
}
// Disabled startup never acquires the pre-target virtual-source lock.
// Use the existing reader's current-generation physical snapshot plus exact PnP identity.
static bool msiCrashPhysicalXinputSnapshotEligible(unsigned long long power,unsigned topology) {
    std::lock_guard<std::mutex> lock(g_inputCaptureGamepadSnapshotMx);
    const auto& sample=g_inputCaptureGamepadSnapshot;
    return sample.live && sample.connectedMask!=0 && sample.primarySlot<4 &&
        (sample.connectedMask&(1u<<sample.primarySlot))!=0 &&
        (sample.inputSource=="xinput" || sample.inputSource=="xinput-any-connected") &&
        sample.readIdentityGeneration==g_xinputSlotIdentityEpoch.load(std::memory_order_acquire) &&
        currentPowerGeneration()==power && g_xinputTopologyEpoch.load(std::memory_order_acquire)==topology &&
        ymcc::virtualOutputSourceCurrent({sample.readAttemptSequence,sample.readStartedTick,sample.readCompletedTick,
            sample.readPowerGeneration,sample.readTopologyGeneration,sample.readContextStable},GetTickCount64(),power,topology);
}
static bool msiCrashPrepareOwner(msiRecovery::Record& next,const std::string& root,const std::vector<std::string>& locations) {
    const auto owner=msiCrashCurrentOwner();if(owner.empty())return false;
    if(msiCrashHasDuty(next)) {
        if(next.boardKey!=msiCrashBoardKey() || !msiRecovery::sameDeviceRoot(next,root,locations))return false;
        const auto previous=std::to_string(next.ownerPid)+":"+next.ownerCreationTime;
        if(previous!=owner && !msiCrashOldOwnerDead(next))return false;
    } else {next={};next.boardKey=msiCrashBoardKey();next.rootInstance=root;next.locationPaths=locations;}
    if(next.locationPaths.empty() && !locations.empty())next.locationPaths=locations;
    next.ownerPid=GetCurrentProcessId();next.ownerCreationTime=msiCrashProcessCreation(GetCurrentProcess());return true;
}
static bool msiCrashRecoveryBeforeModeWrite(BYTE mode,const MsiClawHidDiag& diag,bool ownedReturnRetry=false) {
    if(!msiClawKnownBoardAuthority() || ymStartSeamActive())return true;
    const auto capturedPower=currentPowerGeneration();const auto capturedEpoch=inputHostEpochToken();
    const auto capturedReturnEpisode=g_msiDinputReturnDebtEpisode.load();
    const auto capturedModeGeneration=g_msiFeedbackModeGeneration.load();
    const auto capturedTopology=g_xinputTopologyEpoch.load();
    const auto capturedSourceEvent=g_msiOwnedReturnSourceEvent.load();
    std::wstring rootWide;
    if(!deviceInterfaceUsbInstance(U2W(diag.interfacePath).c_str(),rootWide)) {
        msiCrashDecision("mode-write-interface-root-query-failed");return false;
    }
    if(!msiRecovery::validRootInstance(W2U(rootWide))) {
        msiCrashDecision("mode-write-root-identity-invalid");return false;
    }
    const auto root=W2U(rootWide);const auto locations=msiCrashLocations(root);
    if(mode==2 && locations.empty()) {
        msiCrashDecision("mode2-write-location-paths-unproven");return false;
    }
    std::lock_guard<std::mutex> lock(g_msiCrashMx);msiCrashLoadLocked();
    if(g_msiCrashStoreInvalid) {msiCrashNote("blocked","mode-write-durable-store-invalid");return false;}
    auto next=g_msiCrashRecord;
    if(!msiCrashPrepareOwner(next,root,locations)) {msiCrashNote("blocked","mode-root-or-owner-unproven");return false;}
    if(mode==2) {next.modeReturnPending=true;next.modeAttemptOwner.clear();}
    else if(mode==1) {
        // No new request/retry: durable consumption precedes the existing write.
        if(!next.modeReturnPending)return true;
        const auto owner=msiCrashCurrentOwner();
        // Only the serialized owned-return reconciler can request its consumed
        // single retry. PrepareOwner above still proves the same board/root;
        // this cannot claim a different process owner's unfinished operation.
        const bool sameOwnerRetry=ownedReturnRetry && !owner.empty() && next.modeAttemptOwner==owner &&
            next.ownerPid==GetCurrentProcessId() && next.ownerCreationTime==msiCrashProcessCreation(GetCurrentProcess());
        if(!msiRecovery::claimModeAttempt(next,owner) && !sameOwnerRetry) {msiCrashNote("blocked","mode1-already-attempted-by-owner");return false;}
    } else return true;
    if(!msiCrashStoreLocked(next,mode==2?"before-mode2":"before-mode1"))return false;
    // Durable I/O creates a new scheduling boundary: recheck the existing
    // family/epoch/power and terminal ownership facts before the caller writes.
    if(!msiClawKnownBoardAuthority() || !hardwareWriteAllowed() || currentPowerGeneration()!=capturedPower || inputHostEpochToken()!=capturedEpoch ||
       (mode==2 && inputHostExitFenceRaised()) ||
       (ownedReturnRetry && (mode!=1 || g_realStickTest.virtualTargetEnabled || inputHostConfiguredPersona()!="disabled" ||
           g_msiNativeForegroundCyclePending.load() || g_msiNativeForegroundPowerHold.load() || msiForegroundIsolationBorrowBusy() ||
           g_msiDinputReturnDebtEpisode.load()!=capturedReturnEpisode || g_msiFeedbackModeGeneration.load()!=capturedModeGeneration ||
           g_xinputTopologyEpoch.load()!=capturedTopology || g_msiOwnedReturnSourceEvent.load()!=capturedSourceEvent)) ||
       (mode==1 && !msiMode1WritebackVerdict(g_msiDinputReturnOwed.load(),inputHostExitFenceRaised(),
           !g_msiDinputRouteDinputArmed.load(),msiDinputDeviceTargetAbsent(),false,"input-host-stop").allowed)) {
        msiCrashNote("blocked","context-changed-after-durable-mode-claim");
        appendNativeLifecycleLog("msi-owned-return-recovery",{{"action","mode-write-context-rejected"},{"requestedMode",mode},
            {"capturedPower",capturedPower},{"currentPower",currentPowerGeneration()},
            {"capturedHostEpoch",capturedEpoch},{"currentHostEpoch",inputHostEpochToken()},
            {"hardwareWriteAllowed",hardwareWriteAllowed()},{"boardAuthorized",msiClawKnownBoardAuthority()},
            {"routeArmed",g_msiDinputRouteDinputArmed.load()},{"returnOwed",g_msiDinputReturnOwed.load()},
            {"deviceTargetLocallyAbsent",msiDinputDeviceTargetAbsent()},{"exitFence",inputHostExitFenceRaised()},
            {"retryRequested",ownedReturnRetry},{"deviceWriteAttempted",false},
            {"capturedTopology",capturedTopology},{"currentTopology",g_xinputTopologyEpoch.load()},
            {"capturedSourceEvent",capturedSourceEvent},{"currentSourceEvent",g_msiOwnedReturnSourceEvent.load()},
            {"shieldCyclePending",g_msiNativeForegroundCyclePending.load()},
            {"shieldPowerHold",g_msiNativeForegroundPowerHold.load()},{"physicalBorrowBusy",msiForegroundIsolationBorrowBusy()}});
        return false;
    }
    if(mode==2) {g_msiCrashInheritedPending.store(false,std::memory_order_release);g_msiCrashStartupAttempted=false;}
    return true;
}
static bool msiCrashRecoveryBeforeHide(const HidHidePhysicalIdentity& physical,const std::vector<std::string>& targets,
                                     const HidHideVisibilitySnapshot& before,const std::string& transaction) {
    if(!msiClawKnownBoardAuthority() || ymStartSeamActive())return true;
    const auto capturedPower=currentPowerGeneration();const auto capturedEpoch=inputHostEpochToken();
    std::lock_guard<std::mutex> lock(g_msiCrashMx);msiCrashLoadLocked();if(g_msiCrashStoreInvalid || !before.valid)return false;
    auto next=g_msiCrashRecord;if(!msiCrashPrepareOwner(next,physical.baseContainerDeviceInstanceId,msiCrashLocations(physical.baseContainerDeviceInstanceId)))return false;
    std::vector<std::string> additions;
    for(const auto& path:targets)if(!hidHideContainsPath(before.hidden,path))additions.push_back(path);
    if(!msiRecovery::mergeOwnedPaths(next,additions))return false;next.transactionId=transaction;
    if(!msiCrashStoreLocked(next,"before-owned-hide"))return false;
    if(!hardwareWriteAllowed() || currentPowerGeneration()!=capturedPower || inputHostEpochToken()!=capturedEpoch || inputHostExitFenceRaised()) {
        msiCrashNote("blocked","context-changed-after-durable-hide-claim");return false;
    }
    // Carry only recorded ownership, including pre-existing entries from a dead owner.
    for(const auto& path:next.hiddenPaths)if(!hidHideContainsPath(g_hidHideVisibilityJournal.hiddenByUs,path))
        g_hidHideVisibilityJournal.hiddenByUs.push_back(U2W(path));
    return true;
}
static void msiCrashRecoveryAfterOwnedRestore(const HidHideVisibilitySnapshot& after,bool readback) {
    if(!msiClawKnownBoardAuthority() || ymStartSeamActive() || !readback || !after.valid)return;
    std::lock_guard<std::mutex> lock(g_msiCrashMx);if(!g_msiCrashLoaded || g_msiCrashStoreInvalid)return;
    auto next=g_msiCrashRecord;const auto size=next.hiddenPaths.size();
    next.hiddenPaths.erase(std::remove_if(next.hiddenPaths.begin(),next.hiddenPaths.end(),[&](const auto& path){
        return !hidHideContainsPath(after.hidden,path);}),next.hiddenPaths.end());
    if(next.hiddenPaths.size()!=size)(void)msiCrashStoreLocked(next,"owned-hidden-paths-read-back-returned");
}
static bool msiCrashRecoveryAfterModeDischarge(const std::function<bool()>& proofCurrent) {
    if(!msiClawKnownBoardAuthority() || ymStartSeamActive())return false;
    std::lock_guard<std::mutex> lock(g_msiCrashMx);
    if(!g_msiCrashLoaded || g_msiCrashStoreInvalid)return false;
    // Matched owned-return proof may age while this mutex is contended. Its
    // optional MSI validator must run under the durable owner before commit.
    if(proofCurrent && !proofCurrent())return false;
    if(!g_msiCrashRecord.modeReturnPending)return true;
    auto next=g_msiCrashRecord;next.modeReturnPending=false;next.modeAttemptOwner.clear();
    return msiCrashStoreLocked(next,"qualified-XInput-return-not-Steam-consumption-proof");
}
static void msiCrashRecoveryInitialize() {
    if(!msiClawKnownBoardAuthority() || ymStartSeamActive())return;
    std::lock_guard<std::mutex> lock(g_msiCrashMx);msiCrashLoadLocked();
}
// The low-level identity resolver requires the SP_DEVINFO_DATA belonging to
// a live SetupAPI set. A published interface path alone is not that context.
// Resolve only the exact current main source; do not select a same-model peer.
static bool msiCrashResolveDinputIdentity(const HidHideMsiMainSourceRecord& source,HidHidePhysicalIdentity& out) {
    out={};
    if(!source.valid || source.interfacePath.empty() || source.usbInstance.empty() ||
       source.vendorId!=0x0db0 || source.productId!=0x1902) {
        msiCrashDecision("identity-current-DInput-source-invalid");return false;
    }
    const HDEVINFO set=SetupDiGetClassDevsW(&kYeManGuidDeviceInterfaceHid,nullptr,nullptr,
        DIGCF_PRESENT|DIGCF_DEVICEINTERFACE);
    if(set==INVALID_HANDLE_VALUE) {msiCrashDecision("identity-present-set-open-failed");return false;}
    struct CloseSet {HDEVINFO value;~CloseSet(){SetupDiDestroyDeviceInfoList(value);}} close{set};
    bool detailsIncomplete=false;
    for(DWORD index=0;index<512;++index) {
        SP_DEVICE_INTERFACE_DATA iface{};iface.cbSize=sizeof(iface);
        if(!SetupDiEnumDeviceInterfaces(set,nullptr,&kYeManGuidDeviceInterfaceHid,index,&iface)) {
            msiCrashDecision(GetLastError()==ERROR_NO_MORE_ITEMS
                ?(detailsIncomplete?"identity-current-interface-unproven":"identity-current-interface-not-present")
                :"identity-present-enumeration-failed");return false;
        }
        DWORD bytes=0;
        SetupDiGetDeviceInterfaceDetailW(set,&iface,nullptr,0,&bytes,nullptr);
        if(bytes<sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W) || bytes>65536) {detailsIncomplete=true;continue;}
        std::vector<BYTE> buffer(bytes);
        auto* detail=reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buffer.data());
        detail->cbSize=sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        SP_DEVINFO_DATA data{};data.cbSize=sizeof(data);
        if(!SetupDiGetDeviceInterfaceDetailW(set,&iface,detail,bytes,nullptr,&data)) {detailsIncomplete=true;continue;}
        if(ascii_lower(W2U(detail->DevicePath))!=ascii_lower(source.interfacePath))continue;
        if(!hidHideResolveInterfaceIdentity(detail->DevicePath,&data,set,out)) {
            msiCrashDecision("identity-current-interface-resolver-rejected");return false;
        }
        if(!out.resolved || out.vendorId!=source.vendorId || out.productId!=source.productId ||
           ascii_lower(out.sourcePath)!=ascii_lower(source.interfacePath) ||
           hidHideUpper(out.baseContainerDeviceInstanceId)!=hidHideUpper(source.usbInstance)) {
            out={};msiCrashDecision("identity-current-source-binding-mismatch");return false;
        }
        return true;
    }
    msiCrashDecision("identity-present-enumeration-limit");return false;
}
// OFF startup has no pre-target XInput lock, so common endpoint absence can
// remain unproven even after exact owned configuration and mode debt return.
// Close only our reconstructed configuration journal; endpoint/source admission
// for the next target remains the existing start path's responsibility.
// Caller holds both the Host lifecycle and MSI source-pump locks.
static bool msiCrashRetireStartupJournal(const msiRecovery::Record& record) {
    if(g_msiCrashStartupTransactionId.empty())return true;
    const auto contextEligible=[] {
        return msiClawKnownBoardAuthority() && !ymStartSeamActive() && hardwareWriteAllowed() &&
            g_powerLifecycle.load()==PowerLifecycle::Ready && !g_realStickTest.virtualTargetEnabled &&
            inputHostConfiguredPersona()=="disabled" && !g_inputHostProcess && !g_inputHostPrepared &&
            !inputHostExitFenceRaised() && !g_inputHostReleaseQuarantined;
    };
    if(!contextEligible()) {msiCrashDecision("startup-transaction-retirement-context-unready");return false;}
    if(!g_msiCrashStartupAttempted || !msiRecovery::validRecord(record) || msiCrashHasDuty(record) ||
       record.boardKey!=msiCrashBoardKey() || record.transactionId+"#startup"!=g_msiCrashStartupTransactionId) {
        msiCrashDecision("startup-transaction-retirement-duty-or-owner-unproven");return false;
    }
    if(!g_hidHideVisibilityJournal.active) {
        g_msiCrashStartupTransactionId.clear();g_msiCrashStartupHiddenPaths.clear();return true;
    }
    const auto exactStartupJournal=[] {
        const auto& journal=g_hidHideVisibilityJournal;
        return journal.active && journal.transactionId==g_msiCrashStartupTransactionId &&
            !g_msiCrashStartupHiddenPaths.empty() && journal.hiddenByUs==g_msiCrashStartupHiddenPaths &&
            !journal.applicationAdded && journal.ownedApplications.empty() &&
            !journal.ownedApplicationResponsibilityUnknown && !journal.cloakChanged && !journal.before.valid;
    };
    if(!exactStartupJournal()) {msiCrashDecision("startup-transaction-retirement-foreign-responsibility");return false;}
    const auto power=currentPowerGeneration(),epoch=inputHostEpochToken();
    const auto mode=g_msiFeedbackModeGeneration.load();const auto topology=g_xinputTopologyEpoch.load();
    HidHideVisibilitySnapshot after;
    if(!hidHideCaptureVisibilitySnapshot(after) || !after.valid) {
        msiCrashDecision("startup-transaction-retirement-config-readback-unknown");return false;
    }
    for(const auto& path:g_msiCrashStartupHiddenPaths) {
        if(path.empty() || hidHideContainsPath(after.hidden,W2U(path))) {
            msiCrashDecision("startup-transaction-retirement-owned-path-still-hidden-or-invalid");return false;
        }
    }
    // A blocking CLI read is not authority to finish a new lifecycle generation.
    if(!contextEligible() || currentPowerGeneration()!=power || inputHostEpochToken()!=epoch ||
       g_msiFeedbackModeGeneration.load()!=mode || g_xinputTopologyEpoch.load()!=topology || !exactStartupJournal()) {
        msiCrashDecision("startup-transaction-retirement-context-changed");return false;
    }
    std::lock_guard<std::mutex> lock(g_msiCrashMx);
    if(g_msiCrashStoreInvalid || !g_msiCrashLoaded || msiCrashHasDuty(g_msiCrashRecord) ||
       g_msiCrashRecord.boardKey!=record.boardKey || g_msiCrashRecord.transactionId!=record.transactionId ||
       g_msiCrashRecord.rootInstance!=record.rootInstance || g_msiCrashRecord.locationPaths!=record.locationPaths ||
       g_msiCrashRecord.ownerPid!=record.ownerPid || g_msiCrashRecord.ownerCreationTime!=record.ownerCreationTime) {
        msiCrashNote("blocked","startup-transaction-retirement-durable-record-changed");return false;
    }
    appendNativeLifecycleLog("msi-crash-return",{{"phase","startup-transaction-retired"},
        {"transactionId",g_msiCrashStartupTransactionId},{"ownedHiddenPathsChecked",g_msiCrashStartupHiddenPaths.size()},
        {"durableDutyPending",false},{"virtualEndpointAbsenceProven",false},{"steamConsumptionProven",false},
        {"scope","local-startup-configuration-journal-only;next-start-keeps-endpoint-admission"}});
    g_hidHideVisibilityJournal={};
    g_msiCrashStartupTransactionId.clear();g_msiCrashStartupHiddenPaths.clear();return true;
}
// Caller has proved a current same-root/location physical source and holds
// Host + source-pump ownership. Transfer the durable obligation BEFORE any
// command open, so no-device and deferred outcomes still have an observer.
static bool msiCrashAdoptStartupModeDuty(const msiRecovery::Record& expected) {
    if(!msiClawKnownBoardAuthority() || ymStartSeamActive() || !hardwareWriteAllowed() ||
       inputHostExitFenceRaised() || g_realStickTest.virtualTargetEnabled ||
       inputHostConfiguredPersona()!="disabled" || g_inputHostProcess || g_inputHostPrepared ||
       g_msiNativeForegroundCyclePending.load() || g_msiNativeForegroundPowerHold.load() ||
       msiForegroundIsolationBorrowBusy())return false;
    std::lock_guard<std::mutex> lock(g_msiCrashMx);
    const auto& current=g_msiCrashRecord;
    if(!g_msiCrashLoaded || g_msiCrashStoreInvalid || !msiRecovery::validRecord(current) ||
       !current.modeReturnPending || current.boardKey!=expected.boardKey ||
       current.rootInstance!=expected.rootInstance || current.locationPaths!=expected.locationPaths ||
       current.ownerPid!=expected.ownerPid || current.ownerCreationTime!=expected.ownerCreationTime ||
       current.modeAttemptOwner!=expected.modeAttemptOwner || current.transactionId!=expected.transactionId)return false;
    auto next=current;
    if(!msiCrashPrepareOwner(next,expected.rootInstance,expected.locationPaths) ||
       !msiCrashStoreLocked(next,"startup-mode-duty-adopted-before-command"))return false;
    const auto episode=g_msiDinputReturnDebtEpisode.fetch_add(1,std::memory_order_acq_rel)+1;
    g_msiDinputReturnOwed.store(true,std::memory_order_release);
    appendNativeLifecycleLog("msi-owned-return-recovery",{{"action","startup-duty-adopted"},
        {"returnDebtEpisodeId",episode},{"expectedRoot",next.rootInstance},{"expectedLocations",next.locationPaths},
        {"previousOwnerPid",expected.ownerPid},{"currentOwnerPid",GetCurrentProcessId()},
        {"modeWriteIssued",false},{"physicalSignalVerified",false},{"steamConsumptionVerified",false}});
    return true;
}
static void msiCrashRetryOwnedRecordLoad(ULONGLONG now) {
    if(!g_msiCrashReadRetryPending.load(std::memory_order_acquire) ||
       !msiClawKnownBoardAuthority() || ymStartSeamActive() || inputHostExitFenceRaised() ||
       g_powerLifecycle.load(std::memory_order_acquire)!=PowerLifecycle::Ready)return;
    std::unique_lock<std::mutex> lock(g_msiCrashMx,std::try_to_lock);
    if(lock.owns_lock() && g_msiCrashReadRetryPending.load(std::memory_order_acquire) && now>=g_msiCrashReadRetryAt)
        msiCrashLoadLocked();
}
static void msiCrashRecoveryMaintenance(ULONGLONG now) {
    msiCrashRetryOwnedRecordLoad(now);
    if(!g_msiCrashInheritedPending.load(std::memory_order_acquire) || now<g_msiCrashNextTick)return;
    if(g_msiCrashNextTick==0)msiCrashDecision("maintenance-entered");
    g_msiCrashNextTick=now+1000;
    std::unique_lock hostLock(g_inputHostMx,std::try_to_lock);
    if(!hostLock.owns_lock()) {msiCrashDecision("waiting-host-lock");return;}
    if(!hardwareWriteAllowed() || g_realStickTest.virtualTargetEnabled || inputHostConfiguredPersona()!="disabled" || g_inputHostProcess || g_inputHostPrepared ||
       inputHostExitFenceRaised() || g_powerLifecycle.load()!=PowerLifecycle::Ready) {msiCrashDecision("waiting-virtual-off-host-absent-power-ready");return;}
    std::unique_lock<std::mutex> pump(g_msiPersonaPumpMx,std::try_to_lock);
    if(!pump.owns_lock()) {msiCrashDecision("waiting-source-pump");return;}
    msiRecovery::Record record;
    {std::lock_guard<std::mutex> lock(g_msiCrashMx);record=g_msiCrashRecord;}
    if(!msiCrashHasDuty(record)) {
        if(msiCrashRetireStartupJournal(record))g_msiCrashInheritedPending.store(false,std::memory_order_release);
        return;
    }
    if(!g_msiCrashStartupAttempted && !msiCrashOldOwnerDead(record)) {msiCrashDecision("previous-owner-alive-or-unknown");return;}
    if(g_msiCrashStartupAttempted && record.modeReturnPending && record.hiddenPaths.empty()) {
        // The new observer owns signal confirmation and its one retry. Do not
        // let this older startup path clear the durable half ahead of it.
        msiCrashDecision("mode-return-handed-to-owned-signal-observer");return;
    }
    if(g_msiCrashStartupAttempted && g_msiCrashUnhideAttempts>=3 &&
       g_msiCrashSettlementChecks>=kMsiCrashSettlementMaxChecks) {
        msiCrashDecision("settlement-budget-exhausted-duty-retained");return;
    }
    const auto capturedPower=currentPowerGeneration(),capturedEpoch=inputHostEpochToken();
    const auto capturedMode=g_msiFeedbackModeGeneration.load();const auto capturedTopology=g_xinputTopologyEpoch.load();
    HidHideMsiMainSourceRecord source;HidHidePhysicalIdentity physical;
    const bool dinput=msiDinputMainSourceRecordSnapshot(source) && g_msiDinputSourceValid.load() &&
        source.vendorId==0x0db0 && source.productId==0x1902 &&
        g_msiFeedbackSamplePower.load()==currentPowerGeneration() &&
        g_msiFeedbackSampleMode.load()==g_msiFeedbackModeGeneration.load() &&
        g_msiFeedbackSampleTopology.load()==g_xinputTopologyEpoch.load();
    if(dinput) {
        if(!msiCrashResolveDinputIdentity(source,physical))return;
        HidHideMsiMainSourceRecord recheck;
        if(!msiDinputMainSourceRecordSnapshot(recheck) || recheck.generation!=source.generation ||
           recheck.interfacePath!=source.interfacePath || !g_msiDinputSourceValid.load()) {
            msiCrashDecision("context-changed-during-DInput-identity-resolution");return;
        }
    } else {
        // Already back in XInput: return hidden ownership without another mode write.
        std::vector<HidHidePhysicalIdentity> sources;
        if(!msiCrashPhysicalXinputSnapshotEligible(capturedPower,capturedTopology) || !hidHideGetPhysicalIdentities(sources)) {msiCrashDecision("waiting-fresh-matching-physical-source");return;}
        unsigned matched=0;for(const auto& current:sources)if(current.vendorId==0x0db0 && current.productId==0x1901 &&
            msiRecovery::sameDeviceRoot(record,current.baseContainerDeviceInstanceId,msiCrashLocations(current.baseContainerDeviceInstanceId))) {physical=current;++matched;}
        if(matched!=1) {msiCrashDecision("physical-XInput-root-ambiguous-or-mismatched");return;}
    }
    const auto locations=msiCrashLocations(physical.baseContainerDeviceInstanceId);
    if(!msiRecovery::sameDeviceRoot(record,physical.baseContainerDeviceInstanceId,locations)) {msiCrashDecision("root-and-location-ownership-unproven");return;}
    if(record.locationPaths.empty() && !locations.empty()) {
        std::lock_guard<std::mutex> lock(g_msiCrashMx);auto next=g_msiCrashRecord;next.locationPaths=locations;
        if(!msiCrashStoreLocked(next,"current-exact-root-location-captured"))return;record=next;
    }
    // Every blocking identity/property/persistence boundary is followed by the
    // same disabled intent and current source-generation checks, before unhide.
    const bool contextStable=hardwareWriteAllowed() && !g_realStickTest.virtualTargetEnabled &&
        inputHostConfiguredPersona()=="disabled" && !g_inputHostProcess && !g_inputHostPrepared && !inputHostExitFenceRaised() &&
        currentPowerGeneration()==capturedPower && inputHostEpochToken()==capturedEpoch &&
        g_msiFeedbackModeGeneration.load()==capturedMode && g_xinputTopologyEpoch.load()==capturedTopology;
    HidHideMsiMainSourceRecord finalSource;
    if(!contextStable || (!dinput && !msiCrashPhysicalXinputSnapshotEligible(capturedPower,capturedTopology)) || (dinput && (!msiDinputMainSourceRecordSnapshot(finalSource) ||
        finalSource.generation!=source.generation || finalSource.interfacePath!=source.interfacePath ||
        !g_msiDinputSourceValid.load() || g_msiFeedbackSamplePower.load()!=capturedPower ||
        g_msiFeedbackSampleMode.load()!=capturedMode || g_msiFeedbackSampleTopology.load()!=capturedTopology))) {
        msiCrashDecision("context-changed-during-startup-source-query");return;
    }
    if(g_msiCrashStartupAttempted && (record.hiddenPaths.empty() || g_msiCrashUnhideAttempts>=3)) {
        // No further driver mutation. Once the configuration is already back,
        // retry only observation and durable close, with an independent budget.
        if(g_msiCrashSettlementChecks>=kMsiCrashSettlementMaxChecks) {
            msiCrashDecision("settlement-budget-exhausted-duty-retained");return;
        }
        ++g_msiCrashSettlementChecks;
        if(!record.hiddenPaths.empty()) {
            HidHideVisibilitySnapshot after;const bool readback=hidHideCaptureVisibilitySnapshot(after);
            msiCrashRecoveryAfterOwnedRestore(after,readback);
        }
        msiCrashDecision("configuration-readback;mode-confirmation-owned-by-signal-observer");return;
    }
    // Restore exact owned entries. No cloak-off, sweep, service or OEM policy changes.
    if(!g_hidHideVisibilityJournal.active && !record.hiddenPaths.empty()) {
        g_hidHideVisibilityJournal={};g_hidHideVisibilityJournal.active=true;
        g_hidHideVisibilityJournal.transactionId=record.transactionId+"#startup";
        g_hidHideVisibilityJournal.physical=physical;g_hidHideVisibilityJournal.physicalSources={physical};
        for(const auto& path:record.hiddenPaths)g_hidHideVisibilityJournal.hiddenByUs.push_back(U2W(path));
        g_msiCrashStartupTransactionId=g_hidHideVisibilityJournal.transactionId;
        g_msiCrashStartupHiddenPaths=g_hidHideVisibilityJournal.hiddenByUs;
    }
    const bool firstAttempt=!g_msiCrashStartupAttempted;
    if(firstAttempt && record.modeReturnPending && !msiCrashAdoptStartupModeDuty(record)) {
        msiCrashDecision("startup-mode-duty-adoption-unproven");return;
    }
    g_msiCrashStartupAttempted=true;
    {std::lock_guard<std::mutex> lock(g_msiCrashMx);msiCrashNote("startup-recovery",dinput?"old-owner-dead-and-current-DInput-root-confirmed":"old-owner-dead-and-current-XInput-root-confirmed");}
    // Adopting the owner adds a blocking durable boundary AFTER the earlier
    // source proof. Recheck it before any driver mutation, not only mode I/O.
    HidHideMsiMainSourceRecord afterAdoptionSource;
    const bool unhideContextStable=hardwareWriteAllowed() && !inputHostExitFenceRaised() &&
        !g_realStickTest.virtualTargetEnabled && inputHostConfiguredPersona()=="disabled" &&
        !g_inputHostProcess && !g_inputHostPrepared && g_powerLifecycle.load()==PowerLifecycle::Ready &&
        currentPowerGeneration()==capturedPower && inputHostEpochToken()==capturedEpoch &&
        g_msiFeedbackModeGeneration.load()==capturedMode && g_xinputTopologyEpoch.load()==capturedTopology &&
        !g_msiNativeForegroundCyclePending.load() && !g_msiNativeForegroundPowerHold.load() &&
        !msiForegroundIsolationBorrowBusy() &&
        (dinput ? (msiDinputMainSourceRecordSnapshot(afterAdoptionSource) && g_msiDinputSourceValid.load() &&
            afterAdoptionSource.generation==source.generation && afterAdoptionSource.interfacePath==source.interfacePath &&
            g_msiFeedbackSamplePower.load()==capturedPower && g_msiFeedbackSampleMode.load()==capturedMode &&
            g_msiFeedbackSampleTopology.load()==capturedTopology) :
            msiCrashPhysicalXinputSnapshotEligible(capturedPower,capturedTopology));
    if(!unhideContextStable)msiCrashDecision("context-changed-after-startup-duty-adoption-no-driver-mutation");
    if(unhideContextStable && g_hidHideVisibilityJournal.active && !record.hiddenPaths.empty() && g_msiCrashUnhideAttempts<3) {
        ++g_msiCrashUnhideAttempts;(void)hidHideRestorePhysicalTransaction("msi-startup-owned-return");
    }
    if(firstAttempt && record.modeReturnPending) {
        msiDinputReleasePad("input-host-stop");msiDinputClearOwnedState("input-host-stop");
        g_msiDinputRouteDinputArmed.store(false);g_msiDinputRouteConfirmed.store(false);
        g_msiDinputReturnOwed.store(true);g_msiMode1TerminalAttempted.store(false);
        // An already returned XInput source needs two fresh signal windows,
        // with no initial mode command. A changed context also starts only
        // observation; the reconciler revalidates all write gates for a retry.
        const bool dispatch=unhideContextStable && dinput && hardwareWriteAllowed() && !inputHostExitFenceRaised() &&
            !g_realStickTest.virtualTargetEnabled && inputHostConfiguredPersona()=="disabled" &&
            currentPowerGeneration()==capturedPower && inputHostEpochToken()==capturedEpoch &&
            g_msiFeedbackModeGeneration.load()==capturedMode && g_xinputTopologyEpoch.load()==capturedTopology &&
            !g_msiNativeForegroundCyclePending.load() && !g_msiNativeForegroundPowerHold.load() &&
            !msiForegroundIsolationBorrowBusy();
        if(dispatch)(void)msiClawRestoreXInputMode("input-host-stop");
        else msiOwnedReturnRecordInitial("input-host-stop",MsiClawCommandOutcome{});
    }
}

// Read-only companion to shared OFF cleanup evidence. No recovery, record
// write, registry/PnP read, mode command or journal retirement is performed.
static json msiCrashCleanupEvidenceLocked() noexcept {
    if(!g_inputHostMx.ownedByCurrentThread())return nullptr;
    try {
        if(!msiClawKnownBoardAuthority())return {{"applicable",false}};
        json result={{"applicable",true},{"snapshotAvailable",false},
            {"startupAttempted",g_msiCrashStartupAttempted},
            {"startupTransactionPresent",!g_msiCrashStartupTransactionId.empty()},
            {"startupHiddenPathCount",g_msiCrashStartupHiddenPaths.size()},
            {"scope","cached-native-return-record;not-physical-return-proof"}};
        std::unique_lock<std::mutex> lock(g_msiCrashMx,std::try_to_lock);
        if(!lock.owns_lock())return result;
        result.update({{"snapshotAvailable",true},{"loaded",g_msiCrashLoaded},
            {"invalid",g_msiCrashStoreInvalid},{"modeReturnPending",g_msiCrashRecord.modeReturnPending},
            {"ownedHiddenPathCount",g_msiCrashRecord.hiddenPaths.size()},
            {"recordOwnerPid",g_msiCrashRecord.ownerPid},
            {"recordOwnerCreationTime",g_msiCrashRecord.ownerCreationTime},
            {"recordTransactionId",g_msiCrashRecord.transactionId.substr(0,128)}});
        return result;
    }catch(...){return nullptr;}
}
