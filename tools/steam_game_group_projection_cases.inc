// Pure cases of the existing tracker-source-projections consumer. These are
// historical log groups only, never native parent/child roles or action proof.
static bool trackerGameGroupMetadataEqual(const ymcc::steamgamefacts::TrackerGroupObservation& a,
    const ymcc::steamgamefacts::TrackerGroupObservation& b){
    if(a.sourceRevision!=b.sourceRevision||a.scope!=b.scope||a.status!=b.status||a.reason!=b.reason||a.appid!=b.appid||
        a.selectedPid!=b.selectedPid||a.fullGameId!=b.fullGameId||a.fullGameIdKnown!=b.fullGameIdKnown||
        a.fullGameIdDecimal!=b.fullGameIdDecimal||a.gameIdRaw!=b.gameIdRaw||
        a.selectedRowObserved!=b.selectedRowObserved||a.relatedFullGameIdUnknown!=b.relatedFullGameIdUnknown||
        a.pidConflict!=b.pidConflict||a.gameIdKind!=b.gameIdKind||a.selectedSourceLine!=b.selectedSourceLine||a.members.size()!=b.members.size())return false;
    for(std::size_t i=0;i<a.members.size();++i){const auto& x=a.members[i];const auto& y=b.members[i];
        if(x.appid!=y.appid||x.pid!=y.pid||x.gameIdRaw!=y.gameIdRaw||x.fullGameId!=y.fullGameId||x.fullGameIdKnown!=y.fullGameIdKnown||
            x.path!=y.path||x.at!=y.at||x.sourceLine!=y.sourceLine||x.identityConflict!=y.identityConflict||x.gameIdKind!=y.gameIdKind)return false;
    }
    return true;
}
template<class Check> static void trackerGameGroupProjectionCases(Check check){
    using namespace ymcc::steamgamefacts;
    const std::string at="2026-11-13 10:00:00",selectedAt="2026-11-13 10:00:01";
    const std::string earliest="2026-11-13 09:59:58",latest="2026-11-13 10:00:10";
    const std::string path="D:\\Game\\selected.exe",otherPath="D:\\Game\\launcher.exe";
    const std::uint32_t app=2399932135U,selectedPid=456,otherPid=123;
    const auto full=(std::uint64_t(app)<<32)|0x02000000ULL;
    const auto differentFull=(std::uint64_t(app-1)<<32)|0x02000000ULL;
    const auto raw=std::to_string(full);
    const auto add=[](const std::string& id,unsigned pid,const std::string& exe,const std::string& time){
        return "["+time+"] AppID "+id+" adding PID "+std::to_string(pid)+" as a tracked process \""+exe+"\"";
    };
    const auto drop=[&](unsigned pid){return "[2026-11-13 10:00:04] AppID "+raw+" no longer tracking PID "+std::to_string(pid)+", exit code 0";};
    const std::vector<std::string> original={add(raw,otherPid,otherPath,at),add(raw,selectedPid,path,selectedAt)};
    const auto parse=[&](std::uint64_t revision,const std::vector<std::string>& lines){
        return tracker_detail::projectSource(revision,lines,selectedPid,path,earliest,latest);
    };
    const auto observed=[](const TrackerGroupObservation& group){return group.status==TrackerGroupStatus::Observed&&group.reason=="steam-tracker-group-observed";};
    const auto incomplete=[](const TrackerGroupObservation& group,const char* reason){return group.status==TrackerGroupStatus::Incomplete&&group.reason==reason;};
    const auto source=parse(101,original);
    const auto& life=source->processLifetime;const auto& running=source->runningList;
    const auto& group=life.group;
    check("group-type2-two-members-never-reselect-first-pid",observed(group)&&observed(running.group)&&group.members.size()==2&&
        group.selectedPid==selectedPid&&group.members.front().pid==otherPid&&group.selectedSourceLine==2&&
        group.appid==app&&group.fullGameIdKnown&&group.fullGameId==full&&group.fullGameIdDecimal==raw&&group.gameIdRaw==raw&&group.gameIdKind==TrackerGameIdKind::ShortcutType2&&
        life.binding.sourceLine==2&&life.binding.gameIdRaw==raw);
    check("group-member-path-time-line-and-raw-id-are-exact",group.members.size()==2&&
        group.members[0].pid==otherPid&&group.members[0].path==otherPath&&group.members[0].at==at&&group.members[0].sourceLine==1&&
        group.members[0].gameIdRaw==raw&&group.members[0].fullGameId==full&&group.members[0].fullGameIdKnown&&
        group.members[1].pid==selectedPid&&group.members[1].path==path&&group.members[1].at==selectedAt&&group.members[1].sourceLine==2&&
        group.members[1].gameIdRaw==raw&&group.members[1].fullGameId==full&&group.members[1].fullGameIdKnown&&
        group.members[0].gameIdKind==TrackerGameIdKind::ShortcutType2&&group.members[1].gameIdKind==TrackerGameIdKind::ShortcutType2&&
        !group.members[0].identityConflict&&!group.members[1].identityConflict);
    check("group-dual-scopes-share-only-the-same-source-revision",group.sourceRevision==101&&running.group.sourceRevision==101&&
        group.scope==TrackerLogScope::ProcessLifetime&&running.group.scope==TrackerLogScope::RunningListAndProcess&&
        life.inventory.sourceRevision==group.sourceRevision&&life.binding.sourceRevision==group.sourceRevision&&
        running.inventory.sourceRevision==running.group.sourceRevision&&running.binding.sourceRevision==running.group.sourceRevision);
    const auto native=parse(102,{add("730",otherPid,otherPath,at),add("730",selectedPid,path,selectedAt)});
    check("group-native-730-keeps-a-separate-known-kind",observed(native->processLifetime.group)&&observed(native->runningList.group)&&
        native->processLifetime.group.gameIdKind==TrackerGameIdKind::NativeApp&&native->processLifetime.group.appid==730&&
        native->processLifetime.group.fullGameId==730&&native->processLifetime.group.fullGameIdDecimal=="730"&&native->processLifetime.group.gameIdRaw=="730"&&native->processLifetime.group.members.size()==2&&
        native->processLifetime.group.members[0].gameIdRaw=="730"&&native->processLifetime.group.members[1].gameIdKind==TrackerGameIdKind::NativeApp);
    const auto shortId=parse(103,{add(std::to_string(app),selectedPid,path,selectedAt)});
    check("group-short-id-is-never-expanded-to-full-type2",shortId->processLifetime.group.status==TrackerGroupStatus::Unknown&&
        shortId->runningList.group.status==TrackerGroupStatus::Unknown&&!shortId->processLifetime.group.fullGameIdKnown&&shortId->processLifetime.group.fullGameIdDecimal.empty()&&
        shortId->processLifetime.group.gameIdKind==TrackerGameIdKind::Unknown&&shortId->processLifetime.group.members.empty()&&
        shortId->processLifetime.inventory.members[0].gameIdRaw==std::to_string(app)&&!shortId->processLifetime.inventory.members[0].fullGameIdKnown);
    const auto signedRaw=std::to_string(static_cast<std::int64_t>(app)-0x100000000LL);
    const auto signedId=parse(104,{add(signedRaw,selectedPid,path,selectedAt)});
    check("group-signed-id-is-never-expanded-to-full-type2",signedId->processLifetime.binding.appid==app&&
        signedId->processLifetime.group.status==TrackerGroupStatus::Unknown&&signedId->runningList.group.status==TrackerGroupStatus::Unknown&&
        !signedId->processLifetime.group.fullGameIdKnown&&signedId->processLifetime.group.fullGameIdDecimal.empty()&&signedId->processLifetime.group.members.empty()&&
        signedId->processLifetime.inventory.members[0].gameIdRaw==signedRaw&&!signedId->processLifetime.inventory.members[0].fullGameIdKnown);
    auto relatedLines=original;relatedLines.push_back(add(std::to_string(app),789,"D:\\Game\\unknown.exe",at));
    const auto related=parse(105,relatedLines);
    check("group-related-normalized-short-member-remains-incomplete-not-merged",incomplete(related->processLifetime.group,"steam-tracker-group-related-gameid-unknown")&&
        incomplete(related->runningList.group,"steam-tracker-group-related-gameid-unknown")&&related->processLifetime.group.relatedFullGameIdUnknown&&
        related->processLifetime.group.members.size()==2&&related->processLifetime.inventory.members.size()==3&&
        std::none_of(related->processLifetime.group.members.begin(),related->processLifetime.group.members.end(),[](const auto& row){return row.pid==789;}));
    auto duplicateLines=original;duplicateLines.push_back(add(std::to_string(differentFull),selectedPid,path,selectedAt));
    const auto duplicate=parse(106,duplicateLines);
    // Contract check: an ambiguous selected binding must retain Incomplete
    // group metadata instead of silently skipping the PID conflict scan.
    check("group-selected-pid-under-another-full-id-is-incomplete",duplicate->processLifetime.binding.status=="ambiguous"&&
        duplicate->runningList.binding.status=="ambiguous"&&duplicate->processLifetime.group.status==TrackerGroupStatus::Incomplete&&
        duplicate->runningList.group.status==TrackerGroupStatus::Incomplete);
    auto conflictLines=original;
    conflictLines.push_back(add(raw,selectedPid,"D:\\Other\\selected.exe","2026-11-13 10:00:02"));
    conflictLines.push_back(add(raw,selectedPid,path,"2026-11-13 10:00:03"));
    const auto conflict=parse(107,conflictLines);
    check("group-same-key-path-conflict-stays-sticky-after-matching-add",incomplete(conflict->processLifetime.group,"steam-tracker-group-pid-conflict")&&
        incomplete(conflict->runningList.group,"steam-tracker-group-pid-conflict")&&conflict->processLifetime.group.pidConflict&&
        conflict->processLifetime.inventory.members[1].identityConflict&&conflict->processLifetime.inventory.members[1].sourceLine==4&&
        conflict->processLifetime.inventory.members[1].path==path);
    auto dropRebuildLines=conflictLines;dropRebuildLines.push_back(drop(selectedPid));
    dropRebuildLines.push_back(add(raw,selectedPid,path,"2026-11-13 10:00:05"));
    const auto dropRebuild=parse(108,dropRebuildLines);
    check("group-drop-starts-new-lifecycle-with-new-exact-row-metadata",observed(dropRebuild->processLifetime.group)&&observed(dropRebuild->runningList.group)&&
        dropRebuild->processLifetime.group.selectedPid==selectedPid&&dropRebuild->processLifetime.group.selectedSourceLine==6&&
        dropRebuild->processLifetime.group.members.size()==2&&!dropRebuild->processLifetime.group.members[1].identityConflict&&
        dropRebuild->processLifetime.group.members[1].at=="2026-11-13 10:00:05"&&dropRebuild->processLifetime.group.members[1].sourceLine==6&&
        dropRebuild->processLifetime.group.members[1].path==path&&dropRebuild->processLifetime.group.members[1].gameIdRaw==raw&&
        conflict->processLifetime.group.pidConflict&&conflict->sourceRevision==107);
    auto resetLines=conflictLines;resetLines.push_back("[2026-11-13 10:00:04] Client version: group-new-lifecycle");
    resetLines.push_back(add(raw,selectedPid,path,"2026-11-13 10:00:05"));
    resetLines.push_back(add(raw,otherPid,otherPath,"2026-11-13 10:00:06"));
    const auto reset=parse(109,resetLines);
    check("group-client-reset-rebuilds-all-member-row-metadata",observed(reset->processLifetime.group)&&observed(reset->runningList.group)&&
        reset->processLifetime.group.members.size()==2&&reset->processLifetime.group.selectedSourceLine==6&&
        reset->processLifetime.group.members[0].sourceLine==7&&reset->processLifetime.group.members[0].at=="2026-11-13 10:00:06"&&
        reset->processLifetime.group.members[1].sourceLine==6&&reset->processLifetime.group.members[1].at=="2026-11-13 10:00:05"&&
        !reset->processLifetime.group.members[0].identityConflict&&!reset->processLifetime.group.members[1].identityConflict);
    auto removedLines=original;removedLines.push_back("[2026-11-13 10:00:03] Remove "+raw+" from running list");
    const auto removed=parse(110,removedLines);
    check("group-running-remove-does-not-destroy-process-lifetime-observation",removed->runningList.group.status==TrackerGroupStatus::Unknown&&
        removed->runningList.group.members.empty()&&observed(removed->processLifetime.group)&&removed->processLifetime.group.members.size()==2&&
        removed->processLifetime.group.selectedPid==selectedPid&&removed->processLifetime.group.sourceRevision==110&&removed->runningList.group.sourceRevision==110);
    auto droppedLines=original;droppedLines.push_back(drop(selectedPid));const auto dropped=parse(111,droppedLines);
    check("group-selected-drop-never-substitutes-surviving-helper-in-either-scope",dropped->processLifetime.group.status==TrackerGroupStatus::Unknown&&
        dropped->runningList.group.status==TrackerGroupStatus::Unknown&&!dropped->processLifetime.group.selectedRowObserved&&
        !dropped->runningList.group.selectedRowObserved&&dropped->processLifetime.group.selectedPid==selectedPid&&
        dropped->runningList.group.selectedPid==selectedPid&&dropped->processLifetime.group.members.empty()&&dropped->runningList.group.members.empty()&&
        dropped->processLifetime.inventory.members.size()==1&&dropped->processLifetime.inventory.members[0].pid==otherPid);
    auto changedInventory=life.inventory;++changedInventory.sourceRevision;
    check("group-inventory-source-revision-mismatch-is-rejected",incomplete(tracker_detail::projectGroup(changedInventory,life.binding,selectedPid,path),"steam-tracker-group-source-mismatch"));
    auto changedBinding=life.binding;++changedBinding.sourceRevision;
    check("group-binding-source-revision-mismatch-is-rejected",incomplete(tracker_detail::projectGroup(life.inventory,changedBinding,selectedPid,path),"steam-tracker-group-source-mismatch"));
    changedBinding=life.binding;changedBinding.scope=TrackerLogScope::RunningListAndProcess;
    check("group-binding-scope-mismatch-is-rejected",incomplete(tracker_detail::projectGroup(life.inventory,changedBinding,selectedPid,path),"steam-tracker-group-source-mismatch"));
    check("group-selected-exact-path-cannot-be-replaced",incomplete(tracker_detail::projectGroup(life.inventory,life.binding,selectedPid,"D:\\Other\\selected.exe"),"steam-tracker-group-selected-row-missing"));
    changedBinding=life.binding;++changedBinding.sourceLine;
    check("group-selected-source-line-must-match-exact-inventory-row",incomplete(tracker_detail::projectGroup(life.inventory,changedBinding,selectedPid,path),"steam-tracker-group-selected-row-missing"));
    changedBinding=life.binding;changedBinding.gameIdRaw="0"+raw;
    check("group-selected-raw-gameid-must-match-even-at-equal-numeric-value",incomplete(tracker_detail::projectGroup(life.inventory,changedBinding,selectedPid,path),"steam-tracker-group-selected-row-missing"));
    changedBinding=life.binding;changedBinding.at=at;
    check("group-selected-row-time-cannot-be-substituted",incomplete(tracker_detail::projectGroup(life.inventory,changedBinding,selectedPid,path),"steam-tracker-group-selected-row-missing"));
    changedInventory=life.inventory;changedInventory.members.erase(changedInventory.members.begin()+1);
    const auto missing=tracker_detail::projectGroup(changedInventory,life.binding,selectedPid,path);
    check("group-selected-row-absent-does-not-elect-another-member",incomplete(missing,"steam-tracker-group-selected-row-missing")&&
        missing.selectedPid==selectedPid&&!missing.selectedRowObserved&&missing.members.size()==1&&missing.members[0].pid==otherPid);
    const auto noSelected=tracker_detail::projectSource(112,original,0,path,earliest,latest);
    const auto unlisted=tracker_detail::projectSource(113,original,999,path,earliest,latest);
    check("group-no-selected-binding-does-not-elect-an-inventory-pid",noSelected->processLifetime.group.status==TrackerGroupStatus::Unknown&&
        noSelected->runningList.group.status==TrackerGroupStatus::Unknown&&noSelected->processLifetime.group.selectedPid==0&&
        noSelected->processLifetime.group.members.empty()&&unlisted->processLifetime.group.status==TrackerGroupStatus::Unknown&&
        unlisted->processLifetime.group.selectedPid==999&&unlisted->processLifetime.group.members.empty()&&unlisted->processLifetime.inventory.members.size()==2);
    std::vector<std::string> bounded={add(raw,selectedPid,path,selectedAt)};
    for(unsigned i=0;i<31;++i)bounded.push_back(add(raw,1000+i,"D:\\Game\\member"+std::to_string(i)+".exe",at));
    const auto thirtyTwo=parse(114,bounded);
    check("group-original-32-member-boundary-is-observed",observed(thirtyTwo->processLifetime.group)&&observed(thirtyTwo->runningList.group)&&
        thirtyTwo->processLifetime.group.members.size()==32&&thirtyTwo->runningList.group.members.size()==32&&
        thirtyTwo->processLifetime.inventory.complete&&thirtyTwo->runningList.inventory.complete&&thirtyTwo->processLifetime.group.selectedPid==selectedPid);
    bounded.push_back(add(raw,1031,"D:\\Game\\member31.exe",at));const auto thirtyThree=parse(115,bounded);
    check("group-original-33-member-overflow-remains-incomplete",incomplete(thirtyThree->processLifetime.group,"steam-tracker-live-process-limit")&&
        incomplete(thirtyThree->runningList.group,"steam-tracker-live-process-limit")&&!thirtyThree->processLifetime.inventory.complete&&
        !thirtyThree->runningList.inventory.complete&&thirtyThree->processLifetime.group.members.empty());
    auto differentLines=original;differentLines.push_back(add(std::to_string(differentFull),789,"D:\\Game\\other-game.exe",at));
    const auto different=parse(116,differentLines);
    check("group-different-complete-gameids-never-mix-members",observed(different->processLifetime.group)&&observed(different->runningList.group)&&
        different->processLifetime.inventory.members.size()==3&&different->processLifetime.group.members.size()==2&&
        std::all_of(different->processLifetime.group.members.begin(),different->processLifetime.group.members.end(),[&](const auto& row){return row.fullGameIdKnown&&row.fullGameId==full&&row.appid==app;}));
}
