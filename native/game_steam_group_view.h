#pragma once
#include "steam_game_tracker_observation.h"
#include "steam_game_process_facts.h"

// Read-only consumer projection of the sole Steam owner's cached observation.
// No process lookup, log parsing, role inference or game selection happens here.
namespace GameSteamContext {
using json=ymcc::sleepsteam::Json;
static json unknownGroup(const char* reason,const char* status="unknown") {
    return {{"status",status},{"reason",reason},{"scope","process-lifetime"},
        {"processValidated",false},{"currentSource",false},{"roleValidated",false}};
}
static bool groupDecimal(const std::string& raw,std::uint64_t expected) {
    std::uint64_t actual=0;
    return raw.size()<=20&&ymcc::sleepsteam::unsignedDecimal(raw,actual)&&actual==expected;
}
static bool groupPathText(const std::string& path) {
    if(path.empty()||path.size()>131068||path.find('\0')!=path.npos)return false;
    // Encoding conversion only; never a process/file/registry lookup. Reject
    // invalid UTF-8 before it could break serialization of the known game.
    const auto count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path.data(),static_cast<int>(path.size()),nullptr,0);
    return count>0&&count<=32767;
}
static json cachedGroupView(const ymcc::steamgamefacts::TrackerSnapshot& association) {
    using namespace ymcc::steamgamefacts;
    const auto& value=association.groupObservation;
    if(!value)return unknownGroup("group-not-observed");
    if(value->status!=TrackerGroupStatus::Observed)
        return unknownGroup(value->status==TrackerGroupStatus::Incomplete?"group-source-incomplete":"group-source-unknown",
            value->status==TrackerGroupStatus::Incomplete?"incomplete":"unknown");
    const auto& game=association.key.observation.selectedGame;
    const auto kind=value->gameIdKind;
    const bool native=kind==TrackerGameIdKind::NativeApp;
    if(value->scope!=TrackerLogScope::ProcessLifetime||!value->sourceRevision||
        value->sourceRevision!=association.observation.sourceRevision||
        !value->fullGameIdKnown||!value->selectedRowObserved||value->relatedFullGameIdUnknown||value->pidConflict||
        value->selectedPid!=game.pid||value->appid!=association.trackerAppId||value->fullGameId!=association.fullGameId||
        value->fullGameIdDecimal!=association.fullGameIdDecimal||!exactGameId(value->appid,value->fullGameId)||
        !groupDecimal(value->gameIdRaw,value->fullGameId)||
        (kind!=TrackerGameIdKind::NativeApp&&kind!=TrackerGameIdKind::ShortcutType2)||
        native!=(value->fullGameId==value->appid)||
        !value->selectedSourceLine||value->selectedSourceLine!=association.sourceLine||
        value->members.empty()||value->members.size()>32)
        return unknownGroup("group-association-mismatch","incomplete");
    unsigned selectedRows=0;
    json members=json::array();
    for(const auto& member:value->members){
        if(!member.pid||!member.sourceLine||!groupPathText(member.path)||
            member.at.size()!=19||member.at.find('\0')!=member.at.npos||member.identityConflict||!member.fullGameIdKnown||
            member.appid!=value->appid||member.fullGameId!=value->fullGameId||member.gameIdKind!=kind||
            !groupDecimal(member.gameIdRaw,value->fullGameId))return unknownGroup("group-member-unconfirmed","incomplete");
        unsigned copies=0;for(const auto& other:value->members)if(other.pid==member.pid)++copies;
        if(copies!=1)return unknownGroup("group-pid-conflict","incomplete");
        if(member.pid==game.pid){
            if(member.sourceLine!=value->selectedSourceLine||member.at!=association.trackerAtLocalSecond||
                member.gameIdRaw!=value->gameIdRaw||
                ymcc::sleepsteam::pathKey(member.path)!=association.key.observation.canonicalGamePath)
                return unknownGroup("group-selected-row-mismatch","incomplete");
            ++selectedRows;
        }
        members.push_back({{"pid",member.pid},{"path",member.path},{"observedAt",member.at},
            {"sourceLine",member.sourceLine},{"gameIdRaw",member.gameIdRaw}});
    }
    if(selectedRows!=1)return unknownGroup("group-selected-row-missing","incomplete");
    return {{"status","observed"},{"reason","historical-gameprocess-group"},{"scope","process-lifetime"},
        {"processValidated",false},{"currentSource",false},{"roleValidated",false},
        {"sourceRevision",std::to_string(value->sourceRevision)},
        {"selectedPid",game.pid},{"selectedSourceLine",value->selectedSourceLine},
        {"appid",value->appid},{"gameId",value->fullGameIdDecimal},{"gameIdRaw",value->gameIdRaw},
        {"gameIdKind",native?"native-app":"shortcut-type2"},{"members",std::move(members)}};
}
}
